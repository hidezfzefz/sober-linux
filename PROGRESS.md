# Progress snapshot (lead, 2026-11-18)

## What the target actually is

Sober is **not** a Windows game client and there is **no CPU translation** involved:

- Linux launcher chain: `sober` → `libloader.so` → `libbadcpu.so`.
- The game engine is an **x86-64 Android ELF**, `libroblox.so`, shipped inside
  `assets/roblox.apkm` (AppImage build: Build ID `204ad049...`, version
  2.740.931; Flatpak build: `791c3f9e...`, 2.745.1142 — **do not mix RVAs**).
- The "translation layer" to eliminate is the Android-on-Linux runtime:
  `basalt` (a Python-3.12 embedding host), `libbasalt.so`, and the
  byte-identical `mocktail`/`bwrap` multicall ELF.
- Goal = a native Linux ELF that needs none of that runtime, and reimplements
  the Android NDK/Bionic/JNI surface itself.

## Done

| Task | Owner | Result |
| --- | --- | --- |
| #368 REA inventory | worker-1 | `analysis/rea/findings.json` + `report.md`, 63 REA evidence IDs |
| #372 AppImage bridge confirmation | worker-1 | BuildID/SHA cross-check; all six allocator bridge RVAs land on valid prologues; mocktail/basalt identified as the host runtime |
| #370 native scaffold + NDK shim | worker-3 | `build/sober-native`, ELF64 PIE, `ldd` = libc only; 174 NDK/Bionic symbols, 91 GL/EGL trampolines; 565/565 strong guest imports resolved; ctest 2/2, Android smoke 30/30 |

## Open

| Task | Owner | State |
| --- | --- | --- |
| #369 Ghidra interface map | worker-2 | in progress |
| #373 versioned Bionic libc bridge | worker-3 | assigned, not claimed |
| #374 REA versioned-symbol table | worker-1 | assigned, not claimed |
| #371 integration/verification | lead | in progress |

## Known blockers / caveats

- `AMediaCodec*` and `AImageReader`/`AHardwareBuffer` are functional stubs,
  marked **unsupported (with reason)** in `docs/interfaces/abi-map.md`.
- Guest `dlopen` currently stops at `undefined symbol: __cxa_finalize, version
  LIBC`: Bionic version nodes `LIBC`/`LIBC_N`/`LIBC_O` vs glibc `GLIBC_*`.
  Tasks #373/#374 address this.
- Ghidra-backed REA cannot open the sectionless Sober ELFs; use
  `inspect_binary_layout` / `inspect_with_objdump` instead.

## Verification

`bash tests/verify-native.sh` passes: native ELF, no translation-layer
dependencies. `./build/sober-native --version` exits 0 immediately.

## Phase 2 (fresh Convio team, milestone: guest init_array completes)

New team `TEAM-GJ4W-25NU-FJLZ`, scope recorded in
`docs/decisions/006-init-milestone.md`. Current blocker: all 411 versioned
symbols resolve, but the guest segfaults inside its `init_array` because the
Bionic bootstrap (allocator/JNI/data seeds + 3618 constructors) is not yet
patched. Tasks #375 (bootstrap patcher), #378 (REA/Ghidra bootstrap map),
#377 (harness/build verification) and #376 (lead integration).

**STATUS: COMPLETE.** All 3618 guest constructors are driven natively and
`tests/guest-dlopen.sh` prints `guest init complete` with exit 0. Root causes
resolved: ctor-ordering control (decision 007), Android APS2 packed RELA
(decision 008, 565390 relocations), and the self-hosting versioned libc stub
(decision 009 — sysconf 0x27 now returns 4096). The on-disk engine is unchanged;
only a private memfd copy has `DT_INIT_ARRAYSZ` neutralised. `verify-native.sh`
PASS, ctest 2/2, `ldd` = libc only.

## Phase 3 (fresh Convio team: native window + first frame)

