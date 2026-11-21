/*
 * delegate_default.c - DEFAULT-ON genuine app+0x408 delegate (T2-4).
 * See delegate_default.h for the RVA/evidence table, the slot +0x10 contract
 * and the exact lead-facing wiring signature.
 *
 * HARD RULES OBSERVED HERE
 * ------------------------
 *  - default ON, with one kill switch: SOBER_DELEGATE_DEFAULT=0;
 *  - the object always comes from guest code (lazy singleton helper or guest
 *    allocator + genuine constructor vptr); there is no host vtable, no host
 *    function in any slot, no fabricated callback and no engine byte patch;
 *  - every candidate is verified before publication: vptr must equal the
 *    guest RVA for the chosen source and slots +0x10/+0x38/+0x90 must be
 *    non-NULL guest executable .text (relaxable to guest image, opt-in);
 *  - the installer itself makes ZERO indirect calls through the delegate;
 *    slot +0x10 is called only by the explicitly gated, getter-only probe;
 *  - a pre-existing non-NULL app+0x408 is never overwritten;
 *  - the decision-013 marker is never touched (the lead ORs
 *    sober_delegate_default_should_withhold() into seeded_state).
 */
#define _GNU_SOURCE
#include "delegate_default.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- env / logging ------------------------------------------------------- */

static int dd_zero(const char *v) { return v && !strcmp(v, "0"); }

int sober_delegate_default_enabled(void)
{
    return !dd_zero(getenv(SOBER_DD_ENV));   /* unset (or "1") == default ON */
}

int sober_delegate_default_should_withhold(void)
{
    /* Manufactured guest state is never decision-013 evidence. */
    return sober_delegate_default_enabled();
}

const char *sober_delegate_default_env_vars(void)
{
    return SOBER_DD_ENV "," SOBER_DD_SOURCE_ENV "," SOBER_DD_VERIFY_ENV ","
           SOBER_DD_CALL10_ENV "," SOBER_DD_VERBOSE_ENV;
}

static int dd_verbose(void) { return getenv(SOBER_DD_VERBOSE_ENV) != NULL; }

#define DDV(...) do { if (dd_verbose()) \
    fprintf(stderr, "[dd] " __VA_ARGS__); } while (0)

/* Bounded append into an optional caller buffer; diagnostics never fail. */
static void dd_trace(char *buf, size_t cap, const char *fmt, ...)
{
    va_list ap;
    size_t used;

    if (!buf || !cap) return;
    used = strnlen(buf, cap);
    if (used + 1 >= cap) return;
    va_start(ap, fmt);
    (void)vsnprintf(buf + used, cap - used, fmt, ap);
    va_end(ap);
}

static void dd_report(char *buf, size_t cap, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    if (buf && cap) (void)vsnprintf(buf, cap, fmt, ap);
    if (dd_verbose()) {
        (void)fputs("[dd] ", stderr);
        (void)vfprintf(stderr, fmt, ap);
    }
    va_end(ap);
}

/* ---- module state -------------------------------------------------------- */

static uint64_t          g_installs;
static void             *g_last_obj;
static uintptr_t         g_guest_base;
static uintptr_t         g_last_slots[3];
static SoberDelegateSource g_last_source = SOBER_DD_SOURCE_AUTO;
static int               g_last_rc = SOBER_DD_ERR;

void sober_delegate_default_reset(void)
{
    g_installs = 0;
    g_last_obj = NULL;
    g_guest_base = 0;
    g_last_slots[0] = g_last_slots[1] = g_last_slots[2] = 0;
    g_last_source = SOBER_DD_SOURCE_AUTO;
    g_last_rc = SOBER_DD_ERR;
}

uint64_t sober_delegate_default_installs(void) { return g_installs; }
void *sober_delegate_default_last_object(void) { return g_last_obj; }

