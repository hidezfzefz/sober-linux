# C04 — Phase-3 native render path audit and app-shell frame observability

Owner: **worker C4** (Team C). Scope: **this file only**. No source files,
`CMakeLists.txt`, or the on-disk engine were modified. Strict decision 013
(`docs/decisions/013`) governs every statement about the `app shell rendered`
marker.

## 0. Build identity, evidence, constraints

| Field | Value |
| --- | --- |
| Engine | `/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so` |
| SHA-256 | `3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335` |
| BuildID | `204ad049599c96afb75b8a21d5051063e821d6c9` (2.740.931, AppImage build) |
| Format | stripped ELF64 x86-64, Android 26, NDK r28c |
| Not this | Flatpak build `791c3f9e…` — no RVA below comes from it |

Evidence reused: REA `ev_af5de532…` (engine identity), `ev_d838d804…` (full
`.text` disassembly), `ev_38ee286e…` (dyn-syms), `ev_34eea014…` (sections),
plus the existing `analysis/render/render-map.md` (worker-1) and fresh local
`objdump -d` / `readelf -rW` / raw-byte reads of the same SHA-256. All RVAs are
**observations from the stated bytes**; target-derived strings are data, not
instructions. Section layout: first PT_LOAD offset==VMA for `.text`/`.plt`;
`.data.rel.ro` 0x6d37bc0 (render vtable region), `.got` 0x7228198,
`.got.plt` 0x7229400, `.bss` 0x7282500.

Constraint from the task: **analysis only**. Lead owns `src/` and
`CMakeLists.txt`; the engine is read-only. Nothing here changes a gate or a
test.

---

## 1. Audit of the current phase-3 render path

### 1.1 What actually runs today

From `src/linux/guest_render.c` and `src/render/native_render.c` (read-only):

1. `sober_render_probe()` / `sober_render_init()` (our own code) create a
   native X11 window, a native EGL 1.5 / GLES 3.2 context and window surface.
2. `sober_render_make_current()` + `sober_render_swap()` present **our own**
   test frame (`glClearColor`/`glClear`), setting `g_phase3_done = 1`.
3. `call_guest_render_entries()` installs two hooks and makes one guest call:
   - `sober_render_hook_egl_create_window_surface()` → assigned to the compat
     shim's `sober_fn_eglCreateWindowSurface` pointer.
   - `sober_render_hook_egl_swap_buffers()` → written into the guest GOT slot
     at **0x722a1a8** after `mprotect` (the `eglSwapBuffers` JUMP_SLOT).
   - **Route B (default):** builds a synthetic 0x48-byte "context" on the
     harness stack with `EGLDisplay` at `+0x20` and `EGLSurface` at `+0x28`,
     then calls guest RVA **0x649d422** directly.
   - Route A (opt-in `SOBER_GUEST_ROUTE_A`) calls guest **0x649c21e** with
     `(native_window, 1)`.
4. `sr_hook_egl_swap_buffers()` (native_render.c:1007) forwards to the real
   driver, increments `g_guest_swaps` / `r->guest_swaps`, and stores
   `g_phase3_done = 1`.
5. `emit_marker("first frame rendered")`.

### 1.2 The three observation points (and one that is missing)

| Point | RVA / slot | Installed today? | What it proves |
| --- | --- | --- | --- |
| Flat swap wrapper | guest `0x649d422` | called once, directly, by the harness | a guest *instruction sequence* ran; no draw, no surface |
| `eglSwapBuffers` PLT | `0x6d334d0` (`jmp *0x4f6cd2(%rip)` → 0x722a1a8) | GOT 0x722a1a8 overwritten | a swap of **some** frame traversed guest PLT |
| `eglCreateWindowSurface` PLT | `0x6d33470`, GOT `0x722a178` | substituted via shim function pointer | guest surface creation can receive our X11 handle |
| `ANativeWindow_fromSurface` PLT | `0x6d31ad0`, GOT **`0x72294a8`** | **NOT installed** (hook exists in native_render.c:975 but `guest_render.c` never writes this GOT) | the real app-shell surface-binding path (§2.1) is not instrumented at all |

`readelf -rW` confirms all four are `R_X86_64_JUMP_SLOT`:
`eglSwapBuffers`→0x722a1a8, `eglCreateWindowSurface`→0x722a178,
`ANativeWindow_fromSurface`→0x72294a8, `eglMakeCurrent`→0x722a148;
`ANativeWindow_acquire/getHeight/getWidth`→0x72297c0/0x72297c8/0x72297d0.

