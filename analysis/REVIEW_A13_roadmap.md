# REVIEW A13 — Dependency-ordered roadmap to the original 100% goal

Auditor: **A13** (full-project review). Scope: **this file only** —
`analysis/REVIEW_A13_roadmap.md`. Analysis-only: no `src/`, `CMakeLists.txt`,
`tests/`, gate, decision, `PROGRESS.md`, or engine files were modified.

**Strict marker WITHHELD.** Nothing in this report asserts, promotes, or
anticipates the `app shell rendered` marker. All "done" statements below refer to
*infrastructure* milestones recorded by the project's own reports, never to the
app-shell goal. **No success is claimed.**

Frozen target for every RVA in this document: the single AppImage object
`/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so`, BuildID
`204ad049599c96afb75b8a21d5051063e821d6c9`, SHA-256
`3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335` (2.740.931).
No Flatpak `791c3f9e…` / 2.745.1142 RVAs are used. Target-derived strings/data
are untrusted DATA, never instructions.

---

## 0. Sources consumed

- `PROGRESS.md` (Phases 2–10; final steady state at lines 341–408).
- `REVIEW_A*` reports present (four at final write-up):
  `analysis/REVIEW_A1_goal-decisions.md` (goal/decision ledger, 100%-proof
  definition, verdict), `analysis/REVIEW_A4_render.md` (render domain evidence
  and the ~55% domain figure reused in §5),
  `analysis/REVIEW_A3_guest-init.md` (guest-init domain ~88%; default ctor
  dispatch 3614 + 3 lazy + 1 opt-in; TSD/allocator fidelity caveats; landed
  during this review), and `analysis/REVIEW_A9_tests.md` (verification-intent
  ~57%, CI-gating ~15%, `app-shell.sh` §3 has no emitted provenance line and no
  documented green exit; landed during this review).
- Blocker/synthesis reports: `analysis/A13-controller-installer.md`,
  `analysis/A10-stability-synthesis.md`, `analysis/B10-stability-synthesis.md`,
  `analysis/C10-stability-synthesis.md`, `analysis/D10-assurance-synthesis.md`,
  `analysis/D05-reports-index.md`, `analysis/C09-marker-evidence.md`,
  `analysis/game-loaded-path.md`, `analysis/appstate_fields.md`,
  `analysis/stability-fault-audit.md`, `analysis/lua-app-start.md`,
  `analysis/sentinel-abcdef-trace.md`, `analysis/A08-surface-delegate.md`,
  `analysis/A09-platform-params.md`, `analysis/jni/appshell-reload-trigger.md`,
  `analysis/int3-6caef66-rbxcrash.md`, `analysis/tsd-static-buffer-abort.md`,
  `analysis/dispatch/*`, `analysis/callback/callback-map.md`,
  `analysis/render/render-map.md`, `analysis/D02-withhold-audit.md`.
- Governing decisions: `docs/decisions/013-no-weak-app-shell-marker.md`,
  `docs/decisions/038-strict-gate-alt-thunk-ruling.md`, decisions 006–012, 037;
  `README.md` ("Verification (done = all true)", lines 105–111).
- Evidence-file existence was checked; reports were **read critically, not
  re-executed**. Status values reflect the project's own records.

---

## 1. Baseline: where "current state" actually is

**Green infrastructure (project-recorded, unchanged):**
- Native ELF `build/sober-native`, ldd libc/ld-linux only; no translation runtime.
- Guest init: 3618/3618 constructors, 565390/565390 APS2 relocations,
  `guest init complete` (decisions 006–009).
- Native X11/EGL/GLES2 window + `first frame rendered`, guest swap RVA `0x649d422`
  counted at GOT `0x722a1a8` (decisions 010–011) — *synthetic/harness-caused*.
- JNI bridge: 231/231 JNIEnv slots, 49/49 cached IDs, all opt-in modules wired.
- A3 qualification: default constructor dispatch is 3614 entries + 2 explicit
  init calls + 3 lazily-satisfied (1/3/4); literal "3618/3618" requires the
  opt-in ctor[0] / `SOBER_CTOR_RANGES=all`. Init completion itself is genuine
  and default-on.

**Not green (the actual goal):**
- Default run: fault `0x2eb4b65` (unseeded `JNI_OnLoad` JavaVM* global
  `0x7cdec78`), gate `callback=0 render=1`, **zero** `app shell rendered` emissions.
