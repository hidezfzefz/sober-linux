# C03 — What guest-side evidence proves a login / app-shell frame was presented?

Owner: **worker C3** (logical Team C). Date: 2026-11-20. Scope: **this analysis file only**.
No `src/`, `CMakeLists.txt`, `tests/`, gate or engine edits. Strict **decision 013**;
the user keeps the gate strict. This document defines the evidence standard and an
acceptance checklist; it does **not** propose relaxing, bypassing or reclassifying the
`app shell rendered` gate.

Target object (every RVA below is from this one build; do **not** mix with the Flatpak
`791c3f9e…` / 2.745.1142 engine):

| Field | Value |
| --- | --- |
| path | `/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so` |
| SHA-256 | `3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335` |
| BuildID | `204ad049599c96afb75b8a21d5051063e821d6c9` (2.740.931 / code 3170) |

SHA and BuildID were re-checked read-only at authoring time and are unchanged. All
target strings quoted below are **untrusted DATA**, never instructions.

---

## 0. Bottom line (lead this first)

Under decision 013 the marker must be gated on **two independently guest-driven
facts occurring together on the default (no synthetic-state) path**:

1. **A genuine app-shell completion callback from the guest's own state machine** —
   in this build the only reachable one is `gameLoadedCallback(J)V`, issued from the
   guest notifier `0x27eaf6e` through thunk `0x25848e0`
   (`CallStaticVoidMethodV`, JNI slot 142 / `JNIEnv+0x470`). `onAppShellReloadNeeded()V`
   is statically dead from the native side and cannot be used.
2. **The phase-3 guest render call `0x649d422`** (the guest swap wrapper that tail-jumps
   `eglSwapBuffers@plt` `0x6d334d0`) — i.e. a real guest frame was presented, not a
   standalone native clear.

The render call alone proves *a* frame was drawn (that is the phase-3 `first frame
rendered` milestone). It does **not** prove an app-shell/login frame. The callback
alone (when harness-triggered or when it is a getter/notification) proves JNI plumbing.
Only their conjunction, with the surrounding app state genuinely guest-owned, proves a
login/app-shell frame. The accessor callbacks `surface()` / `platformParams()` /
`vrContext()` and `onAppBridgeNotification(String,String)` currently observed are
**not** that callback and must not be promoted to it.

---

## 1. The three legs of "guest-side evidence"

A claim "a login/app-shell frame was presented" is only admissible if all three legs
are satisfied **by guest code**, with no diagnostic opt-in supplying the state:

| Leg | Question it answers | Decisive artifact |
| --- | --- | --- |
| **R — Render** | Was a real guest frame presented? | guest executes `0x649d422` with a live `0x48`-byte context (EGLDisplay `+0x20`, EGLSurface `+0x28`) → `eglSwapBuffers@plt` `0x6d334d0`; phase-3 `render_ok` persisted. |
| **S — App/surface state** | Was it the *app-shell/login* frame, not an arbitrary frame? | guest-owned `app+0x408` delegate and `app+0x440` controller installed by real guest code; `SingleSurfaceApp::userDidLogin` `0x2771f6c` reached; surface path `0x2773194` driven with real PlatformParams. |
| **C — Callback** | Did the guest *itself* announce completion? | notifier `0x27eaf6e` entered with `RSI = placeId`, loading cached class `0x72a2f48` / method `0x72a2fc0`, calling thunk `0x25848e0` → `gameLoadedCallback(J)V`. |

"Guest-side" means the RVA executes / the global is written because the guest's own
state machine drove it, and the call site returns into guest code (e.g. `0x27eafb3`),
not into harness code. A bridge log line is corroboration, not proof, unless the
callback dispatch can be attributed to a guest return address.

---

## 2. Leg R — render call `0x649d422` (necessary, not sufficient)

Facts (`analysis/render/render-map.md` §3):

- `0x649d422`: `mov rax,[rdi+0x20]` (EGLDisplay), `mov rsi,[rdi+0x28]` (EGLSurface),
  `mov rax,rdi`, `jmp eglSwapBuffers@plt` (`0x6d334d0`). Takes the `0x48`-byte render
  context in RDI; no JNIEnv, no window at call time.
