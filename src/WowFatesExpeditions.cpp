/*
 * This file is part of mod-wow-fates.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
 * Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "WowFatesExpeditions.h"
#include "WowFates.h"
#include "WowFatesSync.h"

#include "Chat.h"
#include "Containers.h"
#include "Creature.h"
#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "GameObject.h"
#include "Item.h"
#include "Log.h"
#include "Mail.h"
#include "Map.h"
#include "MapMgr.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Random.h"
#include "StringFormat.h"
#include "TemporarySummon.h"
#include "World.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include <algorithm>
#include <cmath>

namespace
{
    // How long a monster of an Expedition stays dead, to be looted.
    constexpr uint32 SUMMON_CORPSE_MS = 60000;

    bool IsFromPlayer(Unit const* unit)
    {
        return unit && unit->GetCharmerOrOwnerPlayerOrPlayerItself();
    }

    // The client's addon channel: a whisper of the player to himself in the
    // addon language, "WowFates<TAB>message" (CHAT_MSG_ADDON, prefix WowFates).
    constexpr char const* ADDON_PREFIX = "WowFates";

    void SendAddon(Player* player, std::string const& message)
    {
        std::string const text = std::string(ADDON_PREFIX) + "\t" + message;
        WorldPacket data(SMSG_MESSAGECHAT, 30 + text.size());
        data << uint8(CHAT_MSG_WHISPER);
        data << int32(LANG_ADDON);
        data << player->GetGUID();
        data << uint32(0);
        data << player->GetGUID();
        data << uint32(text.length() + 1);
        data << text;
        data << uint8(0);
        player->GetSession()->SendPacket(&data);
    }

    // A tab or a line break would cut the message.
    std::string Clean(std::string text)
    {
        std::replace(text.begin(), text.end(), '\t', ' ');
        std::replace(text.begin(), text.end(), '\n', ' ');
        return text;
    }
}

WowFatesExpeditionMgr* WowFatesExpeditionMgr::instance()
{
    static WowFatesExpeditionMgr instance;
    return &instance;
}

void WowFatesExpeditionMgr::LoadFromDB()
{
    std::lock_guard<std::recursive_mutex> guard(_lock);

    // The running Expeditions point into the definitions: they end first.
    StopAll();

    _defs.clear();
    _spawns.clear();
    _rewards.clear();
    _cooldowns.clear();

    if (QueryResult result = WorldDatabase.Query(
        "SELECT `id`, `name`, `level`, `objective_type`, `objective_entry`, `objective_count`, "
        "`summon_entry`, `summon_count`, `summon_respawn`, `radius`, `duration`, `cooldown`, `weight` "
        "FROM `mod_wow_fates_expedition` WHERE `enabled` = 1"))
    {
        do
        {
            Field* f = result->Fetch();
            WowFatesExpeditionDef def;
            def.id = f[0].Get<uint32>();
            def.name = f[1].Get<std::string>();
            def.level = std::max<uint8>(f[2].Get<uint8>(), 1);
            def.objectiveType = f[3].Get<uint8>();
            def.objectiveEntry = f[4].Get<uint32>();
            def.objectiveCount = std::max<uint32>(f[5].Get<uint32>(), 1);
            def.summonEntry = f[6].Get<uint32>();
            def.summonCount = f[7].Get<uint32>();
            def.summonRespawn = f[8].Get<uint32>();
            def.radius = std::max(f[9].Get<float>(), 5.0f);
            def.duration = std::max<uint32>(f[10].Get<uint32>(), 60);
            def.cooldown = f[11].Get<uint32>();
            def.weight = f[12].Get<uint32>();

            if (def.objectiveType < WOW_FATES_OBJECTIVE_KILL || def.objectiveType > WOW_FATES_OBJECTIVE_GATHER)
            {
                LOG_ERROR("module", "mod-wow-fates: Expedition {} has objective type {}, which is not handled yet; skipped.", def.id, def.objectiveType);
                continue;
            }
            if (def.objectiveEntry && !sObjectMgr->GetCreatureTemplate(def.objectiveEntry))
            {
                LOG_ERROR("module", "mod-wow-fates: Expedition {} counts creature {}, which does not exist; skipped.", def.id, def.objectiveEntry);
                continue;
            }
            if (def.summonEntry && !sObjectMgr->GetCreatureTemplate(def.summonEntry))
            {
                LOG_ERROR("module", "mod-wow-fates: Expedition {} brings creature {}, which does not exist; it brings none.", def.id, def.summonEntry);
                def.summonEntry = 0;
            }

            _defs[def.id] = std::move(def);
        } while (result->NextRow());
    }

    // The settings of each type of objective.
    if (QueryResult result = WorldDatabase.Query(
        "SELECT `expedition`, `boss_entry`, `boss_at`, `gather_entry`, `gather_count`, `gather_respawn` FROM `mod_wow_fates_objective`"))
    {
        do
        {
            Field* f = result->Fetch();
            auto itr = _defs.find(f[0].Get<uint32>());
            if (itr == _defs.end())
                continue;
            itr->second.bossEntry = f[1].Get<uint32>();
            itr->second.bossAt = std::clamp<uint8>(f[2].Get<uint8>(), 10, 95);
            itr->second.gatherEntry = f[3].Get<uint32>();
            itr->second.gatherCount = std::max<uint32>(f[4].Get<uint32>(), 1);
            itr->second.gatherRespawn = f[5].Get<uint32>();
        } while (result->NextRow());
    }

    for (auto itr = _defs.begin(); itr != _defs.end();)
    {
        WowFatesExpeditionDef const& def = itr->second;
        if (def.objectiveType == WOW_FATES_OBJECTIVE_ASSAULT && !sObjectMgr->GetCreatureTemplate(def.bossEntry))
        {
            LOG_ERROR("module", "mod-wow-fates: Expedition {} is an assault whose boss, creature {}, does not exist; skipped.", def.id, def.bossEntry);
            itr = _defs.erase(itr);
        }
        else if (def.objectiveType == WOW_FATES_OBJECTIVE_GATHER && !sObjectMgr->GetGameObjectTemplate(def.gatherEntry))
        {
            LOG_ERROR("module", "mod-wow-fates: Expedition {} is a gathering whose object, gameobject {}, does not exist; skipped.", def.id, def.gatherEntry);
            itr = _defs.erase(itr);
        }
        else
            ++itr;
    }

    if (QueryResult result = WorldDatabase.Query("SELECT `id`, `locale`, `name` FROM `mod_wow_fates_expedition_locale`"))
    {
        do
        {
            Field* f = result->Fetch();
            auto itr = _defs.find(f[0].Get<uint32>());
            if (itr != _defs.end())
                itr->second.names[f[1].Get<std::string>()] = f[2].Get<std::string>();
        } while (result->NextRow());
    }

    if (QueryResult result = WorldDatabase.Query("SELECT `id`, `expedition`, `map`, `x`, `y`, `z`, `o` FROM `mod_wow_fates_spawn`"))
    {
        do
        {
            Field* f = result->Fetch();
            WowFatesSpawnPoint spawn;
            spawn.id = f[0].Get<uint32>();
            spawn.expedition = f[1].Get<uint32>();
            spawn.map = f[2].Get<uint32>();
            spawn.x = f[3].Get<float>();
            spawn.y = f[4].Get<float>();
            spawn.z = f[5].Get<float>();
            spawn.o = f[6].Get<float>();

            auto def = _defs.find(spawn.expedition);
            if (def == _defs.end())
                continue;
            MapEntry const* map = sMapStore.LookupEntry(spawn.map);
            if (!map || map->Instanceable())
            {
                LOG_ERROR("module", "mod-wow-fates: spawn point {} is on map {}, which is not a continent; skipped.", spawn.id, spawn.map);
                continue;
            }

            def->second.spawns.push_back(spawn.id);
            _spawns[spawn.id] = spawn;
        } while (result->NextRow());
    }

    if (QueryResult result = WorldDatabase.Query(
        "SELECT `expedition`, `min_level`, `max_level`, `money`, `xp`, `item`, `item_count`, `hidden` FROM `mod_wow_fates_reward`"))
    {
        do
        {
            Field* f = result->Fetch();
            WowFatesReward reward;
            reward.expedition = f[0].Get<uint32>();
            reward.minLevel = f[1].Get<uint8>();
            reward.maxLevel = f[2].Get<uint8>();
            reward.money = f[3].Get<uint32>();
            reward.xp = f[4].Get<uint32>();
            reward.item = f[5].Get<uint32>();
            reward.itemCount = std::max<uint32>(f[6].Get<uint32>(), 1);
            reward.hidden = f[7].Get<uint8>() != 0;

            if (reward.item && !sObjectMgr->GetItemTemplate(reward.item))
            {
                LOG_ERROR("module", "mod-wow-fates: a reward gives item {}, which does not exist; the item is left out.", reward.item);
                reward.item = 0;
            }

            _rewards.push_back(reward);
        } while (result->NextRow());
    }

    LOG_INFO("server.loading", ">> mod-wow-fates: {} Expeditions, {} spawn points, {} rewards.", _defs.size(), _spawns.size(), _rewards.size());

    // The first Expeditions appear at the next tick.
    _rotateTimer = 0;
}

std::string WowFatesExpeditionMgr::Name(WowFatesExpeditionDef const* def, WorldSession* session) const
{
    if (session)
    {
        LocaleConstant const locale = session->GetSessionDbLocaleIndex();
        if (locale != LOCALE_enUS && locale < TOTAL_LOCALES)
        {
            auto itr = def->names.find(localeNames[locale]);
            if (itr != def->names.end() && !itr->second.empty())
                return itr->second;
        }
    }
    return def->name;
}

std::string WowFatesExpeditionMgr::CreatureName(uint32 entry, WorldSession* session) const
{
    std::string name;
    if (CreatureTemplate const* creature = sObjectMgr->GetCreatureTemplate(entry))
        name = creature->Name;
    if (session)
        if (CreatureLocale const* locale = sObjectMgr->GetCreatureLocale(entry))
            ObjectMgr::GetLocaleString(locale->Name, session->GetSessionDbLocaleIndex(), name);
    return name;
}

std::string WowFatesExpeditionMgr::GameObjectName(uint32 entry, WorldSession* session) const
{
    std::string name;
    if (GameObjectTemplate const* go = sObjectMgr->GetGameObjectTemplate(entry))
        name = go->name;
    if (session)
        if (GameObjectLocale const* locale = sObjectMgr->GetGameObjectLocale(entry))
            ObjectMgr::GetLocaleString(locale->Name, session->GetSessionDbLocaleIndex(), name);
    return name;
}

std::string WowFatesExpeditionMgr::Objective(WowFatesActiveExpedition const& active, WorldSession* session) const
{
    WowFatesExpeditionDef const* def = active.def;
    if (def->objectiveType == WOW_FATES_OBJECTIVE_GATHER)
        return Acore::StringFormat(WowFatesText(session, WOW_FATES_STR_OBJECTIVE_GATHER), GameObjectName(def->gatherEntry, session));
    if (def->objectiveType == WOW_FATES_OBJECTIVE_ASSAULT)
    {
        std::string const boss = CreatureName(def->bossEntry, session);
        if (!def->objectiveEntry)
            return Acore::StringFormat(WowFatesText(session, WOW_FATES_STR_OBJECTIVE_ASSAULT_ANY), boss);
        return Acore::StringFormat(WowFatesText(session, WOW_FATES_STR_OBJECTIVE_ASSAULT),
            CreatureName(def->objectiveEntry, session), boss);
    }

    if (!def->objectiveEntry)
        return WowFatesText(session, WOW_FATES_STR_OBJECTIVE_KILL_ANY);
    return Acore::StringFormat(WowFatesText(session, WOW_FATES_STR_OBJECTIVE_KILL), CreatureName(def->objectiveEntry, session));
}

bool WowFatesExpeditionMgr::Inside(WowFatesActiveExpedition const& active, Unit const* unit, float rangeFactor) const
{
    return unit->IsInWorld() && unit->GetMapId() == active.spawn.map && unit->GetInstanceId() == 0
        && unit->GetExactDist2d(active.spawn.x, active.spawn.y) <= active.def->radius * rangeFactor;
}

WowFatesActiveExpedition* WowFatesExpeditionMgr::FindAt(Unit const* unit)
{
    for (auto& [id, active] : _active)
        if (Inside(active, unit))
            return &active;
    return nullptr;
}

// ---------------------------------------------------------------------------
// Start and end

bool WowFatesExpeditionMgr::Start(uint32 id, uint32 spawnId)
{
    std::lock_guard<std::recursive_mutex> guard(_lock);

    auto def = _defs.find(id);
    if (def == _defs.end() || _active.count(id) || def->second.spawns.empty())
        return false;

    if (!spawnId)
        spawnId = Acore::Containers::SelectRandomContainerElement(def->second.spawns);
    auto spawn = _spawns.find(spawnId);
    if (spawn == _spawns.end() || spawn->second.expedition != id)
        return false;

    StartLocked(&def->second, &spawn->second);
    return true;
}

void WowFatesExpeditionMgr::StartLocked(WowFatesExpeditionDef const* def, WowFatesSpawnPoint const* spawn)
{
    WowFatesActiveExpedition& active = _active[def->id];
    active = WowFatesActiveExpedition();
    active.def = def;
    active.spawn = *spawn;
    active.zone = sMapMgr->GetZoneId(PHASEMASK_NORMAL, spawn->map, spawn->x, spawn->y, spawn->z);
    active.remaining = int64(def->duration) * IN_MILLISECONDS;
    _cooldowns.erase(def->id);
    ++_activeCount;

    LOG_INFO("module", "mod-wow-fates: Expedition {} ({}) starts at spawn point {} (map {}, zone {}).",
        def->id, def->name, spawn->id, spawn->map, active.zone);

    BroadcastSet(active, true);
}

bool WowFatesExpeditionMgr::Stop(uint32 id)
{
    std::lock_guard<std::recursive_mutex> guard(_lock);

    auto itr = _active.find(id);
    if (itr == _active.end())
        return false;

    Finish(itr->second, false);
    _active.erase(itr);
    return true;
}

bool WowFatesExpeditionMgr::Complete(uint32 id)
{
    std::lock_guard<std::recursive_mutex> guard(_lock);

    auto itr = _active.find(id);
    if (itr == _active.end())
        return false;

    itr->second.progress = itr->second.def->objectiveCount;
    itr->second.progressPct = 100.0;
    Finish(itr->second, true);
    _active.erase(itr);
    return true;
}

void WowFatesExpeditionMgr::StopAll()
{
    std::lock_guard<std::recursive_mutex> guard(_lock);

    for (auto& [id, active] : _active)
        Finish(active, false);
    _active.clear();
}

void WowFatesExpeditionMgr::Finish(WowFatesActiveExpedition& active, bool success)
{
    WowFatesConfig const& c = WowFatesConf();
    Map* map = sMapMgr->FindBaseNonInstanceMap(active.spawn.map);

    LOG_INFO("module", "mod-wow-fates: Expedition {} ends, {} ({} %, {} kills, {} contributors).",
        active.def->id, success ? "complete" : "failed", uint32(active.progressPct), active.progress, active.contributors.size());

    BroadcastDel(active.def->id);

    // Everyone concerned hears the end: those inside and those who helped.
    std::unordered_set<ObjectGuid> concerned(active.inside.begin(), active.inside.end());
    for (auto const& [guid, contribution] : active.contributors)
        concerned.insert(guid);

    for (ObjectGuid const& guid : concerned)
    {
        Player* player = ObjectAccessor::FindPlayer(guid);
        if (!player)
            continue;

        // No chat: the client's banner, then, on success, its window of what
        // was won ("won", empty when nothing).
        SendAddon(player, Acore::StringFormat("end\t{}\t{}", active.def->id, success ? 1 : 0));
        if (!success)
            continue;

        WowFatesParticipation const part = Participation(active, guid);
        bool const inRange = Inside(active, player, std::max(c.rewardRange, 1.0f));
        if (part != WOW_FATES_PART_NONE && inRange)
            Reward(active, player, part);
        else
            SendAddon(player, Acore::StringFormat("won\t{}\t0\t0\t", active.def->id));
    }

    // Its living monsters go with it, in a fight or far away; the corpses go
    // by themselves, when their time is up (SUMMON_CORPSE_MS).
    if (map)
        for (ObjectGuid const& guid : active.summons)
            if (Creature* creature = map->GetCreature(guid))
                if (creature->IsAlive())
                {
                    creature->CombatStop(true);
                    creature->DespawnOrUnsummon();
                }
    active.summons.clear();
    if (map)
        for (ObjectGuid const& guid : active.objects)
            if (GameObject* go = map->GetGameObject(guid))
                go->DespawnOrUnsummon();
    active.objects.clear();
    if (map && active.boss)
        if (Creature* boss = map->GetCreature(active.boss))
            if (boss->IsAlive())
            {
                boss->CombatStop(true);
                boss->DespawnOrUnsummon();
            }

    for (ObjectGuid const& guid : active.inside)
        Leave(guid, true);
    active.inside.clear();

    _cooldowns[active.def->id] = int64(active.def->cooldown ? active.def->cooldown : c.cooldown) * IN_MILLISECONDS;
    if (_activeCount)
        --_activeCount;
}

namespace
{
    // A share of an amount, rounded up: 33 % of 2 potions is 1.
    uint32 Share(uint32 amount, uint32 pct)
    {
        return uint32((uint64(amount) * pct + 99) / 100);
    }

    uint32 PartPct(WowFatesParticipation part)
    {
        WowFatesConfig const& c = WowFatesConf();
        switch (part)
        {
            case WOW_FATES_PART_LIGHT:  return c.lightPct;
            case WOW_FATES_PART_MEDIUM: return c.mediumPct;
            case WOW_FATES_PART_STRONG: return 100;
            default:                    return 0;
        }
    }
}

// The rows of the player's level (or of the Expedition's, when the
// configuration keeps strong players from farming low Expeditions): the
// Expedition's own and those of every Expedition.
std::vector<WowFatesReward const*> WowFatesExpeditionMgr::RewardRows(WowFatesActiveExpedition const& active, Player const* player) const
{
    int32 const margin = WowFatesConf().rewardLevelMargin;
    uint8 level = player->GetLevel();
    if (margin >= 0)
        level = uint8(std::min<int32>(level, int32(active.def->level) + margin));

    std::vector<WowFatesReward const*> rows;
    for (WowFatesReward const& reward : _rewards)
        if ((reward.expedition == active.def->id || reward.expedition == 0) && level >= reward.minLevel && level <= reward.maxLevel)
            rows.push_back(&reward);
    return rows;
}

// "rew", id, money, xp, items: what the player gets with a strong
// participation, for the map's tooltip and the tracker. Items are "entry,
// count,hidden,single" separated by ";"; a hidden row shows neither its item
// nor its amounts ("0,0,1,single").
std::string WowFatesExpeditionMgr::RewardLine(WowFatesActiveExpedition const& active, Player const* player) const
{
    bool const maxLevel = player->GetLevel() >= sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL);
    uint32 money = 0, xp = 0;
    std::string items;
    for (WowFatesReward const* reward : RewardRows(active, player))
    {
        bool const single = reward->item && reward->itemCount == 1;
        if (reward->hidden)
        {
            items += Acore::StringFormat("{}0,0,1,{}", items.empty() ? "" : ";", single ? 1 : 0);
            continue;
        }
        money += reward->money;
        if (!maxLevel)
            xp += reward->xp;
        if (reward->item)
            items += Acore::StringFormat("{}{},{},0,{}", items.empty() ? "" : ";", reward->item, reward->itemCount, single ? 1 : 0);
    }
    return Acore::StringFormat("rew\t{}\t{}\t{}\t{}", active.def->id, money, xp, items);
}

void WowFatesExpeditionMgr::Reward(WowFatesActiveExpedition const& active, Player* player, WowFatesParticipation part)
{
    std::vector<WowFatesReward const*> const rows = RewardRows(active, player);
    uint32 const pct = PartPct(part);

    WorldSession* session = player->GetSession();
    std::string const name = Name(active.def, session);

    // What was won, for the client's window: "won", id, money, xp, items as
    // "entry,count" separated by ";", the hidden ones shown now.
    uint32 wonMoney = 0, wonXp = 0;
    std::map<uint32, uint32> wonItems;

    for (WowFatesReward const* reward : rows)
    {
        if (uint32 const money = Share(reward->money, pct))
        {
            player->ModifyMoney(int32(money));
            wonMoney += money;
        }

        if (uint32 const xp = Share(reward->xp, pct))
            if (player->GetLevel() < sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL))
            {
                player->GiveXP(xp, nullptr);
                wonXp += xp;
            }

        // A single item goes to a strong participation only.
        if (!reward->item || (reward->itemCount == 1 && part != WOW_FATES_PART_STRONG))
            continue;

        // In the bags what fits, by mail the rest.
        uint32 count = Share(reward->itemCount, pct);
        if (!count)
            continue;
        wonItems[reward->item] += count;
        uint32 noSpace = 0;
        ItemPosCountVec dest;
        InventoryResult const msg = player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, reward->item, count, &noSpace);
        uint32 const stored = dest.empty() ? 0 : (msg == EQUIP_ERR_OK ? count : count - noSpace);
        if (stored)
            if (Item* item = player->StoreNewItem(dest, reward->item, true, Item::GenerateItemRandomPropertyId(reward->item)))
            {
                player->SendNewItem(item, stored, true, false, false, false);
                count -= stored;
            }

        if (!count)
            continue;

        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(reward->item);
        if (!proto)
            continue;

        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
        MailDraft draft(WowFatesText(session, WOW_FATES_STR_MAIL_SUBJECT),
            Acore::StringFormat(WowFatesText(session, WOW_FATES_STR_MAIL_BODY), name));
        uint32 const stack = std::max<uint32>(proto->GetMaxStackSize(), 1);
        for (uint32 left = count; left > 0;)
        {
            uint32 const n = std::min(left, stack);
            if (Item* item = Item::CreateItem(reward->item, n, player))
            {
                item->SaveToDB(trans);
                draft.AddItem(item);
            }
            left -= n;
        }
        draft.SendMailTo(trans, MailReceiver(player), MailSender(MAIL_NORMAL, 0, MAIL_STATIONERY_GM), MAIL_CHECK_MASK_COPIED);
        CharacterDatabase.CommitTransaction(trans);
    }

    std::string items;
    for (auto const& [entry, count] : wonItems)
        items += Acore::StringFormat("{}{},{}", items.empty() ? "" : ";", entry, count);
    SendAddon(player, Acore::StringFormat("won\t{}\t{}\t{}\t{}", active.def->id, wonMoney, wonXp, items));
}

// ---------------------------------------------------------------------------
// The clients' map

// "set", id, map, x, y, radius, level, progress in percent, 100, seconds left, name,
// objective, tracker line, duration, type of objective (the map's icon): the
// texts in the player's language.
std::string WowFatesExpeditionMgr::MapLine(WowFatesActiveExpedition const& active, WorldSession* session) const
{
    WowFatesExpeditionDef const* def = active.def;
    return Acore::StringFormat("set\t{}\t{}\t{:.1f}\t{:.1f}\t{:.1f}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}",
        def->id, active.spawn.map, active.spawn.x, active.spawn.y, def->radius, uint32(def->level),
        std::min<uint32>(uint32(active.progressPct), 100), 100u,
        uint32(std::max<int64>(active.remaining, 0) / IN_MILLISECONDS),
        Clean(Name(def, session)), Clean(Objective(active, session)), Clean(TrackerLabel(active, session)),
        def->duration, uint32(def->objectiveType));
}

// The objective as the tracker shows it, the count apart: "Kobold Miner slain";
// once the leader of an assault is out, "Defias Ringleader slain".
std::string WowFatesExpeditionMgr::TrackerLabel(WowFatesActiveExpedition const& active, WorldSession* session) const
{
    WowFatesExpeditionDef const* def = active.def;
    if (def->objectiveType == WOW_FATES_OBJECTIVE_GATHER)
        return Acore::StringFormat(WowFatesText(session, WOW_FATES_STR_TRACKER_GATHER), GameObjectName(def->gatherEntry, session));
    if (def->objectiveType == WOW_FATES_OBJECTIVE_ASSAULT && active.bossOut)
        return Acore::StringFormat(WowFatesText(session, WOW_FATES_STR_TRACKER_KILL), CreatureName(def->bossEntry, session));
    if (!def->objectiveEntry)
        return WowFatesText(session, WOW_FATES_STR_TRACKER_KILL_ANY);
    return Acore::StringFormat(WowFatesText(session, WOW_FATES_STR_TRACKER_KILL), CreatureName(def->objectiveEntry, session));
}

// None: nothing done. Light: a kill, an object gathered, or any damage or
// healing. Medium and strong: MediumShare and StrongShare of the average of
// those who did any, counted twice, on damage and healing and on kills and
// objects gathered, the better kept: alone,
// a player is the average (strong), even when his blows were not counted (an
// instant kill deals no damage).
WowFatesParticipation WowFatesExpeditionMgr::Participation(WowFatesActiveExpedition const& active, ObjectGuid guid) const
{
    auto contribution = active.contributors.find(guid);
    if (contribution == active.contributors.end())
        return WOW_FATES_PART_NONE;

    WowFatesConfig const& c = WowFatesConf();
    auto tier = [&c](double value, double average)
    {
        if (value <= 0.0)
            return WOW_FATES_PART_NONE;
        if (value >= average * double(c.strongShare))
            return WOW_FATES_PART_STRONG;
        if (value >= average * double(c.mediumShare))
            return WOW_FATES_PART_MEDIUM;
        return WOW_FATES_PART_LIGHT;
    };

    uint64 totalValue = 0, totalKills = 0;
    uint32 withValue = 0, withKills = 0;
    for (auto const& [other, o] : active.contributors)
    {
        if (uint64 const v = o.damage + o.healing)
        {
            totalValue += v;
            ++withValue;
        }
        if (uint32 const actions = o.kills + o.picks)
        {
            totalKills += actions;
            ++withKills;
        }
    }

    WowFatesContribution const& own = contribution->second;
    WowFatesParticipation const byValue = tier(double(own.damage + own.healing),
        withValue ? double(totalValue) / double(withValue) : 0.0);
    WowFatesParticipation const byKills = tier(double(own.kills + own.picks),
        withKills ? double(totalKills) / double(withKills) : 0.0);
    return std::max(byValue, byKills);
}

// "me", id, kills, participation (0 none, 1 light, 2 medium, 3 strong) and
// the share of the rewards it gives: to each player inside or who helped,
// when it changes.
void WowFatesExpeditionMgr::SendStatus(WowFatesActiveExpedition& active)
{
    std::unordered_set<ObjectGuid> concerned(active.inside.begin(), active.inside.end());
    for (auto const& [guid, contribution] : active.contributors)
        concerned.insert(guid);

    for (ObjectGuid const& guid : concerned)
    {
        uint32 kills = 0;
        auto contribution = active.contributors.find(guid);
        if (contribution != active.contributors.end())
            kills = contribution->second.kills;
        WowFatesParticipation const part = Participation(active, guid);

        uint32 const packed = kills * 4 + part;
        auto sent = active.sentStatus.find(guid);
        if (sent != active.sentStatus.end() && sent->second == packed)
            continue;

        if (Player* player = ObjectAccessor::FindPlayer(guid))
        {
            SendAddon(player, Acore::StringFormat("me\t{}\t{}\t{}\t{}", active.def->id, kills, uint32(part), PartPct(part)));
            active.sentStatus[guid] = packed;
        }
    }
}

void WowFatesExpeditionMgr::BroadcastSet(WowFatesActiveExpedition const& active, bool withRewards)
{
    for (auto const& [guid, player] : ObjectAccessor::GetPlayers())
        if (player && player->IsInWorld())
        {
            SendAddon(player, MapLine(active, player->GetSession()));
            if (withRewards)
                SendAddon(player, RewardLine(active, player));
        }
}

void WowFatesExpeditionMgr::BroadcastDel(uint32 id)
{
    std::string const line = Acore::StringFormat("del\t{}", id);
    for (auto const& [guid, player] : ObjectAccessor::GetPlayers())
        if (player && player->IsInWorld())
            SendAddon(player, line);
}

void WowFatesExpeditionMgr::SendList(Player* player)
{
    std::lock_guard<std::recursive_mutex> guard(_lock);

    SendAddon(player, "reset");
    if (!WowFatesConf().enable)
        return;
    for (auto const& [id, active] : _active)
    {
        SendAddon(player, MapLine(active, player->GetSession()));
        SendAddon(player, RewardLine(active, player));
    }
}

// The client asks for the list when its map code is loaded (after the
// login's list, which it may have missed).
bool WowFatesExpeditionMgr::OnAddonMessage(Player* player, std::string const& msg)
{
    std::string const prefix = std::string(ADDON_PREFIX) + "\t";
    if (msg.compare(0, prefix.size(), prefix) != 0)
        return false;
    if (msg.compare(prefix.size(), std::string::npos, "hello") == 0)
        SendList(player);
    return true;
}

// ---------------------------------------------------------------------------
// The world tick

void WowFatesExpeditionMgr::Update(uint32 diff)
{
    WowFatesConfig const& c = WowFatesConf();
    std::lock_guard<std::recursive_mutex> guard(_lock);

    if (!c.enable)
    {
        if (!_active.empty())
            StopAll();
        return;
    }

    for (auto itr = _cooldowns.begin(); itr != _cooldowns.end();)
    {
        itr->second -= diff;
        if (itr->second <= 0)
            itr = _cooldowns.erase(itr);
        else
            ++itr;
    }

    _rotateTimer -= diff;
    if (_rotateTimer <= 0)
    {
        _rotateTimer = c.spawnInterval;
        Rotate();
    }

    _areaTimer -= diff;
    bool const areaTick = _areaTimer <= 0;
    if (areaTick)
        _areaTimer = c.areaInterval;

    std::vector<std::pair<uint32, bool>> ended;
    for (auto& [id, active] : _active)
    {
        active.remaining -= diff;
        active.summonTimer -= diff;
        active.gatherTimer -= diff;

        if (areaTick)
        {
            Map* map = sMapMgr->FindBaseNonInstanceMap(active.spawn.map);
            UpdateArea(active, map);
            UpdateSummons(active, map, diff);
            if (active.def->objectiveType == WOW_FATES_OBJECTIVE_ASSAULT)
                UpdateBoss(active, map);
            else if (active.def->objectiveType == WOW_FATES_OBJECTIVE_GATHER)
                UpdateGather(active, map);

            uint32 const pct = uint32(active.progressPct);
            if (pct != active.sentProgress && pct < 100)
            {
                active.sentProgress = pct;
                BroadcastSet(active);
            }
            SendStatus(active);
        }

        if (active.progressPct >= 100.0)
            ended.emplace_back(id, true);
        else if (active.remaining <= 0)
            ended.emplace_back(id, false);
    }

    for (auto const& [id, success] : ended)
    {
        auto itr = _active.find(id);
        Finish(itr->second, success);
        _active.erase(itr);
    }

    UpdateSync(diff);
}

void WowFatesExpeditionMgr::Rotate()
{
    WowFatesConfig const& c = WowFatesConf();

    while (_active.size() < c.maxActive)
    {
        std::vector<WowFatesExpeditionDef const*> candidates;
        uint64 totalWeight = 0;
        for (auto const& [id, def] : _defs)
            if (def.weight && !def.spawns.empty() && !_active.count(id) && !_cooldowns.count(id))
            {
                candidates.push_back(&def);
                totalWeight += def.weight;
            }
        if (candidates.empty())
            return;

        uint64 roll = urand(0, uint32(std::min<uint64>(totalWeight, 0xFFFFFFFF) - 1));
        WowFatesExpeditionDef const* chosen = candidates.back();
        for (WowFatesExpeditionDef const* def : candidates)
        {
            if (roll < def->weight)
            {
                chosen = def;
                break;
            }
            roll -= def->weight;
        }

        uint32 const spawnId = Acore::Containers::SelectRandomContainerElement(chosen->spawns);
        StartLocked(chosen, &_spawns[spawnId]);
    }
}

void WowFatesExpeditionMgr::UpdateArea(WowFatesActiveExpedition& active, Map* map)
{
    std::unordered_set<ObjectGuid> now;
    if (map)
        for (auto const& ref : map->GetPlayers())
        {
            Player* player = ref.GetSource();
            if (!player || player->IsGameMaster() || !Inside(active, player))
                continue;

            ObjectGuid const guid = player->GetGUID();
            if (!active.inside.count(guid))
            {
                // Already in another Expedition's area: that one keeps him.
                auto synced = _synced.find(guid);
                if (synced != _synced.end() && synced->second.expedition != active.def->id)
                    continue;

                _synced[guid] = SyncState();
                _synced[guid].expedition = active.def->id;
                _pendingUnsync.erase(guid);
                // the banner and the tracker of the client
                SendAddon(player, MapLine(active, player->GetSession()));
                SendAddon(player, RewardLine(active, player));
                SendAddon(player, Acore::StringFormat("enter\t{}", active.def->id));
                active.sentStatus.erase(guid);
            }
            now.insert(guid);
        }

    for (ObjectGuid const& guid : active.inside)
        if (!now.count(guid))
            LeaveArea(active, guid);

    active.inside = std::move(now);
}

// Out of the area while the Expedition goes on.
void WowFatesExpeditionMgr::LeaveArea(WowFatesActiveExpedition const& active, ObjectGuid guid)
{
    if (Player* player = ObjectAccessor::FindPlayer(guid))
    {
        SendAddon(player, Acore::StringFormat("leave\t{}", active.def->id));
    }
    Leave(guid, true);
}

void WowFatesExpeditionMgr::Leave(ObjectGuid guid, bool /*announce*/)
{
    _synced.erase(guid);

    Player* player = ObjectAccessor::FindPlayer(guid);
    if (!player || !player->IsInWorld())
    {
        _pendingUnsync.insert(guid);
        return;
    }

    if (sWowFatesSync->IsSynced(player))
    {
        sWowFatesSync->Remove(player);
    }
    SendAddon(player, "sync\t0\t100");
}

