-- mod-wow-fates — the creatures of the Expeditions: the leaders of the
-- assaults (96100 to 96199) and the allies of the defences (96200 to 96299),
-- in the module's blocks 96000-96399 of ID_RANGES.md.
--
--   96100  Defias Ringleader: the leader of the Defias Assault (Elwynn
--          Forest, level 10 elite), a Defias Bandit made bigger and stronger.
--   96101  Grimmaw the Packleader: the elite of The Packleader's End (Elwynn
--          Forest, level 11 elite), a big Riverpaw gnoll.
--   96200  Westbrook Sentinel: the ally of the Riverpaw Raid (Elwynn Forest,
--          level 10), a Stormwind guard of faction 495 (an escortee's: friend
--          of every player, Alliance and Horde, foe of the monsters), no
--          gossip, no guard's script.
--   96201  Razor Hill Veteran: the allies of the Quilboar Onslaught (Durotar,
--          level 9), Orgrimmar grunts of the same faction 495.
--   96202  Marshal Dughan: the ally of The Packleader's End, the marshal of
--          Goldshire with his own name and model (240, display 1985), of
--          faction 495 as every ally, with his words (creature_text);
--          without the town's immunity to creatures (unit_flags 512), to
--          fight.
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
  `minlevel` = 12, `maxlevel` = 12, `rank` = 0, `faction` = 495,
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

DELETE FROM `creature_template_model` WHERE `CreatureID` BETWEEN 96100 AND 96399;
INSERT INTO `creature_template_model` (`CreatureID`, `Idx`, `CreatureDisplayID`, `DisplayScale`, `Probability`, `VerifiedBuild`) VALUES
(96100, 0, 2357, 1.35, 1, 0),
(96200, 0, 3167, 1, 1, 0),
(96101, 0, 512, 1.6, 1, 0),
(96201, 0, 4259, 1, 1, 0),
(96202, 0, 1985, 1, 1, 0),
(96300, 0, 11686, 1, 1, 0);

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
(96202, 'ruRU', 'Маршал Дугхан', '', 0);

-- Its blows: the Bandit's Snap Kick, more often, and the Bodyguard's Disarm.
DELETE FROM `smart_scripts` WHERE `entryorguid` BETWEEN 96100 AND 96399 AND `source_type` = 0;
INSERT INTO `smart_scripts` (`entryorguid`, `source_type`, `id`, `link`, `event_type`, `event_phase_mask`, `event_chance`, `event_flags`, `event_param1`, `event_param2`, `event_param3`, `event_param4`, `event_param5`, `event_param6`, `action_type`, `action_param1`, `action_param2`, `action_param3`, `action_param4`, `action_param5`, `action_param6`, `target_type`, `target_param1`, `target_param2`, `target_param3`, `target_param4`, `target_x`, `target_y`, `target_z`, `target_o`, `comment`) VALUES
(96100, 0, 0, 0, 0, 0, 100, 0, 3000, 5000, 6000, 9000, 0, 0, 11, 8646, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 'Defias Ringleader - In Combat - Cast Snap Kick'),
(96100, 0, 1, 0, 0, 0, 100, 0, 8000, 10000, 25000, 30000, 0, 0, 11, 6713, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 'Defias Ringleader - In Combat - Cast Disarm'),
(96101, 0, 0, 0, 0, 0, 100, 0, 4000, 6000, 7000, 11000, 0, 0, 11, 8646, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 'Grimmaw the Packleader - In Combat - Cast Snap Kick');

-- The words of the boss supports' creatures: said (a bubble, not a yell),
-- in the universal language, through creature_text groups the module calls: 0 at the
-- start, 1 in the fight (every 20 to 40 s, one at random), 2 at the elite's
-- half health, 3 at the victory, 4 when he falls; the elite's, 0 when the
-- true fight begins (a player in the area), 1 to 4 at 75, 50, 25 and 10 % of
-- its health, 5 at its death.
DELETE FROM `creature_text` WHERE `CreatureID` BETWEEN 96100 AND 96399;
INSERT INTO `creature_text` (`CreatureID`, `GroupID`, `ID`, `Text`, `Type`, `Language`, `Probability`, `Emote`, `Duration`, `Sound`, `BroadcastTextId`, `TextRange`, `comment`) VALUES
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
(96202, 4, 0, 'I... have failed...', 12, 0, 100, 0, 0, 0, 0, 0, 'Marshal Dughan - falls');

DELETE FROM `creature_text_locale` WHERE `CreatureID` BETWEEN 96100 AND 96399;
INSERT INTO `creature_text_locale` (`CreatureID`, `GroupID`, `ID`, `Locale`, `Text`) VALUES
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
(96202, 4, 0, 'ruRU', 'Я... подвёл вас...');
