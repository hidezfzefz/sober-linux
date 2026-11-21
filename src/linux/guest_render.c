/*
 * guest_render.c - Phase 3 window/first-frame phase (worker-3, task #380).
 *
 * Called from the init harness once all guest constructors have returned.
 * Uses worker-2's src/render backend (runtime dlopen, ldd stays clean) and,
 * when available, the guest post-init render entry points mapped by worker-1
 * in analysis/render/render-map.md.
 */
#define _GNU_SOURCE
#include "guest_render.h"
#include "native_render.h"
#include "render_guest.h"

#include <dlfcn.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/* deterministic signal                                                */
/* ------------------------------------------------------------------ */
static void emit_marker(const char *text, const char *env_name)
{
    ssize_t n = write(STDOUT_FILENO, text, strlen(text));
    (void)n;
    const char *path = getenv(env_name);
    if (path) {
        int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) {
            ssize_t m = write(fd, text, strlen(text));
            (void)m;
            close(fd);
        }
    }
}

/* Hold the window open so the smoke test can observe it on the X server. */
static void hold_window(void)
{
    const char *v = getenv("SOBER_WINDOW_HOLD_MS");
    long ms = v ? atol(v) : 4000;
    if (ms <= 0) return;
    fprintf(stderr, "[render] holding native window for %ld ms (SOBER_WINDOW_HOLD_MS)\n", ms);
    struct timespec ts = { .tv_sec = ms / 1000,
                           .tv_nsec = (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

/* Compat shim's EGL fn-pointer slots (src/compat/gl_egl_forward.c, frozen). */
extern void (*sober_fn_eglCreateWindowSurface)(void);
extern void (*sober_fn_eglSwapBuffers)(void);

/* ------------------------------------------------------------------ */
/* render_guest contracts (spec: src/jni/render_guest.h)               */
/* ------------------------------------------------------------------ */
/* The canonical module is not necessarily in the link (sober_jni is only
 * pulled into guest-init-host and CMake ownership is elsewhere).  Resolve it
 * at runtime; if absent, install the same three GOT hooks and drive the same
 * lifecycle/context entry points locally.  Both paths keep guest-tagged
 * swaps/draws separate from our synthetic first frame.  No guest code bytes
 * are patched and no decision-013 marker is touched. */
typedef int         (*gr_run_fn)(const struct sober_render_guest_config *);
typedef const char *(*gr_describe_fn)(char *, size_t);
typedef uint64_t    (*gr_count_fn)(void);
typedef uint64_t    (*gr_life_fn)(unsigned);

struct gr_module {
    gr_run_fn      run;
    gr_describe_fn describe;
    gr_count_fn    swaps_total;
    gr_count_fn    swaps_guest;
    gr_count_fn    swaps_synth;
    gr_count_fn    draws;
    gr_count_fn    proc_loads;
    gr_count_fn    from_surface;
    gr_life_fn     life;
    int            present;
};
static struct gr_module s_gr;

/* native_render additions (native_render.h is another worker's file). */
extern void     sober_render_set_guest_base_for_hooks(uintptr_t);
extern int      sober_render_install_guest_got_hooks(uintptr_t);
extern void     sober_render_note_lifecycle(unsigned);
extern uint64_t sober_render_swaps_guest_tagged(void);
extern uint64_t sober_render_swaps_synthetic(void);
extern uint64_t sober_render_from_surface_calls(void);
extern uint64_t sober_render_gles_proc_loads(void);
extern uint64_t sober_render_gles_draw_calls(void);
extern uint64_t sober_render_lifecycle_calls(unsigned);

#define GR_RVA_GOT_FROM_SURFACE 0x72294a8u
#define GR_RVA_GOT_SWAP_BUFFERS 0x722a1a8u
#define GR_RVA_GOT_GET_PROCADDR 0x722a180u
#define GR_RVA_LIFE_CREATED     0x2a20850u
#define GR_RVA_LIFE_CHANGED     0x2a208c0u
#define GR_RVA_LIFE_REDRAW      0x2a20a00u
#define GR_RVA_LIFE_DESTROYED   0x2a20a30u
#define GR_RVA_CREATE_CONTEXT   0x649c21eu
#define GR_RVA_VTABLE           0x71912e8u
#define GR_G_WINDOW             0x72a3308u
#define GR_G_WINDOW_GEN         0x72a3310u

static SoberGuestANativeWindow *s_gr_anw;
static unsigned char *s_gr_nd;
static char s_gr_env[16];
static void *s_gr_token[2];

static int gr_contract_enabled(void)
{
    const char *e = getenv("SOBER_RENDER_GUEST");
    return !(e && !strcmp(e, "0"));   /* DEFAULT ON */
}

static void gr_resolve_module(void)
{
    static int done;
    if (done) return;
    done = 1;
    s_gr.run          = (gr_run_fn)(uintptr_t)
        dlsym(RTLD_DEFAULT, "sober_render_guest_run");
    s_gr.describe     = (gr_describe_fn)(uintptr_t)
        dlsym(RTLD_DEFAULT, "sober_render_guest_describe");
    s_gr.swaps_total  = (gr_count_fn)(uintptr_t)
        dlsym(RTLD_DEFAULT, "sober_render_guest_swaps_total");
    s_gr.swaps_guest  = (gr_count_fn)(uintptr_t)
        dlsym(RTLD_DEFAULT, "sober_render_guest_swaps_guest_tagged");
    s_gr.swaps_synth  = (gr_count_fn)(uintptr_t)
        dlsym(RTLD_DEFAULT, "sober_render_guest_swaps_synthetic");
    s_gr.draws        = (gr_count_fn)(uintptr_t)
        dlsym(RTLD_DEFAULT, "sober_render_guest_gles_draw_calls");
    s_gr.proc_loads   = (gr_count_fn)(uintptr_t)
        dlsym(RTLD_DEFAULT, "sober_render_guest_gles_proc_loads");
    s_gr.from_surface = (gr_count_fn)(uintptr_t)
        dlsym(RTLD_DEFAULT, "sober_render_guest_from_surface_calls");
    s_gr.life         = (gr_life_fn)(uintptr_t)
        dlsym(RTLD_DEFAULT, "sober_render_guest_lifecycle_calls");
    s_gr.present = s_gr.run != NULL;
}

/* Local equivalent of render_guest: publish a guest-shaped window, install
 * the three GOT hooks, drive the four lifecycle thunks, then create a guest
 * EGL context with pbuffer=0 (a non-zero value faults at 0x649cf9a). */
static int gr_local_render_contract(uintptr_t base, SoberRender *r)
{
    typedef void (*life_created_fn)(void *, void *, uintptr_t, void *);
    typedef void (*life_changed_fn)(void *, void *, uintptr_t, void *,
                                    int, int, int);
    typedef void (*life_destroy_fn)(void *, void *, uintptr_t);
    typedef uintptr_t (*create_ctx_fn)(uintptr_t, uintptr_t);

    int w = 1280, h = 720;
    void *env = (void *)s_gr_env;
    void *tok = (void *)&s_gr_token;
    void *nd;
    uintptr_t ctx;
    int rc;

    if (!gr_contract_enabled() || !base || !r) return 0;
    sober_render_size(r, &w, &h);

    s_gr_anw = (SoberGuestANativeWindow *)calloc(1, sizeof(*s_gr_anw));
    s_gr_nd  = (unsigned char *)calloc(1, SOBER_RENDER_GUEST_ND_SIZE);
    if (!s_gr_anw || !s_gr_nd) {
        free(s_gr_anw); s_gr_anw = NULL;
        free(s_gr_nd);  s_gr_nd = NULL;
        return -1;
    }
    /* Large refcount: guest pause/destroy paths release this window but we
     * keep the allocation live for the process lifetime. */
    s_gr_anw->refs         = 0x10000u;
    s_gr_anw->width        = (int32_t)w;
    s_gr_anw->height       = (int32_t)h;
    s_gr_anw->magic        = 0x41574e57u;   /* 'AWNW' */
    s_gr_anw->version      = 1;
    s_gr_anw->vtable       = (void *)(base + GR_RVA_VTABLE);
    s_gr_anw->native_window = sober_render_native_window(r);
    s_gr_anw->native_handle = sober_render_native_handle(r);
    s_gr_anw->egl_display  = sober_render_egl_display(r);
    s_gr_anw->egl_surface  = sober_render_egl_surface(r);
    s_gr_anw->egl_context  = sober_render_egl_context(r);
    s_gr_token[0] = (void *)s_gr_anw;        /* opaque Surface token */
    nd = (void *)s_gr_nd;
    *(void **)(s_gr_nd + SOBER_RENDER_GUEST_ND_OFF_ENV) = env;
    /* Callback slots +0x88/+0x98/+0xa0 stay NULL: the guest NULL-checks all
     * of them before an indirect call, so no host code enters a vtable. */

    *(volatile uintptr_t *)(base + GR_G_WINDOW) = (uintptr_t)s_gr_anw;
    __atomic_fetch_add((uintptr_t *)(base + GR_G_WINDOW_GEN), 1u,
                       __ATOMIC_SEQ_CST);

    rc = sober_render_install_guest_got_hooks(base);
    fprintf(stderr,
            "[render] render_guest GOT hooks (local): "
            "%#x/%#x/%#x rc=%d\n",
            (unsigned)GR_RVA_GOT_FROM_SURFACE,
            (unsigned)GR_RVA_GOT_SWAP_BUFFERS,
            (unsigned)GR_RVA_GOT_GET_PROCADDR, rc);
    if (rc != 0) return rc;

    /* Four genuine guest lifecycle thunks; nativeData callbacks are NULL. */
    ((life_created_fn)(base + GR_RVA_LIFE_CREATED))(env, NULL, (uintptr_t)nd, tok);
    sober_render_note_lifecycle(0);
    ((life_changed_fn)(base + GR_RVA_LIFE_CHANGED))(env, NULL, (uintptr_t)nd,
                                                    tok, w, h, 0);
    sober_render_note_lifecycle(1);
    ((life_created_fn)(base + GR_RVA_LIFE_REDRAW))(env, NULL, (uintptr_t)nd, tok);
    sober_render_note_lifecycle(2);
    ((life_destroy_fn)(base + GR_RVA_LIFE_DESTROYED))(env, NULL, (uintptr_t)nd);
    sober_render_note_lifecycle(3);

    /* pbuffer MUST be 0: a non-zero value is stored at ctx+0x10 and then
     * dereferenced by the config helper at 0x649cf9a. */
    ctx = ((create_ctx_fn)(base + GR_RVA_CREATE_CONTEXT))((uintptr_t)s_gr_anw,
                                                          SOBER_RENDER_GUEST_PBUFFER);
    fprintf(stderr,
            "[render] render_guest: context 0x%lx vtable=%p (pbuffer=%u)\n",
            (unsigned long)ctx,
            ctx ? *(void **)ctx : NULL,
            (unsigned)SOBER_RENDER_GUEST_PBUFFER);
    return ctx ? 0 : -2;
}

/* Install the render_guest contracts once, before the harness's direct
 * 0x649d422 swap.  Module path wins when the module is linked in. */
static int gr_install_render_contract(uintptr_t base, SoberRender *r)
{
    gr_resolve_module();
    if (!gr_contract_enabled()) {
        fprintf(stderr, "[render] render_guest contracts disabled by env\n");
        return 0;
    }
    if (s_gr.present) {
        struct sober_render_guest_config cfg;
        char dbg[512];
        int rc;
        memset(&cfg, 0, sizeof(cfg));
        cfg.guest_base = (void *)base;
        cfg.native_window = sober_render_native_window(r);
        cfg.native_handle = sober_render_native_handle(r);
        cfg.egl_display   = sober_render_egl_display(r);
        cfg.egl_surface   = sober_render_egl_surface(r);
        cfg.egl_context   = sober_render_egl_context(r);
        cfg.egl_create_window_surface_hook =
            sober_render_hook_egl_create_window_surface();
        cfg.width = 1280; cfg.height = 720;
        rc = s_gr.run(&cfg);
        fprintf(stderr, "[render] %s\n",
                s_gr.describe ? s_gr.describe(dbg, sizeof(dbg))
                              : "render_guest module active");
        if (rc == SOBER_RENDER_GUEST_DISABLED) return 0;
        if (rc != SOBER_RENDER_GUEST_OK) {
            fprintf(stderr, "[render] render_guest module rc=%d\n", rc);
            return rc;
        }
        return 0;
    }
    /* Never silently mask a missing module (lead integration constraint). */
    fprintf(stderr,
            "[render] WARNING: render_guest module not present; using "
            "contract-identical local GOT/lifecycle fallback\n");
    return gr_local_render_contract(base, r);
}

/* Combined guest-swap count: native_render always counts, and the canonical
 * module (when present) counts independently.  Either source is sufficient
 * evidence that the 0x649d422 call reached the swap GOT hook. */
static uint64_t gr_guest_swap_evidence(void)
{
    uint64_t total = sober_render_guest_swap_count(NULL);
    if (s_gr.present && s_gr.swaps_total) total += s_gr.swaps_total();
    return total;
}

static void gr_report_counters(void)
{
    fprintf(stderr,
            "[render] render_guest counters: source=%s from_surface=%llu "
            "proc_loads=%llu draws=%llu swaps(total/guest/synth)=%llu/%llu/%llu "
            "lifecycle=%llu/%llu/%llu/%llu\n",
            s_gr.present ? "module" : "local",
            (unsigned long long)(s_gr.present && s_gr.from_surface
                ? s_gr.from_surface() : sober_render_from_surface_calls()),
            (unsigned long long)(s_gr.present && s_gr.proc_loads
                ? s_gr.proc_loads() : sober_render_gles_proc_loads()),
            (unsigned long long)(s_gr.present && s_gr.draws
                ? s_gr.draws() : sober_render_gles_draw_calls()),
            (unsigned long long)gr_guest_swap_evidence(),
            (unsigned long long)(s_gr.present && s_gr.swaps_guest
                ? s_gr.swaps_guest() : sober_render_swaps_guest_tagged()),
            (unsigned long long)(s_gr.present && s_gr.swaps_synth
                ? s_gr.swaps_synth() : sober_render_swaps_synthetic()),
            (unsigned long long)(s_gr.present && s_gr.life
                ? s_gr.life(0) : sober_render_lifecycle_calls(0)),
            (unsigned long long)(s_gr.present && s_gr.life
                ? s_gr.life(1) : sober_render_lifecycle_calls(1)),
            (unsigned long long)(s_gr.present && s_gr.life
                ? s_gr.life(2) : sober_render_lifecycle_calls(2)),
            (unsigned long long)(s_gr.present && s_gr.life
                ? s_gr.life(3) : sober_render_lifecycle_calls(3)));
}

/* ------------------------------------------------------------------ */
/* guest render entry points (worker-1's render-map.md, BuildID 204ad049) */
/* ------------------------------------------------------------------ */
static int call_guest_render_entries(uintptr_t base, SoberRender *r)
{
    long (*guest_swap)(uintptr_t) =
        (long (*)(uintptr_t))(base + 0x649d422u);   /* vtable[3], jmp eglSwapBuffers */

    /* Redirect the guest's eglCreateWindowSurface through worker-2's hook so
     * it receives our real native X11 window instead of an ANativeWindow. */
    void *hook = sober_render_hook_egl_create_window_surface();
    if (hook) {
        sober_fn_eglCreateWindowSurface = (void (*)(void))hook;
        fprintf(stderr, "[render] installed EGL window-surface hook into compat shim\n");
    }

    /* Install worker-2's guest-swap counter over the guest's eglSwapBuffers
     * JUMP_SLOT (render-map §2: GOT VMA 0x722a1a8). The guest swap wrapper at
     * 0x649d422 tail-jumps through this slot, so a count > 0 proves the swap
     * really came from guest code, not from our own native test frame. */
    void *swap_hook = sober_render_hook_egl_swap_buffers();
    if (s_gr.present) {
        /* render_guest already installed its RA-tagging hook at this slot;
         * overwriting it would silently zero its guest/synthetic counters. */
        fprintf(stderr,
                "[render] render_guest owns guest-swap counter hook at GOT 0x722a1a8\n");
    } else if (swap_hook) {
        uintptr_t got = base + 0x722a1a8u;
        long ps = sysconf(_SC_PAGESIZE);
        if (ps <= 0) ps = 4096;
        uintptr_t page = got & ~((uintptr_t)ps - 1);
        if (mprotect((void *)page, (size_t)ps, PROT_READ | PROT_WRITE) == 0) {
            *(uintptr_t *)got = (uintptr_t)swap_hook;
            mprotect((void *)page, (size_t)ps, PROT_READ);
            fprintf(stderr, "[render] installed guest-swap counter hook at GOT 0x722a1a8\n");
        } else {
            fprintf(stderr, "[render] WARNING: could not mprotect guest eglSwapBuffers GOT\n");
        }
    }

    /* Route B (worker-1 §5): harness-built 0x48-byte guest context carrying
     * our native EGLDisplay/EGLSurface. Deterministic and crash-free here;
     * this is a REAL guest entry-point call at RVA 0x649d422. */
    uint64_t ctx[9];   /* 0x48 bytes */
    memset(ctx, 0, sizeof(ctx));
    ctx[0x20 / 8] = (uint64_t)(uintptr_t)sober_render_egl_display(r);
    ctx[0x28 / 8] = (uint64_t)(uintptr_t)sober_render_egl_surface(r);
    long src = guest_swap((uintptr_t)ctx);
    /* Use the combined evidence counter: native_render always counts here,
     * and a linked render_guest module may count independently. */
    uint64_t guest_swaps = gr_guest_swap_evidence();
    fprintf(stderr,
            "[render] guest call 0x649d422 swap(ctx=%p, egl=%p, surf=%p) -> rc=%ld "
            "| guest-driven swaps observed: %llu\n",
            (void *)ctx, (void *)ctx[4], (void *)ctx[5], src,
            (unsigned long long)guest_swaps);
    if (guest_swaps == 0) {
        fprintf(stderr, "[render] guest swap hook did NOT observe the call\n");
        return -1;
    }

    /* Route A (worker-1 §5, opt-in): guest creates its own EGL context from
     * our ANativeWindow shim. Currently faults in the guest's EGL config helper
     * (0x649cf9a), so it is disabled by default; kept for A/B experimentation. */
    if (getenv("SOBER_GUEST_ROUTE_A")) {
        uintptr_t anw = (uintptr_t)sober_render_native_window(r);
        uintptr_t (*create_ctx)(uintptr_t, uintptr_t) =
            (uintptr_t (*)(uintptr_t, uintptr_t))(base + 0x649c21eu);
        uintptr_t guest_ctx = create_ctx(anw, 1);
        fprintf(stderr,
                "[render] guest call 0x649c21e create_context(anw=%p, skip=1) -> 0x%lx\n",
                (void *)anw, (unsigned long)guest_ctx);
        if (guest_ctx) {
            long rsrc = guest_swap(guest_ctx);
            fprintf(stderr,
                    "[render] guest call 0x649d422 swap(routeA ctx=0x%lx) -> rc=%ld\n",
                    (unsigned long)guest_ctx, rsrc);
        }
    }
    return src >= 0 ? 0 : -1;
}

/* ------------------------------------------------------------------ */
/* main entry                                                          */
/* ------------------------------------------------------------------ */
int guest_render_run(uintptr_t guest_base)
{

    /* 1. safe, headless-capable probe */
    SoberRenderProbe probe;
    memset(&probe, 0, sizeof(probe));
    if (sober_render_probe(&probe) != SOBER_RENDER_OK) {
        fprintf(stderr, "[render] backend probe FAILED\n");
        return -1;
    }
    char desc[256];
    fprintf(stderr, "[render] %s\n",
            sober_render_probe_describe(desc, sizeof(desc)));

    /* 2. create the native Linux window + EGL context */
    SoberRenderOptions opts;
    memset(&opts, 0, sizeof(opts));
    /* AUTO prefers Wayland when WAYLAND_DISPLAY is set, but the X11 path is
     * the one validated here (real window, xdotool-visible, llvmpipe EGL). */
    opts.backend = SOBER_RENDER_X11;
    const char *be = getenv("SOBER_RENDER_BACKEND");
    if (be && !strcmp(be, "wayland")) opts.backend = SOBER_RENDER_WAYLAND;
    else if (be && !strcmp(be, "surfaceless")) opts.backend = SOBER_RENDER_SURFACELESS;
    else if (be && !strcmp(be, "auto")) opts.backend = SOBER_RENDER_AUTO;
    opts.width = 640;
    opts.height = 480;
    opts.title = "SoberNativeRenderPhase3";
    opts.map_window = 1;
    opts.draw_test_frame = 1;
    SoberRender *r = NULL;
    if (sober_render_init(&opts, &r) != SOBER_RENDER_OK || !r) {
        fprintf(stderr, "[render] window/EGL init FAILED\n");
        return -2;
    }
    sober_render_set_active(r);

    int w = 0, h = 0;
    sober_render_size(r, &w, &h);
    fprintf(stderr,
        "[render] native window: handle=%p backend=%s %dx%d egl=%p ctx=%p surf=%p\n",
        sober_render_native_handle(r), sober_render_backend_name(r), w, h,
        sober_render_egl_display(r), sober_render_egl_context(r),
        sober_render_egl_surface(r));

    /* 3. make our EGL context current and draw+swap the first frame */
    if (sober_render_make_current(r) != SOBER_RENDER_OK) {
        fprintf(stderr, "[render] make_current FAILED: %s\n",
                sober_render_error(r));
        sober_render_shutdown(r);
        return -3;
    }
    if (sober_render_swap(r) != SOBER_RENDER_OK) {
        fprintf(stderr, "[render] first swap FAILED: %s\n",
                sober_render_error(r));
        sober_render_shutdown(r);
        return -4;
    }
    if (!sober_render_is_first_frame(r)) {
        fprintf(stderr, "[render] first-frame flag not set after successful swap\n");
        sober_render_shutdown(r);
        return -5;
    }
    fprintf(stderr, "[render] GL version: %s | frames swapped: %llu\n",
            sober_render_gl_version(r),
            (unsigned long long)sober_render_frame_count(r));

    /* 3b. install the render_guest contracts BEFORE the direct 0x649d422
     * call: three GOT hooks, four surface lifecycle thunks and a guest EGL
     * context with pbuffer=0.  Guest-tagged swaps/draws are counted apart
     * from our synthetic first frame; no marker is emitted here. */
    if (gr_install_render_contract(guest_base, r) != 0) {
        fprintf(stderr, "[render] WARNING: render_guest contract install failed\n");
    }
    gr_report_counters();

    /* 4. drive the guest's post-init render entry points (worker-1 map). The
     * marker below is gated on these guest calls, not on our own test frame. */
    if (call_guest_render_entries(guest_base, r) != 0) {
        fprintf(stderr, "[render] guest render entry-point calls FAILED\n");
        sober_render_shutdown(r);
        return -6;
    }
    gr_report_counters();   /* post-swap: guest-tagged vs synthetic evidence */

    /* 5. deterministic signal: only after native window + swap + guest calls */
    emit_marker("first frame rendered\n", "SOBER_FRAME_MARKER");

    /* 6. keep the window visible for the external smoke test */
    hold_window();

    sober_render_shutdown(r);
    return 0;
}
