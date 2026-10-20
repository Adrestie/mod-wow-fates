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

#include "WowFatesSync.h"
#include "WowFates.h"

#include "DBCStores.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "SpellAuraEffects.h"
#include "SpellAuras.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace
{
    constexpr std::array<uint32, 4> SYNC_SPELLS =
    {
        SPELL_WOW_FATES_SYNCED,
        SPELL_WOW_FATES_SYNCED_OFFENSE,
        SPELL_WOW_FATES_SYNCED_DEFENSE,
        SPELL_WOW_FATES_SYNCED_HEALTH
    };

    // The highest cut the debuff makes, so that no amount reaches -100 %.
    constexpr int32 MAX_CUT_PCT = 99;

    uint32 Rating(Player const* player, CombatRating rating)
    {
        return player->GetUInt32Value(uint16(PLAYER_FIELD_COMBAT_RATING_1) + rating);
    }

    uint32 MaxRating(Player const* player, CombatRating a, CombatRating b, CombatRating c)
    {
        return std::max({ Rating(player, a), Rating(player, b), Rating(player, c) });
    }

    // One amount moves every rating of a group: the smallest of them, so that
    // none goes below its share.
    uint32 MinRating(Player const* player, CombatRating a, CombatRating b, CombatRating c)
    {
        return std::min({ Rating(player, a), Rating(player, b), Rating(player, c) });
    }

    uint32 MinRating(Player const* player, CombatRating a, CombatRating b)
    {
        return std::min(Rating(player, a), Rating(player, b));
    }

    void SetAmount(Aura* aura, uint8 effIndex, int32 amount)
    {
        if (!aura)
            return;
        if (AuraEffect* effect = aura->GetEffect(effIndex))
            if (effect->GetAmount() != amount)
                effect->ChangeAmount(amount);
    }

    using Amounts = std::array<std::array<int32, MAX_SPELL_EFFECTS>, SYNC_SPELLS.size()>;

    // Sets every amount of the debuff to 0, and says what they were. The
    // health aura goes first and comes back last: it keeps the health
    // percentage when it changes, the stats do not, so that order gives the
    // player back exactly the health he had.
    Amounts Neutralize(Player* player)
    {
        Amounts previous{};
        for (std::size_t s = SYNC_SPELLS.size(); s-- > 0;)
            if (Aura* aura = player->GetAura(SYNC_SPELLS[s]))
                for (uint8 e = 0; e < MAX_SPELL_EFFECTS; ++e)
                    if (AuraEffect* effect = aura->GetEffect(e))
                    {
                        previous[s][e] = effect->GetAmount();
                        SetAmount(aura, e, 0);
                    }
        return previous;
    }

    void Restore(Player* player, Amounts const& amounts)
    {
        for (std::size_t s = 0; s < SYNC_SPELLS.size(); ++s)
            if (Aura* aura = player->GetAura(SYNC_SPELLS[s]))
                for (uint8 e = 0; e < MAX_SPELL_EFFECTS; ++e)
                    SetAmount(aura, e, amounts[s][e]);
    }

    // The heirloom curve at a level: the stat multiplier of a shoulder slot
    // and the spell power, read from ScalingStatValues.dbc. Past its last
    // level (80), the curve goes on in a straight line from its last two
    // levels.
    struct Curve
    {
        float stat = 0.0f;
        float spellPower = 0.0f;
    };

    Curve HeirloomCurve(uint8 level)
    {
        ScalingStatValuesEntry const* byLevel[256] = {};
        uint32 top = 0;
        for (uint32 i = 0; i < sScalingStatValuesStore.GetNumRows(); ++i)
            if (ScalingStatValuesEntry const* ssv = sScalingStatValuesStore.LookupEntry(i))
                if (ssv->Level > 0 && ssv->Level < 256)
                {
                    byLevel[ssv->Level] = ssv;
                    top = std::max(top, ssv->Level);
                }

        auto at = [&](uint32 l) -> Curve
        {
            for (; l > 0; --l)
                if (byLevel[l])
                    return { float(byLevel[l]->getssdMultiplier(1)), float(byLevel[l]->spellPower) };
            return {};
        };

        if (!top)
            return {};
        if (level <= top)
            return at(std::max<uint8>(level, 1));

        Curve const last = at(top);
        Curve const before = at(top - 1);
        float const steps = float(level - top);
        return { last.stat + (last.stat - before.stat) * steps,
                 last.spellPower + (last.spellPower - before.spellPower) * steps };
    }

    int32 CutPct(float gap, float share)
    {
        int32 const pct = int32(std::lround(gap * std::clamp(share, 0.0f, 1.0f) * 100.0f));
        return -std::clamp(pct, 0, MAX_CUT_PCT);
    }

    int32 CutRating(float gap, float share, uint32 rating)
    {
        return -int32(std::lround(gap * std::clamp(share, 0.0f, 1.0f) * float(rating)));
    }

    // The percentage that brings `measured` to `target`, never an increase.
    int32 PctToward(float target, float measured)
    {
        if (measured <= 0.0f || target >= measured)
            return 0;
        int32 const pct = int32(std::lround((1.0f - target / measured) * 100.0f));
        return -std::clamp(pct, 0, MAX_CUT_PCT);
    }
}

