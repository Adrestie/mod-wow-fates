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
 * mod-wow-fates — the Expeditions.
 *
 * An Expedition is defined in mod_wow_fates_expedition (world database), with
 * the points where it may appear (mod_wow_fates_spawn) and the rewards by
 * level of the player (mod_wow_fates_reward). Only the zones with players
 * have Expeditions: one at least, WowFates.Rotation.MaxPerZone at most, each
 * picked at random among the zone's (weighted, out of cooldown), at one of
 * its points in the zone. One with no activity for WowFates.Rotation.
 * IdleExpire goes, never while players are at it. The area of an Expedition
 * is a circle around its point; whoever stands in it is synced to its level.
 *
 * Objectives, by type:
 *   1 kill: a number of monsters in the area (one entry, or any);
 *   2 assault and boss: the same up to boss_at percent, then a leader appears
 *     in the centre, whose health is the rest of the bar (its own settings in
 *     mod_wow_fates_objective);
 *   3 gathering: objects to pick up, spread over the area and put back as
 *     they are taken; a click counts, nothing goes in the bags;
 *   4 defence: an ally in the centre holds out for hold_time seconds against
 *     waves of monsters from the edge of the area; the bar is the time held,
 *     the Expedition fails when the ally dies;
 *   5 invocation: players channel at an altar in the centre while waves come
 *     at them; the bar fills while someone channels and never goes back;
 *   6 wave support: allies in the centre push back wave_count waves, one
 *     after the other; the bar is the waves pushed back, the Expedition fails
 *     when every ally is dead.
 * The Expedition may bring its own monsters, kept at a number alive.
 *
 * Only active players are rewarded: a kill, or damage and healing in the area
 * worth a share of the average contribution. Standing there earns nothing.
 *
 * Threads: the combat hooks come from the map threads and only record numbers
 * under the lock; everything else (start, end, area, sync, rewards) runs on
 * the world thread, in Update, while the maps are not updating.
 */

#ifndef MOD_WOW_FATES_EXPEDITIONS_H
#define MOD_WOW_FATES_EXPEDITIONS_H

#include "Define.h"
#include "ObjectGuid.h"
#include "Position.h"

#include <atomic>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class ChatHandler;
class Creature;
class GameObject;
class Map;
class Player;
class Unit;
class WorldSession;

enum WowFatesObjective : uint8
{
    WOW_FATES_OBJECTIVE_KILL    = 1,
    WOW_FATES_OBJECTIVE_ASSAULT = 2, // kills, then a boss
    WOW_FATES_OBJECTIVE_GATHER  = 3, // objects to pick up
    WOW_FATES_OBJECTIVE_DEFENCE = 4, // an ally to keep alive against waves
    WOW_FATES_OBJECTIVE_INVOKE  = 5, // channel at an altar against waves
    WOW_FATES_OBJECTIVE_SUPPORT = 6  // allies pushing back a number of waves
};

// The channel of an invocation: Ritual (tools/gen_spells.py), ten seconds at
// the altar's focus, an invisible creature on it, for the beam; a blow,
// moving or a cast breaks it.
constexpr uint32 WOW_FATES_CHANNEL_SPELL = 96004;
constexpr uint32 WOW_FATES_ALTAR_FOCUS = 96300;
constexpr float WOW_FATES_CHANNEL_RANGE = 6.0f; // yards from the altar

struct WowFatesExpeditionDef
{
    uint32 id = 0;
    std::string name;                                  // enUS
    std::unordered_map<std::string, std::string> names; // by locale (frFR, deDE...)
    uint8 level = 1;                                   // sync level and reference
    uint8 objectiveType = WOW_FATES_OBJECTIVE_KILL;
    uint32 objectiveEntry = 0;                         // 0 = any hostile monster
    uint32 objectiveCount = 10;
    uint32 summonEntry = 0;                            // 0 = the monsters already there
    uint32 summonCount = 0;                            // alive at once
    uint32 summonRespawn = 10;                         // s between two summons
    float radius = 60.0f;
    uint32 duration = 0;                               // s without activity before it goes, 0 = WowFates.Rotation.IdleExpire
    uint32 cooldown = 0;                               // s, 0 = WowFates.Expedition.Cooldown
    uint32 weight = 1;                                 // 0 = only by command
    // mod_wow_fates_objective, by type
    uint32 bossEntry = 0;                              // assault: the leader
    uint8 bossAt = 70;                                 // assault: percent at which it appears
    uint32 gatherEntry = 0;                            // gathering: the gameobject
    uint32 gatherCount = 8;                            // gathering: of them out at once
    uint32 gatherRespawn = 5;                          // gathering: s before a taken one is put back
    uint32 allyEntry = 0;                              // defence: the ally in the centre
    uint32 holdTime = 240;                             // defence: s to hold out
    uint32 waveInterval = 40;                          // waves: s between two
    uint32 waveSize = 3;                               // waves: monsters of the first, for one player
    uint32 waveGrowth = 1;                             // waves: more in each next one
    uint32 channelEntry = 0;                           // invocation: the altar (gameobject)
    uint32 allyCount = 1;                              // wave support: allies in the centre
    uint32 waveCount = 5;                              // wave support: waves to push back
    std::vector<uint32> spawns;                        // ids in mod_wow_fates_spawn
};

