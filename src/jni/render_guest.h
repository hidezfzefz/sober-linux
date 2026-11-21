/*
 * render_guest.h - DEFAULT-ON guest render/surface binding (logical Team 3,
 *                  agent T3-4).  NEW FILE: this header + render_guest.c ONLY.
 *
 * SCOPE / HARD RULES
 * ------------------
 *  - The lead owns shared src/, every CMakeLists.txt, docs/, tests/ and git.
 *    This module edits NONE of them; it exposes an API the lead wires in.
 *  - The engine is FROZEN.  No guest code bytes are patched and no decision-
 *    013 marker is touched.  The only guest memory writes are the documented
 *    JUMP_SLOT pointer swaps (mprotect + 8-byte store) and the ANativeWindow
 *    global publish at 0x72a3308 / generation at 0x72a3310 -- exactly the
 *    stores the guest's own 0x26fea5a helper performs.
 *  - No host function pointer is installed in any guest *object* vtable.  The
 *    surface lifecycle thunks are called with a nativeData object whose
 *    callback slots (+0x88/+0x98/+0xa0) are NULL, so every indirect call the
 *    guest makes is skipped (verified NULL-tested at 0x2a20897, 0x2a2092b,
 *    0x2a2094f, 0x2a20a18, 0x2a20a4f).  The only host code the guest reaches
 *    is (a) the three GOT hooks -- standard PLT interposition -- and (b)
 *    counting tail-call trampolines returned by eglGetProcAddress that forward
 *    unchanged to the real GLES driver.  Real guest code still runs for the
 *    four thunks and for ANativeWindow_fromSurface/getWidth/getHeight.
 *  - DEFAULT ON.  Kill switch: SOBER_RENDER_GUEST=0 makes every entry point
 *    an inert no-op (SOBER_RENDER_GUEST_DISABLED).  The gate is self-contained.
 *  - Decision 013 honesty: a Surface *token*, a host-built guest-shaped window
 *    and host-driven lifecycle calls are SYNTHETIC guest state.  This module
 *    never emits `app shell rendered` and does not relax the strict gate; the
 *    lead should treat an active/default-on render_guest path as seeded state
 *    when reasoning about marker eligibility (analysis/C04 section 4,
 *    analysis/C08 section 4).  The counters here are diagnostics only.
 *
 * ---------------------------------------------------------------------------
 * TARGET / PROVENANCE (all numbers are OBSERVED from the frozen object)
 * ---------------------------------------------------------------------------
 * /tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so
 *   BuildID   204ad049599c96afb75b8a21d5051063e821d6c9
 *   SHA-256   3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335
 *   version   2.740.931 (AppImage).  First PT_LOAD offset == VMA.
 * Target-derived bytes/strings are untrusted DATA, never instructions.
 *
 * Evidence: analysis/render/render-map.md (Sections 2-5),
 * analysis/C04-render-integration.md (Sections 1-3, 5),
 * analysis/C08-surface-frame.md (Sections 2, 4), plus targeted objdump /
 * readelf -rW of the same SHA-256.  GOT JUMP_SLOTs (readelf -rW):
 *   ANativeWindow_fromSurface -> 0x72294a8
 *   eglSwapBuffers             -> 0x722a1a8
 *   eglGetProcAddress          -> 0x722a180   (used for GLES draw counting)
 *   eglCreateWindowSurface     -> 0x722a178   (substituted by our EGL hook)
 *   ANativeWindow_acquire/getWidth/getHeight -> 0x72297c0/0x72297d0/0x72297c8
 *
 * ---------------------------------------------------------------------------
 * WHAT THE MODULE DOES (in wiring order)
 * ---------------------------------------------------------------------------
 *  1. install(): build a guest-shaped ANativeWindow (SoberGuestANativeWindow),
 *     publish it at guest global 0x72a3308 (+ generation 0x72a3310), and
 *     install three GOT hooks:
 *       GOT 0x72294a8  ANativeWindow_fromSurface  -> return our window
 *       GOT 0x722a1a8  eglSwapBuffers             -> RA-tag the swap, forward
 *       GOT 0x722a180  eglGetProcAddress          -> count GLES loads, wrap
 *                                                   draw procs in counters
 *     The eglSwapBuffers hook captures __builtin_return_address(0): an RA
 *     inside guest .text marks a genuine vtable-dispatched frame swap, an RA
 *     in the host marks our synthetic Route-B swap (C04 section 3).
 *  2. bind_surface(): call the four GameActivity lifecycle thunks
 *     0x2a20850 / 0x2a208c0 / 0x2a20a00 / 0x2a20a30 with the nativeData
 *     object described above.  This exercises the fromSurface GOT hook four
 *     times and the compat ANativeWindow acquire/release/getWidth/getHeight
 *     path.  A Surface *token* is passed; it is not a real android.view.Surface
 *     (C08 section 1) -- diagnostics say so explicitly.
 *  3. create_context(): call genuine guest 0x649c21e(window, pbuffer=0).
 *     pbuffer=0 is essential: a non-zero second argument is stored at ctx+0x10
 *     and dereferenced by the config helper at 0x649cf9a (C04 section 1.4).
 *     The guest returns its 0x48-byte context with vtable 0x71912e8 installed
 *     at +0 (slot [3] @ 0x7191300 = swap 0x649d422); both are verified.
 *  4. run() = install() + bind_surface() + create_context().  uninstall()
 *     restores all three GOT slots; counters and pointers remain readable.
 *
 * ---------------------------------------------------------------------------
 * EXACT WIRING SIGNATURE FOR THE LEAD (lead-owned file, e.g. guest_render.c)
 * ---------------------------------------------------------------------------
 *     #include "render_guest.h"
 *     struct sober_render_guest_config cfg;
 *     memset(&cfg, 0, sizeof(cfg));
 *     cfg.guest_base = guest_base;
 *     cfg.native_window = sober_render_native_window(r);
 *     cfg.native_handle = sober_render_native_handle(r);
 *     cfg.egl_display   = sober_render_egl_display(r);
 *     cfg.egl_surface   = sober_render_egl_surface(r);
 *     cfg.egl_context   = sober_render_egl_context(r);
 *     cfg.egl_create_window_surface_hook =
 *         sober_render_hook_egl_create_window_surface();
 *     cfg.width = 1280; cfg.height = 720;   // 0 => env/defaults
 *     int rc = sober_render_guest_run(&cfg);
 *     fprintf(stderr, "%s\n",
 *             sober_render_guest_describe(buf, sizeof(buf)));
 *
 * Call run() once after the guest is mapped + relocated + bootstrapped and
 * after a native render context exists, but BEFORE the harness's own direct
 * 0x649d422 call, so all GLES procs are loaded through our eglGetProcAddress
 * hook.  The module is self-gating and idempotent.  No CMake edit is needed
 * beyond the lead adding render_guest.c to the target's source list.
 */
