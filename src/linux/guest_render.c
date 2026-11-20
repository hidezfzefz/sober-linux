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
    if (swap_hook) {
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
    uint64_t guest_swaps = sober_render_guest_swap_count(r);
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

    /* 4. drive the guest's post-init render entry points (worker-1 map). The
     * marker below is gated on these guest calls, not on our own test frame. */
    if (call_guest_render_entries(guest_base, r) != 0) {
        fprintf(stderr, "[render] guest render entry-point calls FAILED\n");
        sober_render_shutdown(r);
        return -6;
    }

    /* 5. deterministic signal: only after native window + swap + guest calls */
    emit_marker("first frame rendered\n", "SOBER_FRAME_MARKER");

    /* 6. keep the window visible for the external smoke test */
    hold_window();

    sober_render_shutdown(r);
    return 0;
}