### 1.3 The swap wrapper (exact bytes, RVA 0x649d422)

```
649d422: 48 8b 47 20   mov  0x20(%rdi),%rax   ; EGLDisplay
649d426: 48 8b 77 28   mov  0x28(%rdi),%rsi   ; EGLSurface
649d42a: 48 89 c7      mov  %rax,%rdi
649d42d: e9 9e 60 89 00  jmp  6d334d0 <eglSwapBuffers@plt>
```

`jmp` (not `call`): the return address on the stack at `eglSwapBuffers` entry
is **whoever called 0x649d422**. Today that is the host C caller in
`guest_render.c`; in a real frame it is the guest code after a `call [vtable+0x18]`.
This asymmetry is the basis of the evidence plan (§3). The wrapper itself
issues **no GLES draw**, creates/binds **no surface**, and touches only `+0x20`
and `+0x28` of the 0x48-byte context.

### 1.4 Route A fault root cause (new finding)

`0x649c21e` (create_context) allocates 0x48 bytes via bridge `0x1dbab02`, then:

```
649c23e: mov %r15,%rsi   ; r15 = arg1 = ANativeWindow
649c241: mov %r14,%rdx   ; r14 = arg2 = "extra"
649c244: call 649c272    ; egl_init(ctx, window, extra)
```

`egl_init` does `649c2a9: mov %rdx,0x10(%rdi)` — the `extra` argument is the
**pbuffer object stored at `ctx+0x10`**, not a boolean. Confirmed by surface
helper `0x649d1c2`: `mov 0x10(%rdi),%rax; test %rax,%rax; jne → eglCreatePbufferSurface
([rax+0x20]); else → eglCreateWindowSurface([ctx+0x18])`. Config helper
`0x649cf72` likewise does `rax=[rdi+0x10]; if rax!=0 use [rax+0x20]` at
`0x649cf9a`.

The harness passes `extra = 1`. Therefore `ctx+0x10 = 1`, and the config helper
dereferences address `0x21` at **`0x649cf9a`** — exactly the documented Route A
fault. **Inference (high confidence, A/B-verifiable):** passing `extra = 0`
makes the guest take the real window path — `ANativeWindow_acquire` at
`0x649c2d6`, `ANativeWindow_getWidth`/`getHeight` at `0x649c303`/`0x649c30f`,
`eglCreateWindowSurface` via `0x649d1c2` → PLT `0x6d33470` — and removes this
fault. This is the flag semantic the real surface path uses; the existing
render-map wording ("non-zero RSI skips acquire") is incomplete: it also
installs a bogus pbuffer pointer.

### 1.5 What the "first frame" does and does not prove

The single swap at `0x649d422` re-presents **our native frame** through the
guest PLT. There is no guest-created render context, no `ANativeWindow` bound,
no guest GLES draw, no vtable-dispatched frame loop, and no surface lifecycle
callback. The decision-013 render half of the gate is currently satisfied by
this synthetic call: `guest_app_shell.c` reads `sober_render_phase3_done()`,
which the GOT hook sets on the **first** swap. So a future genuine gate
callback would pair with a *synthetic* render signal, not an app-shell frame.
**This is the central audit finding for C4.**

---

## 2. What a genuine app-shell frame requires (RVAs)

### 2.1 Surface lifecycle — bind a real ANativeWindow into the guest

Callbacks registered by the guest (render-map §4); all are Java→native thunks
taking a JNIEnv and a `jobject Surface` and a native pointer in RDX/RSI:

| Callback | Thunk RVA | Guest action (observed) |
| --- | --- | --- |
| `onSurfaceCreatedNative` | `0x2a20850` | release old window at `[obj+0x140]`; `ANativeWindow_fromSurface` (PLT `0x6d31ad0`); store new window at `[obj+0x140]`; call `[obj+0x88]` with `(rdi=obj, rsi=window)` at `0x2a208a6` |
| `onSurfaceChangedNative` | `0x2a208c0` | acquire/rebind window; call `[obj+0xa0]` (destroy, `0x2a20933`) and `[obj+0x88]` (create/rebind, `0x2a20957`) |
| `onSurfaceRedrawNeededNative` | `0x2a20a00` | `rsi=[obj+0x140]` (window); tail-call `[obj+0x98]` at `0x2a20a20` — **the redraw/frame trigger** |
| `onSurfaceDestroyedNative` | `0x2a20a30` | call `[obj+0xa0]` with window |

