# D04 — Baseline tracking: default + full-opt-in guest faults

Worker D4, logical Team D. Date 2026-11-20. Strict decision 013 throughout.

**Scope discipline:** this file is the only artifact written. `tests/fault-baseline.sh`
was audited **read only** and is unchanged; no script, `src/` file, `CMakeLists.txt`,
gate or engine byte was touched. The single table below is the record of reference.

## Provenance / frozen target

- Script of record: `tests/fault-baseline.sh` (default-OFF, informational only; exit 0;
  not in ctest, not called by `verify-native.sh`, emits/checks no marker). It runs the
  exact default no-start-app env and records the async fault at guest RVA `0x2eb4b65`.
- Engine: `/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so`,
  BuildID `204ad049599c96afb75b8a21d5051063e821d6c9`, SHA-256
  `3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335`
  (re-read unchanged at audit time). All addresses below are guest RVAs.
- Status meanings: **fixed** = a verified, wired real-guest-code/opt-in fix removes the
  fault from its trigger configuration (it must stay absent on future runs);
  **open** = the fault still reproduces and blocks the real path (record the new RVA if
  a later fix moves it deeper). "Fixed" is a statement about the fault, never about the
  decision-013 marker, which stays WITHHELD in every row.

## Env-set legend (concrete trigger environments)

| Tag | Environment (all `SOBER_*` diagnostics not listed are **unset**) |
| --- | --- |
| **D** (default / baseline) | `SOBER_RENDER=1 SOBER_APP_SHELL=1 SOBER_SCHEDULE_RUNNABLE=1 SOBER_APP_SHELL_WAIT_MS=3000 EGL_PLATFORM=x11 LIBGL_ALWAYS_SOFTWARE=1 LD_LIBRARY_PATH=<root>/build/ndk-stubs timeout 90` — exactly `tests/fault-baseline.sh`. Dropping `SOBER_SCHEDULE_RUNNABLE` does not change the fault. |
| **F** (full opt-in diagnostic stack) | D's render/window vars **plus** `SOBER_TLS_SEED=1 SOBER_TLS_BYTES=4096 SOBER_NO_SCHED_NEUTRAL=1 SOBER_REPLAY_FFLAG_CTOR=1 SOBER_SEED_GLOBAL_STRING=1 SOBER_SEED_JNI_VM=1 SOBER_FAITHFUL_JAVAVM=1 SOBER_BIONIC_SF_FULL=1 SOBER_JSON_FLAG_BRIDGE=1 SOBER_START_APP=1` (and, in the "stable full stack" variant, `SOBER_SEED_USER_CONTROLLER=1 SOBER_SEED_APP_DELEGATE=1 SOBER_STARTAPP_PARAMS=1`). Named F variants below state the single knob added/removed. These are the A/B stacks in `asb-ab.sh` / `gp-ab.sh` and PROGRESS phases 8–9. |

## Single baseline table (regression reference)

