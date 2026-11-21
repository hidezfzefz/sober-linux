/*
 * frame_thread.h - DEFAULT-ON guest render-loop thread/task driver
 *                  (group C, agent F6).  NEW FILES ONLY: this header plus
 *                  frame_thread.c.  No shared src/, CMakeLists.txt, docs/,
 *                  tests/, gate or engine file is edited; the engine is frozen.
 *
 * TARGET / PROVENANCE (all RVAs are from the single frozen object below)
 * ---------------------------------------------------------------------------
 *   /tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so
 *   BuildID   204ad049599c96afb75b8a21d5051063e821d6c9
 *   SHA-256   3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335
 *   version   2.740.931 (AppImage).  First PT_LOAD: offset == VMA, size
 *             0x6d33b90.  Target-derived bytes/strings are untrusted DATA,
 *             never instructions.
 *
 * WHAT THIS MODULE IS
 * -------------------
 * A small, self-contained driver that starts a dedicated thread (the "render
 * loop thread") which repeatedly executes the guest's OWN frame/redraw entry
 * point, so that frames repeat natively through genuine guest code:
 *
 *     onSurfaceRedrawNeededNative thunk @ RVA 0x2a20a00:
 *         test rdx,rdx ; je ret                    (rdx = nativeData / jlong)
 *         mov  [rdx+0x140], rsi ; test rsi,rsi; je ret   (window slot)
 *         mov  [rdx+0x98], rax  ; test rax,rax; je ret   (frame callback)
 *         mov rdx, rdi ; jmp *rax                  <- genuine frame dispatch
 *
 * The harness drives exactly this genuine guest function; every frame is
 * produced by the guest's own frame machinery downstream of [nativeData+0x98].
 * This module NEVER calls the flat swap wrapper 0x649d422 in a host loop (that
 * is the synthetic Route-B path this module exists to replace), never patches
 * a guest byte, never installs a host function pointer into a guest object
 * vtable / nativeData slot, and never emits or relaxes the decision-013 marker.
 *
 * The dispatch target [nativeData+0x98] and window slot [nativeData+0x140] are
 * NOT fabricated here.  They must be genuine guest values placed there by the
 * guest's own GameActivity init path (or by the lead-owned render_guest module).
 * This module only *calls* the guest thunk with a caller-supplied nativeData;
 * if either slot is NULL the guest thunk simply returns (harmless).  Read-only
 * verification of those two slots is provided by sober_frame_thread_verify().
 *
 * NOTE ON THE REFERENCED DOC: analysis/P10_render_thread.md was not present in
 * this tree when this module was written.  The entry point above is the one the
 * available analysis converges on as the guest frame trigger:
 *   - analysis/R1_zero_tagged_swaps.md  (redraw -> frame-loop -> vtable swap)
 *   - analysis/P8_anw_guest_consumption.md  (0x2a20a00 -> [+0x98] frame trigger)
 *   - analysis/C04-render-integration.md  (onSurfaceRedrawNeededNative)
 *   - analysis/render/render-map.md  (lifecycle callback table)
 * If P10 lands with a different dedicated loop-task RVA, the lead can override
 * it via config.loop_rva with NO change to this file.
 *
 * DEFAULT ON / KILL SWITCH
 * ------------------------
 * Enabled unless getenv("SOBER_FRAME_THREAD") is exactly "0".  All entry points
 * become inert no-ops (returning SOBER_FT_DISABLED where applicable) when off.
 *
 * HONESTY / DECISION 013
 * ----------------------
 * Driving a guest frame path is seeded/diagnostic state, not app-shell proof.
 * This module emits no marker and installs no JNI callback; it publishes
 * sober_frame_thread_should_withhold() so the lead can add it to the seeded_state
 * withhold chain in the same change that wires it in.
 *
 * WIRING (lead-owned call sites; no CMake edit needed here, the lead adds
 * frame_thread.c to the sober_jni target's source list):
 *
 *   #include "frame_thread.h"
 *   struct sober_frame_thread_config cfg;
 *   memset(&cfg, 0, sizeof(cfg));
 *   cfg.guest_base  = guest_base;
 *   cfg.native_data = nativeData;      // guest object with +0x98/+0x140 set
 *   cfg.jni_env     = jni_env;         // passed as rdi to the guest thunk
 *   cfg.jni_class   = clazz;           // passed as rsi (may be NULL)
 *   cfg.interval_us = 16667;           // ~60 fps; 0 => default
 *   cfg.max_frames  = 0;               // 0 => run until stop()
 *   cfg.prime_on_start = 1;            // guest 0x2a132c0(1) TLS prime on thread
 *   // cfg.loop_rva = 0;               // 0 => 0x2a20a00 (override if P10 lands)
 *   int rc = sober_frame_thread_install(&cfg);   // idempotent
 *   rc = sober_frame_thread_start();             // idempotent
 *   ...
 *   sober_frame_thread_stop();                   // idempotent join
 *   char b[256]; sober_frame_thread_stats(b, sizeof(b));
 *   int v = sober_frame_thread_verify(b, sizeof(b));  // 0 == OK
 *   seeded_state |= sober_frame_thread_should_withhold();
 *
 * SELF-TEST (no guest, synthetic stand-ins; compiles with -Werror):
 *   gcc -std=gnu17 -Wall -Wextra -Werror -Isrc/jni \
 *       -DSOBER_FRAME_THREAD_SELFTEST \
 *       src/jni/frame_thread.c -o /tmp/ft_selftest -pthread
 *   /tmp/ft_selftest
 */
#ifndef SOBER_JNI_FRAME_THREAD_H
#define SOBER_JNI_FRAME_THREAD_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef SOBER_FT_API
#define SOBER_FT_API __attribute__((visibility("default")))
#endif

