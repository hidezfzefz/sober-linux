/* Ghidra decompilation of target routine FUN_032859c2 @ 0x32859c2
 * REA evidence: ev_439b29f6ef315227952ac382be5de3af210057eaf8f25753f6b769c67041226b
 * Requested address: 0x32875c6
 * Target-derived data, not source. Do not execute.
 */


/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

void FUN_032859c2(undefined8 *param_1,undefined8 *param_2)

{
  undefined1 (*pauVar1) [16];
  byte *pbVar2;
  uint *puVar3;
  undefined1 uVar4;
  undefined1 uVar5;
  uint uVar6;
  int iVar7;
  int iVar8;
  int iVar9;
  double dVar10;
  double dVar11;
  undefined4 uVar12;
  undefined4 uVar13;
  undefined4 uVar14;
  undefined4 uVar15;
  undefined4 uVar16;
  undefined8 uVar17;
  undefined1 auVar18 [16];
  undefined1 auVar19 [16];
  undefined1 auVar20 [16];
  long *plVar21;
  char cVar22;
  char cVar23;
  byte bVar24;
  int iVar25;
  undefined4 uVar26;
  undefined4 uVar27;
  int iVar28;
  int iVar29;
  int iVar30;
  int iVar31;
  pthread_t pVar32;
  undefined8 uVar33;
  undefined8 *puVar34;
  long lVar35;
  long *plVar36;
  char *pcVar37;
  char *pcVar38;
  size_t sVar39;
  char *pcVar40;
  char *pcVar41;
  undefined1 *puVar42;
  undefined8 uVar43;
  ulong uVar44;
  long lVar45;
  long lVar46;
  long extraout_RDX;
  int *piVar47;
  long *plVar48;
  long lVar49;
  int *piVar50;
  undefined *puVar51;
  undefined8 *puVar52;
  undefined4 *puVar53;
  char cVar54;
  ulong uVar55;
  ulong uVar56;
  ulong uVar57;
  int *piVar58;
  bool bVar59;
  bool bVar60;
  byte bVar61;
  ulong uStack_278;
  ulong uStack_270;
  undefined8 uStack_268;
  long *plStack_258;
  long lStack_250;
  undefined8 uStack_248;
  int iStack_23c;
  ulong uStack_238;
  undefined4 uStack_230;
  undefined8 uStack_228;
  undefined8 uStack_220;
  char *pcStack_218;
  char *pcStack_208;
  char *pcStack_1f8;
  char *pcStack_1e8;
  char *pcStack_1e0;
  char *pcStack_1d8;
  undefined8 uStack_1c8;
  undefined8 uStack_1c0;
  char *pcStack_1b8;
  undefined1 auStack_1a8 [11];
  bool bStack_19d;
  undefined4 uStack_19c;
  undefined8 uStack_198;
  undefined8 uStack_190;
  bool bStack_188;
  undefined4 uStack_187;
  undefined2 uStack_183;
  undefined1 uStack_181;
  undefined1 uStack_180;
  undefined1 uStack_17f;
  undefined2 uStack_17e;
  undefined2 uStack_17c;
  byte bStack_17a;
  undefined1 uStack_179;
  undefined1 uStack_178;
  undefined1 uStack_177;
  undefined1 uStack_176;
  undefined1 uStack_175;
  undefined2 uStack_174;
  uint uStack_172;
  undefined1 uStack_16e;
  undefined1 uStack_16d;
  undefined8 uStack_16c;
  undefined4 uStack_164;
  undefined4 uStack_160;
  undefined8 uStack_15c;
  int iStack_154;
  undefined4 uStack_150;
  undefined2 uStack_14c;
  undefined1 uStack_14a;
  undefined8 uStack_148;
  undefined8 uStack_140;
  ushort uStack_138;
  undefined1 uStack_136;
  undefined4 uStack_135;
  char cStack_131;
  undefined2 auStack_130 [4];
  undefined8 uStack_128;
  undefined8 uStack_120;
  undefined8 uStack_118;
  undefined1 uStack_110;
  byte bStack_10f;
  char cStack_10e;
  undefined4 uStack_10d;
  bool bStack_109;
  undefined1 uStack_108;
  undefined1 uStack_107;
  undefined1 uStack_106;
  undefined **appuStack_f0 [19];
  int aiStack_58 [4];
  int iStack_48;
  undefined2 uStack_44;
  undefined2 uStack_42;
  undefined4 uStack_40;
  undefined2 uStack_3c;
  undefined2 uStack_3a;
  long lStack_38;
  
  bVar61 = 0;
  lStack_38 = ___stack_chk_guard;
  *(undefined1 *)(param_1 + 1) = 0;
  pVar32 = pthread_self();
  param_1[2] = pVar32;
  param_1[3] = 1;
  param_1[4] = 0;
  param_1[5] = 0;
  param_1[6] = 0;
  param_1[7] = 0;
  param_1[8] = 0;
  param_1[9] = 0;
  param_1[10] = 0;
  param_1[0xb] = 0;
  param_1[0xc] = 0;
  param_1[0xd] = 0;
  *param_1 = &DAT_06b27488;
  param_1[0xe] = 0;
  param_1[0xf] = 0;
  param_1[0x10] = 0;
  param_1[0x11] = 0;
  param_1[0x12] = 0;
  param_1[0x13] = 0;
  param_1[0x14] = 0;
  param_1[0x15] = 0;
  *(undefined4 *)(param_1 + 0x16) = 0;
  param_1[0x17] = 0;
  param_1[0x18] = 0;
  param_1[0x19] = 0;
  param_1[0x1a] = 0;
  param_1[0x1b] = 0;
  param_1[0x1c] = 0;
  param_1[0x1d] = 0;
  param_1[0x1e] = 0;
  param_1[0x1f] = 0;
  param_1[0x20] = 0;
  *(undefined8 *)((long)param_1 + 0x104) = 0;
  *(undefined8 *)((long)param_1 + 0x10c) = 0;
  *(undefined8 *)((long)param_1 + 0x124) = 0;
  *(undefined8 *)((long)param_1 + 300) = 0;
  param_1[0x23] = 0;
  param_1[0x24] = 0;
  *(undefined2 *)(param_1 + 0x35) = 0;
  param_1[0x33] = 0;
  param_1[0x34] = 0;
  param_1[0x31] = 0;
  param_1[0x32] = 0;
  param_1[0x2f] = 0;
  param_1[0x30] = 0;
  param_1[0x2d] = 0;
  param_1[0x2e] = 0;
  param_1[0x2b] = 0;
  param_1[0x2c] = 0;
  param_1[0x29] = 0;
  param_1[0x2a] = 0;
  param_1[0x27] = 0;
  param_1[0x28] = 0;
  *(undefined **)((long)param_1 + 0x1aa) = &DAT_01010101;
  *(undefined2 *)((long)param_1 + 0x1b2) = 0;
  *(undefined4 *)((long)param_1 + 0x1b4) = 1;
  *(undefined4 *)(param_1 + 0x37) = 1;
  *(undefined4 *)((long)param_1 + 0x1bc) = 0x400;
  *(undefined4 *)(param_1 + 0x38) = 8;
  *(undefined8 *)((long)param_1 + 0x1c4) = 0x100000000000;
  *(undefined4 *)((long)param_1 + 0x1d3) = 0;
  *(undefined8 *)((long)param_1 + 0x1cc) = 0;
  param_1[0x3b] = 0x4000000;
  param_1[0x3c] = 0;
  *(undefined2 *)(param_1 + 0x3d) = 0x100;
  *(undefined1 *)((long)param_1 + 0x1ea) = 1;
  *(undefined1 *)((long)param_1 + 0x1ef) = 0;
  *(undefined4 *)((long)param_1 + 0x1eb) = 0;
  *(undefined2 *)(param_1 + 0x3e) = 1;
  *(undefined1 *)(param_1 + 0x49) = 0;
  param_1[0x4a] = 0;
  *(undefined1 *)(param_1 + 0x4b) = 0;
  *(undefined4 *)((long)param_1 + 0x25c) = 0;
  *(undefined1 *)(param_1 + 0x4c) = 0;
  param_1[0x4d] = 0;
  *(undefined1 *)(param_1 + 0x48) = 0;
  param_1[0x46] = 0;
  param_1[0x47] = 0;
  param_1[0x44] = 0;
  param_1[0x45] = 0;
  *(undefined4 *)((long)param_1 + 0x217) = 0;
  param_1[0x41] = 0;
  param_1[0x42] = 0;
  param_1[0x3f] = 0;
  param_1[0x40] = 0;
  uVar33 = FUN_01bb0f6a(0x100);
  pauVar1 = (undefined1 (*) [16])(param_1 + 0x44);
  param_1[0x4f] = uVar33;
  param_1[0x50] = 8;
  pbVar2 = (byte *)(param_1 + 0x54);
  puVar3 = (uint *)(param_1 + 0x5a);
  param_1[0x54] = 0;
  param_1[0x55] = 0;
  param_1[0x56] = 0;
  param_1[0x57] = 0;
  param_1[0x58] = 0;
  param_1[0x59] = 0;
  param_1[0x51] = 0;
  param_1[0x52] = 0;
  *(undefined1 *)(param_1 + 0x53) = 0;
  uVar33 = FUN_01bc9e1a();
  param_1[0x5e] = uVar33;
  param_1[0x5a] = 0;
  param_1[0x5b] = 0;
  param_1[0x5c] = 0;
  param_1[0x5d] = 0;
  param_1[0x5f] = 0;
  param_1[0x60] = 0;
  *(undefined8 *)((long)param_1 + 0x302) = 0;
  *(undefined8 *)((long)param_1 + 0x30a) = 0;
  if (DAT_06f6d648 != '\0') {
    uVar33 = FUN_02af7f10(0x10);
    FUN_02aa9c10(uVar33,"OpenGL is disabled");
                    /* WARNING: Subroutine does not return */
    FUN_02af80c0(uVar33,&DAT_06acbc40,FUN_02ade2a0);
  }
  uVar33 = *param_2;
  param_1[0x27] = uVar33;
  uVar33 = FUN_03297e52(uVar33,0);
  puVar34 = (undefined8 *)FUN_01bb0f6a(0x20);
  puVar34[1] = 0;
  puVar34[2] = 0;
  *puVar34 = &DAT_06b27a68;
  puVar34[3] = uVar33;
  param_1[0x28] = uVar33;
  plVar36 = (long *)param_1[0x29];
  param_1[0x29] = puVar34;
  if (plVar36 != (long *)0x0) {
    LOCK();
    plVar21 = plVar36 + 1;
    lVar35 = *plVar21;
    *plVar21 = *plVar21 + -1;
    UNLOCK();
    if (lVar35 == 0) {
      (**(code **)(*plVar36 + 0x10))(plVar36);
      FUN_02aa9910(plVar36);
    }
  }
  plStack_258 = &lStack_250;
  lStack_250 = 0;
  uStack_248 = 0;
  lVar35 = glGetString(0x1f03);
  if (lVar35 == 0) {
    glGetError();
  }
  else {
    FUN_01bb776a(&uStack_228,lVar35);
    FUN_02037374(auStack_1a8,&uStack_228,8);
    if (((ulong)uStack_228 & 1) != 0) {
      FUN_01bb4b03(pcStack_218);
    }
    uStack_228 = (char *)0x0;
    uStack_220 = (char *)0x0;
    pcStack_218 = (char *)0x0;
    while( true ) {
      plVar36 = (long *)FUN_023ae716(auStack_1a8,&uStack_228);
      if ((*(byte *)((long)plVar36 + *(long *)(*plVar36 + -0x18) + 0x20) & 5) != 0) break;
      FUN_02b4f4b8(&plStack_258,&uStack_228,&uStack_228);
    }
    if (((ulong)uStack_228 & 1) != 0) {
      FUN_01bb4b03(pcStack_218);
    }
    auStack_1a8._0_8_ = &PTR_FUN_06ac8d98;
    uStack_198 = &DAT_06ac8748;
    if ((uStack_15c & 0x100000000) != 0) {
      FUN_01bb4b03(uStack_148);
    }
    FUN_02aaeee0(&uStack_198);
    Java_com_roblox_universalapp_activitylifecyclecallbacks_JNIActivityLifecycleCallbacks_nativeOnPostResumed
              (auStack_1a8,&PTR_PTR_06ac8dd8);
    thunk_FUN_02aaeac0(auStack_130);
  }
  pcVar37 = (char *)glGetString(0x1f02);
  iVar25 = strncmp(pcVar37,"OpenGL ES ",10);
  if ((iVar25 != 0) || (pcVar37[10] < '3')) {
                    /* WARNING: Subroutine does not return */
    FUN_023e6556("OpenGL ES 3.0 support or higher required on mobile hardware.");
  }
  pcVar37 = (char *)glGetString(0x1f01);
  pcVar38 = (char *)glGetString(0x1f02);
  iVar25 = strncmp(pcVar38,"OpenGL ES 3.",0xc);
  bVar60 = false;
  if (iVar25 == 0) {
    bVar60 = '1' < pcVar38[0xc];
  }
  bStack_188 = false;
  uStack_187 = 0;
  uStack_183 = 0;
  uStack_181 = 0;
  uStack_180 = 0;
  uStack_17f = 0;
  uStack_17e = 0;
  uStack_17c = 0;
  bStack_17a = 0;
  uStack_179 = 0;
  uStack_198 = (char *)0x0;
  uStack_190 = 0;
  _auStack_1a8 = ZEXT816(0);
  uStack_178 = 0;
  uStack_177 = 0;
  uStack_176 = 1;
  uStack_175 = 1;
  uStack_174 = 0x101;
  uStack_172 = 0;
  uStack_16e = 0;
  uStack_16d = 0;
  uStack_16c = 0x100000001;
  uStack_164 = 0x400;
  uStack_160 = 8;
  uStack_15c._0_4_ = 0;
  uStack_15c._4_4_ = 0x1000;
  uStack_14c = 0;
  uStack_14a = 0;
  iStack_154 = 0;
  uStack_150 = 0;
  uStack_148 = 0x4000000;
  uStack_140 = 0;
  uStack_138 = 0x100;
  uStack_136 = 1;
  uStack_135 = 0;
  cStack_131 = 0;
  auStack_130[0] = 1;
  uStack_108 = 0;
  uStack_107 = 0;
  uStack_106 = 0;
  uStack_118 = 0;
  uStack_110 = 0;
  bStack_10f = 0;
  cStack_10e = '\0';
  uStack_10d = 0;
  bStack_109 = false;
  uStack_128 = 0;
  uStack_120 = 0;
  glGetIntegerv(0xd33,&uStack_278);
  iVar25 = glGetError();
  if (iVar25 == 0x505) {
    pcVar37 = "glGetIntegerv(GL_MAX_TEXTURE_SIZE, &texSize)";
    goto LAB_03288e77;
  }
  glGetIntegerv(0xd57,&iStack_48);
  iVar25 = glGetError();
  if (iVar25 == 0x505) {
    pcVar37 = "glGetIntegerv(GL_STENCIL_BITS, &stencilBits)";
    goto LAB_03288e77;
  }
  FUN_01bb776a(&uStack_228,"GL_EXT_color_buffer_float");
  lVar35 = FUN_01ff4dc4(&plStack_258,&uStack_228);
  auStack_1a8[2] = lVar35 != 0;
  if (((ulong)uStack_228 & 1) != 0) {
    FUN_01bb4b03(pcStack_218);
  }
  pcVar38 = uStack_198;
  auStack_1a8._3_2_ = 0x101;
  uStack_177 = 1;
  uStack_176 = 0;
  bStack_19d = 7 < iStack_48;
  uStack_19c._0_2_ = 0x101;
  uStack_19c._2_1_ = 1;
  uVar26 = 1;
  if (DAT_06f6dc68 == '\0') {
    uVar26 = DAT_06f6dc80;
  }
  bVar59 = (char)DAT_06f6dc80 != '\0' || DAT_06f6dc68 != '\0';
  uStack_19c._3_1_ = (char)uVar26;
  uStack_198 = (char *)CONCAT71(uStack_198._1_7_,bVar59);
  uVar33 = uStack_198;
  uStack_183 = 1;
  uStack_198._4_4_ = SUB84(pcVar38,4);
  uStack_198._0_3_ = (undefined3)uVar33;
  uStack_198._0_4_ = CONCAT13(1,(undefined3)uStack_198);
  uVar33 = uStack_198;
  uStack_198._0_2_ = (ushort)bVar59;
  if (bVar60 == false) {
    FUN_01bb776a(&uStack_228,"GL_OES_texture_border_clamp");
    lVar35 = FUN_01ff4dc4(&plStack_258,&uStack_228);
    uStack_198._0_3_ = CONCAT12(lVar35 != 0,(ushort)uStack_198);
    if (((ulong)uStack_228 & 1) != 0) {
      FUN_01bb4b03(pcStack_218);
    }
  }
  else {
    uStack_198._3_5_ = SUB85(uVar33,3);
    uStack_198._0_3_ = CONCAT12(1,(ushort)uStack_198);
  }
  FUN_01bb776a(&uStack_228,"GL_IMG_texture_compression_pvrtc");
  lVar35 = FUN_01ff4dc4(&plStack_258,&uStack_228);
  uStack_190._0_2_ = CONCAT11(lVar35 != 0,(undefined1)uStack_190);
  if (((ulong)uStack_228 & 1) != 0) {
    FUN_01bb4b03(pcStack_218);
  }
  uStack_181 = 1;
  uStack_180 = 1;
  uStack_17f = 1;
  FUN_01bb776a(&uStack_228,"GL_OES_compressed_ETC1_RGB8_texture");
  lVar35 = FUN_01ff4dc4(&plStack_258,&uStack_228);
  uStack_190._0_3_ = CONCAT12(lVar35 != 0,(ushort)uStack_190);
  if (((ulong)uStack_228 & 1) != 0) {
    FUN_01bb4b03(pcStack_218);
  }
  uStack_190._0_4_ = CONCAT13(1,(uint3)uStack_190);
  uStack_17e = 0x101;
  bStack_17a = 0;
  uStack_179 = 0;
  uStack_175 = 1;
  uStack_16c = 0x100000001;
  aiStack_58[0] = 0;
  glGetIntegerv(0x8d57);
  iVar25 = glGetError();
  if (iVar25 == 0x505) {
    pcVar37 = "glGetIntegerv(GL_MAX_SAMPLES, &samples)";
    goto LAB_03288e77;
  }
  uStack_238 = uStack_238 & 0xffffffff00000000;
  glGetIntegerv(0x8824);
  iVar25 = glGetError();
  if (iVar25 == 0x505) {
    pcVar37 = "glGetIntegerv(GL_MAX_DRAW_BUFFERS, &drawBuffers)";
    goto LAB_03288e77;
  }
  iVar25 = aiStack_58[0];
  if (aiStack_58[0] < 2) {
    iVar25 = 1;
  }
  iVar7 = 1;
  if (1 < (int)uStack_238) {
    iVar7 = (int)uStack_238;
  }
  uStack_16c = CONCAT44(iVar25,iVar7);
  uStack_164 = (undefined4)uStack_278;
  uStack_160 = 0x10;
  iStack_23c = 0;
  glGetIntegerv(0x8b4c);
  iVar25 = glGetError();
  if (iVar25 == 0x505) {
    pcVar37 = "glGetIntegerv(GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS, &maxVertexTextureUnits)";
    goto LAB_03288e77;
  }
  uStack_15c._0_4_ = iStack_23c;
  uStack_14c = 0x101;
  uStack_118 = CONCAT71(uStack_118._1_7_,1);
  pcVar38 = strstr(pcVar37,"Adreno");
  uVar33 = uStack_118;
  uStack_118._0_4_ = CONCAT22(0x101,CONCAT11(pcVar38 == (char *)0x0,(undefined1)uStack_118));
  uStack_118._5_3_ = SUB83(uVar33,5);
  uStack_118._0_5_ = CONCAT14(1,(undefined4)uStack_118);
  FUN_01bb776a(&uStack_228,"GL_EXT_timer_query");
  lVar35 = FUN_01ff4dc4(&plStack_258,&uStack_228);
  uStack_118._0_6_ = CONCAT15(lVar35 != 0,(undefined5)uStack_118);
  if (((ulong)uStack_228 & 1) != 0) {
    FUN_01bb4b03(pcStack_218);
  }
  FUN_01bb776a(&uStack_228,"GL_EXT_disjoint_timer_query");
  lVar35 = FUN_01ff4dc4(&plStack_258,&uStack_228);
  uStack_118._0_7_ = CONCAT16(lVar35 != 0,(undefined6)uStack_118);
  if (((ulong)uStack_228 & 1) != 0) {
    FUN_01bb4b03(pcStack_218);
  }
  uStack_118 = CONCAT17(1,(undefined7)uStack_118);
  uStack_198._0_5_ = CONCAT14(1,(uint)uStack_198);
  sVar39 = strlen("");
  uVar55 = DAT_07082668;
  if ((DAT_07082660 & 1) == 0) {
    uVar55 = (ulong)(DAT_07082660 >> 1);
  }
  if (sVar39 == uVar55) {
    if (sVar39 == 0xffffffffffffffff) {
                    /* WARNING: Subroutine does not return */
      FUN_02b1a1fc(&DAT_07082660);
    }
    puVar51 = DAT_07082670;
    if ((DAT_07082660 & 1) == 0) {
      puVar51 = &DAT_07082661;
    }
    iVar25 = memcmp(puVar51,&DAT_003db094,sVar39);
    if (iVar25 != 0) goto LAB_0328632f;
  }
  else {
LAB_0328632f:
    pcVar40 = (char *)glGetString(0x1f00);
    pcVar41 = (char *)glGetString(0x1f02);
    pcVar38 = pcVar37;
    if (pcVar37 == (char *)0x0) {
      pcVar38 = "";
    }
    sVar39 = strlen("{}:{}:{}");
    uStack_228 = pcVar38;
    pcStack_218 = pcVar41;
    pcStack_208 = pcVar40;
    FUN_01e351b5(&uStack_1c8,"{}:{}:{}",sVar39,0xccc);
    cVar22 = FUN_02384f06(&uStack_1c8,&DAT_07082660,0x7c);
    puVar51 = PTR_s_Graphics_06f6d790;
    uVar33 = DAT_06f6d788;
    if (cVar22 != '\0') {
      uStack_198._0_5_ = (uint5)(uint)uStack_198;
      if (2 < (byte)((ulong)DAT_06f6d788 >> 8) && 5 < (byte)DAT_06f6d788) {
        sVar39 = strlen("[FLog::Graphics] GLES Constant buffer support is disabled for {}");
        FUN_01ff2e02(uVar33,puVar51,3,
                     "[FLog::Graphics] GLES Constant buffer support is disabled for {}",sVar39,
                     &uStack_1c8);
      }
    }
    if (((ulong)uStack_1c8 & 1) != 0) {
      FUN_01bb4b03(pcStack_1b8);
    }
  }
  FUN_01bb776a(&uStack_228,"GL_ARB_pixel_buffer_object");
  lVar35 = FUN_01ff4dc4(&plStack_258,&uStack_228);
  uStack_110 = lVar35 != 0;
  if (((ulong)uStack_228 & 1) != 0) {
    FUN_01bb4b03(pcStack_218);
  }
  FUN_01bb776a(&uStack_228,"GL_EXT_copy_image");
  lVar35 = FUN_01ff4dc4(&plStack_258,&uStack_228);
  bVar59 = true;
  if (lVar35 == 0) {
    FUN_01bb776a(&uStack_1c8,"GL_OES_copy_image");
    lVar35 = FUN_01ff4dc4(&plStack_258,&uStack_1c8);
    bVar59 = lVar35 != 0;
    if (((ulong)uStack_1c8 & 1) != 0) {
      FUN_01bb4b03(pcStack_1b8);
    }
  }
  if (((ulong)uStack_228 & 1) != 0) {
    FUN_01bb4b03(pcStack_218);
  }
  bStack_10f = bVar59 | bVar60;
  cStack_10e = '\x01';
  if (DAT_06f6d750 == '\0') {
    uStack_198._0_7_ = (uint7)(uint6)uStack_198;
  }
  else {
    FUN_01bb776a(&uStack_228,"GL_EXT_multisampled_render_to_texture");
    lVar35 = FUN_01ff4dc4(&plStack_258,&uStack_228);
    if (lVar35 == 0) {
      FUN_01bb776a(&uStack_1c8,"GL_EXT_multisampled_render_to_texture2");
      lVar35 = FUN_01ff4dc4(&plStack_258,&uStack_1c8);
      uStack_198._0_7_ = CONCAT16(lVar35 != 0,(uint6)uStack_198);
      if (((ulong)uStack_1c8 & 1) != 0) {
        FUN_01bb4b03(pcStack_1b8);
      }
    }
    else {
      uStack_198._0_7_ = CONCAT16(1,(uint6)uStack_198);
    }
    if (((ulong)uStack_228 & 1) != 0) {
      FUN_01bb4b03(pcStack_218);
    }
  }
  bVar24 = bStack_10f;
  bVar59 = true;
  if (bStack_10f == 0) {
    pcVar38 = strstr(pcVar37,"PowerVR Rogue");
    bVar59 = cStack_10e != '\0' && pcVar38 != (char *)0x0;
  }
  bStack_17a = bVar24;
  uStack_178 = bVar59;
  if (uStack_198._4_1_ != '\0') {
    uStack_15c = CONCAT44(0x4000,(int)uStack_15c);
    FUN_01bb776a(&uStack_228,"GL_EXT_buffer_storage");
    lVar35 = FUN_01ff4dc4(&plStack_258,&uStack_228);
    uStack_108 = lVar35 != 0;
    if (((ulong)uStack_228 & 1) != 0) {
      FUN_01bb4b03(pcStack_218);
    }
    uStack_1c8 = (char *)((ulong)uStack_1c8._4_4_ << 0x20);
    glGetIntegerv(0x8a30);
    uStack_15c._4_4_ = (uint)uStack_1c8;
    if ((int)(uint)uStack_1c8 < 1) {
      uStack_198._0_5_ = (uint5)(uint)uStack_198;
    }
    else {
      uStack_198._0_6_ = CONCAT15(1,(uint5)uStack_198);
    }
    uVar55 = DAT_07082698;
    if ((DAT_07082690 & 1) == 0) {
      uVar55 = (ulong)(DAT_07082690 >> 1);
    }
    if (uVar55 != 0) {
      pcVar38 = pcVar37;
      if (pcVar37 == (char *)0x0) {
        pcVar38 = "";
      }
      FUN_01bb776a(&uStack_228,pcVar38);
      cVar22 = FUN_02384f06(&uStack_228,&DAT_07082690,0x7c);
      puVar51 = PTR_s_Graphics_06f6d790;
      uVar33 = DAT_06f6d788;
      if (cVar22 != '\0') {
        if (DAT_06f6d738 < uStack_15c._4_4_) {
          uStack_15c._4_4_ = DAT_06f6d738;
        }
        uStack_106 = 1;
        if (2 < (byte)((ulong)DAT_06f6d788 >> 8) && 5 < (byte)DAT_06f6d788) {
          sVar39 = strlen("[FLog::Graphics] GLES Constant buffer size is clamped to {} for {}");
          FUN_02b1a6c0(uVar33,puVar51,3,
                       "[FLog::Graphics] GLES Constant buffer size is clamped to {} for {}",sVar39,
                       (long)&uStack_15c + 4,&uStack_228);
        }
      }
      if (((ulong)uStack_228 & 1) != 0) {
        FUN_01bb4b03(pcStack_218);
      }
    }
  }
  FUN_01bb776a(&uStack_228,"GL_ARB_shader_storage_buffer_object");
  lVar35 = FUN_01ff4dc4(&plStack_258);
  if (((ulong)uStack_228 & 1) != 0) {
    FUN_01bb4b03(pcStack_218);
  }
  if (lVar35 != 0) {
    glGetIntegerv(0x90de);
    iVar25 = glGetError();
    if (iVar25 == 0x505) {
      pcVar37 = "glGetIntegerv(GL_MAX_SHADER_STORAGE_BLOCK_SIZE, &maxStructBufferSize)";
      goto LAB_03288e77;
    }
    iStack_154 = (int)uStack_228;
  }
  auStack_1a8[6] = 1;
  pcVar38 = DAT_07667b30;
  if ((DAT_07667b20 & 1) == 0) {
    pcVar38 = &DAT_07667b21;
  }
  lVar35 = strtol(pcVar38,(char **)0x0,10);
  if (2 < (byte)((ulong)DAT_06f6d788 >> 8) && 5 < (byte)DAT_06f6d788) {
    FUN_01fe2480(DAT_06f6d788,PTR_s_Graphics_06f6d790,"[FLog::Graphics] Android API %d",
                 (long)(int)lVar35,0,0,0,0);
  }
  if (DAT_06f6d678 != '\0') {
    if (DAT_06f6d9b0 == '\0') {
      FUN_01bb776a(&uStack_228,pcVar37);
      cVar23 = FUN_032853e8(&uStack_228,&DAT_07082558);
      cVar22 = '\x01';
      if (cVar23 == '\0') {
        if ((_DAT_07667b38 & 1) == 0) {
          pcStack_1b8 = DAT_07667b48;
          uStack_1c8 = (char *)CONCAT44(uRam0000000007667b3c,_DAT_07667b38);
          uStack_1c0 = (int *)CONCAT44(DAT_07667b40._4_4_,(undefined4)DAT_07667b40);
        }
        else {
          FUN_01bb7eda(&uStack_1c8,DAT_07667b48,
                       CONCAT44(DAT_07667b40._4_4_,(undefined4)DAT_07667b40));
        }
        cVar23 = FUN_032853e8(&uStack_1c8,&DAT_07082570);
        cVar22 = '\x01';
        if (cVar23 == '\0') {
          FUN_01bb776a(&pcStack_1e8,"glsles3");
          cVar22 = FUN_032853e8(&pcStack_1e8,&DAT_07082588);
          if (((ulong)pcStack_1e8 & 1) != 0) {
            FUN_01bb4b03(pcStack_1d8);
          }
        }
        if (((ulong)uStack_1c8 & 1) != 0) {
          FUN_01bb4b03(pcStack_1b8);
        }
      }
      if (((ulong)uStack_228 & 1) != 0) {
        FUN_01bb4b03(pcStack_218);
      }
      puVar51 = PTR_s_Graphics_06f6d790;
      uVar33 = DAT_06f6d788;
      if ((cVar22 != '\0') &&
         (uStack_138 = uStack_138 & 0xff,
         2 < (byte)((ulong)DAT_06f6d788 >> 8) && 5 < (byte)DAT_06f6d788)) {
        if ((_DAT_07667b38 & 1) == 0) {
          pcStack_218 = DAT_07667b48;
          uStack_228 = (char *)CONCAT44(uRam0000000007667b3c,_DAT_07667b38);
          uStack_220 = (char *)CONCAT44(DAT_07667b40._4_4_,(undefined4)DAT_07667b40);
        }
        else {
          FUN_01bb7eda(&uStack_228,DAT_07667b48,
                       CONCAT44(DAT_07667b40._4_4_,(undefined4)DAT_07667b40));
        }
        pcVar38 = pcStack_218;
        if (((ulong)uStack_228 & 1) == 0) {
          pcVar38 = (char *)((long)&uStack_228 + 1);
        }
        FUN_02149b32(uVar33,puVar51,"[FLog::Graphics] Excluded \'%s:%s\' - disabling HQ shaders",
                     pcVar38,pcVar37);
        if (((ulong)uStack_228 & 1) != 0) {
          FUN_01bb4b03(pcStack_218);
        }
      }
    }
    else {
      FUN_01bb776a(&uStack_228,pcVar37);
      cVar23 = FUN_032853e8(&uStack_228,&DAT_07082558);
      cVar22 = '\x01';
      if (cVar23 == '\0') {
        if ((_DAT_07667b38 & 1) == 0) {
          pcStack_1b8 = DAT_07667b48;
          uStack_1c8 = (char *)CONCAT44(uRam0000000007667b3c,_DAT_07667b38);
          uStack_1c0 = (int *)CONCAT44(DAT_07667b40._4_4_,(undefined4)DAT_07667b40);
        }
        else {
          FUN_01bb7eda(&uStack_1c8,DAT_07667b48,
                       CONCAT44(DAT_07667b40._4_4_,(undefined4)DAT_07667b40));
        }
        cVar23 = FUN_032853e8(&uStack_1c8,&DAT_07082570);
        cVar22 = '\x01';
        if (cVar23 == '\0') {
          pcStack_1e8 = (char *)0x3373656c736c670e;
          pcStack_1e0 = (char *)((ulong)pcStack_1e0 & 0xffffffffffffff00);
          cVar22 = FUN_032853e8(&pcStack_1e8,&DAT_07082588);
          if (((ulong)pcStack_1e8 & 1) != 0) {
            FUN_01bb4b03(pcStack_1d8);
          }
        }
        if (((ulong)uStack_1c8 & 1) != 0) {
          FUN_01bb4b03(pcStack_1b8);
        }
      }
      if (((ulong)uStack_228 & 1) != 0) {
        FUN_01bb4b03(pcStack_218);
      }
      puVar51 = PTR_s_Graphics_06f6d790;
      uVar33 = DAT_06f6d788;
      if ((cVar22 != '\0') &&
         (uStack_138 = uStack_138 & 0xff,
         2 < (byte)((ulong)DAT_06f6d788 >> 8) && 5 < (byte)DAT_06f6d788)) {
        if ((_DAT_07667b38 & 1) == 0) {
          pcStack_218 = DAT_07667b48;
          uStack_228 = (char *)CONCAT44(uRam0000000007667b3c,_DAT_07667b38);
          uStack_220 = (char *)CONCAT44(DAT_07667b40._4_4_,(undefined4)DAT_07667b40);
        }
        else {
          FUN_01bb7eda(&uStack_228,DAT_07667b48,
                       CONCAT44(DAT_07667b40._4_4_,(undefined4)DAT_07667b40));
        }
        pcVar38 = pcStack_218;
        if (((ulong)uStack_228 & 1) == 0) {
          pcVar38 = (char *)((long)&uStack_228 + 1);
        }
        FUN_02149b32(uVar33,puVar51,"[FLog::Graphics] Excluded \'%s:%s\' - disabling HQ shaders",
                     pcVar38,pcVar37);
        if (((ulong)uStack_228 & 1) != 0) {
          FUN_01bb4b03(pcStack_218);
        }
      }
    }
  }
  if (DAT_06f6d690 != '\0') {
    if (DAT_06f6d9b0 == '\0') {
      FUN_01bb776a(&uStack_228,pcVar37);
      cVar23 = FUN_032853e8(&uStack_228,&DAT_070825a0);
      cVar22 = '\x01';
      if (cVar23 == '\0') {
        if ((_DAT_07667b38 & 1) == 0) {
          pcStack_1b8 = DAT_07667b48;
          uStack_1c8 = (char *)CONCAT44(uRam0000000007667b3c,_DAT_07667b38);
          uStack_1c0 = (int *)CONCAT44(DAT_07667b40._4_4_,(undefined4)DAT_07667b40);
        }
        else {
          FUN_01bb7eda(&uStack_1c8,DAT_07667b48,
                       CONCAT44(DAT_07667b40._4_4_,(undefined4)DAT_07667b40));
        }
        cVar23 = FUN_032853e8(&uStack_1c8,&DAT_070825b8);
        cVar22 = '\x01';
        if (cVar23 == '\0') {
          FUN_01bb776a(&pcStack_1e8,"glsles3");
          cVar22 = FUN_032853e8(&pcStack_1e8,&DAT_070825d0);
          if (((ulong)pcStack_1e8 & 1) != 0) {
            FUN_01bb4b03(pcStack_1d8);
          }
        }
        if (((ulong)uStack_1c8 & 1) != 0) {
          FUN_01bb4b03(pcStack_1b8);
        }
      }
      if (((ulong)uStack_228 & 1) != 0) {
        FUN_01bb4b03(pcStack_218);
      }
      puVar51 = PTR_s_Graphics_06f6d790;
      uVar33 = DAT_06f6d788;
      if ((cVar22 != '\0') &&
         (uStack_136 = 0, 2 < (byte)((ulong)DAT_06f6d788 >> 8) && 5 < (byte)DAT_06f6d788)) {
        if ((_DAT_07667b38 & 1) == 0) {
          pcStack_218 = DAT_07667b48;
          uStack_228 = (char *)CONCAT44(uRam0000000007667b3c,_DAT_07667b38);
          uStack_220 = (char *)CONCAT44(DAT_07667b40._4_4_,(undefined4)DAT_07667b40);
        }
        else {
          FUN_01bb7eda(&uStack_228,DAT_07667b48,
                       CONCAT44(DAT_07667b40._4_4_,(undefined4)DAT_07667b40));
        }
        pcVar38 = pcStack_218;
        if (((ulong)uStack_228 & 1) == 0) {
          pcVar38 = (char *)((long)&uStack_228 + 1);
        }
        FUN_02149b32(uVar33,puVar51,
                     "[FLog::Graphics] Excluded \'%s:%s\' - disabling SuperHQ shaders",pcVar38,
                     pcVar37);
        if (((ulong)uStack_228 & 1) != 0) {
          FUN_01bb4b03(pcStack_218);
        }
      }
    }
    else {
      FUN_01bb776a(&uStack_228,pcVar37);
      cVar23 = FUN_032853e8(&uStack_228,&DAT_070825a0);
      cVar22 = '\x01';
      if (cVar23 == '\0') {
        if ((_DAT_07667b38 & 1) == 0) {
          pcStack_1b8 = DAT_07667b48;
          uStack_1c8 = (char *)CONCAT44(uRam0000000007667b3c,_DAT_07667b38);
          uStack_1c0 = (int *)CONCAT44(DAT_07667b40._4_4_,(undefined4)DAT_07667b40);
        }
        else {
          FUN_01bb7eda(&uStack_1c8,DAT_07667b48,
                       CONCAT44(DAT_07667b40._4_4_,(undefined4)DAT_07667b40));
        }
        cVar23 = FUN_032853e8(&uStack_1c8,&DAT_070825b8);
        cVar22 = '\x01';
        if (cVar23 == '\0') {
          pcStack_1e8 = (char *)0x3373656c736c670e;
          pcStack_1e0 = (char *)((ulong)pcStack_1e0 & 0xffffffffffffff00);
          cVar22 = FUN_032853e8(&pcStack_1e8,&DAT_070825d0);
          if (((ulong)pcStack_1e8 & 1) != 0) {
            FUN_01bb4b03(pcStack_1d8);
          }
        }
        if (((ulong)uStack_1c8 & 1) != 0) {
          FUN_01bb4b03(pcStack_1b8);
        }
      }
      if (((ulong)uStack_228 & 1) != 0) {
        FUN_01bb4b03(pcStack_218);
      }
      puVar51 = PTR_s_Graphics_06f6d790;
      uVar33 = DAT_06f6d788;
      if ((cVar22 != '\0') &&
         (uStack_136 = 0, 2 < (byte)((ulong)DAT_06f6d788 >> 8) && 5 < (byte)DAT_06f6d788)) {
        if ((_DAT_07667b38 & 1) == 0) {
          pcStack_218 = DAT_07667b48;
          uStack_228 = (char *)CONCAT44(uRam0000000007667b3c,_DAT_07667b38);
          uStack_220 = (char *)CONCAT44(DAT_07667b40._4_4_,(undefined4)DAT_07667b40);
        }
        else {
          FUN_01bb7eda(&uStack_228,DAT_07667b48,
                       CONCAT44(DAT_07667b40._4_4_,(undefined4)DAT_07667b40));
        }
        pcVar38 = pcStack_218;
        if (((ulong)uStack_228 & 1) == 0) {
          pcVar38 = (char *)((long)&uStack_228 + 1);
        }
        FUN_02149b32(uVar33,puVar51,
                     "[FLog::Graphics] Excluded \'%s:%s\' - disabling SuperHQ shaders",pcVar38,
                     pcVar37);
        if (((ulong)uStack_228 & 1) != 0) {
          FUN_01bb4b03(pcStack_218);
        }
      }
    }
  }
  pcVar38 = strstr(pcVar37,"PowerVR SGX");
  if (pcVar38 != (char *)0x0) {
    uStack_174 = 0;
    uStack_190._0_3_ = (uint3)(ushort)uStack_190;
  }
  pcVar38 = strstr(pcVar37,"Mali");
  if (pcVar38 == (char *)0x0) {
    pcVar38 = strstr(pcVar37,"Adreno");
    if (pcVar38 == (char *)0x0) {
      uStack_135._0_1_ = DAT_06f6d6d8;
      FUN_01bb776a(&uStack_228,pcVar37);
      bVar24 = FUN_032853e8(&uStack_228,&DAT_07082618);
      uStack_135 = CONCAT31(uStack_135._1_3_,(byte)uStack_135 & (bVar24 ^ 1));
      if (((ulong)uStack_228 & 1) != 0) {
        FUN_01bb4b03(pcStack_218);
      }
    }
    else {
      uStack_135._0_1_ = DAT_06f6d6c0;
      FUN_01bb776a(&uStack_228,pcVar37);
      bVar24 = FUN_032853e8(&uStack_228,&DAT_07082600);
      uStack_135 = CONCAT31(uStack_135._1_3_,(byte)uStack_135 & (bVar24 ^ 1));
      if (((ulong)uStack_228 & 1) != 0) {
        FUN_01bb4b03(pcStack_218);
      }
    }
  }
  else {
    uStack_135._0_1_ = DAT_06f6d6a8;
    FUN_01bb776a(&uStack_228,pcVar37);
    bVar24 = FUN_032853e8(&uStack_228,&DAT_070825e8);
    uStack_135 = CONCAT31(uStack_135._1_3_,(byte)uStack_135 & (bVar24 ^ 1));
    if (((ulong)uStack_228 & 1) != 0) {
      FUN_01bb4b03(pcStack_218);
    }
  }
  if (2 < (byte)((ulong)DAT_06f6d788 >> 8) && 5 < (byte)DAT_06f6d788) {
    pcVar38 = "enabled";
    if ((byte)uStack_135 == '\0') {
      pcVar38 = "disabled";
    }
    FUN_02149b32(DAT_06f6d788,PTR_s_Graphics_06f6d790,
                 "[FLog::Graphics] GLES MT shader loading is %s",pcVar38);
  }
  pcStack_1e8 = (char *)((ulong)pcStack_1e8 & 0xffffffff00000000);
  glGetIntegerv(0x87fe);
  iVar25 = glGetError();
  if (iVar25 == 0x505) {
    pcVar37 = "glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS, &formats)";
LAB_03288e77:
                    /* WARNING: Subroutine does not return */
    FUN_069dd6b3(2,pcVar37);
  }
  cStack_131 = 0 < (int)pcStack_1e8;
  if (0 < (int)pcStack_1e8) {
    uVar55 = DAT_07082680;
    if ((DAT_07082678 & 1) == 0) {
      uVar55 = (ulong)(DAT_07082678 >> 1);
    }
    if (uVar55 != 0) {
      pcVar38 = (char *)glGetString(0x1f00);
      pcVar40 = (char *)glGetString(0x1f02);
      if (pcVar37 == (char *)0x0) {
        pcVar37 = "";
      }
      sVar39 = strlen("{}:{}:{}");
      uStack_228 = pcVar37;
      pcStack_218 = pcVar40;
      pcStack_208 = pcVar38;
      FUN_01e351b5(&uStack_1c8,"{}:{}:{}",sVar39,0xccc);
      cVar22 = FUN_02384f06(&uStack_1c8,&DAT_07082678,0x7c);
      if (cVar22 != '\0') {
        cStack_131 = '\0';
      }
      if (((ulong)uStack_1c8 & 1) != 0) {
        FUN_01bb4b03(pcStack_1b8);
      }
    }
  }
  puVar51 = PTR_s_Graphics_06f6d790;
  uVar33 = DAT_06f6d788;
  if (2 < (byte)((ulong)DAT_06f6d788 >> 8) && 5 < (byte)DAT_06f6d788) {
    sVar39 = strlen("[FLog::Graphics] Shader program binary support is: {}");
    uStack_1c8 = "enabled";
    if (cStack_131 == '\0') {
      uStack_1c8 = "disabled";
    }
    uStack_220 = (char *)&uStack_1c8;
    uStack_228 = &DAT_0000000c;
    FUN_01ff2e72(uVar33,puVar51,3,"[FLog::Graphics] Shader program binary support is: {}",sVar39,1,
                 0xc,uStack_220);
  }
  uStack_107 = 1;
  FUN_01bb776a(&uStack_228,"GL_OES_texture_compression_astc");
  lVar35 = FUN_01ff4dc4(&plStack_258,&uStack_228);
  uStack_187 = CONCAT31(uStack_187._1_3_,lVar35 != 0);
  cVar22 = lVar35 != 0;
  if (((ulong)uStack_228 & 1) != 0) {
    FUN_01bb4b03(pcStack_218);
    cVar22 = (char)uStack_187;
  }
  if (cVar22 == '\0') {
    FUN_01bb776a(&uStack_228,"GL_KHR_texture_compression_astc_hdr");
    lVar35 = FUN_01ff4dc4(&plStack_258,&uStack_228);
    uStack_190 = CONCAT17(lVar35 != 0,(undefined7)uStack_190);
    cVar22 = lVar35 != 0;
    if (((ulong)uStack_228 & 1) != 0) {
      FUN_01bb4b03(pcStack_218);
      cVar22 = uStack_190._7_1_;
    }
    if (cVar22 != '\0') goto LAB_03287332;
    FUN_01bb776a(&uStack_228,"GL_KHR_texture_compression_astc_sliced_3d");
    lVar35 = FUN_01ff4dc4(&plStack_258,&uStack_228);
    bVar59 = lVar35 != 0;
    bStack_188 = bVar59;
    if (((ulong)uStack_228 & 1) != 0) {
      FUN_01bb4b03(pcStack_218);
      bVar59 = bStack_188 != false;
    }
    if ((bVar59 | bVar60) == 1) goto LAB_03287339;
    FUN_01bb776a(&uStack_228,"GL_KHR_texture_compression_astc_ldr");
    lVar35 = FUN_01ff4dc4(&plStack_258,&uStack_228);
    uStack_190._0_7_ = CONCAT16(lVar35 != 0,(undefined6)uStack_190);
    if (((ulong)uStack_228 & 1) != 0) {
      FUN_01bb4b03(pcStack_218);
    }
  }
  else {
    uStack_190 = CONCAT17(1,(undefined7)uStack_190);
LAB_03287332:
    bStack_188 = true;
LAB_03287339:
    uStack_190._0_7_ = CONCAT16(1,(undefined6)uStack_190);
  }
  puVar51 = PTR_s_Graphics_06f6d790;
  uVar33 = DAT_06f6d788;
  if (2 < (byte)((ulong)DAT_06f6d788 >> 8) && 5 < (byte)DAT_06f6d788) {
    sVar39 = strlen(
                   "[FLog::Graphics] Caps: ASTC Support ( LDR: {}, Sliced 3D: {}, HDR: {}, Volumetric 3D: {} )"
                   );
    pcStack_1f8 = "true";
    uStack_228 = "true";
    if (uStack_190._6_1_ == '\0') {
      uStack_228 = "false";
    }
    pcStack_218 = "true";
    if (bStack_188 == false) {
      pcStack_218 = "false";
    }
    pcStack_208 = "true";
    if (uStack_190._7_1_ == '\0') {
      pcStack_208 = "false";
    }
    if ((char)uStack_187 == '\0') {
      pcStack_1f8 = "false";
    }
    uStack_1c8 = (char *)0xcccc;
    uStack_1c0 = (int *)&uStack_228;
    FUN_01ff2e72(uVar33,puVar51,3,
                 "[FLog::Graphics] Caps: ASTC Support ( LDR: {}, Sliced 3D: {}, HDR: {}, Volumetric 3D: {} )"
                 ,sVar39,1,0xcccc,&uStack_228);
  }
  uStack_172 = uStack_172 & 0xffffff;
  uStack_16e = 0;
  if (DAT_06f6d7d0 != '\0') {
    FUN_01bb776a(&uStack_228,"GL_KHR_debug");
    lVar35 = FUN_01ff4dc4(&plStack_258,&uStack_228);
    bStack_109 = lVar35 != 0;
    if (((ulong)uStack_228 & 1) != 0) {
      FUN_01bb4b03(pcStack_218);
    }
  }
  puVar34 = (undefined8 *)auStack_1a8;
  puVar52 = param_1 + 0x2f;
  for (lVar35 = 0x10; lVar35 != 0; lVar35 = lVar35 + -1) {
    *puVar52 = *puVar34;
    puVar34 = puVar34 + (ulong)bVar61 * -2 + 1;
    puVar52 = puVar52 + (ulong)bVar61 * -2 + 1;
  }
  param_1[0x3f] = uStack_128;
  param_1[0x40] = uStack_120;
  *(uint *)((long)param_1 + 0x217) =
       CONCAT13(uStack_106,CONCAT12(uStack_107,CONCAT11(uStack_108,bStack_109)));
  *(undefined4 *)(param_1 + 0x41) = (undefined4)uStack_118;
  *(undefined4 *)((long)param_1 + 0x20c) = uStack_118._4_4_;
  *(uint *)(param_1 + 0x42) =
       CONCAT13((undefined1)uStack_10d,CONCAT12(cStack_10e,CONCAT11(bStack_10f,uStack_110)));
  *(uint *)((long)param_1 + 0x214) = CONCAT13(bStack_109,uStack_10d._1_3_);
  if (*(char *)((long)param_1 + 0x209) != '\0') {
    lVar35 = param_1[0x28];
    (*DAT_07082858)(1,(undefined4 *)(lVar35 + 8));
    iVar25 = glGetError();
    if (iVar25 == 0x505) {
      pcVar37 = "glGenVertexArrays(1, &defaultVao)";
LAB_03288e91:
                    /* WARNING: Subroutine does not return */
      FUN_069dd6b3(2,pcVar37);
    }
    (*DAT_07082848)(*(undefined4 *)(lVar35 + 8));
    iVar25 = glGetError();
    if (iVar25 == 0x505) {
      pcVar37 = "glBindVertexArray(defaultVao)";
      goto LAB_03288e91;
    }
  }
  puVar42 = (undefined1 *)glGetString(0x1f01);
  if (puVar42 == (undefined1 *)0x0) {
    puVar42 = &DAT_003db094;
  }
  thunk_FUN_02aab1c0(param_1 + 7,puVar42);
  uVar33 = glGetString(0x1f02);
  FUN_01bb776a(auStack_1a8,uVar33);
  if ((*(byte *)(param_1 + 10) & 1) != 0) {
    FUN_01bb4b03(param_1[0xc]);
  }
  param_1[0xc] = uStack_198;
  *(undefined4 *)(param_1 + 10) = auStack_1a8._0_4_;
  *(undefined4 *)((long)param_1 + 0x54) = auStack_1a8._4_4_;
  *(undefined4 *)(param_1 + 0xb) = stack0xfffffffffffffe60;
  *(undefined4 *)((long)param_1 + 0x5c) = uStack_19c;
  bVar61 = (byte)((ulong)DAT_06f6d788 >> 8);
  if (2 < bVar61 && 5 < (byte)DAT_06f6d788) {
    if ((*(byte *)(param_1 + 7) & 1) == 0) {
      lVar35 = (long)param_1 + 0x39;
    }
    else {
      lVar35 = param_1[9];
    }
    FUN_01fec34d(DAT_06f6d788,PTR_s_Graphics_06f6d790,"[FLog::Graphics] GL Renderer: %s",lVar35);
    bVar61 = (byte)((ulong)DAT_06f6d788 >> 8);
  }
  puVar51 = PTR_s_Graphics_06f6d790;
  uVar33 = DAT_06f6d788;
  if (2 < bVar61 && 5 < (byte)DAT_06f6d788) {
    uVar43 = glGetString(0x1f02);
    thunk_FUN_01fec34d(uVar33,puVar51,"[FLog::Graphics] GL Version: %s",uVar43);
    bVar61 = (byte)((ulong)DAT_06f6d788 >> 8);
  }
  puVar51 = PTR_s_Graphics_06f6d790;
  uVar33 = DAT_06f6d788;
  if (2 < bVar61 && 5 < (byte)DAT_06f6d788) {
    uVar43 = glGetString(0x1f00);
    thunk_FUN_01fec34d(uVar33,puVar51,"[FLog::Graphics] GL Vendor: %s",uVar43);
  }
  puVar51 = PTR_s_Graphics_06f6d790;
  uVar33 = DAT_06f6d788;
  if ((*(char *)((long)param_1 + 0x17c) != '\0') &&
     (2 < (byte)((ulong)DAT_06f6d788 >> 8) && 5 < (byte)DAT_06f6d788)) {
    uVar43 = glGetString(0x8b8c);
    thunk_FUN_01fec34d(uVar33,puVar51,"[FLog::Graphics] GLSL version: %s",uVar43);
  }
  uStack_278 = 0;
  uStack_270 = 0;
  uStack_268 = 0;
  if (plStack_258 != &lStack_250) {
    plVar36 = plStack_258;
    do {
      FUN_02aab580(&uStack_278,&DAT_0034b8fd);
      bVar60 = (*(byte *)(plVar36 + 4) & 1) != 0;
      lVar35 = (long)plVar36 + 0x21;
      if (bVar60) {
        lVar35 = plVar36[6];
      }
      uVar55 = (ulong)(*(byte *)(plVar36 + 4) >> 1);
      if (bVar60) {
        uVar55 = plVar36[5];
      }
      FUN_02aaa730(&uStack_278,lVar35,uVar55);
      plVar21 = (long *)plVar36[1];
      plVar48 = plVar36;
      if ((long *)plVar36[1] == (long *)0x0) {
        do {
          plVar36 = (long *)plVar48[2];
          bVar60 = (long *)*plVar36 != plVar48;
          plVar48 = plVar36;
        } while (bVar60);
      }
      else {
        do {
          plVar36 = plVar21;
          plVar21 = (long *)*plVar36;
        } while ((long *)*plVar36 != (long *)0x0);
      }
    } while (plVar36 != &lStack_250);
  }
  uVar55 = DAT_07082650;
  if ((DAT_07082648 & 1) == 0) {
    uVar55 = (ulong)(DAT_07082648 >> 1);
  }
  if (uVar55 != 0) {
    if ((*pbVar2 & 1) == 0) {
      *(undefined2 *)(param_1 + 0x54) = 0;
    }
    else {
      *(undefined1 *)param_1[0x56] = 0;
      param_1[0x55] = 0;
    }
    FUN_02aab580(pbVar2,&DAT_004fb7e4);
    plVar36 = plStack_258;
    while (plVar36 != &lStack_250) {
      cVar22 = FUN_02384f06(plVar36 + 4,&DAT_07082648,0x7c);
      if (cVar22 != '\0') {
        bVar60 = (*(byte *)(plVar36 + 4) & 1) != 0;
        lVar35 = (long)plVar36 + 0x21;
        if (bVar60) {
          lVar35 = plVar36[6];
        }
        uVar55 = (ulong)(*(byte *)(plVar36 + 4) >> 1);
        if (bVar60) {
          uVar55 = plVar36[5];
        }
        FUN_02aaa730(pbVar2,lVar35,uVar55);
        FUN_02aab580(pbVar2,&DAT_004fb7e4);
      }
      plVar21 = (long *)plVar36[1];
      plVar48 = plVar36;
      if ((long *)plVar36[1] == (long *)0x0) {
        do {
          plVar36 = (long *)plVar48[2];
          bVar60 = (long *)*plVar36 != plVar48;
          plVar48 = plVar36;
        } while (bVar60);
      }
      else {
        do {
          plVar36 = plVar21;
          plVar21 = (long *)*plVar36;
        } while ((long *)*plVar36 != (long *)0x0);
      }
    }
  }
  while( true ) {
    uVar55 = uStack_270;
    if ((uStack_278 & 1) == 0) {
      uVar55 = uStack_278 >> 1 & 0x7f;
    }
    if (uVar55 == 0) break;
    uVar43 = FUN_02aaa970(&uStack_278,0x20,0x80);
    puVar51 = PTR_s_Graphics_06f6d790;
    uVar33 = DAT_06f6d788;
    if (2 < (byte)((ulong)DAT_06f6d788 >> 8) && 5 < (byte)DAT_06f6d788) {
      FUN_02aaa890(auStack_1a8,&uStack_278,0,uVar43);
      pcVar37 = auStack_1a8 + 1;
      if ((auStack_1a8[0] & 1) != 0) {
        pcVar37 = uStack_198;
      }
      FUN_01fec34d(uVar33,puVar51,"[FLog::Graphics] Ext:%s",pcVar37);
      if ((auStack_1a8[0] & 1) != 0) {
        FUN_01bb4b03(uStack_198);
      }
    }
    FUN_02aaae30(&uStack_278,0,uVar43);
  }
  FUN_023a1ba2(param_1 + 0x2f,DAT_06f6d788,PTR_s_Graphics_06f6d790);
  bVar61 = (byte)((ulong)DAT_06f6d788 >> 8);
  if (2 < bVar61 && 5 < (byte)DAT_06f6d788) {
    FUN_01fe2480(DAT_06f6d788,PTR_s_Graphics_06f6d790,"[FLog::Graphics] Caps: GL3 %d",
                 *(undefined1 *)(param_1 + 0x41),0,0,0,0);
    bVar61 = (byte)((ulong)DAT_06f6d788 >> 8);
  }
  if (2 < bVar61 && 5 < (byte)DAT_06f6d788) {
    FUN_01fe2480(DAT_06f6d788,PTR_s_Graphics_06f6d790,
                 "[FLog::Graphics] Caps: VAO %d TexStorage %d MapBuffer %d MapBufferRange %d",
                 *(undefined1 *)((long)param_1 + 0x209),*(undefined1 *)((long)param_1 + 0x20a),
                 *(undefined1 *)((long)param_1 + 0x20b),*(undefined1 *)((long)param_1 + 0x20c),0);
    bVar61 = (byte)((ulong)DAT_06f6d788 >> 8);
  }
  if (2 < bVar61 && 5 < (byte)DAT_06f6d788) {
    FUN_01fe2480(DAT_06f6d788,PTR_s_Graphics_06f6d790,
                 "[FLog::Graphics] Caps: TimerQuery %d Sync %d UBO %d Views %d Persistent %d",
                 *(undefined1 *)((long)param_1 + 0x20d),*(undefined1 *)((long)param_1 + 0x20f),
                 *(undefined1 *)((long)param_1 + 0x18c),*(undefined1 *)((long)param_1 + 0x18d),
                 *(undefined1 *)(param_1 + 0x43));
  }
  pcVar37 = (char *)glGetString(0x1f02);
  if (pcVar37 == (char *)0x0) {
    thunk_FUN_02aab1c0(pauVar1,"OpenGL");
  }
  else {
    pcVar38 = strchr(pcVar37,0x2e);
    if (((pcVar37 < pcVar38 && pcVar38 != (char *)0x0) && (0xfffffff5 < (int)pcVar38[-1] - 0x3aU))
       && (0xfffffff5 < (int)pcVar38[1] - 0x3aU)) {
      FUN_01bb776a(&pcStack_1e8,"OpenGL ");
      FUN_02aaab30(&pcStack_1e8,(int)pcVar38[-1]);
      pcStack_1b8 = pcStack_1d8;
      uStack_1c8 = pcStack_1e8;
      uStack_1c0 = (int *)pcStack_1e0;
      pcStack_1e8 = (char *)0x0;
      pcStack_1e0 = (char *)0x0;
      pcStack_1d8 = (char *)0x0;
      FUN_02aaab30(&uStack_1c8,0x2e);
      pcStack_218 = pcStack_1b8;
      uStack_228 = uStack_1c8;
      uStack_220 = (char *)uStack_1c0;
      uStack_1c8 = (char *)0x0;
      uStack_1c0 = (int *)0x0;
      pcStack_1b8 = (char *)0x0;
      FUN_02aaab30(&uStack_228,(int)pcVar38[1]);
      uStack_198 = pcStack_218;
      stack0xfffffffffffffe60 = uStack_220;
      auStack_1a8._0_8_ = uStack_228;
      uStack_228 = (char *)0x0;
      uStack_220 = (char *)0x0;
      pcStack_218 = (char *)0x0;
      puVar42 = &DAT_004a8663;
      if (*(char *)((long)param_1 + 0x18c) == '\0') {
        puVar42 = &DAT_003db094;
      }
      puVar34 = (undefined8 *)FUN_02aab580(auStack_1a8,puVar42);
      uVar4 = *(undefined1 *)puVar34;
      uVar5 = *(undefined1 *)((long)puVar34 + 1);
      uStack_40 = (undefined4)((ulong)puVar34[1] >> 0x10);
      uStack_3c = (undefined2)((ulong)puVar34[1] >> 0x30);
      uVar33 = *(undefined8 *)((long)puVar34 + 2);
      iStack_48 = (int)uVar33;
      uStack_44 = (undefined2)((ulong)uVar33 >> 0x20);
      uStack_42 = (undefined2)((ulong)uVar33 >> 0x30);
      uVar33 = puVar34[2];
      *puVar34 = 0;
      puVar34[1] = 0;
      puVar34[2] = 0;
      if (((*pauVar1)[0] & 1) != 0) {
        FUN_01bb4b03(param_1[0x46]);
      }
      *(undefined1 *)(param_1 + 0x44) = uVar4;
      *(undefined1 *)((long)param_1 + 0x221) = uVar5;
      *(ulong *)((long)param_1 + 0x222) = CONCAT26(uStack_42,CONCAT24(uStack_44,iStack_48));
      param_1[0x45] = CONCAT26(uStack_3c,CONCAT42(uStack_40,uStack_42));
      param_1[0x46] = uVar33;
      if ((auStack_1a8[0] & 1) != 0) {
        FUN_01bb4b03(uStack_198);
      }
      if (((ulong)uStack_228 & 1) != 0) {
        FUN_01bb4b03(pcStack_218);
      }
      if (((ulong)uStack_1c8 & 1) != 0) {
        FUN_01bb4b03(pcStack_1b8);
      }
      if (((ulong)pcStack_1e8 & 1) != 0) {
        FUN_01bb4b03(pcStack_1d8);
      }
    }
  }
  if (2 < (byte)((ulong)DAT_06f6d788 >> 8) && 5 < (byte)DAT_06f6d788) {
    if ((*(byte *)(param_1 + 0x44) & 1) == 0) {
      lVar35 = (long)param_1 + 0x221;
    }
    else {
      lVar35 = param_1[0x46];
    }
    thunk_FUN_01fec34d(DAT_06f6d788,PTR_s_Graphics_06f6d790,"[FLog::Graphics] GL feature level: %s",
                       lVar35);
  }
  uVar33 = FUN_01bb0f6a(0x378);
  FUN_0328f05c(uVar33,param_1);
  lVar35 = param_1[0x47];
  param_1[0x47] = uVar33;
  if (lVar35 != 0) {
    FUN_0328d7a2();
  }
  uVar33 = (**(code **)(*(long *)param_1[0x28] + 0x48))((long *)param_1[0x28],0,0);
  if (DAT_06f6dc68 == '\0') {
    uVar26 = (**(code **)(*(long *)param_1[0x28] + 0x20))();
    puVar34 = (undefined8 *)FUN_01bb0f6a(0xa0);
    FUN_023a991a(puVar34,param_1);
    *(undefined8 *)((long)puVar34 + 0x54) = 0;
    *(undefined8 *)((long)puVar34 + 0x5c) = 0;
    puVar34[9] = 0;
    puVar34[10] = 0;
    puVar34[7] = 0;
    puVar34[8] = 0;
    *(undefined8 *)((long)puVar34 + 100) = 1;
    *(int *)(puVar34 + 0xe) = (int)uVar33;
    uVar27 = (undefined4)((ulong)uVar33 >> 0x20);
    *(undefined4 *)((long)puVar34 + 0x74) = uVar27;
    puVar34[0xf] = 0x100000001;
    *(int *)(puVar34 + 0x10) = (int)uVar33;
    *(undefined4 *)((long)puVar34 + 0x84) = uVar27;
    *(undefined4 *)(puVar34 + 0x11) = 4;
    *puVar34 = &DAT_06b28b90;
    *(undefined4 *)((long)puVar34 + 0x8c) = uVar26;
    *(undefined2 *)(puVar34 + 0x12) = 1;
    *(undefined8 *)((long)puVar34 + 0x94) = 0;
    *(undefined4 *)((long)puVar34 + 0x9c) = 0;
    FUN_032898c6(param_1 + 0x2a,puVar34);
  }
  else {
    FUN_032856d6(param_1,param_1 + 0x27,uVar33);
  }
  glEnable(0xc11);
  iVar25 = glGetError();
  if (iVar25 == 0x505) {
                    /* WARNING: Subroutine does not return */
    FUN_069dd6b3(2,"glEnable(GL_SCISSOR_TEST)");
  }
  glDisable(0xbd0);
  iVar25 = glGetError();
  if (iVar25 == 0x505) {
                    /* WARNING: Subroutine does not return */
    FUN_069dd6b3(2,"glDisable(GL_DITHER)");
  }
  plVar36 = (long *)param_1[0x4d];
  if (plVar36 != (long *)0x0) {
    (**(code **)(*plVar36 + 0xd0))(plVar36,param_1);
  }
  pcVar37 = (char *)((long)param_1 + 0x39);
  pcVar38 = pcVar37;
  if ((*(byte *)(param_1 + 7) & 1) != 0) {
    pcVar38 = (char *)param_1[9];
  }
  strncpy(&DAT_076635a0,pcVar38,0x7f);
  DAT_0766361f = 0;
  pcVar38 = (char *)glGetString(0x1f02);
  strncpy(&DAT_07663620,pcVar38,0x7f);
  DAT_0766369f = 0;
  pcVar38 = (char *)glGetString(0x1f00);
  strncpy(&DAT_076636a0,pcVar38,0x7f);
  DAT_0766371f = 0;
  DAT_07663a40 = 0;
  DAT_07663a48 = 0;
  if (((*pauVar1)[0] & 1) == 0) {
    uStack_198 = (char *)param_1[0x46];
    _auStack_1a8 = *pauVar1;
  }
  else {
    FUN_01bb7eda(auStack_1a8,param_1[0x46],param_1[0x45]);
  }
  bVar61 = auStack_1a8[0];
  pcVar38 = uStack_198;
  if ((auStack_1a8[0] & 1) == 0) {
    pcVar38 = auStack_1a8 + 1;
  }
  strncpy(&DAT_07663720,pcVar38,0x7f);
  DAT_0766379f = 0;
  if ((bVar61 & 1) != 0) {
    FUN_01bb4b03(uStack_198);
  }
  DAT_07663e60 = param_1[0x3b];
  if (*(char *)((long)param_1 + 0x18d) == '\0') goto LAB_0328824a;
  uStack_228 = (char *)((ulong)uStack_228 & 0xffffffff00000000);
  glGetIntegerv();
  cVar22 = '\x01';
  if (*(char *)(param_1 + 0x43) == '\0') {
    uVar55 = DAT_07082638;
    if ((DAT_07082630 & 1) == 0) {
      uVar55 = (ulong)(DAT_07082630 >> 1);
    }
    if (uVar55 == 0) {
      cVar23 = '\0';
    }
    else {
      uVar33 = glGetString(0x1f00);
      uVar43 = glGetString(0x1f02);
      if ((*(byte *)(param_1 + 7) & 1) != 0) {
        pcVar37 = (char *)param_1[9];
      }
      FUN_01bc7052(auStack_1a8,"%s:%s:%s",uVar33,pcVar37,uVar43);
      cVar23 = FUN_02384f06(auStack_1a8,&DAT_07082630,0x7c);
      if ((auStack_1a8[0] & 1) != 0) {
        FUN_01bb4b03();
      }
      cVar54 = *(char *)(param_1 + 0x43);
      if (cVar54 != '\0') goto LAB_03287ff5;
    }
    cVar22 = '\0';
    cVar54 = '\0';
  }
  else {
    cVar23 = '\0';
    cVar54 = '\x01';
LAB_03287ff5:
    puVar51 = PTR_s_Graphics_06f6d790;
    uVar33 = DAT_06f6d788;
    if (2 < (byte)((ulong)DAT_06f6d788 >> 8) && 5 < (byte)DAT_06f6d788) {
      sVar39 = strlen("[FLog::Graphics] Using bind buffer padding for UBOs on {}");
      FUN_01ff2e02(uVar33,puVar51,3,"[FLog::Graphics] Using bind buffer padding for UBOs on {}",
                   sVar39,param_1 + 7);
      cVar22 = *(char *)(param_1 + 0x43);
    }
  }
  uVar27 = DAT_06f6d708;
  uVar26 = DAT_06f6d6f0;
  iVar25 = (int)uStack_228 + DAT_06f6d768;
  lVar35 = 0;
  do {
    *(undefined4 *)((long)param_1 + lVar35 + 0xa8) = uVar26;
    *(int *)((long)param_1 + lVar35 + 0xac) = (int)uStack_228;
    if (cVar22 == '\0') {
      if (cVar23 != '\0') {
        *(undefined1 *)((long)param_1 + lVar35 + 0xca) = 1;
        *(undefined4 *)((long)param_1 + lVar35 + 0xa8) = uVar27;
      }
    }
    else {
      *(undefined1 *)((long)param_1 + lVar35 + 200) = 1;
    }
    if (*(char *)((long)param_1 + lVar35 + 0xca) == '\0') {
      *(char *)((long)param_1 + lVar35 + 0xcb) = cVar54;
    }
    *(uint *)((long)param_1 + lVar35 + 0xcc) = -(int)uStack_228 & iVar25 - 1U;
    lVar35 = lVar35 + 0x60;
  } while (lVar35 != 0xc0);
LAB_0328824a:
  if (*(char *)((long)param_1 + 0x1ef) != '\0') {
    iStack_23c = 0;
    glGetIntegerv(0x87fe);
    iVar25 = glGetError();
    if (iVar25 == 0x505) {
                    /* WARNING: Subroutine does not return */
      FUN_069dd6b3(2,"glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS, &formats)");
    }
    FUN_024453c2(param_1 + 0x57,(long)iStack_23c);
    glGetIntegerv(0x87ff,param_1[0x57]);
    iVar25 = glGetError();
    puVar51 = PTR_s_Graphics_06f6d790;
    uVar33 = DAT_06f6d788;
    if (iVar25 == 0x505) {
                    /* WARNING: Subroutine does not return */
      FUN_069dd6b3(2,"glGetIntegerv(GL_PROGRAM_BINARY_FORMATS, supportedBinaryFormats.data())");
    }
    uVar55 = DAT_073a9dd0;
    if ((DAT_073a9dc8 & 1) == 0) {
      uVar55 = (ulong)(DAT_073a9dc8 >> 1);
    }
    if (uVar55 == 0) {
      if (2 < (byte)((ulong)DAT_06f6d788 >> 8) && 5 < (byte)DAT_06f6d788) {
        sVar39 = strlen("[FLog::Graphics] GL shader program cache load skipped: cache dir is empty."
                       );
        auVar18._8_8_ = 0;
        auVar18._0_8_ = &uStack_228;
        _auStack_1a8 = auVar18 << 0x40;
        FUN_01ff2e72(uVar33,puVar51,3,
                     "[FLog::Graphics] GL shader program cache load skipped: cache dir is empty.",
                     sVar39,1,0,&uStack_228);
      }
    }
    else {
      dVar10 = (double)FUN_01c6bebc();
      uStack_1c8 = (char *)0x0;
      uStack_1c0 = (int *)0x0;
      pcStack_1b8 = (char *)0x0;
      FUN_0328cec3(&uStack_228,"shadercachegl.bin");
      FUN_020a0bac(auStack_1a8,&uStack_228,6);
      if ((((&bStack_188)[*(long *)(auStack_1a8._0_8_ + -0x18)] & 5U) == 0) &&
         (FUN_02ab0210(auStack_1a8), -1 < extraout_RDX)) {
        FUN_02ab03e0(auStack_1a8,0,0);
        if (extraout_RDX == 0) {
          piVar50 = (int *)0x0;
        }
        else {
          FUN_023d681e(&uStack_1c8,extraout_RDX);
          piVar50 = (int *)uStack_1c8;
        }
        plVar36 = (long *)FUN_02ab0140(auStack_1a8,piVar50,extraout_RDX);
        if ((*(byte *)((long)plVar36 + *(long *)(*plVar36 + -0x18) + 0x20) & 5) != 0) {
          uStack_1c0 = (int *)uStack_1c8;
        }
      }
      auStack_1a8._0_8_ = &PTR_FUN_06ac8850;
      appuStack_f0[0] = &PTR_FUN_06ac8878;
      FUN_02ab2cc0(&uStack_198);
      Java_com_roblox_universalapp_activitylifecyclecallbacks_JNIActivityLifecycleCallbacks_nativeOnPostResumed
                (auStack_1a8,&PTR_PTR_06ac8890);
      thunk_FUN_02aaeac0(appuStack_f0);
      if (((ulong)uStack_228 & 1) != 0) {
        FUN_01bb4b03(pcStack_218);
      }
      piVar50 = (int *)uStack_1c8;
      puVar51 = PTR_s_Graphics_06f6d790;
      uVar33 = DAT_06f6d788;
      uVar55 = (long)uStack_1c0 - (long)uStack_1c8;
      if (uVar55 < 0x1c) {
        if (2 < (byte)((ulong)DAT_06f6d788 >> 8) && 5 < (byte)DAT_06f6d788) {
          sVar39 = strlen(
                         "[FLog::Graphics] GL shader program cache file missing or truncated ({} bytes read, {} byte header); skipping load."
                         );
          auStack_1a8._0_8_ = uVar55;
          uStack_198 = &DAT_0000001c;
          pcStack_1e8 = (char *)0x44;
          pcStack_1e0 = auStack_1a8;
          FUN_01ff2e72(uVar33,puVar51,3,
                       "[FLog::Graphics] GL shader program cache file missing or truncated ({} bytes read, {} byte header); skipping load."
                       ,sVar39,1,0x44,auStack_1a8);
        }
      }
      else {
        uVar6 = *(uint *)((long)uStack_1c8 + 4);
        iVar25 = *(int *)((long)uStack_1c8 + 8);
        iVar7 = *(int *)((long)uStack_1c8 + 0xc);
        iVar29 = *(int *)((long)uStack_1c8 + 0x10);
        iVar31 = *(int *)((long)uStack_1c8 + 0x14);
        iVar8 = *(int *)((long)uStack_1c8 + 0x18);
        uVar55 = uVar55 - 0x1c;
        iVar9 = *(int *)uStack_1c8;
        iVar28 = FUN_023a8088();
        bVar60 = iVar29 == DAT_06f6d720;
        pcVar37 = (char *)glGetString(0x1f02);
        pcVar38 = (char *)glGetString(0x1f01);
        sVar39 = strlen(pcVar37);
        iVar29 = FUN_023a8088(pcVar37,sVar39 & 0xffffffff);
        sVar39 = strlen(pcVar38);
        iVar30 = FUN_023a8088(pcVar38,sVar39 & 0xffffffff);
        if (((bVar60 &&
             ((iVar7 == 1 && iVar25 == iVar28) && (iVar9 == 0x43504c47 && uVar55 == uVar6))) &&
             iVar31 == iVar29) && (iVar8 == iVar30)) {
          _auStack_1a8 = ZEXT816(0);
          uStack_198 = (char *)0x0;
          if (uVar55 < 0x1c) {
            pcVar37 = (char *)0x0;
            auStack_1a8._0_8_ = (char *)0x0;
            pcVar38 = (char *)0x0;
          }
          else {
            uVar44 = 0x1c;
            uVar57 = 0;
            do {
              piVar47 = (int *)((long)piVar50 + uVar57 + 0x1c);
              piVar58 = piVar47 + 7;
              iVar25 = *piVar47;
              uVar6 = piVar47[1];
              iVar7 = piVar47[2];
              iVar29 = piVar47[6];
              iVar31 = FUN_023a8088(piVar58);
              puVar51 = PTR_s_Graphics_06f6d790;
              uVar33 = DAT_06f6d788;
              piVar47 = (int *)param_1[0x57];
              if (piVar47 == (int *)param_1[0x58]) {
LAB_03288a93:
                if (2 < (byte)((ulong)DAT_06f6d788 >> 8) && 5 < (byte)DAT_06f6d788) {
                  sVar39 = strlen("[FLog::Graphics] Failed to parse shader program cache.");
                  iStack_48 = 0;
                  uStack_44 = 0;
                  uStack_42 = 0;
                  uStack_40 = SUB84(aiStack_58,0);
                  uStack_3c = (undefined2)((ulong)aiStack_58 >> 0x20);
                  uStack_3a = (undefined2)((ulong)aiStack_58 >> 0x30);
                  FUN_01ff2e72(uVar33,puVar51,3,
                               "[FLog::Graphics] Failed to parse shader program cache.",sVar39,1,0,
                               aiStack_58);
                }
                pcStack_1e8 = (char *)0x0;
                pcStack_1e0 = (char *)0x0;
                pcStack_1d8 = (char *)0x0;
                goto LAB_03288b1f;
              }
              while (*piVar47 != iVar29) {
                piVar47 = piVar47 + 1;
                if (piVar47 == (int *)param_1[0x58]) goto LAB_03288a93;
              }
              if (((iVar25 != 0x43504c47) || (iVar7 != iVar31)) ||
                 (uVar56 = uVar44 + uVar6, uVar55 < uVar56)) goto LAB_03288a93;
              uStack_230 = *(undefined4 *)((long)piVar50 + uVar57 + 0x30);
              uStack_238 = *(ulong *)((long)piVar50 + uVar57 + 0x28);
              uStack_228 = (char *)0x0;
              uStack_220 = (char *)0x0;
              pcStack_218 = (char *)0x0;
              FUN_0328d018(&uStack_228,piVar58,(long)piVar58 + (ulong)uVar6);
              pcVar38 = pcStack_218;
              puVar34 = (undefined8 *)uStack_220;
              pcVar37 = uStack_228;
              pcVar40 = (char *)auStack_1a8._0_8_;
              pcVar41 = stack0xfffffffffffffe60;
              if (stack0xfffffffffffffe60 < uStack_198) {
                unique0x10004fd1[0] = 'G';
                unique0x10004fd1[1] = 'L';
                unique0x10004fd1[2] = 'P';
                unique0x10004fd1[3] = 'C';
                *(uint *)(stack0xfffffffffffffe60 + 4) = uVar6;
                *(int *)(stack0xfffffffffffffe60 + 8) = iVar7;
                *(ulong *)(stack0xfffffffffffffe60 + 0xc) = uStack_238;
                *(undefined4 *)(stack0xfffffffffffffe60 + 0x14) = uStack_230;
                *(int *)(stack0xfffffffffffffe60 + 0x18) = iVar29;
                *(char **)(stack0xfffffffffffffe60 + 0x20) = uStack_228;
                *(char **)(stack0xfffffffffffffe60 + 0x28) = uStack_220;
                *(char **)(stack0xfffffffffffffe60 + 0x30) = pcStack_218;
                pcVar37 = stack0xfffffffffffffe60 + 0x38;
              }
              else {
                lVar49 = (long)stack0xfffffffffffffe60 - auStack_1a8._0_8_;
                lVar35 = lVar49 >> 3;
                uVar44 = lVar35 * 0x6db6db6db6db6db7 + 1;
                if (0x492492492492492 < uVar44) {
                    /* WARNING: Subroutine does not return */
                  FUN_02aa8ba0();
                }
                lVar45 = (long)uStack_198 - auStack_1a8._0_8_ >> 3;
                uVar57 = lVar45 * -0x2492492492492492;
                if (uVar44 < uVar57) {
                  uVar44 = uVar57;
                }
                if (0x249249249249248 < (ulong)(lVar45 * 0x6db6db6db6db6db7)) {
                  uVar44 = 0x492492492492492;
                }
                if (uVar44 == 0) {
                  lVar45 = 0;
                }
                else {
                  if (0x492492492492492 < uVar44) {
                    /* WARNING: Subroutine does not return */
                    FUN_02b09cb5();
                  }
                  lVar45 = FUN_01bb0f6a(uVar44 * 0x38);
                }
                lVar35 = lVar35 * 8;
                *(undefined4 *)(lVar45 + lVar35) = 0x43504c47;
                *(uint *)(lVar45 + 4 + lVar35) = uVar6;
                *(int *)(lVar45 + 8 + lVar35) = iVar7;
                *(ulong *)(lVar45 + 0xc + lVar35) = uStack_238;
                *(undefined4 *)(lVar45 + 0x14 + lVar35) = uStack_230;
                *(int *)(lVar45 + 0x18 + lVar35) = iVar29;
                *(char **)(lVar45 + 0x20 + lVar35) = pcVar37;
                *(undefined8 **)(lVar45 + 0x28 + lVar35) = puVar34;
                *(char **)(lVar45 + 0x30 + lVar35) = pcVar38;
                if (pcVar40 != pcVar41) {
                  lVar46 = 0;
                  do {
                    pcVar37 = (char *)((undefined8)pcVar40 + lVar46);
                    uVar33 = *(undefined8 *)pcVar37;
                    uVar43 = *(undefined8 *)(pcVar37 + 8);
                    uVar17 = *(undefined8 *)(pcVar37 + 0x14);
                    puVar34 = (undefined8 *)(lVar45 + 0xc + lVar46);
                    *puVar34 = *(undefined8 *)(pcVar37 + 0xc);
                    puVar34[1] = uVar17;
                    *(undefined8 *)(lVar45 + lVar46) = uVar33;
                    ((undefined8 *)(lVar45 + lVar46))[1] = uVar43;
                    puVar34 = (undefined8 *)(lVar45 + 0x28 + lVar46);
                    *puVar34 = 0;
                    puVar34[1] = 0;
                    uVar26 = *(undefined4 *)(pcVar37 + 0x24);
                    uVar27 = *(undefined4 *)(pcVar37 + 0x28);
                    uVar12 = *(undefined4 *)(pcVar37 + 0x2c);
                    puVar53 = (undefined4 *)(lVar45 + 0x20 + lVar46);
                    *puVar53 = *(undefined4 *)(pcVar37 + 0x20);
                    puVar53[1] = uVar26;
                    puVar53[2] = uVar27;
                    puVar53[3] = uVar12;
                    *(undefined8 *)(lVar45 + 0x30 + lVar46) = *(undefined8 *)(pcVar37 + 0x30);
                    pcVar37[0x20] = '\0';
                    pcVar37[0x21] = '\0';
                    pcVar37[0x22] = '\0';
                    pcVar37[0x23] = '\0';
                    pcVar37[0x24] = '\0';
                    pcVar37[0x25] = '\0';
                    pcVar37[0x26] = '\0';
                    pcVar37[0x27] = '\0';
                    pcVar37[0x28] = '\0';
                    pcVar37[0x29] = '\0';
                    pcVar37[0x2a] = '\0';
                    pcVar37[0x2b] = '\0';
                    pcVar37[0x2c] = '\0';
                    pcVar37[0x2d] = '\0';
                    pcVar37[0x2e] = '\0';
                    pcVar37[0x2f] = '\0';
                    pcVar37[0x30] = '\0';
                    pcVar37[0x31] = '\0';
                    pcVar37[0x32] = '\0';
                    pcVar37[0x33] = '\0';
                    pcVar37[0x34] = '\0';
                    pcVar37[0x35] = '\0';
                    pcVar37[0x36] = '\0';
                    pcVar37[0x37] = '\0';
                    lVar46 = lVar46 + 0x38;
                  } while (pcVar37 + 0x38 != pcVar41);
                  do {
                    if (*(long *)(pcVar40 + 0x20) != 0) {
                      *(long *)(pcVar40 + 0x28) = *(long *)(pcVar40 + 0x20);
                      FUN_01bb4b03();
                    }
                    pcVar40 = pcVar40 + 0x38;
                  } while (pcVar40 != pcVar41);
                  pcVar41 = (char *)auStack_1a8._0_8_;
                }
                auStack_1a8._0_8_ = (lVar35 + lVar45) - lVar49;
                pcVar37 = (char *)(lVar35 + lVar45 + 0x38);
                uStack_198 = (char *)(uVar44 * 0x38 + lVar45);
                stack0xfffffffffffffe60 = pcVar37;
                if (pcVar41 != (char *)0x0) {
                  FUN_01bb4b03(pcVar41);
                }
              }
              stack0xfffffffffffffe60 = pcVar37;
              uVar44 = uVar56 + 0x1c;
              pcVar38 = uStack_198;
              uVar57 = uVar56;
            } while (uVar44 <= uVar55);
          }
          uStack_198 = (char *)0x0;
          pcStack_1e8 = (char *)auStack_1a8._0_8_;
          pcStack_1e0 = pcVar37;
          pcStack_1d8 = pcVar38;
          _auStack_1a8 = ZEXT816(0) << 0x20;
LAB_03288b1f:
          uStack_228 = auStack_1a8;
          FUN_0328e0c0();
          if (pcStack_1e8 == pcStack_1e0) {
            FUN_0328cc75();
            puVar51 = PTR_s_Graphics_06f6d790;
            uVar33 = DAT_06f6d788;
            if (2 < (byte)((ulong)DAT_06f6d788 >> 8) && 5 < (byte)DAT_06f6d788) {
              sVar39 = strlen(
                             "[FLog::Graphics] No GL shader programs loaded from cache; cache file removed."
                             );
              auVar20._8_8_ = 0;
              auVar20._0_8_ = &iStack_48;
              _auStack_1a8 = auVar20 << 0x40;
              FUN_01ff2e72(uVar33,puVar51,3,
                           "[FLog::Graphics] No GL shader programs loaded from cache; cache file removed."
                           ,sVar39,1,0,&iStack_48);
            }
          }
          else {
            pcVar37 = pcStack_1e8;
            pcVar38 = pcStack_1e0;
            if (*(uint *)((long)param_1 + 0x2d4) <
                (uint)((int)((ulong)((long)pcStack_1e0 - (long)pcStack_1e8) >> 3) * -0x49249249)) {
              FUN_0328e17c(puVar3);
              pcVar37 = pcStack_1e8;
              pcVar38 = pcStack_1e0;
            }
            for (; puVar51 = PTR_s_Graphics_06f6d790, uVar33 = DAT_06f6d788, pcVar37 != pcVar38;
                pcVar37 = pcVar37 + 0x38) {
              uVar55 = FUN_0328e62c(puVar3,pcVar37 + 0xc);
              puVar53 = (undefined4 *)((uVar55 & 0xffffffff) * 0x40 + param_1[0x5c]);
              if (uVar55 >> 0x20 != 0) {
                *puVar53 = *(undefined4 *)(pcVar37 + 0xc);
              }
              uVar26 = *(undefined4 *)pcVar37;
              uVar27 = *(undefined4 *)(pcVar37 + 4);
              uVar12 = *(undefined4 *)(pcVar37 + 8);
              uVar13 = *(undefined4 *)(pcVar37 + 0xc);
              uVar14 = *(undefined4 *)(pcVar37 + 0x10);
              uVar15 = *(undefined4 *)(pcVar37 + 0x14);
              uVar16 = *(undefined4 *)(pcVar37 + 0x18);
              puVar53[5] = *(undefined4 *)(pcVar37 + 0xc);
              puVar53[6] = uVar14;
              puVar53[7] = uVar15;
              puVar53[8] = uVar16;
              puVar53[2] = uVar26;
              puVar53[3] = uVar27;
              puVar53[4] = uVar12;
              puVar53[5] = uVar13;
              FUN_0328e124(puVar53 + 10,pcVar37 + 0x20);
            }
            if (2 < (byte)((ulong)DAT_06f6d788 >> 8) && 5 < (byte)DAT_06f6d788) {
              sVar39 = strlen(
                             "[FLog::Graphics] Loaded GL shader program cache. {} shader programs loaded with {} bytes. {} msec."
                             );
              auVar18 = _auStack_1a8;
              piVar47 = uStack_1c0;
              piVar50 = (int *)uStack_1c8;
              auStack_1a8._4_4_ = 0;
              auStack_1a8._0_4_ = *puVar3;
              uVar55 = auStack_1a8._0_8_;
              _auStack_1a8 = auVar18;
              dVar11 = (double)FUN_01c6bebc();
              uStack_198 = (char *)((long)piVar47 - (long)piVar50);
              dVar10 = (dVar11 - dVar10) * 1000.0;
              _auStack_1a8 = CONCAT88(stack0xfffffffffffffe60,uVar55);
              bStack_188 = SUB81(dVar10,0);
              uStack_187 = (undefined4)((ulong)dVar10 >> 8);
              uStack_183 = (undefined2)((ulong)dVar10 >> 0x28);
              uStack_181 = (undefined1)((ulong)dVar10 >> 0x38);
              iStack_48 = 0xa42;
              uStack_44 = 0;
              uStack_42 = 0;
              uStack_40 = SUB84(auStack_1a8,0);
              uStack_3c = (undefined2)((ulong)auStack_1a8 >> 0x20);
              uStack_3a = (undefined2)((ulong)auStack_1a8 >> 0x30);
              FUN_01ff2e72(uVar33,puVar51,3,
                           "[FLog::Graphics] Loaded GL shader program cache. {} shader programs loaded with {} bytes. {} msec."
                           ,sVar39,1,0xa42,auStack_1a8);
            }
          }
          auStack_1a8._0_8_ = &pcStack_1e8;
          FUN_0328e0c0();
        }
        else {
          FUN_0328cc75();
          puVar51 = PTR_s_Graphics_06f6d790;
          uVar33 = DAT_06f6d788;
          if (2 < (byte)((ulong)DAT_06f6d788 >> 8) && 5 < (byte)DAT_06f6d788) {
            sVar39 = strlen(
                           "[FLog::Graphics] GL shader program cache header is invalid; cache file removed."
                           );
            auVar19._8_8_ = 0;
            auVar19._0_8_ = &pcStack_1e8;
            _auStack_1a8 = auVar19 << 0x40;
            FUN_01ff2e72(uVar33,puVar51,3,
                         "[FLog::Graphics] GL shader program cache header is invalid; cache file removed."
                         ,sVar39,1,0,&pcStack_1e8);
          }
        }
      }
      if ((int *)uStack_1c8 != (int *)0x0) {
        uStack_1c0 = (int *)uStack_1c8;
        FUN_01bb4b03();
      }
    }
    *(bool *)((long)param_1 + 0x311) = *(int *)(param_1 + 0x5a) != 0;
  }
  if ((uStack_278 & 1) != 0) {
    FUN_01bb4b03(uStack_268);
  }
  FUN_01f460cc(&plStack_258,lStack_250);
  if (___stack_chk_guard != lStack_38) {
                    /* WARNING: Subroutine does not return */
    __stack_chk_fail();
  }
  return;
}

