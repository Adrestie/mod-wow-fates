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

    template<typename... Args>
    void Tell(Player* player, uint32 id, Args&&... args)
    {
        WorldSession* session = player->GetSession();
        std::string const text = WowFatesText(session, id);
        if (text.empty())
            return;
        ChatHandler(session).SendSysMessage("|cffffd000" + Acore::StringFormat(text, std::forward<Args>(args)...) + "|r");
    }

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

            if (def.objectiveType != WOW_FATES_OBJECTIVE_KILL)
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

std::string WowFatesExpeditionMgr::Objective(WowFatesActiveExpedition const& active, WorldSession* session) const
{
    WowFatesExpeditionDef const* def = active.def;
    if (!def->objectiveEntry)
        return Acore::StringFormat(WowFatesText(session, WOW_FATES_STR_OBJECTIVE_KILL_ANY), def->objectiveCount);

    std::string name;
    if (CreatureTemplate const* creature = sObjectMgr->GetCreatureTemplate(def->objectiveEntry))
        name = creature->Name;
    if (session)
        if (CreatureLocale const* locale = sObjectMgr->GetCreatureLocale(def->objectiveEntry))
            ObjectMgr::GetLocaleString(locale->Name, session->GetSessionDbLocaleIndex(), name);
    return Acore::StringFormat(WowFatesText(session, WOW_FATES_STR_OBJECTIVE_KILL), def->objectiveCount, name);
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

    if (WowFatesConf().announce)
        AnnounceZone(active, WOW_FATES_STR_STARTED);
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

    LOG_INFO("module", "mod-wow-fates: Expedition {} ends, {} ({}/{}, {} contributors).",
        active.def->id, success ? "complete" : "failed", active.progress, active.def->objectiveCount, active.contributors.size());

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

        std::string const name = Name(active.def, player->GetSession());
        Tell(player, success ? WOW_FATES_STR_SUCCESS : WOW_FATES_STR_FAILED, name);
        SendAddon(player, Acore::StringFormat("end\t{}\t{}", active.def->id, success ? 1 : 0));
        if (!success)
            continue;

        WowFatesParticipation const part = Participation(active, guid);
        bool const inRange = Inside(active, player, std::max(c.rewardRange, 1.0f));
        if (part != WOW_FATES_PART_NONE && inRange)
            Reward(active, player, part);
        else
            Tell(player, WOW_FATES_STR_NOT_ELIGIBLE, name);
    }

    // Its monsters go with it.
    if (map)
        for (ObjectGuid const& guid : active.summons)
            if (Creature* creature = map->GetCreature(guid))
                creature->DespawnOrUnsummon();
    active.summons.clear();

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
    if (!rows.empty())
        Tell(player, WOW_FATES_STR_REWARD, name);

    for (WowFatesReward const* reward : rows)
    {
        if (uint32 const money = Share(reward->money, pct))
        {
            player->ModifyMoney(int32(money));
            Tell(player, WOW_FATES_STR_REWARD_MONEY, money / GOLD, (money % GOLD) / SILVER, money % SILVER);
        }

        if (uint32 const xp = Share(reward->xp, pct))
            player->GiveXP(xp, nullptr);

        // A single item goes to a strong participation only.
        if (!reward->item || (reward->itemCount == 1 && part != WOW_FATES_PART_STRONG))
            continue;

        // In the bags what fits, by mail the rest.
        uint32 count = Share(reward->itemCount, pct);
        if (!count)
            continue;
        uint32 noSpace = 0;
        ItemPosCountVec dest;
        InventoryResult const msg = player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, reward->item, count, &noSpace);
        uint32 const stored = dest.empty() ? 0 : (msg == EQUIP_ERR_OK ? count : count - noSpace);
        if (stored)
            if (Item* item = player->StoreNewItem(dest, reward->item, true, Item::GenerateItemRandomPropertyId(reward->item)))
            {
                player->SendNewItem(item, stored, true, false);
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
        Tell(player, WOW_FATES_STR_REWARD_MAILED);
    }
}

// ---------------------------------------------------------------------------
// Announcements

