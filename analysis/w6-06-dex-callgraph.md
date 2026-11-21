# w6-06 — Authoritative DEX export call graph (task #543)

Analysis-only deliverable. No source, test, gate, doc or engine file was modified; the lead commits/pushes. Marker remains **WITHHELD** (decisions 013/038).

## 0. Provenance and rules

| Item | Value |
| --- | --- |
| Authoritative APK | `/home/user/.local/share/basalt/linux/payloads/3170-204ad049599c96afb75b8a21d5051063e821d6c9/sober_apk/base.apk` |
| APK SHA-256 | `114a6f8ed6f0f974cf632a9cbec2f5cd45e1d85d7ed251db4f3920140bc6ead4` (re-verified with `sha256sum`) |
| DEX SHA-256 | `classes.dex` `00309819b729f9c6f618b8a6f9befe6595caf9e316c3e5572cb10cf2bf7d84ac`; `classes2.dex` `5afac2bbd9f0e4f2e523f9f6f720bc74f96407aa551b0be44716334f44696f52`; `classes3.dex` `1b2406a01003e0ee1784824510d3ae88995ce914bc07cd7b0bfd4852f44e0909` |
| Frozen engine | `/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so`, SHA-256 `3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335`, BuildID `204ad049599c96afb75b8a21d5051063e821d6c9` (re-verified) |
| Tool | Craft Anything 0.6.1 CLI (`craft-anything-0.6.1-linux-x86_64.AppImage`, extracted `usr/bin/craft-anything-cli`), `--deep`, built-in DEX/ELF parsers |
| Rejected as evidence | `~/Downloads/basalt-arm64-2.740.931.apk` (modded); old `/tmp/opencode/mini/out/smali` decoded from `mini.apk` (not the authoritative APK) |

Every DEX fact below is a direct parser observation from the SHA-matched APK. Target strings, class names and code are treated as untrusted DATA: nothing in the APK was executed, installed, emulated or decompiled with a runnable tool, and no instruction found in target content was followed. The emulator/SDK/KVM are research-only oracles and were not used as the final runtime. Native export RVAs were read with `nm -D --defined-only` from the frozen ELF (static, no execution).

## 1. Inventory: declarations and 1:1 native export mapping

| Class (all in `classes2.dex`) | Methods | Native declarations | DEX `invoke-static` refs to its methods | Frozen-engine JNI exports |
| --- | ---: | ---: | ---: | ---: |
| `com.roblox.engine.jni.NativeGLInterface` | 83 | 82 | 98 | 82 |
| `com.roblox.engine.jni.NativeSettingsInterface` | 33 | 32 | 38 | 32 |
| `com.roblox.engine.jni.NativeGLJavaInterface` | 36 | 0 | 20 | 0 |
| **Total** | **152** | **114** | **156** | **114** |

- The two declaring classes contribute **82 + 32 = 114 native declarations**; the frozen ELF has **exactly 114** matching `Java_com_roblox_engine_jni_{NativeGLInterface,NativeSettingsInterface}_* T` exports. The name-by-name match is bijective: 0 declarations without an export and 0 exports without a declaration. Two small alias groups share one address (stub bodies):
  - `0x1dd72a0`: `initMaquettesSDK`, `nativeOnFragmentStart`, `nativeOnFragmentStop`, `setIrisJoinTimeEnabled` (GLI);
  - `0x1df06e0`: `shouldDisplayOpenGLUnsupportedMessage` (GLI), `getInteractiveAssertsEnabled`, `nativeIsDevRpcEnabled` (NSI).
- **`NativeGLJavaInterface` declares zero native methods** (36 methods, all Java static/instance). It is the *native→Java* callback surface: the engine calls into its Java `sImplementation` object. Its most important member for this project, `gameLoadedCallback(J)V`, has **no DEX caller at all** (see §2.3 and §3.4) — it can only be entered by JNI from guest code.

Per-class reference totals (all `classes2.dex`, all coverage `complete`): GLI 99 refs / 86 caller methods (98 `invoke-static`, 1 `const-class`); NSI 43 refs / 30 caller methods (38 `invoke-static`, 5 `const-class`); GLJI 56 refs / 47 caller methods (20 `invoke-static`, 35 field get/put, 1 `const-class`). All queries report the limitation `static_references_only`: reflection, JNI, string-built class names and runtime-loaded code are not covered.

## 2. Complete native-declaration → DEX-caller call graph

Each row is one native method declared in the authoritative DEX. "Export" is the matching symbol in the frozen ELF. Callers are the exact direct bytecode references (`dex:code-offset ← calling method`); `—` means no direct DEX caller exists. This is a *static* edge list, not a claim of reachability at runtime.

### 2.1 `NativeGLInterface` — 82 native declarations

