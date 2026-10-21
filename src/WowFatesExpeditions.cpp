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
#include "CreatureAI.h"
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
#include "Spell.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
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
        "`summon_entry`, `summon_count`, `summon_respawn`, `radius`, `duration`, `cooldown`, `weight`, "
        "`objective_text`, `tracker_text`, `wave_text` "
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
            def.duration = f[10].Get<uint32>();
            def.cooldown = f[11].Get<uint32>();
            def.weight = f[12].Get<uint32>();
            def.objectiveText = f[13].Get<std::string>();
            def.trackerText = f[14].Get<std::string>();
            def.waveText = f[15].Get<std::string>();

            if (def.objectiveType < WOW_FATES_OBJECTIVE_KILL || def.objectiveType > WOW_FATES_OBJECTIVE_BOSS_SUPPORT)
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
        "SELECT `expedition`, `boss_entry`, `boss_at`, `gather_entry`, `gather_count`, `gather_respawn`, "
        "`ally_entry`, `hold_time`, `wave_interval`, `wave_size`, `wave_growth`, `channel_entry`, `ally_count`, `wave_count` "
        "FROM `mod_wow_fates_objective`"))
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
            itr->second.allyEntry = f[6].Get<uint32>();
            itr->second.holdTime = std::max<uint32>(f[7].Get<uint32>(), 30);
            itr->second.waveInterval = std::max<uint32>(f[8].Get<uint32>(), 5);
            itr->second.waveSize = std::max<uint32>(f[9].Get<uint32>(), 1);
            itr->second.waveGrowth = f[10].Get<uint32>();
            itr->second.channelEntry = f[11].Get<uint32>();
            itr->second.allyCount = std::clamp<uint32>(f[12].Get<uint32>(), 1, 10);
            itr->second.waveCount = std::max<uint32>(f[13].Get<uint32>(), 1);
        } while (result->NextRow());
    }

    for (auto itr = _defs.begin(); itr != _defs.end();)
    {
        WowFatesExpeditionDef const& def = itr->second;
        if ((def.objectiveType == WOW_FATES_OBJECTIVE_ASSAULT || def.objectiveType == WOW_FATES_OBJECTIVE_BOSS_SUPPORT)
            && !sObjectMgr->GetCreatureTemplate(def.bossEntry))
        {
            LOG_ERROR("module", "mod-wow-fates: Expedition {} is an assault whose boss, creature {}, does not exist; skipped.", def.id, def.bossEntry);
            itr = _defs.erase(itr);
        }
        else if (def.objectiveType == WOW_FATES_OBJECTIVE_BOSS_SUPPORT && !sObjectMgr->GetCreatureTemplate(def.allyEntry))
        {
            LOG_ERROR("module", "mod-wow-fates: Expedition {} is a boss support whose ally, creature {}, does not exist; skipped.", def.id, def.allyEntry);
            itr = _defs.erase(itr);
        }
        else if ((def.objectiveType == WOW_FATES_OBJECTIVE_DEFENCE || def.objectiveType == WOW_FATES_OBJECTIVE_SUPPORT)
            && (!sObjectMgr->GetCreatureTemplate(def.allyEntry) || !sObjectMgr->GetCreatureTemplate(def.summonEntry)))
        {
            LOG_ERROR("module", "mod-wow-fates: Expedition {} is a defence whose ally (creature {}) or monsters (summon_entry {}) do not exist; skipped.",
                def.id, def.allyEntry, def.summonEntry);
            itr = _defs.erase(itr);
        }
        else if (def.objectiveType == WOW_FATES_OBJECTIVE_INVOKE
            && (!sObjectMgr->GetGameObjectTemplate(def.channelEntry) || !sObjectMgr->GetCreatureTemplate(def.summonEntry)))
        {
            LOG_ERROR("module", "mod-wow-fates: Expedition {} is an invocation whose altar (gameobject {}) or monsters (summon_entry {}) do not exist; skipped.",
                def.id, def.channelEntry, def.summonEntry);
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

    if (QueryResult result = WorldDatabase.Query(
        "SELECT `id`, `locale`, `name`, `objective_text`, `tracker_text`, `wave_text` FROM `mod_wow_fates_expedition_locale`"))
    {
        do
        {
            Field* f = result->Fetch();
            auto itr = _defs.find(f[0].Get<uint32>());
            if (itr == _defs.end())
                continue;
            std::string const locale = f[1].Get<std::string>();
            itr->second.names[locale] = f[2].Get<std::string>();
            itr->second.objectiveTexts[locale] = f[3].Get<std::string>();
            itr->second.trackerTexts[locale] = f[4].Get<std::string>();
            itr->second.waveTexts[locale] = f[5].Get<std::string>();
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

            spawn.zone = sMapMgr->GetZoneId(PHASEMASK_NORMAL, spawn.map, spawn.x, spawn.y, spawn.z);
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

// A text of an Expedition in the player's language, else the English one.
std::string WowFatesExpeditionMgr::Localized(std::string const& enUS, std::unordered_map<std::string, std::string> const& texts, WorldSession* session)
{
    if (session)
    {
        LocaleConstant const locale = session->GetSessionDbLocaleIndex();
        if (locale != LOCALE_enUS && locale < TOTAL_LOCALES)
        {
            auto itr = texts.find(localeNames[locale]);
            if (itr != texts.end() && !itr->second.empty())
                return itr->second;
        }
    }
    return enUS;
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
    if (!def->objectiveText.empty())
        return Localized(def->objectiveText, def->objectiveTexts, session);
    if (def->objectiveType == WOW_FATES_OBJECTIVE_GATHER)
        return Acore::StringFormat(WowFatesText(session, WOW_FATES_STR_OBJECTIVE_GATHER), GameObjectName(def->gatherEntry, session));
    if (def->objectiveType == WOW_FATES_OBJECTIVE_DEFENCE)
        return Acore::StringFormat(WowFatesText(session, WOW_FATES_STR_OBJECTIVE_DEFENCE), CreatureName(def->allyEntry, session),
            (def->holdTime + 59) / 60);
    if (def->objectiveType == WOW_FATES_OBJECTIVE_INVOKE)
        return Acore::StringFormat(WowFatesText(session, WOW_FATES_STR_OBJECTIVE_INVOKE), GameObjectName(def->channelEntry, session));
    if (def->objectiveType == WOW_FATES_OBJECTIVE_BOSS_SUPPORT)
        return Acore::StringFormat(WowFatesText(session, WOW_FATES_STR_OBJECTIVE_KILL), CreatureName(def->bossEntry, session));
    if (def->objectiveType == WOW_FATES_OBJECTIVE_SUPPORT)
        return Acore::StringFormat(WowFatesText(session, WOW_FATES_STR_OBJECTIVE_SUPPORT), CreatureName(def->allyEntry, session), def->waveCount);
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
    active.zone = spawn->zone;
    active.remaining = int64(IdleExpire(def)) * IN_MILLISECONDS;
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

uint32 WowFatesExpeditionMgr::IdleExpire(WowFatesExpeditionDef const* def) const
{
    return def->duration ? std::max<uint32>(def->duration, 60) : WowFatesConf().idleExpire;
}

void WowFatesExpeditionMgr::Finish(WowFatesActiveExpedition& active, bool success, bool expired)
{
    WowFatesConfig const& c = WowFatesConf();
    Map* map = sMapMgr->FindBaseNonInstanceMap(active.spawn.map);

    LOG_INFO("module", "mod-wow-fates: Expedition {} ends, {} ({} %, {} kills, {} contributors).",
        active.def->id, success ? "complete" : (expired ? "gone for lack of activity" : "failed"),
        uint32(active.progressPct), active.progress, active.contributors.size());

    BroadcastDel(active.def->id);

    // Everyone concerned hears the end: those inside and those who helped.
    std::unordered_set<ObjectGuid> concerned(active.inside.begin(), active.inside.end());
    for (auto const& [guid, contribution] : active.contributors)
        concerned.insert(guid);

    for (ObjectGuid const& guid : concerned)
    {
        Player* player = ObjectAccessor::FindPlayer(guid);
        if (!player || expired)
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
    // The allies are never left behind, their corpses after a while.
    if (map)
    {
        std::vector<ObjectGuid> allies(active.allies);
        if (active.ally)
            allies.push_back(active.ally);
        for (ObjectGuid const& guid : allies)
            if (Creature* ally = map->GetCreature(guid))
            {
                ally->CombatStop(true);
                ally->DespawnOrUnsummon(ally->IsAlive() ? 0ms : Milliseconds(SUMMON_CORPSE_MS));
            }
        if (active.altar)
            if (GameObject* altar = map->GetGameObject(active.altar))
                altar->DespawnOrUnsummon();
        if (active.focus)
            if (Creature* focus = map->GetCreature(active.focus))
                focus->DespawnOrUnsummon();
    }
    for (auto const& [guid, end] : active.channelers)
        if (Player* player = ObjectAccessor::FindPlayer(guid))
            if (player->IsInWorld())
                player->InterruptSpell(CURRENT_CHANNELED_SPELL);
    active.channelers.clear();
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
// objective, tracker line, seconds it waits for players, type of objective
// (named in the map's tooltip), 1 when players are at it, the tracker's
// second line (empty but for a defence or a wave support): the texts in the
// player's language. The seconds left are not shown, only "expires soon" under three
// minutes; while players are at it, they do not run.
std::string WowFatesExpeditionMgr::MapLine(WowFatesActiveExpedition const& active, WorldSession* session) const
{
    WowFatesExpeditionDef const* def = active.def;
    return Acore::StringFormat("set\t{}\t{}\t{:.1f}\t{:.1f}\t{:.1f}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}",
        def->id, active.spawn.map, active.spawn.x, active.spawn.y, def->radius, uint32(def->level),
        std::min<uint32>(uint32(active.progressPct), 100), 100u,
        uint32(std::max<int64>(active.remaining, 0) / IN_MILLISECONDS),
        Clean(Name(def, session)), Clean(Objective(active, session)), Clean(TrackerLabel(active, session)),
        IdleExpire(def), uint32(def->objectiveType), active.inProgress ? 1 : 0, Clean(TrackerGuard(active, session)));
}

// The objective as the tracker shows it, the count apart: "Kobold Miner slain";
// once the leader of an assault is out, "Defias Ringleader slain".
std::string WowFatesExpeditionMgr::TrackerLabel(WowFatesActiveExpedition const& active, WorldSession* session) const
{
    WowFatesExpeditionDef const* def = active.def;
    // the leader of an assault, once out, by its name; else the Expedition's own words
    if (!def->trackerText.empty() && !(def->objectiveType == WOW_FATES_OBJECTIVE_ASSAULT && active.bossOut))
        return Acore::StringFormat(Localized(def->trackerText, def->trackerTexts, session), active.repelled, def->waveCount);
    if (def->objectiveType == WOW_FATES_OBJECTIVE_GATHER)
        return Acore::StringFormat(WowFatesText(session, WOW_FATES_STR_TRACKER_GATHER), GameObjectName(def->gatherEntry, session));
    if (def->objectiveType == WOW_FATES_OBJECTIVE_DEFENCE || def->objectiveType == WOW_FATES_OBJECTIVE_BOSS_SUPPORT)
        return Acore::StringFormat(WowFatesText(session, WOW_FATES_STR_TRACKER_DEFENCE), CreatureName(def->allyEntry, session));
    if (def->objectiveType == WOW_FATES_OBJECTIVE_INVOKE)
        return Acore::StringFormat(WowFatesText(session, WOW_FATES_STR_TRACKER_INVOKE), GameObjectName(def->channelEntry, session));
    if (def->objectiveType == WOW_FATES_OBJECTIVE_SUPPORT)
        return Acore::StringFormat(WowFatesText(session, WOW_FATES_STR_TRACKER_SUPPORT), active.repelled, def->waveCount);
    if (def->objectiveType == WOW_FATES_OBJECTIVE_ASSAULT && active.bossOut)
        return Acore::StringFormat(WowFatesText(session, WOW_FATES_STR_TRACKER_KILL), CreatureName(def->bossEntry, session));
    if (!def->objectiveEntry)
        return WowFatesText(session, WOW_FATES_STR_TRACKER_KILL_ANY);
    return Acore::StringFormat(WowFatesText(session, WOW_FATES_STR_TRACKER_KILL), CreatureName(def->objectiveEntry, session));
}

// The tracker's second line: what there is to protect. A wave support: its
// allies alive out of ally_count; a defence: its ally's health.
std::string WowFatesExpeditionMgr::TrackerGuard(WowFatesActiveExpedition const& active, WorldSession* session) const
{
    WowFatesExpeditionDef const* def = active.def;
    if (def->objectiveType == WOW_FATES_OBJECTIVE_SUPPORT && active.alliesAlive >= 0)
        return Acore::StringFormat(WowFatesText(session, WOW_FATES_STR_ALLIES_ALIVE), CreatureName(def->allyEntry, session),
            uint32(active.alliesAlive), def->allyCount);
    if ((def->objectiveType == WOW_FATES_OBJECTIVE_DEFENCE || def->objectiveType == WOW_FATES_OBJECTIVE_BOSS_SUPPORT)
        && active.allyHealthPct >= 0)
        return Acore::StringFormat(WowFatesText(session, WOW_FATES_STR_ALLY_HEALTH), CreatureName(def->allyEntry, session),
            uint32(active.allyHealthPct));
    return "";
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
        if (uint32 const actions = o.kills + o.picks + uint32(o.channel / 5000))
        {
            totalKills += actions;
            ++withKills;
        }
    }

    WowFatesContribution const& own = contribution->second;
    WowFatesParticipation const byValue = tier(double(own.damage + own.healing),
        withValue ? double(totalValue) / double(withValue) : 0.0);
    WowFatesParticipation const byKills = tier(double(own.kills + own.picks + uint32(own.channel / 5000)),
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

    _areaTimer -= diff;
    bool const areaTick = _areaTimer <= 0;
    if (areaTick)
    {
        _areaTimer = c.areaInterval;

        // The zones with players, those of the continents only. A bot (a
        // session with no client behind it) does not count, unless CountBots:
        // it may still take part, and its activity keeps an Expedition alive.
        _populated.clear();
        for (auto const& [guid, player] : ObjectAccessor::GetPlayers())
            if (player && player->IsInWorld() && player->GetInstanceId() == 0
                && (c.countBots || !player->GetSession()->IsSocketClosed()))
                _populated.insert(player->GetZoneId());
    }

    // A zone with players and no Expedition gets one at once; one more every
    // spawn interval, up to MaxPerZone.
    _rotateTimer -= diff;
    bool const fill = _rotateTimer <= 0;
    if (fill)
        _rotateTimer = c.spawnInterval;
    if (areaTick || fill)
        Rotate(fill);

    std::vector<std::pair<uint32, bool>> ended;
    std::vector<uint32> expired;
    for (auto& [id, active] : _active)
    {
        // The wait for players: it runs only when nobody is at it, and starts
        // again with each activity.
        active.elapsed += diff;
        bool const recent = active.elapsed - active.lastActivity < 120 * IN_MILLISECONDS && active.lastActivity > 0;
        bool const inProgress = recent && !active.inside.empty();
        if (recent)
            active.remaining = int64(IdleExpire(active.def)) * IN_MILLISECONDS;
        else
            active.remaining -= diff;
        if (inProgress != active.inProgress)
        {
            active.inProgress = inProgress;
            BroadcastSet(active);
        }
        active.summonTimer -= diff;
        active.gatherTimer -= diff;
        if (active.def->objectiveType == WOW_FATES_OBJECTIVE_DEFENCE)
            UpdateDefence(active, sMapMgr->FindBaseNonInstanceMap(active.spawn.map), diff);
        else if (active.def->objectiveType == WOW_FATES_OBJECTIVE_INVOKE)
            UpdateInvoke(active, sMapMgr->FindBaseNonInstanceMap(active.spawn.map), diff);
        else if (active.def->objectiveType == WOW_FATES_OBJECTIVE_SUPPORT)
            UpdateSupport(active, sMapMgr->FindBaseNonInstanceMap(active.spawn.map), diff);
        else if (active.def->objectiveType == WOW_FATES_OBJECTIVE_BOSS_SUPPORT)
            UpdateBossSupport(active, sMapMgr->FindBaseNonInstanceMap(active.spawn.map), diff);

        if (areaTick)
        {
            Map* map = sMapMgr->FindBaseNonInstanceMap(active.spawn.map);
            UpdateArea(active, map);
            UpdateSummons(active, map, diff);
            // when the wave on its way was all killed, for the pace of the next
            if (map && !active.waveMobs.empty() && active.waveClearedAt < 0
                && std::none_of(active.waveMobs.begin(), active.waveMobs.end(), [map](ObjectGuid const& guid)
                {
                    Creature* creature = map->GetCreature(guid);
                    return creature && creature->IsAlive();
                }))
                active.waveClearedAt = active.elapsed;

            if (active.def->objectiveType == WOW_FATES_OBJECTIVE_ASSAULT)
                UpdateBoss(active, map);
            else if (active.def->objectiveType == WOW_FATES_OBJECTIVE_GATHER)
                UpdateGather(active, map);

            uint32 const pct = uint32(active.progressPct);
            int32 const guarded = active.alliesAlive * 1000 + active.allyHealthPct;
            if ((pct != active.sentProgress && pct < 100) || guarded != active.sentGuard)
            {
                active.sentProgress = pct;
                active.sentGuard = guarded;
                BroadcastSet(active);
            }
            SendStatus(active);
        }

        if (active.progressPct >= 100.0)
            ended.emplace_back(id, true);
        else if (active.failed)
            ended.emplace_back(id, false);
        else if (active.remaining <= 0)
            expired.push_back(id);
    }

    for (auto const& [id, success] : ended)
    {
        auto itr = _active.find(id);
        Finish(itr->second, success);
        _active.erase(itr);
    }
    for (uint32 id : expired)
    {
        // gone for lack of activity: another takes its place, if the zone has players
        auto itr = _active.find(id);
        uint32 const zone = itr->second.zone;
        Finish(itr->second, false, true);
        _active.erase(itr);
        if (_populated.count(zone))
            StartInZone(zone, true);
    }
    // the zones left without one get another at once
    if (!ended.empty() || !expired.empty())
        Rotate(false);

    UpdateSync(diff);
}

// Each zone with players: one Expedition at least, at once, cooldown or not;
// with fill, one more up to MaxPerZone. The zones without players get none.
void WowFatesExpeditionMgr::Rotate(bool fill)
{
    WowFatesConfig const& c = WowFatesConf();
    if (!c.maxPerZone)
        return;

    for (uint32 zone : _populated)
    {
        uint32 const count = uint32(std::count_if(_active.begin(), _active.end(), [zone](auto const& pair)
        {
            return pair.second.zone == zone;
        }));
        if (!count)
            StartInZone(zone, true);
        else if (fill && count < c.maxPerZone)
            StartInZone(zone, false);
    }
}

// One of the zone's Expeditions, picked at random by weight among those out
// of cooldown (forced: in cooldown too), at one of its points in the zone
// whose area does not cross an active one's.
bool WowFatesExpeditionMgr::StartInZone(uint32 zone, bool force)
{
    auto freePoints = [this, zone](WowFatesExpeditionDef const& def)
    {
        std::vector<WowFatesSpawnPoint const*> points;
        for (uint32 spawnId : def.spawns)
        {
            WowFatesSpawnPoint const& spawn = _spawns[spawnId];
            if (spawn.zone != zone)
                continue;
            bool const crosses = std::any_of(_active.begin(), _active.end(), [&spawn, &def](auto const& pair)
            {
                WowFatesActiveExpedition const& other = pair.second;
                return other.spawn.map == spawn.map
                    && std::hypot(other.spawn.x - spawn.x, other.spawn.y - spawn.y) < other.def->radius + def.radius;
            });
            if (!crosses)
                points.push_back(&spawn);
        }
        return points;
    };

    for (bool const ignoreCooldown : { false, true })
    {
        if (ignoreCooldown && !force)
            return false;

        std::vector<WowFatesExpeditionDef const*> candidates;
        uint64 totalWeight = 0;
        for (auto const& [id, def] : _defs)
            if (def.weight && !_active.count(id) && (ignoreCooldown || !_cooldowns.count(id)) && !freePoints(def).empty())
            {
                candidates.push_back(&def);
                totalWeight += def.weight;
            }
        if (candidates.empty())
            continue;

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

        StartLocked(chosen, Acore::Containers::SelectRandomContainerElement(freePoints(*chosen)));
        return true;
    }
    return false;
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
    int64 const now = active.elapsed;
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

    ScaleHealth(boss, active.bossHealth, active.bossPlayers, uint32(std::max<std::size_t>(active.inside.size(), 1)));

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
    if (active.def->objectiveType == WOW_FATES_OBJECTIVE_ASSAULT)
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
// the area, as a kill does; it goes, another comes back in its time. Only at
// the end of an opening cast: a click straight on it (a client whose cache
// still holds the object without its lock) starts that cast instead.
void WowFatesExpeditionMgr::OnGather(Player* player, GameObject* go)
{
    Spell const* opening = player->GetCurrentSpell(CURRENT_GENERIC_SPELL);
    if (!opening || !opening->m_spellInfo->HasEffect(SPELL_EFFECT_OPEN_LOCK))
    {
        player->CastSpell(go, WOW_FATES_GATHER_SPELL, false);
        return;
    }

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
        Active(active);
        if (active.gatherTimer <= 0)
            active.gatherTimer = int64(active.def->gatherRespawn) * IN_MILLISECONDS;
        return;
    }

    // left over from an Expedition that ended
    go->DespawnOrUnsummon();
}

// ---------------------------------------------------------------------------
// Defence, and the waves

// The health of a creature for the players in the area, as the objective
// grows: never lowered, its share of health kept.
void WowFatesExpeditionMgr::ScaleHealth(Creature* creature, uint32 baseHealth, uint32& setFor, uint32 players) const
{
    if (players <= setFor)
        return;
    float const pct = creature->GetHealthPct();
    uint32 const health = uint32(float(baseHealth) * (1.0f + WowFatesConf().participantScaling * float(players - 1)));
    creature->SetCreateHealth(health);
    creature->SetStatFlatModifier(UNIT_MOD_HEALTH, BASE_VALUE, float(health));
    creature->UpdateMaxHealth();
    creature->SetHealth(std::max<uint32>(1, uint32(float(creature->GetMaxHealth()) * pct / 100.0f)));
    setFor = players;
}

// The centre of the area on the ground, or a point a little around it.
Position WowFatesExpeditionMgr::Centre(WowFatesActiveExpedition const& active, Map* map, float spread) const
{
    WowFatesSpawnPoint const& c = active.spawn;
    float const angle = frand(0.0f, 2.0f * float(M_PI));
    float const x = c.x + spread * std::cos(angle);
    float const y = c.y + spread * std::sin(angle);
    float z = map->GetHeight(PHASEMASK_NORMAL, x, y, c.z + 5.0f, true, 20.0f);
    if (z <= INVALID_HEIGHT)
        return Position(c.x, c.y, c.z, c.o);
    return Position(x, y, z, c.o);
}

Creature* WowFatesExpeditionMgr::SpawnAlly(WowFatesActiveExpedition& active, Map* map, Position const& pos)
{
    TempSummon* ally = map->SummonCreature(active.def->allyEntry, pos);
    if (!ally)
        return nullptr;

    ally->SetTempSummonType(TEMPSUMMON_MANUAL_DESPAWN);
    // Its faction (an escortee's, friend of all players, foe of monsters) is
    // flagged for PvP: healing it would flag the healer.
    ally->SetPvP(false);
    ally->SetHomePosition(pos);
    ally->SetDefaultMovementType(IDLE_MOTION_TYPE);
    ally->GetMotionMaster()->Initialize();
    active.allyHealth = ally->GetMaxHealth();
    return ally;
}

// The first player in the area starts a defence, an invocation or a wave
// support: its clock and its waves (the first one at once), with a banner.
bool WowFatesExpeditionMgr::Start(WowFatesActiveExpedition& active, char const* banner,
    std::string (WowFatesExpeditionMgr::*name)(uint32, WorldSession*) const, uint32 entry)
{
    if (active.started)
        return true;
    if (active.inside.empty())
        return false;
    active.started = true;
    active.waveTimer = 0;
    for (ObjectGuid const& guid : active.inside)
        if (Player* player = ObjectAccessor::FindPlayer(guid))
            SendAddon(player, Acore::StringFormat("{}\t{}\t{}", banner, active.def->id,
                Clean((this->*name)(entry, player->GetSession()))));
    return true;
}

// The ally waits in the centre; the first player who comes starts the clock
// and the waves (the first one at once). Its health grows with the players
// in the area, as the waves do. Gone alive (a grid unloaded), it comes back;
// dead, the Expedition fails.
void WowFatesExpeditionMgr::UpdateDefence(WowFatesActiveExpedition& active, Map* map, uint32 diff)
{
    if (!map || active.failed || active.progressPct >= 100.0)
        return;

    Creature* ally = active.ally ? map->GetCreature(active.ally) : nullptr;
    if (ally && !ally->IsAlive())
    {
        active.allyHealthPct = 0;
        LOG_INFO("module", "mod-wow-fates: Expedition {}: its ally died after {} s and {} waves.",
            active.def->id, active.held / IN_MILLISECONDS, active.wave);
        active.failed = true;
        return;
    }
    if (!ally)
    {
        if (!map->IsGridLoaded(active.spawn.x, active.spawn.y))
            return;
        ally = SpawnAlly(active, map, Centre(active, map));
        if (!ally)
            return;
        active.ally = ally->GetGUID();
        active.allyPlayers = 1;
    }

    uint32 const players = uint32(std::max<std::size_t>(active.inside.size(), 1));
    ScaleHealth(ally, active.allyHealth, active.allyPlayers, players);
    active.allyHealthPct = std::max<int32>(5, int32(std::ceil(ally->GetHealthPct() / 5.0f)) * 5);

    if (!Start(active, "defend", &WowFatesExpeditionMgr::CreatureName, active.def->allyEntry))
        return;

    active.held += diff;
    active.progressPct = std::min(100.0, 100.0 * double(active.held) / double(int64(active.def->holdTime) * IN_MILLISECONDS));

    active.waveTimer -= diff;
    if (active.waveTimer <= 0 && active.progressPct < 100.0 && map->IsGridLoaded(active.spawn.x, active.spawn.y))
    {
        active.waveTimer = int64(active.def->waveInterval) * IN_MILLISECONDS;
        SendWave(active, map, ally);
    }
}

// A point of the edge of the area near an angle, for a wave to come from: the
// ground there, seen from the centre and not much higher or lower, as the
// monsters' points; a few angles around it, else none.
bool WowFatesExpeditionMgr::EdgePoint(WowFatesActiveExpedition const& active, Map* map, float angle, Position& out) const
{
    WowFatesSpawnPoint const& c = active.spawn;
    for (uint32 attempt = 0; attempt < 8; ++attempt)
    {
        float const a = angle + (attempt ? frand(-0.35f, 0.35f) : 0.0f);
        float const dist = active.def->radius * frand(0.8f, 0.95f);
        float const x = c.x + dist * std::cos(a);
        float const y = c.y + dist * std::sin(a);
        float const z = map->GetHeight(PHASEMASK_NORMAL, x, y, c.z + 40.0f, true, 80.0f);
        if (z <= INVALID_HEIGHT || std::fabs(z - c.z) > 25.0f)
            continue;
        if (!map->isInLineOfSight(c.x, c.y, c.z + 2.0f, x, y, z + 2.0f, PHASEMASK_NORMAL,
                LINEOFSIGHT_ALL_CHECKS, VMAP::ModelIgnoreFlags::Nothing))
            continue;
        out.Relocate(x, y, z, std::atan2(c.y - y, c.x - x));
        return true;
    }
    return false;
}

// A wave: wave_size monsters for each character in the area, players and bots
// alike (WowFates.Waves.PerCharacter times), times the pace of the kills, and
// wave_growth more for each wave already sent. The pace: the last wave killed
// within PaceFast of the interval raises it by PaceStep, PaceMax at most;
// killed slower, or not all killed, it goes PaceStep back toward 1. It comes from two to four points of the edge, at angles
// drawn again for each wave and at least a quarter turn over their number
// apart, and runs at the target (or the centre, when there is none). Back
// from a fight with the players, they go back to it.
void WowFatesExpeditionMgr::SendWave(WowFatesActiveExpedition& active, Map* map, Unit* target)
{
    WowFatesExpeditionDef const* def = active.def;
    std::erase_if(active.summons, [map](ObjectGuid const& guid)
    {
        return !map->GetCreature(guid);
    });

    WowFatesConfig const& c = WowFatesConf();
    if (active.wave)
    {
        bool const fast = active.waveClearedAt >= 0
            && active.waveClearedAt - active.waveSentAt <= int64(c.wavePaceFast * float(def->waveInterval) * IN_MILLISECONDS);
        active.pace = fast ? std::min(c.wavePaceMax, active.pace + c.wavePaceStep)
            : std::max(1.0f, active.pace - c.wavePaceStep);
    }

    uint32 const characters = uint32(std::max<std::size_t>(active.inside.size(), 1));
    uint32 const count = std::max<uint32>(1, uint32(std::lround(c.wavePerCharacter * float(def->waveSize) * float(characters) * active.pace))
        + def->waveGrowth * active.wave);
    ++active.wave;
    active.waveMobs.clear();
    active.waveSentAt = active.elapsed;
    active.waveClearedAt = -1;

    // the points of entry: evenly around, each moved by up to a quarter of the
    // gap, so that two are always at least half a gap apart
    uint32 const sides = std::clamp<uint32>((count + 2) / 3, std::min<uint32>(count, 2), 4);
    float const gap = 2.0f * float(M_PI) / float(sides);
    float const start = frand(0.0f, 2.0f * float(M_PI));
    std::vector<Position> entries;
    for (uint32 i = 0; i < sides; ++i)
    {
        Position pos;
        if (EdgePoint(active, map, start + gap * float(i) + frand(-0.25f, 0.25f) * gap, pos))
            entries.push_back(pos);
    }
    if (entries.empty())
        entries.push_back(SpawnPoint(active, map));

    WowFatesSpawnPoint const& centre = active.spawn;
    Position const goal = target ? target->GetPosition() : Centre(active, map);
    for (uint32 i = 0; i < count; ++i)
    {
        Position const& entry = entries[i % entries.size()];
        float const x = entry.GetPositionX() + frand(-3.0f, 3.0f);
        float const y = entry.GetPositionY() + frand(-3.0f, 3.0f);
        float z = map->GetHeight(PHASEMASK_NORMAL, x, y, entry.GetPositionZ() + 5.0f, true, 20.0f);
        if (z <= INVALID_HEIGHT)
            z = entry.GetPositionZ();
        Position const pos(x, y, z, std::atan2(centre.y - y, centre.x - x));

        TempSummon* summon = map->SummonCreature(def->summonEntry, pos);
        if (!summon)
            break;
        summon->SetTempSummonType(TEMPSUMMON_CORPSE_TIMED_DESPAWN);
        summon->SetTimer(SUMMON_CORPSE_MS);
        summon->SetHomePosition(goal.GetPositionX() + frand(-4.0f, 4.0f), goal.GetPositionY() + frand(-4.0f, 4.0f),
            goal.GetPositionZ(), goal.GetOrientation());
        summon->SetReactState(REACT_AGGRESSIVE);
        if (target && summon->AI())
            summon->AI()->AttackStart(target);
        else
            summon->GetMotionMaster()->MovePoint(0, goal.GetPositionX() + frand(-4.0f, 4.0f),
                goal.GetPositionY() + frand(-4.0f, 4.0f), goal.GetPositionZ());
        active.summons.push_back(summon->GetGUID());
        active.waveMobs.push_back(summon->GetGUID());
    }

    LOG_INFO("module", "mod-wow-fates: Expedition {}: wave {}, {} monsters for {} characters at pace {:.2f}, from {} sides.",
        def->id, active.wave, count, characters, active.pace, entries.size());
    for (ObjectGuid const& guid : active.inside)
        if (Player* player = ObjectAccessor::FindPlayer(guid))
            SendAddon(player, Acore::StringFormat("wave\t{}\t{}\t{}", def->id, active.wave,
                Clean(Localized(def->waveText, def->waveTexts, player->GetSession()))));
}

// ---------------------------------------------------------------------------
// Invocation

// The altar in the centre; those who channel at it (the channel of
// WOW_FATES_CHANNEL_SPELL, within WOW_FATES_CHANNEL_RANGE of it) fill the bar,
// each as much, against a time that grows with the players in the area, as
// the objective does; the bar never goes back. The waves run at one of them,
// at the altar when nobody channels.
void WowFatesExpeditionMgr::UpdateInvoke(WowFatesActiveExpedition& active, Map* map, uint32 diff)
{
    if (!map || active.progressPct >= 100.0)
        return;

    GameObject* altar = active.altar ? map->GetGameObject(active.altar) : nullptr;
    if (!altar || !altar->isSpawned())
    {
        if (!map->IsGridLoaded(active.spawn.x, active.spawn.y))
            return;
        Position const pos = Centre(active, map);
        altar = new GameObject();
        if (!altar->Create(map->GenerateLowGuid<HighGuid::GameObject>(), active.def->channelEntry, map, PHASEMASK_NORMAL,
                pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ(), pos.GetOrientation(), G3D::Quat(), 100, GO_STATE_READY))
        {
            delete altar;
            return;
        }
        altar->SetRespawnTime(0);
        altar->SetSpellId(1);
        altar->SetSpawnedByDefault(false);
        map->AddToMap(altar);
        active.altar = altar->GetGUID();
    }

    // the focus of the channels' beams, a little above the altar
    Creature* focus = active.focus ? map->GetCreature(active.focus) : nullptr;
    if (!focus)
    {
        Position const pos(altar->GetPositionX(), altar->GetPositionY(), altar->GetPositionZ() + 1.0f, altar->GetOrientation());
        if (TempSummon* summon = map->SummonCreature(WOW_FATES_ALTAR_FOCUS, pos))
        {
            summon->SetTempSummonType(TEMPSUMMON_MANUAL_DESPAWN);
            summon->SetDisableGravity(true);
            focus = summon;
            active.focus = summon->GetGUID();
        }
    }

    if (!Start(active, "invoke", &WowFatesExpeditionMgr::GameObjectName, active.def->channelEntry))
        return;

    // A channel broken (a blow, a move, a cast) or the player gone from the
    // altar: no more progress from him, until he clicks again. A channel that
    // ran its thirty seconds goes on by itself, a new one at once.
    std::vector<Player*> channeling;
    for (auto itr = active.channelers.begin(); itr != active.channelers.end();)
    {
        Player* player = ObjectAccessor::FindPlayer(itr->first);
        Spell const* spell = player && player->IsInWorld() ? player->GetCurrentSpell(CURRENT_CHANNELED_SPELL) : nullptr;
        bool const ours = spell && spell->m_spellInfo->Id == WOW_FATES_CHANNEL_SPELL;
        bool const there = player && player->IsAlive() && player->GetMap() == map
            && player->GetExactDist(altar) <= WOW_FATES_CHANNEL_RANGE + 1.0f;
        if (!there)
        {
            if (ours)
                player->InterruptSpell(CURRENT_CHANNELED_SPELL);
            itr = active.channelers.erase(itr);
            continue;
        }
        if (!ours)
        {
            bool const ranOut = active.elapsed >= itr->second - 500;
            if (!ranOut || !focus || player->CastSpell(focus, WOW_FATES_CHANNEL_SPELL, false) != SPELL_CAST_OK)
            {
                itr = active.channelers.erase(itr);
                continue;
            }
            itr->second = active.elapsed + ChannelDuration();
        }
        channeling.push_back(player);
        active.contributors[player->GetGUID()].channel += diff;
        Active(active);
        ++itr;
    }

    if (!channeling.empty())
    {
        uint32 const players = uint32(std::max<std::size_t>(active.inside.size(), 1));
        double const needed = double(active.def->holdTime) * IN_MILLISECONDS * (1.0 + WowFatesConf().participantScaling * double(players - 1));
        active.held += int64(diff) * int64(channeling.size());
        active.progressPct = std::min(100.0, 100.0 * double(active.held) / std::max(needed, 1.0));
    }

    active.waveTimer -= diff;
    if (active.waveTimer <= 0 && active.progressPct < 100.0 && map->IsGridLoaded(active.spawn.x, active.spawn.y))
    {
        active.waveTimer = int64(active.def->waveInterval) * IN_MILLISECONDS;
        SendWave(active, map, channeling.empty() ? nullptr : Acore::Containers::SelectRandomContainerElement(channeling));
    }
}

// The channel's own length, thirty seconds by its row.
int64 WowFatesExpeditionMgr::ChannelDuration()
{
    SpellInfo const* info = sSpellMgr->GetSpellInfo(WOW_FATES_CHANNEL_SPELL);
    return info && info->GetDuration() > 0 ? int64(info->GetDuration()) : int64(30 * IN_MILLISECONDS);
}

// A player uses the altar: near enough, he channels at its focus.
void WowFatesExpeditionMgr::OnChannel(Player* player, GameObject* go)
{
    std::lock_guard<std::recursive_mutex> guard(_lock);

    for (auto& [id, active] : _active)
    {
        if (active.altar != go->GetGUID())
            continue;
        if (active.progressPct >= 100.0 || !player->IsAlive() || player->GetExactDist(go) > WOW_FATES_CHANNEL_RANGE)
            return;
        Creature* focus = active.focus ? go->GetMap()->GetCreature(active.focus) : nullptr;
        if (!focus || player->CastSpell(focus, WOW_FATES_CHANNEL_SPELL, false) != SPELL_CAST_OK)
            return;
        active.channelers[player->GetGUID()] = active.elapsed + ChannelDuration();
        return;
    }
}

// ---------------------------------------------------------------------------
// Wave support

// ally_count allies around the centre; the first player starts the waves.
// A wave is pushed back when all its monsters are dead; the next one comes
// wave_interval seconds later. The bar is the waves pushed back out of
// wave_count; when every ally is dead, the Expedition fails.
void WowFatesExpeditionMgr::UpdateSupport(WowFatesActiveExpedition& active, Map* map, uint32 diff)
{
    if (!map || active.failed || active.progressPct >= 100.0)
        return;
    WowFatesExpeditionDef const* def = active.def;

    if (active.allies.empty())
    {
        if (!map->IsGridLoaded(active.spawn.x, active.spawn.y))
            return;
        for (uint32 i = 0; i < def->allyCount; ++i)
            if (Creature* ally = SpawnAlly(active, map, Centre(active, map, def->allyCount > 1 ? 4.0f : 0.0f)))
                active.allies.push_back(ally->GetGUID());
        active.allyPlayers = 1;
        if (active.allies.empty())
            return;
    }

    std::vector<Creature*> alive;
    uint32 const players = uint32(std::max<std::size_t>(active.inside.size(), 1));
    uint32 setFor = active.allyPlayers;
    for (ObjectGuid const& guid : active.allies)
        if (Creature* ally = map->GetCreature(guid))
            if (ally->IsAlive())
            {
                setFor = active.allyPlayers;
                ScaleHealth(ally, active.allyHealth, setFor, players);
                alive.push_back(ally);
            }
    active.allyPlayers = setFor;
    active.alliesAlive = int32(alive.size());
    if (alive.empty())
    {
        LOG_INFO("module", "mod-wow-fates: Expedition {}: its allies died, {} waves pushed back.", def->id, active.repelled);
        active.failed = true;
        return;
    }

    if (!Start(active, "defend", &WowFatesExpeditionMgr::CreatureName, def->allyEntry))
        return;

    // the wave on its way: pushed back when none of its monsters is left alive
    if (!active.waveMobs.empty())
    {
        bool const left = std::any_of(active.waveMobs.begin(), active.waveMobs.end(), [map](ObjectGuid const& guid)
        {
            Creature* creature = map->GetCreature(guid);
            return creature && creature->IsAlive();
        });
        if (left)
            return;
        if (active.waveClearedAt < 0)
            active.waveClearedAt = active.elapsed;
        active.waveMobs.clear();
        ++active.repelled;
        active.progressPct = std::min(100.0, 100.0 * double(active.repelled) / double(def->waveCount));
        active.waveTimer = int64(def->waveInterval) * IN_MILLISECONDS;
        BroadcastSet(active); // the tracker's count
        return;
    }

    active.waveTimer -= diff;
    if (active.waveTimer <= 0 && active.repelled < def->waveCount && map->IsGridLoaded(active.spawn.x, active.spawn.y))
        SendWave(active, map, Acore::Containers::SelectRandomContainerElement(alive));
}

// ---------------------------------------------------------------------------
// Boss support

// The first player in the area brings them out: the elite in the centre, its
// renowned foe a little away, at each other's throats. The bar is the health
// the elite lost (never going back); both grow with the players in the area.
// The elite dead, by a player or by the ally, the Expedition is done; the ally
// dead, it fails. Gone alive (a grid unloaded), they come back where they were.
namespace
{
    // The lines of a boss support's ally (its creature_text groups).
    enum WowFatesAllyTalk : uint8
    {
        ALLY_SAY_START   = 0,
        ALLY_SAY_FIGHT   = 1, // every 20 to 40 s
        ALLY_SAY_HALF    = 2, // the elite at half health
        ALLY_SAY_VICTORY = 3,
        ALLY_SAY_FALL    = 4
    };

    void Say(Creature* creature, uint8 group)
    {
        if (creature && creature->AI())
            creature->AI()->Talk(group);
    }
}

void WowFatesExpeditionMgr::UpdateBossSupport(WowFatesActiveExpedition& active, Map* map, uint32 diff)
{
    if (!map || active.failed || active.progressPct >= 100.0)
        return;
    WowFatesExpeditionDef const* def = active.def;

    if (!active.started)
    {
        if (active.inside.empty() || !map->IsGridLoaded(active.spawn.x, active.spawn.y))
            return;
        active.started = true;
    }

    Creature* boss = active.boss ? map->GetCreature(active.boss) : nullptr;
    if (!boss && map->IsGridLoaded(active.spawn.x, active.spawn.y))
    {
        bool const first = !active.bossOut;
        SpawnBoss(active, map); // its banner, the first time, to those inside
        boss = active.boss ? map->GetCreature(active.boss) : nullptr;
        if (boss && !first)
            boss->SetHealth(std::max<uint32>(1, uint32(boss->GetMaxHealth() * (1.0 - active.progressPct / 100.0))));
    }

    Creature* ally = active.ally ? map->GetCreature(active.ally) : nullptr;
    if (ally && !ally->IsAlive())
    {
        active.allyHealthPct = 0;
        Say(ally, ALLY_SAY_FALL);
        LOG_INFO("module", "mod-wow-fates: Expedition {}: its ally died, the elite at {} %.", def->id, uint32(active.progressPct));
        active.failed = true;
        return;
    }
    if (!ally && boss)
    {
        float const angle = frand(0.0f, 2.0f * float(M_PI));
        Position pos = Centre(active, map);
        float x = pos.GetPositionX() + 8.0f * std::cos(angle);
        float y = pos.GetPositionY() + 8.0f * std::sin(angle);
        float z = map->GetHeight(PHASEMASK_NORMAL, x, y, pos.GetPositionZ() + 5.0f, true, 20.0f);
        if (z <= INVALID_HEIGHT)
        {
            x = pos.GetPositionX();
            y = pos.GetPositionY();
            z = pos.GetPositionZ();
        }
        ally = SpawnAlly(active, map, Position(x, y, z, std::atan2(pos.GetPositionY() - y, pos.GetPositionX() - x)));
        if (ally)
        {
            active.ally = ally->GetGUID();
            active.allyPlayers = 1;
        }
    }

    uint32 const players = uint32(std::max<std::size_t>(active.inside.size(), 1));
    if (ally)
    {
        ScaleHealth(ally, active.allyHealth, active.allyPlayers, players);
        active.allyHealthPct = std::max<int32>(5, int32(std::ceil(ally->GetHealthPct() / 5.0f)) * 5);
    }
    if (!boss)
        return;
    if (!boss->IsAlive())
    {
        Say(ally, ALLY_SAY_VICTORY);
        active.progressPct = 100.0;
        return;
    }
    ScaleHealth(boss, active.bossHealth, active.bossPlayers, players);

    // the ally's words: at the start, now and then, at the elite's half health
    if (ally && ally->IsAlive())
    {
        if (!(active.said & 1))
        {
            active.said |= 1;
            Say(ally, ALLY_SAY_START);
            active.talkTimer = urand(20, 40) * IN_MILLISECONDS;
        }
        else if (!(active.said & 2) && boss->GetHealthPct() <= 50.0f)
        {
            active.said |= 2;
            Say(ally, ALLY_SAY_HALF);
            active.talkTimer = urand(20, 40) * IN_MILLISECONDS;
        }
        else
        {
            active.talkTimer -= diff;
            if (active.talkTimer <= 0)
            {
                Say(ally, ALLY_SAY_FIGHT);
                active.talkTimer = urand(20, 40) * IN_MILLISECONDS;
            }
        }
    }

    // at each other's throats
    if (ally && ally->IsAlive())
    {
        if (!boss->IsInCombat() && boss->AI())
            boss->AI()->AttackStart(ally);
        if (!ally->IsInCombat() && ally->AI())
            ally->AI()->AttackStart(boss);
    }

    double const lost = 1.0 - double(boss->GetHealth()) / double(std::max<uint32>(boss->GetMaxHealth(), 1));
    active.progressPct = std::max(active.progressPct, std::min(100.0 * lost, 99.0));
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
    {
        active->contributors[player->GetGUID()].damage += damage;
        Active(*active);
    }
}

void WowFatesExpeditionMgr::OnHeal(Unit* healer, Unit* target, uint32 gain)
{
    // Healing counts in a fight only: topping up idle friends earns nothing.
    // A defence's ally counts as a friend.
    if (!_activeCount || !gain || !healer || !target || !target->IsInCombat())
        return;
    Player* player = healer->GetCharmerOrOwnerPlayerOrPlayerItself();
    if (!player)
        return;

    std::lock_guard<std::recursive_mutex> guard(_lock);
    if (WowFatesActiveExpedition* active = FindAt(target))
        if (IsFromPlayer(target) || target->GetGUID() == active->ally
            || std::find(active->allies.begin(), active->allies.end(), target->GetGUID()) != active->allies.end())
        {
            active->contributors[player->GetGUID()].healing += gain;
            Active(*active);
        }
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
    Active(*active);

    // The leader of an assault: the end of the bar.
    if ((def->objectiveType == WOW_FATES_OBJECTIVE_ASSAULT || def->objectiveType == WOW_FATES_OBJECTIVE_BOSS_SUPPORT)
        && active->bossOut && creature->GetGUID() == active->boss)
    {
        active->progressPct = 100.0;
        ++active->contributors[player->GetGUID()].kills;
        return;
    }

    // A gathering's guards, the waves: their kills count for the players and
    // the pace of the monsters, not for the bar.
    if (def->objectiveType >= WOW_FATES_OBJECTIVE_GATHER)
    {
        ++active->contributors[player->GetGUID()].kills;
        active->killTimes.push_back(active->elapsed);
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
    active->killTimes.push_back(active->elapsed);
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
