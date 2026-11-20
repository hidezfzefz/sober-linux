# ABI map — canonical symbol contract

Owner: **lead**. This is the contract worker-3 implements in `src/compat/` and
what workers 1 & 2 feed with REA/Ghidra evidence.

Rules:
- Only lead edits this file. Request additions via `team_send` to lead.
- Every row must cite evidence (REA evidence ID and/or Ghidra address).
- Status: `required` | `implemented` | `unsupported` (with reason).

## Target

`libroblox.so` (x86-64, Android 26, NDK r28c), Build ID
`204ad049599c96afb75b8a21d5051063e821d6c9`. See
`docs/decisions/003-target-identification.md` and **decision 004**: the compat
surface is the **Android NDK/Bionic** API (GL/EGL, AMediaCodec/AMediaFormat,
AConfiguration, ALooper, ANativeWindow, AAssetManager, AImageReader,
`__android_log_*`, pthreads/dl/libc), **not Win32**.

- Full defined-symbol surface: `docs/interfaces/libroblox-exports.txt` (559).
- Full undefined-symbol surface (what we must reimplement natively):
  `docs/interfaces/libroblox-imports.txt` (573).

## Symbol table

Seed rows come from the shipped `roblox_host_abi.json` profile for this exact
Build ID. RVA values are *guest-side* addresses in `libroblox.so`; worker-3
implements equivalent native host functions in `src/compat/`.

| Symbol / API | Signature | Calling conv. | Source subsystem | Evidence (RVA / ID) | Status |
| --- | --- | --- | --- | --- | --- |
| host allocate (small) | `void* (*)(size_t)` | SysV AMD64 | mimalloc bridge | `0x1dbad62` | required |
| host usable_size | `size_t (*)(void*)` | SysV AMD64 | mimalloc bridge | `0x1dbdfba` | required |
| host reallocate | `void* (*)(void*, size_t)` | SysV AMD64 | mimalloc bridge | `0x227aafa` | required |
| host allocate | `void* (*)(size_t)` | SysV AMD64 | mimalloc bridge | `0x1dbab02` | required |
| host aligned_allocate | `void* (*)(size_t, size_t)` | SysV AMD64 | mimalloc bridge | `0x1dd60e5` | required |
| host free | `void (*)(void*)` | SysV AMD64 | mimalloc bridge | `0x1dbe559` | required |
| JNI singleton initializer | native data slot, 1024 bytes | data | JNI bootstrap | slot `0x7d9cf40`, init `0x1dba834` | required |
| allocator object initializer | native data slot | data | allocator | slot `0x7834570`, init `0x2251b2a` | required |
| empty string initializer | native data slot | data | runtime strings | slot `0x79413b8`, init `0x225dd2a` | required |
| pre-JNI registry initializer | native data slot | data | JNI bootstrap | slot `0x7cdd118`, init `0x227be14` | required |
| Android NDK imports (573) | see `libroblox-imports.txt` | SysV AMD64 | bionic/Android SDK | `nm -D --undefined-only libroblox.so` | required |
| 411 versioned Bionic symbols | see `analysis/rea/bionic-versions.json` | SysV AMD64 | libc/libm/libdl `LIBC`, `LIBC_N`, `LIBC_O` | `readelf -V` / `readelf --dyn-syms -W` | implemented (native `build/ndk-stubs/libc.so`, forwards to glibc) |
| Bionic runtime bootstrap (allocator object `0x7834570`, JNI singleton `0x7d9cf40`, empty string `0x79413b8`, 3618 ctors at `0x7220e58`) | native data + ctor replay | SysV AMD64 | mocktail host-ABI bridge | REA `ev_af5de532...`, `ev_d7ca1d41...` | required — next blocker (guest `init_array` crashes; see `docs/decisions/005-verification-log.md`) |

## Priority Android NDK/Bionic import groups (worker-2, Ghidra)