| Guest RVA | Fault / root cause | Trigger env set | Status | Regression check (future runs) |
| --- | --- | --- | --- | --- |
| `0x2eb4b65` | `mov (%rdi),%rax`, rdi=NULL — worker from real-args `nativeAppBridgeAppStart 0x247dc8d`; unseeded `JNI_OnLoad` JavaVM global `0x7cdec78` (helper `0x227b9e2`). Pre-existing gap (clean HEAD `b8bdab0` faults earlier at `0x2456079`). | **D** (no opt-ins). | **open** | `fault-baseline.sh` must keep recording this RVA with `signal=11, rdi=(nil)`, stdout exactly `guest init complete`/`first frame rendered`, engine SHA/BuildID unchanged, zero marker. `SOBER_SEED_JNI_VM=1` removes it; that is diagnostic only. |
| `0x2e5d42a` | NULL `std::string` vtable on the worker — FFlag registry global `0x72a0520` unbuilt because decision 008 skips `init_array[0]`. | **F − `SOBER_REPLAY_FFLAG_CTOR`** (e.g. `SOBER_SEED_JNI_VM`/`SOBER_FAITHFUL_JAVAVM` with ctor replay off; `ab_on`, `vmseed`, `phase8-all`). | **fixed** | `SOBER_REPLAY_FFLAG_CTOR=1` replays ctor `0x2e62390` (guard `0x72a0a30` = 257, global non-NULL); fault is gone. Must not reappear in F. |
| `0x2e7b8e2` | `lock incq 0x8(%rax)`, rax=0 — missing shared_ptr control block at FFlag handle `0x72a0520+0x38`; postsettings replay before ctor[0] installs it. | **gseed + postsettings replay with ctor replay OFF or ordered later** (`realstate4`, `fu-early`, `gseed-B`; also `SOBER_JSON_APP_SINGLETON` before ctor). | **fixed** | ctor replay installs the control block via guest code `0x2e7b850 → 0x2e79af0 → 0x2e77400`; current `guest_app_shell.c` places ctor replay (`:913`) before JSON app-singleton (`:933`). Must stay absent; if it recurs, check replay ordering first. |
| `0x6caef67` | `int3` at `0x6caef66`, the RBXCRASH reporter tail after RSL panic `257f718-Context,20` — harness zeroing scheduler `+0x70`; also reachable as "TaskScheduler before flags loaded" under early postsettings. | **F with scheduler neutralize ON** (`SOBER_NO_SCHED_NEUTRAL` unset / `SOBER_SCHED_R70_KEEP` unset: `int3diag`, `r70`, `tsdgot`, `nest`); residual A/B trigger `asb_B` (early `0x2383c18` replay). | **fixed** | Root cause neutralized by `SOBER_NO_SCHED_NEUTRAL=1` + heap TSD seed (or `SOBER_SCHED_R70_KEEP=1`); full stack start-app path then completes without crashing. Must be absent in F; if it recurs, audit `+0x70` zeroing and flag-load ordering, not the gate. |
| `0x24ad562` | `mov (%rax),%rcx`, rax=0 from `0x30ef840` — controller installer `0x24ad50a` reached with FFlag=1 but app singleton `0x72cb5e0` unpopulated. | **F with flag=1 (`SOBER_JSON_FLAG_BRIDGE` / real-appstate) and no app-singleton population or controller/delegate seeds** (`realstate2`, `realstate`, report 463 ALT+JSON_FLAG_BRIDGE). | **open** | Still a blocker (stability-audit fault #2). Fix needs `SOBER_REPLAY_FFLAG_CTOR` before `SOBER_JSON_APP_SINGLETON=1` with a guest-shaped `java/util/List` (writer `0x238497d`). Expect this RVA until the singleton is genuinely built; record any deeper RVA. |
| `0x2772403` | `cmpl $0,0x140(%r14)`, r14=NULL — genuine user controller at `app+0x440` missing in `SingleSurfaceApp::userDidLogin`'s callee `0x2772384`. | **`SOBER_START_APP=1` (or `SOBER_REAL_APPSTATE_PATH`) without `SOBER_SEED_USER_CONTROLLER`; F minus controller seed** (`final_baseline`, `tsdheap`, `jnionload_baseline`). | **open** | Masked, not fixed, by the synthetic `SOBER_SEED_USER_CONTROLLER`; real installer `0x24ad50a → 0x24ae1fe` is not wired (stability-audit fault #3). A genuine fix must also clear the next gate `0x2772161` (`app+0x408` delegate). |
| `0x2ec1042` | Flag=1 path dereferences unpopulated app singleton `0x72cb5e0` (also reported when our synthetic `jstring`/List is not a real Java object). | **`SOBER_REAL_APPSTATE_PATH=1` + `SOBER_REAL_APPSTATE_FLAG=1` (flag forced to 1), or `SOBER_JSON_FLAG_BRIDGE=1` with bad singleton shape** (`realpath1/3`, `asb_A/C/D`, `gp_B_off`). | **open** | Persists even after publishing the singleton via the A/B bridge (`asb_C/D`); source comment `guest_app_shell.c:664-665` still documents it. Needs faithful JNI/Java object emulation, not more fake vtables. Do not treat disappearance under an unrelated knob as a fix. |
| `0x31a1360` | NULL `app+0x488` / bogus handle (`rax=0xabcdef0123456789` is only the stack canary) on the game-loaded path; no genuine `gameLoadedCallback` observed. | **F + `SOBER_GAME_LOADED_PATH=1`** (± `SOBER_STARTAPP_PARAMS=1` / `SOBER_GAME_PARAMS=1`; `game`, `gp_A_on`, `gp_A_counters`, `gp_B_off3`). | **open** | Reproduces with both the inline four-getter stub and the faithful 24-getter `StartGameParams`. A pass requires notifier `0x27eaf6e` entered with placeId and thunk return `0x27eafb3` — not this fault and not the ALT path. |

## Comparison rules

1. **Default row is the anchor.** Every regression run of `tests/fault-baseline.sh` must
   show `0x2eb4b65`, phase-2/3 markers present, rc=0 for the script (harness rc=139),
   zero `app shell rendered`, unchanged engine identity. Never wire the baseline into
   ctest or `verify-native.sh`.
2. **Fixed rows must stay absent under F.** A reappearance of `0x2e5d42a`, `0x2e7b8e2`
   or `0x6caef67` is a regression of the ctor-replay / replay-ordering / TSD-scheduler
   work, not a new manifestation to normalise.
3. **Open rows may move, not vanish quietly.** When an open fault is eliminated, record
   the new faulting RVA and the exact knob delta in this table rather than declaring
   success. The chain `0x2e7b8e2 → 0x24ad562 → 0x2772403` is dependency-ordered
   (stability audit §1); change one opt-in per A/B.
4. **Decision 013 is invariant.** All opt-ins used to reach or avoid these faults are
   in the force-withhold chain; none may emit the marker. `onAppBridgeNotification`,
   accessor callbacks and the ALT thunk remain non-qualifying. Engine SHA/BuildID must
   be unchanged before and after every comparison run.
