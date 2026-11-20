/* Ghidra decompilation of target routine FUN_03297ea6 @ 0x3297ea6
 * REA evidence: ev_439b29f6ef315227952ac382be5de3af210057eaf8f25753f6b769c67041226b
 * Requested address: 0x3298c35
 * Target-derived data, not source. Do not execute.
 */


/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

void FUN_03297ea6(undefined8 *param_1,undefined8 param_2,undefined8 *param_3)

{
  byte bVar1;
  undefined *puVar2;
  undefined4 uVar3;
  int iVar4;
  int iVar5;
  uint uVar6;
  long lVar7;
  undefined8 uVar8;
  undefined8 *puVar9;
  undefined8 uVar10;
  char *pcVar11;
  uint uVar12;
  undefined8 uStack_68;
  uint uStack_5c;
  undefined8 uStack_58;
  ulong *puStack_50;
  ulong auStack_48 [2];
  long lStack_38;
  
  lStack_38 = ___stack_chk_guard;
  *(undefined4 *)(param_1 + 1) = 0;
  *param_1 = &DAT_06b290c0;
  param_1[2] = param_3;
  *(undefined1 *)(param_1 + 8) = 0;
  *(undefined8 *)((long)param_1 + 0x44) = 0;
  uVar3 = FUN_01c6cfc8(DAT_070826d4);
  if (param_3 == (undefined8 *)0x0) {
    param_1[3] = param_2;
    ANativeWindow_acquire(param_2);
    puVar2 = PTR_s_Graphics_06f6d790;
    uVar8 = DAT_06f6d788;
    if (2 < (byte)((ulong)DAT_06f6d788 >> 8) && 5 < (byte)DAT_06f6d788) {
      iVar4 = ANativeWindow_getWidth(param_1[3]);
      iVar5 = ANativeWindow_getHeight(param_1[3]);
      FUN_01fe2480(uVar8,puVar2,"[FLog::Graphics] Window size: %dx%d",(long)iVar4,(long)iVar5,0,0,0)
      ;
    }
    lVar7 = eglGetDisplay(0);
    param_1[4] = lVar7;
    if (lVar7 == 0) {
      uVar8 = FUN_02af7f10(0x10);
      uVar3 = eglGetError();
      FUN_0242f597(uVar8,"Error creating context: eglGetDisplay %x",uVar3);
    }
    else {
      iVar4 = eglInitialize(lVar7,0,0);
      if (iVar4 != 0) goto LAB_03297f96;
      uVar8 = FUN_02af7f10(0x10);
      uVar3 = eglGetError();
      FUN_0242f597(uVar8,"Error creating context: eglInitialize %x",uVar3);
    }
                    /* WARNING: Subroutine does not return */
    FUN_02af80c0(uVar8,&DAT_06acbc40,FUN_02ade2a0);
  }
LAB_03297f96:
  uVar8 = FUN_03298d0a(param_1);
  uStack_68 = uVar8;
  if (DAT_06f6dc68 == '\0') {
    uVar12 = 0;
  }
  else {
    uStack_5c = 0;
    puVar9 = (undefined8 *)param_1[2];
    if ((undefined8 *)param_1[2] == (undefined8 *)0x0) {
      puVar9 = param_1;
    }
    iVar4 = eglGetConfigAttrib(puVar9[4],uVar8,0x3026);
    puVar2 = PTR_s_Graphics_06f6d790;
    uVar10 = DAT_06f6d788;
    bVar1 = (byte)((ulong)DAT_06f6d788 >> 8);
    if (iVar4 == 0) {
      uVar12 = 0;
      if (2 < bVar1 && 5 < (byte)DAT_06f6d788) {
        uVar6 = eglGetError();
        auStack_48[0] = (ulong)uVar6;
        puStack_50 = auStack_48;
        uStack_58 = 1;
        FUN_01ff2e72(uVar10,puVar2,3,
                     "[FLog::Graphics] eglGetConfigAttrib(EGL_STENCIL_SIZE) failed: {:#x}. UI stencil clipping is disabled"
                     ,100,1,1,puStack_50);
      }
    }
    else {
      uVar12 = uStack_5c;
      if ((((int)uStack_5c < 8) && (param_1[2] == 0)) && (2 < bVar1 && 5 < (byte)DAT_06f6d788)) {
        puStack_50 = auStack_48;
        uStack_58 = 1;
        auStack_48[0] = (ulong)uStack_5c;
        FUN_01ff2e72(DAT_06f6d788,PTR_s_Graphics_06f6d790,3,
                     "[FLog::Graphics] EGL config has {} stencil bits, 8 requested. UI stencil clipping is disabled"
                     ,0x5d,1,1,puStack_50);
        uVar12 = uStack_5c;
      }
    }
  }
  *(uint *)(param_1 + 9) = uVar12;
  puVar9 = param_3;
  if (param_3 == (undefined8 *)0x0) {
    puVar9 = param_1;
  }
  if (param_3 == (undefined8 *)0x0) {
    uVar10 = 0;
  }
  else {
    uVar10 = param_3[6];
  }
  lVar7 = eglCreateContext(puVar9[4],uVar8,uVar10,&DAT_0088b864);
  param_1[6] = lVar7;
  if (lVar7 == 0) {
    uVar8 = FUN_02af7f10(0x10);
    uVar3 = eglGetError();
    FUN_0242f597(uVar8,"Error creating context: eglCreateContext %x",uVar3);
  }
  else {
    if (2 < (byte)((ulong)DAT_06f6d788 >> 8) && 5 < (byte)DAT_06f6d788) {
      pcVar11 = "shared";
      if (param_3 == (undefined8 *)0x0) {
        pcVar11 = "";
      }
      FUN_02149b32(DAT_06f6d788,PTR_s_Graphics_06f6d790,"[FLog::Graphics] Created %s context %p",
                   pcVar11,lVar7);
    }
    uVar10 = FUN_03298f5e(param_1,&uStack_68);
    param_1[5] = uVar10;
    iVar4 = eglMakeCurrent(puVar9[4],uVar10,uVar10,param_1[6]);
    if (iVar4 != 0) {
      if (param_3 == (undefined8 *)0x0) {
        eglQuerySurface(param_1[4],param_1[5],0x3057,param_1 + 7);
        eglQuerySurface(param_1[4],param_1[5],0x3056,(long)param_1 + 0x3c);
        if (2 < (byte)((ulong)DAT_06f6d788 >> 8) && 5 < (byte)DAT_06f6d788) {
          FUN_01fe2480(DAT_06f6d788,PTR_s_Graphics_06f6d790,
                       "[FLog::Graphics] Initialized EGL context %p (surface %p) with renderbuffer %dx%d"
                       ,param_1[6],param_1[5],(long)*(int *)(param_1 + 7),
                       (long)*(int *)((long)param_1 + 0x3c),0);
        }
        uStack_58 = CONCAT44(uStack_58._4_4_,0xffffffff);
        eglGetConfigAttrib(param_1[4],uVar8,0x303b);
        if (2 < (byte)((ulong)DAT_06f6d788 >> 8) && 5 < (byte)DAT_06f6d788) {
          FUN_01fe2480(DAT_06f6d788,PTR_s_Graphics_06f6d790,
                       "[FLog::Graphics] EGL_MIN_SWAP_INTERVAL: %d",(long)(int)uStack_58,0,0,0,0);
        }
        if (DAT_06f6dd38 == '\0') {
          iVar4 = 0;
        }
        else {
          iVar4 = 1;
          if (DAT_06f6dd50 != '\0') {
            pcVar11 = (char *)glGetString(0x1f02);
            iVar5 = strncmp(pcVar11,"OpenGL ES ",10);
            if ((iVar5 == 0) && ('2' < pcVar11[10])) {
              iVar4 = DAT_06f6dd68;
            }
          }
        }
        if (2 < (byte)((ulong)DAT_06f6d788 >> 8) && 5 < (byte)DAT_06f6d788) {
          FUN_01fe2480(DAT_06f6d788,PTR_s_Graphics_06f6d790,"[FLog::Graphics] eglSwapInterval(%d)",
                       (long)iVar4,0,0,0,0);
        }
        iVar5 = eglSwapInterval(param_1[4],iVar4);
        if ((iVar5 == 0) && (2 < (byte)((ulong)DAT_06f6d788 >> 8) && 5 < (byte)DAT_06f6d788)) {
          FUN_01fe2480(DAT_06f6d788,PTR_s_Graphics_06f6d790,
                       "[FLog::Graphics] *** eglSwapInterval(%d) EGL_FALSE",(long)iVar4,0,0,0,0);
        }
        *(int *)((long)param_1 + 0x44) = iVar4;
      }
      FUN_03298fe5(&DAT_07082848,"glBindVertexArray","glBindVertexArrayOES");
      FUN_03298fe5(&DAT_07082850,"glDeleteVertexArrays","glDeleteVertexArraysOES");
      FUN_03298fe5(&DAT_07082858,"glGenVertexArrays","glGenVertexArraysOES");
      if (DAT_07082860 == 0) {
        DAT_07082860 = eglGetProcAddress("glMapBuffer");
        if (DAT_07082860 == 0) {
          DAT_07082860 = eglGetProcAddress("glMapBufferOES");
        }
      }
      if (DAT_07082868 == 0) {
        DAT_07082868 = eglGetProcAddress("glUnmapBuffer");
        if (DAT_07082868 == 0) {
          DAT_07082868 = eglGetProcAddress("glUnmapBufferOES");
        }
      }
      if (DAT_07082870 == 0) {
        DAT_07082870 = eglGetProcAddress("glMapBufferRange");
        if (DAT_07082870 == 0) {
          DAT_07082870 = eglGetProcAddress("glMapBufferRangeEXT");
        }
      }
      if (DAT_07082880 == 0) {
        DAT_07082880 = eglGetProcAddress("glTexImage3D");
        if (DAT_07082880 == 0) {
          DAT_07082880 = eglGetProcAddress("glTexImage3DOES");
        }
      }
      if (DAT_07082888 == 0) {
        DAT_07082888 = eglGetProcAddress("glTexSubImage3D");
        if (DAT_07082888 == 0) {
          DAT_07082888 = eglGetProcAddress("glTexSubImage3DOES");
        }
      }
      if (DAT_07082890 == 0) {
        DAT_07082890 = eglGetProcAddress("glCompressedTexImage3D");
        if (DAT_07082890 == 0) {
          DAT_07082890 = eglGetProcAddress("glCompressedTexImage3DOES");
        }
      }
      if (DAT_07082898 == 0) {
        DAT_07082898 = eglGetProcAddress("glCompressedTexSubImage3D");
        if (DAT_07082898 == 0) {
          DAT_07082898 = eglGetProcAddress("glCompressedTexSubImage3DOES");
        }
      }
      FUN_03299019("glCopyImageSubDataOES");
      FUN_03299019("glCopyImageSubDataEXT");
      FUN_03298fe5(&DAT_07082878,"glTexStorage2D","glTexStorage2DEXT");
      if (DAT_070828a8 == 0) {
        DAT_070828a8 = eglGetProcAddress("glTexStorage3D");
        if (DAT_070828a8 == 0) {
          DAT_070828a8 = eglGetProcAddress("glTexStorage3DEXT");
        }
      }
      if (DAT_070828b0 == 0) {
        DAT_070828b0 = eglGetProcAddress("glFenceSync");
        if (DAT_070828b0 == 0) {
          DAT_070828b0 = eglGetProcAddress("glFenceSyncEXT");
        }
      }
      if (DAT_070828b8 == 0) {
        DAT_070828b8 = eglGetProcAddress("glDeleteSync");
        if (DAT_070828b8 == 0) {
          DAT_070828b8 = eglGetProcAddress("glDeleteSyncEXT");
        }
      }
      if (DAT_070828c0 == 0) {
        DAT_070828c0 = eglGetProcAddress("glClientWaitSync");
        if (DAT_070828c0 == 0) {
          DAT_070828c0 = eglGetProcAddress("glClientWaitSyncEXT");
        }
      }
      if (DAT_070828c8 == 0) {
        DAT_070828c8 = eglGetProcAddress("glWaitSync");
        if (DAT_070828c8 == 0) {
          DAT_070828c8 = eglGetProcAddress("glWaitSyncEXT");
        }
      }
      if (DAT_070828d0 == 0) {
        DAT_070828d0 = eglGetProcAddress("glBlitFramebuffer");
        if (DAT_070828d0 == 0) {
          DAT_070828d0 = eglGetProcAddress("glBlitFramebufferEXT");
        }
      }
      FUN_03298fe5(&DAT_070828e0,"glRenderbufferStorageMultisample",
                   "glRenderbufferStorageMultisampleEXT");
      if (DAT_070828e8 == 0) {
        DAT_070828e8 = eglGetProcAddress("glInvalidateFramebuffer");
        if (DAT_070828e8 == 0) {
          DAT_070828e8 = eglGetProcAddress("glInvalidateFramebufferEXT");
        }
      }
      FUN_03298fe5(&DAT_070828f0,"glDrawBuffers","glDrawBuffersEXT");
      if (DAT_070828f8 == 0) {
        DAT_070828f8 = eglGetProcAddress("glClearBufferiv");
        if (DAT_070828f8 == 0) {
          DAT_070828f8 = eglGetProcAddress("glClearBufferivEXT");
        }
      }
      if (DAT_07082900 == 0) {
        DAT_07082900 = eglGetProcAddress("glClearBufferfv");
        if (DAT_07082900 == 0) {
          DAT_07082900 = eglGetProcAddress("glClearBufferfvEXT");
        }
      }
      if (DAT_07082908 == 0) {
        DAT_07082908 = eglGetProcAddress("glClearBufferfi");
        if (DAT_07082908 == 0) {
          DAT_07082908 = eglGetProcAddress("glClearBufferfiEXT");
        }
      }
      FUN_03298fe5(&DAT_07082910,"glUniformBlockBinding","glUniformBlockBindingEXT");
      FUN_03298fe5(&DAT_07082918,"glBindBufferBase","glBindBufferBaseEXT");
      if (DAT_07082920 == 0) {
        DAT_07082920 = eglGetProcAddress("glBindBufferRange");
        if (DAT_07082920 == 0) {
          DAT_07082920 = eglGetProcAddress("glBindBufferRangeEXT");
        }
      }
      if (DAT_07082928 == 0) {
        DAT_07082928 = eglGetProcAddress("glGetUniformBlockIndex");
        if (DAT_07082928 == 0) {
          DAT_07082928 = eglGetProcAddress("glGetUniformBlockIndexEXT");
        }
      }
      if (DAT_07082930 == 0) {
        DAT_07082930 = eglGetProcAddress("glGetActiveUniformBlockiv");
        if (DAT_07082930 == 0) {
          DAT_07082930 = eglGetProcAddress("glGetActiveUniformBlockivEXT");
        }
      }
      if (DAT_07082938 == 0) {
        DAT_07082938 = eglGetProcAddress("glDrawElementsInstanced");
        if (DAT_07082938 == 0) {
          DAT_07082938 = eglGetProcAddress("glDrawElementsInstancedEXT");
        }
      }
      if (DAT_07082940 == 0) {
        DAT_07082940 = eglGetProcAddress("glDrawArraysInstanced");
        if (DAT_07082940 == 0) {
          DAT_07082940 = eglGetProcAddress("glDrawArraysInstancedEXT");
        }
      }
      if (DAT_07082950 == 0) {
        DAT_07082950 = eglGetProcAddress("glPushGroupMarker");
        if (DAT_07082950 == 0) {
          DAT_07082950 = eglGetProcAddress("glPushGroupMarkerEXT");
        }
      }
      if (DAT_07082958 == 0) {
        DAT_07082958 = eglGetProcAddress("glPopGroupMarker");
        if (DAT_07082958 == 0) {
          DAT_07082958 = eglGetProcAddress("glPopGroupMarkerEXT");
        }
      }
      if (DAT_07082960 == 0) {
        DAT_07082960 = eglGetProcAddress("glGenQueries");
        if (DAT_07082960 == 0) {
          DAT_07082960 = eglGetProcAddress("glGenQueriesEXT");
        }
      }
      FUN_03298fe5(&DAT_07082968,"glDeleteQueries","glDeleteQueriesEXT");
      FUN_03298fe5(&DAT_07082970,"glBeginQuery","glBeginQueryEXT");
      FUN_03298fe5(&DAT_07082978,"glEndQuery","glEndQueryEXT");
      FUN_03298fe5(&DAT_07082980,"glQueryCounter","glQueryCounterEXT");
      FUN_03298fe5(&DAT_07082988,"glGetQueryiv","glGetQueryivEXT");
      FUN_03298fe5(&DAT_07082990,"glGetQueryObjectiv","glGetQueryObjectivEXT");
      if (DAT_07082998 == 0) {
        DAT_07082998 = eglGetProcAddress("glGetQueryObjectui64v");
        if (DAT_07082998 == 0) {
          DAT_07082998 = eglGetProcAddress("glGetQueryObjectui64vEXT");
        }
      }
      if (DAT_070829a0 == 0) {
        DAT_070829a0 = eglGetProcAddress("glGetInteger64v");
        if (DAT_070829a0 == 0) {
          DAT_070829a0 = eglGetProcAddress("glGetInteger64vEXT");
        }
      }
      if (DAT_070829a8 == 0) {
        DAT_070829a8 = eglGetProcAddress("glObjectLabelKHR");
        if (DAT_070829a8 == 0) {
          DAT_070829a8 = eglGetProcAddress("glObjectLabelKHRKHR");
        }
      }
      if (DAT_070829b0 == 0) {
        DAT_070829b0 = eglGetProcAddress("glFramebufferTexture2DMultisampleEXT");
        if (DAT_070829b0 == 0) {
          DAT_070829b0 = eglGetProcAddress("glFramebufferTexture2DMultisampleEXTEXT");
        }
      }
      FUN_03298fe5(&DAT_070829b8,"glRenderbufferStorageMultisampleEXT",
                   "glRenderbufferStorageMultisampleEXTEXT");
      if (DAT_07082948 == 0) {
        DAT_07082948 = eglGetProcAddress("glBufferStorage");
        if (DAT_07082948 == 0) {
          DAT_07082948 = eglGetProcAddress("glBufferStorageEXT");
        }
      }
      if (DAT_070828d8 == 0) {
        DAT_070828d8 = eglGetProcAddress("glFramebufferTextureLayer");
        if (DAT_070828d8 == 0) {
          DAT_070828d8 = eglGetProcAddress("glFramebufferTextureLayerEXT");
        }
      }
      if (DAT_070829c0 == 0) {
        DAT_070829c0 = eglGetProcAddress("glGetProgramBinary");
        if (DAT_070829c0 == 0) {
          DAT_070829c0 = eglGetProcAddress("glGetProgramBinaryOES");
        }
      }
      if (DAT_070829c8 == 0) {
        DAT_070829c8 = eglGetProcAddress("glProgramBinary");
        if (DAT_070829c8 == 0) {
          DAT_070829c8 = eglGetProcAddress("glProgramBinaryOES");
        }
      }
      if (DAT_070829d0 == 0) {
        DAT_070829d0 = eglGetProcAddress("glProgramParameteri");
        if (DAT_070829d0 == 0) {
          DAT_070829d0 = eglGetProcAddress("glProgramParameteriOES");
        }
      }
      FUN_01c6cfc8(uVar3);
      if (___stack_chk_guard == lStack_38) {
        return;
      }
                    /* WARNING: Subroutine does not return */
      __stack_chk_fail();
    }
    uVar8 = FUN_02af7f10(0x10);
    uVar3 = eglGetError();
    FUN_0242f597(uVar8,"Error creating context: eglMakeCurrent %x",uVar3);
  }
                    /* WARNING: Subroutine does not return */
  FUN_02af80c0(uVar8,&DAT_06acbc40,FUN_02ade2a0);
}

