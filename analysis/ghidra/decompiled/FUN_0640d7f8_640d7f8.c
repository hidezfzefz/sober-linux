/* Ghidra decompilation of target routine FUN_0640d7f8 @ 0x640d7f8
 * REA evidence: ev_439b29f6ef315227952ac382be5de3af210057eaf8f25753f6b769c67041226b
 * Requested address: 0x640e3b0
 * Target-derived data, not source. Do not execute.
 */


/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

int * FUN_0640d7f8(int *param_1,ulong *param_2,int param_3,undefined8 param_4,undefined8 param_5,
                  undefined8 param_6)

{
  long lVar1;
  int iVar2;
  undefined *puVar3;
  undefined *puVar4;
  byte bVar5;
  uint uVar6;
  char cVar7;
  undefined8 *puVar8;
  long *plVar9;
  long *plVar10;
  long *plVar11;
  undefined8 *puVar12;
  undefined8 *puVar13;
  long lVar14;
  long lVar15;
  ulong uVar16;
  long lVar17;
  ulong uVar18;
  char *pcVar19;
  ulong uVar20;
  undefined8 uVar21;
  long *plVar22;
  undefined1 auVar23 [16];
  long *plStack_1b0;
  undefined8 *puStack_178;
  long *plStack_170;
  undefined8 uStack_168;
  undefined8 uStack_160;
  undefined8 uStack_158;
  long *plStack_150;
  undefined8 uStack_148;
  ulong uStack_140;
  ulong uStack_138;
  undefined4 uStack_130;
  undefined4 uStack_12c;
  undefined4 uStack_128;
  undefined4 uStack_124;
  ulong uStack_120;
  int iStack_118;
  undefined8 uStack_110;
  ulong uStack_108;
  long lStack_100;
  undefined8 uStack_f8;
  long *plStack_f0;
  long lStack_e8;
  undefined8 uStack_d8;
  undefined8 uStack_d0;
  long lStack_c8;
  undefined *puStack_c0;
  undefined8 uStack_b8;
  undefined8 uStack_b0;
  byte bStack_a8;
  undefined8 uStack_98;
  byte bStack_90;
  undefined8 uStack_80;
  byte bStack_78;
  undefined8 uStack_68;
  undefined8 uStack_48;
  long lStack_38;
  
  lStack_38 = ___stack_chk_guard;
  uStack_138 = param_2[2];
  uStack_148 = *param_2;
  uStack_140 = param_2[1];
  param_2[2] = 0;
  *param_2 = 0;
  param_2[1] = 0;
  uStack_120 = param_2[5];
  uStack_130 = (uint)param_2[3];
  uStack_12c = *(undefined4 *)((long)param_2 + 0x1c);
  uStack_128 = (undefined4)param_2[4];
  uStack_124 = *(undefined4 *)((long)param_2 + 0x24);
  param_2[3] = 0;
  param_2[4] = 0;
  param_2[5] = 0;
  iStack_118 = (int)param_2[6];
  puVar8 = (undefined8 *)FUN_01bb0f6a(0x28);
  puVar8[1] = 0;
  puVar8[2] = 0;
  *puVar8 = &DAT_06ef94a8;
  plVar9 = (long *)FUN_01bc9e1a();
  plVar10 = (long *)(**(code **)(*plVar9 + 0x10))(plVar9,0xb0,8);
  plVar22 = puVar8 + 3;
  plVar10[1] = 0;
  plVar10[2] = 0;
  *plVar10 = (long)&PTR_LAB_06ef94f8;
  plVar10[3] = (long)plVar9;
  plVar11 = plVar10 + 4;
  plVar10[4] = 0;
  plVar10[5] = 0;
  plVar10[6] = 1;
  FUN_02a93580(plVar10 + 7,"ServeAdResult",0);
  plVar10[8] = (long)plVar9;
  *(undefined1 *)(plVar10 + 0x13) = 0;
  *(undefined2 *)(plVar10 + 0x14) = 0;
  *(undefined1 *)((long)plVar10 + 0xa2) = 0;
  plVar10[9] = 0;
  plVar10[10] = 0;
  plVar10[0xb] = 0;
  plVar10[0xc] = 0;
  plVar10[0xd] = 0;
  FUN_02a95880(plVar10 + 0x15);
  puVar8[3] = plVar11;
  puVar8[4] = plVar10;
  if (plVar10[5] == 0) {
    LOCK();
    plVar10[1] = plVar10[1] + 1;
    UNLOCK();
    LOCK();
    plVar10[2] = plVar10[2] + 1;
    UNLOCK();
    plVar10[4] = (long)plVar11;
    plVar10[5] = (long)plVar10;
LAB_0640d997:
    plVar11 = plVar10 + 1;
    LOCK();
    lVar1 = *plVar11;
    *plVar11 = *plVar11 + -1;
    UNLOCK();
    if (lVar1 == 0) {
      (**(code **)(*plVar10 + 0x10))(plVar10);
      FUN_02aa9910(plVar10);
    }
  }
  else if (*(long *)(plVar10[5] + 8) == -1) {
    LOCK();
    plVar10[1] = plVar10[1] + 1;
    UNLOCK();
    LOCK();
    plVar10[2] = plVar10[2] + 1;
    UNLOCK();
    plVar10[4] = (long)plVar11;
    plVar10[5] = (long)plVar10;
    FUN_02aa9910();
    goto LAB_0640d997;
  }
  lVar1 = puVar8[3];
  uStack_168 = plVar22;
  uStack_160 = puVar8;
  if (lVar1 == 0) {
                    /* WARNING: Subroutine does not return */
    FUN_02b4ca6b(3);
  }
  FUN_02038318(lVar1 + 0x10);
  uStack_d8 = lVar1 + 0x11;
  if (*(char *)(lVar1 + 0x82) != '\0') {
                    /* WARNING: Subroutine does not return */
    FUN_02b4ca6b(1);
  }
  *(undefined1 *)(lVar1 + 0x82) = 1;
  FUN_0200cfb8(&uStack_d8);
  lVar1 = *plVar22;
  plVar11 = (long *)puVar8[4];
  if (puVar8[4] != 0) {
    LOCK();
    plVar9 = (long *)(puVar8[4] + 8);
    *plVar9 = *plVar9 + 1;
    UNLOCK();
  }
  FUN_02cd0612(&puStack_178,0);
  puVar8 = puStack_178;
  FUN_02030297(&uStack_110,&DAT_00435763);
  lVar15 = (long)&uStack_110 + 1;
  uVar16 = uStack_110 >> 1 & 0x7f;
  if ((uStack_110 & 1) != 0) {
    uVar16 = uStack_108;
    lVar15 = lStack_100;
  }
  FUN_0203ca16(&uStack_d8,"https",5,lVar15,uVar16,param_6,0x5f9140,0x17,&DAT_003db094,0,
               &DAT_003db094,0);
  FUN_0203cc18(&uStack_f8,&uStack_d8);
  lVar15 = (long)&uStack_f8 + 1;
  plVar9 = (long *)(uStack_f8 >> 1 & 0x7f);
  lVar14 = lVar15;
  if ((uStack_f8 & 1) != 0) {
    plVar9 = plStack_f0;
    lVar14 = lStack_e8;
  }
  FUN_0200921e(puVar8,lVar14,plVar9);
  if ((uStack_f8 & 1) != 0) {
    FUN_01bb4b03(lStack_e8);
  }
  if ((bStack_78 & 1) != 0) {
    FUN_01bb4b03(uStack_68);
  }
  if ((bStack_90 & 1) != 0) {
    FUN_01bb4b03(uStack_80);
  }
  if ((bStack_a8 & 1) != 0) {
    FUN_01bb4b03(uStack_98);
  }
  if (((ulong)puStack_c0 & 1) != 0) {
    FUN_01bb4b03(uStack_b0);
  }
  if ((uStack_d8 & 1) != 0) {
    FUN_01bb4b03(lStack_c8);
  }
  if ((uStack_110 & 1) != 0) {
    FUN_01bb4b03(lStack_100);
  }
  puStack_178[0x35] = (long)param_3;
  *(undefined4 *)(puStack_178 + 0x3f) = 9;
  thunk_FUN_02aaa460(puStack_178 + 0x22,&DAT_0058b1b4,4);
  puVar8 = puStack_178;
  uStack_d8._0_4_ = 0;
  uStack_d8._4_4_ = 0;
  uStack_d0._0_4_ = 0;
  uStack_d0._4_4_ = 0;
  lStack_c8._0_4_ = 0;
  puStack_c0 = (undefined *)0x0;
  uStack_b8 = 0;
  puVar12 = (undefined8 *)FUN_01bb0f6a(0x30);
  puVar12[2] = 0;
  puVar12[3] = 0;
  *puVar12 = 0;
  puVar12[1] = 0;
  puVar12[4] = 0;
  puVar12[5] = 0x100;
  uStack_d8._0_4_ = SUB84(puVar12,0);
  uStack_d8._4_4_ = (undefined4)((ulong)puVar12 >> 0x20);
  puVar13 = (undefined8 *)FUN_01bb0f6a(0x50);
  *(undefined4 *)(puVar13 + 1) = 4;
  puVar13[2] = puVar12;
  puVar13[3] = 0;
  puVar13[4] = 0;
  puVar13[5] = 0;
  puVar13[6] = 0;
  puVar13[7] = 0;
  puVar13[8] = 0x200;
  *(undefined4 *)(puVar13 + 9) = 0x144;
  *(undefined1 *)((long)puVar13 + 0x4c) = 0;
  *puVar13 = &PTR_FUN_06f3a360;
  uStack_d0._0_4_ = SUB84(puVar13,0);
  uStack_d0._4_4_ = (undefined4)((ulong)puVar13 >> 0x20);
  lStack_c8._0_4_ = 1;
  FUN_01fde638(puVar13);
  uVar20 = uStack_138;
  uVar16 = uStack_140;
  uVar18 = uStack_148;
  (**(code **)(*(long *)CONCAT44(uStack_d0._4_4_,(undefined4)uStack_d0) + 0x48))
            ((long *)CONCAT44(uStack_d0._4_4_,(undefined4)uStack_d0),"adTracingId",0xb,0);
  if ((uVar18 & 1) == 0) {
    uVar20 = (long)&uStack_148 + 1;
    uVar16 = uVar18 >> 1 & 0x7f;
  }
  (**(code **)(*(long *)CONCAT44(uStack_d0._4_4_,(undefined4)uStack_d0) + 0x48))
            ((long *)CONCAT44(uStack_d0._4_4_,(undefined4)uStack_d0),uVar20,uVar16,0);
  if (iStack_118 == 0) {
    pcVar19 = "Undefined";
    uVar21 = 9;
  }
  else {
    if (iStack_118 != 1) {
                    /* WARNING: Subroutine does not return */
      FUN_069dd6b3(0,&DAT_003db094);
    }
    pcVar19 = "InterstitialAd";
    uVar21 = 0xe;
  }
  (**(code **)(*(long *)CONCAT44(uStack_d0._4_4_,(undefined4)uStack_d0) + 0x48))
            ((long *)CONCAT44(uStack_d0._4_4_,(undefined4)uStack_d0),"placement",9,0);
  (**(code **)(*(long *)CONCAT44(uStack_d0._4_4_,(undefined4)uStack_d0) + 0x48))
            ((long *)CONCAT44(uStack_d0._4_4_,(undefined4)uStack_d0),pcVar19,uVar21,0);
  (**(code **)(*(long *)CONCAT44(uStack_d0._4_4_,(undefined4)uStack_d0) + 0x48))
            ((long *)CONCAT44(uStack_d0._4_4_,(undefined4)uStack_d0),"googleFields",0xc,0);
  lStack_c8 = CONCAT44(lStack_c8._4_4_,(int)lStack_c8 + 1);
  (**(code **)(*(long *)CONCAT44(uStack_d0._4_4_,(undefined4)uStack_d0) + 0x10))();
  auVar23 = FUN_0640221e(iStack_118);
  (**(code **)(*(long *)CONCAT44(uStack_d0._4_4_,(undefined4)uStack_d0) + 0x48))
            ((long *)CONCAT44(uStack_d0._4_4_,(undefined4)uStack_d0),"googleAdFormat",0xe,0);
  (**(code **)(*(long *)CONCAT44(uStack_d0._4_4_,(undefined4)uStack_d0) + 0x48))
            ((long *)CONCAT44(uStack_d0._4_4_,(undefined4)uStack_d0),auVar23._0_8_,auVar23._8_8_,0);
  uVar18 = uStack_120;
  uVar6 = uStack_130;
  bVar5 = (byte)uStack_130;
  uVar16 = CONCAT44(uStack_124,uStack_128);
  (**(code **)(*(long *)CONCAT44(uStack_d0._4_4_,(undefined4)uStack_d0) + 0x48))
            ((long *)CONCAT44(uStack_d0._4_4_,(undefined4)uStack_d0),&DAT_005be935,4,0);
  if ((uVar6 & 1) == 0) {
    uVar18 = (long)&uStack_130 + 1;
    uVar16 = (ulong)(bVar5 >> 1);
  }
  (**(code **)(*(long *)CONCAT44(uStack_d0._4_4_,(undefined4)uStack_d0) + 0x48))
            ((long *)CONCAT44(uStack_d0._4_4_,(undefined4)uStack_d0),uVar18,uVar16,0);
  FUN_0203ea7c(&uStack_158);
  FUN_02045492(&uStack_f8,uStack_158,&DAT_070922b8);
  lVar14 = lStack_e8;
  plVar9 = plStack_f0;
  uVar16 = uStack_f8;
  (**(code **)(*(long *)CONCAT44(uStack_d0._4_4_,(undefined4)uStack_d0) + 0x48))
            ((long *)CONCAT44(uStack_d0._4_4_,(undefined4)uStack_d0),"webViewUserAgent",0x10,0);
  if ((uVar16 & 1) == 0) {
    lVar14 = lVar15;
    plVar9 = (long *)(uVar16 >> 1 & 0x7f);
  }
  (**(code **)(*(long *)CONCAT44(uStack_d0._4_4_,(undefined4)uStack_d0) + 0x48))
            ((long *)CONCAT44(uStack_d0._4_4_,(undefined4)uStack_d0),lVar14,plVar9,0);
  if ((uStack_f8 & 1) != 0) {
    FUN_01bb4b03(lStack_e8);
  }
  plVar9 = plStack_150;
  if (plStack_150 != (long *)0x0) {
    LOCK();
    plVar10 = plStack_150 + 1;
    lVar14 = *plVar10;
    *plVar10 = *plVar10 + -1;
    UNLOCK();
    if (lVar14 == 0) {
      (**(code **)(*plStack_150 + 0x10))(plStack_150);
      FUN_02aa9910(plVar9);
    }
  }
  FUN_0203ea7c(&uStack_158);
  FUN_02045492(&uStack_f8,uStack_158,&DAT_070922e8);
  lVar14 = lStack_e8;
  uVar16 = uStack_f8;
  (**(code **)(*(long *)CONCAT44(uStack_d0._4_4_,(undefined4)uStack_d0) + 0x48))
            ((long *)CONCAT44(uStack_d0._4_4_,(undefined4)uStack_d0),"mobileAdvertisingId",0x13,0);
  if ((uVar16 & 1) == 0) {
    lVar14 = lVar15;
    plStack_f0 = (long *)(uVar16 >> 1 & 0x7f);
  }
  (**(code **)(*(long *)CONCAT44(uStack_d0._4_4_,(undefined4)uStack_d0) + 0x48))
            ((long *)CONCAT44(uStack_d0._4_4_,(undefined4)uStack_d0),lVar14,plStack_f0,0);
  if ((uStack_f8 & 1) != 0) {
    FUN_01bb4b03(lStack_e8);
  }
  if (plStack_150 != (long *)0x0) {
    LOCK();
    plVar9 = plStack_150 + 1;
    lVar15 = *plVar9;
    *plVar9 = *plVar9 + -1;
    UNLOCK();
    if (lVar15 == 0) {
      (**(code **)(*plStack_150 + 0x10))(plStack_150);
      FUN_02aa9910(plStack_150);
    }
  }
  FUN_01fdf884(&uStack_d8);
  FUN_01fdf884(&uStack_d8);
  uVar21 = FUN_01fdf6a4(&uStack_d8);
  FUN_01d4a74e(&uStack_110,uVar21,
               *(long *)(CONCAT44(uStack_d8._4_4_,(undefined4)uStack_d8) + 0x18) -
               *(long *)(CONCAT44(uStack_d8._4_4_,(undefined4)uStack_d8) + 0x10));
  FUN_01fdf7e6(&uStack_d8);
  FUN_020a0dba(puVar8,&uStack_110,0);
  if ((uStack_110 & 1) != 0) {
    FUN_01bb4b03(lStack_100);
  }
  *(undefined4 *)(puStack_178 + 0x28) = 0;
  uVar16 = (ulong)(DAT_07075bb0 >> 1);
  puVar3 = &DAT_07075bb1;
  if ((DAT_07075bb0 & 1) != 0) {
    uVar16 = DAT_07075bb8;
    puVar3 = DAT_07075bc0;
  }
  uVar18 = (ulong)(DAT_07075b98 >> 1);
  puVar4 = &DAT_07075b99;
  if ((DAT_07075b98 & 1) != 0) {
    uVar18 = DAT_07075ba0;
    puVar4 = DAT_07075ba8;
  }
  FUN_02008fae(puStack_178,puVar3,uVar16,puVar4,uVar18);
  uStack_d8._0_4_ = (undefined4)uStack_168;
  uStack_d8._4_4_ = uStack_168._4_4_;
  uStack_d0._0_4_ = (undefined4)uStack_160;
  uStack_d0._4_4_ = uStack_160._4_4_;
  uStack_168 = (long *)0x0;
  uStack_160 = (long *)0x0;
  uStack_f8 = 0;
  plStack_f0 = (long *)0x0;
  puStack_c0 = &DAT_06ef953e;
  (**(code **)*puStack_178)();
  FUN_02affcd6(&uStack_d8);
  if (plStack_170 != (long *)0x0) {
    LOCK();
    plVar9 = plStack_170 + 1;
    lVar15 = *plVar9;
    *plVar9 = *plVar9 + -1;
    UNLOCK();
    if (lVar15 == 0) {
      (**(code **)(*plStack_170 + 0x10))(plStack_170);
      FUN_02aa9910(plStack_170);
    }
  }
  plVar9 = uStack_160;
  if (uStack_160 != (long *)0x0) {
    LOCK();
    plVar10 = uStack_160 + 1;
    lVar15 = *plVar10;
    *plVar10 = *plVar10 + -1;
    UNLOCK();
    if (lVar15 == 0) {
      (**(code **)(*uStack_160 + 0x10))(uStack_160);
      FUN_02aa9910(plVar9);
    }
  }
  if ((uStack_130 & 1) != 0) {
    FUN_01bb4b03(uStack_120);
  }
  if ((uStack_148 & 1) != 0) {
    FUN_01bb4b03(uStack_138);
  }
  lVar17 = (long)(param_3 + 100) * 1000;
  lVar14 = FUN_02a93280();
  lVar15 = 0;
  if (0 < lVar17) {
    lVar15 = lVar17;
  }
  if (lVar1 == 0) goto LAB_0640e4b9;
  if ((*(byte *)(lVar1 + 0x81) & 1) == 0) {
    if ((*(byte *)(lVar1 + 0x80) & 1) == 0) {
      FUN_02038318(lVar1 + 0x10);
      uStack_f8 = lVar1 + 0x11;
      FUN_01bc9e1a();
      uStack_d0 = &lStack_c8;
      uStack_d8._0_4_ = 0x33852ce;
      uStack_d8._4_4_ = 0;
      uStack_48 = 0;
      lStack_c8 = lVar1;
      cVar7 = FUN_02b22b92(lVar1 + 0x18,&uStack_f8,lVar14 + lVar15,&uStack_d8);
      if ((code *)CONCAT44(uStack_d8._4_4_,(undefined4)uStack_d8) != (code *)0x0) {
        (*(code *)CONCAT44(uStack_d8._4_4_,(undefined4)uStack_d8))(uStack_d0,uStack_d0);
      }
      FUN_0200cfb8(&uStack_f8);
      if (cVar7 == '\0') goto LAB_0640e3b0;
    }
    if (lVar1 == 0) {
LAB_0640e4b9:
      fprintf(_stderr,"RSL PANIC: %s\n","Condition failed: valid()");
                    /* WARNING: Subroutine does not return */
      FUN_02ade130();
    }
    if ((*(byte *)(lVar1 + 0x81) & 1) != 0) {
      FUN_02038318(lVar1 + 0x10);
      uStack_f8 = lVar1 + 0x11;
      puVar8 = *(undefined8 **)(lVar1 + 0x38);
      *(undefined8 *)(lVar1 + 0x38) = 0;
      uStack_d8._0_4_ = SUB84(puVar8,0);
      uStack_d8._4_4_ = (undefined4)((ulong)puVar8 >> 0x20);
      uStack_d0._0_4_ = (undefined4)*(undefined8 *)(lVar1 + 0x40);
      uStack_d0._4_4_ = (undefined4)((ulong)*(undefined8 *)(lVar1 + 0x40) >> 0x20);
      *(undefined1 *)(lVar1 + 0x81) = 0;
      FUN_0200cfb8();
      if (puVar8 != (undefined8 *)0x0) {
        (*(code *)*puVar8)(puVar8[1],0);
        (*(code *)*puVar8)(puVar8[1],puVar8[1]);
        *puVar8 = 0;
        lVar15 = CONCAT44(uStack_d8._4_4_,(undefined4)uStack_d8);
        uStack_d8._0_4_ = 0;
        uStack_d8._4_4_ = 0;
        if (lVar15 != 0) {
          FUN_02b4c6a8(&uStack_d0);
        }
      }
    }
    plStack_1b0 = (long *)0x0;
    uStack_f8 = lVar1;
    plStack_f0 = plVar11;
    if ((*(byte *)(lVar1 + 0x80) & 1) == 0) {
      FUN_02038318(lVar1 + 0x10);
      uStack_110 = lVar1 + 0x11;
      FUN_01bc9e1a();
      uStack_d0 = &lStack_c8;
      uStack_d8._0_4_ = 0x33852ce;
      uStack_d8._4_4_ = 0;
      uStack_48 = 0;
      lStack_c8 = lVar1;
      uVar21 = FUN_02a932d0();
      FUN_02b22b92(lVar1 + 0x18,&uStack_110,uVar21,&uStack_d8);
      if ((code *)CONCAT44(uStack_d8._4_4_,(undefined4)uStack_d8) != (code *)0x0) {
        (*(code *)CONCAT44(uStack_d8._4_4_,(undefined4)uStack_d8))(uStack_d0,uStack_d0);
      }
      FUN_0200cfb8(&uStack_110);
    }
    if (*(long *)(lVar1 + 0x48) != 0) {
      uVar21 = FUN_02b4c936(lVar1 + 0x48);
                    /* WARNING: Subroutine does not return */
      FUN_02afd70a(uVar21);
    }
    cVar7 = *(char *)(lVar1 + 0x70);
    *(char *)(param_1 + 8) = cVar7;
    if (cVar7 == '\0') {
      *(undefined8 *)(param_1 + 4) = *(undefined8 *)(lVar1 + 0x60);
      uVar21 = *(undefined8 *)(lVar1 + 0x58);
      *(undefined8 *)param_1 = *(undefined8 *)(lVar1 + 0x50);
      *(undefined8 *)(param_1 + 2) = uVar21;
      *(undefined8 *)(lVar1 + 0x50) = 0;
      *(undefined8 *)(lVar1 + 0x58) = 0;
      *(undefined8 *)(lVar1 + 0x60) = 0;
      param_1[6] = *(int *)(lVar1 + 0x68);
    }
    else {
      iVar2 = *(int *)(lVar1 + 0x50);
      *param_1 = iVar2;
      (*(code *)(&PTR_FUN_06ef91c0)[iVar2])(param_1 + 2,lVar1 + 0x58);
    }
    if (plVar11 != (long *)0x0) {
      LOCK();
      plVar9 = plVar11 + 1;
      lVar1 = *plVar9;
      *plVar9 = *plVar9 + -1;
      UNLOCK();
      if (lVar1 == 0) {
        (**(code **)(*plVar11 + 0x10))(plVar11);
        FUN_02aa9910(plVar11);
      }
    }
  }
  else {
LAB_0640e3b0:
    FUN_01bb776a(&uStack_d8,"HTTP request timed out");
    puStack_c0 = (undefined *)CONCAT44(puStack_c0._4_4_,3);
    *(long **)(param_1 + 2) = uStack_d0;
    *(ulong *)((long)param_1 + 1) =
         CONCAT17((undefined1)uStack_d0,CONCAT43(uStack_d8._4_4_,uStack_d8._1_3_));
    *(undefined1 *)(param_1 + 8) = 0;
    *(undefined1 *)param_1 = (undefined1)uStack_d8;
    *(long *)(param_1 + 4) = lStack_c8;
    param_1[6] = 3;
    plStack_1b0 = plVar11;
  }
  if (plStack_1b0 != (long *)0x0) {
    LOCK();
    plVar11 = plStack_1b0 + 1;
    lVar1 = *plVar11;
    *plVar11 = *plVar11 + -1;
    UNLOCK();
    if (lVar1 == 0) {
      (**(code **)(*plStack_1b0 + 0x10))(plStack_1b0);
      FUN_02aa9910(plStack_1b0);
    }
  }
  if (___stack_chk_guard != lStack_38) {
                    /* WARNING: Subroutine does not return */
    __stack_chk_fail();
  }
  return param_1;
}

