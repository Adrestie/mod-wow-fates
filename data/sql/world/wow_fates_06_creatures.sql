-- mod-wow-fates — the creatures of the Expeditions: the leaders of the
-- assaults (96100 to 96199) and the allies of the defences (96200 to 96299),
-- in the module's blocks 96000-96399 of ID_RANGES.md.
--
--   96100  Defias Ringleader: the leader of the Defias Assault (Elwynn
--          Forest, level 10 elite), a Defias Bandit made bigger and stronger.
--   96101  Grimmaw the Packleader: the elite of The Packleader's End (Elwynn
--          Forest, level 11 elite), a big Riverpaw gnoll.
--   96102  Hogger: the warlord of Hogger's Rampage (Elwynn Forest, level 11
--          elite), the gnoll himself, his name and model (448, display 384);
--          his blows are the module's (boss_spells), not his SmartAI: the
--          game's Hogger stays away from the area while he is out.
--   96200  Westbrook Sentinel: the ally of the Riverpaw Raid (Elwynn Forest,
--          level 10), a Stormwind guard of faction 495 (an escortee's: friend
--          of every player, Alliance and Horde, foe of the monsters), no
--          gossip, no guard's script.
--   96201  Razor Hill Veteran: the allies of the Quilboar Onslaught (Durotar,
--          level 9), Orgrimmar grunts of the same faction 495.
--   96203  Remy "Two Times": the ally of Remy's Wares (Elwynn Forest, level 6),
--          the Goldshire merchant with his own name and model (241, display
--          3254), of faction 495, without his town's immunity (unit_flags
--          512) nor his shop, with his words (creature_text).
--   96204  Darkspear Captive: the captives of Zalazane's Captives (Durotar,
--          level 8), Darkspear trolls of four looks (the trolls of Sen'jin,
--          displays 4070, 4073 to 4075), of faction 495, with their words.
--   96202  Marshal Dughan: the ally of The Packleader's End, the marshal of
--          Goldshire with his own name and model (240, display 1985), of
--          faction 495 as every ally, with his words (creature_text);
--          without the town's immunity to creatures (unit_flags 512), to
--          fight. An elite, as every boss support's ally, the module making
--          him bigger when he comes (WowFates.Boss.Scale).
--
-- The ally of a boss support is a known figure of the zone's side, by his
-- name and his model only: of the Alliance in an Alliance zone, of the Horde
-- in a Horde zone, of a neutral order (Argent Dawn, Cenarion Circle, the
-- goblin cartels...) in a contested or neutral one. Whatever his side, his
-- faction is 495, friend of every player and foe of the monsters, so that
-- both factions may take part, heal him and help him.
--
--   96300  Altar Focus: an invisible bunny on the altar of an invocation,
--          what the channel of the players (spell 96004) aims at, for its beam
--          (96300 to 96399: the module's helpers).
--
-- Each is a copy of an existing creature of the game, its own values set on
-- top, so that the file follows the columns of creature_template whatever the
-- version of the core. Its health is the one of a single player: the module
-- raises it with the players in the area (WowFates.Expedition.ParticipantScaling).
-- RegenHealth 0: it does not heal back to full between two fights.
--
-- Idempotent: the module's rows are deleted before being written again.

DELETE FROM `creature_template` WHERE `entry` BETWEEN 96100 AND 96399;
DROP TEMPORARY TABLE IF EXISTS `wow_fates_creature`;
CREATE TEMPORARY TABLE `wow_fates_creature` SELECT * FROM `creature_template` WHERE `entry` = 116;
UPDATE `wow_fates_creature` SET
  `entry` = 96100, `name` = 'Defias Ringleader', `subname` = '',
  `minlevel` = 10, `maxlevel` = 10, `rank` = 1,
  `HealthModifier` = 6, `DamageModifier` = 1.6, `ArmorModifier` = 1.2,
  `RegenHealth` = 0, `AIName` = 'SmartAI', `ScriptName` = '',
  `KillCredit1` = 0, `KillCredit2` = 0;
INSERT INTO `creature_template` SELECT * FROM `wow_fates_creature`;
DROP TEMPORARY TABLE `wow_fates_creature`;

CREATE TEMPORARY TABLE `wow_fates_creature` SELECT * FROM `creature_template` WHERE `entry` = 478;
UPDATE `wow_fates_creature` SET
  `entry` = 96101, `name` = 'Grimmaw the Packleader', `subname` = '',
  `minlevel` = 11, `maxlevel` = 11, `rank` = 1,
  `HealthModifier` = 10, `DamageModifier` = 1.8, `ArmorModifier` = 1.3,
  `RegenHealth` = 0, `AIName` = 'SmartAI', `ScriptName` = '',
  `KillCredit1` = 0, `KillCredit2` = 0;
INSERT INTO `creature_template` SELECT * FROM `wow_fates_creature`;
DROP TEMPORARY TABLE `wow_fates_creature`;

CREATE TEMPORARY TABLE `wow_fates_creature` SELECT * FROM `creature_template` WHERE `entry` = 240;
UPDATE `wow_fates_creature` SET
  `entry` = 96202, `subname` = '',
  `minlevel` = 12, `maxlevel` = 12, `rank` = 1, `faction` = 495,
  `gossip_menu_id` = 0, `npcflag` = 0, `flags_extra` = 0, `unit_flags` = 0,
  `HealthModifier` = 8, `DamageModifier` = 1.4, `ArmorModifier` = 1.5,
  `RegenHealth` = 0, `AIName` = '', `ScriptName` = '',
  `KillCredit1` = 0, `KillCredit2` = 0;
INSERT INTO `creature_template` SELECT * FROM `wow_fates_creature`;
DROP TEMPORARY TABLE `wow_fates_creature`;

CREATE TEMPORARY TABLE `wow_fates_creature` SELECT * FROM `creature_template` WHERE `entry` = 3296;
UPDATE `wow_fates_creature` SET
  `entry` = 96201, `name` = 'Razor Hill Veteran', `subname` = '',
  `minlevel` = 9, `maxlevel` = 9, `rank` = 0, `faction` = 495,
  `gossip_menu_id` = 0, `npcflag` = 0, `flags_extra` = 0,
  `HealthModifier` = 4, `DamageModifier` = 1.2, `ArmorModifier` = 1.5,
  `RegenHealth` = 0, `AIName` = '', `ScriptName` = '',
  `KillCredit1` = 0, `KillCredit2` = 0;
INSERT INTO `creature_template` SELECT * FROM `wow_fates_creature`;
DROP TEMPORARY TABLE `wow_fates_creature`;

-- the ELM General Purpose Bunny, what the game's creatures channel at: not
-- selectable, immune, invisible, without its scripts
CREATE TEMPORARY TABLE `wow_fates_creature` SELECT * FROM `creature_template` WHERE `entry` = 23837;
UPDATE `wow_fates_creature` SET
  `entry` = 96300, `name` = 'Altar Focus', `subname` = '',
  `AIName` = '', `ScriptName` = '', `KillCredit1` = 0, `KillCredit2` = 0;
INSERT INTO `creature_template` SELECT * FROM `wow_fates_creature`;
DROP TEMPORARY TABLE `wow_fates_creature`;

CREATE TEMPORARY TABLE `wow_fates_creature` SELECT * FROM `creature_template` WHERE `entry` = 68;
UPDATE `wow_fates_creature` SET
  `entry` = 96200, `name` = 'Westbrook Sentinel', `subname` = '',
  `minlevel` = 10, `maxlevel` = 10, `rank` = 0, `faction` = 495,
  `gossip_menu_id` = 0, `npcflag` = 0, `flags_extra` = 0,
  `HealthModifier` = 12, `DamageModifier` = 1.2, `ArmorModifier` = 1.5,
  `RegenHealth` = 0, `AIName` = '', `ScriptName` = '',
  `KillCredit1` = 0, `KillCredit2` = 0;
INSERT INTO `creature_template` SELECT * FROM `wow_fates_creature`;
DROP TEMPORARY TABLE `wow_fates_creature`;

CREATE TEMPORARY TABLE `wow_fates_creature` SELECT * FROM `creature_template` WHERE `entry` = 241;
UPDATE `wow_fates_creature` SET
  `entry` = 96203, `subname` = '',
  `minlevel` = 6, `maxlevel` = 6, `rank` = 0, `faction` = 495,
  `gossip_menu_id` = 0, `npcflag` = 0, `flags_extra` = 0, `unit_flags` = 0,
  `HealthModifier` = 5, `DamageModifier` = 1, `ArmorModifier` = 1.5,
  `RegenHealth` = 0, `AIName` = '', `ScriptName` = '',
  `KillCredit1` = 0, `KillCredit2` = 0;
INSERT INTO `creature_template` SELECT * FROM `wow_fates_creature`;
DROP TEMPORARY TABLE `wow_fates_creature`;

CREATE TEMPORARY TABLE `wow_fates_creature` SELECT * FROM `creature_template` WHERE `entry` = 3190;
UPDATE `wow_fates_creature` SET
  `entry` = 96204, `name` = 'Darkspear Captive', `subname` = '',
  `minlevel` = 8, `maxlevel` = 8, `rank` = 0, `faction` = 495,
  `gossip_menu_id` = 0, `npcflag` = 0, `flags_extra` = 0, `unit_flags` = 0,
  `HealthModifier` = 1.5, `DamageModifier` = 0.6, `ArmorModifier` = 1,
  `RegenHealth` = 1, `AIName` = '', `ScriptName` = '',
  `KillCredit1` = 0, `KillCredit2` = 0;
INSERT INTO `creature_template` SELECT * FROM `wow_fates_creature`;
DROP TEMPORARY TABLE `wow_fates_creature`;

CREATE TEMPORARY TABLE `wow_fates_creature` SELECT * FROM `creature_template` WHERE `entry` = 448;
UPDATE `wow_fates_creature` SET
  `entry` = 96102, `subname` = '',
  `minlevel` = 11, `maxlevel` = 11, `rank` = 1,
  `HealthModifier` = 7, `DamageModifier` = 1.7, `ArmorModifier` = 1.2,
  `RegenHealth` = 0, `AIName` = '', `ScriptName` = '',
  `KillCredit1` = 0, `KillCredit2` = 0;
INSERT INTO `creature_template` SELECT * FROM `wow_fates_creature`;
DROP TEMPORARY TABLE `wow_fates_creature`;

DELETE FROM `creature_template_model` WHERE `CreatureID` BETWEEN 96100 AND 96399;
INSERT INTO `creature_template_model` (`CreatureID`, `Idx`, `CreatureDisplayID`, `DisplayScale`, `Probability`, `VerifiedBuild`) VALUES
(96100, 0, 2357, 1.35, 1, 0),
(96200, 0, 3167, 1, 1, 0),
(96101, 0, 512, 1.6, 1, 0),
(96201, 0, 4259, 1, 1, 0),
(96202, 0, 1985, 1, 1, 0),
(96300, 0, 11686, 1, 1, 0),
(96203, 0, 3254, 1, 1, 0),
(96204, 0, 4070, 1, 1, 0),
(96204, 1, 4073, 1, 1, 0),
(96204, 2, 4074, 1, 1, 0),
(96204, 3, 4075, 1, 1, 0),
(96102, 0, 384, 1, 1, 0);

DELETE FROM `creature_template_locale` WHERE `entry` BETWEEN 96100 AND 96399;
INSERT INTO `creature_template_locale` (`entry`, `locale`, `Name`, `Title`, `VerifiedBuild`) VALUES
(96100, 'frFR', 'Meneur défias', '', 0),
(96100, 'deDE', 'Rädelsführer der Defias', '', 0),
(96100, 'esES', 'Cabecilla Defias', '', 0),
(96100, 'esMX', 'Cabecilla Defias', '', 0),
(96100, 'ruRU', 'Главарь Братства Справедливости', '', 0),
(96200, 'frFR', 'Sentinelle de Westbrook', '', 0),
(96200, 'deDE', 'Wachposten von Westbrook', '', 0),
(96200, 'esES', 'Centinela de Westbrook', '', 0),
(96200, 'esMX', 'Centinela de Westbrook', '', 0),
(96200, 'ruRU', 'Часовой Вестбрука', '', 0),
(96201, 'frFR', 'Vétéran de Razor Hill', '', 0),
(96201, 'deDE', 'Veteran von Klingenhügel', '', 0),
(96201, 'esES', 'Veterano de Cerrotajo', '', 0),
(96201, 'esMX', 'Veterano de Cerrotajo', '', 0),
(96201, 'ruRU', 'Ветеран Колючего Холма', '', 0),
(96101, 'frFR', 'Grimmaw le chef de meute', '', 0),
(96101, 'deDE', 'Grimmaw der Rudelführer', '', 0),
(96101, 'esES', 'Grimmaw el líder de la manada', '', 0),
(96101, 'esMX', 'Grimmaw el líder de la manada', '', 0),
(96101, 'ruRU', 'Гриммо, вожак стаи', '', 0),
(96202, 'frFR', 'Maréchal Dughan', '', 0),
(96202, 'deDE', 'Marschall Dughan', '', 0),
(96202, 'esES', 'Alguacil Dughan', '', 0),
(96202, 'esMX', 'Alguacil Dughan', '', 0),
(96202, 'ruRU', 'Маршал Дугхан', '', 0),
(96203, 'frFR', 'Remy « Deux fois »', '', 0),
(96203, 'deDE', 'Remy "Zweifach"', '', 0),
(96203, 'esES', 'Remy "Dos Veces"', '', 0),
(96203, 'esMX', 'Remy "Dos Veces"', '', 0),
(96203, 'ruRU', 'Реми Повторюх', '', 0),
(96204, 'frFR', 'Captif sombrelance', '', 0),
(96204, 'deDE', 'Gefangener der Dunkelspeere', '', 0),
(96204, 'esES', 'Cautivo Lanza Negra', '', 0),
(96204, 'esMX', 'Cautivo Lanza Negra', '', 0),
(96204, 'ruRU', 'Пленник из племени Черного Копья', '', 0),
(96102, 'frFR', 'Lardeur', '', 0),
(96102, 'deDE', 'Hogger', '', 0),
(96102, 'esES', 'Hogger', '', 0),
(96102, 'esMX', 'Hogger', '', 0),
(96102, 'ruRU', 'Дробитель', '', 0);

-- Its blows: the Bandit's Snap Kick, more often, and the Bodyguard's Disarm.
DELETE FROM `smart_scripts` WHERE `entryorguid` BETWEEN 96100 AND 96399 AND `source_type` = 0;
INSERT INTO `smart_scripts` (`entryorguid`, `source_type`, `id`, `link`, `event_type`, `event_phase_mask`, `event_chance`, `event_flags`, `event_param1`, `event_param2`, `event_param3`, `event_param4`, `event_param5`, `event_param6`, `action_type`, `action_param1`, `action_param2`, `action_param3`, `action_param4`, `action_param5`, `action_param6`, `target_type`, `target_param1`, `target_param2`, `target_param3`, `target_param4`, `target_x`, `target_y`, `target_z`, `target_o`, `comment`) VALUES
(96100, 0, 0, 0, 0, 0, 100, 0, 3000, 5000, 6000, 9000, 0, 0, 11, 8646, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 'Defias Ringleader - In Combat - Cast Snap Kick'),
(96100, 0, 1, 0, 0, 0, 100, 0, 8000, 10000, 25000, 30000, 0, 0, 11, 6713, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 'Defias Ringleader - In Combat - Cast Disarm'),
(96101, 0, 0, 0, 0, 0, 100, 0, 4000, 6000, 7000, 11000, 0, 0, 11, 8646, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 'Grimmaw the Packleader - In Combat - Cast Snap Kick');

-- A warlord's: 0 when the fight begins, 1 to 4 at its phases (the marks of
-- boss_waves, in order), 5 at its death, 6 when the time is out.
-- A rescue's captives': 0 out of the cage, 1 at the way out.
-- An escort's ally's: 0 when he sets off, 1 at an ambush, 2 waiting for the
-- players (every 30 s), 3 at the end of the road, 4 when he falls.
-- The words of the boss supports' creatures: said (a bubble, not a yell),
-- in the universal language, through creature_text groups the module calls: 0 at the
-- start, 1 in the fight (every 20 to 40 s, one at random), 2 at the elite's
-- half health, 3 at the victory, 4 when he falls, 5 to 8 at 75, 50, 25 and
-- 10 % of his own health; the elite's, 0 when the
-- true fight begins (a player in the area), 1 to 4 at 75, 50, 25 and 10 % of
-- its health, 5 at its death.
DELETE FROM `creature_text` WHERE `CreatureID` BETWEEN 96100 AND 96399;
INSERT INTO `creature_text` (`CreatureID`, `GroupID`, `ID`, `Text`, `Type`, `Language`, `Probability`, `Emote`, `Duration`, `Sound`, `BroadcastTextId`, `TextRange`, `comment`) VALUES
(96202, 5, 0, 'I''ve taken worse in the Second War!', 12, 0, 100, 0, 0, 0, 0, 0, 'Marshal Dughan - his health at 75 %'),
(96202, 6, 0, 'I need some help here!', 12, 0, 100, 0, 0, 0, 0, 0, 'Marshal Dughan - his health at 50 %'),
(96202, 7, 0, 'Heal me, quickly!', 12, 0, 100, 0, 0, 0, 0, 0, 'Marshal Dughan - his health at 25 %'),
(96202, 8, 0, 'I can''t... hold much longer...', 12, 0, 100, 0, 0, 0, 0, 0, 'Marshal Dughan - his health at 10 %'),
(96101, 0, 0, 'Grimmaw smells fresh meat! Kill! Kill!', 12, 0, 100, 0, 0, 0, 0, 0, 'Grimmaw the Packleader - the true fight begins'),
(96101, 1, 0, 'You scratch Grimmaw? Grimmaw bites back!', 12, 0, 100, 0, 0, 0, 0, 0, 'Grimmaw the Packleader - 75 %'),
(96101, 2, 0, 'Pack! To me! Tear them apart!', 12, 0, 100, 0, 0, 0, 0, 0, 'Grimmaw the Packleader - 50 %'),
(96101, 3, 0, 'Grimmaw... will not run!', 12, 0, 100, 0, 0, 0, 0, 0, 'Grimmaw the Packleader - 25 %'),
(96101, 4, 0, 'No... Grimmaw is the packleader...!', 12, 0, 100, 0, 0, 0, 0, 0, 'Grimmaw the Packleader - 10 %'),
(96101, 5, 0, 'The pack... will avenge... Grimmaw...', 12, 0, 100, 0, 0, 0, 0, 0, 'Grimmaw the Packleader - death'),
(96202, 0, 0, 'To arms! Stand with me!', 12, 0, 100, 0, 0, 0, 0, 0, 'Marshal Dughan - start of the fight'),
(96202, 1, 0, 'Hold the line!', 12, 0, 100, 0, 0, 0, 0, 0, 'Marshal Dughan - in the fight'),
(96202, 1, 1, 'Is that all you''ve got?', 12, 0, 100, 0, 0, 0, 0, 0, 'Marshal Dughan - in the fight'),
(96202, 1, 2, 'Keep at it, it can''t last much longer!', 12, 0, 100, 0, 0, 0, 0, 0, 'Marshal Dughan - in the fight'),
(96202, 2, 0, 'It''s weakening! Press the attack!', 12, 0, 100, 0, 0, 0, 0, 0, 'Marshal Dughan - the elite at half health'),
(96202, 3, 0, 'It''s done. Well fought, friends.', 12, 0, 100, 0, 0, 0, 0, 0, 'Marshal Dughan - victory'),
(96202, 4, 0, 'I... have failed...', 12, 0, 100, 0, 0, 0, 0, 0, 'Marshal Dughan - falls'),
(96203, 0, 0, 'Right, let''s get these goods up the road. Stay close!', 12, 0, 100, 0, 0, 0, 0, 0, 'Remy "Two Times" - sets off'),
(96203, 1, 0, 'Defias! Protect the wares!', 12, 0, 100, 0, 0, 0, 0, 0, 'Remy "Two Times" - an ambush'),
(96203, 1, 1, 'Thieves! Keep them off the crates!', 12, 0, 100, 0, 0, 0, 0, 0, 'Remy "Two Times" - an ambush'),
(96203, 2, 0, 'Hey! I''m not going on alone, these hills aren''t safe!', 12, 0, 100, 0, 0, 0, 0, 0, 'Remy "Two Times" - waits for the players'),
(96203, 3, 0, 'We made it, and not a crate lost! You have my thanks, friends.', 12, 0, 100, 0, 0, 0, 0, 0, 'Remy "Two Times" - the end of the road'),
(96203, 4, 0, 'My... wares...', 12, 0, 100, 0, 0, 0, 0, 0, 'Remy "Two Times" - falls'),
(96204, 0, 0, 'Thank the loa! I be out of here, mon!', 12, 0, 100, 0, 0, 0, 0, 0, 'Darkspear Captive - out of the cage'),
(96204, 0, 1, 'You be a true friend of the Darkspear!', 12, 0, 100, 0, 0, 0, 0, 0, 'Darkspear Captive - out of the cage'),
(96204, 1, 0, 'Sen''jin, here I come!', 12, 0, 100, 0, 0, 0, 0, 0, 'Darkspear Captive - at the way out'),
(96102, 0, 0, 'Grr... Hogger is big boss! You die now!', 12, 0, 100, 0, 0, 0, 0, 0, 'Hogger - the fight begins'),
(96102, 1, 0, 'Pack! Come help Hogger!', 12, 0, 100, 0, 0, 0, 0, 0, 'Hogger - first phase'),
(96102, 2, 0, 'Hogger smash you!', 12, 0, 100, 0, 0, 0, 0, 0, 'Hogger - second phase'),
(96102, 3, 0, 'Grraaarrr! Hogger angry now!', 12, 0, 100, 0, 0, 0, 0, 0, 'Hogger - third phase'),
(96102, 4, 0, 'No... Hogger... not lose...', 12, 0, 100, 0, 0, 0, 0, 0, 'Hogger - fourth phase'),
(96102, 5, 0, 'Yipe! Help Hogger!', 12, 0, 100, 0, 0, 0, 0, 0, 'Hogger - death'),
(96102, 6, 0, 'Ha! Weak little ones run! Hogger rules the forest!', 12, 0, 100, 0, 0, 0, 0, 0, 'Hogger - the time is out');

