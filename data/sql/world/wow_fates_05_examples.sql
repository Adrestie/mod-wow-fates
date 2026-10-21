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
-- Their texts are in the world's words: the enemies, the places and the
-- objects by their names (objective_text, tracker_text, wave_text).
--
-- Idempotent: the module's rows are deleted before being written again.

DELETE FROM `mod_wow_fates_expedition` WHERE `id` BETWEEN 1 AND 99;
INSERT INTO `mod_wow_fates_expedition` (`id`, `name`, `level`, `objective_type`, `objective_entry`, `objective_count`, `summon_entry`, `summon_count`, `summon_respawn`, `radius`, `duration`, `cooldown`, `weight`, `enabled`, `comment`, `objective_text`, `tracker_text`, `wave_text`) VALUES
(1, 'Kobold Uprising',  7, 1,    40, 15,    40, 6, 15, 45,   0, 0, 1, 1, 'Elwynn Forest: Kobold Miner',
   'The kobolds of the mine are rising: slay the Kobold Miners.', 'Kobold Miners slain', ''),
(2, 'Razormane Raid',   8, 1,  3111, 15,  3111, 6, 15, 50,   0, 0, 1, 1, 'Durotar: Razormane Quilboar',
   'The Razormane are raiding near Razor Hill: drive off the Razormane Quilboars.', 'Razormane Quilboars driven off', ''),
(3, 'Restless Dead',   80, 1, 31812, 25, 31812, 8, 10, 50,   0, 0, 1, 1, 'Icecrown: Decomposed Ghoul',
   'The Scourge''s dead roam Icecrown: put the Decomposed Ghouls back in the ground.', 'Decomposed Ghouls destroyed', ''),
(4, 'Defias Assault',  10, 2,   116, 12,   116, 6, 15, 45,   0, 0, 1, 1, 'Elwynn Forest: Defias Bandit, then the Defias Ringleader',
   'Defias bandits have taken the fields: cut them down until their Ringleader comes out, then bring him down.', 'Defias Bandits cut down', ''),
(5, 'Kul Tiras Supplies', 7, 3,   0, 12,  3129, 5, 20, 40,   0, 0, 1, 1, 'Durotar: Kul Tiras Supply Crate, guarded by Kul Tiras Marines',
   'Kul Tiras supply crates lie scattered by Tiragarde Keep: carry them off under the Marines'' noses.', 'Supply crates carried off', ''),
(6, 'Riverpaw Raid',   10, 4,     0,  1,   478, 0, 15, 40,   0, 0, 1, 1, 'Elwynn Forest: the Westbrook Sentinel against waves of Riverpaw Outrunners',
   'Protect the Westbrook Sentinel against the Gnolls for 4 minutes.', 'Hold out against the Gnolls', 'The Gnolls attack!'),
(7, 'Harpy Ritual',     8, 5,     0,  1,  3116, 0, 15, 40,   0, 0, 1, 1, 'Durotar: channel at the Ritual Altar against waves of Dustwind Pillagers',
   'Complete the ritual at the altar while the Dustwind harpies try to stop you.', 'Ritual completed', 'The harpies swoop in!'),
(8, 'Quilboar Onslaught', 9, 6,   0,  1,  3114, 0, 15, 40,   0, 0, 1, 1, 'Durotar: three Razor Hill Veterans against five waves of Razormane Battleguards',
   'Help the Razor Hill Veterans push back five Razormane assaults.', 'Push back the Razormane assaults', 'The Razormane charge!');

DELETE FROM `mod_wow_fates_objective` WHERE `expedition` BETWEEN 1 AND 99;
INSERT INTO `mod_wow_fates_objective` (`expedition`, `boss_entry`, `boss_at`, `gather_entry`, `gather_count`, `gather_respawn`,
  `ally_entry`, `hold_time`, `wave_interval`, `wave_size`, `wave_growth`, `channel_entry`, `ally_count`, `wave_count`, `comment`) VALUES
(4, 96100, 70,      0, 8, 5,     0, 240, 40, 3, 1,      0, 1, 5, 'Defias Assault: the Defias Ringleader at 70 %'),
(5,     0, 70, 960000, 8, 5,     0, 240, 40, 3, 1,      0, 1, 5, 'Kul Tiras Supplies: 8 crates out, one back 5 s after it is taken'),
(6,     0, 70,      0, 8, 5, 96200, 240, 40, 2, 1,      0, 1, 5, 'Riverpaw Raid: four minutes, a wave every 40 s, 2 then 3, 4... Outrunners'),
(7,     0, 70,      0, 8, 5,     0, 120, 30, 2, 1, 960100, 1, 5, 'Harpy Ritual: two minutes of channel for one player, a wave every 30 s'),
(8,     0, 70,      0, 8, 5, 96201, 240, 15, 3, 1,      0, 3, 5, 'Quilboar Onslaught: three Veterans, five waves of 3, 4... Battleguards, 15 s apart');

