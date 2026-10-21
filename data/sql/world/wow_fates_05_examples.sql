-- mod-wow-fates — ten example Expeditions, with the rewards every
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
--   9  The Packleader's End, Elwynn Forest, level 10, boss support: Marshal
--      Dughan (creature 96202) against Grimmaw the Packleader (96101), with
--      a few Riverpaw Outrunners about
--  10  The Kolkar War Camp, Durotar, level 8, sabotage: the war tents of the
--      Kolkar centaurs (gameobject 960001), two Outrunners guarding each,
--      out in the open of their camp
--  11  Remy's Wares, Elwynn Forest, level 6, escort: Remy "Two Times"
--      (creature 96203) takes his wares up the hill road north-east of
--      Goldshire, Defias Cutpurses ambushing him at 25, 50 and 75 % of it
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
   'Help the Razor Hill Veterans push back five Razormane assaults.', 'Push back the Razormane assaults', 'The Razormane charge!'),
(9, 'The Packleader''s End', 10, 7,   0,  1,   478, 3, 20, 35,   0, 0, 1, 1, 'Elwynn Forest: Marshal Dughan against Grimmaw the Packleader',
   'Marshal Dughan has tracked down Grimmaw, the Riverpaw packleader: help the marshal bring the gnoll down.', 'Help Marshal Dughan defeat Grimmaw', 'Grimmaw calls the pack!'),
(10, 'The Kolkar War Camp', 8, 8,   0,  8,  3120, 0, 20, 40,   0, 0, 1, 1, 'Durotar: the Kolkar war tents, guarded by Outrunners',
   'The Kolkar centaurs are massing in their camp: burn their war tents before they ride on Razor Hill.', 'Burn the Kolkar war tents', ''),
(11, 'Remy''s Wares', 6, 9,   0,  1,    94, 0, 20, 110,  0, 0, 1, 1, 'Elwynn Forest: Remy "Two Times" up the hill road, Defias Cutpurses lying in wait',
   'Remy "Two Times" is taking his wares up the hill road, where Defias Cutpurses lie in wait: escort him to the end of the road.', 'Escort Remy "Two Times"', 'Defias Cutpurses spring an ambush!');

DELETE FROM `mod_wow_fates_objective` WHERE `expedition` BETWEEN 1 AND 99;
INSERT INTO `mod_wow_fates_objective` (`expedition`, `boss_entry`, `boss_at`, `gather_entry`, `gather_count`, `gather_respawn`,
  `ally_entry`, `hold_time`, `wave_interval`, `wave_size`, `wave_growth`, `channel_entry`, `ally_count`, `wave_count`, `boss_waves`, `comment`) VALUES
(4, 96100, 70,      0, 8, 5,     0, 240, 40, 3, 1,      0, 1, 5, '25,50,75', 'Defias Assault: the Defias Ringleader at 70 %'),
(5,     0, 70, 960000, 8, 5,     0, 240, 40, 3, 1,      0, 1, 5, '25,50,75', 'Kul Tiras Supplies: 8 crates out, one back 5 s after it is taken'),
(6,     0, 70,      0, 8, 5, 96200, 240, 40, 2, 1,      0, 1, 5, '25,50,75', 'Riverpaw Raid: four minutes, a wave every 40 s, 2 then 3, 4... Outrunners'),
(7,     0, 70,      0, 8, 5,     0, 120, 30, 2, 1, 960100, 1, 5, '25,50,75', 'Harpy Ritual: two minutes of channel for one player, a wave every 30 s'),
(8,     0, 70,      0, 8, 5, 96201, 240, 15, 3, 1,      0, 3, 5, '25,50,75', 'Quilboar Onslaught: three Veterans, five waves of 3, 4... Battleguards, 15 s apart'),
(9, 96101, 70,      0, 8, 5, 96202, 240, 40, 3, 1,      0, 1, 5, '25,50,75', 'The Packleader''s End: Marshal Dughan against Grimmaw; the pack at 25, 50 and 75 % of the bar'),
(10,    0, 70, 960001, 4, 25,     0, 240, 40, 2, 1,      0, 1, 5, '25,50,75', 'The Kolkar War Camp: four tents out, two Outrunners each, one back about 25 s after one burns');

