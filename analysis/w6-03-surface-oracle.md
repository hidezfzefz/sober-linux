# W6-03 — Surface lifecycle ground truth via emulator oracle (task #538)

**Agent:** worker-3 (Convio team `TEAM-C2YA-ZTU9-RMAV`), member `mem_7ce12c90aab5c9628170`.
**Scope:** this file only. No `src/`, `CMakeLists.txt`, `tests/`, gate, marker, or another
agent's note/source was edited. No guest bytes were patched, no synthetic/fake callbacks,
no host callbacks, no gate weakening. **Marker remains WITHHELD (decisions 013/038).**

---

## 0. Provenance and role of the oracle

| Item | Value |
| --- | --- |
| Authoritative APK | `/home/user/.local/share/basalt/linux/payloads/3170-204ad049599c96afb75b8a21d5051063e821d6c9/sober_apk/base.apk` |
| base.apk SHA-256 | `114a6f8ed6f0f974cf632a9cbec2f5cd45e1d85d7ed251db4f3920140bc6ead4` (re-verified this task) |
| Split (native, x86_64) | `split_config.x86_64.apk`, SHA-256 `f42ace3b9428b30b4c06d28330937a2470cc13e7321130a5d27a4e29538b9356` |
| DEX files | `classes.dex` `9057172`, `classes2.dex` `6145160`, `classes3.dex` `6983328` bytes (extracted read-only) |
| Frozen engine | `/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so`, SHA-256 `3e498d64…b741335`, BuildID `204ad049…` (re-verified) |
| Emulator | Android SDK emulator 37.2.12.0, AVD `W3SurfaceOracle` (isolated copy on port **5556**; worker-4 owned 5554) |
| Guest | `sdk_gphone64_x86_64`, API 37 (`android-37.0/google_apis_playstore/x86_64`), KVM on, swiftshader GLES |
| Install | `adb install-multiple -r -g base.apk split_config.x86_64.apk` → `Success`; package `com.roblox.client`, version 2.740.931 |

The `~/Downloads/basalt-arm64-2.740.931.apk` copy was **never** used. The emulator is a
**research-only oracle**; nothing here proposes it as the final runtime, and no observation
below is a synthetic event or a seeded state. Target-derived strings/data are treated as
untrusted data.

Raw evidence retained outside the repo:
`/tmp/opencode/surface-oracle/w3-logcat-boot.log` (23,654 lines),
`w3-logcat-gameactivity.log`, `emulator.log`, `w3-emulator.log`,
`dex/gameactivity_dump.txt` (`dexdump -d classes2.dex`, 1,225,654 lines).

---

## 1. Method

1. Booted isolated AVD, `adb logcat -v threadtime -b all` cleared, then launched the
   authoritative app through its exported launcher alias
   (`monkey -p com.roblox.client -c android.intent.category.LAUNCHER 1` →
   `com.roblox.client/.startup.LauncherAliasMain`) and captured 75 s.
2. Correlated framework surface tags (`surfaceview_callback`, `surfaceview_layout`,
   `Surface`, `viewroot_draw_event`) with the engine's own FLog lines emitted by the
   frozen DEX/native bridge (`[FLog::JNIAppBridge] …`, `[FLog::SingleSurfaceApp] …`).
3. Independently decoded the authoritative DEX (`dexdump -d`) to get the exact Java
   callback → native-method wiring, and the frozen ELF dynsym/disassembly to map each
   native method to a guest RVA.
4. Attempted a second run launching the AGDK `MainGameActivity` directly; the production
   build refuses it (`Permission Denial … not exported`; `adb root`/`run-as` also refused),
   so the AGDK path is documented from authoritative DEX + engine bytes, not runtime.

---

## 2. OBSERVED runtime ground truth — default (authoritative) configuration

### 2.1 Decisive configuration finding

The default boot resolves to **`com.roblox.client.ActivityNativeMain` + `RBXSurfaceView`**,
**not** to AGDK `GameActivity`:

```
15:44:05.003 I rbx.JNIRobloxSettings: nativeInitializeNativeFlags: ... 51: EnableGameActivity10 not found.
15:44:05.003 I NativeFlagsInitResult: Using initial value: false for EnableGameActivity10
15:44:04.978 I NativeFlagsInitResult: Using initial value: false for PTFDisableSurfaceViewRequestFocus
```

