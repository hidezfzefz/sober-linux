# Decision 004 — Compat target is Android NDK/Bionic, NOT Win32

Status: accepted (lead). **Supersedes** the Win32 wording in decisions 000-002.
This is a correction, not a new direction: decision 003 already identified the
target correctly; this makes the consequence explicit.

- The guest `libroblox.so` is an x86-64 Android ELF. There is no
  `kernel32`/`user32`/`winsock` surface to reimplement.
- worker-3 must **stop building Win32 stubs**. The compat layer in
  `src/compat/` reimplements the Android NDK/Bionic surface that
  `libroblox.so` imports:
  - GL/EGL: `gl*` (74), `egl*` (17)
  - Media: `AMediaCodec`, `AMediaFormat`, `AMEDIAFORMAT_*` (33)
  - Platform: `AConfiguration` (9), `ALooper` (7), `ANativeWindow` (5),
    `AAsset`/`AAssetManager` (6), `AImageReader`/`AImage`/`AHardwareBuffer` (9)
  - Logging: `__android_log_*` (4)
  - Plus `ZSTD_*` (4), pthreads, dl, and libc (Bionic) symbols.
- The canonical import list is `docs/interfaces/libroblox-imports.txt`
  (573 undefined symbols); worker-2's decompilation-derived list (174 named
  NDK imports with REA/Ghidra evidence) is the prioritized subset.
- Source subsystem labels in `docs/interfaces/abi-map.md` describe Android
  subsystems, not Windows DLLs.

Goal restated: a native Linux ELF that needs no mocktail/basalt/Android
runtime — no Wine, no translation layer of any kind.
