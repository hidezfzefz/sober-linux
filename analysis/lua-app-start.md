# Lua app start → app-shell/login frame: native map and JNI/param gaps (task #468)

Owner: **luastart**. Scope: `analysis/lua-app-start.md` only. No `src/` edits, no
gate changes, no fabricated callbacks. Engine: AppImage
`/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so`, BuildID
`204ad049599c96afb75b8a21d5051063e821d6c9`, SHA-256
`3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335` — read-only
throughout.

**Evidence classes used below.** `OBSERVED` = bytes read from the frozen ELF,
decoded Dalvik in `/tmp/opencode/mini/out/smali`, or harness source; `DERIVED` =
APS2 relocation decode (`python3 /tmp/opencode/reloc.py dump|slots`) or call-graph
inference from disassembly; `UNKNOWN` = not established. No runtime execution was
performed by this worker; runtime claims are quoted from existing lead logs with
their source named. Every RVA is for the AppImage build only — do not mix with
2.745.1142 RVAs.

---

## 1. Executive answer

1. `nativeAppBridgeStartLuaAppDM` (`0x25448f6`, export size `0xf9`/249 bytes,
   `()V` static JNI) is **not** itself the app builder. It is a guard/dispatch
   entry: it constructs a stack guard with vtable `0x6dbc9a0`, calls the internal
   `0x2eb6b2a` → `0x2337cc8` game-global-init chain, then runs a guard cleanup
   through vtable slots `+0x20`/`+0x28`. The **real** Lua-app start driver is the
   adjacent internal function `0x25449fc` (vtable slot `+0x30` of `0x6dbc9a0`),
   which calls the `nativeAppBridgeAppStart` variant `0x24a8e44` with an integer
   mode `4`, and ends with the genuine app-construction writes of a delegate into
   `app+0x408` at `0x25d7e75` / `0x25f7e7e`.
2. The Java call that reaches it is `ActivityNativeMain.Q2(FeatureState)`
   (`ActivityNativeMain.smali:1403`, `:1475-1477`) — *after* `uh/f.F` (global
   init), `uh/f.k` (InitWithParams), `m3`/`showFeature`, and only when
   `rg/y.h().f().i()` is 0. The app-shell manager path `uh/f.y()` (`:1317-1351`)
   independently replays `f()/g()/v()` → `SendAppEventOnAppReady`, `k()`, `G()`.
3. **Three JNI entries are still never called by the harness on any path**:
   `nativeAppBridgeV2UpdateSurfaceAppWithPlatformParams` `0x2773194`,
   `setTaskSchedulerBackgroundMode` `0x2eba91c`, and
   `nativeRegisterJavaFlagProvider` `0x2ecbcf4` (the export; reached only
   indirectly by guest init). Four more are called **only** inside the
   `SOBER_REAL_APPSTATE_PATH` diagnostic, which the decision-013 gate
   force-withholds. See §4.
