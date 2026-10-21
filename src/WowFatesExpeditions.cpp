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
#include "PathGenerator.h"
#include "Player.h"
#include "Random.h"
#include "StringConvert.h"
#include "Tokenize.h"
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

            if (def.objectiveType < WOW_FATES_OBJECTIVE_KILL || def.objectiveType > WOW_FATES_OBJECTIVE_LAST)
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
        "`ally_entry`, `hold_time`, `wave_interval`, `wave_size`, `wave_growth`, `channel_entry`, `ally_count`, `wave_count`, `boss_waves`, "
        "`time_limit`, `max_losses`, `taken_entry`, `boss_spells` "
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
            // "25,50,75": the bar's percents that call the elite's pack
            itr->second.bossWaves.clear();
            std::string const marks = f[14].Get<std::string>();
            for (std::string_view mark : Acore::Tokenize(marks, ',', false))
                if (Optional<uint32> pct = Acore::StringTo<uint32>(mark))
                    if (*pct > 0 && *pct < 100)
                        itr->second.bossWaves.push_back(uint8(*pct));
            std::sort(itr->second.bossWaves.begin(), itr->second.bossWaves.end());
            itr->second.timeLimit = f[15].Get<uint32>();
            itr->second.maxLosses = f[16].Get<uint32>();
            itr->second.takenEntry = f[17].Get<uint32>();
            // "6730,6016,8599": a warlord's blows, the first from the start, one more with each phase
            itr->second.bossSpells.clear();
            std::string const spells = f[18].Get<std::string>();
            for (std::string_view spell : Acore::Tokenize(spells, ',', false))
                if (Optional<uint32> id = Acore::StringTo<uint32>(spell))
                {
                    if (sSpellMgr->GetSpellInfo(*id))
                        itr->second.bossSpells.push_back(*id);
                    else
                        LOG_ERROR("module", "mod-wow-fates: Expedition {}: spell {} of its boss_spells does not exist; left out.", itr->first, *id);
                }
        } while (result->NextRow());
    }

    for (auto itr = _defs.begin(); itr != _defs.end();)
    {
        WowFatesExpeditionDef const& def = itr->second;
        // A boss support's ally is always an elite: the client reads the rank
        // from the creature's template, so one that is not is made so.
        if (def.objectiveType == WOW_FATES_OBJECTIVE_BOSS_SUPPORT)
            if (CreatureTemplate const* hero = sObjectMgr->GetCreatureTemplate(def.allyEntry))
                if (hero->rank < CREATURE_ELITE_ELITE)
                {
                    LOG_ERROR("module", "mod-wow-fates: Expedition {}: its ally, creature {}, is not an elite (rank {}); it is made one.",
                        def.id, def.allyEntry, hero->rank);
                    const_cast<CreatureTemplate*>(hero)->rank = CREATURE_ELITE_ELITE;
                }
        // A warlord is always an elite, as a boss support's ally.
        if (def.objectiveType == WOW_FATES_OBJECTIVE_WARLORD)
            if (CreatureTemplate const* warlord = sObjectMgr->GetCreatureTemplate(def.bossEntry))
                if (warlord->rank < CREATURE_ELITE_ELITE)
                {
                    LOG_ERROR("module", "mod-wow-fates: Expedition {}: its warlord, creature {}, is not an elite (rank {}); it is made one.",
                        def.id, def.bossEntry, warlord->rank);
                    const_cast<CreatureTemplate*>(warlord)->rank = CREATURE_ELITE_ELITE;
                }
        if ((def.objectiveType == WOW_FATES_OBJECTIVE_ASSAULT || def.objectiveType == WOW_FATES_OBJECTIVE_BOSS_SUPPORT
                || def.objectiveType == WOW_FATES_OBJECTIVE_WARLORD)
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
        else if (def.objectiveType == WOW_FATES_OBJECTIVE_ESCORT
            && (!sObjectMgr->GetCreatureTemplate(def.allyEntry) || !sObjectMgr->GetCreatureTemplate(def.summonEntry)))
        {
            LOG_ERROR("module", "mod-wow-fates: Expedition {} is an escort whose ally (creature {}) or ambushers (summon_entry {}) do not exist; skipped.",
                def.id, def.allyEntry, def.summonEntry);
            itr = _defs.erase(itr);
        }
        else if (def.objectiveType == WOW_FATES_OBJECTIVE_RESCUE
            && (!sObjectMgr->GetGameObjectTemplate(def.gatherEntry) || !sObjectMgr->GetCreatureTemplate(def.allyEntry)))
        {
            LOG_ERROR("module", "mod-wow-fates: Expedition {} is a rescue whose cage (gameobject {}) or captive (creature {}) does not exist; skipped.",
                def.id, def.gatherEntry, def.allyEntry);
            itr = _defs.erase(itr);
        }
        else if (def.objectiveType == WOW_FATES_OBJECTIVE_WARLORD && !sObjectMgr->GetCreatureTemplate(def.summonEntry))
        {
            LOG_ERROR("module", "mod-wow-fates: Expedition {} is a warlord whose reinforcements (summon_entry {}) do not exist; skipped.",
                def.id, def.summonEntry);
            itr = _defs.erase(itr);
        }
        else if (def.objectiveType == WOW_FATES_OBJECTIVE_CAPTURE
            && (!sObjectMgr->GetGameObjectTemplate(def.gatherEntry) || !sObjectMgr->GetGameObjectTemplate(def.takenEntry)
                || !sObjectMgr->GetCreatureTemplate(def.summonEntry)))
        {
            LOG_ERROR("module", "mod-wow-fates: Expedition {} is a capture whose banners (gameobjects {} and {}) or foes (summon_entry {}) do not exist; skipped.",
                def.id, def.gatherEntry, def.takenEntry, def.summonEntry);
            itr = _defs.erase(itr);
        }
        else if ((def.objectiveType == WOW_FATES_OBJECTIVE_GATHER || def.objectiveType == WOW_FATES_OBJECTIVE_SABOTAGE)
            && !sObjectMgr->GetGameObjectTemplate(def.gatherEntry))
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

    if (QueryResult result = WorldDatabase.Query("SELECT `id`, `expedition`, `map`, `x`, `y`, `z`, `o`, `path` FROM `mod_wow_fates_spawn`"))
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
            // "x,y,z;x,y,z": an escort's route, a rescue's way out
            std::string const path = f[7].Get<std::string>();
            for (std::string_view point : Acore::Tokenize(path, ';', false))
            {
                std::vector<std::string_view> const xyz = Acore::Tokenize(point, ',', false);
                Optional<float> px = xyz.size() == 3 ? Acore::StringTo<float>(xyz[0]) : std::nullopt;
                Optional<float> py = xyz.size() == 3 ? Acore::StringTo<float>(xyz[1]) : std::nullopt;
                Optional<float> pz = xyz.size() == 3 ? Acore::StringTo<float>(xyz[2]) : std::nullopt;
                if (px && py && pz)
                    spawn.path.emplace_back(*px, *py, *pz, 0.0f);
                else
                    LOG_ERROR("module", "mod-wow-fates: spawn point {}: \"{}\" of its path is no \"x,y,z\"; left out.", spawn.id, point);
            }

            auto def = _defs.find(spawn.expedition);
            if (def == _defs.end())
                continue;
            uint8 const type = def->second.objectiveType;
            if ((type == WOW_FATES_OBJECTIVE_ESCORT && spawn.path.size() < 2) || (type == WOW_FATES_OBJECTIVE_RESCUE && spawn.path.empty()))
            {
                LOG_ERROR("module", "mod-wow-fates: spawn point {} of Expedition {} has no {} in its path; skipped.", spawn.id, spawn.expedition,
                    type == WOW_FATES_OBJECTIVE_ESCORT ? "route (two points at least)" : "way out");
                continue;
            }
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
    if (def->objectiveType == WOW_FATES_OBJECTIVE_GATHER || def->objectiveType == WOW_FATES_OBJECTIVE_SABOTAGE)
        return Acore::StringFormat(WowFatesText(session, WOW_FATES_STR_OBJECTIVE_GATHER), GameObjectName(def->gatherEntry, session));
    if (def->objectiveType == WOW_FATES_OBJECTIVE_DEFENCE)
        return Acore::StringFormat(WowFatesText(session, WOW_FATES_STR_OBJECTIVE_DEFENCE), CreatureName(def->allyEntry, session),
            (def->holdTime + 59) / 60);
    if (def->objectiveType == WOW_FATES_OBJECTIVE_INVOKE)
        return Acore::StringFormat(WowFatesText(session, WOW_FATES_STR_OBJECTIVE_INVOKE), GameObjectName(def->channelEntry, session));
    if (def->objectiveType == WOW_FATES_OBJECTIVE_BOSS_SUPPORT || def->objectiveType == WOW_FATES_OBJECTIVE_WARLORD)
        return Acore::StringFormat(WowFatesText(session, WOW_FATES_STR_OBJECTIVE_KILL), CreatureName(def->bossEntry, session));
    if (def->objectiveType == WOW_FATES_OBJECTIVE_SUPPORT)
        return Acore::StringFormat(WowFatesText(session, WOW_FATES_STR_OBJECTIVE_SUPPORT), CreatureName(def->allyEntry, session), def->waveCount);
    if (def->objectiveType == WOW_FATES_OBJECTIVE_ESCORT)
        return Acore::StringFormat(WowFatesText(session, WOW_FATES_STR_OBJECTIVE_ESCORT), CreatureName(def->allyEntry, session));
    if (def->objectiveType == WOW_FATES_OBJECTIVE_RESCUE)
        return Acore::StringFormat(WowFatesText(session, WOW_FATES_STR_OBJECTIVE_RESCUE), CreatureName(def->allyEntry, session));
    if (def->objectiveType == WOW_FATES_OBJECTIVE_CAPTURE)
        return Acore::StringFormat(WowFatesText(session, WOW_FATES_STR_OBJECTIVE_CAPTURE), GameObjectName(def->gatherEntry, session));
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
    if (map)
        for (auto const& flag : active.flags)
            if (GameObject* go = map->GetGameObject(flag.go))
                go->DespawnOrUnsummon();
    active.flags.clear();
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

    delete active.walker;
    active.walker = nullptr;

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
    if (def->objectiveType == WOW_FATES_OBJECTIVE_GATHER || def->objectiveType == WOW_FATES_OBJECTIVE_SABOTAGE)
        return Acore::StringFormat(WowFatesText(session, WOW_FATES_STR_TRACKER_GATHER), GameObjectName(def->gatherEntry, session));
    if (def->objectiveType == WOW_FATES_OBJECTIVE_DEFENCE || def->objectiveType == WOW_FATES_OBJECTIVE_BOSS_SUPPORT)
        return Acore::StringFormat(WowFatesText(session, WOW_FATES_STR_TRACKER_DEFENCE), CreatureName(def->allyEntry, session));
    if (def->objectiveType == WOW_FATES_OBJECTIVE_INVOKE)
        return Acore::StringFormat(WowFatesText(session, WOW_FATES_STR_TRACKER_INVOKE), GameObjectName(def->channelEntry, session));
    if (def->objectiveType == WOW_FATES_OBJECTIVE_SUPPORT)
        return Acore::StringFormat(WowFatesText(session, WOW_FATES_STR_TRACKER_SUPPORT), active.repelled, def->waveCount);
    if (def->objectiveType == WOW_FATES_OBJECTIVE_ESCORT)
        return Acore::StringFormat(WowFatesText(session, WOW_FATES_STR_TRACKER_ESCORT), CreatureName(def->allyEntry, session));
    if (def->objectiveType == WOW_FATES_OBJECTIVE_RESCUE)
        return Acore::StringFormat(WowFatesText(session, WOW_FATES_STR_TRACKER_RESCUE), CreatureName(def->allyEntry, session));
    if (def->objectiveType == WOW_FATES_OBJECTIVE_CAPTURE)
        return Acore::StringFormat(WowFatesText(session, WOW_FATES_STR_TRACKER_CAPTURE), GameObjectName(def->gatherEntry, session));
    if ((def->objectiveType == WOW_FATES_OBJECTIVE_ASSAULT && active.bossOut) || def->objectiveType == WOW_FATES_OBJECTIVE_WARLORD)
        return Acore::StringFormat(WowFatesText(session, WOW_FATES_STR_TRACKER_KILL), CreatureName(def->bossEntry, session));
    if (!def->objectiveEntry)
        return WowFatesText(session, WOW_FATES_STR_TRACKER_KILL_ANY);
    return Acore::StringFormat(WowFatesText(session, WOW_FATES_STR_TRACKER_KILL), CreatureName(def->objectiveEntry, session));
}

// The tracker's second line: what there is to protect. A wave support: its
// allies alive out of ally_count; a defence: its ally's health; a rescue: the
// captives led out and those lost, against the losses that fail it; a
// capture: the banners held and the time left; a warlord: the time left.
std::string WowFatesExpeditionMgr::TrackerGuard(WowFatesActiveExpedition const& active, WorldSession* session) const
{
    WowFatesExpeditionDef const* def = active.def;
    if (def->objectiveType == WOW_FATES_OBJECTIVE_CAPTURE)
    {
        uint32 const held = uint32(std::count_if(active.flags.begin(), active.flags.end(), [](auto const& flag) { return flag.ours; }));
        uint32 const total = active.flags.empty() ? def->gatherCount : uint32(active.flags.size());
        int64 const left = TimeLeft(active);
        if (left < 0)
            return Acore::StringFormat(WowFatesText(session, WOW_FATES_STR_HELD), held, total);
        uint32 const seconds = uint32((left + 9999) / 10000 * 10); // by steps of ten
        return Acore::StringFormat(WowFatesText(session, WOW_FATES_STR_HELD_TIME), held, total, seconds / 60, seconds % 60);
    }
    if (def->objectiveType == WOW_FATES_OBJECTIVE_WARLORD)
    {
        int64 const left = TimeLeft(active);
        if (left < 0)
            return "";
        uint32 const seconds = uint32((left + 9999) / 10000 * 10); // by steps of ten
        return Acore::StringFormat(WowFatesText(session, WOW_FATES_STR_TIME_LEFT), seconds / 60, seconds % 60);
    }
    if (def->objectiveType == WOW_FATES_OBJECTIVE_RESCUE)
        return Acore::StringFormat(WowFatesText(session, WOW_FATES_STR_RESCUED), active.saved,
            uint32(std::ceil(Needed(active) - 0.001)), active.lost, def->maxLosses + 1);
    if (def->objectiveType == WOW_FATES_OBJECTIVE_SUPPORT && active.alliesAlive >= 0)
        return Acore::StringFormat(WowFatesText(session, WOW_FATES_STR_ALLIES_ALIVE), CreatureName(def->allyEntry, session),
            uint32(active.alliesAlive), def->allyCount);
    if ((def->objectiveType == WOW_FATES_OBJECTIVE_DEFENCE || def->objectiveType == WOW_FATES_OBJECTIVE_BOSS_SUPPORT
            || def->objectiveType == WOW_FATES_OBJECTIVE_ESCORT) && active.allyHealthPct >= 0)
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
        Walker(active, sMapMgr->FindBaseNonInstanceMap(active.spawn.map));
        if (active.def->objectiveType == WOW_FATES_OBJECTIVE_DEFENCE)
            UpdateDefence(active, sMapMgr->FindBaseNonInstanceMap(active.spawn.map), diff);
        else if (active.def->objectiveType == WOW_FATES_OBJECTIVE_INVOKE)
            UpdateInvoke(active, sMapMgr->FindBaseNonInstanceMap(active.spawn.map), diff);
        else if (active.def->objectiveType == WOW_FATES_OBJECTIVE_SUPPORT)
            UpdateSupport(active, sMapMgr->FindBaseNonInstanceMap(active.spawn.map), diff);
        else if (active.def->objectiveType == WOW_FATES_OBJECTIVE_BOSS_SUPPORT)
            UpdateBossSupport(active, sMapMgr->FindBaseNonInstanceMap(active.spawn.map), diff);
        else if (active.def->objectiveType == WOW_FATES_OBJECTIVE_ESCORT)
            UpdateEscort(active, sMapMgr->FindBaseNonInstanceMap(active.spawn.map), diff);
        else if (active.def->objectiveType == WOW_FATES_OBJECTIVE_RESCUE)
            UpdateRescue(active, sMapMgr->FindBaseNonInstanceMap(active.spawn.map));
        else if (active.def->objectiveType == WOW_FATES_OBJECTIVE_CAPTURE)
            UpdateCapture(active, sMapMgr->FindBaseNonInstanceMap(active.spawn.map), diff);
        else if (active.def->objectiveType == WOW_FATES_OBJECTIVE_WARLORD)
            UpdateWarlord(active, sMapMgr->FindBaseNonInstanceMap(active.spawn.map), diff);

        if (areaTick)
        {
            Map* map = sMapMgr->FindBaseNonInstanceMap(active.spawn.map);
            UpdateArea(active, map);
            UpdateSummons(active, map, diff);
            UpdateWaiting(active, map);

            // its point, once its ground is loaded: under a roof, it is never used again
            if (map && !active.checked && NeedsSky(active.def) && map->IsGridLoaded(active.spawn.x, active.spawn.y))
            {
                active.checked = true;
                if (!Outdoors(map, active.spawn.x, active.spawn.y, active.spawn.z + 1.0f))
                {
                    LOG_ERROR("module", "mod-wow-fates: spawn point {} of Expedition {} is under a roof; it is not used again.",
                        active.spawn.id, active.def->id);
                    _indoors.insert(active.spawn.id);
                    active.remaining = 0;
                }
            }

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
            else if (active.def->objectiveType == WOW_FATES_OBJECTIVE_GATHER
                || active.def->objectiveType == WOW_FATES_OBJECTIVE_SABOTAGE
                || active.def->objectiveType == WOW_FATES_OBJECTIVE_RESCUE)
                UpdateGather(active, map);

            uint32 const pct = uint32(active.progressPct);
            int32 guarded = active.alliesAlive * 1000 + active.allyHealthPct;
            if (active.def->objectiveType == WOW_FATES_OBJECTIVE_RESCUE)
                guarded = int32(active.saved * 1000 + active.lost);
            else if (active.def->timeLimit) // a timed one: the time left, by steps of ten seconds
                guarded = active.alliesAlive * 1000 + int32((TimeLeft(active) + 9999) / 10000);
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
            if (spawn.zone != zone || (_indoors.count(spawnId) && NeedsSky(&def)) || _broken.count(spawnId))
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
// of the radius, not much higher or lower than the centre, dry, and reached
// on foot from the centre (WalkPath: not behind a wall, not in a cave under
// it, not on a rock). The ground is searched from well above, so that a slope
// or a hill around the centre is found (from just above the centre, every
// point higher up the hill had no ground). Several tries; false when none.
bool WowFatesExpeditionMgr::MonsterPoint(WowFatesActiveExpedition const& active, Map* map, Position& out) const
{
    WowFatesSpawnPoint const& c = active.spawn;
    Position const centre(c.x, c.y, c.z, 0.0f);
    float const reach = active.def->radius * 0.8f;
    for (uint32 attempt = 0; attempt < 16; ++attempt)
    {
        float const angle = frand(0.0f, 2.0f * float(M_PI));
        float const dist = reach * std::sqrt(frand(0.0f, 1.0f)); // even over the disc
        float const x = c.x + dist * std::cos(angle);
        float const y = c.y + dist * std::sin(angle);
        float const z = map->GetHeight(PHASEMASK_NORMAL, x, y, c.z + 40.0f, true, 80.0f);
        if (z <= INVALID_HEIGHT || std::fabs(z - c.z) > 25.0f || NearPlayer(active, x, y))
            continue;
        bool outdoors = false;
        if (Wet(map, x, y, z, &outdoors) || (NeedsSky(active.def) && !outdoors))
            continue;
        Position const pos(x, y, z, frand(0.0f, 2.0f * float(M_PI)));
        if (!WalkPath(map, active.walker, centre, pos))
            continue;
        out = pos;
        return true;
    }
    return false;
}

// A monster's point (MonsterPoint); the centre, a little spread, as the last
// resort.
Position WowFatesExpeditionMgr::SpawnPoint(WowFatesActiveExpedition const& active, Map* map) const
{
    Position pos;
    if (MonsterPoint(active, map, pos))
        return pos;

    WowFatesSpawnPoint const& c = active.spawn;
    float const angle = frand(0.0f, 2.0f * float(M_PI));
    float const x = c.x + 3.0f * std::cos(angle);
    float const y = c.y + 3.0f * std::sin(angle);
    float z = map->GetHeight(PHASEMASK_NORMAL, x, y, c.z + 5.0f, true, 20.0f);
    if (z <= INVALID_HEIGHT)
        return Position(c.x, c.y, c.z, angle);
    return Position(x, y, z, angle);
}

// The scratch object that walks the paths as a player would (WalkPath), never
// on the map: one per Expedition, made once its map is there, deleted at its end.
GameObject* WowFatesExpeditionMgr::Walker(WowFatesActiveExpedition& active, Map* map)
{
    if (active.walker || !map)
        return active.walker;
    GameObject* go = new GameObject();
    if (!go->Create(map->GenerateLowGuid<HighGuid::GameObject>(), WOW_FATES_WALKER_ENTRY, map, PHASEMASK_NORMAL,
            active.spawn.x, active.spawn.y, active.spawn.z, 0.0f, G3D::Quat(), 0, GO_STATE_READY))
    {
        delete go;
        return nullptr;
    }
    active.walker = go;
    return go;
}

// On foot from one point to the other, as a player: a path of the navigation
// mesh, whole (not cut short, not from or to a point off the mesh), not much
// longer than the straight line (WowFates.Object.PathFactor), with no step
// steeper than WowFates.Path.MaxSlope between two of its points (no jump, no
// cliff) and none in water. Where the paths are not known (no mmap there),
// one seen from the other. The points of the path in points, when asked.
bool WowFatesExpeditionMgr::WalkPath(Map* map, WorldObject const* walker, Position const& from, Position const& to,
    std::vector<Position>* points) const
{
    WowFatesConfig const& conf = WowFatesConf();
    float const straight = from.GetExactDist(to);
    if (walker)
    {
        PathGenerator path(walker);
        path.CalculatePath(from.GetPositionX(), from.GetPositionY(), from.GetPositionZ(),
            to.GetPositionX(), to.GetPositionY(), to.GetPositionZ(), false);
        PathType const type = path.GetPathType();
        if (!(type & PATHFIND_NOT_USING_PATH))
        {
            if (!(type & PATHFIND_NORMAL) || (type & (PATHFIND_NOPATH | PATHFIND_INCOMPLETE | PATHFIND_SHORT | PATHFIND_FARFROMPOLY)))
                return false;
            if (path.getPathLength() > straight * conf.objectPathFactor + 10.0f)
                return false;
            Movement::PointsArray const& steps = path.GetPath();
            for (std::size_t i = 1; i < steps.size(); ++i)
            {
                float const flat = std::hypot(steps[i].x - steps[i - 1].x, steps[i].y - steps[i - 1].y);
                if (std::fabs(steps[i].z - steps[i - 1].z) > 1.5f + flat * conf.pathMaxSlope)
                    return false;
                if (Wet(map, steps[i].x, steps[i].y, steps[i].z))
                    return false;
            }
            if (points)
            {
                points->clear();
                for (G3D::Vector3 const& step : steps)
                    points->emplace_back(step.x, step.y, step.z, 0.0f);
            }
            return true;
        }
    }
    if (!map->isInLineOfSight(from.GetPositionX(), from.GetPositionY(), from.GetPositionZ() + 2.0f,
            to.GetPositionX(), to.GetPositionY(), to.GetPositionZ() + 2.0f, PHASEMASK_NORMAL,
            LINEOFSIGHT_ALL_CHECKS, VMAP::ModelIgnoreFlags::Nothing))
        return false;
    if (points)
        *points = { from, to };
    return true;
}

// A point that cannot be used (its route or way out not walkable): the
// Expedition goes quietly, the point never again.
void WowFatesExpeditionMgr::Broken(WowFatesActiveExpedition& active, std::string const& why)
{
    LOG_ERROR("module", "mod-wow-fates: spawn point {} of Expedition {}: {}; it is not used again.", active.spawn.id, active.def->id, why);
    _broken.insert(active.spawn.id);
    active.remaining = 0;
}

// The route of an escort: its spawn point's path, each leg walked as a player
// would (WalkPath), joined into one line of points, every one in the area;
// the yards done at each. Its grids are loaded first. False, and why, when a
// leg cannot be walked.
bool WowFatesExpeditionMgr::BuildRoute(WowFatesActiveExpedition& active, Map* map, std::string* why)
{
    active.route.clear();
    active.routeAt.clear();
    std::vector<Position> const& legs = active.spawn.path;
    if (legs.size() < 2)
    {
        if (why)
            *why = "no route (two points at least)";
        return false;
    }
    map->LoadGridsInRange(Position(active.spawn.x, active.spawn.y, active.spawn.z, 0.0f), active.def->radius + 30.0f);
    GameObject* walker = Walker(active, map);
    for (std::size_t i = 1; i < legs.size(); ++i)
    {
        std::vector<Position> points;
        if (!WalkPath(map, walker, legs[i - 1], legs[i], &points))
        {
            if (why)
                *why = Acore::StringFormat("leg {} ({:.1f} {:.1f} {:.1f} to {:.1f} {:.1f} {:.1f}) not walked", i,
                    legs[i - 1].GetPositionX(), legs[i - 1].GetPositionY(), legs[i - 1].GetPositionZ(),
                    legs[i].GetPositionX(), legs[i].GetPositionY(), legs[i].GetPositionZ());
            active.route.clear();
            return false;
        }
        for (std::size_t p = active.route.empty() ? 0 : 1; p < points.size(); ++p)
            active.route.push_back(points[p]);
    }
    float done = 0.0f;
    for (std::size_t i = 0; i < active.route.size(); ++i)
    {
        if (i)
            done += active.route[i].GetExactDist(active.route[i - 1]);
        active.routeAt.push_back(done);
        if (std::hypot(active.route[i].GetPositionX() - active.spawn.x, active.route[i].GetPositionY() - active.spawn.y)
            > active.def->radius * 0.95f)
        {
            if (why)
                *why = Acore::StringFormat("point {} of the route ({:.1f} {:.1f}) out of the area", i,
                    active.route[i].GetPositionX(), active.route[i].GetPositionY());
            active.route.clear();
            active.routeAt.clear();
            return false;
        }
    }
    return true;
}

// A capture's banner: an object's point (ObjectPoint), far from the others
// placed (the larger of MinSpacing and 45 % of the radius), so that one
// cannot be held from another. A few tries; false when none.
bool WowFatesExpeditionMgr::FlagPoint(WowFatesActiveExpedition const& active, Map* map, float footprint,
    std::vector<Footing> const& obstacles, std::vector<Position> const& placed, Position& out) const
{
    float const apart = std::max(WowFatesConf().objectMinSpacing, active.def->radius * 0.45f);
    for (uint32 attempt = 0; attempt < 6; ++attempt)
    {
        Position pos;
        if (!ObjectPoint(active, map, active.walker, footprint, obstacles, pos))
            continue;
        if (std::any_of(placed.begin(), placed.end(), [&pos, apart](Position const& other)
            {
                return pos.GetExactDist2d(other.GetPositionX(), other.GetPositionY()) < apart;
            }))
            continue;
        out = pos;
        return true;
    }
    return false;
}

// A point near another (an ambush around an escort's ally, reinforcements
// around a warlord): 18 to 28 yards from it, near the angle, on the ground
// about its level, dry, in the area, and reached on foot to it. A few angles
// around; false when none.
bool WowFatesExpeditionMgr::AroundPoint(WowFatesActiveExpedition const& active, Map* map, Position const& around, float angle,
    Position& out) const
{
    WowFatesSpawnPoint const& c = active.spawn;
    for (uint32 attempt = 0; attempt < 8; ++attempt)
    {
        float const a = angle + (attempt ? frand(-0.6f, 0.6f) : 0.0f);
        float const dist = frand(18.0f, 28.0f);
        float const x = around.GetPositionX() + dist * std::cos(a);
        float const y = around.GetPositionY() + dist * std::sin(a);
        if (std::hypot(x - c.x, y - c.y) > active.def->radius || NearPlayer(active, x, y))
            continue;
        float const z = map->GetHeight(PHASEMASK_NORMAL, x, y, around.GetPositionZ() + 8.0f, true, 16.0f);
        if (z <= INVALID_HEIGHT || std::fabs(z - around.GetPositionZ()) > 8.0f)
            continue;
        bool outdoors = false;
        if (Wet(map, x, y, z, &outdoors) || (NeedsSky(active.def) && !outdoors))
            continue;
        Position const pos(x, y, z, std::atan2(around.GetPositionY() - y, around.GetPositionX() - x));
        if (!WalkPath(map, active.walker, pos, around))
            continue;
        out = pos;
        return true;
    }
    return false;
}

// The radius of an object's model on the ground, as the client draws it.
float WowFatesExpeditionMgr::Footprint(uint32 displayId, float scale)
{
    GameObjectDisplayInfoEntry const* info = sGameObjectDisplayInfoStore.LookupEntry(displayId);
    if (!info)
        return 0.5f * scale;
    float const r = std::max({ std::fabs(info->minX), std::fabs(info->maxX), std::fabs(info->minY), std::fabs(info->maxY) });
    return std::max(r, 0.3f) * scale;
}

// What stands in the area already: the world's own objects (the tents and
// huts of a camp) and the Expedition's.
std::vector<WowFatesExpeditionMgr::Footing> WowFatesExpeditionMgr::Obstacles(WowFatesActiveExpedition const& active, Map* map) const
{
    std::vector<Footing> out;
    WowFatesSpawnPoint const& c = active.spawn;
    float const range = active.def->radius + 30.0f;
    for (auto const& [spawnId, go] : map->GetGameObjectBySpawnIdStore())
    {
        if (!go || !go->IsInWorld() || go->GetExactDist2d(c.x, c.y) > range)
            continue;
        GameObjectTemplate const* info = go->GetGOInfo();
        out.push_back({ go->GetPositionX(), go->GetPositionY(), Footprint(info ? info->displayId : 0, go->GetObjectScale()) });
    }
    for (ObjectGuid const& guid : active.objects)
        if (GameObject* go = map->GetGameObject(guid))
            out.push_back({ go->GetPositionX(), go->GetPositionY(), Footprint(go->GetGOInfo()->displayId, go->GetObjectScale()) });
    return out;
}

// In the water, or under it; outdoors told on the way.
bool WowFatesExpeditionMgr::Wet(Map* map, float x, float y, float z, bool* outdoors)
{
    PositionFullTerrainStatus status;
    map->GetFullTerrainStatusForPosition(PHASEMASK_NORMAL, x, y, z, 2.0f, status);
    if (outdoors)
        *outdoors = status.outdoors;
    return (status.liquidInfo.Status & (LIQUID_MAP_WATER_WALK | LIQUID_MAP_IN_WATER | LIQUID_MAP_UNDER_WATER)) != 0;
}

// Flat ground: around the point, at reach, the ground rises or falls little,
// and no wall nor rock stands in between.
bool WowFatesExpeditionMgr::Level(Map* map, float x, float y, float z, float reach) const
{
    float const slope = WowFatesConf().objectMaxSlope;
    float const turn = frand(0.0f, 2.0f * float(M_PI));
    for (uint32 i = 0; i < 4; ++i)
    {
        float const angle = turn + float(i) * float(M_PI) / 2.0f;
        float const sx = x + reach * std::cos(angle);
        float const sy = y + reach * std::sin(angle);
        float const sz = map->GetHeight(PHASEMASK_NORMAL, sx, sy, z + reach * slope + 1.0f, true, 2.0f * reach * slope + 2.0f);
        if (sz <= INVALID_HEIGHT || std::fabs(sz - z) > reach * slope)
            return false;
        if (!map->isInLineOfSight(x, y, z + 1.5f, sx, sy, sz + 1.5f, PHASEMASK_NORMAL,
                LINEOFSIGHT_ALL_CHECKS, VMAP::ModelIgnoreFlags::Nothing))
            return false;
    }
    return true;
}

// A point for an object of a gathering or a sabotage: on the ground, about the
// level of the centre (the ground searched down from just above that level,
// never on the hill over the camp), flat, dry, under the sky for a sabotage,
// reached on foot from the centre by a path of a sensible length, and clear
// of what stands there already, the object's footprint and theirs apart.
// False when no such point is found: the object waits.
bool WowFatesExpeditionMgr::ObjectPoint(WowFatesActiveExpedition const& active, Map* map, WorldObject const* walker, float footprint,
    std::vector<Footing> const& obstacles, Position& out) const
{
    WowFatesConfig const& conf = WowFatesConf();
    WowFatesSpawnPoint const& c = active.spawn;
    float const reach = active.def->radius * conf.objectReach;
    float const high = conf.objectMaxHeight;
    for (uint32 attempt = 0; attempt < 24; ++attempt)
    {
        float const angle = frand(0.0f, 2.0f * float(M_PI));
        float const dist = reach * std::sqrt(frand(0.0f, 1.0f));
        float const x = c.x + dist * std::cos(angle);
        float const y = c.y + dist * std::sin(angle);
        if (NearPlayer(active, x, y))
            continue;
        bool clear = true;
        for (Footing const& f : obstacles)
            if (std::hypot(f.x - x, f.y - y) < std::max(conf.objectMinSpacing, (f.r + footprint) * conf.objectSpacing))
            {
                clear = false;
                break;
            }
        if (!clear)
            continue;

        float const z = map->GetHeight(PHASEMASK_NORMAL, x, y, c.z + high + 2.0f, true, 2.0f * high + 4.0f);
        if (z <= INVALID_HEIGHT || std::fabs(z - c.z) > high)
            continue;
        bool outdoors = false;
        if (Wet(map, x, y, z, &outdoors) || (NeedsSky(active.def) && !outdoors))
            continue;
        if (!Level(map, x, y, z, std::max(3.0f, footprint * 0.8f)))
            continue;

        // on foot from the centre; a rescue's cage, to the way out as well
        Position const pos(x, y, z, 0.0f);
        if (!WalkPath(map, walker, Position(c.x, c.y, c.z, 0.0f), pos))
            continue;
        if (active.def->objectiveType == WOW_FATES_OBJECTIVE_RESCUE
            && (active.route.empty() || !WalkPath(map, walker, pos, active.route.front())))
            continue;

        out = Position(x, y, z, frand(0.0f, 2.0f * float(M_PI)));
        return true;
    }
    return false;
}

// A guard's place by its object: out of the object's footprint and of what
// stands around, on the ground at about the object's level, dry. False: the
// last one tried, out of the footprint all the same.
bool WowFatesExpeditionMgr::GuardPoint(WowFatesActiveExpedition const& active, Map* map, Position const& object, float footprint,
    std::vector<Footing> const& obstacles, Position& out) const
{
    float const turn = frand(0.0f, 2.0f * float(M_PI));
    float const dist = footprint + 2.5f;
    for (uint32 i = 0; i < 8; ++i)
    {
        float const angle = turn + float(i) * float(M_PI) / 4.0f;
        float const x = object.GetPositionX() + dist * std::cos(angle);
        float const y = object.GetPositionY() + dist * std::sin(angle);
        float z = map->GetHeight(PHASEMASK_NORMAL, x, y, object.GetPositionZ() + 3.0f, true, 8.0f);
        bool const ground = z > INVALID_HEIGHT && std::fabs(z - object.GetPositionZ()) <= 3.0f;
        if (!ground)
            z = object.GetPositionZ();
        out = Position(x, y, z, std::atan2(y - object.GetPositionY(), x - object.GetPositionX()));
        if (!ground)
            continue;
        bool clear = true;
        for (Footing const& f : obstacles)
            if (std::hypot(f.x - x, f.y - y) < f.r + 1.0f)
            {
                clear = false;
                break;
            }
        bool outdoors = false;
        if (!clear || Wet(map, x, y, z, &outdoors) || (NeedsSky(active.def) && !outdoors))
            continue;
        return true;
    }
    return false;
}

// The objective types under the sky (WowFates.Expedition.OutdoorTypes, a
// sabotage by default: its targets are buildings and engines out in the
// open); the others may be in a cave or a building as well.
bool WowFatesExpeditionMgr::NeedsSky(WowFatesExpeditionDef const* def)
{
    std::vector<uint8> const& types = WowFatesConf().outdoorTypes;
    return std::find(types.begin(), types.end(), def->objectiveType) != types.end();
}

// Under the sky: a point under a roof (a cave, a building; an awning is
// outdoors) is not one of a sabotage's.
bool WowFatesExpeditionMgr::Outdoors(Map* map, float x, float y, float z) const
{
    PositionFullTerrainStatus status;
    map->GetFullTerrainStatusForPosition(PHASEMASK_NORMAL, x, y, z, 2.0f, status);
    return status.outdoors;
}

bool WowFatesExpeditionMgr::NearPlayer(WowFatesActiveExpedition const& active, float x, float y) const
{
    float const distance = WowFatesConf().spawnPlayerDistance;
    for (ObjectGuid const& guid : active.inside)
        if (Player* player = ObjectAccessor::FindPlayer(guid))
            if (player->GetExactDist2d(x, y) < distance)
                return true;
    return false;
}

// How many of its monsters may be at the players at once: never an army on
// a player alone.
uint32 WowFatesExpeditionMgr::EngageCap(WowFatesActiveExpedition const& active) const
{
    WowFatesConfig const& c = WowFatesConf();
    uint32 const players = uint32(std::max<std::size_t>(active.inside.size(), 1));
    return c.engageBase + c.engagePerPlayer * (players - 1);
}

// Its monsters fighting, or on their way to: those of a wave that are not
// held back count, as those in a fight.
uint32 WowFatesExpeditionMgr::Engaged(WowFatesActiveExpedition const& active, Map* map) const
{
    std::unordered_set<ObjectGuid> held;
    for (auto const& w : active.waiting)
        held.insert(w.guid);
    std::unordered_set<ObjectGuid> wave(active.waveMobs.begin(), active.waveMobs.end());
    uint32 count = 0;
    for (ObjectGuid const& guid : active.summons)
        if (Creature* creature = map->GetCreature(guid))
            if (creature->IsAlive() && !held.count(guid) && (creature->IsInCombat() || wave.count(guid)))
                ++count;
    return count;
}

// A monster sent: at the target if there is one still, else to the goal.
void WowFatesExpeditionMgr::Send(WowFatesActiveExpedition& /*active*/, Creature* creature, Unit* target, Position const& goal)
{
    creature->SetHomePosition(goal.GetPositionX() + frand(-4.0f, 4.0f), goal.GetPositionY() + frand(-4.0f, 4.0f),
        goal.GetPositionZ(), goal.GetOrientation());
    creature->SetReactState(REACT_AGGRESSIVE);
    if (target && target->IsAlive() && target->IsInWorld() && creature->AI())
        creature->AI()->AttackStart(target);
    else
        creature->GetMotionMaster()->MovePoint(0, goal.GetPositionX() + frand(-4.0f, 4.0f),
            goal.GetPositionY() + frand(-4.0f, 4.0f), goal.GetPositionZ());
}

// Those held back go, one or two at a time, as fewer fight.
void WowFatesExpeditionMgr::UpdateWaiting(WowFatesActiveExpedition& active, Map* map)
{
    if (active.waiting.empty() || !map)
        return;
    uint32 const cap = EngageCap(active);
    uint32 engaged = Engaged(active, map);
    uint32 sent = 0;
    for (auto itr = active.waiting.begin(); itr != active.waiting.end();)
    {
        Creature* creature = map->GetCreature(itr->guid);
        if (!creature || !creature->IsAlive() || creature->IsInCombat())
        {
            itr = active.waiting.erase(itr); // gone, or the players came to it
            continue;
        }
        if (engaged >= cap || sent >= 2)
            break;
        Unit* target = itr->target ? ObjectAccessor::GetUnit(*creature, itr->target) : nullptr;
        WowFatesActiveExpedition::Waiting const w = *itr;
        itr = active.waiting.erase(itr);
        Send(active, creature, target, w.goal);
        ++engaged;
        ++sent;
    }
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
    // once the area is filled, never many at once, none while the players
    // have their fill
    if (active.filled && Engaged(active, map) >= EngageCap(active))
        return;
    // Under the floor between two waves: up to the floor at once, the wave keeps its time.
    uint32 const wanted = (thin && active.summonTimer > 0) ? floor - alive
        : ((!active.filled || pace > 0.0f) ? population - alive : 1);
    bool const wave = !(thin && active.summonTimer > 0);
    uint32 const coming = active.filled ? std::min<uint32>(wanted, 2) : wanted;
    for (uint32 i = 0; i < coming; ++i)
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
// back if it went away alive (a grid unloaded). Its health follows the
// players in the area, as the objective does, its share kept; the bar shows
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

    for (auto itr = active.guards.begin(); itr != active.guards.end();)
        if (std::find(active.objects.begin(), active.objects.end(), itr->first) == active.objects.end())
            itr = active.guards.erase(itr);
        else
            ++itr;

    WowFatesExpeditionDef const* def = active.def;
    if (active.objects.size() >= def->gatherCount || !map->IsGridLoaded(active.spawn.x, active.spawn.y))
        return;
    // a rescue's cages, once its way out is known (UpdateRescue): each walked to it
    if (def->objectiveType == WOW_FATES_OBJECTIVE_RESCUE && active.route.empty())
        return;
    // the first ones together; then one at a time, each in its own time
    bool const all = !active.gatherFilled;
    if (!all && active.gatherTimer > 0)
        return;

    uint32 const wanted = all ? def->gatherCount - uint32(active.objects.size()) : 1;
    active.gatherFilled = true;

    // Each object on its own ground, never on another nor in a building of the
    // place: what does not find room waits, fewer at once rather than piled up.
    GameObjectTemplate const* goInfo = sObjectMgr->GetGameObjectTemplate(def->gatherEntry);
    float const footprint = Footprint(goInfo ? goInfo->displayId : 0, goInfo ? goInfo->size : 1.0f);
    std::vector<Footing> obstacles = Obstacles(active, map);
    GameObject* walker = Walker(active, map);
    uint32 placed = 0;
    for (uint32 i = 0; i < wanted; ++i)
    {
        Position pos;
        if (!ObjectPoint(active, map, walker, footprint, obstacles, pos))
            break;
        obstacles.push_back({ pos.GetPositionX(), pos.GetPositionY(), footprint });
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
        ++placed;
        std::vector<ObjectGuid>& itsGuards = active.guards[go->GetGUID()];

        // a sabotage's object has its guards (wave_size of summon_entry), close
        // by, back to it after a fight
        if (def->objectiveType == WOW_FATES_OBJECTIVE_SABOTAGE && def->summonEntry)
            for (uint32 g = 0; g < def->waveSize; ++g)
            {
                Position at;
                GuardPoint(active, map, pos, footprint, obstacles, at);
                TempSummon* guard = map->SummonCreature(def->summonEntry, at);
                if (!guard)
                    break;
                guard->SetTempSummonType(TEMPSUMMON_CORPSE_TIMED_DESPAWN);
                guard->SetTimer(SUMMON_CORPSE_MS);
                guard->SetHomePosition(at);
                guard->SetWanderDistance(3.0f);
                guard->SetDefaultMovementType(RANDOM_MOTION_TYPE);
                guard->GetMotionMaster()->Initialize();
                active.summons.push_back(guard->GetGUID());
                itsGuards.push_back(guard->GetGUID());
            }
    }
    // no room for all: the others are tried again soon
    active.gatherTimer = placed < wanted ? int64(10 * IN_MILLISECONDS)
        : int64(float(def->gatherRespawn) * frand(0.6f, 1.4f) * IN_MILLISECONDS);
    if (placed < wanted)
        LOG_DEBUG("module", "mod-wow-fates: Expedition {}: room for {} of {} objects, the others wait.", def->id, placed, wanted);
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
        // a capture's banner of the foe: ours now
        if (active.def->objectiveType == WOW_FATES_OBJECTIVE_CAPTURE)
        {
            auto flag = std::find_if(active.flags.begin(), active.flags.end(), [go](auto const& f) { return f.go == go->GetGUID(); });
            if (flag == active.flags.end())
                continue;
            if (!flag->ours && !active.failed && active.progressPct < 100.0)
            {
                PutFlag(active, go->GetMap(), *flag, true);
                ++active.contributors[player->GetGUID()].picks;
                Active(active);
            }
            return;
        }

        auto itr = std::find(active.objects.begin(), active.objects.end(), go->GetGUID());
        if (itr == active.objects.end())
            continue;

        active.objects.erase(itr);
        go->DespawnOrUnsummon();
        if (active.progressPct >= 100.0)
            return;

        ++active.contributors[player->GetGUID()].picks;
        Active(active);
        // a rescue's cage: the bar fills as its captive is led out, not now
        if (active.def->objectiveType == WOW_FATES_OBJECTIVE_RESCUE)
            FreeCaptive(active, go);
        else
        {
            ++active.progress;
            active.progressPct = std::min(100.0, active.progressPct + 100.0 / Needed(active));
        }

        // a sabotage: the object blows up, its guards (and those near) come
        if (active.def->objectiveType == WOW_FATES_OBJECTIVE_SABOTAGE)
        {
            // it goes up in flames: blasts around it, a fire on it for a while
            Map* map = go->GetMap();
            for (uint32 i = 0; i < 3; ++i)
            {
                float const angle = frand(0.0f, 2.0f * float(M_PI));
                float const reach = i ? frand(1.5f, 3.0f) : 0.0f;
                Position const at(go->GetPositionX() + reach * std::cos(angle), go->GetPositionY() + reach * std::sin(angle),
                    go->GetPositionZ() + frand(0.5f, 2.0f), 0.0f);
                if (TempSummon* bunny = map->SummonCreature(WOW_FATES_ALTAR_FOCUS, at, nullptr, 8 * IN_MILLISECONDS))
                {
                    bunny->CastSpell(bunny, WOW_FATES_WRECK_SPELL, true);
                    if (!i)
                        bunny->SummonGameObject(WOW_FATES_WRECK_FIRE, go->GetPositionX(), go->GetPositionY(), go->GetPositionZ(),
                            go->GetOrientation(), 0.0f, 0.0f, 0.0f, 0.0f, 6);
                }
            }
            // its own guards come, as many as the players may take on
            uint32 const cap = EngageCap(active);
            uint32 engaged = Engaged(active, map);
            for (ObjectGuid const& guid : active.guards[go->GetGUID()])
                if (Creature* keeper = map->GetCreature(guid))
                    if (keeper->IsAlive() && !keeper->IsInCombat() && keeper->AI())
                    {
                        if (engaged >= cap)
                            break;
                        keeper->AI()->AttackStart(player);
                        ++engaged;
                    }
            active.guards.erase(go->GetGUID());
        }
        if (active.gatherTimer <= 0)
            active.gatherTimer = int64(float(active.def->gatherRespawn) * frand(0.6f, 1.4f) * IN_MILLISECONDS);
        return;
    }

    // left over from an Expedition that ended
    go->DespawnOrUnsummon();
}

// ---------------------------------------------------------------------------
// Defence, and the waves

// The health of a creature for the players in the area, as the objective:
// its maximum follows them up and down, its share of health stays (78 % with
// fifteen players more is still 78 %), never a death or a full heal from the
// rounding.
void WowFatesExpeditionMgr::ScaleHealth(Creature* creature, uint32 baseHealth, uint32& setFor, uint32 players) const
{
    if (players == setFor || !creature->IsAlive() || !baseHealth)
        return;
    double const share = double(creature->GetHealth()) / double(std::max<uint32>(creature->GetMaxHealth(), 1));
    uint32 const maxHealth = uint32(float(baseHealth) * (1.0f + WowFatesConf().participantScaling * float(players - 1)));
    creature->SetCreateHealth(maxHealth);
    creature->SetStatFlatModifier(UNIT_MOD_HEALTH, BASE_VALUE, float(maxHealth));
    creature->UpdateMaxHealth();
    uint32 const max = creature->GetMaxHealth();
    uint32 health = uint32(std::lround(double(max) * share));
    if (share < 1.0 && health >= max && max > 1)
        health = max - 1;
    creature->SetHealth(std::clamp<uint32>(health, 1, max));
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
// ground there, not much higher or lower than the centre, dry, and walked to
// the centre, as the monsters' points; a few angles around it, else none.
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
        if (z <= INVALID_HEIGHT || std::fabs(z - c.z) > 25.0f || NearPlayer(active, x, y))
            continue;
        bool outdoors = false;
        if (Wet(map, x, y, z, &outdoors) || (NeedsSky(active.def) && !outdoors))
            continue;
        Position const pos(x, y, z, std::atan2(c.y - y, c.x - x));
        if (!WalkPath(map, active.walker, pos, Position(c.x, c.y, c.z, 0.0f)))
            continue;
        out = pos;
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
void WowFatesExpeditionMgr::SendWave(WowFatesActiveExpedition& active, Map* map, Unit* target, Position const* around,
    Position const* goal)
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
        // from the edge of the area, or around a point (an escort's ally, a warlord)
        Position pos;
        float const angle = start + gap * float(i) + frand(-0.25f, 0.25f) * gap;
        if (around ? AroundPoint(active, map, *around, angle, pos) : EdgePoint(active, map, angle, pos))
            entries.push_back(pos);
    }
    if (entries.empty())
        entries.push_back(SpawnPoint(active, map));

    WowFatesSpawnPoint const& centre = active.spawn;
    Position const to = goal ? *goal : (target ? target->GetPosition() : Centre(active, map));
    uint32 const cap = EngageCap(active);
    uint32 engaged = Engaged(active, map);
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
        active.summons.push_back(summon->GetGUID());
        active.waveMobs.push_back(summon->GetGUID());
        if (engaged < cap)
        {
            Send(active, summon, target, to);
            ++engaged;
        }
        else
        {
            // the players have enough on them: it waits at its side
            summon->SetHomePosition(pos);
            active.waiting.push_back({ summon->GetGUID(), target ? target->GetGUID() : ObjectGuid::Empty, to });
        }
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
        ALLY_SAY_FALL    = 4,
        ALLY_SAY_HEALTH_75 = 5, // his own health at 75, 50, 25 and 10 %: 5 to 8
    };

    // The elite's (its creature_text groups).
    enum WowFatesBossTalk : uint8
    {
        BOSS_SAY_START = 0,
        BOSS_SAY_75    = 1,
        BOSS_SAY_50    = 2,
        BOSS_SAY_25    = 3,
        BOSS_SAY_10    = 4,
        BOSS_SAY_DEATH = 5,
        BOSS_SAY_TIMEOUT = 6 // a warlord: the time out, the players beaten
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

    // out as soon as the area is loaded, fighting for show until a player comes
    if (!active.started)
    {
        if (!map->IsGridLoaded(active.spawn.x, active.spawn.y))
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
            // the figure the players come to help, bigger than its kind
            ally->SetObjectScale(ally->GetObjectScale() * WowFatesConf().bossScale);
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
        Say(boss, BOSS_SAY_DEATH);
        Say(ally, ALLY_SAY_VICTORY);
        active.progressPct = 100.0;
        return;
    }
    ScaleHealth(boss, active.bossHealth, active.bossPlayers, players);

    // their words, only with players to hear them: the elite's when the true
    // fight begins and at 75, 50, 25 and 10 % of its health, once each
    if (!active.inside.empty() && boss->IsAlive())
    {
        if (!(active.bossSaid & 1))
        {
            active.bossSaid |= 1;
            Say(boss, BOSS_SAY_START);
        }
        static uint8 const marks[] = { 75, 50, 25, 10 };
        for (uint8 i = 0; i < 4; ++i)
        {
            uint8 const bit = uint8(2 << i);
            if (!(active.bossSaid & bit) && boss->GetHealthPct() <= float(marks[i]))
            {
                active.bossSaid |= bit;
                Say(boss, uint8(BOSS_SAY_75 + i));
                break;
            }
        }
    }

    // the ally's at 75, 50, 25 and 10 % of his own health, once each
    if (ally && ally->IsAlive() && !active.inside.empty())
    {
        static uint8 const marks[] = { 75, 50, 25, 10 };
        for (uint8 i = 0; i < 4; ++i)
        {
            uint8 const bit = uint8(1 << i);
            if (!(active.allySaidHealth & bit) && ally->GetHealthPct() <= float(marks[i]))
            {
                active.allySaidHealth |= bit;
                Say(ally, uint8(ALLY_SAY_HEALTH_75 + i));
                active.talkTimer = urand(20, 40) * IN_MILLISECONDS;
                break;
            }
        }
    }

    // the elite's pack at the bar's marks (none without players: the bar does
    // not move then), from several sides, at the ally or one of the players
    if (!active.inside.empty() && map->IsGridLoaded(active.spawn.x, active.spawn.y))
        for (std::size_t i = 0; i < def->bossWaves.size() && i < 32; ++i)
        {
            uint32 const bit = 1u << i;
            if ((active.bossWavesSent & bit) || active.progressPct < double(def->bossWaves[i]))
                continue;
            active.bossWavesSent |= bit;
            Unit* target = ally && ally->IsAlive() ? static_cast<Unit*>(ally) : nullptr;
            if (urand(0, 1) || !target)
                if (Player* player = ObjectAccessor::FindPlayer(Acore::Containers::SelectRandomContainerElement(active.inside)))
                    if (player->IsInWorld() && player->IsAlive())
                        target = player;
            SendWave(active, map, target);
            break;
        }

    // the ally's: at the start, now and then, at the elite's half health
    if (ally && ally->IsAlive() && !active.inside.empty())
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

        // the elite's own (the Expedition's monsters, come back or not) hold
        // the ally on their list: with no player at hand, they go for him
        for (ObjectGuid const& guid : active.summons)
            if (Creature* creature = map->GetCreature(guid))
                if (creature->IsAlive() && creature->IsWithinDist(ally, def->radius))
                {
                    creature->AddThreat(ally, 0.0f);
                    if (!creature->IsInCombat() && creature->AI())
                        creature->AI()->AttackStart(ally);
                }
    }

    double const lost = 1.0 - double(boss->GetHealth()) / double(std::max<uint32>(boss->GetMaxHealth(), 1));
    active.progressPct = std::max(active.progressPct, std::min(100.0 * lost, 99.0));
}

