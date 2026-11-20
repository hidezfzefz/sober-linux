# Ghidra interface map (task #369, synthesised by lead from worker-2 evidence)

**Build caveat — critical.** The decompiled routines and JNI inventory below
come from worker-2's Ghidra work on the **Flatpak engine**:
`libroblox.so` SHA256 `8afce3e695d08fe830d7857752c459de9a06c97390c7365a6660827e5d072e78`,
Build ID `791c3f9e...`, version 2.745.1142. The AppImage engine that the
host-ABI bridges target is a **different build** (Build ID `204ad049...`,
version 2.740.931). Do **not** reuse these RVAs against the AppImage object.
The *symbol categories and API groups* transfer; the addresses do not.

Primary evidence: worker-2 REA/Ghidra evidence
`ev_e87289a57808a0d0e99194bc534d2aaaa66bd8f438c7be77701c651f374f7292`
(126,706 procedures), deep-pass evidence
`ev_17bccaca010301c7fb6b9c1048a702618be998c86f838669f2f4e13ff5d57829`
(133,315 strings, 44 segments, image base `0x100000`), plus worker-2 decision
message and `analysis/rea/findings.json`.

## 1. Imported Android NDK/Bionic surface -> abi-map groups

| ABI-map group | Count | Representative imports | Native Linux implementation (worker-3) |
| --- | --- | --- | --- |
| OpenGL ES | 74 | `glDrawElements`, `glTexImage2D`, … | 91 register-preserving tail-call trampolines into dlopen'd `libEGL.so.1` / `libGLESv2.so.2` |
| EGL | 17 | `eglInitialize`, `eglCreateContext`, … | same trampoline path; `ANativeWindow` maps to native EGL surface |
| Media | 33 | `AMediaCodec_*`, `AMediaFormat_*`, `AMEDIAFORMAT_*` | `AMediaFormat` native structs implemented; `AMediaCodec*` marked **unsupported (stub)** |
| Configuration | 9 | `AConfiguration_*` | environment-backed native implementation |
| Looper | 7 | `ALooper_*` | `epoll`-backed native implementation |
| Native window | 5 | `ANativeWindow_*` | native EGL surface (plus `libsurface_util_jni.so` consumer) |
| Assets | 6 | `AAsset`, `AAssetManager` | POSIX filesystem asset directory |
| Images/buffers | 9 | `AImageReader`, `AImage`, `AHardwareBuffer` | **unsupported (stub)** — no Linux equivalent yet |
| Logging | 4 | `__android_log_*` | native logging (`[INFO/sober-android]`) |
| Compression | 4 | `ZSTD_*` | trace no-ops (compression provided by system libzstd if needed) |
| Core | rest | pthreads, `dl*`, Bionic libc | glibc + worker-3 shim; **versioned `LIBC`/`LIBC_N`/`LIBC_O` bridge still required** |

## 2. Exported JNI surface -> abi-map / integration categories

From `jni-boundary.json` (573 exported JNI symbols). The native replacement
must expose these names so Java/Kotlin callers (or their native reimplementation)
can bind:

| JNI class prefix | Count | Role |
| --- | --- | --- |
| `NativeGLInterface` | 80 | main engine/graphics/input entry points |
| `JNILinkingProtocol` | 39 | linking/avatar protocol |
| `NativeInputInterface` | 33 | input |
| `NativeSettingsInterface` | 32 | settings/data directories |
| `JNICallProtocol` | 29 | voice/call protocol |
| `JNIAccountProtocol` | 25 | account protocol |
| `WebViewProtocol` | 20 | WebView |
| `MessageBus` | 19 | message bus |
| `JNIActivityLifecycleCallbacks` | 19 | activity lifecycle |
| `JNINativeAdsProtocol` | 19 | ads |
| `MediaPickerProtocol` | 15 | media picker |
| `FlagJniInterface` | 8 | feature flags |
| `MainGameActivity` | 5 | startup |
| other | 230 | assets, storage, purchases, OTA, etc. |

## 3. Decompiled routines of interest

| Function | Subsystem | What it does (observed) |
| --- | --- | --- |
| `JNI_OnLoad` | bootstrap | caches `NativeGLJavaInterface` callback method IDs, installs `LoggingProtocol.getProcessTimestamp` override, resolves `NetworkUtils`, message-bus and data-model helpers, returns `JNI_VERSION_1_6`. |
| `FUN_0202830e` | settings | `LoadClientSettingsFromLocal`; on success applies settings and signals `nativeOnPostResumed` back to Java. |
| `FUN_0203774e` | flags | deferred (post-TTI) flag-cache write via `LoadSettings`. |
| `FUN_020383ca` | flags | flag-cache tombstone initialization. |
| `FUN_06980404` | settings | `LoadSettings` dispatch target. |
| `nativeGetFFlag` | flags | Java FFlag getter (8 exports in class). |
| `nativeInitClientSettings` | settings | thin wrapper over settings pipeline. |
| `nativeGameGlobalInit` | engine | engine-global initialization wrapper. |
| `nativeSetBaseDataDirectories` | settings | stores two path arguments in globals. |

Decompiled sources: `/home/user/sober-rea-fresh/native-decomp/*.c` with paired
`*.evidence.json`.

## 4. Limits

- Stripped binary: internal routines remain `FUN_<address>`; parameter/type
  recovery was disabled, so pseudocode shows raw pointer arithmetic and
  `_DAT_` globals.
- No runtime or `RegisterNatives` proof — JNI bindings are name-based.
- RVAs above belong to the Flatpak build only (see build caveat).
