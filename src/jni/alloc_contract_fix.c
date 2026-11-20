/*
 * alloc_contract_fix.c - opt-in allocator-contract wrappers (worker A14).
 *
 * Evidence: analysis/A07-allocator-audit.md; direct disassembly of the frozen
 * engine libroblox.so BuildID 204ad049..., SHA 3e498d6494...
 *   F1: guest core 0x231f13a writes 0 at 0x231f1ac/0x231f243; our old bridge
 *       bionic_bootstrap.c:299 wrote `total` into the 16-byte header.
 *   F2: raw calloc 0x253225e returns a plain (headerless) block.
 *   F3: guest free thunk 0x231f53a subtracts 0x10; our generic bridges did not.
 *
 * Constraints honoured: NEW files only; no CMake/engine edits; no guest byte
 * patches; no callbacks; no decision-013 marker changes; C17, -Werror clean.
 *
 * Registers header-form `base` pointers so free/realloc/usable_size can apply
 * the -0x10 correction only to pointers that really came from the header
 * calloc.  Plain host_allocate pointers are left untouched.  The guest's own
 * 0x231f53a thunk already subtracts 0x10 and hands us `base`; because we
 * register `base` (not base+0x10), that path also lands on the right glibc
 * pointer without any extra work.
 */
#define _GNU_SOURCE
#include "alloc_contract_fix.h"

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
#define SOBER_ALLOC_REG_BITS 13u
#define SOBER_ALLOC_REG_SIZE (1u << SOBER_ALLOC_REG_BITS)
#define SOBER_ALLOC_REG_MASK ((size_t)(SOBER_ALLOC_REG_SIZE - 1u))
#define SOBER_ALLOC_REG_EMPTY ((uintptr_t)0u)
#define SOBER_ALLOC_REG_TOMB  ((uintptr_t)UINTPTR_MAX)
#define SOBER_ALLOC_REG_NPOS  ((size_t)-1)

static _Atomic uintptr_t s_reg[SOBER_ALLOC_REG_SIZE];

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
    return (size_t)x & SOBER_ALLOC_REG_MASK;
}

static int reg_insert(uintptr_t base)
{
    if (!base || base == SOBER_ALLOC_REG_TOMB) return 0;

    /* The outer retry handles a race where a tombstone we intend to reuse is
     * claimed by another thread inserting a different pointer: simply rescan
     * from the hash instead of skipping the slot with a stale target. */
    for (size_t attempt = 0; attempt < SOBER_ALLOC_REG_SIZE; ++attempt) {
        size_t idx = reg_hash(base);
        size_t first_tomb = SOBER_ALLOC_REG_NPOS;
        for (size_t k = 0; k < SOBER_ALLOC_REG_SIZE; ++k) {
            uintptr_t v = atomic_load_explicit(&s_reg[idx],
                                               memory_order_acquire);
            if (v == base) return 1;
            if (v == SOBER_ALLOC_REG_EMPTY) {
                size_t target = (first_tomb != SOBER_ALLOC_REG_NPOS)
                                ? first_tomb : idx;
                uintptr_t expect = (target == idx)
                                   ? SOBER_ALLOC_REG_EMPTY
                                   : atomic_load_explicit(&s_reg[target],
                                                          memory_order_acquire);
                if (expect != SOBER_ALLOC_REG_EMPTY &&
                    expect != SOBER_ALLOC_REG_TOMB)
                    break; /* tombstone was taken; rescan */
                if (atomic_compare_exchange_strong_explicit(
                        &s_reg[target], &expect, base,
                        memory_order_release, memory_order_acquire))
                    return 1;
                if (expect == base) return 1;
                break; /* another thread raced us; rescan */
            }
            if (v == SOBER_ALLOC_REG_TOMB && first_tomb == SOBER_ALLOC_REG_NPOS)
                first_tomb = idx;
            idx = (idx + 1u) & SOBER_ALLOC_REG_MASK;
        }
    }
    bump(&s_reg_full);
    return 0;
}

static int reg_contains(uintptr_t base)
{
    if (!base) return 0;
    size_t idx = reg_hash(base);
    for (size_t k = 0; k < SOBER_ALLOC_REG_SIZE; ++k) {
        uintptr_t v = atomic_load_explicit(&s_reg[idx], memory_order_acquire);
        if (v == base) return 1;
        if (v == SOBER_ALLOC_REG_EMPTY) return 0;
        idx = (idx + 1u) & SOBER_ALLOC_REG_MASK;
    }
    return 0;
}

