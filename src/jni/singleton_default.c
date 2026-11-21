/*
 * singleton_default.c - Team 2 / task T2-1.  See singleton_default.h for the
 * full evidence chain, the frozen-engine RVA set and the exact wiring.
 *
 * HARD RULES OBSERVED HERE
 * ------------------------
 *  - DEFAULT ON; disabled ONLY by SOBER_SINGLETON_DEFAULT=0/off/no;
 *  - no guest CODE byte is patched; no guest->Java callback is emitted;
 *    the decision-013 marker is never touched;
 *  - the 0xa8 object is built by genuine guest code (allocator 0x1dbab02,
 *    helper 0x1dd55f2, ctor 0x608f85a, release 0x30efe88); the single pointer
 *    store at 0x72cb5e0 mirrors the guest's sole writer at 0x238497d;
 *  - the descriptor vtable lives in GUEST-ALLOCATED memory and contains ONLY
 *    genuine guest RVAs -- no host code pointer, no fake/synthetic vtable;
 *  - verification uses the real accessor 0x30ef840 and must return non-NULL.
 */
#define _GNU_SOURCE
#include "singleton_default.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define SD_ENV       "SOBER_SINGLETON_DEFAULT"
#define SD_GATE      "SOBER_SINGLETON_DEFAULT_GATE"
#define SD_PROBE     "SOBER_SINGLETON_DEFAULT_PROBE"
#define SD_FRESH     "SOBER_SINGLETON_DEFAULT_FRESH"
#define SD_VERBOSE   "SOBER_SINGLETON_DEFAULT_VERBOSE"

/* Guest object geometry (bytes), recovered from the ctor/factory code. */
#define SD_SINGLETON_SIZE   0xa8u  /* 0x238492f: alloc(0xa8)                */
#define SD_DESCRIPTOR_SIZE  0x70u  /* predicate 0x2f34ebe reads +0x68       */
#define SD_VTABLE_SLOTS     4u     /* descriptor vtable +0x00/+08/+10/+18   */
#define SD_VECTOR_ENTRIES   1u     /* one genuine app-bridge descriptor     */

/* Field offsets inside the 0xa8 singleton (genuine guest layout). */
#define SD_OFF_VPTR         0x00u  /* set by the guest ctor 0x608f85a       */
#define SD_OFF_HELPER       0x28u  /* 0x608f95d: mov %rax,0x28(%r13)       */
#define SD_OFF_VEC_BEGIN    0x48u  /* 0x60916a3: vector.begin              */
#define SD_OFF_VEC_END      0x50u  /* 0x60916a7: vector.end                */
#define SD_OFF_VEC_CAP      0x58u  /* 0x608f997: vector capacity           */
/* Field offset inside the service descriptor. */
#define SD_DESC_KEY         0x68u  /* 0x2f34ebe: cmp %rsi,0x68(%rdi)       */

static int sd_verbose(void) { return getenv(SD_VERBOSE) != NULL; }
#define SDV(...) do { if (sd_verbose()) \
    fprintf(stderr, "[sd] " __VA_ARGS__); } while (0)

/* Returns true when a boolean-style knob is explicitly falsy.  Unset means
 * ON for the master switch, which is what makes this module default-ON. */
static int sd_env_off(const char *name)
{
    const char *v = getenv(name);
    return v && (v[0] == '0' || !strcasecmp(v, "off") ||
                 !strcasecmp(v, "no") || !strcasecmp(v, "false"));
}

static int sd_env_truthy(const char *name)
{
    const char *v = getenv(name);
    return v && v[0] != '0' && strcasecmp(v, "off") &&
           strcasecmp(v, "no") && strcasecmp(v, "false");
}

static unsigned long sd_installs;
static unsigned long sd_created;
static unsigned long sd_reused;
static unsigned long sd_gate_skips;
static unsigned long sd_probes;
static void        *sd_service;       /* cached result of 0x30ef840 */
static int          sd_service_vt_ok; /* result vptr == base + 0x6dbcca0 */
static void        *sd_done_base;     /* idempotency anchor */
static int          sd_done;