// ---------------------------------------------------------------------------
// Escort

namespace
{
    // The lines of an escort's ally (its creature_text groups).
    enum WowFatesEscortTalk : uint8
    {
        ESCORT_SAY_START  = 0, // the first player comes: off he goes
        ESCORT_SAY_AMBUSH = 1,
        ESCORT_SAY_WAIT   = 2, // nobody near him: he waits, and says so every 30 s
        ESCORT_SAY_END    = 3, // the end of the route
        ESCORT_SAY_FALL   = 4
    };

    constexpr float ESCORT_REACH = 25.0f; // yards: with a player this near, he walks
}

// The ally waits at the start of its route (BuildRoute, once the area is
// loaded: a route that cannot be walked, and the point is never used again);
// the first player in the area starts it. He walks the route while a player
// is within ESCORT_REACH of him, waits otherwise; he fights what attacks him
// and goes on from there. Ambushes at the bar's marks (boss_waves), from
// around him, at him. His health grows with the players in the area. The bar
// is the route behind him; at its end, done; dead, failed. Gone alive (a grid
// unloaded), he comes back where he was on it.
void WowFatesExpeditionMgr::UpdateEscort(WowFatesActiveExpedition& active, Map* map, uint32 diff)
{
    if (!map || active.failed || active.progressPct >= 100.0 || active.remaining <= 0)
        return;
    WowFatesExpeditionDef const* def = active.def;

    if (active.route.empty())
    {
        if (!map->IsGridLoaded(active.spawn.x, active.spawn.y))
            return;
        std::string why;
        if (!BuildRoute(active, map, &why))
        {
            Broken(active, "its route: " + why);
            return;
        }
    }

    Creature* ally = active.ally ? map->GetCreature(active.ally) : nullptr;
    if (ally && !ally->IsAlive())
    {
        active.allyHealthPct = 0;
        Say(ally, ESCORT_SAY_FALL);
        LOG_INFO("module", "mod-wow-fates: Expedition {}: its ally died, {} % of the route behind him.", def->id, uint32(active.progressPct));
        active.failed = true;
        return;
    }
    std::size_t const last = active.route.size() - 1;
    if (!ally)
    {
        Position const& at = active.route[active.routeIndex];
        if (!map->IsGridLoaded(at.GetPositionX(), at.GetPositionY()))
            return;
        Position const& next = active.route[std::min<std::size_t>(active.routeIndex + 1, last)];
        ally = SpawnAlly(active, map, Position(at.GetPositionX(), at.GetPositionY(), at.GetPositionZ(),
            std::atan2(next.GetPositionY() - at.GetPositionY(), next.GetPositionX() - at.GetPositionX())));
        if (!ally)
            return;
        ally->SetWalk(true);
        active.ally = ally->GetGUID();
        active.allyPlayers = 1;
    }

    uint32 const players = uint32(std::max<std::size_t>(active.inside.size(), 1));
    ScaleHealth(ally, active.allyHealth, active.allyPlayers, players);
    active.allyHealthPct = std::max<int32>(5, int32(std::ceil(ally->GetHealthPct() / 5.0f)) * 5);

    if (!Start(active, "escort", &WowFatesExpeditionMgr::CreatureName, def->allyEntry))
        return;
    if (!(active.said & 1))
    {
        active.said |= 1;
        Say(ally, ESCORT_SAY_START);
        active.talkTimer = 30 * IN_MILLISECONDS;
    }

    // how far along: the farthest of the next points he stands at
    for (std::size_t j = active.routeIndex + 1; j <= std::min<std::size_t>(active.routeIndex + 12, last); ++j)
        if (ally->GetExactDist2d(active.route[j].GetPositionX(), active.route[j].GetPositionY()) < 4.0f)
            active.routeIndex = uint32(j);
    if (active.routeIndex >= last)
    {
        Say(ally, ESCORT_SAY_END);
        ally->StopMoving();
        active.progressPct = 100.0;
        return;
    }
    double const total = std::max<double>(active.routeAt.back(), 1.0);
    active.progressPct = std::max(active.progressPct, std::min(99.0, 100.0 * double(active.routeAt[active.routeIndex]) / total));

    // the ambushes at the bar's marks, around him
    for (std::size_t i = 0; i < def->bossWaves.size() && i < 32; ++i)
    {
        uint32 const bit = 1u << i;
        if ((active.bossWavesSent & bit) || active.progressPct < double(def->bossWaves[i]))
            continue;
        active.bossWavesSent |= bit;
        Say(ally, ESCORT_SAY_AMBUSH);
        Position const around = ally->GetPosition();
        SendWave(active, map, ally, &around);
        break;
    }

    if (ally->IsInCombat())
        return;
    ally->SetHomePosition(ally->GetPosition()); // back from a fight: here, not at the start

    bool followed = false;
    for (ObjectGuid const& guid : active.inside)
        if (Player* player = ObjectAccessor::FindPlayer(guid))
            if (player->IsInWorld() && player->IsAlive() && player->GetMap() == map && player->GetExactDist2d(ally) <= ESCORT_REACH)
            {
                followed = true;
                break;
            }
    MotionMaster* motion = ally->GetMotionMaster();
    if (!followed)
    {
        if (motion->GetCurrentMovementGeneratorType() == POINT_MOTION_TYPE)
        {
            motion->Clear();
            ally->StopMoving();
        }
        active.talkTimer -= diff;
        if (active.talkTimer <= 0)
        {
            Say(ally, ESCORT_SAY_WAIT);
            active.talkTimer = 30 * IN_MILLISECONDS;
        }
        return;
    }
    active.talkTimer = std::max<int64>(active.talkTimer, 15 * IN_MILLISECONDS);
    if (motion->GetCurrentMovementGeneratorType() != POINT_MOTION_TYPE)
        motion->MovePoint(1, active.route[std::min<std::size_t>(active.routeIndex + 5, last)], FORCED_MOVEMENT_WALK);
}

