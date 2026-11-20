/*
 * native_render.h - Native Linux window + EGL/GLES render backend for Sober.
 *
 * Owned by worker-2 (Phase 3, task #379).  Pure native code: the display,
 * window and GL driver libraries are discovered with dlopen() at runtime,
 * so no X11/EGL/Wayland link-time dependency is added and the host's
 * `ldd` stays clean.  Nothing here uses an Android runtime, Wine or any
 * translation layer.
 *
 * Intended integration (worker-3 owns src/linux/ and the harness):
 *   1. sober_render_probe()          -- safe, no display required
 *   2. sober_render_init(&opts, &r)  -- create X11/Wayland window + EGL context
 *   3. install the two guest hooks (below) into the guest's PLT/GOT so that
 *      ANativeWindow_fromSurface() returns our window and
 *      eglCreateWindowSurface() receives the real native window handle
 *   4. after "guest init complete", sober_render_swap(r) draws a deterministic
 *      test frame, swaps it, and sets the first-frame flag
 *   5. sober_render_shutdown(r)
 */
#ifndef SOBER_NATIVE_RENDER_H
#define SOBER_NATIVE_RENDER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SOBER_RENDER_OK     0
#define SOBER_RENDER_ERR  (-1)

/* ---- headless probe flags (sober_render_probe) ------------------------ */
#define SOBER_RENDER_HAVE_X11         0x01u
#define SOBER_RENDER_HAVE_WAYLAND     0x02u
#define SOBER_RENDER_HAVE_EGL         0x04u
#define SOBER_RENDER_HAVE_GLES2       0x08u
#define SOBER_RENDER_HAVE_SURFACELESS 0x10u

typedef struct {
    uint32_t    flags;       /* OR of SOBER_RENDER_HAVE_* */
    int         x11;         /* libX11.so.6 + XOpenDisplay present */
    int         wayland;     /* libwayland-client.so.0 present */
    int         egl;         /* libEGL.so.1 present */
    int         gles2;       /* libGLESv2.so.2 present */
    int         surfaceless; /* EGL_PLATFORM_SURFACELESS_MESA advertised */
    const char *backend;     /* static string, e.g. "x11-egl-gles2" */
} SoberRenderProbe;

/* Idempotent, thread-safe, never opens a display or creates a context.
 * Returns SOBER_RENDER_OK and fills out (out may be NULL). */
int         sober_render_probe(SoberRenderProbe *out);
/* Human-readable one-liner into buf; returns buf. */
const char *sober_render_probe_describe(char *buf, size_t cap);

/* ---- backend / options ------------------------------------------------- */
typedef enum {
    SOBER_RENDER_AUTO = 0,      /* Wayland if reachable, else X11, else surfaceless */
    SOBER_RENDER_X11 = 1,
    SOBER_RENDER_WAYLAND = 2,
    SOBER_RENDER_SURFACELESS = 3
} SoberRenderBackend;

typedef struct {
    SoberRenderBackend backend;   /* default AUTO when zero-initialised */
    int         width;            /* default 1280 */
    int         height;           /* default 720 */
    const char *display;          /* X11 display name; NULL => $DISPLAY */
    const char *title;            /* default "Sober (native render)" */
    int         map_window;       /* default 1; 0 for offscreen/surfaceless */
    int         draw_test_frame;  /* default 1; swap() draws a test pattern */
    int         swap_interval;    /* default 1; -1 leaves driver default */
} SoberRenderOptions;

typedef struct SoberRender SoberRender;   /* opaque */

/* Create the display/window/EGL context.  Returns SOBER_RENDER_OK or ERR;
 * sober_render_error(r) is valid on failure when *out was set. */
int    sober_render_init(const SoberRenderOptions *opts, SoberRender **out);
void   sober_render_shutdown(SoberRender *r);

/* Make the backend's context current on the calling thread. */
int    sober_render_make_current(SoberRender *r);

/* Draw the deterministic first-frame pattern (if enabled), glFinish(),
 * eglSwapBuffers().  Returns OK only when the swap succeeded.  The first
 * successful swap flips sober_render_is_first_frame() to 1. */