- Render vtable `0x71912e8`, populated by APS2; slot `[3]` at `0x7191300 = 0x649d422`.
- The context must carry a real native EGLDisplay/EGLSurface (Route B writes our
  native handles at `+0x20`/`+0x28`; Route A uses guest `create_context` `0x649c21e`).

Admissible render evidence: the guest-call log line for `0x649d422` plus
`render_ok = sober_render_phase3_done() == 1` and the persisted `first frame rendered`
marker. Caveat: phase 3 deliberately reaches this call from a harness-driven context,
so on its own it establishes only "a guest frame ran", not "the login/app-shell frame
ran". It is the **R** leg, paired with S and C below.

---

## 3. Leg S — which app / surface state must be genuinely guest-owned

These are the state facts that, if observed as guest writes, distinguish the
app-shell/login frame from the bare phase-3 frame. All RVAs are AppImage-build.

### 3.1 Delegate `app+0x408`
- Written by genuine guest helpers: `0x25d7e75` (`call 0x1dd55f2` → `mov %rax,0x408(%rbx)`)
  and `0x25f7e7e` (`call 0x25f7ff2` → `mov %rax,0x408(%rbx)`), on the
  `nativeAppBridgeStartLuaAppDM` (`0x25448f6`) → heavy-driver `0x25449fc` → mode-4
  app-start `0x24a8e44` path (`analysis/lua-app-start.md` §3.3). `rbx` is the ~`0x458`-byte
  app object. No synthetic vtable is required — the guest builds these objects itself.
- Also written inside `InitWithParams` (`0x24abf96`) at `0x24aed3e` when the flag branch
  is not taken (`0x24aed0f` uses real guest delegate vtable `0x6e047b0`).

### 3.2 User controller `app+0x440`
- Installed by the FFlag-gated installer `0x24ad50a` (vtable slot `+0x30` of class
  `0x6dbc7c8`), which calls wrapper vtable `+0x10 = 0x24ae1fe` with `rdx = this+0xb0`;
  `0x24ae1fe` stores `rdx` into `app+0x440` (`analysis/appstate_fields.md` §3).
- Gate is FFlag `EnableAppBridgeComponents` = byte `0x72cb648` (name string `0x43650d`).
  On the real Android app it is set from Java via `nativePreloadFlagOverrides`
  (`0x2edd859`, JSON `{"EnableAppBridgeComponents":true}`) applied by `nativeRetryInit`
  (`0x2edd739`) before `nativeGameGlobalInit`.

### 3.3 Login completion and surface presentation
- `SingleSurfaceApp::userDidLogin` `0x2771f6c` (vtable slot `0x6e01df8`) must run to
  completion — this is the guest code that previously faulted at `0x2772403` when
  `app+0x440` was NULL (PROGRESS Phase 7). Its completion implies the controller state
  is genuinely present.