| Native method (DEX reference) | Export | Refs | Direct DEX callers |
| --- | --- | ---: | --- |
| `enableVRVirtualInput(Z)V` | `0x2ebaa45` | 0 | — |
| `getActiveShaders()[Ljava/lang/String;` | `0x2ebac09` | 1 | `classes2.dex:0008` ← `Lug/e$b;->onEvent(Lbacktraceio/library/watchdog/BacktraceWatchdogTimeoutException;)V` |
| `getIxpEnrolledExperiments()Ljava/lang/String;` | `0x2ebacc5` | 1 | `classes2.dex:0000` ← `Lug/b;->get()Ljava/lang/Object;` |
| `getRolloutFlagsInControl()Ljava/lang/String;` | `0x2ebae46` | 1 | `classes2.dex:0000` ← `Lug/d;->get()Ljava/lang/Object;` |
| `getRolloutFlagsInVariant()Ljava/lang/String;` | `0x2ebacd8` | 1 | `classes2.dex:0000` ← `Lug/c;->get()Ljava/lang/Object;` |
| `initMaquettesSDK(Landroid/app/Activity;)V` | `0x1dd72a0` | 1 | `classes2.dex:0004` ← `Luh/e$c;->run()V` |
| `isColdStartDeeplinkToGame()Z` | `0x25448eb` | 1 | `classes2.dex:0019` ← `Lcom/roblox/client/ActivityNativeMain;->P2(Lcom/roblox/client/feature/FeatureState;)V` |
| `isVRVirtualInputFlagEnabled()Z` | `0x2ebaa33` | 0 | — |
| `nativeAppBridgeStartLuaAppDM()V` | `0x25448f6` | 1 | `classes2.dex:004d` ← `Lcom/roblox/client/ActivityNativeMain;->P2(Lcom/roblox/client/feature/FeatureState;)V` |
| `nativeAppBridgeV2DestroyApp()V` | `0x2ebc99f` | 1 | `classes2.dex:000b` ← `Luh/e;->f()V` |
| `nativeAppBridgeV2InitWithParams(Lcom/roblox/engine/jni/autovalue/InitParams;)V` | `0x24abf96` | 1 | `classes2.dex:00dd` ← `Luh/e;->j(Luh/e$d;)V` |
| `nativeAppBridgeV2LeaveGame()V` | `0x2ebf0ac` | 1 | `classes2.dex:0003` ← `Luh/h0;->t()V` |
| `nativeAppBridgeV2OnLowMemory()V` | `0x2ebf2aa` | 1 | `classes2.dex:0025` ← `Luh/r0;->j(I)V` |
| `nativeAppBridgeV2OnLowMemoryForRenderView(II)V` | `0x2ebf3a9` | 1 | `classes2.dex:0033` ← `Luh/r0;->j(I)V` |
| `nativeAppBridgeV2PauseApp()V` | `0x2ebc823` | 1 | `classes2.dex:0012` ← `Luh/e;->G()V` |
| `nativeAppBridgeV2PauseGame()V` | `0x2ebeb2c` | 1 | `classes2.dex:002f` ← `Luh/h0;->F()V` |
| `nativeAppBridgeV2ResumeGameWithPlatformParams(Landroid/view/Surface;Lcom/roblox/engine/jni/model/PlatformParams;Landroid/app/Activity;)V` | `0x2ebeca8` | 1 | `classes2.dex:008f` ← `Luh/h0;->G()V` |
| `nativeAppBridgeV2SendAppEventOnAppReady(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V` | `0x2ebcf36` | 1 | `classes2.dex:0047` ← `Luh/e;->t(Ljava/util/Vector;)V` |
| `nativeAppBridgeV2SendAppEventOnGameLoaded(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V` | `0x2ebca9e` | 1 | `classes2.dex:0012` ← `Luh/e;->z(Lmk/c;)V` |
| `nativeAppBridgeV2StartAppWithParams(Lcom/roblox/engine/jni/autovalue/StartAppParams;)V` | `0x26fe80f` | 1 | `classes2.dex:0055` ← `Luh/e;->F(Landroid/view/Surface;)V` |
| `nativeAppBridgeV2StartGameWithParam(Lcom/roblox/engine/jni/autovalue/StartGameParams;)I` | `0x2ebd51a` | 1 | `classes2.dex:00e0` ← `Luh/h0;->C(Luh/k0;)V` |
| `nativeAppBridgeV2UpdateSurfaceAppWithPlatformParams(Landroid/view/Surface;Lcom/roblox/engine/jni/model/PlatformParams;)V` | `0x2773194` | 1 | `classes2.dex:0016` ← `Luh/e;->H(Landroid/view/Surface;FII)V` |
| `nativeAppBridgeV2UpdateSurfaceGameWithPlatformParams(Landroid/view/Surface;Lcom/roblox/engine/jni/model/PlatformParams;Landroid/app/Activity;)V` | `0x2ebeeaa` | 1 | `classes2.dex:007a` ← `Luh/h0;->G()V` |
| `nativeAppBridgeV2UserDidLogout()V` | `0x2ebf1ab` | 1 | `classes2.dex:001d` ← `Lcom/roblox/client/ActivityNativeMain;->R(ILandroid/os/Bundle;)V` |
| `nativeBroadcastConnection(ILjava/lang/String;Ljava/lang/String;)V` | `0x2eba6ea` | 0 | — |
| `nativeBroadcastEventWithNamespace(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V` | `0x2eba1ae` | 7 | `classes2.dex:000b` ← `Lcom/roblox/client/ActivityNativeMain;->i3(Z)V`<br>`classes2.dex:0013` ← `Lcom/roblox/client/RobloxWebActivity$b;->onCreate(Landroidx/lifecycle/s;)V`<br>`classes2.dex:0013` ← `Lcom/roblox/client/RobloxWebActivity$b;->onDestroy(Landroidx/lifecycle/s;)V`<br>`classes2.dex:000e` ← `Lpj/f;->b(Ljava/lang/String;Z)V`<br>`classes2.dex:0020` ← `Lpj/f;->e(Ljava/lang/String;)V`<br>`classes2.dex:0013` ← `Lvg/a;->onCreate(Landroidx/lifecycle/s;)V`<br>`classes2.dex:0013` ← `Lvg/a;->onDestroy(Landroidx/lifecycle/s;)V` |
| `nativeCallMessagesFromMainThread()V` | `0x2eb5012` | 0 | — |
| `nativeFetchLmkdProperties(Ljava/lang/String;)V` | `0x2eb92ea` | 0 | — |
| `nativeGameGlobalInit()V` | `0x233762c` | 2 | `classes2.dex:0083` ← `Lcom/roblox/client/game/ExperienceSession;->l0()V`<br>`classes2.dex:0050` ← `Luh/e;->E(Landroid/content/Context;)V` |
| `nativeGetTextBoxInfo()Lcom/roblox/engine/jni/model/NativeTextBoxInfo;` | `0x2eba818` | 3 | `classes2.dex:0007` ← `Lcom/roblox/client/game/b;->D(Llk/b;F)V`<br>`classes2.dex:0000` ← `Lcom/roblox/client/game/b;->c()V`<br>`classes2.dex:0000` ← `Luh/p0;->c()V` |
| `nativeHandleBackPressed()V` | `0x2eb6749` | 2 | `classes2.dex:000c` ← `Lcom/roblox/client/startup/MainGameActivity;->u2(Le/m0;)Lkotlin/Unit;`<br>`classes2.dex:0000` ← `Luh/e;->q()V` |
| `nativeImageSavedToAlbumFinished(Ljava/lang/String;ZLjava/lang/String;)V` | `0x2eb9507` | 1 | `classes2.dex:006e` ← `Luh/j;->u(Ljava/lang/String;)V` |
| `nativeInGamePurchaseFinished(ZJLjava/lang/String;)V` | `0x2eb95df` | 3 | `classes2.dex:0078` ← `Lcom/roblox/client/purchase/IAPPurchaseManager;->t(ZJLjava/lang/String;Ljava/lang/String;)V`<br>`classes2.dex:0140` ← `Ldj/m$k;->a(Lcom/android/billingclient/api/d;Ljava/util/List;)V`<br>`classes2.dex:003a` ← `Ldj/m;->g(Ljava/lang/String;JJ)Z` |
| `nativeInGamePurchaseFinishedV2(ZJLjava/lang/String;Ljava/lang/String;)V` | `0x2eb97b5` | 3 | `classes2.dex:0074` ← `Lcom/roblox/client/purchase/IAPPurchaseManager;->t(ZJLjava/lang/String;Ljava/lang/String;)V`<br>`classes2.dex:0130` ← `Ldj/m$k;->a(Lcom/android/billingclient/api/d;Ljava/util/List;)V`<br>`classes2.dex:0036` ← `Ldj/m;->g(Ljava/lang/String;JJ)Z` |
| `nativeInitBenchmarkSettings(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)I` | `0x2eb924f` | 0 | — |
| `nativeInitClientSettings(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)I` | `0x2359277` | 1 | `classes2.dex:0064` ← `Luh/e$f;->a([Ljava/lang/Void;)Ljava/lang/Integer;` |
| `nativeInitClientSettingsCached(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;J)I` | `0x2eb8d87` | 0 | — |
| `nativeInitClientSettingsCachedCompressed([BLjava/lang/String;Ljava/lang/String;Ljava/lang/String;JZ)I` | `0x2eb8ff7` | 0 | — |
| `nativeInitClientSettingsSigned(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)I` | `0x2eb8a83` | 1 | `classes2.dex:0057` ← `Luh/e$f;->a([Ljava/lang/Void;)Ljava/lang/Integer;` |
| `nativeMarketplacePurchaseFinished(ZLjava/lang/String;Ljava/lang/String;)V` | `0x2eb9bed` | 2 | `classes2.dex:0027` ← `Lcom/roblox/client/c;->Y2(Lcj/k;)V`<br>`classes2.dex:0027` ← `Lrg/u1;->x2(Lcj/k;)V` |
| `nativeOOAPPurchaseFinished(Lcom/roblox/engine/jni/autovalue/OOAPPurchaseFinishedParams;)V` | `0x2eb9df6` | 2 | `classes2.dex:00bd` ← `Ldj/a0;->J(Lv7/k;Ldj/e$b;Ldj/m0;)V`<br>`classes2.dex:008b` ← `Ldj/m;->z0(Lv7/k;Ldj/e$b;Ldj/m0;)V` |
| `nativeOnExperienceSessionPause()V` | `0x2eba16b` | 1 | `classes2.dex:000f` ← `Lcom/roblox/client/game/ExperienceSession;->h0(Z)V` |
| `nativeOnExperienceSessionResume()V` | `0x2eba12b` | 1 | `classes2.dex:000f` ← `Lcom/roblox/client/game/ExperienceSession;->g0(Z)V` |
| `nativeOnFragmentStart()V` | `0x1dd72a0` | 1 | `classes2.dex:0015` ← `Lcom/roblox/client/game/ExperienceSession;->g0(Z)V` |
| `nativeOnFragmentStop()V` | `0x1dd72a0` | 1 | `classes2.dex:0015` ← `Lcom/roblox/client/game/ExperienceSession;->h0(Z)V` |
| `nativePassCurrentDisplayRefreshRate(F)V` | `0x24783fb` | 1 | `classes2.dex:000f` ← `Luh/r0;->n(Landroid/view/Display;)V` |
| `nativePassKeyEvent(ZIIZ)V` | `0x2eb67ec` | 2 | `classes2.dex:0020` ← `Lkk/g;->e(Landroid/view/KeyEvent;)Z`<br>`classes2.dex:0022` ← `Lkk/g;->f(Landroid/view/KeyEvent;)Z` |
| `nativePassSupportedRefreshRates([F)V` | `0x24784d9` | 1 | `classes2.dex:003a` ← `Luh/r0;->h()V` |
| `nativePassText(JLjava/lang/String;ZI)V` | `0x2eb93ab` | 2 | `classes2.dex:0027` ← `Lcom/roblox/client/RbxKeyboard$a;->onTextChanged(Ljava/lang/CharSequence;III)V`<br>`classes2.dex:0044` ← `Lcom/roblox/client/RbxKeyboard$b;->onEditorAction(Landroid/widget/TextView;ILandroid/view/KeyEvent;)Z` |
| `nativePostClientSettingsLoadedInitialization3(Ljava/util/List;)V` | `0x2383c18` | 1 | `classes2.dex:000e` ← `Luh/e$f;->b(Ljava/lang/Integer;)V` |
| `nativeReleaseAllRbxEventStreams()V` | `0x2eb502b` | 1 | `classes2.dex:0000` ← `Lug/j;->r()V` |
| `nativeReleaseFocus(J)V` | `0x2eb94c9` | 3 | `classes2.dex:0015` ← `Lcom/roblox/client/RbxKeyboard;->h()V`<br>`classes2.dex:0000` ← `Lcom/roblox/client/RbxKeyboard;->j(J)V`<br>`classes2.dex:0018` ← `Lcom/roblox/client/game/b;->onConfigurationChanged(Landroid/content/res/Configuration;)V` |
| `nativeReportToDiagByCountryCode(Ljava/lang/String;Ljava/lang/String;D)V` | `0x2ebf4c2` | 1 | `classes2.dex:0003` ← `Lti/g;->s(Ljava/lang/String;J)V` |
| `nativeReturnPressedFromOnScreenKeyboard(J)V` | `0x2eb948b` | 1 | `classes2.dex:0025` ← `Lcom/roblox/client/RbxKeyboard$b;->onEditorAction(Landroid/widget/TextView;ILandroid/view/KeyEvent;)Z` |
| `nativeScheduleOnFirstFrame(Ljava/lang/Runnable;)V` | `0x2ebf950` | 1 | `classes2.dex:0032` ← `Lcom/roblox/client/ActivityNativeMain;->C2()V` |
| `nativeSendExtendedAnalytics([BI)V` | `0x2eb5030` | 0 | — |
| `nativeSendExtendedAnalyticsJoinData(Ljava/lang/String;)V` | `0x2eb50f0` | 0 | — |
| `nativeSetAppPreviousExitReasons(Ljava/util/List;)V` | `0x2eb92b6` | 1 | `classes2.dex:0004` ← `Lcom/roblox/client/startup/NativeHelper;->Q(Landroid/content/Context;Lcom/roblox/client/startup/NativeHelper;)V` |
| `nativeUpdateAdapterInit()V` | `0x234df63` | 1 | `classes2.dex:005d` ← `Luh/e;->E(Landroid/content/Context;)V` |
| `nativeUpdateMobileHapticsState(ZLjava/lang/String;)V` | `0x2450da1` | 1 | `classes2.dex:008a` ← `Lfm/b;->l(Landroid/content/Context;)V` |
| `nativeUserGameSettingsGetReducedMotion()Z` | `0x2eba9f2` | 4 | `classes2.dex:00bd` ← `Lcom/roblox/client/RobloxWebActivity;->onCreate(Landroid/os/Bundle;)V`<br>`classes2.dex:0007` ← `Lrg/o;->k(Landroid/app/Activity;Landroid/content/Intent;Z)V`<br>`classes2.dex:004a` ← `Lvi/d;->f(Landroidx/lifecycle/r;)V`<br>`classes2.dex:003a` ← `Lwg/c;->a(Landroid/app/Activity;Ljava/lang/String;)V` |
| `readLocalFlags()Lcom/roblox/engine/jni/model/ClientLocalFlags;` | `0x231b945` | 1 | `classes2.dex:0015` ← `Lcom/roblox/client/startup/a;->l(Landroid/content/Context;)V` |
| `reportBatteryStateChanged(II)V` | `0x2ebf7f1` | 1 | `classes2.dex:003c` ← `Luh/h;->n(Landroid/content/Intent;)V` |
| `reportBatteryStatus(Lcom/roblox/engine/jni/model/BatteryStatus;)V` | `0x2476fac` | 1 | `classes2.dex:01ca` ← `Luh/h;->m()V` |
| `reportNetworkStateChanged(I)V` | `0x2479215` | 1 | `classes2.dex:0004` ← `Luh/t0;->e(Landroid/net/ConnectivityManager;Luh/t0$a;)V` |
| `reportThermalStateChanged(I)V` | `0x24724c0` | 3 | `classes2.dex:0000` ← `Luh/y0$a;->onThermalStatusChanged(I)V`<br>`classes2.dex:001e` ← `Luh/y0;->a()V`<br>`classes2.dex:0029` ← `Luh/y0;->b()V` |
| `screenTimeResume()V` | `0x2ec2d3c` | 0 | — |
| `screenTimeStart()V` | `0x2ec2c97` | 0 | — |
| `screenTimeStartByGuac()V` | `0x2ec2e86` | 2 | `classes2.dex:0045` ← `Lyj/k0;->G(Landroid/app/Activity;Ljava/lang/String;)V`<br>`classes2.dex:0000` ← `Lzj/b$a;->d()V` |
| `screenTimeStop()V` | `0x2ec2de1` | 1 | `classes2.dex:02f2` ← `Luh/m;->a(Ljava/lang/String;Ljava/lang/String;)V` |
| `setIrisJoinTimeEnabled()V` | `0x1dd72a0` | 0 | — |
| `setMobileAdvertisingId(Ljava/lang/String;)V` | `0x2eb53a8` | 2 | `classes2.dex:000b` ← `Lcom/roblox/client/ActivityNativeMain;->S()V`<br>`classes2.dex:0004` ← `Lcom/roblox/client/startup/NativeHelper;->c0()V` |
| `setTaskSchedulerBackgroundMode(ZLjava/lang/String;)V` | `0x2eba91c` | 5 | `classes2.dex:0025` ← `Lcom/roblox/client/game/ExperienceSession;->C()V`<br>`classes2.dex:001b` ← `Lcom/roblox/client/game/ExperienceSession;->X()V`<br>`classes2.dex:0024` ← `Luh/e;->G()V`<br>`classes2.dex:001f` ← `Luh/e;->s()V`<br>`classes2.dex:0006` ← `Luh/e;->y()V` |
| `setVRCameraCFrame(FFF)V` | `0x2ebaa91` | 0 | — |
| `setVRWorldRayCFrameFromScreenPosition(IFFI)V` | `0x2ebab53` | 0 | — |
| `setVirtualVRJoyStickPosition(FF)V` | `0x2ebabb7` | 0 | — |
| `setWebviewUserAgent(Ljava/lang/String;)V` | `0x2eb5160` | 2 | `classes2.dex:0004` ← `Lcom/roblox/client/ActivityNativeMain;->n0()V`<br>`classes2.dex:0006` ← `Lcom/roblox/client/startup/NativeHelper;->q0(Lcom/roblox/client/startup/NativeHelper;)V` |
| `shouldDisplayOpenGLUnsupportedMessage()Z` | `0x1df06e0` | 0 | — |
| `syncTextboxTextAndCursorPosition2(Ljava/lang/String;I)V` | `0x246928d` | 1 | `classes2.dex:002a` ← `Lcom/roblox/client/RbxKeyboard;->k()V` |
| `takeScreenshot()V` | `0x2ebab15` | 0 | — |
| `updateAppUISizes(IIIII)V` | `0x26cbcd2` | 1 | `classes2.dex:0036` ← `Llk/a;->a(IIIII)V` |
| `updateKeyboardSize(ZIIII)V` | `0x26c1139` | 3 | `classes2.dex:0075` ← `Lcom/roblox/client/game/b;->E(Llk/b;F)V`<br>`classes2.dex:0079` ← `Lcom/roblox/client/game/b;->E(Llk/b;F)V`<br>`classes2.dex:0036` ← `Llk/a;->e(ZIIII)V` |