Source: worker-2 decision message, Ghidra evidence
`ev_e87289a57808a0d0e99194bc534d2aaaa66bd8f438c7be77701c651f374f7292`
(126,706 procedures; `libroblox.so` SHA256
`8afce3e695d08fe830d7857752c459de9a06c97390c7365a6660827e5d072e78`).
The full 573-symbol list is `libroblox-imports.txt`; these 174 named imports
are a **symbol-name-only** priority subset.

**Wrong-build caveat (lead, full review):** the Ghidra evidence above is bound to
the **Flatpak** build SHA `8afce3e6…`, not the frozen AppImage
`3e498d6494…` / BuildID `204ad049…` used by all shipped source and RVAs. It must
NOT be treated as authoritative address evidence; it is retained only as a
historical symbol-category hint. No RVA in this project comes from that build.

| Group | Count | Symbols (prefix/example) | Subsystem | Status |
| --- | --- | --- | --- | --- |
| OpenGL ES | 74 | `gl*` | graphics | required |
| EGL | 17 | `egl*` | graphics/display | required |
| Media codec/format | 33 | `AMediaCodec`, `AMediaFormat`, `AMEDIAFORMAT_*` | media | required |
| Configuration | 9 | `AConfiguration*` | platform | required |
| Looper | 7 | `ALooper*` | events | required |
| Native window | 5 | `ANativeWindow*` | graphics/display | required |
| Assets | 6 | `AAsset`, `AAssetManager` | filesystem | required |
| Images/buffers | 9 | `AImageReader`, `AImage`, `AHardwareBuffer` | graphics/media | required |
| Logging | 4 | `__android_log_*` | diagnostics | required |
| Compression | 4 | `ZSTD_*` | data | required |
| Core | rest | pthreads, `dl*`, Bionic libc | runtime | required |

## Unsupported / stub surfaces (worker-3 handoff, accepted by lead)

Per worker-3's handoff, these are functional stubs, **not** implemented:

| Group | Status | Reason |
| --- | --- | --- |
| `AMediaCodec*` | unsupported (stub) | MediaCodec is Android-only; `dequeueOutputBuffer` returns `AMEDIACODEC_INFO_TRY_AGAIN_LATER`. Needs a native media backend before it can be called implemented. |
| `AImageReader` / `AHardwareBuffer` / `AImage` | unsupported (stub) | Android graphics-buffer API has no direct Linux equivalent yet; native EGL surface path works, buffer sharing does not. |

Do not count these toward "ABI coverage" in `tests/verify-native.sh`.

## REA evidence for the bridge rows (worker-1, 63 evidence IDs total)

- AppImage engine identity (BuildID `204ad049...`): REA `ev_af5de532...`.
  Engine object: REA `ev_08ca2afd...` (basalt), `ev_5eda7ab1...`
  (libbasalt), `ev_1aab0b3a...` (mocktail/bwrap multicall).
- Basalt reads `native/mocktail/roblox_host_abi.json`: REA `ev_d7ca1d41...`.
- Six allocator bridges + 3 initializer anchors cross-checked: RVAs land on
  valid x86-64 prologues; exact BuildID + payload SHA match.
- FMOD output-device vtable `0x6e0b298` from `roblox_compatibility.json`.
- **Two builds — never mix RVAs:** AppImage `204ad049...` / 2.740.931 vs
  Flatpak `791c3f9e...` / 2.745.1142 (SHA `8afce3e6...`).

## Notes

- Constructor replay ranges: `[2,3)` and `[5,3618)`; 3618 `init_array` entries
  at file offset `0x7220e58`. Native allocator = mimalloc strategy.
- worker-1/worker-2 add evidence-backed rows; worker-3 consumes this table and
  must not invent symbols outside it without first requesting them here.
- Every Android/bionic import in `libroblox-imports.txt` must end up either
  implemented in `src/compat/` or documented as unsupported with a reason.
