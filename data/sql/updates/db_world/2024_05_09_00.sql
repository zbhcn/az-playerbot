-- DB update 2024_05_04_00 -> 2024_05_09_00
SET @REFIDONE = 35100,
@REFIDTWO     = 35101,
@REFIDTHREE   = 35102,
@TANZAR       = 22699,
@KRAZ         = 22790,
@ASHLI        = 22797,
@HARKOR       = 22968;

DELETE FROM `gameobject_loot_template` WHERE `Entry` IN (@TANZAR, @KRAZ, @ASHLI, @HARKOR);
INSERT INTO `gameobject_loot_template` (`Entry`, `Item`, `Reference`, `Chance`, `QuestRequired`, `LootMode`, `GroupId`, `MinCount`, `MaxCount`, `Comment`) VALUES
(@TANZAR, @REFIDONE,   @REFIDONE,   100, 0, 1, 0, 1, 1, 'Tanzar\'s Trunk - First Hostage Loot'),
(@TANZAR, @REFIDTWO,   @REFIDTWO,   100, 0, 1, 0, 1, 1, 'Tanzar\'s Trunk - Second Hostage Loot'),
(@TANZAR, @REFIDTHREE, @REFIDTHREE, 100, 0, 1, 0, 1, 1, 'Tanzar\'s Trunk - Third Hostage Loot'),
(@KRAZ,   @REFIDONE,   @REFIDONE,   100, 0, 1, 0, 1, 1, 'Kraz\'s Package - First Hostage Loot'),
(@KRAZ,   @REFIDTWO,   @REFIDTWO,   100, 0, 1, 0, 1, 1, 'Kraz\'s Package - Second Hostage Loot'),
(@KRAZ,   @REFIDTHREE, @REFIDTHREE, 100, 0, 1, 0, 1, 1, 'Kraz\'s Package - Third Hostage Loot'),
(@ASHLI,  @REFIDONE,   @REFIDONE,   100, 0, 1, 0, 1, 1, 'Ashli\'s Bag - First Hostage Loot'),
(@ASHLI,  @REFIDTWO,   @REFIDTWO,   100, 0, 1, 0, 1, 1, 'Ashli\'s Bag - Second Hostage Loot'),
(@ASHLI,  @REFIDTHREE, @REFIDTHREE, 100, 0, 1, 0, 1, 1, 'Ashli\'s Bag - Third Hostage Loot'),
(@HARKOR, @REFIDONE,   @REFIDONE,   100, 0, 1, 0, 1, 1, 'Harkor\'s Satchel - First Hostage Loot'),
(@HARKOR, @REFIDTWO,   @REFIDTWO,   100, 0, 1, 0, 1, 1, 'Harkor\'s Satchel - Second Hostage Loot'),
(@HARKOR, @REFIDTHREE, @REFIDTHREE, 100, 0, 1, 0, 1, 1, 'Harkor\'s Satchel - Third Hostage Loot');

