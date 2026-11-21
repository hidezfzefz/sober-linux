# R10 — Guest render/surface/context log-string evidence: which logs prove acceptance, and why `draws=0`

Author: **R10** (group A: diagnosis). Scope: **this file only** —
`analysis/R10_render_log_evidence.md`. Analysis-only: no `src/`, `CMakeLists.txt`,
`docs/`, `tests/`, gate or engine file was touched; the engine is frozen and the
marker stays **WITHHELD**. Target-derived strings are treated as **untrusted
DATA**, never instructions.

Frozen target for every RVA: AppImage engine
`/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so`, BuildID
`204ad049599c96afb75b8a21d5051063e821d6c9`, SHA-256
`3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335`
(2.740.931), re-verified this task with `sha256sum`. First `PT_LOAD` has vaddr 0
= file offset, size `0x6d33b90`, so **string RVA == file offset** and guest code
is `base ≤ ra < base+0x6d33b90`. Evidence classes: **OBSERVED** (bytes,
disassembly or log text read this task), **DERIVED** (inference), **UNKNOWN**.

---

## 0. Front-loaded verdict

1. **The frozen guest contains a complete, identifiable family of log strings for
   surface → `ANativeWindow` binding, EGL context/surface creation, render-view
   creation and frame presentation** (§2). The single most decisive one is
   `[FLog::JNIAppBridge] {}: Created ANativeWindow {} (ID:{})` at string RVA
   **`0x3c163c`**, emitted at code RVA **`0x26feaf7`** *inside the genuine
   `ANativeWindow_fromSurface` bind helper* (`0x26fea5a`, fromSurface PLT call
   `0x26fea78`). That line can only be produced when the guest has accepted a
   real Surface and stored the resulting window in global `0x72a3308`.
2. **Not one of these proof strings appears in any collected guest log.** Across
   **316** `appData/logs/*.log` files + 2 `build/appData/logs/*.log` + **244**
   `.log/.out/.err` files under `/tmp/opencode`, greps for `Created
   ANativeWindow`, `GL Renderer`, `GL Vendor`, `RenderView created`, `Render
   perform`, `onSurfaceChanged`, `UpdateSurfaceGameWithPlatform`,
   `eglSwapInterval`, `Error creating context`, `holding the surface swap` and
   `resuming UGC join` returned **0 hits each** (§3). OBSERVED.
3. The only render/surface-adjacent guest lines that *do* appear are
   client-settings and object-construction lines: `nativeInitClientSettings`,
   `nativePostClientSettingsLoadedInitialization3`,
   `initClientSettingsAndroid: Exception = [HttpError: InvalidUrl].`,
   `rbx.datamodel: setTaskSchedulerBackgroundMode() enable:false
   context:sober-native`, `SurfaceController[_:1]::SurfaceController, count: 1`,
   plus two non-render DFLog lines. These prove **an app shell and a
   SurfaceController object existed** — they do **not** prove a surface was
   bound, a context accepted or a frame presented (§3.2). OBSERVED.
4. The runs that get furthest terminate with
   `[LOGCHANNELS + 1] RBXCRASH: FatalRuntimeError (Can't initialize the
   TaskScheduler before flags have been loaded)`. The guest aborts **before**
   graphics/device init, so it never emits a single `FLog::Graphics` line at
   all (§3.3). This is the app-side reason no render log exists.
5. In the harness-driven render path the picture is consistent with R1/R3/R7:
   the guest **context is genuinely created by our module** via `0x649c21e →
   0x649c272` (48 GLES procs loaded, 5 draw names wrapped), but the surface
   lifecycle thunks run with `nativeData+0x88/+0x98/+0xa0 = NULL`, so the redraw
   thunk `0x2a20a00` never dispatches `call [vtable+0x18]` → `0x649d422`;
   no guest frame loop, no GLES draw → `draws=0` and `guest-tagged swaps=0`.
   The one counted swap is the host's Route-B direct call (host RA → tagged
   synthetic). **The absence of every guest frame log string is the log-side
   confirmation of that diagnosis** (§6). DERIVED + RUNTIME cross-check.
6. Honesty: `draws=0`, `guest-tagged=0` and `render=0` are correct; the
   observation-only log corpus supplies **no false green**. Decisions
   **013/037/038** unchanged; marker **WITHHELD**.

