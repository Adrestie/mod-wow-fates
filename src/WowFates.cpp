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
    c.syncAnnounce = sConfigMgr->GetOption<bool>("WowFates.Synced.Announce", true);

    c.primaryWeight = Positive("WowFates.Score.PrimaryWeight", 1.0f);
    c.secondaryWeight = Positive("WowFates.Score.SecondaryWeight", 0.5f);
    c.powerWeight = Positive("WowFates.Score.PowerWeight", 0.5f);

    c.referenceFactor = Positive("WowFates.Reference.Factor", 1.0f);
    c.referenceGearPrimary = Positive("WowFates.Reference.GearPrimary", 18.0f);
    c.referenceGearSecondary = Positive("WowFates.Reference.GearSecondary", 9.0f);
    c.referenceGearPower = Positive("WowFates.Reference.GearPower", 4.0f);

    c.maxActive = sConfigMgr->GetOption<uint32>("WowFates.Expedition.MaxActive", 3);
    c.spawnInterval = std::max<uint32>(sConfigMgr->GetOption<uint32>("WowFates.Expedition.SpawnInterval", 60), 1) * IN_MILLISECONDS;
    c.cooldown = sConfigMgr->GetOption<uint32>("WowFates.Expedition.Cooldown", 600);
    c.areaInterval = std::max<uint32>(sConfigMgr->GetOption<uint32>("WowFates.Expedition.AreaInterval", 1000), 200);
    c.announce = sConfigMgr->GetOption<bool>("WowFates.Expedition.Announce", true);

    c.minKills = sConfigMgr->GetOption<uint32>("WowFates.Participation.MinKills", 1);
    c.minShare = Positive("WowFates.Participation.MinShare", 0.25f);
    c.rewardRange = std::max(sConfigMgr->GetOption<float>("WowFates.Participation.RewardRange", 2.0f), 1.0f);

    c.rewardLevelMargin = std::max(sConfigMgr->GetOption<int32>("WowFates.Reward.LevelMargin", -1), -1);

    sConfig = c;
}

std::string WowFatesText(WorldSession* session, uint32 id)
{
    std::string const* text = session
        ? session->GetModuleString(WOW_FATES_MODULE, id)
        : sObjectMgr->GetModuleString(WOW_FATES_MODULE, id, DEFAULT_LOCALE);
    return text ? *text : std::string();
}