const char *sober_delegate_default_source_name(SoberDelegateSource source)
{
    switch (source) {
    case SOBER_DD_SOURCE_A:     return "site-A 0x1dd55f2/0x7d9d100";
    case SOBER_DD_SOURCE_B:     return "site-B 0x25f7ff2/0x72a5a88";
    case SOBER_DD_SOURCE_FRESH: return "fresh 0x1dbab02/0x6e04ec0";
    default:                    return "auto";
    }
}

const char *sober_delegate_default_describe(char *buf, size_t cap)
{
    if (!buf || !cap) return "";
    (void)snprintf(buf, cap,
        "enabled=%d installs=%lu obj=%p source=%s vptr=%#lx "
        "slots(+0x10/+0x38/+0x90)=%#lx/%#lx/%#lx rc=%d env=%s",
        sober_delegate_default_enabled(), (unsigned long)g_installs,
        g_last_obj, sober_delegate_default_source_name(g_last_source),
        g_last_obj ? (unsigned long)*(const uintptr_t *)g_last_obj : 0UL,
        (unsigned long)g_last_slots[0], (unsigned long)g_last_slots[1],
        (unsigned long)g_last_slots[2], g_last_rc,
        sober_delegate_default_env_vars());
    return buf;
}

/* ---- guest RVAs / address predicates ------------------------------------ */

static uintptr_t dd_expected_vptr(uintptr_t base, SoberDelegateSource source)
{
    switch (source) {
    case SOBER_DD_SOURCE_A:     return base + SOBER_DD_RVA_VT_A;
    case SOBER_DD_SOURCE_B:     return base + SOBER_DD_RVA_VT_B;
    case SOBER_DD_SOURCE_FRESH: return base + SOBER_DD_RVA_VT_FRESH;
    default:                    return 0;
    }
}

/* Executable guest .text ([0x1db9a40,0x6d3145a) in the frozen object). */
static int dd_in_text(uintptr_t base, uintptr_t p)
{
    uintptr_t lo = base + SOBER_DD_RVA_TEXT_LO;
    uintptr_t hi = base + SOBER_DD_RVA_TEXT_HI;
    return p >= lo && p < hi;
}

/* Whole guest image, used only by the explicit relaxed verification mode. */
static int dd_in_image(uintptr_t base, uintptr_t p)
{
    return p >= base && p < base + SOBER_DD_RVA_IMAGE_END;
}

/* Read a word without assuming the guest object/vtable is aligned for our C
 * type; this is a plain guest-memory read, not a byte patch. */
static uintptr_t dd_load_word(const void *addr)
{
    uintptr_t w;
    memcpy(&w, addr, sizeof(w));
    return w;
}

/* ---- verification ------------------------------------------------------- */

static int dd_verify(uintptr_t base, void *obj, SoberDelegateSource source,
                     SoberDelegateVerify verify, char *buf, size_t cap,
                     uintptr_t slots[3])
{
    static const uintptr_t offs[3] = { 0x10u, 0x38u, 0x90u };
    uintptr_t want_vt = dd_expected_vptr(base, source);
    uintptr_t vt;
    unsigned i;

    if (!obj || !base || !want_vt) return SOBER_DD_BAD_OBJ;

    /* The two lazy singletons must be the exact static objects the guest
     * publishes; a fresh object comes from the guest allocator instead. */
    if (source == SOBER_DD_SOURCE_A || source == SOBER_DD_SOURCE_B) {
        uintptr_t want_obj = base + (source == SOBER_DD_SOURCE_A
                                     ? SOBER_DD_RVA_OBJ_A : SOBER_DD_RVA_OBJ_B);
        if ((uintptr_t)obj != want_obj) {
            dd_report(buf, cap,
                "object %#lx != expected guest singleton %#lx (%s)\n",
                (unsigned long)(uintptr_t)obj, (unsigned long)want_obj,
                sober_delegate_default_source_name(source));
            return SOBER_DD_BAD_OBJ;
        }
    }

    vt = dd_load_word(obj);
    if (vt != want_vt) {
        dd_report(buf, cap, "vptr %#lx != genuine guest vtable %#lx (%s)\n",
                  (unsigned long)vt, (unsigned long)want_vt,
                  sober_delegate_default_source_name(source));
        return SOBER_DD_BAD_VTABLE;
    }

    for (i = 0; i < 3; ++i) {
        uintptr_t fn = dd_load_word((const void *)(vt + offs[i]));
        slots[i] = fn;
        if (!fn || (!dd_in_text(base, fn) &&
                    !(verify == SOBER_DD_VERIFY_IMAGE && dd_in_image(base, fn)))) {
            dd_report(buf, cap,
                "slot +%#lx = %#lx is not %s (%s); refusing to install\n",
                (unsigned long)offs[i], (unsigned long)fn,
                verify == SOBER_DD_VERIFY_TEXT ? "guest .text" : "guest image",
                sober_delegate_default_source_name(source));
            return SOBER_DD_BAD_VTABLE;
        }
    }

    dd_trace(buf, cap, "verified %s: obj=%#lx vt=%#lx "
                       "+0x10=%#lx +0x38=%#lx +0x90=%#lx\n",
             sober_delegate_default_source_name(source),
             (unsigned long)(uintptr_t)obj, (unsigned long)vt,
             (unsigned long)slots[0], (unsigned long)slots[1],
             (unsigned long)slots[2]);
    return SOBER_DD_OK;
}

