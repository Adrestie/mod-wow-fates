# mod-wow-fates

**WORK IN PROGRESS**

An AzerothCore module (WoW 3.3.5a) that brings Expeditions to the open world: timed events that appear on the map where players are, open to everyone passing by.

## Key features

- Expeditions shown as areas on the world map and the minimap, with their level, objective and rewards.
- Defences, invocations, gatherings, assaults, escorts, rescues and more, each in the words of its own zone.
- A Mythic+ style tracker, in the game's tracker or in mod-forever-ui's.
- The Synced debuff brings stronger players down to the Expedition's level.
- Rewards by player level, shared by how much each one took part; hidden rewards.
- Ten example Expeditions; texts in English, French, German, Spanish and Russian.

## Dependencies

| Dependency | For |
|---|---|
| [AzerothCore](https://github.com/azerothcore/azerothcore-wotlk) (WotLK 3.3.5a) | the server |
| [mod-ale](https://github.com/azerothcore/mod-ale) | runs the map, tracker and windows |
| [AIO](https://github.com/Rochet2/AIO) | the map, tracker and windows: `AIO_Server` in `lua_scripts`, and the `AIO_Client` addon on every client |
| [mod-forever-ui](https://github.com/Adrestie/mod-forever-ui) | optional: its objective tracker shows the Expeditions |

## How to install

1. Download `installer.exe` from the [WoW-mods-installer releases](https://github.com/Adrestie/WoW-mods-installer/releases).
2. Stop the worldserver and close the game.
3. Run `installer.exe`, give it this folder and the folders it asks for, click **Check**, then **Install**.
4. Rebuild the worldserver. On its next start, the core applies the module's SQL.
5. The installer writes the game files into `Data\patch-Z.MPQ`: give that archive to the other players.

## How to update

Same steps with the new version's folder, choosing **Update** instead of **Install**. The Expeditions you wrote in the world database, and your `mod-wow-fates.conf` settings, are kept. Rebuild the worldserver.

## How to uninstall

Same steps, choosing **Remove**. Everything goes, database included. Rebuild the worldserver.

## Configuration

`mod-wow-fates.conf` sets:

- the module on or off;
- the Synced debuff: how strongly each stat is reduced, and how power is measured;
- the rotation: Expeditions per zone, idle time, spawn interval and cooldown, bots counted or not;
- where objects and paths may be placed;
- the scaling of enemies, waves and bosses with the number of players;
- the talk of the Expeditions;
- participation shares and rewards.

## Commands

| Command | |
|---|---|
| `.fates list` | game master: the active Expeditions |
| `.fates defs` | game master: every Expedition loaded |
| `.fates start <id> [spawn]` | game master: starts an Expedition |
| `.fates stop <id>` | game master: ends it without rewards |
| `.fates complete <id>` | game master: ends it as completed |
| `.fates reload` | game master: reloads the Expeditions |
| `.fates score [player] [level]` | game master: a player's power against a level's reference |
| `.fates synced` | game master: the players of an area and their Synced state |
