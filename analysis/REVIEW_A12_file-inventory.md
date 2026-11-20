# REVIEW A12 — File Inventory (full-project audit)

**Auditor:** A12 · **Date:** 2026-11-20 · **Scope:** `analysis/REVIEW_A12_file-inventory.md` only
**Mode:** analysis-only (no shared edits) · **Strict marker:** WITHHELD (decisions 013 / 038)

---

## 1. Method

- `git ls-files` is the sole definition of "tracked" (680 paths). Working-tree
  files that are untracked are out of scope.
- "Text lines" = newline count for files with no NUL byte in the first 8 KiB;
  everything else is classified as binary and inventoried by byte size +
  SHA-256 prefix (first 12 hex chars).
- Roles / default-path vs diagnostic were derived from `CMakeLists.txt`,
  `src/*/CMakeLists.txt`, `Makefile`, `README.md`, and the module header comments.
- No file content was modified; target-derived strings/decompiled text were
  treated strictly as data.

## 2. Grand totals

| Metric | Value |
| --- | --- |
| Tracked files | **680** |
| Text files | 602 |
| Text lines (all tracked text) | **124,314** |
| Binary files | **78** |
| Binary bytes | 2,591,551 (~2.59 MiB) |
| Total tracked bytes | 22,202,747 (~22.2 MiB) |
| Tracked files under `build/` | 387 (56.9% of all tracked files) |

Grand total reconciles: analysis 119 + build 387 + docs 47 + root/appData 25 +
src/compat 15 + src/jni 47 + src/linux 12 + src/render 5 + src/trace 4 +
tests 17 + tools 2 = **680**.

## 3. Per-area inventory table

Legend for path class: **DP** = default path (compiled/linked into the shipped
`build/sober-native` or a required DT_NEEDED stub), **HARNESS** = built only
into the diagnostic `guest-init-host`, **OPT-IN** = built but off / not linked /
requires an explicit env var or LD_PRELOAD, **DOC** = non-build documentation &
evidence, **ART** = committed generated artifact.

| Area | Files | Text lines | Bin | Role | Completeness | Path class |
| --- | ---: | ---: | ---: | --- | --- | --- |
| `src/linux` | 12 | 3,337 | 0 | Native ELF entry (`main.c`), runtime loader, APS2 relocs, platform/gfx discovery + instrumented harness driver (`guest_*`) | Complete for init/window/first-frame; no login/app-shell path | DP (`main.c`, platform, relocs) + HARNESS (`guest_*`) |
| `src/compat` | 15 | 6,087 | 0 | Source-level Bionic/Android shim, version-scripted `libc.so`, NDK SONAME stubs, TSD bootstrap patcher, generator | Complete | DP (compat shim, bootstrap, bionic libc, stubs); generator = maintenance |
| `src/render` | 5 | 1,404 | 0 | Runtime-dlopen X11/EGL/GLES window + first frame; headless probe | Complete (capability-level, no display dependency) | HARNESS / OPT-IN (not linked into `sober-native`) |
| `src/jni` | 47 | 12,550 | 0 | 231-slot synthetic `JNIEnv`/`JavaVM`, cached IDs, 22 guest-shaped mitigation modules, probe | Structurally complete; behaviours opt-in and unproven on the guest | HARNESS / OPT-IN (all guest mitigations OFF by default, decision 013) |
| `src/trace` | 4 | 828 | 0 | LD_PRELOAD allocation tracer | Complete | OPT-IN diagnostic (built, never linked) |
| `tests` | 17 | 1,641 | 0 | 9 shell diagnostics, 8 report/invariant docs | Complete; 2 ctest hooks | DP (2 ctest hooks in `verify-native.sh` path) + OPT-IN diagnostics |
| `docs` | 47 | 2,859 | 0 | 35 ADRs, 3 interface specs, 9 generated badges | Complete but with ADR numbering gaps (015/016/034/036 absent) | DOC |
| `analysis` | 119 | 64,267 | 3 | REA/Ghidra evidence, 62+ reports, fault traces, decompiled functions | Extensive; includes stale/empty artifacts | OPT-IN diagnostic evidence |
| `tools` | 2 | 411 | 0 | Badge/progress generator + ptrace watcher | Partial: generator used, watcher unreferenced | DOC/maintenance (`gen_progress.py`); orphan (`ptrace_watch.c`) |
| `build` (committed) | 387 | 30,027 | 75 | Entire CMake out-of-source tree: caches, makefiles, objects, `.a`/`.so`, ELFs | Stale/duplicated; contradicts `Makefile` ("Nothing in build/ is ever committed") | ART |
| root + `appData` | 25 | 903 | 0 | Top-level configs/docs + captured guest logs/settings | n/a | DOC / runtime capture |