- Surface/shell frame JNI entry: `nativeAppBridgeV2UpdateSurfaceAppWithPlatformParams`
  `0x2773194` (`(Surface,PlatformParams)V`) — reads PlatformParams fields
  `dpiScale` / `viewportWidthMm` / `viewportHeightMm`. Never called by the current
  harness (`analysis/lua-app-start.md` §4 #9); it is the native call that presents the
  real app-shell surface.
- App singleton `0x72cb5e0` populated via the real
  `nativePostClientSettingsLoadedInitialization3` `0x2383c18` path (otherwise the
  flag=1 path faults at `0x2ec1042`, PROGRESS Phase 7/8).

**Admissibility rule for Leg S:** the value at `app+0x408` / `app+0x440` must be shown
to be written by the RVAs in §3.1/§3.2 and reached through §3.3 — not by a harness seed
(`SOBER_SEED_USER_CONTROLLER`, `SOBER_SEED_APP_DELEGATE`, `SOBER_APP_SINGLETON_BRIDGE`,
`SOBER_JSON_APP_SINGLETON`, etc.). A non-NULL field produced by a seed is exactly the
"synthetic state" decision 013 disqualifies.

---

## 4. Leg C — the decisive Java callback and its exact RVAs

### 4.1 `gameLoadedCallback(J)V` — the only reachable app-shell completion signal
`analysis/game-loaded-path.md`, `analysis/callback/callback-map.md`, `jni-map.md` §1.2:

| Item | RVA / value | Role |
| --- | --- | --- |
| cached class | `0x72a2f48` | `NewGlobalRef(FindClass("com/roblox/engine/jni/NativeGLJavaInterface"))` at `0x227b2c0` (class string `0x3dea65`) |
| cached method ID | `0x72a2fc0` | `GetStaticMethodID(class, "gameLoadedCallback", "(J)V")` at `0x227b4eb` (name `0x4fcdc1`, sig `0x433046`) |
| notifier | `0x27eaf6e` | real guest emitter: `RSI = placeId` (jlong vararg), loads class/method globals at `0x27eaf98`/`0x27eaf9f`, calls thunk at `0x27eafae`, returns to `0x27eafb3`; logs `"onGameLoaded() SessionReporterState_GameLoaded placeId:%lld"` (`0x36c7c9`) |
| thunk | `0x25848e0` | builds SysV va_list (`gp_offset=0x18`, `fp_offset=0x30`) and issues `call *0x470(%rax)` at `0x258495a` = JNI slot 142 / `CallStaticVoidMethodV`; ABI: RDI=JNIEnv(C++ form), RSI=jclass, RDX=jmethodID, RCX=jlong, AL=0 |
| notifier registration slot | `0x6dbd860` | APS2 addend `0x27eaf6e`; global pointer `0x7234520`; registered by `0x253cb56(1, &0x7234520)` at `0x24ad699` inside `InitWithParams` `0x24abf96` |
| sibling (not the gate) | `0x72a2fa0` `gameDidLeave()V`, fired via `0x2ec45a6` from `0x2ec02xx` | shows the callback family is reached only through registered slots |

The notifier has **no direct static caller**: it is reached only by dispatch through
global `0x7234520` → slot `0x6dbd860`, introduced by the real Java event chain
`nativeAppBridgeV2SendAppEventOnGameLoaded` (`0x2ebca9e`) → event dispatch `0x2eb6b2a`
→ sink `0x72a31a8`, with `nativeAppBridgeV2InitWithParams` (`0x24abf96`) registering
the slot. Therefore a genuine firing must be **observed entering `0x27eaf6e`**, not
synthesised by calling `0x25848e0` from the harness.

### 4.2 `onAppShellReloadNeeded()V` — ruled out for a native harness
`analysis/jni/appshell-reload-trigger.md`:

- cached ID `0x72a3020` (name `0x4a702d`, sig `()V` `0x288017`), written once at
  `0x227b432` in `JNI_OnLoad`; **zero** native readers, no second
  `GetStaticMethodID`, no APS2 addend `0x4a702d`, and no Java `invoke-static` caller.
- Genuine firing needs ART + a live `ActivityNativeMain` listener + UI Looper + a real
  `Surface`. None exists in this native-only harness. A harness log of that name would
  be a fabricated event and must not satisfy decision 013.

### 4.3 Distinction from the callbacks we already see (the core of this task)

The phase-8/9 stack fires the following under the full opt-in set; none is the Leg C
callback:

| Callback | Cached ID / RVA | When it fires | Why it does **not** prove an app-shell frame |
| --- | --- | --- | --- |
| `surface()` | StartAppParams accessor; handler `start_app_get` / `real_path_get` (`guest_app_shell.c:158-161`) | `nativeAppBridgeV2StartAppWithParams` `0x26fe80f` | A **parameter getter**: the guest asks the bridge for a `jobject`, we fabricate a 16-byte `android/view/Surface`. It is plumbing and returns synthetic state; it is not in `shell_cb`. |
| `platformParams()` | accessor; handler returns synthetic `0x100`-byte `PlatformParams` (`:163-167`) | same | Same — getter plumbing, no real `dpiScale`/viewport values, not app-shell completion. |
| `vrContext()` | accessor; handler returns synthetic `android/app/Activity` (`:169-172`) | same | Same; moreover `rg/a1.E0()` is false in this build so the real getter should be **null** — our non-null synthetic object is a fidelity mismatch (`lua-app-start.md` §5). |
| `onAppBridgeNotification(String,String)` | cached ID `0x72a3028`, method-name string `0x3dea91` (`jni-map.md:102`) | real guest→Java notification during app-start | Explicitly rejected by the lead's phase-8/9 ruling (`decision 013` lead note lines 56-62): JNI plumbing, not a frame. Only counted/logged under `SOBER_LOG_APP_BRIDGE_NOTIFICATION`, which itself force-withholds the marker. |
| `gameLoadedCallback` via `SOBER_ALT_CALLBACK_PATH` | thunk `0x25848e0` called directly at `guest_app_shell.c:1319-1323` | harness calls the thunk after benign startup RVAs | Genuine guest thunk but **harness-triggered** (decision 037 honesty clause); returns to harness code, never passes `0x27eaf6e`. Prints `HONESTY NOTICE`. |

The coded gate already reflects part of this: `shell_cb` is *only*
`callback_was_called("onAppShellReloadNeeded") || callback_was_called("gameLoadedCallback")`
(`guest_app_shell.c:1331-1332`), so the accessors and `onAppBridgeNotification` never set
it. The missing discriminator is **provenance of the `gameLoadedCallback`**: the gate
currently cannot tell a real notifier firing from the ALT thunk call, which is why the
`seeded_state` force-withhold list (`:1337-1375`) and the ALT-path notice (`:1403-1408`)
exist. Leg C must be upgraded to a guest-return-address check (checklist C3 below).

---

## 5. Concrete acceptance checklist for the marker

The `app shell rendered` marker may be emitted **only if every line below passes**.
Any failure is a correct "withheld", not a regression to work around.

### Preconditions — no synthetic state, engine intact
- [ ] **P1.** Default path: every decision-013 withhold knob unset, so
      `seeded_state == 0` and no `marker withheld: diagnostic guest-state seed active`
      line appears. This includes all 40+ env vars forced into the withhold chain at
      `guest_app_shell.c:1337-1375` (e.g. `SOBER_SEED_USER_CONTROLLER`,
      `SOBER_SEED_APP_DELEGATE`, `SOBER_SEED_JNI_VM`, `SOBER_FAITHFUL_JAVAVM`,
      `SOBER_NATIVE_OBJECTS`, `SOBER_JSON_FLAG_BRIDGE`, `SOBER_JSON_APP_SINGLETON`,
      `SOBER_REAL_APPSTATE_PATH`, `SOBER_STARTAPP_PARAMS`,
      `SOBER_APP_SINGLETON_BRIDGE`, `SOBER_GAME_LOADED_PATH`, `SOBER_LUA_APP_START`, …).
      Note verifier #471 flagged residual coverage: any *future* guest-mutating opt-in
      must be added to this list before its run can be trusted.
- [ ] **P2.** `SOBER_ALT_CALLBACK_PATH` unset; no `ALT PATH guest call 0x25848e0` line,
      no `HONESTY NOTICE`, no `SOBER_APP_SHELL_DIAGNOSTIC` diagnostic marker.
- [ ] **P3.** Engine SHA-256 `3e498d64…` and BuildID `204ad049…` unchanged; phase-2
      (`guest init complete`) and phase-3 (`first frame rendered`, `0x649d422`) green;
      `verify-native.sh` PASS, ctest 2/2, `ldd` libc/ld-linux only.

### Leg R — render
- [ ] **R1.** Guest call log for `0x649d422` present with a live `0x48`-byte context
      (EGLDisplay `ctx+0x20`, EGLSurface `ctx+0x28` non-NULL) and `rc` success; the
      guest's `eglSwapBuffers@plt` `0x6d334d0` reached our native EGL shim.
- [ ] **R2.** `render_ok = sober_render_phase3_done() == 1` (persisted phase-3 flag).

### Leg S — app / surface state genuinely guest-owned
- [ ] **S1.** `app+0x408` and `app+0x440` are non-NULL **as written by guest code**:
      delegate via `0x25d7e75` / `0x25f7e7e` (StartLuaAppDM `0x25448f6` → `0x25449fc`
      → `0x24a8e44`), controller via `0x24ad50a` → `0x24ae1fe`. Diagnostic slot dump
      (`SOBER_APP_SLOT_DIAG` / `log_app_slots`) shows the transitions, and no seed knob
      is responsible.
- [ ] **S2.** FFlag `EnableAppBridgeComponents` at `0x72cb648 == 1` achieved through the
      guest's real flag path (`nativePreloadFlagOverrides 0x2edd859` JSON object +
      `nativeRetryInit 0x2edd739`), not a direct byte write.
