/*
 * frame_surface.h - DEFAULT-ON real-EGLSurface binding for the frozen guest
 *                   (agent F5, group C).  NEW FILE: this header +
 *                   frame_surface.c ONLY.  Lead owns all existing src/ files,
 *                   every CMakeLists.txt, docs/, tests/, gate and git.
 *
 * SCOPE / HARD RULES
 * ------------------
 *  - DEFAULT ON.  Kill switch: SOBER_FRAME_SURFACE=0 (exact string) makes every
 *    entry point an inert no-op (SOBER_FRAME_SURFACE_DISABLED).  Self-contained
 *    gate, no dependency on any other module's env var.
 *  - The engine is FROZEN.  No guest code byte is patched.  The ONLY guest
 *    memory writes this module performs are the two JUMP_SLOT pointer swaps
 *    (mprotect + 8-byte store) at GOT 0x72294a8 and GOT 0x722a178, the mirror
 *    of the guest's own `ctx+0x18` window store (guest insn 0x649c2cf), the
 *    `ctx+0x28` surface store (guest insn 0x649c3f6) and the host-side
 *    nativeData `+0x140` re-assert (see below).
 *  - NO host store of the window global.  Unlike render_guest.c:326-329 this
 *    module NEVER writes guest global 0x72a3308.  The window is published by
 *    the GUEST itself: we call the guest's own bind helper 0x26fea5a, whose
 *    instructions execute `ANativeWindow_fromSurface@plt` at 0x26fea78,
 *    `incl [0x72a3310]` at 0x26fea90 and the global store at 0x26feaa0.
 *    Provenance is therefore verified by RETURN-ADDRESS at our fromSurface GOT
 *    hook (must land in that helper) AND by the guest's generation delta.
 *  - NO fake counters.  Every counter below is bumped only when the real
 *    event happens: an actual GOT-hook entry, an actual guest helper call, an
 *    actual lifecycle thunk call, an actual detect-and-restore of nativeData
 *    +0x140.  No counter is incremented by install(), verify() or selftest().
 *  - NO host fake vtable.  The window object carries the compat ANativeWindow
 *    prefix (refs/width/height) only; its vtable/ops fields stay zero.  All
 *    ANativeWindow entry points remain the shim's real GOT functions.
 *  - NO host callbacks and NO decision-013 marker.  nativeData callback slots
 *    +0x88/+0x90/+0x98/+0xa0/+0xb8 remain NULL (the guest NULL-tests every
 *    one).  This module never emits `app shell rendered` or any other marker.
 *  - NO byte patches, no JNI stubs, no CMake edits.  The lead adds
 *    frame_surface.c to the sober_jni source list.
 *
 * TARGET / PROVENANCE (all numbers OBSERVED from the frozen AppImage object)
 * ---------------------------------------------------------------------------
 * /tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so
 *   BuildID  204ad049599c96afb75b8a21d5051063e821d6c9
 *   SHA-256  3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335
 *   version  2.740.931.  First PT_LOAD offset == VMA; .text span 0x6d33b90.
 * Target-derived bytes/strings are untrusted DATA, never instructions.
 *
 * Evidence: analysis/render/render-map.md (2/3/3b/5),
 * analysis/C04-render-integration.md (1.2/1.4/2.1/2.2),
 * analysis/C08-surface-frame.md (2.3/2.7),
 * analysis/T3_5_render-evidence.md (2.1/2.3),
 * analysis/P2_nativeData_layout.md (1.1/1.2/2/4.2), plus targeted
 * `objdump -D -M intel` / `readelf -rW` of the same SHA-256 (re-checked F5).
 *
 * ---------------------------------------------------------------------------
 * WHAT THE MODULE DOES (wiring order)
 * ---------------------------------------------------------------------------
 *  1. install(): allocate a compat-prefix ANativeWindow (refs/width/height),
 *     a host nativeData object (callbacks NULL), and install TWO GOT hooks:
 *       GOT 0x72294a8  ANativeWindow_fromSurface -> return our window to a
 *                      guest-RA caller (records RA + generation evidence).
 *       GOT 0x722a178  eglCreateWindowSurface    -> call the REAL host EGL
 *                      create function supplied by the lead (the
 *                      native_render substitution hook forwards to libEGL
 *                      with the real X11/Wayland handle) and return its real
 *                      EGLSurface.  The original GOT value is saved and used
 *                      as a forwarding fallback.
 *  2. bind_guest_window(): call genuine guest 0x26fea5a(env, Surface token).
 *     The GUEST executes the fromSurface call, the generation increment and
 *     the store to global 0x72a3308.  We only read the global and verify that
 *     it equals our window, that the guest generation advanced and that the
 *     fromSurface hook observed a return address inside 0x26fea78..0x26feaa0.
 *  3. bind_native_data(): call the three genuine guest thunks in the real
 *     lifecycle order -- created 0x2a20850, changed 0x2a208c0, redraw
 *     0x2a20a00.  The redraw thunk tests [nativeData+0x140] BEFORE [+0x98]
 *     (0x2a20a0c); we therefore re-assert [+0x140] = the guest-bound window
 *     before and after every thunk.  An explicit on_destroyed() entry point
 *     calls 0x2a20a30 and re-asserts the field again, because the destroy
 *     thunk unconditionally zeroes +0x140 at 0x2a20a6a.  The destroy thunk is
 *     NOT part of run() (that would leave the field NULL -- P2 4.2).
 *  4. bind_surface(): verify ctx+0 == base+0x71912e8 (genuine guest vtable,
 *     NOT a host-installed one) and ctx+0x10 == 0 (window path, not pbuffer),
 *     set ctx+0x18 = guest-bound window, call the guest's REAL config helper
 *     0x649cf72 and then its REAL surface helper 0x649d1c2(ctx, cfg).  The
 *     latter reaches our 0x722a178 hook and returns a real EGLSurface; we
 *     store it at ctx+0x28 (guest insn 0x649c3f6) and mirror the guest's
 *     eglMakeCurrent at 0x649c408 so the surface is current for draws.
 *     Subsequent guest draw/swap (0x649d422 reads +0x20/+0x28) now has a
 *     valid real surface.
 *  5. verify(): checks all of the above and returns OK/negative status with a
 *     one-line verdict.  uninstall() restores both GOT slots; all counters and
 *     accessors stay readable for post-run diagnostics.
 *
 * ---------------------------------------------------------------------------
 * EXACT WIRING SIGNATURE FOR THE LEAD (lead-owned file)
 * ---------------------------------------------------------------------------
 *     #include "frame_surface.h"
 *     struct sober_frame_surface_config cfg;
 *     memset(&cfg, 0, sizeof(cfg));
 *     cfg.guest_base = guest_base;
 *     cfg.context    = sober_render_guest_context(); // genuine 0x649c21e ctx
 *     cfg.egl_create_window_surface =
 *         sober_render_hook_egl_create_window_surface();
 *     cfg.egl_make_current = <real eglMakeCurrent function pointer>;
 *     cfg.native_window = sober_render_native_window(r);
 *     cfg.width = 1280; cfg.height = 720;
 *     int rc = sober_frame_surface_run(&cfg);
 *     char v[256];
 *     fprintf(stderr, "[frame_surface] %s\n",
 *             sober_frame_surface_verify(v, sizeof(v)) == 0 ? v :
 *             sober_frame_surface_describe(v, sizeof(v)));
 *
 * Call run() after a genuine guest context exists (e.g. render_guest's
 * create_context()) and BEFORE the guest frame loop.  The module is
 * self-gating and idempotent.  No CMake edit beyond adding frame_surface.c to
 * the sober_jni target is needed.
 */