Authoritative DEX confirms the two activities are alternatives:
`com.roblox.client.startup.MainGameActivity` has
`Superclass = Lcom/google/androidgamesdk/GameActivity;`, while `ActivityNativeMain` is a
plain activity whose `RBXSurfaceView` supplies the surface. The launcher aliases both
target `ActivitySplash`, which selects the activity from flag `EnableGameActivity10`
(registered as Java flag `sh/a.G2` at DEX `0x3168fe`–`0x316910`, default `false`).
`MainGameActivity` is declared but **not exported**, so it cannot be launched directly in
this production build.

**Consequence:** in the authoritative default build, the *live* surface→native bridge is
`com.roblox.engine.jni.NativeGLInterface` / `NativeAppBridgeInterface` (JNIAppBridge),
**not** the AGDK `onSurface*Native(J,Surface)` thunks at `0x2a20850/0x2a208c0/0x2a20a00/
0x2a20a30` that the current harness drives. This is a genuinely observed fact, not an
inference.

### 2.2 Exact observed sequence (primary process, pid 6196, main thread tid 6196)

All times `11-21 15:44:xx` from `w3-logcat-boot.log`; "tid 6311" is the guest render/
LuaApp thread. Format `w=1080 h=2400`, surface format `4`.

| # | Time | Event (observed log tag/line) | Native/engine effect (paired FLog line) |
| --- | --- | --- | --- |
| 1 | 05.571 | `ActivityNativeMain.onCreate` | — |
| 2 | 05.679 | `createGlAppsFrame() apps frame created` | — |
| 3 | 05.832 | Activity startup | `nativeAppBridgeAppStart:` |
| 4 | 05.852 | `loadDataModel()` | `nativeAppBridgeV2Init:` |
| 5 | 05.858–05.901 | `initializeSingleton`, `setStage(Native)` | `nativeAppBridgeStartLuaAppDM: Calling initializeLuaAppWithLoggedInUser` |
| 6 | 08.236 | `RBXSurfaceView.onSizeChanged(): 0x0 → 1080x2400` | view lays out the surface |
| 7 | 08.243 | `surfaceview_layout: [SV[110820432 ActivityNativeMain],4,1080,2400,-2,layout,1,0,1]` | — |
| 8 | 08.257 | `Surface : Creating surface … SurfaceView[…ActivityNativeMain]#2(BLAST Consumer)2` | real `Surface` created by SurfaceFlinger |
| 9 | **08.257** | **`surfaceview_callback: …,surfaceCreated`** | `MainScreenController.surfaceCreated`, `AppShellFragment.surfaceCreated` → **`AppShellFragment: Start the lua app`** |
| 10 | 08.259 | (immediately after #9, same main thread) | **`nativeAppBridgeV2StartApp:`** → `SingleSurfaceApp.startLuaApp`, `returnToLuaApp` → `Vulkan: swapchain images 3 present mode 2 format 37 size 1080x2400` (08.663) |
| 11 | 08.430–11.262 | Lua/engine init; `setStage: InitializedLuaApp` → `LuaApp` | — |
| 12 | **11.276** | **`surfaceview_callback: …,surfaceChanged -- format=4 w=1080 h=2400`** | `MainScreenController.surfaceChanged`, `AppShellFragment.surfaceChanged` |
| 13 | 11.276 | (same millisecond, main thread) | **`nativeAppBridgeV2UpdateSurfaceAppWithPlatformParams:`** → `SingleSurfaceApp.updateSurfaceLuaApp` |
| 14 | **11.279** | **`surfaceview_callback: …,surfaceRedrawNeeded`** | `viewroot_draw_event: reportDrawFinished seqId=1` at 11.607 |
| 15 | 29.626 | `surfaceview_layout: …,layout,1,0,0` → **`surfaceDestroyed`** | `MainScreenController.surfaceDestroyed`, `AppShellFragment.surfaceDestroyed` |
| 16 | 29.627 | (same millisecond) | **`nativeAppBridgeV2PauseApp:`** → `pauseLuaAppAndDestroyIfNeeded destroySurfaceView:true` |
| 17 | 32.910 | ANativeWindow teardown | **`nativeAppBridgeV2PauseApp: Destroying ANativeWindow at 0x7d329543d180`** |
| 18 | 33.624 | Surface recreated (`#6`) → **`surfaceCreated`** again | `Start the lua app` → **`nativeAppBridgeV2StartApp:`** (33.625), `startLuaApp(LuaApp)` |
| 19 | 38.205 | **`surfaceChanged`** again | **`nativeAppBridgeV2UpdateSurfaceAppWithPlatformParams:`** (38.206), `updateSurfaceLuaApp` |
| 20 | 38.206 | **`surfaceRedrawNeeded`** again | `reportDrawFinished seqId=2` (38.301), new Vulkan swapchain (39.479) |

### 2.3 The canonical lifecycle order (OBSERVED, twice in this run)

```
Surface created (SurfaceFlinger)
  → SurfaceHolder.surfaceCreated
      → AppShellFragment.surfaceCreated → "Start the lua app"
          → nativeAppBridgeV2StartAppWithParams        [first surface bind]
  → SurfaceHolder.surfaceChanged(format=4, w, h)
      → AppShellFragment.surfaceChanged
          → nativeAppBridgeV2UpdateSurfaceAppWithPlatformParams (Surface, PlatformParams)
  → SurfaceHolder.surfaceRedrawNeeded
      → (native redraw already armed by the update call; ViewRoot reportDrawFinished follows)
  … frames …
  → SurfaceHolder.surfaceDestroyed
      → nativeAppBridgeV2PauseApp → "Destroying ANativeWindow at 0x…" → (later) DestroyApp
```

Key ordering facts for native reproduction:
* `surfaceCreated` **precedes** `StartAppWithParams`; `surfaceChanged` **precedes** the
  surface-update native call; `surfaceRedrawNeeded` follows `surfaceChanged` on the same
  main thread within ~3 ms and is the last callback before `reportDrawFinished`.
* The surface-update JNI call is made on the **main thread (tid 6196)**, while the engine
  consumes it on a separate guest thread (tid 6311). A faithful native reproduction must
  deliver the update on the UI/bridge thread and let the engine thread dispatch the frame.
* The same window is **not** torn down until `surfaceDestroyed`/`PauseApp`; destroying the
  window before the redraw would break the redraw guard (cf. P6 §5.8).
* `Created ANativeWindow` (H6 format string `0x3c163c`) **does not appear anywhere** in
  the oracle capture (`grep -c = 0`), while `Destroying ANativeWindow` does. This is the
  live confirmation of H6 §2.4: the guest's level-5 gate (`0x72a3268 == 0x0406`, high
  byte 4 < 5) suppresses that text in the default build. Any harness that emits it must
  declare a seeded log-level change; it is not free ground truth.

