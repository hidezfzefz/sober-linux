/*
 * bionic_sf_full.c - opt-in Bionic __sF / stdio ABI bridge (task #463 follow-up).
 *
 * The guest derives its stdout/stderr FILE* as __sF+0x98 and __sF+0x130,
 * assuming Bionic's `FILE __sF[3]` (0x198 bytes).  Our generated libc stub
 * exports __sF as a 64-byte object, so those pointers land in zeroed memory
 * and glibc's _IO_fflush faults reading the NULL _lock at FILE+0x88.
 *
 * This module widens the object (see gen_bionic_libc.py / bionic_libc.c) and,
 * when SOBER_BIONIC_SF_FULL is set, redirects fflush/fwrite on those two
 * guest stream addresses to the real host stdout/stderr.  No guest bytes are
 * patched, no callback is fabricated, and the decision-013 gate is untouched.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

extern void *sober_libc_fn_fflush;
extern void *sober_libc_fn_fwrite;
static unsigned char *g_sf;

static int (*real_fflush_fn)(FILE *);
static size_t (*real_fwrite_fn)(const void *, size_t, size_t, FILE *);

static FILE *host_stream_for(const void *p)
{
    uintptr_t addr = (uintptr_t)p;
    uintptr_t base = (uintptr_t)g_sf;
    if (addr == base + 0x098u) return stdout;   /* Bionic stdout */
    if (addr == base + 0x130u) return stderr;   /* Bionic stderr */
    if (addr == base)          return stdin;    /* Bionic stdin  */
    return NULL;
}

static int sf_fflush(FILE *p)
{
    FILE *h = host_stream_for(p);
    if (h) p = h;
    if (real_fflush_fn) return real_fflush_fn(p);
    return fflush(p);
}

static size_t sf_fwrite(const void *buf, size_t size, size_t n, FILE *p)
{
    FILE *h = host_stream_for(p);
    if (getenv("SOBER_STDIO_DEBUG"))
        fprintf(stderr, "[stdio] sf_fwrite(%p,%zu,%zu,stream=%p) -> %p\n",
                buf, size, n, (void *)p, (void *)(h ? h : p));
    if (h) p = h;
    if (real_fwrite_fn) return real_fwrite_fn(buf, size, n, p);
    return fwrite(buf, size, n, p);
}

__attribute__((visibility("default")))
void sober_bionic_sf_full_install(unsigned char *sf_base)
{
    if (!getenv("SOBER_BIONIC_SF_FULL")) return;
    g_sf = sf_base;
    real_fflush_fn = (int (*)(FILE *))
        dlvsym(RTLD_DEFAULT, "fflush", "GLIBC_2.2.5");
    real_fwrite_fn = (size_t (*)(const void *, size_t, size_t, FILE *))
        dlvsym(RTLD_DEFAULT, "fwrite", "GLIBC_2.2.5");
    sober_libc_fn_fflush = (void *)sf_fflush;
    sober_libc_fn_fwrite = (void *)sf_fwrite;
    fprintf(stderr, "[bionic-sf] full __sF stdio bridge installed "
                    "(host __sF=%p, size=0x200)\n", (void *)g_sf);
}
