/* Ghidra decompilation of target routine Java_com_roblox_engine_jni_NativeSettingsInterface_nativeSetHttpClientProxy @ 0x200dac2
 * REA evidence: ev_439b29f6ef315227952ac382be5de3af210057eaf8f25753f6b769c67041226b
 * Requested address: 0x200dac2
 * Target-derived data, not source. Do not execute.
 */


/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

void Java_com_roblox_engine_jni_NativeSettingsInterface_nativeSetHttpClientProxy
               (undefined8 param_1,undefined8 param_2,undefined8 param_3,undefined8 param_4)

{
  long lVar1;
  byte bVar2;
  undefined1 *puVar3;
  long *plStack_60;
  byte bStack_58;
  undefined1 auStack_57 [15];
  undefined1 *puStack_48;
  long lStack_30;
  
  lStack_30 = ___stack_chk_guard;
  FUN_01fdcd1c(&bStack_58,param_1);
  FUN_01fe380c(&plStack_60);
  lVar1 = plStack_60[3];
  FUN_02aab370(lVar1 + 0xae8,&bStack_58);
  *(short *)(lVar1 + 0xb00) = (short)param_4;
  (**(code **)(*plStack_60 + 0x10))(plStack_60);
  bVar2 = (byte)((ulong)DAT_06f4e1d0 >> 8);
  if (2 < bVar2 && 5 < (byte)DAT_06f4e1d0) {
    puVar3 = puStack_48;
    if ((bStack_58 & 1) == 0) {
      puVar3 = auStack_57;
    }
    thunk_FUN_01fec34d(DAT_06f4e1d0,PTR_s_Android_06f4e1d8,"[FLog::Android] Set proxy host to %s",
                       puVar3);
    bVar2 = (byte)((ulong)DAT_06f4e1d0 >> 8);
  }
  if (2 < bVar2 && 5 < (byte)DAT_06f4e1d0) {
    FUN_01fe2480(DAT_06f4e1d0,PTR_s_Android_06f4e1d8,"[FLog::Android] Set proxy port to %ld",param_4
                 ,0,0,0,0);
  }
  if ((bStack_58 & 1) != 0) {
    FUN_01bb4b03(puStack_48);
  }
  if (___stack_chk_guard != lStack_30) {
                    /* WARNING: Subroutine does not return */
    __stack_chk_fail();
  }
  return;
}