void WowFatesExpeditionMgr::AnnounceZone(WowFatesActiveExpedition const& active, uint32 text)
{
    Map* map = sMapMgr->FindBaseNonInstanceMap(active.spawn.map);
    if (!map)
        return;

    uint32 const minutes = uint32((active.remaining + MINUTE * IN_MILLISECONDS - 1) / (MINUTE * IN_MILLISECONDS));
    for (auto const& ref : map->GetPlayers())
    {
        Player* player = ref.GetSource();
        if (!player || player->GetZoneId() != active.zone)
            continue;
        WorldSession* session = player->GetSession();
        Tell(player, text, Name(active.def, session), uint32(active.def->level), Objective(active, session), minutes);
    }
}

void WowFatesExpeditionMgr::AnnounceParticipants(WowFatesActiveExpedition const& active, uint32 text, uint32 a, uint32 b)
{
    std::unordered_set<ObjectGuid> concerned(active.inside.begin(), active.inside.end());
    for (auto const& [guid, contribution] : active.contributors)
        concerned.insert(guid);

    for (ObjectGuid const& guid : concerned)
        if (Player* player = ObjectAccessor::FindPlayer(guid))
            Tell(player, text, Name(active.def, player->GetSession()), a, b);
}

// ---------------------------------------------------------------------------
// The clients' map

// "set", id, map, x, y, radius, level, progress, count, seconds left, name,
// objective, tracker line, duration: the texts in the player's language.
std::string WowFatesExpeditionMgr::MapLine(WowFatesActiveExpedition const& active, WorldSession* session) const
{
    WowFatesExpeditionDef const* def = active.def;
    return Acore::StringFormat("set\t{}\t{}\t{:.1f}\t{:.1f}\t{:.1f}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}",
        def->id, active.spawn.map, active.spawn.x, active.spawn.y, def->radius, uint32(def->level),
        std::min(active.progress, def->objectiveCount), def->objectiveCount,
        uint32(std::max<int64>(active.remaining, 0) / IN_MILLISECONDS),
        Clean(Name(def, session)), Clean(Objective(active, session)), Clean(TrackerLabel(active, session)),
        def->duration);
}

// The objective as the tracker shows it, the count apart: "Kobold Miner slain".
std::string WowFatesExpeditionMgr::TrackerLabel(WowFatesActiveExpedition const& active, WorldSession* session) const
{
    WowFatesExpeditionDef const* def = active.def;
    if (!def->objectiveEntry)
        return WowFatesText(session, WOW_FATES_STR_TRACKER_KILL_ANY);

    std::string name;
    if (CreatureTemplate const* creature = sObjectMgr->GetCreatureTemplate(def->objectiveEntry))
        name = creature->Name;
    if (session)
        if (CreatureLocale const* locale = sObjectMgr->GetCreatureLocale(def->objectiveEntry))
            ObjectMgr::GetLocaleString(locale->Name, session->GetSessionDbLocaleIndex(), name);
    return Acore::StringFormat(WowFatesText(session, WOW_FATES_STR_TRACKER_KILL), name);
}

// None: nothing done. Light: a kill or any damage or healing. Medium and
// strong: damage and healing worth MediumShare and StrongShare of the average
// of those who did any (alone, a player is the average: strong).
WowFatesParticipation WowFatesExpeditionMgr::Participation(WowFatesActiveExpedition const& active, ObjectGuid guid) const
{
    auto contribution = active.contributors.find(guid);
    if (contribution == active.contributors.end())
        return WOW_FATES_PART_NONE;

    uint64 const value = contribution->second.damage + contribution->second.healing;
    if (!value)
        return contribution->second.kills ? WOW_FATES_PART_LIGHT : WOW_FATES_PART_NONE;

    uint64 total = 0;
    uint32 counted = 0;
    for (auto const& [other, c] : active.contributors)
        if (uint64 const v = c.damage + c.healing)
        {
            total += v;
            ++counted;
        }
    double const average = counted ? double(total) / double(counted) : 0.0;

    WowFatesConfig const& c = WowFatesConf();
    if (double(value) >= average * double(c.strongShare))
        return WOW_FATES_PART_STRONG;
    if (double(value) >= average * double(c.mediumShare))
        return WOW_FATES_PART_MEDIUM;
    return WOW_FATES_PART_LIGHT;
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

        if (areaTick)
        {
            Map* map = sMapMgr->FindBaseNonInstanceMap(active.spawn.map);
            UpdateArea(active, map);
            UpdateSummons(active, map, diff);

            if (active.progress != active.sentProgress && active.progress < active.def->objectiveCount)
            {
                active.sentProgress = active.progress;
                BroadcastSet(active);
            }
            SendStatus(active);
        }

        uint32 const count = active.def->objectiveCount;
        uint8 const quarter = uint8(std::min<uint32>(active.progress, count) * 4 / count);
        if (quarter > active.announcedQuarter)
        {
            active.announcedQuarter = quarter;
            if (quarter < 4)
                AnnounceParticipants(active, WOW_FATES_STR_PROGRESS, active.progress, count);
        }

        if (active.progress >= count)
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
                Tell(player, WOW_FATES_STR_ENTERED, Name(active.def, player->GetSession()), uint32(active.def->level),
                    Objective(active, player->GetSession()), active.progress, active.def->objectiveCount);
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
        Tell(player, WOW_FATES_STR_LEFT, Name(active.def, player->GetSession()));
        SendAddon(player, Acore::StringFormat("leave\t{}", active.def->id));
    }
    Leave(guid, true);
}

