# Guest post-init render/display map — AppImage engine 204ad049

Owner: **worker-1** (task #381). Date 2026-11-18. Scope: `analysis/render/` only.

## 0. Build identity and evidence

Every RVA below is from **one** object:

| Field | Value |
| --- | --- |
| path | `/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so` |
| SHA-256 | `3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335` |
| BuildID | `204ad049599c96afb75b8a21d5051063e821d6c9` (version 2.740.931 / code 3170) |

`readelf -nW` on that path still reports the exact BuildID above (re-checked
this task). The Flatpak engine (`791c3f9e…`, SHA `8afce3e6…`, 2.745.1142) is a
**different binary** — no RVA in this document comes from it. The JNI
*category* inventory in `analysis/ghidra/interface-map.md` is Flatpak-derived,
so only its category names (not addresses) were used as background.

REA evidence (see `docs/decisions/003/009` for acquisition limits):

| What | Evidence ID |
| --- | --- |
| Engine identity / BuildID | `ev_af5de5324a9e1df40ada21a17ab9fe86a747fc6c81a0421574c952f5d077eba4` |
| Full `.text` disassembly (retained; exceeds MCP response budget) | `ev_d838d804f57d82aed028027e29942e4d198774cab04216a024f18ea6fd1a9bc4` |
| Dynamic symbol table (`readelf --dyn-syms`) | `ev_38ee286ef10912a48ffb7aa49587e8d435a775cac6f5a67268d07a8b5ae7040f` |
| Section headers (`.text`, `.init_array`, GOT, vtable region) | `ev_34eea0146f1ecf619f80c64ef05151d1a5ca24946bc9396129525d88c9a11758` |

Tooling: Ghidra-backed REA fails on this sectionless 120 MB ELF (see
`analysis/rea/findings.json`), so all RVAs below were derived from REA
`inspect_with_objdump` evidence plus targeted local `objdump -d`, `readelf`,
and byte-level xref scans of the same SHA-256 object. Dynamic-symbol RVAs were
cross-checked against `docs/interfaces/libroblox-exports.txt` (verified against
the live binary: `nativeGameGlobalInit=0x233762c`,
`nativeAppBridgeV2InitWithParams=0x24abf96`, `nativeScheduleOnFirstFrame=
0x2ebf950`). All target-derived strings are treated as **untrusted DATA**.

## 1. ELF layout facts used for offset↔RVA

First PT_LOAD: file offset `0x0`, VMA `0x0`, size `0x6d33b90` — **offset == VMA**
for all `.text`, `.plt`, `.rodata` RVAs below. The render vtable region
(`0x6d37c80…`) is in PT_LOAD 2 (offset = VMA − `0x4000`) and is populated by
the APS2 packed relocations at `0x15e00` (already applied by phase-2's
`android_relocs.c` before any post-init call).

## 2. EGL / ANativeWindow import surface (17 EGL symbols)

PLT stubs (VMA), from `objdump` + `readelf -rW`; all are `R_X86_64_JUMP_SLOT`:

| Symbol | PLT VMA | GOT VMA | Direct call sites found in `.text` |
| --- | --- | --- | --- |
| `eglGetDisplay` | `0x6d333d0` | — | `0x649c338` (1) |
| `eglGetError` | `0x6d333e0` | — | 8, all in render cluster `0x649c…`/`0x649d…` |
| `eglInitialize` | `0x6d333f0` | — | `0x649c351` (1) |
| `eglCreateContext` | `0x6d33400` | — | `0x649c38f` (1) |
| `eglMakeCurrent` | `0x6d33410` | `0x722a148` | `0x649c408`, `0x649d2e4`, `0x649d3b0`, `0x649d4ca`, `0x649d501` (5) |
| `eglQuerySurface` | `0x6d33420` | — | `0x649c42f`, `0x649c445`, `0x649d51f`, `0x649d535` (4) |
| `eglGetConfigAttrib` | `0x6d33430` | — | `0x649c4ac` (1) |
| `eglSwapInterval` | `0x6d33440` | — | `0x649c588` (1) |
| `eglChooseConfig` | `0x6d33450` | — | `0x649d73a` (1) |
| `eglCreatePbufferSurface` | `0x6d33460` | — | `0x649d1d8` (1) |
| `eglCreateWindowSurface` | `0x6d33470` | `0x722a178` | `0x649d1e9` (1) |
| `eglGetProcAddress` | `0x6d33480` | — | 70 — GLES function-pointer loader at `0x649c633…`, `0x649d25d`, `0x649d2a9` |
| `eglDestroyContext` | `0x6d33490` | — | `0x649d2fc` (1) |
| `eglDestroySurface` | `0x6d334a0` | — | `0x649d314`, `0x649d4d7` (2) |
| `eglTerminate` | `0x6d334b0` | — | `0x649d324` (1) |
| `eglGetCurrentContext` | `0x6d334c0` | — | `0x649d38c` (1) |
| `eglSwapBuffers` | `0x6d334d0` | `0x722a1a8` | **0 direct calls** — reached by tail-call `jmp` in `0x649d42d` (see §3) |

`ANativeWindow_*` PLT stubs: `ANativeWindow_fromSurface` `0x6d31ad0` (calls at
`0x26fea78`, `0x2a2087c`, `0x2a20907`), `ANativeWindow_acquire` `0x6d32100`,
`ANativeWindow_release` `0x6d31ac0`. The guest's `fromSurface` call at
`0x26fea78` stores the resulting window in global `0x72a3308` and is in the
app-start path; **a native harness has no Java `Surface`, so worker-2's native
window must be passed directly to the guest's EGL init (§5)**.

## 3. Guest render internals (all AppImage RVAs, no symbols — names are ours)

| RVA | Role | Evidence (objdump excerpt / xref) |
| --- | --- | --- |
| `0x649c21e` | **Top-level render-context creator.** Allocates `0x48` bytes via the patched allocator bridge `0x1dbab02`, then calls `0x649c272`; returns the context pointer. Callers at `0x648b94c`, `0x6490157`. | `e8 … call 1dbab02; mov rax,rbx; call 649c272` |
| `0x649c272` | **EGL context + surface setup** (the core "NativeGLInterface surface/window creation" routine). Takes `(ctx, window, extra)`: if `extra != 0` it skips `ANativeWindow_acquire`; calls `eglGetDisplay(0)`, `eglInitialize`, config helper `0x649cf72`, `eglCreateContext`, surface helper `0x649d1c2`, `eglMakeCurrent`, `eglQuerySurface` (width `0x3057`, height `0x3056`), `eglSwapInterval` (`0x649c588`). Stores EGLDisplay at `ctx+0x20`, EGLSurface at `ctx+0x28`, EGLContext at `ctx+0x30`. Installs vtable `0x71912e8` at `ctx+0`. Also calls internal `0x24af927` (inside `nativeAppBridgeV2InitWithParams`). Only caller is `0x649c244` inside `0x649c21e`. | disassembly `0x649c272..0x649c5a4` |
| `0x649cf72` | EGL config helper (called from `0x649c365` and `0x649d4df`); `eglGetError` diagnostics. | 2 xrefs |
| `0x649d1c2` | **Surface creation helper.** Branches between `eglCreatePbufferSurface` (`0x649d1d8`) and `eglCreateWindowSurface` (`0x649d1e9`). Called from `0x649c3f1` (initial create) and `0x649d4ea` (surface update). | 2 xrefs |
| `0x649d24d`, `0x649d285` | GLES entry loaders via `eglGetProcAddress` (the 70-call loader block at `0x649c633…`). | xrefs above |
| `0x649d2bc` | EGL teardown: `eglMakeCurrent(null)`, `eglDestroyContext`, `eglDestroySurface`, `eglTerminate`, `ANativeWindow_release`. | 1 caller (`0x649d35f`) |
| `0x649d37c` | Context rebind (`eglGetCurrentContext`, `eglMakeCurrent`). | vtable[2] |
| `0x649d422` | **First draw/swap entry point.** `mov rax,[rdi+0x20]` (EGLDisplay), `mov rsi,[rdi+0x28]` (EGLSurface), `mov rax,rdi`, `jmp eglSwapBuffers@plt`. Takes the `0x48`-byte render context in RDI. | `0x649d422: 48 8b 47 20 / 48 8b 77 28 / 48 89 c7 / e9 … 6d334d0` |
| `0x649d432` | ANativeWindow release/acquire pair (`0x649d443`, `0x649d44f`). | vtable[5] |
| `0x71912e8` | **Render vtable** (populated by packed RELA at runtime; file bytes are zero). Layout confirmed from addends: `[0]=0x649d2bc` teardown, `[1]=0x649d356`, `[2]=0x649d37c` rebind, **`[3] @ slot 0x7191300 = 0x649d422` swap**, `[4]=0x1df06e0` (`NativeGLInterface_shouldDisplayOpenGLUnsupportedMessage`), `[5]=0x649d432`, `[6]=0x649d45e`, `[7]=0x1dd72a0` (`nativeOnFragmentStart`), `[8]=0x649d466`, … | APS2 decode (`decode_aps2.py`) |
| `0x72a3308` | Global ANativeWindow* set by the fromSurface path (`0x26feaa0`). | disassembly |
| `0x72293f8` | Guest "JNI singleton"/ABI-global slot used throughout this code (read at `0x649c28c`, `0x26fea6a`, `0x2ebf961`, …). Initialised by phase-2 bootstrap ctor[5]. | xrefs |

## 3b. Call ABI summary and 0x48-byte render context

Verified from the disassembly of `0x649c272`, `0x649d1c2` and `0x649d422`:

| RVA | C ABI | Arguments / objects |
| --- | --- | --- |
| `0x649c21e` | `void *create_context(void *native_window, void *extra_or_null)` | arg1 stored at `ctx+0x18` and passed as the ANativeWindow to `eglCreateWindowSurface`; arg2 != 0 makes EGL init skip `ANativeWindow_acquire` (`0x649c2c2..0x649c2db`). Returns the `0x48`-byte context. No JNIEnv. |
| `0x649c272` | `int egl_init(void *ctx, void *native_window, void *extra)` | internal; called by `0x649c21e`. |
| `0x649d1c2` | `void *create_surface(void *ctx)` | `ctx+0x10 != 0` → `eglCreatePbufferSurface`; else `eglCreateWindowSurface(display=ctx+0x20, window=ctx+0x18, NULL)`. |
| `0x649d422` | `void swap(void *ctx)` | reads EGLDisplay `ctx+0x20`, EGLSurface `ctx+0x28`, tail-jumps `eglSwapBuffers`. No JNIEnv, no window at call time. |

Context struct layout (guest-side, by disassembly):

```
+0x00  vtable pointer = 0x71912e8 (filled by APS2; [3]@0x7191300 = swap 0x649d422)
+0x08  0
+0x10  pbuffer object pointer (0 => use window surface)
+0x18  ANativeWindow *
+0x20  EGLDisplay
+0x28  EGLSurface
+0x30  EGLContext
+0x38  width  (eglQuerySurface 0x3057)
+0x3c  height (eglQuerySurface 0x3056)
```

**Deterministic 'first frame rendered' signal for worker-3/lead:** the guest's
swap call ends in `eglSwapBuffers@plt` (`0x6d334d0`). Worker-2's native EGL
backend already hooks two guest PLT symbols; have that hook record that the
swap originated from guest RVA `0x649d422` (or that a guest-driven
`eglSwapBuffers` occurred after the `guest init complete` marker), log the
guest RVA + return status, and gate the `first frame rendered` marker on that
log line. This satisfies lead's scope correction (marker must depend on actual
guest render entry-point calls, not the standalone test frame).

