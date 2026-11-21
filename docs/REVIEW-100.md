# Full project review — percent complete, roadmap and risks

Date: 2026-11-20. Scope: every phase (1–10), every source/test/doc area, every
decision (000–038), and the private-repo delivery. Fourteen independent auditors
(A1–A14) reviewed separate domains in parallel; their reports are
`analysis/REVIEW_A*.md`. This document is the lead synthesis.

**Engine identity used throughout:** AppImage BuildID `204ad049599c96afb75b8a21d5051063e821d6c9`,
SHA-256 `3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335`.
No Flatpak (`791c3f9e` / 2.745) RVA is used anywhere in the shipped source.

## 1. Headline verdict

**Overall: ≈66% complete (sensitivity band 61–70%)** after the scale wave. The original 14-audit baseline was 46%, the integration team 57%, the 40-agent frame push 61%, wave 3 62%, wave 4 64%. A seeded diagnostic reaches full G8 window ACCEPT (C0–C10) and now also drives the genuine MessageBus/notifier chain to `gameLoadedCallback`. The id=1 node discovery bug was a host-side missing indirection (`bus+0x288` is a list container, node at `+0x08`), fixed read-only. The provenance gate was strengthened to require guest-entered node dispatch, so the host-triggered re-drive is correctly rejected and the clean callback leg remains 0/1. Clean `seeded_state==0` render green is proven impossible without real Java surface lifecycle. Marker stays WITHHELD.

**Follow-up (teams #485–#496 and #497–#501):** the singleton predicate blocker
is fixed with genuine guest flag-setter calls, and the old fault `0x24ad562` is
gone. The callback path still reaches the second guest guard, the flags-loaded
latch `0x7cdd149`: `RBXCRASH ... Can't initialize the TaskScheduler before flags
have been loaded` at `0x6caef67`. All six guest `movb $1,0x7cdd149` writers are
mapped in `analysis/A15-flags-latch-route.md`. The crashpad abort was re-root-caused
to crashpad `HandlerMain`'s own argv validation (`fprintf(stderr,"Usage")` through
the Bionic `__sF+0x130` FILE*) rather than an intrinsic stdio bug; a valid JNI
`String[]` argv removes the stdio abort but HandlerMain still does not return, so
every route stays opt-in and `SOBER_FLAGS_LATCH_ROUTE` defaults to `none`
(`analysis/A14-flags-loaded-latch.md`). Callback credentials and latch-ready
prerequisites were hardened (`analysis/callback/task494-genuine-leg.md`,
`task500-latch-ready-prereqs.md`).

**Render leg reached full G8 ACCEPT in a seeded diagnostic** (verdict=1,
reason=NONE, C0–C10 pass): opt-in `SOBER_FRAME_EGL_INIT_DRIVE=1` re-drives genuine
guest EGL init for real ACQUIRE/DIMS RAs, `SOBER_FRAME_FLOG_SINK=1` observes the
real guest log, and `SOBER_FRAME_FLOG_ENABLE=1` raises the JNIAppBridge log level
via the guest's own registration thunk/setter and restores it afterwards. The
real H6 text is guest-formatted and parsed with the true sink RA `0x232e080`
(`analysis/H6_created_anw_sub.md`). The default path is unchanged
(`R8_NO_ACQUIRE`, withhold=0). Every accepting run is seeded and force-WITHHELD;
no string, handle, RA, hook, swap or callback is fabricated. No notifier hit, no
`gameLoadedCallback(J)V`, no marker, no fabricated evidence.

