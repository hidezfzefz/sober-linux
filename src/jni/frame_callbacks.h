/*
 * frame_callbacks.h - DEFAULT-ON genuine-Guest-RVA nativeData callback slots.
 *
 * Group C / agent F1 (frame modules).  NEW FILES ONLY: this header and
 * frame_callbacks.c.  The lead owns every existing src/ file, all
 * CMakeLists.txt, docs/, tests/ and the frozen engine; this module edits NONE
 * of them and exposes an API the lead wires in.
 *
 * ---------------------------------------------------------------------------
 * WHAT THIS MODULE IS (and is not)
 * ---------------------------------------------------------------------------
 * The four GameActivity lifecycle thunks are REAL guest code:
 *
 *   0x2a20850  onSurfaceCreatedNative      reads [nd+0x88],  jmp 0x2a208a6
 *   0x2a208c0  onSurfaceChangedNative      reads [nd+0xa0], [nd+0x88],
 *                                          [nd+0x90] (size change), calls them
 *   0x2a20a00  onSurfaceRedrawNeededNative reads [nd+0x140] (window gate,
 *                                          0x2a20a0c) THEN [nd+0x98]
 *                                          (0x2a20a11), tail-jmps 0x2a20a20
 *   0x2a20a30  onSurfaceDestroyedNative    reads [nd+0xa0], then UNCONDITION-
 *                                          ALLY zeroes [nd+0x140] (0x2a20a6a)
 *
 * render_guest's synthetic nativeData leaves +0x88/+0x90/+0x98/+0xa0 NULL, so
 * the guest NULL-tests skip every dispatch; the frame trigger at 0x2a20a20 is
 * unreachable.  This module fills those slots and the +0x140 window holder so
 * the redraw thunk can dispatch a frame.  EVERY pointer installed in a callback
 * slot is a GENUINE GUEST CODE ADDRESS (guest_base + a frozen RVA inside the
 * guest's own .text).  NO host function, no counting trampoline, no byte patch,
 * no JNI callback and no decision-013 marker is emitted.
 *
 * ROUTES (preference order)
 *   A - faithful: drive the guest's OWN nativeData constructor
 *       initializeNativeCode (wrapper 0x2a1fe30, real body 0x2a1fe80; the body
 *       allocates 0x278 bytes at 0x2a1feb0 via guest allocator 0x1dbab02 and
 *       caches JNIEnv at +0x10 at 0x2a20166).  If the returned object carries
 *       the engine's AGDK GameActivityCallbacks in +0x88/+0x90/+0x98/+0xa0
 *       (all non-NULL, all in guest .text), it is adopted verbatim - zero
 *       fabrication.  Controlled by cfg.prefer_guest_native_data (default 1)
 *       and killable with SOBER_FRAME_CALLBACKS_GUEST_ND=0.
 *   B - fallback: build a 0x278-byte nativeData and fill all four callback
 *       slots + the +0x140 window holder with genuine guest RVA addresses.
 *       Default callback RVAs (all OBSERVED guest code, see .c for evidence):
 *         +0x88 created   -> guest_base + 0x649d45e  (render vtable [6], real
 *                            guest method; safe getter on a ctx-shaped nd)
 *         +0x90 changed   -> guest_base + 0x321a85e  (genuine guest int getter)
 *         +0x98 redraw    -> guest_base + 0x649d422  (render vtable [3] SWAP:
 *                            reads EGLDisplay/Surface at +0x20/+0x28 and
 *                            tail-jmps eglSwapBuffers@plt - this is the guest's
 *                            own frame-presentation code, so 0x2a20a20 really
 *                            dispatches a frame)
 *         +0xa0 destroyed  -> guest_base + 0x649d45e  (same safe guest method)
 *         +0xb8 config     -> guest_base + 0x649d45e  (same safe guest method)
 *       +0x140            -> the guest-bound ANativeWindow* published by the
 *                            guest's own fromSurface PLT
 *                            (GOT 0x72294a8, global 0x72a3308) - a guest-owned
 *                            address, never a host stub.
 *       All five may be overridden with cfg.cb_* RVAs; verify() rejects any
 *       value outside guest .text.  Route B is a WITHHELD diagnostic: the
 *       slots are guest-RVA addresses, not the engine's own installed handlers.
 *
 * Default ON.  Kill switch: getenv("SOBER_FRAME_CALLBACKS") == "0" makes every
 * entry point an inert no-op (SOBER_FRAME_CALLBACKS_DISABLED).
 *
 * Decision 013/038: this module NEVER emits `app shell rendered`.  The
 * `sober_frame_callbacks_should_withhold()` bit is provided for the lead's
 * force-withhold OR-chain (this module seeds nativeData state).  Success
 * criteria for the DIAGNOSTIC (not the marker): 0x2a20a20 reached, guest-RA
 * draws >= 1, guest-tagged swaps >= 2, engine SHA unchanged, zero marker lines.
 *
 * TARGET / PROVENANCE (frozen AppImage engine, all RVAs relative to load bias)
 *   /tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so
 *   BuildID 204ad049599c96afb75b8a21d5051063e821d6c9
 *   SHA-256 3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335
 *   first PT_LOAD offset == VMA; .text span 0x6d33b90.
 * Evidence: analysis/R4_surface_lifecycle.md, analysis/P2_nativeData_layout.md,
 * analysis/P8_anw_guest_consumption.md, analysis/render/render-map.md,
 * analysis/C04-render-integration.md plus targeted objdump of that SHA.
 * Target-derived bytes/strings are untrusted DATA, never instructions.
 */