-- The escort, the rescue, the capture and the warlord, with their own columns.
INSERT INTO `mod_wow_fates_objective` (`expedition`, `boss_entry`, `boss_at`, `gather_entry`, `gather_count`, `gather_respawn`,
  `ally_entry`, `hold_time`, `wave_interval`, `wave_size`, `wave_growth`, `channel_entry`, `ally_count`, `wave_count`, `boss_waves`,
  `time_limit`, `max_losses`, `taken_entry`, `boss_spells`, `comment`) VALUES
(11,    0, 70,      0, 8, 5, 96203, 240, 40, 2, 1,      0, 1, 5, '25,50,75',   0, 2,      0, '', 'Remy''s Wares: Remy walks the hill road; Defias Cutpurses ambush him at 25, 50 and 75 % of it');

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
   'Помогите ветеранам Колючего Холма отбить пять атак Иглогривых.', 'Отбейте атаки Иглогривых', 'Иглогривые идут в атаку!'),
(9, 'frFR', 'La fin du chef de meute',
   'Le maréchal Dughan a retrouvé Grimmaw, le chef de meute des Rivepattes : aidez le maréchal à abattre le gnoll.', 'Aidez le maréchal Dughan à vaincre Grimmaw', 'Grimmaw appelle la meute !'),
(9, 'deDE', 'Das Ende des Rudelführers',
   'Marschall Dughan hat Grimmaw, den Rudelführer der Flusspfoten, aufgespürt: Helft dem Marschall, den Gnoll zu Fall zu bringen.', 'Helft Marschall Dughan, Grimmaw zu besiegen', 'Grimmaw ruft das Rudel!'),
(9, 'esES', 'El fin del líder de la manada',
   'El alguacil Dughan ha dado con Grimmaw, el líder de la manada Zarparrío: ayuda al alguacil a abatir al gnoll.', 'Ayuda al alguacil Dughan a derrotar a Grimmaw', '¡Grimmaw llama a la manada!'),
(9, 'esMX', 'El fin del líder de la manada',
   'El alguacil Dughan ha dado con Grimmaw, el líder de la manada Zarparrío: ayuda al alguacil a abatir al gnoll.', 'Ayuda al alguacil Dughan a derrotar a Grimmaw', '¡Grimmaw llama a la manada!'),
(9, 'ruRU', 'Конец вожака стаи',
   'Маршал Дугхан выследил Гриммо, вожака стаи Речной Лапы: помогите маршалу одолеть гнолла.', 'Помогите маршалу Дугхану одолеть Гриммо', 'Гриммо зовёт стаю!'),
(10, 'frFR', 'Le camp de guerre kolkar',
   'Les centaures kolkars se rassemblent dans leur camp : incendiez leurs tentes de guerre avant qu''ils ne fondent sur Razor Hill.', 'Incendiez les tentes de guerre kolkars', ''),
(10, 'deDE', 'Das Kriegslager der Kolkar',
   'Die Kolkarzentauren sammeln sich in ihrem Lager: Brennt ihre Kriegszelte nieder, bevor sie gegen Klingenhügel reiten.', 'Brennt die Kriegszelte der Kolkar nieder', ''),
(10, 'esES', 'El campamento de guerra kolkar',
   'Los centauros kolkar se reúnen en su campamento: quema sus tiendas de guerra antes de que cabalguen sobre Cerrotajo.', 'Quema las tiendas de guerra kolkar', ''),
(10, 'esMX', 'El campamento de guerra kolkar',
   'Los centauros kolkar se reúnen en su campamento: quema sus tiendas de guerra antes de que cabalguen sobre Cerrotajo.', 'Quema las tiendas de guerra kolkar', ''),
(10, 'ruRU', 'Боевой лагерь колкаров',
   'Кентавры-колкары собираются в своём лагере: сожгите их боевые шатры, пока они не напали на Колючий Холм.', 'Сожгите боевые шатры колкаров', ''),
