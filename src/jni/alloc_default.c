/*
 * alloc_default.c - DEFAULT-ON allocator-contract wrappers (substrate T1-5).
 *
 * Evidence: analysis/A03-fflag-loaded-state.md, analysis/A07-allocator-audit.md;
 * direct disassembly of the frozen engine libroblox.so BuildID 204ad049...,
 * SHA 3e498d6494...
 *   F1: guest core 0x231f13a writes 0 at 0x231f1ac/0x231f243; the old
 *       bionic_bootstrap.c:299 bridge wrote `total` into the 16-byte header.
 *   F2: raw calloc 0x253225e returns a plain (headerless) block.
 *   F3: guest free thunk 0x231f53a subtracts 0x10; the generic bridges did not.
 *
 * Constraints honoured: NEW files only; no CMake/engine edits; no guest byte
 * patches; no callbacks; no decision-013 marker changes; C17, -Werror clean.
 *
 * Registers header-form `base` pointers so free/realloc/usable_size can apply
 * the -0x10 correction only to pointers that really came from the header
 * calloc.  Plain host pointers are left untouched.  The guest's own 0x231f53a
 * thunk already subtracts 0x10 and hands us `base`; because we register `base`
 * (not base+0x10), that path also lands on the right glibc pointer.
 *
 * DEFAULT-ON: all pointers are supplied unless getenv("SOBER_ALLOC_DEFAULT")
 * is exactly "0" (kill switch).  The gate is checked by
 * sober_alloc_default_ops() at install time; the wrappers are pure.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "alloc_default.h"

#include <malloc.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- header/base registry ------------------------------------------------ *
 * Open-addressed set with tombstones, so removals do not break probe chains.
 * 0 is a safe EMPTY sentinel (malloc never returns NULL on success) and
 * UINTPTR_MAX is a safe TOMB sentinel (never a user-space pointer).
 * Every slot is atomic: the guest can call the bridges from worker threads.
 */
#define SOBER_ALLOC_DEFAULT_REG_BITS 13u
#define SOBER_ALLOC_DEFAULT_REG_SIZE (1u << SOBER_ALLOC_DEFAULT_REG_BITS)
#define SOBER_ALLOC_DEFAULT_REG_MASK \
    ((size_t)(SOBER_ALLOC_DEFAULT_REG_SIZE - 1u))
#define SOBER_ALLOC_DEFAULT_REG_EMPTY ((uintptr_t)0u)
#define SOBER_ALLOC_DEFAULT_REG_TOMB  ((uintptr_t)UINTPTR_MAX)
#define SOBER_ALLOC_DEFAULT_REG_NPOS  ((size_t)-1)

static _Atomic uintptr_t s_reg[SOBER_ALLOC_DEFAULT_REG_SIZE];

static _Atomic uint64_t s_header_allocs;
static _Atomic uint64_t s_plain_allocs;
static _Atomic uint64_t s_registry_hits;
static _Atomic uint64_t s_registry_misses;
static _Atomic uint64_t s_realloc_moves;
static _Atomic uint64_t s_reg_full;

static void bump(_Atomic uint64_t *c)
{
    atomic_fetch_add_explicit(c, 1u, memory_order_relaxed);
}

static size_t reg_hash(uintptr_t p)
{
    /* 64-bit mix (MurmurHash3 finalizer); pointer alignment >= 16 so shift
     * away the low four bits before mixing. */
    uint64_t x = (uint64_t)p >> 4;
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return (size_t)x & SOBER_ALLOC_DEFAULT_REG_MASK;
}

