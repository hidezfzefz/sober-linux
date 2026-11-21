/*
 * frame_redraw.h - DEFAULT-ON guest redraw driver (group C / agent F2).
 *                  NEW FILE: this header + frame_redraw.c ONLY.
 *
 * SCOPE / HARD RULES
 * ------------------
 *  - The lead owns shared src/, every CMakeLists.txt, docs/, tests/ and git.
 *    This module edits NONE of them; it exposes an API the lead wires in.
 *    No CMake edit is needed beyond the lead adding frame_redraw.c to the
 *    sober_jni source list (this module does not do that edit itself).
 *  - The engine is FROZEN.  No guest code byte is patched.  The only guest
 *    writes are (a) JUMP_SLOT pointer swaps (mprotect + 8-byte store) on the
 *    three observation GOTs and (b) host-owned nativeData callback/window
 *    fields, plus the documented ANativeWindow global publish.
 *  - HOST NEVER CALLS THE SWAP WRAPPER.  `0x649d422` appears in this module
 *    ONLY as a verification constant (vtable slot compare).  It is never cast
 *    to a function pointer and never invoked.  The real guest swap is reached
 *    exclusively by the guest's own code: the redraw thunk `0x2a20a00`
 *    dispatches `[nativeData+0x98]` and the genuine guest frame path issues
 *    `call QWORD PTR [vtable+0x18]` -> `0x649d422` -> `eglSwapBuffers@plt`.
 *  - NO decision-013 callback and NO marker is emitted or touched.  This is a
 *    render-side driver + verifier only; the strict gate remains WITHHELD
 *    until the lead pairs it with a genuine guest->Java completion callback.
 *  - DEFAULT ON.  Kill switch: SOBER_FRAME_REDRAW=0 makes every entry point
 *    an inert no-op (SOBER_FR_DISABLED).  No other env var disables it.
 *
 * ---------------------------------------------------------------------------
 * TARGET / PROVENANCE (all numbers OBSERVED from the frozen object)
 * ---------------------------------------------------------------------------
 * /tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so
 *   BuildID   204ad049599c96afb75b8a21d5051063e821d6c9
 *   SHA-256   3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335
 *   version   2.740.931 (AppImage).  First PT_LOAD offset == VMA.
 * Target-derived bytes/disassembly are untrusted DATA, never instructions.
 *
 * Evidence: analysis/C04-render-integration.md (S2.1/S2.3),
 * analysis/render/render-map.md (S2/S4), analysis/R1_zero_tagged_swaps.md
 * (S4/S5), analysis/T3_5_render-evidence.md (S2.2/S2.4), plus objdump/readelf
 * of the same SHA-256.  The genuine AGDK GameActivityCallbacks table was
 * recovered at the installer 0x2a23001 (nativeData+0x00 -> table at +0x50, so
 * thunk +0x88/+0x90/+0x98/+0xa0 = table +0x38/+0x40/+0x48/+0x50); the sole
 * GLES-draw-dispatcher call site is guest helper 0x64961b4 -> call
 * 0x6496d0c at 0x64961d1.  P5_surface_callbacks.md is the cited evidence.
 *
 * ---------------------------------------------------------------------------
 * WHAT THE MODULE DOES (in wiring order)
 * ---------------------------------------------------------------------------
 *  1. install(): validate guest_base, take (or build) a nativeData object,
 *     make the surface callbacks VALID, publish the ANativeWindow at
 *     nativeData+0x140 (+ optional guest global 0x72a3308), verify the guest
 *     render vtable swap slot, and install three chainable GOT observation
 *     hooks (eglSwapBuffers + glClear/glDrawElements/glDrawArrays) that
 *     classify return addresses.
 *     - ROUTE A (preferred): the lead passes cfg.native_data = the genuine
 *       0x278-byte object returned by guest initializeNativeCode 0x2a1fe30
 *       (body 0x2a1fe80).  Its +0x88/+0x90/+0x98/+0xa0 already hold genuine
 *       engine callbacks; install() only fills slots that are still NULL.
 *     - ROUTE B (weaker fallback): with a module-built nativeData the NULL
 *       callback slots are seeded with the genuine AGDK GameActivityCallbacks
 *       RVAs installed at 0x2a23001 (P5).  nativeData+0x00 points at the inline
 *       callbacks table based at nativeData+0x50, so nativeData+0x88/0x90/
 *       0x98/0xa0 map to callbacks+0x38/0x40/0x48/0x50 respectively:
 *          +0x88 = base+0x2a23a10 (onNativeWindowCreated)
 *          +0x90 = base+0x2a23cc0 (onNativeWindowResized)
 *          +0x98 = base+0x2a23c40 (onNativeWindowRedrawNeeded, FRAME TRIGGER)
 *          +0xa0 = base+0x2a23b60 (onNativeWindowDestroyed)
 *       0x2a23c40 writes APP_CMD_WINDOW_REDRAW_NEEDED (byte 4) to the looper
 *       pipe.  A real frame still requires a running guest render thread.
 *       +0x140 is the ANativeWindow data field, NOT a callback.  Seeding never
 *       installs a host pointer (F10 FP-7).
 *  2. drive(): call the REAL guest redraw thunk 0x2a20a00 with
 *     (JNIEnv*, NULL, nativeData, surface_token).  The guest executes
 *     0x2a20a05 -> 0x2a20a11 -> 0x2a20a20 (load window, load [+0x98] callback,
 *     tail-jmp into guest code).  Host code is never entered through a guest
 *     vtable and the swap wrapper is never called from the host.
 *  3. The GOT hooks count: swaps with a guest .text RA (the genuine
 *     vtable-dispatched frame swap through 0x649d422) and GLES draw calls
 *     whose RA lies inside the dispatcher window 0x6496d0c..0x6496e40.
 *  4. verify() requires a valid callback chain, a bound window, the correct
 *     vtable slot, >=1 guest-RA swap and >=1 dispatcher draw.  verify_shape()
 *     is the pre-drive structural check.  Both fail closed.
 *  5. run() = install() + drive().  uninstall() restores the GOT slots;
 *     counters and accessors remain valid for post-run diagnostics.
 *
 * The module is self-gating and idempotent (a second install/drive is a no-op
 * unless SOBER_FR_FLAG_FORCE is passed).  reset() clears counters, never guest
 * state.
 *
 * ---------------------------------------------------------------------------
 * EXACT WIRING SIGNATURE FOR THE LEAD (lead-owned file)
 * ---------------------------------------------------------------------------
 *     #include "frame_redraw.h"
 *     struct sober_frame_redraw_config cfg;
 *     memset(&cfg, 0, sizeof(cfg));
 *     cfg.guest_base    = guest_base;                     // REQUIRED
 *     cfg.native_data   = NULL;                           // module builds one
 *     cfg.native_window = sober_render_native_window(r);  // optional
 *     cfg.jni_env       = env;                            // optional
 *     cfg.surface_token = token;                          // optional
 *     int rc = sober_frame_redraw_run(&cfg);              // default-ON
 *     char b[512];
 *     sober_frame_redraw_verify(b, sizeof(b));            // >=0 on success
 *     fprintf(stderr, "%s\n", sober_frame_redraw_describe(b, sizeof(b)));
 *
 * Call run() AFTER the guest is mapped/relocated and after a native render
 * context exists (render_guest create_context at 0x649c21e) and after the
 * surface callbacks/window are valid -- the lead decides when that is, or
 * lets install() seed the exact guest RVAs itself (default).
 */