DELETE FROM `creature_text_locale` WHERE `CreatureID` BETWEEN 96100 AND 96399;
INSERT INTO `creature_text_locale` (`CreatureID`, `GroupID`, `ID`, `Locale`, `Text`) VALUES
(96202, 5, 0, 'frFR', 'J''ai vu pire pendant la Deuxième Guerre !'),
(96202, 5, 0, 'deDE', 'Im Zweiten Krieg habe ich Schlimmeres erlebt!'),
(96202, 5, 0, 'esES', '¡He sufrido cosas peores en la Segunda Guerra!'),
(96202, 5, 0, 'esMX', '¡He sufrido cosas peores en la Segunda Guerra!'),
(96202, 5, 0, 'ruRU', 'Во Второй войне мне доставалось и похуже!'),
(96202, 6, 0, 'frFR', 'Un peu d''aide, ici !'),
(96202, 6, 0, 'deDE', 'Ich brauche hier Hilfe!'),
(96202, 6, 0, 'esES', '¡Necesito ayuda aquí!'),
(96202, 6, 0, 'esMX', '¡Necesito ayuda aquí!'),
(96202, 6, 0, 'ruRU', 'Мне нужна помощь!'),
(96202, 7, 0, 'frFR', 'Soignez-moi, vite !'),
(96202, 7, 0, 'deDE', 'Heilt mich, schnell!'),
(96202, 7, 0, 'esES', '¡Curadme, rápido!'),
(96202, 7, 0, 'esMX', '¡Curadme, rápido!'),
(96202, 7, 0, 'ruRU', 'Лечите меня, скорее!'),
(96202, 8, 0, 'frFR', 'Je ne tiendrai... plus longtemps...'),
(96202, 8, 0, 'deDE', 'Ich halte... nicht mehr lange durch...'),
(96202, 8, 0, 'esES', 'No aguantaré... mucho más...'),
(96202, 8, 0, 'esMX', 'No aguantaré... mucho más...'),
(96202, 8, 0, 'ruRU', 'Я долго... не продержусь...'),
(96101, 0, 0, 'frFR', 'Grimmaw sent la viande fraîche ! Tuez ! Tuez !'),
(96101, 0, 0, 'deDE', 'Grimmaw riecht frisches Fleisch! Tötet! Tötet!'),
(96101, 0, 0, 'esES', '¡Grimmaw huele carne fresca! ¡Matad! ¡Matad!'),
(96101, 0, 0, 'esMX', '¡Grimmaw huele carne fresca! ¡Matad! ¡Matad!'),
(96101, 0, 0, 'ruRU', 'Гриммо чует свежее мясо! Убить! Убить!'),
(96101, 1, 0, 'frFR', 'Tu griffes Grimmaw ? Grimmaw mord !'),
(96101, 1, 0, 'deDE', 'Du kratzt Grimmaw? Grimmaw beißt zurück!'),
(96101, 1, 0, 'esES', '¿Arañas a Grimmaw? ¡Grimmaw muerde!'),
(96101, 1, 0, 'esMX', '¿Arañas a Grimmaw? ¡Grimmaw muerde!'),
(96101, 1, 0, 'ruRU', 'Царапаешь Гриммо? Гриммо кусает!'),
(96101, 2, 0, 'frFR', 'La meute ! À moi ! Déchirez-les !'),
(96101, 2, 0, 'deDE', 'Rudel! Zu mir! Zerfetzt sie!'),
(96101, 2, 0, 'esES', '¡Manada! ¡A mí! ¡Despedazadlos!'),
(96101, 2, 0, 'esMX', '¡Manada! ¡A mí! ¡Despedazadlos!'),
(96101, 2, 0, 'ruRU', 'Стая! Ко мне! Порвите их!'),
(96101, 3, 0, 'frFR', 'Grimmaw... ne fuira pas !'),
(96101, 3, 0, 'deDE', 'Grimmaw... flieht nicht!'),
(96101, 3, 0, 'esES', '¡Grimmaw... no huirá!'),
(96101, 3, 0, 'esMX', '¡Grimmaw... no huirá!'),
(96101, 3, 0, 'ruRU', 'Гриммо... не побежит!'),
(96101, 4, 0, 'frFR', 'Non... Grimmaw est le chef de meute... !'),
(96101, 4, 0, 'deDE', 'Nein... Grimmaw ist der Rudelführer...!'),
(96101, 4, 0, 'esES', 'No... ¡Grimmaw es el líder de la manada...!'),
(96101, 4, 0, 'esMX', 'No... ¡Grimmaw es el líder de la manada...!'),
(96101, 4, 0, 'ruRU', 'Нет... Гриммо — вожак стаи...!'),
(96101, 5, 0, 'frFR', 'La meute... vengera... Grimmaw...'),
(96101, 5, 0, 'deDE', 'Das Rudel... wird Grimmaw... rächen...'),
(96101, 5, 0, 'esES', 'La manada... vengará... a Grimmaw...'),
(96101, 5, 0, 'esMX', 'La manada... vengará... a Grimmaw...'),
(96101, 5, 0, 'ruRU', 'Стая... отомстит... за Гриммо...'),
(96202, 0, 0, 'frFR', 'Aux armes ! Avec moi !'),
(96202, 0, 0, 'deDE', 'Zu den Waffen! Steht mir bei!'),
(96202, 0, 0, 'esES', '¡A las armas! ¡Conmigo!'),
(96202, 0, 0, 'esMX', '¡A las armas! ¡Conmigo!'),
(96202, 0, 0, 'ruRU', 'К оружию! За мной!'),
(96202, 1, 0, 'frFR', 'Tenez bon !'),
(96202, 1, 0, 'deDE', 'Haltet die Stellung!'),
(96202, 1, 0, 'esES', '¡Mantened la posición!'),
(96202, 1, 0, 'esMX', '¡Mantened la posición!'),
(96202, 1, 0, 'ruRU', 'Держите строй!'),
(96202, 1, 1, 'frFR', 'C''est tout ce que tu as ?'),
(96202, 1, 1, 'deDE', 'Ist das alles, was du hast?'),
(96202, 1, 1, 'esES', '¿Eso es todo lo que tienes?'),
(96202, 1, 1, 'esMX', '¿Eso es todo lo que tienes?'),
(96202, 1, 1, 'ruRU', 'И это всё, на что ты способен?'),
(96202, 1, 2, 'frFR', 'Continuez, il ne tiendra plus longtemps !'),
(96202, 1, 2, 'deDE', 'Weiter so, lange hält er nicht mehr durch!'),
(96202, 1, 2, 'esES', '¡Seguid así, no aguantará mucho más!'),
(96202, 1, 2, 'esMX', '¡Seguid así, no aguantará mucho más!'),
(96202, 1, 2, 'ruRU', 'Не отступайте, долго он не продержится!'),
(96202, 2, 0, 'frFR', 'Il faiblit ! Redoublez d''efforts !'),
(96202, 2, 0, 'deDE', 'Er wird schwächer! Greift weiter an!'),
(96202, 2, 0, 'esES', '¡Se está debilitando! ¡Seguid atacando!'),
(96202, 2, 0, 'esMX', '¡Se está debilitando! ¡Seguid atacando!'),
(96202, 2, 0, 'ruRU', 'Он слабеет! Наседайте!'),
(96202, 3, 0, 'frFR', 'C''est fini. Bien combattu, mes amis.'),
(96202, 3, 0, 'deDE', 'Es ist vollbracht. Gut gekämpft, Freunde.'),
(96202, 3, 0, 'esES', 'Se acabó. Bien luchado, amigos.'),
(96202, 3, 0, 'esMX', 'Se acabó. Bien luchado, amigos.'),
(96202, 3, 0, 'ruRU', 'Всё кончено. Хорошо сражались, друзья.'),
(96202, 4, 0, 'frFR', 'J''ai... échoué...'),
(96202, 4, 0, 'deDE', 'Ich... habe versagt...'),
(96202, 4, 0, 'esES', 'He... fracasado...'),
(96202, 4, 0, 'esMX', 'He... fracasado...'),
(96202, 4, 0, 'ruRU', 'Я... подвёл вас...'),
(96203, 0, 0, 'frFR', 'Bon, montons ces marchandises par la route. Restez près de moi !'),
(96203, 0, 0, 'deDE', 'Also gut, bringen wir die Waren die Straße hinauf. Bleibt in meiner Nähe!'),
(96203, 0, 0, 'esES', 'Bien, subamos esta mercancía por el camino. ¡No os alejéis!'),
(96203, 0, 0, 'esMX', 'Bien, subamos esta mercancía por el camino. ¡No os alejéis!'),
(96203, 0, 0, 'ruRU', 'Ну что ж, повезём товар по дороге. Держитесь рядом!'),
(96203, 1, 0, 'frFR', 'Des Défias ! Protégez la marchandise !'),
(96203, 1, 0, 'deDE', 'Defias! Schützt die Waren!'),
(96203, 1, 0, 'esES', '¡Defias! ¡Proteged la mercancía!'),
(96203, 1, 0, 'esMX', '¡Defias! ¡Proteged la mercancía!'),
(96203, 1, 0, 'ruRU', 'Братство! Защитите товар!'),
(96203, 1, 1, 'frFR', 'Des voleurs ! Éloignez-les des caisses !'),
(96203, 1, 1, 'deDE', 'Diebe! Haltet sie von den Kisten fern!'),
(96203, 1, 1, 'esES', '¡Ladrones! ¡Alejadlos de las cajas!'),
(96203, 1, 1, 'esMX', '¡Ladrones! ¡Alejadlos de las cajas!'),
(96203, 1, 1, 'ruRU', 'Воры! Не подпускайте их к ящикам!'),
(96203, 2, 0, 'frFR', 'Hé ! Je ne continue pas seul, ces collines ne sont pas sûres !'),
(96203, 2, 0, 'deDE', 'He! Allein gehe ich nicht weiter, diese Hügel sind nicht sicher!'),
(96203, 2, 0, 'esES', '¡Eh! No sigo solo, estas colinas no son seguras.'),
(96203, 2, 0, 'esMX', '¡Eh! No sigo solo, estas colinas no son seguras.'),
(96203, 2, 0, 'ruRU', 'Эй! Один я дальше не пойду, на этих холмах опасно!'),
(96203, 3, 0, 'frFR', 'On y est, et pas une caisse de perdue ! Merci, mes amis.'),
(96203, 3, 0, 'deDE', 'Geschafft, und keine Kiste verloren! Habt Dank, Freunde.'),
(96203, 3, 0, 'esES', '¡Lo logramos, y sin perder una caja! Gracias, amigos.'),
(96203, 3, 0, 'esMX', '¡Lo logramos, y sin perder una caja! Gracias, amigos.'),
(96203, 3, 0, 'ruRU', 'Добрались, и ни одного ящика не потеряли! Спасибо, друзья.'),
(96203, 4, 0, 'frFR', 'Ma... marchandise...'),
(96203, 4, 0, 'deDE', 'Meine... Waren...'),
(96203, 4, 0, 'esES', 'Mi... mercancía...'),
(96203, 4, 0, 'esMX', 'Mi... mercancía...'),
(96203, 4, 0, 'ruRU', 'Мой... товар...'),
(96204, 0, 0, 'frFR', 'Merci aux loas ! Je file, mon !'),
(96204, 0, 0, 'deDE', 'Dank sei den Loa! Ich verschwinde, Mann!'),
(96204, 0, 0, 'esES', '¡Gracias a los loas! ¡Me largo, hermano!'),
(96204, 0, 0, 'esMX', '¡Gracias a los loas! ¡Me largo, hermano!'),
(96204, 0, 0, 'ruRU', 'Хвала лоа! Я сматываюсь, приятель!'),
(96204, 0, 1, 'frFR', 'T''es un vrai ami des Sombrelances !'),
(96204, 0, 1, 'deDE', 'Du bist ein wahrer Freund der Dunkelspeere!'),
(96204, 0, 1, 'esES', '¡Eres un verdadero amigo de los Lanza Negra!'),
(96204, 0, 1, 'esMX', '¡Eres un verdadero amigo de los Lanza Negra!'),
(96204, 0, 1, 'ruRU', 'Ты настоящий друг Черного Копья!'),
(96204, 1, 0, 'frFR', 'Sen''jin, me voilà !'),
(96204, 1, 0, 'deDE', 'Sen''jin, ich komme!'),
(96204, 1, 0, 'esES', '¡Sen''jin, allá voy!'),
(96204, 1, 0, 'esMX', '¡Sen''jin, allá voy!'),
(96204, 1, 0, 'ruRU', 'Сен''джин, я иду!'),
(96102, 0, 0, 'frFR', 'Grr... Lardeur est grand chef ! Toi mourir maintenant !'),
(96102, 0, 0, 'deDE', 'Grr... Hogger ist großer Boss! Du stirbst jetzt!'),
(96102, 0, 0, 'esES', '¡Grr... Hogger es gran jefe! ¡Tú morir ahora!'),
(96102, 0, 0, 'esMX', '¡Grr... Hogger es gran jefe! ¡Tú morir ahora!'),
(96102, 0, 0, 'ruRU', 'Ррр... Дробитель — большой вожак! Ты умирать сейчас!'),
(96102, 1, 0, 'frFR', 'La meute ! Venez aider Lardeur !'),
(96102, 1, 0, 'deDE', 'Rudel! Kommt Hogger helfen!'),
(96102, 1, 0, 'esES', '¡Manada! ¡Venid a ayudar a Hogger!'),
(96102, 1, 0, 'esMX', '¡Manada! ¡Venid a ayudar a Hogger!'),
(96102, 1, 0, 'ruRU', 'Стая! Помогите Дробителю!'),
(96102, 2, 0, 'frFR', 'Lardeur t''écrase !'),
(96102, 2, 0, 'deDE', 'Hogger zerquetscht dich!'),
(96102, 2, 0, 'esES', '¡Hogger te aplasta!'),
(96102, 2, 0, 'esMX', '¡Hogger te aplasta!'),
(96102, 2, 0, 'ruRU', 'Дробитель раздавит тебя!'),
(96102, 3, 0, 'frFR', 'Grraaarrr ! Lardeur en colère maintenant !'),
(96102, 3, 0, 'deDE', 'Grraaarrr! Hogger jetzt wütend!'),
(96102, 3, 0, 'esES', '¡Grraaarrr! ¡Hogger enfadado ahora!'),
(96102, 3, 0, 'esMX', '¡Grraaarrr! ¡Hogger enfadado ahora!'),
(96102, 3, 0, 'ruRU', 'Грррааа! Дробитель злой теперь!'),
(96102, 4, 0, 'frFR', 'Non... Lardeur... pas perdre...'),
(96102, 4, 0, 'deDE', 'Nein... Hogger... nicht verlieren...'),
(96102, 4, 0, 'esES', 'No... Hogger... no perder...'),
(96102, 4, 0, 'esMX', 'No... Hogger... no perder...'),
(96102, 4, 0, 'ruRU', 'Нет... Дробитель... не проиграть...'),
(96102, 5, 0, 'frFR', 'Kaï ! À l''aide, Lardeur !'),
(96102, 5, 0, 'deDE', 'Jaul! Helft Hogger!'),
(96102, 5, 0, 'esES', '¡Ay! ¡Ayudad a Hogger!'),
(96102, 5, 0, 'esMX', '¡Ay! ¡Ayudad a Hogger!'),
(96102, 5, 0, 'ruRU', 'Ай! Помогите Дробителю!'),
(96102, 6, 0, 'frFR', 'Ha ! Les petits faibles fuient ! Lardeur règne sur la forêt !'),
(96102, 6, 0, 'deDE', 'Ha! Schwache Kleine laufen weg! Hogger herrscht über den Wald!'),
(96102, 6, 0, 'esES', '¡Ja! ¡Los débiles huyen! ¡Hogger manda en el bosque!'),
(96102, 6, 0, 'esMX', '¡Ja! ¡Los débiles huyen! ¡Hogger manda en el bosque!'),
(96102, 6, 0, 'ruRU', 'Ха! Слабаки бегут! Дробитель правит лесом!');
