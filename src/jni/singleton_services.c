/*
 * singleton_services.c - worker A11.  See singleton_services.h for the full
 * evidence chain, the frozen-engine RVA set and the exact wiring signature.
 *
 * HARD RULES OBSERVED HERE
 * ------------------------
 *  - default OFF (SOBER_SINGLETON_SERVICES); no side effect when unset;
 *  - no guest bytes patched; no guest->Java callbacks; decision-013 untouched;
 *  - every pointer published into the guest image is either a genuine guest
 *    RVA (base + RVA) or memory returned by the genuine guest allocator
 *    (0x1dbab02); the descriptor vtable we build lives in guest-allocated
 *    memory and contains ONLY genuine guest function RVAs;
 *  - the singleton is published exactly the way the guest does at 0x238497d.
 */
#define _GNU_SOURCE
#include "singleton_services.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SS_ENV          "SOBER_SINGLETON_SERVICES"
#define SS_GATE         "SOBER_SINGLETON_SERVICES_GATE"
#define SS_FRESH        "SOBER_SINGLETON_SERVICES_FRESH"
#define SS_RELEASEOLD   "SOBER_SINGLETON_SERVICES_RELEASE_OLD"
#define SS_VERBOSE      "SOBER_SINGLETON_SERVICES_VERBOSE"

/* Guest object geometry (bytes), recovered from the ctor/factory code. */
#define SS_SINGLETON_SIZE   0xa8u   /* 0x238492f: mov $0xa8,%edi; alloc      */
#define SS_DESCRIPTOR_SIZE  0x70u   /* predicate reads +0x68 (0x2f34ebe)    */
#define SS_SERVICE_SIZE     0x58u   /* 0x2ec0f55: mov $0x58,%edi; alloc      */
#define SS_VTABLE_SLOTS     4u      /* +0x00/+0x08/+0x10/+0x18 are needed   */
#define SS_VECTOR_ENTRIES   1u      /* one genuine app-bridge descriptor    */

/* Field offsets inside the 0xa8 singleton (genuine guest layout). */
#define SS_OFF_VPTR         0x00u
#define SS_OFF_HELPER       0x28u   /* 0x608f95d: mov %rax,0x28(%r13)       */
#define SS_OFF_VEC_BEGIN    0x48u   /* 0x60916a3                             */
#define SS_OFF_VEC_END      0x50u   /* 0x60916a7                             */
/* Field offsets inside the service descriptor. */
#define SS_DESC_KEY         0x68u   /* 0x2f34ebe: cmp %rsi,0x68(%rdi)       */

static int ss_verbose(void) { return getenv(SS_VERBOSE) != NULL; }
#define SSV(...) do { if (ss_verbose()) \
    fprintf(stderr, "[ss] " __VA_ARGS__); } while (0)

static unsigned long ss_installs;
static unsigned long ss_gate_skips;

const char *sober_singleton_services_env_vars(void)
{
    return SS_ENV "," SS_GATE "," SS_FRESH "," SS_RELEASEOLD "," SS_VERBOSE;
}

int sober_singleton_services_enabled(void)
{
    return getenv(SS_ENV) != NULL;
}

void sober_singleton_services_reset(void)
{
    ss_installs = ss_gate_skips = 0;
}

void *sober_singleton_services_ptr(void *guest_base)
{
    if (!guest_base) return NULL;
    unsigned char *b = (unsigned char *)guest_base;
    return *(void **)(b + SOBER_SS_RVA_APP_SINGLETON);
}

size_t sober_singleton_services_count(void *guest_base)
{
    if (!guest_base) return 0;
    unsigned char *b = (unsigned char *)guest_base;
    uintptr_t begin = *(uintptr_t *)(b + SOBER_SS_RVA_APP_SINGLETON
                                     + SS_OFF_VEC_BEGIN);
    uintptr_t end = *(uintptr_t *)(b + SOBER_SS_RVA_APP_SINGLETON
                                   + SS_OFF_VEC_END);
    if (!begin || end < begin) return 0;
    return (size_t)((end - begin) / sizeof(uintptr_t));
}

void *sober_singleton_services_descriptor(void *guest_base)
{
    if (!guest_base) return NULL;
    unsigned char *b = (unsigned char *)guest_base;
    uintptr_t begin = *(uintptr_t *)(b + SOBER_SS_RVA_APP_SINGLETON
                                     + SS_OFF_VEC_BEGIN);
    if (!begin) return NULL;
    return *(void **)(uintptr_t)begin;
}

