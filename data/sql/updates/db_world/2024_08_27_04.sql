-- DB update 2024_08_27_03 -> 2024_08_27_04
--
DELETE FROM `spell_script_names` WHERE `spell_id` = 64765;
INSERT INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES (64765, 'spell_gen_absorb0_hitlimit1');