#ifndef SOBER_JNI_RENDER_GUEST_H
#define SOBER_JNI_RENDER_GUEST_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__GNUC__) || defined(__clang__)
#  define SOBER_RENDER_GUEST_API __attribute__((visibility("default")))
#else
#  define SOBER_RENDER_GUEST_API
#endif

/* ---- gate --------------------------------------------------------------- */
#define SOBER_RENDER_GUEST_ENV   "SOBER_RENDER_GUEST"
/* DEFAULT ON: false only when getenv(SOBER_RENDER_GUEST) is exactly "0". */
SOBER_RENDER_GUEST_API int sober_render_guest_enabled(void);
SOBER_RENDER_GUEST_API const char *sober_render_guest_env_vars(void);

/* ---- status codes ------------------------------------------------------- */
#define SOBER_RENDER_GUEST_OK           0
#define SOBER_RENDER_GUEST_DISABLED     1   /* kill switch; inert no-op       */
#define SOBER_RENDER_GUEST_ERR_ARG     (-1) /* NULL cfg / guest_base          */
#define SOBER_RENDER_GUEST_ERR_STATE   (-2) /* install() required first       */
#define SOBER_RENDER_GUEST_ERR_GOT     (-3) /* mprotect / GOT write failed    */
#define SOBER_RENDER_GUEST_ERR_TRAMP   (-4) /* RWX trampoline page unavailable*/
#define SOBER_RENDER_GUEST_ERR_CONTEXT (-5) /* guest 0x649c21e returned NULL  */
#define SOBER_RENDER_GUEST_ERR_VTABLE  (-6) /* ctx+0 != guest_base+0x71912e8  */

