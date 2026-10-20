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
 * mod-wow-fates — the Synced debuff.
 *
 * The power score of a player is read live on him: his five main stats, his
 * secondary ratings and the attack power or spell power his gear and auras
 * give him, each weighted. The reference score is what a character of his
 * race and class is expected to have at the Expedition's level: his base stats
 * at that level, plus a gear budget that follows the heirloom curve
 * (ScalingStatValues.dbc). Their ratio, when below 1, is the share of his power
 * the debuff leaves him: stats, ratings, maximum health, armor, damage done
 * and healing done are cut by the gap.
 *
 * The score is always measured without the debuff: its amounts are set to 0,
 * the stats read, then the new amounts set, all within one world tick, so the
 * client only ever sees the result.
 */

#ifndef MOD_WOW_FATES_SYNC_H
#define MOD_WOW_FATES_SYNC_H

#include "Define.h"
#include "ObjectGuid.h"

#include <mutex>
#include <unordered_map>
#include <utility>

class Player;
class Unit;

// What the debuff took from a player, passed on to his pets and guardians.
struct WowFatesShares
{
    uint8 level = 0;
    int32 statsPct = 0;     // the amounts of the visible aura
    int32 damagePct = 0;
    int32 healingPct = 0;
    float healthShare = 1.0f; // maximum health kept, over the bare one
    float manaShare = 1.0f;
    float armorShare = 1.0f;
};

struct WowFatesScore
{
    float primary = 0.0f;   // strength + agility + stamina + intellect + spirit
    float secondary = 0.0f; // ratings, unweighted
    float power = 0.0f;     // attack power or spell power from gear and auras, unweighted
    float total = 0.0f;     // weighted sum
};

class WowFatesSync
{
public:
    static WowFatesSync* instance();

    // The player's score as he stands, debuff included if he has it.
    WowFatesScore Measure(Player* player) const;

    // The player's score without the debuff; his debuff is left as it was.
    WowFatesScore MeasureUnsynced(Player* player) const;

    // What a character with the gear of that level scores (WowFates.Reference.Curve).
    float Reference(Player const* player, uint8 level) const;

    // Maximum health and mana of a character of the player's race and class
    // at that level, with the reference's gear.
    std::pair<float, float> ReferencePools(Player const* player, uint8 level) const;

    // The share of damage and healing a player keeps at that level, from the
    // level alone (1 at or below it).
    float LevelShare(Player const* player, uint8 level) const;

    // Measures the player and sets the debuff for that level: applied,
    // refreshed, or removed when he is not above the reference. Returns the
    // ratio applied, 1 when none.
    float Apply(Player* player, uint8 level) const;

    // The player's debuff and that of every unit he controls (pets, guardians,
    // minions, totems).
    void Remove(Player* player) const;
    bool IsSynced(Player const* player) const;

    // The pets and guardians of a synced player: the same cuts as his, on
    // their own health, armor, stats, damage and healing. Called after each
    // Apply, and between two measures for those summoned since.
    void SyncControlled(Player* player) const;

private:
    // Puts the debuff on a pet or a guardian and sets it bare (every amount
    // at 0); false if the auras could not be put.
    bool PrepareUnit(Unit* unit) const;
    void ApplyToUnit(Unit* unit, WowFatesShares const& shares, uint32 healthBare, uint32 manaBare, uint32 armorBare) const;
    void RemoveFromUnit(Unit* unit) const;

    mutable std::mutex _sharesLock;
    mutable std::unordered_map<ObjectGuid, WowFatesShares> _shares; // by player
};

#define sWowFatesSync WowFatesSync::instance()

#endif
