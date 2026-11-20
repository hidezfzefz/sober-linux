/* Ghidra decompilation of target routine Java_com_roblox_client_startup_MainGameActivity_nativeSetAssetPath @ 0x2b35d6d
 * REA evidence: ev_439b29f6ef315227952ac382be5de3af210057eaf8f25753f6b769c67041226b
 * Requested address: 0x2b35d6d
 * Target-derived data, not source. Do not execute.
 */


/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

void Java_com_roblox_client_startup_MainGameActivity_nativeSetAssetPath(undefined8 param_1)

{
  undefined1 *puVar1;
  byte bStack_48;
  undefined1 auStack_47 [15];
  undefined1 *puStack_38;
  long lStack_20;
  
  lStack_20 = ___stack_chk_guard;
  FUN_01fdcd1c(&bStack_48,param_1);
  puVar1 = puStack_38;
  if ((bStack_48 & 1) == 0) {
    puVar1 = auStack_47;
  }
  FUN_0214ffc0(puVar1);
  if ((bStack_48 & 1) != 0) {
    FUN_01bb4b03(puStack_38);
  }
  if (___stack_chk_guard != lStack_20) {
                    /* WARNING: Subroutine does not return */
    __stack_chk_fail();
  }
  return;
}

