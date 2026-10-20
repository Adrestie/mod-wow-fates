-- mod-wow-fates — the creatures of the Expeditions: the leaders of the
-- assaults (96100 to 96199) and the allies of the defences (96200 to 96299),
-- in tranche 96 of ID_RANGES.md.
--
--   96100  Defias Ringleader: the leader of the Defias Assault (Elwynn
--          Forest, level 10 elite), a Defias Bandit made bigger and stronger.
--   96200  Westbrook Sentinel: the ally of the Riverpaw Raid (Elwynn Forest,
--          level 10), a Stormwind guard of faction 495 (an escortee's: friend
--          of every player, Alliance and Horde, foe of the monsters), no
--          gossip, no guard's script.
--
-- Each is a copy of an existing creature of the game, its own values set on
-- top, so that the file follows the columns of creature_template whatever the
-- version of the core. Its health is the one of a single player: the module
-- raises it with the players in the area (WowFates.Expedition.ParticipantScaling).
-- RegenHealth 0: it does not heal back to full between two fights.
--
-- Idempotent: the module's rows are deleted before being written again.

DELETE FROM `creature_template` WHERE `entry` BETWEEN 96100 AND 96299;
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

DELETE FROM `creature_template_model` WHERE `CreatureID` BETWEEN 96100 AND 96299;
INSERT INTO `creature_template_model` (`CreatureID`, `Idx`, `CreatureDisplayID`, `DisplayScale`, `Probability`, `VerifiedBuild`) VALUES
(96100, 0, 2357, 1.35, 1, 0),
(96200, 0, 3167, 1, 1, 0);

DELETE FROM `creature_template_locale` WHERE `entry` BETWEEN 96100 AND 96299;
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
(96200, 'ruRU', 'Часовой Вестбрука', '', 0);

-- Its blows: the Bandit's Snap Kick, more often, and the Bodyguard's Disarm.
DELETE FROM `smart_scripts` WHERE `entryorguid` BETWEEN 96100 AND 96299 AND `source_type` = 0;
INSERT INTO `smart_scripts` (`entryorguid`, `source_type`, `id`, `link`, `event_type`, `event_phase_mask`, `event_chance`, `event_flags`, `event_param1`, `event_param2`, `event_param3`, `event_param4`, `event_param5`, `event_param6`, `action_type`, `action_param1`, `action_param2`, `action_param3`, `action_param4`, `action_param5`, `action_param6`, `target_type`, `target_param1`, `target_param2`, `target_param3`, `target_param4`, `target_x`, `target_y`, `target_z`, `target_o`, `comment`) VALUES
(96100, 0, 0, 0, 0, 0, 100, 0, 3000, 5000, 6000, 9000, 0, 0, 11, 8646, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 'Defias Ringleader - In Combat - Cast Snap Kick'),
(96100, 0, 1, 0, 0, 0, 100, 0, 8000, 10000, 25000, 30000, 0, 0, 11, 6713, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 'Defias Ringleader - In Combat - Cast Disarm');