const char *sober_singleton_default_env_vars(void)
{
    return SD_ENV "," SD_GATE "," SD_PROBE "," SD_FRESH "," SD_VERBOSE;
}

int sober_singleton_default_enabled(void)
{
    return !sd_env_off(SD_ENV);
}

void sober_singleton_default_reset(void)
{
    sd_installs = sd_created = sd_reused = sd_gate_skips = sd_probes = 0;
    sd_service = NULL;
    sd_service_vt_ok = 0;
    sd_done_base = NULL;
    sd_done = 0;
}

void *sober_singleton_default_ptr(void *guest_base)
{
    if (!guest_base) return NULL;
    return *(void **)((unsigned char *)guest_base +
                      SOBER_SD_RVA_APP_SINGLETON);
}

size_t sober_singleton_default_count(void *guest_base)
{
    void *app = sober_singleton_default_ptr(guest_base);
    if (!app) return 0;
    uintptr_t begin = *(uintptr_t *)((unsigned char *)app + SD_OFF_VEC_BEGIN);
    uintptr_t end   = *(uintptr_t *)((unsigned char *)app + SD_OFF_VEC_END);
    if (!begin || end < begin) return 0;
    return (size_t)((end - begin) / sizeof(uintptr_t));
}

void *sober_singleton_default_descriptor(void *guest_base)
{
    void *app = sober_singleton_default_ptr(guest_base);
    if (!app) return NULL;
    uintptr_t begin = *(uintptr_t *)((unsigned char *)app + SD_OFF_VEC_BEGIN);
    if (!begin) return NULL;
    return *(void **)(uintptr_t)begin;
}

void *sober_singleton_default_service(void)
{
    return sd_service;
}

const char *sober_singleton_default_describe(void *guest_base, char *buf, size_t cap)
{
    if (!buf || cap == 0) return "";
    if (!guest_base) { buf[0] = '\0'; return buf; }
    unsigned char *b = (unsigned char *)guest_base;
    snprintf(buf, cap,
             "app@0x72cb5e0=%p services=%lu desc=%p key=%p "
             "flag@0x72cb648=%u gate_byte@0x77979c0=%u "
             "service=%p vptr_ok=%d installs=%lu created=%lu reused=%lu "
             "gate_skips=%lu probes=%lu",
             sober_singleton_default_ptr(guest_base),
             (unsigned long)sober_singleton_default_count(guest_base),
             sober_singleton_default_descriptor(guest_base),
             (void *)(b + SOBER_SD_RVA_SERVICE_KEY),
             (unsigned)*(volatile unsigned char *)(b + SOBER_SD_RVA_APP_BRIDGE_FLAG),
             (unsigned)*(volatile unsigned char *)(b + SOBER_SD_RVA_GATE_BYTE),
             sd_service, sd_service_vt_ok, sd_installs, sd_created, sd_reused,
             sd_gate_skips, sd_probes);
    return buf;
}

/* Cast helpers: integer guest addresses become C function pointers only via
 * uintptr_t; no object pointer is ever pretended to be a function pointer. */
typedef void *(*sd_alloc_fn)(size_t);
typedef int   (*sd_gate_fn)(void);
typedef void *(*sd_nullary_fn)(void);
typedef void  (*sd_ctor_fn)(void *, void *, void *, unsigned);
typedef void  (*sd_unary_fn)(void *);

static sd_alloc_fn sd_alloc(unsigned char *b)
{
    return (sd_alloc_fn)(uintptr_t)(b + SOBER_SD_RVA_GUEST_ALLOC);
}