#ifndef SOBER_JNI_FRAME_SURFACE_H
#define SOBER_JNI_FRAME_SURFACE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__GNUC__) || defined(__clang__)
#  define SOBER_FRAME_SURFACE_API __attribute__((visibility("default")))
#else
#  define SOBER_FRAME_SURFACE_API
#endif

/* ---- gate --------------------------------------------------------------- */
#define SOBER_FRAME_SURFACE_ENV "SOBER_FRAME_SURFACE"
/* DEFAULT ON: false only when getenv(SOBER_FRAME_SURFACE) is exactly "0". */
SOBER_FRAME_SURFACE_API int sober_frame_surface_enabled(void);
SOBER_FRAME_SURFACE_API const char *sober_frame_surface_env_vars(void);

/* ---- status codes ------------------------------------------------------- */
#define SOBER_FRAME_SURFACE_OK             0
#define SOBER_FRAME_SURFACE_DISABLED       1   /* kill switch; inert no-op    */
#define SOBER_FRAME_SURFACE_ERR_ARG       (-1) /* NULL cfg/base/ctx           */
#define SOBER_FRAME_SURFACE_ERR_STATE     (-2) /* install() required first    */
#define SOBER_FRAME_SURFACE_ERR_GOT       (-3) /* mprotect / GOT write failed */
#define SOBER_FRAME_SURFACE_ERR_WINDOW    (-4) /* guest did not publish window*/
#define SOBER_FRAME_SURFACE_ERR_CONFIG    (-5) /* guest config helper NULL    */
#define SOBER_FRAME_SURFACE_ERR_SURFACE   (-6) /* real EGL surface is NULL    */
#define SOBER_FRAME_SURFACE_ERR_CONTEXT   (-7) /* ctx vtable/parent mismatch  */
#define SOBER_FRAME_SURFACE_ERR_PROVENANCE (-8) /* no guest RA / gen evidence */
#define SOBER_FRAME_SURFACE_ERR_MAKECURRENT (-9) /* eglMakeCurrent failed      */
#define SOBER_FRAME_SURFACE_ERR_IDENTITY (-10)   /* guest BuildID mismatch     */