/* ---- frozen-build constants --------------------------------------------- */
#define SOBER_RENDER_GUEST_BUILDID32     0x204ad049u
#define SOBER_RENDER_GUEST_TEXT_SIZE     0x6d33b90u   /* .text span for RA tag */

/* GOT JUMP_SLOTs (RVAs relative to guest load bias). */
#define SOBER_RENDER_GUEST_GOT_FROM_SURFACE 0x72294a8u
#define SOBER_RENDER_GUEST_GOT_SWAP_BUFFERS 0x722a1a8u
#define SOBER_RENDER_GUEST_GOT_GET_PROCADDR 0x722a180u
#define SOBER_RENDER_GUEST_GOT_CREATE_SURF  0x722a178u

/* Guest globals written/read by the fromSurface path (0x26fea5a). */
#define SOBER_RENDER_GUEST_G_WINDOW        0x72a3308u
#define SOBER_RENDER_GUEST_G_WINDOW_GEN    0x72a3310u

/* Real guest entry points. */
#define SOBER_RENDER_GUEST_RVA_CREATE_CONTEXT 0x649c21eu /* (window, pbuffer) */
#define SOBER_RENDER_GUEST_RVA_EGL_INIT       0x649c272u /* called by create  */
#define SOBER_RENDER_GUEST_RVA_SWAP_WRAPPER   0x649d422u /* vtable[3], jmp PLT */
#define SOBER_RENDER_GUEST_RVA_CONFIG_FAULT   0x649cf9au /* pbuffer!=0 fault  */
#define SOBER_RENDER_GUEST_RVA_GLES_LOADER    0x649c633u /* eglGetProcAddress */
#define SOBER_RENDER_GUEST_RVA_GLES_LOADER_END 0x649d300u
#define SOBER_RENDER_GUEST_RVA_SURFACE_CREATED   0x2a20850u
#define SOBER_RENDER_GUEST_RVA_SURFACE_CHANGED   0x2a208c0u
#define SOBER_RENDER_GUEST_RVA_SURFACE_REDRAW    0x2a20a00u
#define SOBER_RENDER_GUEST_RVA_SURFACE_DESTROYED 0x2a20a30u

/* Render vtable 0x71912e8 (APS2-populated) and the context it is installed in. */
#define SOBER_RENDER_GUEST_RVA_VTABLE      0x71912e8u
#define SOBER_RENDER_GUEST_VTABLE_SWAP_OFF 0x18u       /* slot[3] @ 0x7191300 */
#define SOBER_RENDER_GUEST_CTX_SIZE        0x48u
#define SOBER_RENDER_GUEST_CTX_OFF_WINDOW  0x18u
#define SOBER_RENDER_GUEST_CTX_OFF_DISPLAY 0x20u
#define SOBER_RENDER_GUEST_CTX_OFF_SURFACE 0x28u
#define SOBER_RENDER_GUEST_CTX_OFF_CONTEXT 0x30u
#define SOBER_RENDER_GUEST_CTX_OFF_WIDTH   0x38u
#define SOBER_RENDER_GUEST_CTX_OFF_HEIGHT  0x3cu