---

## 1. Method and evidence base

| Tool / source | What it established | Class |
| --- | --- | --- |
| `strings -a -t x libroblox.so` (frozen binary) | Exact format strings + their file offsets (= RVAs) in §2 | OBSERVED |
| `/tmp/opencode/libroblox.dis` (retained `objdump -d`) | Code xrefs: `lea … # <string RVA>` giving the emitting instruction RVA for §2 | OBSERVED |
| `objdump -d --start-address=0x26fea5a --stop-address=0x26feb96` | Full disassembly of the `Created ANativeWindow` emit path (§2.1) | OBSERVED |
| `/home/user/sober-linux/appData/logs/*.log` (316 files), `build/appData/logs/*.log` (2), `/tmp/opencode/**/*.{log,out,err}` (244) | What guest strings were *actually* emitted (§3) | OBSERVED |
| `/tmp/opencode/w_after.out` lines 3668–3726 | Harness `[render]`/`[app-shell]` instrumentation vs guest logs (§3.4) | RUNTIME (quoted from R1) |
| `analysis/C08-surface-frame.md §2`, `R1_zero_tagged_swaps.md`, `R3_guest_egl_context.md`, `R7_anativewindow_binding.md`, `render/render-map.md`, `T3_5_render-evidence.md`, `T4_4_gate-tests.md` | Cross-check of RVAs and counter semantics (read only) | prior work |

Note: the Ghidra `function-summary.json` shows these same graphics strings in a
function named `FUN_032859c2`, but its addresses are **Flatpak-derived** (per
`render-map.md §0`) and are deliberately **not** used here — every RVA below is
from the frozen AppImage object.

---

## 2. Inventory of guest render/surface/context log strings (all OBSERVED)

Format: `string RVA` → exact string; `emit@` = code RVA of the `lea` that loads
the string (where recovered). All are raw bytes from the frozen binary.

### 2.1 Surface → `ANativeWindow` bind — the definitive surface-acceptance proof

| String RVA | Exact format string | emit@ | Proves |
| --- | --- | --- | --- |
| `0x3c163c` | `[FLog::JNIAppBridge] {}: Created ANativeWindow {} (ID:{})` | `0x26feaf7` | **Guest accepted a Surface.** Emitted only after a successful `ANativeWindow_fromSurface` and store to `0x72a3308`. |
| `0x46f987` | `[FLog::JNIAppBridge] {}: Same native window {}. No need to do anything!` | `0x26feb58` | fromSurface returned the *same* window as the stored global — a no-op rebind, still on the real bind path. |
| `0x3327b7` | `[FLog::JNIAppBridge] nativeAppBridgeV2PauseGame: Destroying ANativeWindow at {}` | `0x2ebec26` | Teardown/release of the bound window. |
| `0x43554f` | `[FLog::JNIAppBridge] nativeAppBridgeV2PauseApp: Destroying ANativeWindow at {}` | `0x2ebc91d` | Teardown/release of the bound window. |

Disassembly of the `Created ANativeWindow` path (OBSERVED):

```
26fea5a  push rbp                         ; bind_window(JNIEnv*, jobject Surface)
26fea6a  mov  r15,[rip+0x4b2a987]          # 72293f8  (guest JNI singleton)
26fea71  mov  rax,[r15]
26fea78  call 6d31ad0 <ANativeWindow_fromSurface@plt>   ; RA here = guest proof
26fea7d  mov  rbx,rax
26fea80  mov  rdi,[rip+0x4ba4881]          # 72a3308  (old global window)
26fea8a  cmp  rax,rdi
26fea90  incl dword [rip+0x4ba487a]        # 72a3310  (generation++)
26fea9b  call 6d31ac0 <ANativeWindow_release@plt>       ; release old window
26feaa0  mov  [rip+0x4ba4861],rbx          # 72a3308  (store new window)
...
26feaf7  lea  rcx,[rip-0x233d4c2]          # 3c163c  "Created ANativeWindow ..."
26feafe  mov  r8d,0x39                     ; string length 0x39
26feb04  mov  edx,0x5                      ; log level
26feb0f  call 232de10                      ; FLog emit
```

