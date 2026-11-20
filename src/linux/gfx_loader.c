/*
 * gfx_loader.c - dlopen-based discovery of native graphics backends.
 * Owned by worker-3 (task #370). No external translation layer involved.
 */
#define _GNU_SOURCE
#include "gfx_loader.h"

#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    const char *soname;
    const char *marker;
    uint32_t    bit;
} Backend;

static const Backend kBackends[] = {
    { "libX11.so.6",            "XOpenDisplay",           SOBER_GFX_X11 },
    { "libwayland-client.so.0", "wl_display_connect",     SOBER_GFX_WAYLAND },
    { "libGL.so.1",             "glGetString",            SOBER_GFX_OPENGL },
    { "libvulkan.so.1",         "vkGetInstanceProcAddr",  SOBER_GFX_VULKAN },
};

static void    *g_libs[4];
static uint32_t g_available;
static pthread_once_t g_once = PTHREAD_ONCE_INIT;

void *sober_gfx_open(const char *soname)
{
    return soname ? dlopen(soname, RTLD_LAZY | RTLD_LOCAL) : NULL;
}
void *sober_gfx_symbol(void *lib, const char *sym)
{
    return (lib && sym) ? dlsym(lib, sym) : NULL;
}
void sober_gfx_close(void *lib)
{
    if (lib) dlclose(lib);
}

static void discover(void)
{
    for (size_t i = 0; i < sizeof(kBackends) / sizeof(kBackends[0]); ++i) {
        void *lib = sober_gfx_open(kBackends[i].soname);
        if (!lib) continue;
        if (sober_gfx_symbol(lib, kBackends[i].marker)) {
            g_libs[i] = lib;
            g_available |= kBackends[i].bit;
        } else {
            sober_gfx_close(lib);
        }
    }
}

uint32_t sober_gfx_init(void)
{
    pthread_once(&g_once, discover);
    return g_available;
}
uint32_t sober_gfx_available(void) { return g_available; }

const char *sober_gfx_describe(uint32_t mask, char *buf, size_t cap)
{
    if (!buf || cap == 0) return "";
    buf[0] = '\0';
    const char *names[] = { "X11", "Wayland", "OpenGL", "Vulkan" };
    uint32_t bits[] = { SOBER_GFX_X11, SOBER_GFX_WAYLAND,
                        SOBER_GFX_OPENGL, SOBER_GFX_VULKAN };
    int first = 1;
    for (size_t i = 0; i < 4; ++i) {
        if (!(mask & bits[i])) continue;
        size_t used = strlen(buf);
        size_t room = cap - used - 1;
        snprintf(buf + used, room + 1, "%s%s", first ? "" : " ", names[i]);
        first = 0;
    }
    if (first) snprintf(buf, cap, "none (headless)");
    return buf;
}

void sober_gfx_shutdown(void)
{
    for (size_t i = 0; i < 4; ++i) {
        if (g_libs[i]) { sober_gfx_close(g_libs[i]); g_libs[i] = NULL; }
    }
    g_available = 0;
}