---

## 3. Authoritative DEX ground truth (Java side)

From `classes2.dex` (`dexdump -d`, class `Lcom/google/androidgamesdk/GameActivity;`,
DEX offsets in parentheses):

| Java callback (bytecode) | Native method invoked | DEX off |
| --- | --- | --- |
| `surfaceCreated(SurfaceHolder)V` | `onSurfaceCreatedNative(JLandroid/view/Surface;)V` | `0x15f6a0`, call at `0x15f6ce` |
| `surfaceChanged(SurfaceHolder;III)V` | `onSurfaceChangedNative(JLandroid/view/Surface;III)V` | `0x15f660`, call at `0x15f696` |
| `surfaceRedrawNeeded(SurfaceHolder)V` | `onSurfaceRedrawNeededNative(JLandroid/view/Surface;)V` | `0x15f708`, call at `0x15f736` |
| `surfaceDestroyed(SurfaceHolder)V` | `onSurfaceDestroyedNative(J)V` (no Surface arg) | `0x15f6d8`, call at `0x15f700` |

Every one of the four begins with `invoke-virtual H1()`
(`()Z`, DEX `0x15ecd4`) and returns immediately when it is false; `H1()` is
`return GameActivity.O == this` — i.e. callbacks only fire on the registered singleton
activity. The `J` first argument is `sget-wide P` (field `0x0aad`), the nativeData handle.
`surfaceDestroyed` is the only one that clears the stored `SurfaceHolder` field `.I`
before calling native. All four native methods are `PUBLIC NATIVE` (no bytecode).
This is the AGDK route compiled into the engine and described in `P5_surface_callbacks.md`;
it is dormant in the default build (§2.1).

The **live** route (observed in §2) is declared in
`Lcom/roblox/engine/jni/NativeGLInterface;` (`classes2.dex`, all `PUBLIC STATIC NATIVE`):