## 4. Detailed per-file enumeration (source, tests, docs, tools)

### 4.1 `src/linux` — 12 files / 3,337 lines

```
   332  src/linux/android_relocs.c
    73  src/linux/android_relocs.h
    89  src/linux/gfx_loader.c
    34  src/linux/gfx_loader.h
  1594  src/linux/guest_app_shell.c
    27  src/linux/guest_app_shell.h
   717  src/linux/guest_dlopen_host.c
   215  src/linux/guest_render.c
    27  src/linux/guest_render.h
    95  src/linux/main.c
   112  src/linux/platform.c
    22  src/linux/platform.h
```

### 4.2 `src/compat` — 15 files / 6,087 lines

```
   817  src/compat/android_compat.c
   175  src/compat/android_compat.h
   113  src/compat/android_types.h
   303  src/compat/bionic_bootstrap/README.md
  1425  src/compat/bionic_bootstrap/bionic_bootstrap.c
   142  src/compat/bionic_bootstrap/bionic_bootstrap.h
   179  src/compat/bionic_bootstrap/bootstrap_profile.h
  1344  src/compat/bionic_libc.c
   425  src/compat/bionic_libc.map
    66  src/compat/bionic_sf_full.c
   171  src/compat/bionic_signal.c
   216  src/compat/exports.map
   214  src/compat/gen_bionic_libc.py
   443  src/compat/gl_egl_forward.c
    54  src/compat/ndk_stub.c
```

### 4.3 `src/render` — 5 files / 1,404 lines

```
    24  src/render/CMakeLists.txt
    72  src/render/README.md
  1023  src/render/native_render.c
   163  src/render/native_render.h
   122  src/render/render_probe.c
```

### 4.4 `src/jni` — 47 files / 12,550 lines

23 `.c` (22 compiled into `sober_jni` + `jni_probe.c` standalone executable),
22 `.h` (all referenced by at least one `#include`), plus `CMakeLists.txt` and
`README.md`.

```
    42  src/jni/CMakeLists.txt
    78  src/jni/README.md
   345  src/jni/alloc_contract_fix.c
   157  src/jni/alloc_contract_fix.h
   206  src/jni/app488_fix.c
   130  src/jni/app488_fix.h
   262  src/jni/app488_inner.c
   134  src/jni/app488_inner.h
   260  src/jni/app_singleton_bridge.c
   140  src/jni/app_singleton_bridge.h
   312  src/jni/callback_provenance.c
   110  src/jni/callback_provenance.h
   497  src/jni/game_params.c
   203  src/jni/game_params.h
   191  src/jni/global_string_seed.c
    92  src/jni/global_string_seed.h
   453  src/jni/javavm_semantics.c
    97  src/jni/javavm_semantics.h
   315  src/jni/jni_arrays.c
    86  src/jni/jni_arrays.h
   295  src/jni/jni_json_bridge.c
   166  src/jni/jni_json_bridge.h
   276  src/jni/jni_native_objects.c
    79  src/jni/jni_native_objects.h
   210  src/jni/jni_probe.c
   470  src/jni/jni_strings.c
   139  src/jni/jni_strings.h
  1492  src/jni/native_jni.c
   172  src/jni/native_jni.h
   457  src/jni/notifier_probe.c
   152  src/jni/notifier_probe.h
   361  src/jni/platform_fields.c
   188  src/jni/platform_fields.h
   289  src/jni/scalar_returns.c
   147  src/jni/scalar_returns.h
   283  src/jni/singleton_fix.c
   137  src/jni/singleton_fix.h
   282  src/jni/singleton_services.c
   154  src/jni/singleton_services.h
   449  src/jni/startapp_params.c
   167  src/jni/startapp_params.h
   522  src/jni/startapp_v2.c
   194  src/jni/startapp_v2.h
   705  src/jni/startgame_v2.c
   244  src/jni/startgame_v2.h
   257  src/jni/static_object_calls.c
   153  src/jni/static_object_calls.h
```

