# Decision 011 — Phase 3 complete: native window + first frame

Status: accepted (lead). Team `TEAM-CEXX-GSEL-3UA5` finished.

## Done condition met

From the working phase-2 state (all 3618 guest constructors, `guest init
complete`), the extended harness now:

- creates a **visible native X11 window** (`SoberNativeRenderPhase3`, verified
  on the display), using worker-2's `src/render/` backend with native EGL 1.5 /
  GLES 3.2 (Mesa llvmpipe), no Android runtime;
- calls a **real guest post-init entry point** at AppImage-build RVA
  `0x649d422` (guest swap wrapper, from worker-1's
  `analysis/render/render-map.md`) with a 0x48-byte context containing our
  native `EGLDisplay` / `EGLSurface`, logging the call and `rc=1` (`EGL_TRUE`);
- gates the deterministic `first frame rendered` marker on that guest call —
  `tests/first-frame.sh` asserts the guest-call log line, so the standalone
  native frame alone can no longer satisfy the milestone;
- leaves the guest's own `eglSwapBuffers@plt` (`0x6d334d0`) reaching worker-2's
  native EGL shim, counted by `sober_render_guest_swap_count()`.

Route A (guest `create_context` `0x649c21e` with an ANativeWindow) faults in
guest EGL config helper `0x649cf9a`; it is kept opt-in via
`SOBER_GUEST_ROUTE_A`. Route B is the default.

## Verification (lead, after all edits)

| Check | Result |
| --- | --- |
| `cmake --build build` | PASS |
| `bash tests/verify-native.sh` | PASS |
| `ctest` | 2/2 PASS |
| `build/guest-init-host` | exit 0, `guest init complete` |
| `bash tests/first-frame.sh` | exit 0, `first frame rendered`, guest RVA call logged, visible window confirmed |
| `ldd build/sober-native` | libc + ld-linux only |
| on-disk engine sha256 | unchanged (`3e498d64...`) |

## Still out of scope

Full JNI replacement, login/app shell, `AMediaCodec`/`AImageReader` stubs.
The native client renders a first frame but is not yet a playable Roblox client.