/* ---- frozen-build constants --------------------------------------------- */
#define SOBER_FRAME_SURFACE_BUILDID32      0x204ad049u
#define SOBER_FRAME_SURFACE_TEXT_SIZE      0x6d33b90u   /* .text RA window */

/* GOT JUMP_SLOTs (RVA relative to the guest load bias; readelf -rW). */
#define SOBER_FRAME_SURFACE_GOT_FROM_SURFACE   0x72294a8u
#define SOBER_FRAME_SURFACE_GOT_CREATE_SURFACE 0x722a178u
#define SOBER_FRAME_SURFACE_GOT_MAKE_CURRENT   0x722a148u /* introspection */

/* Real guest entry points / observed instruction anchors. */
#define SOBER_FRAME_SURFACE_RVA_BIND_WINDOW       0x26fea5au /* 0x26fea5a helper */
#define SOBER_FRAME_SURFACE_RVA_FROM_SURFACE_CALL 0x26fea78u /* plt call        */
#define SOBER_FRAME_SURFACE_RVA_FROM_SURFACE_RA   0x26fea7du /* RA at hook      */
#define SOBER_FRAME_SURFACE_RVA_GEN_INC           0x26fea90u /* incl gen        */
#define SOBER_FRAME_SURFACE_RVA_GLOBAL_STORE      0x26feaa0u /* store 0x72a3308 */
#define SOBER_FRAME_SURFACE_RVA_BIND_RA_END       0x26feaa7u /* helper RA range */
#define SOBER_FRAME_SURFACE_RVA_CONFIG_HELPER     0x649cf72u /* (ctx)->cfg      */
#define SOBER_FRAME_SURFACE_RVA_SURFACE_HELPER    0x649d1c2u /* (ctx,cfg)       */
#define SOBER_FRAME_SURFACE_RVA_WSURFACE_CALL     0x649d1e9u /* plt call        */
#define SOBER_FRAME_SURFACE_RVA_STORE_SURFACE     0x649c3f6u /* ctx+0x28 = ret  */
#define SOBER_FRAME_SURFACE_RVA_MAKE_CURRENT      0x649c408u /* guest mirror   */
#define SOBER_FRAME_SURFACE_RVA_STORE_WINDOW      0x649c2cfu /* ctx+0x18 = win */
#define SOBER_FRAME_SURFACE_RVA_CREATE_CONTEXT    0x649c21eu
#define SOBER_FRAME_SURFACE_RVA_SWAP              0x649d422u
#define SOBER_FRAME_SURFACE_RVA_VTABLE            0x71912e8u
#define SOBER_FRAME_SURFACE_RVA_LIFE_CREATED      0x2a20850u
#define SOBER_FRAME_SURFACE_RVA_LIFE_CHANGED      0x2a208c0u
#define SOBER_FRAME_SURFACE_RVA_LIFE_REDRAW       0x2a20a00u
#define SOBER_FRAME_SURFACE_RVA_LIFE_DESTROYED    0x2a20a30u
#define SOBER_FRAME_SURFACE_REDRAW_FIELD_CHECK    0x2a20a0cu /* +0x140 test    */
#define SOBER_FRAME_SURFACE_DESTROY_FIELD_CLEAR   0x2a20a6au /* +0x140 = 0     */