void WowFatesExpeditionMgr::UpdateSync(uint32 diff)
{
    WowFatesConfig const& c = WowFatesConf();

    for (auto itr = _pendingUnsync.begin(); itr != _pendingUnsync.end();)
    {
        Player* player = ObjectAccessor::FindPlayer(*itr);
        if (player && player->IsInWorld())
        {
            if (!_synced.count(*itr))
                sWowFatesSync->Remove(player);
            itr = _pendingUnsync.erase(itr);
        }
        else
            ++itr;
    }

    for (auto& [guid, state] : _synced)
    {
        state.timer -= diff;
        if (state.timer > 0)
        {
            // A pet or a guardian summoned since the last measure: synced at once.
            if (Player* player = ObjectAccessor::FindPlayer(guid))
                if (player->IsInWorld() && state.ratio < 1.0f)
                    for (Unit* unit : player->m_Controlled)
                        if (unit && unit->IsInWorld() && unit->IsAlive() && !unit->IsPlayer()
                            && !unit->HasAura(SPELL_WOW_FATES_SYNCED))
                        {
                            sWowFatesSync->SyncControlled(player);
                            break;
                        }
            continue;
        }
        state.timer = c.syncInterval;

        auto active = _active.find(state.expedition);
        Player* player = ObjectAccessor::FindPlayer(guid);
        if (active == _active.end() || !player || !player->IsInWorld())
            continue;

        if (!c.syncEnable)
        {
            sWowFatesSync->Remove(player);
            continue;
        }

        float const ratio = sWowFatesSync->Apply(player, active->second.def->level);
        state.ratio = ratio;

        // The debuff's tooltip on the client: the level and the share kept.
        int32 const pct = int32(std::lround(ratio * 100.0f));
        if (pct != state.sentPct)
        {
            state.sentPct = pct;
            SendAddon(player, Acore::StringFormat("sync\t{}\t{}", pct < 100 ? uint32(active->second.def->level) : 0u, pct));
        }
    }
}

