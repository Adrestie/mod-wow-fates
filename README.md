# mod-wow-fates

An AzerothCore module (WoW 3.3.5a) that brings **Expeditions** to the open
world: timed events that appear at chosen places of the map, open to every
player passing by, with a Mythic+ style tracker, level sync for the stronger
ones and rewards for those who take part.

## What are Expeditions

- **On the map.** Each active Expedition is an orange area on the world map and
  the minimap, drawn like the quest areas of the modern client. Hovering it
  shows its name, level, type, objective and the rewards it gives; "In
  progress: 37 %" while players are at it, "Expires soon" when it is about to
  go for lack of players. No clock is shown.
- **Where the players are.** Expeditions appear only in the zones (Elwynn
  Forest, Durotar...) where there are players: one at least, three at most,
  picked at random among the zone's. When the last one of a zone ends, another
  comes at once. One nobody takes up for fifteen minutes goes, and another of
  the zone takes its place; one players are at never goes by itself. An
  Expedition that ended waits a while before it may appear again.
- **A Mythic+ style tracker.** Entering an area shows a banner with a sound, and
  a block heads the objective tracker: the Expedition's name and level, the
  objective with a progress bar in percent, and the
  rewards the player can expect. It works with the game's own tracker and with
  mod-forever-ui's.
- **In the world's words.** Each Expedition speaks of its own enemies, places
  and objects ("Protect the Westbrook Sentinel against the Gnolls for 4
  minutes", "The Gnolls attack!"): its objective, its tracker line and the
  banner of its waves are its own texts, in five languages
  (`objective_text`, `tracker_text`, `wave_text`), with texts made from its
  type when they are empty.
- **Types of objectives.**
  - *Extermination*: kill monsters in the area.
  - *Assault and boss*: kill monsters until the bar reaches a set point (70 %
    by default), then their leader appears in the centre with a banner; its
    health is the rest of the bar and grows with the players in the area.
  - *Gathering*: pick up objects spread over the area, put back as they are
    taken, while monsters guard them; a click counts, nothing goes in the bags.
  - *Defence*: an ally in the centre must hold out for a few minutes against
    waves of monsters coming from two to four sides of the area, bigger each
    time, with each character there (bots too) and when the last wave was
    killed fast; the bar is the time held, and the Expedition fails if
    the ally dies. The clock starts with the first player in the area.
  - *Invocation*: players channel at an altar in the centre while waves of
    monsters come at them: a true channel, with its bar, the hands raised and
    a beam to the altar, broken by a blow, a move or leaving the altar. The
    bar fills while someone channels, faster with more channelers, and never
    goes back.
  - *Wave support*: allies in the centre push back a set number of waves, one
    after the other; the bar is the waves pushed back, and the Expedition
    fails when every ally is dead.
  - *Boss support*: a known figure of the zone's side (an Alliance hero in an
    Alliance zone, a Horde one in a Horde zone, a neutral order's in a
    contested one; friend of every player whatever his side) fights a unique
    elite; the bar is the health the elite lost, and the Expedition fails if
    the ally falls. The ally speaks now and then (said, in five languages).
  - *Escort*: an ally (a known figure of the zone) walks a route while
    players are near him, waits otherwise; his caravan (`caravan`: guards,
    beasts, wagons) in a pack around him, waiting for one that lags behind;
    ambushes at fixed marks of it.
    The bar is the route behind him; he dies, it fails. The route is the
    spawn point's `path`, each leg walked as a player would.
  - *Rescue*: cages to open (a one second cast) spread over the area, each
    walked to the way out (the last point of the spawn point's `path`);
    each captive runs for it. The bar is the captives led out; more than
    `max_losses` dead, it fails.
  - *Capture*: two or three of the foe's banners, far apart, to take (a
    one second cast) and hold against his squads, which take one back when
    they stand at its foot with no player near. The bar is the time they
    are held (`hold_time` holding them all); `time_limit` out first, it
    fails.
  - *Warlord*: a unique elite (always an elite, bigger) whose phases, at
    the marks of `boss_waves`, bring his words, reinforcements and one
    more of his blows (`boss_spells`). The bar is the health he lost;
    `time_limit` out first, it fails. The game's own figure he stands for
    (`objective_entry`) stays out of the area meanwhile.
  - *Sabotage*: enemy buildings or engines of worth to burn (war tents, huts,
    wagons, catapults), each with its guards, spread over the area and put
    back one at a time; a one second cast sets one alight, it goes up in
    flames and its guards come. No failure.