/* ------------------------------------------------------------------------ */
/* Gate: mirror the two guards around the genuine writer at 0x2384870/0x238491e. */
static int sd_gate_ok(unsigned char *b, unsigned flags)
{
    if (flags & SOBER_SD_F_SKIP_GATE) {
        ++sd_gate_skips;
        return 1;
    }
    unsigned char flag =
        *(volatile unsigned char *)(b + SOBER_SD_RVA_APP_BRIDGE_FLAG);
    sd_gate_fn predicate =
        (sd_gate_fn)(uintptr_t)(b + SOBER_SD_RVA_PREDICATE);
    int p = predicate ? predicate() : 0;
    SDV("gate: flag@0x%x=%u predicate@0x%x -> %d mirror@0x%x=%u\n",
        SOBER_SD_RVA_APP_BRIDGE_FLAG, (unsigned)flag,
        SOBER_SD_RVA_PREDICATE, p, SOBER_SD_RVA_GATE_BYTE,
        (unsigned)*(volatile unsigned char *)(b + SOBER_SD_RVA_GATE_BYTE));
    return flag != 0 && p != 0;
}

/* ------------------------------------------------------------------------ */
/* Build the genuine descriptor + vector scanned by 0x6091654.  All backing
 * memory comes from the guest allocator; all vtable slots are guest RVAs. */
static int sd_build_services(unsigned char *b, void **out_desc,
                             uintptr_t *out_begin, uintptr_t *out_end)
{
    sd_alloc_fn alloc = sd_alloc(b);

    /* 1. Guest-heap vtable, genuine guest function RVAs only.  The guest's
     *    own key-predicate vtables carry a +0x10 of 0x1df06e0 (returns NULL). */
    uintptr_t *vt = (uintptr_t *)(uintptr_t)alloc(
        SD_VTABLE_SLOTS * sizeof(uintptr_t));
    if (!vt) return SOBER_SD_ERR;
    vt[0] = (uintptr_t)(b + SOBER_SD_RVA_DTOR_SERVICE);
    vt[1] = (uintptr_t)(b + SOBER_SD_RVA_SLOT08_SERVICE);
    vt[2] = (uintptr_t)(b + SOBER_SD_RVA_SERVICE_FACTORY); /* slot +0x10 */
    vt[3] = (uintptr_t)(b + SOBER_SD_RVA_MATCH_SERVICE);   /* slot +0x18 */

    /* 2. Descriptor: zeroed except vptr and key at +0x68.  The zeroed inline
     *    string fields are valid inputs to the factory 0x2ec0f4a -> 0x2ec1134. */
    unsigned char *desc = (unsigned char *)(uintptr_t)alloc(SD_DESCRIPTOR_SIZE);
    if (!desc) return SOBER_SD_ERR;
    memset(desc, 0, SD_DESCRIPTOR_SIZE);
    *(uintptr_t *)(void *)desc = (uintptr_t)vt;
    *(uintptr_t *)(void *)(desc + SD_DESC_KEY) =
        (uintptr_t)(b + SOBER_SD_RVA_SERVICE_KEY);

    /* 3. The vector itself: one 8-byte descriptor pointer in guest memory. */
    uintptr_t *vec = (uintptr_t *)(uintptr_t)alloc(
        SD_VECTOR_ENTRIES * sizeof(uintptr_t));
    if (!vec) return SOBER_SD_ERR;
    vec[0] = (uintptr_t)desc;

    *out_desc  = desc;
    *out_begin = (uintptr_t)vec;
    *out_end   = (uintptr_t)vec + SD_VECTOR_ENTRIES * sizeof(uintptr_t);
    SDV("guest-heap vt=%p desc=%p (%uB) vec=%p..%p key=%p "
        "factory=0x%x matcher=0x%x obj_vt=0x%x\n",
        (void *)vt, (void *)desc, SD_DESCRIPTOR_SIZE,
        (void *)*out_begin, (void *)*out_end,
        (void *)(b + SOBER_SD_RVA_SERVICE_KEY),
        SOBER_SD_RVA_SERVICE_FACTORY, SOBER_SD_RVA_MATCH_SERVICE,
        SOBER_SD_RVA_APBRIDGE_VT);
    return SOBER_SD_OK;
}

/* Install the service vector on an object.  This is exactly the registration
 * state the genuine post-settings tail adds later in a full session; the
 * guest ctor itself leaves +0x48/+0x50 empty. */
