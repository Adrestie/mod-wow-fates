-- mod-wow-fates — the help of the commands (.help fates).
--
-- Idempotent: the file may be replayed.

DELETE FROM `command` WHERE `name` = 'fates' OR `name` LIKE 'fates %';
INSERT INTO `command` (`name`, `security`, `help`) VALUES
('fates', 2, 'Syntax: .fates $subcommand\n\nThe Expeditions of mod-wow-fates and their Synced debuff.'),
('fates list', 2, 'Syntax: .fates list\n\nThe active Expeditions: where, progress, time left, players inside and contributors.'),
('fates defs', 2, 'Syntax: .fates defs\n\nEvery Expedition loaded: level, spawn points, weight in the rotation, and whether it is active, cooling down or ready.'),
('fates start', 2, 'Syntax: .fates start $id [$spawn]\n\nStarts Expedition $id now, at spawn point $spawn or at one of its points at random, even past WowFates.Expedition.MaxActive.'),
('fates stop', 2, 'Syntax: .fates stop $id\n\nEnds Expedition $id as failed: no rewards, its monsters go.'),
('fates complete', 2, 'Syntax: .fates complete $id\n\nEnds Expedition $id as complete: its active participants are rewarded.'),
('fates reload', 3, 'Syntax: .fates reload\n\nStops every active Expedition, without rewards, and reloads the Expeditions, spawn points and rewards from the world database.'),
('fates score', 2, 'Syntax: .fates score [$player] [$level]\n\nPower score of $player (else the selected player, else yours), measured without the Synced debuff, against the reference score of $level: by default the level of his Expedition, else his own. Below 1, the ratio is what Synced would leave him. Works from the console with a name.'),
('fates check', 3, 'Syntax: .fates check [$id]\n\nChecks every spawn point of Expedition $id (else of all), as the module takes them: its centre on dry ground, the monsters'' points and the wave edges, the objects of its type placed one after the other, an escort''s route and a rescue''s way out, each walked as a player would (navigation mesh, no jump, no cliff, no water). Loads their grids. One line per point: ok, or what fails.'),
('fates synced', 2, 'Syntax: .fates synced\n\nThe players in the area of an Expedition: its level, the ratio last applied to them, and whether they wear the Synced debuff.');
