-- mod-wow-fates — eight example Expeditions, with the rewards every
-- Expedition gives by default. Ids 1 to 99 of the module's tables are the
-- module's own: give yours 100 and up, an update writes these rows again.
--
--   1  Kobold Uprising, Elwynn Forest, level 7: Fargodeep or Jasperlode Mine
--   2  Razormane Raid, Durotar, level 8: the Razormane grounds by Razor Hill
--   3  Restless Dead, Icecrown, level 80: the ghouls of central-east Icecrown (64, 45)
--   4  Defias Assault, Elwynn Forest, level 10, assault and boss: the Defias
--      Bandits of the Brackwell Pumpkin Patch, then their leader, the Defias
--      Ringleader (creature 96100, wow_fates_06_creatures.sql), at 70 %
--   5  Kul Tiras Supplies, Durotar, level 7, gathering: the supply crates
--      by Tiragarde Keep (gameobject 960000, wow_fates_07_gameobjects.sql),
--      guarded by Kul Tiras Marines
--   6  Riverpaw Raid, Elwynn Forest, level 10, defence: the Westbrook
--      Sentinel (creature 96200) holds out four minutes against waves of
--      Riverpaw Outrunners, south of Westbrook Garrison
--   7  Harpy Ritual, Durotar, level 8, invocation: channel at the Ritual
--      Altar (gameobject 960100) while Dustwind Pillagers come in waves
--   8  Quilboar Onslaught, Durotar, level 9, wave support: three Razor Hill
--      Veterans (creature 96201) push back five waves of Razormane
--      Battleguards
--
-- Each brings its own monsters, kept at a number alive, and counts every one
-- of that kind killed in its area, those of the game included.
--
-- Rewards: rows of expedition 0 go to every Expedition, by the player's level;
-- an Expedition's own rows come on top. Kobold Uprising adds a hidden one, a
-- bag shown as a red "?" until given (a single item: strong participation only).
--
-- Idempotent: the module's rows are deleted before being written again.

DELETE FROM `mod_wow_fates_expedition` WHERE `id` BETWEEN 1 AND 99;
INSERT INTO `mod_wow_fates_expedition` (`id`, `name`, `level`, `objective_type`, `objective_entry`, `objective_count`, `summon_entry`, `summon_count`, `summon_respawn`, `radius`, `duration`, `cooldown`, `weight`, `enabled`, `comment`) VALUES
(1, 'Kobold Uprising',  7, 1,    40, 15,    40, 6, 15, 45, 900, 0, 1, 1, 'Elwynn Forest: Kobold Miner'),
(2, 'Razormane Raid',   8, 1,  3111, 15,  3111, 6, 15, 50, 900, 0, 1, 1, 'Durotar: Razormane Quilboar'),
(3, 'Restless Dead',   80, 1, 31812, 25, 31812, 8, 10, 50, 900, 0, 1, 1, 'Icecrown: Decomposed Ghoul'),
(4, 'Defias Assault',  10, 2,   116, 12,   116, 6, 15, 45, 900, 0, 1, 1, 'Elwynn Forest: Defias Bandit, then the Defias Ringleader'),
(5, 'Kul Tiras Supplies', 7, 3,   0, 12,  3129, 5, 20, 40, 900, 0, 1, 1, 'Durotar: Kul Tiras Supply Crate, guarded by Kul Tiras Marines'),
(6, 'Riverpaw Raid',   10, 4,     0,  1,   478, 0, 15, 40, 900, 0, 1, 1, 'Elwynn Forest: the Westbrook Sentinel against waves of Riverpaw Outrunners'),
(7, 'Harpy Ritual',     8, 5,     0,  1,  3116, 0, 15, 40, 900, 0, 1, 1, 'Durotar: channel at the Ritual Altar against waves of Dustwind Pillagers'),
(8, 'Quilboar Onslaught', 9, 6,   0,  1,  3114, 0, 15, 40, 900, 0, 1, 1, 'Durotar: three Razor Hill Veterans against five waves of Razormane Battleguards');

DELETE FROM `mod_wow_fates_objective` WHERE `expedition` BETWEEN 1 AND 99;
INSERT INTO `mod_wow_fates_objective` (`expedition`, `boss_entry`, `boss_at`, `gather_entry`, `gather_count`, `gather_respawn`,
  `ally_entry`, `hold_time`, `wave_interval`, `wave_size`, `wave_growth`, `channel_entry`, `ally_count`, `wave_count`, `comment`) VALUES
(4, 96100, 70,      0, 8, 5,     0, 240, 40, 3, 1,      0, 1, 5, 'Defias Assault: the Defias Ringleader at 70 %'),
(5,     0, 70, 960000, 8, 5,     0, 240, 40, 3, 1,      0, 1, 5, 'Kul Tiras Supplies: 8 crates out, one back 5 s after it is taken'),
(6,     0, 70,      0, 8, 5, 96200, 240, 40, 2, 1,      0, 1, 5, 'Riverpaw Raid: four minutes, a wave every 40 s, 2 then 3, 4... Outrunners'),
(7,     0, 70,      0, 8, 5,     0, 120, 30, 2, 1, 960100, 1, 5, 'Harpy Ritual: two minutes of channel for one player, a wave every 30 s'),
(8,     0, 70,      0, 8, 5, 96201, 240, 15, 3, 1,      0, 3, 5, 'Quilboar Onslaught: three Veterans, five waves of 3, 4... Battleguards, 15 s apart');