DELETE FROM `reference_loot_template` WHERE `Entry` IN (@REFIDONE, @REFIDTWO, @REFIDTHREE);
INSERT INTO `reference_loot_template` (`Entry`, `Item`, `Reference`, `Chance`, `QuestRequired`, `LootMode`, `GroupId`, `MinCount`, `MaxCount`, `Comment`) VALUES
(@REFIDONE,   33480, 0, 0, 0, 1, 1, 1, 1, 'Zul\'Aman - First Hostage Loot - Cord of Braided Troll Hair'),
(@REFIDONE,   33481, 0, 0, 0, 1, 1, 1, 1, 'Zul\'Aman - First Hostage Loot - Pauldrons of Stone Resolve'),
(@REFIDONE,   33483, 0, 0, 0, 1, 1, 1, 1, 'Zul\'Aman - First Hostage Loot - Life-step Belt'),
(@REFIDONE,   33489, 0, 0, 0, 1, 1, 1, 1, 'Zul\'Aman - First Hostage Loot - Mantle of Ill Intent'),
(@REFIDONE,   33590, 0, 0, 0, 1, 1, 1, 1, 'Zul\'Aman - First Hostage Loot - Cloak of Fiends'),
(@REFIDONE,   33591, 0, 0, 0, 1, 1, 1, 1, 'Zul\'Aman - First Hostage Loot - Shadowcaster\'s Drape'),
(@REFIDONE,   33805, 0, 0, 0, 1, 1, 1, 1, 'Zul\'Aman - First Hostage Loot - Shadowhunter\'s Treads'),
(@REFIDONE,   33971, 0, 0, 0, 1, 1, 1, 1, 'Zul\'Aman - First Hostage Loot - Elunite Imbued Leggings'),
(@REFIDTWO,   33490, 0, 0, 0, 1, 1, 1, 1, 'Zul\'Aman - Second Hostage Loot - Staff of Dark Mending'),
(@REFIDTWO,   33491, 0, 0, 0, 1, 1, 1, 1, 'Zul\'Aman - Second Hostage Loot - Tuskbreaker'),
(@REFIDTWO,   33492, 0, 0, 0, 1, 1, 1, 1, 'Zul\'Aman - Second Hostage Loot - Trollbane'),
(@REFIDTWO,   33493, 0, 0, 0, 1, 1, 1, 1, 'Zul\'Aman - Second Hostage Loot - Umbral Shiv'),
(@REFIDTWO,   33494, 0, 0, 0, 1, 1, 1, 1, 'Zul\'Aman - Second Hostage Loot - Amani Divining Staff'),
(@REFIDTWO,   33495, 0, 0, 0, 1, 1, 1, 1, 'Zul\'Aman - Second Hostage Loot - Rage'),
(@REFIDTHREE, 33496, 0, 0, 0, 1, 1, 1, 1, 'Zul\'Aman - Third Hostage Loot - Signet of Primal Wrath'),
(@REFIDTHREE, 33497, 0, 0, 0, 1, 1, 1, 1, 'Zul\'Aman - Third Hostage Loot - Mana Attuned Band'),
(@REFIDTHREE, 33498, 0, 0, 0, 1, 1, 1, 1, 'Zul\'Aman - Third Hostage Loot - Signet of the Quiet Forest'),
(@REFIDTHREE, 33499, 0, 0, 0, 1, 1, 1, 1, 'Zul\'Aman - Third Hostage Loot - Signet of the Last Defender'),
(@REFIDTHREE, 33500, 0, 0, 0, 1, 1, 1, 1, 'Zul\'Aman - Third Hostage Loot - Signet of Eternal Life');

