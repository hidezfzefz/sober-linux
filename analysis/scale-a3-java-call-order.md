# scale-A3 — Real Java app-shell call order around `nativeAppBridgeV2InitWithParams` / `StartGameWithParam`

Frozen engine: `/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so`
BuildID `204ad049599c96afb75b8a21d5051063e821d6c9`,
SHA256 `3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335`
(re-verified with `sha256sum` before this analysis). Smali: `/tmp/opencode/mini/out/smali`.
Target bytes/strings are treated as untrusted DATA. Every RVA below was re-verified
against the frozen ELF with `nm -D`/`readelf -Ws` (all `T` symbols); `objdump -d -M intel`
was used for the native call sites. Nothing here changes `src/`, `tests/`, CMake,
docs, or the frozen image; no call is inferred without a decoded smali invoke or a
native call instruction.

---

## 1. Executive answer

* The real Java order is: **flag/InitParams seeding (`nativeAppBridgeSetInitParams`) →
  `uh/f.F()` global init (`nativeGameGlobalInit`, `nativeUpdateAdapterInit`) →
  `uh/f.k()` `nativeAppBridgeV2InitWithParams` → `uh/f.G()` `setTaskSchedulerBackgroundMode`
  + `nativeAppBridgeV2StartAppWithParams` → `ActivityNativeMain.Q2` `nativeAppBridgeStartLuaAppDM`
  → (game join) `ExperienceSession` `nativeGameGlobalInit` + `uh/j0.C()`
  `nativeAppBridgeV2StartGameWithParam` → `nativeAppBridgeV2SendAppEventOnGameLoaded`.**
  (§2, exact smali file:line.)