### 4.5 `src/trace` — 4 files / 828 lines

```
    17  src/trace/CMakeLists.txt
    65  src/trace/README.md
   671  src/trace/native_trace.c
    75  src/trace/native_trace.h
```

### 4.6 `tests` — 17 files / 1,641 lines

9 scripts + 8 markdown reports. Only `sober_native_check` and
`sober_native_version` (defined in root `CMakeLists.txt`) are ctest default-path;
the rest are manual diagnostics.

```
   115  tests/D01-invariants.md
    60  tests/D04-baseline-tracking.md
   165  tests/D08-dispatch-diag.sh
   143  tests/D09-withhold-audit.sh
   128  tests/D12-final-invariants.md
   151  tests/app-shell.sh
    63  tests/fault-baseline.sh
   117  tests/first-frame.sh
    74  tests/gdb-trace.sh
    91  tests/guest-dlopen.sh
    43  tests/trace-app-shell.sh
   133  tests/verification-report-451.md
    73  tests/verification-report-456.md
    85  tests/verification-report-460.md
    68  tests/verification-report-463.md
   102  tests/verification-report-471.md
    30  tests/verify-native.sh
```

### 4.7 `docs` — 47 files / 2,859 lines

9 SVG badges (generated by `tools/gen_progress.py`), 35 decision records,
3 interface specs. Decision numbering skips 015, 016, 034, 036.

```
    13  docs/assets/badge-callback.svg
    13  docs/assets/badge-gate.svg
    13  docs/assets/badge-guest-init.svg
    13  docs/assets/badge-jni.svg
    13  docs/assets/badge-marker.svg
    13  docs/assets/badge-relocs.svg
    13  docs/assets/badge-render.svg
    13  docs/assets/badge-verification.svg
    51  docs/assets/progress.svg
    16  docs/decisions/000-stack-and-constraints.md
    18  docs/decisions/001-folder-layout.md
    14  docs/decisions/002-interfaces.md
    62  docs/decisions/003-target-identification.md
    25  docs/decisions/004-android-ndk-compat-not-win32.md
    74  docs/decisions/005-verification-log.md
    52  docs/decisions/006-init-milestone.md
    40  docs/decisions/007-init-ordering-control.md
    64  docs/decisions/008-packed-relocations.md
    38  docs/decisions/009-versioned-libc-self-hosting-bug.md
    37  docs/decisions/010-native-window-milestone.md
    42  docs/decisions/011-phase3-complete.md
    38  docs/decisions/012-jni-app-shell-milestone.md
    78  docs/decisions/013-no-weak-app-shell-marker.md
    49  docs/decisions/014-validator-bypass.md
    44  docs/decisions/017-stop-deref-patches.md
    43  docs/decisions/018-phase4-blocked.md
    34  docs/decisions/019-zero-guest-allocations.md
    29  docs/decisions/020-startup-rva-abi.md
    34  docs/decisions/021-source-fixes-exhausted.md
    72  docs/decisions/022-guard-disassembly.md
    39  docs/decisions/023-field-normalisation.md
    30  docs/decisions/024-object-level-fix.md
    35  docs/decisions/025-refbase-reconstruction.md
    45  docs/decisions/026-final-blocked.md
    32  docs/decisions/027-phase4b-runtime-trace.md
    42  docs/decisions/028-root-cause-allocator-and-jni-slots.md
    35  docs/decisions/029-trace-findings.md
    27  docs/decisions/030-bad-object-not-our-handle.md
    38  docs/decisions/031-call-site-seed.md
    44  docs/decisions/032-phase4b-final-blocked.md
    28  docs/decisions/033-curl-holder-fix.md
    41  docs/decisions/035-entry-logger-result.md
    50  docs/decisions/037-callback-thunk-milestone.md
    84  docs/decisions/038-strict-gate-alt-thunk-ruling.md
    99  docs/interfaces/abi-map.md
   559  docs/interfaces/libroblox-exports.txt
   573  docs/interfaces/libroblox-imports.txt
```

### 4.8 `tools` — 2 files / 411 lines

```
   112  tools/gen_progress.py      <- referenced by README.md:20 (used)
   299  tools/ptrace-watch/ptrace_watch.c  <- no build rule, no reference (orphan)
```

