/* Ghidra decompilation of target routine FUN_02c8d8fa @ 0x2c8d8fa
 * REA evidence: ev_439b29f6ef315227952ac382be5de3af210057eaf8f25753f6b769c67041226b
 * Requested address: 0x2c8dac7
 * Target-derived data, not source. Do not execute.
 */


/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

undefined8 FUN_02c8d8fa(void)

{
  long *plVar1;
  char cVar2;
  int iVar3;
  long lVar4;
  undefined8 uVar5;
  long *plStack_30;
  long lStack_28;
  
  lStack_28 = ___stack_chk_guard;
  plStack_30 = (long *)0x0;
  plVar1 = *(long **)(PTR_DAT_06f4f6c0 + 0x1b8);
  if (plVar1 != (long *)0x0) {
    iVar3 = (**(code **)(*plVar1 + 0x30))(plVar1,&plStack_30,0x10006);
    if (iVar3 == 0) {
      lVar4 = (**(code **)(*plStack_30 + 0x388))(plStack_30,DAT_0706d2a0,"checkInit",&DAT_004a577e);
      if (lVar4 != 0) {
        uVar5 = 0;
        cVar2 = FUN_02146e6a(plStack_30,DAT_0706d2a0,lVar4);
        if ((cVar2 == '\0') ||
           (*(undefined **)(PTR_DAT_06f4f6c0 + 0x1b0) = &DAT_0706d248, DAT_0706d258 != 0))
        goto LAB_02c8d94b;
        DAT_0706d258 = dlopen("libandroid.so",1);
        if (DAT_0706d258 == 0) {
          uVar5 = 0x12;
          goto LAB_02c8d94b;
        }
        _DAT_0706d260 = dlsym(DAT_0706d258,"AAssetManager_open");
        if (_DAT_0706d260 != 0) {
          uVar5 = 0x1f;
          if (DAT_0706d258 == 0) goto LAB_02c8d94b;
          _DAT_0706d268 = dlsym(DAT_0706d258,"AAsset_read");
          if (_DAT_0706d268 != 0) {
            if (DAT_0706d258 == 0) goto LAB_02c8d94b;
            _DAT_0706d270 = dlsym(DAT_0706d258,"AAsset_seek");
            if (_DAT_0706d270 != 0) {
              if (DAT_0706d258 == 0) goto LAB_02c8d94b;
              _DAT_0706d278 = dlsym(DAT_0706d258,"AAsset_close");
              if (_DAT_0706d278 != 0) {
                if (DAT_0706d258 == 0) goto LAB_02c8d94b;
                _DAT_0706d280 = dlsym(DAT_0706d258,"AAsset_getLength");
                if (_DAT_0706d280 != 0) {
                  if (DAT_0706d258 == 0) goto LAB_02c8d94b;
                  DAT_0706d288 = (code *)dlsym(DAT_0706d258,"AAssetManager_fromJava");
                  uVar5 = 0xd;
                  if (DAT_0706d288 == (code *)0x0) goto LAB_02c8d94b;
                  lVar4 = (**(code **)(*plStack_30 + 0x388))
                                    (plStack_30,DAT_0706d2a0,"getAssetManager",
                                     "()Landroid/content/res/AssetManager;");
                  if (lVar4 != 0) {
                    lVar4 = FUN_01fdc8de(plStack_30,DAT_0706d2a0,lVar4);
                    uVar5 = 0x1c;
                    if (lVar4 != 0) {
                      _DAT_0706d250 = (**(code **)(*plStack_30 + 0xa8))(plStack_30,lVar4);
                      _DAT_0706d248 = (*DAT_0706d288)(plStack_30,_DAT_0706d250);
                      (**(code **)(*plStack_30 + 0xb8))(plStack_30,lVar4);
                      uVar5 = 0;
                    }
                    goto LAB_02c8d94b;
                  }
                  goto LAB_02c8d946;
                }
              }
            }
          }
        }
        uVar5 = 0xd;
        goto LAB_02c8d94b;
      }
    }
  }
LAB_02c8d946:
  uVar5 = 0x1c;
LAB_02c8d94b:
  if (___stack_chk_guard == lStack_28) {
    return uVar5;
  }
                    /* WARNING: Subroutine does not return */
  __stack_chk_fail();
}

