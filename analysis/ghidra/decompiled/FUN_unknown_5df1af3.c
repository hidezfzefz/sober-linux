/* Ghidra decompilation of target routine FUN_unknown @ 0x5df1af3
 * REA evidence: ev_439b29f6ef315227952ac382be5de3af210057eaf8f25753f6b769c67041226b
 * Requested address: 0x5df1af3
 * Target-derived data, not source. Do not execute.
 */


/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

undefined8 *
FUN_05ef1a22(float param_1,undefined8 *param_2,long param_3,long param_4,float *param_5,
            undefined4 *param_6,int param_7,undefined8 *param_8,undefined8 *param_9)

{
  float fVar1;
  float fVar2;
  float fVar3;
  undefined8 uVar4;
  undefined8 uVar5;
  undefined8 uVar6;
  undefined8 uVar7;
  undefined8 uVar8;
  undefined1 auVar9 [16];
  undefined1 auVar10 [16];
  undefined1 auVar11 [16];
  undefined4 uVar12;
  undefined4 uVar13;
  undefined8 *puVar14;
  float fVar15;
  float fVar16;
  ulong uVar17;
  float fVar18;
  float fVar19;
  float fVar20;
  float fVar21;
  float fVar22;
  float fVar23;
  float fVar24;
  float fVar25;
  float fVar26;
  float fVar27;
  float fVar28;
  undefined1 auVar29 [16];
  undefined1 auVar30 [16];
  undefined1 auVar31 [16];
  undefined1 auVar32 [16];
  undefined1 auVar33 [16];
  float fVar34;
  float fVar35;
  float fVar38;
  undefined1 auVar36 [16];
  undefined1 auVar37 [16];
  float fVar39;
  float fVar40;
  float fVar41;
  float fVar42;
  float fVar43;
  float fStack_1c8;
  float fStack_1c4;
  float fStack_1b8;
  float fStack_1b4;
  float fStack_1a8;
  float fStack_1a4;
  float fStack_178;
  float fStack_174;
  undefined8 uStack_e8;
  float fStack_e0;
  undefined8 uStack_d8;
  float fStack_d0;
  float fStack_cc;
  float fStack_c8;
  float fStack_c4;
  float fStack_c0;
  float fStack_bc;
  float fStack_b8;
  float fStack_b4;
  float fStack_b0;
  float fStack_ac;
  float fStack_a8;
  float fStack_a4;
  float fStack_a0;
  float fStack_9c;
  float fStack_98;
  float fStack_94;
  float fStack_90;
  float fStack_8c;
  float fStack_88;
  float fStack_84;
  float fStack_80;
  float fStack_7c;
  float fStack_78;
  undefined4 uStack_74;
  undefined8 uStack_68;
  undefined8 uStack_60;
  undefined8 uStack_58;
  undefined8 uStack_50;
  undefined8 uStack_48;
  long lStack_38;
  
  lStack_38 = ___stack_chk_guard;
  fVar1 = *param_5;
  fVar21 = (fVar1 + fVar1) * fVar1;
  fStack_c8 = 0.0;
  fStack_c4 = 0.0;
  fStack_c0 = 0.0;
  if (param_7 == 0) {
    FUN_06a28a48(&fStack_98,param_3 + 0xc);
    fVar2 = *(float *)(param_4 + 0xc);
    fVar3 = *(float *)(param_4 + 0x10);
    fVar42 = *(float *)(param_4 + 0x14);
    fVar15 = *(float *)(param_4 + 0x18);
    uStack_68 = CONCAT44(fVar42 * fStack_7c + fVar3 * fStack_88 + fVar2 * fStack_94,
                         fVar42 * fStack_80 + fVar3 * fStack_8c + fVar2 * fStack_98);
    fVar24 = *(float *)(param_4 + 0x1c);
    fVar34 = *(float *)(param_4 + 0x20);
    fVar16 = *(float *)(param_4 + 0x24);
    uStack_60 = CONCAT44(fVar34 * fStack_80 + fVar24 * fStack_8c + fVar15 * fStack_98,
                         fVar42 * fStack_78 + fVar3 * fStack_84 + fVar2 * fStack_90);
    uStack_58 = CONCAT44(fVar34 * fStack_78 + fVar24 * fStack_84 + fVar15 * fStack_90,
                         fVar34 * fStack_7c + fVar24 * fStack_88 + fVar15 * fStack_94);
    fVar2 = *(float *)(param_4 + 0x28);
    fVar3 = *(float *)(param_4 + 0x2c);
    fVar42 = fVar3 * fStack_80 + fVar2 * fStack_8c + fVar16 * fStack_98;
    fVar15 = fVar3 * fStack_7c + fVar2 * fStack_88 + fVar16 * fStack_94;
    uStack_50 = CONCAT44(fVar15,fVar42);
    auVar33._4_4_ = fVar15;
    auVar33._0_4_ = fVar42;
    auVar33._8_4_ = fVar3 * fStack_78 + fVar2 * fStack_84 + fVar16 * fStack_90;
    auVar33._12_4_ = fVar3 * fStack_7c + fVar2 * fStack_80 + fVar16 * fStack_8c;
    uStack_48 = extractps(auVar33,2);
    FUN_06a28d6e(&uStack_68,&fStack_c8,&fStack_cc);
  }
  else {
    uStack_48 = CONCAT44(uStack_48._4_4_,*(undefined4 *)(param_8 + 4));
    uStack_68 = *param_8;
    uStack_60 = param_8[1];
    uStack_58 = param_8[2];
    uStack_50 = param_8[3];
    fStack_c8 = (float)*param_9;
    fStack_c4 = (float)((ulong)*param_9 >> 0x20);
    fStack_c0 = *(float *)(param_9 + 1);
    fStack_cc = param_1;
  }
  fVar42 = fStack_cc;
  fVar34 = fVar1 * fVar1 * fVar1;
  fVar24 = fVar1 * 3.0 * fVar1;
  fVar15 = fVar1 * fVar21;
  fVar2 = *(float *)(param_3 + 0x38);
  uVar4 = *(undefined8 *)(param_3 + 0x30);
  uVar5 = *(undefined8 *)(param_4 + 0x30);
  fVar3 = *(float *)(param_4 + 0x38);
  if (fStack_cc <= 1e-06) {
    fVar18 = (float)*(undefined8 *)(param_4 + 0x48);
    fVar25 = (float)((ulong)*(undefined8 *)(param_4 + 0x48) >> 0x20);
    fVar16 = *(float *)(param_4 + 0x50);
    uVar17 = CONCAT44(fStack_c4,fStack_c8);
    fVar23 = fVar18;
    fVar22 = fVar16;
    fVar43 = fVar25;
  }
  else {
    if (fStack_cc <= 0.0001) {
      fVar16 = fStack_cc * -0.5;
      fVar22 = *(float *)(param_4 + 0x50);
      auVar37._4_4_ = fStack_c0;
      auVar37._0_4_ = fStack_c4;
      uVar17 = *(ulong *)(param_4 + 0x48);
      fVar23 = (float)uVar17;
      fVar43 = (float)(uVar17 >> 0x20);
      auVar30._8_8_ = 0;
      auVar30._0_8_ = uVar17;
      auVar30 = insertps(ZEXT416((uint)fVar22),auVar30,0x1c);
      auVar31._4_4_ = fStack_c0;
      auVar31._0_4_ = fStack_c0;
      auVar31._8_8_ = 0;
      auVar31 = insertps(auVar31,ZEXT416((uint)fStack_c8),0x10);
      auVar37._8_8_ = 0;
      auVar33 = insertps(ZEXT416((uint)fStack_c8),auVar37,0x1c);
      uVar17 = auVar33._0_8_;
      fVar18 = fVar16 * (auVar30._0_4_ * fStack_c4 -
                        auVar31._0_4_ * (float)*(undefined8 *)(param_4 + 0x4c));
      fVar25 = fVar16 * (auVar30._4_4_ * fStack_c0 -
                        auVar31._4_4_ * (float)((ulong)*(undefined8 *)(param_4 + 0x4c) >> 0x20));
      fVar16 = (fVar43 * fStack_c8 - fStack_c4 * fVar23) * fVar16;
    }
    else {
      fVar18 = fStack_cc * 0.5;
      fVar16 = tanf(1.5707964 - fVar18);
      fVar25 = 1.0 - fVar16 * fVar18;
      fVar22 = *(float *)(param_4 + 0x50);
      auVar10._4_4_ = fStack_c0;
      auVar10._0_4_ = fStack_c4;
      auVar32._0_4_ = fStack_c4 * fVar22;
      auVar32._4_4_ = fStack_c0;
      auVar32._8_8_ = 0;
      uVar17 = *(ulong *)(param_4 + 0x48);
      fVar23 = (float)uVar17;
      fVar43 = (float)(uVar17 >> 0x20);
      auVar36._0_4_ = fVar43 * fStack_c8;
      auVar36._4_4_ = fVar43;
      auVar36._8_8_ = 0;
      auVar37 = insertps(auVar36,auVar32,0x10);
      fVar16 = (fStack_c0 * fVar23 - fVar22 * fStack_c8) * fVar25 - fVar18 * fVar43;
      auVar11._8_8_ = 0;
      auVar11._0_8_ = uVar17;
      auVar33 = insertps(ZEXT416((uint)fVar22),auVar11,0x1c);
      fVar26 = fVar25 * (auVar37._0_4_ - fStack_c4 * fVar23) - auVar33._0_4_ * fVar18;
      fVar28 = fVar25 * (auVar37._4_4_ - fStack_c0 * fVar43) - auVar33._4_4_ * fVar18;
      auVar29._4_4_ = fStack_c0;
      auVar29._0_4_ = fStack_c0;
      auVar29._8_8_ = 0;
      auVar37 = insertps(auVar29,ZEXT416((uint)fStack_c8),0x10);
      auVar10._8_8_ = 0;
      auVar33 = insertps(ZEXT416((uint)fStack_c8),auVar10,0x1c);
      uVar17 = auVar33._0_8_;
      fVar18 = fStack_c4 * fVar26 - fVar16 * auVar37._0_4_;
      fVar25 = fStack_c0 * fVar28 - fVar26 * auVar37._4_4_;
      fVar16 = fVar16 * fStack_c8 - fVar28 * fStack_c4;
    }
    fVar18 = fVar18 + fVar23;
    fVar25 = fVar25 + fVar43;
    fVar16 = fVar16 + fVar22;
  }
  fVar28 = fVar24 - fVar15;
  fVar19 = (float)*(undefined8 *)(param_3 + 0x48);
  fVar20 = (float)((ulong)*(undefined8 *)(param_3 + 0x48) >> 0x20);
  fVar27 = param_5[2] * param_5[1];
  fVar26 = *(float *)(param_3 + 0x50);
  *param_6 = 0;
  if (DAT_06fdedd8 == '\0') {
    fVar35 = (fVar18 + fVar19) * 0.5;
    fVar38 = (fVar25 + fVar20) * 0.5;
    fVar39 = (fVar16 + fVar26) * 0.5;
    fVar40 = SQRT(fVar39 * fVar39 + fVar35 * fVar35 + fVar38 * fVar38);
    fVar41 = 1.0 / fVar40;
    fVar40 = fVar27 * fVar40;
    fVar35 = fVar39 * fVar41 * fStack_c0 +
             fVar41 * fVar35 * (float)uVar17 + fVar41 * fVar38 * (float)(uVar17 >> 0x20);
    uVar12 = 2;
    uVar13 = uVar12;
    if ((((fVar35 < 0.0) && (0.8 < ABS(fVar35))) && (uVar13 = 2, 3.1415927 < fVar40)) &&
       (0.01 < fVar42)) {
      fVar38 = fVar26 * fVar26 + fVar20 * fVar20 + fVar19 * fVar19;
      fVar35 = fVar22 * fVar22 + fVar43 * fVar43 + fVar23 * fVar23;
      if (fVar35 <= fVar38) {
        fVar35 = fVar38;
      }
      uVar13 = uVar12;
      if (fVar35 * 0.75 < fVar22 * fVar26 + fVar43 * fVar20 + fVar23 * fVar19) {
        fVar42 = 6.2831855 - fVar42;
        uVar17 = uVar17 ^ 0x8000000080000000;
        fStack_c8 = (float)uVar17;
        fStack_c4 = (float)(uVar17 >> 0x20);
        fStack_c0 = -fStack_c0;
        *param_6 = 1;
        uVar13 = 3;
        fStack_cc = fVar42;
      }
    }
    if ((DAT_06fdeb68 == '\0') && (6.2831855 < fVar40)) {
      fVar42 = (float)(int)(fVar40 / 6.2831855) * 6.2831855 + fVar42;
      *param_6 = uVar13;
      fStack_cc = fVar42;
    }
  }
  fVar43 = fVar1 + (fVar34 - fVar21);
  fVar34 = fVar34 - fVar1 * fVar1;
  fVar42 = fVar42 * fVar28;
  fVar22 = fVar42 * (float)uVar17;
  fVar23 = fVar42 * (float)(uVar17 >> 0x20);
  uStack_d8 = CONCAT44(fVar23,fVar22);
  fStack_d0 = fVar42 * fStack_c0;
  fVar1 = *(float *)(param_3 + 0x44);
  uVar6 = *(undefined8 *)(param_3 + 0x3c);
  uVar7 = *(undefined8 *)(param_4 + 0x3c);
  fVar21 = *(float *)(param_4 + 0x44);
  if (DAT_06fdeb80 == '\0') {
    uStack_e8 = CONCAT44(fVar27 * (fVar34 * fVar25 + fVar43 * fVar20) + fVar23,
                         fVar27 * (fVar34 * fVar18 + fVar43 * fVar19) + fVar22);
    fStack_e0 = (fVar16 * fVar34 + fVar26 * fVar43) * fVar27 + fStack_d0;
    puVar14 = &uStack_e8;
  }
  else {
    puVar14 = &uStack_d8;
  }
  FUN_03775012(&fStack_bc,puVar14);
  fVar42 = *(float *)(param_3 + 0x18);
  fVar16 = *(float *)(param_3 + 0x1c);
  fVar22 = *(float *)(param_3 + 0x20);
  fVar23 = *(float *)(param_3 + 0x24);
  fVar19 = (float)*(undefined8 *)(param_3 + 0x28);
  fVar20 = (float)((ulong)*(undefined8 *)(param_3 + 0x28) >> 0x20);
  fVar18 = *(float *)(param_3 + 0xc);
  fVar25 = *(float *)(param_3 + 0x10);
  fVar26 = *(float *)(param_3 + 0x14);
  fStack_98 = fStack_b4 * fVar23 + fStack_b8 * fVar42 + fStack_bc * fVar18;
  fStack_94 = fStack_b4 * fVar19 + fStack_b8 * fVar16 + fStack_bc * fVar25;
  fStack_90 = fStack_b4 * fVar20 + fStack_b8 * fVar22 + fStack_bc * fVar26;
  fStack_8c = fStack_a8 * fVar23 + fStack_ac * fVar42 + fStack_b0 * fVar18;
  fStack_88 = fStack_a8 * fVar19 + fStack_ac * fVar16 + fStack_b0 * fVar25;
  fStack_84 = fStack_a8 * fVar20 + fStack_ac * fVar22 + fStack_b0 * fVar26;
  fStack_80 = fStack_9c * fVar23 + fStack_a0 * fVar42 + fStack_a4 * fVar18;
  fStack_7c = fStack_9c * fVar19 + fStack_a0 * fVar16 + fStack_a4 * fVar25;
  auVar9._4_4_ = fStack_7c;
  auVar9._0_4_ = fStack_80;
  auVar9._8_4_ = fStack_9c * fVar20 + fStack_a0 * fVar22 + fStack_a4 * fVar26;
  auVar9._12_4_ = fStack_9c * fVar19 + fStack_a0 * fVar23 + fStack_a4 * *(float *)(param_3 + 0x18);
  uVar8 = extractps(auVar9,2);
  fStack_78 = (float)uVar8;
  uStack_74 = (undefined4)((ulong)uVar8 >> 0x20);
  fVar42 = (fVar15 - fVar24) + 1.0;
  fStack_1b8 = (float)uVar5;
  fStack_1b4 = (float)((ulong)uVar5 >> 0x20);
  fStack_1a8 = (float)uVar6;
  fStack_1a4 = (float)((ulong)uVar6 >> 0x20);
  fStack_178 = (float)uVar7;
  fStack_174 = (float)((ulong)uVar7 >> 0x20);
  fStack_1c8 = (float)uVar4;
  fStack_1c4 = (float)((ulong)uVar4 >> 0x20);
  FUN_06a28ba2(&fStack_98);
  *(float *)(param_2 + 4) = fStack_78;
  *(float *)(param_2 + 2) = fStack_88;
  *(float *)((long)param_2 + 0x14) = fStack_84;
  *(float *)(param_2 + 3) = fStack_80;
  *(float *)((long)param_2 + 0x1c) = fStack_7c;
  *param_2 = CONCAT44(fStack_94,fStack_98);
  param_2[1] = CONCAT44(fStack_8c,fStack_90);
  *(ulong *)((long)param_2 + 0x24) =
       CONCAT44(fVar27 * (fVar34 * fStack_174 + fVar43 * fStack_1a4) +
                fVar42 * fStack_1c4 + fVar28 * fStack_1b4,
                fVar27 * (fVar34 * fStack_178 + fVar43 * fStack_1a8) +
                fVar42 * fStack_1c8 + fVar28 * fStack_1b8);
  *(float *)((long)param_2 + 0x2c) =
       (fVar21 * fVar34 + fVar1 * fVar43) * fVar27 + fVar3 * fVar28 + fVar42 * fVar2;
  if (___stack_chk_guard != lStack_38) {
                    /* WARNING: Subroutine does not return */
    __stack_chk_fail();
  }
  return param_2;
}

