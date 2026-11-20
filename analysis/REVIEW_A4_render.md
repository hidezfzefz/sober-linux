# REVIEW A4 — Native window / render stack audit

Auditor: **A4** (full-project review). Scope: **this file only** — analysis, no
`src/`, `CMakeLists.txt`, `tests/`, gate or engine edits. Strict marker
**WITHHELD**: nothing in this report asserts or promotes the `app shell rendered`
marker. All RVA/GOT facts below are from the single frozen AppImage object
`/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so`, BuildID
`204ad049599c96afb75b8a21d5051063e821d6c9`, SHA-256
`3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335`
(AppImage 2.740.931); no Flatpak `791c3f9e…` address is used. Target-derived
strings are untrusted DATA.

---

## 0. Bottom line

- **"A native frame rendered" — PROVEN.** Host-native `src/render/native_render.c`
  opens a real X11 window, creates an EGL/GLES2 context, draws a deterministic
  clear pattern, and swaps it. The `first frame rendered` marker, a visible
  `SoberNativeRenderPhase3` window (xdotool), `frames swapped: 1`, and
  `OpenGL ES 3.2 Mesa 26.2.2` are recorded on every clean default run.
- **"A guest render entry point ran" — PROVEN (narrow).** Guest RVA `0x649d422`
  is invoked once with a synthetic `0x48`-byte context; a GOT hook at
  `0x722a1a8` counts exactly one swap through the guest's `eglSwapBuffers@plt`
  (`0x6d334d0`). This proves a real guest instruction sequence and a PLT
  traversal, **not** a guest-composited frame.
- **"The app-shell frame rendered" — NOT PROVEN (WITHHELD).** No guest-created
  EGL context, no bound `ANativeWindow`, no surface-lifecycle callback, no guest
  GLES draw, no vtable-dispatched frame loop, and no guest-provenanced
  `gameLoadedCallback` has ever been observed. The render half of the decision-013
  gate (`render_ok = sober_render_phase3_done()`) is itself set by our **own**
  native swap, so it is synthetic evidence; the default gate log is correctly
  `marker withheld: callback=0 render=1`.

