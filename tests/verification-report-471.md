> **SUPERSEDED** by `tests/D12-final-invariants.md` and `docs/REVIEW-100.md`.
> Counts, test text and the ALT-thunk observation below are historical snapshots.

# Verification report 471 — invariants + decision-013 gate audit

Verifier, team `TEAM-XT29-DCJS-2UVE`, task #471. Date 2026-11-20 (UTC).
Repo `/home/user/sober-linux`; build `cmake --build build -j4`.
Guest `/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so`: BuildID
`204ad049599c96afb75b8a21d5051063e821d6c9`, SHA-256
`3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335`
(unchanged before/after every run).

Scope: `tests/` only. `src/`, `CMakeLists.txt`, `tests/app-shell.sh`, `tests/first-frame.sh`,
`tests/verify-native.sh` and `tests/fault-baseline.sh` were NOT modified. `src/linux/guest_app_shell.c`
was READ ONLY. Only this report was written.

Snapshots: baseline at 19:50–19:56 UTC; re-run at 19:57–20:01 UTC after appsingleton #465
temporary test wiring (`guest_app_shell.c:885-893` + `src/jni/app_singleton_bridge.c` in CMake)
landed and the harness was rebuilt; final re-run at 20:03 UTC after `app_singleton_bridge.h`
changed and that temporary wiring was reverted (clean hand-off). All invariants PASS at every
snapshot. params #469 files exist but are not yet wired into `src/jni/CMakeLists.txt` (lead
hand-off says to add `startapp_params.c` permanently).

## 1. Invariants — all PASS at both snapshots

| Check | Result |
| --- | --- |
| `cmake --build build -j4` | clean, all targets built (rc=0) |
| `bash tests/verify-native.sh` | PASS rc=0: native ELF, no translation-layer deps |
| `ctest --test-dir build --output-on-failure` | 2/2 Passed (`sober_native_check`, `sober_native_version`); `ctest -N` lists exactly 2 |
| `ldd build/guest-init-host`, `ldd build/sober-native` | `linux-vdso.so.1`, `libc.so.6`, `/lib64/ld-linux-x86-64.so.2` only, both binaries |
| engine BuildID / SHA-256 | unchanged; matches baseline above |
| `bash tests/first-frame.sh` | PASS rc=0: `guest init complete` + `first frame rendered`; guest swap `0x649d422`, 1 guest-driven swap; visible window `SoberNativeRenderPhase3`; SHA unchanged |

## 2. Strict gate (DECISION 013) — upheld

`bash tests/app-shell.sh`: rc=1 at §3, `FAIL: no genuine guest->Java JNI callback observed`
(expected). stdout exactly `guest init complete` + `first frame rendered`. stderr:
`marker withheld: callback=0 render=1 (env calls=33 callbacks=0 last='isVrDevice')`.
**Zero** `app shell rendered` emissions and no `/tmp/sober-app-shell.marker` on the default path.
Pre-existing guest fault `0x2eb4b65` (rdi=NULL) unchanged. After #465 wiring the bridge line
reads `rc=-2, singleton=(nil)` (default-OFF), so the new opt-in is inert on the default path.

The gate at `guest_app_shell.c:1235-1236` requires `sober_jni_callback_was_called` for
`onAppShellReloadNeeded` or `gameLoadedCallback` plus `sober_render_phase3_done()`; the sole
`emit_marker` (`:1292`) is gated by `:1267`. `onAppBridgeNotification` is deliberately not a
completion callback, and its knobs `SOBER_LOG_APP_BRIDGE_NOTIFICATION` /
`SOBER_LOG_HANDLE` are default-OFF (the former is also in the withhold chain). The user's ruling
that it does NOT count is enforced in code; the gate was not weakened.

## 3. Opt-in force-withhold audit (source read only)

### 3a. In-chain opt-ins — CONFIRMED (17)
`guest_app_shell.c:1251-1267`: the `seeded_state` OR-chain contains 17 vars —
`SOBER_SEED_USER_CONTROLLER`, `SOBER_SEED_APP_DELEGATE`, `SOBER_SEED_JNI_VM`,
`SOBER_FAITHFUL_JAVAVM`, `SOBER_NATIVE_OBJECTS`, `SOBER_JSON_FLAG_BRIDGE`,
`SOBER_JSON_APP_SINGLETON`, `SOBER_SEED_GLOBAL_STRING`, `SOBER_GSEED_FORCE`,
`SOBER_GSEED_DIAG`, `SOBER_REPLAY_FFLAG_CTOR`, `SOBER_NATIVE_FLAGS_INIT`,
`SOBER_BIONIC_SF_FULL`, `SOBER_SCHED_R70_KEEP`, `SOBER_SCHED_NEST_ONE`,
`SOBER_LOG_APP_BRIDGE_NOTIFICATION`, `SOBER_REAL_APPSTATE_PATH`. Any set forces
`shell_cb=0` at `:1261`; by construction no in-chain opt-in can emit the marker.