WowFatesSync* WowFatesSync::instance()
{
    static WowFatesSync instance;
    return &instance;
}

WowFatesScore WowFatesSync::Measure(Player* player) const
{
    WowFatesConfig const& c = WowFatesConf();
    WowFatesScore score;

    for (uint8 i = STAT_STRENGTH; i < MAX_STATS; ++i)
        score.primary += player->GetStat(Stats(i));

    // A rating that gear gives to melee, ranged and spells alike counts once.
    score.secondary = float(
        MaxRating(player, CR_HIT_MELEE, CR_HIT_RANGED, CR_HIT_SPELL) +
        MaxRating(player, CR_CRIT_MELEE, CR_CRIT_RANGED, CR_CRIT_SPELL) +
        MaxRating(player, CR_HASTE_MELEE, CR_HASTE_RANGED, CR_HASTE_SPELL) +
        MaxRating(player, CR_CRIT_TAKEN_MELEE, CR_CRIT_TAKEN_RANGED, CR_CRIT_TAKEN_SPELL) +
        Rating(player, CR_EXPERTISE) + Rating(player, CR_ARMOR_PENETRATION) +
        Rating(player, CR_DEFENSE_SKILL) + Rating(player, CR_DODGE) +
        Rating(player, CR_PARRY) + Rating(player, CR_BLOCK));

    // Attack power from gear and auras (not the part stats give, already
    // counted), and the best spell or healing power.
    float power = std::max(player->GetFlatModifierValue(UNIT_MOD_ATTACK_POWER, TOTAL_VALUE),
                           player->GetFlatModifierValue(UNIT_MOD_ATTACK_POWER_RANGED, TOTAL_VALUE));
    for (uint8 school = SPELL_SCHOOL_HOLY; school < MAX_SPELL_SCHOOL; ++school)
        power = std::max(power, float(player->GetInt32Value(PLAYER_FIELD_MOD_DAMAGE_DONE_POS + school)));
    power = std::max(power, float(player->GetInt32Value(PLAYER_FIELD_MOD_HEALING_DONE_POS)));
    score.power = std::max(power, 0.0f);

    score.total = c.primaryWeight * score.primary + c.secondaryWeight * score.secondary + c.powerWeight * score.power;
    return score;
}

WowFatesScore WowFatesSync::MeasureUnsynced(Player* player) const
{
    Amounts const previous = Neutralize(player);
    WowFatesScore const score = Measure(player);
    Restore(player, previous);
    return score;
}

float WowFatesSync::Reference(Player const* player, uint8 level) const
{
    WowFatesConfig const& c = WowFatesConf();

    PlayerLevelInfo info;
    sObjectMgr->GetPlayerLevelInfo(player->getRace(), player->getClass(), std::max<uint8>(level, 1), &info);
    float base = 0.0f;
    for (uint8 i = STAT_STRENGTH; i < MAX_STATS; ++i)
        base += float(info.stats[i]);

    Curve const curve = HeirloomCurve(level);
    float const reference =
        c.primaryWeight * (base + c.referenceGearPrimary * curve.stat) +
        c.secondaryWeight * (c.referenceGearSecondary * curve.stat) +
        c.powerWeight * (c.referenceGearPower * curve.spellPower);

    return std::max(reference * c.referenceFactor, 0.0f);
}

