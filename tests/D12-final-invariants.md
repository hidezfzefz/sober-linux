# D12 — final invariant re-run (worker D12, logical Team D, wave 2)

Date 2026-11-20 ~21:50–21:53 UTC. Repo `/home/user/sober-linux`. Build
`cmake -S . -B build -DCMAKE_BUILD_TYPE=Release` + `cmake --build build --parallel 4`.
Guest `/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so`. Strict decision 013
throughout; gate strict; zero marker emissions required.

**Scope discipline:** this report is the only file written. No script, `src/` file,
`CMakeLists.txt`, doc, or engine binary was modified. `verify-native.sh`,
`first-frame.sh`, `fault-baseline.sh`, `D08-dispatch-diag.sh`, `D09-withhold-audit.sh`
and `app-shell.sh` were executed unmodified. Re-running the suite regenerated
`build/` artifacts (expected), but no tracked source/test file was edited by this
worker. Lead-owned files (`src/`, `CMakeLists.txt`, `docs/`) were not touched.

## Engine identity — unchanged before/after every run

| Field | Value |
| --- | --- |
| BuildID (`readelf -n`) | `204ad049599c96afb75b8a21d5051063e821d6c9` |
| SHA-256 (initial, 21:50 UTC) | `3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335` |
| SHA-256 (final, 21:53 UTC) | `3e498d649421115bdbea69f78e2b818d4351d8990db60e232d2aef5b741335` |

Matched before and after every harness invocation: all three `first-frame.sh`
runs, `fault-baseline.sh`, the `D08` diagnostic (explicit pre/post check inside the
script), `app-shell.sh` (both runs), and the final read-back.

## Results table

| # | Invariant | Result | Evidence |
| --- | --- | --- | --- |
| 1 | `cmake` build | **PASS** | configure rc=0 (`Configuring/Generating done`); build rc=0; all 18 targets built (`sober-native`, `guest-init-host`, `sober_render`, `sober_jni`, `native_trace`, `render_probe`, `jni_probe`, NDK stubs, `ndk_bionic_libc`, …) |
| 2 | `bash tests/verify-native.sh` | **PASS** | rc=0; `build/sober-native` is a native x86-64 ELF; §2 ldd has no wine/proton/box64/qemu/wibo; §3/§4 are the expected manual notices |
| 3 | `ctest 2/2` | **PASS** | `100% tests passed, 0 failed out of 2`; `sober_native_check`, `sober_native_version`; `ctest -N` lists exactly 2 |
| 4 | `ldd` libc-only | **PASS** | both `build/sober-native` and `build/guest-init-host` → `linux-vdso.so.1`, `libc.so.6` (`/usr/lib64/libc.so.6`), `/lib64/ld-linux-x86-64.so.2` only; no X11/EGL/GL/translation-layer link deps |
| 5 | engine SHA / BuildID unchanged | **PASS** | initial == final == expected value; unchanged across every run (see identity table) |
| 6 | `bash tests/first-frame.sh` | **PASS (3/3 clean, rc=0)** | all three runs: `guest init complete` + `first frame rendered`; guest swap `0x649d422` rc=1, `guest-driven swaps observed: 1`; visible window `SoberNativeRenderPhase3`; SHA unchanged; no X11 transient observed (see §2) |
| 7 | `bash tests/fault-baseline.sh` default-OFF | **PASS** | script rc=0 informational; harness exit 139 (expected pre-existing fault): `signal=11`, `rdi=(nil)`, `faulting guest RVA: 0x2eb4b65`; bridge `ready env_slots=231 vm_slots=5 native_jvm=1`; no marker emitted/checked; absent from ctest (2 tests) and `verify-native.sh` |
| 8 | D08/D09 diagnostic scripts default-OFF | **PASS** | D09 bare invocation prints `default-OFF … no source read, no side effects`, rc=0; D08 explicit diagnostic rc=0, no marker; grep confirms neither D08/D09 nor `fault-baseline.sh` is referenced by `CMakeLists.txt`, `verify-native.sh`, or `build/CTestTestfile.cmake` |
| 9 | zero `app shell rendered` emissions | **PASS** | `app-shell.sh` default path rc=1 at §3 (`FAIL: no genuine guest->Java JNI callback observed`); stdout exactly `guest init complete` + `first frame rendered`; `marker withheld: callback=0 render=1`; zero `app shell rendered` in all `/tmp/sober-*.out\|err`; no `/tmp/sober-app-shell.marker` |

## 1. Build / test / link checks (items 1–5)

- Configure rc=0, build rc=0, no compiler/linker warnings; all 18 targets up-to-date
  or rebuilt. `verify-native.sh` §2 ldd: only `linux-vdso.so.1`, `libc.so.6`,
  `/lib64/ld-linux-x86-64.so.2`.
- `ctest --test-dir build --output-on-failure`: `100% tests passed, 0 failed out of
  2`, rc=0. Neither `fault-baseline.sh` nor the D08/D09 diagnostics nor app-shell
  diagnostics are wired into ctest (`ctest -N` = 2).
- Both binaries link glibc only; `guest-init-host` carries no X11/EGL/GL link
  dependency (runtime `dlopen` only), so the libc-only property holds at link time.