### 2.2 `NativeSettingsInterface` — 32 native declarations

| Native method (DEX reference) | Export | Refs | Direct DEX callers |
| --- | --- | ---: | --- |
| `getInteractiveAssertsEnabled()Z` | `0x1df06e0` | 1 | `classes2.dex:0000` ← `Lcom/roblox/engine/jni/util/AssertDialogUtil;->b()Z` |
| `getRunningArchitecture()I` | `0x2327460` | 1 | `classes2.dex:0000` ← `Lrg/r;->i()Lrg/r$b;` |
| `nativeCPUSupportsNEON()Z` | `0x2ecb382` | 0 | — |
| `nativeConfigureDevRpc(Ljava/lang/String;Ljava/lang/String;)V` | `0x1dd72a0` | 1 | `classes2.dex:002d` ← `Lrg/q;->c(Landroid/content/Context;)V` |
| `nativeGetCookiesForDomain(Ljava/lang/String;)Ljava/lang/String;` | `0x2ecb2a7` | 0 | — |
| `nativeGetCookiesInNetscapeFormat(Ljava/lang/String;)Ljava/lang/String;` | `0x2330328` | 3 | `classes2.dex:0010` ← `Lrg/w0;->u()Z`<br>`classes2.dex:0066` ← `Lyj/k0;->T()Z`<br>`classes2.dex:0000` ← `Lzj/d$a;->d(Ljava/lang/String;)Ljava/lang/String;` |
| `nativeGetCpuFamilyAndFeatures()Ljava/lang/String;` | `0x2ecb3c3` | 0 | — |
| `nativeGetDevRpcConfiguration()Ljava/lang/String;` | `0x2ecb4da` | 1 | `classes2.dex:0003` ← `Lrg/q;->b(Landroid/content/Context;)Ljava/lang/String;` |
| `nativeInitAppCrashpadReporter(Lcom/roblox/engine/jni/model/NativeInitCrashpadParams;)Z` | `0x2ec9bd6` | 0 | — |
| `nativeInitCrashpad(Lcom/roblox/engine/jni/model/NativeInitCrashpadParams;)Z` | `0x232d6b8` | 1 | `classes2.dex:00d5` ← `Lrg/w0;->h0()V` |
| `nativeInitFastLog()V` | `0x2329895` | 1 | `classes2.dex:0039` ← `Lrg/w0;->W0(Z)V` |
| `nativeIsDevRpcEnabled()Z` | `0x1df06e0` | 1 | `classes2.dex:0000` ← `Lrg/q;->c(Landroid/content/Context;)V` |
| `nativeIsLuaLoginEnabled()Z` | `0x2ecb4cf` | 1 | `classes2.dex:0000` ← `Luj/c;->b()Z` |
| `nativeOverrideChannelPlatformName(Ljava/lang/String;)V` | `0x232960a` | 1 | `classes2.dex:002f` ← `Lrg/w0;->W0(Z)V` |
| `nativeOverrideChannelPlatformName2(Ljava/lang/String;)V` | `0x2ec6ae3` | 1 | `classes2.dex:0027` ← `Lrg/w0;->W0(Z)V` |
| `nativeParseRandomChannelSelection(Ljava/lang/String;)Lcom/roblox/engine/jni/model/ChannelRecord;` | `0x2ecb859` | 0 | — |
| `nativeRunCrashpadHandler([Ljava/lang/String;)I` | `0x2eca720` | 1 | `classes2.dex:0037` ← `Lcom/roblox/client/analytics/CrashpadHandler;->main([Ljava/lang/String;)V` |
| `nativeSetBaseDataDirectories(Ljava/lang/String;Ljava/lang/String;)V` | `0x24650a9` | 2 | `classes2.dex:00d1` ← `Lyj/k0$a;->a(I)V`<br>`classes2.dex:0000` ← `Lzj/d$b;->d(Ljava/lang/String;Ljava/lang/String;)V` |
| `nativeSetBaseUrl(Ljava/lang/String;Ljava/lang/String;)V` | `0x23268be` | 2 | `classes2.dex:000f` ← `Lrg/w0;->W0(Z)V`<br>`classes2.dex:0012` ← `Lti/b;->m(Landroid/content/Context;)V` |
| `nativeSetCacheDirectory(Ljava/lang/String;)V` | `0x232972f` | 1 | `classes2.dex:0002` ← `Lrg/w0;->T0()V` |
| `nativeSetDefaultAppPolicyFile(Ljava/lang/String;)V` | `0x2ec6ff2` | 1 | `classes2.dex:001d` ← `Lti/b;->a(Landroid/content/Context;Ljava/lang/String;)V` |
| `nativeSetDeviceInfo(Lcom/roblox/engine/jni/model/DeviceParams;)V` | `0x2479c55` | 3 | `classes2.dex:0016` ← `Lcom/roblox/client/startup/MainGameActivity;->E2()V`<br>`classes2.dex:0014` ← `Lyj/k0;->L()V`<br>`classes2.dex:0000` ← `Lzj/d$c;->d(Lcom/roblox/engine/jni/model/DeviceParams;)V` |
| `nativeSetExceptionReasonFilename(Ljava/lang/String;)V` | `0x2329557` | 1 | `classes2.dex:0004` ← `Lrg/w0;->W0(Z)V` |
| `nativeSetExternalDirectory(Ljava/lang/String;)V` | `0x2ec6f3f` | 3 | `classes2.dex:00a6` ← `Lcom/roblox/client/startup/NativeHelper;->Q(Landroid/content/Context;Lcom/roblox/client/startup/NativeHelper;)V`<br>`classes2.dex:0081` ← `Lyj/k0$a;->a(I)V`<br>`classes2.dex:0000` ← `Lzj/d$d;->d(Ljava/lang/String;)V` |
| `nativeSetFilesDirectory(Ljava/lang/String;)V` | `0x23297e2` | 1 | `classes2.dex:0007` ← `Lrg/w0;->T0()V` |
| `nativeSetHttpClientProxy(Ljava/lang/String;J)V` | `0x234bf34` | 1 | `classes2.dex:0014` ← `Lrg/w0;->U0()V` |
| `nativeSetMultipleCookies(Ljava/lang/String;Ljava/lang/String;)V` | `0x2333e4d` | 1 | `classes2.dex:002a` ← `Lrg/w0;->S0()V` |
| `nativeSetPlatformHeadersWithIdfa(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V` | `0x235ccae` | 1 | `classes2.dex:0008` ← `Lrg/w0;->X0()V` |
| `nativeSetPreferencesFile(Ljava/lang/String;)V` | `0x246515d` | 3 | `classes2.dex:003a` ← `Lcom/roblox/client/startup/NativeHelper;->Q(Landroid/content/Context;Lcom/roblox/client/startup/NativeHelper;)V`<br>`classes2.dex:00fe` ← `Lyj/k0$a;->a(I)V`<br>`classes2.dex:0000` ← `Lzj/d$e;->d(Ljava/lang/String;)V` |
| `nativeSetRobloxChannel(Ljava/lang/String;)V` | `0x2326e2e` | 2 | `classes2.dex:0016` ← `Lrg/w0;->W0(Z)V`<br>`classes2.dex:0019` ← `Lti/b;->m(Landroid/content/Context;)V` |
| `nativeSetRobloxVersion(Ljava/lang/String;)V` | `0x232d484` | 1 | `classes2.dex:0040` ← `Lrg/w0;->W0(Z)V` |
| `nativeSetUserId(Ljava/lang/String;)V` | `0x2ec6a1d` | 1 | `classes2.dex:005c` ← `Lrg/w0;->W0(Z)V` |