## 4. JNI callbacks the guest expects (surface lifecycle)

The guest registers a table of Java callback descriptors in `.data.rel.ro`
through the same APS2 relocations. Exact entries in the range
`0x6d37c80..0x6d37d78` (slot → name / signature / thunk):

| Slot | Method name | JNI signature | Native thunk |
| --- | --- | --- | --- |
| `0x6d37cd8` | `onSurfaceCreatedNative` | `(JLandroid/view/Surface;)V` | `0x2a20850` |
| `0x6d37cf0` | `onSurfaceChangedNative` | `(JLandroid/view/Surface;III)V` | `0x2a208c0` |
| `0x6d37d08` | `onSurfaceRedrawNeededNative` | `(JLandroid/view/Surface;)V` | `0x2a20a00` |
| `0x6d37d20` | `onSurfaceDestroyedNative` | `(J)V` | `0x2a20a30` |
| `0x6d37cc0` | `onWindowFocusChangedNative` | `(JZ)V` | `0x2a20830` |
| `0x6d37c90` | `onConfigurationChangedNative` | `(JLandroid/content/res/Configuration;)V` | `0x2a207e0` |
| `0x6d37ca8` | `onTrimMemoryNative` | `(JI)V` | `0x2a20810` |

`JNI_OnLoad` (`0x227af5a`) caches `LoggingProtocol.getProcessTimestamp()J`
(class string `0x535154`, method `0x2defc6`, sig `0x33061d`) **and**
`com/roblox/engine/jni/NativeGLJavaInterface.getDeviceStaticParams()
Lcom/roblox/engine/jni/model/DeviceStaticParams;` (class string `0x3dea65`,
method `0x234658`, sig `0x315c46`). Strings `onSurfaceCreatedNative`
(`0x26bea6`), `onSurfaceChangedNative` (`0x2dd158`) and
`onSurfaceRedrawNeededNative` (`0x3f8dc5`) are in this callback table; a log
string at `0x2df229` reads `nativeActivity_onSurfaceChanged: state:{}` (the
`RBX::NativeDataModelManager` handler).