int    sober_render_swap(SoberRender *r);
int    sober_render_is_first_frame(SoberRender *r);
uint64_t sober_render_frame_count(SoberRender *r);

/* Count of eglSwapBuffers calls made through the guest PLT hook below.
 * Unlike sober_render_swap() (our own test frame), this only increments when
 * the GUEST calls its eglSwapBuffers@plt, so it is the deterministic signal
 * for "a real guest render entry point ran".  Pass NULL for the global count. */
uint64_t sober_render_guest_swap_count(SoberRender *r);
/* Persisted phase-3 evidence for the strict guest-callback gate; unlike
 * sober_render_get_active() this stays valid after shutdown. */
int    sober_render_phase3_done(void);

/* Introspection (all safe with r == NULL, returning NULL/0). */
void       *sober_render_native_handle(SoberRender *r); /* X11 Window / wl_egl_window* */
void       *sober_render_egl_display(SoberRender *r);
void       *sober_render_egl_context(SoberRender *r);
void       *sober_render_egl_surface(SoberRender *r);
int         sober_render_size(SoberRender *r, int *w, int *h);
const char *sober_render_backend_name(SoberRender *r);
const char *sober_render_gl_version(SoberRender *r);
const char *sober_render_error(SoberRender *r);

/* ---- ANativeWindow / ANativeWindow_Buffer shim ------------------------- *
 * SoberNativeWindow's first three members deliberately match the frozen
 * src/compat `struct ANativeWindow { unsigned refs; int32_t w, h; }`, so the
 * already-shipped ANativeWindow_acquire/release/getWidth/getHeight remain
 * ABI-compatible if they are handed one of these pointers.
 */
typedef struct SoberNativeWindow SoberNativeWindow;

SoberNativeWindow *sober_render_native_window(SoberRender *r);
int32_t  sober_render_anw_width(SoberNativeWindow *w);
int32_t  sober_render_anw_height(SoberNativeWindow *w);
void     sober_render_anw_acquire(SoberNativeWindow *w);
void     sober_render_anw_release(SoberNativeWindow *w);

/* Layout-compatible with Android's ANativeWindow_Buffer. */
typedef struct {
    int32_t width;
    int32_t height;
    int32_t stride;   /* pixels per row */
    int32_t format;   /* WINDOW_FORMAT_RGBA_8888 et al */
    void   *bits;     /* CPU-visible RGBA pixels */
} SoberNativeWindowBuffer;

/* Lock returns a CPU buffer; unlock_and_post uploads it through GLES2 and
 * swaps.  These are the native backing for an eventual guest lock/unlock
 * path and are usable without the guest. */
int sober_render_anw_lock(SoberNativeWindow *w, SoberNativeWindowBuffer *buf);
int sober_render_anw_unlock_and_post(SoberNativeWindow *w);

/* ---- guest PLT/GOT integration hooks (for worker-3) -------------------- *
 * Both hooks use the most recently initialised render context (see
 * sober_render_set_active).  Cast the returned void* to the exact guest
 * prototype before installing it over a JUMP_SLOT:
 *   ANativeWindow_fromSurface: void *(*)(void *env, void *surface)
 *   eglCreateWindowSurface:    void *(*)(void *dpy, void *cfg,
 *                                        void *native_window, const int *attribs)
 * The EGL hook substitutes the real X11 Window / wl_egl_window whenever a
 * render context is active; if none is active it returns NULL so the caller
 * can fall back to the original resolver.
 */
void *sober_render_hook_anative_window_from_surface(void);
void *sober_render_hook_egl_create_window_surface(void);
/* Third hook: install over the guest's eglSwapBuffers@plt GOT slot.  Prototype:
 *   EGLBoolean (*)(void *dpy, void *surface)
 * Calls the native driver unchanged and counts the guest-driven swap. */
void *sober_render_hook_egl_swap_buffers(void);
void  sober_render_set_active(SoberRender *r);
SoberRender *sober_render_get_active(void);

#ifdef __cplusplus
}
#endif
#endif /* SOBER_NATIVE_RENDER_H */
