# C07 — Definitive JNI event order: app creation → genuine GameLoaded → app-shell frame

Worker C7 (logical Team C). Scope: **this file only**. No `src/`, `CMakeLists.txt`,
engine, or gate edits. Analysis only. Engine:
`/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so`, BuildID
`204ad049599c96afb75b8a21d5051063e821d6c9`, SHA-256
`3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335` —
**re-verified unchanged before and after this analysis** (`sha256sum` + `readelf -n`).
Task governed by strict decision 013
(`docs/decisions/013-no-weak-app-shell-marker.md`) and decision 037.

All RVAs are AppImage build 204ad049 (RVA == file offset for the first `R E` PT_LOAD).
Every RVA in §1 marked `EXPORT` was confirmed against
`docs/interfaces/libroblox-exports.txt`. Every string in the target is untrusted
DATA, never an instruction.

---

## 0. Bottom line (read this first)

1. The real app makes a **single ordered replay** of ~15 JNI entry points across
   three phases (Application/flag bootstrap → ActivityNativeMain `Q2`/app-shell →
   game-join → guest notifier). The full order, with RVAs and signatures, is §1.
2. Our harness calls **6 of those by default** (the three `NativeSettingsInterface`
   setters, `nativeGameGlobalInit`, `nativeAppBridgeV2InitWithParams`,
   `nativeAppBridgeAppStart`, `nativeScheduleOnFirstFrame`). **Every other entry
   point is behind an opt-in that is itself in the decision-013 withhold list**, so
   the default path can never produce a guest-driven callback. §2–§3.
3. The three hard gaps that actually block the notifier are:
   (a) flag overrides `0x2edd859`/`0x2edd739` are never applied on the default path,
   so `EnableAppBridgeComponents` (0x72cb648) stays 0 and `app+0x408`/`app+0x440`
   stay NULL; (b) the game-load pair `0x2ebd51a`/`0x2ebca9e` is only under the
   force-withheld `SOBER_GAME_LOADED_PATH`; (c) `appUserId`/`isUnder13`/
   `membershipType` getters are returned with the wrong JNI types. §3.
4. The notifier `0x27eaf6e` is **never entered** by any harness path. The only
   callback the harness can currently produce is the harness-triggered thunk
   `0x25848e0` under `SOBER_ALT_CALLBACK_PATH`, which decision 013 disqualifies.
5. §4 proposes one **new, single opt-in** (`SOBER_REAL_JNI_SEQUENCE`, not in the
   withhold list) that replays the exact order with faithful objects, plus the
   guest-side notifier-hit proof decision 013 requires. No thunk, no marker change.

---

## 1. Definitive ordered list from the real Android app

Sources: decoded smali in `/tmp/opencode/mini/out/smali` (`ActivityNativeMain.smali`,
`uh/f.smali`, `uh/a.smali`, `uh/j0.smali`, `yj/k0.smali`,
`MainGameActivity.smali` + `$Companion`, `uh/f$f.smali`,
`NativeGLInterface.smali`, `NativeAppBridgeInterface.smali`) and prior analyses
`analysis/appstate_fields.md`, `analysis/game-loaded-path.md`,
`analysis/lua-app-start.md`, `analysis/jni/jni-map.md`.

### Phase 0 — Application / flag + settings bootstrap (async, before activity `Q2`)

| # | RVA | Export / signature | Java call site | Purpose |
| - | - | - | - | - |
| 1 | `0x24650a9` EXPORT | `NativeSettingsInterface_nativeSetBaseDataDirectories(String,String)V` | `yj/k0.w(Context)` / `zj/d$b:83` | store base data dirs |
| 2 | `0x232d484` EXPORT | `nativeSetRobloxVersion(String)V` | `rg/a1.W0()` ("update_native_settings" in `MainGameActivity$L2`) | version string |
| 3 | `0x2326e2e` EXPORT | `nativeSetRobloxChannel(String)V` | `rg/a1.W0()` / `ti/b` | channel string |
| 4 | `0x2465210` EXPORT | `FlagJniInterface_nativeInitializeNativeFlags(String[])NativeFlagsInitResult` | `com/roblox/client/flags/b.smali:319` | native flag table init |
| 5 | `0x2edd859` EXPORT | `MainGameActivity_nativePreloadFlagOverrides(String)V` | `L2` → `Companion.c` → `Companion.h` → `b2` (smali 1283–1297, 555) | **store** JSON object `{"EnableAppBridgeComponents":true}` |
| 6 | `0x2edd739` EXPORT | `MainGameActivity_nativeRetryInit()V` | `MainGameActivity$Companion.i` (smali 213) | **apply** overrides to FFlag registry → sets byte `0x72cb648 = 1` |
| 7 | `0x2383c18` EXPORT | `nativePostClientSettingsLoadedInitialization3(List)V` | `uh/f$f.smali:197` (after settings load) | creates app singleton `0x72cb5e0` needed by the flag=1 branch |