### 4.9 Root / `appData` — 25 files / 903 lines (outside requested areas, listed for completeness)

```
  top-level: .gitignore, CMakeLists.txt, Makefile, PROGRESS.md, README.md
  appData:   AnalysticsSettings.xml, ClientSettings/IxpSettings.json,
             18 captured *last.log files under appData/logs/
```

`appData/` is listed in `.gitignore` yet all 19 files are tracked (force-added).

### 4.10 `analysis` — 119 files / 64,267 text lines / 3 binaries

Directory distribution:

| Subdir | Files | Text lines | Bin |
| --- | ---: | ---: | ---: |
| `analysis/` (top-level reports) | 47 | 12,977 | 0 |
| `analysis/abort` | 10 | 15,234 | 1 |
| `analysis/bootstrap` (+ `__pycache__`) | 4 | 22,130 | 1 |
| `analysis/callback`, `caller`, `render`, `trace` | 5 | 874 | 0 |
| `analysis/dispatch` | 3 | 1,410 | 0 |
| `analysis/ghidra` (+ `decompiled`, `work`) | 25 | 6,103 | 0 |
| `analysis/handle` | 18 | 1,358 | 0 |
| `analysis/jni` | 3 | 793 | 0 |
| `analysis/rea` (+ `work/lib/x86_64`) | 4 | 2,388 | 1 |

Notable single files: `analysis/bootstrap/init-array.json` (21,743 lines),
`analysis/rea/bionic-versions.json` (1,683 lines), four `analysis/abort/repro-*.err`
trace logs (3,700–3,732 lines each), `analysis/ghidra/decompiled/FUN_032859c2_...c`
(1,921 lines). Full per-file listing for `analysis` is in §4.10 of the working
data (omitted here for brevity; all 119 paths were enumerated and counted).

## 5. Binary / artifact inventory (all 78 tracked binaries)

Sizes in bytes; SHA = first 12 hex chars of SHA-256.

### 5.1 Primary shipped / harness artifacts (README §Binary inventory)

| Size | SHA-256 prefix | Path | Note |
| ---: | --- | --- | --- |
| 93,120 | `7647f6d60c52` | `build/sober-native` | shipped native ELF; matches README |
| 352,552 | `1e7787f28f87` | `build/guest-init-host` | full harness; **README says `733c8f2314eb…` — stale doc** |
| 82,616 | `d131185d781f` | `build/ndk-stubs/libc.so` | versioned Bionic shim; matches README |
| 89,344 | `f00e9c818384` | `build/jni_probe` | JNI self-test/probe executable |
| 35,176 | `2f40d216ffa2` | `build/render_probe` | headless render probe |
| 50,680 | `7dbd1fb1545d` | `build/libnative_trace.so` | correct LD_PRELOAD output name |
| 30,656 | `4024eb6901bb` | `build/native_trace.so` | **stale duplicate with wrong name** (see §6) |

### 5.2 Static libraries

| Size | SHA-256 prefix | Path |
| ---: | --- | --- |
| 49,048 | `d4f1e4208a57` | `build/libsober_bootstrap.a` |
| 123,244 | `6fbdc709a2f7` | `build/libsober_compat.a` |
| 44,502 | `7bc2e77dba59` | `build/libsober_jni.a` *(stale root copy)* |
| 20,422 | `09c81cb47859` | `build/libsober_platform.a` |
| 35,130 | `1b82dea2c421` | `build/libsober_render.a` *(stale root copy)* |
| 331,364 | `7716ed4348e3` | `build/src/jni/libsober_jni.a` *(active)* |
| 36,400 | `fae525789c2f` | `build/src/render/libsober_render.a` *(active)* |

### 5.3 NDK stub DSOs (`build/ndk-stubs/`)

| Size | SHA-256 prefix | Path |
| ---: | --- | --- |
| 14,520 | `64100a385a24` | `libandroid.so` |
| 14,520 | `451882ff7662` | `libdl.so` |
| 15,440 | `d265080b6a3e` | `liblog.so` |
| 14,520 | `5dfe9a3b95b5` | `libm.so` |
| 14,520 | `7c3638a27d5e` | `libmediandk.so` |
| 14,520 | `8c0965357e35` | `libOpenMAXAL.so` |
| 14,520 | `43668e8039c9` | `libOpenSLES.so` |

