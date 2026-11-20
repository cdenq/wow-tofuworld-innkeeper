-- wow-tofuworld-innkeeper: Kreemo <Tofu Innkeeper>
-- Changes: creature_template 900001 (innkeeper fields cloned from Amisi Azuregaze 28687, who is not changed) and creature_template_model 900001 (display 7346, the model of Innkeeper Fizzgrimble 7733 in Gadgetzan, who is not changed). No spawn: the spawn is a Tofu custom SQL file (creature GUIDs 7000100 to 7000119).
-- Schema: acore_world. Applied by the worldserver's updater as a module file (state MODULE). Re-runnable: DELETE, then INSERT with fixed values.
-- Needs: a worldserver restart (or .reload creature_template plus a fresh spawn). New entry, so no client Cache deletion is needed; after any later change to name or subname, players delete the Cache folder.
-- npcflag 65665 = gossip 1 + vendor 128 + innkeeper 65536 (the vendor and hearthstone handlers check these flags). gossip_menu_id 0: the script builds every menu. faction 35: friendly to everyone.
-- Check: SELECT entry, name, subname, npcflag, faction, ScriptName FROM acore_world.creature_template WHERE entry = 900001; -- expected 900001, Kreemo, Tofu Innkeeper, 65665, 35, npc_tofu_innkeeper
--        SELECT CreatureID, Idx, CreatureDisplayID FROM acore_world.creature_template_model WHERE CreatureID = 900001; -- expected 900001, 0, 7346

DELETE FROM `creature_template` WHERE `entry` = 900001;
INSERT INTO `creature_template` (`entry`, `difficulty_entry_1`, `difficulty_entry_2`, `difficulty_entry_3`, `KillCredit1`, `KillCredit2`, `name`, `subname`, `IconName`, `gossip_menu_id`, `minlevel`, `maxlevel`, `exp`, `faction`, `npcflag`, `speed_walk`, `speed_run`, `speed_swim`, `speed_flight`, `detection_range`, `rank`, `dmgschool`, `DamageModifier`, `BaseAttackTime`, `RangeAttackTime`, `BaseVariance`, `RangeVariance`, `unit_class`, `unit_flags`, `unit_flags2`, `dynamicflags`, `family`, `type`, `type_flags`, `lootid`, `pickpocketloot`, `skinloot`, `PetSpellDataId`, `VehicleId`, `mingold`, `maxgold`, `AIName`, `MovementType`, `HoverHeight`, `HealthModifier`, `ManaModifier`, `ArmorModifier`, `ExperienceModifier`, `RacialLeader`, `movementId`, `RegenHealth`, `CreatureImmunitiesId`, `flags_extra`, `ScriptName`, `VerifiedBuild`) VALUES
(900001, 0, 0, 0, 0, 0, 'Kreemo', 'Tofu Innkeeper', '', 0, 80, 80, 2, 35, 65665, 1, 1.14286, 1, 1, 20, 0, 0, 1, 2000, 2000, 1, 1, 1, 32768, 2048, 0, 0, 7, 4096, 0, 0, 0, 0, 0, 0, 0, '', 0, 1, 1, 1, 1, 1, 0, 0, 1, 0, 0, 'npc_tofu_innkeeper', 0);

DELETE FROM `creature_template_model` WHERE `CreatureID` = 900001;
INSERT INTO `creature_template_model` (`CreatureID`, `Idx`, `CreatureDisplayID`, `DisplayScale`, `Probability`, `VerifiedBuild`) VALUES
(900001, 0, 7346, 1, 1, 0);