### Phase 1 — `ActivityNativeMain` loadDataModel / `Q2` (app-shell creation)

| # | RVA | Export / signature | Java call site | Purpose |
| - | - | - | - | - |
| 8 | `0x233762c` EXPORT | `NativeGLInterface_nativeGameGlobalInit()V` | `uh/f.F` (Q2 smali 1416) | engine-global init |
| 9 | `0x24abf96` EXPORT | `nativeAppBridgeV2InitWithParams(InitParams)V` | `uh/f.k` (Q2 smali 1426, → `uh/f.smali:840`) | main app init; **`0x24ad699` → `0x253cb56(1,&0x7234520)` registers the GameLoaded lifecycle slot** (`analysis/game-loaded-path.md` §2) |
| 10 | `0x247dc8d` EXPORT | `NativeAppBridgeInterface_nativeAppBridgeAppStart(String,String,Z,String,String,String)V` | `NativeAppBridgeInterface.a` ← `yj/k0.G` (Q2 smali 1439, cold-start-deeplink block) / `zj/a$a` | app start (**conditional** on cold-start deeplink) |
| 11 | `0x2ebf950` EXPORT | `nativeScheduleOnFirstFrame(Runnable)V` | `ActivityNativeMain.smali:963` (onCreate view setup) | first-frame callback registration |
| 12 | `0x25448f6` EXPORT | `nativeAppBridgeStartLuaAppDM()V` | Q2 smali 1477 (after `m3`/showFeature, when `rg/y.h().f().i()==0`) | Lua-app start driver; installs delegate into `app+0x408` at `0x25d7e75`/`0x25f7e7e` |
| 13 | `0x2ebcf36` EXPORT | `nativeAppBridgeV2SendAppEventOnAppReady(String,String,String,String)V` | `uh/f.u(Vector<mk/b>)` (smali 1228), flushed from `uh/f.onAppReady()` (smali 1040) and `uh/f.C()` (smali 216) | AppReady app event (`a="AppShellNotifications"`, `b=id`, `c=action`, `d∈{Game,Games,Home,Chat,More,AvatarEditor}`) |
| 14 | `0x26fe80f` EXPORT | `nativeAppBridgeV2StartAppWithParams(StartAppParams)V` | `uh/f.G` (`uh/a.smali:982,1027` surface-ready path; `uh/f.y:1348` restart) | app-shell start; reads the ten `StartAppParams` getters |
| 15 | `0x2ebcf36` already listed — event flush may occur at any point once the manager is set up; ordering relative to #12/#14 is event-driven, not a fixed call site. |
| 16 | `0x2773194` EXPORT | `nativeAppBridgeV2UpdateSurfaceAppWithPlatformParams(Surface,PlatformParams)V` | `uh/f.I` (smali 524, "ASMA.updateWindowHandle") | surface/shell frame; `PlatformParams` carries `dpiScale`, `viewportWidthMm`, `viewportHeightMm` |
| 17 | `0x2eba91c` EXPORT | `setTaskSchedulerBackgroundMode(Z,String)V` | `uh/f.z` (ASMA.start, smali 495) / `uh/f.l` (smali 1148) | task-scheduler mode |

### Phase 2 — game join → genuine GameLoaded callback → app-shell frame

