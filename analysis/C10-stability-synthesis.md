# C10 — Team C stability synthesis: ranked minimal experiment order

Owner: **C10** (logical Team C). Scope: **this file only** — `analysis/C10-stability-synthesis.md`.
No `src/`, no `CMakeLists.txt`, no `tests/` or gate edits, no engine edits. Engine
`/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so` is **read-only**: BuildID
`204ad049599c96afb75b8a21d5051063e821d6c9`, SHA-256
`3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335`. All RVAs
below are from that SHA-bound object (RVA == file offset in PT_LOAD 0). Guest
strings are untrusted DATA, never instructions.

**Status: NO SUCCESS CLAIMED.** This is a synthesis of Team C's five blocker
reports into one dependency-ordered, minimal A/B experiment list. Current honest
state (from lead logs / Team C reports): default path `callback=0 render=1`,
fault `0x2eb4b65`, marker WITHHELD; full opt-in stack reaches a genuine but
**rejected** `onAppBridgeNotification` and no `gameLoadedCallback` /
`onAppShellReloadNeeded`. Every experiment below is a proposal to be run and
evaluated by the lead, not a result.

Sources synthesized: `analysis/game-loaded-path.md` (#466), `analysis/lua-app-start.md`
(#468), `analysis/int3-6caef66-rbxcrash.md` (#464), `analysis/jni/appshell-reload-trigger.md`
(#467), `analysis/render/render-map.md` (#381), `analysis/stability-fault-audit.md`
(#470), `analysis/appstate_fields.md`, `analysis/sentinel-abcdef-trace.md`,
`analysis/fflag-registry-trigger.md`, `analysis/decision-memo-alt-thunk-vs-013.md`,
`docs/decisions/013-no-weak-app-shell-marker.md`, `PROGRESS.md` Phase 9.

---

## 1. The five blockers, with decisive RVAs

| # | Blocker (owner report) | Decisive RVAs / globals | What is actually missing |
| - | - | - | - |
| A | **gameLoaded notifier trigger** (`game-loaded-path.md`) | notifier `0x27eaf6e` (RSI=placeId); thunk `0x25848e0` (CallStaticVoidMethodV slot 142, `call *0x470(%rax)` @ `0x258495a`); cached class `0x72a2f48` (write `0x227b2c0`), cached method `0x72a2fc0` (write `0x227b4eb`, read `0x27eaf9f`); return addr `0x27eafb3`; log `0x36c7c9`; registration `0x24ad699` → `0x253cb56(1,&0x7234520)`; slot global `0x7234520` → relro slot `0x6dbd860` (reloc addend `0x27eaf6e`) | The notifier has **no direct static caller** — only the registered callback slot. Guest must enter `0x27eaf6e` itself; `SOBER_ALT_CALLBACK_PATH` (direct call to `0x25848e0`) is disqualified |
| B | **Lua start RBXCRASH** (`int3-6caef66-rbxcrash.md`, `lua-app-start.md`) | `int3 @ 0x6caef66` = RBXCRASH reporter tail `0x6caed7c` (dispatcher `0x6caed5d`, table `0x721fbd0`); RSL panic `0x6c47c52`, tag literal `0x3dc475` ("257f718-Context,20"), assert handler global `0x727e910`; scheduler check `0x2a13315`/`0x2a13327`/`0x2a13329`; neutralize site `native_jni.c:1251-1261`, `p = guest+0x722e540`, `memset(p+0x28,0,16)` + `memset(p+0x70,0,8)`; Lua driver `0x25448f6` → `0x25449fc`, mode-4 start `0x24a8e44`, delegate writes `0x25d7e75`/`0x25f7e7e` | Zeroing `+0x70` crosses the scheduler liveness check and raises a fatal RSL panic; also the real Lua-app driver `0x25449fc` is never reached on the default path |
| C | **app-shell frame evidence** (`appshell-reload-trigger.md`, decision 013) | `onAppShellReloadNeeded` method `0x72a3020`: **write only** @ `0x227b432`, name `0x4a702d`, zero native readers → dead. `onAppBridgeNotification` `0x72a3028` → thunk `0x3dea91` = rejected plumbing. Gate: genuine callback **+** phase-3 render `0x649d422`, marker emit `guest_app_shell.c:1191` gated `shell_cb && render_ok` | No qualifying guest→Java callback observed; the only reachable one is `gameLoadedCallback` via blocker A |
| D | **render/surface binding** (`render-map.md`) | create_context `0x649c21e`; EGL init `0x649c272`; surface helper `0x649d1c2`; swap `0x649d422` → `eglSwapBuffers@plt 0x6d334d0`; render vtable `0x71912e8` ([3]@`0x7191300`=swap); `ANativeWindow_fromSurface 0x26fea78` → global `0x72a3308` (needs Java Surface — avoid); `nativeAppBridgeV2UpdateSurfaceAppWithPlatformParams 0x2773194` (never called) | Phase-3 swap `0x649d422` is already observed (`render_ok=1`), but the real surface/JNI binding step `0x2773194` and a Java-free native-window handoff are missing |
| E | **event order / fault chain** (`stability-fault-audit.md`, `appstate_fields.md`) | ctor[0] `0x2e62390` (@`0x7220e58`) → `0x2e618b0` → `0x2e61280`; registry `0x72a0520`+`0x38`; fault #1 `0x2e7b8e2` (helper `0x2e7b850`/`0x2e79af0`/`0x2e77400`, sentinel `0x72a0b88`, guards `0x72a0a30`/`0x72a0cd8`, ctrl blk `0x72a0cc0`/`0x72a0cd0`); fault #2 `0x24ad562` (`0x30ef840` NULL ← singleton `0x72cb5e0`, sole writer `0x238497d`, gate `0x6096928`); fault #3 `0x2772403` (callee `0x2772384` from `0x2771f6c` @`0x2772129`, controller `app+0x440`, installer `0x24ad50a` → `0x24ae1fe` via vtable `0x6dbc7f8`); next fault `0x2772161` (delegate `app+0x408`); game-path fault `0x31a1360` (NULL from `app+0x488`) | Three faults are one dependency chain; ctor replay/ordering, singleton population, and controller/delegate installation must happen in that order |

---

## 2. Why this rank (dependency, not importance)

```
E0 control                                  (invariants + strict-gate baseline)
  └─ E1 RBXCRASH / scheduler                 (hard fault gates the whole opt-in path)
       └─ E2 FFlag ctor replay + order       (fault #1 — prerequisite for singleton)
            └─ E3 app singleton 0x72cb5e0    (fault #2 — prerequisite for controller)
                 └─ E4 controller/delegate    (fault #3, then 0x2772161; app+0x488)
                      └─ E5 surface/render    (0x2773194 + 0x649d422 handoff)
                           └─ E6 notifier      (0x27eaf6e from guest dispatch)
                                └─ E7 gate     (decision-013 acceptance only)
```

Each fault is a strict prerequisite of the next, so a deeper experiment is
uninterpretable while a shallower one still faults. Per `stability-fault-audit.md`
§5: **change one variable per run**, default all opt-ins OFF; an A/B that changes
two at once is ambiguous. E5 is cheap and partly already green, but is kept
separate because it is a different subsystem (surface binding, not guest object
state).

---

## 3. Ranked minimal experiment order

### E0 — Control / invariants (no change)

- **RVAs checked:** engine SHA/BuildID (above); default fault `0x2eb4b65`; render
  swap `0x649d422`; gate log `callback=0 render=1`, `seeded_state=0`; marker file
  absent.
- **Procedure:** `verify-native.sh`, `ctest --test-dir build`,
  `first-frame.sh`, `bash tests/app-shell.sh` (expect rc=1, zero marker
  emissions, HONESTY NOTICE only if ALT path is set — it must not be).
- **Success (control valid):** SHA/BuildID unchanged, 2/2 ctest, `first frame
  rendered` with guest swap `0x649d422`, `app shell rendered` emitted **zero**
  times. **Failure:** any engine hash change, any marker emission, or any ctest
  regression → stop; do not interpret later A/Bs.

### E1 — RBXCRASH / scheduler (`int3 0x6caef66`), A/B

- **Knob:** `SOBER_SCHED_R70_MODE` (new, default OFF), in
  `sober_jni_neutralize_guest_scheduler()`. **Variant B (recommended):** keep
  `memset(p+0x28,0,16)` (breaks FindClass recursion) but **do not** zero `+0x70`;
  leave the guest ctor's packed nesting word (range `1..0x3f`). Variant C: seed
  `+0x70` from the guest initializer, never a magic constant.
- **RVAs:** `p = guest+0x722e540`; check `0x2a13315` (`test %r12,%r12`),
  `0x2a13327` (healthy jump), `0x2a13329` (panic tag), `0x2a13330`
  (`call *%rax` → `0x6c47c52`); sibling check `0x2a12eef`; assert global
  `0x727e910`; reporter `0x6caed7c` → `0x6caef66`; tag `0x3dc475`. Do **not**
  write crash-mode bytes `0x7cdd149` / `0x7cef650` (would only turn int3 into
  abort and is a forbidden byte write).
- **Success:** with variant B, SIGTRAP at `0x6caef67` and the line
  `RBXCRASH: FatalRuntimeError (RSL - panic: 257f718-Context,20)` are absent;
  the `FunctionMarshaller` task completes; the next genuine fault RVA (or first
  real guest→Java callback) is captured.
- **Failure:** int3 chain `0x2a0daf0→0x1e477ec→0x2a13332→0x6c47cd4→0x6caed7c`
  persists (neutralize still zeroing `+0x70`) → fix wiring; or FindClass
  recursion returns → apply the Bionic-compatible TSD shim from
  `jni/recursion-note.md` Addendum 2 (decision 013 root cause), still no byte
  patch. Current full-opt-in stack already reports "no scheduler neutralize" —
  confirm that state rather than assuming the durable fix landed.

### E2 — FFlag ctor replay + ordering (fault #1 `0x2e7b8e2`), A/B

- **Knob:** `SOBER_REPLAY_FFLAG_CTOR=1`; move `SOBER_JSON_APP_SINGLETON` block
  (`guest_app_shell.c:858-866`) to **after** the ctor replay block (`:875-882`);
  global-string seed stays earliest.
- **RVAs:** `0x2e62390` → `0x2e618b0` → `0x2e61280`; registry `0x72a0520`, its
  control block at `+0x38`; guards `0x72a0a30` (expect `0x101`), `0x72a0cd8`
  (expect `0x101`), control block `0x72a0cc0`/`0x72a0cd0` (non-NULL), sentinel
  `0x72a0b88`; fault `0x2e7b8e2` (`lock incq 0x8(%rax)`, `rax=0`, `si_addr=0x8`);
  helper `0x2e7b850` (narrow fallback: call with `rdi=0x72a0520+0x38`, do **not**
  store the sentinel by hand).
- **Success:** ctor replay log line appears **before** the postsettings line;
  `*(0x72a0520+0x38)` non-NULL; `0x2e7b8e2` no longer faults.
- **Failure:** no ctor log line and `+0x38 == 0` (ctor not invoked); or fault
  persists after reorder → log `0x72a0cd0`/`0x72a0cc0` and re-check ctor[0] was
  not skipped by DECISION 008 replay range `[2,3)+[5,3618)`.

### E3 — App singleton population (fault #2 `0x24ad562`), A/B

- **Knob:** previous + `SOBER_JSON_APP_SINGLETON=1`, `SOBER_NATIVE_OBJECTS=1`,
  FFlag `EnableAppBridgeComponents=1` (via JSON bridge setter `0x23648be`,
  name `0x43650d`, not a byte write). Do **not** use
  `SOBER_REAL_APPSTATE_POSTSETTINGS` with the bare zeroed `java/util/List`.
- **RVAs:** `0x2383c18` → `0x238491e` → `0x238497d` (sole write of singleton
  `0x72cb5e0`); gate `0x6096928` / byte `0x77979c0`; reader `0x30ef840`
  (`0x30ef844` gate, `0x30ef87c` returns 0 when singleton NULL); fault
  `0x24ad562` in installer `0x24ad50a` (`0x24ad531` flag test, `0x24ad547`).
  Harden `sober_native_java_list()` backing to guest allocator `0x1dbab02`
  with a real control block at `+0x30`, array at `+0x40`, count at `+0x48`
  (avoids destructor `0x2e5baa0` / retain `0x2e7b8d0` misreading host calloc).
- **Success:** `*(0x72cb5e0)` non-NULL and attributable to `0x238497d`;
  `0x30ef840` returns a real object; `0x24ad562` no longer faults.
- **Failure:** `0x30ef840` still returns 0 → check gate `0x6096928` and List
  shape; if gate false, this fault needs a different diagnostic.

### E4 — Controller `app+0x440` + delegate `app+0x408` (fault #3 `0x2772403`), A/B

- **Knob:** previous + remove `SOBER_SEED_USER_CONTROLLER` and
  `SOBER_SEED_APP_DELEGATE` (noop when guest-owned slot already non-NULL);
  replay `0x24abf96` (fully backed getters) then dispatch vtable
  `0x6dbc7f8 -> 0x24ad50a -> 0x24ae1fe` (store `app+0x440`); call
  `nativeAppBridgeStartLuaAppDM 0x25448f6` so `0x25449fc` (mode-4 `0x24a8e44`)
  installs the delegate via `0x25d7e75`/`0x25f7e7e`.
- **RVAs:** flag `0x72cb648` (must be 1); installer `0x24ad50a`, `0x24ad59e`
  → `0x30ef840`, shared_ptr at `this+0xb0` (`0x24ac5a9`/`0x24ac5c8`),
  `0x24ad5b6` → `0x24ae1fe`; userDidLogin `0x2771f6c` @`0x277211f`
  (`mov 0x440(%rbx),%rdi`), call `0x2772384` @`0x2772129`, fault `0x2772403`
  (`cmpl $0,0x140(%r14)` — key on the **callee's** rdi); next gate `0x2772161`
  on `app+0x408`; game-path fault `0x31a1360` (`rbx=NULL` from `app+0x488`;
  `rax=0xabcdef0123456789` is only the canary from GOT `0x72293f8`, not the
  cause).
- **Success:** `app+0x440` and `app+0x408` non-NULL, inside the guest heap and
  guest-written (no fake vtable); `0x2772403` gone; next genuine fault captured
  (expected `0x2772161`).
- **Failure:** fault persists → confirm guest-owned installer actually
  dispatched (no direct call exists); if `0x31a1360` blocks the game path, seed
  the missing `app+0x488` field with a guest object, not the stack-gu sentinel.

### E5 — Surface / render binding, A/B

- **Knob:** call `nativeAppBridgeV2UpdateSurfaceAppWithPlatformParams 0x2773194`
  once with a real `PlatformParams` carrying `dpiScale`/`viewportWidthMm`/
  `viewportHeightMm`; hand the native drawable to the guest without a Java
  Surface.
- **RVAs:** `0x2773194` (383-byte leaf; reads surface + PlatformParams);
  Route A `0x649c21e`(`native_window, extra!=0` to skip ANativeWindow_acquire)
  → `0x649c272` → `0x649d1c2` (`0x649d1e9` window surface) → `0x649d422`;
  Route B: harness 0x48-byte ctx with native EGLDisplay at `+0x20`, EGLSurface
  at `+0x28`, call `0x649d422` directly; swap → `eglSwapBuffers@plt 0x6d334d0`;
  vtable `0x71912e8` ([3]@`0x7191300`); GOTs `0x722a148`/`0x722a178`/`0x722a1a8`.
  **Do not** call `ANativeWindow_fromSurface 0x26fea78` (no Java Surface) or the
  §4 Java-callback thunks (no JVM).
- **Success:** guest-driven swap `0x649d422` observed with the bound surface;
  EGL status 0; `first frame rendered` / `render_ok=1` retained; engine SHA
  unchanged.
- **Failure:** no guest swap → fall back Route B; if `eglCreateWindowSurface`
  rejects the shim, report it (Route A is unproven) and keep Route B as the
  deterministic binding. Never emit `app shell rendered` from this experiment.

### E6 — Genuine gameLoaded notifier from guest dispatch (no thunk)

- **Knob:** replay the real opt-in call order (all real JNI exports, all opt-ins
  OFF at gate time; **do not** set `SOBER_ALT_CALLBACK_PATH` or
  `SOBER_GAME_LOADED_PATH` that faults at `0x31a1360`). Use the typed
  `StartAppParams` builder `sober_startapp_params_make` (guest vptr
  `0x6dbca20`, parser `0x26feb9b`) for all ten getters; JSON override must be
  the object form `{"EnableAppBridgeComponents":true}`.
- **Order (RVAs):** `0x2edd859` → `0x2edd739` (verify `0x72cb648==1`) →
  `0x2383c18` (E3) → `0x2eba91c` → `0x233762c` → `0x2ebcf36`
  ("AppShellNotifications","<id>","StartConversationWithUserId","Chat") →
  `0x24abf96` (registers notifier: `0x24ad699` → `0x253cb56(1,&0x7234520)`,
  slot `0x6dbd860`) → `0x247dc8d`/`0x24a8e44` → `0x26fe80f` → `0x25448f6`
  (E4) → `0x2773194` (E5) → `0x2ebd51a` (StartGameWithParam) →
  `0x2ebca9e` (SendAppEventOnGameLoaded, after manager setup) → observe
  `0x27eaf6e` → `0x25848e0` → `gameLoadedCallback(J)V`.
- **Success (guest-side proof, all required):** breakpoint/hit-counter at
  `0x27eaf6e` with `RSI == placeId`, return address `0x27eafb3`, and
  `0x72a2fc0` non-NULL; bridge log `guest->Java callback: gameLoadedCallback`,
  `last='gameLoadedCallback'`, `callbacks` increased ≥1 over pre-step baseline;
  **no** `ALT PATH guest call 0x25848e0` / `HONESTY NOTICE` line.
- **Failure:** no hit at `0x27eaf6e` → re-check registration `0x24ad699`
  executed and getter-slot `0x6dbd860` non-NULL; hit but no bridge callback →
  check cached method `0x72a2fc0`; only `onAppBridgeNotification` → JNI plumbing,
  not proof. `onAppShellReloadNeeded` (`0x72a3020`) must **not** be chased — it
  has no native reader (only write `0x227b432`).

### E7 — Strict decision-013 gate acceptance (verification only, no fix)

- **RVAs checked:** notifier proof `0x27eaf6e` (E6) **and** render `0x649d422`
  (E5); marker emit `guest_app_shell.c:1191` reachable only via
  `shell_cb && render_ok`.
- **Procedure:** re-run E0 invariants with **every** guest-mutating opt-in unset
  (`seeded_state==0`), `SOBER_ALT_CALLBACK_PATH` unset, no `HONESTY NOTICE`, no
  `onAppBridgeNotification` counted as `shell_cb`. Keep all withhold knobs.
- **Success (marker may legitimately be emitted):** guest-driven
  `0x27eaf6e`/`0x25848e0` callback **plus** guest swap `0x649d422`, with zero
  opt-ins, zero ALT-path lines, zero synthetic state, engine SHA unchanged.
- **Failure (correct state until then):** gate WITHHELD, `callback=0 render=1`,
  zero marker emissions. A green produced by re-enabling the ALT thunk is a
  **false green** and must be rejected (decision 013 / 037, memo Options B+C),
  as is any count from `onAppBridgeNotification` or any seeded guest state.

---

## 4. Cross-cutting constraints (apply to every E1–E7)

1. **Decision 013 is strict and unchanged.** Genuine guest→Java callback from
   the real path + phase-3 render `0x649d422`. No relaxation, no fake vtable,
   no direct thunk `0x25848e0` from the harness, no byte write, no
   `onAppBridgeNotification` as proof, no `onAppShellReloadNeeded` (dead).
2. **One new opt-in variable per run**; default path stays byte-for-byte the
   same (fault `0x2eb4b65`, phase-2/3 markers present, no marker emission).
3. **Engine is read-only** — re-check SHA-256/BuildID before and after every run.
4. **No success claim** until E7 passes with all opt-ins off; until then the
   marker stays WITHHELD and `app-shell.sh` remaining red is the correct result.
5. Lead owns `src/` and `CMakeLists.txt`; C10 makes no source edits — all
   knobs/patches above are proposals for the lead or the owning worker.