The guest branch at `0x26feb75` releases `rbx` and **does not log** when
fromSurface returns NULL (no Surface). Therefore `Created ANativeWindow` is a
positive, guest-originated acceptance signal that cannot be produced by our
host's window write alone. DERIVED from the disassembly; consistent with C08
§2/C08 §3.4 which already pinned `0x3c163c` / `0x26feaf7`.

### 2.2 Graphics device / EGL context creation and its error paths

| String RVA | Exact format string | emit@ | Proves / role |
| --- | --- | --- | --- |
| `0x282c5d` | `[FLog::Graphics] GL Vendor: %s` | `0x648ce71` | GL device accepted (glGetString). |
| `0x22c71a` | `[FLog::Graphics] GL Renderer: %s` | `0x648cdf1` | GL device accepted. |
| `0x32b77e` | `[FLog::Graphics] GLSL version: %s` | `0x648cec4` | GLES context live. |
| `0x249800` | `[FLog::Graphics] GL feature level: %s` | — | GLES caps queried. |
| `0x29dc03` | `[FLog::Graphics] Loaded shader program cache. {} shader programs loaded with {} bytes. {} msec` | — | Renderer initialised. |
| `0x272c79` | `[FLog::Graphics] DRS init: denied={} fullSizeFramebufferDenied={} canUseHighDPIFullFB={}` | — | Dynamic-resolution init. |
| `0x2baec0` | `[FLog::Graphics] Android API %d` | — | Device params read. |
| `0x29df3e` | `[FLog::Graphics] eglSwapInterval(%d)` | `0x649c56f` | **Inside `egl_init` `0x649c272`** — the guest completed EGL make-current, query and swap-interval setup. |
| `0x565a02` | `Error creating context: eglGetDisplay %x` | `0x649ced9` | EGL error path. |
| `0x4f53c3` | `Error creating context: eglInitialize %x` | `0x649cf00` | EGL error path. |
| `0x58151a` | `Error creating context: eglCreateContext %x` | `0x649ce6a` | EGL error path. |
| `0x3d66da` | `Error creating context: eglMakeCurrent %x` | `0x649ce95`, `0x649d5a9` | EGL error path. |
| `0x2bb1d2` | `Error creating context: eglCreateWindowSurface %x` | `0x649d20a` | EGL error path. |
| `0x32b9df` | `[FLog::Graphics] eglMakeCurrent failed with '%d'` | `0x649d3ec` | EGL error path (vtable rebind). |

The `emit@` addresses `0x649c56f`, `0x649ce6a`, `0x649ced9`, `0x649cf00`,
`0x649ce95`, `0x649d20a`, `0x649d3ec` all lie in the guest render cluster
`0x649c272..0x649d5a9` identified by `render-map.md §3` as the EGL
context/surface setup routine. **Absence of both the success lines and the
`Error creating context:` lines means the guest app's own EGL init was never
entered in these runs** — it is not a silent failure; it is "never reached".
EGL work that did occur came from our module driving `0x649c21e` through compat
shims (§6), which does not route through the app's graphics-device logger.

### 2.3 RenderView / SurfaceController / frame loop — frame-presentation proof