A/B (ALT thunk `SOBER_ALT_CALLBACK_PATH=1` used as a genuine-callback proxy; short window hold):

| Run | Marker | Result |
| --- | --- | --- |
| ALT alone (control) | emitted | rc=0, `callbacks=1`, HONESTY NOTICE — the labelled false-green the gate must catch |
| ALT + each of the 17 chain vars | none in all 17 | 11 printed `marker withheld: diagnostic guest-state seed active`; 6 faulted pre-gate (rc=139); marker file absent in every case |

### 3b. Coverage GAP — opt-ins NOT in the chain (escalated to lead)
A/B shows 13 opt-ins that mutate guest state are absent from the chain and still emit
`app shell rendered` when combined with the ALT callback, with **no** seed-withhold line:
`SOBER_SEED_JNI_SINGLETON`, `SOBER_HELPER_REPAIR_V2`, `SOBER_REAL_APPSTATE_FLAG`,
`SOBER_REAL_APPSTATE_POSTSETTINGS`, `SOBER_TLS_SEED`, `SOBER_STUB_TSD_SEED`,
`SOBER_VALIDATOR_PATCH`, `SOBER_FINAL_PATCH`, `SOBER_FIELD_NORMALISER`,
`SOBER_OBJECT_FIX`, `SOBER_APP_SHELL_FIX`, `SOBER_CALL_SITE_FIX`, `SOBER_NO_SCHED_NEUTRAL`
(`SOBER_ALLOC_SITE_FIX` faults pre-gate). Example: ALT+`SOBER_SEED_JNI_SINGLETON` ⇒ stdout
`guest init complete / first frame rendered / app shell rendered`; stderr logs a seeded
JNI singleton + `guest->Java callback: gameLoadedCallback` and no withhold line.
Default path (all opt-ins OFF) still emits zero marker, so decision 013 holds today; but if a
real callback lands while any of these opt-ins is set, the marker would be a false-green with
neither withhold nor honesty notice. Recommendation sent to lead: add at least the true
state-seeds to the `seeded_state` chain (lead-owned `src` edit; not done by this worker).

### 3c. New opt-ins from this team — not yet in chain
`SOBER_APP_SINGLETON_BRIDGE` (+ `SOBER_APP_SINGLETON_STRATEGY`, `_VERBOSE`, `_RELEASE_OLD`)
from #465 writes guest singleton `0x72cb5e0` and is NOT in the chain. A/B: ALT+bridge currently
faults pre-gate (rc=139, no marker), so no false-green today, but it must be added before the
fault is fixed. `SOBER_STARTAPP_PARAMS=1` (+ `_PLACE/_SCRIPT/_USER_ID/_UNDER13/_USERNAME/
_MEMBERSHIP/_THEME/_DPI/_WIDTH_MM/_HEIGHT_MM`) from #469 is a strict no-op unless exactly `1`
and is also not in the chain; `startapp_only` faults pre-gate with no marker. Both need chain
coverage when the lead wires them.

## 4. Fault baseline — default-OFF maintained
`bash tests/fault-baseline.sh`: rc=0, informational; fault record `signal=11`, `rdi=(nil)`,
`faulting guest RVA: 0x2eb4b65`, SHA/BuildID unchanged. It emits/checks no marker and is absent
from ctest (`ctest -N` = 2) and `tests/verify-native.sh`. No default-ON wiring was introduced.

## 5. Outcome
All five requested invariants PASS at both snapshots; zero marker on the default real path and
on every diagnostic-seed opt-in; strict decision 013 upheld; `fault-baseline.sh` stays default-OFF.
The only marker emission observed in A/B is the explicitly labelled ALT-thunk control
(default-OFF, HONESTY NOTICE), retained from prior reports as the positive control.
Open items escalated to lead: complete the withhold chain (§3b/3c). A final invariant re-run is
advised after the lead's final integration/chain edit.