**Wave 4 (#507–#510, #516):** the flags-loaded latch is now genuinely set in an
opt-in run. `src/jni/crashpad_return.c` supplies real AF_UNIX `SOCK_SEQPACKET`
IPC with `SO_PASSCRED`, a 32-byte `CheckCredentials` server-peer message and a
Bionic `__sF+0x130` `fputs`/`fflush` bridge; `nativeRunCrashpadHandler 0x2eca720`
then returns normally with `latch=1` (verified `crashpad handler: succeed`, exit
13 withhold, no SIGSEGV). The app-shell phase completes: singleton, app+0x408 and
app+0x488 are installed, all JNI exports run. The dialog-writer latch route is
proven impossible (`analysis/task510-dialog-cppproxy-route.md`). A MessageBus
pre-init wrapper is wired but not yet exercised cleanly
(`analysis/wave4-messagebus-preinit.md`). The remaining callback blocker is the
id=1 MessageBus node: `publish_seeded` reports NO_NODE with guard
`0x72a31ba=0`, so no notifier-driven `gameLoadedCallback(J)V`. Marker still
WITHHELD; score ≈64%.

**Scale wave (#522–#527 + 3 parallel analyses):** the NO_NODE blocker was a
host-side discovery bug, not a missing guest prerequisite. `bus+0x288` holds a
0x18-byte intrusive-list container; the 0x60-byte id=1 node is linked at
`container+0x08` (`analysis/scale-a1-node-registration.md`). `publish_default`
now dereferences one extra level read-only, and a seeded run reaches
`gameLoadedCallback` (`MessageBus publish rc=0`, `callbacks=25`). The provenance
gate was strengthened with a 3-frame stack walk (`[rbp_not+8]==base+0x27eaf32`
and node dispatch entered from guest `.text`), so the host-triggered re-drive is
correctly rejected; `callback=0` and the marker stays WITHHELD. Worker-3 proved a
clean render green is impossible without real Java surface lifecycle; worker-5's
N1–N9 negative audit stayed fail-closed and residual withhold gaps were closed.
Score ≈66%.

**Wave 6 (20 agents, one Craft Anything CLI, authoritative SHA-matched DEX):**
a research-only AVD oracle showed main-process boot sets the flags-loaded latch
without crashpad, that authoritative default surface lifecycle uses
`ActivityNativeMain`/`RBXSurfaceView` → JNIAppBridge (not AGDK thunks), and that
MessageBus event-1 has no DEX publisher. The gate gained a JavaVM vtable
identity leg and `nativeInitClientSettings` fidelity was corrected from the
authoritative DEX. The exact in-process latch writer is still being isolated;
none of this makes a clean callback pass. Marker stays WITHHELD.

**Wave 5 (20 agents, shared Obsidian vault `/home/user/sober-vault`):** the clean
path was exhaustively investigated and found unavailable in this harness. The
flags latch is process-local and only written by the crash-handler subprocess;
`0x27eaf04` has no static caller and requires a guest DataModel lifecycle event
the harness never produces; and clean render needs real Android `Surface`,
AGDK nativeData callbacks, IoEvLoop/RenderJob and FFlag state that a stock native
JVM cannot supply. A01–A14 vault notes and worker reports N1–N20 confirm every
alternative either requires a forbidden runtime or is a withhold-worthy seed.
Fail-closed improvements: gate-only render-provenance emitter, SKIP exit 1,
inherited `SOBER_*` scrub. No gate condition was relaxed. Marker stays WITHHELD;
score ≈66%.

The original goal — a native Linux ELF running the real Roblox client with a
genuinely rendered login/app-shell frame witnessed by a notifier-driven
guest→Java callback under clean state — is **NOT PROVEN**. Infrastructure is
strong; the decisive claims are at zero. The `app shell rendered` marker is
correctly **WITHHELD** under decisions 013/038, and `tests/app-shell.sh` is
intentionally RED.

| Milestone | Status |
| --- | --- |
| Native ELF, no translation layer | ✅ proven |
| Guest init: 3,618 ctors / 565,390 APS2 relocations | ✅ proven |
| Native window + a guest-rendered first frame (`0x649d422`) | ✅ proven (narrow) |
| Full JNIEnv / typed JNI surface | ⚠️ partial, mostly opt-in |
| Real app state (singleton/controller/delegate/488) | ⚠️ diagnosis mostly done, no clean default-path creation |
| Genuine `gameLoadedCallback(J)V` from notifier `0x27eaf6e` | ⬜ never observed |
| Login/app-shell frame | ⬜ not proven |
| Strict gate + decisions 013/038 | ✅ enforced; provenance not yet coded positively |
| `app shell rendered` marker | ⬜ WITHHELD |

## 2. Weighted score

| Category | Weight | Score | Contribution |
| --- | --- | --- | --- |
| Guest init / loader / bootstrap | 20% | 0.95 | 19.00 |
| Native render / frame | 15% | 0.60 | 9.00 |
| JNI bridge / typed fidelity | 15% | 0.85 | 12.75 |
| App state (singleton, controller, delegate, params) | 20% | 0.40 | 8.00 |
| Genuine app-shell callback | 20% | 0.00 | 0.00 |
| Gate / tests / docs / delivery | 10% | 0.85 | 8.50 |
| **TOTAL (after integration team)** | **100%** | — | **57.25 ≈ 57%** |

The original audit was 46%. The integration team raised JNI (default slot modules
wired, `jni_probe` PASS), render (guest GOT hooks/lifecycle installed, though
guest-tagged swaps/draws are still zero) and gate (positive provenance wired).
App state still faults when prerequisites are missing, and the decisive callback
leg remains zero — so the marker is correctly WITHHELD.

Even perfect scores everywhere else would reach only 60% while Callback and App
state remain zero — those two legs carry 40% of the goal.

## 3. Domain audit results

| Auditor | Domain | Result |
| --- | --- | --- |
| A1 | Original goal and decisions 000–038 | Infrastructure partial at goal level: **NOT DELIVERED** |
| A2 | Native build / hard constraints | **≈90%**; bit-identical reproducible ELFs, libc-only ldd, frozen identity; gaps are automation/hygiene |
| A3 | Guest init / relocs / allocator / TSD | **≈88%**; 565,390 relocations, 3,614 default ctors + explicit arena/thread init, signal/TSD shim; ctor[0] still opt-in |
| A4 | Native render | **≈55%**; native frame proven, guest frame 0%; `render_ok` set by our own swap |
| A5 | JNI bridge | Default 74/231 = 32% faithful; even with opt-ins 138/231 slots stay stubs; 22/49 cached IDs are placeholders; provenance recorder was dead code (now fixed) |
| A6 | App state | **≈37%**; root causes ~90% mapped, env-free realized state ~20%, admissible evidence 0% |
| A7 | Genuine callback | Static chain fully mapped, runtime occurrence **0%**; no notifier hit, placeId, or RA pair ever observed |
| A8 | Strict gate | Gate upheld, no live false-green; all Tier‑1/2 mutators withheld; provenance required by policy but not positively coded |
| A9 | Tests / verification | **≈57%** intent, **≈15%** CI-gated; strict test correctly RED |
| A10 | Documentation | **≈67%**; honest but numerically stale and internally inconsistent |
| A11 | Delivery | **≈80%**; private repo synced at `afed56d`; single squashed root commit |
| A12 | File / binary inventory | 680 tracked files, 124,314 text lines, 78 binaries; source clean, 387 generated `build/` files committed |
| A13 | Roadmap | Nine blockers, 25 effort units to 100% |
| A14 | Honesty / false-green | Core posture sound, but several public facts stale — not safe to publish as-is until fixed |

## 4. Roadmap to 100%

Effort: L=1, M=2, H=3, VH=5. Critical path: BLK‑01→02→03→04→06→07→08→09
(BLK‑05 parallel). Total **25 units**.

| ID | Blocker | RVAs | Acceptance (all opt-ins OFF) | Effort |
| --- | --- | --- | --- | --- |
| BLK‑01 | TSD / JavaVM / scheduler substrate | GOT `0x72298b0`/`0x72295c8`; keys `0x7282000`/`0x72a1b08`; TLS `obj+0x70`; int3 `0x6caef66`; fault `0x2eb4b65` | TSD bound by default, no int3/realloc abort, JNI_OnLoad globals live | H |
| BLK‑02 | FFlag ctor[0] + registry control block | `0x2e62390→0x2e618b0→0x2e61280`; `0x72a0520+0x38`; faults `0x2e7b8e2`/`0x2e5d42a` | control block non-NULL, faults absent, ordered before post-settings | M |
| BLK‑03 | App singleton + service vector | writer `0x238497d`; gate `0x6096928`; reader `0x30ef840`; fault `0x24ad562` | `0x72cb5e0` non-NULL via guest code, fault gone | H |
| BLK‑04 | Controller / delegate / `app+0x488` | `0x23389f7→0x24ad50a→0x24ae1fe→0x24aea9c`; `0x24af2e6`; `0x25d7e75`/`0x25f7e7e`; faults `0x31a1360`/`0x2772403` | all three fields guest-written, no fake seeds | H |
| BLK‑05 | Typed JNI surface | getters `0x232dbbc`/`0x26feb9b`; slots 90/96/98, 111/112, 114–136, 167–172; `0x227b2c0`/`0x227b4eb` | no generic-String substitution; no `0x6c8f340` retry loop | H |
| BLK‑06 | Guest render / surface binding | `0x649c21e→0x649c272→0x649d422`; GOT `0x722a1a8`/`0x72294a8`; vtable `0x71912e8`; `0x2773194` | guest-tagged swap + ≥1 guest GLES draw; `render_ok` not self-set | M |
| BLK‑07 | Genuine GameLoaded callback | notifier `0x27eaf6e`, publish `0x27eaf2f`, invoke `0x27eaf4c`, slot `0x6dbd860`, thunk `0x25848e0`, RA `0x27eafb3`, introducers `0x2ebd51a`/`0x2ebca9e` | notifier hit with `RSI=placeId>0`, RA pair, callback log; no ALT | VH |
| BLK‑08 | Positive provenance in gate | `sr_dispatch_call` / `sr_CallStaticVoidMethodV` in `native_jni.c` | gate consumes notifier RA as a positive condition with opt-ins unset | M |
| BLK‑09 | Assurance and clean publication | withhold chain, `app-shell.sh`, README, `abi-map.md` | zero unclassified mutators, green exit criterion documented, 4/4 DoD, final re-run | M |

## 5. Risk register (documentation and delivery)

| ID | Risk | Fix status in this review |
| --- | --- | --- |
| R1 | README says provenance "required" in a ✅ row while gate is name-only | reword: required by policy, not yet positively coded |
| R2/R4 | README `guest-init-host` SHA and counts stale; progress.svg marks provenance/probe green | regenerate with current SHA, set both tiles orange |
| R3 | "22 withhold knobs" understates the live OR-chain | cite live chain, drop fixed count |
| R5 | verification badge links stale report 471 | link D12, stamp 463/471 superseded |
| R6–R9 | phantom `SOBER_SEED_JNI_STRING_BRIDGE`, "open inconsistency", "B+C not enacted", stale HONESTY claims | amend 013/037/PROGRESS/memo |
| DOC‑03 | `abi-map.md` cites Flatpak-bound Ghidra evidence as authoritative | add wrong-build caveat |
| H1 | 387 generated `build/` files committed; orphan Win32 object; stale SHA | hygiene follow-up |
| H2 | `SOBER_PROP_*` and Tier‑3 config seeds outside withhold chain | add for strict parity before any future green |

## 6. Integration follow-up (team `TEAM-HCUY-MAGB-VPX7`)

A three-worker integration team is wiring the delivered default-ON modules into
the real default path. `analysis/MODULE_INTEGRATION.md` is the authoritative
one-module-per-slot table and boot order; it resolves duplicate pairs
(`fields_default` over `platform_fields`, `alloc_default` over `alloc_contract_fix`,
`tsd_default` over the inline TSD knob, `javavm_default` over `javavm_semantics`,
etc.). The render contract is now
`render_ok = phase3_done && guest_tagged_render_ok()`, with a guest-tagged swap
and counted guest draw decisive; `ANativeWindow_fromSurface` is supporting
evidence, not a hard requirement (T3‑5 question resolved by lead).

The 46% score stands until the wiring is built and A/B verified; it will be
recomputed in the final verification task (#480). Marker stays WITHHELD.

`analysis/T2_5_flags_latch.md` has now landed: the latch write at `0x2eb7520`
is unconditional, but `nativeInitClientSettings 0x2eb7155` exits early on parse
ctx byte `0x7697d08 == 1` (and on empty jstrings), so our calls return rc=1 with
latch 0. The legitimate sibling writer is `CrashpadHandlerMain 0x2ec9c30(int,
char**)`: argv `"--channel" "LIVE"` populates global `0x72a3710` and reaches
`0x2ec9eed → 0x2ec9fae`, writing `0x7cdd149 = 1`. This was routed to worker-2
for default-path wiring; it is guest initialization, not a crash fix, and emits
no callback.

### Frame root cause (40-agent push)

`analysis/R1_zero_tagged_swaps.md`, `R3_guest_egl_context.md`, `R6_swap_provenance.md`
and `P8_anw_guest_consumption.md` agree: `render_guest` creates a genuine guest
EGL context (`ctx+0 = base+0x71912e8`, swap slot `[3] = 0x649d422`) but stops at
context creation. The only swap traverses guest GOT `0x722a1a8` from the host
Route-B call at `guest_render.c:354`; `0x649d422` is a tail-jump, so its RA is
correctly classified host/synthetic. `nativeData[+0x98]` (and `+0x88`/`+0xa0`)
are NULL, so redraw thunk `0x2a20a00` early-returns before `jmp *%rax` at
`0x2a20a20`; the guest draw dispatcher `0x6496d0c` and render loop never run.
Fixing this needs genuine JNI-installed callbacks plus the app-start/game-loaded
chain and notifier `0x27eaf6e` — all currently absent. Guest-tagged=0 is correct,
not a counter bug. `analysis/R5_gles_draw_procs.md` adds a counter blind spot:
plain GLES PLT GOTs `0x7229fc8` (glClear), `0x722a040` (glDrawElements) and
`0x722a048` (glDrawArrays) are not hooked, only the instanced extension procs,
so a real plain-draw frame would still report `draws=0` until those are wrapped.
`analysis/R10_render_log_evidence.md` confirms no guest surface-accept log
(`Created ANativeWindow …` at `0x26feaf7`) has ever appeared. `analysis/R4_surface_lifecycle.md`
adds the byte-level reason: the four thunks consult `nativeData+0x88/+0x90/+0x98/+0xa0`,
all NULL in our synthetic 0x200 nativeData, so the frame trigger `jmp` at
`0x2a20a20` never executes. The fix is to use the guest's own 0x278-byte nativeData
from `initializeNativeCode 0x2a1fe80` (AGDK callbacks) rather than hand-building it.

### Guest frame dispatch discovered (40-agent push)

`analysis/R9_virtual_display_path.md` located the real guest frame dispatch:
graphics-device routine `0x6490488` calls `call *0x18(%rax)` at `0x649067b`
through render vtable `0x7191300` → `0x649d422`, with guest RA `base+0x649067e`.
Driving that site with a shaped ~0x400-byte device object (`+0x160=ctx`,
`+0x260=1`, `+0x180==+0x188=0`, `+0x22f=0`, `+0x288=1`, `+0x298=0`) should make
`swaps(total/guest/synth)` move from `1/0/1` toward `3/2/1` with a wrapped GLES
draw between swaps. `analysis/G1_gate_guest_render.md` specifies the gate edit
(`render_ok = phase3_done && guest_render_ok()`), and `G7`/`F10` define the
unforgeable discriminator and false-green tests (RA interval + indirect-dispatch
byte pattern + stable recurrence + bracketed draw + guest-window provenance +
mixed-evidence taint + BuildID guard). Any success here stays a withhold
diagnostic until the notifier leg closes.

### Exact guest frame hooks (40-agent push)

- `analysis/P5_surface_callbacks.md`: the AGDK installer `0x2a23001` fills
  nativeData callbacks `+0x88=0x2a23a10`, `+0x90=0x2a23cc0`,
  `+0x98=0x2a23c40` (redraw/frame trigger), `+0xa0=0x2a23b60`;
  `+0x140` is the bound ANativeWindow data field.
- `analysis/R9_virtual_display_path.md`: guest dispatch `0x6490488` reaches
  `call *0x18(%rax)` at `0x649067b` → `0x649d422`, guest RA `0x649067e`.
- `analysis/P10_render_thread.md` / `P4_frame_dispatch.md`: cleaner helper
  `0x6493242` with `call *0x18(%rax)` at `0x649325a` → guest RA `0x649325d`.
  Shape `W[0]=base+0x71908e0`, `W[0x18]=ctx` and call `base+0x6493242(W)`;
  P4 enumerates the full 14-site dispatch family. Cadence comes from
  native `RBX IoEvLoop` (`pthread_create 0x6474599`, `timerfd`/`epoll_wait`)
  and `SurfaceController::RenderJob` (`0x276d36a`).
- `analysis/P3_egl_proc_table.md` + F4: plain GLES draws use PLT GOTs
  `0x7229fc8`, `0x722a040`, `0x722a048`; instanced draws use extension slots
  `0x77a4fe0/8`. Counting requires guest RA at `0x6496d75/0x6496dec/0x6496e1a`
  etc., not proc loads.

All of these remain **withhold diagnostics**: executing real guest code against
a synthetic device object is observability, not app-shell proof.

### Stop condition (40-agent push, `analysis/G10_final_honesty.md`)

The marker may be emitted only on a default-path run reproduced twice with:
notifier-driven `gameLoadedCallback(J)V` (`0x27eaf6e`, `placeId≠0`, RA pair
`0x2584960 → 0x27eafb3`, cached IDs), guest-tagged swap with stable RA across
≥2 swaps plus a counted guest draw and guest-bound window, `seeded_state==0`,
and all invariants green. Any render-only or seed-only signal keeps the gate
RED. Standby honest sentence if the callback remains absent:

> No notifier-driven `gameLoadedCallback(J)V` was observed — the guest never
> entered notifier `0x27eaf6e` with `placeId != 0`, and the required return-address
> pair (`base+0x2584960` → `base+0x27eafb3`) is absent — so the callback leg remains
> 0/1 and, with guest-tagged render evidence also unproven, the strict
> decision-013/038 gate correctly withholds `app shell rendered`; no login/app-shell
> frame is proven and no success is claimed.

### Frame work scoring policy (40-agent push)

Per `analysis/G6_progress_score.md`, the standing score remains **57%** until
real A/B counter logs justify a change. Projections: frame modules failing → 57%;
one guest render signal positive → ≈59%; guest-tagged swap + counted guest draw
but seeded/host-written state → ≈61%. All scenarios keep the callback leg at
0.00 and the marker WITHHELD. Tagging the host Route-B swap as guest would be a
false green (`analysis/R1_zero_tagged_swaps.md`, `analysis/R6_swap_provenance.md`).

### Worker hand-offs (2026-11-21)

- **worker-2 (#477, harness)** wired all default modules in dependency order and
  added every new knob plus four `should_withhold()` terms to `seeded_state`. Its
  default A/B still fails: FFlag ctor[0] replay reports guard `257` but
  `handle+0x38` control block is NULL, so the singleton gate returns `-3`, the
  flag setter is skipped, and a late guest worker faults through
  `0x2e5c3c1 → 0x2e5d454 → 0x2ed3ad8 → 0x6caee44 → 0x6caed7c` (the known
  `0x2e5d42a` family). `app488` ctor `0x253c93c` also faults when the FFlag
  control block is missing, so app-state calls are now guarded. No genuine
  notifier callback; `guest-tagged swaps=0`; marker WITHHELD.
- **worker-3 (#478, bootstrap/render)** wired all eight allocator bridges and
  `render_guest` GOT hooks. Build, ctest 2/2, verify-native, guest-dlopen and
  first-frame all PASS; engine SHA unchanged. `render_guest` reports installed
  with context vtable `0x71912e8`, all four lifecycle thunks called once, 48
  proc loads, 5 draw procs wrapped — but `swaps(total/guest/synth)=1/0/1` and
  `draws=0`: the only swap is our synthetic host swap. Strict gate correctly
  WITHHELD; fromSurface is present but not decisive per the lead contract.
- **worker-1 (#476)** is finishing the JNI/CMake integration (one module per
  slot, provenance dispatch hook). Until that lands, `is_genuine()` cannot fire.


## 7. Final statement

The project has built a genuinely native, translation-free host for the frozen
Android guest, with verified init, relocation, window and render mechanics, and
an extensive honest diagnostic apparatus. What it has **not** done is present a
login/app-shell frame or observe a guest-driven app-shell callback. The marker is
withheld, the strict test is red, and no success is claimed. The next unit of
work is BLK‑01 (TSD/JavaVM/scheduler substrate), not another callback shortcut.
