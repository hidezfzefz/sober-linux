/*
 * singleton_fix.c - worker A2.  See singleton_fix.h for the full evidence
 * chain, the frozen-engine RVA set and the exact wiring signature.
 *
 * HARD RULES OBSERVED HERE
 * ------------------------
 *  - default OFF (SOBER_SINGLETON_FIX); no side effect when unset;
 *  - no guest code byte is patched; no guest->Java callback is emitted;
 *  - every pointer that becomes guest state is either a genuine guest RVA
 *    (base + RVA) or memory returned by the genuine guest allocator 0x1dbab02;
 *  - the descriptor vtable we build in guest memory contains ONLY genuine
 *    guest RVA function pointers (no host code, no synthetic functions);
 *  - the singleton is published exactly the way the guest does at 0x238497d;
 *  - decision-013 marker is never touched.
 */
#define _GNU_SOURCE
#include "singleton_fix.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define SF_ENV       "SOBER_SINGLETON_FIX"
#define SF_GATE      "SOBER_SINGLETON_FIX_GATE"
#define SF_FRESH     "SOBER_SINGLETON_FIX_FRESH"
#define SF_PROBE     "SOBER_SINGLETON_FIX_PROBE"
#define SF_VERBOSE   "SOBER_SINGLETON_FIX_VERBOSE"

/* Guest object geometry (bytes), recovered from the ctor/factory code. */
#define SF_SINGLETON_SIZE   0xa8u  /* 0x238492f: alloc(0xa8)               */
#define SF_DESCRIPTOR_SIZE  0x70u  /* predicate reads +0x68 (0x2f34ebe)    */
#define SF_VTABLE_SLOTS     4u     /* +0x00/+0x08/+0x10/+0x18              */
#define SF_VECTOR_ENTRIES   1u     /* one genuine app-bridge descriptor     */

/* Field offsets inside the 0xa8 singleton (genuine guest layout). */
#define SF_OFF_VPTR         0x00u
#define SF_OFF_HELPER       0x28u  /* 0x608f95d: mov %rax,0x28(%r13)       */
#define SF_OFF_VEC_BEGIN    0x48u  /* 0x60916a3                            */
#define SF_OFF_VEC_END      0x50u  /* 0x60916a7                            */
#define SF_OFF_VEC_CAP      0x58u  /* 0x608f997                            */
/* Field offsets inside the service descriptor. */
#define SF_DESC_KEY         0x68u  /* 0x2f34ebe: cmp %rsi,0x68(%rdi)       */

static int sf_verbose(void) { return getenv(SF_VERBOSE) != NULL; }
#define SFV(...) do { if (sf_verbose()) \
    fprintf(stderr, "[sf] " __VA_ARGS__); } while (0)

static unsigned long sf_installs;
static unsigned long sf_gate_skips;
static unsigned long sf_probes;

const char *sober_singleton_fix_env_vars(void)
{
    return SF_ENV "," SF_GATE "," SF_FRESH "," SF_PROBE "," SF_VERBOSE;
}

int sober_singleton_fix_enabled(void)
{
    const char *e = getenv(SF_ENV);
    return e && strcmp(e, "0") && strcasecmp(e, "off") && strcasecmp(e, "no");
}

void sober_singleton_fix_reset(void)
{
    sf_installs = sf_gate_skips = sf_probes = 0;
}

void *sober_singleton_fix_ptr(void *guest_base)
{
    if (!guest_base) return NULL;
    return *(void **)((unsigned char *)guest_base + SOBER_SF_RVA_APP_SINGLETON);
}

size_t sober_singleton_fix_count(void *guest_base)
{
    void *app = sober_singleton_fix_ptr(guest_base);
    if (!app) return 0;
    uintptr_t begin = *(uintptr_t *)((unsigned char *)app + SF_OFF_VEC_BEGIN);
    uintptr_t end   = *(uintptr_t *)((unsigned char *)app + SF_OFF_VEC_END);
    if (!begin || end < begin) return 0;
    return (size_t)((end - begin) / sizeof(uintptr_t));
}

void *sober_singleton_fix_descriptor(void *guest_base)
{
    void *app = sober_singleton_fix_ptr(guest_base);
    if (!app) return NULL;
    uintptr_t begin = *(uintptr_t *)((unsigned char *)app + SF_OFF_VEC_BEGIN);
    if (!begin) return NULL;
    return *(void **)(uintptr_t)begin;
}

void *sober_singleton_fix_lookup(void *guest_base)
{
    if (!guest_base) return NULL;
    typedef void *(*sf_nullary_fn)(void);
    sf_nullary_fn getter =
        (sf_nullary_fn)(uintptr_t)((unsigned char *)guest_base +
                                    SOBER_SF_RVA_ACCESSOR);
    ++sf_probes;
    return getter();
}