static void reg_remove(uintptr_t base)
{
    if (!base) return;
    size_t idx = reg_hash(base);
    for (size_t k = 0; k < SOBER_ALLOC_REG_SIZE; ++k) {
        uintptr_t v = atomic_load_explicit(&s_reg[idx], memory_order_acquire);
        if (v == base) {
            uintptr_t expect = base;
            (void)atomic_compare_exchange_strong_explicit(
                &s_reg[idx], &expect, SOBER_ALLOC_REG_TOMB,
                memory_order_release, memory_order_acquire);
            return;
        }
        if (v == SOBER_ALLOC_REG_EMPTY) return;
        idx = (idx + 1u) & SOBER_ALLOC_REG_MASK;
    }
}

/* ---- F1: header-form calloc --------------------------------------------- */
void *sober_alloc_contract_calloc_header(size_t nmemb, size_t size)
{
    if (!nmemb) nmemb = 1u;
    if (!size) size = 1u;
    if (nmemb > SIZE_MAX / size) return NULL;
    size_t n = nmemb * size;
    if (n > SIZE_MAX - SOBER_ALLOC_CONTRACT_HEADER_SIZE) return NULL;
    size_t total = n + SOBER_ALLOC_CONTRACT_HEADER_SIZE;

    unsigned char *base = (unsigned char *)calloc(1u, total);
    if (!base) return NULL;

    /* F1: the guest's 0x231f13a core stores ZERO here (0x231f1ac xor,
     * 0x231f243 mov).  It is a 0-vs-custom-handle discriminator for the free
     * thunk 0x231f53a, NOT the allocation size.  Writing `total` here (the old
     * bionic_bootstrap.c:299 behaviour) makes the special-free path
     * 0x6a28bf4 dereference a near-zero "handle" when both flags are set. */
    memset(base, 0, SOBER_ALLOC_CONTRACT_HEADER_SIZE);

    if (!reg_insert((uintptr_t)base)) {
        /* Registry exhausted: handing back an unregistered base+0x10 would
         * make a later free/realloc misclassify it.  Fail closed instead. */
        free(base);
        return NULL;
    }
    bump(&s_header_allocs);
    return base + SOBER_ALLOC_CONTRACT_HEADER_SIZE;
}

/* ---- F2: plain-pointer (headerless) calloc ------------------------------- */
void *sober_alloc_contract_calloc_plain(size_t nmemb, size_t size)
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
void sober_alloc_contract_free(void *p)
{
    if (!p) return;
    uintptr_t base = (uintptr_t)p - (uintptr_t)SOBER_ALLOC_CONTRACT_HEADER_SIZE;
    if (reg_contains(base)) {
        reg_remove(base);
        bump(&s_registry_hits);
        free((void *)base);
        return;
    }
    /* Plain host_allocate pointer, or `base` already subtracted by the guest
     * thunk 0x231f53a (we registered `base`, so p-0x10 is not in the set). */
    bump(&s_registry_misses);
    free(p);
}

/* ---- F3: header-aware realloc ------------------------------------------- */
void *sober_alloc_contract_realloc(void *p, size_t n)
{
    if (!n) n = 1u;
    uintptr_t base = p ? (uintptr_t)p
                         - (uintptr_t)SOBER_ALLOC_CONTRACT_HEADER_SIZE : 0u;
    int header = (p != NULL) && reg_contains(base);
    uintptr_t alloc_base = header ? base : (uintptr_t)p;

    if (header && n > SIZE_MAX - SOBER_ALLOC_CONTRACT_HEADER_SIZE) return NULL;
    size_t req = header ? n + SOBER_ALLOC_CONTRACT_HEADER_SIZE : n;

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
    memset(q, 0, SOBER_ALLOC_CONTRACT_HEADER_SIZE); /* F1 again */
    if (!reg_insert((uintptr_t)q)) {
        free(q);
        return NULL;
    }
    if ((uintptr_t)q != base) bump(&s_realloc_moves);

    size_t new_usable = malloc_usable_size(q);
    size_t old_payload = old_usable > SOBER_ALLOC_CONTRACT_HEADER_SIZE
                         ? old_usable - SOBER_ALLOC_CONTRACT_HEADER_SIZE : 0u;
    size_t new_payload = new_usable > SOBER_ALLOC_CONTRACT_HEADER_SIZE
                         ? new_usable - SOBER_ALLOC_CONTRACT_HEADER_SIZE : 0u;
    if (n > old_payload && new_payload > old_payload) {
        size_t z = n - old_payload;
        size_t avail = new_payload - old_payload;
        if (z > avail) z = avail;
        memset((unsigned char *)q + SOBER_ALLOC_CONTRACT_HEADER_SIZE
               + old_payload, 0, z);
    }
    return (unsigned char *)q + SOBER_ALLOC_CONTRACT_HEADER_SIZE;
}