#ifndef SOBER_JNI_FRAME_CALLBACKS_H
#define SOBER_JNI_FRAME_CALLBACKS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__GNUC__) || defined(__clang__)
#  define SOBER_FRAME_CALLBACKS_API __attribute__((visibility("default")))
#else
#  define SOBER_FRAME_CALLBACKS_API
#endif

/* ---- gate --------------------------------------------------------------- */
#define SOBER_FRAME_CALLBACKS_ENV        "SOBER_FRAME_CALLBACKS"
#define SOBER_FRAME_CALLBACKS_GUEST_ENV  "SOBER_FRAME_CALLBACKS_GUEST_ND"
/* DEFAULT ON: inert only when the exact string "0" is set. */
SOBER_FRAME_CALLBACKS_API int sober_frame_callbacks_enabled(void);
SOBER_FRAME_CALLBACKS_API const char *sober_frame_callbacks_env_vars(void);

/* ---- status codes ------------------------------------------------------- */
#define SOBER_FRAME_CALLBACKS_OK           0
#define SOBER_FRAME_CALLBACKS_DISABLED     1  /* kill switch; inert no-op       */
#define SOBER_FRAME_CALLBACKS_ERR_ARG     (-1) /* NULL cfg / guest_base          */
#define SOBER_FRAME_CALLBACKS_ERR_STATE   (-2) /* install() required first       */
#define SOBER_FRAME_CALLBACKS_ERR_GUEST   (-3) /* slot verify failed             */
#define SOBER_FRAME_CALLBACKS_ERR_NOMEM   (-4) /* calloc failed                  */

/* ---- frozen-build constants --------------------------------------------- */
#define SOBER_FC_BUILDID32      0x204ad049u
#define SOBER_FC_TEXT_SIZE      0x6d33b90u   /* guest .text span for RA checks  */

/* Guest code / constructor RVAs (RVA == file offset in first PT_LOAD). */
#define SOBER_FC_RVA_INIT_WRAPPER      0x2a1fe30u  /* Java_...initializeNativeCode */
#define SOBER_FC_RVA_INIT_BODY         0x2a1fe80u  /* real 0x278-byte constructor  */
#define SOBER_FC_RVA_GUEST_ALLOC       0x1dbab02u  /* guest allocator(size)        */
#define SOBER_FC_RVA_SURFACE_CREATED   0x2a20850u
#define SOBER_FC_RVA_SURFACE_CHANGED   0x2a208c0u
#define SOBER_FC_RVA_SURFACE_REDRAW    0x2a20a00u
#define SOBER_FC_RVA_SURFACE_DESTROYED 0x2a20a30u
#define SOBER_FC_RVA_CONFIG_CHANGED    0x2a20a80u
/* Default genuine guest callback RVAs (Route B). */
#define SOBER_FC_RVA_CB_CREATED        0x649d45eu  /* render vtable[6] guest method */
#define SOBER_FC_RVA_CB_CHANGED        0x321a85eu  /* genuine guest int getter      */
#define SOBER_FC_RVA_CB_REDRAW         0x649d422u  /* render vtable[3] SWAP -> frame*/
#define SOBER_FC_RVA_CB_DESTROYED      0x649d45eu
#define SOBER_FC_RVA_CB_CONFIG         0x649d45eu