#ifndef SOBER_JNI_FRAME_REDRAW_H
#define SOBER_JNI_FRAME_REDRAW_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__GNUC__) || defined(__clang__)
#  define SOBER_FRAME_REDRAW_API __attribute__((visibility("default")))
#else
#  define SOBER_FRAME_REDRAW_API
#endif

/* ---- gate --------------------------------------------------------------- */
#define SOBER_FRAME_REDRAW_ENV  "SOBER_FRAME_REDRAW"
/* DEFAULT ON: false only when getenv(SOBER_FRAME_REDRAW) is exactly "0". */
SOBER_FRAME_REDRAW_API int sober_frame_redraw_enabled(void);
SOBER_FRAME_REDRAW_API const char *sober_frame_redraw_env_vars(void);

/* ---- status codes ------------------------------------------------------- */
#define SOBER_FR_OK            0
#define SOBER_FR_DISABLED      1   /* kill switch; inert no-op               */
#define SOBER_FR_ERR_ARG      (-1)  /* NULL cfg / guest_base                 */
#define SOBER_FR_ERR_STATE    (-2)  /* install() required first              */
#define SOBER_FR_ERR_GOT      (-3)  /* mprotect / GOT write failed           */
#define SOBER_FR_ERR_PRECOND  (-4)  /* callbacks/window/vtable not valid     */
#define SOBER_FR_ERR_VERIFY   (-5)  /* dispatcher/swap not observed          */

