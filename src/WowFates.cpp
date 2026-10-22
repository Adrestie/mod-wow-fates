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

#include "WowFates.h"

#include "Config.h"
#include "ObjectMgr.h"
#include "WorldSession.h"

#include <algorithm>
#include <cstdlib>

namespace
{
    WowFatesConfig sConfig;

    float Share(char const* option, float fallback)
    {
        return std::clamp(sConfigMgr->GetOption<float>(option, fallback), 0.0f, 1.0f);
    }

    float Positive(char const* option, float fallback)
    {
        return std::max(sConfigMgr->GetOption<float>(option, fallback), 0.0f);
    }

    // "level:score level:score ...", sorted by level; the default when empty or wrong.
    std::vector<std::pair<uint32, float>> ParseCurve(std::string const& text)
    {
        std::vector<std::pair<uint32, float>> curve;
        std::size_t pos = 0;
        while (pos < text.size())
        {
            std::size_t const end = text.find_first_of(" ,;", pos);
            std::string const item = text.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
            std::size_t const colon = item.find(':');
            if (colon != std::string::npos)
            {
                uint32 const level = uint32(std::strtoul(item.substr(0, colon).c_str(), nullptr, 10));
                float const score = std::strtof(item.substr(colon + 1).c_str(), nullptr);
                if (level > 0 && score > 0.0f)
                    curve.emplace_back(level, score);
            }
            if (end == std::string::npos)
                break;
            pos = end + 1;
        }
        std::sort(curve.begin(), curve.end());
        return curve;
    }

    // The power score of the random bots at their own level (median), measured on the test realm
    // with .fates score: characters with the gear of their level.
    constexpr char const* DEFAULT_CURVE =
        "1:100 5:126 10:160 15:210 20:300 25:350 30:390 35:430 40:520 45:620 "
        "50:800 55:850 60:900 65:1300 70:2000 75:2350 80:3400";
}

WowFatesConfig const& WowFatesConf()
{
    return sConfig;
}

