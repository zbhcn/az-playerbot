-- DB update 2024_06_16_00 -> 2024_06_16_01
--
DELETE FROM `spell_custom_attr` WHERE `spell_id`=36478;
INSERT INTO `spell_custom_attr` (`spell_id`, `attributes`) VALUES (36478, 4194304);