const char *sober_singleton_fix_describe(void *guest_base, char *buf, size_t cap)
{
    if (!buf || cap == 0) return "";
    if (!guest_base) { buf[0] = '\0'; return buf; }
    unsigned char *b = (unsigned char *)guest_base;
    snprintf(buf, cap,
             "app@0x72cb5e0=%p services=%lu desc=%p key=%p gate=%u "
             "installs=%lu gate_skips=%lu probes=%lu",
             sober_singleton_fix_ptr(guest_base),
             (unsigned long)sober_singleton_fix_count(guest_base),
             sober_singleton_fix_descriptor(guest_base),
             (void *)(b + SOBER_SF_RVA_SERVICE_KEY),
             (unsigned)*(volatile unsigned char *)(b + SOBER_SF_RVA_GATE_BYTE),
             sf_installs, sf_gate_skips, sf_probes);
    return buf;
}

typedef void *(*sf_alloc_fn)(size_t);
typedef int   (*sf_gate_fn)(void);
typedef void *(*sf_helper_fn)(void);

static sf_alloc_fn sf_alloc(unsigned char *b)
{
    return (sf_alloc_fn)(uintptr_t)(b + SOBER_SF_RVA_GUEST_ALLOC);
}

/* Build the service descriptor and the vector that 0x6091654 scans.  All
 * backing memory comes from the guest allocator; all vtable slots are genuine
 * guest RVAs (see the header for the recovered slot semantics). */
static int sf_build_services(unsigned char *b, void **out_desc,
                             uintptr_t *out_begin, uintptr_t *out_end)
{
    sf_alloc_fn alloc = sf_alloc(b);

    /* 1. Guest-heap vtable with genuine guest function RVAs only. */
    uintptr_t *vt = (uintptr_t *)(uintptr_t)alloc(
        SF_VTABLE_SLOTS * sizeof(uintptr_t));
    if (!vt) return SOBER_SF_ERR;
    vt[0] = (uintptr_t)(b + SOBER_SF_RVA_DTOR_SERVICE);      /* +0x00 */
    vt[1] = (uintptr_t)(b + SOBER_SF_RVA_SLOT08_SERVICE);    /* +0x08 */
    vt[2] = (uintptr_t)(b + SOBER_SF_RVA_SERVICE_FACTORY);   /* +0x10 */
    vt[3] = (uintptr_t)(b + SOBER_SF_RVA_MATCH_SERVICE);     /* +0x18 */

    /* 2. Descriptor: zeroed except vptr and the key at +0x68.  The zeroed
     * inline string fields are valid inputs to the factory 0x2ec0f4a. */
    unsigned char *desc =
        (unsigned char *)(uintptr_t)alloc(SF_DESCRIPTOR_SIZE);
    if (!desc) return SOBER_SF_ERR;
    memset(desc, 0, SF_DESCRIPTOR_SIZE);
    *(uintptr_t *)(void *)desc = (uintptr_t)vt;
    *(uintptr_t *)(void *)(desc + SF_DESC_KEY) =
        (uintptr_t)(b + SOBER_SF_RVA_SERVICE_KEY);

    /* 3. The vector: an array of one 8-byte descriptor pointer. */
    uintptr_t *vec = (uintptr_t *)(uintptr_t)alloc(
        SF_VECTOR_ENTRIES * sizeof(uintptr_t));
    if (!vec) return SOBER_SF_ERR;
    vec[0] = (uintptr_t)desc;

    *out_desc = desc;
    *out_begin = (uintptr_t)vec;
    *out_end = (uintptr_t)vec + SF_VECTOR_ENTRIES * sizeof(uintptr_t);
    SFV("guest-heap vt=%p desc=%p (%uB) vec=%p..%p key=%p\n",
        (void *)vt, (void *)desc, SF_DESCRIPTOR_SIZE,
        (void *)*out_begin, (void *)*out_end,
        (void *)(b + SOBER_SF_RVA_SERVICE_KEY));
    return SOBER_SF_OK;
}

/* Install the service vector on a singleton object.  This is exactly the state
 * the genuine post-settings tail registers later in a full session: the real
 * ctor leaves +0x48/+0x50 empty. */
static int sf_install_vector(unsigned char *b, unsigned char *obj)
{
    void *desc = NULL;
    uintptr_t begin = 0, end = 0;
    int rc = sf_build_services(b, &desc, &begin, &end);
    if (rc != SOBER_SF_OK) return rc;
    *(uintptr_t *)(void *)(obj + SF_OFF_VEC_BEGIN) = begin;
    *(uintptr_t *)(void *)(obj + SF_OFF_VEC_END) = end;
    *(uintptr_t *)(void *)(obj + SF_OFF_VEC_CAP) = end; /* capacity == end */
    SFV("service vector: obj=%p begin=%p end=%p desc=%p\n",
        (void *)obj, (void *)begin, (void *)end, desc);
    return SOBER_SF_OK;
}