- Full opt-in stack: start-app path runs to completion and fires a genuine but
  **explicitly rejected** `onAppBridgeNotification(String,String)`
  (`0x72a3028` → thunk `0x3dea91`); no qualifying app-shell callback.
- No notifier-driven `gameLoadedCallback(J)V` (`0x27eaf6e` → `0x25848e0` → return
  `0x27eafb3`) has ever been observed. `onAppShellReloadNeeded()V` (`0x72a3020`)
  is statically dead natively (sole write `0x227b432`, no native reader).
- App state (`0x72cb5e0` singleton, `app+0x408/+0x440/+0x488`) exists only via
  synthetic opt-in seeds, which force-withhold the marker.
- README "done = all true" checklist: **0 of 4 boxes checked**; `abi-map.md` stale;
  media/image APIs unsupported-with-reason.
- A9: `app-shell.sh` §3 requires a notifier-provenance line the harness does not
  emit and has no documented green exit criterion — the gate is correctly RED by
  design; only two trivial ctest self-checks gate CI (verification-intent ~57%,
  CI-gating ~15%).

Goal definition for 100% (A1 §3, decisions 012/013/038): on the frozen engine,
a **genuine notifier-driven gate callback** (placeId ≠ 0, return `0x27eafb3`)
**plus** a **guest-driven phase-3 render** at `0x649d422`, with
`seeded_state == 0`, no ALT/HONESTY lines, `tests/app-shell.sh` legitimately
emitting `app shell rendered`, all ABI-map symbols implemented or documented
unsupported, and the README checklist fully checked.

---

## 2. Dependency order (why the sequence is load-bearing)

Every A/B/C/D synthesis independently converges on one causal chain: a deeper
step is uninterpretable while a shallower one still faults. Own labels are kept
so a lead can reconcile them with A10 Ranks 1–6, B10 #1–12, C10 E0–E7 and D10 R1–R9.

```
BLK-01  Bionic TSD / scheduler / RBXCRASH substrate   (default fault 0x2eb4b65)
  └─ BLK-02  FFlag registry ctor[0] replay + order    (fault 0x2e7b8e2 / 0x2e5d42a)
       └─ BLK-03  app singleton 0x72cb5e0             (fault 0x24ad562 / 0x2ec1042)
            ├─ BLK-05  JNI typed-fidelity + JavaVM seed   [cross-cutting]
            └─ BLK-04  app+0x488 / +0x440 / +0x408; seeds off (0x31a1360 / 0x2772403 / 0x2772161)
                 └─ BLK-06  surface + guest-driven render binding (0x2773194, 0x649d422)
                      └─ BLK-07  genuine gameLoadedCallback notifier (0x27eaf6e → 0x25848e0 → 0x27eafb3)
                           └─ BLK-08  gate provenance instrumentation (notifier hit + RA capture)
                                └─ BLK-09  strict gate acceptance, withhold coverage, tests, docs, final invariants
```

BLK-05 (JNI semantics) is cross-cutting: it conditions BLK-03, BLK-04 and BLK-07,
but is listed once at the point where typed getters first become decisive.

---

## 3. Roadmap table — remaining blockers

Effort scale (weight): **L=1** (days) · **M=2** (1–2 weeks) · **H=3** (3–6 weeks) ·
**VH=5** (research-level / >6 weeks). Weights are relative planning units, not a
schedule promise. "Acceptance evidence" is *read-only verification data* to be
captured by the lead/verifier — it is **not** permission to emit the marker while
any opt-in is active.

