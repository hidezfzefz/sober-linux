/* Ghidra decompilation of target routine FUN_041366c6 @ 0x41366c6
 * REA evidence: ev_439b29f6ef315227952ac382be5de3af210057eaf8f25753f6b769c67041226b
 * Requested address: 0x4136981
 * Target-derived data, not source. Do not execute.
 */


/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

void FUN_041366c6(long param_1,byte *param_2)

{
  long *plVar1;
  long lVar2;
  undefined8 uVar3;
  long *plVar4;
  ulong uVar5;
  undefined4 uStack_108;
  undefined4 uStack_104;
  undefined4 uStack_100;
  undefined4 uStack_fc;
  undefined4 uStack_f8;
  undefined4 uStack_f4;
  undefined4 uStack_f0;
  undefined4 uStack_ec;
  undefined *puStack_e0;
  ulong uStack_d8;
  byte *pbStack_d0;
  undefined8 uStack_c8;
  long *plStack_c0;
  undefined1 auStack_b8 [24];
  undefined8 uStack_a0;
  undefined8 uStack_98;
  long *plStack_90;
  undefined8 uStack_88;
  long *plStack_80;
  undefined8 uStack_78;
  long *plStack_70;
  byte abStack_60 [16];
  undefined8 uStack_50;
  byte abStack_48 [16];
  undefined8 uStack_38;
  long lStack_30;
  
  lStack_30 = ___stack_chk_guard;
  if (5 < (byte)(DAT_06f84910 >> 8) && 5 < (byte)DAT_06f84910) {
    FUN_01ff2e02(DAT_06f84910,PTR_s_GenerationService_06f84918,6,
                 "[DFLog::GenerationService] Beginning RBXM download from: {}",0x3b,param_2);
  }
  if ((*param_2 & 1) == 0) {
    uVar5 = (ulong)(*param_2 >> 1);
  }
  else {
    uVar5 = *(ulong *)(param_2 + 8);
  }
  if (uVar5 == 0) {
    if ((DAT_06f84910 & 0xfe00) != 0 && 5 < (byte)DAT_06f84910) {
      uStack_d8 = 0;
      pbStack_d0 = abStack_48;
      FUN_01ff2e72(DAT_06f84910,PTR_s_GenerationService_06f84918,2,
                   "[DFLog::GenerationService] Empty RBXM download url",0x32,1,0,pbStack_d0._0_4_);
    }
    FUN_01bb776a(abStack_60,"Unable to download generated model");
    FUN_04053680(param_1,abStack_60);
    if ((abStack_60[0] & 1) != 0) {
      FUN_01bb4b03(uStack_50);
    }
  }
  else {
    FUN_01bb776a(&uStack_d8,&DAT_006542b9);
    FUN_0414a4ea(&uStack_78,&uStack_d8,param_2);
    if ((uStack_d8 & 1) != 0) {
      FUN_01bb4b03(uStack_c8);
    }
    FUN_040d0181(&uStack_88,&uStack_d8);
    uVar3 = *(undefined8 *)(param_1 + 0x18);
    uStack_98 = uStack_78;
    plStack_90 = plStack_70;
    uStack_78 = 0;
    plStack_70 = (long *)0x0;
    FUN_01bb776a(abStack_48,"ModelGenerationJob::downloadRbxm");
    FUN_021fa6c3(&uStack_d8,param_1);
    uStack_c8 = uStack_88;
    plStack_c0 = plStack_80;
    uStack_88 = 0;
    plStack_80 = (long *)0x0;
    uStack_a0 = 0;
    FUN_02b02cec(auStack_b8,0x20,8,&uStack_d8,&PTR_FUN_06c234d8,&LAB_02b9b360);
    FUN_021fa6c3(&uStack_108,param_1);
    uStack_f8 = uStack_108;
    uStack_f4 = uStack_104;
    uStack_f0 = uStack_100;
    uStack_ec = uStack_fc;
    puStack_e0 = &DAT_06c234fe;
    FUN_0414a600(uVar3,&uStack_98,abStack_48,auStack_b8);
    FUN_02affcd6(&uStack_f8);
    FUN_02affcd6(auStack_b8);
    plVar4 = plStack_c0;
    if (plStack_c0 != (long *)0x0) {
      LOCK();
      plVar1 = plStack_c0 + 1;
      lVar2 = *plVar1;
      *plVar1 = *plVar1 + -1;
      UNLOCK();
      if (lVar2 == 0) {
        (**(code **)(*plStack_c0 + 0x10))(plStack_c0);
        FUN_02aa9910(plVar4);
      }
    }
    if (pbStack_d0 != (byte *)0x0) {
      FUN_02aa9910();
    }
    if ((abStack_48[0] & 1) != 0) {
      FUN_01bb4b03(uStack_38);
    }
    plVar4 = plStack_90;
    if (plStack_90 != (long *)0x0) {
      LOCK();
      plVar1 = plStack_90 + 1;
      lVar2 = *plVar1;
      *plVar1 = *plVar1 + -1;
      UNLOCK();
      if (lVar2 == 0) {
        (**(code **)(*plStack_90 + 0x10))(plStack_90);
        FUN_02aa9910(plVar4);
      }
    }
    plVar4 = plStack_80;
    if (plStack_80 != (long *)0x0) {
      LOCK();
      plVar1 = plStack_80 + 1;
      lVar2 = *plVar1;
      *plVar1 = *plVar1 + -1;
      UNLOCK();
      if (lVar2 == 0) {
        (**(code **)(*plStack_80 + 0x10))(plStack_80);
        FUN_02aa9910(plVar4);
      }
    }
    plVar4 = plStack_70;
    if (plStack_70 != (long *)0x0) {
      LOCK();
      plVar1 = plStack_70 + 1;
      lVar2 = *plVar1;
      *plVar1 = *plVar1 + -1;
      UNLOCK();
      if (lVar2 == 0) {
        (**(code **)(*plStack_70 + 0x10))(plStack_70);
        FUN_02aa9910(plVar4);
      }
    }
  }
  if (___stack_chk_guard == lStack_30) {
    return;
  }
                    /* WARNING: Subroutine does not return */
  __stack_chk_fail();
}

