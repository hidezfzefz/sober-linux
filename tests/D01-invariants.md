# D01 — invariant re-run (worker D1, logical Team D)

Date 2026-11-20 ~21:34–21:39 UTC. Repo `/home/user/sober-linux`; build
`cmake -S . -B build -DCMAKE_BUILD_TYPE=Release` +
`cmake --build build --parallel 4`. Guest `/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so`.

**Scope discipline:** only this report was written. No script, `src/` file,
`CMakeLists.txt`, or engine binary was modified. `src/linux/guest_app_shell.c`
was READ ONLY. `verify-native.sh`, `first-frame.sh`, `fault-baseline.sh`,
`app-shell.sh` were executed unmodified. (Concurrent Team-D workers created
`tests/D08-dispatch-diag.sh` and `tests/D09-withhold-audit.sh` during this
window; those are their scope, not touched here.)

## Engine identity (unchanged before/after every run)

| Field | Value |
| --- | --- |
| BuildID (readelf -n) | `204ad049599c96afb75b8a21d5051063e821d6c9` |
| SHA-256 (pre-run) | `3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335` |
| SHA-256 (post-run) | `3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335` |

Matched before and after each harness run, including all three `first-frame.sh`
runs, `fault-baseline.sh`, and the `app-shell.sh` gate check.

## Results table

| # | Invariant | Result | Evidence |
| --- | --- | --- | --- |
| 1 | `cmake` build | **PASS** | configure rc=0; build rc=0; all 18 targets built (sober-native, guest-init-host, render/JNI/trace, NDK stubs, bionic libc) |
| 2 | `bash tests/verify-native.sh` | **PASS** | rc=0; native ELF confirmed; no translation-layer deps; manual notices printed |
| 3 | `ctest 2/2` | **PASS** | `2/2 Passed` (`sober_native_check`, `sober_native_version`); `ctest -N` lists exactly 2 |
| 4 | `ldd` libc-only | **PASS** | both `build/sober-native` and `build/guest-init-host` → `linux-vdso.so.1`, `libc.so.6`, `/lib64/ld-linux-x86-64.so.2` only |
| 5 | engine SHA / BuildID unchanged | **PASS** | pre/post identical, every run (see identity table) |
| 6 | `bash tests/first-frame.sh` | **PASS (2/3 clean; 1 transient env error, see §2)** | all functional markers present in all 3 runs: `guest init complete`, `first frame rendered`, guest swap `0x649d422` rc=1, 1 guest-driven swap, visible window `SoberNativeRenderPhase3`; rc=0 on runs 2 and 3 |
| 7 | `bash tests/fault-baseline.sh` default-OFF | **PASS** | script rc=0 informational; fault `signal=11`, `rdi=(nil)`, RVA `0x2eb4b65` unchanged; no marker emitted/checked; absent from ctest (2 tests) and `verify-native.sh` |
| 8 | zero `app shell rendered` emissions | **PASS** | no `app shell rendered` in any `/tmp/sober-*.out\|err`; no `/tmp/sober-app-shell.marker`; default-path `app-shell.sh` rc=1 at §3 with empty stdout markers |

## 1. Build / test / link checks (items 1–5)

- Configure: `-- Configuring done / Generating done`, rc=0. Build: rc=0, no
  compiler/linker warnings surfaced, all targets up-to-date or rebuilt.
- `verify-native.sh` §2 `ldd`: `linux-vdso.so.1`, `libc.so.6 =>
  /usr/lib64/libc.so.6`, `/lib64/ld-linux-x86-64.so.2` — no wine/proton/box64/
  qemu/wibo. §3/§4 are the expected manual notices.
- `ctest --test-dir build --output-on-failure`: `100% tests passed, 0 failed
  out of 2`, rc=0. `fault-baseline.sh` and app-shell diagnostics are NOT wired
  into ctest.
- Both binaries link glibc only; `guest-init-host` carries no X11/EGL/GL link
  dependency (runtime `dlopen` only), so the libc-only property holds.

## 2. `tests/first-frame.sh` — three runs

