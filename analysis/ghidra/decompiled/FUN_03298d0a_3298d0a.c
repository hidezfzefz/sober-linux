/* Ghidra decompilation of target routine FUN_03298d0a @ 0x3298d0a
 * REA evidence: ev_439b29f6ef315227952ac382be5de3af210057eaf8f25753f6b769c67041226b
 * Requested address: 0x3298f22
 * Target-derived data, not source. Do not execute.
 */


/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

undefined8 FUN_03298d0a(long param_1)

{
  byte bVar1;
  char cVar2;
  undefined4 uVar3;
  long lVar4;
  undefined8 uVar5;
  uint uVar6;
  undefined8 in_stack_ffffffffffffffb8;
  undefined8 uVar7;
  ulong uVar8;
  undefined8 uStack_38;
  long lStack_30;
  
  bVar1 = DAT_06f6dc68;
  lStack_30 = ___stack_chk_guard;
  lVar4 = *(long *)(param_1 + 0x10);
  if (*(long *)(param_1 + 0x10) == 0) {
    lVar4 = param_1;
  }
  uVar5 = *(undefined8 *)(lVar4 + 0x20);
  uVar6 = (uint)DAT_06f6dc68;
  uVar7 = CONCAT44((int)((ulong)in_stack_ffffffffffffffb8 >> 0x20),(uint)DAT_06f6dc68);
  cVar2 = FUN_032993c0(uVar5,&uStack_38,8,8,8,0,uVar7);
  if (cVar2 == '\0') {
    uVar7 = CONCAT44((int)((ulong)uVar7 >> 0x20),uVar6);
    cVar2 = FUN_032993c0(uVar5,&uStack_38,5,6,5,0,uVar7);
    if (cVar2 == '\0') {
      uVar7 = CONCAT44((int)((ulong)uVar7 >> 0x20),uVar6);
      cVar2 = FUN_032993c0(uVar5,&uStack_38,8,8,8,1,uVar7);
      if (cVar2 == '\0') {
        uVar8 = CONCAT44((int)((ulong)uVar7 >> 0x20),uVar6);
        cVar2 = FUN_032993c0(uVar5,&uStack_38,5,6,5,1,uVar8);
        if (cVar2 == '\0') {
          if (bVar1 == 0) {
LAB_03298f10:
            uVar5 = FUN_02af7f10(0x10);
            uVar3 = eglGetError();
            FUN_0242f597(uVar5,"Error creating context: could not find suitable config (%x)",uVar3);
                    /* WARNING: Subroutine does not return */
            FUN_02af80c0(uVar5,&DAT_06acbc40,FUN_02ade2a0);
          }
          uVar8 = uVar8 & 0xffffffff00000000;
          cVar2 = FUN_032993c0(uVar5,&uStack_38,8,8,8,0,uVar8);
          if (cVar2 == '\0') {
            uVar8 = uVar8 & 0xffffffff00000000;
            cVar2 = FUN_032993c0(uVar5,&uStack_38,5,6,5,0,uVar8);
            if (cVar2 == '\0') {
              uVar8 = uVar8 & 0xffffffff00000000;
              cVar2 = FUN_032993c0(uVar5,&uStack_38,8,8,8,1,uVar8);
              if (cVar2 == '\0') {
                cVar2 = FUN_032993c0(uVar5,&uStack_38,5,6,5,1,uVar8 & 0xffffffff00000000);
                if (cVar2 == '\0') goto LAB_03298f10;
              }
            }
          }
          if (2 < (byte)((ulong)DAT_06f6d788 >> 8) && 5 < (byte)DAT_06f6d788) {
            FUN_01fe2480(DAT_06f6d788,PTR_s_Graphics_06f6d790,
                         "[FLog::Graphics] No EGL config with 8-bit stencil; using stencil-free config, UI stencil clipping disabled on this device"
                         ,0,0,0,0,0);
          }
        }
      }
    }
  }
  if (___stack_chk_guard == lStack_30) {
    return uStack_38;
  }
                    /* WARNING: Subroutine does not return */
  __stack_chk_fail();
}