DELETE FROM `mod_wow_fates_expedition_locale` WHERE `id` BETWEEN 1 AND 99;
INSERT INTO `mod_wow_fates_expedition_locale` (`id`, `locale`, `name`, `objective_text`, `tracker_text`, `wave_text`) VALUES
(1, 'frFR', 'Soulèvement kobold',
   'Les kobolds de la mine se soulèvent : éliminez les mineurs kobolds.', 'Mineurs kobolds éliminés', ''),
(1, 'deDE', 'Koboldaufstand',
   'Die Kobolde der Mine erheben sich: Tötet die Koboldminenarbeiter.', 'Getötete Koboldminenarbeiter', ''),
(1, 'esES', 'Levantamiento kóbold',
   'Los kóbolds de la mina se alzan: acaba con los mineros kóbold.', 'Mineros kóbold abatidos', ''),
(1, 'esMX', 'Levantamiento kóbold',
   'Los kóbolds de la mina se alzan: acaba con los mineros kóbold.', 'Mineros kóbold abatidos', ''),
(1, 'ruRU', 'Восстание кобольдов',
   'Кобольды в шахте восстали: перебейте кобольдов-шахтеров.', 'Убито кобольдов-шахтеров', ''),
(2, 'frFR', 'Raid tranchecrin',
   'Les Tranchecrins pillent les environs de Razor Hill : repoussez les hurans Tranchecrins.', 'Hurans Tranchecrins repoussés', ''),
(2, 'deDE', 'Raubzug der Klingenmähnen',
   'Die Klingenmähnen plündern bei Klingenhügel: Vertreibt die Stacheleber der Klingenmähnen.', 'Vertriebene Stacheleber der Klingenmähnen', ''),
(2, 'esES', 'Incursión Crines de Acero',
   'Los Crines de Acero saquean los alrededores de Cerrotajo: expulsa a los jabaespines Crines de Acero.', 'Jabaespines Crines de Acero expulsados', ''),
(2, 'esMX', 'Incursión Crines de Acero',
   'Los Crines de Acero saquean los alrededores de Cerrotajo: expulsa a los jabaespines Crines de Acero.', 'Jabaespines Crines de Acero expulsados', ''),
(2, 'ruRU', 'Набег Иглогривых',
   'Иглогривые грабят окрестности Колючего Холма: прогоните свинобразов из племени Иглогривых.', 'Изгнано свинобразов Иглогривых', ''),
(3, 'frFR', 'Morts sans repos',
   'Les morts du Fléau errent dans la Couronne de glace : renvoyez les goules décomposées sous terre.', 'Goules décomposées détruites', ''),
(3, 'deDE', 'Ruhelose Tote',
   'Die Toten der Geißel streifen durch Eiskrone: Schickt die verrotteten Ghule zurück in die Erde.', 'Zerstörte verrottete Ghule', ''),
(3, 'esES', 'Muertos sin descanso',
   'Los muertos de la Plaga vagan por Corona de Hielo: devuelve a los necrófagos descompuestos a la tierra.', 'Necrófagos descompuestos destruidos', ''),
(3, 'esMX', 'Muertos sin descanso',
   'Los muertos de la Plaga vagan por Corona de Hielo: devuelve a los necrófagos descompuestos a la tierra.', 'Necrófagos descompuestos destruidos', ''),
(3, 'ruRU', 'Неупокоенные мертвецы',
   'Мертвецы Плети бродят по Ледяной Короне: верните изгнивших вурдалаков в землю.', 'Уничтожено изгнивших вурдалаков', ''),
(4, 'frFR', 'Assaut défias',
   'Des bandits défias ont pris les champs : abattez-les jusqu''à ce que leur meneur se montre, puis éliminez-le.', 'Bandits défias abattus', ''),
(4, 'deDE', 'Angriff der Defias',
   'Banditen der Defias haben die Felder besetzt: Streckt sie nieder, bis ihr Rädelsführer erscheint, und bezwingt ihn dann.', 'Niedergestreckte Banditen der Defias', ''),
(4, 'esES', 'Asalto Defias',
   'Los bandidos Defias han tomado los campos: abátelos hasta que aparezca su cabecilla y acaba con él.', 'Bandidos Defias abatidos', ''),
(4, 'esMX', 'Asalto Defias',
   'Los bandidos Defias han tomado los campos: abátelos hasta que aparezca su cabecilla y acaba con él.', 'Bandidos Defias abatidos', ''),
(4, 'ruRU', 'Штурм Братства Справедливости',
   'Бандиты из Братства Справедливости захватили поля: перебейте их, пока не появится их главарь, а затем сразите его.', 'Сражено бандитов', ''),
