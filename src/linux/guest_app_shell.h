/*
 * guest_app_shell.h - Phase 4 JNI bridge + app-shell startup phase.
 *
 * Runs AFTER the phase-3 window/render phase. Initialises worker-2's native
 * JNI bridge, calls the guest's JNI_OnLoad to satisfy cached callback lookups,
 * drives the app-shell startup RVAs from worker-1's jni-map.md, and emits the
 * deterministic "app shell rendered" marker only after a guest->Java app-shell
 * callback has been observed.
 *
 * Owned by worker-3 (task #385).
 */
#ifndef SOBER_GUEST_APP_SHELL_H
#define SOBER_GUEST_APP_SHELL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Returns 0 on success, negative on failure. */
int guest_app_shell_run(uintptr_t guest_base);

#ifdef __cplusplus
}
#endif
#endif /* SOBER_GUEST_APP_SHELL_H */