## 2. `tests/first-frame.sh` — three runs (item 6)

| Run | rc | Markers / evidence | Note |
| --- | --- | --- | --- |
| 1 | 0 | `guest init complete`, `first frame rendered`, swap `0x649d422` rc=1, 1 guest swap, window `SoberNativeRenderPhase3`, SHA unchanged | clean PASS |
| 2 | 0 | all above | clean PASS |
| 3 | 0 | all above | clean PASS |

Render log (all runs): `backend=x11-egl-gles2 x11=1 wayland=1 egl=1 gles2=1
surfaceless=1`; native window `handle=0x2a… backend=x11 640x480`; `GL version:
OpenGL ES 3.2 Mesa 26.2.2 | frames swapped: 1`; `guest call 0x649d422
swap(...) -> rc=1 | guest-driven swaps observed: 1`.

**X11 transient — honest note.** Xwayland `:0` stayed alive (PID 3161) and
`xdotool` was available throughout. All three runs in this re-run exited cleanly
(rc=0) with the window observed by xdotool; **no XIO / `fatal IO error` message
appeared** in any run (raw `/tmp/sober-first-frame.err|.out` scanned). This is
noted because D01's earlier re-run saw an intermittent `XIO: fatal IO error 2
… on X server ":0"` at window teardown in one of three runs; that headless
Xwayland teardown flake did not reproduce here. Every functional invariant
asserted by the script was satisfied in all three runs regardless.

## 3. Fault baseline — default-OFF maintained (item 7)

`bash tests/fault-baseline.sh`: script rc=0 (informational), harness exit 139
(expected pre-existing fault): `signal=11 (Segmentation fault) si_addr=(nil)`,
`regs: rdi=(nil)`, `rip maps to …+0x2eb4b65`, `faulting guest RVA: 0x2eb4b65`.
Bridge summary `bridge=ready env_slots=231 vm_slots=5 native_jvm=1`; default
no-start-app path reaching `nativeAppBridgeAppStart (0x247dc8d)`. Preserved
stdout markers: `guest init complete`, `first frame rendered`. No `app shell
rendered` emission. SHA/BuildID unchanged. The script never asserts success, is
not invoked by `verify-native.sh`, and is absent from ctest (`ctest -N` = 2).
No default-ON wiring introduced.

## 4. D08/D09 diagnostic scripts — default-OFF (item 8)

- **D09** (`D09-withhold-audit.sh`): bare invocation prints `D09 withhold audit:
  default-OFF (no source read, no side effects)` and exits 0. The audit itself is
  gated behind `SOBER_D09_WITHHOLD_AUDIT=1` or `--run`; the default path does not
  read source or write files.
- **D08** (`D08-dispatch-diag.sh`): explicit invocation (informational only)
  exited 0; pre/post engine SHA and BuildID unchanged; it unsets all marker-file
  env vars and emits/checks no marker. Harness exit 133 (SIGTRAP, guest RVA
  `0x6caef67`) was recorded as data on the full opt-in game-loaded stack — D08
  never asserts success, so this does not affect any invariant. (Observed openly;
  note this RVA is listed as `fixed` under the D04 F-stack only with a different
  knob combination; D08's exact stack is informational and outside the strict
  default path.)
- **Wiring audit:** `grep` over `CMakeLists.txt`, `tests/verify-native.sh`, and
  `build/CTestTestfile.cmake` found no reference to D08, D09, or `fault-baseline.sh`
  — all three remain default-OFF / explicit-invocation-only.

## 5. Strict decision-013 gate — zero marker emissions (item 9)

`bash tests/app-shell.sh` (default path, all opt-ins OFF), run twice: rc=1 at §3,
`FAIL: no genuine guest->Java JNI callback observed`. stdout contained exactly
`guest init complete` and `first frame rendered`. stderr:
`env_calls=33 callbacks=0 … last=isVrDevice` and
`marker withheld: callback=0 render=1 (env calls=33 callbacks=0 last='isVrDevice')`.
Pre-existing fault `0x2eb4b65` unchanged. **Zero** `app shell rendered` emissions
in any `/tmp/sober-*.out` or `.err`; no `/tmp/sober-app-shell.marker`.

Gate unchanged and not weakened: a genuine guest→Java completion callback
(`onAppShellReloadNeeded` / `gameLoadedCallback`) plus the phase-3 render call
remain necessary; `onAppBridgeNotification` is diagnostic-only and rejected as
completion proof. No marker was emitted or checked by any diagnostic script.

## 6. Outcome

All nine requested invariants are satisfied at this snapshot. Functional
first-frame evidence is green in all three runs (clean rc=0, no X11 transient
this time — noted honestly against D01's prior intermittent XIO). Decision 013
is upheld: zero `app shell rendered` emissions and no marker on every default-
path run; the strict gate still rejects with rc=1. `fault-baseline.sh` and the
D08/D09 diagnostics remain default-OFF and outside ctest/verify-native/CMake.
Engine SHA-256 and BuildID are unchanged throughout. No scripts, sources, or
lead-owned files were edited by this worker.