static int reg_insert(uintptr_t base)
{
    if (!base || base == SOBER_ALLOC_DEFAULT_REG_TOMB) return 0;

    /* The outer retry handles a race where a tombstone we intend to reuse is
     * claimed by another thread inserting a different pointer: rescan. */
    for (size_t attempt = 0; attempt < SOBER_ALLOC_DEFAULT_REG_SIZE; ++attempt) {
        size_t idx = reg_hash(base);
        size_t first_tomb = SOBER_ALLOC_DEFAULT_REG_NPOS;
        for (size_t k = 0; k < SOBER_ALLOC_DEFAULT_REG_SIZE; ++k) {
            uintptr_t v = atomic_load_explicit(&s_reg[idx],
                                               memory_order_acquire);
            if (v == base) return 1;
            if (v == SOBER_ALLOC_DEFAULT_REG_EMPTY) {
                size_t target = (first_tomb != SOBER_ALLOC_DEFAULT_REG_NPOS)
                                ? first_tomb : idx;
                uintptr_t expect = (target == idx)
                                   ? SOBER_ALLOC_DEFAULT_REG_EMPTY
                                   : atomic_load_explicit(&s_reg[target],
                                                          memory_order_acquire);
                if (expect != SOBER_ALLOC_DEFAULT_REG_EMPTY &&
                    expect != SOBER_ALLOC_DEFAULT_REG_TOMB)
                    break; /* tombstone was taken; rescan */
                if (atomic_compare_exchange_strong_explicit(
                        &s_reg[target], &expect, base,
                        memory_order_release, memory_order_acquire))
                    return 1;
                if (expect == base) return 1;
                break; /* another thread raced us; rescan */
            }
            if (v == SOBER_ALLOC_DEFAULT_REG_TOMB &&
                first_tomb == SOBER_ALLOC_DEFAULT_REG_NPOS)
                first_tomb = idx;
            idx = (idx + 1u) & SOBER_ALLOC_DEFAULT_REG_MASK;
        }
    }
    bump(&s_reg_full);
    return 0;
}

static int reg_contains(uintptr_t base)
{
    if (!base) return 0;
    size_t idx = reg_hash(base);
    for (size_t k = 0; k < SOBER_ALLOC_DEFAULT_REG_SIZE; ++k) {
        uintptr_t v = atomic_load_explicit(&s_reg[idx], memory_order_acquire);
        if (v == base) return 1;
        if (v == SOBER_ALLOC_DEFAULT_REG_EMPTY) return 0;
        idx = (idx + 1u) & SOBER_ALLOC_DEFAULT_REG_MASK;
    }
    return 0;
}

static void reg_remove(uintptr_t base)
{
    if (!base) return;
    size_t idx = reg_hash(base);
    for (size_t k = 0; k < SOBER_ALLOC_DEFAULT_REG_SIZE; ++k) {
        uintptr_t v = atomic_load_explicit(&s_reg[idx], memory_order_acquire);
        if (v == base) {
            uintptr_t expect = base;
            (void)atomic_compare_exchange_strong_explicit(
                &s_reg[idx], &expect, SOBER_ALLOC_DEFAULT_REG_TOMB,
                memory_order_release, memory_order_acquire);
            return;
        }
        if (v == SOBER_ALLOC_DEFAULT_REG_EMPTY) return;
        idx = (idx + 1u) & SOBER_ALLOC_DEFAULT_REG_MASK;
    }
}

/* ---- plain allocation bridges (Contract A) ------------------------------ */
void *sober_alloc_default_allocate(size_t n)
{
    if (!n) n = 1u;
    void *p = malloc(n);
    if (p) memset(p, 0, n);
    return p;
}

void *sober_alloc_default_alloc_small(size_t n)
{
    return sober_alloc_default_allocate(n);
}

void *sober_alloc_default_aligned_alloc(size_t alignment, size_t n)
{
    if (!n) n = 1u;
    if (!alignment) alignment = sizeof(void *);
    size_t a = 1u;
    while (a < alignment && a < (SIZE_MAX >> 1)) a <<= 1u;
    if (!a) a = sizeof(void *);
    size_t span = ((n + a - 1u) / a) * a; /* glibc wants size multiple of align */
    void *p = aligned_alloc(a, span);
    if (p) memset(p, 0, n);
    return p;
}

