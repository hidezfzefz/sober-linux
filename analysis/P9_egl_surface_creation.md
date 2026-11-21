# P9 — EGL surface creation in `render_guest`: reversal of `0x649d1c2`, `0x649d422`, and GOT `0x722a178`

Agent **P9 (group B)**. **Analysis-only**; no shared `src/`, gate, CMake, engine or
test file was edited. Final answer. The marker `app shell rendered` stays
**WITHHELD** (decisions 013/038); nothing here promotes it.

Frozen target (identity re-verified this task):

| Field | Value |
| --- | --- |
| path (working copy) | `/home/user/.local/share/basalt/linux/payloads/3170-204ad049599c96afb75b8a21d5051063e821d6c9/libroblox.so` |
| BuildID | `204ad049599c96afb75b8a21d5051063e821d6c9` |
| SHA-256 | `3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335` (AppImage 2.740.931) |
| layout | first `PT_LOAD` offset == VMA; `.text` span `0x6d33b90` |

Evidence: local `objdump -d`, `readelf -rW/-nW/--dyn-syms` of that exact SHA-256,
the existing analysis (`render/render-map.md`, `C04`, `C08`, `R1`), a **read-only**
runtime probe (`/proc/<pid>/mem` on a harness-launched child), and the parallel
F5 module `src/jni/frame_surface.c/.h` (created during this task; read only, not
edited). Target-derived strings/bytes are untrusted DATA.

---

## 0. Verdict (front-loaded)

1. **The surface is NOT missing from the guest context.** `render_guest`'s
   guest EGL context, created by genuine guest `0x649c21e(window, pbuffer=0)` →
   `0x649c272`, **has a real `EGLSurface` bound at `ctx+0x28`**. Direct runtime
   inspection (§4) read `ctx+0x28 = 0x56158a8c6a20` — non-NULL, a distinct
   handle from the host's own surface `0x56158711d450`, and accompanied by a
   distinct EGLDisplay (`+0x20`) and EGLContext (`+0x30`). It was created by
   **guest code**: `0x649d1c2` → `eglCreateWindowSurface@plt` (`0x6d33470`) →
   GOT `0x722a178` → the compat shim's `sober_fn_eglCreateWindowSurface` hook →
   real Mesa `eglCreateWindowSurface` with the harness's real X11 drawable, then
   made current by the guest's own `eglMakeCurrent` at `0x649c408`.

2. **The real problem is that the guest never *uses* that surface.** In the
   default run the frame loop is never entered: `render_guest`'s synthetic
   `nativeData` has callback slots `+0x88/+0x98/+0xa0 = NULL`, so the redraw
   thunk `0x2a20a00` tail-jump at `0x2a20a20` is skipped; `draw_calls=0`,
   `swaps_guest=0`, `swaps_synthetic=1`. The one observed swap is the **host's**
   direct call of `0x649d422` with a separate synthetic `0x48`-byte stack
   context containing the **host's** EGLDisplay/EGLSurface (worker-1 Route B),
   not the guest context. So "only a display/config" is false for the guest
   context, but the guest context is dead state: no guest draw is issued into
   it and no guest-attributed swap presents it.

3. **To get real guest draws**, the missing link is the redraw→frame-loop→swap
   chain with a *real* `nativeData` callback at `+0x98`; §7 gives the exact
   real-guest-code call sequence. The surface-creation/bind half (steps 1–4 of
   §7) is already implemented correctly by the guest and by the parallel F5
   module (`frame_surface.c`), which writes GOT `0x722a178` directly and drives
   the guest's genuine helpers `0x649cf72`/`0x649d1c2`/`0x649c408`.

---

## 1. Reversal of `0x649d1c2` — the surface creation helper

Full disassembly of the frozen object (`objdump -d`, offsets == RVAs):