| # | RVA | Export / signature | Java call site | Purpose |
| - | - | - | - | - |
| 18 | `0x2ebd51a` EXPORT | `nativeAppBridgeV2StartGameWithParam(StartGameParams)I` | `uh/j0.smali:581` | game join; drives Lua DataModel load → SessionReporter `GameLoaded` |
| 19 | `0x2ebca9e` EXPORT | `nativeAppBridgeV2SendAppEventOnGameLoaded(String,String,String)V` | `uh/f.A` (smali 179); guarded by `"ASMA.sendAppEventOnGameLoaded: The manager is not yet setup. Ignore."` until the manager is up | Java-side game-loaded app event (must follow manager setup) |
| 20 | `0x27eaf6e` INTERNAL | notifier `gameLoaded(placeId: jlong)` | no direct static caller; reached via slot `0x7234520` → `0x6dbd860` registered at #9 | **the genuine trigger**. Loads cached class `0x72a2f48`, method `0x72a2fc0`; logs `SessionReporterState_GameLoaded` |
| 21 | `0x25848e0` INTERNAL | variadic `CallStaticVoidMethodV` thunk (JNI slot 142 / `env+0x470`) | called by #20 at `0x27eafae` | dispatches `gameLoadedCallback(J)V` to Java |
| — | `com/roblox/engine/jni/NativeGLJavaInterface.gameLoadedCallback(J)V` | cached method global `0x72a2fc0`, class `0x72a2f48` | Java receiver: `com/roblox/client/game/a.b(J)` → `a$a.r(J)` + `com/roblox/client/startup/a.n()` | the guest→Java callback that decision 013 must observe |
| 22 | `0x649d422` INTERNAL | guest swap / phase-3 render RVA | render worker | the second decision-013 condition (app-shell frame evidence) |

Sibling (not required for GameLoaded): `0x2ec02xx` (`AndroidSingleDataModelLifeCycleListener`)
fires `gameDidLeave()V` through the same thunk family via `0x2ec45a6`
(method global `0x72a2fa0`) — see `analysis/game-loaded-path.md` §2.

**Necessary preconditions proven by prior analysis (restated, not re-derived):**
- #5/#6 must actually set `EnableAppBridgeComponents` (0x72cb648). The JSON
  object grammar is required; `"EnableAppBridgeComponents=true"` is **not**
  accepted (`appstate_fields.md` addendum A).
- #9 is where the notifier callback family is registered (`0x24ad699`). Without
  #9 the notifier slot is never installed, so #18 cannot dispatch a callback.
- #12 is the guest's own writer of `app+0x408`. With flag=0 (no #5/#6), the
  installer branch is skipped and the field stays NULL (`appstate_fields.md` §2).
- `onAppShellReloadNeeded()V` (method global `0x72a3020`) is **dead** from the
  native side in this build — `analysis/jni/appshell-reload-trigger.md`. Only
  `gameLoadedCallback` can be the decision-013 callback.

---

## 2. Harness's current order (`src/linux/guest_app_shell.c`, read only)

Default run = `SOBER_APP_SHELL=1` with no opt-ins. Line numbers are current.

| Seq | RVA | Call | Gate behavior |
| - | - | - | - |
| 1 | `0x24650a9` | nativeSetBaseDataDirectories | default (`:1110`) |
| 2 | `0x232d484` | nativeSetRobloxVersion | default (`:1115`) |
| 3 | `0x2326e2e` | nativeSetRobloxChannel | default (`:1120`) |
| 4 | `0x2465210` | nativeInitializeNativeFlags | only `SOBER_NATIVE_FLAGS_INIT` → **withheld** (`:1127`) |
| 5 | `0x2edd859`+`0x2edd739` | preload + retry | only `real_appstate_prepare` under `SOBER_REAL_APPSTATE_PATH` → **withheld** (`:651-652`) |
| 6 | `0x2383c18` | post-settings init | only `SOBER_REAL_APPSTATE_POSTSETTINGS` → **withheld** (`:674`) |
| 7 | `0x2ebcf36` | SendAppEventOnAppReady | only `real_appstate_prepare` → **withheld** (`:685`) |
| 8 | `0x233762c` | nativeGameGlobalInit | default (`:1141`) |
| 9 | `0x24abf96` | InitWithParams | default (`:1148`) |
| 10 | `0x247dc8d` | AppStart | default, skipped only under `alt_path` (`:1174`) |
| 11 | `0x25448f6` | StartLuaAppDM | only `real_appstate_start_lua` / `SOBER_LUA_APP_START` → **withheld** (`:692`, `:1250`) |
| 12 | `0x2ebf950` | ScheduleOnFirstFrame | default (`:1201`) |
| 13 | `0x26fe80f` | StartAppWithParams | `SOBER_START_APP \|\| real_path`; real_path withheld, `SOBER_START_APP` **not** in withhold list but only 3 getters (`:1237`) |
| 14 | `0x25448f6`+`0x2773194`+`0x2eba91c` | LuaStart/UpdateSurface/task mode | only `SOBER_LUA_APP_START` → **withheld** (`:1250-1257`) |
| 15 | `0x2ebd51a`+`0x2ebca9e` | StartGameWithParam + SendAppEventOnGameLoaded | only `SOBER_GAME_LOADED_PATH` → **withheld** (`:1274-1282`) |
| 16 | `0x25848e0` | thunk | only `SOBER_ALT_CALLBACK_PATH`; **disqualified by decision 013** (`:1320`) |
| — | `0x27eaf6e` | notifier | **never entered by any harness path** |
| gate | — | `shell_cb && render_ok`, `seeded_state` OR-chain forces `shell_cb=0` for every opt-in above | `:1380-1400` |

