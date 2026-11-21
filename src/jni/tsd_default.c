/*
 * tsd_default.c - DEFAULT-ON host binding of the guest's Bionic TSD namespace.
 *
 * Owner: Team 1 (substrate) / agent T1-1.  NEW FILE — no existing file edited.
 * See tsd_default.h for the root-cause explanation (decisions 013/038).
 *
 * The only guest writes performed here are the two approved GOT rebinds:
 *   0x72298b0 <- pthread_getspecific@@LIBC
 *   0x72295c8 <- pthread_setspecific@@LIBC
 * The canary global 0x72293f8 is snapshotted before/after and never written.
 */
#define _GNU_SOURCE
#include "tsd_default.h"

#include <dlfcn.h>
#include <elf.h>
#include <link.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#ifndef NT_GNU_BUILD_ID
#define NT_GNU_BUILD_ID 3
#endif

/* 20-byte NT_GNU_BUILD_ID of the frozen engine (hex in tsd_default.h). */
static const unsigned char k_expected_build_id[20] = {
    0x20, 0x4a, 0xd0, 0x49, 0x59, 0x9c, 0x96, 0xaf,
    0xb7, 0x5b, 0x8a, 0x21, 0xd5, 0x05, 0x10, 0x63,
    0xe8, 0x21, 0xd6, 0xc9
};

static volatile int g_applied;      /* idempotence guard                     */
static void *g_get_fn;              /* resolved pthread_getspecific@@LIBC    */
static void *g_set_fn;              /* resolved pthread_setspecific@@LIBC    */

/* ------------------------------------------------------------------ */
/* enable / state                                                      */
/* ------------------------------------------------------------------ */

int sober_tsd_default_enabled(void)
{
    const char *v = getenv(SOBER_TSD_DEFAULT_ENV);
    /* Default ON when wired.  The documented kill switch is exactly "=0". */
    return !(v && v[0] == '0' && v[1] == '\0');
}