```asm
649d1c2: push  rbp                       ; void *create_surface(ctx, EGLConfig cfg)
649d1c3: mov   rbp,rsp                   ;   RDI = ctx, RSI = cfg (from caller)
649d1c6: push  r14
649d1c8: push  rbx
649d1c9: mov   0x10(%rdi),%rax           ; rax = ctx+0x10 (shared parent / pbuffer)
649d1cd: test  %rax,%rax
649d1d0: je    649d1df                   ; 0 -> window-surface path
649d1d2: mov   0x20(%rax),%rdi           ; dpy = pbuffer->dpy
649d1d6: xor   %edx,%edx                 ; attribs = NULL
649d1d8: call  6d33460 <eglCreatePbufferSurface@plt>
649d1dd: jmp   649d1ee
649d1df: mov   0x18(%rdi),%rdx           ; win = ctx+0x18  (ANativeWindow*)
649d1e3: mov   0x20(%rdi),%rdi           ; dpy = ctx+0x20  (EGLDisplay)
649d1e7: xor   %ecx,%ecx                 ; attribs = NULL
649d1e9: call  6d33470 <eglCreateWindowSurface@plt>   ; (dpy, cfg=rsi, win, NULL)
649d1ee: test  %rax,%rax
649d1f1: je    649d1f8                   ; NULL -> log error / recover
649d1f3: pop   rbx; pop r14; pop rbp; ret              ; return EGLSurface in RAX
```

Facts:

- Signature is `EGLSurface create_surface(void *ctx, EGLConfig cfg)`: the config
  arrives in **RSI from the caller** (the function never writes RSI before the
  EGL calls). `render-map.md`'s `void *create_surface(void *ctx)` label is
  incomplete; the disassembly and both call sites prove the second argument.
- `ctx+0x10 != 0` selects `eglCreatePbufferSurface([pbuffer+0x20], cfg, NULL)`;
  `ctx+0x10 == 0` selects `eglCreateWindowSurface(ctx+0x20, cfg, ctx+0x18, NULL)`.
  **This is why `render_guest` must pass `pbuffer=0`** to `0x649c21e`: any
  non-zero second argument is stored at `ctx+0x10` and dereferenced as a
  pbuffer object at `0x649cf9a` (config helper) — the documented Route-A fault
  (verified by A/B in `C04 §1.4`, `T3-5 §2.1`). The guest's own device-init
  call sites already pass 0 (`0x648b94a: xor esi,esi` before `call 649c21e`).
- Two callers: `0x649c3f1` (initial EGL setup) stores the result at `ctx+0x28`
  (`0x649c3f6`); `0x649d4ea` (surface recreate/rebind) stores it at `0x649c3f6`
  equivalent after `eglDestroySurface`.
- The **only** `eglCreateWindowSurface@plt` call site in the binary is
  `0x649d1e9` (PLT `0x6d33470`, GOT `0x722a178`) — every guest window surface,
  initial or recreated, funnels through this one PLT/GOT pair.

## 2. Reversal of `0x649d422` — the swap wrapper

```asm
649d422: 48 8b 47 20   mov  0x20(%rdi),%rax   ; EGLDisplay
649d426: 48 8b 77 28   mov  0x28(%rdi),%rsi   ; EGLSurface
649d42a: 48 89 c7      mov  %rax,%rdi
649d42d: e9 9e 60 89 00 jmp  6d334d0 <eglSwapBuffers@plt>
```

- It is a **tail `jmp`, not a `call`**: the return address seen at
  `eglSwapBuffers@plt` (and hence at the GOT `0x722a1a8` hook) is whoever called
  `0x649d422`. A genuine frame reaches it via guest
  `call QWORD PTR [vtable+0x18]` with
  `[0x71912e8+0x18] = [0x7191300] = 0x649d422`; the default harness reaches it
  by a direct host call (`guest_render.c:354`).
- It issues **no GLES draw**, creates/binds **no surface**, and reads only
  `ctx+0x20` (EGLDisplay) and `ctx+0x28` (EGLSurface). If `ctx+0x28` were NULL,
  `eglSwapBuffers(dpy, NULL)` would present nothing. This is why §4's non-NULL
  read matters: the fill is real, even though today it is the *host* fill that
  the Route-B call presents.
