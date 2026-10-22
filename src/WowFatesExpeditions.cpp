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

// A capture's positions (UpdateCapture).
namespace
{
    constexpr float CAPTURE_RADIUS = 10.0f;    // yards: a position's circle
    constexpr float CAPTURE_HEIGHT = 8.0f;     // yards above or below its banner, inside
    constexpr uint32 CAPTURE_MARKERS = 8;      // torches around it
    constexpr uint32 WOW_FATES_CAPTURE_TORCH = 960005;
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
    _mechanics.clear();
    _cooldowns.clear();
    _indoors.clear();
    _broken.clear();

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
        "`time_limit`, `max_losses`, `taken_entry`, `boss_spells`, `caravan`, `mechanics_min`, `mechanics_max` "
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
            if (itr->second.objectiveType == WOW_FATES_OBJECTIVE_CAPTURE)
                itr->second.gatherCount = std::clamp<uint32>(itr->second.gatherCount, 2, 5); // a position alone is no capture
            itr->second.mechanicsMin = std::clamp<uint8>(f[20].Get<uint8>(), 0, 4);
            itr->second.mechanicsMax = std::clamp<uint8>(f[21].Get<uint8>(), itr->second.mechanicsMin, 4);
            // "96205,96205,96206": an escort's caravan, around its leader
            itr->second.caravan.clear();
            std::string const caravan = f[19].Get<std::string>();
            for (std::string_view member : Acore::Tokenize(caravan, ',', false))
                if (Optional<uint32> entry = Acore::StringTo<uint32>(member))
                {
                    if (sObjectMgr->GetCreatureTemplate(*entry))
                        itr->second.caravan.push_back(*entry);
                    else
                        LOG_ERROR("module", "mod-wow-fates: Expedition {}: creature {} of its caravan does not exist; left out.", itr->first, *entry);
                }
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