The app-start path itself sets global ANativeWindow at **`0x72a3308`** via
`ANativeWindow_fromSurface` at `0x26fea78` → store at `0x26feaa0`, inside
`nativeAppBridgeV2StartAppWithParams`; consumers of that global are at
`0x2773232`, `0x2ebc8fb`, `0x2ebd5e2`, `0x2ebec04`, `0x2ebed4b`, `0x2ebef4d`.
A native-only harness has no Java `Surface`, so this path can only be entered
by substituting the window through GOT `0x72294a8` (the un-installed hook of
§1.2). The compat shim's `ANativeWindow_fromSurface` (`android_compat.c:498`)
returns a 12-byte synthetic `{refs,width,height}` — enough for
`ANativeWindow_getWidth/getHeight/acquire` (GOT 0x72297d0/0x72297c8/0x72297c0),
but a real `eglCreateWindowSurface` needs the substitution hook at `0x722a178`.

### 2.2 Guest render device creation (the real Route A)

- `0x649c21e` create_context(window, pbuffer=**0**) → `0x649c272` egl_init.
- `0x649c29f` installs render vtable **`0x71912e8`** at `ctx+0`.
- `0x649cf72` EGL config helper (fault site `0x649cf9a` avoided with pbuffer=0).
- `0x649d1c2` surface helper → `eglCreateWindowSurface` PLT `0x6d33470`.
- `eglCreateContext` PLT `0x6d33400` (call site `0x649c38f`),
  `eglMakeCurrent` PLT `0x6d33410` (call `0x649c408`), stored at
  `ctx+0x20/+0x28/+0x30`; width/height at `ctx+0x38/+0x3c`.
- Two callers of `0x649c21e`: `0x648b94c` (inside graphics-device init
  function `0x648b682`; wraps ctx in a 0x20 object with vtable `0x71908e0`,
  ctx at `+0x18`) and the small wrapper `0x6490140` (loads
  `[rdi+0x158]/[rdi+0x160]` → create_context, then `call [vtable+0x10]` =
  rebind `0x649d37c`). These carry the real window into the render layer.

### 2.3 The frame loop and the swap vmethod

- Render vtable `0x71912e8` (file bytes zero; populated by APS2 packed RELA).
  Slot `[3] @ 0x7191300 = 0x649d422` (swap). Slots: `[0]=0x649d2bc`
  teardown, `[2]=0x649d37c` rebind, `[5]=0x649d432` window swap/release.
- A real frame therefore dispatches `call QWORD PTR [vtable+0x18]` → `0x649d422`
  → PLT `0x6d334d0` → GOT `0x722a1a8`. **Unknown (honest):** the exact guest
  frame-loop function that executes this indirect call was not statically
  pinned — vtable dispatch gives ~11,600 `call *0x18(%r..)` sites in `.text`,
  and the vtable has no direct code references. The redraw trigger
  `[obj+0x98]` (from `0x2a20a11`) and `[obj+0x88]`/`[obj+0xa0]` are the
  lifecycle entry points; discovering the downstream loop belongs in the
  runtime evidence plan (§4, step E4), not in a guess here.
- Guest GLES work for a real frame comes from the ~70 `eglGetProcAddress`
  loads beginning at `0x649c633`, helpers `0x649d24d`/`0x649d285`. A synthetic
  swap has none of these; counting them is a second discriminator.

### 2.4 The decision-013 gate callbacks (render alone is never enough)

Per `docs/decisions/013` the marker needs a **genuine** guest→Java app-shell
callback plus the phase-3 render call, with no guest-mutating opt-in active:

- `gameLoadedCallback(J)V`: notifier `0x27eaf6e` → thunk `0x25848e0`
  (`CallStaticVoidMethodV`, JNI slot 142 / `0x470`); cached class `0x72a2f48`,
  method `0x72a2fc0` (`analysis/callback/callback-map.md`).
- `onAppShellReloadNeeded()V`: cached slot `0x72a3020` — `analysis/jni/appshell-reload-trigger.md`
  shows **no native reader**, so it cannot fire in a native-only harness.
- `onAppBridgeNotification(String,String)` (id `0x72a3028` → thunk `0x3dea91`)
  is explicitly **rejected** as JNI plumbing.