// ---------------------------------------------------------------------------
// Rescue

namespace
{
    // The lines of a rescue's captives (their creature_text groups).
    enum WowFatesCaptiveTalk : uint8
    {
        CAPTIVE_SAY_FREED = 0, // out of his cage
        CAPTIVE_SAY_OUT   = 1  // at the way out
    };
}

// The way out, the last point of the spawn point's path, walked from the
// centre once the area is loaded (else the point is never used again); the
// cages are a gathering's objects (UpdateGather), each walked to the way out
// as well. A cage opened (FreeCaptive), its captive runs for the way out by
// the paths, fights what stops him and runs on; there, he is led out. The bar
// is the captives led out against objective_count (more with the players, as
// the kills); more than max_losses dead, it fails.
void WowFatesExpeditionMgr::UpdateRescue(WowFatesActiveExpedition& active, Map* map)
{
    if (!map || active.failed || active.progressPct >= 100.0 || active.remaining <= 0)
        return;
    WowFatesExpeditionDef const* def = active.def;

    if (active.route.empty())
    {
        if (!map->IsGridLoaded(active.spawn.x, active.spawn.y))
            return;
        map->LoadGridsInRange(Position(active.spawn.x, active.spawn.y, active.spawn.z, 0.0f), def->radius + 30.0f);
        Position const& way = active.spawn.path.back();
        if (!WalkPath(map, Walker(active, map), Position(active.spawn.x, active.spawn.y, active.spawn.z, 0.0f), way))
        {
            Broken(active, Acore::StringFormat("its way out ({:.1f} {:.1f} {:.1f}) is not walked from the centre",
                way.GetPositionX(), way.GetPositionY(), way.GetPositionZ()));
            return;
        }
        active.route = { way };
    }
    Start(active, "rescue", &WowFatesExpeditionMgr::CreatureName, def->allyEntry);

    Position const& way = active.route.front();
    for (auto itr = active.allies.begin(); itr != active.allies.end();)
    {
        Creature* captive = map->GetCreature(*itr);
        if (!captive)
        {
            itr = active.allies.erase(itr); // gone with his grid: neither led out nor lost
            continue;
        }
        if (!captive->IsAlive())
        {
            itr = active.allies.erase(itr);
            captive->DespawnOrUnsummon(Milliseconds(SUMMON_CORPSE_MS));
            ++active.lost;
            LOG_INFO("module", "mod-wow-fates: Expedition {}: a captive died, {} lost ({} may be).", def->id, active.lost, def->maxLosses);
            if (active.lost > def->maxLosses)
            {
                active.failed = true;
                return;
            }
            continue;
        }
        if (captive->GetExactDist2d(way.GetPositionX(), way.GetPositionY()) < 5.0f)
        {
            itr = active.allies.erase(itr);
            Say(captive, CAPTIVE_SAY_OUT);
            captive->DespawnOrUnsummon(Milliseconds(3000));
            ++active.saved;
            ++active.progress;
            active.progressPct = std::min(100.0, active.progressPct + 100.0 / Needed(active));
            Active(active);
            continue;
        }
        if (!captive->IsInCombat())
        {
            captive->SetHomePosition(captive->GetPosition()); // back from a fight: here, not at his cage
            if (captive->GetMotionMaster()->GetCurrentMovementGeneratorType() != POINT_MOTION_TYPE)
                captive->GetMotionMaster()->MovePoint(1, way);
        }
        ++itr;
    }
}