- **An objective that grows with the crowd.** Players see a percentage, never a
  count: the more players in the area, the more work each percent takes.
- **Fair fights.** A sabotage, its targets and its monsters are outdoors (a
  point found under a roof is never used again); the other Expeditions may be
  in caves and buildings too. Monsters and objects come back one or two at a time, never on a player;
  no more of them come at the players at once than they can take (three for
  one player, two more for each other one), the others wait.
- **Monsters that keep up.** An Expedition brings its own monsters. The faster
  the area is cleared, the faster and the more of them come back, and the area
  is never left empty.
- **What you won.** When an Expedition is completed, a window at the top of the
  screen shows what the player earned (items with their icons, money,
  experience) for five seconds.
- **Quiet.** The module writes nothing in the chat; banners, the tracker and the
  end window say everything.

## Synced

A level 80 should not breeze through a level 7 Expedition. Whoever stands in an
Expedition's area while above its level, or stronger than a character of that
level would be, gets the **Synced** debuff (its icon shows the level):

- the player's power is measured live from his stats, ratings and attack or
  spell power, so gear, buffs and the bonuses of other modules all count;
- stats, ratings and armor are cut to match a character of the Expedition's
  level;
- maximum health and mana are brought to those of a character of his race and
  class at that level;
- damage and healing done are cut by the difference of level.

The debuff's tooltip gives the level and the share of power kept. It comes off
when the player leaves the area or the Expedition ends.

## Rewards

Rewards are set by level of the player: money, experience and items, from rows
that apply to every Expedition and rows of a given Expedition. A player is
rewarded only if he took an active part: standing in the area earns nothing,
and the more he did compared to the others, the more of the rewards he gets
(alone, a player gets them all). Some rewards can be **hidden**: they show as a
red question mark ("???") on the map and in the tracker until they are won.

Items go to the bags, or by mail when the bags are full.

## Requirements

- AzerothCore 3.3.5a with this module compiled in.
- ALE (Lua engine) with AIO: the client code of the map, the tracker and the
  windows is sent by AIO.
