/* Ghidra decompilation of target routine FUN_02a31960 @ 0x2a31960
 * REA evidence: ev_439b29f6ef315227952ac382be5de3af210057eaf8f25753f6b769c67041226b
 * Requested address: 0x2a326ff
 * Target-derived data, not source. Do not execute.
 */


/* WARNING: Type propagation algorithm not settling */

undefined8
FUN_02a31960(long param_1,long param_2,long param_3,int param_4,ulong *param_5,byte *param_6)

{
  byte bVar1;
  long *plVar2;
  undefined4 uVar3;
  undefined2 uVar4;
  undefined4 *puVar5;
  char acVar6 [4];
  undefined3 uVar7;
  char cVar8;
  long *plVar9;
  undefined4 *puVar10;
  char *pcVar11;
  long *plVar12;
  long *plVar13;
  char *pcVar14;
  undefined8 *puVar15;
  size_t sVar16;
  long lVar17;
  long lVar18;
  undefined8 uVar19;
  char *pcVar20;
  size_t __n;
  ulong uVar21;
  long lVar22;
  char *pcVar23;
  byte *pbVar24;
  undefined1 *puVar25;
  byte bVar26;
  undefined4 uVar27;
  undefined8 uVar28;
  size_t sVar29;
  long *plVar30;
  long in_FS_OFFSET;
  bool bVar31;
  long *plStack_260;
  long *plStack_258;
  undefined8 uStack_250;
  char *pcStack_248;
  byte bStack_240;
  undefined1 auStack_23f [7];
  ulong uStack_238;
  undefined1 *puStack_230;
  byte bStack_228;
  char acStack_227 [4];
  undefined1 auStack_223 [4];
  char acStack_21f [4];
  undefined4 uStack_21b;
  undefined1 uStack_217;
  undefined6 uStack_216;
  long *plStack_208;
  long alStack_200 [2];
  char cStack_1e9;
  undefined4 *puStack_1e8;
  undefined4 *puStack_1e0;
  undefined4 *puStack_1d8;
  ulong uStack_1c8;
  ulong uStack_1c0;
  ulong uStack_1b8;
  undefined1 auStack_1b0 [80];
  byte bStack_160;
  char acStack_15f [4];
  undefined3 uStack_15b;
  undefined1 auStack_158 [8];
  char cStack_150;
  undefined4 uStack_14f;
  undefined1 uStack_14b;
  undefined2 uStack_14a;
  long lStack_38;
  
  lStack_38 = *(long *)(in_FS_OFFSET + 0x28);
  plVar2 = *(long **)(param_2 + 0x70);
  if ((*param_5 & 1) == 0) {
    uStack_1b8 = param_5[2];
    uStack_1c8 = *param_5;
    uStack_1c0 = param_5[1];
    bVar1 = *param_6;
  }
  else {
    FUN_01bb7eda(&uStack_1c8,param_5[2],param_5[1]);
    bVar1 = *param_6;
  }
  if ((bVar1 & 1) == 0) {
    param_6[0] = 0;
    param_6[1] = 0;
  }
  else {
    **(undefined1 **)(param_6 + 0x10) = 0;
    param_6[8] = 0;
    param_6[9] = 0;
    param_6[10] = 0;
    param_6[0xb] = 0;
    param_6[0xc] = 0;
    param_6[0xd] = 0;
    param_6[0xe] = 0;
    param_6[0xf] = 0;
  }
  puStack_1e8 = (undefined4 *)0x0;
  puStack_1e0 = (undefined4 *)0x0;
  puStack_1d8 = (undefined4 *)0x0;
  auStack_158[1] = '\0';
  auStack_158[2] = '\0';
  auStack_158[3] = '\0';
  auStack_158[4] = '\0';
  auStack_158[5] = '\0';
  auStack_158[6] = '\0';
  auStack_158[7] = '\0';
  cStack_150 = '\0';
  uStack_14f = 0;
  uStack_14b = 0;
  uStack_14a = 0;
  bStack_160 = (byte)auStack_158;
  bVar1 = bStack_160;
  acStack_15f = SUB84((ulong)auStack_158 >> 8,0);
  acVar6 = acStack_15f;
  auStack_158[0] = '\0';
  uStack_15b = (undefined3)((ulong)auStack_158 >> 0x28);
  uVar7 = uStack_15b;
  plVar12 = *(long **)(param_2 + 0x98);
  plVar13 = (long *)(param_2 + 0xa0);
  if (plVar12 != plVar13) {
    do {
      FUN_02a2e620(&bStack_160,auStack_158,plVar12 + 4);
      plVar30 = (long *)plVar12[1];
      plVar9 = plVar12;
      if ((long *)plVar12[1] == (long *)0x0) {
        do {
          plVar12 = (long *)plVar9[2];
          bVar31 = (long *)*plVar12 != plVar9;
          plVar9 = plVar12;
        } while (bVar31);
      }
      else {
        do {
          plVar12 = plVar30;
          plVar30 = (long *)*plVar12;
        } while ((long *)*plVar12 != (long *)0x0);
      }
    } while (plVar12 != plVar13);
    plVar12 = (long *)CONCAT35(uStack_15b,CONCAT41(acStack_15f,bStack_160));
    if (plVar12 != (long *)auStack_158) {
      puVar10 = puStack_1e0;
      do {
        if (puVar10 < puStack_1d8) {
          if ((*(byte *)(plVar12 + 4) & 1) != 0) {
            FUN_01bb7eda(puVar10,plVar12[6],plVar12[5]);
            puVar10 = puVar10 + 6;
            plVar30 = (long *)plVar12[1];
            goto joined_r0x02a31b39;
          }
          *(long *)(puVar10 + 4) = plVar12[6];
          uVar27 = *(undefined4 *)((long)plVar12 + 0x24);
          lVar22 = plVar12[5];
          uVar3 = *(undefined4 *)((long)plVar12 + 0x2c);
          *puVar10 = *(undefined4 *)(plVar12 + 4);
          puVar10[1] = uVar27;
          puVar10[2] = (int)lVar22;
          puVar10[3] = uVar3;
          puVar10 = puVar10 + 6;
          plVar30 = (long *)plVar12[1];
          puStack_1e0 = puVar10;
          if ((long *)plVar12[1] == (long *)0x0) goto LAB_02a31b80;
LAB_02a31b70:
          do {
            plVar9 = plVar30;
            plVar30 = (long *)*plVar9;
          } while ((long *)*plVar9 != (long *)0x0);
        }
        else {
          puVar10 = (undefined4 *)FUN_01ff2c90(&puStack_1e8);
          plVar30 = (long *)plVar12[1];
joined_r0x02a31b39:
          puStack_1e0 = puVar10;
          if (plVar30 != (long *)0x0) goto LAB_02a31b70;
LAB_02a31b80:
          do {
            plVar9 = (long *)plVar12[2];
            bVar31 = (long *)*plVar9 != plVar12;
            plVar12 = plVar9;
          } while (bVar31);
        }
        plVar12 = plVar9;
      } while (plVar9 != (long *)auStack_158);
    }
  }
  FUN_02a21480(&bStack_160,CONCAT35(auStack_158._5_3_,CONCAT41(auStack_158._1_4_,auStack_158[0])));
  cStack_1e9 = '\0';
  FUN_02a30c70(&plStack_208,param_4,param_3,&puStack_1e8,&cStack_1e9);
  if ((param_4 == 1) && (cStack_1e9 == '\0')) {
    FUN_02a15cb0(&bStack_160,
                 "UploadResult crashpad::RbxSequentialUploader::UploadToDestination(const CrashReportDatabase::UploadReport *, const CrashUploadParameters &, CrashUploadDestination, const std::string &, std::string *)"
                 ,"../../../../../../handler/rbx_sequential_uploader.cc",0x6b,2);
    FUN_02a159d0(auStack_158,"Sequential Sentry upload requires application parameter mapping",0x3f)
    ;
    FUN_02a164f0(&bStack_160);
    uVar28 = 1;
  }
  else {
    cVar8 = FUN_02a68760(*(long *)(*plVar2 + -0x20) + (long)plVar2,0);
    uVar28 = 1;
    if (cVar8 != '\0') {
      FUN_02a32af0(auStack_1b0);
      FUN_02a32f50(auStack_1b0,*(undefined1 *)(param_1 + 0x20));
      if (plStack_208 != alStack_200) {
        plVar12 = plStack_208;
        do {
          FUN_02a32f90(auStack_1b0,plVar12 + 4,plVar12 + 7);
          plVar30 = (long *)plVar12[1];
          plVar9 = plVar12;
          if ((long *)plVar12[1] == (long *)0x0) {
            do {
              plVar12 = (long *)plVar9[2];
              bVar31 = (long *)*plVar12 != plVar9;
              plVar9 = plVar12;
            } while (bVar31);
          }
          else {
            do {
              plVar12 = plVar30;
              plVar30 = (long *)*plVar12;
            } while ((long *)*plVar12 != (long *)0x0);
          }
        } while (plVar12 != alStack_200);
      }
      if (param_4 == 1) {
        FUN_02a69760(&bStack_160,param_2 + 8);
        sVar29 = (size_t)(bStack_160 >> 1);
        bVar26 = bStack_160 & 1;
        sVar16 = CONCAT35(auStack_158._5_3_,CONCAT41(auStack_158._1_4_,auStack_158[0]));
        pcVar14 = (char *)CONCAT26(uStack_14a,CONCAT15(uStack_14b,CONCAT41(uStack_14f,cStack_150)));
        __n = sVar16;
        pcVar11 = pcVar14;
        if (bVar26 == 0) {
          __n = sVar29;
          pcVar11 = acStack_15f;
        }
        pcVar23 = pcVar11 + __n;
        pcVar11 = memchr(pcVar11,0x2d,__n);
        if (pcVar11 == (char *)0x0) {
          pcVar11 = pcVar23;
        }
        if ((pcVar11 != pcVar23) && (pcVar20 = pcVar11 + 1, pcVar20 != pcVar23)) {
          do {
            if (*pcVar20 != '-') {
              *pcVar11 = *pcVar20;
              pcVar11 = pcVar11 + 1;
            }
            pcVar20 = pcVar20 + 1;
          } while (pcVar23 != pcVar20);
          sVar16 = CONCAT35(auStack_158._5_3_,CONCAT41(auStack_158._1_4_,auStack_158[0]));
          pcVar14 = (char *)CONCAT26(uStack_14a,CONCAT15(uStack_14b,CONCAT41(uStack_14f,cStack_150))
                                    );
          bVar26 = bStack_160 & 1;
          sVar29 = (size_t)(bStack_160 >> 1);
        }
        pcVar23 = acStack_15f;
        if (bVar26 != 0) {
          sVar29 = sVar16;
          pcVar23 = pcVar14;
        }
        FUN_02aaae30(&bStack_160,(long)pcVar11 - (long)pcVar23,pcVar23 + (sVar29 - (long)pcVar11));
        bStack_228 = 0x20;
        builtin_strncpy(acStack_227,"sent",4);
        auStack_223[0] = 'r';
        auStack_223[1] = 'y';
        auStack_223[2] = '[';
        auStack_223[3] = 'e';
        builtin_strncpy(acStack_21f,"vent",4);
        uStack_21b._0_1_ = '_';
        uStack_21b._1_1_ = 'i';
        uStack_21b._2_1_ = 'd';
        uStack_21b._3_1_ = ']';
        uStack_217 = 0;
        FUN_02a32f90(auStack_1b0,&bStack_228,&bStack_160);
        if ((bStack_228 & 1) != 0) {
          FUN_069db734(CONCAT62(uStack_216,CONCAT11(uStack_217,uStack_21b._3_1_)));
        }
        if ((bStack_160 & 1) != 0) {
          FUN_069db734(CONCAT26(uStack_14a,CONCAT15(uStack_14b,CONCAT41(uStack_14f,cStack_150))));
        }
      }
      auStack_158[1] = '\0';
      auStack_158[2] = '\0';
      auStack_158[3] = '\0';
      auStack_158[4] = '\0';
      auStack_158[5] = '\0';
      auStack_158[6] = '\0';
      auStack_158[7] = '\0';
      cStack_150 = '\0';
      uStack_14f = 0;
      uStack_14b = 0;
      uStack_14a = 0;
      auStack_158[0] = '\0';
      plVar12 = *(long **)(param_2 + 0x98);
      bStack_160 = bVar1;
      acStack_15f = acVar6;
      uStack_15b = uVar7;
      if (plVar12 != plVar13) {
        do {
          FUN_02a2e620(&bStack_160,auStack_158,plVar12 + 4);
          plVar30 = (long *)plVar12[1];
          plVar9 = plVar12;
          if ((long *)plVar12[1] == (long *)0x0) {
            do {
              plVar12 = (long *)plVar9[2];
              bVar31 = (long *)*plVar12 != plVar9;
              plVar9 = plVar12;
            } while (bVar31);
          }
          else {
            do {
              plVar12 = plVar30;
              plVar30 = (long *)*plVar12;
            } while ((long *)*plVar12 != (long *)0x0);
          }
        } while (plVar12 != plVar13);
        plVar13 = (long *)CONCAT35(uStack_15b,CONCAT41(acStack_15f,bStack_160));
        if (plVar13 != (long *)auStack_158) {
          do {
            cVar8 = FUN_02a68760(plVar13[7] + *(long *)(*(long *)plVar13[7] + -0x20),0);
            if (cVar8 == '\0') {
              FUN_02a21480(&bStack_160,
                           CONCAT35(auStack_158._5_3_,CONCAT41(auStack_158._1_4_,auStack_158[0])));
              goto LAB_02a32784;
            }
            lVar22 = plVar13[7];
            pcVar14 = (char *)FUN_01bb0f6a(0x20);
            uStack_21b._3_1_ = (byte)pcVar14;
            uStack_217 = (undefined1)((ulong)pcVar14 >> 8);
            uStack_216 = (undefined6)((ulong)pcVar14 >> 0x10);
            bStack_228 = 0x21;
            acStack_227[0] = '\0';
            acStack_227[1] = '\0';
            acStack_227[2] = '\0';
            acStack_227[3] = '\0';
            auStack_223 = (undefined1  [4])0x18000000;
            acStack_21f[0] = '\0';
            acStack_21f[1] = '\0';
            acStack_21f[2] = '\0';
            acStack_21f[3] = '\0';
            uStack_21b = (uint)uStack_21b._3_1_ << 0x18;
            builtin_strncpy(pcVar14,"application/octet-stream",0x19);
            FUN_02a33120(auStack_1b0,plVar13 + 4,plVar13 + 4,lVar22,&bStack_228);
            if ((bStack_228 & 1) != 0) {
              FUN_069db734(CONCAT62(uStack_216,CONCAT11(uStack_217,uStack_21b._3_1_)));
            }
            plVar12 = (long *)plVar13[1];
            plVar30 = plVar13;
            if ((long *)plVar13[1] == (long *)0x0) {
              do {
                plVar13 = (long *)plVar30[2];
                bVar31 = (long *)*plVar13 != plVar30;
                plVar30 = plVar13;
              } while (bVar31);
            }
            else {
              do {
                plVar13 = plVar12;
                plVar12 = (long *)*plVar13;
              } while ((long *)*plVar13 != (long *)0x0);
            }
          } while (plVar13 != (long *)auStack_158);
        }
      }
      FUN_02a21480(&bStack_160,
                   CONCAT35(auStack_158._5_3_,CONCAT41(auStack_158._1_4_,auStack_158[0])));
      bStack_160 = 0x28;
      builtin_strncpy(acStack_15f,"uplo",4);
      uStack_15b = 0x5f6461;
      auStack_158[0] = 'f';
      auStack_158[1] = 'i';
      auStack_158[2] = 'l';
      auStack_158[3] = 'e';
      auStack_158[4] = '_';
      auStack_158[5] = 'm';
      auStack_158[6] = 'i';
      auStack_158[7] = 'n';
      cStack_150 = 'i';
      uStack_14f = 0x706d7564;
      uStack_14b = 0;
      param_2 = param_2 + 8;
      FUN_02a69760(&bStack_240);
      puVar15 = (undefined8 *)FUN_02aab580(&bStack_240,&DAT_005a8ef8);
      uVar28 = puVar15[2];
      uStack_21b._3_1_ = (byte)uVar28;
      uStack_217 = (undefined1)((ulong)uVar28 >> 8);
      uStack_216 = (undefined6)((ulong)uVar28 >> 0x10);
      uVar28 = *puVar15;
      uVar19 = puVar15[1];
      bStack_228 = (byte)uVar28;
      acStack_227 = SUB84((ulong)uVar28 >> 8,0);
      auStack_223._0_3_ = (undefined3)((ulong)uVar28 >> 0x28);
      auStack_223[3] = (char)uVar19;
      acStack_21f = SUB84((ulong)uVar19 >> 8,0);
      uStack_21b._0_3_ = (undefined3)((ulong)uVar19 >> 0x28);
      *puVar15 = 0;
      puVar15[1] = 0;
      puVar15[2] = 0;
      pcStack_248 = (char *)FUN_01bb0f6a(0x20);
      plStack_258 = (long *)&DAT_00000021;
      uStack_250 = 0x18;
      builtin_strncpy(pcStack_248,"application/octet-stream",0x19);
      FUN_02a33120(auStack_1b0,&bStack_160,&bStack_228,plVar2,&plStack_258);
      if (((ulong)plStack_258 & 1) != 0) {
        FUN_069db734(pcStack_248);
      }
      if ((bStack_228 & 1) != 0) {
        uStack_21b._3_1_ = (byte)((uint)uStack_21b >> 0x18);
        uVar4 = CONCAT11(uStack_217,uStack_21b._3_1_);
        FUN_069db734(CONCAT62(uStack_216,uVar4));
      }
      if ((bStack_240 & 1) != 0) {
        FUN_069db734(puStack_230);
      }
      if ((bStack_160 & 1) != 0) {
        FUN_069db734(CONCAT26(uStack_14a,CONCAT15(uStack_14b,CONCAT41(uStack_14f,cStack_150))));
      }
      FUN_02a32920(&plStack_258,*(undefined8 *)(param_1 + 0x18));
      if (plStack_258 == (long *)0x0) {
LAB_02a32784:
        uVar28 = 1;
      }
      else {
        plVar2 = (long *)(auStack_223 + 3);
        acStack_21f[0] = '\0';
        acStack_21f[1] = '\0';
        acStack_21f[2] = '\0';
        acStack_21f[3] = '\0';
        uStack_21b = 0;
        uStack_217 = 0;
        uStack_216 = 0;
        bStack_228 = (byte)plVar2;
        acStack_227 = SUB84((ulong)plVar2 >> 8,0);
        auStack_223._0_3_ = (undefined3)((ulong)plVar2 >> 0x28);
        auStack_223[3] = 0;
        FUN_02a33f00(auStack_1b0,&bStack_228);
        plVar13 = (long *)CONCAT35(auStack_223._0_3_,CONCAT41(acStack_227,bStack_228));
        plVar12 = plStack_258;
        while (plStack_258 = plVar12, plVar13 != plVar2) {
          FUN_02a35520(plVar12,plVar13 + 4,plVar13 + 7);
          plVar30 = (long *)plVar13[1];
          plVar9 = plVar13;
          plVar12 = plStack_258;
          if ((long *)plVar13[1] == (long *)0x0) {
            do {
              plVar13 = (long *)plVar9[2];
              bVar31 = (long *)*plVar13 != plVar9;
              plVar9 = plVar13;
            } while (bVar31);
          }
          else {
            do {
              plVar13 = plVar30;
              plVar30 = (long *)*plVar13;
            } while ((long *)*plVar13 != (long *)0x0);
          }
        }
        FUN_02a33450(&plStack_260,auStack_1b0);
        FUN_02a35580(plVar12,&plStack_260);
        plVar2 = plStack_260;
        plStack_260 = (long *)0x0;
        if (plVar2 != (long *)0x0) {
          (**(code **)(*plVar2 + 8))();
        }
        FUN_02a355f0(0,plStack_258);
        if ((param_4 != 1) && (*(char *)(param_1 + 0x21) != '\0')) {
          lVar22 = 8;
          do {
            pcVar14 = *(char **)(&UNK_06ac5b28 + lVar22);
            sVar16 = strlen(pcVar14);
            if (0xfffffffffffffff7 < sVar16) {
                    /* WARNING: Subroutine does not return */
              FUN_02a15970(&bStack_160);
            }
            if (sVar16 < 0x17) {
              bStack_160 = (char)sVar16 * '\x02';
              pcVar11 = acStack_15f;
              if (sVar16 != 0) goto LAB_02a322f1;
            }
            else {
              uVar21 = sVar16 | 7;
              if ((sVar16 | 7) == 0x17) {
                uVar21 = (sVar16 & 0xfffffffffffffff8) + 8;
              }
              lVar17 = uVar21 + 1;
              pcVar11 = (char *)FUN_01bb0f6a(lVar17);
              cStack_150 = (char)pcVar11;
              uStack_14f = (undefined4)((ulong)pcVar11 >> 8);
              uStack_14b = (undefined1)((ulong)pcVar11 >> 0x28);
              uStack_14a = (undefined2)((ulong)pcVar11 >> 0x30);
              bStack_160 = (byte)lVar17 | 1;
              acStack_15f = SUB84((ulong)lVar17 >> 8,0);
              uStack_15b = (undefined3)((ulong)lVar17 >> 0x28);
              auStack_158._1_4_ = (undefined4)(sVar16 >> 8);
              auStack_158._5_3_ = (undefined3)(sVar16 >> 0x28);
              auStack_158[0] = (char)sVar16;
LAB_02a322f1:
              memmove(pcVar11,pcVar14,sVar16);
            }
            pcVar11[sVar16] = '\0';
            lVar17 = FUN_02110358(param_3,&bStack_160);
            if ((bStack_160 & 1) != 0) {
              FUN_069db734(CONCAT26(uStack_14a,CONCAT15(uStack_14b,CONCAT41(uStack_14f,cStack_150)))
                          );
            }
            if (lVar17 != param_3 + 8) {
              lVar18 = FUN_02aaa970(&uStack_1c8,0x3f,0);
              uVar27 = 0x26;
              if (lVar18 == -1) {
                uVar27 = 0x3f;
              }
              uVar28 = *(undefined8 *)((long)&PTR_DAT_06ac5b30 + lVar22);
              FUN_02a35630(&bStack_240,lVar17 + 0x38);
              puVar25 = auStack_23f;
              if ((bStack_240 & 1) != 0) {
                puVar25 = puStack_230;
              }
              FUN_02a19e90(&bStack_160,"%c%s=%s",uVar27,uVar28,puVar25);
              bVar31 = (bStack_160 & 1) == 0;
              pcVar14 = (char *)CONCAT26(uStack_14a,
                                         CONCAT15(uStack_14b,CONCAT41(uStack_14f,cStack_150)));
              if (bVar31) {
                pcVar14 = acStack_15f;
              }
              uVar21 = (ulong)(bStack_160 >> 1);
              if (!bVar31) {
                uVar21 = CONCAT35(auStack_158._5_3_,CONCAT41(auStack_158._1_4_,auStack_158[0]));
              }
              FUN_02aaa730(&uStack_1c8,pcVar14,uVar21);
              if ((bStack_160 & 1) != 0) {
                FUN_069db734(CONCAT26(uStack_14a,
                                      CONCAT15(uStack_14b,CONCAT41(uStack_14f,cStack_150))));
              }
              if ((bStack_240 & 1) != 0) {
                FUN_069db734(puStack_230);
              }
            }
            lVar22 = lVar22 + 0x10;
          } while (lVar22 != 0x58);
        }
        FUN_02a15cb0(&bStack_160,
                     "UploadResult crashpad::RbxSequentialUploader::UploadToDestination(const CrashReportDatabase::UploadReport *, const CrashUploadParameters &, CrashUploadDestination, const std::string &, std::string *)"
                     ,"../../../../../../handler/rbx_sequential_uploader.cc",0xc3,0);
        uVar28 = FUN_02a159d0(auStack_158,"Uploading report ",0x11);
        FUN_02a69760(&bStack_240,param_2);
        uVar21 = (ulong)(bStack_240 >> 1);
        puVar25 = auStack_23f;
        if ((bStack_240 & 1) != 0) {
          uVar21 = uStack_238;
          puVar25 = puStack_230;
        }
        uVar28 = FUN_02a159d0(uVar28,puVar25,uVar21);
        uVar28 = FUN_02a159d0(uVar28,&DAT_00536013,4);
        pcVar14 = "Backtrace";
        if (param_4 == 1) {
          pcVar14 = "Sentry";
        }
        uVar28 = FUN_02a159d0(uVar28,pcVar14,(param_4 != 1) * '\x03' + '\x06');
        FUN_02a159d0(uVar28,&DAT_003dc4ab,1);
        if ((bStack_240 & 1) != 0) {
          FUN_069db734(puStack_230);
        }
        FUN_02a164f0(&bStack_160);
        FUN_02a354e0(plStack_258,&uStack_1c8);
        cVar8 = (**(code **)(*plStack_258 + 0x10))(plStack_258,param_6);
        if (cVar8 == '\0') {
          FUN_02a15cb0(&bStack_160,
                       "UploadResult crashpad::RbxSequentialUploader::UploadToDestination(const CrashReportDatabase::UploadReport *, const CrashUploadParameters &, CrashUploadDestination, const std::string &, std::string *)"
                       ,"../../../../../../handler/rbx_sequential_uploader.cc",0xe0,1);
          uVar28 = FUN_02a159d0(auStack_158,"Failed to upload report ",0x18);
          FUN_02a69760(&bStack_240,param_2);
          uVar21 = (ulong)(bStack_240 >> 1);
          puVar25 = auStack_23f;
          if ((bStack_240 & 1) != 0) {
            uVar21 = uStack_238;
            puVar25 = puStack_230;
          }
          FUN_02a159d0(uVar28,puVar25,uVar21);
          if ((bStack_240 & 1) != 0) {
            FUN_069db734(puStack_230);
          }
          FUN_02a164f0(&bStack_160);
          FUN_02a15cb0(&bStack_160,
                       "UploadResult crashpad::RbxSequentialUploader::UploadToDestination(const CrashReportDatabase::UploadReport *, const CrashUploadParameters &, CrashUploadDestination, const std::string &, std::string *)"
                       ,"../../../../../../handler/rbx_sequential_uploader.cc",0xe1,1);
          uVar28 = FUN_02a159d0(auStack_158,"The HTTP response is: ",0x16);
          bVar31 = (*param_6 & 1) != 0;
          pbVar24 = param_6 + 1;
          if (bVar31) {
            pbVar24 = *(byte **)(param_6 + 0x10);
          }
          uVar21 = (ulong)(*param_6 >> 1);
          if (bVar31) {
            uVar21 = *(ulong *)(param_6 + 8);
          }
          FUN_02a159d0(uVar28,pbVar24,uVar21);
          FUN_02a164f0(&bStack_160);
          uVar28 = 2;
        }
        else {
          uVar28 = 0;
          FUN_02a15cb0(&bStack_160,
                       "UploadResult crashpad::RbxSequentialUploader::UploadToDestination(const CrashReportDatabase::UploadReport *, const CrashUploadParameters &, CrashUploadDestination, const std::string &, std::string *)"
                       ,"../../../../../../handler/rbx_sequential_uploader.cc",0xdd,0);
          uVar19 = FUN_02a159d0(auStack_158,"Successfully uploaded report ",0x1d);
          FUN_02a69760(&bStack_240,param_2);
          uVar21 = (ulong)(bStack_240 >> 1);
          puVar25 = auStack_23f;
          if ((bStack_240 & 1) != 0) {
            uVar21 = uStack_238;
            puVar25 = puStack_230;
          }
          FUN_02a159d0(uVar19,puVar25,uVar21);
          if ((bStack_240 & 1) != 0) {
            FUN_069db734(puStack_230);
          }
          FUN_02a164f0(&bStack_160);
        }
        FUN_01ff52ae(&bStack_228,
                     CONCAT35((undefined3)uStack_21b,CONCAT41(acStack_21f,auStack_223[3])));
        plVar2 = plStack_258;
        plStack_258 = (long *)0x0;
        if (plVar2 != (long *)0x0) {
          (**(code **)(*plVar2 + 8))();
        }
      }
      FUN_02a32ee0(auStack_1b0);
    }
  }
  FUN_01ff52ae(&plStack_208,alStack_200[0]);
  puVar5 = puStack_1e8;
  puVar10 = puStack_1e0;
  if (puStack_1e8 != (undefined4 *)0x0) {
    for (; puVar10 != puVar5; puVar10 = puVar10 + -6) {
      if ((*(byte *)(puVar10 + -6) & 1) != 0) {
        FUN_069db734(*(undefined8 *)(puVar10 + -2));
      }
    }
    puStack_1e0 = puVar5;
    FUN_069db734(puStack_1e8);
  }
  if ((uStack_1c8 & 1) != 0) {
    FUN_069db734(uStack_1b8);
  }
  if (*(long *)(in_FS_OFFSET + 0x28) != lStack_38) {
                    /* WARNING: Subroutine does not return */
    __stack_chk_fail();
  }
  return uVar28;
}

