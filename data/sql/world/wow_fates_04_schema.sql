-- mod-wow-fates — the Expeditions: what they are, where they appear and what
-- they give. Read at startup and by `.fates reload`.
--
-- Idempotent: the file may be replayed, it creates nothing that already exists.

CREATE TABLE IF NOT EXISTS `mod_wow_fates_expedition` (
  `id` INT UNSIGNED NOT NULL,
  `name` VARCHAR(100) NOT NULL DEFAULT '' COMMENT 'English; the other languages in mod_wow_fates_expedition_locale',
  `level` TINYINT UNSIGNED NOT NULL DEFAULT 1 COMMENT 'Synced level: whoever is stronger than this level in the area is brought down to it',
  `objective_type` TINYINT UNSIGNED NOT NULL DEFAULT 1 COMMENT '1 = kill monsters in the area; 2 = assault and boss, 3 = gathering, 4 = defence, 5 = invocation, 6 = wave support, 7 = boss support (mod_wow_fates_objective)',
  `objective_entry` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'creature_template.entry counted; 0 = any monster',
  `objective_count` INT UNSIGNED NOT NULL DEFAULT 10 COMMENT 'kills to complete it',
  `summon_entry` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'creature_template.entry the Expedition brings; 0 = the monsters already there',
  `summon_count` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'of them alive at once',
  `summon_respawn` INT UNSIGNED NOT NULL DEFAULT 10 COMMENT 'seconds between two of them',
  `radius` FLOAT NOT NULL DEFAULT 60 COMMENT 'yards around the spawn point',
  `duration` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'seconds without activity before it goes (never while players are at it); 0 = WowFates.Rotation.IdleExpire',
  `cooldown` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'seconds before it may appear again; 0 = WowFates.Expedition.Cooldown',
  `weight` INT UNSIGNED NOT NULL DEFAULT 1 COMMENT 'chance in the rotation; 0 = only by .fates start',
  `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `comment` VARCHAR(255) NOT NULL DEFAULT '',
  PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='WowFates: the Expeditions';

CREATE TABLE IF NOT EXISTS `mod_wow_fates_expedition_locale` (
  `id` INT UNSIGNED NOT NULL COMMENT 'mod_wow_fates_expedition.id',
  `locale` VARCHAR(4) NOT NULL COMMENT 'frFR, deDE, esES, ruRU...',
  `name` VARCHAR(100) NOT NULL DEFAULT '',
  PRIMARY KEY (`id`, `locale`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='WowFates: the names of the Expeditions in other languages';

CREATE TABLE IF NOT EXISTS `mod_wow_fates_spawn` (
  `id` INT UNSIGNED NOT NULL,
  `expedition` INT UNSIGNED NOT NULL COMMENT 'mod_wow_fates_expedition.id',
  `map` SMALLINT UNSIGNED NOT NULL COMMENT 'a continent: Eastern Kingdoms 0, Kalimdor 1, Outland 530, Northrend 571',
  `x` FLOAT NOT NULL,
  `y` FLOAT NOT NULL,
  `z` FLOAT NOT NULL,
  `o` FLOAT NOT NULL DEFAULT 0,
  `comment` VARCHAR(255) NOT NULL DEFAULT '',
  PRIMARY KEY (`id`),
  KEY `expedition` (`expedition`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='WowFates: the points where each Expedition may appear, one picked at random';

CREATE TABLE IF NOT EXISTS `mod_wow_fates_objective` (
  `expedition` INT UNSIGNED NOT NULL COMMENT 'mod_wow_fates_expedition.id',
  `boss_entry` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'assault: creature_template.entry of the leader, out in the centre',
  `boss_at` TINYINT UNSIGNED NOT NULL DEFAULT 70 COMMENT 'assault: percent the kills bring before the leader appears (10 to 95); its health is the rest',
  `gather_entry` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'gathering: gameobject_template.entry to pick up (ScriptName go_wow_fates_gather); objective_count of them',
  `gather_count` INT UNSIGNED NOT NULL DEFAULT 8 COMMENT 'gathering: of them out at once, at random points of the area',
  `gather_respawn` INT UNSIGNED NOT NULL DEFAULT 5 COMMENT 'gathering: seconds before one taken is put back',
  `ally_entry` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'defence: creature_template.entry of the ally in the centre (faction 495: friend of all players, foe of monsters); the Expedition fails when it dies',
  `hold_time` INT UNSIGNED NOT NULL DEFAULT 240 COMMENT 'defence: seconds to hold out, counted from the first player in the area',
  `wave_interval` INT UNSIGNED NOT NULL DEFAULT 40 COMMENT 'waves: seconds between two; their monsters are summon_entry',
  `wave_size` INT UNSIGNED NOT NULL DEFAULT 3 COMMENT 'waves: monsters of the first one, for one player (more with the players, as the objective)',
  `wave_growth` INT UNSIGNED NOT NULL DEFAULT 1 COMMENT 'waves: monsters added to each next one',
  `channel_entry` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'invocation: gameobject_template.entry of the altar to channel at (ScriptName go_wow_fates_channel); hold_time is the channel asked',
  `ally_count` TINYINT UNSIGNED NOT NULL DEFAULT 1 COMMENT 'wave support: allies (ally_entry) around the centre, 1 to 10; the Expedition fails when all are dead',
  `wave_count` INT UNSIGNED NOT NULL DEFAULT 5 COMMENT 'wave support: waves to push back; the next one comes wave_interval seconds after',
  `comment` VARCHAR(255) NOT NULL DEFAULT '',
  PRIMARY KEY (`expedition`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='WowFates: the settings of the objectives other than kills, one row per Expedition';

CREATE TABLE IF NOT EXISTS `mod_wow_fates_reward` (
  `id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
  `expedition` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'mod_wow_fates_expedition.id; 0 = every Expedition, on top of its own rows',
  `min_level` TINYINT UNSIGNED NOT NULL DEFAULT 1 COMMENT 'level of the player',
  `max_level` TINYINT UNSIGNED NOT NULL DEFAULT 255,
  `money` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'copper',
  `xp` INT UNSIGNED NOT NULL DEFAULT 0,
  `item` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'item_template.entry; by mail when the bags are full',
  `item_count` INT UNSIGNED NOT NULL DEFAULT 1,
  `hidden` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '1 = shown as a red ? until given',
  `comment` VARCHAR(255) NOT NULL DEFAULT '',
  PRIMARY KEY (`id`),
  KEY `expedition` (`expedition`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='WowFates: the rewards by level of the player; every row that matches is given, by participation';

-- `hidden` came after the first version of the table: added when missing.
SET @wow_fates_hidden := (SELECT COUNT(*) FROM information_schema.COLUMNS
  WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'mod_wow_fates_reward' AND COLUMN_NAME = 'hidden');
SET @wow_fates_sql := IF(@wow_fates_hidden = 0,
  'ALTER TABLE `mod_wow_fates_reward` ADD COLUMN `hidden` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT ''1 = shown as a red ? until given'' AFTER `item_count`',
  'DO 0');
PREPARE wow_fates_stmt FROM @wow_fates_sql;
EXECUTE wow_fates_stmt;
DEALLOCATE PREPARE wow_fates_stmt;

-- The columns of the invocation and the wave support came after the first
-- version of mod_wow_fates_objective: added when missing.
SET @wow_fates_col := (SELECT COUNT(*) FROM information_schema.COLUMNS
  WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'mod_wow_fates_objective' AND COLUMN_NAME = 'channel_entry');
SET @wow_fates_sql := IF(@wow_fates_col = 0,
  'ALTER TABLE `mod_wow_fates_objective` ADD COLUMN `channel_entry` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT ''invocation: gameobject_template.entry of the altar to channel at (ScriptName go_wow_fates_channel); hold_time is the channel asked'' AFTER `wave_growth`',
  'DO 0');
PREPARE wow_fates_stmt FROM @wow_fates_sql;
EXECUTE wow_fates_stmt;
DEALLOCATE PREPARE wow_fates_stmt;
SET @wow_fates_col := (SELECT COUNT(*) FROM information_schema.COLUMNS
  WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'mod_wow_fates_objective' AND COLUMN_NAME = 'ally_count');
SET @wow_fates_sql := IF(@wow_fates_col = 0,
  'ALTER TABLE `mod_wow_fates_objective` ADD COLUMN `ally_count` TINYINT UNSIGNED NOT NULL DEFAULT 1 COMMENT ''wave support: allies (ally_entry) around the centre, 1 to 10; the Expedition fails when all are dead'' AFTER `channel_entry`',
  'DO 0');
PREPARE wow_fates_stmt FROM @wow_fates_sql;
EXECUTE wow_fates_stmt;
DEALLOCATE PREPARE wow_fates_stmt;
SET @wow_fates_col := (SELECT COUNT(*) FROM information_schema.COLUMNS
  WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'mod_wow_fates_objective' AND COLUMN_NAME = 'wave_count');
SET @wow_fates_sql := IF(@wow_fates_col = 0,
  'ALTER TABLE `mod_wow_fates_objective` ADD COLUMN `wave_count` INT UNSIGNED NOT NULL DEFAULT 5 COMMENT ''wave support: waves to push back; the next one comes wave_interval seconds after'' AFTER `ally_count`',
  'DO 0');
PREPARE wow_fates_stmt FROM @wow_fates_sql;
EXECUTE wow_fates_stmt;
DEALLOCATE PREPARE wow_fates_stmt;

-- The texts of an Expedition in the world's words (the map's tooltip, the
-- tracker, the banner of the waves), English in mod_wow_fates_expedition, the
-- other languages in mod_wow_fates_expedition_locale: added when missing.
SET @wow_fates_col := (SELECT COUNT(*) FROM information_schema.COLUMNS
  WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'mod_wow_fates_expedition' AND COLUMN_NAME = 'objective_text');
SET @wow_fates_sql := IF(@wow_fates_col = 0,
  'ALTER TABLE `mod_wow_fates_expedition` ADD COLUMN `objective_text` VARCHAR(255) NOT NULL DEFAULT '''' COMMENT ''what the map''''s tooltip says of the objective, in the world''''s words; empty = a text made from the type'' AFTER `name`',
  'DO 0');
PREPARE wow_fates_stmt FROM @wow_fates_sql;
EXECUTE wow_fates_stmt;
DEALLOCATE PREPARE wow_fates_stmt;
SET @wow_fates_col := (SELECT COUNT(*) FROM information_schema.COLUMNS
  WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'mod_wow_fates_expedition' AND COLUMN_NAME = 'tracker_text');
SET @wow_fates_sql := IF(@wow_fates_col = 0,
  'ALTER TABLE `mod_wow_fates_expedition` ADD COLUMN `tracker_text` VARCHAR(255) NOT NULL DEFAULT '''' COMMENT ''the tracker''''s line; a wave support''''s may hold {}/{} (pushed back, all); empty = a text made from the type'' AFTER `objective_text`',
  'DO 0');
PREPARE wow_fates_stmt FROM @wow_fates_sql;
EXECUTE wow_fates_stmt;
DEALLOCATE PREPARE wow_fates_stmt;
SET @wow_fates_col := (SELECT COUNT(*) FROM information_schema.COLUMNS
  WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'mod_wow_fates_expedition' AND COLUMN_NAME = 'wave_text');
SET @wow_fates_sql := IF(@wow_fates_col = 0,
  'ALTER TABLE `mod_wow_fates_expedition` ADD COLUMN `wave_text` VARCHAR(255) NOT NULL DEFAULT '''' COMMENT ''the banner of each wave ("The Gnolls attack!"); empty = Wave N'' AFTER `tracker_text`',
  'DO 0');
PREPARE wow_fates_stmt FROM @wow_fates_sql;
EXECUTE wow_fates_stmt;
DEALLOCATE PREPARE wow_fates_stmt;
SET @wow_fates_col := (SELECT COUNT(*) FROM information_schema.COLUMNS
  WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'mod_wow_fates_expedition_locale' AND COLUMN_NAME = 'objective_text');
SET @wow_fates_sql := IF(@wow_fates_col = 0,
  'ALTER TABLE `mod_wow_fates_expedition_locale` ADD COLUMN `objective_text` VARCHAR(255) NOT NULL DEFAULT '''' COMMENT ''what the map''''s tooltip says of the objective, in the world''''s words; empty = a text made from the type'' AFTER `name`',
  'DO 0');
PREPARE wow_fates_stmt FROM @wow_fates_sql;
EXECUTE wow_fates_stmt;
DEALLOCATE PREPARE wow_fates_stmt;
SET @wow_fates_col := (SELECT COUNT(*) FROM information_schema.COLUMNS
  WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'mod_wow_fates_expedition_locale' AND COLUMN_NAME = 'tracker_text');
SET @wow_fates_sql := IF(@wow_fates_col = 0,
  'ALTER TABLE `mod_wow_fates_expedition_locale` ADD COLUMN `tracker_text` VARCHAR(255) NOT NULL DEFAULT '''' COMMENT ''the tracker''''s line; a wave support''''s may hold {}/{} (pushed back, all); empty = a text made from the type'' AFTER `objective_text`',
  'DO 0');
PREPARE wow_fates_stmt FROM @wow_fates_sql;
EXECUTE wow_fates_stmt;
DEALLOCATE PREPARE wow_fates_stmt;
SET @wow_fates_col := (SELECT COUNT(*) FROM information_schema.COLUMNS
  WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'mod_wow_fates_expedition_locale' AND COLUMN_NAME = 'wave_text');
SET @wow_fates_sql := IF(@wow_fates_col = 0,
  'ALTER TABLE `mod_wow_fates_expedition_locale` ADD COLUMN `wave_text` VARCHAR(255) NOT NULL DEFAULT '''' COMMENT ''the banner of each wave ("The Gnolls attack!"); empty = Wave N'' AFTER `tracker_text`',
  'DO 0');
PREPARE wow_fates_stmt FROM @wow_fates_sql;
EXECUTE wow_fates_stmt;
DEALLOCATE PREPARE wow_fates_stmt;

-- A boss support's pack: the bar's percents that call it (added when missing).
SET @wow_fates_col := (SELECT COUNT(*) FROM information_schema.COLUMNS
  WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'mod_wow_fates_objective' AND COLUMN_NAME = 'boss_waves');
SET @wow_fates_sql := IF(@wow_fates_col = 0,
  'ALTER TABLE `mod_wow_fates_objective` ADD COLUMN `boss_waves` VARCHAR(64) NOT NULL DEFAULT ''25,50,75'' COMMENT ''boss support: the bar''''s percents that call the elite''''s pack (summon_entry, as the waves)'' AFTER `wave_count`',
  'DO 0');
PREPARE wow_fates_stmt FROM @wow_fates_sql;
EXECUTE wow_fates_stmt;
DEALLOCATE PREPARE wow_fates_stmt;
