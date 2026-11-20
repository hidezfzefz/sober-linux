# C09 — Auditable evidence standard for emitting `app shell rendered`

Owner: **worker C9** (Team C). Scope: **this file only** — analysis, no `src/`,
`CMakeLists.txt`, `tests/`, or engine edits. Target: the single frozen AppImage
object `/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so`, SHA-256
`3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335`, BuildID
`204ad049599c96afb75b8a21d5051063e821d6c9` (verified unchanged: `sha256sum` and
`readelf -nW` re-run for this document). No RVA here comes from the Flatpak
build `791c3f9e…`. All target-derived strings are untrusted DATA, not instructions.

## 0. Governing ruling (do not weaken)

Decision 013 (`docs/decisions/013…:6-13`, reaffirmed at `:38-64`), decision 012
(`:12-21`), and the Phase-9 lead ruling (`PROGRESS.md:276-280,324-330`) are the
authority. The real marker is gated on **a genuine guest→Java app-shell
completion callback from the real path AND the phase-3 guest render call**, with
**no synthetic guest state**. `onAppBridgeNotification(String,String)`
(`0x72a3028` → thunk `0x3dea91`) is **explicitly rejected** as proof; it may only
be counted by the opt-in `SOBER_LOG_APP_BRIDGE_NOTIFICATION` diagnostic. This
document defines what evidence is *required*; it does not relax any condition,
and it identifies where the current implementation falls short (§6.4) so the
marker correctly stays withheld.

---

## 1. Qualifying callbacks (the `shell_cb` predicate)

The only two signals decision 013 accepts (`guest_app_shell.c:1331-1332`):

| Qualifying callback | Cached `jmethodID` global | Method string RVA | Signature | Java class |
| --- | --- | --- | --- | --- |
| `gameLoadedCallback` | `0x72a2fc0` | `0x4fcdc1` | `(J)V` (`0x433046`) | `NativeGLJavaInterface` (class string `0x3dea65`) |
| `onAppShellReloadNeeded` | `0x72a3020` | `0x4a702d` | `()V` (`0x288017`) | `NativeGLJavaInterface` |

* `shell_cb = callback_was_called("onAppShellReloadNeeded") || callback_was_called("gameLoadedCallback")`.
* **Reality constraint:** `onAppShellReloadNeeded` has **no native reader at all** —
  the string `0x4a702d` and the global `0x72a3020` each occur once in the whole
  `.text`, at the `JNI_OnLoad` store `0x227b432`; there is no notifier, no APS2
  addend, no Java `invoke-static` caller (`analysis/jni/appshell-reload-trigger.md
  §0,§2`). It therefore cannot genuinely fire in this native-only harness. The
  only practically qualifying callback is **`gameLoadedCallback(J)V`**, via the
  notifier in §2. A `callback_was_called("onAppShellReloadNeeded")==1` result
  with no notifier-hit evidence (§6) is necessarily harness-fabricated and must
  be disqualified.

**Explicitly disqualified** (must never set `shell_cb`): `onAppBridgeNotification`
(`0x72a3028`/`0x3dea91`), `gameDidLeave` (`0x72a2fa0`), `run` / first-frame task,
`surface()` / `platformParams()` / `vrContext()` and the other StartAppParams
getters, and any callback delivered only because a diagnostic seed/thunk was
invoked. Naming any of these in the gate log does not qualify.

---

## 2. Guest RVAs that must be observed for a qualifying callback

All RVAs below are relative to the guest load base; `RVA == file offset` in the
first `R E` PT_LOAD. Evidence: REA `ev_d838d804…` (full `.text`), `ev_af5de532…`
(identity), `ev_34eea014…` (sections), plus local `objdump`/raw-byte reads on the
SHA above; see `analysis/callback/callback-map.md`, `analysis/game-loaded-path.md`,
`analysis/jni/jni-map.md`.

### 2.1 Cached-ID setup (`JNI_OnLoad` 0x227af5a)
* `0x227b2c0` — store `NewGlobalRef(FindClass("…/NativeGLJavaInterface"))` → class global `0x72a2f48`.
* `0x227b4eb` — `GetStaticMethodID(gameLoadedCallback,(J)V)` → method global `0x72a2fc0`.
* `0x227b432` — `GetStaticMethodID(onAppShellReloadNeeded,()V)` → method global `0x72a3020` (store only; see §1).
* Both `0x72a2f48` and `0x72a2fc0` must be **non-NULL at call time** — verified live, not merely seeded by a diagnostic.