### 2.3 `NativeGLJavaInterface` — 0 native declarations (Java callback surface)

Because there are no native declarations here, the table lists its Java methods and the direct DEX callers observed. Field gets (`sget-object`) are shown separately where they are the only reference: they are the Java side of the JNI callback dispatch.

| Java method | Direct DEX `invoke-static` callers | Other references |
| --- | --- | --- |
| `<clinit>()V` | — | `sput-object sImplementation:Lcom/roblox/engine/jni/EngineJavaCallback2; @ 0005` |
| `<init>()V` | — | — |
| `exitGameWithError(I)V` | — | `sget-object sImplementation:Lcom/roblox/engine/jni/EngineJavaCallback2; @ 0000` |
| `gameDidLeave()V` | — | `sget-object sExitImplementation:Lcom/roblox/engine/jni/EngineExitJavaCallback2; @ 0000` |
| `gameLoadedCallback(J)V` | — | `sget-object sImplementation:Lcom/roblox/engine/jni/EngineJavaCallback2; @ 0000` |
| `getDeviceStaticParams()Lcom/roblox/engine/jni/model/DeviceStaticParams;` | — | `sget-object sDeviceStaticParams:Lcom/roblox/engine/jni/model/DeviceStaticParams; @ 0000` |
| `getImplementation()Lcom/roblox/engine/jni/EngineJavaCallback2;` | — | `sget-object sImplementation:Lcom/roblox/engine/jni/EngineJavaCallback2; @ 0000` |
| `getMobileAdvertisingId()V` | — | `sget-object sMobileAdvertisingIdGetter:Ljava/lang/ref/WeakReference; @ 0000` |
| `getWebViewUserAgent()V` | — | `sget-object sWebViewUserAgentGetter:Ljava/lang/ref/WeakReference; @ 0000` |
| `hideKeyboard()V` | — | `sget-object sImplementation:Lcom/roblox/engine/jni/EngineJavaCallback2; @ 0000` |
| `listenToMotionEvents(Ljava/lang/String;)V` | — | `sget-object sImplementation:Lcom/roblox/engine/jni/EngineJavaCallback2; @ 0000` |
| `onAppBridgeNotification(Ljava/lang/String;Ljava/lang/String;)V` | — | `sget-object sAppBridgeNotificationListener:Lcom/roblox/engine/jni/OnAppBridgeNotificationListener; @ 0000` |
| `onAppShellReloadNeeded()V` | — | `sget-object sAppShellReloadNeededListener:Ljava/lang/ref/WeakReference; @ 0000` |
| `onDataModelNotificationCallback(Ljava/lang/String;Ljava/lang/String;)V` | — | `sget-object sImplementation:Lcom/roblox/engine/jni/EngineJavaCallback2; @ 0000` |
| `onExtendedAnalyticsRecvCallback([BI)V` | — | `sget-object sExtendedAnalyticsImplementation:Lcom/roblox/engine/jni/NativeGLJavaInterface$IExtendedAnalyticsInterface; @ 0000` |
| `onLuaTextBoxChangedCallback(Ljava/lang/String;)V` | — | `sget-object sImplementation:Lcom/roblox/engine/jni/EngineJavaCallback2; @ 0000` |
| `onLuaTextBoxPropertyChangedCallback()V` | — | `sget-object sImplementation:Lcom/roblox/engine/jni/EngineJavaCallback2; @ 0000` |
| `onVrSessionStateUpdate(I)V` | — | `sget-object sImplementation:Lcom/roblox/engine/jni/EngineJavaCallback2; @ 0000` |
| `openNativeOverlay(Ljava/lang/String;Ljava/lang/String;)V` | — | `sget-object sImplementation:Lcom/roblox/engine/jni/EngineJavaCallback2; @ 0000` |
| `promptNativePurchase(JLjava/lang/String;)V` | — | `sget-object sImplementation:Lcom/roblox/engine/jni/EngineJavaCallback2; @ 0000` |
| `promptNativePurchase(JLjava/lang/String;Ljava/lang/String;)V` | — | `sget-object sImplementation:Lcom/roblox/engine/jni/EngineJavaCallback2; @ 0000` |
| `promptNativePurchaseWithPayload(JLjava/lang/String;Ljava/lang/String;)V` | — | `sget-object sImplementation:Lcom/roblox/engine/jni/EngineJavaCallback2; @ 0000` |
| `promptNativePurchaseWithPaymentSessionId(JLjava/lang/String;Ljava/lang/String;)V` | — | `sget-object sImplementation:Lcom/roblox/engine/jni/EngineJavaCallback2; @ 0000` |
| `promptNativePurchaseWithPaymentSessionId(JLjava/lang/String;Ljava/lang/String;Ljava/lang/String;)V` | — | `sget-object sImplementation:Lcom/roblox/engine/jni/EngineJavaCallback2; @ 0000` |
| `replaceImplementation(Ljava/lang/Class;Lcom/roblox/engine/jni/EngineJavaCallback2;)V` | `classes2.dex:0018` ← `Luh/e;->u()V` | `sget-object sImplementation:Lcom/roblox/engine/jni/EngineJavaCallback2; @ 0000` |
| `saveImageToAlbum(Ljava/lang/String;)V` | — | `sget-object sImplementation:Lcom/roblox/engine/jni/EngineJavaCallback2; @ 0000` |
| `screenOrientationChanged(I)V` | — | `sget-object sImplementation:Lcom/roblox/engine/jni/EngineJavaCallback2; @ 0000` |
| `setAppBridgeNotificationListener(Lcom/roblox/engine/jni/OnAppBridgeNotificationListener;)V` | `classes2.dex:006e` ← `Lyj/k0;->P()V`<br>`classes2.dex:0000` ← `Lzj/c$a;->d(Lcom/roblox/engine/jni/OnAppBridgeNotificationListener;)V` | `sput-object sAppBridgeNotificationListener:Lcom/roblox/engine/jni/OnAppBridgeNotificationListener; @ 0000` |
| `setAppShellReloadNeededListener(Lcom/roblox/engine/jni/NativeGLJavaInterface$OnAppShellReloadNeededListener;)V` | `classes2.dex:010d` ← `Lcom/roblox/client/ActivityNativeMain;->onCreate(Landroid/os/Bundle;)V`<br>`classes2.dex:002a` ← `Lcom/roblox/client/ActivityNativeMain;->onDestroy()V` | `sput-object sAppShellReloadNeededListener:Ljava/lang/ref/WeakReference; @ 0005` |
| `setDeviceStaticParams(Lcom/roblox/engine/jni/model/DeviceStaticParams;)V` | `classes2.dex:012c` ← `Lcom/roblox/client/RobloxApplication;->onCreate()V` | `sput-object sDeviceStaticParams:Lcom/roblox/engine/jni/model/DeviceStaticParams; @ 0000` |
| `setExitImplementation(Lcom/roblox/engine/jni/EngineExitJavaCallback2;)V` | `classes2.dex:00ba` ← `Lcom/roblox/client/game/ExperienceSession;->D(Z)V`<br>`classes2.dex:0016` ← `Lcom/roblox/client/game/ExperienceSession;->j0(J)V` | `sput-object sExitImplementation:Lcom/roblox/engine/jni/EngineExitJavaCallback2; @ 0000` |
| `setExtendedAnalyticsImplementation(Lcom/roblox/engine/jni/NativeGLJavaInterface$IExtendedAnalyticsInterface;)V` | — | `sput-object sExtendedAnalyticsImplementation:Lcom/roblox/engine/jni/NativeGLJavaInterface$IExtendedAnalyticsInterface; @ 0000` |
| `setImplementation(Lcom/roblox/engine/jni/EngineJavaCallback2;)V` | `classes2.dex:000e` ← `Lcom/roblox/client/game/ExperienceSession;->j0(J)V`<br>`classes2.dex:0008` ← `Lcom/roblox/engine/jni/NativeGLJavaInterface;->replaceImplementation(Ljava/lang/Class;Lcom/roblox/engine/jni/EngineJavaCallback2;)V`<br>`classes2.dex:0007` ← `Luh/e;->C(Lcom/roblox/engine/jni/EngineJavaCallback2;)V`<br>`classes2.dex:0004` ← `Luh/e;->g()V` | `sput-object sImplementation:Lcom/roblox/engine/jni/EngineJavaCallback2; @ 0002` |
| `setMobileAdvertisingIdGetter(Lcom/roblox/engine/jni/NativeGLJavaInterface$MobileAdvertisingIdGetter;)V` | `classes2.dex:0113` ← `Lcom/roblox/client/ActivityNativeMain;->onCreate(Landroid/os/Bundle;)V`<br>`classes2.dex:0030` ← `Lcom/roblox/client/ActivityNativeMain;->onDestroy()V`<br>`classes2.dex:0045` ← `Lcom/roblox/client/startup/NativeHelper;->a0(Lcom/roblox/client/startup/NativeHelper;Landroidx/lifecycle/s;Landroidx/lifecycle/l$a;)V`<br>`classes2.dex:01d1` ← `Lcom/roblox/client/startup/NativeHelper;->a0(Lcom/roblox/client/startup/NativeHelper;Landroidx/lifecycle/s;Landroidx/lifecycle/l$a;)V` | `sput-object sMobileAdvertisingIdGetter:Ljava/lang/ref/WeakReference; @ 0005` |
| `setWebviewUserAgentGetter(Lcom/roblox/engine/jni/NativeGLJavaInterface$WebViewUserAgentGetter;)V` | `classes2.dex:0110` ← `Lcom/roblox/client/ActivityNativeMain;->onCreate(Landroid/os/Bundle;)V`<br>`classes2.dex:002d` ← `Lcom/roblox/client/ActivityNativeMain;->onDestroy()V`<br>`classes2.dex:0042` ← `Lcom/roblox/client/startup/NativeHelper;->a0(Lcom/roblox/client/startup/NativeHelper;Landroidx/lifecycle/s;Landroidx/lifecycle/l$a;)V`<br>`classes2.dex:01cc` ← `Lcom/roblox/client/startup/NativeHelper;->a0(Lcom/roblox/client/startup/NativeHelper;Landroidx/lifecycle/s;Landroidx/lifecycle/l$a;)V` | `sput-object sWebViewUserAgentGetter:Ljava/lang/ref/WeakReference; @ 0005` |
| `showKeyboard(JZ[BLcom/roblox/engine/jni/model/NativeTextBoxInfo;)V` | — | `sget-object sImplementation:Lcom/roblox/engine/jni/EngineJavaCallback2; @ 0000` |

