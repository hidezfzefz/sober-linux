/* Ghidra decompilation of target routine FUN_05bf95b5 @ 0x5bf95b5
 * REA evidence: ev_439b29f6ef315227952ac382be5de3af210057eaf8f25753f6b769c67041226b
 * Requested address: 0x5bf9827
 * Target-derived data, not source. Do not execute.
 */


/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

void FUN_05bf95b5(undefined8 param_1,code *param_2,undefined8 param_3)

{
  int iVar1;
  char cVar2;
  int *piVar3;
  int *piVar4;
  void *__dest;
  undefined8 uVar5;
  char extraout_DL;
  char extraout_DL_00;
  ulong uStack_880;
  undefined1 auStack_878 [8];
  char cStack_870;
  ulong uStack_868;
  undefined1 auStack_860 [8];
  char cStack_858;
  undefined1 *puStack_850;
  size_t sStack_848;
  undefined8 uStack_840;
  undefined1 auStack_838 [2048];
  long lStack_38;
  
  lStack_38 = ___stack_chk_guard;
  piVar3 = (int *)FUN_02111eb2(param_1,"zbase64",7);
  if ((extraout_DL == '\0') && ((*(ushort *)((long)piVar3 + 0xe) >> 10 & 1) != 0)) {
    piVar4 = piVar3;
    iVar1 = 0xd - ((int)(*(ulong *)(piVar3 + 2) >> 0x10) >> 0x18);
    if ((*(ushort *)((long)piVar3 + 0xe) >> 0xc & 1) == 0) {
      piVar4 = (int *)(*(ulong *)(piVar3 + 2) & 0xffffffffffff);
      iVar1 = *piVar3;
    }
    sStack_848 = 0;
    uStack_840 = 0x800;
    puStack_850 = auStack_838;
    cVar2 = FUN_05bfabeb(piVar4,iVar1,&puStack_850);
    if (cVar2 == '\0') {
                    /* WARNING: Subroutine does not return */
      FUN_023e6556("invalid zbase64 data");
    }
    FUN_022f5698(&uStack_868,puStack_850,sStack_848);
    if (cStack_858 == '\0') {
      if (uStack_868 <= (ulong)(long)DAT_06fcfc80) {
        uVar5 = (*param_2)(param_3,uStack_868);
        FUN_022f58f0(&uStack_880,uVar5,uStack_868,puStack_850,sStack_848);
        if (cStack_870 == '\0') {
          if (uStack_880 == uStack_868) {
            if (puStack_850 != auStack_838) {
              FUN_01bb4b03();
            }
            goto LAB_05bf96c2;
          }
        }
        else {
          FUN_04b863ce("ZSTD_decompress returned error {}",auStack_878);
        }
                    /* WARNING: Subroutine does not return */
        FUN_023e6556("ZSTD_decompress decompressed size mismatch");
      }
    }
    else {
      FUN_04b863ce("ZSTD_getFrameContentSize returned error {}",auStack_860);
    }
                    /* WARNING: Subroutine does not return */
    FUN_023e6556("invalid buffer size");
  }
  piVar3 = (int *)FUN_02111eb2(param_1,"base64",6);
  if ((extraout_DL_00 != '\0') || ((*(ushort *)((long)piVar3 + 0xe) >> 10 & 1) == 0)) {
                    /* WARNING: Subroutine does not return */
    FUN_023e6556("invalid buffer node");
  }
  piVar4 = piVar3;
  iVar1 = 0xd - ((int)(*(ulong *)(piVar3 + 2) >> 0x10) >> 0x18);
  if ((*(ushort *)((long)piVar3 + 0xe) >> 0xc & 1) == 0) {
    piVar4 = (int *)(*(ulong *)(piVar3 + 2) & 0xffffffffffff);
    iVar1 = *piVar3;
  }
  sStack_848 = 0;
  uStack_840 = 0x800;
  puStack_850 = auStack_838;
  cVar2 = FUN_05bfabeb(piVar4,iVar1,&puStack_850);
  if (cVar2 == '\0') {
                    /* WARNING: Subroutine does not return */
    FUN_023e6556("invalid base64 data");
  }
  __dest = (void *)(*param_2)(param_3,sStack_848);
  if (__dest != (void *)0x0) {
    memcpy(__dest,puStack_850,sStack_848);
  }
  if (puStack_850 != auStack_838) {
    FUN_01bb4b03();
  }
LAB_05bf96c2:
  if (___stack_chk_guard != lStack_38) {
                    /* WARNING: Subroutine does not return */
    __stack_chk_fail();
  }
  return;
}