| String RVA | Exact format string | emit@ | Proves |
| --- | --- | --- | --- |
| `0x5556fa` | `[FLog::ViewRbxInit] RenderView created - %p` | — | A RenderView was constructed. |
| `0x2c7141` | `[FLog::Graphics] RenderView created[{}]` | `0x384043e` | RenderView registered with graphics. |
| `0x337f18` | `[FLog::ViewRbxBase] Render perform start` | `0x29682fc` | A frame's render pass began. |
| `0x2a9537` | `[FLog::ViewRbxBase] Render perform end. Delta: %f, Total %f, Present: %f, Prepare: %f` | `0x296c67a` | **A frame was composed and presented** (`Present:` field). |
| `0x28e727` | `[FLog::Graphics] RenderView destroyed[{}]` | `0x384238c` | Teardown. |
| `0x4c5571` | `[FLog::SurfaceController] SurfaceController[%s:%d]::SurfaceController, count: %d` | — | Object constructed (**this one IS observed**, see §3.2). |
| `0x4a8d8a` | `[FLog::SurfaceController] SurfaceController[%s:%d]::start dataModel(%p)` | `0x276d0f6` | SurfaceController started with a DataModel. |
| `0x437263` | `[FLog::SurfaceController] SurfaceController[%s:%d]::run dataModel(%p)` | — | Render job running. |
| `0x3c2f02` | `[FLog::SurfaceControllerDebug] run ... add render job to task scheduler.` | — | Frame render job scheduled. |
| `0x352088` | `[FLog::SurfaceController] RenderView is NULL` | `0x2701d74` | Failure: no view to render into. |
| `0x2364e2` | `[FLog::SurfaceController] data model was null during SurfaceController::run() - unexpected` | `0x3215577` | Failure: no DataModel. |
| `0x23653d` | `[FLog::SurfaceController] SurfaceController::run unable to proceed due to empty renderJob` | `0x3215a5e` | Failure: empty render job. |
| `0x28bd42` | `[FLog::SurfaceController] SurfaceController::RenderJob::stepDataModelJob: No DM yet. Continue...` | `0x2825b8e` | Render job waiting for DataModel. |
| `0x2c426d` | `[FLog::SurfaceController] SurfaceController[{}:{}]::update.` | `0x2773443` | Surface update processed. |
| `0x2fc4ce` | `[FLog::SurfaceController] SurfaceController[%s:%d]::~SurfaceController, count: %d` | `0x3214f72` | Teardown. |

### 2.4 Deferred-swap gate and surface-lifecycle JNI/NativeDM strings

| String RVA | Exact format string | emit@ | Proves |
| --- | --- | --- | --- |
| `0x236433` | `[FLog::SingleSurfaceApp] [DeferredRbxm] holding the surface swap until the game DataModel reports loaded` | `0x321230c` | Engine owes a frame until game DM loads. |
| `0x2e0b70` | `[FLog::SingleSurfaceApp] [DeferredRbxm] abandoning the owed swap; stopping the game DataModel loading behind the screen` | `0x320956a` | Owed swap abandoned. |
| `0x2c41aa` | `[FLog::SingleSurfaceApp] [DeferredRbxm] resuming UGC join after download completed (window={})` | `0x32092bc` | Owed swap resumed with a window. |
| `0x2df0a5` | `[FLog::JNIAppBridge] nativeAppBridgeV2UpdateSurfaceGameWithPlatformParams:` | `0x2ebef1e` | Game-surface update entry reached. |
| `0x2df218` | `[FLog::NativeDM] nativeActivity_onSurfaceChanged: state:{}.` | `0x2ee4005` | Native surface-changed handler reached. |
| `0x234888` | `[FLog::NativeDM] nativeActivity_onKillSurface: state:{}.` | `0x2ee3b0c` | Surface destruction handler. |
| `0x2347e8` | `[FLog::NativeEngine] APP_CMD_TERM_WINDOW: app-window: {}.` | `0x2ede15e` | Android lifecycle TERM_WINDOW. |

`onSurfaceCreatedNative` (`0x26bea6`) and `onSurfaceDestroyedNative`
(`0x2a2a53`) are the JNI callback *names* registered at descriptor table
`0x6d37cd8`/`0x6d37d20` (`render-map.md §4`); they are not log format strings
and their native thunks are `0x2a20850`/`0x2a20a30`.

### 2.5 DFLog strings — none render-related

The task's "DFLog render strings" hint resolves as follows (OBSERVED): the
frozen binary has **no** `DFLog::Graphics`/`DFLog::Render`/`DFLog::Surface`
category. Render/surface logs are on `FLog::Graphics`, `FLog::ViewRbx*`,
`FLog::SurfaceController*`, `FLog::SingleSurfaceApp`, `FLog::JNIAppBridge` and
`FLog::NativeDM` (§2.1–2.4). The only DFLog lines actually observed in the
corpus are:

| String RVA | Exact string (as logged) | Render relevance |
| --- | --- | --- |
| `[DFLog::CoordinatorMemoryPressure]` | `Setting memory pressure threshold to none` | None — memory-pressure config (4 logs). |
| `[DFLog::LoadSettingsZstdDictionary]` | `Platform assets path not set for dictionary loading.` | None — settings dictionary (6 logs). |