    // The warlords' mechanics, then who may appear in each.
    if (QueryResult result = WorldDatabase.Query(
        "SELECT `id`, `name`, `kind`, `spell`, `target`, `cooldown_min`, `cooldown_max`, `weight`, `min_level`, `place`, `group`, `role` "
        "FROM `mod_wow_fates_mechanic`"))
    {
        do
        {
            Field* f = result->Fetch();
            WowFatesMechanic mechanic;
            mechanic.id = f[0].Get<uint32>();
            if (mechanic.id >= 1000000)
            {
                LOG_ERROR("module", "mod-wow-fates: mechanic {}: ids from 1000000 are the module's own (boss_spells); skipped.", mechanic.id);
                continue;
            }
            mechanic.name = f[1].Get<std::string>();
            mechanic.kind = f[2].Get<uint8>();
            mechanic.spell = f[3].Get<uint32>();
            mechanic.target = f[4].Get<uint8>();
            mechanic.cooldownMin = f[5].Get<uint32>();
            mechanic.cooldownMax = std::max(f[6].Get<uint32>(), mechanic.cooldownMin);
            mechanic.weight = f[7].Get<uint32>();
            mechanic.minLevel = f[8].Get<uint8>();
            mechanic.place = f[9].Get<uint8>();
            mechanic.group = f[10].Get<uint8>();
            mechanic.role = f[11].Get<uint8>();
            if (mechanic.kind > WOW_FATES_MECHANIC_AGGRO || mechanic.target > WOW_FATES_TARGET_GROUND || mechanic.role >= WOW_FATES_ROLE_MAX)
            {
                LOG_ERROR("module", "mod-wow-fates: mechanic {} ({}) has kind {} or target {}, not handled; skipped.",
                    mechanic.id, mechanic.name, mechanic.kind, mechanic.target);
                continue;
            }
            if ((mechanic.kind == WOW_FATES_MECHANIC_SPELL || mechanic.kind == WOW_FATES_MECHANIC_LEAP) && !sSpellMgr->GetSpellInfo(mechanic.spell))
            {
                LOG_ERROR("module", "mod-wow-fates: mechanic {} ({}) casts spell {}, which does not exist; skipped.",
                    mechanic.id, mechanic.name, mechanic.spell);
                continue;
            }
            _mechanics[mechanic.id] = mechanic;
        } while (result->NextRow());
    }
    if (QueryResult result = WorldDatabase.Query(
        "SELECT `id`, `expedition`, `creature`, `adds_entry`, `weight`, `mechanics`, `hide_entry` FROM `mod_wow_fates_chief`"))
    {
        do
        {
            Field* f = result->Fetch();
            auto def = _defs.find(f[1].Get<uint32>());
            if (def == _defs.end())
                continue;
            WowFatesChief chief;
            chief.creature = f[2].Get<uint32>();
            chief.adds = f[3].Get<uint32>();
            chief.weight = std::max<uint32>(f[4].Get<uint32>(), 1);
            chief.hide = f[6].Get<uint32>();
            CreatureTemplate const* figure = sObjectMgr->GetCreatureTemplate(chief.creature);
            if (!figure || (chief.adds && !sObjectMgr->GetCreatureTemplate(chief.adds)))
            {
                LOG_ERROR("module", "mod-wow-fates: chief {} of Expedition {}: creature {} or his band {} does not exist; skipped.",
                    f[0].Get<uint32>(), def->first, chief.creature, chief.adds);
                continue;
            }
            // always an elite
            if (figure->rank < CREATURE_ELITE_ELITE)
                const_cast<CreatureTemplate*>(figure)->rank = CREATURE_ELITE_ELITE;
            std::string const mechanics = f[5].Get<std::string>();
            for (std::string_view id : Acore::Tokenize(mechanics, ',', false))
                if (Optional<uint32> mechanic = Acore::StringTo<uint32>(id))
                {
                    if (_mechanics.count(*mechanic))
                        chief.mechanics.push_back(*mechanic);
                    else
                        LOG_ERROR("module", "mod-wow-fates: chief {}: mechanic {} does not exist; left out.", f[0].Get<uint32>(), *mechanic);
                }
            def->second.chiefs.push_back(chief);
        } while (result->NextRow());
    }
    // A warlord without chiefs: boss_entry, his boss_spells his own mechanics.
    for (auto& [id, def] : _defs)
        if (def.objectiveType == WOW_FATES_OBJECTIVE_WARLORD && def.chiefs.empty() && sObjectMgr->GetCreatureTemplate(def.bossEntry))
        {
            WowFatesChief chief;
            chief.creature = def.bossEntry;
            chief.hide = def.objectiveEntry;
            for (std::size_t i = 0; i < def.bossSpells.size() && i < 16; ++i)
            {
                WowFatesMechanic mechanic;
                mechanic.id = 1000000 + id * 16 + uint32(i);
                mechanic.name = Acore::StringFormat("spell {}", def.bossSpells[i]);
                mechanic.spell = def.bossSpells[i];
                SpellInfo const* info = sSpellMgr->GetSpellInfo(mechanic.spell);
                bool const self = info && info->IsPositive();
                mechanic.target = self ? WOW_FATES_TARGET_SELF : WOW_FATES_TARGET_VICTIM;
                mechanic.role = self ? WOW_FATES_ROLE_BUFF : WOW_FATES_ROLE_STRIKE;
                mechanic.cooldownMin = self ? 0 : 8;
                mechanic.cooldownMax = self ? 0 : 14;
                mechanic.weight = 0;
                _mechanics[mechanic.id] = mechanic;
                chief.mechanics.push_back(mechanic.id);
            }
            def.chiefs.push_back(chief);
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
        if (def.objectiveType == WOW_FATES_OBJECTIVE_WARLORD && def.chiefs.empty())
        {
            LOG_ERROR("module", "mod-wow-fates: Expedition {} is a warlord with no chief (mod_wow_fates_chief) nor boss_entry; skipped.", def.id);
            itr = _defs.erase(itr);
        }
        else if ((def.objectiveType == WOW_FATES_OBJECTIVE_ASSAULT || def.objectiveType == WOW_FATES_OBJECTIVE_BOSS_SUPPORT)
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
        else if (def.objectiveType == WOW_FATES_OBJECTIVE_WARLORD && !sObjectMgr->GetCreatureTemplate(def.summonEntry)
            && std::any_of(def.chiefs.begin(), def.chiefs.end(), [](WowFatesChief const& chief) { return !chief.adds; }))
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
    {
        std::string const text = Localized(def->objectiveText, def->objectiveTexts, session);
        // a warlord's, his name in it ("{}"): he is drawn at each start
        if (def->objectiveType == WOW_FATES_OBJECTIVE_WARLORD)
            return Acore::StringFormat(text, CreatureName(Leader(active), session));
        return text;
    }
    if (def->objectiveType == WOW_FATES_OBJECTIVE_GATHER || def->objectiveType == WOW_FATES_OBJECTIVE_SABOTAGE)
        return Acore::StringFormat(WowFatesText(session, WOW_FATES_STR_OBJECTIVE_GATHER), GameObjectName(def->gatherEntry, session));
    if (def->objectiveType == WOW_FATES_OBJECTIVE_DEFENCE)
        return Acore::StringFormat(WowFatesText(session, WOW_FATES_STR_OBJECTIVE_DEFENCE), CreatureName(def->allyEntry, session),
            (def->holdTime + 59) / 60);
    if (def->objectiveType == WOW_FATES_OBJECTIVE_INVOKE)
        return Acore::StringFormat(WowFatesText(session, WOW_FATES_STR_OBJECTIVE_INVOKE), GameObjectName(def->channelEntry, session));
    if (def->objectiveType == WOW_FATES_OBJECTIVE_BOSS_SUPPORT || def->objectiveType == WOW_FATES_OBJECTIVE_WARLORD)
        return Acore::StringFormat(WowFatesText(session, WOW_FATES_STR_OBJECTIVE_KILL), CreatureName(Leader(active), session));
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

    if (def->objectiveType == WOW_FATES_OBJECTIVE_WARLORD)
        PickChief(active);

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
        {
            if (GameObject* go = map->GetGameObject(flag.go))
                go->DespawnOrUnsummon();
            for (ObjectGuid const& guid : flag.markers)
                if (GameObject* torch = map->GetGameObject(guid))
                    torch->DespawnOrUnsummon();
        }
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
    // a rescue done when its time ran out: by the share of captives led out
    uint32 const pct = uint32(std::lround(double(PartPct(part)) * std::clamp(active.rewardShare, 0.0, 1.0)));

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
    if (!def->trackerText.empty() && def->objectiveType == WOW_FATES_OBJECTIVE_WARLORD)
        return Acore::StringFormat(Localized(def->trackerText, def->trackerTexts, session), CreatureName(Leader(active), session));
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
        return Acore::StringFormat(WowFatesText(session, WOW_FATES_STR_TRACKER_KILL), CreatureName(Leader(active), session));
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
        // each position's bar, by steps of five
        std::string bars;
        for (WowFatesActiveExpedition::Flag const& flag : active.flags)
            bars += Acore::StringFormat("{}{}%", bars.empty() ? "" : " · ", uint32(flag.bar) / 5 * 5);
        if (bars.empty())
            bars = "-";
        int64 const left = TimeLeft(active);
        if (left < 0)
            return Acore::StringFormat(WowFatesText(session, WOW_FATES_STR_HELD), bars);
        uint32 const seconds = uint32((left + 9999) / 10000 * 10); // by steps of ten
        return Acore::StringFormat(WowFatesText(session, WOW_FATES_STR_HELD_TIME), bars, seconds / 60, seconds % 60);
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
    {
        uint32 const needed = std::max(active.rescueTotal, uint32(std::ceil(Needed(active) - 0.001)));
        int64 const left = TimeLeft(active);
        if (left < 0)
            return Acore::StringFormat(WowFatesText(session, WOW_FATES_STR_RESCUED), active.saved, needed, active.lost);
        uint32 const seconds = uint32((left + 9999) / 10000 * 10); // by steps of ten
        return Acore::StringFormat(WowFatesText(session, WOW_FATES_STR_RESCUED_TIME), active.saved, needed, active.lost,
            seconds / 60, seconds % 60);
    }
    if (def->objectiveType == WOW_FATES_OBJECTIVE_SUPPORT && active.alliesAlive >= 0)
        return Acore::StringFormat(WowFatesText(session, WOW_FATES_STR_ALLIES_ALIVE), CreatureName(def->allyEntry, session),
            uint32(active.alliesAlive), def->allyCount);
    if (def->objectiveType == WOW_FATES_OBJECTIVE_ESCORT && !active.members.empty() && active.allyHealthPct >= 0)
        return Acore::StringFormat(WowFatesText(session, WOW_FATES_STR_CARAVAN), CreatureName(def->allyEntry, session),
            uint32(active.allyHealthPct), uint32(std::max(active.alliesAlive, 0)),
            uint32(std::count_if(active.members.begin(), active.members.end(), [](auto const& member) { return member.guid || member.dead; })));
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
        if (!active.flags.empty())
            SendAddon(player, ZonesLine(active));
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

            // when each of its monsters was first fought, for the kills' speed;
            // where no wave comes, their health follows it every thirty seconds
            if (map)
            {
                std::erase_if(active.addEngaged, [map](auto const& pair) { return !map->GetCreature(pair.first); });
                for (ObjectGuid const& guid : active.summons)
                    if (Creature* creature = map->GetCreature(guid))
                        if (creature->IsAlive() && creature->IsInCombat())
                            active.addEngaged.try_emplace(guid, active.elapsed);
                active.adjustTimer -= c.areaInterval;
                if (active.adjustTimer <= 0)
                {
                    active.adjustTimer = 30 * IN_MILLISECONDS;
                    if (!HasWaves(active.def->objectiveType))
                        AdjustAdds(active);
                }
            }

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
                guarded = int32(active.saved * 100000 + active.lost * 1000 + (TimeLeft(active) + 9999) / 10000);
            else if (active.def->objectiveType == WOW_FATES_OBJECTIVE_CAPTURE)
            {
                // the time left by steps of ten seconds, each bar by steps of five
                uint64 key = uint64((TimeLeft(active) + 9999) / 10000);
                for (WowFatesActiveExpedition::Flag const& flag : active.flags)
                    key = key * 21 + uint32(flag.bar) / 5;
                guarded = int32(key % 2000000000);
            }
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
    float z = map->GetHeight(PHASEMASK_NORMAL, x, y, c.z + 2.5f, true, 20.0f); // the ground, not a crate's top
    if (z <= INVALID_HEIGHT || Wet(map, x, y, z))
        return Position(c.x, c.y, c.z, angle);
    return Position(x, y, z, angle);
}

// The scratch object that walks the paths as a player would (WalkPath), never
// on the map: one per Expedition, made once its map is there, deleted at its end.
GameObject* WowFatesExpeditionMgr::Walker(WowFatesActiveExpedition& active, Map* map)
{
    if (active.walker || active.walkerFailed || !map)
        return active.walker;
    GameObject* go = new GameObject();
    if (!go->Create(map->GenerateLowGuid<HighGuid::GameObject>(), WOW_FATES_WALKER_ENTRY, map, PHASEMASK_NORMAL,
            active.spawn.x, active.spawn.y, active.spawn.z, 0.0f, G3D::Quat(), 0, GO_STATE_READY))
    {
        delete go;
        active.walkerFailed = true;
        LOG_ERROR("module", "mod-wow-fates: Expedition {}: no walker (gameobject {}); its paths are only seen, not walked.",
            active.def->id, WOW_FATES_WALKER_ENTRY);
        return nullptr;
    }
    active.walker = go;
    return go;
}

// On foot from one point to the other, as a player: a path of the navigation
// mesh, whole (not cut short, not from or to a point off the mesh), not much
// longer than the straight line (WowFates.Object.PathFactor), with no step
// steeper than WowFates.Path.MaxSlope between two of its points (no jump, no
// cliff) and none in water, its ends where they were asked (a point on a
// rock or a cart above the mesh is not walked to). On a map with no mmaps
// only, one seen from the other. The points of the path in points, when asked.
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
        // no polygon at one end (the mesh has none there: a roof, a rock, a tree)
        if ((type & PATHFIND_NOT_USING_PATH) && map->GetMapCollisionData().GetMMapData().GetNavMesh())
            return false;
        if (!(type & PATHFIND_NOT_USING_PATH))
        {
            if (!(type & PATHFIND_NORMAL) || (type & (PATHFIND_NOPATH | PATHFIND_INCOMPLETE | PATHFIND_SHORT | PATHFIND_FARFROMPOLY)))
                return false;
            if (path.getPathLength() > straight * conf.objectPathFactor + 10.0f)
                return false;
            Movement::PointsArray const& steps = path.GetPath();
            if (steps.empty())
                return false;
            auto off = [](G3D::Vector3 const& step, Position const& asked)
            {
                return std::hypot(step.x - asked.GetPositionX(), step.y - asked.GetPositionY()) > 2.5f
                    || std::fabs(step.z - asked.GetPositionZ()) > 3.0f;
            };
            if (off(steps.front(), from) || off(steps.back(), to))
                return false;
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
    // a caravan's: room for its whole width on both sides, on the ground at
    // the route's level, dry, no wall in between (trees, posts and lamps do
    // not count). A fifth of the points too narrow at most; more, and the
    // caravan goes in a file behind its leader (narrow), not the point lost.
    active.narrow = false;
    uint32 checkedWidth = 0, narrowPoints = 0;
    if (float const half = CaravanWidth(active.def); half > 0.0f)
        for (std::size_t i = 1; i + 1 < active.route.size(); i += 2)
        {
            ++checkedWidth;
            Position const& at = active.route[i];
            float const dx = active.route[i + 1].GetPositionX() - active.route[i - 1].GetPositionX();
            float const dy = active.route[i + 1].GetPositionY() - active.route[i - 1].GetPositionY();
            float const len = std::max(std::hypot(dx, dy), 0.01f);
            for (float side : { -1.0f, 1.0f })
            {
                float const x = at.GetPositionX() - side * half * dy / len;
                float const y = at.GetPositionY() + side * half * dx / len;
                float const z = map->GetHeight(PHASEMASK_NORMAL, x, y, at.GetPositionZ() + 3.0f, true, 6.0f);
                if (z <= INVALID_HEIGHT || std::fabs(z - at.GetPositionZ()) > 2.5f || Wet(map, x, y, z)
                    || !map->isInLineOfSight(at.GetPositionX(), at.GetPositionY(), at.GetPositionZ() + 1.5f, x, y, z + 1.5f,
                        PHASEMASK_NORMAL, LINEOFSIGHT_ALL_CHECKS, VMAP::ModelIgnoreFlags::M2))
                {
                    ++narrowPoints;
                    break;
                }
            }
        }
    if (checkedWidth && narrowPoints * 5 > checkedWidth)
    {
        active.narrow = true;
        LOG_INFO("module", "mod-wow-fates: spawn point {} of Expedition {}: its route is too narrow for the pack at {} of {} points; the caravan goes in a file.",
            active.spawn.id, active.def->id, narrowPoints, checkedWidth);
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
    // its whole circle inside the edge the waves wait at (80 % of the radius)
    float const reach = std::max(active.def->radius * 0.8f - footprint - 2.0f, 0.0f);
    for (uint32 attempt = 0; attempt < 6; ++attempt)
    {
        Position pos;
        if (!ObjectPoint(active, map, active.walker, footprint, obstacles, pos))
            continue;
        if (pos.GetExactDist2d(active.spawn.x, active.spawn.y) > reach)
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
// around a warlord): 24 to 34 yards from it, near the angle, half of
// Spawn.PlayerDistance from the players at least (they stand by the ally, or
// around the warlord: an ambush comes close), on the ground about its level,
// dry, in the area, and reached on foot to it. A few angles around, then the
// other side; false when none.
bool WowFatesExpeditionMgr::AroundPoint(WowFatesActiveExpedition const& active, Map* map, Position const& around, float angle,
    Position& out) const
{
    WowFatesSpawnPoint const& c = active.spawn;
    for (uint32 attempt = 0; attempt < 10; ++attempt)
    {
        float const a = angle + (attempt ? frand(-0.6f, 0.6f) : 0.0f) + (attempt >= 5 ? float(M_PI) : 0.0f);
        float const dist = frand(24.0f, 34.0f);
        float const x = around.GetPositionX() + dist * std::cos(a);
        float const y = around.GetPositionY() + dist * std::sin(a);
        if (std::hypot(x - c.x, y - c.y) > active.def->radius || NearPlayer(active, x, y, 0.5f))
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

bool WowFatesExpeditionMgr::NearPlayer(WowFatesActiveExpedition const& active, float x, float y, float share) const
{
    float const distance = WowFatesConf().spawnPlayerDistance * share;
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
    uint32 const entry = active.summonEntry ? active.summonEntry : def->summonEntry;
    if (!entry || !def->summonCount || !map)
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
    // summon_count for one player, times the square root of the players
    // (Crowd: their health takes the rest) where no wave comes; where waves
    // come, they carry the players' number, the monsters about stay as many
    // (tougher all the same). The pace of the kills a little on top
    // (MaxPopulation times at most).
    uint32 const players = uint32(std::max<std::size_t>(active.inside.size(), 1));
    float const base = float(def->summonCount) * (HasWaves(def->objectiveType) ? 1.0f : Crowd(players));
    uint32 const population = uint32(std::lround(std::clamp(base + pace * respawn * c.adaptiveAggression, base, base * c.maxPopulation)));

    // Nobody near: the grid sleeps, no monster is brought.
    uint32 const floor = uint32(std::ceil(base * c.minAlivePct / 100.0f));
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
        TempSummon* summon = map->SummonCreature(entry, pos);
        if (!summon)
            break;

        summon->SetTempSummonType(TEMPSUMMON_CORPSE_TIMED_DESPAWN);
        summon->SetTimer(SUMMON_CORPSE_MS);
        Toughen(active, summon, players);

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

    uint32 const players = uint32(std::max<std::size_t>(active.inside.size(), 1));
    ScaleHealth(boss, active.bossHealth, active.bossPlayers, players, true);
    ScaleFoe(boss, players);
    // his mechanics, drawn when he first comes, at the area's tick
    if (!active.drawn)
        DrawMechanics(active, Outdoors(map, boss->GetPositionX(), boss->GetPositionY(), boss->GetPositionZ() + 1.0f));
    RunMechanics(active, map, boss, WowFatesConf().areaInterval);

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

    uint32 wanted = all ? def->gatherCount - uint32(active.objects.size()) : 1;
    // a rescue's: no more cages than captives left to free
    if (def->objectiveType == WOW_FATES_OBJECTIVE_RESCUE)
    {
        uint32 const left = active.rescueTotal > active.freed + uint32(active.objects.size())
            ? active.rescueTotal - active.freed - uint32(active.objects.size()) : 0;
        wanted = std::min(wanted, left);
        if (!wanted)
            return;
    }
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
                Toughen(active, guard, uint32(std::max<std::size_t>(active.inside.size(), 1)));
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
    // no room at all, round after round (a minute): the point is never used again
    if (!placed && active.objects.empty() && ++active.placeFails >= 6)
    {
        Broken(active, "no room for any of its objects");
        return;
    }
    if (placed)
        active.placeFails = 0;
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

// How many monsters for the players in the area: the players to the power
// WowFates.Scaling.Count (a square root): one player 1, nine 3, forty 6.3.
// Their health (Toughen) takes the rest, so that count x health follows the
// players.
// The types whose waves come (from the edge, around an ally or a chief).
bool WowFatesExpeditionMgr::HasWaves(uint8 type)
{
    return type == WOW_FATES_OBJECTIVE_DEFENCE || type == WOW_FATES_OBJECTIVE_INVOKE || type == WOW_FATES_OBJECTIVE_SUPPORT
        || type == WOW_FATES_OBJECTIVE_BOSS_SUPPORT || type == WOW_FATES_OBJECTIVE_ESCORT || type == WOW_FATES_OBJECTIVE_CAPTURE
        || type == WOW_FATES_OBJECTIVE_WARLORD;
}

float WowFatesExpeditionMgr::Crowd(uint32 players)
{
    return std::pow(float(std::max<uint32>(players, 1)), WowFatesConf().scaleCount);
}

// A monster of an Expedition as it comes, for the players in its area: its
// health times the players to the power Scaling.Health, times what the kills'
// speed made of it (addToughness, AdjustAdds); its damage times the players
// to the power Scaling.Damage. Once out, it keeps them.
void WowFatesExpeditionMgr::Toughen(WowFatesActiveExpedition& active, Creature* creature, uint32 players)
{
    if (!creature->IsAlive())
        return;
    players = std::max<uint32>(players, 1);
    WowFatesConfig const& c = WowFatesConf();
    double const factor = std::pow(double(players), double(c.scaleHealth)) * active.addToughness;
    uint32 const maxHealth = std::max<uint32>(1, uint32(double(creature->GetMaxHealth()) * factor));
    creature->SetCreateHealth(maxHealth);
    creature->SetStatFlatModifier(UNIT_MOD_HEALTH, BASE_VALUE, float(maxHealth));
    creature->UpdateMaxHealth();
    creature->SetFullHealth();
    ScaleFoe(creature, players);
}

// The kills' speed: how long the monsters of the Expedition took to kill
// since they were first fought, against Target.AddSeconds; their health, for
// those to come, moved toward it by Target.AddStep at most (three kills at
// least). At each wave, else every thirty seconds.
void WowFatesExpeditionMgr::AdjustAdds(WowFatesActiveExpedition& active)
{
    if (active.addKillTimes.size() < 3)
        return;
    WowFatesConfig const& c = WowFatesConf();
    double total = 0.0;
    for (int64 ms : active.addKillTimes)
        total += double(ms);
    double const seconds = std::max(total / double(active.addKillTimes.size()) / IN_MILLISECONDS, 0.5);
    double const ratio = std::clamp(double(c.targetAddSeconds) / seconds, 1.0 - double(c.targetAddStep), 1.0 + double(c.targetAddStep));
    active.addToughness *= ratio;
    LOG_INFO("module", "mod-wow-fates: Expedition {}: its monsters died in {:.1f} s, {:.0f} s asked: their health x{:.2f} now.",
        active.def->id, seconds, c.targetAddSeconds, active.addToughness);
    active.addKillTimes.clear();
}

// A boss's, a chief's damage for the players in the area: times them to the
// power Scaling.Damage (his health follows ScaleHealth).
void WowFatesExpeditionMgr::ScaleFoe(Creature* creature, uint32 players) const
{
    float const hit = std::pow(float(std::max<uint32>(players, 1)), WowFatesConf().scaleDamage);
    if (std::fabs(creature->GetPctModifierValue(UNIT_MOD_DAMAGE_MAINHAND, TOTAL_PCT) - hit) < 0.001f)
        return;
    creature->SetStatPctModifier(UNIT_MOD_DAMAGE_MAINHAND, TOTAL_PCT, hit);
    creature->SetStatPctModifier(UNIT_MOD_DAMAGE_RANGED, TOTAL_PCT, hit);
    creature->UpdateDamagePhysical(BASE_ATTACK);
    creature->UpdateDamagePhysical(RANGED_ATTACK);
}

// The health of a boss or a chief (foe) for the players in the area: x the
// players to the power Scaling.Boss (1.11: x11.5 for nine, x60 for forty, a
// raid's strength growing faster than its number); an ally's: x (1 +
// Scaling.Ally x (players - 1)). Its maximum follows them up and down, its
// share of health stays (78 % with fifteen players more is still 78 %), never
// a death or a full heal from the rounding.
void WowFatesExpeditionMgr::ScaleHealth(Creature* creature, uint32 baseHealth, uint32& setFor, uint32 players, bool foe) const
{
    if (players == setFor || !creature->IsAlive() || !baseHealth)
        return;
    double const share = double(creature->GetHealth()) / double(std::max<uint32>(creature->GetMaxHealth(), 1));
    WowFatesConfig const& c = WowFatesConf();
    double const factor = foe ? std::pow(double(players), double(c.scaleBoss)) : 1.0 + double(c.scaleAlly) * double(players - 1);
    uint32 const maxHealth = uint32(double(baseHealth) * factor);
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
    float z = map->GetHeight(PHASEMASK_NORMAL, x, y, c.z + 2.5f, true, 20.0f); // the ground, not a crate's top
    if (z <= INVALID_HEIGHT || Wet(map, x, y, z))
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
    if (active.waveTimer <= 0 && active.progressPct < 100.0 && map->IsGridLoaded(active.spawn.x, active.spawn.y)
        && SendWave(active, map, ally))
        active.waveTimer = int64(active.def->waveInterval) * IN_MILLISECONDS;
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
bool WowFatesExpeditionMgr::SendWave(WowFatesActiveExpedition& active, Map* map, Unit* target, Position const* around,
    Position const* goal)
{
    WowFatesExpeditionDef const* def = active.def;
    std::erase_if(active.summons, [map](ObjectGuid const& guid)
    {
        return !map->GetCreature(guid);
    });

    // one after the other, never piled up: the last one down to NextAt of
    // its monsters alive first (false: the caller tries again later)
    WowFatesConfig const& c = WowFatesConf();
    if (!active.waveMobs.empty())
    {
        std::size_t const alive = std::count_if(active.waveMobs.begin(), active.waveMobs.end(), [map](ObjectGuid const& guid)
        {
            Creature* creature = map->GetCreature(guid);
            return creature && creature->IsAlive();
        });
        if (float(alive) > c.waveNextAt * float(active.waveMobs.size()))
            return false;
    }

    if (active.wave)
    {
        bool const fast = active.waveClearedAt >= 0
            && active.waveClearedAt - active.waveSentAt <= int64(c.wavePaceFast * float(def->waveInterval) * IN_MILLISECONDS);
        active.pace = fast ? std::min(c.wavePaceMax, active.pace + c.wavePaceStep)
            : std::max(c.wavePaceMin, active.pace - c.wavePaceStep);
    }

    // their health for the kills' speed so far
    AdjustAdds(active);

    // wave_size for one player, times the square root of the players
    // (Crowd: their health takes the rest), the pace of the kills a little
    // on top (PaceMin to PaceMax), wave_growth more for each wave sent
    uint32 const characters = uint32(std::max<std::size_t>(active.inside.size(), 1));
    uint32 const count = std::max<uint32>(1, uint32(std::lround(c.wavePerCharacter * float(def->waveSize) * Crowd(characters) * active.pace))
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

        TempSummon* summon = map->SummonCreature(active.summonEntry ? active.summonEntry : def->summonEntry, pos);
        if (!summon)
            break;
        summon->SetTempSummonType(TEMPSUMMON_CORPSE_TIMED_DESPAWN);
        summon->SetTimer(SUMMON_CORPSE_MS);
        Toughen(active, summon, characters);
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
        {
            std::string words = Localized(def->waveText, def->waveTexts, player->GetSession());
            if (def->objectiveType == WOW_FATES_OBJECTIVE_WARLORD && !words.empty())
                words = Acore::StringFormat(words, CreatureName(Leader(active), player->GetSession()));
            SendAddon(player, Acore::StringFormat("wave\t{}\t{}\t{}", def->id, active.wave, Clean(words)));
        }
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
    if (active.waveTimer <= 0 && active.progressPct < 100.0 && map->IsGridLoaded(active.spawn.x, active.spawn.y)
        && SendWave(active, map, channeling.empty() ? nullptr : Acore::Containers::SelectRandomContainerElement(channeling)))
        active.waveTimer = int64(active.def->waveInterval) * IN_MILLISECONDS;
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
    ScaleHealth(boss, active.bossHealth, active.bossPlayers, players, true);
    ScaleFoe(boss, players);
    // his mechanics, drawn when he first comes; only with players to see them
    if (!active.drawn)
        DrawMechanics(active, Outdoors(map, boss->GetPositionX(), boss->GetPositionY(), boss->GetPositionZ() + 1.0f));
    if (!active.inside.empty())
        RunMechanics(active, map, boss, diff);

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
            Unit* target = ally && ally->IsAlive() ? static_cast<Unit*>(ally) : nullptr;
            if (urand(0, 1) || !target)
                if (Player* player = ObjectAccessor::FindPlayer(Acore::Containers::SelectRandomContainerElement(active.inside)))
                    if (player->IsInWorld() && player->IsAlive())
                        target = player;
            if (SendWave(active, map, target)) // else again, once the last pack is down
                active.bossWavesSent |= bit;
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
        active.caravanPace = active.caravanPace > 0.0f ? std::min(active.caravanPace, ally->GetSpeed(MOVE_WALK)) : ally->GetSpeed(MOVE_WALK);
        SpawnCaravan(active, map, ally);
    }

    uint32 const players = uint32(std::max<std::size_t>(active.inside.size(), 1));
    ScaleHealth(ally, active.allyHealth, active.allyPlayers, players);
    active.allyHealthPct = std::max<int32>(5, int32(std::ceil(ally->GetHealthPct() / 5.0f)) * 5);

    // all at the walk of the slowest of the caravan, his own too
    if (active.caravanPace > 0.0f)
    {
        float const rate = active.caravanPace / baseMoveSpeed[MOVE_WALK];
        Pace(ally, rate);
        for (WowFatesActiveExpedition::Member const& member : active.members)
            if (Creature* creature = member.dead || !member.guid ? nullptr : map->GetCreature(member.guid))
                if (creature->IsAlive())
                    Pace(creature, rate);
    }

    // the caravan in a pack around him, at its places, at his pace; one that
    // lags behind out of a fight holds the pack back, and stuck there a while
    // (or far), it is put back at its place
    bool straggler = false;
    uint32 standing = 0;
    for (WowFatesActiveExpedition::Member& member : active.members)
    {
        Creature* creature = member.dead || !member.guid ? nullptr : map->GetCreature(member.guid);
        if (!creature)
            continue;
        if (!creature->IsAlive())
        {
            member.dead = true;
            continue;
        }
        ++standing;
        ScaleHealth(creature, member.baseHealth, member.setFor, players);
        if (creature->IsInCombat())
        {
            member.behind = 0;
            continue;
        }
        creature->SetHomePosition(creature->GetPosition());
        float const off = creature->GetExactDist2d(ally);
        if (off > member.dist + 6.0f)
        {
            straggler = true;
            member.behind += diff;
            if ((member.behind >= 8 * IN_MILLISECONDS || off > 40.0f) && !ally->IsInCombat())
            {
                Position const spot = CaravanSpot(active, map, ally, member);
                creature->NearTeleportTo(spot.GetPositionX(), spot.GetPositionY(), spot.GetPositionZ(), ally->GetOrientation());
                member.behind = 0;
            }
        }
        else
            member.behind = 0;
        if (creature->GetMotionMaster()->GetCurrentMovementGeneratorType() != FOLLOW_MOTION_TYPE)
            creature->GetMotionMaster()->MoveFollow(ally, member.dist, member.angle);
    }
    active.alliesAlive = int32(standing);

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
        Position const around = ally->GetPosition();
        // at him, or at one of his caravan
        Unit* target = ally;
        std::vector<Creature*> others;
        for (WowFatesActiveExpedition::Member const& member : active.members)
            if (Creature* creature = member.dead ? nullptr : map->GetCreature(member.guid))
                if (creature->IsAlive())
                    others.push_back(creature);
        if (!others.empty() && urand(0, 1))
            target = Acore::Containers::SelectRandomContainerElement(others);
        if (SendWave(active, map, target, &around)) // else again, once the last one is down
        {
            active.bossWavesSent |= bit;
            Say(ally, ESCORT_SAY_AMBUSH);
        }
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
    if (!followed || straggler)
    {
        if (motion->GetCurrentMovementGeneratorType() == POINT_MOTION_TYPE)
        {
            motion->Clear();
            ally->StopMoving();
        }
        if (!followed)
        {
            active.talkTimer -= diff;
            if (active.talkTimer <= 0)
            {
                Say(ally, ESCORT_SAY_WAIT);
                active.talkTimer = 30 * IN_MILLISECONDS;
            }
        }
        return;
    }
    active.talkTimer = std::max<int64>(active.talkTimer, 15 * IN_MILLISECONDS);
    if (motion->GetCurrentMovementGeneratorType() != POINT_MOTION_TYPE)
        motion->MovePoint(1, active.route[std::min<std::size_t>(active.routeIndex + 5, last)], FORCED_MOVEMENT_WALK);
}

namespace
{
    // The places of a caravan around its leader: an angle from his facing and
    // a distance, at his sides first, then behind, then before him; a pack,
    // not a file.
    struct CaravanPlace { float angle, dist; };
    CaravanPlace const CARAVAN_PLACES[] =
    {
        { float(M_PI) / 2.0f, 3.5f }, { -float(M_PI) / 2.0f, 3.5f }, { float(M_PI), 4.5f },
        { 3.0f * float(M_PI) / 4.0f, 5.5f }, { -3.0f * float(M_PI) / 4.0f, 5.5f },
        { float(M_PI) / 4.0f, 4.0f }, { -float(M_PI) / 4.0f, 4.0f }, { float(M_PI), 8.0f },
        { float(M_PI) / 2.0f, 7.0f }, { -float(M_PI) / 2.0f, 7.0f }
    };
    constexpr std::size_t CARAVAN_MAX = sizeof(CARAVAN_PLACES) / sizeof(CARAVAN_PLACES[0]);
}

// How far a caravan reaches on each side of its route: its widest place, a
// yard more; 0 without one.
float WowFatesExpeditionMgr::CaravanWidth(WowFatesExpeditionDef const* def)
{
    float half = 0.0f;
    for (std::size_t i = 0; i < def->caravan.size() && i < CARAVAN_MAX; ++i)
        half = std::max(half, std::fabs(CARAVAN_PLACES[i].dist * std::sin(CARAVAN_PLACES[i].angle)));
    return def->caravan.empty() ? 0.0f : half + 1.0f;
}

// A member's place around the leader: on the ground at his level, dry, seen
// from him (no wall, fence or house between); else his own place.
Position WowFatesExpeditionMgr::CaravanSpot(WowFatesActiveExpedition const& /*active*/, Map* map, Creature* leader,
    WowFatesActiveExpedition::Member const& member) const
{
    float const a = leader->GetOrientation() + member.angle;
    float const x = leader->GetPositionX() + member.dist * std::cos(a);
    float const y = leader->GetPositionY() + member.dist * std::sin(a);
    float const z = map->GetHeight(PHASEMASK_NORMAL, x, y, leader->GetPositionZ() + 3.0f, true, 8.0f);
    if (z > INVALID_HEIGHT && std::fabs(z - leader->GetPositionZ()) <= 3.0f && !Wet(map, x, y, z)
        && map->isInLineOfSight(leader->GetPositionX(), leader->GetPositionY(), leader->GetPositionZ() + 1.5f, x, y, z + 1.5f,
            PHASEMASK_NORMAL, LINEOFSIGHT_ALL_CHECKS, VMAP::ModelIgnoreFlags::Nothing))
        return Position(x, y, z, leader->GetOrientation());
    return leader->GetPosition();
}

// The caravan around its leader, each at its place (CARAVAN_PLACES, or in a
// file behind him on a narrow route), on the ground there, else at his side; those that live and are not there (all, the
// first time; those gone with a grid) come, the dead stay dead. Its health
// grows with the players, as every ally's.
void WowFatesExpeditionMgr::SpawnCaravan(WowFatesActiveExpedition& active, Map* map, Creature* leader)
{
    WowFatesExpeditionDef const* def = active.def;
    for (std::size_t i = 0; i < def->caravan.size() && i < CARAVAN_MAX; ++i)
    {
        if (i >= active.members.size())
        {
            WowFatesActiveExpedition::Member member;
            member.entry = def->caravan[i];
            member.angle = active.narrow ? float(M_PI) : CARAVAN_PLACES[i].angle;
            member.dist = active.narrow ? 3.0f + 2.5f * float(i) : CARAVAN_PLACES[i].dist;
            active.members.push_back(member);
        }
        WowFatesActiveExpedition::Member& member = active.members[i];
        if (member.dead || (member.guid && map->GetCreature(member.guid)))
            continue;
        TempSummon* creature = map->SummonCreature(member.entry, CaravanSpot(active, map, leader, member));
        if (!creature)
            continue;
        creature->SetTempSummonType(TEMPSUMMON_MANUAL_DESPAWN);
        creature->SetPvP(false); // faction 495: healing it would flag the healer
        creature->SetWalk(true);
        creature->SetHomePosition(creature->GetPosition());
        creature->GetMotionMaster()->MoveFollow(leader, member.dist, member.angle);
        member.guid = creature->GetGUID();
        member.baseHealth = creature->GetMaxHealth();
        active.caravanPace = active.caravanPace > 0.0f ? std::min(active.caravanPace, creature->GetSpeed(MOVE_WALK)) : creature->GetSpeed(MOVE_WALK);
        member.setFor = 1;
        member.behind = 0;
        active.allies.push_back(member.guid); // healed as an ally, gone at the end
    }
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
// the kills, never fewer once more came). It ends when no captive is left to
// free nor on the map, all led out or dead: done when one was led out at
// least, the rewards by the share led out; failed when all died. The first
// player starts a clock (time_limit, long: only against a rescue left
// undone): out, it ends the same way, the captives left go.
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
    active.rescueTotal = std::max(active.rescueTotal, uint32(std::ceil(Needed(active) - 0.001)));
    if (Start(active, "rescue", &WowFatesExpeditionMgr::CreatureName, def->allyEntry) && def->timeLimit && !active.deadline)
        active.deadline = active.elapsed + int64(def->timeLimit) * IN_MILLISECONDS;
    // the end: done if one at least was led out, by the share led out; failed if none
    auto end = [&active, def](char const* why)
    {
        LOG_INFO("module", "mod-wow-fates: Expedition {}: {}, {} captives led out of {}, {} lost.", def->id, why,
            active.saved, active.rescueTotal, active.lost);
        if (!active.saved)
        {
            active.failed = true;
            return;
        }
        active.rewardShare = std::min(1.0, double(active.saved) / double(std::max<uint32>(active.rescueTotal, 1)));
        active.progressPct = 100.0;
    };
    if (active.deadline && active.elapsed >= active.deadline)
    {
        end("the time is out");
        return;
    }

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
            LOG_INFO("module", "mod-wow-fates: Expedition {}: a captive died, {} lost, {} led out.", def->id, active.lost, active.saved);
            continue;
        }
        if (captive->GetExactDist2d(way.GetPositionX(), way.GetPositionY()) < 5.0f)
        {
            itr = active.allies.erase(itr);
            Say(captive, CAPTIVE_SAY_OUT);
            captive->DespawnOrUnsummon(Milliseconds(3000));
            ++active.saved;
            ++active.progress;
            active.progressPct = std::min(99.0, 100.0 * double(active.saved) / double(std::max<uint32>(active.rescueTotal, 1)));
            Active(active);
            continue;
        }
        Pace(captive, WowFatesConf().captiveSpeed);
        if (!captive->IsInCombat())
        {
            captive->SetHomePosition(captive->GetPosition()); // back from a fight: here, not at his cage
            if (captive->GetMotionMaster()->GetCurrentMovementGeneratorType() != POINT_MOTION_TYPE)
                captive->GetMotionMaster()->MovePoint(1, way, FORCED_MOVEMENT_WALK); // at a walk: the players keep up
        }
        ++itr;
    }

    // none left to free nor on the map
    if (active.rescueTotal && active.freed >= active.rescueTotal && active.allies.empty())
        end("no captive is left");
}

// A creature's walk at a rate of a walk's (2.5 yards a second), and its run
// no faster: no flight, no chase, no run for the way out outruns it. Again
// when something set them back (an aura's end updates the speeds).
void WowFatesExpeditionMgr::Pace(Creature* creature, float walk)
{
    float const run = walk * baseMoveSpeed[MOVE_WALK] / baseMoveSpeed[MOVE_RUN];
    if (std::fabs(creature->GetSpeedRate(MOVE_WALK) - walk) > 0.01f)
        creature->SetSpeed(MOVE_WALK, walk);
    if (std::fabs(creature->GetSpeedRate(MOVE_RUN) - run) > 0.01f)
        creature->SetSpeed(MOVE_RUN, run);
}

// A cage opened (OnGather, its map's thread): its captive out, thankful,
// walking for the way out (at a walk: the players keep up with him).
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
    captive->SetWalk(true);
    Pace(captive, WowFatesConf().captiveSpeed);
    captive->GetMotionMaster()->Initialize();
    Say(captive, CAPTIVE_SAY_FREED);
    if (!active.route.empty())
        captive->GetMotionMaster()->MovePoint(1, active.route.front(), FORCED_MOVEMENT_WALK);
    active.allies.push_back(captive->GetGUID());
    ++active.freed;
}

// ---------------------------------------------------------------------------
// Capture

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

// A capture's banner put up in the middle of its position: the foe's
// (gather_entry) or ours (taken_entry); the one standing there goes.
void WowFatesExpeditionMgr::PutFlag(WowFatesActiveExpedition& active, Map* map, WowFatesActiveExpedition::Flag& flag, bool ours)
{
    if (flag.go)
        if (GameObject* old = map->GetGameObject(flag.go))
            old->DespawnOrUnsummon();
    flag.go.Clear();
    flag.ours = ours;
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

// The torches around a position (CAPTURE_MARKERS on its circle), on the
// ground there; none where there is no ground, or water.
void WowFatesExpeditionMgr::PutMarkers(Map* map, WowFatesActiveExpedition::Flag& flag)
{
    for (ObjectGuid const& guid : flag.markers)
        if (GameObject* old = map->GetGameObject(guid))
            old->DespawnOrUnsummon();
    flag.markers.clear();
    for (uint32 i = 0; i < CAPTURE_MARKERS; ++i)
    {
        float const angle = 2.0f * float(M_PI) * float(i) / float(CAPTURE_MARKERS);
        float const x = flag.pos.GetPositionX() + CAPTURE_RADIUS * std::cos(angle);
        float const y = flag.pos.GetPositionY() + CAPTURE_RADIUS * std::sin(angle);
        float const z = map->GetHeight(PHASEMASK_NORMAL, x, y, flag.pos.GetPositionZ() + 4.0f, true, 10.0f);
        if (z <= INVALID_HEIGHT || Wet(map, x, y, z))
            continue;
        GameObject* go = new GameObject();
        if (!go->Create(map->GenerateLowGuid<HighGuid::GameObject>(), WOW_FATES_CAPTURE_TORCH, map, PHASEMASK_NORMAL,
                x, y, z, angle, G3D::Quat(), 100, GO_STATE_READY))
        {
            delete go;
            continue;
        }
        go->SetRespawnTime(0);
        go->SetSpellId(1);
        go->SetSpawnedByDefault(false);
        map->AddToMap(go);
        flag.markers.push_back(go->GetGUID());
    }
}

// "zones", id, then each position as "x,y,radius,state,bar" separated by
// ";": the circles the clients draw on their maps (state: 0 the foe's, 1
// being taken, 2 ours, 3 fought over).
std::string WowFatesExpeditionMgr::ZonesLine(WowFatesActiveExpedition const& active) const
{
    std::string zones;
    for (WowFatesActiveExpedition::Flag const& flag : active.flags)
        zones += Acore::StringFormat("{}{:.1f},{:.1f},{:.1f},{},{}", zones.empty() ? "" : ";", flag.pos.GetPositionX(),
            flag.pos.GetPositionY(), CAPTURE_RADIUS, uint32(flag.state), uint32(flag.bar));
    return Acore::StringFormat("zones\t{}\t{}", active.def->id, zones);
}

// To every player, when what the map shows changed (or forced).
void WowFatesExpeditionMgr::BroadcastZones(WowFatesActiveExpedition& active, bool force)
{
    std::string line = ZonesLine(active);
    // the bars by steps of ten, for the map's tooltip: not every tick
    std::string key;
    for (WowFatesActiveExpedition::Flag const& flag : active.flags)
        key += Acore::StringFormat("{}:{};", uint32(flag.state), uint32(flag.bar) / 10);
    if (!force && key == active.sentZones)
        return;
    active.sentZones = key;
    for (auto const& [guid, player] : ObjectAccessor::GetPlayers())
        if (player && player->IsInWorld())
            SendAddon(player, line);
}

// Its positions, gather_count of them (two at least), placed once the area
// is loaded, far apart (FlagPoint: each walked from the centre, its whole
// circle clear and flat), the foe's banner in the middle, torches around;
// none placed, tried again ten seconds later. The first player starts the
// clock (time_limit). As the towers of Hellfire Peninsula, each has its bar:
// players in its circle and no foe, it fills (hold_time seconds for one
// player, up to twice as fast with more); a foe in it with them, it waits
// until all are dead; foes alone, it empties. Full, the position is ours
// (our banner); empty again, the foe's. The bar of the Expedition is theirs
// together: all ours, done; the clock out first, it fails. Every
// wave_interval or so the foe sends a squad (summon_entry, as a wave, within
// the engagement cap) at one of the positions, rather one we hold or take.
// Gone (a grid unloaded), a banner or its torches are put back.
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
        std::vector<Footing> obstacles = Obstacles(active, map);
        std::vector<Position> placed;
        for (uint32 i = 0; i < def->gatherCount; ++i)
        {
            Position pos;
            if (!FlagPoint(active, map, CAPTURE_RADIUS, obstacles, placed, pos))
                break;
            obstacles.push_back({ pos.GetPositionX(), pos.GetPositionY(), CAPTURE_RADIUS });
            placed.push_back(pos);
        }
        if (placed.size() < 2)
        {
            LOG_DEBUG("module", "mod-wow-fates: Expedition {}: room for {} positions, two at least; tried again.", def->id, uint32(placed.size()));
            active.gatherTimer = 10 * IN_MILLISECONDS;
            if (++active.placeFails >= 6)
                Broken(active, "no room for two of its positions");
            return;
        }
        for (Position const& pos : placed)
        {
            WowFatesActiveExpedition::Flag flag;
            flag.pos = pos;
            PutFlag(active, map, flag, false);
            PutMarkers(map, flag);
            active.flags.push_back(flag);
        }
        BroadcastZones(active, true);
    }

    for (WowFatesActiveExpedition::Flag& flag : active.flags)
    {
        if (!map->IsGridLoaded(flag.pos.GetPositionX(), flag.pos.GetPositionY()))
            continue;
        GameObject* go = flag.go ? map->GetGameObject(flag.go) : nullptr;
        if (!go || !go->isSpawned())
            PutFlag(active, map, flag, flag.ours);
        if (!flag.markers.empty() && !map->GetGameObject(flag.markers.front()))
            PutMarkers(map, flag);
    }

    if (!Start(active, "capture", &WowFatesExpeditionMgr::GameObjectName, def->gatherEntry))
        return;
    if (def->timeLimit && !active.deadline)
        active.deadline = active.elapsed + int64(def->timeLimit) * IN_MILLISECONDS;
    if (active.deadline && active.elapsed >= active.deadline)
    {
        LOG_INFO("module", "mod-wow-fates: Expedition {}: the time is out, {} % taken.", def->id, uint32(active.progressPct));
        active.failed = true;
        return;
    }

    double const rate = 100.0 / (double(std::max<uint32>(def->holdTime, 1)) * IN_MILLISECONDS); // a bar's share per ms
    double total = 0.0;
    uint32 ours = 0;
    for (WowFatesActiveExpedition::Flag& flag : active.flags)
    {
        auto in = [&flag](Unit const* unit)
        {
            return unit->GetExactDist2d(flag.pos.GetPositionX(), flag.pos.GetPositionY()) <= CAPTURE_RADIUS
                && std::fabs(unit->GetPositionZ() - flag.pos.GetPositionZ()) <= CAPTURE_HEIGHT;
        };
        uint32 players = 0, foes = 0;
        for (ObjectGuid const& guid : active.inside)
            if (Player* player = ObjectAccessor::FindPlayer(guid))
                if (player->IsInWorld() && player->IsAlive() && player->GetMap() == map && in(player))
                    ++players;
        for (ObjectGuid const& guid : active.summons)
            if (Creature* creature = map->GetCreature(guid))
                if (creature->IsAlive() && in(creature))
                    ++foes;

        if (players && !foes)
            flag.bar += float(rate * double(diff) * std::min(1.0 + 0.5 * double(players - 1), 2.0));
        else if (!players && foes)
            flag.bar -= float(rate * double(diff));
        flag.bar = std::clamp(flag.bar, 0.0f, 100.0f);
        if (players)
            Active(active);

        if (!flag.ours && flag.bar >= 100.0f)
        {
            PutFlag(active, map, flag, true);
            LOG_INFO("module", "mod-wow-fates: Expedition {}: a position is taken.", def->id);
        }
        else if (flag.ours && flag.bar <= 0.0f)
        {
            PutFlag(active, map, flag, false);
            LOG_INFO("module", "mod-wow-fates: Expedition {}: the foe took a position back.", def->id);
        }
        flag.state = players && foes ? 3 : (flag.ours ? 2 : (flag.bar > 0.0f ? 1 : 0));
        ours += flag.ours ? 1 : 0;
        total += double(flag.bar);
    }
    active.alliesAlive = int32(ours);
    active.progressPct = ours == active.flags.size() ? 100.0 : std::min(99.0, total / double(active.flags.size()));
    BroadcastZones(active);

    // the foe's squads, a little apart from each other
    active.waveTimer -= diff;
    if (active.waveTimer <= 0 && map->IsGridLoaded(active.spawn.x, active.spawn.y))
    {
        std::vector<Position> held, all;
        for (WowFatesActiveExpedition::Flag const& flag : active.flags)
        {
            all.push_back(flag.pos);
            if (flag.ours || flag.bar > 0.0f)
                held.push_back(flag.pos);
        }
        Position const goal = Acore::Containers::SelectRandomContainerElement(held.empty() ? all : held);
        if (SendWave(active, map, nullptr, nullptr, &goal)) // else again, once the last squad is down
            active.waveTimer = int64(float(def->waveInterval) * frand(0.7f, 1.3f) * IN_MILLISECONDS);
    }
}

// ---------------------------------------------------------------------------
// Warlord

// Who leads a warlord's Expedition: its chief, else boss_entry.
uint32 WowFatesExpeditionMgr::Leader(WowFatesActiveExpedition const& active)
{
    return active.chief.creature ? active.chief.creature : active.def->bossEntry;
}

// At its start, a warlord's chief drawn from its pool, by weight; his band.
void WowFatesExpeditionMgr::PickChief(WowFatesActiveExpedition& active)
{
    std::vector<WowFatesChief> const& chiefs = active.def->chiefs;
    if (chiefs.empty())
        return;
    uint32 total = 0;
    for (WowFatesChief const& chief : chiefs)
        total += chief.weight;
    uint32 roll = urand(0, std::max<uint32>(total, 1) - 1);
    active.chief = chiefs.back();
    for (WowFatesChief const& chief : chiefs)
    {
        if (roll < chief.weight)
        {
            active.chief = chief;
            break;
        }
        roll -= chief.weight;
    }
    active.summonEntry = active.chief.adds;
}

// His mechanics, once, when he first comes. A known figure's own first, in
// their order; then, from the pool by weight (none twice, one of each group,
// none above the Expedition's level, none for the open sky under a roof nor
// the other way), what he lacks of a strike, an area, a control and a change
// of aggro; a made-up one draws mechanics_min to mechanics_max more among a
// buff, a cast to interrupt and his band (one at most). The first of each of
// a strike, an area, a control, a change of aggro and a cast are his from the
// start; the others come with his phases (75, 50, 25 % of his health), one
// each, the last ones together.
void WowFatesExpeditionMgr::DrawMechanics(WowFatesActiveExpedition& active, bool outdoors)
{
    active.drawn = true;
    active.mechanics.clear();
    WowFatesExpeditionDef const* def = active.def;
    std::vector<WowFatesMechanic const*> chosen;
    uint32 roles[WOW_FATES_ROLE_MAX] = {};
    std::unordered_set<uint8> groups;
    auto take = [&](WowFatesMechanic const* mechanic)
    {
        chosen.push_back(mechanic);
        ++roles[mechanic->role];
        if (mechanic->group)
            groups.insert(mechanic->group);
    };
    for (uint32 id : active.chief.mechanics)
        if (auto itr = _mechanics.find(id); itr != _mechanics.end())
            take(&itr->second);
    auto draw = [&](uint8 role)
    {
        std::vector<WowFatesMechanic const*> pool;
        uint32 total = 0;
        for (auto const& [id, mechanic] : _mechanics)
            if (mechanic.role == role && mechanic.weight && mechanic.minLevel <= def->level
                && (mechanic.place == 0 || (mechanic.place == 1) == outdoors)
                && (!mechanic.group || !groups.count(mechanic.group))
                && std::find(chosen.begin(), chosen.end(), &mechanic) == chosen.end())
            {
                pool.push_back(&mechanic);
                total += mechanic.weight;
            }
        if (pool.empty())
            return;
        uint32 roll = urand(0, total - 1);
        for (WowFatesMechanic const* mechanic : pool)
        {
            if (roll < mechanic->weight)
            {
                take(mechanic);
                return;
            }
            roll -= mechanic->weight;
        }
        take(pool.back());
    };
    for (uint8 role : { WOW_FATES_ROLE_STRIKE, WOW_FATES_ROLE_AREA, WOW_FATES_ROLE_CONTROL, WOW_FATES_ROLE_AGGRO })
        if (!roles[role])
            draw(role);
    if (active.chief.mechanics.empty())
    {
        uint32 const extras = urand(def->mechanicsMin, def->mechanicsMax);
        for (uint32 i = 0; i < extras; ++i)
        {
            std::vector<uint8> left = { WOW_FATES_ROLE_BUFF, WOW_FATES_ROLE_CAST };
            if (!roles[WOW_FATES_ROLE_ADDS])
                left.push_back(WOW_FATES_ROLE_ADDS);
            draw(Acore::Containers::SelectRandomContainerElement(left));
        }
    }

    // from the start, the first of each role of the fight; then one at each phase
    bool seen[WOW_FATES_ROLE_MAX] = {};
    uint32 later = 0;
    for (WowFatesMechanic const* mechanic : chosen)
    {
        WowFatesActiveExpedition::Drawn drawn;
        drawn.mechanic = mechanic;
        bool const fight = mechanic->role == WOW_FATES_ROLE_STRIKE || mechanic->role == WOW_FATES_ROLE_AREA
            || mechanic->role == WOW_FATES_ROLE_CONTROL || mechanic->role == WOW_FATES_ROLE_AGGRO || mechanic->role == WOW_FATES_ROLE_CAST;
        if (fight && !seen[mechanic->role])
        {
            seen[mechanic->role] = true;
            drawn.phase = 0;
        }
        else
            drawn.phase = std::min<uint32>(++later, 3);
        drawn.cooldown = int64(urand(mechanic->cooldownMin, mechanic->cooldownMax)) * IN_MILLISECONDS / 2;
        active.mechanics.push_back(drawn);
    }
    LOG_INFO("module", "mod-wow-fates: Expedition {}: its chief, creature {}: {}.", def->id, Leader(active), Actions(active));
}

// His mechanics as words: "Mortal Strike (strike), Thunderclap (area, 50 %)...".
std::string WowFatesExpeditionMgr::Actions(WowFatesActiveExpedition const& active) const
{
    static char const* const names[WOW_FATES_ROLE_MAX] = { "-", "strike", "area", "control", "buff", "aggro", "band", "cast" };
    static char const* const phases[4] = { "", ", from 75 %", ", from 50 %", ", from 25 %" };
    std::string out;
    for (WowFatesActiveExpedition::Drawn const& drawn : active.mechanics)
        out += Acore::StringFormat("{}{} ({}{})", out.empty() ? "" : ", ", drawn.mechanic->name, names[drawn.mechanic->role],
            phases[std::min<uint32>(drawn.phase, 3)]);
    return out.empty() ? "nothing" : out;
}

// A line said by a chief before a heavy attack, in each player's language,
// a bubble over his head (a monster's say, no system message).
void WowFatesExpeditionMgr::Announce(Creature* speaker, uint32 text)
{
    Map* map = speaker->GetMap();
    for (auto const& ref : map->GetPlayers())
    {
        Player* player = ref.GetSource();
        if (!player || !player->IsInWorld() || !player->IsWithinDist(speaker, 60.0f))
            continue;
        WorldPacket data;
        ChatHandler::BuildChatPacket(data, CHAT_MSG_MONSTER_SAY, LANG_UNIVERSAL, speaker, player,
            WowFatesText(player->GetSession(), text));
        player->SendDirectMessage(&data);
    }
}

// A mechanic used. A spell at its target, its damage times the players to the
// power Scaling.Damage; one at a player falls on more of them as they are more
// (the cube root of the players: 1, 2 for 8, 3 for 27); his band from around
// him; a leap at the farthest player, its spell when he lands; a change of
// his target. His words before the heavy ones.
void WowFatesExpeditionMgr::UseMechanic(WowFatesActiveExpedition& active, Map* map, Creature* boss, WowFatesMechanic const* mechanic)
{
    // the players of the area within forty yards
    std::vector<Player*> around;
    for (ObjectGuid const& guid : active.inside)
        if (Player* player = ObjectAccessor::FindPlayer(guid))
            if (player->IsInWorld() && player->IsAlive() && player->GetMap() == map && player->IsWithinDist(boss, 40.0f))
                around.push_back(player);
    Unit* victim = boss->GetVictim();
    Player* farthest = nullptr;
    for (Player* player : around)
        if (!farthest || boss->GetExactDist2d(player) > boss->GetExactDist2d(farthest))
            farthest = player;
    uint32 const players = uint32(std::max<std::size_t>(active.inside.size(), 1));

    // his words before it
    switch (mechanic->role)
    {
        case WOW_FATES_ROLE_STRIKE:  if (urand(0, 1)) Announce(boss, WOW_FATES_STR_SAY_STRIKE); break;
        case WOW_FATES_ROLE_AREA:    Announce(boss, WOW_FATES_STR_SAY_AREA); break;
        case WOW_FATES_ROLE_CONTROL: if (urand(0, 1)) Announce(boss, WOW_FATES_STR_SAY_CONTROL); break;
        case WOW_FATES_ROLE_AGGRO:   Announce(boss, WOW_FATES_STR_SAY_AGGRO); break;
        case WOW_FATES_ROLE_CAST:    Announce(boss, WOW_FATES_STR_SAY_CAST); break;
        case WOW_FATES_ROLE_BUFF:    Announce(boss, WOW_FATES_STR_SAY_BUFF); break;
        default: break;
    }

    // a spell's damage for the players: its damaging effects' points
    auto cast = [&](Unit* at, bool triggered)
    {
        SpellInfo const* info = sSpellMgr->GetSpellInfo(mechanic->spell);
        if (!info || !at)
            return;
        float const hit = std::pow(float(players), WowFatesConf().scaleDamage);
        CustomSpellValues values;
        static SpellValueMod const mods[3] = { SPELLVALUE_BASE_POINT0, SPELLVALUE_BASE_POINT1, SPELLVALUE_BASE_POINT2 };
        for (uint8 i = 0; i < 3; ++i)
        {
            SpellEffectInfo const& effect = info->Effects[i];
            bool const damage = effect.Effect == SPELL_EFFECT_SCHOOL_DAMAGE || effect.Effect == SPELL_EFFECT_WEAPON_DAMAGE
                || effect.Effect == SPELL_EFFECT_NORMALIZED_WEAPON_DMG || effect.Effect == SPELL_EFFECT_WEAPON_DAMAGE_NOSCHOOL
                || (effect.Effect == SPELL_EFFECT_APPLY_AURA && effect.ApplyAuraName == SPELL_AURA_PERIODIC_DAMAGE);
            if (damage)
                values.AddSpellMod(mods[i], int32(float(effect.CalcValue(boss)) * hit));
        }
        // no mana, no reagent, no cooldown of the spell's own: the pool's
        // cooldowns are his; a cast to interrupt keeps its cast time
        TriggerCastFlags const flags = triggered ? TRIGGERED_FULL_MASK
            : TriggerCastFlags(TRIGGERED_IGNORE_POWER_AND_REAGENT_COST | TRIGGERED_IGNORE_GCD | TRIGGERED_IGNORE_SPELL_AND_CATEGORY_CD);
        boss->CastCustomSpell(mechanic->spell, values, at, flags);
    };
    // more of them as the players are more
    auto several = [&]()
    {
        std::vector<Unit*> targets;
        std::vector<Player*> pool(around);
        uint32 const count = std::max<uint32>(1, uint32(std::lround(std::cbrt(double(players)))));
        while (targets.size() < count && !pool.empty())
        {
            auto pick = pool.begin() + urand(0, uint32(pool.size()) - 1);
            targets.push_back(*pick);
            pool.erase(pick);
        }
        if (targets.empty() && victim)
            targets.push_back(victim);
        return targets;
    };

    switch (mechanic->kind)
    {
        case WOW_FATES_MECHANIC_ADDS:
        {
            Position const from = boss->GetPosition();
            SendWave(active, map, around.empty() ? victim : Acore::Containers::SelectRandomContainerElement(around), &from);
            break;
        }
        case WOW_FATES_MECHANIC_LEAP:
        {
            Unit* at = farthest ? static_cast<Unit*>(farthest) : victim;
            if (!at || at->IsFlying() || at->IsFalling())
                break;
            float const z = map->GetHeight(PHASEMASK_NORMAL, at->GetPositionX(), at->GetPositionY(), at->GetPositionZ() + 2.0f, true, 10.0f);
            if (z <= INVALID_HEIGHT || std::fabs(z - at->GetPositionZ()) > 2.0f || Wet(map, at->GetPositionX(), at->GetPositionY(), z))
                break;
            Position const landing(at->GetPositionX(), at->GetPositionY(), z, at->GetOrientation());
            boss->GetMotionMaster()->MoveJump(landing, 25.0f, 12.0f);
            active.leapAt = active.elapsed + int64(boss->GetExactDist(landing) / 25.0f * IN_MILLISECONDS) + 300;
            active.leapSpell = mechanic->spell;
            break;
        }
        case WOW_FATES_MECHANIC_AGGRO:
        {
            ThreatManager& threat = boss->GetThreatMgr();
            if (mechanic->target == 0 || around.empty())
            {
                threat.ResetAllThreat(); // all wiped: whoever hits him next
                break;
            }
            Player* chosen = nullptr;
            if (mechanic->target == 2)
            {
                // the one who healed most in the Expedition
                uint64 best = 0;
                for (Player* player : around)
                    if (auto itr = active.contributors.find(player->GetGUID()); itr != active.contributors.end() && itr->second.healing > best)
                    {
                        best = itr->second.healing;
                        chosen = player;
                    }
            }
            if (!chosen)
            {
                std::vector<Player*> others;
                for (Player* player : around)
                    if (player != victim)
                        others.push_back(player);
                chosen = Acore::Containers::SelectRandomContainerElement(others.empty() ? around : others);
            }
            threat.MatchUnitThreatToHighestThreat(chosen);
            threat.AddThreat(chosen, float(chosen->GetMaxHealth()), nullptr, true, true);
            break;
        }
        default:
            switch (mechanic->target)
            {
                case WOW_FATES_TARGET_SELF:
                    cast(boss, true);
                    break;
                case WOW_FATES_TARGET_RANDOM:
                    for (Unit* at : several())
                        cast(at, false);
                    break;
                case WOW_FATES_TARGET_FARTHEST:
                    cast(farthest ? static_cast<Unit*>(farthest) : victim, false);
                    break;
                case WOW_FATES_TARGET_GROUND:
                    for (Unit* at : several())
                        boss->CastSpell(at->GetPositionX(), at->GetPositionY(), at->GetPositionZ(), mechanic->spell, true);
                    break;
                default:
                    cast(victim, mechanic->role != WOW_FATES_ROLE_CAST); // a cast to interrupt is not triggered
                    break;
            }
            break;
    }
}

// A chief's, a boss's mechanics in his fight. His phases by his health (75,
// 50, 25 %): each one makes his mechanics come back sooner (x0.85) and brings
// those of the phase (one used once, at once). Each mechanic at its cooldown,
// one at a time, two and a half seconds apart at least.
void WowFatesExpeditionMgr::RunMechanics(WowFatesActiveExpedition& active, Map* map, Creature* boss, uint32 diff)
{
    if (active.mechanics.empty() || !boss->IsAlive())
        return;
    float const health = boss->GetHealthPct();
    uint32 const phase = (health <= 75.0f) + (health <= 50.0f) + (health <= 25.0f);
    while (active.mechanicPhase < phase)
    {
        ++active.mechanicPhase;
        active.cooldownScale *= 0.85f;
        for (WowFatesActiveExpedition::Drawn& drawn : active.mechanics)
            if (drawn.phase == active.mechanicPhase && !drawn.mechanic->cooldownMax && !drawn.used && boss->IsInCombat())
            {
                UseMechanic(active, map, boss, drawn.mechanic);
                drawn.used = true;
            }
    }

    // a leap that lands
    if (active.leapAt && active.elapsed >= active.leapAt)
    {
        active.leapAt = 0;
        if (active.leapSpell)
            boss->CastSpell(boss, active.leapSpell, true);
    }

    if (!boss->IsInCombat() || !boss->GetVictim())
        return;
    active.spellTimer -= diff;
    for (WowFatesActiveExpedition::Drawn& drawn : active.mechanics)
    {
        if (drawn.phase > active.mechanicPhase)
            continue;
        if (!drawn.mechanic->cooldownMax)
        {
            if (drawn.used || active.spellTimer > 0 || boss->HasUnitState(UNIT_STATE_CASTING) || active.leapAt)
                continue;
            UseMechanic(active, map, boss, drawn.mechanic);
            drawn.used = true;
            active.spellTimer = 2500;
            continue;
        }
        drawn.cooldown -= diff;
        if (drawn.cooldown > 0 || active.spellTimer > 0 || boss->HasUnitState(UNIT_STATE_CASTING) || active.leapAt)
            continue;
        UseMechanic(active, map, boss, drawn.mechanic);
        drawn.cooldown = int64(float(urand(drawn.mechanic->cooldownMin, drawn.mechanic->cooldownMax)) * active.cooldownScale * IN_MILLISECONDS);
        active.spellTimer = 2500;
    }
}

// A warlord out of his fight alive (he evaded, went back to his place): his
// band and its reinforcements go, every monster of the Expedition alive; his
// phases and mechanics are armed again, his health full for the players
// there, the bar back to 0; he speaks again when the next fight begins.
void WowFatesExpeditionMgr::ResetWarlord(WowFatesActiveExpedition& active, Map* map, Creature* boss)
{
    active.bossFought = false;
    for (ObjectGuid const& guid : active.summons)
        if (Creature* creature = map->GetCreature(guid))
            if (creature->IsAlive())
            {
                creature->CombatStop(true);
                creature->DespawnOrUnsummon();
            }
    active.waveMobs.clear();
    active.waiting.clear();
    active.phaseWave = false;
    active.bossWavesSent = 0;
    active.phase = 0;
    active.bossSaid = 0;
    active.leapAt = 0;
    active.filled = false;
    active.mechanicPhase = 0;
    active.cooldownScale = 1.0f;
    for (WowFatesActiveExpedition::Drawn& drawn : active.mechanics)
    {
        drawn.used = false;
        drawn.cooldown = int64(urand(drawn.mechanic->cooldownMin, drawn.mechanic->cooldownMax)) * IN_MILLISECONDS / 2;
    }
    boss->SetFullHealth();
    active.progressPct = 0.0;
    active.sentProgress = 0;
    LOG_INFO("module", "mod-wow-fates: Expedition {}: its warlord was reset; his band is gone, the bar back to 0.", active.def->id);
}

// A warlord's chief (PickChief: drawn from mod_wow_fates_chief at the start,
// always an elite, bigger by Boss.Scale) in the centre as soon as the area is
// loaded; the game's own figure he stands for kept out of the area meanwhile
// (despawned, back at its own respawn time). His mechanics are drawn when he
// first comes (DrawMechanics: under the sky or a roof, as his place is). The
// first player starts the fight and its clock (time_limit). His health follows
// the players in the area; the bar is the health he lost, never going back.
// Each mark of boss_waves passed is a phase: his words, his band from around
// him at one of the players, and his mechanics of that phase (one used once,
// a cooldown of 0, at once). In a fight, each of his mechanics at its
// cooldown, one at a time, two and a half seconds apart at least. Dead, done;
// the clock out first, it fails. Gone alive (a grid unloaded), he comes back
// as he was.
void WowFatesExpeditionMgr::UpdateWarlord(WowFatesActiveExpedition& active, Map* map, uint32 diff)
{
    if (!map || active.failed || active.progressPct >= 100.0 || active.remaining <= 0)
        return;
    WowFatesExpeditionDef const* def = active.def;
    if (!map->IsGridLoaded(active.spawn.x, active.spawn.y))
        return;

    active.hideTimer -= diff;
    if (active.chief.hide && active.hideTimer <= 0)
    {
        active.hideTimer = 5 * IN_MILLISECONDS;
        std::vector<Creature*> others;
        for (auto const& [spawnId, creature] : map->GetCreatureBySpawnIdStore())
            if (creature && creature->IsInWorld() && creature->IsAlive() && creature->GetEntry() == active.chief.hide
                && creature->GetExactDist2d(active.spawn.x, active.spawn.y) <= def->radius + 20.0f)
                others.push_back(creature);
        for (Creature* creature : others)
            creature->DespawnOrUnsummon(); // its own respawn time: back after, as ever
    }

    Creature* boss = active.boss ? map->GetCreature(active.boss) : nullptr;
    if (!boss)
    {
        bool const first = !active.bossOut;
        Position const pos = Centre(active, map);
        TempSummon* summon = map->SummonCreature(Leader(active), pos);
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
        if (!active.drawn)
            DrawMechanics(active, Outdoors(map, pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ() + 1.0f));
        LOG_INFO("module", "mod-wow-fates: Expedition {}: its warlord appears ({} health).", def->id, boss->GetMaxHealth());
    }
    if (!boss->IsAlive())
    {
        Say(boss, BOSS_SAY_DEATH);
        active.progressPct = 100.0;
        return;
    }
    // out of a fight alive (he evaded, went back to his place): reset
    if (boss->IsInCombat())
        active.bossFought = true;
    else if (active.bossFought)
        ResetWarlord(active, map, boss);

    uint32 const players = uint32(std::max<std::size_t>(active.inside.size(), 1));
    ScaleHealth(boss, active.bossHealth, active.bossPlayers, players, true);
    ScaleFoe(boss, players);
    double const lost = 1.0 - double(boss->GetHealth()) / double(std::max<uint32>(boss->GetMaxHealth(), 1));
    active.progressPct = std::max(active.progressPct, std::min(100.0 * lost, 99.0));

    if (!Start(active, "warlord", &WowFatesExpeditionMgr::CreatureName, Leader(active)))
        return;
    if (!(active.bossSaid & 1))
    {
        active.bossSaid |= 1;
        Say(boss, BOSS_SAY_START);
        if (def->timeLimit && !active.deadline) // the clock runs on through a reset
            active.deadline = active.elapsed + int64(def->timeLimit) * IN_MILLISECONDS;
        active.spellTimer = urand(4, 8) * IN_MILLISECONDS;
        for (WowFatesActiveExpedition::Drawn& drawn : active.mechanics)
            if (!drawn.phase && !drawn.mechanic->cooldownMax && drawn.mechanic->kind == WOW_FATES_MECHANIC_SPELL
                && drawn.mechanic->target == WOW_FATES_TARGET_SELF)
            {
                UseMechanic(active, map, boss, drawn.mechanic);
                drawn.used = true;
            }
    }
    if (active.deadline && active.elapsed >= active.deadline)
    {
        Say(boss, BOSS_SAY_TIMEOUT);
        LOG_INFO("module", "mod-wow-fates: Expedition {}: the time is out, its warlord at {} %.", def->id, uint32(100.0 - active.progressPct));
        active.failed = true;
        return;
    }

    // his phases, at the bar's marks
    for (std::size_t i = 0; i < def->bossWaves.size() && i < 32; ++i)
    {
        uint32 const bit = 1u << i;
        if ((active.bossWavesSent & bit) || active.progressPct < double(def->bossWaves[i]))
            continue;
        active.bossWavesSent |= bit;
        active.phase = uint32(i + 1);
        Say(boss, uint8(BOSS_SAY_75 + std::min<std::size_t>(i, 3)));
        active.phaseWave = true;
        break;
    }
    // the phase's band, once the last one is down
    if (active.phaseWave)
    {
        Unit* target = boss->GetVictim();
        if (!active.inside.empty())
            if (Player* player = ObjectAccessor::FindPlayer(Acore::Containers::SelectRandomContainerElement(active.inside)))
                if (player->IsInWorld() && player->IsAlive() && player->GetMap() == map)
                    target = player;
        Position const around = boss->GetPosition();
        if (SendWave(active, map, target, &around))
            active.phaseWave = false;
    }

    // his mechanics, as his health goes
    RunMechanics(active, map, boss, diff);
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

    // one of its monsters: how long it took to kill, since it was first fought
    if (auto engaged = active->addEngaged.find(creature->GetGUID()); engaged != active->addEngaged.end())
    {
        active->addKillTimes.push_back(active->elapsed - engaged->second);
        active->addEngaged.erase(engaged);
    }

    // The leader of an assault: the end of the bar.
    if ((def->objectiveType == WOW_FATES_OBJECTIVE_ASSAULT || def->objectiveType == WOW_FATES_OBJECTIVE_BOSS_SUPPORT
            || def->objectiveType == WOW_FATES_OBJECTIVE_WARLORD)
        && active->bossOut && creature->GetGUID() == active->boss)
    {
        if (def->objectiveType == WOW_FATES_OBJECTIVE_WARLORD)
            Say(victim->ToCreature(), BOSS_SAY_DEATH);
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
            auto found = _spawns.find(spawnId);
            if (found == _spawns.end())
                continue;
            WowFatesSpawnPoint const& spawn = found->second;
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
                    notes += Acore::StringFormat(", route {:.0f} yd in {} points{}", probe.routeAt.back(), uint32(probe.route.size()),
                        probe.narrow ? ", too narrow for the pack: in a file" : (def.caravan.empty() ? "" : ", wide enough for the pack"));
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
                float const footprint = type == WOW_FATES_OBJECTIVE_CAPTURE ? CAPTURE_RADIUS
                    : Footprint(info ? info->displayId : 0, info ? info->size : 1.0f);
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

// The scaling of an Expedition (0: of all) for some numbers of players (0:
// 1, 5, 9, 10, 20 and 40): the first wave and the fourth (wave_growth), each
// between PaceMin and PaceMax; the monsters about (MaxPopulation at most);
// the most alive at once (the monsters about at their most, a wave at its
// biggest and what is left of the last one, NextAt; a sabotage's guards);
// each one's health and damage, and those of a boss or an ally.
void WowFatesExpeditionMgr::Simulate(ChatHandler* handler, uint32 id, uint32 players)
{
    std::lock_guard<std::recursive_mutex> guard(_lock);
    WowFatesConfig const& c = WowFatesConf();
    std::vector<uint32> counts = players ? std::vector<uint32>{ players } : std::vector<uint32>{ 1, 5, 9, 10, 20, 40 };
    for (auto const& [defId, def] : _defs)
    {
        if (id && defId != id)
            continue;
        uint8 const type = def.objectiveType;
        bool const waves = HasWaves(type);
        handler->SendSysMessage(Acore::StringFormat("Expedition {} ({}), type {}: wave_size {}, wave_growth {}, summon_count {}{}",
            defId, def.name, uint32(type), def.waveSize, def.waveGrowth, def.summonCount, waves ? "" : ", no waves"));
        for (uint32 n : counts)
        {
            float const crowd = Crowd(n);
            auto wave = [&](uint32 index, float pace)
            {
                return std::max<uint32>(1, uint32(std::lround(c.wavePerCharacter * float(def.waveSize) * crowd * pace))) + def.waveGrowth * index;
            };
            float const base = float(def.summonCount) * (waves ? 1.0f : crowd);
            uint32 const about = uint32(std::lround(base)), aboutMax = uint32(std::lround(base * c.maxPopulation));
            uint32 const biggest = waves ? wave(3, c.wavePaceMax) : 0;
            uint32 alive = aboutMax + biggest + uint32(std::ceil(float(biggest) * c.waveNextAt));
            if (type == WOW_FATES_OBJECTIVE_SABOTAGE)
                alive += def.gatherCount * def.waveSize;
            std::string line = Acore::StringFormat("  {} players: ", n);
            if (waves)
                line += Acore::StringFormat("wave 1: {} ({}-{}), wave 4: {} ({}-{}); ", wave(0, 1.0f), wave(0, c.wavePaceMin),
                    wave(0, c.wavePaceMax), wave(3, 1.0f), wave(3, c.wavePaceMin), wave(3, c.wavePaceMax));
            line += Acore::StringFormat("about {} (up to {}); at most {} alive; each x{:.2f} health at first (then by the kills' speed, "
                "{:.0f} s each asked), x{:.2f} damage; a boss or a chief x{:.2f} health, x{:.2f} damage; an ally x{:.2f} health",
                about, aboutMax, alive, std::pow(float(n), c.scaleHealth), c.targetAddSeconds, std::pow(float(n), c.scaleDamage),
                std::pow(float(n), c.scaleBoss), std::pow(float(n), c.scaleDamage), 1.0f + c.scaleAlly * float(n - 1));
            handler->SendSysMessage(line);
        }
        // a chief's mechanics: a draw for each, as one at his coming (under the sky)
        WowFatesActiveExpedition probe;
        probe.def = &def;
        std::vector<WowFatesChief> chiefs = def.chiefs;
        if (chiefs.empty() && (type == WOW_FATES_OBJECTIVE_ASSAULT || type == WOW_FATES_OBJECTIVE_BOSS_SUPPORT))
        {
            WowFatesChief boss;
            boss.creature = def.bossEntry;
            chiefs.push_back(boss);
        }
        for (WowFatesChief const& chief : chiefs)
        {
            probe.chief = chief;
            DrawMechanics(probe, true);
            handler->SendSysMessage(Acore::StringFormat("  {} ({}): {}", CreatureName(chief.creature, handler->GetSession()),
                chief.mechanics.empty() ? "drawn" : "his own, completed", Actions(probe)));
        }
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
