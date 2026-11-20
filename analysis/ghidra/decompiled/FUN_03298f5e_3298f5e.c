/* Ghidra decompilation of target routine FUN_03298f5e @ 0x3298f5e
 * REA evidence: ev_439b29f6ef315227952ac382be5de3af210057eaf8f25753f6b769c67041226b
 * Requested address: 0x3298fa9
 * Target-derived data, not source. Do not execute.
 */


void FUN_03298f5e(long param_1,undefined8 *param_2)

{
  undefined4 uVar1;
  long lVar2;
  undefined8 uVar3;
  
  if (*(long *)(param_1 + 0x10) == 0) {
    lVar2 = eglCreateWindowSurface
                      (*(undefined8 *)(param_1 + 0x20),*param_2,*(undefined8 *)(param_1 + 0x18),0);
  }
  else {
    lVar2 = eglCreatePbufferSurface(*(undefined8 *)(*(long *)(param_1 + 0x10) + 0x20),*param_2,0);
  }
  if (lVar2 != 0) {
    return;
  }
  uVar3 = FUN_02af7f10(0x10);
  uVar1 = eglGetError();
  FUN_0242f597(uVar3,"Error creating context: eglCreateWindowSurface %x",uVar1);
                    /* WARNING: Subroutine does not return */
  FUN_02af80c0(uVar3,&DAT_06acbc40,FUN_02ade2a0);
}

