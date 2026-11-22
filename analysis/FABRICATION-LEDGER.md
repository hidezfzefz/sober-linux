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
