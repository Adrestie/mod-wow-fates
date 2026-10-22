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
 * mod-wow-fates — the GM commands (.fates).
 */

#include "WowFates.h"
#include "WowFatesExpeditions.h"
#include "WowFatesSync.h"

#include "Chat.h"
#include "ChatCommand.h"
#include "Player.h"
#include "ScriptMgr.h"

#include <algorithm>

using namespace Acore::ChatCommands;

namespace
{
    template<typename... Args>
    void Say(ChatHandler* handler, uint32 id, Args&&... args)
    {
        handler->SendSysMessage(handler->PGetParseModuleString(WOW_FATES_MODULE, id, std::forward<Args>(args)...));
    }

    bool Enabled(ChatHandler* handler)
    {
        if (WowFatesConf().enable)
            return true;
        Say(handler, WOW_FATES_STR_CMD_DISABLED);
        return false;
    }
}

class wow_fates_commandscript : public CommandScript
{
public:
    wow_fates_commandscript() : CommandScript("wow_fates_commandscript") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable fatesTable =
        {
            { "list",     HandleListCommand,     SEC_GAMEMASTER,    Console::Yes },
            { "defs",     HandleDefsCommand,     SEC_GAMEMASTER,    Console::Yes },
            { "start",    HandleStartCommand,    SEC_GAMEMASTER,    Console::Yes },
            { "stop",     HandleStopCommand,     SEC_GAMEMASTER,    Console::Yes },
            { "complete", HandleCompleteCommand, SEC_GAMEMASTER,    Console::Yes },
            { "reload",   HandleReloadCommand,   SEC_ADMINISTRATOR, Console::Yes },
            { "score",    HandleScoreCommand,    SEC_GAMEMASTER,    Console::Yes },
            { "synced",   HandleSyncedCommand,   SEC_GAMEMASTER,    Console::Yes },
            { "check",    HandleCheckCommand,    SEC_ADMINISTRATOR, Console::Yes },
            { "simulate", HandleSimulateCommand, SEC_GAMEMASTER,    Console::Yes }
        };

        static ChatCommandTable commandTable =
        {
            { "fates", fatesTable }
        };

        return commandTable;
    }

    static bool HandleListCommand(ChatHandler* handler)
    {
        sWowFatesExpeditionMgr->List(handler);
        return true;
    }

    static bool HandleDefsCommand(ChatHandler* handler)
    {
        sWowFatesExpeditionMgr->ListDefinitions(handler);
        return true;
    }

    static bool HandleStartCommand(ChatHandler* handler, uint32 id, Optional<uint32> spawn)
    {
        if (!Enabled(handler))
            return true;

        if (sWowFatesExpeditionMgr->Start(id, spawn.value_or(0)))
            Say(handler, WOW_FATES_STR_CMD_STARTED, id);
        else
            Say(handler, WOW_FATES_STR_CMD_CANNOT_START, id);
        return true;
    }

    static bool HandleStopCommand(ChatHandler* handler, uint32 id)
    {
        if (sWowFatesExpeditionMgr->Stop(id))
            Say(handler, WOW_FATES_STR_CMD_STOPPED, id);
        else
            Say(handler, WOW_FATES_STR_CMD_NOT_ACTIVE, id);
        return true;
    }

    static bool HandleCompleteCommand(ChatHandler* handler, uint32 id)
    {
        if (sWowFatesExpeditionMgr->Complete(id))
            Say(handler, WOW_FATES_STR_CMD_COMPLETED, id);
        else
            Say(handler, WOW_FATES_STR_CMD_NOT_ACTIVE, id);
        return true;
    }

    static bool HandleReloadCommand(ChatHandler* handler)
    {
        sWowFatesExpeditionMgr->LoadFromDB();
        Say(handler, WOW_FATES_STR_CMD_RELOADED, sWowFatesExpeditionMgr->CountDefinitions(),
            sWowFatesExpeditionMgr->CountSpawns(), sWowFatesExpeditionMgr->CountRewards());
        return true;
    }

    static bool HandleSyncedCommand(ChatHandler* handler)
    {
        sWowFatesExpeditionMgr->ListSynced(handler);
        return true;
    }

    static bool HandleCheckCommand(ChatHandler* handler, Optional<uint32> id)
    {
        sWowFatesExpeditionMgr->Check(handler, id.value_or(0));
        return true;
    }

    static bool HandleSimulateCommand(ChatHandler* handler, Optional<uint32> id, Optional<uint32> players)
    {
        sWowFatesExpeditionMgr->Simulate(handler, id.value_or(0), players.value_or(0));
        return true;
    }

    // The score of a player (named, else the selected one, else you), without
    // the debuff, against the reference of a level: the one given, else his
    // Expedition's, else his own.
    static bool HandleScoreCommand(ChatHandler* handler, Optional<PlayerIdentifier> target, Optional<uint8> level)
    {
        Player* player = target ? target->GetConnectedPlayer()
            : (handler->GetSession() ? handler->getSelectedPlayerOrSelf() : nullptr);
        if (!player || !player->IsInWorld())
        {
            Say(handler, WOW_FATES_STR_CMD_NO_PLAYER);
            return true;
        }

        uint8 at = level.value_or(0);
        if (!at)
            at = sWowFatesExpeditionMgr->SyncLevel(player);
        if (!at)
            at = player->GetLevel();

        WowFatesScore const score = sWowFatesSync->MeasureUnsynced(player);
        float const reference = sWowFatesSync->Reference(player, at);
        float const ratio = score.total > 0.0f ? std::min(reference / score.total, 1.0f) : 1.0f;

        Say(handler, WOW_FATES_STR_CMD_SCORE, player->GetName(),
            uint32(score.total), uint32(score.primary), uint32(score.secondary), uint32(score.power),
            uint32(at), uint32(reference), ratio);
        return true;
    }
};

void AddSC_wow_fates_commands()
{
    new wow_fates_commandscript();
}