int sober_delegate_default_check(void *guest_base, void *obj,
                                 SoberDelegateSource source,
                                 char *log, size_t cap)
{
    uintptr_t slots[3] = { 0, 0, 0 };
    if (log && cap) log[0] = '\0';
    return dd_verify((uintptr_t)guest_base, obj, source,
                     SOBER_DD_VERIFY_TEXT, log, cap, slots);
}

/* ---- object sources: all calls are genuine guest code -------------------- */

/* Site A: 0x1dd55f2 is a no-argument one-shot lazy singleton; it returns the
 * static object at base+0x7d9d100 whose +0 word is base+0x7220868. */
static void *dd_object_a(uintptr_t base, char *buf, size_t cap)
{
    typedef void *(*dd_get_a_fn)(void);
    dd_get_a_fn get = (dd_get_a_fn)(base + SOBER_DD_RVA_HELPER_A);
    void *obj = get();
    dd_trace(buf, cap, "site-A helper 0x%x -> %p\n",
             (unsigned)SOBER_DD_RVA_HELPER_A, obj);
    return obj;
}

/* Site B: 0x25f7ff2(void *out) writes the object into *out and returns out.
 * It also takes an intrusive ref (0x25f806a); publishing it is the one host
 * action that mirrors the guest's own store at 0x25f7e7e. */
static void *dd_object_b(uintptr_t base, char *buf, size_t cap)
{
    typedef void *(*dd_get_b_fn)(void *);
    dd_get_b_fn get = (dd_get_b_fn)(base + SOBER_DD_RVA_HELPER_B);
    void *out = NULL;
    (void)get(&out);
    dd_trace(buf, cap, "site-B helper 0x%x -> %p\n",
             (unsigned)SOBER_DD_RVA_HELPER_B, out);
    return out;
}

/* Fallback: build exactly the header the guest's own factory 0x2520892 /
 * ctor 0x252095e builds -- 0x50 bytes from the guest allocator, vptr
 * 0x6e04ec0, scalar fields zero, float 1.0f at +0x48.  No host function
 * ever enters the vtable (it is the guest's own table in the image). */
static void *dd_object_fresh(uintptr_t base, char *buf, size_t cap)
{
    typedef void *(*dd_alloc_fn)(size_t);
    dd_alloc_fn alloc = (dd_alloc_fn)(base + SOBER_DD_RVA_ALLOC);
    unsigned char *o = (unsigned char *)alloc(SOBER_DD_OBJ_SIZE);
    uintptr_t vt = base + SOBER_DD_RVA_VT_FRESH;
    uint32_t one = 0x3f800000u;              /* 1.0f, from ctor 0x25209ab */

    if (!o) {
        dd_trace(buf, cap, "guest allocator 0x%x(0x%x) returned NULL\n",
                 (unsigned)SOBER_DD_RVA_ALLOC, (unsigned)SOBER_DD_OBJ_SIZE);
        return NULL;
    }
    memset(o, 0, SOBER_DD_OBJ_SIZE);
    memcpy(o + 0x00, &vt, sizeof(vt));
    memcpy(o + 0x48, &one, sizeof(one));
    dd_trace(buf, cap, "fresh object %p: alloc=0x%x size=%#x vt=%#lx\n",
             (void *)o, (unsigned)SOBER_DD_RVA_ALLOC,
             (unsigned)SOBER_DD_OBJ_SIZE, (unsigned long)vt);
    return o;
}