// A point of the area for a monster: a random spot of the ground within 80 %
// of the radius, seen from the centre (not behind a wall nor in a cave under
// it) and not much higher or lower than the centre. The ground is searched
// from well above, so that a slope or a hill around the centre is found (from
// just above the centre, every point higher up the hill had no ground and the
// monster fell back on the centre). Several tries; the centre, a little
// spread, as the last resort.
Position WowFatesExpeditionMgr::SpawnPoint(WowFatesActiveExpedition const& active, Map* map) const
{
    WowFatesSpawnPoint const& c = active.spawn;
    float const reach = active.def->radius * 0.8f;
    for (uint32 attempt = 0; attempt < 16; ++attempt)
    {
        float const angle = frand(0.0f, 2.0f * float(M_PI));
        float const dist = reach * std::sqrt(frand(0.0f, 1.0f)); // even over the disc
        float const x = c.x + dist * std::cos(angle);
        float const y = c.y + dist * std::sin(angle);
        float const z = map->GetHeight(PHASEMASK_NORMAL, x, y, c.z + 40.0f, true, 80.0f);
        if (z <= INVALID_HEIGHT || std::fabs(z - c.z) > 25.0f)
            continue;
        if (!map->isInLineOfSight(c.x, c.y, c.z + 2.0f, x, y, z + 2.0f, PHASEMASK_NORMAL,
                LINEOFSIGHT_ALL_CHECKS, VMAP::ModelIgnoreFlags::Nothing))
            continue;
        return Position(x, y, z, frand(0.0f, 2.0f * float(M_PI)));
    }

    float const angle = frand(0.0f, 2.0f * float(M_PI));
    float const x = c.x + 3.0f * std::cos(angle);
    float const y = c.y + 3.0f * std::sin(angle);
    float z = map->GetHeight(PHASEMASK_NORMAL, x, y, c.z + 5.0f, true, 20.0f);
    if (z <= INVALID_HEIGHT)
        return Position(c.x, c.y, c.z, angle);
    return Position(x, y, z, angle);
}