- [ ] **S3.** App singleton `0x72cb5e0` populated by real guest code
      (`nativePostClientSettingsLoadedInitialization3 0x2383c18`), not a synthetic List.
- [ ] **S4.** Surface path `nativeAppBridgeV2UpdateSurfaceAppWithPlatformParams`
      `0x2773194` driven with a real PlatformParams (`dpiScale`, `viewportWidthMm`,
      `viewportHeightMm`), and `SingleSurfaceApp::userDidLogin` `0x2771f6c` reached to
      completion (no fault at `0x2772403`).

### Leg C — the callback, with guest provenance
- [ ] **C1.** `sober_jni_callback_was_called(j, "gameLoadedCallback")` is true (or
      `onAppShellReloadNeeded`, which is ruled out here); bridge log shows
      `guest->Java callback: gameLoadedCallback`, `last='gameLoadedCallback'`, and
      `callbacks` increased by ≥1 over the pre-event baseline.
- [ ] **C2.** The cached globals used are the genuine ones: class `0x72a2f48` and
      method `0x72a2fc0` non-NULL, sourced from `JNI_OnLoad` (`0x227b2c0`/`0x227b4eb`).
- [ ] **C3 (decisive discriminator).** Guest-side proof that the notifier itself ran:
      a breakpoint / hit-counter at `0x27eaf6e` (or a call-stack/return-address log from
      thunk `0x25848e0`) showing `RSI == placeId`, `AL = 0`, and return address
      `0x27eafb3` — i.e. control returned into guest code, not into the
      `guest_app_shell.c:1319` harness call. Optionally confirm the registration path
      `0x24ad699 → 0x253cb56 → slot 0x6dbd860` executed and the
      `"SessionReporterState_GameLoaded"` log (`0x36c7c9`) was emitted. This is what
      separates a real firing from the ALT thunk and from accessor plumbing.