/* ---- source / verify selection from the environment --------------------- */

static SoberDelegateSource dd_source_from_env(void)
{
    const char *s = getenv(SOBER_DD_SOURCE_ENV);
    if (s && !strcasecmp(s, "A"))     return SOBER_DD_SOURCE_A;
    if (s && !strcasecmp(s, "B"))     return SOBER_DD_SOURCE_B;
    if (s && (!strcasecmp(s, "FRESH") || !strcasecmp(s, "MANUAL")))
                                      return SOBER_DD_SOURCE_FRESH;
    return SOBER_DD_SOURCE_AUTO;
}

static SoberDelegateVerify dd_verify_from_env(void)
{
    const char *v = getenv(SOBER_DD_VERIFY_ENV);
    return (v && !strcasecmp(v, "IMAGE")) ? SOBER_DD_VERIFY_IMAGE
                                          : SOBER_DD_VERIFY_TEXT;
}

/* ---- install ------------------------------------------------------------ */

int sober_delegate_default_install_ex(void *guest_base, void *app,
                                      SoberDelegateSource source,
                                      SoberDelegateVerify verify,
                                      char *log, size_t cap)
{
    uintptr_t base = (uintptr_t)guest_base;
    uintptr_t slots[3] = { 0, 0, 0 };
    volatile void **slot;
    void *cur;
    void *obj;
    int rc;

    if (log && cap) log[0] = '\0';
    if (!sober_delegate_default_enabled()) return SOBER_DD_DISABLED;
    if (!base || !app) return base ? SOBER_DD_NO_APP : SOBER_DD_ERR;

    slot = (volatile void **)((unsigned char *)app + SOBER_DD_APP_SLOT);
    cur = (void *)*slot;
    if (cur) {                      /* never clobber a guest-installed delegate */
        dd_trace(log, cap, "app+0x%x already non-NULL (%p); left untouched\n",
                 (unsigned)SOBER_DD_APP_SLOT, cur);
        g_last_obj = cur;
        g_guest_base = base;
        g_last_source = source;
        g_last_rc = SOBER_DD_PRESENT;
        return SOBER_DD_PRESENT;
    }

    obj = NULL;
    if (source == SOBER_DD_SOURCE_AUTO) {
        /* Site A first: simplest ABI (no refcount), all three slots are
         * executable guest text. */
        obj = dd_object_a(base, log, cap);
        rc = dd_verify(base, obj, SOBER_DD_SOURCE_A, verify, log, cap, slots);
        if (rc == SOBER_DD_OK) {
            source = SOBER_DD_SOURCE_A;
        } else {
            dd_trace(log, cap, "site A unusable (rc=%d); trying fresh object\n", rc);
            obj = dd_object_fresh(base, log, cap);
            rc = dd_verify(base, obj, SOBER_DD_SOURCE_FRESH, verify, log, cap, slots);
            source = SOBER_DD_SOURCE_FRESH;
        }
    } else {
        switch (source) {
        case SOBER_DD_SOURCE_A:     obj = dd_object_a(base, log, cap); break;
        case SOBER_DD_SOURCE_B:     obj = dd_object_b(base, log, cap); break;
        case SOBER_DD_SOURCE_FRESH: obj = dd_object_fresh(base, log, cap); break;
        default:                    return SOBER_DD_ERR;
        }
        rc = dd_verify(base, obj, source, verify, log, cap, slots);
    }

    if (rc != SOBER_DD_OK) {
        dd_trace(log, cap, "install aborted: verification rc=%d\n", rc);
        g_last_rc = rc;
        return rc;
    }

    /* The single host-side write: identical to the guest's own stores. */
    *slot = obj;
    g_last_obj = obj;
    g_guest_base = base;
    g_last_source = source;
    g_last_slots[0] = slots[0];
    g_last_slots[1] = slots[1];
    g_last_slots[2] = slots[2];
    ++g_installs;
    g_last_rc = SOBER_DD_OK;

    dd_trace(log, cap,
             "installed genuine delegate at app+0x%x: obj=%p vt=%#lx (%s)\n",
             (unsigned)SOBER_DD_APP_SLOT, obj,
             (unsigned long)dd_load_word(obj),
             sober_delegate_default_source_name(source));
    DDV("app+0x%x <- %p via %s; slots %#lx/%#lx/%#lx\n",
        (unsigned)SOBER_DD_APP_SLOT, obj,
        sober_delegate_default_source_name(source),
        (unsigned long)slots[0], (unsigned long)slots[1],
        (unsigned long)slots[2]);
    return SOBER_DD_OK;
}