**Implication for the native port:** these are Java→native callbacks (the
guest calls *into* Java). A harness with no JVM cannot satisfy them via real
JNI; worker-3 should either provide native stubs at the thunk RVAs or, more
simply, bypass them by calling the internal surface/EGL routines directly
(§5). The `fromSurface` path also requires a Java `Surface` object, so it is
not usable as-is.

## 5. Ranked post-init call sequence for worker-3

Evidence-based, smallest-first; each step is independently testable. All RVAs
are added to the guest load base (phase-2 harness already has it).

1. **Create/native window + EGL context/surface in our own code (worker-2,
   `src/render/`).** This is the only part that must be pure native X11/Wayland
   + `libEGL.so.1`; no guest call is needed to have a real drawable.
2. **Provide EGL handles to the guest.** Two supported routes:
   - **Route A (guest-driven, higher fidelity):** call guest `0x649c21e`
     (`create_context`) with `(native ANativeWindow* as RDI, 0 or opaque extra
     as RSI)`. The guest will call `eglGetDisplay(0)`, `eglInitialize`,
     `eglCreateContext`, `eglCreateWindowSurface(native_window)`,
     `eglMakeCurrent` through our compat shims (all resolved in phase 2) and
     return its `0x48`-byte context. Non-zero RSI makes it skip
     `ANativeWindow_acquire` (see `0x649c2c2..0x649c2db`) — use that when
     passing a borrowed native window.
   - **Route B (fastest deterministic):** allocate a `0x48`-byte context in the
     harness, write worker-2's native `EGLDisplay` at `+0x20` and `EGLSurface`
     at `+0x28`, call guest `0x649d422` (the swap wrapper). This exercises a
     real guest entry point with an exact RVA and a native EGL surface.