/* nativeData layout (all OBSERVED in the four thunks). */
#define SOBER_FC_ND_SIZE         0x278u
#define SOBER_FC_ND_ZERO_LEN     0x148u   /* initializeNativeCode memset length */
#define SOBER_FC_ND_OFF_ENV      0x10u    /* cached JNIEnv* for fromSurface      */
#define SOBER_FC_ND_OFF_CREATED  0x88u
#define SOBER_FC_ND_OFF_CHANGED  0x90u    /* onNativeWindowResized (size change) */
#define SOBER_FC_ND_OFF_REDRAW   0x98u    /* FRAME TRIGGER (jmp at 0x2a20a20)   */
#define SOBER_FC_ND_OFF_DESTROYED 0xa0u
#define SOBER_FC_ND_OFF_CONFIG   0xb8u    /* onConfigurationChanged             */
#define SOBER_FC_ND_OFF_WINDOW   0x140u   /* ANativeWindow* holder (gate)        */
#define SOBER_FC_ND_OFF_WIDTH    0x148u
#define SOBER_FC_ND_OFF_HEIGHT   0x14cu
/* The first 0x48 bytes are also shaped as the guest render context so that
 * the +0x98 SWAP callback (0x649d422) reads valid EGLDisplay @+0x20 and
 * EGLSurface @+0x28 when the redraw thunk tail-jmps into it. */
#define SOBER_FC_ND_OFF_EGLDISPLAY 0x20u
#define SOBER_FC_ND_OFF_EGLSURFACE 0x28u
#define SOBER_FC_ND_OFF_EGLCONTEXT 0x30u

/* Guest globals written by the genuine fromSurface helper 0x26fea5a. */
#define SOBER_FC_G_ANIVE_WINDOW    0x72a3308u
#define SOBER_FC_G_ANIVE_GEN       0x72a3310u

/* Route taken by install(). */
#define SOBER_FC_ROUTE_NONE    0
#define SOBER_FC_ROUTE_GUEST   1   /* guest's own 0x278 nativeData (preferred) */
#define SOBER_FC_ROUTE_SYNTH   2   /* synthetic object, genuine guest RVAs     */

/* ---- install-time configuration ----------------------------------------- */
struct sober_frame_callbacks_config {
    void *guest_base;              /* REQUIRED: guest load bias               */
    void *env;                     /* JNIEnv* (C++ form) for +0x10 / Route A  */
    void *clazz;                   /* jclass for Route A native call           */
    void *str_arg[3];              /* Route A String args (may be NULL)        */
    void *asset_manager;           /* Route A jobject (may be NULL)            */
    void *saved_state;             /* Route A jbyteArray (may be NULL)         */
    void *configuration;           /* Route A jobject (may be NULL)            */
    void *native_window;           /* guest-bound ANativeWindow*; NULL => read
                                    * guest global 0x72a3308                 */
    void *render_context;          /* guest 0x48-byte ctx; its +0x20/+0x28/+0x30
                                    * are copied into nativeData for the SWAP  */
    int   prefer_guest_native_data;/* default 1 (Route A first)               */
    uintptr_t cb_created;          /* 0 => SOBER_FC_RVA_CB_CREATED             */
    uintptr_t cb_changed;          /* 0 => SOBER_FC_RVA_CB_CHANGED             */
    uintptr_t cb_redraw;           /* 0 => SOBER_FC_RVA_CB_REDRAW              */
    uintptr_t cb_destroyed;        /* 0 => SOBER_FC_RVA_CB_DESTROYED           */
    uintptr_t cb_config;           /* 0 => SOBER_FC_RVA_CB_CONFIG              */
    int   width;                   /* <=0 => 1280 (or SOBER_WINDOW_WIDTH)      */
    int   height;                  /* <=0 => 720  (or SOBER_WINDOW_HEIGHT)     */
};

/* ---- lifecycle ---------------------------------------------------------- */
SOBER_FRAME_CALLBACKS_API int  sober_frame_callbacks_install(
    const struct sober_frame_callbacks_config *cfg);