struct WowFatesSpawnPoint
{
    uint32 id = 0;
    uint32 expedition = 0;
    uint32 map = 0;
    float x = 0.0f, y = 0.0f, z = 0.0f, o = 0.0f;
    uint32 zone = 0;   // the game's zone (AreaTable), read at loading
};

// How much a player took part, from his damage and healing against the
// average of those who did any (WowFates.Participation.*).
enum WowFatesParticipation : uint8
{
    WOW_FATES_PART_NONE   = 0, // nothing done: no reward
    WOW_FATES_PART_LIGHT  = 1, // something: WowFates.Reward.LightPct of each reward
    WOW_FATES_PART_MEDIUM = 2, // MediumShare of the average: WowFates.Reward.MediumPct
    WOW_FATES_PART_STRONG = 3  // StrongShare of the average: everything, single items included
};

struct WowFatesReward
{
    uint32 expedition = 0; // 0 = every Expedition, added to its own rows
    uint8 minLevel = 1;
    uint8 maxLevel = 255;
    uint32 money = 0;      // copper
    uint32 xp = 0;
    uint32 item = 0;
    uint32 itemCount = 1;
    bool hidden = false;   // shown as "?" until given
};

struct WowFatesContribution
{
    uint64 damage = 0;
    uint64 healing = 0;
    uint32 kills = 0;
    uint32 picks = 0;     // objects gathered
    int64 channel = 0;    // ms channeled at an invocation's altar
};

struct WowFatesActiveExpedition
{
    WowFatesExpeditionDef const* def = nullptr;
    WowFatesSpawnPoint spawn;
    uint32 zone = 0;
    int64 remaining = 0;        // ms before it goes for lack of activity
    int64 elapsed = 0;          // ms since its start
    int64 lastActivity = 0;     // ms since its start, of the last kill, blow, heal, object, channel
    bool inProgress = false;    // players at it: in the area, active lately (the map's "in progress")
    uint32 progress = 0;        // kills counted, for the GM commands
    double progressPct = 0.0;   // what the players see: 100 completes it
    uint8 announcedQuarter = 0; // quarters of the objective already announced
    uint32 sentProgress = 0;    // progress the clients last heard of
    int64 summonTimer = 0;      // ms
    bool filled = false;        // the first wave of monsters is out
    std::deque<int64> killTimes; // when its monsters died (ms since the start), for the pace
    std::vector<ObjectGuid> summons;
    std::unordered_map<ObjectGuid, WowFatesContribution> contributors;
    std::unordered_set<ObjectGuid> inside; // players in the area, synced to it
    std::unordered_map<ObjectGuid, uint32> sentStatus; // kills x 4 + participation, last sent

    // Assault and boss
    bool bossOut = false;       // the leader has appeared
    ObjectGuid boss;
    uint32 bossHealth = 0;      // its maximum health for one player
    uint32 bossPlayers = 1;     // the players its health was set for

    // Gathering
    std::vector<ObjectGuid> objects; // out in the area
    int64 gatherTimer = 0;      // ms before the next one is put back

    // Defence and its waves
    ObjectGuid ally;
    uint32 allyHealth = 0;      // its maximum health for one player
    uint32 allyPlayers = 1;     // the players its health was set for
    bool started = false;       // a player came: the clock and the waves run
    int64 held = 0;             // ms held
    int64 waveTimer = 0;        // ms before the next wave
    uint32 wave = 0;            // waves sent
    bool failed = false;        // the ally died

    // Invocation
    ObjectGuid altar;
    ObjectGuid focus;           // what the channels aim at
    std::unordered_map<ObjectGuid, int64> channelers; // when each one's channel ends (elapsed)

    // Wave support
    std::vector<ObjectGuid> allies;
    std::vector<ObjectGuid> waveMobs; // the wave on its way
    uint32 repelled = 0;        // waves pushed back
};

class WowFatesExpeditionMgr
{
public:
    static WowFatesExpeditionMgr* instance();

    void LoadFromDB();
    void Update(uint32 diff);

    // Commands. Return false when it could not be done.
    bool Start(uint32 id, uint32 spawnId = 0);
    bool Stop(uint32 id);
    bool Complete(uint32 id);
    void StopAll();
    void List(ChatHandler* handler);
    void ListDefinitions(ChatHandler* handler);
    void ListSynced(ChatHandler* handler);
    uint32 CountDefinitions() const { return uint32(_defs.size()); }
    uint32 CountSpawns() const { return uint32(_spawns.size()); }
    uint32 CountRewards() const { return uint32(_rewards.size()); }

    // The level a player is synced to, 0 when he is in no Expedition.
    uint8 SyncLevel(Player const* player);

    // Combat hooks, from any map thread.
    void OnDamage(Unit* attacker, Unit* victim, uint32 damage);
    void OnHeal(Unit* healer, Unit* target, uint32 gain);
    void OnDeath(Unit* victim, Unit* killer);
    // A gathering's object used, from its map's thread.
    void OnGather(Player* player, GameObject* go);
    // An invocation's altar used, from its map's thread.
    void OnChannel(Player* player, GameObject* go);