- A client patch, written by the WoW-mods installer into `patch-Z.MPQ`: the
  Synced debuff (`Spell.dbc`) and the textures of the areas and the tracker
  (`Interface\WowFates\`). mod-forever-ui is optional.

## Installation

With the [WoW-mods installer](https://github.com/Adrestie/WoW-mods-installer):
it copies the module into the server's sources, its configuration and Lua
scripts, and writes the client patch. Then rebuild the worldserver (reconfigure
CMake first) and start it: the module's SQL is applied by the core updater.

By hand: copy the module into `modules/`, copy `data/lua/WowFates` into the
ALE scripts folder, copy `conf/mod-wow-fates.conf.dist` to `configs/modules/`
as `mod-wow-fates.conf`, rebuild, and add the client patch with the installer.

Ten example Expeditions come with it: Kobold Uprising (Elwynn Forest,
level 7), Razormane Raid (Durotar, level 8), Restless Dead (Icecrown,
level 80), Defias Assault (Elwynn Forest, level 10, assault and boss: the
Defias Ringleader), Kul Tiras Supplies (Durotar, level 7, gathering: supply
crates by Tiragarde Keep), Riverpaw Raid (Elwynn Forest, level 10, defence:
the Westbrook Sentinel against Riverpaw Outrunners), Harpy Ritual (Durotar,
level 8, invocation: the Ritual Altar against Dustwind Pillagers) and
Quilboar Onslaught (Durotar, level 9, wave support: three Razor Hill Veterans
against five waves of Razormane Battleguards) and The Packleader's End
(Elwynn Forest, level 10, boss support: Marshal Dughan against Grimmaw the
Packleader) and The Kolkar War Camp (Durotar, level 8, sabotage: the war
tents of the Kolkar centaurs, two Outrunners guarding each). Expeditions, spawn points and rewards live in the world
database (`mod_wow_fates_expedition`, `mod_wow_fates_expedition_locale`,
`mod_wow_fates_spawn`, `mod_wow_fates_reward`), and the settings of the
objectives other than kills in `mod_wow_fates_objective` (for an assault, the
leader's creature and the percent at which it appears; for a gathering, the
gameobject, how many are out at once and how soon one comes back, and for a
sabotage the guards of each object in `wave_size`; for a
defence, the ally, how long to hold out and the waves; for an invocation, the
altar and the channel asked; for a wave support, the allies and the number of
waves); give your own ids 100 and up. A defence's ally takes faction 495, friend of every player and foe of
the monsters, and its waves are the Expedition's `summon_entry`. A gathering's gameobject must have the ScriptName
`go_wow_fates_gather`, an invocation's altar `go_wow_fates_channel`.

## Configuration

`conf/mod-wow-fates.conf.dist` documents every setting. The main ones:

| Setting | Default | |
|---|---|---|
| `WowFates.Enable` | 1 | the whole module |
| `WowFates.Rotation.MaxPerZone` | 3 | Expeditions at once in a zone with players |
| `WowFates.Rotation.CountBots` | 0 | whether bots make a zone "with players" (they may take part either way) |
| `WowFates.Rotation.IdleExpire` | 900 | seconds without activity before an Expedition goes (never while players are at it) |
| `WowFates.Expedition.Cooldown` | 600 | seconds before an Expedition may come back |
| `WowFates.Waves.PerCharacter` | 1.0 | monsters of a wave per character in the area, times wave_size |
| `WowFates.Waves.PaceFast` / `PaceStep` / `PaceMax` | 0.5 / 0.25 / 2.0 | a wave killed within half the interval makes the next ones a quarter bigger, twice at most |
| `WowFates.Expedition.OutdoorTypes` | "8" | objective types kept out from under a roof (8 = sabotage) |
| `WowFates.Engage.Base` / `PerPlayer` | 3 / 2 | the Expedition's monsters at the players at once; the others wait |
| `WowFates.Spawn.PlayerDistance` | 20 | yards: nothing of an Expedition comes this near a player |
| `WowFates.Object.Reach` | 0.6 | a gathering's or sabotage's objects within this share of the radius |
| `WowFates.Object.Spacing` / `MinSpacing` | 1.5 / 6 | objects apart by their footprints, and from the world's own |
| `WowFates.Object.MaxHeight` / `MaxSlope` | 6 / 0.5 | objects on flat ground about the centre's level |
| `WowFates.Object.PathFactor` | 2.0 | objects reached on foot by a sensible walk |
| `WowFates.Path.MaxSlope` | 1.0 | no path with a jump or a cliff |
| `WowFates.Expedition.ParticipantScaling` | 0.6 | how much the objective grows per player |
| `WowFates.Expedition.AdaptiveSpawn` | 1 | monsters come back faster when the area is cleared fast |
| `WowFates.Expedition.MinRespawn` | 2 | shortest delay between two waves, in seconds |
| `WowFates.Expedition.MaxPopulation` | 3.0 | most monsters at once, times the Expedition's number |
| `WowFates.Expedition.MinAlivePct` | 50 | under this share alive, the area is filled up at once |
| `WowFates.Synced.Enable` | 1 | the Synced debuff |
| `WowFates.Synced.Interval` | 5 | seconds between two measures of a synced player |
| `WowFates.Synced.PoolBonus` | 1.15 | health and mana kept, times those of a character of the level |
| `WowFates.Synced.DamageLevelExponent` | 1.0 | how hard the level difference cuts damage |
| `WowFates.Reference.Curve` | 1:100 … 80:3400 | the power of a geared character, by level |
| `WowFates.Reward.LightPct` / `MediumPct` | 33 / 66 | share of the rewards for a smaller part |
| `WowFates.Reward.LevelMargin` | -1 | caps the reward level above the Expedition's (-1: off) |

`.reload config` applies them.

## GM commands

| Command | |
|---|---|
| `.fates list` | the active Expeditions: where, progress, time left, players inside |
| `.fates defs` | every Expedition loaded, ready or cooling down |
| `.fates start $id [$spawn]` | starts an Expedition now |
| `.fates stop $id` | ends it without rewards |
| `.fates complete $id` | ends it as completed, rewards given |
| `.fates reload` | reloads the Expeditions from the database |
| `.fates score [$player] [$level]` | a player's power against the reference of a level |
| `.fates synced` | the players in an area and their Synced state |

## Localization

Every text the players see is in English, French, German, Spanish and Russian.

## Identifiers

The module uses the blocks **96000-96399** of the
[WoW-mods registry](https://github.com/Adrestie/WoW-mods/blob/main/ID_RANGES.md):
spells 96000-96003 are the Synced debuff and 96004 the channel of the
invocations; creatures 96100-96199 are the leaders of the assaults,
96200-96299 the allies of the defences and wave supports, 96300 the focus of
the altars; gameobjects 960000-960999 are the objects of the gatherings and
the altars of the invocations.

## License

GNU General Public License v2 or later.