- The swap is render vtable slot `[3]` of `0x71912e8` (APS2-populated; file
  bytes zero); the context is the `0x48`-byte object from `0x649c21e`.

## 3. GOT `0x722a178` (`eglCreateWindowSurface`) — how it is actually used

Relocation (verified with `readelf -rW`):

```
000000000722a178  000001c700000007 R_X86_64_JUMP_SLOT  eglCreateWindowSurface + 0
```

Resolution chain at runtime:

1. The guest is loaded with `dlopen(path, RTLD_NOW | RTLD_LOCAL)`
   (`guest_dlopen_host.c:524`), so glibc binds all JUMP_SLOTs at load from the
   global scope. Both shipped harness binaries export the symbol:
   `nm -D build/sober-native | grep eglCreateWindowSurface` →
   `eglCreateWindowSurface@@SOBER_NATIVE_1.0`; likewise `build/guest-init-host`.
   Therefore **GOT `0x722a178` resolves to the harness's exported trampoline**
   in `src/compat/gl_egl_forward.c`, not directly to libEGL's symbol
   (`native_render.c` dlopens libEGL with `RTLD_LOCAL`, so it is not ahead in
   the search order).
2. The trampoline (`TRAMP(eglCreateWindowSurface)`) is
   `movq sober_fn_eglCreateWindowSurface(%rip),%rax; test rax,rax; jz ret; jmp *rax`.
   All guest EGL calls are therefore dispatched through the compat shim's
   function-pointer slots, which `sober_gl_egl_init()` binds to real
   `libEGL.so.1`/`libGLESv2.so.2` symbols via `dlsym`.
3. `render_guest.c:427-430` overwrites
   `sober_fn_eglCreateWindowSurface` with
   `cfg->egl_create_window_surface_hook` =
   `sober_render_hook_egl_create_window_surface()` =
   `&sr_hook_egl_create_window_surface` (`native_render.c:1002-1011`). That hook
   **ignores the guest's `ANativeWindow` argument** and calls
   `g.eglCreateWindowSurface(dpy, cfg, r->native_handle /* real X11 Window */, attribs)`.
   This is the substitution that turns the guest's synthetic window into a real
   drawable and yields a real EGLSurface.

**Important correction to the existing docs.** `render_guest.h:48` says
`eglCreateWindowSurface -> 0x722a178 (substituted by our EGL hook)`, and defines
`SOBER_RENDER_GUEST_GOT_CREATE_SURF = 0x722a178` (`:143`), but
**`render_guest.c` never writes that GOT slot** — grep of all of `src/` finds
the constant only in the header. The three GOT slots `render_guest` actually
patches are `0x72294a8` (fromSurface), `0x722a1a8` (swapBuffers) and `0x722a180`
(getProcAddr). The "substitution" of `0x722a178` is **indirect**, via the compat
function pointer, and works only because the guest's JUMP_SLOT resolves to the
exported trampoline (step 1). Any harness that does not export that trampoline
would silently break the guest's surface creation.

**Corroboration (parallel agent F5, read-only).** `src/jni/frame_surface.c`
(created 00:42–00:43 this session, group C) installs a **direct** GOT hook at
`0x722a178` (`SOBER_FRAME_SURFACE_GOT_CREATE_SURFACE`, `:436`, hook body
`fs_hook_create_surface()` `:308-341` → calls the real host EGL create function →
returns a real EGLSurface) and drives the guest's genuine helpers
`0x649cf72`/`0x649d1c2`/`0x649c408` in `sober_frame_surface_bind_surface()`
(`:587-690`). Its existence and design independently confirm the chain above.

## 4. Runtime proof that the guest context has a real EGLSurface

Because gdb cannot run this guest (its frequent `int3`/SIGTRAP control transfers
either stop the tracer or, with `nopass`, hang the target — four prior sessions
in `ps` are wedged this way) and `ptrace_scope=1` blocks `gdb -p`, I used a
**read-only** analysis wrapper: a tiny C program forks the unmodified
`build/guest-init-host` as its own child (so `/proc/<pid>/mem` is accessible),
lets the normal default run reach `first frame rendered`, parses the guest
context pointer from the harness's own `describe()` line, and `pread`s the
`0x48`-byte context from the child's address space. No engine byte, source file,
or environment-dependent guest state was changed.