Team `TEAM-CEXX-GSEL-3UA5`; scope in `docs/decisions/010-native-window-milestone.md`.
Goal: from the working init-complete harness, create a native X11/Wayland +
EGL/GL window and render a first frame, with a deterministic `first frame
rendered` marker — no Android runtime or translation layer. Tasks #381
(render-path map), #379 (native backend), #380 (harness), #382 (lead).

**STATUS: COMPLETE.** `tests/first-frame.sh` passes with a visible native X11
window, a real guest render call at RVA `0x649d422`, and `first frame rendered`
gated on that call. All phase-2 checks preserved (see
`docs/decisions/011-phase3-complete.md`). Full JNI/app-shell and playable
gameplay remain out of scope.

## Phase 4 (fresh Convio team: native JNI bridge / app shell)

Team `TEAM-CYLN-PBYP-H7RL`; scope in
`docs/decisions/012-jni-app-shell-milestone.md`. Goal: satisfy the guest's JNI
callback surface natively, drive app-shell startup, and present a natively
rendered login/app-shell frame with an `app shell rendered` marker. Tasks #383
(JNI map), #386 (bridge), #385 (harness), #384 (lead).

**Phase 4 ended BLOCKED** (decision 018) — JNI bridge and strict test shipped,
but no genuine guest→Java callback fires. A focused follow-up team
`TEAM-R3YA-SH5U-2GKX` (tasks #387-#390) is tracing the writer of the bogus
guest-handle `+0x68` field to fix it at the source; all reader-side patches
stay reverted and the strict marker gate remains in force.

**Phase 4b** (decision 027, team `TEAM-972H-AWF5-BCVG`): a fresh team plus
four parallel research subagents captured the guest-internal allocation site
and ran targeted fixes; ended honestly blocked (decision 032). Tasks #391-#395.

**Phase 4c** (team `TEAM-3D7M-6DSA-THN9`, #400-#403) pinned the real consumer
caller via an F_inner int3 + frame-pointer chain (`guest+0x249c767`, a vtable
call at `0x249c76d`), added caller-side diagnostics and a JNI-singleton seed.
The strict gate still withholds the marker; three more parallel research
agents are tracing the remaining `+0x68` writer. Phase-2/3 remain green.

**Phase 5** (team `TEAM-898H-QDEA-BP9J`, #441-#444) passed the coded strict
gate via the guest's own JNI thunk `0x25848e0` under opt-in
`SOBER_ALT_CALLBACK_PATH`, with an explicit honesty notice: no login/app-shell
frame was proven, and the `+0x68` bug was bypassed.

**Phase 6** (team `TEAM-Z736-YJNQ-7WVF`, #445-#448) targets the REAL path:
`nativeAppBridgeAppStart` `0x247dc8d` with the repaired object pointer must
produce a genuine guest callback without the ALT bypass. Task #446 maps the
post-repair abort; #447 fixes it and the class-seeding type bug; #448 verifies
the real path. Phase-2/3 green stays mandatory.

## Phase 7 (real start-app path): still BLOCKED on the strict marker

Progress: the real guest call sequence now completes
(`nativeSetBaseDataDirectories`, `nativeSetRobloxVersion`,
`nativeSetRobloxChannel`, `nativeGameGlobalInit`,
`nativeAppBridgeV2InitWithParams`, `nativeAppBridgeAppStart`,
`nativeScheduleOnFirstFrame`, `nativeAppBridgeV2StartAppWithParams`), and the
genuine guest→Java accessor callbacks `surface()`, `platformParams()` and
`vrContext()` fire from it. Per decision 013 those are JNI plumbing, not
app-shell proof, so the marker stays withheld.

Static + live analysis added this phase:
- the user controller at `app+0x440` is installed through the wrapper vtable
  slot `+0x10` (guest `0x24ae1fe`), reached via `0x24ad50a`;
- `SingleSurfaceApp::userDidLogin` (`0x2771f6c`) faults at `0x2772403` when
  `app+0x440` is NULL; an opt-in `SOBER_SEED_USER_CONTROLLER` (guest-owned
  zeroed object) gets past it;
- the next two genuine faults are `app+0x408` NULL (`0x2772161`, opt-in
  `SOBER_SEED_APP_DELEGATE`) and an unseeded `JNI_OnLoad` string-helper global
  `0x7cdec78` (`0x2eb4b65`). Beyond this point the harness would have to
  emulate substantial Java/Android internals with synthetic vtable objects;
  that is outside the strict-evidence rule, so it was not pursued. The
  incomplete inline JNI-string-bridge stub was removed; a faithful JNI_OnLoad
  seed is being investigated on the `jnionload` team track.
- all new seeds are OPT-IN, off by default. They are diagnostics only and
  MUST NOT be used as evidence for `app shell rendered`. The gate now
  force-withholds the marker whenever any of them is active (task #452).

Verification after the phase-7 edits: `guest init complete` and
`first frame rendered` still present, `verify-native.sh` PASS, ctest 2/2,
`ldd build/guest-init-host` = libc/ld-linux only, on-disk engine SHA unchanged
(`3e498d6494…`). The no-start-app path faults asynchronously at `0x2eb4b65`
before reaching the wait loop, but never emits the marker.

**Verifier audit (task #451):** the `0x2eb4b65` fault is a *new manifestation of
a pre-existing gap*, not a regression of a working path. Clean HEAD (`b8bdab0`)
also exits 139 but faults earlier at `0x2456079` inside `InitWithParams`; the
phase-7 edits let execution reach the real six-argument `AppStart`, whose worker
thread dereferences the unseeded `JNI_OnLoad` string-helper global `0x7cdec78`
(helper `0x227b9e2`, caller `0x2eb4b65`). Removing `SOBER_SCHEDULE_RUNNABLE`
does not change it, so the trigger is the real-args `AppStart`, not the Runnable.
No safe tests/-only fix exists; the faithful fix is jnionload's task #449.
A default-OFF diagnostic baseline is recorded in `tests/fault-baseline.sh`;
full audit in `tests/verification-report-451.md`.

**App-state provenance (task #450):** `analysis/appstate_fields.md` identifies
the missing real Java sequence: `nativeAppBridgeV2SendAppEventOnAppReady`
(`0x2ebcf36`), `nativeAppBridgeStartLuaAppDM` (`0x25448f6`) and
`nativeAppBridgeV2UpdateSurfaceAppWithPlatformParams` (`0x2773194`) were never
called, and seven `StartAppParams` getters had no handlers. Crucially, the
controller installer `0x24ad50a` is gated by FFlag
`EnableAppBridgeComponents` (`0x72cb648`), normally set by Java via
`nativePreloadFlagOverrides` (`0x2edd859`). An opt-in
`SOBER_REAL_APPSTATE_PATH` replay was added to `guest_app_shell.c` (flag override,
app-ready event, StartLuaAppDM, all ten getters; marker force-withheld). It did
not unblock the path: forcing the flag to 1 faults at `0x2ec1042` because app
singleton `0x72cb5e0` is unpopulated; replaying
`nativePostClientSettingsLoadedInitialization3` with our synthetic `List`
faults at `0x2e5d42a`; leaving the flag at 0 and calling StartLuaAppDM faults at
`0x320581b` on the same null `app+0x408`. The remaining gap is that the guest
treats these JNI "autovalue" jobjects as native C++ collections/objects, and our
bridge's synthetic objects do not reproduce that layout. This needs faithful
JNI_OnLoad / Java-object emulation rather than more fake vtables.

**JNI_OnLoad global (task #449):** `0x7cdec78` is not a bespoke string helper —
it is the **JavaVM\*** captured from `JNI_OnLoad` argument 1
(`0x227af6e → 0x227b8fb → 0x227b912/0x227b946 → 0x227b92d`). Consumers use the
genuine two-level layout: `JNIInvokeInterface_::GetEnv` (`+0x30` → `0x6c8e8a8`),
then `JNIEnv::NewStringUTF` (`+0x538`), which is why a single-level fake vtable
failed. Implemented opt-in `SOBER_SEED_JNI_VM` →
`sober_jni_seed_guest_onload_globals(j, base)` in `src/jni/native_jni.c/.h`,
wired once from `guest_app_shell.c` and added to the forced-withhold list.
A/B (3/3 each): OFF faults at `0x2eb4b65`; ON removes that fault but exposes an
earlier `0x2e5d42a` (NULL `std::string` vtable on the worker thread). Remaining
blocker: other consumers (`0x227aecc` from `0x23387de` → Attach `+0x20`;
`0x23388d6`/`0x6c47d3c` → Detach `+0x28`) need real Android JavaVM thread/log
semantics our bridge cannot supply without fabricating internals. Diagnostic
only — never evidence for the marker.

**FFlag grammar (appstate follow-up):** `nativePreloadFlagOverrides(String)`
takes a JSON object string (`{"EnableAppBridgeComponents":true}`), stored at
`0x72a3b20/0x72a3b30` and applied only by `nativeRetryInit` (`0x2edd739`), so
both must precede `nativeGameGlobalInit`. Real `SendAppEventOnAppReady` args are
`("AppShellNotifications","<id>","StartConversationWithUserId","Chat")`. The
opt-in `SOBER_REAL_APPSTATE_PATH` replay was corrected accordingly, but the flag
still reads 0 afterwards — our synthetic `jstring` is not a real Java String the
guest's JSON parser can consume. Combined with the JNI_OnLoad result, all
remaining blockers are Java/ART-side semantics that cannot be supplied by fake
objects under decision 013. Phase 7 therefore ends honestly BLOCKED: genuine
accessor callbacks are observed, but no app-shell callback and no marker.

## Phase 8 — lead integration of the delivered opt-in modules

Late reports from the closed phase-7 team delivered three default-OFF modules,
which the lead has now wired in:
- `src/jni/javavm_semantics.c/.h` (`SOBER_FAITHFUL_JAVAVM`) — genuine per-thread
  JavaVM attach/detach/GetEnv/ExceptionCheck. Added to `sober_jni` target and
  installed after the JNI_OnLoad seed.
- `src/jni/jni_native_objects.c/.h` (`SOBER_NATIVE_OBJECTS`) — guest-shaped
  native layouts for autovalue jobjects. Installed before any guest RVA.
- `src/jni/jni_json_bridge.c/.h` (`SOBER_JSON_FLAG_BRIDGE`,
  `SOBER_JSON_APP_SINGLETON`) — replays flag overrides through the guest's own
  descriptor thunk `0x22e567a`, JSON driver `0x237dc30` and setter `0x23648be`.
  Live A/B: the FFlag genuinely becomes 1 (rc=1) with no direct byte write.
All four env vars were added to the decision-013 forced-withhold list.

A/B with every module ON: FFlag 1, guest attach path exercised, but the fault
remains `0x2e5d42a`; no callbacks and no marker. All three analyses independently
attribute it to the guest global `std::string` at `0x72a0520` with a NULL
header, which no delivered module seeds. Default (all OFF) path is unchanged:
baseline fault `0x2eb4b65`, phase-2/3 markers present, no marker emitted.
Build PASS; engine SHA unchanged. A separate default-OFF diagnostic memo on the
`tests/app-shell.sh` ALT-thunk contradiction is in
`analysis/decision-memo-alt-thunk-vs-013.md` (recommendation: B+C, not enacted).

### Phase 8 follow-up — real guest init defects found and repaired (opt-in)

Three more genuine guest-side root causes were identified and wired:
1. **FFlag registry** — decision 008 skips `init_array[0]` (`0x2e62390`), the
   sole caller of the guarded initializer `0x2e618b0 → 0x2e61280`. Opt-in
   `SOBER_REPLAY_FFLAG_CTOR` calls that real ctor; guard `0x72a0a30` becomes 257
   and `0x72a0520` becomes `base+0x6db4008`. Fault `0x2e5d42a` is eliminated.
2. **Global-string seed ordering** — `SOBER_SEED_GLOBAL_STRING` must be applied
   BEFORE the JNI_OnLoad/JavaVM seeds (worker race); moved early.
3. **Bionic `__sF` ABI** — runtime GOT inspection proved the guest binds to
   `android_compat.c`'s `__sF` (not the libc-stub one). It was a 24-byte
   `FILE *[3]`, but the guest derives stdout/stderr at `+0x98`/`+0x130`.
   Enlarged to `0x200` bytes in both definitions; opt-in
   `SOBER_BIONIC_SF_FULL` redirects those two guest stream addresses to the
   real host `stdout`/`stderr` through the versioned `fflush`/`fwrite`
   trampolines. The glibc `_IO_fflush` SIGSEGV at `libc+0x7c8ff` is gone.

With all opt-ins ON, execution now advances to a **deliberate guest `int3`
assertion** at `0x6caef66` immediately after
`nativeRegisterJavaFlagProvider` (`0x2ed3aa4`), gated by globals `0x7cdd149`
and `0x7cef650`. Still no genuine app-shell callback; marker withheld by the
gate (every opt-in, including the new ones, force-withholds it).

Final verification: build PASS, verify-native.sh PASS, ctest 2/2, ldd
libc/ld-linux only, engine SHA `3e498d6494…` unchanged, phase-2/3 markers
present, default path unchanged (`0x2eb4b65`), zero marker emissions.

### Phase 8 final consolidation and explicit gate ruling

- The `int3` at `0x6caef66` was NOT a flag fault: it is the guest's RBXCRASH
  reporter terminal. Root cause was our scheduler neutralize: `0x722e540` is a
  Bionic TLS-key descriptor, not the scheduler object; the real scheduler state
  is per-thread (`0x2a0de60` → `TLS+0x410` → `obj+0x70`). See
  `analysis/tls-2a0de60-r70-correction.md`.
- Enabling persistent Bionic TSD removed the RBXCRASH path but first aborted in
  glibc `realloc()`: `SOBER_STUB_TSD_SEED` used a static BSS buffer that the
  guest reallocs. Fixed by using a real `calloc`'d buffer. See
  `analysis/tsd-static-buffer-abort.md`.
- With the full opt-in stack (TSD heap seed, no scheduler neutralize, FFlag ctor
  replay, global-string seed, faithful JavaVM, full `__sF`, controller/delegate
  seeds) the **start-app path completes without crashing** and fires a genuine,
  counted guest→Java callback `onAppBridgeNotification(String,String)`
  (`jni-map.md:102`, `0x72a3028` → thunk `0x3dea91`) alongside phase-3 render.
- Attempts to remove the fake controller/delegate seeds reached guest-native
  app creation (`0x24ad562`) but faulted because app singleton `0x72cb5e0` is
  unpopulated; replaying `postClientSettings` to create it faults at `0x2e7b8e2`.
- **Explicit lead ruling requested and received:** KEEP decision 013 strict.
  `onAppBridgeNotification` is not accepted as app-shell proof, and seeded state
  does not qualify. The marker remains withheld; no gate change was made. A
  diagnostic handler can count/log that callback (`SOBER_LOG_APP_BRIDGE_NOTIFICATION`)
  without affecting the marker.

## Phase 9 (TEAM-XT29-DCJS-2UVE) — parallel fidelity work

Workers #465–#472 delivered:
- `analysis/jni/appshell-reload-trigger.md`: `onAppShellReloadNeeded` (0x72a3020)
  has NO native reader anywhere; it cannot fire in a native-only harness.
- `analysis/game-loaded-path.md`: only viable gate callback is
  `gameLoadedCallback(J)V` via notifier 0x27eaf6e → 0x27eaf9f → thunk 0x25848e0;
  trigger is indirect through registered slot 0x6dbd860, driven by
  `nativeAppBridgeV2StartGameWithParam` (0x2ebd51a) and
  `nativeAppBridgeV2SendAppEventOnGameLoaded` (0x2ebca9e).
- `analysis/stability-fault-audit.md`: 0x2e7b8e2 → 0x24ad562 → 0x2772403 are
  one dependency chain rooted in skipped ctor[0] and the app singleton.
- New opt-in modules wired: `startapp_params.c` (all ten faithfully named JNI
  getters, guest-shaped PlatformParams), `app_singleton_bridge.c`
  (real-guest-code singleton construction; must run AFTER FFlag ctor replay),
  `global_string_seed.c` (early), `javavm_semantics.c`, `jni_native_objects.c`,
  `jni_json_bridge.c`, `bionic_sf_full.c`.
- Verifier #471: all invariants PASS at three snapshots; **17/17** documented
  opt-ins force-withhold the marker. Lead added 14 more guest-mutating knobs
  (patches, TSD seeds, singleton/params modifiers) to the withhold chain.
- Opt-in `SOBER_GAME_LOADED_PATH` replays StartGameParams + game-loaded event;
  it faults at guest 0x31a1360 on a bogus handle (rax=TSD sentinel), so no
  genuine gameLoadedCallback was observed. Strict gate unchanged, no marker.

Final verification: build PASS, verify-native.sh PASS, ctest 2/2, ldd
libc/ld-linux only, engine SHA 3e498d6494… unchanged, phase-2/3 markers
present, default fault 0x2eb4b65, zero marker emissions.

## Phase 10 — four parallel 10-worker groups (guest state / JNI / callbacks / assurance)

No new Convio teams could be registered (account cap), so four logical groups of
ten subagents each ran with strict per-file ownership; the lead integrated all
deliverables. Engine identity unchanged throughout.

Key corrected findings:
- **Sentinel** `0xABCDEF0123456789` is the faulting guest function's own
  `__stack_chk_guard` canary, NOT an object field. Real fault at `0x31a1360` is
  `app+0x488 == NULL` (`0x320b0ba`); an opt-in guest-shaped seed clears it and
  advances to `0x2772161` (delegate).
- **Skipped ctors 1/3/4 are irrelevant** (A5): ctor[1] lazily reached via
  ctor[6], ctor[3] DFLog-only, ctor[4] self-initializing; only ctor[0] is real.
- **Real delegate** comes from guest helpers `0x1dd55f2`/`0x25f7ff2` with genuine
  guest vtables (`0x7220868`/`0x6dc3840`), not our fake host-vtable object (A8).
  Wired as `SOBER_FAITHFUL_APP_DELEGATE`.
- **PlatformParams** must be read through JNI FIELDS
  (`GetFieldID/GetFloatField/GetIntField`, fn 90/96/98), not getter methods
  (A9/B5). Module `platform_fields.c` wired behind `SOBER_PLATFORM_PARAMS_FIELDS`.
- **Layer-1 pass** (B1/B2/B4): corrected `startapp_v2`, `startgame_v2`, canonical
  `jni_strings`, `game_params`; all wired, all default-OFF. Getter shadowing fixed
  and `reclaim/reassert` used immediately before guest RVAs.
- **RBXCRASH** under Lua start is caused solely by `0x7cdd149 == 0` (flags not
  loaded); only real writer is `nativeInitClientSettings 0x2359277` →
  `0x2eb7155` → `0x2eb7520` (C2/luafault). Wired as `SOBER_CLIENT_SETTINGS_INIT`.
- **Gate provenance**: name-only callback counting cannot distinguish the ALT
  thunk from notifier `0x27eaf6e`; admissible evidence requires notifier hit
  (`RSI=placeId`, return `0x27eafb3`) + render `0x649d422` (C3/C9/B6/B9).
- **Withhold chain** expanded to cover all Tier-1 guest mutators across modules
  (D02); D09 audit reports no unclassified gaps in the harness file.

Final state: all opt-ins OFF by default, strict decision 013 unchanged, marker
WITHHELD; default run `callback=0 render=1`, fault `0x2eb4b65`. Final verification
build PASS, verify-native.sh PASS, ctest 2/2, libc-only ldd, engine SHA
`3e498d6494…` unchanged. No success claimed.

## Phase 10 wave 2 — strict ruling and provenance

- **Decision 038 accepted** (`docs/decisions/038-strict-gate-alt-thunk-ruling.md`):
  the ALT thunk `0x25848e0` is diagnostic-only and never proof; the real marker
  requires notifier-driven `gameLoadedCallback(J)V` (`0x27eaf6e`, `placeId != 0`,
  return `0x27eafb3`) or a notifier-driven `onAppShellReloadNeeded`, plus phase-3
  render `0x649d422`, plus `seeded_state == 0` and no ALT/HONESTY lines. Name-only
  counters are necessary but not sufficient. Decision 013/037 amended; the stale
  ALT assertion in `tests/app-shell.sh` replaced with strict absence checks and a
  notifier-provenance requirement, so the test stays correctly RED.
- Wave-2 modules wired (all default-OFF, withhold-listed): `callback_provenance`
  (return-address discriminator `0x2584960`/`0x27eafb3`), `static_object_calls`
  (fn111/112/165), `scalar_returns` (fn114/115/125-130/135/136),
  `alloc_contract_fix` (header-zero/plain-calloc/registered free). New
  `SOBER_*` knobs and all value-modifier prefixes force-withhold the marker.
- Default path unchanged: phase-2/3 markers present, zero marker emissions;
  build/verify-native/ctest 2/2/libc-only ldd all PASS; engine SHA unchanged.
  Occasional X11 `:0` `XIO` teardown errors are environmental (D12: 3/3 clean
  re-runs). No app-shell callback proven; marker WITHHELD.

## Phase 9b — `TEAM-XT29-DCJS-2UVE` follow-up (superseded by `docs/REVIEW-100.md`)

Scope: reach a genuine guest→Java app-shell callback and a natively rendered
login/app-shell frame under decision 013, by closing the remaining guest-side
state and JNI-fidelity gaps in parallel. This section is a status record only;
docs owns no source files.

**State:** with the full opt-in stack enabled (Bionic TSD heap seed, no scheduler
neutralize, FFlag ctor replay, global-string seed, faithful JavaVM attach/detach,
full `__sF` streams, and controller/delegate seeds), the real start-app path now
completes without crashing and fires a genuine, counted guest→Java
`onAppBridgeNotification(String,String)` callback (`jni-map.md:102`; cached ID
`0x72a3028` → thunk `0x3dea91`) alongside the phase-3 render call.

**Gate: WITHHELD — ruling unchanged.** Per the explicit lead ruling in decision
013, `onAppBridgeNotification` is JNI plumbing, NOT app-shell proof, and seeded
state does not qualify. The strict condition is unchanged: a genuine
`onAppShellReloadNeeded()V` or `gameLoadedCallback(J)V` from the real path plus
the phase-3 render call. No gate weakening was made and decision 013 was not
weakened. The callback is only counted/logged by the opt-in diagnostic
`SOBER_LOG_APP_BRIDGE_NOTIFICATION`, which cannot emit the marker.

**Remaining blockers:**
- app singleton `0x72cb5e0` is unpopulated — removing the fake controller/delegate
  seeds reaches guest app creation at `0x24ad562`, but the singleton lookup returns NULL;
- replaying `postClientSettings` to populate that singleton faults at `0x2e7b8e2`;
- the controller/delegate seeds are synthetic guest state, so the path that
  currently works cannot be used as evidence;
- no genuine gate callback (`onAppShellReloadNeeded` / `gameLoadedCallback`) has
  been observed — only the non-qualifying `onAppBridgeNotification`.

**Next steps (active tasks):**
- #465 appsingleton — populate `0x72cb5e0` via real guest code (opt-in API).
- #470 stability — audit `0x2e7b8e2` / `0x24ad562` faults; propose real-guest-code fixes.
- #469 params — faithful guest-native StartAppParams (all ten fields) for the real builder.
- #466 gameload — map genuine `gameLoadedCallback` trigger + opt-in call sequence.
- #467 shellcb — map genuine `onAppShellReloadNeeded` trigger and required state.
- #468 luastart — map Lua app start → app-shell frame and missing JNI entry points.
- #471 verifier — re-run invariants after each integration; confirm zero marker emissions under strict 013.
- #472 docs — keep this record current.

No success is claimed: until a real gate callback plus phase-3 render is observed
with no synthetic state, `app shell rendered` stays withheld.