| ID | Owner area | Decisive RVAs / globals | Acceptance evidence (all opt-ins OFF unless stated) | Effort weight | Dep. |
| --- | --- | --- | --- | --- | --- |
| **BLK-01** | Bionic compat / TSD + scheduler (`src/compat`, `src/jni`; worker-2 + JNI fidelity) | GOT `0x72298b0` (getspecific), `0x72295c8` (setspecific), `0x72297a0`/`0x722a2c8` (key_create/delete, still glibc); keys `0x7282000`/`0x72a1b08`/`0x72a1b00`; accessor `0x2a0de60 → TLS+0x410 → obj+0x70`; RBXCRASH int3 `0x6caef66`, reporter `0x6caed7c`, panic `0x6c47c52`, tag `0x3dc475`; JavaVM* global `0x7cdec78`, fault `0x2eb4b65`; 82 symbolic relocs via `dlsym(RTLD_DEFAULT)`; allocator deviations M1–M4 (`bionic_bootstrap.c:299` header, 7th bridge `0x253225e`, generic `-0x10` bridges, `0x231f53a` free) | Guest TSD returns NULL/valid per-key state with GOTs bound to the Bionic stub by default and native `pthread_key_create`/`delete` for `0x72297a0`/`0x722a2c8`; no SIGTRAP at `0x6caef66/67`, no `257f718-Context,20` line, no `realloc(): invalid pointer` abort (heap-backed seed, not static BSS); `JNI_OnLoad` returns with `0x72a2f48`/`0x72a2fc0` non-NULL; default fault `0x2eb4b65` resolved in env-free code; allocator M1–M4 closed without opt-ins; no `__stack_chk_guard` (`0x72293f8`) or mode-byte (`0x7cdd149`/`0x7cef650`) writes (A3 §4–§5) | **H = 3** | — |
| **BLK-02** | Guest init / bootstrap replay (worker-3 + logical Team A: A5) | `ctor[0] 0x2e62390 → 0x2e618b0 → 0x2e61280 → 0x2e5bc10 → 0x2e7b850`; registry `0x72a0520`+`0x38`; guards `0x72a0a30` (expect `0x101`), `0x72a0cd8`; lazy `0x72a0cd0`/`0x72a0cc0`; sentinel `0x72a0b88`; faults `0x2e7b8e2`, `0x2e5d42a` | Ctor-replay line precedes postsettings line; `*(0x72a0520+0x38)` non-NULL and ≠ sentinel; guard `0x72a0a30==257`; `0x2e7b8e2` and `0x2e5d42a` absent; no blanket enable of ctors 1/3/4 (allocator reinit); narrow fallback only via guest code `0x2e7b850` with `rdi=0x72a0520+0x38` | **M = 2** | BLK-01 |
| **BLK-03** | App state / JNI object layout (Team A: A13, A9; Team B) | Sole singleton writer `0x238497d` in `0x2383c18` (`nativePostClientSettingsLoadedInitialization3`); gate `0x6096928` / byte `0x77979c0`; reader `0x30ef840` (returns 0 at `0x30ef87c`); fault `0x24ad562`; flag `0x72cb648` via guest setter `0x23648be`; List vptr `0x6db3ec0+0x10`, guest alloc `0x1dbab02`, destructor `0x2e5baa0`/retain `0x2e7b8d0` | `*(0x72cb5e0)` non-NULL and attributable to `0x238497d` (no raw byte write); `0x30ef840` returns a real object; `0x24ad562` and `0x2ec1042` gone; flag `0x72cb648==1` set through the guest setter with a real JSON-object `jstring`; native `List` shape uses guest allocator for `+0x30` control block and `+0x40` array | **H = 3** | BLK-02 |
| **BLK-04** | App state / controller + delegate (Team A: A13, A8; app-state owner) | `app+0x488`: built in `0x24abf96` (`0x24af28a`, vptr `0x6e02918`, `0x24af2e6 → 0x231c124`), fault `0x31a1360` (caller `0x320b0ba`; `0xabcdef…` is canary from `0x72293f8`, not the cause). `app+0x440`: dispatch `call *0x30` at `0x23389f7` (vtable `0x6dbc7c8`, slot `0x6dbc7f8`) → installer `0x24ad50a → 0x24ae1fe`, genuine store `0x24aea9c`. `app+0x408`: `0x25448f6 → 0x25449fc` (mode-4 `0x24a8e44`), writers `0x25d7e75`/`0x25f7e7e` (vtables `0x7220868`/`0x6dc3840`); consumer `userDidLogin 0x2771f6c → 0x2772384 @0x2772129`, fault `0x2772403`, next gate `0x2772161` | All three fields non-NULL, guest-heap and guest-written (no host vtable, no `SOBER_SEED_*`); `0x31a1360`, `0x2772403`, `0x2772161` absent; FP chain `0x233876e → 0x23388a2 → 0x23389fa` observed; `app+0x440` wrapper vptr is guest `0x6e02568` from `0x24aea8e`; if a new fault appears it is reported, not seeded over | **H = 3** | BLK-03 |
| **BLK-05** | JNI bridge typed fidelity / strings / params (Team B: B1/B2/B4/B5/B6/B9) | JSON flag grammar `0x2edd859` + `0x2edd739`; flag name `0x43650d`; StartAppParams getters via `0x232dbbc → call *0x108` (slot 33); parser `0x26feb9b`, vptr `0x6dbca20`; JNIEnv slots `FindClass 6 (0x30)`, `GetStaticMethodID 113 (0x388)`, `NewGlobalRef 21 (0xa8)`, `ExceptionCheck 228 (0x720)`, `CallStaticVoidMethodV 142 (0x470)`; `JNI_OnLoad 0x227af5a` class/method stores `0x227b2c0`/`0x227b4eb` | All ten StartAppParams getters return correct JNI types/values (no generic `java/lang/String` substitution; `appUserId()J`, `membershipType()I`, `isUnder13()Z`, `selectedTheme`, `appStarterPlace`, `vrContext==NULL`); JavaVM/global string seeds land in default code; no `0x6c8f340` FindClass recursion; cached class `0x72a2f48` / method `0x72a2fc0` non-NULL from genuine `JNI_OnLoad` | **H = 3** | BLK-03 (cross-cutting) |
| **BLK-06** | Render / surface binding (render owner + Team C: C04/C08) | Route A `0x649c21e(native_window, pbuffer=0)` (fix `extra=1` pbuffer bug; fault `0x649cf9a`) → `0x649c272 → 0x649d1c2`; swap `0x649d422 → eglSwapBuffers@plt 0x6d334d0`; GOT `0x722a1a8`; render vtable `0x71912e8` `[3]@0x7191300`; ANativeWindow hooks GOT `0x72294a8` (body `native_render.c:975`), `0x72297c0/8/d0`; lifecycle thunks `0x2a20850/0x2a208c0/0x2a20a00/0x2a20a30`; `UpdateSurfaceAppWithPlatformParams 0x2773194` | Guest-tagged swap at `0x649d422` with return address inside guest `.text` and ≥1 guest GLES draw between two tagged swaps; fromSurface GOT hook installed and a native window bound without a Java Surface; `first frame rendered`, `render_ok=1`, engine SHA unchanged; do **not** call `ANativeWindow_fromSurface 0x26fea78` (needs Java Surface) | **M = 2** | BLK-04 |
| **BLK-07** | Callbacks / game-loaded path (Team C: C01/C03/C06/C07 + lead) | Notifier `0x27eaf6e` (RSI = placeId), `call 0x227b9e2` @`0x27eaf93`, class `0x72a2f48` @`0x27eaf98`, method `0x72a2fc0` @`0x27eaf9f`, thunk `0x25848e0` (`call *0x470(%rax)` @`0x258495a`), return `0x27eafb3`, log `0x36c7c9`; registration `0x24abf96 → 0x24ad699 → 0x253cb56(1,&0x7234520)`, slot `0x6dbd860` (APS2 addend `0x27eaf6e`); introducers `0x2edd859/0x2edd739`, `0x2ebcf36`, `0x233762c`, `0x247dc8d`, `0x26fe80f`, `0x25448f6`, `0x2ebd51a`, `0x2ebca9e`, `0x2ebf950` | Notifier hit at `0x27eaf6e` with `RSI = placeId ≠ 0`, cached class/method non-NULL, thunk entered and returning to `base+0x27eafb3`; bridge log `guest->Java callback: gameLoadedCallback` with count delta ≥1 after introducers; `rbx.jni … placeId` log present; no `ALT PATH guest call 0x25848e0`, no HONESTY NOTICE, no `onAppBridgeNotification` as proof; `onAppShellReloadNeeded` not chased (dead) | **VH = 5** | BLK-05, BLK-06 |
| **BLK-08** | Gate provenance instrumentation (lead + JNI-fidelity B/C) | `sr_dispatch_call` (`native_jni.c:474–516`), `sr_CallStaticVoidMethodV` (`:607–611`), `sober_jni_callback_was_called` (`:1432–1441`); thunk return `0x27eafb3` vs host ALT return near `guest_app_shell.c:1319–1323`; marker emit site gated by `shell_cb && render_ok` | Additive, default-OFF recorder captures notifier hit, thunk return address and jlong argument; A/B proves it distinguishes `0x27eafb3` guest return from an ALT/host return; the recorded check is enforced by the gate with all opt-ins unset (decision 038 §2 requires native provenance to exist before the marker can pass); B09 handler fragilities fixed additively | **M = 2** | BLK-07 |
| **BLK-09** | Gate / tests / docs / assurance (lead + verifier + Team D) | Withhold OR-chain `guest_app_shell.c:1337–1375`; D02 gaps: `SOBER_STARTAPP_V2` alias, `SOBER_HELPER_REPAIR` v1, `SOBER_CTOR0`/`CTOR_RANGES=all`, `SOBER_BIONIC_TSD_GOT`, `SOBER_PROP_*`; `tests/app-shell.sh` stale ALT assertion; README lines 105–111; `docs/interfaces/abi-map.md` | Source-tree-wide withhold audit reports zero unclassified guest-mutating knobs (report 471's 15 gaps now in-chain; D09 file-local 86/92 in-chain + 6 benign; residual Tier-2/3 + `SOBER_PROP_*` open); `app-shell.sh` asserts absence of ALT/HONESTY/seed lines plus notifier provenance, has a **documented green exit criterion** (exact provenance line, emitting code path, all opt-ins unset), and stays **RED** until BLK-07/08 pass; the two manual "definition of done" notices and ≥1 structural/smoke check are automated in ctest (currently only 2 trivial self-checks, CI-gating ~15%); README "done = all true" 4/4 checked; `abi-map.md` current; final verifier report with all invariants PASS, ctest 2/2, ldd libc-only, engine SHA unchanged, zero marker emissions; XIO teardown flake documented as environmental | **M = 2** | BLK-08 |

**Total effort weight: 25 units** (L/M/H/VH = 1/2/3/5). The critical path is
BLK-01 → BLK-02 → BLK-03 → BLK-04 → BLK-06 → BLK-07 (VH) → BLK-08 → BLK-09;
BLK-05 runs in parallel but must land before BLK-07 is interpretable.

---

## 4. Blocker notes (acceptance detail that did not fit the table)

**BLK-01 — substrate.** This is the root cause named in decision 013 itself:
guest TSD keys land in the private Bionic array, not glibc's key space; the
`pthread_getspecific` GOT at `0x72298b0` still binds glibc at runtime
(`sentinel-abcdef-trace.md`). Three distinct manifestations are one fix: SIGTRAP
at `0x6caef66`, the `realloc()` abort from a static TSD seed buffer, and the
default `0x2eb4b65` fault from the unseeded JavaVM* global `0x7cdec78`. A
scheduler "neutralize" that zeroes `+0x70` of the wrong object (`0x722e540` is a
TLS-key descriptor, not the scheduler) triggers the RBXCRASH reporter; the real
state is per-thread at `0x2a0de60 → TLS+0x410 → obj+0x70`
(`tls-2a0de60-r70-correction.md`). Every one of these is currently masked by an
opt-in that force-withholds, so none counts as env-free progress. A3 adds two
substrate caveats: the 82 symbolic relocations still resolve through
`dlsym(RTLD_DEFAULT)` (glibc), so `pthread_key_create`/`delete` at
`0x72297a0`/`0x722a2c8` remain glibc-bound; and four latent allocator-contract
deviations M1–M4 remain in the default bridges (common path structurally
matched, so not the observed fault cause).

**BLK-02/03/04 — the single fault chain.** `stability-fault-audit.md` and A13
prove `0x2e7b8e2 → 0x24ad562 → 0x2772403` (plus `0x2ec1042`, `0x31a1360`,
`0x2772161`) is *one* dependency chain: skipped `ctor[0]` leaves the FFlag
registry control block at `0x72a0520+0x38` NULL → the sole singleton writer
`0x238497d` never runs → `0x30ef840` returns 0 → the controller installer
dispatched at `0x23389f7` faults at `0x24ad562` before its genuine store
`0x24aea9c` to `app+0x440` → `userDidLogin` faults at `0x2772403`. Skipped ctors
1/3/4 are irrelevant (A5), and the genuine delegate comes from guest helpers
`0x1dd55f2`/`0x25f7e7e` with real guest vtables (A8) — so no fake object is
needed at any link. Withhold every new knob until the guest writes are observed
in default code.

**BLK-05 — JNI fidelity is necessary but not sufficient.** Seven StartAppParams
getters have no handler, several return wrong JNI types, and the FFlag JSON
override's synthetic `jstring` is not a real Java String the guest parser can
consume (`appstate_fields.md`). Correcting these is cross-cutting: it unblocks
BLK-03/04 and is a prerequisite for BLK-07.

**BLK-06 — render: mechanism vs claim (A4).** The native mechanism is ~90% done;
the *claim* "the app-shell frame rendered" is 0%. `0x649d422` is a tail `jmp` to
`eglSwapBuffers@plt`, invoked today by the harness with a synthetic `0x48`-byte
context; the GOT counter cannot tell a host-caused swap from a guest frame loop.
The fromSurface hook exists in code but is never installed. Fixing the
`extra=1` pbuffer bug in Route A and adding caller provenance are the two
concrete upgrades; `render_ok` must not be satisfied by our own test-frame swap.

**BLK-07 — the decisive blocker.** `onAppShellReloadNeeded` is statically dead
natively (`appshell-reload-trigger.md`); `onAppBridgeNotification` is rejected;
the notifier `0x27eaf6e` has no static caller and is reachable only through the
registered dispatch slot `0x6dbd860`. Driving the real introducer order from
C10 §3 E6 / B10 #7/#11 and proving the notifier hit + `0x27eafb3` return is the
only route to the callback leg. This is research-level because each prerequisite
must first be made env-free (BLK-01…06).

**BLK-08 — gate cannot currently prove leg C.** C09 §6.4: `sr_dispatch_call`
records only name, count and `last_callback`; no return address, notifier hit,
stack frame or argument value. Decision 038 §2 states that until native
provenance instrumentation exists, the coded gate cannot prove leg C and the
marker stays withheld. So even a genuine callback observed under diagnostics
would not legitimately pass without this work plus removal of the diagnostic knobs.

**BLK-09 — assurance drift.** D02 found 78 confirmed Tier-1/2 withhold gaps plus
aliases and a dynamic `SOBER_PROP_*` family; D10 R2 keeps the `app-shell.sh`
contract open despite decision 038 claiming the stale assertion removed (a
documentation/state discrepancy the lead must reconcile). D10 R6/R8: stale docs
and no final invariant re-run after integration. A9 confirms report 471's 15 gaps
are closed in source (D09: 92 referenced, 86 in-chain, 6 benign, 0 file-local
gaps) but that D09 parses only `guest_app_shell.c`, and that `app-shell.sh` §3
requires a provenance line the harness never emits with no documented green exit
criterion; verification-intent coverage is ~57% and CI-gating ~15%. All are
gate-integrity items, not goal achievements.

---

## 5. Weighted percent-complete calculation

Rubric weights are the requested ones. Each category score is a fraction of its
own weight, justified from the cited evidence; the overall figure is the weighted
sum. I round to the nearest whole percent and give a sensitivity band.

| Category | Weight | Score | Contribution | Basis for score |
| --- | --- | --- | --- | --- |
| **Init** | 20% | 0.95 | **19.00 pp** | 565390/565390 relocs, `guest init complete`, ldd-clean ELF, signal ABI shim and JNI table — all delivered (decisions 006–009). A3 scores constructor dispatch 95%: default is 3614 + 3 lazy + 1 opt-in, so literal "3618/3618" needs an opt-in; the residual is scored here, while its downstream app-state consequence is scored under "App state". |
| **Render** | 15% | 0.55 | **8.25 pp** | A4 domain figure: native window ~95% (30%), guest swap counter ~85% (20%), GOT/ANativeWindow hooks ~45% (20%), surface lifecycle ~15% (15%), guest-driven app-shell frame 0% (15%) = ~57%, reported as ≈55%. |
| **JNI** | 15% | 0.70 | **10.50 pp** | Table 231/231 and IDs 49/49 complete; opt-in modules wired. Deducted for untyped/missing getters, non-real JSON `jstring`, JavaVM seed opt-in, default fault `0x2eb4b65`, and no provenance capture. |
| **App state** | 20% | 0.20 | **4.00 pp** | Root causes identified and opt-in modules exist, but `0x72cb5e0` and `app+0x408/+0x440/+0x488` are never created by clean default code; five open faults; README marks ⚠️. |
| **Callback** | 20% | 0.00 | **0.00 pp** | README metric "Genuine app-shell callback observed 0/1 — 0%"; no notifier-driven `gameLoadedCallback`; `onAppShellReloadNeeded` dead; only rejected `onAppBridgeNotification`. |
| **Gate / tests / docs** | 10% | 0.45 | **4.50 pp** | Gate enforcement and withhold chain strong (report 471's 15 gaps closed); deducted for `app-shell.sh` having no documented green exit criterion, D09/D02 residual coverage gaps, README checklist 0/4, stale `abi-map.md`/docs, no final invariant re-run, and A9's ~57% verification-intent / ~15% CI-gating coverage. |
| **TOTAL** | **100%** | — | **46.25 pp** | **≈ 46% complete** |

**Sensitivity:** reasonable alternative judgements move the figure only within a
narrow band — e.g. JNI 0.85 and render 0.65 and docs 0.60 gives
19 + 9.75 + 12.75 + 4 + 0 + 6 = **51.5%**; conservative JNI 0.60 / render 0.50 /
docs 0.35 gives **~43%**. So the defensible statement is **roughly 45–50%
complete** (central estimate ≈46%), dominated by the two 20% categories that are
near zero. For cross-check, A3's separate guest-init domain estimate is ~88%, but
that domain is dominated by loader mechanics and is not the whole-project figure.

**Why the figure cannot be higher without new work:** the two heaviest remaining
categories — Callback (20%) and App state (20%) — are at 0.00 and 0.20. Even
perfect scores in Init, Render, JNI and Gate/tests/docs sum to only 20+15+15+0+0+10
= 60% while those two are zero. Reaching 100% therefore *requires* BLK-03/04
(app state) and BLK-07/08 (callback + provenance) to land in env-free default
code; no amount of additional diagnosis or opt-in wiring changes the number.

---

## 6. What prevents 100% today (explicit)

1. **No qualifying callback.** No run has ever shown the guest entering notifier
   `0x27eaf6e` with `placeId ≠ 0` and returning to `0x27eafb3`; the only genuine
   callback observed is the rejected `onAppBridgeNotification`, and
   `onAppShellReloadNeeded` has no native reader. (BLK-07 — 20% weight at zero.)
2. **No clean app state.** `0x72cb5e0`, `app+0x408/+0x440/+0x488` are created only
   by synthetic seeds; the genuine writers (`0x238497d`, `0x24aea9c`,
   `0x25d7e75`/`0x25f7e7e`) never execute on the default path, which faults at
   `0x2eb4b65`. (BLK-01…04.)
3. **The gate cannot prove provenance.** Name-only counting cannot distinguish a
   notifier-driven callback from a harness-triggered thunk; decision 038 forbids
   the marker until native provenance instrumentation exists. (BLK-08.)
4. **The render leg is synthetic.** `0x649d422` is invoked by the harness with a
   fake context; no guest-created EGL context, bound ANativeWindow, guest GLES
   draw or vtable-dispatched frame loop has been observed. (BLK-06.)
5. **Assurance/acceptance artifacts are incomplete.** Residual withhold-chain
   gaps and aliases, `app-shell.sh` having no emitted provenance line / no
   documented green exit criterion, README checklist 0/4, stale `abi-map.md`,
   two manual-only done items and ~15% CI-gating, and no final post-integration
   invariant re-run. (BLK-09.)
6. **Overarching constraint:** all of the above must be achieved on the frozen
   engine (SHA `3e498d6494…` unchanged), with every `SOBER_*` opt-in unset and
   no ALT/HONESTY lines. Under decisions 013/038 the marker stays **WITHHELD**
   and `tests/app-shell.sh` staying RED is the correct state. **No success is
   claimed.**

---

## 7. Honesty limits

This is a synthesis of other auditors' reports plus read-only inspection of
`PROGRESS.md`, `README.md` and the cited decisions. No build, harness run,
debugger capture, REA query or test was executed for this review; no evidence
file was independently reproduced. Effort weights and category scores are
planning judgements, not measurements. The project's sources are still being
edited, so knob counts/line numbers cited from D02/D10 are snapshot-specific —
re-verify anchors before acting; REVIEW_A3 and REVIEW_A9 landed after my first
source snapshot and were folded in at final write-up. Nothing here relaxes
decision 013; the
`app shell rendered` marker remains WITHHELD throughout.