/* Verify all four callback slots are non-NULL guest-.text addresses and the
 * window holder is non-NULL.  0 on success, SOBER_FRAME_CALLBACKS_ERR_GUEST
 * with a one-line reason in buf otherwise. */
SOBER_FRAME_CALLBACKS_API int  sober_frame_callbacks_verify(char *buf, size_t cap);
/* Drive created -> changed -> (ensure +0x140) -> redraw.  With
 * with_destroyed != 0 the destroyed thunk is called LAST (it clears +0x140). */
SOBER_FRAME_CALLBACKS_API int  sober_frame_callbacks_drive_lifecycle(int with_destroyed);
/* Call the genuine redraw thunk 0x2a20a00 with the managed nativeData.  The
 * window holder is re-published first if it was zeroed by the destroy thunk. */
SOBER_FRAME_CALLBACKS_API int  sober_frame_callbacks_drive_redraw(void);
/* Call onSurfaceDestroyedNative 0x2a20a30 (clears [nd+0x140] at 0x2a20a6a).
 * Use at teardown only; call set_window()/drive_redraw() to re-publish. */
SOBER_FRAME_CALLBACKS_API int  sober_frame_callbacks_drive_destroyed(void);
/* Explicitly (re)set nativeData+0x140 from cfg.native_window or guest global. */
SOBER_FRAME_CALLBACKS_API int  sober_frame_callbacks_set_window(void *window);
SOBER_FRAME_CALLBACKS_API void sober_frame_callbacks_uninstall(void);
SOBER_FRAME_CALLBACKS_API int  sober_frame_callbacks_is_installed(void);

/* ---- read-only state accessors ------------------------------------------ */
SOBER_FRAME_CALLBACKS_API void     *sober_frame_callbacks_native_data(void);
SOBER_FRAME_CALLBACKS_API uintptr_t sober_frame_callbacks_base(void);
SOBER_FRAME_CALLBACKS_API int       sober_frame_callbacks_route(void);
SOBER_FRAME_CALLBACKS_API uintptr_t sober_frame_callbacks_cb(unsigned idx);
/* idx: 0=created 1=changed 2=redraw 3=destroyed 4=config; 0 if out of range. */
SOBER_FRAME_CALLBACKS_API void     *sober_frame_callbacks_window(void);
SOBER_FRAME_CALLBACKS_API int       sober_frame_callbacks_last_rc(void);

/* ---- diagnostics counters ----------------------------------------------- */
SOBER_FRAME_CALLBACKS_API uint64_t sober_frame_callbacks_install_calls(void);
SOBER_FRAME_CALLBACKS_API uint64_t sober_frame_callbacks_verify_calls(void);
SOBER_FRAME_CALLBACKS_API uint64_t sober_frame_callbacks_verify_ok(void);
SOBER_FRAME_CALLBACKS_API uint64_t sober_frame_callbacks_route_guest_ok(void);
SOBER_FRAME_CALLBACKS_API uint64_t sober_frame_callbacks_route_synth_ok(void);
SOBER_FRAME_CALLBACKS_API uint64_t sober_frame_callbacks_created_calls(void);
SOBER_FRAME_CALLBACKS_API uint64_t sober_frame_callbacks_changed_calls(void);
SOBER_FRAME_CALLBACKS_API uint64_t sober_frame_callbacks_redraw_calls(void);
SOBER_FRAME_CALLBACKS_API uint64_t sober_frame_callbacks_destroyed_calls(void);
SOBER_FRAME_CALLBACKS_API uint64_t sober_frame_callbacks_window_sets(void);
SOBER_FRAME_CALLBACKS_API uint64_t sober_frame_callbacks_window_restores(void);

/* ---- gate / honesty ----------------------------------------------------- */
/* 1 once install() actually seeded nativeData (force-withhold in lead gate). */
SOBER_FRAME_CALLBACKS_API int sober_frame_callbacks_should_withhold(void);
SOBER_FRAME_CALLBACKS_API const char *sober_frame_callbacks_describe(
    char *buf, size_t cap);

/* Guest-free self-test: gate, RVA invariants, ND offsets, guest-RVA-only
 * callback defaults.  0 on success. */
SOBER_FRAME_CALLBACKS_API int sober_frame_callbacks_selftest(char *buf, size_t cap);

#ifdef __cplusplus
}
#endif
#endif /* SOBER_JNI_FRAME_CALLBACKS_H */