DELETE FROM `conditions` WHERE (`SourceTypeOrReferenceId` = 10) AND (`SourceGroup` IN (@REFIDONE, @REFIDTWO, @REFIDTHREE, @REFIDFOUR)) AND (`ConditionTypeOrReference` = 13) AND (`ConditionValue1` = 8);
INSERT INTO `conditions` (`SourceTypeOrReferenceId`, `SourceGroup`, `SourceEntry`, `SourceId`, `ElseGroup`, `ConditionTypeOrReference`, `ConditionTarget`, `ConditionValue1`, `ConditionValue2`, `ConditionValue3`, `NegativeCondition`, `ErrorType`, `ErrorTextId`, `ScriptName`, `Comment`) VALUES
(10, @REFIDONE,   33480, 0, 0, 13, 0, 8, 1, 0, 0, 0, 0, '', 'Yield first hostage loot if first hostage has been rescued'),
(10, @REFIDONE,   33481, 0, 0, 13, 0, 8, 1, 0, 0, 0, 0, '', 'Yield first hostage loot if first hostage has been rescued'),
(10, @REFIDONE,   33483, 0, 0, 13, 0, 8, 1, 0, 0, 0, 0, '', 'Yield first hostage loot if first hostage has been rescued'),
(10, @REFIDONE,   33489, 0, 0, 13, 0, 8, 1, 0, 0, 0, 0, '', 'Yield first hostage loot if first hostage has been rescued'),
(10, @REFIDONE,   33590, 0, 0, 13, 0, 8, 1, 0, 0, 0, 0, '', 'Yield first hostage loot if first hostage has been rescued'),
(10, @REFIDONE,   33591, 0, 0, 13, 0, 8, 1, 0, 0, 0, 0, '', 'Yield first hostage loot if first hostage has been rescued'),
(10, @REFIDONE,   33805, 0, 0, 13, 0, 8, 1, 0, 0, 0, 0, '', 'Yield first hostage loot if first hostage has been rescued'),
(10, @REFIDONE,   33971, 0, 0, 13, 0, 8, 1, 0, 0, 0, 0, '', 'Yield first hostage loot if first hostage has been rescued'),
(10, @REFIDTWO,   33490, 0, 0, 13, 0, 8, 2, 0, 0, 0, 0, '', 'Yield second hostage loot if second hostage has been rescued'),
(10, @REFIDTWO,   33491, 0, 0, 13, 0, 8, 2, 0, 0, 0, 0, '', 'Yield second hostage loot if second hostage has been rescued'),
(10, @REFIDTWO,   33492, 0, 0, 13, 0, 8, 2, 0, 0, 0, 0, '', 'Yield second hostage loot if second hostage has been rescued'),
(10, @REFIDTWO,   33493, 0, 0, 13, 0, 8, 2, 0, 0, 0, 0, '', 'Yield second hostage loot if second hostage has been rescued'),
(10, @REFIDTWO,   33494, 0, 0, 13, 0, 8, 2, 0, 0, 0, 0, '', 'Yield second hostage loot if second hostage has been rescued'),
(10, @REFIDTWO,   33495, 0, 0, 13, 0, 8, 2, 0, 0, 0, 0, '', 'Yield second hostage loot if second hostage has been rescued'),
(10, @REFIDTHREE, 33496, 0, 1, 13, 0, 8, 3, 0, 0, 0, 0, '', 'Yield third hostage loot if third hostage has been rescued'),
(10, @REFIDTHREE, 33497, 0, 1, 13, 0, 8, 3, 0, 0, 0, 0, '', 'Yield third hostage loot if third hostage has been rescued'),
(10, @REFIDTHREE, 33498, 0, 1, 13, 0, 8, 3, 0, 0, 0, 0, '', 'Yield third hostage loot if third hostage has been rescued'),
(10, @REFIDTHREE, 33499, 0, 1, 13, 0, 8, 3, 0, 0, 0, 0, '', 'Yield third hostage loot if third hostage has been rescued'),
(10, @REFIDTHREE, 33500, 0, 1, 13, 0, 8, 3, 0, 0, 0, 0, '', 'Yield third hostage loot if third hostage has been rescued'),
(10, @REFIDTHREE, 33496, 0, 2, 13, 0, 8, 4, 0, 0, 0, 0, '', 'Yield third hostage loot if all hostages have been rescued'),
(10, @REFIDTHREE, 33497, 0, 2, 13, 0, 8, 4, 0, 0, 0, 0, '', 'Yield third hostage loot if all hostages have been rescued'),
(10, @REFIDTHREE, 33498, 0, 2, 13, 0, 8, 4, 0, 0, 0, 0, '', 'Yield third hostage loot if all hostages have been rescued'),
(10, @REFIDTHREE, 33499, 0, 2, 13, 0, 8, 4, 0, 0, 0, 0, '', 'Yield third hostage loot if all hostages have been rescued'),
(10, @REFIDTHREE, 33500, 0, 2, 13, 0, 8, 4, 0, 0, 0, 0, '', 'Yield third hostage loot if all hostages have been rescued');