    void OnLogout(Player* player);

    // The map of the clients (data/lua/WowFates/WowFates_Client.lua): every
    // active Expedition, sent at login, on a change of zone and when the
    // client asks; then each start, progress and end to every player.
    void SendList(Player* player);
    bool OnAddonMessage(Player* player, std::string const& msg);

    std::string Name(WowFatesExpeditionDef const* def, WorldSession* session) const;

private:
    WowFatesActiveExpedition* FindAt(Unit const* unit);
    bool Inside(WowFatesActiveExpedition const& active, Unit const* unit, float rangeFactor = 1.0f) const;
    void StartLocked(WowFatesExpeditionDef const* def, WowFatesSpawnPoint const* spawn);
    void Rotate(bool fill);
    bool StartInZone(uint32 zone, bool force);
    uint32 IdleExpire(WowFatesExpeditionDef const* def) const;
    int64 Elapsed(WowFatesActiveExpedition const& active) const { return active.elapsed; }
    void Active(WowFatesActiveExpedition& active) { active.lastActivity = active.elapsed; }
    void UpdateArea(WowFatesActiveExpedition& active, Map* map);
    void UpdateSync(uint32 diff);
    void UpdateSummons(WowFatesActiveExpedition& active, Map* map, uint32 diff);
    Position SpawnPoint(WowFatesActiveExpedition const& active, Map* map) const;
    void UpdateBoss(WowFatesActiveExpedition& active, Map* map);
    void SpawnBoss(WowFatesActiveExpedition& active, Map* map);
    void UpdateGather(WowFatesActiveExpedition& active, Map* map);
    void UpdateDefence(WowFatesActiveExpedition& active, Map* map, uint32 diff);
    Creature* SpawnAlly(WowFatesActiveExpedition& active, Map* map, Position const& pos);
    void SendWave(WowFatesActiveExpedition& active, Map* map, Unit* target);
    bool Start(WowFatesActiveExpedition& active, char const* banner, std::string (WowFatesExpeditionMgr::*name)(uint32, WorldSession*) const, uint32 entry);
    Position Centre(WowFatesActiveExpedition const& active, Map* map, float spread = 0.0f) const;
    void UpdateInvoke(WowFatesActiveExpedition& active, Map* map, uint32 diff);
    static int64 ChannelDuration();
    void UpdateSupport(WowFatesActiveExpedition& active, Map* map, uint32 diff);
    void ScaleHealth(Creature* creature, uint32 baseHealth, uint32& setFor, uint32 players) const;
    std::string CreatureName(uint32 entry, WorldSession* session) const;
    std::string GameObjectName(uint32 entry, WorldSession* session) const;
    double Needed(WowFatesActiveExpedition const& active) const;
    // Ended: success, failure, or gone for lack of activity (quietly).
    void Finish(WowFatesActiveExpedition& active, bool success, bool expired = false);
    void Reward(WowFatesActiveExpedition const& active, Player* player, WowFatesParticipation part);
    std::string Objective(WowFatesActiveExpedition const& active, WorldSession* session) const;
    void Leave(ObjectGuid guid, bool announce);
    std::string MapLine(WowFatesActiveExpedition const& active, WorldSession* session) const;
    std::string TrackerLabel(WowFatesActiveExpedition const& active, WorldSession* session) const;
    WowFatesParticipation Participation(WowFatesActiveExpedition const& active, ObjectGuid guid) const;
    std::vector<WowFatesReward const*> RewardRows(WowFatesActiveExpedition const& active, Player const* player) const;
    std::string RewardLine(WowFatesActiveExpedition const& active, Player const* player) const;
    void SendStatus(WowFatesActiveExpedition& active);
    void LeaveArea(WowFatesActiveExpedition const& active, ObjectGuid guid);
    void BroadcastSet(WowFatesActiveExpedition const& active, bool withRewards = false);
    void BroadcastDel(uint32 id);

    std::map<uint32, WowFatesExpeditionDef> _defs;
    std::map<uint32, WowFatesSpawnPoint> _spawns;
    std::vector<WowFatesReward> _rewards;

    std::recursive_mutex _lock;
    std::map<uint32, WowFatesActiveExpedition> _active; // by Expedition id
    std::unordered_map<uint32, int64> _cooldowns;        // ms left, by Expedition id

    struct SyncState
    {
        uint32 expedition = 0;
        int64 timer = 0; // ms to the next measure
        float ratio = 1.0f; // the last one applied
        int32 sentPct = -1; // the share of power the client last heard of
    };
    std::unordered_map<ObjectGuid, SyncState> _synced;
    // Players who left while out of the world (a teleport between maps):
    // their debuff comes off when they are back in it.
    std::unordered_set<ObjectGuid> _pendingUnsync;
    std::atomic<uint32> _activeCount{0};

    std::unordered_set<uint32> _populated; // zones with players, at the last look
    int64 _rotateTimer = 0;
    int64 _areaTimer = 0;
};

#define sWowFatesExpeditionMgr WowFatesExpeditionMgr::instance()

#endif