// A cage opened (OnGather, its map's thread): its captive out, thankful,
// running for the way out.
void WowFatesExpeditionMgr::FreeCaptive(WowFatesActiveExpedition& active, GameObject* cage)
{
    Map* map = cage->GetMap();
    TempSummon* captive = map->SummonCreature(active.def->allyEntry, cage->GetPosition());
    if (!captive)
        return;
    captive->SetTempSummonType(TEMPSUMMON_MANUAL_DESPAWN);
    captive->SetPvP(false); // faction 495: healing him would flag the healer
    captive->SetHomePosition(cage->GetPosition());
    captive->SetDefaultMovementType(IDLE_MOTION_TYPE);
    captive->GetMotionMaster()->Initialize();
    Say(captive, CAPTIVE_SAY_FREED);
    if (!active.route.empty())
        captive->GetMotionMaster()->MovePoint(1, active.route.front());
    active.allies.push_back(captive->GetGUID());
}

// ---------------------------------------------------------------------------
// Capture

namespace
{
    constexpr float CAPTURE_FOE_REACH = 8.0f;              // yards: the foe at the foot of a banner
    constexpr float CAPTURE_GUARD_REACH = 15.0f;           // yards: a player this near keeps it
    constexpr int64 CAPTURE_RETAKE = 10 * IN_MILLISECONDS; // the foe there that long, nobody near: his again
}

