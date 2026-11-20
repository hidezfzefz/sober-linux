/* Ghidra decompilation of target routine FUN_02283e0c @ 0x2283e0c
 * REA evidence: ev_439b29f6ef315227952ac382be5de3af210057eaf8f25753f6b769c67041226b
 * Requested address: 0x2283e8b
 * Target-derived data, not source. Do not execute.
 */


/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

void FUN_02283e0c(long *param_1)

{
  long *plVar1;
  long lVar2;
  long *plVar3;
  int iVar4;
  long *plVar5;
  undefined4 uVar6;
  byte *pbVar7;
  undefined8 uVar8;
  long *plVar9;
  long *plVar10;
  long *plVar11;
  bool bVar12;
  ulong auStack_f8 [3];
  undefined4 uStack_e0;
  undefined4 uStack_dc;
  undefined4 uStack_d8;
  undefined4 uStack_d4;
  undefined4 uStack_d0;
  ulong auStack_c8 [5];
  long *plStack_a0;
  undefined4 uStack_98;
  undefined1 uStack_90;
  undefined7 uStack_8f;
  int iStack_88;
  char cStack_80;
  ulong uStack_78;
  undefined1 uStack_70;
  long *plStack_68;
  uint uStack_60;
  char cStack_58;
  undefined8 uStack_50;
  long lStack_48;
  undefined8 uStack_40;
  long lStack_38;
  
  lStack_38 = ___stack_chk_guard;
  uStack_78 = uStack_78 & 0xffffffffffffff00;
  uStack_70 = 0;
  uStack_90 = 0;
  cStack_80 = '\0';
  if (DAT_06f98af8 == '\0') {
    FUN_020a022a(&uStack_78);
    uStack_78 = FUN_01fa9418();
    uStack_70 = 1;
    FUN_020a0254(&uStack_90);
    pbVar7 = (byte *)(**(code **)(*param_1 + 0x40))(param_1);
    if ((*pbVar7 & 1) == 0) {
      pbVar7 = pbVar7 + 1;
    }
    else {
      pbVar7 = *(byte **)(pbVar7 + 0x10);
    }
    FUN_01fa8937(pbVar7);
    FUN_020f8a36("CacheableContentProvider::workerThreadFunction");
  }
  uVar6 = FUN_01c6cfc8((int)param_1[0x24]);
  uStack_d8 = 0;
  uStack_d4 = 0;
  uStack_d0 = 0;
  auStack_f8[2] = 0;
  uStack_e0 = 0;
  uStack_dc = 0;
  auStack_f8[0] = 0;
  auStack_f8[1] = 0;
  auStack_c8[0] = 0;
  auStack_c8[1] = 0;
  auStack_c8[2] = 0;
  auStack_c8[3] = 0;
  auStack_c8[4] = 0;
  plStack_a0 = (long *)0x0;
  uStack_98 = 0;
  if ((char)param_1[0x2b] == '\0') {
    plVar3 = param_1 + 0x21;
    plVar1 = param_1 + 0x25;
    do {
      FUN_02038318(param_1 + 0x1f);
      lStack_48 = (long)param_1 + 0xf9;
      while (((char)param_1[0x2b] == '\0' && (param_1[0x23] == 0))) {
        plStack_68 = (long *)((ulong)plStack_68 & 0xffffffffffffff00);
        cStack_58 = '\0';
        if (DAT_06f98af8 == '\0') {
          FUN_020a0e3a(&plStack_68);
        }
        uVar8 = FUN_02a932d0();
        FUN_02b66dac(param_1 + 0x20,&lStack_48,uVar8);
        if (cStack_58 != '\0') {
          if (uStack_60 == 0) {
            FUN_01fab164(plStack_68);
          }
          else {
            FUN_01fa954c();
          }
        }
      }
      FUN_0200cfb8(&lStack_48);
      lStack_48 = 500;
      uStack_40 = FUN_02a93280();
      while( true ) {
        uStack_60 = CONCAT31(uStack_60._1_3_,1);
        plStack_68 = param_1 + 0x1f;
        FUN_02038318();
        if (((char)param_1[0x2b] != '\0') || (param_1[0x23] == 0)) break;
        lVar2 = *plVar3;
        FUN_02aab370(auStack_f8,(ulong *)(lVar2 + 0x28));
        FUN_01fe52ae(&uStack_e0,lVar2 + 0x40);
        if (auStack_f8 != (ulong *)(lVar2 + 0x28)) {
          uStack_d0 = *(undefined4 *)(lVar2 + 0x50);
          FUN_02aab370(auStack_c8,lVar2 + 0x58);
        }
        auStack_c8[3] = *(undefined8 *)(lVar2 + 0x70);
        FUN_01fe52ae(auStack_c8 + 4,lVar2 + 0x78);
        uStack_98 = *(undefined4 *)(lVar2 + 0x88);
        lVar2 = *plVar3;
        FUN_0200cc5e(plVar3,lVar2);
        FUN_049243e7(lVar2 + 0x20);
        FUN_01bb4b03(lVar2);
        plVar9 = (long *)FUN_02110358(plVar1,auStack_f8);
        plVar5 = (long *)plVar9[1];
        plVar11 = plVar9;
        if ((long *)plVar9[1] == (long *)0x0) {
          do {
            plVar10 = (long *)plVar11[2];
            bVar12 = (long *)*plVar10 != plVar11;
            plVar11 = plVar10;
          } while (bVar12);
        }
        else {
          do {
            plVar10 = plVar5;
            plVar5 = (long *)*plVar10;
          } while ((long *)*plVar10 != (long *)0x0);
        }
        if ((long *)*plVar1 == plVar9) {
          *plVar1 = (long)plVar10;
        }
        param_1[0x27] = param_1[0x27] + -1;
        FUN_0200cc98(param_1[0x26],plVar9);
        if ((*(byte *)(plVar9 + 4) & 1) != 0) {
          FUN_01bb4b03(plVar9[6]);
        }
        FUN_01bb4b03(plVar9);
        FUN_02affca0(&plStack_68);
        FUN_04921896(&plStack_68,param_1,auStack_f8);
        iVar4 = (int)plStack_68;
        if ((uStack_60 & 1) != 0) {
          FUN_01bb4b03(uStack_50);
        }
        if (iVar4 == 0) {
          (**(code **)(*param_1 + 0x270))(param_1,auStack_f8);
        }
        else {
          LOCK();
          *(int *)(param_1 + 0x17) = (int)param_1[0x17] + -1;
          UNLOCK();
        }
        FUN_049241e0(&lStack_48);
      }
      FUN_02affca0(&plStack_68);
    } while ((char)param_1[0x2b] == '\0');
  }
  if (DAT_06f98af8 == '\0') {
    FUN_0696975e();
  }
  plVar3 = plStack_a0;
  if (plStack_a0 != (long *)0x0) {
    LOCK();
    plVar1 = plStack_a0 + 1;
    lVar2 = *plVar1;
    *plVar1 = *plVar1 + -1;
    UNLOCK();
    if (lVar2 == 0) {
      (**(code **)(*plStack_a0 + 0x10))(plStack_a0);
      FUN_02aa9910(plVar3);
    }
  }
  if ((auStack_c8[0] & 1) != 0) {
    FUN_01bb4b03(auStack_c8[2]);
  }
  plVar3 = (long *)CONCAT44(uStack_d4,uStack_d8);
  if (plVar3 != (long *)0x0) {
    LOCK();
    plVar1 = plVar3 + 1;
    lVar2 = *plVar1;
    *plVar1 = *plVar1 + -1;
    UNLOCK();
    if (lVar2 == 0) {
      (**(code **)(*plVar3 + 0x10))(plVar3);
      FUN_02aa9910(plVar3);
    }
  }
  if ((auStack_f8[0] & 1) != 0) {
    FUN_01bb4b03(auStack_f8[2]);
  }
  FUN_01c6cfc8(uVar6);
  if (cStack_80 != '\0') {
    if (iStack_88 == 0) {
      FUN_01fab164(CONCAT71(uStack_8f,uStack_90));
    }
    else {
      FUN_01fa954c();
    }
  }
  FUN_0492426c(&uStack_78);
  if (___stack_chk_guard != lStack_38) {
                    /* WARNING: Subroutine does not return */
    __stack_chk_fail();
  }
  return;
}