void WowFatesLoadConfig()
{
    WowFatesConfig c;

    c.enable = sConfigMgr->GetOption<bool>("WowFates.Enable", true);

    c.syncEnable = sConfigMgr->GetOption<bool>("WowFates.Synced.Enable", true);
    c.syncInterval = std::max<uint32>(sConfigMgr->GetOption<uint32>("WowFates.Synced.Interval", 5), 1) * IN_MILLISECONDS;
    c.minRatio = std::clamp(sConfigMgr->GetOption<float>("WowFates.Synced.MinRatio", 0.05f), 0.01f, 1.0f);
    c.reduceStats = Share("WowFates.Synced.Reduce.Stats", 1.0f);
    c.reduceRatings = Share("WowFates.Synced.Reduce.Ratings", 1.0f);
    c.reduceHealth = Share("WowFates.Synced.Reduce.Health", 1.0f);
    c.reduceArmor = Share("WowFates.Synced.Reduce.Armor", 1.0f);
    c.reduceDamage = Share("WowFates.Synced.Reduce.Damage", 1.0f);
    c.reduceHealing = Share("WowFates.Synced.Reduce.Healing", 1.0f);
    c.reduceMana = Share("WowFates.Synced.Reduce.Mana", 1.0f);
    c.damageLevelExponent = Positive("WowFates.Synced.DamageLevelExponent", 1.0f);
    c.poolBonus = std::max(sConfigMgr->GetOption<float>("WowFates.Synced.PoolBonus", 1.15f), 0.1f);

    c.primaryWeight = Positive("WowFates.Score.PrimaryWeight", 1.0f);
    c.secondaryWeight = Positive("WowFates.Score.SecondaryWeight", 0.5f);
    c.powerWeight = Positive("WowFates.Score.PowerWeight", 0.5f);

    c.referenceFactor = Positive("WowFates.Reference.Factor", 1.0f);
    c.referenceCurve = ParseCurve(sConfigMgr->GetOption<std::string>("WowFates.Reference.Curve", DEFAULT_CURVE));
    if (c.referenceCurve.empty())
        c.referenceCurve = ParseCurve(DEFAULT_CURVE);

    c.maxPerZone = sConfigMgr->GetOption<uint32>("WowFates.Rotation.MaxPerZone", 3);
    c.countBots = sConfigMgr->GetOption<bool>("WowFates.Rotation.CountBots", false);
    c.idleExpire = std::max<uint32>(sConfigMgr->GetOption<uint32>("WowFates.Rotation.IdleExpire", 900), 60);
    c.spawnInterval = std::max<uint32>(sConfigMgr->GetOption<uint32>("WowFates.Expedition.SpawnInterval", 60), 1) * IN_MILLISECONDS;
    c.cooldown = sConfigMgr->GetOption<uint32>("WowFates.Expedition.Cooldown", 600);
    c.areaInterval = std::max<uint32>(sConfigMgr->GetOption<uint32>("WowFates.Expedition.AreaInterval", 1000), 200);
    // "8": the objective types kept out from under a roof
    c.outdoorTypes.clear();
    {
        std::string const types = sConfigMgr->GetOption<std::string>("WowFates.Expedition.OutdoorTypes", "8");
        for (char const* p = types.c_str(); *p;)
        {
            char* end = nullptr;
            unsigned long const type = std::strtoul(p, &end, 10);
            if (end == p)
            {
                ++p;
                continue;
            }
            if (type > 0 && type < 256)
                c.outdoorTypes.push_back(uint8(type));
            p = end;
        }
    }
    c.engageBase = std::max<uint32>(sConfigMgr->GetOption<uint32>("WowFates.Engage.Base", 3), 1);
    c.engagePerPlayer = sConfigMgr->GetOption<uint32>("WowFates.Engage.PerPlayer", 2);
    c.spawnPlayerDistance = std::max(sConfigMgr->GetOption<float>("WowFates.Spawn.PlayerDistance", 20.0f), 0.0f);
    c.objectReach = std::clamp(sConfigMgr->GetOption<float>("WowFates.Object.Reach", 0.6f), 0.1f, 1.0f);
    c.objectSpacing = std::max(sConfigMgr->GetOption<float>("WowFates.Object.Spacing", 1.5f), 0.5f);
    c.objectMinSpacing = std::max(sConfigMgr->GetOption<float>("WowFates.Object.MinSpacing", 6.0f), 0.0f);
    c.objectMaxHeight = std::max(sConfigMgr->GetOption<float>("WowFates.Object.MaxHeight", 6.0f), 1.0f);
    c.objectMaxSlope = std::max(sConfigMgr->GetOption<float>("WowFates.Object.MaxSlope", 0.5f), 0.05f);
    c.objectPathFactor = std::max(sConfigMgr->GetOption<float>("WowFates.Object.PathFactor", 2.0f), 1.0f);
    c.pathMaxSlope = std::max(sConfigMgr->GetOption<float>("WowFates.Path.MaxSlope", 1.0f), 0.1f);
    c.captiveSpeed = std::clamp(sConfigMgr->GetOption<float>("WowFates.Rescue.CaptiveSpeed", 1.0f), 0.3f, 3.0f);
    c.bossScale = std::clamp(sConfigMgr->GetOption<float>("WowFates.Boss.Scale", 1.44f), 0.5f, 4.0f);
    c.wavePerCharacter = std::max(sConfigMgr->GetOption<float>("WowFates.Waves.PerCharacter", 1.0f), 0.0f);
    c.wavePaceFast = std::clamp(sConfigMgr->GetOption<float>("WowFates.Waves.PaceFast", 0.5f), 0.0f, 1.0f);
    c.wavePaceStep = std::max(sConfigMgr->GetOption<float>("WowFates.Waves.PaceStep", 0.1f), 0.0f);
    c.wavePaceMax = std::max(sConfigMgr->GetOption<float>("WowFates.Waves.PaceMax", 1.25f), 1.0f);
    c.wavePaceMin = std::clamp(sConfigMgr->GetOption<float>("WowFates.Waves.PaceMin", 0.8f), 0.1f, 1.0f);
    c.scaleCount = std::max(sConfigMgr->GetOption<float>("WowFates.Scaling.Count", 0.5f), 0.0f);
    c.scaleHealth = std::max(sConfigMgr->GetOption<float>("WowFates.Scaling.Health", 0.5f), 0.0f);
    c.scaleDamage = std::max(sConfigMgr->GetOption<float>("WowFates.Scaling.Damage", 0.15f), 0.0f);
    c.waveNextAt = std::clamp(sConfigMgr->GetOption<float>("WowFates.Waves.NextAt", 0.25f), 0.0f, 1.0f);
    c.participantScaling = Positive("WowFates.Expedition.ParticipantScaling", 0.6f);
    c.adaptiveSpawn = sConfigMgr->GetOption<bool>("WowFates.Expedition.AdaptiveSpawn", true);
    c.adaptiveWindow = std::max<uint32>(sConfigMgr->GetOption<uint32>("WowFates.Expedition.AdaptiveWindow", 15), 5) * IN_MILLISECONDS;
    c.minRespawn = sConfigMgr->GetOption<uint32>("WowFates.Expedition.MinRespawn", 2) * IN_MILLISECONDS;
    c.maxPopulation = std::max(sConfigMgr->GetOption<float>("WowFates.Expedition.MaxPopulation", 1.25f), 1.0f);
    c.adaptiveAggression = Positive("WowFates.Expedition.AdaptiveAggression", 1.5f);
    c.minAlivePct = std::clamp(sConfigMgr->GetOption<float>("WowFates.Expedition.MinAlivePct", 50.0f), 0.0f, 100.0f);

    c.mediumShare = Positive("WowFates.Participation.MediumShare", 0.5f);
    c.strongShare = std::max(Positive("WowFates.Participation.StrongShare", 1.0f), c.mediumShare);
    c.rewardRange = std::max(sConfigMgr->GetOption<float>("WowFates.Participation.RewardRange", 2.0f), 1.0f);

    c.rewardLevelMargin = std::max(sConfigMgr->GetOption<int32>("WowFates.Reward.LevelMargin", -1), -1);
    c.lightPct = std::min<uint32>(sConfigMgr->GetOption<uint32>("WowFates.Reward.LightPct", 33), 100);
    c.mediumPct = std::clamp<uint32>(sConfigMgr->GetOption<uint32>("WowFates.Reward.MediumPct", 66), c.lightPct, 100);

    sConfig = c;
}

std::string WowFatesText(WorldSession* session, uint32 id)
{
    std::string const* text = session
        ? session->GetModuleString(WOW_FATES_MODULE, id)
        : sObjectMgr->GetModuleString(WOW_FATES_MODULE, id, DEFAULT_LOCALE);
    return text ? *text : std::string();
}