- [ ] **C4.** The accessor callbacks `surface()` / `platformParams()` / `vrContext()`
      and `onAppBridgeNotification` (`0x72a3028`) are **not** counted as the gate signal,
      even though they may be logged; their presence never substitutes for C1–C3.

### Gate outcome
- [ ] **G1.** With R1–R2, S1–S4 and C1–C4 all true and P1–P3 clean, `shell_cb && render_ok`
      holds with `seeded_state == 0`; `emit_marker("app shell rendered\n", …)` fires
      exactly once (`guest_app_shell.c:1409`). Any other combination → marker withheld.

---

## 6. Evidence index (all read-only)

- `docs/decisions/013-no-weak-app-shell-marker.md` (ruling + phase-8/9 reaffirmation,
  lines 6-13 and 53-64), `docs/decisions/037-callback-thunk-milestone.md` (ALT honesty),
  `docs/decisions/011-phase3-complete.md` (render milestone).
- `analysis/render/render-map.md` §2-§5 (render RVAs, vtable, context layout).
- `analysis/game-loaded-path.md` §1-§6 (notifier `0x27eaf6e`, registration, call order).
- `analysis/callback/callback-map.md` §1-§3 (thunk ABI, cached globals).
- `analysis/jni/jni-map.md` §1.2, §4 (cached callback ID table, signal suggestion).
- `analysis/jni/appshell-reload-trigger.md` (onAppShellReloadNeeded ruled out).
- `analysis/appstate_fields.md`, `analysis/lua-app-start.md` §3-§7 (app/surface state,
  accessor callbacks, decision-013 separation).
- `analysis/decision-memo-alt-thunk-vs-013.md` (withhold list, gate semantics).
- Harness (read only): `src/linux/guest_app_shell.c` (`:130-175`, `:949-958`,
  `:1312-1324`, `:1329-1409`).

## 7. Honesty limits

- This is a static/definitional analysis. **No genuine app-shell/login callback has been
  observed** in any recorded run: the default path is `callback=0 render=1`, and the
  full opt-in stack reaches only the rejected `onAppBridgeNotification` plus the
  accessor getters. No success is claimed here.
- The `SessionReporterState_GameLoaded → notifier` arrow is strongly supported (relocation
  data-flow and log semantics) but the precise virtual call consuming slot `0x6dbd860`
  was not isolated; C3 therefore requires observing the notifier entry at runtime.
- Steps that create Leg S state (StartLuaAppDM, surface update, flag overrides) remain
  unachieved without synthetic objects; until they run from real guest code with every
  opt-in off, the marker stays withheld. This document changes nothing in the gate.