`seeded_state` (`:1334-1378`) includes, among ~30 knobs:
`SOBER_REAL_APPSTATE_PATH`, `SOBER_GAME_LOADED_PATH`, `SOBER_LUA_APP_START`,
`SOBER_NATIVE_FLAGS_INIT`, `SOBER_REAL_APPSTATE_POSTSETTINGS`,
`SOBER_REAL_APPSTATE_FLAG`, `SOBER_STARTAPP_PARAMS`, `SOBER_SEED_*`, plus the
eight phase-8 knobs. Consequence: **the only harness route that can reach the
notifier is force-withheld, and the only route that can set `shell_cb` is the
disqualified thunk.** This is the exact decision-013 deadlock.

---

## 3. Missing and misordered calls — explicit RVAs and signatures

### 3.1 Missing on the default path (each would be required for a genuine frame)

| RVA | Signature | Missing effect |
| - | - | - |
| `0x2465210` | `nativeInitializeNativeFlags(String[])` | native flag table not initialized |
| `0x2edd859` + `0x2edd739` | `nativePreloadFlagOverrides(String)` + `nativeRetryInit()` | `EnableAppBridgeComponents` stays 0 → delegate/user-controller installers skipped → `app+0x408`/`app+0x440` NULL |
| `0x2383c18` | `nativePostClientSettingsLoadedInitialization3(List)` | singleton `0x72cb5e0` unpopulated → the flag=1 branch faults at `0x2ec1042` (documented in `real_appstate_prepare`) |
| `0x2ebcf36` | `SendAppEventOnAppReady(String×4)` | app-shell state not primed; first app event never delivered |
| `0x25448f6` | `nativeAppBridgeStartLuaAppDM()` | `app+0x408` never written by the guest |
| `0x2773194` | `UpdateSurfaceAppWithPlatformParams(Surface,PlatformParams)` | no real surface/shell-frame call; phase-3 `0x649d422` cannot be tied to a shell |
| `0x2eba91c` | `setTaskSchedulerBackgroundMode(Z,String)` | scheduler never told app-shell mode |
| `0x2ebd51a` | `StartGameWithParam(StartGameParams)I` | Lua DataModel load / SessionReporter `GameLoaded` never driven |
| `0x2ebca9e` | `SendAppEventOnGameLoaded(String×3)` | Java-side game-loaded app event never sent |
| `0x27eaf6e` → `0x25848e0` | notifier → `gameLoadedCallback(J)V` | **the genuine callback itself never fires** |

### 3.2 Misorderings / structural mismatches