4. The ten `StartAppParams` getters have exact signatures from smali; three of
   them (`appUserId()J`, `isUnder13()Z`, `membershipType()I`) are **primitives**,
   and the current harness handler `real_path_get()` returns a `jstring` for
   `appUserId` and neutral `0` for the other two — a type/fidelity mismatch.
   Four string values in the harness are also invented ("0", "-- app starter
   --\n", "1", "SoberUser", "default", "https://www.roblox.com"); the real ones
   are in §5.
5. No genuine app-shell/login frame has been observed. Under decision 013 the
   only acceptable evidence is a **guest-driven** `gameLoadedCallback(J)V` from
   notifier `0x27eaf6e` (or a genuine `onAppShellReloadNeeded()V`) plus the
   phase-3 render RVA `0x649d422`, with every diagnostic opt-in off. §7 separates
   that from harness assertions.

---

## 2. The Java sequence that actually starts the Lua app (smali, OBSERVED)

### 2.1 `ActivityNativeMain.Q2(FeatureState)` — the entry point

`/tmp/opencode/mini/out/smali/com/roblox/client/ActivityNativeMain.smali:1403-1481`
(method header confirmed by `awk` over the file):

```
1412  invoke-static {} Luh/f;->j()Luh/f;         ; app-shell manager singleton
1416  invoke-virtual {v0,p0} Luh/f;->F(Landroid/content/Context;)V   ; nativeGameGlobalInit
1418  invoke-static {p0} Luh/p;->a(Landroid/content/Context;)Luh/f$d; ; build params
1426  invoke-virtual {v1,v0} Luh/f;->k(Luh/f$d;)V ; nativeAppBridgeV2InitWithParams
1428-1459  optional cold-start-deeplink block
1461-1469  Lrg/y;->h().f().i()Z                    ; feature/gate check
1473  if-nez v0, :cond_1                           ; skip when flag nonzero
1475  invoke-virtual {p0,p1} ...->m3(FeatureState)V; show feature (app-shell UI)
1477  invoke-static {} LNativeGLInterface;->nativeAppBridgeStartLuaAppDM()V
```

`m3` (`:3370-3402`) → `F2()` → `ph/a.g(FeatureState)` — the Java side of showing
the app-shell/login feature. `NativeGLInterface.smali:39` declares
`.method public static native nativeAppBridgeStartLuaAppDM()V`, so the JNI call is
`JNICALL(JNIEnv *env, jclass clazz)` with **no Java arguments** (DERIVED: JNI
static-native ABI).

### 2.2 `uh/f.y(Context, Surface, Activity)` — the app-shell restart

`uh/f.smali:1317-1351` (log tag `rbx.appshell` / `ASMA.restart`):

```
1326  f()V           -> g()V -> v()V  = nativeAppBridgeV2SendAppEventOnAppReady
1334/1341  Luh/p;->b|a(...)Luh/f$d    ; build params (VR variant vs normal)
1346  k(f$d)         = nativeAppBridgeV2InitWithParams   (0x24abf96)
1348  G(Surface)     = nativeAppBridgeV2StartAppWithParams (0x26fe80f)
```

`uh/f.z()` (`:1353-1364`) additionally calls
`setTaskSchedulerBackgroundMode(Z,String)` (`0x2eba91c`) — never called by the
harness.

---

## 3. `nativeAppBridgeStartLuaAppDM` @ `0x25448f6` — native anatomy

### 3.1 Export and exact entry bytes (OBSERVED)

`readelf -Ws` gives one global FUNC at `0x25448f6`, size `0xf9`. Disassembly
(`/tmp/opencode/fn_StartLuaAppDM.dis:7-62`, cross-checked with
`fn_25448f6.txt`):

| Address | Instruction | Meaning (DERIVED unless noted) |
| --- | --- | --- |
| `2544901` | `mov 0x4ce4af0(%rip),%r14 # 72293f8` | ABI/JNI singleton (also the stack canary source used across the engine) |
| `254490f-2544928` | read `0x72a3268`, validate, maybe log via `0x232de10` | same crash/log guard prologue as every other big JNI entry |
| `2544966` | `lea 0x4878033(%rip),%rax # 6dbc9a0` | vtable of the guard class |
| `254496d-2544974` | `lea -0x60(%rbp),%rbx; mov %rax,(%rbx); mov %rbx,0x20(%rbx)` | stack guard: vptr at `+0`, self-pointer at `+0x20` |
| `254497d` | `call 0x2eb6b2a` with `rdi=&guard, esi=0` | enters game-global-init (`0x2eb6b2a`: `0x2337cc8` with `rdi=0x72a31a8, rsi=guard, rdx=0`) |
| `2544982-25449a6` | compare `guard.selfptr` to `&guard`; `call *(%vtable,0x20 or 0x28)` | **cleanup dispatch**: `+0x20 = 0x1dd72a0` (bare `ret`) / `+0x28 = 0x2ec0446` (delete via `0x1dbe5be`) |

APS2 relocation decode (`reloc.py dump 0x6dbc9a0`) — the vtable is zero in the
file and filled at load:

```
0x6dbc9a0 +0x00 -> 0x1dd72a0    +0x18 -> 0x25449f0   (re-seat vptr into [rsi])
0x6dbc9a0 +0x08 -> 0x2ec0418    +0x20 -> 0x1dd72a0   (no-op dtor)
0x6dbc9a0 +0x10 -> 0x2ec042c    +0x28 -> 0x2ec0446   (deleting dtor)
0x6dbc9a0 +0x18 -> 0x25449f0    +0x30 -> 0x25449fc   (the heavy start driver)
0x6dbc9a0 +0x28 -> 0x2ec0446
```

`0x2ec042c` is the factory (allocates 0x10 bytes, stores vptr `0x6dbc9a0`).
**Caveat (honesty):** objdump labels the deep functions with offsets such as
`+0x95798` from this 249-byte export; those offsets are distance-from-symbol,
not function membership. `0x25449fc`, `0x25d7e75` and `0x25f7e7e` are distinct
internal functions reached deeper in the call graph.

### 3.2 The heavy driver `0x25449fc` (internal, OBSERVED bytes / DERIVED roles)

`fn_StartLuaAppDM.dis:82-189`:

```
2544a1a  cmpb $0, 0x72cb648     ; FFlag EnableAppBridgeComponents
2544a23  call 0x24acb12         ; build InitParams from guest globals
2544a2f  call 0x30ef840         ; flag-provider result -> virtual *0x18(...)
2544a46  call 0x24ad6fc         ; wrapper object -> virtual *0x18(...)
2544a70  call 0x2512a70         ; init/finalise
2544a7f  call 0x234e0ca         ; produces object; byte at +0x108 controls branch
2544ac3  mov $0x4,%edi ; call 0x24a8e44   <-- nativeAppBridgeAppStart(MODE=4)
2544ad0  call 0x234e0ca ; copy 16 bytes ; call 0x262892c (nativeDestroy)
2544aba..2544b55  release shared_ptr control blocks (*0x10 refcount)
```

Mode `4` is the **Lua-app** app-start variant (DERIVED from the call site; the
Java `nativeAppBridgeAppStart(String,String,Z,String,String,String)` export is
the *different* `0x247dc8d`, which the harness calls instead).

### 3.3 Where `app+0x408` (delegate) is really installed (OBSERVED)

The two writes claimed in `appstate_fields.md` are confirmed byte-for-byte:

```
25d7e5f  call 0x1dd55f2            ; genuine guest helper
25d7e75  mov %rax,0x408(%rbx)      ; install delegate into app+0x408
25d7e7c  mov %r14b,0x410(%rbx) ; mov %r14,0x438(%rbx); mov %r14b,0x440(%rbx)
...
25f7e72  call 0x25f7ff2            ; genuine guest helper
25f7e7e  mov %rax,0x408(%rbx)      ; install delegate into app+0x408
25f7e8c  call 0x1dc1208 ; 25f7e98 call 0x24bb532
```

`rbx` is a ~`0x458`-byte app object. Both values come from the guest's own
helpers — **no fake vtable is required**. `app+0x440` (user controller) remains
the FFlag-gated install at `0x24ad50a` documented in
`analysis/appstate_fields.md §3` (flag `EnableAppBridgeComponents`,
`0x72cb648`, normally set from Java via `nativePreloadFlagOverrides`).

---

## 4. Missing JNI entry points / RVAs (the harness gap list)

All RVAs are AppImage-build exports unless marked "internal". "Current" column
reflects `src/linux/guest_app_shell.c` read at 2026-11-20 (lead owns `src/`;
unchanged by this worker).

| # | Java caller | Native entry / RVA | Current harness | Gap |
| - | - | - | - | - |
| 1 | `MainGameActivity.b2` | `nativePreloadFlagOverrides(String)` `0x2edd859` | only in `real_appstate_prepare()` under `SOBER_REAL_APPSTATE_PATH` (`:647-649`) | Not on the default path; our synthetic `jstring` is not a real Java String the guest JSON parser accepts (lead PROGRESS Phase 7) |
| 2 | flag application | `nativeRetryInit()` `0x2edd739` | same opt-in (`:650`) | same |
| 3 | `uh/f.f()→g()→v()` | `nativeAppBridgeV2SendAppEventOnAppReady(String,String,String,String)` `0x2ebcf36` | same opt-in (`:678-685`) | Not on default path; 4th arg must be `Game`/`Chat`/etc., never `""` |
| 4 | `uh/f.F(Context)` | `nativeGameGlobalInit()` `0x233762c` | always (`:1082`) | OK (but guest may re-enter via `0x2eb6b2a`/`0x2337cc8`) |
| 5 | `uh/f.k(InitParams)` | `nativeAppBridgeV2InitWithParams(InitParams)` `0x24abf96` | always (`:1089`) with synthetic object | InitParams `baseURL/userAgent/platformParams/deviceParams/isTablet/pioneerPlaceId/buildVariant/isVrDevice/isPotato/vrContext` not faithfully backed |
| 6 | AppBridge | `nativeAppBridgeAppStart(6 args)` `0x247dc8d` | always on non-ALT path (`:1133-1136`) | This is **not** the mode-4 variant `0x24a8e44` that the StartLuaAppDM driver uses (§3.2) |
| 7 | `uh/f.G(Surface)` | `nativeAppBridgeV2StartAppWithParams(StartAppParams)` `0x26fe80f` | opt-in `SOBER_START_APP` or `real_path` (`:1150-1177`) | Ten getters incompletely/wrongly backed (§5) |
| 8 | `ActivityNativeMain.Q2` | **`nativeAppBridgeStartLuaAppDM()` `0x25448f6`** | opt-in `real_path` only (`:688-693`, called at `:1130`) | **Missing on default path**; this is the entry that reaches the delegate installs |
| 9 | `uh/f.I(Surface,FII)` | **`nativeAppBridgeV2UpdateSurfaceAppWithPlatformParams` `0x2773194`** | **NEVER called** | Needed for a real shell surface; its own getter path reads `PlatformParams` (`dpiScale`, `viewportWidthMm`, `viewportHeightMm`) |
| 10 | `uh/f.z()` | **`setTaskSchedulerBackgroundMode(Z,String)` `0x2eba91c`** | **NEVER called** | Scheduler mode reset that precedes shell display on Java |
| 11 | flag bridge | **`nativeRegisterJavaFlagProvider` `0x2ecbcf4`** (internal call site `0x2ed3aa4`) | never called as JNI entry | Guest reaches it only through the global-string seed path; relevant to FFlag `0x72cb648` becoming 1 |
| 12 | app singleton | `nativePostClientSettingsLoadedInitialization3` `0x2383c18` | opt-in `SOBER_REAL_APPSTATE_POSTSETTINGS` only (`:670-677`) | synthetic `java/util/List` faults at `0x2e5d42a` (lead PROGRESS) |

Also already-called plumbing (not gaps): `nativeSetBaseDataDirectories`,
`nativeSetRobloxVersion`, `nativeSetRobloxChannel` `0x2326e2e`,
`nativeScheduleOnFirstFrame` `0x2ebf950`, `nativeInitializeNativeFlags`
`0x2465210` (opt-in).

---

## 5. `StartAppParams`: exact signatures, real values, harness values

Getter signatures are OBSERVED from
`com/roblox/engine/jni/autovalue/StartAppParams.smali:34-63`. The builder and the
real values are OBSERVED from `uh/f.smali:363-434` and `uh/p.smali:21-135`.
`Ldk/c` defaults are OBSERVED from `dk/c.smali:49-83,167-277`; theme enum values
from `ml/a.smali:77-114,220-228`.

| Getter (exact JNI sig) | Built from (`uh/f$d` field) | Real value in decoded APK | Guest/user default (OBSERVED) | Current harness value / mismatch |
| --- | --- | --- | --- | --- |
| `surface()Landroid/view/Surface;` | method arg `p2` | actual Android `Surface` from the activity | n/a | `start_app_get`/`real_path_get` returns synthetic 16-byte `android/view/Surface` — **plumbing only** |
| `platformParams()Lcom/roblox/engine/jni/model/PlatformParams;` | `d.b` | `new ok/a(p.f())` with `isLuaHomePageEnabled=true`, `isLuaGamesPageEnabled=true`, `isLuaChatEnabled=true`, `isTablet=rg/a1.r0()` (`uh/p.smali:40-61,70`) | `rg/a1.r0()` | synthetic 0x100-byte object; no real fields (`dpiScale`/viewport) |
| `appStarterPlace()Ljava/lang/String;` | `d.d` | **`"rbxasset://places/Mobile.rbxl"`** (`uh/p.smali:74-76`) | constant | harness returns **`"0"`** (`:704-705`) — wrong |
| `appStarterScript()Ljava/lang/String;` | `d.e` | **`"LuaAppStarterScript"`** (`uh/p.smali:78-80`) | constant | harness returns **`"-- app starter --\n"`** (`:706-707`) — wrong |
| `appUserId()J` (**long**, not String) | `d.f` (`Ldk/c.c().j()`) | `Ldk/c.a` (userId field) | **`0xffffffffffffffff` (-1)** until login (`dk/c.smali:65-68,242-248`) | harness returns a **jstring `"1"`** (`:708-709`) — **wrong JNI type** |
| `username()Ljava/lang/String;` | `d.h` (`Ldk/c.c().k()`) | `Ldk/c.d` | **`""`** (empty; null or literal `"null"` → `""`, `dk/c.smali:250-277`) | harness returns **`"SoberUser"`** (`:710-711`) — wrong |
| `membershipType()I` (**int**) | `d.i` (`Ldk/c.c().d()`) | `Ldk/c.h` | **0** (default int field, `dk/c.smali:33,167-173`) | harness returns neutral `0` (`:719-721`) — value OK, type is int |
| `isUnder13()Z` (**boolean**) | `d.g` (`Ldk/c.c().n()`) | `Ldk/c.f` | **true** (`dk/c.smali:70-73,306-312`) | harness returns neutral `0` (`:719-721`) — plausible for guest but not the decoded default |
| `selectedTheme()Ljava/lang/String;` | `d.j` (`Ldk/c.c().i()`) | `Ldk/c.g().toString()` → `ml/a.a` field | **`"Light"`** (default `ml/a.c`, `dk/c.smali:52-55`; `ml/a.smali:84-90,220-228`; other values `"Dark"`, `"Classic"`) | harness returns **`"default"`** (`:712-713`) — wrong |
| `vrContext()Landroid/app/Activity;` | `d.a` | set only when `rg/a1.E0()` is true (`uh/f.smali:419-428`) | `rg/a1.E0()` **returns false** in this build (`rg/a1.smali:391-397`) → getter must yield `null` | harness returns a synthetic `android/app/Activity` (`:167-171`) — wrong on this build |

**Builder validity constraint (OBSERVED, `AutoValue_StartAppParams$Builder.smali:52+`):**
`build()` requires `set$0 == 0x7` AND non-null `surface`, `platformParams`,
`appStarterPlace`, `appStarterScript`, `username`, `selectedTheme`. Bits are set
by `setAppUserId(J)` = `0x1`, `setIsUnder13(Z)` = `0x2`,
`setMembershipType(I)` = `0x4`. `vrContext` is nullable. Since the guest never
sees the Java builder (it calls the native accessor methods directly through
JNI), this matters mainly as a specification of which getters are mandatory and
what a faithful synthetic object must return: **all three primitives must be
callable and return the correct JNI type**.

**AutoValue getter discovery (OBSERVED/inference boundary):** the strings
`appUserId`, `selectedTheme`, `appStarterPlace`, `appStarterScript` do **not**
appear anywhere in the engine bytes; only `membershipType` (`0x2a647b`),
`isUnder13` (`0x2346ae`, `0x4ecca8`), `platformParams` (`0x217cb8`), `vrContext`
(`0x2346a4`), `username` and `surface` do, and `0x26fe8de/0x26fe8fd` reference
the latter two in `StartAppWithParams`. So the guest reads most getters through a
generic AutoValue/reflection-style dispatch (helper `0x232dbbc` → `GetMethodID`
→ `CallObjectMethod` family, cf. `appstate_fields.md §1`), keyed by the name our
JNI bridge registers. Registering a handler by name is therefore necessary but
**not sufficient** — the returned handle's JNI type must match the getter
signature or the guest misinterprets it.

---

## 6. Exact opt-in order to reach a genuine app-shell frame (target sequence)

This is the **real Java order** reconstructed from §2 and `appstate_fields.md`;
it is the order an opt-in replay must follow. Each step is a real guest entry
point; no guest bytes are patched and no callback is fabricated.

1. **`nativePreloadFlagOverrides` `0x2edd859`** with a genuine JSON `jstring`
   `{"EnableAppBridgeComponents":true}` (grammar from `appstate_fields.md`
   addendum A), then **`nativeRetryInit` `0x2edd739`** to apply it — both
   **before** `nativeGameGlobalInit`. Verify flag `0x72cb648 == 1`.
2. **`nativePostClientSettingsLoadedInitialization3` `0x2383c18`** with a real
   `List` containing real flag-provider objects, so app singleton `0x72cb5e0`
   is populated (otherwise the flag=1 path faults at `0x2ec1042`).
   **Ordering constraint (stability, #470):** the real FFlag-ctor replay
   `0x2e62390→0x2e618b0→0x2e61280→0x2e5bc10→0x2e7b850` must run **before** any
   postsettings replay, else the FFlag registry handle `0x72a0520` has a NULL
   control block and faults at `0x2e7b8e2`. Existing wiring bug:
   `guest_app_shell.c:858` (`SOBER_JSON_APP_SINGLETON`) currently precedes
   `:875` (`SOBER_REPLAY_FFLAG_CTOR`); lead must reorder.
3. **`setTaskSchedulerBackgroundMode` `0x2eba91c`** (`false, "ASMA.start"`).
4. **`nativeGameGlobalInit` `0x233762c`**.
5. **`nativeAppBridgeV2SendAppEventOnAppReady` `0x2ebcf36`** with
   `("AppShellNotifications","<decimal id>","StartConversationWithUserId","Chat")`
   (4th arg must be one of the recognized tokens, `appstate_fields.md` addendum B).
6. **`nativeAppBridgeV2InitWithParams` `0x24abf96`** with a faithful `InitParams`
   (all ten getters, correct types).
7. **`nativeAppBridgeAppStart` `0x247dc8d`** (six-arg Java export) and/or the
   internal mode-4 variant `0x24a8e44`.
8. **`nativeAppBridgeV2StartAppWithParams` `0x26fe80f`** with all ten
   `StartAppParams` getters backed by correctly typed values from §5. A faithful
   object now exists from **params, #469**: `src/jni/startapp_params.c/.h`,
   opt-in `SOBER_STARTAPP_PARAMS=1`, API `void *sober_startapp_params_make(SoberJni*,
   void *guest_base)`; use it *instead of* the sparse object + `start_app_get` /
   `real_path_get`. It uses the genuine guest vptr `base+0x6dbca20` and the
   `0x26feb9b` parser layout; lead still needs to add the file to the `sober_jni`
   target in `src/jni/CMakeLists.txt` permanently.
9. **`nativeAppBridgeStartLuaAppDM` `0x25448f6`** (`env, clazz`) — reaches
   `0x2eb6b2a`/`0x2337cc8`, the mode-4 driver `0x25449fc`, and the delegate
   installs at `0x25d7e75`/`0x25f7e7e`.
10. **`nativeAppBridgeV2UpdateSurfaceAppWithPlatformParams` `0x2773194`** with a
    real `PlatformParams` carrying `dpiScale` / `viewportWidthMm` /
    `viewportHeightMm`.
11. Observe `app+0x408` and `app+0x440` via `SOBER_APP_SLOT_DIAG` /
    `log_app_slots`; then wait for the guest to drive `SingleSurfaceApp::userDidLogin`
    (`0x2771f6c`, vtable slot `0x6e01df8`) and the `gameLoadedCallback` notifier
    (`0x27eaf6e`, vtable slot `0x6dbd860`).

**Status: UNKNOWN / not achieved.** Every one of steps 1, 2, 3, 9, 10 is missing
from the default path; steps 1, 2, 5, 9 exist only under diagnostic opt-ins that
force-withhold the marker (`guest_app_shell.c:1230-1251`). Lead logs
(PROGRESS Phase 8/9) show that even with the full opt-in stack the path reaches a
genuine but **rejected** `onAppBridgeNotification(String,String)` callback
(cached ID `0x72a3028`) and never `gameLoadedCallback` / `onAppShellReloadNeeded`.

---

## 7. Guest-driven frame evidence vs harness assertions (decision 013)

### Counts as guest-driven (would satisfy the strict gate)
- A `CallStaticVoidMethodV` to `gameLoadedCallback(J)V` issued from the guest's
  own notifier `0x27eaf6e` (loads cached class `0x72a2f48`, method `0x72a2fc0`,
  calls thunk `0x25848e0` with AL=0, `RCX`=jlong; `callback/callback-map.md §2`),
  **plus** phase-3 guest render RVA `0x649d422`.
- A genuine `onAppShellReloadNeeded()V` invocation (cached ID `0x72a3020`).
  **Not achievable from this native-only harness** (shellcb, #467,
  `analysis/jni/appshell-reload-trigger.md`): the only static reference to
  `0x72a3020` is the JNI_OnLoad cache write at `0x227b432`; no native reader, no
  second `GetStaticMethodID`, no APS2 addend for name string `0x4a702d`, and no
  Java caller exist in the decoded APK. It needs ART/Activity/Looper/Surface
  semantics. Therefore the only reachable decision-013 callback is
  `gameLoadedCallback(J)V`.
- Guest-native state that proves the frame: non-NULL `app+0x408` / `app+0x440`
  written by the guest's own code at the RVAs in §3.3 / `appstate_fields.md §3`,
  and the guest's own `SingleSurfaceApp::userDidLogin` reaching completion.

### Does NOT count (harness assertions / diagnostics)
- `SOBER_ALT_CALLBACK_PATH`: `guest_app_shell.c:1205-1217` calls thunk
  `0x25848e0` **directly** with the cached class/method and a jlong — a genuine
  guest thunk but **harness-triggered**. Decision 037 itself says no frame is
  proven; the gate prints `HONESTY NOTICE` (`:1274-1279`). Must stay opt-in.
- The `emit_marker("app shell rendered")` call at `guest_app_shell.c:1280` is the
  harness's own stdout/marker file — it is an assertion gated on `shell_cb &&
  render_ok`, **not** a guest frame. With any of the 17 `seeded_state` env vars
  (`:1230-1246`) the gate forces `shell_cb = 0`. Verifier (#471) found a residual
  coverage gap: 13 non-chain guest-mutating opt-ins plus the new
  `SOBER_APP_SINGLETON_BRIDGE` / `SOBER_STARTAPP_PARAMS` are **not** in the
  withhold list — they must be added before a genuine callback could be trusted.
- Accessor callbacks `surface()`, `platformParams()`, `vrContext()` and the
  `onAppBridgeNotification(String,String)` observed under the full opt-in stack
  are JNI plumbing, explicitly rejected by the lead's phase-8/9 ruling
  (`docs/decisions/013-no-weak-app-shell-marker.md:56-64`).
- `app shell rendered` under `SOBER_APP_SHELL_DIAGNOSTIC` is a default-OFF
  diagnostic and prints `DIAGNOSTIC ONLY`: no marker.

### Net finding
No guest-driven login/app-shell callback has been observed in any recorded run.
The honest current state is `callback=0 render=1` on the default path
(`decision-memo-alt-thunk-vs-013.md §2.2`) and a rejected
`onAppBridgeNotification` under the full opt-in stack. Marker remains withheld.

---

## 8. Recommended next analysis (no code changes by this worker)

1. Reproduce the full order of §6 with **real** JNI jobjects (the guest treats
   autovalue jobjects as native C++ collections — the root cause already found
   in PROGRESS Phase 7). Highest-value single gap: typed `StartAppParams`
   getters (§5), then `0x2773194`/`0x2eba91c`.
2. Trace the virtual dispatch that invokes `0x25449fc` (slot `+0x30` of
   `0x6dbc9a0`) from the `0x2eb6b2a`/`0x2337cc8` chain, to confirm whether the
   real delegate writes (§3.3) execute on the JNI `StartLuaAppDM` path or only
   via the mode-4 driver.
3. Find the virtual caller of the `gameLoadedCallback` notifier vtable slot
   `0x6dbd860` (only indirect callers exist in static disassembly) and identify
   the guest state/frame event that triggers it. This is the decisive
   decision-013 evidence source. gameload (#466,
   `analysis/game-loaded-path.md`) adds that the notifier is registered as a
   global at `0x7234520` from `0x24ad699`; start there.
4. `onAppShellReloadNeeded` is ruled out for a native-only harness (shellcb,
   #467) — do not spend further effort there.

---

## 9. Evidence index

- Engine: `/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so` (BuildID/SHA above); disassembly `/tmp/opencode/libroblox.dis`, slices `fn_StartLuaAppDM.dis`, `fn_25448f6.txt`, `fn_StartAppWithParams.dis`, `fn_24abf96.txt`, `fn_26fe80f.txt`, `fn_2773194.txt`; `bash /tmp/opencode/dis.sh <rva>`; `python3 /tmp/opencode/reloc.py dump|slots`.
- Smali: `/tmp/opencode/mini/out/smali/uh/f.smali`, `uh/f$d.smali`, `uh/p.smali`, `dk/c.smali`, `ml/a.smali`, `com/roblox/client/ActivityNativeMain.smali`, `com/roblox/engine/jni/NativeGLInterface.smali`, `com/roblox/engine/jni/autovalue/StartAppParams.smali`, `AutoValue_StartAppParams$Builder.smali`, `InitParams.smali`, `rg/a1.smali`.
- Prior analysis (read only): `analysis/appstate_fields.md`, `analysis/callback/callback-map.md`, `analysis/decision-memo-alt-thunk-vs-013.md`, `analysis/jni/jni-map.md`, `PROGRESS.md`, `docs/decisions/013-no-weak-app-shell-marker.md`, plus teammate reports `analysis/game-loaded-path.md` (#466), `analysis/jni/appshell-reload-trigger.md` (#467), `analysis/stability-fault-audit.md` (#470), `tests/verification-report-471.md` (#471).
- Harness (read only): `src/linux/guest_app_shell.c`, `src/jni/native_jni.c`.
