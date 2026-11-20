/* Ghidra decompilation of target routine FUN_02cb3eb4 @ 0x2cb3eb4
 * REA evidence: ev_439b29f6ef315227952ac382be5de3af210057eaf8f25753f6b769c67041226b
 * Requested address: 0x2cb4529
 * Target-derived data, not source. Do not execute.
 */


/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

void FUN_02cb3eb4(long param_1,long param_2,undefined8 param_3,byte *param_4)

{
  byte bVar1;
  byte bVar2;
  undefined4 uVar3;
  byte *pbVar4;
  char cVar5;
  undefined **ppuVar6;
  long *plVar7;
  ulong uVar8;
  long lVar9;
  undefined1 *puVar10;
  byte *pbVar11;
  undefined8 uVar12;
  int *piVar13;
  char *pcVar14;
  long lVar15;
  char *pcVar16;
  bool bVar17;
  double dVar18;
  undefined1 auVar19 [16];
  undefined8 uStack_198;
  undefined8 uStack_190;
  byte *pbStack_188;
  byte bStack_178;
  undefined1 auStack_177 [15];
  undefined1 *puStack_168;
  byte bStack_160;
  int iStack_15f;
  ulong uStack_158;
  int *piStack_150;
  ulong uStack_148;
  undefined8 uStack_140;
  undefined8 uStack_138;
  undefined1 auStack_128 [24];
  undefined8 uStack_110;
  int *piStack_108;
  ulong uStack_100;
  byte *pbStack_f8;
  undefined4 uStack_f0;
  undefined4 uStack_ec;
  double dStack_e8;
  ulong uStack_e0;
  undefined8 uStack_d8;
  undefined8 uStack_d0;
  ulong auStack_c8 [4];
  undefined8 uStack_a8;
  ulong uStack_a0;
  byte *pbStack_98;
  ulong uStack_90;
  double dStack_88;
  ulong uStack_78;
  double dStack_68;
  byte bStack_60;
  undefined8 uStack_50;
  long lStack_38;
  
  lStack_38 = ___stack_chk_guard;
  pbVar4 = *(byte **)(param_2 + 0xb0);
  FUN_02107a08(&bStack_160,pbVar4);
  cVar5 = FUN_0200fc02();
  pcVar16 = " HttpClient::destroyed:1";
  if (cVar5 == '\0') {
    pcVar16 = "";
  }
  dVar18 = (*(double *)(pbVar4 + 0x238) - *(double *)(pbVar4 + 0x218)) * 1000.0;
  uStack_78 = -(ulong)(*(double *)(pbVar4 + 0x230) != 0.0) &
              (ulong)((*(double *)(pbVar4 + 0x238) - *(double *)(pbVar4 + 0x230)) * 1000.0);
  dStack_68 = *(double *)(pbVar4 + 0x2b0) * 1000.0;
  bVar1 = pbVar4[0x1b0];
  uStack_a8 = (int *)(ulong)*(uint *)(pbVar4 + 0x330);
  pbStack_98 = pbVar4;
  dStack_88 = dVar18;
  FUN_01e351b5(&bStack_178,
               "HttpResponse(#{} {}) time:{:.1f}ms (net:{:.1f}ms timeInRetryQueue:{:.1f}ms)",0x4b,
               0xaaae2);
  uStack_198 = (int *)0x0;
  uStack_190 = 0;
  pbStack_188 = (byte *)0x0;
  if (*(int *)(param_2 + 0x108) < 1) {
    piStack_108 = &iStack_15f;
    uStack_100 = (ulong)(bStack_160 >> 1);
    if ((bStack_160 & 1) != 0) {
      uStack_100 = uStack_158;
      piStack_108 = piStack_150;
    }
    FUN_01e351b5(&uStack_a8,&DAT_0063a413,4,0xd);
    if (((ulong)uStack_198 & 1) != 0) {
      FUN_01bb4b03(pbStack_188);
    }
    pbStack_188 = pbStack_98;
    uStack_198 = uStack_a8;
  }
  else {
    uStack_a8 = &iStack_15f;
    uStack_a0 = (ulong)(bStack_160 >> 1);
    if ((bStack_160 & 1) != 0) {
      uStack_a0 = uStack_158;
      uStack_a8 = piStack_150;
    }
    bVar17 = (*param_4 & 1) != 0;
    pbStack_98 = param_4 + 1;
    if (bVar17) {
      pbStack_98 = *(byte **)(param_4 + 0x10);
    }
    uStack_90 = (ulong)(*param_4 >> 1);
    if (bVar17) {
      uStack_90 = *(ulong *)(param_4 + 8);
    }
    FUN_01e351b5(&piStack_108,"original: \"{}\" current: \"{}\"",0x1c,0xdd);
    if (((ulong)uStack_198 & 1) != 0) {
      FUN_01bb4b03(pbStack_188);
    }
    pbStack_188 = pbStack_f8;
    uStack_198 = piStack_108;
  }
  if (*(int *)(param_2 + 0x88) == 0) {
    if ((*(byte *)(param_2 + 0x20) & 1) == 0) {
      lVar9 = param_2 + 0x21;
    }
    else {
      lVar9 = *(long *)(param_2 + 0x30);
    }
    pcVar14 = " cached:1";
    if (*(char *)(param_2 + 0xa8) == '\0') {
      pcVar14 = "";
    }
    pbVar11 = pbStack_188;
    if (((ulong)uStack_198 & 1) == 0) {
      pbVar11 = (byte *)((long)&uStack_198 + 1);
    }
    if ((*(byte *)(param_2 + 0x58) & 1) == 0) {
      lVar15 = param_2 + 0x59;
    }
    else {
      lVar15 = *(long *)(param_2 + 0x68);
    }
    FUN_01bc7052(&uStack_a8,
                 "status:%d %s bodySize:%zu%s%s url:{ %s } ip:%s external:%d numberOfTimesRetried:%d"
                 ,*(undefined4 *)(param_2 + 0x18),lVar9,param_3,pcVar14,pcVar16,pbVar11,lVar15,bVar1
                 ,*(undefined4 *)(pbVar4 + 0x2b8));
    uVar8 = (ulong)uStack_a8 >> 1 & 0x7f;
    pbVar11 = (byte *)((long)&uStack_a8 + 1);
    if (((ulong)uStack_a8 & 1) != 0) {
      uVar8 = uStack_a0;
      pbVar11 = pbStack_98;
    }
    FUN_02aaa730(&bStack_178,pbVar11,uVar8);
    if (((ulong)uStack_a8 & 1) != 0) {
      FUN_01bb4b03(pbStack_98);
    }
    if (pbVar4[0x3d8] != 0) {
      FUN_02aab580(&bStack_178," challengedByGcs");
    }
    bVar2 = pbVar4[0x1c0];
    if ((bVar2 & 1) == 0) {
      uVar8 = (ulong)(bVar2 >> 1);
    }
    else {
      uVar8 = *(ulong *)(pbVar4 + 0x1c8);
    }
    if (uVar8 != 0) {
      if ((bVar2 & 1) == 0) {
        pbVar11 = pbVar4 + 0x1c1;
      }
      else {
        pbVar11 = *(byte **)(pbVar4 + 0x1d0);
      }
      FUN_01bc7052(&uStack_a8," proxy: \"%s\"",pbVar11);
      uVar8 = (ulong)uStack_a8 >> 1 & 0x7f;
      pbVar11 = (byte *)((long)&uStack_a8 + 1);
      if (((ulong)uStack_a8 & 1) != 0) {
        uVar8 = uStack_a0;
        pbVar11 = pbStack_98;
      }
      FUN_02aaa730(&bStack_178,pbVar11,uVar8);
      if (((ulong)uStack_a8 & 1) != 0) {
        FUN_01bb4b03(pbStack_98);
      }
    }
    if ((*(int *)(param_2 + 0x18) - 400U < 200) && (*(int *)(pbVar4 + 0x208) != 4)) {
      if ((byte)((ulong)DAT_06f54008 >> 8) < 3 || (byte)DAT_06f54008 < 6) goto LAB_02cb4484;
      goto LAB_02cb429d;
    }
    if ((byte)DAT_06f54050 < 6 || (byte)((ulong)DAT_06f54050 >> 8) < 3) goto LAB_02cb4484;
    pcVar16 = "[DFLog::HttpTraceLight] %s";
    ppuVar6 = &PTR_s_HttpTraceLight_06f54058;
    uVar12 = DAT_06f54050;
  }
  else {
    if ((*(byte *)(param_2 + 0x90) & 1) == 0) {
      lVar9 = param_2 + 0x91;
    }
    else {
      lVar9 = *(long *)(param_2 + 0xa0);
    }
    pbVar11 = pbStack_188;
    if (((ulong)uStack_198 & 1) == 0) {
      pbVar11 = (byte *)((long)&uStack_198 + 1);
    }
    if ((*(byte *)(param_2 + 0x58) & 1) == 0) {
      lVar15 = param_2 + 0x59;
    }
    else {
      lVar15 = *(long *)(param_2 + 0x68);
    }
    FUN_01bc7052(&uStack_a8,
                 " error:%u message:%s%s url:{ %s } ip:%s external:%d numberOfTimesRetried:%d",
                 *(int *)(param_2 + 0x88),lVar9,pcVar16,pbVar11,lVar15,bVar1,
                 *(undefined4 *)(pbVar4 + 0x2b8));
    uVar8 = (ulong)uStack_a8 >> 1 & 0x7f;
    pbVar11 = (byte *)((long)&uStack_a8 + 1);
    if (((ulong)uStack_a8 & 1) != 0) {
      uVar8 = uStack_a0;
      pbVar11 = pbStack_98;
    }
    FUN_02aaa730(&bStack_178,pbVar11,uVar8);
    if (((ulong)uStack_a8 & 1) != 0) {
      FUN_01bb4b03(pbStack_98);
    }
    if (pbVar4[0x3d8] != 0) {
      FUN_02aab580(&bStack_178," challengedByGcs");
    }
    bVar2 = pbVar4[0x1c0];
    if ((bVar2 & 1) == 0) {
      uVar8 = (ulong)(bVar2 >> 1);
    }
    else {
      uVar8 = *(ulong *)(pbVar4 + 0x1c8);
    }
    if (uVar8 != 0) {
      if ((bVar2 & 1) == 0) {
        pbVar11 = pbVar4 + 0x1c1;
      }
      else {
        pbVar11 = *(byte **)(pbVar4 + 0x1d0);
      }
      FUN_01bc7052(&uStack_a8," proxy: \"%s\"",pbVar11);
      uVar8 = (ulong)uStack_a8 >> 1 & 0x7f;
      pbVar11 = (byte *)((long)&uStack_a8 + 1);
      if (((ulong)uStack_a8 & 1) != 0) {
        uVar8 = uStack_a0;
        pbVar11 = pbStack_98;
      }
      FUN_02aaa730(&bStack_178,pbVar11,uVar8);
      if (((ulong)uStack_a8 & 1) != 0) {
        FUN_01bb4b03(pbStack_98);
      }
    }
    if ((byte)DAT_06f54008 < 6 || (byte)((ulong)DAT_06f54008 >> 8) < 3) goto LAB_02cb4484;
LAB_02cb429d:
    pcVar16 = "[DFLog::HttpTraceError] %s";
    ppuVar6 = &PTR_s_HttpTraceError_06f54010;
    uVar12 = DAT_06f54008;
  }
  puVar10 = puStack_168;
  if ((bStack_178 & 1) == 0) {
    puVar10 = auStack_177;
  }
  FUN_01fec34d(uVar12,*ppuVar6,pcVar16,puVar10);
LAB_02cb4484:
  if ((bVar1 != 0 || *(int *)(pbVar4 + 0x1f8) == 4) &&
     (2 < (byte)((ulong)DAT_06f54070 >> 8) && 5 < (byte)DAT_06f54070)) {
    if ((pbVar4[0x110] & 1) == 0) {
      pbVar11 = pbVar4 + 0x111;
    }
    else {
      pbVar11 = *(byte **)(pbVar4 + 0x120);
    }
    piVar13 = piStack_150;
    if ((bStack_160 & 1) == 0) {
      piVar13 = &iStack_15f;
    }
    if ((pbVar4[0x128] & 1) == 0) {
      uVar8 = (ulong)(pbVar4[0x128] >> 1);
    }
    else {
      uVar8 = *(ulong *)(pbVar4 + 0x130);
    }
    FUN_02149b32(DAT_06f54070,PTR_s_NetworkAudit_06f54078,
                 "[DFLog::NetworkAudit] {\"http\":{\"method\":\"%s\",\"url\":\"%s\",\"asset\":\"%d\",\"fromLua\":\"%d\",\"reqDataSize\":\"%zu\",\"respDataSize\":\"%zu\"}}"
                 ,pbVar11,piVar13,*(int *)(pbVar4 + 0x1f8) == 4,bVar1,uVar8,param_3);
  }
  uVar3 = *(undefined4 *)(*(long *)(param_2 + 0xb0) + 0x1f8);
  FUN_02107a08(&uStack_148);
  auVar19 = FUN_021001fc(param_2 + 0x48);
  uVar8 = (long)DAT_06f537a0;
  if (auVar19._8_8_ < (ulong)(long)DAT_06f537a0) {
    uVar8 = auVar19._8_8_;
  }
  lVar9 = *(long *)(param_2 + 0xb0);
  if ((*(byte *)(lVar9 + 0x110) & 1) == 0) {
    pbStack_f8 = *(byte **)(lVar9 + 0x120);
    piStack_108 = *(int **)(lVar9 + 0x110);
    uStack_100 = *(ulong *)(lVar9 + 0x118);
  }
  else {
    FUN_01bb7eda(&piStack_108,*(undefined8 *)(lVar9 + 0x120),*(undefined8 *)(lVar9 + 0x118));
  }
  uStack_f0 = *(undefined4 *)(param_2 + 0x18);
  uStack_e0 = uStack_148;
  uStack_d8 = uStack_140;
  uStack_d0 = uStack_138;
  uStack_148 = 0;
  uStack_140 = 0;
  uStack_138 = 0;
  uStack_ec = uVar3;
  dStack_e8 = dVar18;
  if (auVar19._0_8_ == 0) {
    auStack_c8[0] = 0;
    auStack_c8[1] = 0;
    auStack_c8[2] = 0;
  }
  else {
    FUN_01d4a74e(auStack_c8,auVar19._0_8_,uVar8);
  }
  auStack_c8[3] = *(undefined8 *)(*(long *)(param_2 + 0xb0) + 0x200);
  if ((*(long *)(param_1 + 0x238) != 0) && (*(long *)(*(long *)(param_1 + 0x238) + 8) != 0)) {
    plVar7 = (long *)FUN_02a92ee0(&DAT_07725ad8,1);
    lVar9 = *plVar7;
    if (lVar9 == 0) {
      FUN_022976d2(*(undefined8 *)(param_1 + 0x238),&piStack_108);
    }
    else {
      uStack_a8 = *(int **)(param_1 + 0x238);
      if (uStack_a8 != (int *)0x0) {
        LOCK();
        *uStack_a8 = *uStack_a8 + 1;
        UNLOCK();
      }
      FUN_02297a60(&uStack_a0,&piStack_108);
      uStack_110 = 0;
      FUN_02b02cec(auStack_128,0x68,8,&uStack_a8,&PTR_LAB_06add470,FUN_02cc020a);
      FUN_02b860c4(lVar9,auStack_128);
      FUN_02affcd6(auStack_128);
      if ((bStack_60 & 1) != 0) {
        FUN_01bb4b03(uStack_50);
      }
      if ((uStack_78 & 1) != 0) {
        FUN_01bb4b03(dStack_68);
      }
      if ((uStack_a0 & 1) != 0) {
        FUN_01bb4b03(uStack_90);
      }
      FUN_01fddf36(&uStack_a8);
    }
  }
  if ((auStack_c8[0] & 1) != 0) {
    FUN_01bb4b03(auStack_c8[2]);
  }
  if ((uStack_e0 & 1) != 0) {
    FUN_01bb4b03(uStack_d0);
  }
  if (((ulong)piStack_108 & 1) != 0) {
    FUN_01bb4b03(pbStack_f8);
  }
  if ((uStack_148 & 1) != 0) {
    FUN_01bb4b03(uStack_138);
  }
  if (((ulong)uStack_198 & 1) != 0) {
    FUN_01bb4b03(pbStack_188);
  }
  if ((bStack_178 & 1) != 0) {
    FUN_01bb4b03(puStack_168);
  }
  if ((bStack_160 & 1) != 0) {
    FUN_01bb4b03(piStack_150);
  }
  if (___stack_chk_guard == lStack_38) {
    return;
  }
                    /* WARNING: Subroutine does not return */
  __stack_chk_fail();
}