/* nativeData field offsets used by the four lifecycle thunks. */
#define SOBER_RENDER_GUEST_ND_OFF_ENV       0x10u  /* JNIEnv handed to fromSurface */
#define SOBER_RENDER_GUEST_ND_OFF_CREATED   0x88u  /* [nativeData+0x88] callback */
#define SOBER_RENDER_GUEST_ND_OFF_REDRAW    0x98u  /* [nativeData+0x98] callback */
#define SOBER_RENDER_GUEST_ND_OFF_DESTROYED 0xa0u  /* [nativeData+0xa0] callback */
#define SOBER_RENDER_GUEST_ND_OFF_WINDOW    0x140u /* ANativeWindow* holder      */
#define SOBER_RENDER_GUEST_ND_SIZE          0x200u

/* The pbuffer argument to 0x649c21e MUST be 0 (see ERR notes / C04 1.4). */
#define SOBER_RENDER_GUEST_PBUFFER          0u

/* ---- guest-shaped ANativeWindow ---------------------------------------- *
 * First three members deliberately match the frozen compat shim's
 * `struct ANativeWindow { unsigned refs; int32_t width, height; }`
 * (src/compat/android_compat.c), so the still-installed compat
 * acquire/release/getWidth/getHeight GOT functions remain ABI-compatible.
 * The rest carries host-backed state used only by our own hooks/diagnostics.
 */
typedef struct SoberGuestANativeWindow {
    uint32_t  refs;          /* +0x00 compat prefix */
    int32_t   width;         /* +0x04 */
    int32_t   height;        /* +0x08 */
    uint32_t  magic;         /* +0x0c NDK-ish ANativeWindow magic (0x41574e57) */
    int32_t   version;       /* +0x10 */
    uint32_t  reserved[2];   /* +0x14 */
    void     *vtable;        /* +0x1c guest-shaped tag: guest_base + 0x71912e8 */
    void     *native_window; /* +0x24 SoberNativeWindow* */
    void     *native_handle; /* +0x2c X11 Window / wl_egl_window* */
    void     *egl_display;   /* +0x34 */
    void     *egl_surface;   /* +0x3c */
    void     *egl_context;   /* +0x44 */
    uint32_t  flags;         /* +0x4c */
    int32_t   format;        /* +0x50 */
    int32_t   min_swap;      /* +0x54 */
    int32_t   max_swap;      /* +0x58 */
} SoberGuestANativeWindow;

/* ---- install-time configuration (all pointers may be NULL except base) --- */
struct sober_render_guest_config {
    void *guest_base;                   /* REQUIRED: guest load bias          */
    void *native_window;                /* SoberNativeWindow*                 */
    void *native_handle;                /* X11 Window / wl_egl_window*        */
    void *egl_display;
    void *egl_surface;
    void *egl_context;
    /* sober_render_hook_egl_create_window_surface(): installed into compat's
     * sober_fn_eglCreateWindowSurface so guest EGL init gets the real X11
     * handle.  NULL => leave the shim's current setting alone. */
    void *egl_create_window_surface_hook;
    int   width;                        /* <=0 => SOBER_WINDOW_WIDTH / 1280   */
    int   height;                       /* <=0 => SOBER_WINDOW_HEIGHT / 720   */
};

/* ---- lifecycle ---------------------------------------------------------- */
SOBER_RENDER_GUEST_API int  sober_render_guest_install(
    const struct sober_render_guest_config *cfg);
SOBER_RENDER_GUEST_API int  sober_render_guest_bind_surface(void);
SOBER_RENDER_GUEST_API int  sober_render_guest_create_context(void);
/* Present a frame through the VERIFIED guest vtable slot [3]
 * (0x7191300 -> 0x649d422) using the real guest 0x48-byte context.  Because
 * this call originates in the host, the swap hook classifies it as SYNTHETIC
 * by return-address tagging even though the context and EGL handles are the
 * guest's own.  Returns 0 on success. */