/* ---- F1: header-form calloc (Contract B) -------------------------------- */
void *sober_alloc_default_calloc_header(size_t nmemb, size_t size)
{
    if (!nmemb) nmemb = 1u;
    if (!size) size = 1u;
    if (nmemb > SIZE_MAX / size) return NULL;
    size_t n = nmemb * size;
    if (n > SIZE_MAX - SOBER_ALLOC_DEFAULT_HEADER_SIZE) return NULL;
    size_t total = n + SOBER_ALLOC_DEFAULT_HEADER_SIZE;

    unsigned char *base = (unsigned char *)calloc(1u, total);
    if (!base) return NULL;

    /* F1: the guest's 0x231f13a core stores ZERO here (0x231f1ac xor,
     * 0x231f243 mov).  It is a 0-vs-custom-handle discriminator for the free
     * thunk 0x231f53a, NOT the allocation size.  Writing `total` here (the old
     * bionic_bootstrap.c:299 behaviour) makes the special-free path 0x6a28bf4
     * dereference a near-zero "handle" when both feature flags are set. */
    memset(base, 0, SOBER_ALLOC_DEFAULT_HEADER_SIZE);

    if (!reg_insert((uintptr_t)base)) {
        /* Registry exhausted: handing back an unregistered base+0x10 would
         * make a later free/realloc misclassify it.  Fail closed. */
        free(base);
        return NULL;
    }
    bump(&s_header_allocs);
    return base + SOBER_ALLOC_DEFAULT_HEADER_SIZE;
}

/* ---- F2: plain-pointer (headerless) calloc ------------------------------- */
void *sober_alloc_default_calloc_plain(size_t nmemb, size_t size)
{
    /* Guest 0x253225e tail-jumps to memset@plt at 0x25322ea and returns the
     * arena block itself.  No header, no +0x10, no registry entry. */
    if (!nmemb) nmemb = 1u;
    if (!size) size = 1u;
    void *p = calloc(nmemb, size);
    if (p) bump(&s_plain_allocs);
    return p;
}

/* ---- F3: header-aware free ---------------------------------------------- */
void sober_alloc_default_free(void *p)
{
    if (!p) return;
    uintptr_t base = (uintptr_t)p
                     - (uintptr_t)SOBER_ALLOC_DEFAULT_HEADER_SIZE;
    if (reg_contains(base)) {
        reg_remove(base);
        bump(&s_registry_hits);
        free((void *)base);
        return;
    }
    /* Plain pointer, or `base` already subtracted by the guest thunk
     * 0x231f53a (we registered `base`, so p-0x10 is not in the set). */
    bump(&s_registry_misses);
    free(p);
}

/* ---- F3: header-aware realloc ------------------------------------------- */
void *sober_alloc_default_realloc(void *p, size_t n)
{
    if (!n) n = 1u;
    uintptr_t base = p ? (uintptr_t)p
                         - (uintptr_t)SOBER_ALLOC_DEFAULT_HEADER_SIZE : 0u;
    int header = (p != NULL) && reg_contains(base);
    uintptr_t alloc_base = header ? base : (uintptr_t)p;

    if (header && n > SIZE_MAX - SOBER_ALLOC_DEFAULT_HEADER_SIZE) return NULL;
    size_t req = header ? n + SOBER_ALLOC_DEFAULT_HEADER_SIZE : n;

    size_t old_usable = alloc_base ? malloc_usable_size((void *)alloc_base) : 0u;
    void *q = realloc((void *)alloc_base, req);
    if (!q) return NULL;

    if (!header) {
        if (n > old_usable)
            memset((unsigned char *)q + old_usable, 0, n - old_usable);
        return q;
    }

    /* Keep the block in Contract-B form so the guest's -0x10 free thunk
     * continues to work after a grow/shrink. */
    reg_remove(base);
    memset(q, 0, SOBER_ALLOC_DEFAULT_HEADER_SIZE); /* F1 again */
    if (!reg_insert((uintptr_t)q)) {
        free(q);
        return NULL;
    }
    if ((uintptr_t)q != base) bump(&s_realloc_moves);

    size_t new_usable = malloc_usable_size(q);
    size_t old_payload = old_usable > SOBER_ALLOC_DEFAULT_HEADER_SIZE
                         ? old_usable - SOBER_ALLOC_DEFAULT_HEADER_SIZE : 0u;
    size_t new_payload = new_usable > SOBER_ALLOC_DEFAULT_HEADER_SIZE
                         ? new_usable - SOBER_ALLOC_DEFAULT_HEADER_SIZE : 0u;
    if (n > old_payload && new_payload > old_payload) {
        size_t z = n - old_payload;
        size_t avail = new_payload - old_payload;
        if (z > avail) z = avail;
        memset((unsigned char *)q + SOBER_ALLOC_DEFAULT_HEADER_SIZE
               + old_payload, 0, z);
    }
    return (unsigned char *)q + SOBER_ALLOC_DEFAULT_HEADER_SIZE;
}