**Key negative result:** `gameLoadedCallback(J)V` is entered from native (JNI) only — its sole DEX reference is `sget-object sImplementation @ 0000` inside the method itself. No DEX method invokes it. A Java-side caller for this callback does not exist in the authoritative APK; any claim of a "Java call to gameLoadedCallback" would be fabrication. When entered, its 3-instruction body is `sget-object sImplementation` (`0x0000`) → `invoke-virtual EngineJavaCallback2.b(J)V` (`0x0002`) → `return-void` (`0x0005`): the JNI call is forwarded to the installed implementation object, e.g. `RunOnMainEngineJavaCallback(Handler, com/roblox/client/game/a)` installed by `ExperienceSession.j0` (§3.3) or `uh/e$e` installed by `uh/e.g` (§3.1). Evidence `ev_8622b8854a6cf440cb87df54b89c137f01333a585452b730cb01237dfcf8291d`.

## 3. Multi-hop chains in the authoritative DEX (decoded bytecode, not inference)

All offsets are code offsets inside `classes2.dex`. The arrows are decoded `invoke-*` instructions from `android-method-show`; each cited method was re-opened and its full instruction listing read.

### 3.1 Cold-start app-shell / DataModel init

```
ActivityNativeMain.j3()                         (only caller of P2: P2 <- j3 0x003b)
  -> P2(FeatureState)                           tag "rbx.appshell" / "loadDataModel()"
     0x0007  uh/e.i()                            singleton
     0x000b  uh/e.E(Context)
             0x0050  NativeGLInterface.nativeGameGlobalInit()                 RVA 0x233762c
             0x005d  NativeGLInterface.nativeUpdateAdapterInit()             RVA 0x234df63
     0x0016  uh/e.j(uh/e$d)                      (guarded: hasSettings && !initialized, else 0x00e7 return)
             0x0044  uh/e.g() -> 0x0004 NativeGLJavaInterface.setImplementation(uh/e$e)
                                                (this installs the app-shell JNI callback impl)
             0x00dd  NativeGLInterface.nativeAppBridgeV2InitWithParams(InitParams)  RVA 0x24abf96
     0x0019  NativeGLInterface.isColdStartDeeplinkToGame()Z
     0x004d  NativeGLInterface.nativeAppBridgeStartLuaAppDM()               RVA 0x25448f6
```