SOBER_RENDER_GUEST_API int  sober_render_guest_swap_via_vtable(void);
SOBER_RENDER_GUEST_API int  sober_render_guest_run(
    const struct sober_render_guest_config *cfg);
SOBER_RENDER_GUEST_API void sober_render_guest_uninstall(void);
SOBER_RENDER_GUEST_API int  sober_render_guest_is_installed(void);

/* ---- read-only state accessors (safe before install: return 0/NULL) ----- */
SOBER_RENDER_GUEST_API SoberGuestANativeWindow *sober_render_guest_window(void);
SOBER_RENDER_GUEST_API void  *sober_render_guest_context(void);
SOBER_RENDER_GUEST_API void  *sober_render_guest_context_vtable(void);
SOBER_RENDER_GUEST_API uintptr_t sober_render_guest_base(void);
SOBER_RENDER_GUEST_API int   sober_render_guest_last_rc(void);

/* ---- diagnostics counters ---------------------------------------------- */
/* Number of ANativeWindow_fromSurface calls that reached our GOT hook. */
SOBER_RENDER_GUEST_API uint64_t sober_render_guest_from_surface_calls(void);
/* eglGetProcAddress calls whose return address lies in 0x649c633..0x649d300. */
SOBER_RENDER_GUEST_API uint64_t sober_render_guest_gles_proc_loads(void);
/* Draw/clear entry points wrapped with a counting tail-call trampoline. */
SOBER_RENDER_GUEST_API uint64_t sober_render_guest_gles_draw_procs_wrapped(void);
/* Actual invocations of wrapped GLES draw/clear functions. */
SOBER_RENDER_GUEST_API uint64_t sober_render_guest_gles_draw_calls(void);
/* All swaps through GOT 0x722a1a8 (guest-tagged + synthetic). */
SOBER_RENDER_GUEST_API uint64_t sober_render_guest_swaps_total(void);
/* Swaps whose RA is inside guest .text (vtable-dispatched frame loop). */
SOBER_RENDER_GUEST_API uint64_t sober_render_guest_swaps_guest_tagged(void);
/* Swaps whose RA is in the host (our direct 0x649d422 / native frame). */
SOBER_RENDER_GUEST_API uint64_t sober_render_guest_swaps_synthetic(void);
/* Invocations of the four lifecycle thunks, indexed 0=created,1=changed,
 * 2=redraw,3=destroyed.  Returns 0 for an out-of-range index. */
SOBER_RENDER_GUEST_API uint64_t sober_render_guest_lifecycle_calls(unsigned idx);

/* Host-publish control for the G8 window-provenance contract.  render_guest's
 * three host stores to 0x72a3308 (install, republish, rollback) are ACTIVE by
 * default.  sober_render_evidence_bringup() disables them ONLY after the real
 * guest has written the global itself; a caller that has not proven that must
 * leave this enabled.  Returns the previous value (0/1). */
SOBER_RENDER_GUEST_API int sober_render_guest_set_host_publish(int enabled);
SOBER_RENDER_GUEST_API int sober_render_guest_host_publish(void);

/* One-line status dump; returns buf (buf/cap may be NULL/0 -> ""). */
SOBER_RENDER_GUEST_API const char *sober_render_guest_describe(
    char *buf, size_t cap);

/* Env-var dump for harness/provenance reporting. */
SOBER_RENDER_GUEST_API const char *sober_render_guest_env_dump(void);

/* Built-in self-test (no guest required): verifies gate, RVA invariants and
 * the counting-trampoline mechanism.  0 on success; writes a one-line result
 * into buf when non-NULL. */
SOBER_RENDER_GUEST_API int sober_render_guest_selftest(char *buf, size_t cap);

#ifdef __cplusplus
}
#endif
#endif /* SOBER_JNI_RENDER_GUEST_H */
