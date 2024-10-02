-- DB update 2024_08_27_05 -> 2024_08_28_00
DELETE FROM `smart_scripts` WHERE (`entryorguid` = 3584) AND (`source_type` = 0) AND (`id` IN (2, 3, 9, 10));
INSERT INTO `smart_scripts` (`entryorguid`, `source_type`, `id`, `link`, `event_type`, `event_phase_mask`, `event_chance`, `event_flags`, `event_param1`, `event_param2`, `event_param3`, `event_param4`, `event_param5`, `event_param6`, `action_type`, `action_param1`, `action_param2`, `action_param3`, `action_param4`, `action_param5`, `action_param6`, `target_type`, `target_param1`, `target_param2`, `target_param3`, `target_param4`, `target_x`, `target_y`, `target_z`, `target_o`, `comment`) VALUES
(3584, 0, 2, 3, 40, 0, 100, 0, 20, 3584, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 12, 1, 0, 0, 0, 0, 0, 0, 0, 'Therylune - On WP 20 - Say Line 1'),
(3584, 0, 3, 0, 61, 0, 100, 512, 0, 0, 0, 0, 0, 0, 15, 945, 0, 0, 0, 0, 0, 12, 1, 0, 0, 0, 0, 0, 0, 0, 'Therylune - On WP 20 - Quest Credit'),
(3584, 0, 9, 10, 61, 0, 100, 512, 0, 0, 0, 0, 0, 0, 83, 2, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 'Therylune - On Quest \'Therylune\'s Escape\' Taken - Remove npc flag'),
(3584, 0, 10, 0, 61, 0, 100, 0, 0, 0, 0, 0, 0, 0, 64, 1, 0, 0, 0, 0, 0, 16, 0, 0, 0, 0, 0, 0, 0, 0, 'Therylune - On Quest \'Therylune\'s Escape\' Taken - Store Targetlist');
