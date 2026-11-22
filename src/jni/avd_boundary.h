/*
 * avd_boundary.h - replay of the authoritative AVD-captured JNI boundary.
 *
 * Research oracle only. This module reads the exact arg0 bytes captured by the
 * Frida session w11-hook-settings on the rooted x86_64 AVD (emulator-5556),
 * verifies their SHA-256 before use, and reports a provenance class that is
 * DISTINCT from the decision-013 seeded_state chain. It never writes guest
 * memory, never patches a gate, and never emits a marker.
 */
#ifndef SOBER_JNI_AVD_BOUNDARY_H
#define SOBER_JNI_AVD_BOUNDARY_H

#include <stddef.h>

#define SOBER_AVD_BOUNDARY_ENV     "SOBER_AVD_BOUNDARY_REPLAY"
#define SOBER_AVD_BOUNDARY_PATH    "/tmp/opencode/w11-arg0.json"
/* Diagnostic bisect only. Never counts toward a checkpoint rung. */
#define SOBER_AVD_BOUNDARY_UNVERIFIED "SOBER_AVD_BOUNDARY_UNVERIFIED"
#define SOBER_AVD_BOUNDARY_PATH_ENV   "SOBER_AVD_BOUNDARY_PATH"
#define SOBER_AVD_BOUNDARY_SHA256  "8215519ff31bb79e66baa65baf9b09fe46ea172f64df33d2399db315fd27634b"
#define SOBER_AVD_BOUNDARY_SESSION "AVD emulator-5556 / frida 17.19.0 / w11-hook-settings / 2026-11-22"
#define SOBER_AVD_BOUNDARY_CLASS   "AVD-captured boundary input"
#define SOBER_AVD_BOUNDARY_ARG2    "GoogleAndroidApp"

#ifdef __cplusplus
extern "C" {
#endif

/* Non-zero only when the explicit kill-switch env is exactly "1". */
int sober_avd_boundary_enabled(void);

/* Read the captured file, verify its SHA-256, and return an owned buffer.
 * Returns 0 on success; *buf must be freed by the caller. No guest access. */
int sober_avd_boundary_load(char **buf, size_t *len);

/* One-line provenance record: class, session, file, hash, arg count. */
void sober_avd_boundary_provenance(char *buf, size_t cap);

/* Fail-closed: this provenance class withholds the marker, separately from
 * seeded_state, because the input is host-supplied oracle data. */
int sober_avd_boundary_should_withhold(void);

#ifdef __cplusplus
}
#endif
#endif