### 5.4 Object files under `build/CMakeFiles/` (top-level configure)

| Size | SHA-256 prefix | Path |
| ---: | --- | --- |
| 15,568 | `ae34b49edd4c` | `4.3.5/CMakeDetermineCompilerABI_C.bin` |
| 15,592 | `8822e197052d` | `4.3.5/CMakeDetermineCompilerABI_CXX.bin` |
| 15,632 | `bfdedbea6f41` | `4.3.5/CompilerIdC/a.out` |
| 15,648 | `2c0cea266d64` | `4.3.5/CompilerIdCXX/a.out` |
| 63,912 | `93dc99726b67` | `guest-init-host.dir/src/linux/guest_app_shell.c.o` |
| 34,872 | `2b1d67944c38` | `guest-init-host.dir/src/linux/guest_dlopen_host.c.o` |
| 9,720 | `39c6bb7f594b` | `guest-init-host.dir/src/linux/guest_render.c.o` |
| 95,056 | `b4d7df758b9f` | `ndk_bionic_libc.dir/src/compat/bionic_libc.c.o` |
| 3,616 | `5c3b99ae1749` | `ndk_bionic_libc.dir/src/compat/bionic_sf_full.c.o` |
| 3,008 | `ba4003ce2bc4` | `ndk_bionic_libc.dir/src/compat/bionic_signal.c.o` |
| 1,016 | `4593966d6d7a` | `ndk_stub_android.dir/src/compat/ndk_stub.c.o` |
| 1,016 | `4593966d6d7a` | `ndk_stub_c.dir/src/compat/ndk_stub.c.o` |
| 1,016 | `4593966d6d7a` | `ndk_stub_dl.dir/src/compat/ndk_stub.c.o` |
| 3,408 | `1a774c15c78e` | `ndk_stub_log.dir/src/compat/ndk_stub.c.o` |
| 1,016 | `4593966d6d7a` | `ndk_stub_m.dir/src/compat/ndk_stub.c.o` |
| 1,016 | `4593966d6d7a` | `ndk_stub_mediandk.dir/src/compat/ndk_stub.c.o` |
| 1,016 | `4593966d6d7a` | `ndk_stub_openmaxal.dir/src/compat/ndk_stub.c.o` |
| 1,016 | `4593966d6d7a` | `ndk_stub_opensles.dir/src/compat/ndk_stub.c.o` |
| 6,720 | `fbab2f2bc0ca` | `sober-native.dir/src/linux/main.c.o` |
| 47,896 | `c59fe068a9bd` | `sober_bootstrap.dir/src/compat/bionic_bootstrap/bionic_bootstrap.c.o` |
| 33,120 | `c9762919cf1c` | `sober_compat.dir/src/compat/android_compat.c.o` |
| 82,768 | `0916638befd0` | `sober_compat.dir/src/compat/gl_egl_forward.c.o` |
| **97,392** | `02a5771ece9a` | **`sober_compat.dir/src/compat/win32_compat.c.o` — ORPHAN, no source** |
| 43,744 | `ab9632f8eabb` | `sober_jni.dir/src/jni/native_jni.c.o` *(stale root JNI obj)* |
| 9,592 | `aea74f9de241` | `sober_platform.dir/src/linux/android_relocs.c.o` |
| 4,984 | `6ac31d501021` | `sober_platform.dir/src/linux/gfx_loader.c.o` |
| 5,120 | `06f967bf7d43` | `sober_platform.dir/src/linux/platform.c.o` |
| 34,120 | `bcf9810d5248` | `sober_render.dir/src/render/native_render.c.o` *(stale root render obj)* |

### 5.5 Object files under `build/src/` (active sub-configure)