**Domain % (this audit's scope, weighted): ≈ 55 % proven.** Breakdown below.

| Sub-domain | Weight | Proven | Evidence status |
| --- | --- | --- | --- |
| Native X11/EGL/GLES window + first native frame | 30 % | ~95 % | complete; runtime green, only a deterministic clear (not app content) |
| Guest swap `0x649d422` + GOT `0x722a1a8` counter hook | 20 % | ~85 % | invoked once, counted; no caller tagging so synthetic vs real swap cannot be told apart |
| GOT hooks / ANativeWindow binding shim | 20 % | ~45 % | swap + `eglCreateWindowSurface` hooks live; `ANativeWindow_fromSurface` GOT hook exists in code but is never installed; shim never enters the guest surface path |
| Surface lifecycle thunks + guest render device | 15 % | ~15 % | thunks mapped statically; none driven at runtime; Route A faults and is disabled; no render vtable installed in guest |
| "Guest-driven app-shell frame rendered" claim | 15 % | **0 %** | no guest draw, no frame loop, no callback provenance; marker WITHHELD |

The native *mechanism* is essentially done (~90 %); the specific *claim* the
scope is meant to prove ("the app-shell frame rendered") is at **0 %**.

---

## 1. What the code actually does (read-only audit)

### 1.1 Native backend — `src/render/native_render.c`
- `sober_render_probe()` (`:237`) dlopens X11/EGL/GLES2/Wayland; host `ldd` stays
  libc-only (verified by `verify-native.sh`, `ctest` 2/2).
- `sober_render_init()` (`:674`) with `SOBER_RENDER_X11` (forced in
  `guest_render.c:152`) → `sr_x11_window()` (`:454`) creates/maps a real X11
  window → `sr_egl_setup()` (`:487`) gets EGL_PLATFORM=x11, config, GLES2
  context, window surface, `eglMakeCurrent`, `eglSwapInterval`.
- `sober_render_swap()` (`:817`) calls `sr_draw_test_frame()` (`:798`):
  `glViewport` + two `glClear` (full clear + a scissored orange rectangle) +
  `glFinish`, then `eglSwapBuffers`, `frames++`, `first_frame=1`,
  `g_phase3_done=1`.
- `sober_render_anw_lock()` / `anw_unlock_and_post()` (`:895/:914`) implement a
  CPU-shape `ANativeWindow_Buffer` shim and upload it through a GLES2 texture +
  full-screen triangle strip, then swap. Exercised only by `render_probe.c`, not
  by the guest harness.

### 1.2 Guest harness — `src/linux/guest_render.c`
- Creates native window (640x480, "SoberNativeRenderPhase3"), make_current,
  `sober_render_swap()` → `first frame rendered` marker (`:208`).
- `call_guest_render_entries()` (`:58`):
  - installs `sober_render_hook_egl_create_window_surface()` into the compat
    shim's `sober_fn_eglCreateWindowSurface` pointer (`:65-69`);
  - writes `sober_render_hook_egl_swap_buffers()` over GOT `0x722a1a8` after
    `mprotect` (`:75-88`) → prints `installed guest-swap counter hook at GOT
    0x722a1a8`;
  - **Route B (default):** builds a stack `uint64_t ctx[9]` with
    `EGLDisplay = ctx+0x20`, `EGLSurface = ctx+0x28`, calls
    `base + 0x649d422` directly (`:93-97`), then reads
    `sober_render_guest_swap_count()` (`:98`) and requires `> 0` (`:104`);
  - **Route A (opt-in `SOBER_GUEST_ROUTE_A`):** calls guest `0x649c21e(anw, 1)`
    (`:112-126`) — documented to fault at `0x649cf9a`, disabled by default.

### 1.3 The swap wrapper at `0x649d422` (exact bytes)
```
649d422: 48 8b 47 20   mov  0x20(%rdi),%rax   ; EGLDisplay
649d426: 48 8b 77 28   mov  0x28(%rdi),%rsi   ; EGLSurface
649d42a: 48 89 c7      mov  %rax,%rdi
649d42d: e9 9e 60 89 00 jmp  6d334d0 <eglSwapBuffers@plt>
```
It is a **tail `jmp`, not a `call`**: the return address at `eglSwapBuffers`
entry is whoever called `0x649d422`. Today that is the host caller in
`guest_render.c:97`. In a real frame it would be guest code after
`call [vtable+0x18]` (render vtable `0x71912e8`, slot[3] `0x7191300 =
0x649d422`). The hook at `0x722a1a8` sees **both** cases and records only a
single undifferentiated `g_guest_swaps` counter. The wrapper issues **no GLES
draw**, creates/binds **no surface**, and touches only `+0x20/+0x28` of the
context. This is the central distinction: the counted swap re-presents **our
native frame**, not a guest-composited app-shell frame.

### 1.4 Hooks and binding — what is live vs dormant
| Symbol | Location | Installed on default path? | Proves |
| --- | --- | --- | --- |
| `eglSwapBuffers` counter | GOT `0x722a1a8` | **yes** (`guest_render.c:81`) | a swap traversed guest PLT — origin unknowable (no RA capture) |
| `eglCreateWindowSurface` substitution | compat shim fn-ptr | **yes** (`:67`) | guest *could* receive our X11 handle; not used on default path |
| `ANativeWindow_fromSurface` → our shim | GOT `0x72294a8`; hook body `native_render.c:975` | **NO** — `guest_render.c` never writes this GOT | the real app-shell surface-bind path is uninstrumented |
| `ANativeWindow_acquire/getWidth/getHeight` | GOT `0x72297c0/8/d0` | **NO** | never observed in a guest path |
| Guest surface lifecycle thunks `0x2a20850/0x2a208c0/0x2a20a00/0x2a20a30` | guest `.text` | **NO** | never driven; object `+0x140` window bind never observed |

The `SoberNativeWindow` struct is deliberately ABI-prefix compatible with the
frozen `struct ANativeWindow { unsigned refs; int32_t w,h; }`, and
`sr_hook_anw_from_surface()` (`:975`) returns it — but because the fromSurface
GOT is never hooked and there is no Java `Surface`, no guest code ever receives
it. The surface-binding capability exists **in code only**.

---

## 2. "A native frame rendered" vs "the app-shell frame rendered"

| Question | Status | Concrete proof / counter-evidence |
| --- | --- | --- |
| Did a real native X11 window exist and was a drawable swapped? | **YES** | `first frame rendered`, `frames swapped: 1`, xdotool-visible `SoberNativeRenderPhase3`, `backend=x11-egl-gles2`, Mesa GL ES 3.2 (D12 §2). |
| Was the drawing done by guest code? | **NO** | the only pixels drawn are `sr_draw_test_frame()` (native `glClear`) and, in `render_probe`, the CPU gradient path. The guest's ~70 `eglGetProcAddress`-loaded GLES pointers (`0x649c633…`, helpers `0x649d24d/0x649d285`) are never counted or exercised. |
| Did a guest render entry point execute? | **YES (once)** | `[render] guest call 0x649d422 swap(...) -> rc=1 | guest-driven swaps observed: 1` (D12 §2). |
| Did the guest build its own EGL context / install render vtable `0x71912e8`? | **NO** | Route A (`0x649c21e`) is opt-in and faults at `0x649cf9a` because `extra=1` is used as a bogus pbuffer pointer (C04 §1.4); default path uses a harness-built context. |
| Was a real `ANativeWindow` bound into the guest surface lifecycle? | **NO** | fromSurface GOT not hooked; lifecycle thunks not called; `app+0x140`/global `0x72a3308` never observed set by guest code. |
| Did a guest frame loop (`call [vtable+0x18]`) run with a guest return address? | **NO** | no return-address capture; the observed return address is the host caller of `0x649d422`. |
| Did the guest itself announce app-shell completion (`gameLoadedCallback` from notifier `0x27eaf6e`, RA `base+0x27eafb3`)? | **NO** | no notifier hit, no RA capture; default gate `callback=0`; only rejected `onAppBridgeNotification`/accessors or harness-triggered ALT thunk ever observed (C03/C09/verification 471). |

**Conclusion:** the render stack has proven *a* native frame and *a* guest
instruction; it has not proven *the app-shell frame*. The two are conflated only
because `g_phase3_done` is set by our own swap and is then used as the render leg
of the decision-013 gate.

---

## 3. Concrete missing evidence (to lift the marker — all currently absent)

1. **Caller provenance at GOT `0x722a1a8`.** Capture
   `__builtin_return_address(0)` in `sr_hook_egl_swap_buffers`; split counters
   `synthetic_swaps` (return address in host binary) vs `guest_loop_swaps`
   (return address in guest `.text`, `base ≤ ra < base+0x6d33b90`, after a
   `call [vtable+0x18]`). Today `g_guest_swaps==1` cannot distinguish the two.
2. **Guest draw correlation.** Count guest GLES calls loaded via
   `eglGetProcAddress` (`0x649c633…`, `0x649d24d`, `0x649d285`); require ≥1 guest
   draw between two guest-tagged swaps before calling the interval a frame.
   Current count is **0**.
3. **Install the `ANativeWindow_fromSurface` hook** into GOT `0x72294a8`
   (body already at `native_render.c:975`), then drive
   `onSurfaceCreatedNative 0x2a20850` / `onSurfaceChangedNative 0x2a208c0` /
   `onSurfaceRedrawNeededNative 0x2a20a00` / `onSurfaceDestroyedNative 0x2a20a30`
   and observe `+0x140` window binding and callback `[+0x88]/[+0x98]` hit counts.
4. **A working guest render device:** call `0x649c21e(window, pbuffer=0)` (fix the
   `extra=1` pbuffer bug), confirm no `0x649cf9a` fault, `eglCreateWindowSurface`
   reaching GOT `0x722a178`, and render vtable `0x71912e8` installed at `ctx+0`
   with EGLDisplay/EGLSurface at `+0x20/+0x28`. Route A is currently disabled.
5. **Suppress or tag the synthetic Route-B call** in any evidence run so
   `guest_loop_swaps` is unambiguous.
6. **Guest-provenanced completion callback** (Leg C): a hit at notifier
   `0x27eaf6e` with `RSI=placeId≠0`, cached class `0x72a2f48`/method `0x72a2fc0`
   non-NULL, thunk `0x25848e0` entered with return address `base+0x27eafb3`, and
   no `SOBER_ALT_CALLBACK_PATH` / seed knobs — none of which is captured today
   (`sr_dispatch_call` records only name/count).
7. **Honest gate provenance for `render_ok`:** the render leg must not be
   satisfied by our own test-frame swap; it must be the guest-tagged swap from
   items 1–2 paired with item 6 under decision 013 with **zero** synthetic state.
8. **Close withhold-chain coverage gaps:** 13 guest-mutating opt-ins (e.g.
   `SOBER_SEED_JNI_SINGLETON`, `SOBER_HELPER_REPAIR_V2`, `SOBER_REAL_APPSTATE_FLAG`)
   are absent from the `seeded_state` chain (verification 471 §3b); a future real
   callback combined with one would emit a false-green with no withhold line.
   This is a robustness gap, not current proof.

Per C04 §4 / C09, even after 1–7 the marker must remain **WITHHELD** while these
are opt-in evidence modes; they produce diagnostic lines only. A genuine,
all-knobs-off guest frame + callback has never been observed.

---

## 4. Runtime evidence consulted (all default path)
- `tests/D12-final-invariants.md §2`: 3/3 clean `first-frame.sh` PASS; render log
  with `guest call 0x649d422 … guest-driven swaps observed: 1`; zero
  `app shell rendered`.
- `tests/verification-report-471.md §1-2`: `first frame rendered` present,
  `app shell rendered` absent; gate `callback=0 render=1`; ALT thunk is the only
  marker emitter and is explicitly labelled (default-OFF, HONESTY NOTICE).
- `analysis/C04-render-integration.md`, `analysis/C03-appshell-frame-evidence.md`,
  `analysis/C09-marker-evidence.md`, `analysis/render/render-map.md`: static RVA
  and evidence-standard basis; no success claim.

## 5. Honesty limits
Static source review + existing run logs only; no new runtime, REA, or debugger
capture was performed for this audit. "Proven" above means *evidence exists in
the cited clean default runs*, not that the app-shell frame was reached. The
marker remains **WITHHELD**; this report does not weaken decision 013.