### 2.2 Lifecycle registration (proves the notifier is wired into guest dispatch)
* `nativeAppBridgeV2InitWithParams` `0x24abf96` executes `lea rsi,[0x7234520]` /
  `call 0x253cb56` at `0x24ad699` — registers the GameLoaded/GameLeave family.
* APS2: `off 0x6dbd860, addend 0x27eaf6e` materialises the notifier pointer in
  relro slot `0x6dbd860`; `off 0x7234520, addend 0x6dbd860` makes
  `*(0x7234520) == &slot(0x6dbd860) == &notifier`. At runtime
  `slot 0x6dbd860` must equal `base + 0x27eaf6e`.
* Note `0x6dbbfc8`/addend `0x27eaf4c` is a sibling trampoline, **not** the GameLoaded notifier, and `0x6dbd780..0x6dbd940` is the surrounding family; do not confuse them.

### 2.3 The GameLoaded notifier (the discriminator)
* `0x27eaf6e` — notifier entry. `RSI = placeId` (a **nonzero** `jlong`), `rbx=rsi`.
* `0x27eaf93` — `call 0x227b9e2` obtains `JNIEnv*` into `[rbp-0x30]`.
* `0x27eaf98` — loads class global `0x72a2f48` into RSI.
* `0x27eaf9f` — loads method global `0x72a2fc0` into RDX.
* `0x27eafac` — `xor eax,eax` (AL=0).
* `0x27eafae` — `call 0x25848e0` (thunk); **returns to `0x27eafb3`**. This return
  address is the hard discriminator (§6.2).
* `0x27eafb8..0x27eafd0` — `__android_log_print(3, "rbx.jni", "onGameLoaded() SessionReporterState_GameLoaded placeId:%lld", placeId)`; the only textual xref to that string in the 1.1 GB disassembly.
* `0x27eafd9` — `call 0x227bdd8` detach/release env.
* The notifier has **no static caller** (`grep "27eaf6e" libroblox.dis` = the label
  only); it is entered solely through the registered dispatch slot from §2.2.

### 2.4 The thunk and JNIEnv call
* `0x25848e0` — generic variadic thunk: builds SysV `va_list` (`gp_offset=0x18`,
  `fp_offset=0x30`, `movabs $0x3000000018` at `0x258494a`) and issues
  `call *0x470(%rax)` at `0x258495a` = JNI global slot 142 / JNIEnv offset `0x470`
  = `CallStaticVoidMethodV` (bridge: `native_jni.c:607,837`).
* `RDI` is `SREnvObj*` (first field = function table), `RSI` = class, `RDX` =
  method, `RCX` = the single `jlong` placeId vararg, `AL=0`.

### 2.5 Java→native introducers that must have run (real path, no thunk injection)
`nativePreloadFlagOverrides 0x2edd859` + `nativeRetryInit 0x2edd739` (flag byte
`0x72cb648 == 1`), `nativeAppBridgeV2InitWithParams 0x24abf96` (§2.2),
`nativeAppBridgeAppStart 0x247dc8d`, `nativeScheduleOnFirstFrame 0x2ebf950`,
`nativeAppBridgeStartLuaAppDM 0x25448f6`, `nativeAppBridgeV2StartAppWithParams
0x26fe80f`, `nativeAppBridgeV2StartGameWithParam 0x2ebd51a`,
`nativeAppBridgeV2SendAppEventOnGameLoaded 0x2ebca9e`,
`nativeAppBridgeV2UpdateSurfaceAppWithPlatformParams 0x2773194`. Exact order and
state preconditions: `analysis/game-loaded-path.md §4-§5`.

### 2.6 Excluded RVAs — any of these at the dispatch invalidates the callback
* `SOBER_ALT_CALLBACK_PATH` calls the thunk directly:
  `guest_app_shell.c:1316-1324` executes `call base+0x25848e0` and logs
  `ALT PATH guest call 0x25848e0`; its return address is host harness code near
  `guest_app_shell.c:1319-1323`, never `0x27eafb3`. This path also skips
  `nativeAppBridgeAppStart` (`ALT PATH: skipping 0x247dc8d`) — another tell.
* Any direct call to `0x25848e0` from harness C, or from a seeded diagnostic
  (decisions 013/037). No `call 0x25848e0` from host code is acceptable proof.