DELETE FROM `mod_wow_fates_expedition_locale` WHERE `id` BETWEEN 1 AND 99;
INSERT INTO `mod_wow_fates_expedition_locale` (`id`, `locale`, `name`) VALUES
(1, 'frFR', 'Soulèvement kobold'),
(1, 'deDE', 'Koboldaufstand'),
(1, 'esES', 'Levantamiento kóbold'),
(1, 'esMX', 'Levantamiento kóbold'),
(1, 'ruRU', 'Восстание кобольдов'),
(2, 'frFR', 'Raid tranchecrin'),
(2, 'deDE', 'Raubzug der Klingenmähnen'),
(2, 'esES', 'Incursión Crines de Acero'),
(2, 'esMX', 'Incursión Crines de Acero'),
(2, 'ruRU', 'Набег Иглогривых'),
(3, 'frFR', 'Morts sans repos'),
(3, 'deDE', 'Ruhelose Tote'),
(3, 'esES', 'Muertos sin descanso'),
(3, 'esMX', 'Muertos sin descanso'),
(3, 'ruRU', 'Неупокоенные мертвецы'),
(4, 'frFR', 'Assaut défias'),
(4, 'deDE', 'Angriff der Defias'),
(4, 'esES', 'Asalto Defias'),
(4, 'esMX', 'Asalto Defias'),
(4, 'ruRU', 'Штурм Братства Справедливости'),
(5, 'frFR', 'Ravitaillement de Kul Tiras'),
(5, 'deDE', 'Nachschub von Kul Tiras'),
(5, 'esES', 'Suministros de Kul Tiras'),
(5, 'esMX', 'Suministros de Kul Tiras'),
(5, 'ruRU', 'Припасы Кул-Тираса'),
(6, 'frFR', 'Raid Rivepatte'),
(6, 'deDE', 'Überfall der Flusspfoten'),
(6, 'esES', 'Incursión Zarparrío'),
(6, 'esMX', 'Incursión Zarparrío'),
(6, 'ruRU', 'Набег Речных Лап'),
(7, 'frFR', 'Rituel des harpies'),
(7, 'deDE', 'Ritual der Harpyien'),
(7, 'esES', 'Ritual de las arpías'),
(7, 'esMX', 'Ritual de las arpías'),
(7, 'ruRU', 'Ритуал гарпий'),
(8, 'frFR', 'Déferlement tranchecrin'),
(8, 'deDE', 'Ansturm der Klingenmähnen'),
(8, 'esES', 'Embestida Crines de Acero'),
(8, 'esMX', 'Embestida Crines de Acero'),
(8, 'ruRU', 'Натиск Иглогривых');

DELETE FROM `mod_wow_fates_spawn` WHERE `id` BETWEEN 1 AND 99;
INSERT INTO `mod_wow_fates_spawn` (`id`, `expedition`, `map`, `x`, `y`, `z`, `o`, `comment`) VALUES
(1, 1,   0, -9797.38,   131.632,  24.5919, 0, 'Fargodeep Mine, outside'),
(2, 1,   0, -9256.03,  -707.889,  63.1122, 0, 'Jasperlode Mine, outside'),
(3, 2,   1,  92.7794, -4557.56,   54.6971, 0, 'Razormane grounds, south'),
(4, 2,   1,  80.3499, -4406.12,   43.5863, 0, 'Razormane grounds, north'),
(5, 3, 571,  7547.05,  1417.36,  324.996,  0, 'Icecrown, central-east (map 64, 45)'),
(6, 4,   0, -9776.18, -1574.37,   41.901,  0, 'Brackwell Pumpkin Patch'),
(7, 4,   0, -9738.45,  -834.382,  40.3696, 0, 'Defias Bandit camp, southern Elwynn'),
(8, 5,   1,  -119.671, -4988.08,   19.4948, 0, 'Tiragarde Keep, the beach east of the keep'),
(9, 5,   1,  -209.532, -4982.4,    21.5041, 0, 'Tiragarde Keep, north of the keep'),
(10, 6,  0, -9930.0,    499.7,    32.5,    0, 'Elwynn Forest, south of Westbrook Garrison'),
(11, 6,  0, -9881.6,    418.2,    34.9,    0, 'Elwynn Forest, the fields by the Riverpaw camp'),
(12, 7,  1,   793.9,  -4849.5,    38.0,    0, 'Durotar, south of Drygulch Ravine'),
(13, 7,  1,   641.3,  -4664.4,    22.1,    0, 'Durotar, west of Drygulch Ravine'),
(14, 8,  1,   298.0,  -4269.0,    31.1,    0, 'Durotar, east of the Razormane grounds'),
(15, 8,  1,   -29.5,  -4265.1,    66.6,    0, 'Durotar, north of the Razormane grounds');

DELETE FROM `mod_wow_fates_reward` WHERE `id` BETWEEN 1 AND 99;
INSERT INTO `mod_wow_fates_reward` (`id`, `expedition`, `min_level`, `max_level`, `money`, `xp`, `item`, `item_count`, `hidden`, `comment`) VALUES
(1, 0,  1,  19,    500,   400,   858, 2, 0, 'Lesser Healing Potion'),
(2, 0, 20,  39,   3000,  2500,   929, 3, 0, 'Healing Potion'),
(3, 0, 40,  59,  10000,  9000,  1710, 3, 0, 'Greater Healing Potion'),
(4, 0, 60,  69,  30000, 25000,  1710, 5, 0, 'Greater Healing Potion'),
(5, 0, 70,  79,  60000, 60000, 33447, 3, 0, 'Runic Healing Potion'),
(6, 0, 80, 255, 150000,     0, 47241, 1, 0, 'Emblem of Triumph'),
(7, 1,  1, 255,      0,     0,  4496, 1, 1, 'Kobold Uprising, hidden: Small Brown Pouch');