(11, 'frFR', 'La marchandise de Remy',
   'Remy « Deux fois » monte sa marchandise par la route des collines, où guettent des vide-goussets défias : escortez-le jusqu''au bout du chemin.', 'Escortez Remy « Deux fois »', 'Des vide-goussets défias tendent une embuscade !'),
(11, 'deDE', 'Remys Waren',
   'Remy "Zweifach" bringt seine Waren die Hügelstraße hinauf, wo Taschendiebe der Defias lauern: Begleitet ihn bis ans Ende des Weges.', 'Begleitet Remy "Zweifach"', 'Taschendiebe der Defias greifen aus dem Hinterhalt an!'),
(11, 'esES', 'La mercancía de Remy',
   'Remy "Dos Veces" sube su mercancía por el camino de las colinas, donde acechan rateros Defias: escóltalo hasta el final del camino.', 'Escolta a Remy "Dos Veces"', '¡Los rateros Defias tienden una emboscada!'),
(11, 'esMX', 'La mercancía de Remy',
   'Remy "Dos Veces" sube su mercancía por el camino de las colinas, donde acechan rateros Defias: escóltalo hasta el final del camino.', 'Escolta a Remy "Dos Veces"', '¡Los rateros Defias tienden una emboscada!'),
(11, 'ruRU', 'Товары Реми',
   'Реми Повторюх везёт товар по холмистой дороге, где подстерегают карманники из Братства Справедливости: сопроводите его до конца пути.', 'Сопроводите Реми Повторюха', 'Карманники из Братства Справедливости устроили засаду!');

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
(15, 8,  1,   -29.5,  -4265.1,    66.6,    0, 'Durotar, north of the Razormane grounds'),
(16, 9,  0, -9795.0,    389.0,    39.6,    0, 'Elwynn Forest, the hills west of Westbrook'),
(17, 9,  0, -9945.1,    423.8,    34.8,    0, 'Elwynn Forest, the fields south of the Riverpaw camp'),
(18, 10, 1, -1012.7,  -4600.6,    25.7,    0, 'Durotar, Kolkar Crag, the heart of the camp'),
(19, 10, 1,  -984.8,  -4503.7,    26.8,    0, 'Durotar, Kolkar Crag, the north of the camp');

-- An escort's route, a rescue's way out: "x,y,z" points (path), each leg
-- walked as a player would; .fates check tries them.
INSERT INTO `mod_wow_fates_spawn` (`id`, `expedition`, `map`, `x`, `y`, `z`, `o`, `comment`, `path`) VALUES
(20, 11, 0, -9175.0,     78.0,    77.7,    0, 'Elwynn Forest, the hill road north-east of Goldshire',
   '-9244.0,122.0,72.5;-9184.0,83.0,77.9;-9144.0,54.0,77.7;-9109.0,7.0,88.3');

DELETE FROM `mod_wow_fates_reward` WHERE `id` BETWEEN 1 AND 99;
INSERT INTO `mod_wow_fates_reward` (`id`, `expedition`, `min_level`, `max_level`, `money`, `xp`, `item`, `item_count`, `hidden`, `comment`) VALUES
(1, 0,  1,  19,    500,   400,   858, 2, 0, 'Lesser Healing Potion'),
(2, 0, 20,  39,   3000,  2500,   929, 3, 0, 'Healing Potion'),
(3, 0, 40,  59,  10000,  9000,  1710, 3, 0, 'Greater Healing Potion'),
(4, 0, 60,  69,  30000, 25000,  1710, 5, 0, 'Greater Healing Potion'),
(5, 0, 70,  79,  60000, 60000, 33447, 3, 0, 'Runic Healing Potion'),
(6, 0, 80, 255, 150000,     0, 47241, 1, 0, 'Emblem of Triumph'),
(7, 1,  1, 255,      0,     0,  4496, 1, 1, 'Kobold Uprising, hidden: Small Brown Pouch');
