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

/*
 * mod-wow-fates — what every part of the module shares: the identifiers
 * (tranche 96 of the WoW-mods registry), the texts of module_string and the
 * configuration (conf/mod-wow-fates.conf.dist).
 */

#ifndef MOD_WOW_FATES_H
#define MOD_WOW_FATES_H

#include "Define.h"

#include <string>
#include <utility>
#include <vector>

class Player;
class WorldSession;

constexpr char const* WOW_FATES_MODULE = "mod-wow-fates";

// The Synced debuff (data/sql/world/wow_fates_03_spells.sql). One visible
// aura, its charges showing the level the player is synced to, and three
// hidden ones: a spell holds three effects only.
enum WowFatesSpells : uint32
{
    SPELL_WOW_FATES_SYNCED          = 96000, // all stats %, damage done %, healing done %
    SPELL_WOW_FATES_SYNCED_OFFENSE  = 96001, // hit, critical strike, haste ratings
    SPELL_WOW_FATES_SYNCED_DEFENSE  = 96002, // defense, dodge and parry, expertise and armor penetration ratings
    SPELL_WOW_FATES_SYNCED_HEALTH   = 96003  // maximum health %, armor %, maximum mana %
};

// module_string ids (data/sql/world/wow_fates_01_strings.sql).
enum WowFatesStrings : uint32
{
    WOW_FATES_STR_STARTED           = 1,
    WOW_FATES_STR_PROGRESS          = 2,
    WOW_FATES_STR_SUCCESS           = 3,
    WOW_FATES_STR_FAILED            = 4,
    WOW_FATES_STR_REWARD            = 5,
    WOW_FATES_STR_REWARD_MONEY      = 6,
    WOW_FATES_STR_NOT_ELIGIBLE      = 7,
    WOW_FATES_STR_REWARD_MAILED     = 8,
    WOW_FATES_STR_SYNCED            = 9,
    WOW_FATES_STR_UNSYNCED          = 10,
    WOW_FATES_STR_MAIL_SUBJECT      = 11,
    WOW_FATES_STR_MAIL_BODY         = 12,
    WOW_FATES_STR_ENTERED           = 13,
    WOW_FATES_STR_LEFT              = 14,

    WOW_FATES_STR_OBJECTIVE_KILL_ANY = 20,
    WOW_FATES_STR_OBJECTIVE_KILL     = 21,
    WOW_FATES_STR_TRACKER_KILL_ANY   = 22,  // the tracker's short line
    WOW_FATES_STR_TRACKER_KILL       = 23,
    WOW_FATES_STR_OBJECTIVE_ASSAULT_ANY = 24, // then their leader
    WOW_FATES_STR_OBJECTIVE_ASSAULT  = 25,
    WOW_FATES_STR_OBJECTIVE_GATHER   = 26,
    WOW_FATES_STR_TRACKER_GATHER     = 27,

    WOW_FATES_STR_CMD_LIST          = 101,
    WOW_FATES_STR_CMD_LIST_LINE     = 102,
    WOW_FATES_STR_CMD_LIST_NONE     = 103,
    WOW_FATES_STR_CMD_STARTED       = 104,
    WOW_FATES_STR_CMD_CANNOT_START  = 105,
    WOW_FATES_STR_CMD_STOPPED       = 106,
    WOW_FATES_STR_CMD_NOT_ACTIVE    = 107,
    WOW_FATES_STR_CMD_RELOADED      = 108,
    WOW_FATES_STR_CMD_SCORE         = 109,
    WOW_FATES_STR_CMD_DISABLED      = 110,
    WOW_FATES_STR_CMD_COMPLETED     = 111,
    WOW_FATES_STR_CMD_DEFS_LINE     = 112,
    WOW_FATES_STR_CMD_SYNCED_LINE   = 113,
    WOW_FATES_STR_CMD_SYNCED_NONE   = 114,
    WOW_FATES_STR_CMD_NO_PLAYER     = 115
};

struct WowFatesConfig
{
    bool enable = true;

    // Synced
    bool syncEnable = true;
    uint32 syncInterval = 5000;      // ms between two measures of a synced player
    float minRatio = 0.05f;          // the debuff never takes more than 95 %
    float reduceStats = 1.0f;        // share of the gap taken off, per category
    float reduceRatings = 1.0f;
    float reduceHealth = 1.0f;
    float reduceArmor = 1.0f;
    float reduceDamage = 1.0f;
    float reduceHealing = 1.0f;
    float reduceMana = 1.0f;
    float damageLevelExponent = 1.0f; // damage by level: the creatures' base damage curve, to this power
    float poolBonus = 1.15f;          // the reference health and mana, times this
    bool syncAnnounce = true;

    // Power score
    float primaryWeight = 1.0f;
    float secondaryWeight = 0.5f;
    float powerWeight = 0.5f;

    // Reference score: (level, score) points, a straight line between them
    float referenceFactor = 1.0f;
    std::vector<std::pair<uint32, float>> referenceCurve;

    // Expeditions
    uint32 maxActive = 3;
    uint32 spawnInterval = 60000;    // ms between two looks for a free slot
    uint32 cooldown = 600;           // s, when the Expedition's own is 0
    uint32 areaInterval = 1000;      // ms between two looks at who is inside
    bool announce = true;

    // The objective grows with the players in the area: base x (1 + this x (players - 1))
    float participantScaling = 0.6f;

    // Monsters of an Expedition: brought back faster, and more of them, when
    // the area is cleared fast
    bool adaptiveSpawn = true;
    uint32 adaptiveWindow = 15000;   // ms of kills the pace is read on
    uint32 minRespawn = 2000;        // ms, the shortest delay
    float maxPopulation = 3.0f;      // times summon_count, the most alive at once
    float adaptiveAggression = 1.5f; // how hard the pace raises the population
    float minAlivePct = 50.0f;       // under this share of summon_count alive, filled up at once

    // Participation
    float mediumShare = 0.5f;        // of the average contribution
    float strongShare = 1.0f;
    float rewardRange = 2.0f;        // times the radius

    // Rewards
    int32 rewardLevelMargin = -1;    // -1 = the player's level; N = at most the Expedition's level + N
    uint32 lightPct = 33;            // share of the rewards by participation
    uint32 mediumPct = 66;
};

WowFatesConfig const& WowFatesConf();
void WowFatesLoadConfig();

// The text of a module string in the player's language, English otherwise.
std::string WowFatesText(WorldSession* session, uint32 id);

#endif