float WowFatesSync::Apply(Player* player, uint8 level) const
{
    WowFatesConfig const& c = WowFatesConf();

    // Measured bare: the debuff, if any, set to nothing first.
    Neutralize(player);
    WowFatesScore const score = Measure(player);
    float const reference = Reference(player, level);
    float const ratio = score.total > 0.0f ? std::clamp(reference / score.total, c.minRatio, 1.0f) : 1.0f;

    if (ratio >= 0.995f)
    {
        Remove(player);
        return 1.0f;
    }

    std::array<Aura*, SYNC_SPELLS.size()> auras{};
    for (std::size_t s = 0; s < SYNC_SPELLS.size(); ++s)
    {
        auras[s] = player->GetAura(SYNC_SPELLS[s]);
        if (!auras[s])
            auras[s] = player->AddAura(SYNC_SPELLS[s], player);
        if (!auras[s])
        {
            Remove(player);
            return 1.0f;
        }
    }

    float const gap = 1.0f - ratio;
    uint32 const healthBare = player->GetMaxHealth();
    uint32 const armorBare = player->GetArmor();

    // Ratings first, read before anything moves.
    SetAmount(auras[1], EFFECT_0, CutRating(gap, c.reduceRatings, MinRating(player, CR_HIT_MELEE, CR_HIT_RANGED, CR_HIT_SPELL)));
    SetAmount(auras[1], EFFECT_1, CutRating(gap, c.reduceRatings, MinRating(player, CR_CRIT_MELEE, CR_CRIT_RANGED, CR_CRIT_SPELL)));
    SetAmount(auras[1], EFFECT_2, CutRating(gap, c.reduceRatings, MinRating(player, CR_HASTE_MELEE, CR_HASTE_RANGED, CR_HASTE_SPELL)));
    SetAmount(auras[2], EFFECT_0, CutRating(gap, c.reduceRatings, Rating(player, CR_DEFENSE_SKILL)));
    SetAmount(auras[2], EFFECT_1, CutRating(gap, c.reduceRatings, MinRating(player, CR_DODGE, CR_PARRY)));
    SetAmount(auras[2], EFFECT_2, CutRating(gap, c.reduceRatings, MinRating(player, CR_EXPERTISE, CR_ARMOR_PENETRATION)));
    SetAmount(auras[3], EFFECT_2, CutRating(gap, c.reduceRatings, MinRating(player, CR_CRIT_TAKEN_MELEE, CR_CRIT_TAKEN_RANGED, CR_CRIT_TAKEN_SPELL)));

    SetAmount(auras[0], EFFECT_0, CutPct(gap, c.reduceStats));
    SetAmount(auras[0], EFFECT_1, CutPct(gap, c.reduceDamage));
    SetAmount(auras[0], EFFECT_2, CutPct(gap, c.reduceHealing));

    // Health and armor do not follow the stats alone (base health of the
    // level, armor of the gear): what the stats left, brought to the share.
    SetAmount(auras[3], EFFECT_0, PctToward(float(healthBare) * (1.0f - gap * std::clamp(c.reduceHealth, 0.0f, 1.0f)), float(player->GetMaxHealth())));
    SetAmount(auras[3], EFFECT_1, PctToward(float(armorBare) * (1.0f - gap * std::clamp(c.reduceArmor, 0.0f, 1.0f)), float(player->GetArmor())));

    // The level shows on the icon.
    if (auras[0]->GetCharges() != level)
        auras[0]->SetCharges(level);

    return ratio;
}

void WowFatesSync::Remove(Player* player) const
{
    for (uint32 spell : SYNC_SPELLS)
        player->RemoveAurasDueToSpell(spell);
}

bool WowFatesSync::IsSynced(Player const* player) const
{
    return player->HasAura(SPELL_WOW_FATES_SYNCED);
}