Other callers of `uh/e.E`/`uh/e.j` (so the same exports can be reached outside `P2`): `uh/a.F0(Bundle)` (`E` 0x0021, `j` 0x0043), `uh/a.N2()` (`j` 0x002f), `uh/e.r(Context,uh/e$h,Z)` (`E` 0x0011), and `uh/e.x(Context,Surface,Activity)` (`j` 0x0019).

### 3.2 StartApp / surface lifecycle

```
uh/a.P0(Z) 0x004a | uh/a.P2() 0x0024 | uh/e.x(...) 0x001c
  -> uh/e.F(Surface)
     0x0007  uh/e.y()                       -> setTaskSchedulerBackgroundMode (0x0006 there)
     0x0055  NativeGLInterface.nativeAppBridgeV2StartAppWithParams(StartAppParams)  RVA 0x26fe80f

uh/a.surfaceDestroyed(SurfaceHolder) 0x000e
  -> uh/e.G()
     0x0012  NativeGLInterface.nativeAppBridgeV2PauseApp()                  RVA 0x2ebc823
     0x0024  NativeGLInterface.setTaskSchedulerBackgroundMode(Z,String)     RVA 0x2eba91c

uh/e.H(Surface,F,I,I) 0x0016
  -> NativeGLInterface.nativeAppBridgeV2UpdateSurfaceAppWithPlatformParams(Surface,PlatformParams)
                                                                            RVA 0x2773194
```

