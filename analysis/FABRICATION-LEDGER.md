# Fabrication ledger (wave13)

Owner: lead. Rule: every host-invented object/value is debt with an owner and
a removal condition. Nothing here is a "fix"; entries are temporary, opt-in,
kill-switchable, and must be removed before any clean marker claim.

| # | Entry | Where | Owner | Why | Removal condition |
| - | ----- | ----- | ----- | --- | --- |
| 1 | `SOBER_AVD_BOUNDARY_REPLAY` exact captured arg0 | `src/jni/avd_boundary.c`, `guest_app_shell.c` | lead | Replays AVD-captured 1,533,221-byte JSON (SHA `8215519f…`) because the clean harness has no Java caller | Remove when a genuine Java/DEX startup supplies this call |
| 2 | Provenance class "AVD-captured boundary input" | `guest_app_shell.c` gate | lead | Distinguishes oracle input from seeded_state; fail-closed | Remove with #1 |
| 3 | Canonical jstring install for the boundary call | `guest_app_shell.c` | lead | GetStringUTFChars needs exact bytes; without it the guest sees NULL | Remove with #1 |
| 4 | stdio GOT rebind (`SOBER_BIONIC_SF_FULL`) | `src/compat/bionic_sf_full.c` | manager (wave10) | Guests' `fflush`/`fwrite` PLT GOT slots bypass the Bionic `__sF` bridge | Remove when the guest's own relocations resolve those slots correctly |
| 5 | Worker-scoped scheduler prime (`SOBER_SCHEDULER_GUEST`) | `src/jni/scheduler_guest.c` | manager (wave10) | Compensates for missing real worker attach; itself seeded | Remove when the real JavaVM attach path is present |
| 6 | `SOBER_SCHED_DIAG` read-only dump | `guest_app_shell.c` | lead | Diagnostic only, no guest writes | Remove when the wave13 question is answered |
| 7 | `SOBER_STOP_BEFORE_SETTINGS` + `prctl(PR_SET_PTRACER_ANY)` | `guest_app_shell.c` | lead | Diagnostic SIGSTOP attach point; no guest state change | Remove after the writer-RA question is answered |
| 8 | `SOBER_HW_WATCH` / `SOBER_HW_WATCH_ADDR` perf breakpoint | `src/jni/hw_breakpoint.c` | lead | Read-only in-process DR watch; logs RIP in a signal handler | Remove after writer analysis |
| 9 | `SOBER_AVD_BOUNDARY_UNVERIFIED` / `SOBER_AVD_BOUNDARY_PATH` | `src/jni/avd_boundary.c` | lead | **MODIFIED INPUT, diagnostic only** bisect path; SHA verification deliberately skipped and labeled | Remove after bisect; never counts toward a rung |
| 10 | Enhanced perf-watch handler (RIP/regs/bytes/stack RAs) | `src/jni/hw_breakpoint.c` | lead | Read-only diagnostic dump; first 16 hits only | Remove after writer attribution |
| 11 | Activation-scoped exec+data perf watchpoints | `src/jni/hw_breakpoint.c` | lead | Exec breakpoint after prologue `0x2312009` re-arms a write watch on this activation's `rbp-0xb8` | Remove after writer attribution |
| 12 | Fault-time thread census | `src/linux/guest_dlopen_host.c` | lead | Reads `/proc/self/task/*/comm|stat`; no guest writes | Remove after thread comparison |

**Not applied (rejected):** the proposed "real guest-addressable FFlag object"
shim at `jni_native_objects.c`. Read-only diagnostic at `guest_app_shell.c`
shows `fflag_obj = *(0x72a3058 + 0x18)` is a real host pointer
(`0x55b7d51c2dc0`), not the `0x30` token claimed in wave12. The shim would
target the wrong object; applying it would be fabrication.

## Marker reproducibility status

NOT VERIFIED on the AVD. Three attempts:
- w11 hook: reached `nativeInitClientSettings`, captured the exact arg0 bytes.
- w12 150 s / 300 s Java→native capture: reached `nativeInitClientSettings`,
  `nativeAppBridgeAppStart`, `V2InitWithParams`, `StartLuaAppDM`,
  `MessageBus.publishRaw("Credentials.isAvailableResponse")`; **zero** surface
  lifecycle, placeId, `StartGame`, or `gameLoadedCallback`.
- w13 stimulus attempt (`am start` deep link `roblox.com/games/606849621`):
  app process died; Frida attach lost; logcat shows repeated HTTP 401
  (`voice.roblox.com`, auth-dependent endpoints).

Answer to "is the marker reachable": **(c) never verified on AVD** — the
contract came from `Goal.md`/decisions 013/038. Evidence leans toward
(a)+(b): a logged-in session/valid credentials and an actual game join are
probably required, and neither was supplied. That remains a hypothesis, not a
fact.

Kill switch: the AVD cannot reach the marker in this environment and the
harness has not reached `StartLuaAppDM`. Per the agreed stop rule, this
approach is not converging; further fault-chasing waves should not continue
without first solving the oracle/auth problem.

## Wave15 additions (no shim, no guest writes)

- AVD path hook run (`w15-avd-paths.log`, not committed): ordered events
  `PARSE_ENT(0x2311516)` → `PARSE_MID(0x231c8fb)` → `PARSE_RET(0x2eb7334)` →
  `LATCH_WR(0x2eb7520)`; `SLOT_READ(0x23121ae)` never fired in 144 events.
  The earlier "path divergence" claim based on a non-firing hook is **retracted**:
  that run was stuck at an upgrade dialog, not evidence of a path difference.
- Harness writer search: SIGSTOP + `gdb -p` blocked by `ptrace_scope=1`;
  remote gdbserver stalled on `int3` SIGTRAPs; in-process `perf_event_open`
  hardware watch armed correctly and fired, but every observed write of `0x30`
  to the watched stack slot came from **host** RIPs, never guest code. No
  guest writer RA for `0x30` was found. No shim return log exists that records
  `0x30`, so the shim-return hypothesis is also unconfirmed.
- Diagnostic bisect (**MODIFIED INPUT, diagnostic only**): 1, 100, 1000 flags
  → SIGSEGV in `pthread_mutex_lock` (libc+0x95aa4, `si_addr=0x13f/0x160`,
  CF0=0, D08=0); 10000 flags → SIGTRAP at the TaskScheduler guard `0x6caef67`
  with CF0=0x41, D08=1; all 26044 flags → SIGSEGV as before. Smallest crashing
  count = **1**, first flag = `DFFlagConsumePlatformNameOverAlternateName`.
  Behaviour is non-monotonic in flag count, so this is not a clean threshold.
  None of these runs counts toward any checkpoint rung.