3. **First draw:** the guest's GLES functions are loaded via
   `eglGetProcAddress` (70 calls at `0x649c633…`, helpers `0x649d24d`/
   `0x649d285`). For the milestone, worker-2's native code performs the first
   clear/draw (e.g. `glClearColor`+`glClear`) using the current native context.
4. **Present:** call guest swap `0x649d422` (Route B) or let the guest's own
   swap path run (Route A). Either ends in `eglSwapBuffers@plt` at
   `0x6d334d0` → our native EGL shim.
5. **Deterministic marker:** print `first frame rendered` only after the swap
   returns, then run the phase-2 shutdown path. Preserve `guest init complete`
   and all phase-2 checks.

Do **not** call `ANativeWindow_fromSurface` (`0x26fea78`) without a real Java
`Surface`, and do not call the Java callback thunks in §4 without a JVM. The
flat `0x649d422` swap wrapper and the `0x649c21e` creator are the safest guest
entry points.

## 6. Limits / open items

- Internal routines are unnamed (stripped); names like `create_context` are
  worker-1's labels, not recovered symbols. RVAs are exact; semantics come from
  the cited disassembly.
- No runtime proof that Route A's `eglCreateWindowSurface` accepts our
  `ANativeWindow` shim as-is; Route B is the fallback if it rejects it.
- The 70 `eglGetProcAddress` loads indicate ANGLE/GLES extension usage; a
  first-frame clear does not depend on them.
- No JNI VM exists in the harness, so the §4 callbacks can only be stubbed;
  full JNI replacement is explicitly out of scope (decision 010).
- If worker-2 publishes `src/render/native_render.h` with a different surface
  abstraction, adapt step 2 to pass its native `EGLDisplay`/`EGLSurface`
  handles into Route B; the guest RVAs do not change.