/* ---- F3: header-aware usable_size --------------------------------------- */
size_t sober_alloc_default_usable_size(void *p)
{
    if (!p) return 0u;
    uintptr_t base = (uintptr_t)p
                     - (uintptr_t)SOBER_ALLOC_DEFAULT_HEADER_SIZE;
    if (reg_contains(base)) {
        bump(&s_registry_hits);
        size_t u = malloc_usable_size((void *)base);
        /* The 16 bytes at base are the header, not guest payload. */
        return u > SOBER_ALLOC_DEFAULT_HEADER_SIZE
               ? u - SOBER_ALLOC_DEFAULT_HEADER_SIZE : 0u;
    }
    bump(&s_registry_misses);
    return malloc_usable_size(p);
}

/* ---- installable pointer table ------------------------------------------ */
static const struct sober_alloc_default_ops s_ops = {
    sober_alloc_default_allocate,
    sober_alloc_default_alloc_small,
    sober_alloc_default_free,
    sober_alloc_default_usable_size,
    sober_alloc_default_realloc,
    sober_alloc_default_aligned_alloc,
    sober_alloc_default_calloc_plain,
    sober_alloc_default_calloc_header,
};

int sober_alloc_default_enabled(void)
{
    const char *e = getenv(SOBER_ALLOC_DEFAULT_ENV);
    /* DEFAULT ON: only the exact string "0" kills it. */
    return !(e != NULL && strcmp(e, "0") == 0);
}

const struct sober_alloc_default_ops *sober_alloc_default_ops(void)
{
    return sober_alloc_default_enabled() ? &s_ops : NULL;
}

const char *sober_alloc_default_env_vars(void)
{
    return SOBER_ALLOC_DEFAULT_ENV
           "=0 (kill switch; default ON - supplies correct host pointers for "
           "the 8 bridges + slot 0x726c0c0; no guest byte patches, no callbacks)";
}

sober_alloc_default_calloc_fn sober_alloc_default_slot_target(void)
{
    return sober_alloc_default_calloc_header;
}

/* ---- diagnostics -------------------------------------------------------- */
const char *sober_alloc_default_describe(char *buf, size_t cap)
{
    if (!buf || !cap) return "";
    snprintf(buf, cap,
             "enabled=%d ops=%p header_allocs=%llu plain_allocs=%llu "
             "hits=%llu misses=%llu moves=%llu reg_full=%llu "
             "alloc=%#x alloc_small=%#x free=%#x usable=%#x realloc=%#x "
             "aligned=%#x calloc_plain=%#x calloc_header=%#x slot=%#x "
             "env=%s",
             sober_alloc_default_enabled(),
             (void *)sober_alloc_default_ops(),
             (unsigned long long)atomic_load_explicit(&s_header_allocs,
                                                      memory_order_relaxed),
             (unsigned long long)atomic_load_explicit(&s_plain_allocs,
                                                      memory_order_relaxed),
             (unsigned long long)atomic_load_explicit(&s_registry_hits,
                                                      memory_order_relaxed),
             (unsigned long long)atomic_load_explicit(&s_registry_misses,
                                                      memory_order_relaxed),
             (unsigned long long)atomic_load_explicit(&s_realloc_moves,
                                                      memory_order_relaxed),
             (unsigned long long)atomic_load_explicit(&s_reg_full,
                                                      memory_order_relaxed),
             (unsigned)SOBER_ALLOC_DEFAULT_RVA_ALLOCATE,
             (unsigned)SOBER_ALLOC_DEFAULT_RVA_ALLOC_SMALL,
             (unsigned)SOBER_ALLOC_DEFAULT_RVA_FREE,
             (unsigned)SOBER_ALLOC_DEFAULT_RVA_USABLE_SIZE,
             (unsigned)SOBER_ALLOC_DEFAULT_RVA_REALLOCATE,
             (unsigned)SOBER_ALLOC_DEFAULT_RVA_ALIGNED,
             (unsigned)SOBER_ALLOC_DEFAULT_RVA_CALLOC_PLAIN,
             (unsigned)SOBER_ALLOC_DEFAULT_RVA_CALLOC_HEADER,
             (unsigned)SOBER_ALLOC_DEFAULT_RVA_CALLOC_SLOT,
             SOBER_ALLOC_DEFAULT_ENV "=0");
    return buf;
}