void WowFatesExpeditionMgr::UpdateSummons(WowFatesActiveExpedition& active, Map* map, uint32 /*diff*/)
{
    WowFatesExpeditionDef const* def = active.def;
    if (!def->summonEntry || !def->summonCount || !map)
        return;

    // Every monster brought is kept in the list, corpses included, so that the
    // end takes them all away; only those gone from the map (despawned, grid
    // unloaded) leave it. The living count against summon_count.
    std::erase_if(active.summons, [map](ObjectGuid const& guid)
    {
        return !map->GetCreature(guid);
    });
    uint32 const alive = uint32(std::count_if(active.summons.begin(), active.summons.end(), [map](ObjectGuid const& guid)
    {
        Creature* creature = map->GetCreature(guid);
        return creature && creature->IsAlive();
    }));

    // The pace of the area: kills per second over the last AdaptiveWindow.
    // The faster it is cleared, the shorter the delay (MinRespawn at least)
    // and the more monsters at once (summon_count + pace x summon_respawn x
    // AdaptiveAggression, MaxPopulation times summon_count at most), each wave
    // filling the area back up; when the pace drops, both go back to the
    // Expedition's own. The area is never empty: under MinAlivePct of
    // summon_count alive, the missing ones come at once, at random points.
    WowFatesConfig const& c = WowFatesConf();
    int64 const now = int64(def->duration) * IN_MILLISECONDS - active.remaining;
    while (!active.killTimes.empty() && active.killTimes.front() < now - int64(c.adaptiveWindow))
        active.killTimes.pop_front();
    float const pace = c.adaptiveSpawn ? float(active.killTimes.size()) * IN_MILLISECONDS / float(c.adaptiveWindow) : 0.0f;
    float const respawn = float(def->summonRespawn);
    uint32 const population = std::clamp<uint32>(def->summonCount + uint32(std::lround(pace * respawn * c.adaptiveAggression)),
        def->summonCount, uint32(std::lround(float(def->summonCount) * c.maxPopulation)));

    // Nobody near: the grid sleeps, no monster is brought.
    uint32 const floor = uint32(std::ceil(float(def->summonCount) * c.minAlivePct / 100.0f));
    bool const thin = c.adaptiveSpawn && active.filled && alive < floor;
    if (alive >= population || (active.summonTimer > 0 && !thin) || !map->IsGridLoaded(active.spawn.x, active.spawn.y))
        return;

    float const delay = std::max(respawn / (1.0f + pace * respawn), float(c.minRespawn) / IN_MILLISECONDS);
    // Under the floor between two waves: up to the floor at once, the wave keeps its time.
    uint32 const wanted = (thin && active.summonTimer > 0) ? floor - alive
        : ((!active.filled || pace > 0.0f) ? population - alive : 1);
    bool const wave = !(thin && active.summonTimer > 0);
    for (uint32 i = 0; i < wanted; ++i)
    {
        Position const pos = SpawnPoint(active, map);
        TempSummon* summon = map->SummonCreature(def->summonEntry, pos);
        if (!summon)
            break;

        summon->SetTempSummonType(TEMPSUMMON_CORPSE_TIMED_DESPAWN);
        summon->SetTimer(SUMMON_CORPSE_MS);

        // They roam around where they appeared, back to it after a fight: random
        // as their default movement, so that a reset of their AI keeps it.
        float const wander = std::clamp(def->radius * 0.25f, 5.0f, 15.0f);
        summon->SetHomePosition(pos);
        summon->SetWanderDistance(wander);
        summon->SetDefaultMovementType(RANDOM_MOTION_TYPE);
        summon->GetMotionMaster()->Initialize();
        active.summons.push_back(summon->GetGUID());
    }

    active.filled = true;
    if (wave)
        active.summonTimer = c.adaptiveSpawn && pace > 0.0f
            ? int64(delay * IN_MILLISECONDS)
            : int64(def->summonRespawn) * IN_MILLISECONDS;
}