// The time left to a timed one (capture, warlord), in ms: its whole time
// before its first player; -1 when it has none.
int64 WowFatesExpeditionMgr::TimeLeft(WowFatesActiveExpedition const& active) const
{
    if (!active.def->timeLimit)
        return -1;
    if (!active.deadline)
        return int64(active.def->timeLimit) * IN_MILLISECONDS;
    return std::max<int64>(0, active.deadline - active.elapsed);
}

// A capture's banner put up on its place: the foe's (gather_entry, taken by a
// one second cast) or ours (taken_entry, nothing to do with it); the one
// standing there goes.
void WowFatesExpeditionMgr::PutFlag(WowFatesActiveExpedition& active, Map* map, WowFatesActiveExpedition::Flag& flag, bool ours)
{
    if (flag.go)
        if (GameObject* old = map->GetGameObject(flag.go))
            old->DespawnOrUnsummon();
    flag.go.Clear();
    flag.ours = ours;
    flag.contest = 0;
    Position const& pos = flag.pos;
    GameObject* go = new GameObject();
    if (!go->Create(map->GenerateLowGuid<HighGuid::GameObject>(), ours ? active.def->takenEntry : active.def->gatherEntry, map,
            PHASEMASK_NORMAL, pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ(), pos.GetOrientation(),
            G3D::Quat(), 100, GO_STATE_READY))
    {
        delete go;
        return;
    }
    go->SetRespawnTime(0);
    go->SetSpellId(1);
    go->SetSpawnedByDefault(false);
    map->AddToMap(go);
    flag.go = go->GetGUID();
}

