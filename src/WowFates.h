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
 * (blocks 96000-96399 of the WoW-mods registry), the texts of module_string and the
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
    WOW_FATES_STR_OBJECTIVE_DEFENCE  = 28,
    WOW_FATES_STR_TRACKER_DEFENCE    = 29,
    WOW_FATES_STR_OBJECTIVE_INVOKE   = 30,
    WOW_FATES_STR_TRACKER_INVOKE     = 31,
    WOW_FATES_STR_OBJECTIVE_SUPPORT  = 32,
    WOW_FATES_STR_TRACKER_SUPPORT    = 33,
    WOW_FATES_STR_ALLIES_ALIVE       = 34,  // the tracker's second line: allies alive
    WOW_FATES_STR_ALLY_HEALTH        = 35,  // the tracker's second line: the ally's health
    WOW_FATES_STR_OBJECTIVE_ESCORT   = 36,
    WOW_FATES_STR_TRACKER_ESCORT     = 37,
    WOW_FATES_STR_OBJECTIVE_RESCUE   = 38,
    WOW_FATES_STR_TRACKER_RESCUE     = 39,
    WOW_FATES_STR_RESCUED            = 40,  // the tracker's second line: led out, lost
    WOW_FATES_STR_OBJECTIVE_CAPTURE  = 41,
    WOW_FATES_STR_TRACKER_CAPTURE    = 42,
    WOW_FATES_STR_HELD_TIME          = 43,  // the tracker's second line: the positions' bars, time left
    WOW_FATES_STR_HELD               = 44,  // the same, with no time limit
    WOW_FATES_STR_TIME_LEFT          = 45,  // the tracker's second line: a warlord's time left
    WOW_FATES_STR_CARAVAN            = 46,
    WOW_FATES_STR_RESCUED_TIME       = 47,  // the tracker's second line: led out, lost, time left  // the tracker's second line: an escort's leader's health, his caravan standing

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
    uint32 maxPerZone = 3;           // Expeditions at once in a zone with players
    bool countBots = false;          // bots make a zone "with players"
    uint32 idleExpire = 900;         // s without activity before one goes, when its own duration is 0
    uint32 spawnInterval = 60000;    // ms between two more in a zone with players
    uint32 cooldown = 600;           // s, when the Expedition's own is 0
    uint32 areaInterval = 1000;      // ms between two looks at who is inside
    bool announce = true;

    // The objective grows with the players in the area: base x (1 + this x (players - 1))
    float participantScaling = 0.6f;
    std::vector<uint8> outdoorTypes = { 8 }; // objective types kept out from under a roof (8 sabotage)
    uint32 engageBase = 3;           // monsters at the players at once, for one player
    uint32 engagePerPlayer = 2;      // and this many more for each other one
    float spawnPlayerDistance = 20.0f; // no monster nor object comes this near a player
    float objectReach = 0.6f;        // a gathering's or sabotage's objects within this share of the radius
    float objectSpacing = 1.5f;      // two objects' centres at least this times their footprints apart
    float objectMinSpacing = 6.0f;   // yards, and at least this
    float objectMaxHeight = 6.0f;    // yards above or below the centre at most
    float objectMaxSlope = 0.5f;     // rise per yard of the ground around it at most
    float objectPathFactor = 2.0f;   // its walk from the centre at most this times the straight line, + 10 yards
    float pathMaxSlope = 1.0f;       // a path's rise per yard between two of its points at most (+ 1.5 yards): no jump, no cliff
    float bossScale = 1.44f;         // a boss support's ally (its boss), times its template's size
    float wavePerCharacter = 1.0f;   // times wave_size, per character in the area
    float wavePaceFast = 0.5f;       // a wave killed within this share of the interval is fast
    float wavePaceStep = 0.25f;      // the pace factor's move after each wave
    float wavePaceMax = 2.0f;        // the pace factor's cap

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