| Run | rc | Markers | Note |
| --- | --- | --- | --- |
| 1 | 1 | `guest init complete`, `first frame rendered`, swap `0x649d422`, 1 guest swap, SHA unchanged | failed only at §5 with `FAIL: no first-frame marker (rc=1 marker=1)` because libX11 emitted `XIO: fatal IO error 2 (No such file or directory) on X server ":0"` at window teardown, forcing a nonzero harness exit. Frame marker file and stdout line were both present. |
| 2 | 0 | all above | clean PASS; `visible native window confirmed on display: SoberNativeRenderPhase3` |
| 3 | 0 | all above | clean PASS; `visible native window confirmed on display: SoberNativeRenderPhase3` |

Render log (all runs): `backend=x11-egl-gles2 ... surfaceless=1`, native window
`handle=... backend=x11 640x480`, `OpenGL ES 3.2 Mesa 26.2.2 | frames swapped: 1`,
`guest call 0x649d422 swap(...) -> rc=1 | guest-driven swaps observed: 1`.
The Xwayland `:0` server stayed alive (PID 3161) throughout; the run-1 XIO
message is an intermittent X-client teardown/disconnect on this headless
Xwayland display, not a code or engine regression — every functional invariant
the check asserts was satisfied in that run, and two immediate re-runs were
clean. Reported transparently rather than hidden.

## 3. Fault baseline — default-OFF maintained (item 7)

`bash tests/fault-baseline.sh`: script rc=0 (informational). Harness exit 139
(expected pre-existing fault): `signal=11 (Segmentation fault) si_addr=(nil)`,
`rdi=(nil)`, `rip maps to ...+0x2eb4b65`, `faulting guest RVA: 0x2eb4b65`.
Bridge summary shows `bridge=ready env_slots=231 vm_slots=5 native_jvm=1` and
the default no-start-app path reaching `nativeAppBridgeAppStart (0x247dc8d)`.
Preserved markers on stdout: `guest init complete`, `first frame rendered`.
No `app shell rendered` emission. Script exits 0, never asserts success, is not
invoked by `verify-native.sh`, and is not in ctest (`ctest -N` = 2). No
default-ON wiring introduced.

## 4. Strict decision-013 gate — `onAppBridgeNotification` rejected (item 8)

`bash tests/app-shell.sh` (default path, all opt-ins OFF): rc=1 at §3 —
`FAIL: no genuine guest->Java JNI callback observed`. stdout contained exactly
`guest init complete` and `first frame rendered`; no `app shell rendered`, no
shell marker file. (This run also showed the same intermittent libX11 XIO
teardown message as first-frame run 1; it does not affect the gate outcome.)

Read-only source confirmation in `src/linux/guest_app_shell.c`:
- :1345–1346 `shell_cb` is true only for `onAppShellReloadNeeded` or
  `gameLoadedCallback` — never `onAppBridgeNotification`.
- :969–973 `onAppBridgeNotification` is installed only under
  `SOBER_LOG_APP_BRIDGE_NOTIFICATION` (default-OFF) and is explicitly commented
  diagnostic-only; that env is also in the `seeded_state` withhold chain.
- :1349–1390 `seeded_state` withhold chain (now expanded to cover the
  guest-mutating opt-ins escalated in report 471) forces `shell_cb = 0`.
- :1401 `render_ok = sober_render_phase3_done()`; both `shell_cb` and
  `render_ok` are required or the marker is withheld (`return -4`).
- :1425 is the sole `emit_marker("app shell rendered\n", ...)`.

Gate unchanged and not weakened; the real completion callback plus the
phase-3 guest render call remain necessary. `onAppBridgeNotification` remains
rejected as app-shell proof.

## 5. Outcome

All eight requested invariants are satisfied at this snapshot. Functional
first-frame evidence is green in all three runs (two with clean rc=0); one run
hit an intermittent X-server teardown error that forced a nonzero harness exit
without losing any asserted marker — recorded above, not concealed. Decision
013 is upheld: zero `app shell rendered` emissions and no marker on every
default-path run; `onAppBridgeNotification` is still diagnostic-only and
rejected as completion proof. `fault-baseline.sh` remains default-OFF and
outside ctest/verify-native. Engine SHA-256 and BuildID unchanged throughout.
A subsequent re-run is advised after the lead's final integration, since other
Team-D workers are still editing `tests/` in parallel.