/* Guest globals (RVA relative to load bias). */
#define SOBER_FRAME_SURFACE_RVA_G_WINDOW     0x72a3308u /* ANativeWindow*    */
#define SOBER_FRAME_SURFACE_RVA_G_WINDOW_GEN 0x72a3310u /* dword generation  */

/* 0x48-byte guest EGL context (P2 1.2). */
#define SOBER_FRAME_SURFACE_CTX_SIZE         0x48u
#define SOBER_FRAME_SURFACE_CTX_OFF_VPTR     0x00u
#define SOBER_FRAME_SURFACE_CTX_OFF_PARENT   0x10u /* shared parent, 0 here   */
#define SOBER_FRAME_SURFACE_CTX_OFF_WINDOW   0x18u /* ANativeWindow*          */
#define SOBER_FRAME_SURFACE_CTX_OFF_DISPLAY  0x20u /* EGLDisplay              */
#define SOBER_FRAME_SURFACE_CTX_OFF_SURFACE  0x28u /* EGLSurface              */
#define SOBER_FRAME_SURFACE_CTX_OFF_CONTEXT  0x30u /* EGLContext              */

/* nativeData layout (P2 2). */
#define SOBER_FRAME_SURFACE_ND_SIZE          0x200u
#define SOBER_FRAME_SURFACE_ND_OFF_ENV       0x10u
#define SOBER_FRAME_SURFACE_ND_OFF_CREATED   0x88u
#define SOBER_FRAME_SURFACE_ND_OFF_CHANGED   0x90u
#define SOBER_FRAME_SURFACE_ND_OFF_REDRAW    0x98u
#define SOBER_FRAME_SURFACE_ND_OFF_DESTROYED 0xa0u
#define SOBER_FRAME_SURFACE_ND_OFF_WINDOW    0x140u
#define SOBER_FRAME_SURFACE_ND_OFF_WIDTH     0x148u
#define SOBER_FRAME_SURFACE_ND_OFF_HEIGHT    0x14cu

/* Host-backed, guest-shaped ANativeWindow.  First three members deliberately
 * match the frozen compat shim's `struct ANativeWindow { unsigned refs;
 * int32_t width, height; }` so the still-installed compat
 * acquire/release/getWidth/getHeight GOT functions remain ABI-compatible.
 * NO vtable pointer is stored here (no host fake vtable). */
typedef struct SoberFrameSurfaceWindow {
    uint32_t  refs;          /* +0x00 compat prefix; high seed survives release */
    int32_t   width;         /* +0x04 */
    int32_t   height;        /* +0x08 */
    uint32_t  magic;         /* +0x0c diagnostic tag 'AWNW' (0x41574e57) */
    void     *native_window; /* +0x10 host SoberNativeWindow* (diagnostic only) */
} SoberFrameSurfaceWindow;

