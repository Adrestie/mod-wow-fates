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
 * level of the player (mod_wow_fates_reward). The manager keeps up to
 * WowFates.Expedition.MaxActive of them running: it picks one at random
 * (weighted, out of cooldown), at one of its points, for its duration. The
 * area of an Expedition is a circle around its point; whoever stands in it is
 * synced to its level.
 *
 * Objective, first type: kill a number of monsters in the area (one entry, or
 * any). The Expedition may bring its own monsters, kept at a number alive.
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

#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class ChatHandler;
class Creature;
class Map;
class Player;
class Unit;
class WorldSession;

enum WowFatesObjective : uint8
{
    WOW_FATES_OBJECTIVE_KILL = 1
    // 2 collect, 3 escort: later
};

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
    uint32 duration = 900;                             // s
    uint32 cooldown = 0;                               // s, 0 = WowFates.Expedition.Cooldown
    uint32 weight = 1;                                 // 0 = only by command
    std::vector<uint32> spawns;                        // ids in mod_wow_fates_spawn
};

struct WowFatesSpawnPoint
{
    uint32 id = 0;
    uint32 expedition = 0;
    uint32 map = 0;
    float x = 0.0f, y = 0.0f, z = 0.0f, o = 0.0f;
};

struct WowFatesReward
{
    uint32 expedition = 0; // 0 = every Expedition without rows of its own for that level
    uint8 minLevel = 1;
    uint8 maxLevel = 255;
    uint32 money = 0;      // copper
    uint32 xp = 0;
    uint32 item = 0;
    uint32 itemCount = 1;
};

struct WowFatesContribution
{
    uint64 damage = 0;
    uint64 healing = 0;
    uint32 kills = 0;
};

struct WowFatesActiveExpedition
{
    WowFatesExpeditionDef const* def = nullptr;
    WowFatesSpawnPoint spawn;
    uint32 zone = 0;
    int64 remaining = 0;        // ms
    uint32 progress = 0;
    uint8 announcedQuarter = 0; // quarters of the objective already announced
    uint32 sentProgress = 0;    // progress the clients last heard of
    int64 summonTimer = 0;      // ms
    bool filled = false;        // the first wave of monsters is out
    std::vector<ObjectGuid> summons;
    std::unordered_map<ObjectGuid, WowFatesContribution> contributors;
    std::unordered_set<ObjectGuid> inside; // players in the area, synced to it
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
    uint32 CountDefinitions() const { return uint32(_defs.size()); }
    uint32 CountSpawns() const { return uint32(_spawns.size()); }
    uint32 CountRewards() const { return uint32(_rewards.size()); }

    // The level a player is synced to, 0 when he is in no Expedition.
    uint8 SyncLevel(Player const* player);

    // Combat hooks, from any map thread.
    void OnDamage(Unit* attacker, Unit* victim, uint32 damage);
    void OnHeal(Unit* healer, Unit* target, uint32 gain);
    void OnDeath(Unit* victim, Unit* killer);

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
    void Rotate();
    void UpdateArea(WowFatesActiveExpedition& active, Map* map);
    void UpdateSync(uint32 diff);
    void UpdateSummons(WowFatesActiveExpedition& active, Map* map, uint32 diff);
    void Finish(WowFatesActiveExpedition& active, bool success);
    void Reward(WowFatesActiveExpedition const& active, Player* player);
    void AnnounceZone(WowFatesActiveExpedition const& active, uint32 text);
    void AnnounceParticipants(WowFatesActiveExpedition const& active, uint32 text, uint32 a = 0, uint32 b = 0);
    std::string Objective(WowFatesActiveExpedition const& active, WorldSession* session) const;
    void Leave(ObjectGuid guid, bool announce);
    std::string MapLine(WowFatesActiveExpedition const& active, WorldSession* session) const;
    void BroadcastSet(WowFatesActiveExpedition const& active);
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
    };
    std::unordered_map<ObjectGuid, SyncState> _synced;
    // Players who left while out of the world (a teleport between maps):
    // their debuff comes off when they are back in it.
    std::unordered_set<ObjectGuid> _pendingUnsync;
    std::atomic<uint32> _activeCount{0};

    int64 _rotateTimer = 0;
    int64 _areaTimer = 0;
};

#define sWowFatesExpeditionMgr WowFatesExpeditionMgr::instance()

#endif