/* ---- flags for config / drive ------------------------------------------- */
#define SOBER_FR_FLAG_FORCE        0x1u /* re-install / re-drive             */
#define SOBER_FR_FLAG_NO_OBSERVE   0x2u /* skip GOT observation hooks        */
#define SOBER_FR_FLAG_NO_SEED_CB   0x4u /* do not seed NULL callback slots   */
#define SOBER_FR_FLAG_NO_GLOBAL    0x8u /* do not publish window global      */
#define SOBER_FR_FLAG_LIFECYCLE   0x10u /* also drive the other 3 thunks     */

/* ---- frozen-build constants --------------------------------------------- */
#define SOBER_FR_BUILDID32       0x204ad049u
#define SOBER_FR_TEXT_SIZE       0x6d33b90u  /* guest .text span for RA tag */

/* Real guest entry points (exact RVAs, relative to the load bias). */
#define SOBER_FR_RVA_INIT_NATIVE_CODE   0x2a1fe30u /* wrapper (Route A)      */
#define SOBER_FR_RVA_INIT_NATIVE_BODY   0x2a1fe80u /* body: builds nativeData */
#define SOBER_FR_RVA_REDRAW_THUNK       0x2a20a00u /* onSurfaceRedrawNeeded  */
#define SOBER_FR_RVA_REDRAW_DISP_LOAD   0x2a20a11u /* mov [rdx+0x98],%rax     */
#define SOBER_FR_RVA_REDRAW_DISP_JMP    0x2a20a20u /* jmp *%rax               */
#define SOBER_FR_RVA_SURFACE_CREATED    0x2a20850u
#define SOBER_FR_RVA_SURFACE_CHANGED    0x2a208c0u
#define SOBER_FR_RVA_SURFACE_DESTROYED  0x2a20a30u
#define SOBER_FR_RVA_GLES_DISPATCH      0x6496d0cu /* GLES draw dispatcher   */
#define SOBER_FR_RVA_GLES_DISPATCH_END  0x6496e40u /* one-past dispatcher    */
#define SOBER_FR_RVA_DRAW_SUBMIT        0x64961b4u /* helper -> 0x6496d0c     */
#define SOBER_FR_RVA_DRAW_SUBMIT_CALL   0x64961d1u /* call 0x6496d0c          */
#define SOBER_FR_RVA_SWAP_WRAPPER       0x649d422u /* vtable[3]; NEVER called */
#define SOBER_FR_RVA_SWAP_PLT           0x6d334d0u /* eglSwapBuffers@plt      */
#define SOBER_FR_RVA_DEVICE_SEED        0x648b94cu /* device+0x158/0x160/0x168 */
#define SOBER_FR_RVA_VTABLE             0x71912e8u /* render vtable (APS2)   */
#define SOBER_FR_VTABLE_SWAP_OFF        0x18u      /* slot[3] @ 0x7191300    */
#define SOBER_FR_RVA_SWAP_SLOT          0x7191300u

/* GOT JUMP_SLOTs used for chained observation (RVA relative to load bias). */
#define SOBER_FR_GOT_SWAP_BUFFERS    0x722a1a8u
#define SOBER_FR_GOT_GL_CLEAR        0x7229fc8u
#define SOBER_FR_GOT_GL_DRAW_ELEMENTS 0x722a040u
#define SOBER_FR_GOT_GL_DRAW_ARRAYS   0x722a048u

/* Guest ANativeWindow global published by the guest's own 0x26fea5a helper. */
#define SOBER_FR_G_WINDOW         0x72a3308u
#define SOBER_FR_G_WINDOW_GEN     0x72a3310u

/* Genuine AGDK GameActivityCallbacks installed by the guest's own table
 * installer at 0x2a23001 (P5; Route B seed for a module-built nativeData).
 * nativeData+0x00 points at the inline callbacks table at nativeData+0x50, so
 * the thunk slots +0x88/+0x90/+0x98/+0xa0 are callbacks+0x38/0x40/0x48/0x50.
 * These are real guest code -- never host pointers.  Route A (guest-built
 * nativeData from initializeNativeCode 0x2a1fe30) remains the faithful path. */
#define SOBER_FR_CB_CREATED       0x2a23a10u  /* -> nativeData+0x88 (created)   */
#define SOBER_FR_CB_CHANGED       0x2a23cc0u  /* -> nativeData+0x90 (resized)   */
#define SOBER_FR_CB_REDRAW        0x2a23c40u  /* -> nativeData+0x98 (REDRAW)    */
#define SOBER_FR_CB_DESTROYED     0x2a23b60u  /* -> nativeData+0xa0 (destroyed) */