- `nativeScheduleOnFirstFrame` `0x2ebf950` (real app-start call sequence,
  PROGRESS.md Phase 7) registers a Java-side first-frame action via
  `call *0x710(%rax)` — it is app-start plumbing, not itself render proof.

---

## 3. How a genuine app-shell frame becomes observable at GOT 0x722a1a8

The hook at `0x722a1a8` sees **every** swap through `eglSwapBuffers@plt`,
regardless of origin. Both the harness's direct `0x649d422` call and a real
vtable-dispatched frame swap converge on the same tail-jump, so the current
counter (`g_guest_swaps == 1`) cannot distinguish them. To make the GOT signal
*frame-specific* and decision-013-honest, an opt-in evidence build must add
four things:

1. **Caller tagging.** In the GOT hook, capture `__builtin_return_address(0)`.
   - Synthetic: return address in the **host** binary (caller of `0x649d422`
     in `guest_render.c`).
   - Real: return address in **guest .text** immediately after a
     `call [vtable+0x18]` (the frame loop). Assert `base ≤ ra < base+0x6d33b90`.
   Keep separate counters `synthetic_swaps` / `guest_loop_swaps`.
2. **Draw correlation.** Count calls into the guest-loaded GLES function
   pointers from `eglGetProcAddress` (`0x649c633…`). Require ≥1 guest draw
   between two tagged GOT swaps before calling the interval a "frame".
3. **Gate correlation.** Only treat tagged swaps occurring **after** a genuine
   `gameLoadedCallback`/`onAppShellReloadNeeded` from the real path (§2.4) as
   app-shell frames; never after the harness-triggered ALT thunk or a seeded
   state.
4. **Suppress the synthetic call in evidence mode.** Do not execute Route B's
   direct `0x649d422` call in the evidence run (or tag it explicitly), so
   `guest_loop_swaps` is unambiguous.

Even with all four, the marker stays **withheld**: every opt-in is added to
the decision-013 forced-withhold list, and `app shell rendered` remains
gated on a genuine callback + render with no seeded state. The evidence plan
produces a diagnostic log line only.

---

## 4. Opt-in evidence plan (default OFF; force-withhold the marker)

All steps are new, default-disabled knobs. Each logs an `[ev-*]` line and
leaves the engine and on-disk SHA untouched. A/B each step with the knob OFF.

| Step | Env / knob | Action & exact RVAs / GOTs | Expected observation |
| --- | --- | --- | --- |
| **E0 baseline** | (none) | Run current `tests/first-frame.sh` path. | `g_guest_swaps==1`, return address = host caller of `0x649d422`; 0 guest GLES draws; `sober_render_phase3_done()==1`. Documents that today's render evidence is synthetic. |
| **E1 tag swaps** | `SOBER_RENDER_FRAME_EVIDENCE=1` | Replace the GOT `0x722a1a8` hook with a variant capturing `__builtin_return_address(0)`; split counters; log `ra` and whether it lies in guest `.text`. | Baseline RA host-based; after a guest-loop swap, RA in guest text. Establishes the discriminator without changing rendering (still forward to real driver). |
| **E2 install fromSurface hook** | `SOBER_EVIDENCE_SURFACE=1` | Write `sober_render_hook_anative_window_from_surface()` into GOT **`0x72294a8`**. Drive `onSurfaceCreatedNative` **`0x2a20850`** / `onSurfaceChangedNative` **`0x2a208c0`** with the native window; verify `ANativeWindow_fromSurface` PLT `0x6d31ad0`, object `+0x140`, and callback `[+0x88]` (`0x2a208a6`/`0x2a20957`) reached. | `[ev-surface] window bound`, `+0x140 != 0`, `[+0x88]` hit count ≥1. |
| **E3 real render device** | `SOBER_EVIDENCE_DEVICE=1` | Call `0x649c21e(window, pbuffer=0)` (fix from §1.4). Confirm no fault at `0x649cf9a`; confirm `eglCreateWindowSurface` reaches GOT `0x722a178` (substitution hook), vtable `0x71912e8` installed at `ctx+0`, EGLDisplay/EGLSurface at `+0x20/+0x28`. | `[ev-device] ctx=%p vt=71912e8 surf via 722a178`. A/B: with pbuffer=1 reproduce the `0x649cf9a` fault. |
| **E4 drive a redraw + count draws** | `SOBER_EVIDENCE_REDRAW=1` | Trigger `onSurfaceRedrawNeededNative` **`0x2a20a00`** → `[+0x98]` (`0x2a20a11`). Instrument the GLES pointers loaded at `0x649c633…`; capture the guest `call [vtable+0x18]` return site dynamically from E1. | `guest_loop_swaps ≥ 2`, guest draw count ≥1 between them, RA of both swaps inside guest `.text` (identifies the frame-loop call site). |
| **E5 gate correlation** | `SOBER_EVIDENCE_FRAME=1` (+ strict gate only) | Fire the genuine gate callback from the real path: `gameLoadedCallback` notifier `0x27eaf6e` → thunk `0x25848e0` (class `0x72a2f48`, method `0x72a2fc0`), or `onAppShellReloadNeeded` slot `0x72a3020` if a native reader is proven. Log swaps occurring after it. | `[ev-frame] app-shell frame: loop_swaps=N draws=M ra=<guest>` **as a diagnostic line only**; marker still withheld because E1–E5 are opt-ins. |