DFLog therefore contributes **zero** render-acceptance evidence in this corpus;
it must not be cited as proof of a frame.

---

## 3. What the collected guest logs actually contain

### 3.1 Corpus

- `appData/logs/`: 316 `*_last.log` files (2026-11-18T21:46 → 2026-11-21T00:23),
  2,384 lines total, 1.3 MB.
- `build/appData/logs/`: 2 files (2026-11-20T21:23).
- `/tmp/opencode/`: 244 `.log`/`.out`/`.err` files (harness captures).

### 3.2 Strings that DO appear (render/surface-adjacent, all OBSERVED)

| String RVA | Logged line (representative) | Files | What it proves |
| --- | --- | --- | --- |
| — | `[FLog::Output] All use of Roblox services must comply …` | 316 | Banner only. |
| `0x453233` | `[FLog::AndroidGLView] nativePostClientSettingsLoadedInitialization3` | 7 | Client-settings init; **not** GL context. |
| `0x2fad06` | `[FLog::AndroidGLView] nativeInitClientSettings` | 6 | Client-settings init. |
| `0x34ff5b` | `[FLog::AndroidGLView] initClientSettingsAndroid: Exception = [HttpError: InvalidUrl].` | 6 | Settings fetch failed (no asset base URL). |
| `0x4fce17` | `[FLog::AndroidGLView] rbx.datamodel: setTaskSchedulerBackgroundMode() enable:false context:sober-native` | 1 | A DataModel exists; harness context. |
| `0x4c5571` | `[FLog::SurfaceController] SurfaceController[_:1]::SurfaceController, count: 1` | 1 (log `4d715`) | **SurfaceController object constructed** — the only surface-layer success line present. |
| — | `[DFLog::CoordinatorMemoryPressure] Setting memory pressure threshold to none` | 4 | Non-render. |
| — | `[DFLog::LoadSettingsZstdDictionary] Platform assets path not set for dictionary loading.` | 6 | Non-render. |

Also present (app-start, not render): `nativeAppBridgeV2Init:`,
`initializeWithAppStarter`, `initializeSingleton`, `setAssetFolder `,
`Error: The path '' does not exist`, `nativeAppBridgeAppStart:`,
`nativeAppBridgeV2StartApp:`, `startLuaApp: (stage:None).`, `setStage: (stage:LuaApp)`,
`userDidLogin`, `applyLocale`, `nativeAppBridgeV2StartGameWithParam:`,
`launchUGCGame: (stage:LuaApp).`, `launchUGCGameInternal`,
`nativeAppBridgeV2UpdateSurfaceAppWithPlatformParams:`,
`updateSurfaceLuaApp: (stage:InitializedLuaApp)`,
`... StartupController finished starting: stage = 4, BTID = -1, upgradeStatus = 0.`

### 3.3 The terminal line in the deepest runs

`[LOGCHANNELS + 1] RBXCRASH: FatalRuntimeError (Can't initialize the
TaskScheduler before flags have been loaded)` appears in 11 logs, including the
two deepest app-start logs and the newest harness-era logs. Per
`analysis/T2_5_flags_latch.md` and `lua-rbxcrash-flags-loaded.md`, this guard
(aborting at guest code around `0x6caef66/0x6caef67`, message string `0x36a389`)
fires when the FFlag latch `0x7cdd149` is not set. Consequence (DERIVED): the
game DataModel and its render job never come up, so none of §2's graphics /
RenderView / frame strings can be emitted. This is the *app-side* cause of the
empty render log; the harness-side cause is §6.

### 3.4 Harness instrumentation vs guest logs (`/tmp/opencode/w_after.out`)

The `[render]`, `[harness]` and `[app-shell]` lines at 3668–3726 are **our own
stderr instrumentation**, not guest FLog/DFLog output. They never contain
`Created ANativeWindow`, `GL Renderer`, `RenderView created` or `Render perform`.
The guest log for the same run (`appData/logs/...T002330Z_...cece4_last.log`,
7 lines) stops at `StartupController finished starting: stage = 4`. Confirmed:
no render strings in either stream.

---

## 4. Which logs prove acceptance, and which are missing

