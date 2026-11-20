/*
 * app488_inner.c - install the GENUINE app+0x488 object using real guest code
 *                  (game-fault worker A12, logical Team A wave 2).
 *
 * Everything the object contains comes from executing frozen guest code at
 * BuildID 204ad049...; see app488_inner.h for the full call-chain evidence.
 * In particular:
 *   - the 0xa8 allocation is made by guest allocator 0x1dbab02,
 *   - the +0x18 inner object is built by genuine guest ctor 0x31a0a52,
 *   - the published {inner, outer} pair is moved into app+0x488 by the genuine
 *     guest move/store helper 0x231c124 (the 0x24af2e6 destination path),
 *   - the only vptr ever written is base + 0x6dfb1c0, a real relocated guest
 *     vtable used by the guest's own outer-object constructor 0x253c93c.
 *
 * No host vtables, no byte patches, no callbacks, no decision-013 changes.
 * Default OFF: SOBER_APP488_INNER=1.
 */
#define _GNU_SOURCE
#include "app488_inner.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- genuine guest function signatures ---------------------------------- */
typedef void *(*a48i_alloc_fn)(size_t);
typedef void  (*a48i_ctor_fn)(void *);
/* 0x1dce826 ABI from 0x3190896..0x319089c: rdi=shared_ptr slot,
 * rsi=inner, rdx=inner (third arg is consumed as the stored pointer). */
typedef void  (*a48i_retain_fn)(void *slot, void *inner, void *value);
/* 0x231c124 ABI from 0x24af2e0..0x24af2e6: rdi=dest (&app->field_0x488),
 * rsi=temp {inner, outer}; returns dest and zeroes the temp. */
typedef void *(*a48i_store_fn)(void *dest, void *temp);
typedef void *(*a48i_wrapper_fn)(void);

/* ---- module state (diagnostics only) ------------------------------------ */
static uint64_t s_installs;
static void    *s_last_object;
static void    *s_last_inner;
static void    *s_last_app;

/* ---- opt-in ------------------------------------------------------------- */
int sober_app488_inner_enabled(void)
{
    const char *e = getenv(SOBER_APP488_INNER_ENV);
    return (e != NULL && strcmp(e, "1") == 0);
}

const char *sober_app488_inner_env_vars(void)
{
    return SOBER_APP488_INNER_ENV "=1 (enable; default OFF; installs the "
           "genuine guest 0x31a0a52-built inner object at app+0x488; "
           "diagnostic only, never evidence for the decision-013 marker)";
}

/* ---- guest function resolution ------------------------------------------ */
static a48i_alloc_fn guest_alloc(void *base)
{
    return (a48i_alloc_fn)(void *)((unsigned char *)base
                                   + SOBER_A48I_RVA_ALLOC);
}

static a48i_ctor_fn guest_inner_ctor(void *base)
{
    return (a48i_ctor_fn)(void *)((unsigned char *)base
                                  + SOBER_A48I_RVA_CTOR_INNER);
}

static a48i_retain_fn guest_shared_retain(void *base)
{
    return (a48i_retain_fn)(void *)((unsigned char *)base
                                    + SOBER_A48I_RVA_SHARED_RETAIN);
}

static a48i_store_fn guest_move_store(void *base)
{
    return (a48i_store_fn)(void *)((unsigned char *)base
                                   + SOBER_A48I_RVA_MOVE_STORE);
}

static a48i_wrapper_fn guest_wrapper(void *base)
{
    return (a48i_wrapper_fn)(void *)((unsigned char *)base
                                     + SOBER_A48I_RVA_GET_WRAPPER);
}

/* ---- object construction (no publication) ------------------------------- */
void *sober_app488_inner_build(void *guest_base)
{
    if (!sober_app488_inner_enabled() || !guest_base)
        return NULL;

    unsigned char *base = (unsigned char *)guest_base;
    a48i_alloc_fn alloc = guest_alloc(base);
    a48i_ctor_fn ctor = guest_inner_ctor(base);
    a48i_retain_fn retain = guest_shared_retain(base);

    unsigned char *obj = (unsigned char *)alloc(SOBER_A48I_OBJ_SIZE);
    if (!obj) {
        fprintf(stderr,
                "[app488-inner] guest allocator %#x(%#x) returned NULL\n",
                (unsigned)SOBER_A48I_RVA_ALLOC,
                (unsigned)SOBER_A48I_OBJ_SIZE);
        return NULL;
    }

    /* Prologue of the guest's own outer-object constructor 0x253c93c:
     *   obj+0x00 = base + 0x6dfb1c0   (genuine relocated guest vtable)
     *   obj+0x08 = obj+0x10 = 0       (shared_ptr use/weak counts)
     * No host function ever occupies this vptr. */
    uintptr_t vt = (uintptr_t)base + (uintptr_t)SOBER_A48I_RVA_OUTER_VT;
    memcpy(obj + SOBER_A48I_OUTER_VPTR, &vt, sizeof(vt));
    memset(obj + SOBER_A48I_OUTER_USECOUNT, 0, 2 * sizeof(void *));

    /* The real inner object: genuine guest ctor 0x31a0a52 builds the +0x10,
     * +0x18 and +0x28 fields (0x31a0a8b / 0x31a09b / 0x31a0ab) that the
     * consumer at 0x31a1360 dereferences via inner+0x28. */
    unsigned char *inner = obj + SOBER_A48I_INNER_OFF;
    ctor(inner);

    /* Guest shared_ptr fixup from helper 0x3190869 (0x1dce826): writes the
     * inner object's back-reference and control block.  It is real guest code
     * and performs no callback for these freshly initialised fields. */
    void *temp[2];
    temp[0] = inner;
    temp[1] = obj;
    retain(temp, inner, inner);

    s_last_object = obj;
    s_last_inner = inner;
    return obj;
}

