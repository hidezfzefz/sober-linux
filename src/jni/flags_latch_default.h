/*
 * flags_latch_default.h - DEFAULT-ON flags-loaded latch module (task #489).
 *
 * Sets the guest's "flags loaded" byte 0x7cdd149 ONLY by replaying genuine
 * shipped guest code.  No host byte write, no fake vtable, no fabricated
 * callback, no decision-013 marker.  Engine frozen: BuildID 204ad049…,
 * SHA 3e498d6494…; every RVA below is from that object.
 *
 * WHY: RBXCRASH "Can't initialize the TaskScheduler before flags have been
 * loaded" (int3 at 0x6caef67) is gated by
 *     23862b0: cmpb $0,0x7cdd149 ; je <reporter>
 * The only movb $1,0x7cdd149 writers in the frozen engine are:
 *   0x2eb7520  nativeInitClientSettings 0x2359277 -> 0x2eb7155
 *              (skipped when parse-ctx byte 0x7697d08 == 1, our default state)
 *   0x2ec9fae  crashpad writer 0x2ec9eed (reached via nativeRunCrashpadHandler
 *              0x2eca720 or CrashpadHandlerMain 0x2ec9c30); writes the latch
 *              but its tail launches the crashpad fatal path.
 *   0x56d31a3 / 0x56d3386 / 0x56d6a4b / 0x56d6b80  (crash-dispatch family).
 * A route that sets the latch and returns cleanly is still being established
 * (worker-1 #487 / analysis A15).  Every attempted route is gated by an env
 * knob and forces the marker withhold while active.
 */
#ifndef SOBER_FLAGS_LATCH_DEFAULT_H
#define SOBER_FLAGS_LATCH_DEFAULT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__GNUC__) || defined(__clang__)
#  define SOBER_FLAGS_LATCH_API __attribute__((visibility("default")))
#else
#  define SOBER_FLAGS_LATCH_API
#endif

#define SOBER_FLAGS_LATCH_DEFAULT_ENV   "SOBER_FLAGS_LATCH_DEFAULT"
#define SOBER_FLAGS_LATCH_ROUTE_ENV     "SOBER_FLAGS_LATCH_ROUTE"
/* New (task #499): comma-separated argv strings for route=crashpad-valid.
 * These are genuine JNI String[] entries handed to nativeRunCrashpadHandler;
 * the guest converts them to the argv it passes to crashpad::HandlerMain. */
#define SOBER_FLAGS_LATCH_ARGV_ENV      "SOBER_FLAGS_LATCH_ARGV"

/* Frozen RVAs. */
#define SOBER_FLAGS_LATCH_RVA_BYTE       0x7cdd149u  /* the latch byte        */
#define SOBER_FLAGS_LATCH_RVA_COMPANION  0x7cdd148u  /* ShadowFValues latch   */
#define SOBER_FLAGS_LATCH_RVA_SHADOW     0x7cdd0d8u  /* ShadowFValuesEnabled  */
#define SOBER_FLAGS_LATCH_RVA_INIT_CS    0x2359277u  /* nativeInitClientSettings*/
#define SOBER_FLAGS_LATCH_RVA_CRASHPAD_JNI 0x2eca720u/* nativeRunCrashpadHandler*/
#define SOBER_FLAGS_LATCH_RVA_CRASHPAD_MAIN 0x2ec9c30u/* CrashpadHandlerMain   */
#define SOBER_FLAGS_LATCH_RVA_PARSE_CTX  0x7697d08u  /* parse-ctx gate byte   */
#define SOBER_FLAGS_LATCH_RVA_PREDICATE  0x23862b0u  /* TaskScheduler guard   */

/* Result codes. */
#define SOBER_FLAGS_LATCH_OK         0   /* latch is 1                        */
#define SOBER_FLAGS_LATCH_DISABLED   1   /* kill switch SOBER_*_DEFAULT=0     */
#define SOBER_FLAGS_LATCH_ERR       (-1)  /* bad argument                      */
#define SOBER_FLAGS_LATCH_ZERO      (-2)  /* guest ran, latch still 0          */

/* Default ON (kill switch SOBER_FLAGS_LATCH_DEFAULT=0 makes install() inert).
 * Lead ruling: the DEFAULT ROUTE is "none" -- install() is active but makes
 * NO guest call and should_withhold() stays 0 until a real route is chosen
 * via SOBER_FLAGS_LATCH_ROUTE=client-settings|crashpad-jni|crashpad-valid|
 * crashpad-main|narrow.
 * The crashpad routes set the latch through guest code.  #499 refined the
 * "fatal tail": the stdio abort is crashpad::HandlerMain's own Usage fprintf
 * on invalid argv; route=crashpad-valid supplies genuine crashpad argv via a
 * real JNI String[], which avoids that abort but still segfaults later inside
 * crashpad handler setup.  No route returns cleanly yet; default stays none.
 * `jni` is the SoberJni* (void* avoids a header dependency);
 * `jni_env`/`jclass` are the real JNI handles for the JNI-export routes. */
SOBER_FLAGS_LATCH_API int sober_flags_latch_default_install(
    void *guest_base, void *jni_env, void *jclass, void *jni);

/* 1 whenever any knob/seed of this module is active: the lead ORs this into
 * the decision-013 seeded_state chain so no such run can emit the marker. */
SOBER_FLAGS_LATCH_API int sober_flags_latch_default_should_withhold(void);

/* Read-only: current value of guest byte 0x7cdd149 (0 if install() not run). */
SOBER_FLAGS_LATCH_API unsigned sober_flags_latch_default_latch(void);

SOBER_FLAGS_LATCH_API const char *sober_flags_latch_default_env_vars(void);

/* 1 unless SOBER_FLAGS_LATCH_DEFAULT=0/off/no/false. */
SOBER_FLAGS_LATCH_API int sober_flags_latch_default_enabled(void);

#ifdef __cplusplus
}
#endif
#endif /* SOBER_FLAGS_LATCH_DEFAULT_H */