uint64_t sober_alloc_default_header_allocs(void)
{
    return atomic_load_explicit(&s_header_allocs, memory_order_relaxed);
}

uint64_t sober_alloc_default_plain_allocs(void)
{
    return atomic_load_explicit(&s_plain_allocs, memory_order_relaxed);
}

uint64_t sober_alloc_default_registry_hits(void)
{
    return atomic_load_explicit(&s_registry_hits, memory_order_relaxed);
}

uint64_t sober_alloc_default_registry_misses(void)
{
    return atomic_load_explicit(&s_registry_misses, memory_order_relaxed);
}

uint64_t sober_alloc_default_realloc_moves(void)
{
    return atomic_load_explicit(&s_realloc_moves, memory_order_relaxed);
}

uint64_t sober_alloc_default_registry_full(void)
{
    return atomic_load_explicit(&s_reg_full, memory_order_relaxed);
}

/* ---- built-in contract self-test ---------------------------------------- */
int sober_alloc_default_selftest(char *buf, size_t cap)
{
    int fails = 0;
    const size_t H = SOBER_ALLOC_DEFAULT_HEADER_SIZE;

    /* F1: header calloc -> base+0x10, header bytes all zero. */
    unsigned char *p = (unsigned char *)sober_alloc_default_calloc_header(1, 64);
    if (!p) {
        ++fails;
    } else {
        unsigned char *base = p - H;
        for (size_t i = 0; i < H; ++i)
            if (base[i] != 0) { ++fails; break; }
        for (size_t i = 0; i < 64; ++i)
            if (p[i] != 0) { ++fails; break; }
        /* F3 usable_size: payload = usable(base) - 0x10. */
        size_t u = sober_alloc_default_usable_size(p);
        size_t ub = malloc_usable_size(base);
        if (u != (ub > H ? ub - H : 0u)) ++fails;
        /* F3 realloc keeps the header contract. */
        unsigned char *q = (unsigned char *)sober_alloc_default_realloc(p, 256);
        if (!q) {
            ++fails;
        } else {
            unsigned char *qb = q - H;
            for (size_t i = 0; i < H; ++i)
                if (qb[i] != 0) { ++fails; break; }
            sober_alloc_default_free(q);
        }
    }

    /* F2: plain calloc -> plain pointer (no header/registry). */
    unsigned char *r = (unsigned char *)sober_alloc_default_calloc_plain(1, 48);
    if (!r) {
        ++fails;
    } else {
        for (size_t i = 0; i < 48; ++i)
            if (r[i] != 0) { ++fails; break; }
        /* Plain pointer: usable_size is the plain glibc usable size. */
        if (sober_alloc_default_usable_size(r) != malloc_usable_size(r)) ++fails;
        sober_alloc_default_free(r);
    }

    /* Plain allocate path is verbatim. */
    unsigned char *a = (unsigned char *)sober_alloc_default_allocate(32);
    if (!a) {
        ++fails;
    } else {
        for (size_t i = 0; i < 32; ++i)
            if (a[i] != 0) { ++fails; break; }
        sober_alloc_default_free(a);
    }

    if (buf && cap) {
        snprintf(buf, cap,
                 "selftest=%s fails=%d header_allocs=%llu plain_allocs=%llu "
                 "hits=%llu misses=%llu moves=%llu",
                 fails == 0 ? "PASS" : "FAIL", fails,
                 (unsigned long long)sober_alloc_default_header_allocs(),
                 (unsigned long long)sober_alloc_default_plain_allocs(),
                 (unsigned long long)sober_alloc_default_registry_hits(),
                 (unsigned long long)sober_alloc_default_registry_misses(),
                 (unsigned long long)sober_alloc_default_realloc_moves());
    }
    return fails;
}