const char *sober_singleton_services_describe(void *guest_base,
                                              char *buf, size_t cap)
{
    if (!buf || cap == 0) return "";
    if (!guest_base) { buf[0] = '\0'; return buf; }
    unsigned char *b = (unsigned char *)guest_base;
    snprintf(buf, cap,
             "app@0x72cb5e0=%p services=%lu desc=%p key=%p gate=%u "
             "installs=%lu gate_skips=%lu",
             sober_singleton_services_ptr(guest_base),
             (unsigned long)sober_singleton_services_count(guest_base),
             sober_singleton_services_descriptor(guest_base),
             (void *)(b + SOBER_SS_RVA_SERVICE_KEY),
             (unsigned)*(volatile unsigned char *)(b + SOBER_SS_RVA_GATE_BYTE),
             ss_installs, ss_gate_skips);
    return buf;
}

/* Cast helper: an integer guest address to a C function pointer.  The cast is
 * via uintptr_t so we never pretend an object pointer is a function pointer. */
typedef void *(*ss_alloc_fn)(size_t);
typedef int   (*ss_gate_fn)(void);
typedef void *(*ss_helper_fn)(void);
typedef void  (*ss_dtor_fn)(void *);

static ss_alloc_fn ss_alloc(unsigned char *b)
{
    return (ss_alloc_fn)(uintptr_t)(b + SOBER_SS_RVA_GUEST_ALLOC);
}

/* Build the service descriptor and the vector that 0x6091654 scans.
 * All backing memory comes from the guest allocator; all vtable slots are
 * genuine guest RVAs.  See the header for the recovered slot semantics. */
static int ss_build_services(unsigned char *b, void **out_desc,
                             uintptr_t *out_begin, uintptr_t *out_end)
{
    ss_alloc_fn alloc = ss_alloc(b);

    /* 1. Guest-heap vtable with genuine guest function RVAs only. */
    uintptr_t *vt = (uintptr_t *)(uintptr_t)alloc(SS_VTABLE_SLOTS * sizeof(uintptr_t));
    if (!vt) return SOBER_SS_ERR;
    vt[0] = (uintptr_t)(b + SOBER_SS_RVA_DTOR_SERVICE);
    vt[1] = (uintptr_t)(b + SOBER_SS_RVA_SLOT08_SERVICE);
    vt[2] = (uintptr_t)(b + SOBER_SS_RVA_SERVICE_FACTORY); /* slot +0x10 */
    vt[3] = (uintptr_t)(b + SOBER_SS_RVA_MATCH_SERVICE);   /* slot +0x18 */

    /* 2. Descriptor: zeroed except vptr and the key at +0x68.  The zeroed
     * +0x08/+0x20/+0x38 fields are valid inline std::strings for the factory
     * at 0x2ec0f4a -> 0x2ec1134 (flag byte 0 -> inline copy path). */
    unsigned char *desc = (unsigned char *)(uintptr_t)alloc(SS_DESCRIPTOR_SIZE);
    if (!desc) return SOBER_SS_ERR;
    memset(desc, 0, SS_DESCRIPTOR_SIZE);
    *(uintptr_t *)(void *)desc = (uintptr_t)vt;
    *(uintptr_t *)(void *)(desc + SS_DESC_KEY) =
        (uintptr_t)(b + SOBER_SS_RVA_SERVICE_KEY);

    /* 3. The vector itself: an array of one 8-byte descriptor pointer. */
    uintptr_t *vec = (uintptr_t *)(uintptr_t)alloc(
        SS_VECTOR_ENTRIES * sizeof(uintptr_t));
    if (!vec) return SOBER_SS_ERR;
    vec[0] = (uintptr_t)desc;

    *out_desc = desc;
    *out_begin = (uintptr_t)vec;
    *out_end = (uintptr_t)vec + SS_VECTOR_ENTRIES * sizeof(uintptr_t);
    SSV("guest-heap vt=%p desc=%p (%uB) vec=%p..%p key=%p\n",
        (void *)vt, (void *)desc, SS_DESCRIPTOR_SIZE,
        (void *)*out_begin, (void *)*out_end,
        (void *)(b + SOBER_SS_RVA_SERVICE_KEY));
    return SOBER_SS_OK;
}

/* Install the service vector on an existing singleton object.  This is the
 * whole missing state: the genuine ctor left +0x48/+0x50 empty. */
static int ss_populate_vector(unsigned char *b, unsigned char *obj)
{
    void *desc = NULL;
    uintptr_t begin = 0, end = 0;
    int rc = ss_build_services(b, &desc, &begin, &end);
    if (rc != SOBER_SS_OK) return rc;
    *(uintptr_t *)(void *)(obj + SS_OFF_VEC_BEGIN) = begin;
    *(uintptr_t *)(void *)(obj + SS_OFF_VEC_END) = end;
    SSV("service vector: obj=%p begin=%p end=%p desc=%p\n",
        (void *)obj, (void *)begin, (void *)end, desc);
    return SOBER_SS_OK;
}

/* Allocate + initialise the 0xa8 singleton.  The object is guest-allocated and
 * carries the genuine guest vtable; the only words we fill ourselves are the
 * service vector (+0x48/+0x50), which is exactly the registration the real
 * post-settings tail performs later in a full session. */