/* Install-time configuration.  Only guest_base and context are required.
 * egl_create_window_surface / egl_make_current may be NULL: the module then
 * falls back to the compat shim's weak function-pointer slots.  The lead
 * should pass the native_render substitution hook for the create function so
 * the real X11/Wayland native handle reaches libEGL. */
struct sober_frame_surface_config {
    void *guest_base;                /* REQUIRED: guest load bias            */
    void *context;                   /* REQUIRED: genuine 0x48-byte ctx      */
    void *egl_create_window_surface; /* real EGL create fn (guest prototype) */
    void *egl_make_current;          /* real EGL makeCurrent fn              */
    void *native_window;             /* host SoberNativeWindow* (diagnostic) */
    int   width;                     /* <=0 => SOBER_WINDOW_WIDTH / 1280     */
    int   height;                    /* <=0 => SOBER_WINDOW_HEIGHT / 720    */
};

/* ---- lifecycle ---------------------------------------------------------- */
SOBER_FRAME_SURFACE_API int  sober_frame_surface_install(
    const struct sober_frame_surface_config *cfg);
/* Guest writes global 0x72a3308 via 0x26fea5a; RA + generation verified. */
SOBER_FRAME_SURFACE_API int  sober_frame_surface_bind_guest_window(void);
/* created -> changed -> redraw, re-asserting nativeData+0x140 each time. */
SOBER_FRAME_SURFACE_API int  sober_frame_surface_bind_native_data(void);
/* Drive guest 0x649d1c2 through the 0x722a178 hook; bind real EGL surface. */
SOBER_FRAME_SURFACE_API int  sober_frame_surface_bind_surface(void);
/* Optional: call destroy thunk 0x2a20a30, then re-assert +0x140 again. */
SOBER_FRAME_SURFACE_API int  sober_frame_surface_on_destroyed(void);
SOBER_FRAME_SURFACE_API int  sober_frame_surface_run(
    const struct sober_frame_surface_config *cfg);
SOBER_FRAME_SURFACE_API void sober_frame_surface_uninstall(void);
SOBER_FRAME_SURFACE_API int  sober_frame_surface_is_installed(void);

/* Full provenance/consistency check.  0 = OK; negative status otherwise;
 * buf receives a one-line verdict (buf/cap may be NULL/0). */
SOBER_FRAME_SURFACE_API int  sober_frame_surface_verify(char *buf, size_t cap);

/* ---- read-only state accessors (safe before install: 0/NULL) ------------- */
SOBER_FRAME_SURFACE_API SoberFrameSurfaceWindow *sober_frame_surface_window(void);
SOBER_FRAME_SURFACE_API void     *sober_frame_surface_context(void);
SOBER_FRAME_SURFACE_API void     *sober_frame_surface_surface(void);
SOBER_FRAME_SURFACE_API void     *sober_frame_surface_egl_config(void);
SOBER_FRAME_SURFACE_API uintptr_t sober_frame_surface_base(void);
SOBER_FRAME_SURFACE_API void     *sober_frame_surface_native_data(void);
/* Value read back from guest global base+0x72a3308 (never host-written). */
SOBER_FRAME_SURFACE_API uintptr_t sober_frame_surface_bound_window(void);

/* G8 C3/C4 contract for the mNativeObject JNI field provider (worker-1 #492):
 *  - is_surface_token(obj): 1 when `obj` is the exact Surface handle this
 *    module hands to the genuine guest bind helper 0x26fea5a.  No other object
 *    is accepted.
 *  - native_handle(): the ANativeWindow* that the GUEST publishes at
 *    0x72a3308 via its own store at 0x26feaa0 (identical to bound_window()).
 *    GetLongField(surface,"mNativeObject") must return THIS value so C4
 *    (ret == mnative) is satisfied by the real guest store, never a made-up
 *    host pointer.  Returns NULL until the genuine guest bind succeeded. */