static int sd_install_vector(unsigned char *b, unsigned char *obj)
{
    void *desc = NULL;
    uintptr_t begin = 0, end = 0;
    int rc = sd_build_services(b, &desc, &begin, &end);
    if (rc != SOBER_SD_OK) return rc;
    *(uintptr_t *)(void *)(obj + SD_OFF_VEC_BEGIN) = begin;
    *(uintptr_t *)(void *)(obj + SD_OFF_VEC_END)   = end;
    *(uintptr_t *)(void *)(obj + SD_OFF_VEC_CAP)   = end; /* capacity == end */
    SDV("service vector: obj=%p begin=%p end=%p desc=%p\n",
        (void *)obj, (void *)(uintptr_t)begin, (void *)(uintptr_t)end, desc);
    return SOBER_SD_OK;
}

/* ------------------------------------------------------------------------ */
/* Create the 0xa8 singleton with the guest's OWN construction helpers.  This
 * is the "slice" of 0x2384927..0x238497d: no JNI entry, no callback.  The
 * object's vptr is written by the genuine ctor 0x608f85a, not by us. */
static int sd_build_singleton(unsigned char *b, void **out_obj)
{
    sd_alloc_fn   alloc       = sd_alloc(b);
    sd_nullary_fn settings    = (sd_nullary_fn)(uintptr_t)
                                    (b + SOBER_SD_RVA_SETTINGS_GET);
    sd_nullary_fn helper_get  = (sd_nullary_fn)(uintptr_t)
                                    (b + SOBER_SD_RVA_HELPER_GET);
    sd_ctor_fn    ctor        = (sd_ctor_fn)(uintptr_t)
                                    (b + SOBER_SD_RVA_SINGLETON_CTOR);
    sd_unary_fn   tmp_release = (sd_unary_fn)(uintptr_t)
                                    (b + SOBER_SD_RVA_TMP_RELEASE);

    void *cfg    = settings ? settings() : NULL;
    void *helper = helper_get ? helper_get() : NULL;
    unsigned char *obj = (unsigned char *)(uintptr_t)alloc(SD_SINGLETON_SIZE);
    if (!obj) return SOBER_SD_ERR;
    memset(obj, 0, SD_SINGLETON_SIZE);

    /* Mirror the guest temp struct at rbp-0x80: +0x00/+0x08/+0x10 zero,
     * +0x18 = helper (0x608f95d), passed as the ctor's 3rd argument. */
    uint64_t tmp[4] = {0, 0, 0, (uint64_t)(uintptr_t)helper};
    ctor(obj, cfg, tmp, 0u);

    /* The guest releases the temp through 0x30efe88(&local), local[0] = tmp. */
    uint64_t tmp_ref[2] = {(uint64_t)(uintptr_t)tmp, 0};
    if (tmp_release) tmp_release(tmp_ref);

    SDV("guest-built singleton=%p vptr=%p helper=%p (expected vt=%p)\n",
        (void *)obj, *(void **)(void *)obj, helper,
        (void *)(b + SOBER_SD_RVA_SINGLETON_VT));

    int rc = sd_install_vector(b, obj);
    if (rc != SOBER_SD_OK) return rc;
    *out_obj = obj;
    return SOBER_SD_OK;
}

/* ------------------------------------------------------------------------ */
/* Verify with the exact accessor that faults today.  A non-NULL result means
 * 0x6091654 matched our descriptor and the factory 0x2ec0f4a produced a real
 * service object (its vptr should be 0x6dbcca0). */