// ---------------------------------------------------------------------------
// Assault and boss

// The leader of an assault: out when the kills reach boss_at, in the centre;
// back if it went away alive (a grid unloaded). Its health grows with the
// players in the area, as the objective does, and never shrinks; the bar shows
// the health it lost, and never goes back, even when it heals after a fight.
void WowFatesExpeditionMgr::UpdateBoss(WowFatesActiveExpedition& active, Map* map)
{
    if (!map || active.progressPct >= 100.0)
        return;

    WowFatesExpeditionDef const* def = active.def;
    if (!active.bossOut)
    {
        if (active.progressPct + 0.001 >= double(def->bossAt))
            SpawnBoss(active, map);
        return;
    }

    Creature* boss = active.boss ? map->GetCreature(active.boss) : nullptr;
    if (!boss)
    {
        if (map->IsGridLoaded(active.spawn.x, active.spawn.y))
            SpawnBoss(active, map);
        return;
    }
    if (!boss->IsAlive())
    {
        // killed by no player (a guard): the Expedition is done all the same
        active.progressPct = 100.0;
        return;
    }

    uint32 const players = uint32(std::max<std::size_t>(active.inside.size(), 1));
    if (players > active.bossPlayers)
    {
        float const pct = boss->GetHealthPct();
        uint32 const health = uint32(float(active.bossHealth) * (1.0f + WowFatesConf().participantScaling * float(players - 1)));
        boss->SetCreateHealth(health);
        boss->SetStatFlatModifier(UNIT_MOD_HEALTH, BASE_VALUE, float(health));
        boss->UpdateMaxHealth();
        boss->SetHealth(std::max<uint32>(1, uint32(float(boss->GetMaxHealth()) * pct / 100.0f)));
        active.bossPlayers = players;
    }

    double const lost = 1.0 - double(boss->GetHealth()) / double(std::max<uint32>(boss->GetMaxHealth(), 1));
    double const pct = double(def->bossAt) + (100.0 - double(def->bossAt)) * lost;
    active.progressPct = std::max(active.progressPct, std::min(pct, 99.0));
}