Result (one of two identical runs):

```
PARENT childpid=1048873 guest_ctx=0x561588d27220
pread=72 errno=0
CTX vtbl=0x7fa66a5bd2e8 win=0x561588d336c0 dpy=0x561588d45990 SURF=0x56158a8c6a20 eglctx=0x561588ce0e90 w=468 h=513
```

Cross-checks from the same run's stderr:

```
base=0x7fa66342c000  ... vt=0x7fa66a5bd2e8(vt_expect=0x7fa66a5bd2e8)
native window: handle=0x1e00001 ... egl=0x5615870f7920 ctx=0x5615870e18b0 surf=0x56158711d450
guest call 0x649d422 swap(ctx=0x7ffc841ea9a0, egl=0x5615870f7920, surf=0x56158711d450) -> rc=0
```

Interpretation:

- `vtbl = 0x7fa66a5bd2e8 = base + 0x71912e8` ✓ — a genuine guest render vtable
  installed by `0x649c29f`.
- `dpy = 0x561588d45990`, `eglctx = 0x561588ce0e90`, `SURF = 0x56158a8c6a20`
  are all non-NULL and **all distinct** from the host triple
  (`0x5615870f7920 / 0x5615870e18b0 / 0x56158711d450`). So the guest really
  allocated its own EGLDisplay (from `eglGetDisplay(0)` → shim), its own
  EGLContext (`0x649c38f`) and its **own window surface**
  (`0x649d1c2` → GOT `0x722a178` → hook → Mesa). `w/h` at `+0x38/+0x3c` are the
  `eglQuerySurface(0x3057/0x3056)` results from `0x649c42f/0x649c445`.
- The clean, abort-free run is itself evidence: the failure paths of surface
  creation (`0x649d1f8…`) and `eglMakeCurrent` (`0x649ce7f…`) report via
  `eglGetError` and terminate with `__stack_chk_fail`/`int3`; none fired. The
  48 `eglGetProcAddress` loads and 5 wrapped draw names (`gles_loads=48,
  draw_wrapped=5`) further show `0x649c272` ran past `eglMakeCurrent` into the
  GLES loader block (`0x649c633…0x649d300`).

**Conclusion:** `render_guest`'s guest EGL context is *not* "only a display/config".
It is a fully populated, valid, current EGL context with a real bound
EGLSurface. The surface is, however, **synthetic in provenance** (created through
a host substitution hook from a host-shaped `ANativeWindow`), so decision 013
still treats it as seeded state.

## 5. Which guest code creates / binds the surface (all RVAs are file offsets)

Primary path — `0x649c21e(ctx creator)` → `0x649c272(ctx, window, extra=pbuffer)`:

| Guest RVA | Action |
| --- | --- |
| `0x649c29f` | store render vtable `0x71912e8` at `ctx+0x00` |
| `0x649c2a9` | `ctx+0x10 = extra (pbuffer)` — must be 0 for the window path |
| `0x649c2cf` | `ctx+0x18 = window` (ANativeWindow*) |
| `0x649c2d6` | `ANativeWindow_acquire(window)` (skipped when extra≠0) |
| `0x649c338` / `0x649c351` | `eglGetDisplay(0)` → `ctx+0x20`; `eglInitialize` |
| `0x649cf72` | config helper; `rax=[ctx+0x10]? [rax+0x20] : [ctx+0x20]` at `0x649cf9a`; → `eglChooseConfig`; returns EGLConfig (fault `0x649cf9a` iff pbuffer≠0) |
| `0x649c38f` | `eglCreateContext(dpy, cfg, share, attribs)` → `ctx+0x30` |
| `0x649c3f1` → `0x649d1c2` | surface helper (§1): `eglCreateWindowSurface(dpy, cfg, window, NULL)` → GOT `0x722a178` |
| `0x649c3f6` | `ctx+0x28 = returned EGLSurface` |
| `0x649c408` | `eglMakeCurrent(dpy, surf, surf, eglctx)` — **binds the surface** |
| `0x649c42f/0x649c445` | `eglQuerySurface` W/H (0x3057/0x3056) → `ctx+0x38/+0x3c` |
| `0x649c588` | `eglSwapInterval` |

