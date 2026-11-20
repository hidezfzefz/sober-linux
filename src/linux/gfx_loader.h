/*
 * gfx_loader.h - Runtime discovery of native graphics backends.
 *
 * Backends are loaded with dlopen() at runtime. There is NO hard link-time
 * dependency on X11/Wayland/GL/Vulkan, so `ldd build/sober-native` stays
 * clean and the ELF degrades gracefully on headless machines.
 *
 * Owned by worker-3 (task #370).
 */
#ifndef SOBER_GFX_LOADER_H
#define SOBER_GFX_LOADER_H

#include <stddef.h>
#include <stdint.h>

#define SOBER_GFX_X11     0x01u
#define SOBER_GFX_WAYLAND 0x02u
#define SOBER_GFX_OPENGL  0x04u
#define SOBER_GFX_VULKAN  0x08u

/* Idempotent. Returns a bitmask of the backends that were found. */
uint32_t sober_gfx_init(void);
uint32_t sober_gfx_available(void);
void     sober_gfx_shutdown(void);

/* Fill buf with a human-readable list, e.g. "X11 OpenGL Vulkan". */
const char *sober_gfx_describe(uint32_t mask, char *buf, size_t cap);

/* Generic dlopen/dlsym helpers used by the rest of the port. */
void *sober_gfx_open(const char *soname);
void *sober_gfx_symbol(void *lib, const char *sym);
void  sober_gfx_close(void *lib);

#endif /* SOBER_GFX_LOADER_H */