static int ss_build_singleton(unsigned char *b, void **out_obj)
{
    ss_alloc_fn alloc = ss_alloc(b);
    ss_helper_fn helper = (ss_helper_fn)(uintptr_t)(b + SOBER_SS_RVA_HELPER_GET);

    unsigned char *obj = (unsigned char *)(uintptr_t)alloc(SS_SINGLETON_SIZE);
    if (!obj) return SOBER_SS_ERR;
    memset(obj, 0, SS_SINGLETON_SIZE);
    *(uintptr_t *)(void *)(obj + SS_OFF_VPTR) =
        (uintptr_t)(b + SOBER_SS_RVA_SINGLETON_VT);

    /* The ctor stores the guest helper result at +0x28; mirror that so any
     * destructor path sees a genuine control block. */
    void *h = helper ? helper() : NULL;
    *(uintptr_t *)(void *)(obj + SS_OFF_HELPER) = (uintptr_t)h;
    SSV("singleton=%p vptr=%p helper=%p\n", (void *)obj,
        (void *)(b + SOBER_SS_RVA_SINGLETON_VT), h);

    int rc = ss_populate_vector(b, obj);
    if (rc != SOBER_SS_OK) return rc;
    *out_obj = obj;
    return SOBER_SS_OK;
}

int sober_singleton_services_install_flags(void *guest_base, unsigned flags)
{
    if (!sober_singleton_services_enabled()) return SOBER_SS_DISABLED;
    if (!guest_base) return SOBER_SS_ERR;
    unsigned char *b = (unsigned char *)guest_base;
    ++ss_installs;

    /* The genuine feature gate must be true or 0x30ef840 never takes the
     * singleton path at all (0x30ef84b -> 0x30ef86a). */
    if (!(flags & SOBER_SS_F_SKIP_GATE)) {
        ss_gate_fn gate = (ss_gate_fn)(uintptr_t)(b + SOBER_SS_RVA_FEATURE_GATE);
        int g = gate ? gate() : 0;
        SSV("feature gate 0x%x -> %d (byte@0x%x=%u)\n",
            SOBER_SS_RVA_FEATURE_GATE, g, SOBER_SS_RVA_GATE_BYTE,
            (unsigned)*(volatile unsigned char *)(b + SOBER_SS_RVA_GATE_BYTE));
        if (!g) {
            ++ss_gate_skips;
            return SOBER_SS_GATE;
        }
    }

    volatile uintptr_t *slot =
        (volatile uintptr_t *)(b + SOBER_SS_RVA_APP_SINGLETON);
    uintptr_t old = *slot;

    /* Prefer the object the genuine ctor already published.  That object has
     * every guest-built field; only its service vector is missing. */
    void *obj = NULL;
    int rc = SOBER_SS_OK;
    if (old && !getenv(SS_FRESH) &&
        *(uintptr_t *)(uintptr_t)old ==
            (uintptr_t)(b + SOBER_SS_RVA_SINGLETON_VT)) {
        SSV("reusing genuine ctor-built singleton %p\n", (void *)old);
        obj = (void *)(uintptr_t)old;
        rc = ss_populate_vector(b, (unsigned char *)(uintptr_t)old);
        old = 0;  /* nothing was replaced; never release the object we keep */
    } else {
        rc = ss_build_singleton(b, &obj);
        if (rc == SOBER_SS_OK) {
            /* Publish exactly as 0x238497d does.  This is the only pointer
             * slot write we perform in the guest image. */
            *slot = (uintptr_t)obj;
            SSV("published singleton: old=%p new=%p\n", (void *)old, obj);
        }
    }
    if (rc != SOBER_SS_OK) return rc;

    int release_old = (flags & SOBER_SS_F_RELEASE_OLD) ||
                      getenv(SS_RELEASEOLD) != NULL;
    if (old && release_old) {
        uintptr_t *vt = *(uintptr_t **)(uintptr_t)old;
        if (vt && vt[1]) {
            ((ss_dtor_fn)(uintptr_t)vt[1])((void *)(uintptr_t)old);
            SSV("released old singleton via vtable +0x08\n");
        }
    }

    if (!sober_singleton_services_ptr(guest_base) ||
        sober_singleton_services_count(guest_base) == 0)
        return SOBER_SS_EMPTY;

    char desc[256];
    SSV("install ok: %s\n",
        sober_singleton_services_describe(guest_base, desc, sizeof(desc)));
    return SOBER_SS_OK;
}

int sober_singleton_services_install(void *guest_base)
{
    unsigned flags = 0;
    const char *g = getenv(SS_GATE);
    if (g && g[0] == '0') flags |= SOBER_SS_F_SKIP_GATE;
    return sober_singleton_services_install_flags(guest_base, flags);
}
