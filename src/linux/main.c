/*
 * main.c - Native Linux entry point for the Sober port.
 *
 * Produces a genuine Linux ELF needing no mocktail/basalt/Android runtime,
 * no Wine/Proton/box64/qemu and no translation layer. The guest is an x86-64
 * Android ELF, so our job is to replace its bionic/NDK imports with native
 * Linux/glibc implementations (docs/decisions/003 and 004).
 *
 * Flags (all non-blocking except the default):
 *   --version | -V   print the banner/version and exit 0
 *   --help    | -h   print the banner, usage and exit 0
 *   --check          run the native self-check (used by CTest) and exit 0
 *   (none)           enter the native epoll event loop until SIGINT/SIGTERM
 *
 * Owned by worker-3 (task #370).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "android_compat.h"
#include "gfx_loader.h"
#include "platform.h"

#define SOBER_NATIVE_VERSION "0.1.0"

static void print_banner(void)
{
    fprintf(stderr,
        "sober-native %s | native Linux ELF | no translation layer\n"
        "compat backend: %s\n"
        "gfx backend: %s\n",
        SOBER_NATIVE_VERSION, sober_android_backend(),
        sober_android_gfx_backend());
}

int main(int argc, char **argv)
{
    sober_android_init(argc, argv);

    if (argc > 1 && (!strcmp(argv[1], "--version") ||
                     !strcmp(argv[1], "-V") ||
                     !strcmp(argv[1], "--help") ||
                     !strcmp(argv[1], "-h"))) {
        int help = !strcmp(argv[1], "--help") || !strcmp(argv[1], "-h");
        print_banner();
        printf("sober-native %s\n", SOBER_NATIVE_VERSION);
        if (help) {
            printf("usage: sober-native [--version|-V] [--help|-h] [--check]\n"
                   "  --version  print version and exit\n"
                   "  --help     print this help and exit\n"
                   "  --check    run native self-check and exit (CI smoke test)\n"
                   "  (no flags) enter the native event loop (Ctrl-C to quit)\n");
        }
        sober_gfx_shutdown();
        sober_android_shutdown();
        return 0;
    }

    print_banner();

    char gfxdesc[256];
    uint32_t gfx = sober_gfx_init();
    sober_gfx_describe(gfx, gfxdesc, sizeof(gfxdesc));
    fprintf(stderr, "native graphics libraries (dlopen): %s\n", gfxdesc);

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    fprintf(stderr, "native monotonic clock (s): %lld\n",
            (long long)ts.tv_sec);

    if (argc > 1 && !strcmp(argv[1], "--check")) {
        fprintf(stderr, "command line: %s\n", getenv("_") ? getenv("_") : argv[0]);
        fprintf(stderr, "NDK/Bionic shim entries: %u | GL/EGL trampolines: %u\n",
                sober_android_implemented_count(), sober_gl_egl_symbol_count());
        fprintf(stderr, "sober-native self-check: OK\n");
        sober_gfx_shutdown();
        sober_android_shutdown();
        return 0;
    }

    if (sober_platform_init() != 0) {
        fprintf(stderr, "sober-native: platform init failed\n");
        sober_gfx_shutdown();
        sober_android_shutdown();
        return 1;
    }
    fprintf(stderr, "sober-native: entering native event loop (Ctrl-C to quit)\n");
    int rc = sober_platform_run();
    sober_platform_shutdown();
    sober_gfx_shutdown();
    sober_android_shutdown();
    return rc == 0 ? 0 : 1;
}
