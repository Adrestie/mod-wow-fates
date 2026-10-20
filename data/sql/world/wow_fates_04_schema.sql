-- mod-wow-fates — the Expeditions: what they are, where they appear and what
-- they give. Read at startup and by `.fates reload`.
--
-- Idempotent: the file may be replayed, it creates nothing that already exists.

CREATE TABLE IF NOT EXISTS `mod_wow_fates_expedition` (
  `id` INT UNSIGNED NOT NULL,
  `name` VARCHAR(100) NOT NULL DEFAULT '' COMMENT 'English; the other languages in mod_wow_fates_expedition_locale',
  `level` TINYINT UNSIGNED NOT NULL DEFAULT 1 COMMENT 'Synced level: whoever is stronger than this level in the area is brought down to it',
  `objective_type` TINYINT UNSIGNED NOT NULL DEFAULT 1 COMMENT '1 = kill monsters in the area',
  `objective_entry` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'creature_template.entry counted; 0 = any monster',
  `objective_count` INT UNSIGNED NOT NULL DEFAULT 10 COMMENT 'kills to complete it',
  `summon_entry` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'creature_template.entry the Expedition brings; 0 = the monsters already there',
  `summon_count` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'of them alive at once',
  `summon_respawn` INT UNSIGNED NOT NULL DEFAULT 10 COMMENT 'seconds between two of them',
  `radius` FLOAT NOT NULL DEFAULT 60 COMMENT 'yards around the spawn point',
  `duration` INT UNSIGNED NOT NULL DEFAULT 900 COMMENT 'seconds to complete it',
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