* The id=1 node is registered exactly once, inside `nativeAppBridgeV2InitWithParams` at
  native `0x24ad699: lea rsi,[0x7234520]; mov edi,1; call 0x253cb56`. `0x253cb56` spends
  its one-shot guard `0x72a31ba` at entry and then immediately calls the MessageBus
  service getter `0x24a8e44(4)` (`0x253cb9d`); if that lazy construction faults, the guard
  is spent and no node is inserted (F1, task #500/#507).
* **JNI export that primes the bus before `0x24abf96`:** the only genuine Java→native
  export whose own call chain *directly* reaches `0x24a8e44(4)` is
  **`nativeAppBridgeAppStart` = `0x247dc8d`** (call site `0x24a8daf`, `mov edi,0x4` at
  `0x24a8daa`, inside helper `0x24a8d8c`; reached from `NativeAppBridgeInterface.a`
  `NativeAppBridgeInterface.smali:32` ← `zj/a$a.smali:86` ← `yj/k0.P()`
  `yj/k0.smali:1930`). The harness calls it only **after** `0x24abf96`
  (`guest_app_shell.c:1521`), so it never primes the bus in time. **Caveat:** prior
  analysis (decision C) records that this same export re-enters the broken `0x233319a`
  object path; do not assume it is a clean prime.
* `nativeAppBridgeStartLuaAppDM` = `0x25448f6` (the path already tried and reported
  unstable) reaches `0x24a8e44(4)` only **indirectly**: the 249-byte export body
  (`0x25448f6..0x25449ef`, verified) does **not** call it; it builds a stack guard with
  vtable `0x6dbc9a0`, calls `0x2eb6b2a`, then dispatches vtable cleanup `+0x20/+0x28`.
  The heavy driver `0x25449fc` is vtable slot `+0x30` of `0x6dbc9a0` and is invoked
  deeper in the chain (`0x2544ac8`, `0x25451af` → `0x24a8e44(4)`). That vtable/indirect
  dependency is the concrete reason this pre-init is state-sensitive/unstable.
* The genuinely clean prime is the **internal** `((uintptr_t(*)(int))(base+0x24a8e44))(4)`
  — not a JNI export — documented in `analysis/callback/task507-preinit-ordering.md` §2.
  No JNI export in the decoded APK reaches `0x24a8e44(4)` both *directly* and *before*
  `0x24abf96` on the harness path.
* Separately and genuinely omitted by the harness: `nativeAppBridgeSetInitParams`
  (`0x2edcf18`, Java `MainGameActivity.smali:1025`) — a real early Java call that seeds
  native InitParams state before the app-shell manager init. It does **not** call the bus
  getter (its calls are `0x24ac8b8`, `0x24adfae`, `0x24ac975`, `0x24ac9d4`, `0x24acb12`,
  `0x2512a70`, `0x253e462`, `0x2e57de0`, …), so adding it helps "app state" but not the
  MessageBus registration window.

---

## 2. Real Java call order (decoded smali, OBSERVED)

### 2.1 App startup / state seeding — before the app-shell manager

| # | Smali (file:line) | Java native method | Export RVA | Args |
|---|---|---|---|---|
| 1 | `com/roblox/client/startup/MainGameActivity.smali:1025` → `MainGameActivity$Companion.smali:189` | `nativeAppBridgeSetInitParams` (decl `MainGameActivity.smali:708`) | `0x2edcf18` | `(InitParams)` jobject |
| 2 | flag setup, `yj/k0.smali:1930` → `NativeAppBridgeInterface.smali:32` (`a()` wrapper) / `zj/a$a.smali:86` | `nativeAppBridgeAppStart` (decl `NativeAppBridgeInterface.smali:37`) | `0x247dc8d` | 6 args `(String,String,boolean,String,String,String)` — harness passes url/ua/false/androidid/platform/empty; Java passes `rg/a1.t`, `rg/a1.E()`, `r0()`, androidId, launchSource |

`yj/k0.P()` (method at `yj/k0.smali:1770`) contains the `nativeAppBridgeAppStart`
call at `:1930`, guarded by `"Flags are not loaded when starting AppBridge"`; it is
reached from `yj/k0.Q()` at `:2040`, called from `ActivityNativeMain.q3()` at
`ActivityNativeMain.smali:6235` and from `ActivityNativeMain.M()` (`:1292`).
Also in the flag path: `nativePreloadFlagOverrides` `0x2edd859` and `nativeRetryInit`
`0x2edd739` (declared/called via `MainGameActivity`).

### 2.2 App-shell manager (`uh/f`, tag `rbx.appshell` / `ASMA`)

`ActivityNativeMain.Q2(FeatureState)` (`ActivityNativeMain.smali:1402`, tag
`loadDataModel()`) is the entry:

| # | Smali (file:line) | Java call → native | Export RVA | Args |
|---|---|---|---|---|
| 1 | `ActivityNativeMain.smali:1412` | `uh/f.j()` singleton (`uh/f.smali:130-138`) | — | — |
| 2 | `ActivityNativeMain.smali:1416` → `uh/f.F(Context)` `uh/f.smali:247-344` | `nativeGameGlobalInit` (`uh/f.smali:325`) | `0x233762c` | `(env, clazz)` |
| 2b | same method, `uh/f.smali:336` | `nativeUpdateAdapterInit` | `0x234df63` | `(env, clazz)` — **omitted by harness** |
| 3 | `ActivityNativeMain.smali:1418` | `uh/p.a(Context)` builds `uh/f$d` params | — | — |
| 4 | `ActivityNativeMain.smali:1426` → `uh/f.k(f$d)` `uh/f.smali:621-853` | `h()` (`uh/f.smali:682` → `NativeGLJavaInterface.setImplementation`, `:232`) then **`nativeAppBridgeV2InitWithParams`** (`uh/f.smali:840`) | `0x24abf96` | `(InitParams)` jobject; here `0x24ad699` registers the id=1 node |
| 5 | `uh/f.G(Surface)` `uh/f.smali:346-454` → `z()` (`uh/f.smali:357,1353-1364`) | `setTaskSchedulerBackgroundMode(0, "ASMA.start")` (`uh/f.smali:1362`) | `0x2eba91c` | `(boolean,String)` — **omitted by harness on default path** |
| 6 | same method, `uh/f.smali:434` | `nativeAppBridgeV2StartAppWithParams` | `0x26fe80f` | `(StartAppParams)` jobject |
| 7 | `ActivityNativeMain.smali:1477` (after `m3()`/`showFeature` at `:1475`, gated by `rg/y.h().f().i()==0` at `:1461-1473`) | `nativeAppBridgeStartLuaAppDM` (decl `NativeGLInterface.smali:39`) | `0x25448f6` | `(env, clazz)` |

Other `uh/f` native calls (not on the cold-start path above): `nativeAppBridgeV2SendAppEventOnAppReady`
`0x2ebcf36` at `uh/f.smali:1228`; `nativeAppBridgeV2SendAppEventOnGameLoaded` `0x2ebca9e`
at `uh/f.smali:179`; `nativeAppBridgeV2PauseApp` `0x2ebc823` at `:477`;
`nativeAppBridgeV2DestroyApp` `0x2ebc99f` at `:592`; `setTaskSchedulerBackgroundMode`
again at `:1148`; `nativeHandleBackPressed` `0x2eb6749` at `:1068`.
`uh/f.y()` (`uh/f.smali:1317-1351`, `ASMA.restart`) replays `f()/g()/v()` →
`k()` (InitWithParams) at `:1346` and `G()` (StartApp) at `:1348`.

### 2.3 Game-join path — `StartGameWithParam`

| # | Smali (file:line) | Java call → native | Export RVA | Args |
|---|---|---|---|---|
| 1 | `com/roblox/client/game/ExperienceSession.smali:2131` (one-shot static `x`, `:2120-2131`) | `nativeGameGlobalInit` | `0x233762c` | `(env, clazz)` |
| 2 | `ExperienceSession.smali:~2140` | `new uh/j0` then `j0.A(null,null,null)` | — | — |
| 3 | `uh/j0.smali:360-600`, method `C(m0)`, native call at `uh/j0.smali:581` | **`nativeAppBridgeV2StartGameWithParam`** (decl `NativeGLInterface.smali:75`) | `0x2ebd51a` | `(StartGameParams)` jobject; builder at `:399-577` sets surface/platformParams/placeId/userId/accessCode/…; returns `I` |
| 4 | `uh/f.smali:179`, `uh/f.A(mk/c)` | `nativeAppBridgeV2SendAppEventOnGameLoaded` | `0x2ebca9e` | `(String,String,String)` — driven after manager setup |

Other `uh/j0` native calls: `nativeAppBridgeV2PauseGame` `0x2ebeb2c` (`:708`),
`UpdateSurfaceGameWithPlatformParams` `0x2ebeeaa` (`:862`),
`ResumeGameWithPlatformParams` `0x2ebeca8` (`:884`), `LeaveGame` `0x2ebf0ac` (`:1241`).

### 2.4 Ordering summary (single cold-start → join)

```
MainGameActivity.setInitParamsForEngine   -> nativeAppBridgeSetInitParams 0x2edcf18   [harness OMITS]
flags: nativePreloadFlagOverrides 0x2edd859 / nativeRetryInit 0x2edd739              [opt-in only]
yj/k0.P()/Q() (q3/M)                       -> nativeAppBridgeAppStart 0x247dc8d        [harness calls AFTER InitWithParams]
ActivityNativeMain.Q2:
  uh/f.F()                                 -> nativeGameGlobalInit 0x233762c
                                           -> nativeUpdateAdapterInit 0x234df63       [harness OMITS]
  uh/f.k()                                 -> nativeAppBridgeV2InitWithParams 0x24abf96
                                                -> 0x24ad699: 0x253cb56(1,&0x7234520)  <-- F1 window
  uh/f.G()/z()                             -> setTaskSchedulerBackgroundMode 0x2eba91c
                                           -> nativeAppBridgeV2StartAppWithParams 0x26fe80f
  ActivityNativeMain.Q2                    -> nativeAppBridgeStartLuaAppDM 0x25448f6
ExperienceSession (join):
  nativeGameGlobalInit 0x233762c ; uh/j0.C() -> nativeAppBridgeV2StartGameWithParam 0x2ebd51a
  uh/f.A()                                 -> nativeAppBridgeV2SendAppEventOnGameLoaded 0x2ebca9e
```

Relative order of `ActivityNativeMain.q3()`/`M()` (appStart) vs `Q2()` (InitWithParams)
is a function of the Android lifecycle; both are decoded above, but the smali snippets
alone do not fix which fires first on this build. That is stated as a boundary, not
asserted as fact.

---

## 3. Java-native → frozen export RVA map (all verified `nm -D --defined-only`)

| Native method (decl file:line) | RVA | Role / note |
|---|---|---|
| `nativeGameGlobalInit` (`NativeGLInterface.smali:99`) | `0x233762c` | engine-global init |
| `nativeUpdateAdapterInit` (`:199`) | `0x234df63` | `uh/f.F()`; harness omits |
| `nativePostClientSettingsLoadedInitialization3` (`:156`) | `0x2383c18` | app singleton; opt-in |
| `nativeAppBridgeAppStart` (`NativeAppBridgeInterface.smali:37`) | `0x247dc8d` | **reaches bus getter `0x24a8e44(4)` at `0x24a8daf`** |
| `nativeAppBridgeV2InitWithParams` (`NativeGLInterface.smali:45`) | `0x24abf96` | main app init; registers id=1 at `0x24ad699` |
| `nativeAppBridgeSetInitParams` (`MainGameActivity.smali:708`) | `0x2edcf18` | early InitParams seed; harness omits |
| `nativeAppBridgeStartLuaAppDM` (`NativeGLInterface.smali:39`) | `0x25448f6` | indirect bus getter via vtable `0x6dbc9a0+0x30` |
| `nativeAppBridgeV2StartAppWithParams` (`:72`) | `0x26fe80f` | app-shell start |
| `nativeAppBridgeV2UpdateSurfaceAppWithPlatformParams` (`:78`) | `0x2773194` | surface update; never on default path |
| `setTaskSchedulerBackgroundMode` (`:241`) | `0x2eba91c` | scheduler reset |
| `nativeAppBridgeV2SendAppEventOnGameLoaded` (`:69`) | `0x2ebca9e` | game-loaded event |
| `nativeAppBridgeV2SendAppEventOnAppReady` (`:66`) | `0x2ebcf36` | app-ready event; opt-in |
| `nativeAppBridgeV2StartGameWithParam` (`:75`) | `0x2ebd51a` | game join; placeId cached to `0x72a31c0` |
| `nativeHandleBackPressed` (`:105`) | `0x2eb6749` | back key |
| `nativeRegisterJavaFlagProvider` (flags JNI) | `0x2ecbcf4` | flag bridge; never called as JNI entry |
| `nativePreloadFlagOverrides` / `nativeRetryInit` (`MainGameActivity`) | `0x2edd859` / `0x2edd739` | flag store/apply; opt-in |

Internal (not exports): `0x24a8e44` MessageBus service getter (`int id`; 4 → default
singleton `0x72cbdd0`, 3 → `0x72cc278`); `0x253cb56` id=1 registration helper;
`0x25449fc` heavy Lua-app start driver; `0x2a0d6c0` guard init; `0x30f9aae` bus ctor.

---

## 4. The MessageBus readiness window (why a pre-init is needed)

Verified bytes of the frozen image:

* `0x24ad699` (inside `0x24abf96`): `lea rsi,[rip+0x4d86e80] # 0x7234520`;
  `mov edi,0x1`; `call 0x253cb56` — the *only* call to the id=1 registration helper.
* `0x253cb7c..0x253cb91`: `test edi,edi`; `cmp BYTE PTR [0x72a31ba],0x0`;
  `jne …` (spent → silent early exit); `mov BYTE PTR [0x72a31ba],0x1` — guard spent
  ~13 bytes into the function, before the descriptor is built.
* `0x253cb9d`: `call 0x24a8e44` with `edi=4` — first potentially-failing step after the
  guard; the singleton (guards `0x72cc270`, `0x72cc718`) is lazily constructed here.
* `0x253d069`: second `0x24a8e44(4)` for the descriptor; `0x253e126` inserts the node
  into bus `+0x288`.
* `0x24af21e` (also inside `0x24abf96`, gated by byte `0x74d7438`): another
  `mov edi,0x4; call 0x24a8e44` — but this occurs **after** `0x24ad699`, too late to
  protect the one-shot window.

Consequence: prime `0x24a8e44(4)` before entering `0x24abf96` and both calls inside
`0x253cb56` become idempotent getters; the descriptor and node then build normally.
The harness's `SOBER_MESSAGEBUS_PREINIT` uses `0x25448f6` for this
(`guest_app_shell.c:1454-1467`), which is the unstable indirect path (§1).

---

## 5. Answer to the omitted-export question — concise

1. **Best direct JNI export: `nativeAppBridgeAppStart` `0x247dc8d`**, Java
   `NativeAppBridgeInterface.smali:32` (`zj/a$a.smali:86`, `yj/k0.smali:1930`),
   `(String,String,boolean,String,String,String)`. It reaches
   `0x24a8e44(4)` at `0x24a8daf` with `mov edi,0x4` (`0x24a8daa`) inside helper
   `0x24a8d8c`. The harness calls it only after `0x24abf96` (`guest_app_shell.c:1521`);
   calling it before would prime the bus, but decision C records a re-entry into the
   broken `0x233319a` object path — so treat as a *candidate*, not a proven clean fix.
2. **Already-tried export: `nativeAppBridgeStartLuaAppDM` `0x25448f6`**, Java
   `ActivityNativeMain.smali:1477` / `NativeGLInterface.smali:39`, `(env, clazz)`.
   Its 249-byte body does not call `0x24a8e44`; the bus getter is reached indirectly
   via vtable `0x6dbc9a0+0x30 = 0x25449fc` → `0x2544ac8`/`0x25451af`. This is why the
   harness's pre-init via this export was unstable.
3. **Cleanest prime is not a JNI export:** internal `0x24a8e44(4)` — see
   `analysis/callback/task507-preinit-ordering.md` §2/§4. It needs no JNIEnv/jobject,
   is idempotent, and is safe once the image is mapped/relocated.
4. **Genuinely omitted early Java export: `nativeAppBridgeSetInitParams` `0x2edcf18`**
   (`MainGameActivity.smali:1025`) seeds native InitParams before `0x24abf96` but does
   not touch the bus getter — useful for "app state", not for the F1 registration window.
5. Other harness omissions before `0x24abf96` (app-state only, no bus getter):
   `nativeUpdateAdapterInit 0x234df63` (`uh/f.smali:336`), `setTaskSchedulerBackgroundMode
   0x2eba91c` (`uh/f.smali:1362`), `nativeAppBridgeV2SendAppEventOnAppReady 0x2ebcf36`
   (`uh/f.smali:1228`), `nativeRegisterJavaFlagProvider 0x2ecbcf4`, and the opt-in
   `0x2edd859`/`0x2edd739`/`0x2383c18` flag/singleton path.

No fabricated calls are reported: every RVA above is a verified `T` export or a cited
internal call instruction, and every Java call has a decoded smali `invoke`.

---

## 6. Evidence index

* Frozen ELF: `nm -D --defined-only`, `readelf -Ws`, `objdump -d -M intel` slices
  `0x25448f6..0x25449ef`, `0x24ad680..0x24ad6bf`, `0x24a8d80..0x24a8e44`,
  `0x24af1f0..0x24af240`, `0x2edcf18..0x2edd709` (all from the SHA above).
* Smali: `com/roblox/engine/jni/NativeGLInterface.smali`,
  `com/roblox/engine/jni/NativeAppBridgeInterface.smali`,
  `com/roblox/client/ActivityNativeMain.smali`,
  `com/roblox/client/startup/MainGameActivity.smali` + `MainGameActivity$Companion.smali`,
  `com/roblox/client/game/ExperienceSession.smali`,
  `uh/f.smali`, `uh/j0.smali`, `zj/a$a.smali`, `yj/k0.smali`.
* Prior analysis (read only): `analysis/callback/task507-preinit-ordering.md`,
  `analysis/lua-app-start.md` (§2, §4, §6), `analysis/appstate_fields.md`,
  `analysis/jni/jni-map.md` §3, `analysis/game-loaded-path.md`,
  `analysis/callback/task494-genuine-leg.md`, `analysis/wave4-messagebus-preinit.md`.
* Harness (read only): `src/linux/guest_app_shell.c` lines 706-753, 1446-1523, 1590-1650.