| Size | SHA-256 prefix | Path |
| ---: | --- | --- |
| 11,648 | `e9db1da6eeee` | `jni/CMakeFiles/jni_probe.dir/jni_probe.c.o` |
| 8,056 | `dfbda89fa098` | `jni/CMakeFiles/sober_jni.dir/alloc_contract_fix.c.o` |
| 5,624 | `e8e63c3673af` | `jni/CMakeFiles/sober_jni.dir/app488_fix.c.o` |
| 6,432 | `92821c16c9b8` | `jni/CMakeFiles/sober_jni.dir/app488_inner.c.o` |
| 10,672 | `42d360c82ba1` | `jni/CMakeFiles/sober_jni.dir/app_singleton_bridge.c.o` |
| 8,824 | `a8c28a5cb765` | `jni/CMakeFiles/sober_jni.dir/callback_provenance.c.o` |
| 21,752 | `fb6beb1cfb27` | `jni/CMakeFiles/sober_jni.dir/game_params.c.o` |
| 5,200 | `85a34e208412` | `jni/CMakeFiles/sober_jni.dir/global_string_seed.c.o` |
| 12,296 | `8eff0b441869` | `jni/CMakeFiles/sober_jni.dir/javavm_semantics.c.o` |
| 9,896 | `bfd668ff4cf4` | `jni/CMakeFiles/sober_jni.dir/jni_arrays.c.o` |
| 9,400 | `5cdf2a704822` | `jni/CMakeFiles/sober_jni.dir/jni_json_bridge.c.o` |
| 7,072 | `e88d6d93d44b` | `jni/CMakeFiles/sober_jni.dir/jni_native_objects.c.o` |
| 12,136 | `2397acfdf7e7` | `jni/CMakeFiles/sober_jni.dir/jni_strings.c.o` |
| 67,832 | `9433d8ba4a8a` | `jni/CMakeFiles/sober_jni.dir/native_jni.c.o` |
| 10,880 | `68a93e0d9ad8` | `jni/CMakeFiles/sober_jni.dir/notifier_probe.c.o` |
| 16,456 | `98032b3a45fe` | `jni/CMakeFiles/sober_jni.dir/platform_fields.c.o` |
| 8,464 | `effab3e4e581` | `jni/CMakeFiles/sober_jni.dir/scalar_returns.c.o` |
| 8,736 | `73efaa0312bd` | `jni/CMakeFiles/sober_jni.dir/singleton_fix.c.o` |
| 7,912 | `ab8826c8f55b` | `jni/CMakeFiles/sober_jni.dir/singleton_services.c.o` |
| 19,824 | `d2a1ae55273f` | `jni/CMakeFiles/sober_jni.dir/startapp_params.c.o` |
| 22,824 | `44f54f0332ba` | `jni/CMakeFiles/sober_jni.dir/startapp_v2.c.o` |
| 34,272 | `5b44218b841e` | `jni/CMakeFiles/sober_jni.dir/startgame_v2.c.o` |
| 6,384 | `cd3182b00bc3` | `jni/CMakeFiles/sober_jni.dir/static_object_calls.c.o` |
| 7,728 | `a1dca1544ae9` | `render/CMakeFiles/render_probe.dir/render_probe.c.o` |
| 35,288 | `180bc40bd82f` | `render/CMakeFiles/sober_render.dir/native_render.c.o` |
| 65,272 | `9b3a7a02d559` | `trace/CMakeFiles/native_trace.dir/native_trace.c.o` |

### 5.6 Analysis binaries (3)

| Size | SHA-256 prefix | Path | Note |
| ---: | --- | --- | --- |
| 15,376 | `fe3d8ffbc2c1` | `analysis/abort/stack_chk_interpose.so` | interposer for abort repro |
| 4,961 | `d112a728f78c` | `analysis/bootstrap/__pycache__/decode_aps2.cpython-315.pyc` | **committed bytecode cache** |
| 4,920 | `cb176fc08ae3` | `analysis/rea/work/lib/x86_64/libtrampoline.so` | under gitignored path, force-added |

## 6. Unused / orphaned / duplicated — highlights

1. **Committed build tree contradicts the build contract.** `Makefile:11` states
   *"Nothing in build/ is ever committed"*, yet **387 generated files** (caches,
   makefiles, `.o`, `.a`, `.so`, ELFs) are tracked. `make clean` would delete
   tracked content and leave a dirty tree. Highest-severity inventory defect.
2. **Orphan object with no source:** `build/CMakeFiles/sober_compat.dir/src/compat/win32_compat.c.o`
   (97,392 B, `02a5771ece9a`). No `win32_compat.c` is tracked; decision 004
   explicitly rules out a Win32 surface. Stale artifact of a discarded design.