---

## 3. Render evidence (`render_ok`)

1. `sober_render_phase3_done() == 1` — the **persisted** flag
   (`native_render.c:960-963`), set by a successful native swap (`:831`, `:945`)
   or by the guest-PLT hook `sr_hook_egl_swap_buffers` (`:1009-1017`). It stays
   valid after shutdown, unlike `sober_render_get_active()`.
2. Guest render entry point `0x649d422` (vtable slot `[3]` at `0x7191300`,
   `jmp eglSwapBuffers@plt 0x6d334d0`, GOT `0x722a1a8`) must have been executed,
   evidenced by the guest-swap counter `g_guest_swaps > 0` and the log line at
   `guest_render.c:99-103`:
   `[render] guest call 0x649d422 swap(...) -> rc=<rc> | guest-driven swaps observed: N`
   with `N >= 1` and `rc >= 0`. The GOT-hook install line
   (`[render] installed guest-swap counter hook at GOT 0x722a1a8`) must be present.
3. Phase-2/3 ancestry markers present: `guest init complete` and `first frame
   rendered` (`guest_dlopen_host.c:650`, `guest_render.c:208`).
4. Engine SHA-256 / BuildID unchanged (§0), `sober-native` ldd clean (libc only),
   phase-2/3 checks green (`tests/verify-native.sh`, `ctest 2/2`).

**Honest limit:** the harness currently *causes* the `0x649d422` swap itself
(Route B, `guest_render.c:90-107`); this proves a real guest render entry point
ran, **not** that the app-shell state machine presented a frame. Render evidence
is therefore necessary but never sufficient — the qualifying callback (§1-§2)
and guest-driven proof (§6) are what make the conjunction meaningful. This
conjunction is exactly decision 013; do not accept render alone.

---

## 4. Absence of withhold knobs (`seeded_state == 0`)

Authoritative list = the OR-chain in `guest_app_shell.c:1337-1375` (the older
"eight knobs" in `decision-memo-alt-thunk-vs-013.md §2.3` is superseded by the
current source). **All 37 must be unset**, plus `SOBER_ALT_CALLBACK_PATH` unset:

`SOBER_SEED_USER_CONTROLLER`, `SOBER_SEED_APP_DELEGATE`, `SOBER_SEED_APP_488`,
`SOBER_SEED_JNI_VM`, `SOBER_FAITHFUL_JAVAVM`, `SOBER_NATIVE_OBJECTS`,
`SOBER_JSON_FLAG_BRIDGE`, `SOBER_JSON_APP_SINGLETON`, `SOBER_SEED_GLOBAL_STRING`,
`SOBER_GSEED_FORCE`, `SOBER_GSEED_DIAG`, `SOBER_REPLAY_FFLAG_CTOR`,
`SOBER_NATIVE_FLAGS_INIT`, `SOBER_BIONIC_SF_FULL`, `SOBER_SCHED_R70_KEEP`,
`SOBER_SCHED_NEST_ONE`, `SOBER_LOG_APP_BRIDGE_NOTIFICATION`,
`SOBER_GAME_LOADED_PATH`, `SOBER_LUA_APP_START`, `SOBER_STARTAPP_PARAMS`,
`SOBER_APP_SINGLETON_BRIDGE`, `SOBER_REAL_APPSTATE_PATH`,
`SOBER_SEED_JNI_SINGLETON`, `SOBER_REAL_APPSTATE_FLAG`,
`SOBER_REAL_APPSTATE_POSTSETTINGS`, `SOBER_TLS_SEED`, `SOBER_STUB_TSD_SEED`,
`SOBER_HELPER_REPAIR_V2`, `SOBER_NO_SCHED_NEUTRAL`, `SOBER_VALIDATOR_PATCH`,
`SOBER_FINAL_PATCH`, `SOBER_FIELD_NORMALISER`, `SOBER_OBJECT_FIX`,
`SOBER_ALLOC_SITE_FIX`, `SOBER_LOG_HANDLE`, `SOBER_APP_SHELL_FIX`,
`SOBER_CALL_SITE_FIX`.

