/*
 * render_probe.c - standalone self-test for the native render backend.
 *
 * Worker-2, task #379.  Owned by src/render/.
 *
 * Usage:
 *   render_probe --probe                 headless capability probe only
 *   render_probe [--backend x11|wayland|surfaceless] [--width W] [--height H]
 *                [--headless]           create window + GL context, draw/swap
 *
 * Prints deterministic markers used by the Phase 3 harness:
 *   "render probe: backend=... x11=... egl=..."
 *   "native window: backend=... size=WxH gl=..."
 *   "first frame rendered"     (only after a successful swap)
 */
#include "native_render.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *argv0)
{
    fprintf(stderr,
            "usage: %s [--probe] [--backend x11|wayland|surfaceless|auto] "
            "[--width N] [--height N] [--headless] [--no-map]\n", argv0);
}

int main(int argc, char **argv)
{
    SoberRenderOptions opts;
    memset(&opts, 0, sizeof(opts));
    opts.width = 640;
    opts.height = 360;
    opts.title = "Sober native render probe";
    opts.map_window = 1;
    opts.draw_test_frame = 1;
    opts.swap_interval = 0;

    int probe_only = 0;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--probe")) probe_only = 1;
        else if (!strcmp(argv[i], "--headless")) { probe_only = 0; opts.backend = SOBER_RENDER_SURFACELESS; }
        else if (!strcmp(argv[i], "--no-map")) opts.map_window = 0;
        else if (!strcmp(argv[i], "--backend") && i + 1 < argc) {
            const char *b = argv[++i];
            if (!strcmp(b, "x11")) opts.backend = SOBER_RENDER_X11;
            else if (!strcmp(b, "wayland")) opts.backend = SOBER_RENDER_WAYLAND;
            else if (!strcmp(b, "surfaceless")) opts.backend = SOBER_RENDER_SURFACELESS;
            else if (!strcmp(b, "auto")) opts.backend = SOBER_RENDER_AUTO;
            else { usage(argv[0]); return 2; }
        } else if (!strcmp(argv[i], "--width") && i + 1 < argc) opts.width = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--height") && i + 1 < argc) opts.height = atoi(argv[++i]);
        else { usage(argv[0]); return 2; }
    }

    char desc[256];
    sober_render_probe_describe(desc, sizeof(desc));
    printf("render probe: %s\n", desc);
    if (probe_only) return 0;

    SoberRender *r = NULL;
    if (sober_render_init(&opts, &r) != 0 || !r) {
        fprintf(stderr, "native window: FAILED to initialise\n");
        return 1;
    }
    int w = 0, h = 0;
    sober_render_size(r, &w, &h);
    printf("native window: backend=%s size=%dx%d handle=%p gl=%s\n",
           sober_render_backend_name(r), w, h,
           sober_render_native_handle(r), sober_render_gl_version(r));

    if (sober_render_swap(r) != 0) {
        fprintf(stderr, "first frame: FAILED (%s)\n", sober_render_error(r));
        sober_render_shutdown(r);
        return 1;
    }
    printf("first frame rendered\n");

    /* Exercise the ANativeWindow_Buffer shim as a second (CPU-path) frame. */
    SoberNativeWindow *anw = sober_render_native_window(r);
    SoberNativeWindowBuffer buf;
    if (anw && sober_render_anw_lock(anw, &buf) == 0) {
        /* paint a simple diagonal gradient into the CPU buffer */
        int stride = buf.stride;
        for (int y = 0; y < buf.height; ++y) {
            for (int x = 0; x < buf.width; ++x) {
                unsigned char *p = (unsigned char *)buf.bits + (size_t)y * stride * 4 + (size_t)x * 4;
                p[0] = (unsigned char)(x * 255 / (buf.width ? buf.width : 1));
                p[1] = (unsigned char)(y * 255 / (buf.height ? buf.height : 1));
                p[2] = 0x80;
                p[3] = 0xff;
            }
        }
        if (sober_render_anw_unlock_and_post(anw) == 0)
            printf("anw buffer frame rendered\n");
        else
            fprintf(stderr, "anw buffer frame: FAILED (%s)\n", sober_render_error(r));
    }

    /* Exercise the guest-swap hook (the marker gate for Phase 3). */
    int (*guest_swap)(void *, void *) =
        (int (*)(void *, void *))sober_render_hook_egl_swap_buffers();
    if (guest_swap &&
        guest_swap(sober_render_egl_display(r), sober_render_egl_surface(r)) != 0) {
        printf("guest swap hook: count=%llu (global=%llu)\n",
               (unsigned long long)sober_render_guest_swap_count(r),
               (unsigned long long)sober_render_guest_swap_count(NULL));
    }

    printf("render summary: frames=%llu first=%d\n",
           (unsigned long long)sober_render_frame_count(r),
           sober_render_is_first_frame(r));
    sober_render_shutdown(r);
    /* Phase 5 gate evidence must survive shutdown (unlike get_active). */
    printf("phase3 persisted after shutdown: %d\n", sober_render_phase3_done());
    if (!sober_render_phase3_done()) {
        fprintf(stderr, "phase3 flag did not persist\\n");
        return 1;
    }
    return 0;
}