Surface recreate/rebind path (e.g. `onSurfaceChanged`): `0x649d4c0`
`eglMakeCurrent(null)` → `0x649d4d7` `eglDestroySurface(ctx+0x28)` →
`0x649d4df` config helper → `0x649d4ea` **`0x649d1c2(ctx, cfg)`** → store at
`ctx+0x28` → `0x649d501` `eglMakeCurrent(dpy, surf, surf, ctx)`. Vtable rebind is
slot `[2] = 0x649d37c` (`eglGetCurrentContext`+`eglMakeCurrent`).

Device-install sites for the `0x48`-byte context: `0x648b94c` calls
`0x649c21e(window, pbuffer=0)` and wraps the ctx in a `0x20`-byte device object
(vtable `0x71908e0`, ctx at `+0x18`); `0x6490157` calls
`0x649c21e([rdi+0x158], [rdi+0x160])` then dispatches vtable `+0x10` (the
`0x649d37c` rebind). These are the guest's own "render device" plumbing;
`render_guest` calls the creator but does **not** perform this device install.

## 6. Why the bound surface produces no draws today

`render_guest` deliberately gives the four lifecycle thunks a synthetic
`nativeData` whose callback slots `+0x88/+0x98/+0xa0` are NULL (the guest
NULL-tests each at `0x2a20897`, `0x2a2092b`, `0x2a2094f`, `0x2a20a18`,
`0x2a20a4f`). Disassembly confirms the frame trigger:

```asm
; onSurfaceRedrawNeededNative 0x2a20a00
2a20a05: mov 0x140(%rdx),%rsi       ; ANativeWindow from nativeData
2a20a11: mov 0x98(%rdx),%rax        ; redraw/frame callback
2a20a18: test %rax,%rax
2a20a20: jmp  *%rax                 ; TAIL-CALL into guest frame loop
```

With `[+0x98]==NULL` the guest skips the jump, never enters the frame loop,
never reaches the GLES draw dispatcher (`0x6496d0c`; call sites `0x6496dbc`
`glDrawElements`, `0x6496dec`, `0x6496e1a` `glDrawArrays`, `0x6494ae1`
`glClear`), and never does a guest-attributed `call [vtable+0x18]`.
`R1_zero_tagged_swaps.md` records exactly this: `draw_calls=0`, `swaps=1/0/1`,
with `from_surface=2` (the hook works) but the chain terminating at the NULL
callback. F5's `frame_surface.c` further documents that the redraw thunk tests
`[nativeData+0x140]` *before* `[+0x98]` (`0x2a20a0c`) and that the destroy thunk
`0x2a20a6a` zeroes `+0x140` — both must be handled when driving the thunks.

## 7. Real-guest-code call sequence to bind the surface **and get draws**

All RVAs added to the guest load base. This is the sequence the frozen guest
itself executes; steps 1–5 establish a real surface (already achieved today),
step 6 is the actually-missing link to draws, step 7 is the evidence gate.

1. **Install the observation/substitution hooks** (no guest byte patches):
   GOT `0x72294a8` (`ANativeWindow_fromSurface` → return a compat-prefix
   ANativeWindow), GOT `0x722a178` (`eglCreateWindowSurface` → call the real
   host EGL create fn with the real X11/Wayland drawable; F5 writes this slot
   directly, `render_guest` reaches it via `sober_fn_eglCreateWindowSurface`),
   GOT `0x722a1a8` (`eglSwapBuffers`, RA-tag), GOT `0x722a180`
   (`eglGetProcAddress`, count GLES loads/wrap draws). Ensure
   `sober_gl_egl_init()` has populated the shim's `sober_fn_egl*` slots first.