// Its banners, gather_count of them (two at least), the foe's, placed once
// the area is loaded, far apart (FlagPoint: each walked from the centre);
// none placed, tried again ten seconds later. The first player starts the
// clock (time_limit). A banner is taken by a one second cast (OnGather): held,
// it fills the bar, hold_time seconds of holding them all making it full (one
// of three, three times as long). While one is held, every wave_interval the
// foe sends a squad (summon_entry, as a wave) at one of ours: his monsters at
// its foot (CAPTURE_FOE_REACH) with no player near (CAPTURE_GUARD_REACH) for
// CAPTURE_RETAKE, it is his again. The clock out before the bar is full, it
// fails. Gone (a grid unloaded), a banner is put back as it was.
void WowFatesExpeditionMgr::UpdateCapture(WowFatesActiveExpedition& active, Map* map, uint32 diff)
{
    if (!map || active.failed || active.progressPct >= 100.0 || active.remaining <= 0)
        return;
    WowFatesExpeditionDef const* def = active.def;
    if (!map->IsGridLoaded(active.spawn.x, active.spawn.y))
        return;

    if (active.flags.empty())
    {
        if (active.gatherTimer > 0)
            return;
        GameObjectTemplate const* info = sObjectMgr->GetGameObjectTemplate(def->gatherEntry);
        float const footprint = Footprint(info ? info->displayId : 0, info ? info->size : 1.0f);
        std::vector<Footing> obstacles = Obstacles(active, map);
        std::vector<Position> placed;
        for (uint32 i = 0; i < def->gatherCount; ++i)
        {
            Position pos;
            if (!FlagPoint(active, map, footprint, obstacles, placed, pos))
                break;
            obstacles.push_back({ pos.GetPositionX(), pos.GetPositionY(), footprint });
            placed.push_back(pos);
        }
        if (placed.size() < 2)
        {
            LOG_DEBUG("module", "mod-wow-fates: Expedition {}: room for {} banners, two at least; tried again.", def->id, uint32(placed.size()));
            active.gatherTimer = 10 * IN_MILLISECONDS;
            return;
        }
        for (Position const& pos : placed)
        {
            WowFatesActiveExpedition::Flag flag;
            flag.pos = pos;
            PutFlag(active, map, flag, false);
            active.flags.push_back(flag);
        }
    }

    uint32 ours = 0;
    for (WowFatesActiveExpedition::Flag& flag : active.flags)
    {
        GameObject* go = flag.go ? map->GetGameObject(flag.go) : nullptr;
        if (!go || !go->isSpawned())
        {
            if (map->IsGridLoaded(flag.pos.GetPositionX(), flag.pos.GetPositionY()))
                PutFlag(active, map, flag, flag.ours);
            ours += flag.ours ? 1 : 0;
            continue;
        }
        if (!flag.ours)
            continue;
        ++ours;
        if (!active.started)
            continue;

        bool foe = false, kept = false;
        for (ObjectGuid const& guid : active.summons)
            if (Creature* creature = map->GetCreature(guid))
                if (creature->IsAlive() && creature->GetExactDist2d(flag.pos.GetPositionX(), flag.pos.GetPositionY()) <= CAPTURE_FOE_REACH)
                {
                    foe = true;
                    break;
                }
        for (ObjectGuid const& guid : active.inside)
            if (Player* player = ObjectAccessor::FindPlayer(guid))
                if (player->IsInWorld() && player->IsAlive() && player->GetMap() == map
                    && player->GetExactDist2d(flag.pos.GetPositionX(), flag.pos.GetPositionY()) <= CAPTURE_GUARD_REACH)
                {
                    kept = true;
                    break;
                }
        if (!foe || kept)
        {
            flag.contest = 0;
            continue;
        }
        flag.contest += diff;
        if (flag.contest >= CAPTURE_RETAKE)
        {
            PutFlag(active, map, flag, false);
            --ours;
            LOG_INFO("module", "mod-wow-fates: Expedition {}: the foe took a banner back, {} held.", def->id, ours);
        }
    }
    active.alliesAlive = int32(ours);

    if (!Start(active, "capture", &WowFatesExpeditionMgr::GameObjectName, def->gatherEntry))
        return;
    if (def->timeLimit && !active.deadline)
        active.deadline = active.elapsed + int64(def->timeLimit) * IN_MILLISECONDS;
    if (active.deadline && active.elapsed >= active.deadline)
    {
        LOG_INFO("module", "mod-wow-fates: Expedition {}: the time is out, {} % held.", def->id, uint32(active.progressPct));
        active.failed = true;
        return;
    }

    if (!ours)
    {
        active.waveTimer = int64(def->waveInterval) * IN_MILLISECONDS; // the foe's squads, once one is held
        return;
    }
    double const full = double(std::max<uint32>(def->holdTime, 1)) * IN_MILLISECONDS;
    active.progressPct = std::min(100.0, active.progressPct + 100.0 * double(diff) * double(ours) / double(active.flags.size()) / full);
    if (!active.inside.empty())
        Active(active);

    active.waveTimer -= diff;
    if (active.waveTimer <= 0)
    {
        active.waveTimer = int64(def->waveInterval) * IN_MILLISECONDS;
        std::vector<Position> held;
        for (WowFatesActiveExpedition::Flag const& flag : active.flags)
            if (flag.ours)
                held.push_back(flag.pos);
        Position const goal = Acore::Containers::SelectRandomContainerElement(held);
        SendWave(active, map, nullptr, nullptr, &goal);
    }
}

