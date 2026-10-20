-- mod-wow-fates — the objects of the Expeditions: what the gatherings ask to
-- pick up. Gameobjects 960000 to 960999 (tranche 96 of ID_RANGES.md: a
-- module's gameobject templates are its tranche times ten).
--
--   960000  Kul Tiras Supply Crate: Kul Tiras Supplies (Durotar, level 7).
--
-- Each is a goober (type 10, consumable) whose ScriptName go_wow_fates_gather
-- hands the click to the module: it counts for the Expedition and the object
-- goes; nothing is looted, nothing goes in the bags.
--
-- Idempotent: the module's rows are deleted before being written again.

DELETE FROM `gameobject_template` WHERE `entry` BETWEEN 960000 AND 960999;
INSERT INTO `gameobject_template` (`entry`, `type`, `displayId`, `name`, `IconName`, `castBarCaption`, `unk1`, `size`,
  `Data0`, `Data1`, `Data2`, `Data3`, `Data4`, `Data5`, `Data6`, `Data7`, `Data8`, `Data9`, `Data10`, `Data11`,
  `Data12`, `Data13`, `Data14`, `Data15`, `Data16`, `Data17`, `Data18`, `Data19`, `Data20`, `Data21`, `Data22`, `Data23`,
  `AIName`, `ScriptName`, `VerifiedBuild`) VALUES
(960000, 10, 336, 'Kul Tiras Supply Crate', '', '', '', 1,
  0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  '', 'go_wow_fates_gather', 0);

DELETE FROM `gameobject_template_locale` WHERE `entry` BETWEEN 960000 AND 960999;
INSERT INTO `gameobject_template_locale` (`entry`, `locale`, `name`, `castBarCaption`, `VerifiedBuild`) VALUES
(960000, 'frFR', 'Caisse de ravitaillement de Kul Tiras', '', 0),
(960000, 'deDE', 'Nachschubkiste von Kul Tiras', '', 0),
(960000, 'esES', 'Caja de suministros de Kul Tiras', '', 0),
(960000, 'esMX', 'Caja de suministros de Kul Tiras', '', 0),
(960000, 'ruRU', 'Ящик с припасами Кул-Тираса', '', 0);