/* Allocate + initialise a fresh singleton.  It is guest-allocated, carries the
 * genuine guest vtable and the guest helper control block; only the service
 * vector words are filled by us (the registration being recovered). */
static int sf_build_singleton(unsigned char *b, void **out_obj)
{
    sf_alloc_fn alloc = sf_alloc(b);
    sf_helper_fn helper =
        (sf_helper_fn)(uintptr_t)(b + SOBER_SF_RVA_HELPER_GET);

    unsigned char *obj = (unsigned char *)(uintptr_t)alloc(SF_SINGLETON_SIZE);
    if (!obj) return SOBER_SF_ERR;
    memset(obj, 0, SF_SINGLETON_SIZE);
    *(uintptr_t *)(void *)(obj + SF_OFF_VPTR) =
        (uintptr_t)(b + SOBER_SF_RVA_VTABLE);
    void *h = helper ? helper() : NULL;
    *(uintptr_t *)(void *)(obj + SF_OFF_HELPER) = (uintptr_t)h;
    SFV("fresh singleton=%p vptr=%p helper=%p\n",
        (void *)obj, (void *)(b + SOBER_SF_RVA_VTABLE), h);

    int rc = sf_install_vector(b, obj);
    if (rc == SOBER_SF_OK) *out_obj = obj;
    return rc;
}

int sober_singleton_fix_populate_flags(void *guest_base, unsigned flags)
{
    if (!sober_singleton_fix_enabled()) return SOBER_SF_DISABLED;
    if (!guest_base) return SOBER_SF_ERR;
    unsigned char *b = (unsigned char *)guest_base;
    ++sf_installs;

    /* The genuine feature gate must be true or 0x30ef840 never takes the
     * singleton path at all (0x30ef84b -> 0x30ef86a). */
    if (!(flags & SOBER_SF_F_SKIP_GATE)) {
        sf_gate_fn gate = (sf_gate_fn)(uintptr_t)(b + SOBER_SF_RVA_PREDICATE);
        int g = gate ? gate() : 0;
        SFV("feature gate 0x%x -> %d (byte@0x%x=%u)\n",
            SOBER_SF_RVA_PREDICATE, g, SOBER_SF_RVA_GATE_BYTE,
            (unsigned)*(volatile unsigned char *)(b + SOBER_SF_RVA_GATE_BYTE));
        if (!g) { ++sf_gate_skips; return SOBER_SF_GATE; }
    }

    volatile uintptr_t *slot =
        (volatile uintptr_t *)(b + SOBER_SF_RVA_APP_SINGLETON);
    uintptr_t old = *slot;
    void *obj = NULL;
    int rc = SOBER_SF_OK;

    /* Prefer the object the genuine ctor already published: only its service
     * vector is missing. */
    if (old && !getenv(SF_FRESH) &&
        *(uintptr_t *)(uintptr_t)old == (uintptr_t)(b + SOBER_SF_RVA_VTABLE)) {
        SFV("reusing genuine ctor-built singleton %p\n", (void *)(uintptr_t)old);
        obj = (void *)(uintptr_t)old;
        rc = sf_install_vector(b, (unsigned char *)(uintptr_t)old);
        old = 0; /* nothing was replaced; never release the object we keep */
    } else {
        rc = sf_build_singleton(b, &obj);
        if (rc == SOBER_SF_OK) {
            *slot = (uintptr_t)obj; /* exactly the store at 0x238497d */
            SFV("published singleton: old=%p new=%p\n", (void *)old, obj);
        }
    }
    if (rc != SOBER_SF_OK) return rc;

    if (!sober_singleton_fix_ptr(guest_base) ||
        sober_singleton_fix_count(guest_base) == 0)
        return SOBER_SF_EMPTY;

    /* Optional self-check with the exact guest accessor that faults today. */
    if (!(flags & SOBER_SF_F_SKIP_PROBE)) {
        const char *p = getenv(SF_PROBE);
        if (!(p && (!strcmp(p, "0") || !strcasecmp(p, "off") ||
                    !strcasecmp(p, "no")))) {
            void *result = sober_singleton_fix_lookup(guest_base);
            SFV("guest accessor 0x%x -> %p\n", SOBER_SF_RVA_ACCESSOR, result);
            if (!result) return SOBER_SF_LOOKUP_NULL;
        }
    }

    char desc[256];
    SFV("install ok: %s\n",
        sober_singleton_fix_describe(guest_base, desc, sizeof(desc)));
    return SOBER_SF_OK;
}

int sober_singleton_fix_populate(void *guest_base)
{
    unsigned flags = 0;
    const char *g = getenv(SF_GATE);
    if (g && g[0] == '0') flags |= SOBER_SF_F_SKIP_GATE;
    return sober_singleton_fix_populate_flags(guest_base, flags);
}