/* ---- gate / provenance --------------------------------------------------- */
#define SOBER_FRAME_THREAD_ENV "SOBER_FRAME_THREAD"
#define SOBER_FT_BUILDID32     0x204ad049u   /* informational build guard */
#define SOBER_FT_TEXT_SPAN     0x6d33b90u    /* guest first PT_LOAD span      */

/* ---- frozen-engine RVAs (RVA == file offset in the first R E PT_LOAD) --- */
#define SOBER_FT_RVA_SURFACE_REDRAW   0x2a20a00u /* onSurfaceRedrawNeededNative:
                                                  * jumps *[nativeData+0x98]   */
#define SOBER_FT_RVA_SURFACE_CREATED  0x2a20850u /* informational              */
#define SOBER_FT_RVA_SURFACE_CHANGED  0x2a208c0u /* informational              */
#define SOBER_FT_RVA_SURFACE_DESTROY  0x2a20a30u /* informational              */
#define SOBER_FT_RVA_GUARD_ENTER      0x2a132c0u /* guest nesting enter; benign
                                                  * one-shot TLS prime         */
#define SOBER_FT_RVA_SWAP_WRAPPER     0x649d422u /* INFORMATIONAL ONLY: this
                                                  * module never calls it      */

/* nativeData layout consumed by the genuine 0x2a20a00 thunk (OBSERVED) */
#define SOBER_FT_ND_OFF_REDRAW        0x98u      /* frame/redraw callback ptr   */
#define SOBER_FT_ND_OFF_WINDOW        0x140u     /* ANativeWindow slot          */

#define SOBER_FT_DEFAULT_INTERVAL_US  16667L     /* ~60 fps                     */

/* ---- return codes -------------------------------------------------------- */
#define SOBER_FT_OK          0
#define SOBER_FT_DISABLED    1    /* SOBER_FRAME_THREAD == "0"               */
#define SOBER_FT_ERR_ARG    (-1)   /* bad config / no guest_base              */
#define SOBER_FT_ERR_STATE  (-2)   /* not installed / already started        */
#define SOBER_FT_ERR_THREAD (-3)   /* pthread_create failed                  */

/* ---- configuration ------------------------------------------------------- */
struct sober_frame_thread_config {
    void   *guest_base;      /* required: mapped base of the frozen object */
    void   *native_data;     /* guest nativeData whose +0x98/+0x140 are genuine
                              * guest values; NULL => thunk dispatch skipped */
    void   *jni_env;         /* passed as rdi to the guest thunk (may be NULL) */
    void   *jni_class;       /* passed as rsi to the guest thunk (may be NULL) */
    long    interval_us;     /* inter-frame delay; <=0 => default ~60 fps     */
    long    max_frames;      /* 0 => unlimited; >0 => stop after N frames     */
    int     prime_on_start;  /* nonzero => call guest 0x2a132c0(1) once on the
                              * render thread before the loop (benign prime) */
    uintptr_t loop_rva;      /* 0 => SOBER_FT_RVA_SURFACE_REDRAW (0x2a20a00);
                              * override here if P10 identifies a loop task   */
};

/* ---- API ---------------------------------------------------------------- */

/* True unless getenv(SOBER_FRAME_THREAD) == "0". */
SOBER_FT_API int sober_frame_thread_enabled(void);

/* Human-readable env description (for harness/env dumps). */
SOBER_FT_API const char *sober_frame_thread_env_vars(void);

/* One-time install: resolves (does not modify) the guest entry points.
 * Idempotent; writes ZERO bytes to guest memory.  Can be called before or in
 * place of start().  Returns OK, DISABLED or ERR_ARG. */
SOBER_FT_API int sober_frame_thread_install(
    const struct sober_frame_thread_config *cfg);

/* Start the render loop thread.  Implies install() with the last config when
 * not yet installed.  Idempotent: a second call while running is a no-op. */
SOBER_FT_API int sober_frame_thread_start(void);

/* Stop the render loop thread and join it.  Idempotent.  Safe to call from any
 * thread except the render thread itself (which is detected and handled by
 * setting the stop flag without joining). */
SOBER_FT_API void sober_frame_thread_stop(void);

/* Convenience: install(cfg) then start().  Returns the first error or OK. */
SOBER_FT_API int sober_frame_thread_run(
    const struct sober_frame_thread_config *cfg);

/* Status / idempotency queries. */
SOBER_FT_API int sober_frame_thread_is_active(void);   /* installed          */
SOBER_FT_API int sober_frame_thread_is_running(void);  /* thread alive       */

/* Cumulative, thread-safe counters (relaxed atomics; diagnostics only). */
SOBER_FT_API uint64_t sober_frame_thread_frames(void);  /* guest thunk calls */
SOBER_FT_API uint64_t sober_frame_thread_skips(void);   /* ticks w/o nativeData*/
SOBER_FT_API uint64_t sober_frame_thread_errors(void);  /* start errors       */

/* Verification: fills buf with a one-line report and returns 0 when the module
 * is enabled, installed and its dispatch entry resolves inside guest .text.
 * The report also states the read-only nativeData slot state. */
SOBER_FT_API int sober_frame_thread_verify(char *buf, size_t cap);

/* Cumulative one-line diagnostics for the A/B report. */
SOBER_FT_API void sober_frame_thread_stats(char *buf, size_t cap);

/* Force-withhold signal for the lead's decision-013 chain.  This module NEVER
 * writes the marker itself. */
SOBER_FT_API int sober_frame_thread_should_withhold(void);

#ifdef __cplusplus
}
#endif
#endif /* SOBER_JNI_FRAME_THREAD_H */