void WowFatesExpeditionMgr::Leave(ObjectGuid guid, bool announce)
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
        if (announce && WowFatesConf().syncAnnounce)
            Tell(player, WOW_FATES_STR_UNSYNCED);
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
            continue;
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

        bool const was = sWowFatesSync->IsSynced(player);
        float const ratio = sWowFatesSync->Apply(player, active->second.def->level);
        state.ratio = ratio;
        if (!was && ratio < 1.0f && c.syncAnnounce)
            Tell(player, WOW_FATES_STR_SYNCED, uint32(active->second.def->level), uint32(std::lround(ratio * 100.0f)));

        // The debuff's tooltip on the client: the level and the share kept.
        int32 const pct = int32(std::lround(ratio * 100.0f));
        if (pct != state.sentPct)
        {
            state.sentPct = pct;
            SendAddon(player, Acore::StringFormat("sync\t{}\t{}", pct < 100 ? uint32(active->second.def->level) : 0u, pct));
        }
    }
}

void WowFatesExpeditionMgr::UpdateSummons(WowFatesActiveExpedition& active, Map* map, uint32 /*diff*/)
{
    WowFatesExpeditionDef const* def = active.def;
    if (!def->summonEntry || !def->summonCount || !map)
        return;

    // The living only; a corpse goes away by itself.
    std::erase_if(active.summons, [map](ObjectGuid const& guid)
    {
        Creature* creature = map->GetCreature(guid);
        return !creature || !creature->IsAlive();
    });

    // Nobody near: the grid sleeps, no monster is brought.
    if (active.summons.size() >= def->summonCount || active.summonTimer > 0 || !map->IsGridLoaded(active.spawn.x, active.spawn.y))
        return;

    uint32 const wanted = active.filled ? 1 : def->summonCount - uint32(active.summons.size());
    for (uint32 i = 0; i < wanted; ++i)
    {
        float const angle = frand(0.0f, 2.0f * float(M_PI));
        float const dist = frand(0.0f, def->radius * 0.8f);
        float x = active.spawn.x + dist * std::cos(angle);
        float y = active.spawn.y + dist * std::sin(angle);
        float z = map->GetHeight(PHASEMASK_NORMAL, x, y, active.spawn.z + 10.0f, true, 50.0f);
        if (z <= INVALID_HEIGHT)
        {
            x = active.spawn.x;
            y = active.spawn.y;
            z = active.spawn.z;
        }

        Position const pos(x, y, z, frand(0.0f, 2.0f * float(M_PI)));
        TempSummon* summon = map->SummonCreature(def->summonEntry, pos);
        if (!summon)
            break;

        summon->SetTempSummonType(TEMPSUMMON_CORPSE_TIMED_DESPAWN);
        summon->SetTimer(SUMMON_CORPSE_MS);
        summon->SetWanderDistance(8.0f);
        summon->GetMotionMaster()->MoveRandom(8.0f);
        active.summons.push_back(summon->GetGUID());
    }

    active.filled = true;
    active.summonTimer = int64(def->summonRespawn) * IN_MILLISECONDS;
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
    if (!active || active->def->objectiveType != WOW_FATES_OBJECTIVE_KILL)
        return;
    if (active->def->objectiveEntry && creature->GetEntry() != active->def->objectiveEntry)
        return;

    if (active->progress < active->def->objectiveCount)
        ++active->progress;
    ++active->contributors[player->GetGUID()].kills;
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
            active.progress, active.def->objectiveCount, uint32(std::max<int64>(active.remaining, 0) / IN_MILLISECONDS),
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
