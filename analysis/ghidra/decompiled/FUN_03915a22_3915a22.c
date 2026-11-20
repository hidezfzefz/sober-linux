/* Ghidra decompilation of target routine FUN_03915a22 @ 0x3915a22
 * REA evidence: ev_439b29f6ef315227952ac382be5de3af210057eaf8f25753f6b769c67041226b
 * Requested address: 0x3915c20
 * Target-derived data, not source. Do not execute.
 */


/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

void FUN_03915a22(int *param_1,long *param_2)

{
  int *piVar1;
  long *plVar2;
  undefined1 uVar3;
  int iVar4;
  long lVar5;
  long *plVar6;
  undefined8 uVar7;
  undefined8 uVar8;
  long lVar9;
  long lVar10;
  bool bVar11;
  byte abStack_c8 [16];
  undefined8 uStack_b8;
  undefined8 uStack_b0;
  int iStack_a8;
  int iStack_a4;
  int iStack_a0;
  int iStack_9c;
  undefined8 uStack_98;
  undefined8 uStack_90;
  undefined8 uStack_88;
  undefined8 uStack_80;
  long *plStack_78;
  long *plStack_70;
  long *plStack_68;
  long *plStack_60;
  undefined2 uStack_58;
  undefined8 uStack_50;
  undefined8 uStack_48;
  long lStack_40;
  long lStack_38;
  
  lStack_38 = ___stack_chk_guard;
  uStack_58 = 2;
  uStack_50 = 0;
  uStack_48 = 0;
  lStack_40 = 0;
  iVar4 = *param_1;
  if (iVar4 == 2) {
    uStack_58 = (undefined2)param_1[6];
    lVar10 = 0x20;
  }
  else if (iVar4 == 1) {
    uStack_58 = (undefined2)param_1[0xe];
    lVar10 = 0x40;
  }
  else {
    if (iVar4 != 0) goto LAB_03915aa7;
    uStack_58 = (undefined2)param_1[0x16];
    lVar10 = 0x60;
  }
  FUN_02aab370(&uStack_50,lVar10 + (long)param_1);
LAB_03915aa7:
  if ((int)param_2[4] == 1) {
    if (((long *)*param_2 == (long *)0x0) ||
       (lVar10 = (**(code **)(*(long *)*param_2 + 0x10))(), lVar10 == 0)) {
      lVar10 = lStack_40;
      if ((uStack_50 & 1) == 0) {
        lVar10 = (long)&uStack_50 + 1;
      }
      FUN_01fee9dd(3,
                   "ContentProvider:getContent() in generate HSR data does not have response for %s"
                   ,lVar10);
      if (7 < *(ulong *)(param_1 + 0x4a)) {
        FUN_01bb776a(abStack_c8,"Failed to fetch mesh data");
        FUN_0332d84a(param_1 + 0x44,0,abStack_c8);
        if ((abStack_c8[0] & 1) != 0) {
          FUN_01bb4b03(uStack_b8);
        }
      }
    }
    else {
      param_2 = (long *)*param_2;
      uVar7 = (**(code **)(*param_2 + 0x18))(param_2);
      uVar8 = (**(code **)(*param_2 + 0x10))(param_2);
      FUN_038252fe(&plStack_68,uVar7,uVar8,0);
      lVar10 = *plStack_68;
      lVar5 = plStack_68[1];
      if (lVar5 == lVar10) {
        lVar9 = lStack_40;
        if ((uStack_50 & 1) == 0) {
          lVar9 = (long)&uStack_50 + 1;
        }
        FUN_01fee9dd(3,"Mesh in generate HSR data for %s does not have vertices",lVar9);
        if (7 < *(ulong *)(param_1 + 0x4a)) {
          FUN_01bb776a(abStack_c8,"Mesh does not have vertices");
          FUN_0332d84a(param_1 + 0x44,0,abStack_c8);
          if ((abStack_c8[0] & 1) != 0) {
            FUN_01bb4b03(uStack_b8);
          }
        }
        bVar11 = false;
      }
      else {
        plVar6 = *(long **)(param_1 + 2);
        iVar4 = *param_1;
        plStack_78 = plStack_68;
        plStack_70 = plStack_60;
        if (plStack_60 != (long *)0x0) {
          LOCK();
          plStack_60[1] = plStack_60[1] + 1;
          UNLOCK();
        }
        FUN_01fe2af8((long)iVar4 * 0x10 + *plVar6,&plStack_78);
        plVar2 = plStack_70;
        LOCK();
        piVar1 = (int *)((long)plVar6 + 0x1c);
        iVar4 = *piVar1;
        *piVar1 = *piVar1 + 1;
        UNLOCK();
        bVar11 = plVar6[1] - *plVar6 >> 4 == (long)(iVar4 + 1);
        if (plStack_70 != (long *)0x0) {
          LOCK();
          plVar6 = plStack_70 + 1;
          lVar9 = *plVar6;
          *plVar6 = *plVar6 + -1;
          UNLOCK();
          if (lVar9 == 0) {
            (**(code **)(*plStack_70 + 0x10))(plStack_70);
            FUN_02aa9910(plVar2);
          }
        }
      }
      plVar6 = plStack_60;
      if (plStack_60 != (long *)0x0) {
        LOCK();
        plVar2 = plStack_60 + 1;
        lVar9 = *plVar2;
        *plVar2 = *plVar2 + -1;
        UNLOCK();
        if (lVar9 == 0) {
          (**(code **)(*plStack_60 + 0x10))(plStack_60);
          FUN_02aa9910(plVar6);
        }
      }
      if ((bool)(lVar5 != lVar10 & bVar11)) {
        plVar6 = *(long **)(param_1 + 4);
        plStack_68 = *(long **)(param_1 + 2);
        plStack_60 = *(long **)(param_1 + 4);
        if (plVar6 != (long *)0x0) {
          LOCK();
          plVar6[1] = plVar6[1] + 1;
          UNLOCK();
        }
        lVar10 = *(long *)(param_1 + 0x20);
        uStack_88 = *(undefined8 *)(param_1 + 0x1e);
        uStack_80 = *(undefined8 *)(param_1 + 0x20);
        if (lVar10 != 0) {
          LOCK();
          *(long *)(lVar10 + 0x10) = *(long *)(lVar10 + 0x10) + 1;
          UNLOCK();
        }
        lVar5 = *(long *)(param_1 + 0x24);
        uStack_98 = *(undefined8 *)(param_1 + 0x22);
        uStack_90 = *(undefined8 *)(param_1 + 0x24);
        if (lVar5 != 0) {
          LOCK();
          *(long *)(lVar5 + 0x10) = *(long *)(lVar5 + 0x10) + 1;
          UNLOCK();
        }
        lVar9 = *(long *)(param_1 + 0x28);
        iStack_a8 = param_1[0x26];
        iStack_a4 = param_1[0x27];
        iStack_a0 = param_1[0x28];
        iStack_9c = param_1[0x29];
        if (lVar9 != 0) {
          LOCK();
          *(long *)(lVar9 + 0x10) = *(long *)(lVar9 + 0x10) + 1;
          UNLOCK();
        }
        iVar4 = param_1[0x2a];
        uVar3 = *(undefined1 *)((long)param_1 + 0xa9);
        uStack_b0 = 0;
        FUN_02b34544(abStack_c8,param_1 + 0x44);
        FUN_03912a5e(&plStack_68,param_1 + 6,param_1 + 0xe,param_1 + 0x16,&uStack_88,&uStack_98,
                     &iStack_a8,(char)iVar4,uVar3,param_1 + 0x2b,param_1 + 0x37,abStack_c8);
        FUN_02affcd6(abStack_c8);
        if (lVar9 != 0) {
          FUN_02aa9910(lVar9);
        }
        if (lVar5 != 0) {
          FUN_02aa9910();
        }
        if (lVar10 != 0) {
          FUN_02aa9910();
        }
        if (plVar6 != (long *)0x0) {
          LOCK();
          plVar2 = plVar6 + 1;
          lVar10 = *plVar2;
          *plVar2 = *plVar2 + -1;
          UNLOCK();
          if (lVar10 == 0) {
            (**(code **)(*plVar6 + 0x10))(plVar6);
            FUN_02aa9910(plVar6);
          }
        }
      }
    }
  }
  else {
    lVar10 = lStack_40;
    if ((uStack_50 & 1) == 0) {
      lVar10 = (long)&uStack_50 + 1;
    }
    FUN_01fee9dd(3,"ContentProvider:getContent() in generate HSR data failed for %s",lVar10);
    if (7 < *(ulong *)(param_1 + 0x4a)) {
      FUN_01bb776a(abStack_c8,"Failed to fetch mesh data");
      FUN_0332d84a(param_1 + 0x44,0,abStack_c8);
      if ((abStack_c8[0] & 1) != 0) {
        FUN_01bb4b03(uStack_b8);
      }
    }
  }
  if ((uStack_50 & 1) != 0) {
    FUN_01bb4b03(lStack_40);
  }
  if (___stack_chk_guard != lStack_38) {
                    /* WARNING: Subroutine does not return */
    __stack_chk_fail();
  }
  return;
}

