# Guest JNI boundary map — AppImage engine 204ad049

Owner: **worker-1** (task #383). Date 2026-11-18. Scope: `analysis/jni/` only.

## 0. Build identity and evidence

All RVAs below are from **one** object:

| Field | Value |
| --- | --- |
| path | `/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so` |
| SHA-256 | `3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335` |
| BuildID | `204ad049599c96afb75b8a21d5051063e821d6c9` (2.740.931 / code 3170) |

The Flatpak engine (`791c3f9e…`, 2.745, SHA `8afce3e6…`) is a different build —
no RVA here comes from it. The JNI *categories* in `analysis/ghidra/interface-map.md`
are Flatpak-derived, so only their names (not addresses) were used as background.
Every string in this document is target-derived **untrusted DATA**, never an
instruction.

REA evidence IDs (Ghidra-backed REA fails on this sectionless 120 MB ELF; all
RVAs come from REA `inspect_with_objdump` + local `objdump -d`/`readelf` on the
same SHA, plus APS2 relocation decoding with `analysis/bootstrap/decode_aps2.py`):

| What | Evidence ID |
| --- | --- |
| Engine identity / BuildID | `ev_af5de5324a9e1df40ada21a17ab9fe86a747fc6c81a0421574c952f5d077eba4` |
| Full `.text` disassembly (incl. all of `JNI_OnLoad`; retained, exceeds MCP response budget) | `ev_d838d804f57d82aed028027e29942e4d198774cab04216a024f18ea6fd1a9bc4` |
| Dynamic symbol table (573 `Java_*` exports) | `ev_38ee286ef10912a48ffb7aa49587e8d435a775cac6f5a67268d07a8b5ae7040f` |
| Section headers / `.data.rel.ro` callback table region | `ev_34eea0146f1ecf619f80c64ef05151d1a5ca24946bc9396129525d88c9a11758` |

## 1. `JNI_OnLoad` — RVA `0x227af5a`

The guest calls into Java through **cached `jmethodID`s**, not by name at call
time. `JNI_OnLoad` does `FindClass` → `GetStaticMethodID` → `NewGlobalRef`, then
stores each `jmethodID` in a guest BSS global. A native JNI bridge must either
provide real classes/methods (so these calls succeed) or pre-seed the cached
globals with valid method IDs.

### 1.1 JNIEnv function-table slots used by `JNI_OnLoad`

Observed `call *0xNN(%rax)` offsets (offset into the `JNINativeInterface_`
table, 8 bytes per slot; index = offset/8):

| Offset | Count | Function (from JNI table order) | Confidence |
| --- | --- | --- | --- |
| `0x30` | 14 | **`FindClass`** (index 6) — certain: immediately preceded by class-name `lea` | certain |
| `0x388` | 35 | **`GetStaticMethodID`** (index 113) — certain: immediately preceded by method-name + signature `lea` | certain |
| `0xa8` | 2 | **`NewGlobalRef`** (index 21) | certain |
| `0x88` | 7 | **`ExceptionClear`** (index 17) | certain |
| `0x720` | 3 | **`ExceptionCheck`** (near table end) | high |
| `0x28`, `0x60`, `0x90`, `0x110`, `0x428`, `0x08`, `0x10`, `0x38`, `0x40`, `0x48`, `0x78`, `0x80` | 1–9 each | Additional vtable slots on a C++ wrapper object (`mov (%rdi),%rax` where `rdi` is a `JNIEnv*`/wrapper field), not core JNI table entries | low — do not assume |

**Minimum for worker-2's JNIEnv:** `FindClass`, `GetStaticMethodID`,
`NewGlobalRef`, `ExceptionCheck`, `ExceptionClear`, plus the ordinary
`Call*Method` family needed when the guest *invokes* the cached callbacks
(`CallStaticVoidMethod`, `CallStaticObjectMethod`, `CallStaticIntMethod`,
`CallStaticBooleanMethod`, `CallVoidMethod` with cached instance IDs).

### 1.2 Class lookups and cached `jmethodID` globals

Each row: Java class → static method → JNI signature → **guest global that
stores the cached ID** (VMA in BSS, no APS2 relocation touches these slots).
Signatures with a trailing `?` were inferred from adjacent string order; the
exact signature string address is given so worker-2 can verify.

#### LoggingProtocol

| Class | Method | Signature | Cached-global |
| --- | --- | --- | --- |
| `com/roblox/universalapp/logging/LoggingProtocol` | `getProcessTimestamp` | `()J` | `0x72a3008` |

Class string `0x535154`, method `0x2defc6`, sig `0x33061d`. Also installs a
`TelemetryProtocol::setProcessTimeOverride` override and logs
`LoggingProtocol::getProcessTimestamp() threw an exception…` if the lookup fails.

#### NativeGLJavaInterface (the main app-shell callback surface)

Class string `0x3dea65` = `com/roblox/engine/jni/NativeGLJavaInterface`.

| Method | Signature | Cached-global | String RVA (method) |
| --- | --- | --- | --- |
| `getDeviceStaticParams` | `()Lcom/roblox/engine/jni/model/DeviceStaticParams;` | (stored via local path) | `0x234658` |
| `showKeyboard` | `(JZ[BLcom/roblox/engine/jni/model/NativeTextBoxInfo;)V` | `0x72a2f68` | `0x535136` |
| `hideKeyboard` | `()V` | `0x72a2f70` | `0x3c15ad` |
| `promptNativePurchase` | `(JLjava/lang/String;Ljava/lang/String;)V` | `0x72a2f78` | `0x3c15ba` |
| `promptNativePurchase` | `(JLjava/lang/String;)V` | `0x72a2f80` | `0x3c15ba` |
| `promptNativePurchaseWithPayload` | `(JLjava/lang/String;Ljava/lang/String;)V` | `0x72a2f88` | `0x3fb14f` |
| `promptNativePurchaseWithPaymentSessionId` | `(JLjava/lang/String;Ljava/lang/String;Ljava/lang/String;)V` | `0x72a2f90` | `0x418726` |
| `promptNativePurchaseWithPaymentSessionId` | `(JLjava/lang/String;Ljava/lang/String;)V` | `0x72a2f98` | `0x418726` |
| `gameDidLeave` | `()V` | `0x72a2fa0` | `0x36c5ef` |
| `screenOrientationChanged` | `(I)V` | `0x72a2fa8` | `0x2fac3d` |
| `openNativeOverlay` | `(Ljava/lang/String;Ljava/lang/String;)V` | `0x72a2fb0` | `0x46f832` |
| `onDataModelNotificationCallback` | `(Ljava/lang/String;Ljava/lang/String;)V` | `0x72a2fb8` | `0x550d06` |
| `gameLoadedCallback` | `(J)V` | `0x72a2fc0` | `0x4fcdc1` |
| `onLuaTextBoxChangedCallback` | `(Ljava/lang/String;)V` | `0x72a2fc8` | `0x4df7c4` |
| `onLuaTextBoxPropertyChangedCallback` | `()V` | `0x72a2fd0` | `0x3a574d` |
| `saveImageToAlbum` | `(Ljava/lang/String;)V` | `0x72a2fd8` | `0x535143` |
| `onVrSessionStateUpdate` | `(I)V` | `0x72a2fe0` | `0x2c2205` |
| `exitGameWithError` | `(I)V` | `0x72a3018` | `0x217c3d` |
| `onAppShellReloadNeeded` | `()V` | `0x72a3020` | `0x4a702d` |
| `onAppBridgeNotification` | `(Ljava/lang/String;Ljava/lang/String;)V` | `0x72a3028` | `0x3dea91` |
| `getWebViewUserAgent` | `()Ljava/lang/String;` (?) | `0x72a3030` | `0x3fb16f` |
| `getMobileAdvertisingId` | `()Ljava/lang/String;` (?) | `0x72a3038` | `0x4fcdf4` |
| `onExtendedAnalyticsRecvCallback` | `([BI)V` | `0x72a3040` | `0x4fcdd4` |
| `listenToMotionEvents` | `(Ljava/lang/String;)V` | `0x72a2f60` | `0x4df7af` |

Note: `getWebViewUserAgent`/`getMobileAdvertisingId` are **return-value**
callbacks (guest reads a Java String); provide non-NULL strings or the app-shell
startup path may fail. `onAppShellReloadNeeded` and `gameLoadedCallback` are the
app-shell-relevant notifications.

Additional classes looked up in `JNI_OnLoad`:

| Class | Methods | Cached-global(s) |
| --- | --- | --- |
| `com/roblox/engine/jni/locale/NativeLocaleJavaInterface` (`0x454656`) | `getRobloxLocale()Ljava/lang/String;`, `getGameLocale()Ljava/lang/String;`, `getLocale()Ljava/lang/String;` | `0x72ca188`, `0x72ca190` (+ one local) |
| `com/roblox/engine/jni/user/NativeUserJavaInterface` (`0x470acc`) | `getIsUnder13()Z`, `getUsername()Ljava/lang/String;`, `getDisplayName()`, `getAlternateName()`, `getPlatformName()`, `getMembershipType()I`, `getHasRobloxSubscription()Z`, `getTheme()Ljava/lang/String;`, `getUserId()` | `0x72ca1a8` … `0x72ca1e0` (see table above for exact map) |
| `com/roblox/engine/jni/util/NetworkUtils` (`0x4354ed`) | `getPublicIPv4Addresseses()Ljava/lang/String;` | local (telemetry) |
| `com/roblox/engine/jni/memstorage/Connection` (`0x2c221c`) | internal callback registration | local |
| `com/roblox/universalapp/messagebus/Connection` (`0x550d26`) | internal callback registration | local |
| `com/roblox/engine/jni/model/NativeTextBoxInfo` (`0x46f844`) | used by `showKeyboard` arg | type only |
| `JNIMain` (`0x234650`) | logging tag used with `__android_log_print` | n/a |

## 2. Guest→Java lifecycle callback table (APS2-filled, `0x6d37bd0..0x6d37e08`)

The guest holds a table of `{thunk, method-name, signature}` triples in `.data.rel.ro`,
populated by the **same APS2 packed relocations** phase 2 already applies. File bytes
are zero; after relocation the slots contain pointers into the guest's strings and
code. Each entry is 24 bytes: `+0` = native thunk, `+8` = method name string,
`+0x10` = JNI signature string.

| Name (slot) | Signature | Thunk RVA | Name slot | Sig slot |
| --- | --- | --- | --- | --- |
| `onStartNative` | `(J)V` | `0x2a206d0` | `0x6d37c18` | `0x6d37c20` |
| `onResumeNative` | `(J)V` | `0x2a206f0` | `0x6d37c30` | `0x6d37c38` |
| `onSaveInstanceStateNative` | `(J)[B` | `0x2a20730` | `0x6d37c48` | `0x6d37c50` |
| `onPauseNative` | `(J)V` | `0x2a207a0` | `0x6d37c60` | `0x6d37c68` |
| `onStopNative` | `(J)V` | `0x2a207c0` | `0x6d37c78` | `0x6d37c80` |
| `onConfigurationChangedNative` | `(JLandroid/content/res/Configuration;)V` | `0x2a207e0` | `0x6d37c90` | `0x6d37c98` |
| `onTrimMemoryNative` | `(JI)V` | `0x2a20810` | `0x6d37ca8` | `0x6d37cb0` |
| `onWindowFocusChangedNative` | `(JZ)V` | `0x2a20830` | `0x6d37cc0` | `0x6d37cc8` |
| `onSurfaceCreatedNative` | `(JLandroid/view/Surface;)V` | `0x2a20850` | `0x6d37cd8` | `0x6d37ce0` |
| `onSurfaceChangedNative` | `(JLandroid/view/Surface;III)V` | `0x2a208c0` | `0x6d37cf0` | `0x6d37cf8` |
| `onSurfaceRedrawNeededNative` | `(JLandroid/view/Surface;)V` | `0x2a20a00` | `0x6d37d08` | `0x6d37d10` |
| `onSurfaceDestroyedNative` | `(J)V` | `0x2a20a30` | `0x6d37d20` | `0x6d37d28` |
| `onTouchEventNative` | `(JLandroid/view/MotionEvent;IIIIIJJIIIIIIFF)Z` | `0x2a20a80` | `0x6d37d38` | `0x6d37d40` |
| `onKeyDownNative` | `(JLandroid/view/KeyEvent;)Z` | `0x2a20b90` | `0x6d37d50` | `0x6d37d58` |
| `onKeyUpNative` | `(JLandroid/view/KeyEvent;)Z` | `0x2a20be0` | `0x6d37d68` | `0x6d37d70` |
| `onTextInputEventNative` | `(JLcom/google/androidgamesdk/gametextinput/State;)V` | `0x2a20c30` | `0x6d37d80` | `0x6d37d88` |
| `onWindowInsetsChangedNative` | `(J)V` | `0x2a20c50` | `0x6d37d98` | `0x6d37da0` |
| `setInputConnectionNative` | `(JLcom/google/androidgamesdk/gametextinput/InputConnection;)V` | `0x2a20de0` | `0x6d37db0` | `0x6d37db8` |
| `onContentRectChangedNative` | `(JIIII)V` | `0x2a20df0` | `0x6d37dc8` | `0x6d37dd0` |
| `onSoftwareKeyboardVisibilityChangedNative` | `(JZ)V` | `0x2a20e50` | `0x6d37de0` | `0x6d37de8` |
| `onEditorActionNative` | `(JI)V` | `0x2a20e70` | `0x6d37df8` | `0x6d37e00` |

The table also has two larger entries at `0x6d37bd0` (thunk `0x3dc529`, sig
`(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Landroid/content/res/AssetManager;[BLandroid/content/res/Configuration;)J`
at `0x3f8d44`) and `0x6d37be8` (thunk `0x4c0e55`, sig `()Ljava/lang/String;`
at `0x24e9ef`) — these correspond to the big init entry
`nativeAppBridgeV2InitWithParams` and a string-returning bootstrap query.

These callbacks are **guest→Java**. In a native harness (no ART/JVM) worker-2
must implement the receiving side: either intercept the cached `jmethodID`
calls and dispatch them to C handlers, or register native stand-ins for these
`*Native` methods. The thunk RVAs above are the guest-side functions that
perform the `Call*Method` through the cached IDs — useful as interception
points.

## 3. Java→native entry points for app-shell startup (all AppImage RVAs)

From `docs/interfaces/libroblox-exports.txt` (verified against the live binary):

| RVA | Export (abridged) | Role |
| --- | --- | --- |
| `0x227af5a` | `JNI_OnLoad` | caches all callbacks above; returns `JNI_VERSION_1_6` |
| `0x24650a9` | `NativeSettingsInterface_nativeSetBaseDataDirectories` | stores two path args in globals — call first |
| `0x232d484` | `NativeSettingsInterface_nativeSetRobloxVersion` | version string |
| `0x2326e2e` | `NativeSettingsInterface_nativeSetRobloxChannel` | channel string |
| `0x2479c55` | `NativeSettingsInterface_nativeSetDeviceInfo` | device info |
| `0x233762c` | `NativeGLInterface_nativeGameGlobalInit` | engine-global init |
| `0x2359277` | `NativeGLInterface_nativeInitClientSettings` | settings pipeline |
| `0x2383c18` | `NativeGLInterface_nativePostClientSettingsLoadedInitialization3` | post-settings init |
| `0x24abf96` | `NativeGLInterface_nativeAppBridgeV2InitWithParams` | **main app init** (sig in §2) |
| `0x247dc8d` | `NativeAppBridgeInterface_nativeAppBridgeAppStart` | app start |
| `0x26fe80f` | `NativeGLInterface_nativeAppBridgeV2StartAppWithParams` | start app (fromSurface path) |
| `0x1dd72a0` | `NativeGLInterface_nativeOnFragmentStart` | fragment start |
| `0x2ebf950` | `NativeGLInterface_nativeScheduleOnFirstFrame` | first-frame callback registration |
| `0x231bd7c` | `JNIAAssetManagerSetup_initNative` | asset manager setup |
| `0x2eb3ef1` … | `WebViewProtocol_*` (many) | WebView key/flag helpers |

Purchase (Java→native): `com/roblox/client/purchase/IAPPurchaseManager_*`
`0x2ed7b96..0x2ed9716` — only needed when the app shell issues a purchase;
the guest→Java side is `promptNativePurchase*` in §1.2.

## 4. Concrete handoff list for worker-2 (`src/jni/native_jni.h`)

**Required JNIEnv functions** (minimum to survive `JNI_OnLoad`):
`FindClass`, `GetStaticMethodID`, `NewGlobalRef`, `ExceptionCheck`,
`ExceptionClear`, `CallStaticVoidMethod`, `CallStaticObjectMethod`,
`CallStaticIntMethod`, `CallStaticBooleanMethod`, `DeleteLocalRef`.

**Classes that must resolve** (exact names):
`com/roblox/universalapp/logging/LoggingProtocol`,
`com/roblox/engine/jni/NativeGLJavaInterface`,
`com/roblox/engine/jni/locale/NativeLocaleJavaInterface`,
`com/roblox/engine/jni/user/NativeUserJavaInterface`,
`com/roblox/engine/jni/util/NetworkUtils`,
`com/roblox/engine/jni/memstorage/Connection`,
`com/roblox/universalapp/messagebus/Connection`,
`com/roblox/engine/jni/model/NativeTextBoxInfo`.

**Callbacks to satisfy natively** (highest priority for the app-shell screen):
`onAppShellReloadNeeded()V`, `gameLoadedCallback(J)V`,
`getDeviceStaticParams()Lcom/roblox/engine/jni/model/DeviceStaticParams;`,
`getWebViewUserAgent()Ljava/lang/String;`,
`getMobileAdvertisingId()Ljava/lang/String;`,
`showKeyboard`/`hideKeyboard`, `getRobloxLocale`/`getGameLocale`,
`getUsername`/`getDisplayName`/`getUserId`/`getMembershipType`/`getTheme`,
`promptNativePurchase*`, `getProcessTimestamp()J`.

**Lifecycle callbacks** (must be dispatchable through the §2 table):
`onStartNative`, `onResumeNative`, `onPauseNative`, `onStopNative`,
`onSurfaceCreated/Changed/DestroyedNative`, `onWindowFocusChangedNative`,
`onConfigurationChangedNative`, `onTrimMemoryNative`, `onTouchEventNative`,
`onKeyDown/UpNative`.

**Deterministic signal suggestion for worker-3:** the most app-shell-specific
guest→Java callback is `onAppShellReloadNeeded` (cached ID global
`0x72a3020`, method string `0x4a702d`). Have worker-2's JNI dispatch log each
callback invocation with its name and the guest RVA of the thunk that invoked
it, and gate `app shell rendered` on observing `onAppShellReloadNeeded` or
`gameLoadedCallback` **plus** the phase-3 render call, so the marker cannot
pass on the standalone frame alone.

## 5. Limits / open items

- Stripped binary: internal routines are unnamed; RVAs are exact, semantics
  come from cited disassembly and APS2 addends.
- A few signatures marked `?` were inferred from string ordering; verify at
  the listed string RVA if the bridge rejects them.
- No runtime proof yet that all cached globals must be non-NULL; `JNI_OnLoad`
  logs non-fatal warnings for some (LoggingProtocol), so others may tolerate
  NULL. Start with the priority list in §4 and add on failure.
- Full JNI/app shell replacement is the milestone; playable gameplay and
  `AMediaCodec`/`AImageReader` remain out of scope (decisions 010/Phase 4).