/* ---- publication -------------------------------------------------------- */
void *sober_app488_inner_install(void *guest_base, void *app)
{
    if (!sober_app488_inner_enabled() || !guest_base || !app)
        return NULL;

    unsigned char *slot =
        (unsigned char *)app + SOBER_A48I_APP_SLOT;

    /* Idempotent, matching the guest reuse branch at 0x24af271/0x24af279.
     * The slot is a {inner, outer} pair; the outer object is its second word.
     * (Fall back to word 0 only for a legacy single-pointer occupant.) */
    void *published = *(void **)slot;
    if (published) {
        void *existing = *((void **)slot + 1);
        if (!existing)
            existing = published;
        s_last_app = app;
        s_last_object = existing;
        s_last_inner = sober_app488_inner_inner(existing);
        return existing;
    }

    void *obj = sober_app488_inner_build(guest_base);
    if (!obj)
        return NULL;

    void *inner = sober_app488_inner_inner(obj);

    /* Exactly the 0x24af2e0..0x24af2e6 store: dest = &app->field_0x488,
     * source = temporary {inner, outer}; the guest helper moves the pair and
     * zeroes the temporary (and releases any previous occupant). */
    void *temp[2];
    temp[0] = inner;
    temp[1] = obj;
    (void)guest_move_store(guest_base)(slot, temp);

    if (!*(void **)slot) {
        fprintf(stderr, "[app488-inner] guest move/store 0x%x left slot NULL\n",
                (unsigned)SOBER_A48I_RVA_MOVE_STORE);
        return NULL;
    }
    s_last_app = app;
    s_last_object = obj;
    s_last_inner = inner;

    s_installs++;
    fprintf(stderr,
            "[app488-inner] installed at app+%#x: outer=%p inner=%p "
            "ctor=%#x alloc=%#x store=%#x vptr=%#lx (base %p + %#x), "
            "size=%#x; real guest code only\n",
            (unsigned)SOBER_A48I_APP_SLOT, obj, inner,
            (unsigned)SOBER_A48I_RVA_CTOR_INNER,
            (unsigned)SOBER_A48I_RVA_ALLOC,
            (unsigned)SOBER_A48I_RVA_MOVE_STORE,
            (unsigned long)((uintptr_t)guest_base + SOBER_A48I_RVA_OUTER_VT),
            guest_base, (unsigned)SOBER_A48I_RVA_OUTER_VT,
            (unsigned)SOBER_A48I_OBJ_SIZE);
    return obj;
}

void *sober_app488_inner_install_from_wrapper(void *guest_base)
{
    if (!sober_app488_inner_enabled() || !guest_base)
        return NULL;

    a48i_wrapper_fn get_wrapper = guest_wrapper(guest_base);
    void *wrapper = get_wrapper();
    if (!wrapper) {
        fprintf(stderr, "[app488-inner] static wrapper (0x%x) is NULL\n",
                (unsigned)SOBER_A48I_RVA_GET_WRAPPER);
        return NULL;
    }
    void *app = *(void **)((unsigned char *)wrapper + SOBER_A48I_WRAPPER_APP);
    if (!app) {
        fprintf(stderr, "[app488-inner] wrapper+%#x app pointer is NULL\n",
                (unsigned)SOBER_A48I_WRAPPER_APP);
        return NULL;
    }
    return sober_app488_inner_install(guest_base, app);
}

/* ---- diagnostics -------------------------------------------------------- */
void *sober_app488_inner_slot(void *app)
{
    if (!app)
        return NULL;
    return *(void **)((unsigned char *)app + SOBER_A48I_APP_SLOT);
}

void *sober_app488_inner_inner(void *obj)
{
    if (!obj)
        return NULL;
    return (void *)((unsigned char *)obj + SOBER_A48I_INNER_OFF);
}

const char *sober_app488_inner_describe(char *buf, size_t cap)
{
    if (!buf || !cap)
        return "";
    snprintf(buf, cap,
             "enabled=%d installs=%llu app=%p outer=%p inner=%p "
             "alloc=%#x(%#x) ctor=%#x store=%#x vptr=%#x env=%s",
             sober_app488_inner_enabled(), (unsigned long long)s_installs,
             s_last_app, s_last_object, s_last_inner,
             (unsigned)SOBER_A48I_RVA_ALLOC,
             (unsigned)SOBER_A48I_OBJ_SIZE,
             (unsigned)SOBER_A48I_RVA_CTOR_INNER,
             (unsigned)SOBER_A48I_RVA_MOVE_STORE,
             (unsigned)SOBER_A48I_RVA_OUTER_VT,
             SOBER_APP488_INNER_ENV "=1");
    return buf;
}

uint64_t sober_app488_inner_installs(void)
{
    return s_installs;
}

void *sober_app488_inner_last_object(void)
{
    return s_last_object;
}

void *sober_app488_inner_last_inner(void)
{
    return s_last_inner;
}