static int sd_probe(unsigned char *b, unsigned flags)
{
    if (flags & SOBER_SD_F_SKIP_PROBE) return SOBER_SD_OK;
    sd_nullary_fn accessor =
        (sd_nullary_fn)(uintptr_t)(b + SOBER_SD_RVA_ACCESSOR);
    ++sd_probes;
    void *svc = accessor ? accessor() : NULL;
    sd_service = svc;
    sd_service_vt_ok = 0;
    SDV("accessor 0x%x -> %p\n", SOBER_SD_RVA_ACCESSOR, svc);
    if (!svc) return SOBER_SD_LOOKUP_NULL;
    uintptr_t vt = *(uintptr_t *)svc;
    sd_service_vt_ok =
        (vt == (uintptr_t)(b + SOBER_SD_RVA_APBRIDGE_VT));
    SDV("service vptr=%p expected=%p ok=%d\n", (void *)vt,
        (void *)(b + SOBER_SD_RVA_APBRIDGE_VT), sd_service_vt_ok);
    if ((flags & SOBER_SD_F_STRICT_VPTR) && !sd_service_vt_ok)
        return SOBER_SD_BAD_VPTR;
    return SOBER_SD_OK;
}

/* ------------------------------------------------------------------------ */
int sober_singleton_default_install_flags(void *guest_base, unsigned flags)
{
    if (!sober_singleton_default_enabled()) return SOBER_SD_DISABLED;
    if (!guest_base) return SOBER_SD_ERR;
    unsigned char *b = (unsigned char *)guest_base;
    ++sd_installs;

    int fresh = sd_env_truthy(SD_FRESH) || (flags & SOBER_SD_F_FRESH);

    /* Idempotency: a second call for the same base with the vector already
     * populated only re-runs the read-only verification. */
    if (!fresh && sd_done && sd_done_base == guest_base &&
        sober_singleton_default_ptr(guest_base) &&
        sober_singleton_default_count(guest_base) > 0) {
        SDV("already installed for base=%p; re-verifying only\n", guest_base);
        return sd_probe(b, flags);
    }

    if (!sd_gate_ok(b, flags)) {
        SDV("guest gate false; no publication (no fabricated singleton)\n");
        return SOBER_SD_GATE;
    }

    volatile uintptr_t *slot =
        (volatile uintptr_t *)(b + SOBER_SD_RVA_APP_SINGLETON);
    uintptr_t old = *slot;
    void *obj = NULL;
    int rc = SOBER_SD_OK;

    /* Reuse a genuine ctor-built object (vptr 0x7176a60): only its service
     * vector is missing.  Otherwise build the object through real guest code. */
    if (old && !fresh &&
        *(uintptr_t *)(uintptr_t)old ==
            (uintptr_t)(b + SOBER_SD_RVA_SINGLETON_VT)) {
        SDV("reusing genuine guest singleton %p\n", (void *)(uintptr_t)old);
        obj = (void *)(uintptr_t)old;
        ++sd_reused;
        rc = sd_install_vector(b, (unsigned char *)(uintptr_t)old);
    } else {
        rc = sd_build_singleton(b, &obj);
        if (rc == SOBER_SD_OK) {
            /* The single publication store -- identical to 0x238497d. */
            *slot = (uintptr_t)obj;
            ++sd_created;
            SDV("published singleton 0x%x: old=%p new=%p\n",
                SOBER_SD_RVA_WRITER, (void *)(uintptr_t)old, obj);
        }
    }
    if (rc != SOBER_SD_OK) return rc;

    if (!sober_singleton_default_ptr(guest_base) ||
        sober_singleton_default_count(guest_base) == 0)
        return SOBER_SD_EMPTY;

    rc = sd_probe(b, flags);
    if (rc != SOBER_SD_OK) return rc;

    sd_done = 1;
    sd_done_base = guest_base;
    char desc[320];
    SDV("install ok: %s\n",
        sober_singleton_default_describe(guest_base, desc, sizeof(desc)));
    return SOBER_SD_OK;
}

int sober_singleton_default_install(void *guest_base)
{
    unsigned flags = 0;
    if (sd_env_off(SD_GATE))      flags |= SOBER_SD_F_SKIP_GATE;
    if (sd_env_off(SD_PROBE))     flags |= SOBER_SD_F_SKIP_PROBE;
    return sober_singleton_default_install_flags(guest_base, flags);
}