### 3.3 Game join and the native→Java callback installation

```
com/roblox/client/game/c$a.run() 0x0058
  -> ExperienceSession.l0()                     "Start game session"
     0x0083  NativeGLInterface.nativeGameGlobalInit()                       RVA 0x233762c
             (one-shot: static ExperienceSession.w, set at 0x0081)
     0x0089  new uh/h0(ExperienceSession, uh/k0 from Bundle)
     0x0091  uh/h0.A(LayoutInflater,ViewGroup,Bundle)
     0x009b  ExperienceSession.j0(placeId)
             0x000e  NativeGLJavaInterface.setImplementation(
                        RunOnMainEngineJavaCallback(Handler, com/roblox/client/game/a))
             0x0016  NativeGLJavaInterface.setExitImplementation(ExperienceSession$b)

uh/h0.G()   "updateSurface()" state machine; sole direct DEX caller: uh/h0.d(uh/h0) 0x0000
     0x0037  (surfaceState==0) -> uh/h0.C(uh/k0)
                 0x00e0  NativeGLInterface.nativeAppBridgeV2StartGameWithParam(StartGameParams) I
                                                                            RVA 0x2ebd51a
                         StartGameParams.setPlaceId(j0.c) at uh/h0.C 0x0038
     0x007a  (state==2, f) -> nativeAppBridgeV2UpdateSurfaceGameWithPlatformParams  RVA 0x2ebeeaa
     0x008f  (state==2,!f) -> nativeAppBridgeV2ResumeGameWithPlatformParams        RVA 0x2ebeca8
```

### 3.4 Game-loaded event (Java→native) and the return callback (native→Java)

```
ActivityNativeMain.onActivityResult 0x0084 | ph/a.h(mk/c) 0x0004 | uh/e.A(Z) 0x0005
  -> uh/e.z(mk/c)
     0x0000 guard uh/e.b (manager setup); if false log "manager is not yet setup. Ignore." + return
     0x0012  NativeGLInterface.nativeAppBridgeV2SendAppEventOnGameLoaded(String,String,String)
                                                                            RVA 0x2ebca9e

native side -> JNI -> NativeGLJavaInterface.gameLoadedCallback(J)V
     (NO DEX caller; 0x0000 sget-object sImplementation -> 0x0002 EngineJavaCallback2.b(J)V
      -> 0x0005 return-void; implementation installed by setImplementation above)
```

### 3.5 Crashpad: boot init vs crash-handler subprocess

```
rg/w0.W0(Z) 0x0051
  -> rg/w0.h0()
     0x00af  const-class Lcom/roblox/client/analytics/CrashpadHandler; (canonical-name builder arg)
     0x00d5  NativeSettingsInterface.nativeInitCrashpad(NativeInitCrashpadParams) Z  RVA 0x232d6b8
     NO nativeRunCrashpadHandler call in this method (108 instructions, complete coverage)

com/roblox/client/analytics/CrashpadHandler.main(String[])
     0x0037  NativeSettingsInterface.nativeRunCrashpadHandler(String[]) I
     (sole DEX caller of nativeRunCrashpadHandler)
```

