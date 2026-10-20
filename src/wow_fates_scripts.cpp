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
 * mod-wow-fates — the hooks into the core.
 */

#include "WowFates.h"
#include "WowFatesExpeditions.h"
#include "WowFatesSync.h"

#include "Player.h"
#include "ScriptMgr.h"

class WowFatesWorldScript : public WorldScript
{
public:
    WowFatesWorldScript() : WorldScript("WowFatesWorldScript",
        {
            WORLDHOOK_ON_AFTER_CONFIG_LOAD,
            WORLDHOOK_ON_STARTUP,
            WORLDHOOK_ON_UPDATE,
            WORLDHOOK_ON_SHUTDOWN
        }) { }

    // At startup and on every `.reload config`.
    void OnAfterConfigLoad(bool /*reload*/) override
    {
        WowFatesLoadConfig();
    }

    void OnStartup() override
    {
        sWowFatesExpeditionMgr->LoadFromDB();
    }

    void OnUpdate(uint32 diff) override
    {
        sWowFatesExpeditionMgr->Update(diff);
    }

    void OnShutdown() override
    {
        sWowFatesExpeditionMgr->StopAll();
    }
};

class WowFatesPlayerScript : public PlayerScript
{
public:
    WowFatesPlayerScript() : PlayerScript("WowFatesPlayerScript",
        {
            // THIS LIST GATES THE CALLS: an overridden method missing from
            // here compiles and never fires.
            PLAYERHOOK_ON_LOGIN,
            PLAYERHOOK_ON_LOGOUT
        }) { }

    // A debuff saved with the character (a crash, a shutdown) does not
    // outlive its Expedition: the area puts it back if he is still inside.
    void OnPlayerLogin(Player* player) override
    {
        sWowFatesSync->Remove(player);
    }

    void OnPlayerLogout(Player* player) override
    {
        sWowFatesExpeditionMgr->OnLogout(player);
    }
};

class WowFatesUnitScript : public UnitScript
{
public:
    WowFatesUnitScript() : UnitScript("WowFatesUnitScript", true,
        {
            UNITHOOK_ON_DAMAGE,
            UNITHOOK_ON_HEAL,
            UNITHOOK_ON_UNIT_DEATH
        }) { }

    // The contributions to the Expeditions.
    void OnDamage(Unit* attacker, Unit* victim, uint32& damage) override
    {
        sWowFatesExpeditionMgr->OnDamage(attacker, victim, damage);
    }

    void OnHeal(Unit* healer, Unit* receiver, uint32& gain) override
    {
        sWowFatesExpeditionMgr->OnHeal(healer, receiver, gain);
    }

    void OnUnitDeath(Unit* unit, Unit* killer) override
    {
        sWowFatesExpeditionMgr->OnDeath(unit, killer);
    }
};

void AddSC_wow_fates_commands();

void AddSC_wow_fates_scripts()
{
    new WowFatesWorldScript();
    new WowFatesPlayerScript();
    new WowFatesUnitScript();
    AddSC_wow_fates_commands();
}