void WowFatesExpeditionMgr::SpawnBoss(WowFatesActiveExpedition& active, Map* map)
{
    WowFatesSpawnPoint const& c = active.spawn;
    float z = map->GetHeight(PHASEMASK_NORMAL, c.x, c.y, c.z + 5.0f, true, 20.0f);
    if (z <= INVALID_HEIGHT)
        z = c.z;
    Position const pos(c.x, c.y, z, c.o);
    TempSummon* boss = map->SummonCreature(active.def->bossEntry, pos);
    if (!boss)
        return;

    boss->SetTempSummonType(TEMPSUMMON_CORPSE_TIMED_DESPAWN);
    boss->SetTimer(SUMMON_CORPSE_MS);
    boss->SetHomePosition(pos);
    boss->SetWanderDistance(5.0f);
    boss->SetDefaultMovementType(RANDOM_MOTION_TYPE);
    boss->GetMotionMaster()->Initialize();

    bool const first = !active.bossOut;
    active.boss = boss->GetGUID();
    active.bossOut = true;
    active.bossHealth = boss->GetMaxHealth();
    active.bossPlayers = 1;
    UpdateBoss(active, map); // its health for the players already there

    LOG_INFO("module", "mod-wow-fates: Expedition {}: its leader appears ({} health).", active.def->id, boss->GetMaxHealth());
    if (!first)
        return;

    // The tracker's line changes; those inside see a banner with its name.
    BroadcastSet(active);
    for (ObjectGuid const& guid : active.inside)
        if (Player* player = ObjectAccessor::FindPlayer(guid))
            SendAddon(player, Acore::StringFormat("boss\t{}\t{}", active.def->id,
                Clean(CreatureName(active.def->bossEntry, player->GetSession()))));
}

