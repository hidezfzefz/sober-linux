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
#include <elf.h>
#include <link.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#ifndef NT_GNU_BUILD_ID
#define NT_GNU_BUILD_ID 3
#endif

extern void *sober_libc_fn_fflush;
extern void *sober_libc_fn_fwrite;
static unsigned char *g_sf;

static int (*real_fflush_fn)(FILE *);
static size_t (*real_fwrite_fn)(const void *, size_t, size_t, FILE *);

/* Frozen engine identity (BuildID 204ad049...). */
static const unsigned char k_build_id[20] = {
    0x20, 0x4a, 0xd0, 0x49, 0x59, 0x9c, 0x96, 0xaf,
    0xb7, 0x5b, 0x8a, 0x21, 0xd5, 0x05, 0x10, 0x63,
    0xe8, 0x21, 0xd6, 0xc9
};

static int notes_match(const unsigned char *notes, size_t size)
{
    size_t off = 0;
    while (off + sizeof(Elf64_Nhdr) <= size) {
        Elf64_Nhdr nh;
        memcpy(&nh, notes + off, sizeof(nh));
        off += sizeof(nh);
        size_t nsz = nh.n_namesz, dsz = nh.n_descsz;
        size_t np = (nsz + 3u) & ~(size_t)3u, dp = (dsz + 3u) & ~(size_t)3u;
        if (nsz > 4096 || dsz > 4096 || np > size - off ||
            dp > size - off - np)
            return 0;
        if (nh.n_type == NT_GNU_BUILD_ID && nsz >= 4 &&
            memcmp(notes + off, "GNU", 4) == 0 && dsz == sizeof(k_build_id) &&
            memcmp(notes + off + np, k_build_id, sizeof(k_build_id)) == 0)
            return 1;
        off += np + dp;
    }
    return 0;
}

static int find_guest_cb(struct dl_phdr_info *info, size_t size, void *data)
{
    (void)size;
    void **out = (void **)data;
    for (unsigned i = 0; i < info->dlpi_phnum; ++i) {
        if (info->dlpi_phdr[i].p_type != PT_NOTE) continue;
        const unsigned char *n = (const unsigned char *)
            (info->dlpi_addr + info->dlpi_phdr[i].p_vaddr);
        if (notes_match(n, (size_t)info->dlpi_phdr[i].p_memsz)) {
            *out = (void *)info->dlpi_addr;
            return 1;
        }
    }
    return 0;
}

/* Rebind ONLY the two stdio PLT GOT slots the FLog writer uses:
 *   fwrite@plt 0x6d32010 -> GOT 0x7229748
 *   fflush@plt 0x6d32020 -> GOT 0x7229750
 * Both are 8 bytes, 0x7229748/0x7229750, so one page covers them.  After this,
 * the guest's calls land on THIS DSO's versioned fwrite/fflush, whose forwarders
 * we have just pointed at sf_fwrite/sf_fflush; those map the guest's Bionic
 * __sF+0x98/+0x130 addresses to the REAL host stdout/stderr FILE*.  No guest
 * code byte is patched, no guest object is fabricated, and the clean path is
 * untouched (this runs only under the already-gated SOBER_BIONIC_SF_FULL). */
static void bind_guest_stdio_got(void)
{
    void *base = NULL;
    (void)dl_iterate_phdr(find_guest_cb, &base);
    if (!base) {
        fprintf(stderr, "[bionic-sf] guest build-ID match not found; "
                        "stdio GOT left unchanged\n");
        return;
    }
    uintptr_t low = (uintptr_t)base + 0x7229748u;
    uintptr_t high = (uintptr_t)base + 0x7229750u + sizeof(uint64_t);
    long ps = sysconf(_SC_PAGESIZE);
    if (ps <= 0) ps = 4096;
    uintptr_t start = low & ~(uintptr_t)(ps - 1);
    uintptr_t end = (high + (uintptr_t)(ps - 1)) & ~(uintptr_t)(ps - 1);
    if (mprotect((void *)start, (size_t)(end - start),
                 PROT_READ | PROT_WRITE) != 0) {
        fprintf(stderr, "[bionic-sf] mprotect(RW) of stdio GOT page failed\n");
        return;
    }
    /* Resolve OUR OWN versioned definitions, not `&fflush`/`&fwrite`: the host
     * executable is built -rdynamic and exports the same names, so a plain
     * address-of inside this DSO is interposed to the host/glibc copy and the
     * "rebind" would leave the guest GOT pointing at glibc (before == after). */
    void *stub = dlopen("libc.so", RTLD_NOLOAD | RTLD_LAZY);
    void *our_write = stub ? dlvsym(stub, "fwrite", "LIBC") : NULL;
    void *our_flush = stub ? dlvsym(stub, "fflush", "LIBC") : NULL;
    if (!our_write || !our_flush) {
        fprintf(stderr, "[bionic-sf] versioned fwrite/fflush@@LIBC unavailable; "
                        "stdio GOT left unchanged\n");
        (void)mprotect((void *)start, (size_t)(end - start), PROT_READ);
        return;
    }
    uint64_t *gw = (uint64_t *)low;
    uint64_t *gf = (uint64_t *)((uintptr_t)base + 0x7229750u);
    uint64_t old_w = *gw, old_f = *gf;
    *gw = (uint64_t)(uintptr_t)our_write;
    *gf = (uint64_t)(uintptr_t)our_flush;
    (void)mprotect((void *)start, (size_t)(end - start), PROT_READ);
    fprintf(stderr,
            "[bionic-sf] stdio GOT bound: fwrite 0x7229748 %#lx -> %p; "
            "fflush 0x7229750 %#lx -> %p (guest base %p)\n",
            (unsigned long)old_w, (void *)(uintptr_t)*gw,
            (unsigned long)old_f, (void *)(uintptr_t)*gf, base);
}

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
    /* n5 A/B: unconditional host-only banner showing switch state. */
    fprintf(stderr, "[N6_BANNER] SOBER_BIONIC_SF_FULL=%s\n",
            getenv("SOBER_BIONIC_SF_FULL") ? "1" : "UNSET");
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
    /* Make the bridge reachable: by default the guest's stdio PLT GOT slots
     * resolve to glibc, so __sF+0x130 reaches _IO_fflush with a zeroed Bionic
     * FILE* (_lock == NULL) and faults.  Rebind those two slots to our
     * versioned stubs, which now forward into sf_fflush/sf_fwrite above. */
    bind_guest_stdio_got();
}
