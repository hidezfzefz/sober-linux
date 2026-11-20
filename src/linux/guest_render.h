/*
 * guest_render.h - Phase 3 render phase for the native guest-init harness.
 *
 * Runs AFTER all 3618 guest constructors have completed ("guest init
 * complete"). Creates a native Linux window + EGL/GLES context through
 * worker-2's runtime-dlopen backend, installs the ANativeWindow/EGL hooks the
 * guest needs, drives the guest render entry points published by worker-1's
 * render-map, and emits the deterministic "first frame rendered" signal.
 *
 * Owned by worker-3 (task #380).
 */
#ifndef SOBER_GUEST_RENDER_H
#define SOBER_GUEST_RENDER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Returns 0 on success (first frame swapped), negative on failure. */
int guest_render_run(uintptr_t guest_base);

#ifdef __cplusplus
}
#endif
#endif /* SOBER_GUEST_RENDER_H */