// ---------------------------------------------------------------------------
// Gathering

// The objects of a gathering: gather_count of them out, at random points of
// the area as the monsters; one taken is put back after gather_respawn
// seconds, all at once at the start and when none is left.
void WowFatesExpeditionMgr::UpdateGather(WowFatesActiveExpedition& active, Map* map)
{
    if (!map)
        return;

    std::erase_if(active.objects, [map](ObjectGuid const& guid)
    {
        GameObject* go = map->GetGameObject(guid);
        return !go || !go->isSpawned();
    });

    WowFatesExpeditionDef const* def = active.def;
    if (active.objects.size() >= def->gatherCount || !map->IsGridLoaded(active.spawn.x, active.spawn.y))
        return;
    bool const all = active.objects.empty();
    if (!all && active.gatherTimer > 0)
        return;

    uint32 const wanted = all ? def->gatherCount - uint32(active.objects.size()) : 1;
    for (uint32 i = 0; i < wanted; ++i)
    {
        Position const pos = SpawnPoint(active, map);
        GameObject* go = new GameObject();
        if (!go->Create(map->GenerateLowGuid<HighGuid::GameObject>(), def->gatherEntry, map, PHASEMASK_NORMAL,
                pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ(), pos.GetOrientation(),
                G3D::Quat(), 100, GO_STATE_READY))
        {
            delete go;
            break;
        }
        // Summoned, as a spell's: deleted when it is taken, not put back.
        go->SetRespawnTime(0);
        go->SetSpellId(1);
        go->SetSpawnedByDefault(false);
        map->AddToMap(go);
        active.objects.push_back(go->GetGUID());
    }
    active.gatherTimer = int64(def->gatherRespawn) * IN_MILLISECONDS;
}

// An object taken: the share of an objective that grows with the players in
// the area, as a kill does; it goes, another comes back in its time.
void WowFatesExpeditionMgr::OnGather(Player* player, GameObject* go)
{
    std::lock_guard<std::recursive_mutex> guard(_lock);

    for (auto& [id, active] : _active)
    {
        auto itr = std::find(active.objects.begin(), active.objects.end(), go->GetGUID());
        if (itr == active.objects.end())
            continue;

        active.objects.erase(itr);
        go->DespawnOrUnsummon();
        if (active.progressPct >= 100.0)
            return;

        ++active.progress;
        active.progressPct = std::min(100.0, active.progressPct + 100.0 / Needed(active));
        ++active.contributors[player->GetGUID()].picks;
        if (active.gatherTimer <= 0)
            active.gatherTimer = int64(active.def->gatherRespawn) * IN_MILLISECONDS;
        return;
    }

    // left over from an Expedition that ended
    go->DespawnOrUnsummon();
}

// ---------------------------------------------------------------------------
// Queries and hooks