**Invariants to re-check after each step:** engine SHA `3e498d64…` unchanged;
phase-2 `guest init complete` and phase-3 `first frame rendered` (default path)
green; `ctest` 2/2; `ldd` libc/ld-linux only; **zero** `app shell rendered`
marker emissions in any evidence mode; every new env var in the forced-withhold
chain in `guest_app_shell.c`.

**Honesty rule:** E2/E3 seed a native window through guest code — useful for
*observability* of the render path, but it is still synthetic guest state and
cannot itself satisfy decision 013. Only a genuine gate callback from the real
path plus a guest-loop swap with guest draws, with **all** seeds/opt-ins off,
could qualify — and the current harness has not reached that state.

---

## 5. RVA quick reference (AppImage build 204ad049)

| RVA / slot | Meaning |
| --- | --- |
| `0x649d422` | flat swap wrapper (tail-jmp `eglSwapBuffers@plt`) |
| `0x6d334d0` / GOT `0x722a1a8` | `eglSwapBuffers` PLT / JUMP_SLOT — current observation point |
| `0x6d31ad0` / GOT `0x72294a8` | `ANativeWindow_fromSurface` PLT / GOT — **not hooked today** |
| `0x722a178` | `eglCreateWindowSurface` GOT (substituted by shim) |
| `0x72297c0/8/d0` | `ANativeWindow_acquire/getHeight/getWidth` GOTs |
| `0x649c21e` → `0x649c272` | create_context → egl_init (pass pbuffer=**0**) |
| `0x649cf72` / fault `0x649cf9a` | EGL config helper / bogus-pbuffer deref |
| `0x649d1c2` | surface helper (pbuffer vs window surface) |
| `0x649c633…`, `0x649d24d`, `0x649d285` | GLES function-pointer loads (`eglGetProcAddress`) |
| `0x71912e8`, slot[3] `0x7191300` | render vtable / swap vmethod |
| `0x2a20850` / `0x2a208c0` / `0x2a20a00` / `0x2a20a30` | surface created / changed / redraw-needed / destroyed thunks |
| `0x72a3308` / `0x26fea78`,`0x26feaa0` | global ANativeWindow / fromSurface in app-start |
| `0x27eaf6e` → `0x25848e0` | gameLoadedCallback notifier → thunk (decision-013 gate callback) |
| `0x72a3020` | onAppShellReloadNeeded slot (no native reader — dead) |
| `0x2ebf950` | nativeScheduleOnFirstFrame (app-start plumbing) |

## 6. Observations / inferences / unknowns

- **Observed:** all RVAs and bytes in §§1–2; GOT relocations; the synthetic
  Route-B context; `g_phase3_done` set on first swap; the `extra`→`ctx+0x10`
  store and the `0x649cf9a` pbuffer deref.
- **Inferred (high confidence):** Route A fault cause = `extra=1` used as a
  pbuffer pointer; passing 0 reaches the window path. Verify by E3 A/B.
- **Unknown / deferred:** the exact guest frame-loop call site of
  `call [vtable+0x18]` (indirect dispatch; discover via E1/E4 return-address
  capture); whether the synthetic ANativeWindow is accepted by the native EGL
  backend once `eglCreateWindowSurface` substitution is in place (E3); whether
  any genuine gate callback can fire without seeded state (out of C4 scope —
  owned by the JNI/shell workers). No success is claimed.