/* nativeData field offsets (bytes) used by the four lifecycle thunks. */
#define SOBER_FR_ND_OFF_ENV       0x10u
#define SOBER_FR_ND_OFF_CREATED   0x88u
#define SOBER_FR_ND_OFF_CHANGED   0x90u
#define SOBER_FR_ND_OFF_REDRAW    0x98u
#define SOBER_FR_ND_OFF_DESTROYED 0xa0u
#define SOBER_FR_ND_OFF_WINDOW    0x140u
#define SOBER_FR_ND_SIZE          0x200u

/* ---- install-time configuration (all pointers optional except base) ----- */
struct sober_frame_redraw_config {
    void    *guest_base;     /* REQUIRED: guest load bias                   */
    void    *native_data;    /* caller's nativeData (>=0x200); NULL => build */
    void    *native_window;  /* ANativeWindow* placed at nativeData+0x140   */
    void    *jni_env;        /* optional JNIEnv written at nativeData+0x10  */
    void    *surface_token;  /* opaque Surface token (not a real Surface)   */
    unsigned flags;          /* SOBER_FR_FLAG_* bitmask                     */
};

/* ---- lifecycle ---------------------------------------------------------- */
SOBER_FRAME_REDRAW_API int  sober_frame_redraw_install(
    const struct sober_frame_redraw_config *cfg);
SOBER_FRAME_REDRAW_API int  sober_frame_redraw_drive(void);
SOBER_FRAME_REDRAW_API int  sober_frame_redraw_drive_flags(unsigned flags);
SOBER_FRAME_REDRAW_API int  sober_frame_redraw_run(
    const struct sober_frame_redraw_config *cfg);
SOBER_FRAME_REDRAW_API void sober_frame_redraw_uninstall(void);
SOBER_FRAME_REDRAW_API void sober_frame_redraw_reset(void);
SOBER_FRAME_REDRAW_API int  sober_frame_redraw_is_installed(void);
SOBER_FRAME_REDRAW_API int  sober_frame_redraw_is_driven(void);
SOBER_FRAME_REDRAW_API int  sober_frame_redraw_last_rc(void);

/* ---- verify API --------------------------------------------------------- */
/* Structural pre-drive check: gate, RVA invariants, valid callback chain,
 * bound window, correct vtable swap slot.  Requires install() first. */
SOBER_FRAME_REDRAW_API int sober_frame_redraw_verify_shape(
    char *buf, size_t cap);
/* Full check: shape + >=1 guest-RA swap + >=1 GLES-dispatcher draw. */
SOBER_FRAME_REDRAW_API int sober_frame_redraw_verify(char *buf, size_t cap);

/* ---- read-only state accessors (safe before install) ------------------- */
SOBER_FRAME_REDRAW_API uintptr_t sober_frame_redraw_base(void);
SOBER_FRAME_REDRAW_API void  *sober_frame_redraw_native_data(void);
SOBER_FRAME_REDRAW_API void  *sober_frame_redraw_window(void);
SOBER_FRAME_REDRAW_API uintptr_t sober_frame_redraw_redraw_cb(void);

/* ---- diagnostic counters ------------------------------------------------ */
SOBER_FRAME_REDRAW_API uint64_t sober_frame_redraw_drives(void);
SOBER_FRAME_REDRAW_API uint64_t sober_frame_redraw_swaps_total(void);
SOBER_FRAME_REDRAW_API uint64_t sober_frame_redraw_swaps_guest_tagged(void);
SOBER_FRAME_REDRAW_API uint64_t sober_frame_redraw_swaps_synthetic(void);
SOBER_FRAME_REDRAW_API uint64_t sober_frame_redraw_gles_dispatch_draws(void);
SOBER_FRAME_REDRAW_API uint64_t sober_frame_redraw_gles_guest_draws(void);

/* ---- diagnostics / self-test ------------------------------------------- */
SOBER_FRAME_REDRAW_API const char *sober_frame_redraw_describe(
    char *buf, size_t cap);
SOBER_FRAME_REDRAW_API const char *sober_frame_redraw_env_dump(void);
SOBER_FRAME_REDRAW_API int sober_frame_redraw_selftest(char *buf, size_t cap);

#ifdef __cplusplus
}
#endif
#endif /* SOBER_JNI_FRAME_REDRAW_H */
