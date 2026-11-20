# src/render — native Linux window + EGL/GLES2 backend

Worker-2, Phase 3 task #379. Everything in this directory is owned by worker-2.

## Files

- `native_render.h` — public, clean C API. **This is the integration contract.**
- `native_render.c` — implementation. Pure native code; no translation layer.
- `render_probe.c` — standalone self-test / headless capability probe.
- `CMakeLists.txt` — `add_library(sober_render STATIC ...)` + optional
  `render_probe` executable. No link-time EGL/X11/Wayland dependencies.

## Design

- **Runtime loading only.** `libX11.so.6`, `libEGL.so.1`, `libGLESv2.so.2`,
  `libwayland-client.so.0` and `libwayland-egl.so.1` are `dlopen()`d lazily.
  There is no ELF DT_NEEDED for any graphics library, so the harness keeps its
  clean `ldd` (libc/ld-linux only).
- **Backends:** X11 (full), Wayland (wl_surface via exported
  `wl_*_interface` descriptors + `wl_egl_window`, best-effort with XWayland
  fallback), and surfaceless EGL (Pbuffer, for headless self-test).
- **Environment hardening:** Mesa reads `EGL_PLATFORM` at init and can override
  an explicit platform request; the backend pins `EGL_PLATFORM` to `x11` /
  `wayland` / `surfaceless` to match the selected backend. This avoids the
  `wl_proxy_create_wrapper` crash seen when `EGL_PLATFORM=wayland` is inherited.
- **ANativeWindow shim:** `SoberNativeWindow`'s first three fields
  (`unsigned refs; int32_t width; int32_t height;`) match the frozen
  `src/compat` `struct ANativeWindow`, so the existing
  `ANativeWindow_acquire/release/getWidth/getHeight` stay ABI-compatible.
  The rest of the struct carries the EGL/GL state, an RGBA shadow buffer and a
  magic word. `sober_render_anw_lock()` / `sober_render_anw_unlock_and_post()`
  are a layout-compatible `ANativeWindow_Buffer` path: unlock uploads the CPU
  pixels through a minimal GLES2 textured quad and swaps.
- **Guest PLT hooks:** `sober_render_hook_anative_window_from_surface()` and
  `sober_render_hook_egl_create_window_surface()` return function pointers
  with the exact guest prototypes, to be installed over the corresponding
  JUMP_SLOTs by the harness. With an active render context the EGL hook
  substitutes the real native window handle (X11 `Window` or `wl_egl_window`)
  for the guest's ANativeWindow pointer.
  `sober_render_hook_egl_swap_buffers()` returns an `EGLBoolean (*)(EGLDisplay,
  EGLSurface)`-compatible pointer: it forwards the swap to the native driver
  unchanged and increments `sober_render_guest_swap_count()`. Because our own
  `sober_render_swap()` does not go through this hook, a non-zero count proves
  the guest called its own `eglSwapBuffers@plt` (guest RVA `0x649d422` → PLT
  `0x6d334d0`) — the deterministic gate for the `first frame rendered` marker
  required by the lead's scope correction.

## Return codes and thread safety

- All `int` entry points return `SOBER_RENDER_OK` (0) or `SOBER_RENDER_ERR` (-1).
- `sober_render_error(r)` returns a human-readable static string after failure.
- `sober_render_init()` sets the active context (see `sober_render_set_active`);
  the active pointer is mutex-protected. One window per harness is assumed.

## Self-test

```sh
# build standalone (no CMake needed)
gcc -std=gnu17 -Wall -Wextra -Isrc/render \
    src/render/native_render.c src/render/render_probe.c \
    -o /tmp/render_probe -ldl -lpthread

/tmp/render_probe --probe                       # headless, no display touched
/tmp/render_probe --backend x11                 # visible X11 window + first frame
env -u DISPLAY -u WAYLAND_DISPLAY \
    /tmp/render_probe --headless                # surfaceless EGL Pbuffer
```

Verified here: `--probe` reports
`backend=x11-egl-gles2 x11=1 wayland=1 egl=1 gles2=1 surfaceless=1`;
`--backend x11` reports `first frame rendered` with `OpenGL ES 3.2 Mesa`;
`--headless` reports `first frame rendered` on a surfaceless Pbuffer.