/* ---- F3: header-aware usable_size --------------------------------------- */
size_t sober_alloc_contract_usable_size(void *p)
{
    if (!p) return 0u;
    uintptr_t base = (uintptr_t)p - (uintptr_t)SOBER_ALLOC_CONTRACT_HEADER_SIZE;
    if (reg_contains(base)) {
        bump(&s_registry_hits);
        size_t u = malloc_usable_size((void *)base);
        /* The 16 bytes at base are the header, not guest payload. */
        return u > SOBER_ALLOC_CONTRACT_HEADER_SIZE
               ? u - SOBER_ALLOC_CONTRACT_HEADER_SIZE : 0u;
    }
    bump(&s_registry_misses);
    return malloc_usable_size(p);
}

/* ---- installable pointer table ------------------------------------------ */
static const struct sober_alloc_contract_ops s_ops = {
    sober_alloc_contract_calloc_header,
    sober_alloc_contract_calloc_plain,
    sober_alloc_contract_free,
    sober_alloc_contract_realloc,
    sober_alloc_contract_usable_size,
};

int sober_alloc_contract_fix_enabled(void)
{
    const char *e = getenv(SOBER_ALLOC_CONTRACT_FIX_ENV);
    return e != NULL && strcmp(e, "1") == 0;
}

const struct sober_alloc_contract_ops *sober_alloc_contract_fix_ops(void)
{
    return sober_alloc_contract_fix_enabled() ? &s_ops : NULL;
}

const char *sober_alloc_contract_fix_env_vars(void)
{
    return SOBER_ALLOC_CONTRACT_FIX_ENV
           "=1 (install F1/F2/F3 wrappers; default OFF; "
           "no guest byte patches, no callbacks)";
}

/* ---- diagnostics -------------------------------------------------------- */
const char *sober_alloc_contract_fix_describe(char *buf, size_t cap)
{
    if (!buf || !cap) return "";
    snprintf(buf, cap,
             "enabled=%d ops=%p header_allocs=%llu plain_allocs=%llu "
             "hits=%llu misses=%llu moves=%llu reg_full=%llu "
             "header_calloc=%#x raw_calloc=%#x free=%#x realloc=%#x "
             "usable=%#x env=%s",
             sober_alloc_contract_fix_enabled(),
             (void *)sober_alloc_contract_fix_ops(),
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
             (unsigned)SOBER_ALLOC_FIX_RVA_HEADER_CALLOC,
             (unsigned)SOBER_ALLOC_FIX_RVA_RAW_CALLOC,
             (unsigned)SOBER_ALLOC_FIX_RVA_FREE,
             (unsigned)SOBER_ALLOC_FIX_RVA_REALLOC,
             (unsigned)SOBER_ALLOC_FIX_RVA_USABLE_SIZE,
             SOBER_ALLOC_CONTRACT_FIX_ENV "=1");
    return buf;
}

uint64_t sober_alloc_contract_header_allocs(void)
{
    return atomic_load_explicit(&s_header_allocs, memory_order_relaxed);
}

uint64_t sober_alloc_contract_plain_allocs(void)
{
    return atomic_load_explicit(&s_plain_allocs, memory_order_relaxed);
}

uint64_t sober_alloc_contract_registry_hits(void)
{
    return atomic_load_explicit(&s_registry_hits, memory_order_relaxed);
}

uint64_t sober_alloc_contract_registry_misses(void)
{
    return atomic_load_explicit(&s_registry_misses, memory_order_relaxed);
}

uint64_t sober_alloc_contract_realloc_moves(void)
{
    return atomic_load_explicit(&s_realloc_moves, memory_order_relaxed);
}