(5, 'frFR', 'Ravitaillement de Kul Tiras',
   'Des caisses de ravitaillement de Kul Tiras traînent près du fort de Tiragarde : emportez-les sous le nez des soldats de marine.', 'Caisses de ravitaillement emportées', ''),
(5, 'deDE', 'Nachschub von Kul Tiras',
   'Nachschubkisten von Kul Tiras liegen bei Burg Tiragarde verstreut: Schafft sie den Marinesoldaten vor der Nase weg.', 'Weggeschaffte Nachschubkisten', ''),
(5, 'esES', 'Suministros de Kul Tiras',
   'Hay cajas de suministros de Kul Tiras esparcidas junto al Fuerte de Tiragarde: llévatelas delante de las narices de los marineros.', 'Cajas de suministros robadas', ''),
(5, 'esMX', 'Suministros de Kul Tiras',
   'Hay cajas de suministros de Kul Tiras esparcidas junto al Fuerte de Tiragarde: llévatelas delante de las narices de los marineros.', 'Cajas de suministros robadas', ''),
(5, 'ruRU', 'Припасы Кул-Тираса',
   'У крепости Тирагард разбросаны ящики с припасами Кул-Тираса: унесите их прямо из-под носа у стражи.', 'Унесено ящиков с припасами', ''),
(6, 'frFR', 'Raid Rivepatte',
   'Protégez la sentinelle de Westbrook contre les gnolls pendant 4 minutes.', 'Tenez bon face aux gnolls', 'Les gnolls attaquent !'),
(6, 'deDE', 'Überfall der Flusspfoten',
   'Beschützt den Wachposten von Westbrook 4 Minuten lang vor den Gnollen.', 'Haltet den Gnollen stand', 'Die Gnolle greifen an!'),
(6, 'esES', 'Incursión Zarparrío',
   'Protege al centinela de Westbrook de los gnolls durante 4 minutos.', 'Resiste a los gnolls', '¡Los gnolls atacan!'),
(6, 'esMX', 'Incursión Zarparrío',
   'Protege al centinela de Westbrook de los gnolls durante 4 minutos.', 'Resiste a los gnolls', '¡Los gnolls atacan!'),
(6, 'ruRU', 'Набег Речных Лап',
   'Защищайте часового Вестбрука от гноллов в течение 4 минут.', 'Сдерживайте гноллов', 'Гноллы нападают!'),
(7, 'frFR', 'Rituel des harpies',
   'Accomplissez le rituel à l''autel pendant que les harpies Terrevent tentent de vous en empêcher.', 'Rituel accompli', 'Les harpies fondent sur vous !'),
(7, 'deDE', 'Ritual der Harpyien',
   'Vollendet das Ritual am Altar, während die Harpyien der Staubschwingen euch aufhalten wollen.', 'Ritual vollendet', 'Die Harpyien stoßen herab!'),
(7, 'esES', 'Ritual de las arpías',
   'Completa el ritual en el altar mientras las arpías Viento Seco intentan detenerte.', 'Ritual completado', '¡Las arpías se abalanzan!'),
(7, 'esMX', 'Ritual de las arpías',
   'Completa el ritual en el altar mientras las arpías Viento Seco intentan detenerte.', 'Ritual completado', '¡Las arpías se abalanzan!'),
(7, 'ruRU', 'Ритуал гарпий',
   'Завершите ритуал у алтаря, пока гарпии Пыльного Ветра пытаются вам помешать.', 'Ритуал выполнен', 'Гарпии пикируют!'),
(8, 'frFR', 'Déferlement tranchecrin',
   'Aidez les vétérans de Razor Hill à repousser cinq assauts des Tranchecrins.', 'Repoussez les assauts des Tranchecrins', 'Les Tranchecrins chargent !'),
(8, 'deDE', 'Ansturm der Klingenmähnen',
   'Helft den Veteranen von Klingenhügel, fünf Angriffe der Klingenmähnen zurückzuschlagen.', 'Schlagt die Angriffe der Klingenmähnen zurück', 'Die Klingenmähnen stürmen heran!'),
(8, 'esES', 'Embestida Crines de Acero',
   'Ayuda a los veteranos de Cerrotajo a rechazar cinco asaltos de los Crines de Acero.', 'Rechaza los asaltos de los Crines de Acero', '¡Cargan los Crines de Acero!'),
(8, 'esMX', 'Embestida Crines de Acero',
   'Ayuda a los veteranos de Cerrotajo a rechazar cinco asaltos de los Crines de Acero.', 'Rechaza los asaltos de los Crines de Acero', '¡Cargan los Crines de Acero!'),
(8, 'ruRU', 'Натиск Иглогривых',
   'Помогите ветеранам Колючего Холма отбить пять атак Иглогривых.', 'Отбейте атаки Иглогривых', 'Иглогривые идут в атаку!');

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