3. **Duplicated/stale build subdirectory:** both `build/CMakeFiles/sober_jni.dir/`
   and `build/src/jni/CMakeFiles/sober_jni.dir/` exist; likewise
   `sober_render.dir` at both levels. Generated files (`flags.make`,
   `depend.make`, `compiler_depend.ts`, `cmake_clean_target.cmake`,
   `CMakeDirectoryInformation.cmake`) are byte-identical across the two trees
   (12 duplicate groups found by content hash). Only `build/src/*` matches the
   current `add_subdirectory()` layout; the root-level copies are leftovers
   from an earlier top-level JNI/render configuration.
4. **Duplicate library/DSO pairs with divergent content:**
   - `build/libsober_jni.a` (44,502 B, `7bc2e77dba59`) vs active
     `build/src/jni/libsober_jni.a` (331,364 B, `7716ed4348e3`).
   - `build/libsober_render.a` (35,130 B, `1b82dea2c421`) vs active
     `build/src/render/libsober_render.a` (36,400 B, `fae525789c2f`).
   - `build/native_trace.so` (30,656 B, `4024eb6901bb`) vs correctly-named
     `build/libnative_trace.so` (50,680 B, `7dbd1fb1545d`).
5. **Stale SHA in README:** `README.md:62` gives `build/guest-init-host` SHA
   `733c8f2314eb…`; the tracked file hashes to `1e7787f28f87…`.
6. **Two zero-byte files:** `analysis/ghidra/decompiled/network_0x2a326ff.err`
   and `.json` (both 0 B, SHA `e3b0c44298fc…`) — failed/empty decompilation
   outputs kept as tracked placeholders.
7. **Unreferenced source:** `tools/ptrace-watch/ptrace_watch.c` (299 lines) has
   no build rule in any `CMakeLists.txt`/`Makefile` and no reference in docs or
   scripts. `tools/gen_progress.py` is referenced (`README.md:20`) and is live.
8. **Committed bytecode cache:** `analysis/bootstrap/__pycache__/decode_aps2.cpython-315.pyc`
   — Python 3.15-specific generated file, should not be tracked.
9. **Force-added gitignored paths:** `appData/` (`.gitignore:1`) has 19 tracked
   files; `analysis/rea/work/lib/` (`.gitignore:4`) has `libtrampoline.so`.
10. **ADR numbering gaps:** `docs/decisions/` skips 015, 016, 034, 036 — no
    orphan files, but the decision chain has holes.
11. **No orphaned headers or unbuildable sources in `src/`:** all 33 tracked
    headers in `src/` are referenced by an `#include`, and every tracked
    `src/**/*.c` is named in a `CMakeLists.txt` (the 22 JNI mitigation modules
    plus `jni_probe.c`, which is its own executable). The source tree itself is
    clean; the contamination is confined to `build/`, `analysis/`, and one tool.

## 7. Final counts and verdict

**Counts:** 680 tracked files · 602 text files / 124,314 lines · 78 binaries /
2.59 MiB · 22.2 MiB total · 387 files (56.9%) in `build/` · 12 duplicate-content
groups · 1 orphan object · 1 unreferenced source · 2 empty files · 1 stale SHA
in README.

**Verdict — INVENTORY: QUALIFIED / needs hygiene.** The hand-written source
tree (`src/linux`, `src/compat`, `src/render`, `src/jni`, `src/trace`: 83 files,
24,006 lines) is complete, internally consistent, and cleanly separated into
default-path (`sober-native` + required stubs) versus harness/opt-in diagnostics.
Docs and tests are complete for the current milestone. The defect surface is
almost entirely *committed generated output*: a 387-file CMake build tree that
the Makefile says must never be committed, with duplicate/stale copies, one
orphan object from an abandoned Win32 design, and small cache/bytecode/empty
artifacts in `analysis/`. These do not change the functional status of the port
but make the repository non-reproducible under `make clean` and inflate review
surface substantially. Recommended cleanup (out of my scope): untrack
`build/`, `appData/logs/`, `__pycache__`, the empty decompilation placeholders,
and `ptrace_watch.c` (or add a build rule), then refresh the README SHA table.

**Strict marker:** **WITHHELD.** Nothing in this inventory changes the
decision-013 / 038 gate: the genuine app-shell callback remains unobserved
(0/1) and no default-path app-shell frame is proven. The 22 JNI mitigation
modules and all harness-only subsystems are OFF by default and must not be
counted as default-path evidence.