| Candidate proof (string RVA) | Appears in corpus? | Verdict |
| --- | --- | --- |
| `Created ANativeWindow` `0x3c163c` | **No** (0 hits) | **MISSING — decisive.** No guest-originated Surface→ANativeWindow accept. |
| `Same native window` `0x46f987` | No | MISSING — no real rebind/no-op either. |
| `Destroying ANativeWindow` `0x3327b7`/`0x43554f` | No | MISSING — consistent with no bound window ever. |
| `GL Renderer`/`GL Vendor`/`GLSL version` `0x22c71a`/`0x282c5d`/`0x32b77e` | No | **MISSING** — guest graphics device logger never ran. |
| `eglSwapInterval` `0x29df3e` (emit inside `0x649c272`) | No | **MISSING** — guest's own EGL init never reached. |
| `Error creating context: …` (6 strings, §2.2) | No | MISSING — not even an EGL *failure* was logged by the app. |
| `RenderView created` `0x5556fa`/`0x2c7141` | No | **MISSING** — no RenderView. |
| `Render perform start`/`end` `0x337f18`/`0x2a9537` | No | **MISSING — decisive for draws=0.** No present/frame pass. |
| `SurfaceController … start/run dataModel` `0x4a8d8a`/`0x437263` | No | MISSING — controller never started a render job. |
| `run … add render job to task scheduler` `0x3c2f02` | No | MISSING. |
| `holding/abandoning/resuming … swap` `0x236433`/`0x2e0b70`/`0x2c41aa` | No | MISSING — deferred-swap stage never reached. |
| `onSurfaceChanged`/`UpdateSurfaceGame…` `0x2df218`/`0x2df0a5` | No | MISSING — only the *app* variant (`UpdateSurfaceApp…`) appears, not the game-surface update. |
| `SurfaceController … SurfaceController, count` `0x4c5571` | **Yes** (1 log) | **Present but weak** — object construction only; not acceptance or frame. |
| `nativeInitClientSettings`/`nativePostClientSettingsLoadedInitialization3` `0x2fad06`/`0x453233` | Yes (6–7 logs) | Present but **not render proof** — client settings only. |
| `rbx.datamodel: setTaskSchedulerBackgroundMode … context:sober-native` `0x4fce17` | Yes (1 log) | Present but **not render proof** — DataModel + harness tag. |
| DFLog lines (§2.5) | Yes (10 logs) | **Not render proof.** |

**Conclusion:** the *only* guest line that would prove surface acceptance is
`Created ANativeWindow` (`0x3c163c`/`emit 0x26feaf7`), and it is absent. The
only lines that would prove a presented frame are `RenderView created` and
`Render perform end` (`0x2c7141`/`0x2a9537`), also absent. The single present
surface-layer line (`SurfaceController…::SurfaceController`) proves construction,
not acceptance. Therefore the log corpus **cannot** support `render_ok` on guest
evidence; `draws=0` is corroborated, not contradicted.

---

## 5. Expected log sequence for a genuine guest frame (Android/JVM path)

This is the normative order implied by the emit sites and call graph (DERIVED;
no line below has been observed in this corpus). Compare with §3 to see exactly
where our runs stop.

1. Boot / app start (§3.2 lines 1–17 — **observed today**):
   `All use of Roblox services …` → `nativeAppBridgeV2Init:` →
   `initializeWithAppStarter` / `initializeSingleton` → `setAssetFolder ` →
   `nativeAppBridgeAppStart:` → `nativeAppBridgeV2StartApp:` →
   `startLuaApp:` / `setStage: (stage:LuaApp)` / `userDidLogin` / `applyLocale`.
2. Game start (partly observed): `nativeAppBridgeV2StartGameWithParam:` →
   `launchUGCGame:` → `launchUGCGameInternal` → `nativeInitClientSettings` →
   `nativePostClientSettingsLoadedInitialization3` → flags loaded (latch
   `0x7cdd149` set; **no RBXCRASH** — our runs fail here).