// ---------------------------------------------------------------------------
// Warlord

// A unique elite (boss_entry, always an elite, bigger by Boss.Scale) in the
// centre as soon as the area is loaded; the game's own figure it stands for
// (objective_entry, if any) kept out of the area meanwhile, back two minutes
// after it goes. The first player starts the fight and its clock
// (time_limit). Its health follows the players in the area; the bar is the
// health it lost, never going back. Each mark of boss_waves passed is a
// phase: its words, reinforcements (summon_entry, as a wave) from around it
// at one of the players, and one more of its blows (boss_spells, the first
// from the start; one on itself, an enrage, cast once then). In a fight, one
// of its blows every 8 to 14 s at its victim. Dead, done; the clock out
// first, it fails. Gone alive (a grid unloaded), it comes back as it was.
void WowFatesExpeditionMgr::UpdateWarlord(WowFatesActiveExpedition& active, Map* map, uint32 diff)
{
    if (!map || active.failed || active.progressPct >= 100.0 || active.remaining <= 0)
        return;
    WowFatesExpeditionDef const* def = active.def;
    if (!map->IsGridLoaded(active.spawn.x, active.spawn.y))
        return;

    active.hideTimer -= diff;
    if (def->objectiveEntry && active.hideTimer <= 0)
    {
        active.hideTimer = 5 * IN_MILLISECONDS;
        std::vector<Creature*> others;
        for (auto const& [spawnId, creature] : map->GetCreatureBySpawnIdStore())
            if (creature && creature->IsInWorld() && creature->IsAlive() && creature->GetEntry() == def->objectiveEntry
                && creature->GetExactDist2d(active.spawn.x, active.spawn.y) <= def->radius + 20.0f)
                others.push_back(creature);
        for (Creature* creature : others)
            creature->DespawnOrUnsummon(0ms, Seconds(120));
    }

    Creature* boss = active.boss ? map->GetCreature(active.boss) : nullptr;
    if (!boss)
    {
        bool const first = !active.bossOut;
        Position const pos = Centre(active, map);
        TempSummon* summon = map->SummonCreature(def->bossEntry, pos);
        if (!summon)
            return;
        summon->SetTempSummonType(TEMPSUMMON_CORPSE_TIMED_DESPAWN);
        summon->SetTimer(SUMMON_CORPSE_MS);
        summon->SetObjectScale(summon->GetObjectScale() * WowFatesConf().bossScale);
        summon->SetHomePosition(pos);
        summon->SetWanderDistance(5.0f);
        summon->SetDefaultMovementType(RANDOM_MOTION_TYPE);
        summon->GetMotionMaster()->Initialize();
        boss = summon;
        active.boss = boss->GetGUID();
        active.bossOut = true;
        active.bossHealth = boss->GetMaxHealth();
        active.bossPlayers = 1;
        if (!first)
            boss->SetHealth(std::max<uint32>(1, uint32(boss->GetMaxHealth() * (1.0 - active.progressPct / 100.0))));
        LOG_INFO("module", "mod-wow-fates: Expedition {}: its warlord appears ({} health).", def->id, boss->GetMaxHealth());
    }
    if (!boss->IsAlive())
    {
        Say(boss, BOSS_SAY_DEATH);
        active.progressPct = 100.0;
        return;
    }
    uint32 const players = uint32(std::max<std::size_t>(active.inside.size(), 1));
    ScaleHealth(boss, active.bossHealth, active.bossPlayers, players);
    double const lost = 1.0 - double(boss->GetHealth()) / double(std::max<uint32>(boss->GetMaxHealth(), 1));
    active.progressPct = std::max(active.progressPct, std::min(100.0 * lost, 99.0));

    if (!Start(active, "warlord", &WowFatesExpeditionMgr::CreatureName, def->bossEntry))
        return;
    if (!(active.bossSaid & 1))
    {
        active.bossSaid |= 1;
        Say(boss, BOSS_SAY_START);
        if (def->timeLimit)
            active.deadline = active.elapsed + int64(def->timeLimit) * IN_MILLISECONDS;
        active.spellTimer = urand(4, 8) * IN_MILLISECONDS;
    }
    if (active.deadline && active.elapsed >= active.deadline)
    {
        Say(boss, BOSS_SAY_TIMEOUT);
        LOG_INFO("module", "mod-wow-fates: Expedition {}: the time is out, its warlord at {} %.", def->id, uint32(100.0 - active.progressPct));
        active.failed = true;
        return;
    }

    // its phases, at the bar's marks
    for (std::size_t i = 0; i < def->bossWaves.size() && i < 32; ++i)
    {
        uint32 const bit = 1u << i;
        if ((active.bossWavesSent & bit) || active.progressPct < double(def->bossWaves[i]))
            continue;
        active.bossWavesSent |= bit;
        active.phase = uint32(i + 1);
        Say(boss, uint8(BOSS_SAY_75 + std::min<std::size_t>(i, 3)));
        if (active.phase < def->bossSpells.size())
            if (SpellInfo const* info = sSpellMgr->GetSpellInfo(def->bossSpells[active.phase]))
                if (info->IsPositive())
                    boss->CastSpell(boss, info->Id, true);
        Unit* target = boss->GetVictim();
        if (Player* player = ObjectAccessor::FindPlayer(Acore::Containers::SelectRandomContainerElement(active.inside)))
            if (player->IsInWorld() && player->IsAlive() && player->GetMap() == map)
                target = player;
        Position const around = boss->GetPosition();
        SendWave(active, map, target, &around);
        break;
    }

    // its blows, those of the phases passed
    if (boss->IsInCombat() && boss->GetVictim() && !def->bossSpells.empty())
    {
        active.spellTimer -= diff;
        if (active.spellTimer <= 0 && !boss->HasUnitState(UNIT_STATE_CASTING))
        {
            active.spellTimer = urand(8, 14) * IN_MILLISECONDS;
            std::vector<uint32> pool;
            for (std::size_t i = 0; i <= active.phase && i < def->bossSpells.size(); ++i)
                if (SpellInfo const* info = sSpellMgr->GetSpellInfo(def->bossSpells[i]))
                    if (!info->IsPositive())
                        pool.push_back(info->Id);
            if (!pool.empty())
                boss->CastSpell(boss->GetVictim(), Acore::Containers::SelectRandomContainerElement(pool), false);
        }
    }
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

bool WowFatesExpeditionMgr::IsForShow(Unit const* unit)
{
    if (!_activeCount || !unit || !unit->IsCreature() || IsFromPlayer(unit))
        return false;
    std::lock_guard<std::recursive_mutex> guard(_lock);
    WowFatesActiveExpedition* active = FindAt(unit);
    if (!active || !active->inside.empty())
        return false;
    uint8 const type = active->def->objectiveType;
    return type == WOW_FATES_OBJECTIVE_BOSS_SUPPORT || type == WOW_FATES_OBJECTIVE_ESCORT || type == WOW_FATES_OBJECTIVE_RESCUE
        || type == WOW_FATES_OBJECTIVE_CAPTURE || type == WOW_FATES_OBJECTIVE_WARLORD;
}

void WowFatesExpeditionMgr::OnDamage(Unit* attacker, Unit* victim, uint32& damage)
{
    if (!_activeCount || !damage || !attacker || !victim || attacker == victim)
        return;
    if (!IsFromPlayer(attacker) && IsForShow(victim))
    {
        damage = 0;
        return;
    }
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
    if ((def->objectiveType == WOW_FATES_OBJECTIVE_ASSAULT || def->objectiveType == WOW_FATES_OBJECTIVE_BOSS_SUPPORT
            || def->objectiveType == WOW_FATES_OBJECTIVE_WARLORD)
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

// Every point and path of an Expedition (0: of all), as the module takes them:
// its centre on dry ground (under the sky for a sabotage), the monsters'
// points and the edges the waves come from, the objects of its type placed one
// after the other (gathering, sabotage, rescue, capture), an escort's route
// and a rescue's way out, each walked as a player would (WalkPath). Its grids
// are loaded for it. One line per point, "ok" or what fails.
void WowFatesExpeditionMgr::Check(ChatHandler* handler, uint32 id)
{
    std::lock_guard<std::recursive_mutex> guard(_lock);

    uint32 checked = 0, failing = 0;
    for (auto const& [defId, def] : _defs)
    {
        if (id && defId != id)
            continue;
        for (uint32 spawnId : def.spawns)
        {
            WowFatesSpawnPoint const& spawn = _spawns[spawnId];
            Map* map = sMapMgr->CreateBaseMap(spawn.map);
            if (!map)
                continue;
            map->LoadGridsInRange(Position(spawn.x, spawn.y, spawn.z, 0.0f), def.radius + 30.0f);

            WowFatesActiveExpedition probe;
            probe.def = &def;
            probe.spawn = spawn;
            Walker(probe, map);
            std::vector<std::string> faults;
            std::string notes;
            Position const centre(spawn.x, spawn.y, spawn.z, 0.0f);

            float const ground = map->GetHeight(PHASEMASK_NORMAL, spawn.x, spawn.y, spawn.z + 3.0f, true, 10.0f);
            bool outdoors = false;
            if (ground <= INVALID_HEIGHT || std::fabs(ground - spawn.z) > 3.0f)
                faults.push_back(Acore::StringFormat("centre off the ground (z {:.1f}, ground {:.1f})", spawn.z, ground));
            else if (Wet(map, spawn.x, spawn.y, ground, &outdoors))
                faults.push_back("centre in water");
            else if (NeedsSky(&def) && !outdoors)
                faults.push_back("centre under a roof");

            uint32 monsters = 0;
            for (uint32 i = 0; i < 20; ++i)
            {
                Position pos;
                if (MonsterPoint(probe, map, pos))
                    ++monsters;
            }
            notes += Acore::StringFormat("monsters {}/20", monsters);
            if (monsters < 10)
                faults.push_back(Acore::StringFormat("monster points {}/20", monsters));

            uint32 edges = 0;
            for (uint32 i = 0; i < 8; ++i)
            {
                Position pos;
                if (EdgePoint(probe, map, float(i) * float(M_PI) / 4.0f, pos))
                    ++edges;
            }
            notes += Acore::StringFormat(", edges {}/8", edges);
            if (edges < 3)
                faults.push_back(Acore::StringFormat("wave edges {}/8", edges));

            uint8 const type = def.objectiveType;
            if (type == WOW_FATES_OBJECTIVE_ESCORT)
            {
                std::string why;
                if (BuildRoute(probe, map, &why))
                    notes += Acore::StringFormat(", route {:.0f} yd in {} points", probe.routeAt.back(), uint32(probe.route.size()));
                else
                    faults.push_back("route: " + why);
            }
            if (type == WOW_FATES_OBJECTIVE_RESCUE)
            {
                if (!spawn.path.empty() && WalkPath(map, probe.walker, centre, spawn.path.back()))
                {
                    probe.route = { spawn.path.back() };
                    notes += ", way out ok";
                }
                else
                    faults.push_back("way out not walked from the centre");
            }
            if (type == WOW_FATES_OBJECTIVE_GATHER || type == WOW_FATES_OBJECTIVE_SABOTAGE
                || type == WOW_FATES_OBJECTIVE_RESCUE || type == WOW_FATES_OBJECTIVE_CAPTURE)
            {
                GameObjectTemplate const* info = sObjectMgr->GetGameObjectTemplate(def.gatherEntry);
                float const footprint = Footprint(info ? info->displayId : 0, info ? info->size : 1.0f);
                std::vector<Footing> obstacles = Obstacles(probe, map);
                std::vector<Position> placed;
                uint32 const wanted = def.gatherCount;
                for (uint32 i = 0; i < wanted; ++i)
                {
                    Position pos;
                    if (type == WOW_FATES_OBJECTIVE_CAPTURE ? !FlagPoint(probe, map, footprint, obstacles, placed, pos)
                        : !ObjectPoint(probe, map, probe.walker, footprint, obstacles, pos))
                        break;
                    obstacles.push_back({ pos.GetPositionX(), pos.GetPositionY(), footprint });
                    placed.push_back(pos);
                }
                notes += Acore::StringFormat(", objects {}/{}", uint32(placed.size()), wanted);
                if (placed.size() < std::min<uint32>(wanted, type == WOW_FATES_OBJECTIVE_CAPTURE ? 2 : wanted))
                    faults.push_back(Acore::StringFormat("objects placed {}/{}", uint32(placed.size()), wanted));
            }

            delete probe.walker;
            probe.walker = nullptr;

            ++checked;
            std::string line = Acore::StringFormat("Expedition {} point {} ({:.1f} {:.1f} {:.1f}): {}", defId, spawnId,
                spawn.x, spawn.y, spawn.z, notes);
            if (faults.empty())
                line += " -- ok";
            else
            {
                ++failing;
                line += " -- FAIL:";
                for (std::string const& fault : faults)
                    line += " " + fault + ";";
            }
            handler->SendSysMessage(line);
            LOG_INFO("module", "mod-wow-fates: check: {}", line);
        }
    }
    std::string const summary = Acore::StringFormat("mod-wow-fates: {} points checked, {} failing.", checked, failing);
    handler->SendSysMessage(summary);
    LOG_INFO("module", "{}", summary);
}

void WowFatesExpeditionMgr::ListDefinitions(ChatHandler* handler)
{
    std::lock_guard<std::recursive_mutex> guard(_lock);

    for (auto const& [id, def] : _defs)
        handler->SendSysMessage(handler->PGetParseModuleString(WOW_FATES_MODULE, WOW_FATES_STR_CMD_DEFS_LINE,
            id, Name(&def, handler->GetSession()), uint32(def.level), uint32(def.spawns.size()), def.weight,
            _active.count(id) ? "active" : (_cooldowns.count(id) ? "cooldown" : "ready")));
}
