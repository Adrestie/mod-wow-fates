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

    // The reference curve at a level: a straight line between its points, and
    // past its ends from the two nearest.
    float CurveAt(std::vector<std::pair<uint32, float>> const& curve, uint32 level)
    {
        if (curve.empty())
            return 0.0f;
        if (curve.size() == 1)
            return curve.front().second;
        std::size_t i = 1;
        while (i < curve.size() - 1 && curve[i].first < level)
            ++i;
        auto const& [l0, s0] = curve[i - 1];
        auto const& [l1, s1] = curve[i];
        float const t = l1 == l0 ? 0.0f : (float(level) - float(l0)) / (float(l1) - float(l0));
        return std::max(s0 + (s1 - s0) * t, 1.0f);
    }

    // Health from stamina and mana from intellect, as the game counts them
    // (Player::GetHealthBonusFromStamina, GetManaBonusFromIntellect).
    float HealthFromStamina(float stamina)
    {
        float const base = std::min(stamina, 20.0f);
        return base + std::max(stamina - base, 0.0f) * 10.0f;
    }

    float ManaFromIntellect(float intellect)
    {
        float const base = std::min(intellect, 20.0f);
        return base + std::max(intellect - base, 0.0f) * 15.0f;
    }

    // The class's base health and mana at a level (player_class_stats); a
    // class without rows (custom) follows the warrior's health and the mage's
    // mana, from its own values at its own level.
    std::pair<float, float> BaseHealthMana(Player const* player, uint8 level)
    {
        PlayerClassLevelInfo info;
        sObjectMgr->GetPlayerClassLevelInfo(player->getClass(), level, &info);
        float health = float(info.basehealth);
        float mana = float(info.basemana);

        if (!health || (!mana && player->GetCreateMana()))
        {
            PlayerClassLevelInfo warriorAt, warriorOwn, mageAt, mageOwn;
            sObjectMgr->GetPlayerClassLevelInfo(CLASS_WARRIOR, level, &warriorAt);
            sObjectMgr->GetPlayerClassLevelInfo(CLASS_WARRIOR, player->GetLevel(), &warriorOwn);
            sObjectMgr->GetPlayerClassLevelInfo(CLASS_MAGE, level, &mageAt);
            sObjectMgr->GetPlayerClassLevelInfo(CLASS_MAGE, player->GetLevel(), &mageOwn);
            if (!health && warriorOwn.basehealth)
                health = float(player->GetCreateHealth()) * float(warriorAt.basehealth) / float(warriorOwn.basehealth);
            if (!mana && player->GetCreateMana() && mageOwn.basemana)
                mana = float(player->GetCreateMana()) * float(mageAt.basemana) / float(mageOwn.basemana);
        }
        return { health, mana };
    }

    // The creatures' base damage at a level (creature_classlevelstats, of the
    // expansion of that level): how much harder a level hits than another.
    float LevelDamage(uint8 level)
    {
        CreatureBaseStats const* stats = sObjectMgr->GetCreatureBaseStats(std::max<uint8>(level, 1), CLASS_WARRIOR);
        if (!stats)
            return 0.0f;
        uint8 const expansion = level > 70 ? 2 : (level > 60 ? 1 : 0);
        return stats->BaseDamage[expansion];
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

float WowFatesSync::Reference(Player const* /*player*/, uint8 level) const
{
    WowFatesConfig const& c = WowFatesConf();
    return CurveAt(c.referenceCurve, std::max<uint8>(level, 1)) * c.referenceFactor;
}

// The maximum health and mana of a character of the player's race and class
// at that level, with the gear of the reference: base health and mana, plus
// what its stamina and intellect give, the base stats raised by the share of
// the reference score above them.
std::pair<float, float> WowFatesSync::ReferencePools(Player const* player, uint8 level) const
{
    WowFatesConfig const& c = WowFatesConf();
    level = std::max<uint8>(level, 1);

    PlayerLevelInfo info;
    sObjectMgr->GetPlayerLevelInfo(player->getRace(), player->getClass(), level, &info);
    float base = 0.0f;
    for (uint8 i = STAT_STRENGTH; i < MAX_STATS; ++i)
        base += float(info.stats[i]);
    float const gear = base > 0.0f ? std::max(Reference(player, level) / (std::max(c.primaryWeight, 0.01f) * base), 1.0f) : 1.0f;

    auto const [baseHealth, baseMana] = BaseHealthMana(player, level);
    float const health = baseHealth + HealthFromStamina(float(info.stats[STAT_STAMINA]) * gear);
    float const mana = baseMana > 0.0f ? baseMana + ManaFromIntellect(float(info.stats[STAT_INTELLECT]) * gear) : 0.0f;
    return { health * c.poolBonus, mana * c.poolBonus };
}

// How much of his damage and healing a player of his level keeps at that
// level, from the level alone: the creatures' damage curve, to a power.
float WowFatesSync::LevelShare(Player const* player, uint8 level) const
{
    if (player->GetLevel() <= level)
        return 1.0f;
    float const at = LevelDamage(level);
    float const own = LevelDamage(player->GetLevel());
    if (at <= 0.0f || own <= 0.0f)
        return 1.0f;
    return std::clamp(std::pow(at / own, WowFatesConf().damageLevelExponent), 0.0f, 1.0f);
}

float WowFatesSync::Apply(Player* player, uint8 level) const
{
    WowFatesConfig const& c = WowFatesConf();

    // Measured bare: the debuff, if any, set to nothing first.
    Neutralize(player);
    WowFatesScore const score = Measure(player);
    float const reference = Reference(player, level);
    float const ratio = score.total > 0.0f ? std::clamp(reference / score.total, c.minRatio, 1.0f) : 1.0f;

    // Above the Expedition's level, or stronger than its reference: synced.
    float const levelShare = LevelShare(player, level);
    if (ratio >= 0.995f && levelShare >= 0.995f)
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

    // A fresh aura comes with its base amounts (-1): back to 0 before the
    // health and armor are read.
    Neutralize(player);

    float const gap = 1.0f - ratio;
    uint32 const healthBare = player->GetMaxHealth();
    uint32 const manaBare = player->GetMaxPower(POWER_MANA);
    uint32 const armorBare = player->GetArmor();

    // Ratings first, read before anything moves.
    SetAmount(auras[1], EFFECT_0, CutRating(gap, c.reduceRatings, MinRating(player, CR_HIT_MELEE, CR_HIT_RANGED, CR_HIT_SPELL)));
    SetAmount(auras[1], EFFECT_1, CutRating(gap, c.reduceRatings, MinRating(player, CR_CRIT_MELEE, CR_CRIT_RANGED, CR_CRIT_SPELL)));
    SetAmount(auras[1], EFFECT_2, CutRating(gap, c.reduceRatings, MinRating(player, CR_HASTE_MELEE, CR_HASTE_RANGED, CR_HASTE_SPELL)));
    SetAmount(auras[2], EFFECT_0, CutRating(gap, c.reduceRatings, Rating(player, CR_DEFENSE_SKILL)));
    SetAmount(auras[2], EFFECT_1, CutRating(gap, c.reduceRatings, MinRating(player, CR_DODGE, CR_PARRY)));
    SetAmount(auras[2], EFFECT_2, CutRating(gap, c.reduceRatings, MinRating(player, CR_EXPERTISE, CR_ARMOR_PENETRATION)));

    // Stats by the power ratio; damage and healing by the lower of the power
    // ratio and the level's share (an ability of level 80 hits as hard naked).
    SetAmount(auras[0], EFFECT_0, CutPct(gap, c.reduceStats));
    float const damageGap = 1.0f - std::min(ratio, levelShare);
    SetAmount(auras[0], EFFECT_1, CutPct(damageGap, c.reduceDamage));
    SetAmount(auras[0], EFFECT_2, CutPct(damageGap, c.reduceHealing));

    // Health and mana to those of the reference character of that level (the
    // base health of a level 80 alone is forty times a level 7's), never more
    // than his own; armor by the power ratio.
    auto const [refHealth, refMana] = ReferencePools(player, level);
    auto toward = [](float own, float reference, float share)
    {
        float const target = std::min(own, reference);
        return own - (own - target) * std::clamp(share, 0.0f, 1.0f);
    };
    SetAmount(auras[3], EFFECT_0, PctToward(toward(float(healthBare), refHealth, c.reduceHealth), float(player->GetMaxHealth())));
    SetAmount(auras[3], EFFECT_1, PctToward(float(armorBare) * (1.0f - gap * std::clamp(c.reduceArmor, 0.0f, 1.0f)), float(player->GetArmor())));
    if (manaBare && refMana > 0.0f)
        SetAmount(auras[3], EFFECT_2, PctToward(toward(float(manaBare), refMana, c.reduceMana), float(player->GetMaxPower(POWER_MANA))));

    // The level shows on the icon.
    if (auras[0]->GetCharges() != level)
        auras[0]->SetCharges(level);

    // Synced by his level alone, he still is: below 1.
    return std::min(ratio, 0.99f);
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