3. Device/graphics init (**missing today**): `getDeviceStaticParams` →
   `[FLog::Graphics] Android API %d` → `GL Vendor:` → `GL Renderer:` →
   `GLSL version:` → `GL feature level:` → `DRS init:` →
   `[FLog::Graphics] Loaded shader program cache. …`
   (EGL context/surface would be created via `0x649c21e → 0x649c272`,
   which emits `[FLog::Graphics] eglSwapInterval(%d)` at `0x649c56f`; any
   failure emits one of the six `Error creating context:` strings).
4. Surface bind / lifecycle (**missing today**):
   `nativeAppBridgeV2UpdateSurfaceAppWithPlatformParams:` (observed) →
   `nativeAppBridgeV2UpdateSurfaceGameWithPlatformParams:` (`0x2df0a5`) →
   `nativeActivity_onSurfaceChanged: state:{}` (`0x2df218`) → bind_window
   `0x26fea5a`/`0x26fea78` → **`[FLog::JNIAppBridge] …: Created ANativeWindow
   0x… (ID:n)`** (`0x3c163c` @ `0x26feaf7`) →
   `SurfaceController[…]::initialize` (`0x36e5f5`) →
   `SurfaceController[…]::start dataModel(%p)` (`0x4a8d8a`) →
   `run … add render job to task scheduler.` (`0x3c2f02`) →
   `SurfaceController[…]::run dataModel(%p)` (`0x437263`).
   (A repeat bind of the same window yields `Same native window …` `0x46f987`.)
5. RenderView + frame loop (**missing today**):
   `[FLog::ViewRbxInit] RenderView created - %p` (`0x5556fa`) /
   `[FLog::Graphics] RenderView created[{}]` (`0x2c7141`) →
   `[DeferredRbxm] holding the surface swap …` (`0x236433`) →
   `[DeferredRbxm] resuming UGC join after download completed (window=…)` (`0x2c41aa`) →
   `[FLog::ViewRbxBase] Render perform start` (`0x337f18`) →
   guest `call QWORD PTR [vtable+0x18]` through `[0x71912e8+0x18]=[0x7191300]=0x649d422`
   → `eglSwapBuffers@plt 0x6d334d0` → GOT `0x722a1a8`, with the GLES draw
   dispatcher `0x6496d0c` (sites `0x6496dbc`, `0x6496dec`, `0x6496e1a`,
   `0x6494ae1`) issuing draws →
   `[FLog::ViewRbxBase] Render perform end. Delta: …, Present: …` (`0x2a9537`).
6. Teardown (only on app exit): `nativeActivity_onKillSurface: state:{}`
   (`0x234888`) / `APP_CMD_TERM_WINDOW` (`0x2347e8`) →
   `Destroying ANativeWindow at {}` (`0x3327b7`/`0x43554f`) →
   `RenderView destroyed[{}]` (`0x28e727`) / `SurfaceController…::~SurfaceController`
   (`0x2fc4ce`).

A clean guest-tagged verification run (T3-5 §4 / T4-4 §1.3) would additionally
show our *instrumentation* lines between steps 5a and 5b:
`[render] guest-tagged swap at 0x649d422 ra=base+0x… tag=guest-loop | guest-tagged
swaps: N | guest GLES draws between tagged swaps: M`,
`[render] guest render verdict: … -> GUEST-TAGGED`, and
`[app-shell] render provenance: … phase3_done=1 -> OK`. Those are **our** log
lines, not guest FLog strings, and none has been observed (marker WITHHELD).

---

## 6. Reconciliation with `draws=0` (harness side)

The log evidence agrees with the counter diagnosis in R1/R3/R7:

- `gles_loads=48` / `proc_loads=48` and `draw_wrapped=5` come from our module
  calling genuine `0x649c21e(window, pbuffer=0)` → `0x649c272`, which loads GLES
  procs through hooked `eglGetProcAddress` GOT `0x722a180` and wraps 5 draw
  names. The native module's `ctx=0x…92b0 vt=0x…12e8(vt_expect=0x…12e8)` line
  confirms the vtable, but **no guest FLog line in §2.2 accompanies it** — the
  app's own device logger is bypassed. OBSERVED (RUNTIME) + DERIVED.
- `from_surface=2` are the two genuine guest call sites `0x2a2087c`
  (`onSurfaceCreatedNative`) and `0x2a20907` (`onSurfaceChangedNative`) hit via
  host-driven thunks. Crucially, the **app-start site `0x26fea78` was not
  reached**, which is exactly why `Created ANativeWindow` (`0x3c163c`) was
  never emitted: the only code that logs it is downstream of `0x26fea78`.