int sober_delegate_default_install(void *guest_base, void *app)
{
    return sober_delegate_default_install_ex(guest_base, app,
                                             dd_source_from_env(),
                                             dd_verify_from_env(), NULL, 0);
}

int sober_delegate_default_install_from_wrapper(void *guest_base)
{
    typedef void *(*dd_wrapper_fn)(void);
    uintptr_t base = (uintptr_t)guest_base;
    dd_wrapper_fn get_wrapper;
    void *wrapper;
    void *app;

    if (!sober_delegate_default_enabled()) return SOBER_DD_DISABLED;
    if (!base) return SOBER_DD_ERR;

    get_wrapper = (dd_wrapper_fn)(base + SOBER_DD_RVA_GET_WRAPPER);
    wrapper = get_wrapper();
    if (!wrapper) {
        DDV("static wrapper getter 0x%x returned NULL\n",
            (unsigned)SOBER_DD_RVA_GET_WRAPPER);
        return SOBER_DD_NO_APP;
    }
    app = *(void **)((unsigned char *)wrapper + SOBER_DD_WRAPPER_APP);
    if (!app) {
        DDV("wrapper+%#x app pointer is NULL\n",
            (unsigned)SOBER_DD_WRAPPER_APP);
        return SOBER_DD_NO_APP;
    }
    return sober_delegate_default_install(guest_base, app);
}

void *sober_delegate_default_ptr(void *app)
{
    if (!app) return NULL;
    return *(void **)((unsigned char *)app + SOBER_DD_APP_SLOT);
}

/* ---- the ONLY call into the delegate, and only when the contract permits  */

int sober_delegate_default_probe_slot10(int *out_value)
{
    typedef int (*dd_getter_fn)(void *);
    uintptr_t vt;
    uintptr_t fn;

    if (!sober_delegate_default_enabled() || !g_last_obj || !g_guest_base)
        return 0;
    /* Site A slot +0x10 is a factory whose result the guest writes through at
     * 0x25d7f55; site B slot +0x10 is the intrusive refcount op.  Neither may
     * be called outside the guest's own frame.  Only the fresh object's
     * 0x6e04ec0+0x10 = 0x321a85e is a pure int getter. */
    if (g_last_source != SOBER_DD_SOURCE_FRESH) return 0;
    if (!getenv(SOBER_DD_CALL10_ENV)) return 0;

    vt = dd_load_word(g_last_obj);
    if (vt != g_guest_base + SOBER_DD_RVA_VT_FRESH) return 0;
    fn = dd_load_word((const void *)(vt + 0x10));
    if (!dd_in_text(g_guest_base, fn)) return 0;

    if (out_value) *out_value = ((dd_getter_fn)fn)(g_last_obj);
    DDV("slot +0x10 (%#lx) getter called on fresh object -> %d\n",
        (unsigned long)fn, out_value ? *out_value : 0);
    return 1;
}