Rationale: each manufactures guest state, patches guest bytes, or invokes a
guest thunk from the host; none can prove a login/app-shell frame. Any one set
forces `shell_cb = 0` at `guest_app_shell.c:1376-1380` and logs
`marker withheld: diagnostic guest-state seed active`. The audit must confirm
the **absence** of that line, not just the callback count. Note the wait knobs
`SOBER_APP_SHELL_WAIT_MS` / `SOBER_WINDOW_HOLD_MS` are timing-only and are not in
the withhold chain; they may be used for log collection.

---

## 5. Required and forbidden log evidence

**Must be present in the passing run's stderr/stdout:**
1. `[app-shell] guest->Java callback: gameLoadedCallback` (or
   `onAppShellReloadNeeded`) — emitted by `shell_callback`
   (`guest_app_shell.c:130-136`) for the qualifying callback, **and** corroborated
   by §6 evidence (the name line alone is insufficient).
2. `[app-shell] callback count X -> Y last='gameLoadedCallback'` (wait loop,
   `:1298-1309`) and/or `[app-shell] wait complete: callbacks=… last='gameLoadedCallback'`
   — a count delta ≥ 1 attributable to the qualifying callback.
3. `[app-shell] env_calls=… callbacks=… register_natives=… last=gameLoadedCallback`
   from `sober_jni_describe_activity` (`:1326-1327`, `native_jni.c:1443-1454`).
4. Notifier-hit and return-address evidence per §6 (capture line with the exact
   return address `base+0x27eafb3`, `RSI=placeId`, method global `0x72a2fc0` non-NULL).
5. The §3 render lines (GOT-hook install, `0x649d422` swap with N≥1) and §3 phase-2/3 ancestry markers.
6. Exactly one `app shell rendered` token (stdout via `emit_marker`, `:1409`; plus
   the `SOBER_SHELL_MARKER` file if set).

**Must be absent (any one present ⇒ run is not valid proof):**
* `ALT PATH guest call 0x25848e0`, `ALT PATH: gameLoadedCallback method=…`,
  `ALT PATH: skipping 0x247dc8d` (`:1159,1318,1322`);
* `HONESTY NOTICE: SOBER_ALT_CALLBACK_PATH …` (`:1405-1407`);
* `marker withheld: callback=0 render=1` (`:1386-1392`) — the current default state;
* `marker withheld: diagnostic guest-state seed active` (`:1377-1378`);
* `DIAGNOSTIC ONLY (no marker)` (`:1394-1395`);
* any `onAppBridgeNotification` line used as the qualifying callback: the only
  permitted occurrence is the opt-in `SOBER_LOG_APP_BRIDGE_NOTIFICATION`
  diagnostic handler, which cannot satisfy §1.

---

## 6. Proving the callback is guest-driven, not harness-triggered

The central audit question: did the guest's own state machine deliver
`gameLoadedCallback`, or did host code call the thunk? A per-name counter is
**not** a discriminator (the ALT path increments the same counter). Require the
following, jointly:

### 6.1 Notifier-hit evidence (guest entered the notifier itself)
A breakpoint / hit-counter / instrumentation at `0x27eaf6e` that records at
least one hit with:
* `RSI == placeId`, a **nonzero** `jlong` (ALT path passes `0,0,0` → placeId 0);
* cached method global `base+0x72a2fc0` **non-NULL** and cached class
  `base+0x72a2f48` non-NULL at entry;
* the hit occurs **after** registration `0x24abf96 → 0x253cb56(1,&0x7234520)` at
  `0x24ad699` (§2.2), and the slot `0x6dbd860 == base+0x27eaf6e`.

Because the notifier has no static caller, a hit at `0x27eaf6e` can only come
from the guest's own registered dispatch. This is the primary proof.

### 6.2 Return-address evidence (the thunk was entered from the notifier)
At the JNI dispatch that receives the call — `sr_CallStaticVoidMethodV`
(`native_jni.c:607-611`) and `sr_dispatch_call` (`native_jni.c:474-516`) — capture
the immediate caller return address (e.g. `__builtin_return_address(0)`, plus a
1–2 frame stack walk) and the `jmethodID`. A guest-driven `gameLoadedCallback`
must show:
```
thunk 0x25848e0  ->  CallStaticVoidMethodV (JNIEnv+0x470)
return address   ==  base + 0x27eafb3    # instruction after `call 0x25848e0` in the notifier
stack frames     :   0x27eaf6e (notifier) -> 0x25848e0 (thunk) -> sr_* (bridge)
```
The ALT path instead returns to a **host** address in `sober-native` near
`guest_app_shell.c:1319-1323`, with no `0x27eaf6e` frame. For
`onAppShellReloadNeeded`, no guest notifier exists at all (§1), so any observed
hit must show a guest return address from a real notifier or be disqualified.