- The window global `0x72a3308` is satisfied by a **host store**
  (`render_guest.c:326-329`), not by the guest's `0x26feaa0`, so the guest emits
  no acceptance log for it (R7 §0/§1).
- `nativeData+0x88/+0x98/+0xa0 = NULL` makes redraw thunk `0x2a20a00` skip the
  dispatch at `0x2a20a11`/`0x2a20a20`; no frame loop → no `call [vtable+0x18]`
  → no GLES draw dispatcher `0x6496d0c` → `draw_calls=0`. In log terms: no
  `RenderView created`, no `Render perform start/end` (§2.3), and no
  `holding/resuming the owed swap` (§2.4). This is the log-side signature of
  `draws=0`.
- The single swap `swaps(total/guest/synth)=1/0/1` is the host's Route-B direct
  call `guest_swap((uintptr_t)ctx)` at `guest_render.c:354`; its RA is in the
  host, so `rg_in_guest(ra)` correctly refuses to tag it (R1 §1–§3). No guest
  frame log accompanies it — consistent.

**Why there are no EGL error strings either:** because the guest's *app* EGL
init (`0x649c272` from the app path) never runs; the only EGL work is our
module's direct call through compat shims and the Route-B swap, neither of
which goes through the app's `Error creating context:` logger. So the logs show
neither success nor error — they show *absence*, which is the correct signal.

**Honesty:** seeding a guest-shaped Surface/window and driving the thunks so
that `Created ANativeWindow` and a frame log appear would create a **fake**
acceptance line and a withheld diagnostic (`marker withheld: diagnostic
guest-state seed active`). Under decision 013 such a line is still not proof;
the marker remains WITHHELD. `draws=0` must be explained, not inflated.

---

## 7. Limits / honesty

- All proof-string RVAs and emit-site RVAs are OBSERVED from the frozen ELF and
  its retained disassembly; no target string was executed or trusted as a
  command. A few emit-site RVAs are marked `—` where the retained dump did not
  expose a direct `lea … # addr` comment; the string RVA itself is OBSERVED in
  every case.
- The "expected sequence" (§5) is a DERIVED call-graph ordering, not a captured
  genuine frame; no clean guest frame exists in this corpus. Evidence classes
  for each claim are given inline.
- DFLog contributes no render evidence (§2.5). The FFlag/TaskScheduler abort
  (`0x36a389` message) is a separate blocker documented in
  `T2_5_flags_latch.md`/`lua-rbxcrash-flags-loaded.md`; this report only notes
  its log consequence.
- No build, harness run, debugger capture or REA query was performed for this
  document. All log counts are from reads of existing files.
- **No success claimed. Marker WITHHELD. Decisions 013/037/038 unchanged.**

---

## 8. Evidence index

Read only this task: frozen `libroblox.so` (SHA/BuildID above) via `strings -a -t
x` and `objdump -d` slice `0x26fea5a..0x26feb96`; `/tmp/opencode/libroblox.dis`
(`lea … # <string RVA>` xrefs); `appData/logs/*.log` (316),
`build/appData/logs/*.log` (2), `/tmp/opencode/**/*.{log,out,err}` (244);
`/tmp/opencode/w_after.out:3668-3726`; `appData/logs/2.740.0.931_20261120T071938Z_Player_4d715_last.log`
and `...T002330Z_Player_cece4_last.log`.

Prior analyses cross-checked (read only): `analysis/C08-surface-frame.md §2-§4`,
`analysis/R1_zero_tagged_swaps.md §0-§7`, `analysis/R3_guest_egl_context.md §0-§1`,
`analysis/R7_anativewindow_binding.md §0-§1`, `analysis/render/render-map.md §2-§4`,
`analysis/T3_5_render-evidence.md §2-§6`, `analysis/T4_4_gate-tests.md §1-§3`,
`analysis/T2_5_flags_latch.md`, `analysis/lua-rbxcrash-flags-loaded.md`,
`analysis/ghidra/work/function-summary.json` (Flatpak RVAs rejected).

No file other than this report was created or modified.
