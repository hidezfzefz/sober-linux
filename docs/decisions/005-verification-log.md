# Verification log (lead)

## Baseline 2026-11-18 — worker-3 scaffold build

Command: `bash tests/verify-native.sh`

| Check | Result |
| --- | --- |
| `build/sober-native` is an ELF | PASS — ELF64 PIE x86-64, dynamically linked, Build ID `952b4228...`, not stripped |
| No translation-layer deps in `ldd` | PASS — only `libc.so.6` and `ld-linux-x86-64.so.2` |
| Runs without compat runtime | PASS for banner/tick startup; enters native event loop (blocking by design) |
| ABI coverage | OPEN — see `docs/interfaces/abi-map.md`; Android NDK/Bionic shim in progress |

Startup output observed:

```
sober-native 0.1.0 | native Linux ELF | no translation layer
compat backend: native-linux/posix (mmap+pthread+epoll+dlopen)
graphics backends (dlopen): X11 Wayland OpenGL Vulkan
native tick (ms): <elapsed>
sober-native: entering native event loop (Ctrl-C to quit)
```

Note: `sober-native --help` also enters the event loop; a non-blocking
`--version` exit path was requested from worker-3 for CI smoke tests.

Worker-3 also reported `make check` 2/2 CTests and 30/30 functional smoke
tests passing, plus successful `dlsym(RTLD_DEFAULT)` resolution from a
dlopen'd test `.so` (100 exported symbols, hidden-by-default visibility).

## Versioned Bionic libc bridge (#373, lead) — 2026-11-18

`src/compat/bionic_libc.c` + `bionic_libc.map` are generated from
`analysis/rea/bionic-versions.json` by `src/compat/gen_bionic_libc.py`:
398 native register-preserving x86-64 tail-call trampolines + 13 native data
symbols, with version nodes `LIBC`, `LIBC_N`, `LIBC_O`. Built as
`build/ndk-stubs/libc.so`; `readelf -V` confirms all three nodes, and
`__cxa_finalize@@LIBC`, `in6addr_any@@LIBC_N`, `__sendto_chk@@LIBC_O` are
exported. The main ELF's `ldd` is unchanged (libc only).

`tests/guest-dlopen.sh` now reaches `calling init: .../libroblox.so` — i.e.
**every DT_NEEDED entry and all 411 versioned symbols resolve**. The process
then exits 139 inside the guest's `init_array`, because the Android/Bionic
bootstrap (allocator object, JNI singleton, empty-string slot, data seeds,
constructor replay) is not patched in yet. That is the next blocker.

## Phase 2 final verification (2026-11-18) — MILESTONE COMPLETE

Decision 006's done condition is met, verified independently by lead:

| Check | Result |
| --- | --- |
| `cmake --build build` | PASS, clean |
| `build/guest-init-host` (120 MB guest via memfd) | exit 0, stdout `guest init complete` |
| Constructors driven | all 3618 (last `ctor[3617] rva=0x227a8ba`) |
| APS2 packed relocations | 565390/565390 applied, 0 unresolved |
| `tests/guest-dlopen.sh` | PASS — completion signal, on-disk sha unchanged, memfd delta only `DT_INIT_ARRAYSZ` |
| `tests/verify-native.sh` | PASS — native ELF, `ldd` = libc/ld-linux only |
| `ctest` | 2/2 PASS |

Root causes fixed along the way (decisions 007-009): ctor ordering control,
APS2 packed RELA, self-hosting versioned libc stub (sysconf returning 1000
instead of 4096), and the guest's uninitialised arena table. No Android
runtime, Wine, or translation layer is involved.

Out of scope still: rendering a window, login/app shell, full JNI replacement,
`AMediaCodec`/`AImageReader`.

## Remaining work before this log can say PASS overall

- `src/compat/android_compat.*` covering the `libroblox-imports.txt` surface.
- `analysis/rea/findings.json` + `report.md` (worker-1).
- `analysis/ghidra/interface-map.md` (worker-2).
- ABI map rows for the Android NDK/Bionic imports, with evidence.