int sober_tsd_default_is_applied(void)
{
    return g_applied ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* build-ID verification (read-only)                                   */
/* ------------------------------------------------------------------ */

/* Scan a PT_NOTE segment for NT_GNU_BUILD_ID == k_expected_build_id. */
static int notes_match_build_id(const unsigned char *notes, size_t size)
{
    size_t off = 0;
    while (off + sizeof(Elf64_Nhdr) <= size) {
        Elf64_Nhdr nh;
        memcpy(&nh, notes + off, sizeof(nh));
        off += sizeof(Elf64_Nhdr);

        size_t namesz = (size_t)nh.n_namesz;
        size_t descsz = (size_t)nh.n_descsz;
        size_t name_pad = (namesz + 3u) & ~(size_t)3u;
        size_t desc_pad = (descsz + 3u) & ~(size_t)3u;
        if (namesz > 4096 || descsz > 4096 ||
            name_pad > size - off || desc_pad > size - off - name_pad)
            return 0;

        const unsigned char *name = notes + off;
        const unsigned char *desc = name + name_pad;
        if (nh.n_type == NT_GNU_BUILD_ID && namesz >= 4u &&
            memcmp(name, "GNU", 4) == 0 && descsz == sizeof(k_expected_build_id) &&
            memcmp(desc, k_expected_build_id, sizeof(k_expected_build_id)) == 0)
            return 1;

        off += name_pad + desc_pad;
    }
    return 0;
}

int sober_tsd_default_verify_build(void *guest_base)
{
    if (!guest_base) return 0;
    const unsigned char *b = (const unsigned char *)guest_base;

    if (b[EI_MAG0] != ELFMAG0 || b[EI_MAG1] != ELFMAG1 ||
        b[EI_MAG2] != ELFMAG2 || b[EI_MAG3] != ELFMAG3 ||
        b[EI_CLASS] != ELFCLASS64 || b[EI_DATA] != ELFDATA2LSB)
        return 0;

    Elf64_Ehdr eh;
    memcpy(&eh, b, sizeof(eh));
    if (eh.e_phentsize < sizeof(Elf64_Phdr) || eh.e_phnum == 0 ||
        eh.e_phnum > 4096)
        return 0;

    for (unsigned i = 0; i < eh.e_phnum; ++i) {
        Elf64_Phdr ph;
        memcpy(&ph, b + eh.e_phoff + (size_t)i * eh.e_phentsize, sizeof(ph));
        if (ph.p_type == PT_NOTE && ph.p_memsz >= sizeof(Elf64_Nhdr))
            if (notes_match_build_id(b + ph.p_vaddr, (size_t)ph.p_memsz))
                return 1;
    }
    return 0;
}

/* dl_iterate_phdr callback context. */
struct find_ctx {
    void *base;
};

static int find_guest_cb(struct dl_phdr_info *info, size_t size, void *data)
{
    (void)size;                       /* struct may grow; we use the fixed part */
    struct find_ctx *ctx = (struct find_ctx *)data;
    if (!info->dlpi_name || strstr(info->dlpi_name, "libroblox.so") == NULL)
        return 0;
    for (unsigned i = 0; i < info->dlpi_phnum; ++i) {
        if (info->dlpi_phdr[i].p_type != PT_NOTE) continue;
        const unsigned char *notes =
            (const unsigned char *)(info->dlpi_addr + info->dlpi_phdr[i].p_vaddr);
        if (notes_match_build_id(notes, (size_t)info->dlpi_phdr[i].p_memsz)) {
            ctx->base = (void *)info->dlpi_addr;
            return 1;                 /* stop iteration */
        }
    }
    return 0;
}

void *sober_tsd_default_find_guest(void)
{
    struct find_ctx ctx = { NULL };
    (void)dl_iterate_phdr(find_guest_cb, &ctx);
    return ctx.base;
}

/* ------------------------------------------------------------------ */
/* versioned LIBC stub resolution                                      */
/* ------------------------------------------------------------------ */

/* The stub DSO is build/ndk-stubs/libc.so (SONAME "libc.so").  RTLD_NOLOAD
 * requires it to already be in the guest's link map — dlopen() reached it via
 * the guest's DT_NEEDED.  Matches android_relocs.c's approved rebind path. */
static void *open_stub(void)
{
    return dlopen("libc.so", RTLD_NOLOAD | RTLD_LAZY);
}

static int resolve_stub_tsd(void **out_get, void **out_set)
{
    void *stub = open_stub();
    if (!stub) return -1;
    void *get = dlvsym(stub, "pthread_getspecific", "LIBC");
    void *set = dlvsym(stub, "pthread_setspecific", "LIBC");
    if (!get || !set) return -1;
    if (out_get) *out_get = get;
    if (out_set) *out_set = set;
    return 0;
}

/* ------------------------------------------------------------------ */
/* GOT write with page protection restore                              */
/* ------------------------------------------------------------------ */

static long page_size(void)
{
    long p = sysconf(_SC_PAGESIZE);
    return p > 0 ? p : 4096L;
}

/* Read the original protection of the page containing `addr` from the kernel. */
static int protection_of(uintptr_t addr, int *out_prot)
{
    FILE *f = fopen("/proc/self/maps", "r");
    if (!f) return -1;
    char line[512];
    int rc = -1;
    while (fgets(line, sizeof(line), f)) {
        unsigned long lo = 0, hi = 0;
        char perms[8] = { 0 };
        if (sscanf(line, "%lx-%lx %7s", &lo, &hi, perms) == 3 &&
            (uintptr_t)lo <= addr && addr < (uintptr_t)hi) {
            int p = 0;
            if (perms[0] == 'r') p |= PROT_READ;
            if (perms[1] == 'w') p |= PROT_WRITE;
            if (perms[2] == 'x') p |= PROT_EXEC;
            *out_prot = p;
            rc = 0;
            break;
        }
    }
    fclose(f);
    return rc;
}

/* The two GOT slots lie 0x2e8 apart, hence in one 4K page.  Compute the
 * covering page range generically anyway, make it RW, and report old prot. */
static int got_make_writable(uintptr_t base, uintptr_t *start_out,
                             size_t *len_out, int *old_prot_out)
{
    const long ps = page_size();
    const uintptr_t psz = (uintptr_t)ps;
    uintptr_t a = base + SOBER_TSD_GOT_GETSPECIFIC_RVA;
    uintptr_t b = base + SOBER_TSD_GOT_SETSPECIFIC_RVA;
    uintptr_t lo = a < b ? a : b;
    uintptr_t hi = (a > b ? a : b) + sizeof(uint64_t);

    uintptr_t start = lo & ~(psz - 1u);
    uintptr_t end = (hi + (psz - 1u)) & ~(psz - 1u);

    if (protection_of(start, old_prot_out) != 0)
        *old_prot_out = PROT_READ;          /* conservative default (RELRO) */
    if (mprotect((void *)start, end - start, PROT_READ | PROT_WRITE) != 0)
        return -1;

    *start_out = start;
    *len_out = (size_t)(end - start);
    return 0;
}

/* ------------------------------------------------------------------ */
/* apply                                                               */
/* ------------------------------------------------------------------ */

int sober_tsd_default_apply(void *guest_base)
{
    if (!sober_tsd_default_enabled())
        return SOBER_TSD_DEFAULT_ERR_DISABLED;   /* kill switch: clean no-op */
    if (g_applied)
        return SOBER_TSD_DEFAULT_OK;             /* idempotent */
    if (!guest_base)
        return SOBER_TSD_DEFAULT_ERR_NO_GUEST;
    if (!sober_tsd_default_verify_build(guest_base)) {
        fprintf(stderr, "[tsd-default] build-ID mismatch; refusing GOT rebind\n");
        return SOBER_TSD_DEFAULT_ERR_BUILD;
    }
    if (resolve_stub_tsd(&g_get_fn, &g_set_fn) != 0) {
        fprintf(stderr,
                "[tsd-default] versioned libc.so stub or TSD symbols unavailable: %s\n",
                dlerror() ? dlerror() : "not loaded");
        return SOBER_TSD_DEFAULT_ERR_STUB;
    }

    const uintptr_t base = (uintptr_t)guest_base;
    const uintptr_t canary_addr = base + SOBER_TSD_CANARY_GLOBAL_RVA;
    const uintptr_t canary_before = *(volatile uintptr_t *)canary_addr;

    uintptr_t page_start = 0;
    size_t page_len = 0;
    int old_prot = PROT_READ;
    if (got_make_writable(base, &page_start, &page_len, &old_prot) != 0) {
        fprintf(stderr, "[tsd-default] mprotect(RW) of GOT page failed\n");
        return SOBER_TSD_DEFAULT_ERR_PROTECT;
    }

    uint64_t *got_get = (uint64_t *)(base + SOBER_TSD_GOT_GETSPECIFIC_RVA);
    uint64_t *got_set = (uint64_t *)(base + SOBER_TSD_GOT_SETSPECIFIC_RVA);
    const uintptr_t before_get = (uintptr_t)*got_get;
    const uintptr_t before_set = (uintptr_t)*got_set;

    *got_get = (uint64_t)(uintptr_t)g_get_fn;
    *got_set = (uint64_t)(uintptr_t)g_set_fn;

    if (mprotect((void *)page_start, page_len, old_prot) != 0)
        fprintf(stderr, "[tsd-default] WARNING: failed to restore GOT page protection\n");

    const uintptr_t canary_after = *(volatile uintptr_t *)canary_addr;
    if (canary_after != canary_before) {
        fprintf(stderr,
                "[tsd-default] FATAL: canary 0x72293f8 changed (%#lx -> %#lx)\n",
                (unsigned long)canary_before, (unsigned long)canary_after);
        return SOBER_TSD_DEFAULT_ERR_CANARY;
    }

    g_applied = 1;
    fprintf(stderr,
            "[tsd-default] Bionic TSD GOT bound (default-on): "
            "get 0x72298b0 %#lx -> %p, set 0x72295c8 %#lx -> %p; "
            "canary 0x72293f8 == %#lx (unchanged)\n",
            (unsigned long)before_get, g_get_fn,
            (unsigned long)before_set, g_set_fn,
            (unsigned long)canary_after);
    return SOBER_TSD_DEFAULT_OK;
}

int sober_tsd_default_install(void)
{
    if (!sober_tsd_default_enabled())
        return SOBER_TSD_DEFAULT_ERR_DISABLED;
    void *base = (void *)sober_tsd_default_find_guest();
    if (!base)
        return SOBER_TSD_DEFAULT_ERR_NO_GUEST;
    return sober_tsd_default_apply(base);
}

/* ------------------------------------------------------------------ */
/* self-test / diagnostics (read-only)                                 */
/* ------------------------------------------------------------------ */

/* Read a u32 key global and report whether it is a valid Bionic key and
 * whether it matches the build's expected value. */
static uint32_t read_key(void *base, uintptr_t rva, int *in_range)
{
    uint32_t key = 0;
    memcpy(&key, (const unsigned char *)base + rva, sizeof(key));
    *in_range = (key < SOBER_TSD_BIONIC_SLOTS);
    return key;
}

int sober_tsd_default_diagnose(void *guest_base, char *buf, size_t buf_len)
{
    if (buf && buf_len) buf[0] = '\0';
    char scratch[1024];
    const char *out = scratch;
    int healthy = 1;

    void *base = guest_base;
    if (!base) base = sober_tsd_default_find_guest();
    if (!base) {
        healthy = 0;
        snprintf(scratch, sizeof(scratch),
                 "tsd-default: guest not found in link map (enabled=%d)\n",
                 sober_tsd_default_enabled());
        goto done;
    }

    const int build_ok = sober_tsd_default_verify_build(base);
    if (!build_ok) healthy = 0;

    void *get = NULL, *set = NULL;
    const int stub_ok = (resolve_stub_tsd(&get, &set) == 0);
    if (!stub_ok) healthy = 0;

    int k2_ok = 0, k3_ok = 0;
    uint32_t k2 = read_key(base, SOBER_TSD_KEY2_GLOBAL_RVA, &k2_ok);
    uint32_t k3 = read_key(base, SOBER_TSD_KEY3_GLOBAL_RVA, &k3_ok);
    const int k2_exp = (k2 == 2u);
    const int k3_exp = (k3 == 3u);
    if (!k2_ok || !k2_exp || !k3_ok || !k3_exp) healthy = 0;

    const uintptr_t b = (uintptr_t)base;
    const uintptr_t got_get = *(volatile uintptr_t *)(b + SOBER_TSD_GOT_GETSPECIFIC_RVA);
    const uintptr_t got_set = *(volatile uintptr_t *)(b + SOBER_TSD_GOT_SETSPECIFIC_RVA);
    const uintptr_t canary = *(volatile uintptr_t *)(b + SOBER_TSD_CANARY_GLOBAL_RVA);

    snprintf(scratch, sizeof(scratch),
             "tsd-default: enabled=%d applied=%d build_id=%s\n"
             "  stub=%s get@@LIBC=%p set@@LIBC=%p\n"
             "  GOT get(0x%x)=%#lx %s stub | set(0x%x)=%#lx %s stub\n"
             "  key2(0x%x)=%u (valid=%d expect2=%d) "
             "key3(0x%x)=%u (valid=%d expect3=%d)\n"
             "  canary(0x%x)=%#lx (never written)\n",
             sober_tsd_default_enabled(), sober_tsd_default_is_applied(),
             build_ok ? "204ad049 MATCH" : "MISMATCH",
             stub_ok ? "OK" : "UNAVAILABLE", get, set,
             SOBER_TSD_GOT_GETSPECIFIC_RVA, (unsigned long)got_get,
             (stub_ok && got_get == (uintptr_t)get) ? "==" : "!=",
             SOBER_TSD_GOT_SETSPECIFIC_RVA, (unsigned long)got_set,
             (stub_ok && got_set == (uintptr_t)set) ? "==" : "!=",
             SOBER_TSD_KEY2_GLOBAL_RVA, k2, k2_ok, k2_exp,
             SOBER_TSD_KEY3_GLOBAL_RVA, k3, k3_ok, k3_exp,
             SOBER_TSD_CANARY_GLOBAL_RVA, (unsigned long)canary);

done:
    if (buf && buf_len) {
        size_t n = strlen(out);
        if (n >= buf_len) n = buf_len - 1;
        memcpy(buf, out, n);
        buf[n] = '\0';
    } else {
        fputs(out, stderr);
    }
    return healthy ? SOBER_TSD_DEFAULT_HEALTHY : SOBER_TSD_DEFAULT_UNHEALTHY;
}

int sober_tsd_default_selftest(char *buf, size_t buf_len)
{
    char scratch[512];
    void *get = NULL, *set = NULL;
    int rc = resolve_stub_tsd(&get, &set);
    snprintf(scratch, sizeof(scratch),
             "tsd-default selftest: %s get@@LIBC=%p set@@LIBC=%p "
             "(no guest memory touched)\n",
             rc == 0 ? "PASS" : "FAIL (libc.so stub / symbols absent)", get, set);
    if (buf && buf_len) {
        size_t n = strlen(scratch);
        if (n >= buf_len) n = buf_len - 1;
        memcpy(buf, scratch, n);
        buf[n] = '\0';
    } else {
        fputs(scratch, stderr);
    }
    return rc == 0 ? SOBER_TSD_DEFAULT_HEALTHY : SOBER_TSD_DEFAULT_UNHEALTHY;
}

int sober_tsd_default_key_management_status(char *buf, size_t buf_len)
{
    const char *msg =
        "tsd-default: GOT 0x72297a0 (key_create) / 0x722a2c8 (key_delete) "
        "are deliberately NOT rebound. The versioned libc.so stub forwards "
        "pthread_key_create/pthread_key_delete to glibc, whose key space is "
        "unrelated to the guest's preallocated Bionic keys (2 @ 0x7282000, "
        "3 @ 0x72a1b08); it cannot provide native Bionic semantics. "
        "The stub's get/setspecific private TSD array is indexed directly by "
        "those keys, so key create/delete are not needed for this fix.\n";
    if (buf && buf_len) {
        size_t n = strlen(msg);
        if (n >= buf_len) n = buf_len - 1;
        memcpy(buf, msg, n);
        buf[n] = '\0';
    } else {
        fputs(msg, stderr);
    }
    return SOBER_TSD_DEFAULT_OK;
}