* `nativeAppBridgeAppStart(String,String,boolean,String,String,String)V`
  (via `NativeAppBridgeInterface.a`, DEX `0x1dd000`)
* `nativeAppBridgeV2InitWithParams(InitParams)V`
* `nativeAppBridgeStartLuaAppDM()V`
* `nativeAppBridgeV2StartAppWithParams(StartAppParams)V`
* `nativeAppBridgeV2UpdateSurfaceAppWithPlatformParams(Surface,PlatformParams)V`
  — declared at DEX `0x1dd31?` (method #22 of `NativeGLInterface`); **the only Java
  caller** is `uh.e.H(Surface,float,int,int)V` at DEX `0x335ca0`, guarded by instance
  boolean `uh.e.d`; it logs `rbx.appshell / ASMA.updateWindowHandle` and builds a
  `PlatformParams{dpiScale, viewportWidthMm, viewportHeightMm}` before the native call
  (`invoke-static … method@3b30` at `0x335cdc`).
* `nativeAppBridgeV2PauseApp()V`, `nativeAppBridgeV2DestroyApp()V`
* `nativeAppBridgeV2SendAppEventOnGameLoaded(String,String,String)V`,
  `nativeAppBridgeV2StartGameWithParam(StartGameParams)I` (notifier/callback chain).

---

## 4. Frozen-engine native entry points (dynsym of the SHA-matched `libroblox.so`)

| RVA (dynsym value) | Size | JNI symbol (LIBROBLOX) | Role in observed sequence |
| --- | --- | --- | --- |
| `0x247dc8d` | 1674 | `NativeAppBridgeInterface_nativeAppBridgeAppStart__…` | #3 |
| `0x24abf96` | 2337 | `NativeGLInterface_nativeAppBridgeV2InitWithParams` | #4 (id=1 MessageBus reg at `0x24ad6a5`) |
| `0x25448f6` | 249 | `NativeGLInterface_nativeAppBridgeStartLuaAppDM` | #5 |
| `0x26fe80f` | 587 | `NativeGLInterface_nativeAppBridgeV2StartAppWithParams` | #10/#18 — calls bind helper `0x26fea5a` |
| `0x2773194` | 448 | `NativeGLInterface_nativeAppBridgeV2UpdateSurfaceAppWithPlatformParams` | #13/#19 — calls `0x26fea5a` then `0x26feb9b`, builds a params object and runs a cleanup loop (§4) |
| `0x2ebc823` | 380 | `NativeGLInterface_nativeAppBridgeV2PauseApp` | #16/#17 — `Destroying ANativeWindow` |
| `0x2ebc99f` | 255 | `NativeGLInterface_nativeAppBridgeV2DestroyApp` | teardown |
| `0x2ebca9e` | 1103 | `NativeGLInterface_nativeAppBridgeV2SendAppEventOnGameLoaded` | callback leg |
| `0x2ebd51a` | 5298 | `NativeGLInterface_nativeAppBridgeV2StartGameWithParam` | callback leg |

Bind helper internals (OBSERVED, H6 + fresh disassembly):
`0x26fea5a` → `ANativeWindow_fromSurface@plt 0x6d31ad0` (guest RA `0x26fea7d`),
release old window `0x72293f8`, `incl 0x72a3310` (generation), store new window at
`0x72a3308`, optional `[FLog::JNIAppBridge] … Created ANativeWindow {win} (ID:{gen})`
via emitter `0x232de10` (true sink RA `0x232e080`). `0x2773194` calls this helper at
`0x277322a`, reloads `[0x72a3308]` at `0x2773232`, builds a 0x40-byte params object with
vptr `0x6dbcb20`, and executes the indirect-call cleanup loop at `0x27732db..0x2773301`
(`call *(%rcx,%rax,1)`, slots `+0x20/+0x28`).

**Correction (from worker-16, post-publication):** the vtable slots of `0x6dbcb20` were
decoded against the frozen engine's APS2 relocations: `+0x20 → 0x1dd72a0` (bare `ret`),
`+0x28 → 0x2773728` (trivial destructor), `+0x08 → 0x2ec0584` (destructor),
`+0x18 → 0x2ec0598` (copy constructor). The `0x27732db..0x2773301` sequence is therefore
a **cleanup loop over the object's embedded list, not an event publish**. No slot resolves
to `0x27eaf04`/`0x27eaf4c`/`0x27eaf6e`/`0x6dbbf98`/`0x6dbd860`. An earlier version of
this report floated this call as a candidate MessageBus event-1 producer; that hypothesis
is **disproven** and must not be used. (worker-16, `analysis/w6-16-event1-producer.md §8`.)

AGDK route (dormant in default build, from P5/P6 and authoritative DEX):
`0x2a20850` created, `0x2a208c0` changed, `0x2a20a00` redraw (guards `rdx!=0`,
`[rdx+0x140]!=0`, `[rdx+0x98]!=0`, tail `jmp *%rax` at `0x2a20a20`),
`0x2a20a30` destroyed; callbacks installed by guest glue at `0x2a23001` into
`nativeData+0x88/+0x90/+0x98/+0xa0`.

---

## 5. Exact sequence for native reproduction (no synthetic evidence)

This is the order a clean native route would have to reproduce. Every item is an
*observed/authoritative* event or RVA; whether the harness can supply genuine inputs is a
separate question (§6).

1. **Bridge init:** `nativeAppBridgeAppStart` `0x247dc8d` →
   `nativeAppBridgeV2InitWithParams` `0x24abf96` →
   `nativeAppBridgeStartLuaAppDM` `0x25448f6` (start Lua app, `SingleSurfaceApp` init).
2. **Real Surface exists** (SurfaceFlinger-created, `w/h` from the view), then
   `surfaceCreated` → `StartAppWithParams` `0x26fe80f`:
   bind helper `0x26fea5a` performs `ANativeWindow_fromSurface` (RA `0x26fea7d`),
   releases any old window, increments `0x72a3310`, stores the new window at
   `0x72a3308`. This is what makes the redraw guard `[nativeData+0x140] != 0`
   satisfiable with *genuine* provenance.
3. **`surfaceChanged(format,w,h)`** → `UpdateSurfaceAppWithPlatformParams` `0x2773194`:
   re-bind via `0x26fea5a`, build `PlatformParams`, then run the object cleanup loop at
   `0x27732db..0x2773301` (destructor/`ret` slots — **not** an event publish, §4). The
   call arrives on the bridge/main thread; the render dispatch is a separate engine-loop
   action (step 4), not this vtable call.
4. **`surfaceRedrawNeeded`** → engine render loop consumes the armed update on the guest
   thread, reaching the draw dispatcher `0x6496d0c` and swap `0x649d422`
   (`call [vtable+0x18]`, GOT `0x722a1a8`); genuine guest RA at the GOT is the
   discriminator. Destroyed must **not** precede this.
5. **`surfaceDestroyed`/PauseApp** `0x2ebc823`: `Destroying ANativeWindow at 0x…`
   (observed), window released/zeroed; `DestroyApp` `0x2ebc99f` at final teardown.
6. If the AGDK variant is selected instead (flag `EnableGameActivity10=true`, which the
   oracle shows is **false** here), the four Java callbacks in §3 map to thunks
   `0x2a20850 → 0x2a208c0 → 0x2a20a00` (created→changed→redraw), with destroyed
   `0x2a20a30` deferred to teardown; the harness must let `initializeNativeCode`
   (`0x2a1fe30/0x2a1fe80`) install real guest callbacks into `+0x88/+0x90/+0x98/+0xa0`.

---

## 6. Honesty, gate, and handoff

* **No clean path was demonstrated by this oracle.** The emulator is a *research*
  oracle: it shows what genuine Android delivers, not that the native harness can
  deliver it cleanly. Supplying a token Surface, a host-shaped `ANativeWindow`, host-written
  `0x72a3308`, or a host pointer in a guest callback slot remains seeded state (decisions
  013/038; P5 §6, P6 §6, H6 §2.4). `seeded_state` must stay in the withhold chain.
* The oracle **corroborates** Wave-5's terminal finding (State.md §23): real
  surface→native delivery requires a real `Surface`→`ANativeWindow`, the JNIAppBridge
  lifecycle, and the guest render thread. A stock native JVM cannot supply those.
* `Created ANativeWindow` is genuinely absent in the default build; the harness must not
  manufacture it to satisfy C10. `Destroying ANativeWindow` is present and can be used as
  a *corroborating* teardown observation only.
* **Handoff — worker-1 / worker-16 (notifier, event-1) — RESOLVED NEGATIVE:** my initial
  candidate that the `0x2773194` → vptr `0x6dbcb20` → `0x2773301` call publishes MessageBus
  event 1 was **disproven** by worker-16's APS2 decode (§4): those slots are `ret`/
  destructors, and event 1 has no DEX/native publisher at this site. Event 1 remains a
  native virtual dispatch via node+`0x08` (`0x27eaf04`), with only host re-drive reaching
  it (worker-16 `analysis/w6-16-event1-producer.md`). Do not cite my earlier handoff as a
  positive producer.
* **Handoff — worker-2 (provenance):** the default build's surface JNI class is
  `NativeGLInterface`, not AGDK `GameActivity`; a provenance gate keyed only on
  `onSurfaceRedrawNeededNative` RVAs would key on a dormant path.
* **Handoff — lead:** no source changes made; this is analysis-only. Marker stays
  WITHHELD and `tests/app-shell.sh` stays RED.

---

## 7. Limitations / residual unknowns

* `adb root` and `run-as` are unavailable (production build); the AGDK `MainGameActivity`
  is not exported and could not be launched, so §3/§4's AGDK path is DEX/ELF-derived, not
  runtime-observed in this build. Its default-off status **is** runtime-observed.
* No method-level JNI entry trace (Frida/JDWP) was available; native method invocation is
  established by the engine's own FLog lines paired with dynsym RVAs and the DEX call
  sites, not by instrumenting `art`.
* The vptr `0x6dbcb20` slots were zero in the file but are APS2-decoded by worker-16
  (§4) as cleanup (`ret`/destructors); the `0x2773301` call is not a provenance-bearing
  event dispatch. No runtime pointer trace was needed for that disproof.
* The two observed process instances are explained by `killDueToPackageUpdate` (pid 5281)
  and a post-capture `FATAL EXCEPTION` (pid 6196 at 15:45:31); the canonical sequence is
  from the stable pid 6196 window and was observed **twice**.

---

## 8. Addendum — AGDK enablement attempt (research-only config seed, NEGATIVE)

After worker-15 identified the live external-override path, I retried the oracle with
`/data/local/tmp/ClientAppSettings.json` written by `adb push`. **This is a configuration
seed inside the research-only emulator, never a final-runtime or harness change.**

* `{"EnableGameActivity10":"true"}` (string value) — **no effect**:
  `Using initial value: false for EnableGameActivity10`, `ActivityNativeMain` launched.
* `{"EnableGameActivity10":true,"FFlagEnableGameActivity10":true}` (boolean values) —
  the override **was applied**: `nativeInitializeNativeFlags: ... 51: EnableGameActivity10 = true`,
  `Saved FFlagEnableGameActivity10 = true to SharedPreference` (pid 4291,
  `w3-logcat-flagoverride2.log`). Yet `ActivitySplash.w2(): launchMainActivity` still created
  **`com.roblox.client/.ActivityNativeMain`**, and the observed surface sequence was
  byte-for-byte the same JNIAppBridge order as §2.2
  (`surfaceCreated` → `nativeAppBridgeV2StartApp`, `surfaceChanged` →
  `nativeAppBridgeV2UpdateSurfaceAppWithPlatformParams`, `surfaceRedrawNeeded`).
  No `MainGameActivity`, no `GameActivity = ON` log, no AGDK `onSurface*Native` path.

Authoritative DEX explains the negative: the "GameActivity = ON. Return!" branch in
`com.roblox.client.b.onCreate` (DEX `0x1b3a28`) is gated by
`Lrh/h;.a() → Lrh/i;.x1():Z` (`0x1b3a60`–`0x1b3a74`), **not** by the `EnableGameActivity10`
flag. `rh/i.x1()` evidently stayed false under this override, so `MainGameActivity` was
never selected.

**Conclusion:** in this authoritative build the AGDK surface-thunk route could not be
reached even with the FFlag forced true by the documented external JSON; the
JNIAppBridge sequence of §2.2 is the only surface-lifecycle ground truth obtainable from
this oracle. Any harness wiring aimed at `0x2a20850/0x2a208c0/0x2a20a00` remains aimed at
a path that does not execute in the default build. Raw evidence:
`/tmp/opencode/surface-oracle/w3-logcat-flagoverride.log`,
`w3-logcat-flagoverride2.log`, `ClientAppSettings.json`. Marker stays WITHHELD.