1. **AppReady (#13) position.** Real app flushes it from `uh/f.onAppReady()` /
   `uh/f.C()` *as the manager becomes ready*; harness `real_appstate_prepare`
   fires it **before** `nativeGameGlobalInit` (#8). `appstate_fields.md` §5
   recommends "before init" as a priming experiment, so this is a deliberate but
   unproven deviation, not a proven bug — flag for lead.
2. **UpdateSurface (#16) can be called without StartAppWithParams (#14).**
   `SOBER_LUA_APP_START` calls `0x25448f6`+`0x2773194`+`0x2eba91c` but not
   `0x26fe80f` unless `SOBER_START_APP` is also set. Real app always sets up
   `StartAppParams` via `uh/f.G` before/alongside surface updates.
3. **AppStart (#10) before StartLuaAppDM (#12).** Harness order matches Q2's
   cold-start block, but `0x247dc8d` is known to reach the broken `0x233319a`
   object path; decision-014/decision-017 patches exist for it. On the default
   path it still returns via the patch rather than a faithful object.
4. **Getter type/fidelity (StartAppParams & StartGameParams).** `real_path_get`
   (`guest_app_shell.c:698-720`) returns a **jstring for `appUserId`** (real
   signature `appUserId()J` → must be `jlong`) and neutral `0` for `isUnder13()Z`
   and `membershipType()I`; values `"0"`, `"-- app starter --\n"`, `"1"`,
   `"SoberUser"`, `"default"`, `"https://www.roblox.com"` are invented
   (`analysis/lua-app-start.md` §4). Seven of the ten `StartAppParams` getters
   have no handler on the default path (`appStarterPlace`, `appStarterScript`,
   `appUserId`, `isUnder13`, `username`, `membershipType`, `selectedTheme`);
   the generic fallback returns a bare `java/lang/String`.
5. **Flag override currently ineffective.** Prior `realpath*.log` runs show
   `0x72cb648` still 0 after `0x2edd859` alone (`appstate_fields.md` addendum A).
   #6 (`0x2edd739`) is required to apply it, and #7 (`0x2383c18`) to create the
   singleton the flag=1 branch reads. Until the byte is verified 1, the entire
   delegate path is unreachable no matter what else is called.
6. **`nativeRegisterJavaFlagProvider` `0x2ecbcf4`** (export confirmed) has **no
   Java caller in the DEX** and is reached only indirectly by guest init; it is
   not called by the harness. Low priority, but note it if flag plumbing remains
   broken after #5–#7 (`analysis/lua-app-start.md` §1.3).

---

## 4. Minimal opt-in sequence proposed for lead

Design goals: (a) one knob, (b) every call a real guest export with faithful
argument objects, (c) **no** fabricated callback and **no** `0x25848e0` thunk,
(d) decision-013 proof is the guest-side notifier hit, not a harness assertion.

**Proposed knob:** `SOBER_REAL_JNI_SEQUENCE=1` — a *new* opt-in, deliberately
**not** added to the `seeded_state` withhold list (the lead must rule on this;
see §5). It replays, in this exact order, with these argument shapes:

| Step | RVA | Call | Required argument fidelity |
| - | - | - | - |
| 1 | `0x2edd859` | `nativePreloadFlagOverrides(String)` | jstring `{"EnableAppBridgeComponents":true}` (**JSON object**, not `key=value`) |
| 2 | `0x2edd739` | `nativeRetryInit()` | no args; then **verify byte at `0x72cb648 == 1`**; if 0, stop and report (do not fake the byte unless lead approves `SOBER_REAL_APPSTATE_FLAG` as a separately-labelled diagnostic) |
| 3 | `0x2465210` | `nativeInitializeNativeFlags(String[])` | real `String[]`, not the current 0x80 dummy |
| 4 | `0x2383c18` | `nativePostClientSettingsLoadedInitialization3(List)` | must be a guest-shaped native `List`; current synthetic `List` faults at `0x2e5d42a` — this is a known open gap (a faithful `java/util/List` object model is required) |
| 5 | `0x233762c` | `nativeGameGlobalInit()` | no args |
| 6 | `0x24abf96` | `nativeAppBridgeV2InitWithParams(InitParams)` | getters backed: `baseURL`, `userAgent`, `platformParams`, `deviceParams`, `isTablet`, `pioneerPlaceId`, `buildVariant`, `isVrDevice`, `isPotato` — register notifier slot, log `0x7234520`/`0x6dbd860` non-NULL |
| 7 | `0x2ebcf36` | `SendAppEventOnAppReady(String×4)` | `"AppShellNotifications","123456789","StartConversationWithUserId","Game"` (use `"Chat"` for the chat path; `""` is invalid) |
| 8 | `0x247dc8d` | `nativeAppBridgeAppStart(String,String,Z,String,String,String)` | only when the object path is fixed; otherwise skip (conditional in real app too) |
| 9 | `0x25448f6` | `nativeAppBridgeStartLuaAppDM()` | no args; verify `app+0x408` non-NULL via `SOBER_APP_SLOT_DIAG` logger |
| 10 | `0x26fe80f` | `nativeAppBridgeV2StartAppWithParams(StartAppParams)` | **all ten** getters with correct types (`surface` obj, `platformParams` obj, `vrContext` obj, `appStarterPlace` String, `appStarterScript` String, `appUserId` **jlong**, `isUnder13` **jboolean**, `username` String, `membershipType` **jint**, `selectedTheme` String), realistic values |
| 11 | `0x2773194` | `UpdateSurfaceAppWithPlatformParams(Surface,PlatformParams)` | real `PlatformParams` with `dpiScale`, `viewportWidthMm`, `viewportHeightMm` |
| 12 | `0x2eba91c` | `setTaskSchedulerBackgroundMode(Z,String)` | `(false,"sober-native")` |
| 13 | `0x2ebf950` | `nativeScheduleOnFirstFrame(Runnable)` | real Runnable with `run` handler |
| 14 | `0x2ebd51a` | `StartGameWithParam(StartGameParams)I` | getters `gameId`, `placeId`, `accessCode`, `username` with correct primitive/String types |
| 15 | `0x2ebca9e` | `SendAppEventOnGameLoaded(String×3)` | three strings per `uh/f.A` (app-shell notification category/id/action), **after** manager setup |
| 16 | observe | notifier `0x27eaf6e` | breakpoint/hit-counter at `0x27eaf6e` with `RSI==placeId` and `0x72a2fc0` non-NULL; return address `0x27eafb3` from thunk `0x25848e0` is the discriminator |

Minimal *critical* subset, if lead wants the smallest experiment first: steps
1,2,5,6,9,14,15 + the notifier probe — i.e. flags → GameGlobalInit →
InitWithParams (registers notifier) → StartLuaAppDM (delegate) →
StartGameWithParam (drives DataModel load) → SendAppEventOnGameLoaded → observe.
That is the shortest path that can legitimately enter `0x27eaf6e`. Steps 3,4,7,
10–13 add the app-shell *frame* fidelity required by decision 013's render half.

---

## 5. Decision-013 compatibility / acceptance evidence

- The new knob must be reconciled with the `seeded_state` OR-chain
  (`guest_app_shell.c:1334-1378`). Recommendation: keep it **out** of the list
  because it fabricates no callback and no guest bytes; instead require the
  **notifier-hit proof** at `0x27eaf6e` as the anti-fraud discriminator. If lead
  prefers all opt-ins to withhold, then these calls must eventually become the
  default path — a separate decision.
- A pass under decision 013 still requires **all** of (`analysis/game-loaded-path.md`
  §6): notifier entered at `0x27eaf6e` with `RSI=placeId`; bridge log
  `guest->Java callback: gameLoadedCallback`, `last='gameLoadedCallback'`,
  `callbacks` ≥ baseline+1; **no** `ALT PATH guest call 0x25848e0` / HONESTY
  NOTICE and `SOBER_ALT_CALLBACK_PATH` unset; phase-3 RVA `0x649d422` observed;
  all existing withhold knobs unset; engine SHA/BuildID unchanged.
- This report does not change the gate, does not emit `app shell rendered`, and
  does not propose calling the thunk. `onAppShellReloadNeeded` is dead and must
  not be used as a signal.

---

## 6. Limits / honesty

- Static analysis only. No runtime capture of `0x27eaf6e` being entered was
  performed here; steps 14–16 are a predicted ordering grounded in the observed
  registration at `0x24ad699` and the `SessionReporterState_GameLoaded` log
  semantics. The single largest unknown is whether steps 1–4 actually set
  `0x72cb648 = 1` and populate `0x72cb5e0` with faithful objects — prior
  experiments did **not** achieve this. Until verified live, the notifier path
  remains unproven.
- The exact relative order of the two app events (#7 AppReady, #15
  GameLoaded) is event-driven on the Java side; the order given is the
  startup-common case, not a rigid ABI contract.
- All other claims (RVA→export mapping, smali call sites, getter strings,
  callback globals, withhold semantics) are OBSERVED from the SHA-bound engine,
  decoded DEX, exports file, or harness source, and are cited above.