This independently confirms the CA01 / A05 **DEX-level** observation from the authoritative APK: on the real Java boot path the only crashpad native export called is `nativeInitCrashpad`; `nativeRunCrashpadHandler` is called only by `CrashpadHandler.main`, and boot never calls it. This is a statement about direct bytecode references only — it does **not** by itself prove where the flags-loaded latch (`0x7cdd149`) is or is not written at runtime. Wave-6 oracle work (worker-4, task #546) reports a runtime finding that may override the earlier Wave-5 static conclusion about latch writability in the main process; that question is outside this task's evidence and is left to the lead's integration. No claim about latch reachability is made here.

### 3.6 Scope-adjacent cross-check (separate class, not part of this task)

`nativeAppBridgeAppStart(...)` is declared in `com.roblox.engine.jni.NativeAppBridgeInterface`, not in any of the three owned classes. Its authoritative caller is `NativeAppBridgeInterface.a(String,String,Z,String,String) 0x0007`, which is reached from `yj/k0.P()` at `0x00a6`; `yj/k0.P()` also calls `setIsFirstInstall` (`0x003e`). This matches the bootstrap order described in `analysis/scale-a3-java-call-order.md` at the export-name level.

## 4. Divergence from earlier notes (report, do not "fix")

Older analysis notes (e.g. `analysis/scale-a3-java-call-order.md`, Wave-5 A05) decoded smali from `/tmp/opencode/mini/out/smali`, which `apktool-mini.log` shows came from `mini.apk`, **not** from the SHA-matched `base.apk`. Their obfuscated caller names therefore differ from the authoritative ones:

| Role | Earlier (`mini.apk`) note | Authoritative `base.apk` (this report) |
| --- | --- | --- |
| App-shell DataModel init wrapper | `uh/f.k(f$d)` | `Luh/e;->j(Luh/e$d;)V` |
| App-shell setup | `uh/f.F(Context)` | `Luh/e;->E(Landroid/content/Context;)V` |
| StartApp | `uh/f.G(Surface)` | `Luh/e;->F(Landroid/view/Surface;)V` |
| Cold-start loadDataModel | `ActivityNativeMain.Q2(FeatureState)` | `ActivityNativeMain.P2(FeatureState)` |
| Game-join startGame | `uh/j0.C(m0)` | `Luh/h0;->C(Luh/k0;)V` |
| GameLoaded event sender | `uh/f.A(mk/c)` | `Luh/e;->z(Lmk/c;)V` |
| Java callback installer | `uh/f.h()` / `NativeGLJavaInterface.setImplementation` | `Luh/e;->g()V` → `setImplementation(uh/e$e)` at `0x0004` |
| Boot flag setup | `yj/k0.P()` | `yj/k0.P()` (same) |

The **export-level facts** (method names, signatures, RVAs, and the semantic order init → StartApp → StartGame → game-loaded) agree; only obfuscated Java caller labels differ. This report is the authoritative DEX-level source for caller names. No earlier file was edited.

## 5. Limitations and honest consequences (decisions 013/038)

- Craft Anything reports `static_references_only` on every `android-class-users-find` call: reflection, JNI lookup by string, and runtime-loaded DEX are invisible. "Zero direct DEX callers" does **not** prove a method is never called at runtime; it proves no direct bytecode reference exists in these three DEX files.
- `NativeGLJavaInterface.setImplementation` is called from DEX (so a Java implementation object exists on the real Android boot path), but the harness cannot conjure a genuine guest-entered notifier → `gameLoadedCallback` path from this map alone. The graph shows *where* Java would call native and *where* native would call back; it does not supply the missing guest lifecycle event.
- No byte patches, fake callbacks, host callbacks, synthetic observations, counter inflation, gate weakening or relabeling of seeded runs are implied or endorsed by this report.
- No change to `src/`, `tests/`, CMake, docs, README, or the frozen image. No runtime was executed. Marker stays **WITHHELD**; `tests/app-shell.sh` stays RED. This is an evidence contribution only.

## 6. Evidence / reproduction

Evidence IDs are Craft Anything's own IDs (each record includes subject SHA and limitations).

| Operation | Command (from `/tmp/opencode`, `--deep`) | Evidence id |
| --- | --- | --- |
| class show `NativeGLInterface` | `android-class-show --path <base.apk> --class-name com.roblox.engine.jni.NativeGLInterface --output-format json` | `ev_311b1772bfd8115685150bb3d7421dba084535690dbf549e2ce91f3f8a16657b` |
| class show `NativeSettingsInterface` | `android-class-show --path <base.apk> --class-name com.roblox.engine.jni.NativeSettingsInterface --output-format json` | `ev_16ff017491c181f5bebf2316e713c49e20d1698ae6e578169110d8493b1f221b` |
| class show `NativeGLJavaInterface` | `android-class-show --path <base.apk> --class-name com.roblox.engine.jni.NativeGLJavaInterface --output-format json` | `ev_a9407a49efff5296e5e84ab348bab60de7edf381fac3c6e7ac9a34a1231c58fc` |
| DEX users of `NativeGLInterface` | `android-class-users-find --path <base.apk> --class-name com.roblox.engine.jni.NativeGLInterface --output-format json` | `ev_acb0214891bf889dbbdfacc70b72bccfe7799dceadbc9bdc210bce4ebd9999b0` |
| DEX users of `NativeSettingsInterface` | `android-class-users-find --path <base.apk> --class-name com.roblox.engine.jni.NativeSettingsInterface --output-format json` | `ev_57f123ea55bc2b6ce3898408c4d38cc981f89d2408aa22ccc758b5410e0240ae` |
| DEX users of `NativeGLJavaInterface` | `android-class-users-find --path <base.apk> --class-name com.roblox.engine.jni.NativeGLJavaInterface --output-format json` | `ev_64bfe6c804493e3f1ecccbc7be2e79ef8393de58473d887a65a9ffff11b61163` |
| bytecode `Lcom/roblox/client/ActivityNativeMain;->P2(Lcom/roblox/client/feature/FeatureState;)V` | `android-method-show --path <base.apk> --class-name <class> --method-name <name> --output-format json` | `ev_dfb0dc3e866ab055ef40e68f18863579e57871a9379c9de75e1308f5b159107c` |
| bytecode `Luh/e;->E(Landroid/content/Context;)V` | `android-method-show --path <base.apk> --class-name <class> --method-name <name> --output-format json` | `ev_5cfa71cb037b261217f3b66a993042065d8343d4042cf559e7cd24a9f378b89c` |
| bytecode `Luh/e;->j(Luh/e$d;)V` | `android-method-show --path <base.apk> --class-name <class> --method-name <name> --output-format json` | `ev_5723dd734a35ed63e1a784363c7bb9e199b4d58ab512e50f97da0b318b8069e1` |
| bytecode `Luh/e;->F(Landroid/view/Surface;)V` | `android-method-show --path <base.apk> --class-name <class> --method-name <name> --output-format json` | `ev_62c1bf7b205b50dea7dc6ef12f04e0cf854ebd391dd6618f9526049bb1adb9b8` |
| bytecode `Luh/e;->G()V` | `android-method-show --path <base.apk> --class-name <class> --method-name <name> --output-format json` | `ev_f8209d8c1088dbd5299664ec7f62f71cd8a3bda7d3eebf3db4dabb60bcc6c910` |
| bytecode `Lcom/roblox/client/game/ExperienceSession;->l0()V` | `android-method-show --path <base.apk> --class-name <class> --method-name <name> --output-format json` | `ev_55e91d822ae3b04172ce592fb03fa4ec89ab06e3d96f4d860e9ff979160a4e39` |
| bytecode `Luh/h0;->C(Luh/k0;)V` | `android-method-show --path <base.apk> --class-name <class> --method-name <name> --output-format json` | `ev_aa4ec6bee644222cbc89f3787643dfd2d6735ca6263ec251fe2a29a3725f1408` |
| bytecode `Luh/e;->z(Lmk/c;)V` | `android-method-show --path <base.apk> --class-name <class> --method-name <name> --output-format json` | `ev_94391c510d4253e71ace108ab0cb5be6be81e97a3850f2c6852e0201d696ea34` |
| bytecode `Lrg/w0;->h0()V` | `android-method-show --path <base.apk> --class-name <class> --method-name <name> --output-format json` | `ev_df3bbe29149df74e76fbb273ca85a27c3b0160699edc599a59f9cb72e430d38d` |
| bytecode `Lcom/roblox/client/analytics/CrashpadHandler;->main([Ljava/lang/String;)V` | `android-method-show --path <base.apk> --class-name <class> --method-name <name> --output-format json` | `ev_a2c121fad5e466f3f44484946d72b742c8b2a332559f0d82311a8d135cb372c6` |
| bytecode `Lcom/roblox/client/game/ExperienceSession;->j0(J)V` | `android-method-show --path <base.apk> --class-name <class> --method-name <name> --output-format json` | `ev_0b8c88b52f0f7278780fa12b3b08adc0088fd24eff7c5c52c474bca1fb03d399` |
| bytecode `Luh/h0;->G()V` | `android-method-show --path <base.apk> --class-name <class> --method-name <name> --output-format json` | `ev_33bda737627f862b20b9415a6dfa612ad14094817111bdfddf65cf14bec894a5` |
| bytecode `Lph/a;->h(Lmk/c;)V` | `android-method-show --path <base.apk> --class-name <class> --method-name <name> --output-format json` | `ev_596a9f1d141ac545e7d349791a995a3d3b28feb61839228727a35bc714662f1d` |
| bytecode `Luh/e;->g()V` | `android-method-show --path <base.apk> --class-name <class> --method-name <name> --output-format json` | `ev_994d6e8a453e962a1c8f52b6d43890f9407e18e40ccda1499a01aeb25f3b2dd3` |
| bytecode `NativeGLJavaInterface.gameLoadedCallback(J)V` | `android-method-show --path <base.apk> --class-name com.roblox.engine.jni.NativeGLJavaInterface --method-name gameLoadedCallback --output-format json` | `ev_8622b8854a6cf440cb87df54b89c137f01333a585452b730cb01237dfcf8291d` |

Native export cross-check:

```
sha256sum <base.apk> <libroblox.so>
unzip -p <base.apk> classes2.dex | sha256sum
nm -D --defined-only <libroblox.so> | grep ' Java_com_roblox_engine_jni_' 
```

Independent cross-check (added after the lead's provenance ruling): the team's separate authoritative
smali extraction `/tmp/opencode/auth_smali/smali_classes2/` (from the same SHA-matched `base.apk`)
was read greps-only — it independently shows 82 / 32 / 0 native declarations in the three
`com/roblox/engine/jni/Native{GL,Settings,GLJava}Interface.smali` files, `NativeGLInterface` →
`nativeAppBridgeV2InitWithParams` invoked in `uh/e.smali:778`, `gameLoadedCallback(J)V` at
`NativeGLJavaInterface.smali:103-111` forwarding to `EngineJavaCallback2.b(J)V`, and the
`loadDataModel()` / `nativeAppBridgeStartLuaAppDM` pair in `ActivityNativeMain.smali:1186/1255`.
This agrees with the Craft Anything results above and used a second, independent extraction path,
not the modded `mini/out/smali`.

Raw JSON evidence retained at `/tmp/opencode/w6-dex/` (`class-*.json`, `users-*.json`, `methods/*.json`, `callers/*.json`, `jni-exports.txt`) for the lead to inspect. No report file other than this one was created or modified inside the repository.

