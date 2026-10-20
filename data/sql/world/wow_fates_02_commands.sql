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
('fates score', 2, 'Syntax: .fates score [$level]\n\nPower score of the selected player (or yours), measured without the Synced debuff, against the reference score of $level: by default the level of his Expedition, else his own. Below 1, the ratio is what Synced would leave him.');