uint8 WowFatesExpeditionMgr::SyncLevel(Player const* player)
{
    std::lock_guard<std::recursive_mutex> guard(_lock);

    auto synced = _synced.find(player->GetGUID());
    if (synced == _synced.end())
        return 0;
    auto active = _active.find(synced->second.expedition);
    return active == _active.end() ? 0 : active->second.def->level;
}

void WowFatesExpeditionMgr::OnDamage(Unit* attacker, Unit* victim, uint32 damage)
{
    if (!_activeCount || !damage || !attacker || !victim || attacker == victim)
        return;
    if (!victim->IsCreature() || IsFromPlayer(victim))
        return;
    Player* player = attacker->GetCharmerOrOwnerPlayerOrPlayerItself();
    if (!player)
        return;

    std::lock_guard<std::recursive_mutex> guard(_lock);
    if (WowFatesActiveExpedition* active = FindAt(victim))
        active->contributors[player->GetGUID()].damage += damage;
}

void WowFatesExpeditionMgr::OnHeal(Unit* healer, Unit* target, uint32 gain)
{
    // Healing counts in a fight only: topping up idle friends earns nothing.
    if (!_activeCount || !gain || !healer || !target || !target->IsInCombat() || !IsFromPlayer(target))
        return;
    Player* player = healer->GetCharmerOrOwnerPlayerOrPlayerItself();
    if (!player)
        return;

    std::lock_guard<std::recursive_mutex> guard(_lock);
    if (WowFatesActiveExpedition* active = FindAt(target))
        active->contributors[player->GetGUID()].healing += gain;
}

void WowFatesExpeditionMgr::OnDeath(Unit* victim, Unit* killer)
{
    if (!_activeCount || !victim || !killer || !victim->IsCreature() || IsFromPlayer(victim))
        return;
    Player* player = killer->GetCharmerOrOwnerPlayerOrPlayerItself();
    if (!player)
        return;

    Creature const* creature = victim->ToCreature();
    if (creature->IsCritter() || creature->IsTotem())
        return;

    std::lock_guard<std::recursive_mutex> guard(_lock);
    WowFatesActiveExpedition* active = FindAt(victim);
    if (!active)
        return;
    WowFatesExpeditionDef const* def = active->def;

    // The leader of an assault: the end of the bar.
    if (def->objectiveType == WOW_FATES_OBJECTIVE_ASSAULT && active->bossOut && creature->GetGUID() == active->boss)
    {
        active->progressPct = 100.0;
        ++active->contributors[player->GetGUID()].kills;
        return;
    }

    // A gathering's guards: their kills count for the players and the pace of
    // the monsters, not for the bar.
    if (def->objectiveType == WOW_FATES_OBJECTIVE_GATHER)
    {
        ++active->contributors[player->GetGUID()].kills;
        active->killTimes.push_back(int64(def->duration) * IN_MILLISECONDS - active->remaining);
        return;
    }

    if (def->objectiveEntry && creature->GetEntry() != def->objectiveEntry)
        return;

    // Each kill brings the share of an objective that grows with the players
    // in the area at that moment: nothing is taken back when one comes or goes.
    // An assault's kills fill the bar up to where its leader appears; past it,
    // they still count for the players and the pace of the monsters.
    ++active->progress;
    if (def->objectiveType == WOW_FATES_OBJECTIVE_ASSAULT)
    {
        if (!active->bossOut)
            active->progressPct = std::min<double>(def->bossAt, active->progressPct + double(def->bossAt) / Needed(*active));
    }
    else
        active->progressPct = std::min(100.0, active->progressPct + 100.0 / Needed(*active));
    ++active->contributors[player->GetGUID()].kills;
    active->killTimes.push_back(int64(def->duration) * IN_MILLISECONDS - active->remaining);
}

// The kills the objective asks for, with the players in the area now.
double WowFatesExpeditionMgr::Needed(WowFatesActiveExpedition const& active) const
{
    float const players = float(std::max<std::size_t>(active.inside.size(), 1));
    double const needed = double(active.def->objectiveCount) * (1.0 + WowFatesConf().participantScaling * (players - 1.0f));
    return std::max(needed, 1.0);
}

void WowFatesExpeditionMgr::OnLogout(Player* player)
{
    std::lock_guard<std::recursive_mutex> guard(_lock);

    ObjectGuid const guid = player->GetGUID();
    _synced.erase(guid);
    _pendingUnsync.erase(guid);
    for (auto& [id, active] : _active)
        active.inside.erase(guid);
    sWowFatesSync->Remove(player);
}

// ---------------------------------------------------------------------------
// Commands

void WowFatesExpeditionMgr::List(ChatHandler* handler)
{
    std::lock_guard<std::recursive_mutex> guard(_lock);

    if (_active.empty())
    {
        handler->SendSysMessage(handler->PGetParseModuleString(WOW_FATES_MODULE, WOW_FATES_STR_CMD_LIST_NONE));
        return;
    }

    handler->SendSysMessage(handler->PGetParseModuleString(WOW_FATES_MODULE, WOW_FATES_STR_CMD_LIST, uint32(_active.size())));
    for (auto const& [id, active] : _active)
        handler->SendSysMessage(handler->PGetParseModuleString(WOW_FATES_MODULE, WOW_FATES_STR_CMD_LIST_LINE,
            id, Name(active.def, handler->GetSession()), uint32(active.def->level), active.spawn.id, active.spawn.map,
            int32(active.spawn.x), int32(active.spawn.y), int32(active.spawn.z),
            uint32(active.progressPct), active.progress, uint32(std::max<int64>(active.remaining, 0) / IN_MILLISECONDS),
            uint32(active.inside.size()), uint32(active.contributors.size())));
}

// Who is in an area, the ratio last applied and whether he wears the debuff.
void WowFatesExpeditionMgr::ListSynced(ChatHandler* handler)
{
    std::lock_guard<std::recursive_mutex> guard(_lock);

    if (_synced.empty())
    {
        handler->SendSysMessage(handler->PGetParseModuleString(WOW_FATES_MODULE, WOW_FATES_STR_CMD_SYNCED_NONE));
        return;
    }

    for (auto const& [guid, state] : _synced)
    {
        Player* player = ObjectAccessor::FindPlayer(guid);
        auto active = _active.find(state.expedition);
        handler->SendSysMessage(handler->PGetParseModuleString(WOW_FATES_MODULE, WOW_FATES_STR_CMD_SYNCED_LINE,
            player ? player->GetName() : guid.ToString(), state.expedition,
            active != _active.end() ? uint32(active->second.def->level) : 0u, state.ratio,
            player && sWowFatesSync->IsSynced(player) ? "yes" : "no"));
    }
}

void WowFatesExpeditionMgr::ListDefinitions(ChatHandler* handler)
{
    std::lock_guard<std::recursive_mutex> guard(_lock);

    for (auto const& [id, def] : _defs)
        handler->SendSysMessage(handler->PGetParseModuleString(WOW_FATES_MODULE, WOW_FATES_STR_CMD_DEFS_LINE,
            id, Name(&def, handler->GetSession()), uint32(def.level), uint32(def.spawns.size()), def.weight,
            _active.count(id) ? "active" : (_cooldowns.count(id) ? "cooldown" : "ready")));
}
