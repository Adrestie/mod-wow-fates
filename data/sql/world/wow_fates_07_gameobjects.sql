-- mod-wow-fates — the objects of the Expeditions: what the gatherings ask to
-- pick up (960000 to 960099) and the altars of the invocations (960100 to
-- 960199). Gameobjects 960000 to 960999 (blocks 96000-96399 of ID_RANGES.md: a
-- module's gameobject templates are its numbers times ten).
--
--   960000  Kul Tiras Supply Crate: Kul Tiras Supplies (Durotar, level 7).
--   960001  Kolkar War Tent: The Kolkar War Camp (Durotar, level 8), the
--           centaurs' own tent (display 2352, CentaurTent01), to burn,
--           as a gathering's object: the same lock and cast. A sabotage's
--           targets are buildings or engines of worth (tents, huts, wagons,
--           catapults), never a small fire.
--   960002  Voodoo Cage: Zalazane's Captives (Durotar, level 9), the bamboo
--           cage of Stranglethorn's trolls (display 6858, Cage01), a
--           gathering's object: its captive comes out (creature 96204).
--   960100  Ritual Altar: Harpy Ritual (Durotar, level 8).
--
-- Each is a goober (type 10) whose ScriptName hands the click to the module:
-- go_wow_fates_gather (consumable) counts it for the Expedition and the
-- object goes, nothing is looted, nothing goes in the bags. Its lock (Data0
-- 43, as many objects of the game's quests) asks for a one second kneeling
-- "Opening" (spell 6478), broken by moving or a blow: only one that ends
-- counts;
-- go_wow_fates_channel starts the channel of whoever clicks near it.
--
-- Idempotent: the module's rows are deleted before being written again.

DELETE FROM `gameobject_template` WHERE `entry` BETWEEN 960000 AND 960999;
INSERT INTO `gameobject_template` (`entry`, `type`, `displayId`, `name`, `IconName`, `castBarCaption`, `unk1`, `size`,
  `Data0`, `Data1`, `Data2`, `Data3`, `Data4`, `Data5`, `Data6`, `Data7`, `Data8`, `Data9`, `Data10`, `Data11`,
  `Data12`, `Data13`, `Data14`, `Data15`, `Data16`, `Data17`, `Data18`, `Data19`, `Data20`, `Data21`, `Data22`, `Data23`,
  `AIName`, `ScriptName`, `VerifiedBuild`) VALUES
(960000, 10, 336, 'Kul Tiras Supply Crate', '', '', '', 1,
  43, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  '', 'go_wow_fates_gather', 0),
(960001, 10, 2352, 'Kolkar War Tent', '', '', '', 1,
  43, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  '', 'go_wow_fates_gather', 0),
(960002, 10, 6858, 'Voodoo Cage', '', '', '', 1,
  43, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  '', 'go_wow_fates_gather', 0),
(960100, 10, 328, 'Ritual Altar', '', '', '', 1,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  '', 'go_wow_fates_channel', 0);

DELETE FROM `gameobject_template_locale` WHERE `entry` BETWEEN 960000 AND 960999;
INSERT INTO `gameobject_template_locale` (`entry`, `locale`, `name`, `castBarCaption`, `VerifiedBuild`) VALUES
(960000, 'frFR', 'Caisse de ravitaillement de Kul Tiras', '', 0),
(960000, 'deDE', 'Nachschubkiste von Kul Tiras', '', 0),
(960000, 'esES', 'Caja de suministros de Kul Tiras', '', 0),
(960000, 'esMX', 'Caja de suministros de Kul Tiras', '', 0),
(960000, 'ruRU', 'Ящик с припасами Кул-Тираса', '', 0),
(960001, 'frFR', 'Tente de guerre kolkar', '', 0),
(960001, 'deDE', 'Kriegszelt der Kolkar', '', 0),
(960001, 'esES', 'Tienda de guerra kolkar', '', 0),
(960001, 'esMX', 'Tienda de guerra kolkar', '', 0),
(960001, 'ruRU', 'Боевой шатёр колкаров', '', 0),
(960100, 'frFR', 'Autel du rituel', '', 0),
(960100, 'deDE', 'Ritualaltar', '', 0),
(960100, 'esES', 'Altar del ritual', '', 0),
(960100, 'esMX', 'Altar del ritual', '', 0),
(960100, 'ruRU', 'Ритуальный алтарь', '', 0),
(960002, 'frFR', 'Cage vaudou', '', 0),
(960002, 'deDE', 'Voodookäfig', '', 0),
(960002, 'esES', 'Jaula vudú', '', 0),
(960002, 'esMX', 'Jaula vudú', '', 0),
(960002, 'ruRU', 'Клетка вуду', '', 0);
