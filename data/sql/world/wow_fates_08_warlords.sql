-- mod-wow-fates — the warlords' pools: the mechanics they use, ready blocks
-- (mod_wow_fates_mechanic), and who may lead each warlord Expedition
-- (mod_wow_fates_chief).
--
-- At each start a warlord Expedition draws its chief by weight. A known figure
-- of the zone (his name and model, his own creature_template copy) uses his own
-- mechanics, in their order. A made-up one draws two to four
-- (mechanics_min, mechanics_max) from the pool by weight: none twice, one of
-- each group at most, none above the Expedition's level, none for the open
-- sky under a roof nor the other way. The first is his from the start, then
-- one more at each phase (75, 50, 25 % of his health). A ready pool rather than
-- SmartAI mixed on the fly: no broken combination.
--
-- Ids 1 to 999 of the mechanics and 1 to 99 of the chiefs are the module's.
--
-- Idempotent: the module's rows are deleted before being written again.

DELETE FROM `mod_wow_fates_mechanic` WHERE `id` BETWEEN 1 AND 999;
INSERT INTO `mod_wow_fates_mechanic` (`id`, `name`, `kind`, `spell`, `target`, `cooldown_min`, `cooldown_max`, `weight`, `min_level`, `place`, `group`, `comment`) VALUES
(1, 'Head Butt', 0, 6730, 0, 12, 18, 10, 1, 0, 0, 'a stun on his victim'),
(2, 'Pierce Armor', 0, 6016, 0, 15, 22, 8, 1, 0, 0, 'his victim''s armour down'),
(3, 'Rushing Charge', 0, 6268, 3, 18, 25, 8, 1, 0, 1, 'a charge at the farthest player'),
(4, 'Enrage', 0, 8599, 2, 0, 0, 8, 1, 0, 2, 'once, at its phase'),
(5, 'Thunderclap', 0, 8078, 2, 14, 20, 10, 6, 0, 3, 'a shockwave around him'),
(6, 'Demoralizing Shout', 0, 13730, 2, 25, 30, 6, 1, 0, 4, 'the players around him weaker'),
(7, 'Knockdown', 0, 5164, 0, 14, 20, 8, 1, 0, 0, 'his victim down'),
(8, 'Sunder Armor', 0, 11971, 0, 10, 14, 6, 1, 0, 0, ''),
(9, 'Disarm', 0, 6713, 0, 20, 26, 6, 1, 0, 0, ''),
(10, 'Tendon Rip', 0, 3604, 0, 15, 20, 6, 1, 0, 0, 'his victim slowed'),
(11, 'Call the band', 1, 0, 0, 35, 45, 8, 1, 0, 5, 'his band comes, from around him'),
(12, 'Leap', 2, 8078, 3, 20, 28, 6, 8, 1, 1, 'a leap at the farthest player, a shockwave when he lands; under the sky only'),
(13, 'Flamestrike', 0, 2120, 4, 16, 22, 6, 10, 0, 6, 'a burning patch of ground under a player'),
(14, 'Power Word: Shield', 0, 17, 2, 25, 30, 5, 1, 0, 7, 'a shield on himself'),
(15, 'Renew', 0, 139, 2, 22, 28, 5, 1, 0, 7, 'a heal on himself, over time'),
(16, 'Psychic Scream', 0, 8122, 2, 25, 32, 5, 8, 0, 8, 'the players around him flee'),
(20, 'Gouge', 0, 1776, 0, 14, 18, 0, 1, 0, 0, 'Morgaine the Sly'),
(21, 'Poison', 0, 11918, 0, 12, 16, 0, 1, 0, 0, 'Morgaine the Sly'),
(22, 'Evasion', 0, 5277, 2, 30, 40, 0, 1, 0, 0, 'Morgaine the Sly'),
(23, 'Shadow Bolt', 0, 20791, 0, 6, 9, 0, 1, 0, 0, 'Gazz''uz'),
(24, 'Corruption', 0, 172, 1, 12, 16, 0, 1, 0, 0, 'Gazz''uz'),
(25, 'Demon Skin', 0, 20798, 2, 0, 0, 0, 1, 0, 0, 'Gazz''uz, once'),
(26, 'Fear', 0, 5782, 1, 20, 28, 0, 1, 0, 0, 'Gazz''uz'),
(27, 'Lightning Bolt', 0, 403, 0, 7, 10, 0, 1, 0, 0, 'Zalazane'),
(28, 'Shrink', 0, 7289, 1, 15, 20, 0, 1, 0, 0, 'Zalazane'),
(29, 'Healing Wave', 0, 332, 2, 18, 24, 0, 1, 0, 0, 'Zalazane, on himself');

DELETE FROM `mod_wow_fates_chief` WHERE `id` BETWEEN 1 AND 99;
INSERT INTO `mod_wow_fates_chief` (`id`, `expedition`, `creature`, `adds_entry`, `weight`, `mechanics`, `hide_entry`, `comment`) VALUES
(1, 14, 96102, 478, 1, '1,2,3,4', 448, 'Hogger, known: his own blows, the Riverpaw his band'),
(2, 14, 96105, 116, 1, '20,21,11,22', 99, 'Morgaine the Sly, known: her own tricks, Defias Bandits her band'),
(3, 14, 96103, 478, 1, '', 0, 'Grizzlefang the Ravager, made up: a Riverpaw gnoll, his mechanics drawn'),
(4, 14, 96104, 116, 1, '', 0, 'Captain Varrek Blackthorn, made up: a Defias captain, his mechanics drawn'),
(5, 15, 96106, 3207, 1, '27,28,11,29', 3205, 'Zalazane, known: his voodoo, his Hexed Trolls'),
(6, 15, 96107, 3197, 1, '23,24,25,26', 3204, 'Gazz''uz, known: his shadow magic, the Burning Blade Fanatics'),
(7, 15, 96108, 3120, 1, '', 0, 'Khan Ruuzak, made up: a Kolkar centaur khan, his mechanics drawn'),
(8, 15, 96109, 3114, 1, '', 0, 'Bloodtusk the Gorer, made up: a Razormane quilboar, his mechanics drawn');