2. **Publish the window with guest provenance.** Prefer calling the guest's own
   bind helper `0x26fea5a(env, Surface token)`: its instructions execute
   `ANativeWindow_fromSurface@plt 0x26fea78`, `incl [0x72a3310]` at `0x26fea90`
   and store the window at global `0x72a3308` (`0x26feaa0`). Verify at the
   `0x72294a8` hook that the return address lies in
   `0x26fea78..0x26feaa7` and that the generation advanced. (A plain host store
   of `0x72a3308`, as `render_guest.c:326-329` does, is synthetic and not
   provenance.)
3. **Create the guest EGL device:** call `0x649c21e(window, pbuffer=0)` →
   `0x649c272`. Verify `ctx != 0`, `ctx+0 == base+0x71912e8`, `ctx+0x10 == 0`.
   This already yields EGLDisplay/EGLContext/**real EGLSurface** (§4). To be
   explicit/robust, F5's `sober_frame_surface_bind_surface()` mirrors exactly:
   `ctx+0x18 = window` (guest insn `0x649c2cf`) → `cfg = 0x649cf72(ctx)` →
   `surf = 0x649d1c2(ctx, cfg)` (reaches GOT `0x722a178`) →
   `ctx+0x28 = surf` (insn `0x649c3f6`) →
   `eglMakeCurrent(ctx+0x20, surf, surf, ctx+0x30)` (insn `0x649c408`).
4. **Install the context into the guest render device** using the real plumbing:
   the `0x648b94c` pattern (0x20-byte object, vtable `0x71908e0`, ctx at `+0x18`)
   or the `0x6490157` wrapper followed by vtable `+0x10` rebind (`0x649d37c`).
   Without this the guest frame code has no path to the context.
5. **Re-assert `nativeData +0x140 = guest-bound window`** after each lifecycle
   thunk, because `0x2a20a6a` (destroy) clears it and `0x2a20a0c` (redraw)
   tests it first. Drive the real thunks in order: created `0x2a20850`,
   changed `0x2a208c0`, redraw `0x2a20a00`.
6. **Supply real guest callbacks and drive the frame loop — the decisive
   missing step.** The `nativeData` passed to `0x2a20a00` must have `+0x98`
   pointing at a **genuine guest redraw callback** (and realistically `+0x88`
   create/rebind and `+0xa0` destroy), not NULL, so that `0x2a20a20` tail-jumps
   into guest code. The guest then issues draws through its
   `eglGetProcAddress`-loaded GLES table (`0x77a4ef0..0x77a4ff8`; dispatcher
   `0x6496d0c`) and dispatches the frame via
   `call QWORD PTR [0x7191300]` → `0x649d422` → `eglSwapBuffers@plt`, this time
   with a **guest return address**. (A host-installed callback pointer would be
   synthetic state and must not count.)
7. **Evidence gate:** require a swap seen at GOT `0x722a1a8` whose RA lies in
   `[base, base+0x6d33b90)` *and* ≥1 counted guest GLES draw in the same window,
   with no synthetic Route-B swap in the counting window; pair with an
   origin-verified `gameLoadedCallback` for the callback leg. The phase-3
   `first frame rendered` marker may remain, but `app shell rendered` stays
   **WITHHELD** while any of these steps is driven by an opt-in/seed knob
   (decisions 013/038).

## 8. Honesty limits

- Static reversal is from the single frozen SHA-256 object; runtime numbers are
  from the unmodified default harness at the date of this report (BuildID and
  SHA re-verified). The `/proc/<pid>/mem` probe is read-only and changed no
  engine state.
- gdb-based confirmation was attempted and abandoned (guest SIGTRAP semantics
  and `ptrace_scope=1`); the `/proc` read is the direct substitute and returned
  the full `0x48`-byte context with `errno=0`.
- F5's `frame_surface.c/.h` appeared in the tree during this task; I read it
  only as corroborating evidence and did not edit it. Whether it is wired into
  a build/gate is outside my scope.
- Nothing here establishes a *genuine* app-shell frame or callback. A real
  EGLSurface exists in the guest context, but it is created via host
  substitution and drawn into by no guest code on the default path; the marker
  remains WITHHELD.
