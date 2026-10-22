-- mod-wow-fates — the warlords' pools: the mechanics they use, ready blocks
-- (mod_wow_fates_mechanic), and who may lead each warlord Expedition
-- (mod_wow_fates_chief).
--
-- At each start a warlord Expedition draws its chief by weight. Every chief
-- (and an assault's leader, a boss support's elite) has from the start a
-- heavy blow on the tank, an area (a wave around him, burning ground), a
-- control (a charge, a leap, a fear, a stun, a knock back) and a change of
-- aggro (threat wiped, a player or the healer taken as his target): a known
-- figure of the zone (his name and model) keeps his own mechanics and draws
-- what he lacks; a made-up one draws them all, then mechanics_min to
-- mechanics_max more among a buff, a cast to interrupt and his band (one at
-- most). By weight, none twice, one of each group, none above the
-- Expedition's level, none for the open sky under a roof nor the other way.
-- His phases (75, 50, 25 % of his health) bring the others, one each, and
-- make all come back sooner. He says a line before the heavy ones (a bubble,
-- in each player's language). The spells are the game's creatures', their
-- damage times the players to the power Scaling.Damage; one at a player falls
-- on more of them as they are more. A ready pool rather than SmartAI mixed on
-- the fly: no broken combination.
--
-- Ids 1 to 999 of the mechanics and 1 to 99 of the chiefs are the module's.
--
-- Idempotent: the module's rows are deleted before being written again.

DELETE FROM `mod_wow_fates_mechanic` WHERE `id` BETWEEN 1 AND 999;
INSERT INTO `mod_wow_fates_mechanic` (`id`, `name`, `kind`, `spell`, `target`, `cooldown_min`, `cooldown_max`, `weight`, `min_level`, `place`, `group`, `role`, `comment`) VALUES
(30, 'Mortal Strike', 0, 13737, 0, 10, 14, 10, 1, 0, 0, 1, 'a heavy blow, healing on the tank cut'),
(31, 'Cleave', 0, 15496, 0, 8, 12, 8, 1, 0, 0, 1, 'the tank and who stands by him'),
(32, 'Strike', 0, 11976, 0, 6, 9, 8, 1, 0, 0, 1, ''),
(2, 'Pierce Armor', 0, 6016, 0, 15, 22, 6, 1, 0, 0, 1, 'his victim''s armour down'),
(8, 'Sunder Armor', 0, 11971, 0, 10, 14, 6, 1, 0, 0, 1, ''),
(5, 'Thunderclap', 0, 8078, 2, 14, 20, 10, 1, 0, 3, 2, 'a shockwave around him, slowing'),
(33, 'War Stomp', 0, 11876, 2, 16, 22, 8, 1, 0, 3, 2, 'a stomp around him, stunning'),
(34, 'Fire Nova', 0, 11969, 2, 15, 20, 6, 1, 0, 3, 2, 'a ring of fire around him'),
(35, 'Trample', 0, 15550, 2, 14, 18, 6, 1, 0, 3, 2, 'all around him trampled'),
(13, 'Flamestrike', 0, 2120, 4, 16, 22, 8, 1, 0, 6, 2, 'burning ground under players, to step out of'),
(6, 'Demoralizing Shout', 0, 13730, 2, 25, 30, 4, 1, 0, 4, 2, 'the players around him weaker'),
(1, 'Head Butt', 0, 6730, 0, 12, 18, 8, 1, 0, 0, 3, 'his victim stunned'),
(3, 'Rushing Charge', 0, 6268, 3, 18, 25, 8, 1, 0, 1, 3, 'a charge at the farthest player'),
(12, 'Leap', 2, 8078, 3, 20, 28, 6, 1, 1, 1, 3, 'a leap at the farthest player, a shockwave when he lands; under the sky only'),
(16, 'Psychic Scream', 0, 8122, 2, 25, 32, 6, 1, 0, 8, 3, 'the players around him flee'),
(7, 'Knockdown', 0, 5164, 0, 14, 20, 8, 1, 0, 0, 3, 'his victim down'),
(36, 'Knock Away', 0, 10101, 0, 18, 24, 8, 1, 0, 9, 3, 'his victim thrown back'),
(37, 'Uppercut', 0, 10966, 0, 15, 20, 6, 1, 0, 9, 3, 'his victim thrown up and back'),
(38, 'Frost Nova', 0, 122, 2, 20, 26, 6, 1, 0, 8, 3, 'the players around him frozen'),
(9, 'Disarm', 0, 6713, 0, 20, 26, 5, 1, 0, 0, 3, ''),
(10, 'Tendon Rip', 0, 3604, 0, 15, 20, 5, 1, 0, 0, 3, 'his victim slowed'),
(4, 'Enrage', 0, 8599, 2, 0, 0, 8, 1, 0, 2, 4, 'once, at its phase'),
(39, 'Frenzy', 0, 8269, 2, 25, 35, 6, 1, 0, 2, 4, 'faster blows a while'),
(14, 'Power Word: Shield', 0, 17, 2, 25, 30, 5, 1, 0, 7, 4, 'a shield on himself'),
(15, 'Renew', 0, 139, 2, 22, 28, 5, 1, 0, 7, 4, 'a heal on himself, over time'),
(40, 'Fireball', 0, 9053, 0, 10, 14, 8, 1, 0, 10, 7, 'a cast at his victim, to interrupt'),
(41, 'Chain Lightning', 0, 12058, 0, 12, 16, 8, 1, 0, 10, 7, 'a cast jumping from player to player, to interrupt'),
(42, 'Shadow Bolt Volley', 0, 9081, 0, 14, 18, 6, 1, 0, 10, 7, 'bolts at every player near, to interrupt'),
(43, 'Threat wiped', 3, 0, 0, 30, 40, 6, 1, 0, 11, 5, 'all his threat wiped: whoever hits him next'),
(44, 'Turns on a player', 3, 0, 1, 20, 28, 8, 1, 0, 11, 5, 'a player at random, not his victim, taken as his target'),
(45, 'Goes for the healer', 3, 0, 2, 25, 35, 6, 1, 0, 11, 5, 'the one who healed most taken as his target'),
(11, 'Call the band', 1, 0, 0, 35, 45, 8, 1, 0, 5, 6, 'his band comes, from around him'),
(20, 'Gouge', 0, 1776, 0, 14, 18, 0, 1, 0, 0, 3, 'Morgaine the Sly'),
(21, 'Poison', 0, 11918, 0, 12, 16, 0, 1, 0, 0, 1, 'Morgaine the Sly'),
(22, 'Evasion', 0, 5277, 2, 30, 40, 0, 1, 0, 0, 4, 'Morgaine the Sly'),
(23, 'Shadow Bolt', 0, 20791, 0, 6, 9, 0, 1, 0, 0, 7, 'Gazz''uz, to interrupt'),
(24, 'Corruption', 0, 172, 1, 12, 16, 0, 1, 0, 0, 1, 'Gazz''uz, on players'),
(25, 'Demon Skin', 0, 20798, 2, 0, 0, 0, 1, 0, 0, 4, 'Gazz''uz, once'),
(26, 'Fear', 0, 5782, 1, 20, 28, 0, 1, 0, 0, 3, 'Gazz''uz'),
(27, 'Lightning Bolt', 0, 403, 0, 7, 10, 0, 1, 0, 0, 7, 'Zalazane, to interrupt'),
(28, 'Shrink', 0, 7289, 1, 15, 20, 0, 1, 0, 0, 3, 'Zalazane'),
(29, 'Healing Wave', 0, 332, 2, 18, 24, 0, 1, 0, 0, 4, 'Zalazane, on himself');

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