### 6.3 Argument and temporal corroboration
* Dispatch `name == "gameLoadedCallback"`, `sig == "(J)V"`, single `jlong` arg
  equal to the `RSI` captured at the notifier hit (§6.1), and `placeId != 0`.
* Callback count delta (§5.2) occurs **after** the introducer calls `0x2ebd51a`
  and `0x2ebca9e` (§2.5), not before them or at bridge creation.
* `rbx.jni / onGameLoaded() SessionReporterState_GameLoaded placeId:%lld`
  guest log emitted around the same time (§2.3).

### 6.4 Current implementation gap — why the marker must stay withheld today
`sr_dispatch_call` records only `last_callback` by name, `callback_calls`, and a
per-handler `h->count` (`native_jni.c:486,502-504`); `sober_jni_callback_was_called`
only checks `h->count > 0` (`:1432-1441`). It captures **no return address, no
notifier-hit, no stack frame, and no argument value**. Consequently the current
gate cannot distinguish a guest-driven callback from the ALT-thunk callback, and
`guest_app_shell.c:1331-1332` alone cannot satisfy §6. The honest status is
therefore **`callback=0` / marker withheld** (matches the observed default gate
log `marker withheld: callback=0 render=1 … last='isVrDevice'` and Phase-9
state: only `onAppBridgeNotification` was ever counted). Adding notifier-hit and
return-address capture would be a **code change requiring an explicit lead
decision**; this document neither makes nor requests that change. Until such
evidence exists, no run can meet this standard and the gate must not pass.

---

## 7. Audit checklist (all conditions are ANDed; any fail ⇒ withhold)

| # | Condition | Evidence anchor |
| --- | --- | --- |
| 1 | Engine SHA/BuildID unchanged; Flatpak build unused | §0 |
| 2 | Qualifying callback is `gameLoadedCallback(J)V` (or a genuinely-firing `onAppShellReloadNeeded()V`); `onAppBridgeNotification` not counted | §1 |
| 3 | Cached class `0x72a2f48` and method `0x72a2fc0` non-NULL at call time | §2.1 |
| 4 | Lifecycle family registered: `0x24abf96`→`0x253cb56` at `0x24ad699`; slot `0x6dbd860 == base+0x27eaf6e` | §2.2 |
| 5 | Notifier `0x27eaf6e` hit with `RSI=placeId≠0` | §6.1 |
| 6 | Thunk return address `base+0x27eafb3`; guest stack frames present | §6.2 |
| 7 | Dispatch name/sig/arg match, arg equals notifier placeId, delta after introducers | §6.3 |
| 8 | `0x649d422` executed; `g_guest_swaps ≥ 1`; `sober_render_phase3_done()==1`; `first frame rendered` | §3 |
| 9 | All 37 withhold knobs + `SOBER_ALT_CALLBACK_PATH` unset; no seed-withhold log | §4 |
| 10 | Present logs per §5.1; forbidden logs per §5.2 absent | §5 |
| 11 | Phase-2/3 checks green, ldd libc-only, exactly one marker emitted | §3, §5 |

Current status: **conditions 5, 6, 7 unproven (§6.4); condition 2 observed only as
the rejected `onAppBridgeNotification`; default path also fails 8-as-proof
(§3 limit).** The gate is correctly closed; no `app shell rendered` may be emitted.

---

## 8. Limits / honesty

* This is an evidence **standard**, not a claim that the marker can currently be
  earned. Static analysis + source review only; no runtime notifier hit or
  return-address capture was performed here.
* Steps §2.5 are a predicted real-path ordering; the exact Java event strings and
  StartGameParams getter values must still be verified live.
* The notifier's dispatch via `0x7234520 → 0x6dbd860` is proven as data-flow
  (relocations + xrefs, `game-loaded-path.md §2`), but the precise virtual call
  consuming the slot was not isolated; treat the
  `SessionReporterState_GameLoaded → notifier` arrow as strongly supported, not
  a direct-call proof.
* `onAppBridgeNotification`, harness-triggered thunks, and all phase-7/8/9
  opt-ins remain disqualified by the lead's ruling and decision 013. This
  document proposes **no** weakening of the gate.