SOBER_FRAME_SURFACE_API int    sober_frame_surface_is_surface_token(void *obj);
SOBER_FRAME_SURFACE_API void  *sober_frame_surface_native_handle(void);
SOBER_FRAME_SURFACE_API uint32_t  sober_frame_surface_generation_before(void);
SOBER_FRAME_SURFACE_API uint32_t  sober_frame_surface_generation_after(void);
SOBER_FRAME_SURFACE_API int       sober_frame_surface_guest_store_seen(void);
SOBER_FRAME_SURFACE_API int       sober_frame_surface_surface_ready(void);
SOBER_FRAME_SURFACE_API int       sober_frame_surface_last_rc(void);

/* ---- real-event counters (diagnostics only, never a marker input) ------- */
/* Entries into the fromSurface GOT hook (0x72294a8). */
SOBER_FRAME_SURFACE_API uint64_t sober_frame_surface_from_surface_calls(void);
/* ...whose return address lies in guest .text. */
SOBER_FRAME_SURFACE_API uint64_t sober_frame_surface_from_surface_guest_ra(void);
/* ...whose return address lies in 0x26fea78..0x26feaa7 (bind helper proof). */
SOBER_FRAME_SURFACE_API uint64_t sober_frame_surface_from_surface_bind_ra(void);
/* Entries into the eglCreateWindowSurface GOT hook (0x722a178). */
SOBER_FRAME_SURFACE_API uint64_t sober_frame_surface_create_surface_calls(void);
/* ...where the real EGL create returned a non-NULL EGLSurface. */
SOBER_FRAME_SURFACE_API uint64_t sober_frame_surface_create_surface_ok(void);
/* ...whose return address lies in guest 0x649d1c2 (the surface helper). */
SOBER_FRAME_SURFACE_API uint64_t sober_frame_surface_create_from_helper(void);
/* ...where the window argument matched the guest-bound window global. */
SOBER_FRAME_SURFACE_API uint64_t sober_frame_surface_create_window_matches(void);
/* Actual calls to guest bind helper 0x26fea5a. */
SOBER_FRAME_SURFACE_API uint64_t sober_frame_surface_bind_helper_calls(void);
/* Actual calls to guest config helper 0x649cf72. */
SOBER_FRAME_SURFACE_API uint64_t sober_frame_surface_config_helper_calls(void);
/* Actual calls to guest surface helper 0x649d1c2. */
SOBER_FRAME_SURFACE_API uint64_t sober_frame_surface_surface_helper_calls(void);
/* Actual writes of a real EGLSurface into ctx+0x28. */
SOBER_FRAME_SURFACE_API uint64_t sober_frame_surface_surface_bound_count(void);
/* Actual makeCurrent calls / successes. */
SOBER_FRAME_SURFACE_API uint64_t sober_frame_surface_make_current_calls(void);
SOBER_FRAME_SURFACE_API uint64_t sober_frame_surface_make_current_ok(void);
/* Lifecycle thunk invocations: 0=created,1=changed,2=redraw,3=destroyed. */
SOBER_FRAME_SURFACE_API uint64_t sober_frame_surface_lifecycle_calls(unsigned idx);
/* Times nativeData+0x140 was found cleared and actually re-asserted. */
SOBER_FRAME_SURFACE_API uint64_t sober_frame_surface_window_field_reasserts(void);

/* ---- diagnostics -------------------------------------------------------- */
SOBER_FRAME_SURFACE_API const char *sober_frame_surface_describe(
    char *buf, size_t cap);
SOBER_FRAME_SURFACE_API const char *sober_frame_surface_env_dump(void);

/* Built-in self-test (no guest required): gate defaults, RVA invariants and
 * the +0x140 re-assert mechanism on a local scratch object.  Never bumps the
 * real event counters.  0 on success; one-line result into buf when non-NULL. */
SOBER_FRAME_SURFACE_API int sober_frame_surface_selftest(char *buf, size_t cap);

#ifdef __cplusplus
}
#endif
#endif /* SOBER_JNI_FRAME_SURFACE_H */
