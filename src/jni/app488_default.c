/*
 * app488_default.c - DEFAULT-ON installation of the genuine app+0x488 object.
 *
 * Evidence (frozen engine BuildID 204ad049..., SHA 3e498d6494...;
 * disassembly /tmp/opencode/libroblox.dis; reloc check /tmp/opencode/reloc.py):
 *
 *   CREATE branch of nativeAppBridgeV2InitWithParams:
 *     0x24af26a  lea  0x488(%rbx),%r14
 *     0x24af271  cmpq $0x0,0x488(%rbx)
 *     0x24af279  je   0x24af2cc
 *     0x24af2cc  lea  -0x70(%rbp),%rdi
 *     0x24af2d7  call 0x3190869      ; alloc(0xa8) + outer/inner ctor + retain
 *     0x24af2e0  mov  %r14,%rdi
 *     0x24af2e6  call 0x231c124      ; move/store {inner, outer} -> app+0x488
 *
 *   guest helper 0x3190869:
 *     0x3190873  mov  $0xa8,%edi
 *     0x3190878  call 0x1dbab02      ; genuine guest allocator -> obj (outer)
 *     0x3190883  call 0x253c93c      ; genuine OUTER init(obj):
 *       0x253c949  movups %xmm0,0x8(%rdi)  ; obj+0x08/+0x10 = 0
 *       0x253c94d  lea  0x6dfb1c0,%rax     ; genuine OUTER vtable RVA
 *       0x253c954  mov  %rax,(%rdi)        ; obj+0x00 = base+0x6dfb1c0
 *       0x253c95b  call 0x31a0a52           ; INNER ctor(obj+0x18)
 *     0x319088f  out[0] = obj+0x18 (inner); 0x3190892 out[1] = obj (outer)
 *     0x319089c  call 0x1dce826      ; shared_ptr retain/back-ref
 *
 *   move/store 0x231c124 (0x24af2e6 path), rdi=dest, rsi=temp {inner, outer}:
 *     0x231c12e  movups (%rsi),%xmm0
 *     0x231c134  movups %xmm1,(%rsi)  ; zero the temp
 *     0x231c13b  movups %xmm0,(%rdi)  ; publish the pair into app+0x488
 *     (old occupant released only when non-NULL -- we call this only when the
 *      slot is NULL, so no indirect vtable call / guest->Java callback occurs.)
 *
 * All engine addresses are RVAs against the caller-supplied guest base for the
 * frozen engine.  The ONLY vptr ever written is base+0x6dfb1c0, written by the
 * guest's own constructor 0x253c93c.  No host stub tables, no code-byte patches,
 * no guest->Java callbacks, no decision-013 marker changes.
 *
 * DEFAULT ON: installed unless getenv("SOBER_APP488_DEFAULT") == "0".
 */
#define _GNU_SOURCE
#include "app488_default.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- genuine guest function signatures ---------------------------------- */
typedef void *(*a48d_alloc_fn)(size_t);
typedef void  (*a48d_ctor_fn)(void *obj);
/* 0x1dce826 ABI from 0x3190896..0x319089c: rdi=out slot, rsi=inner,
 * rdx=inner (third arg is consumed as the stored pointer). */
typedef void  (*a48d_retain_fn)(void *out, void *inner, void *value);
/* 0x231c124 ABI from 0x24af2e0..0x24af2e6: rdi=dest (&app->field_0x488),
 * rsi=temp {inner, outer}; returns dest and zeroes the temp. */
typedef void *(*a48d_store_fn)(void *dest, void *temp);
typedef void *(*a48d_wrapper_fn)(void);

/* ---- module state (diagnostics only) ------------------------------------ */
static uint64_t s_installs;
static void    *s_last_object;
static void    *s_last_inner;
static void    *s_last_app;
static unsigned char *s_last_base;

/* ---- master kill switch ------------------------------------------------- */
int sober_app488_default_enabled(void)
{
    const char *e = getenv(SOBER_APP488_DEFAULT_ENV);
    /* DEFAULT ON: disabled only by an explicit SOBER_APP488_DEFAULT=0. */
    return !(e != NULL && strcmp(e, SOBER_APP488_DEFAULT_KILL) == 0);
}

const char *sober_app488_default_env_vars(void)
{
    return SOBER_APP488_DEFAULT_ENV "=0 (kill switch; object is installed by "
           "default when unset; diagnostic only, never evidence for the "
           "decision-013 marker)";
}

/* ---- guest function resolution (genuine guest RVAs only) ---------------- */
static a48d_alloc_fn guest_alloc(void *base)
{
    return (a48d_alloc_fn)(uintptr_t)((uintptr_t)base
                                      + SOBER_A48D_RVA_ALLOC);
}

static a48d_ctor_fn guest_outer_init(void *base)
{
    return (a48d_ctor_fn)(uintptr_t)((uintptr_t)base
                                     + SOBER_A48D_RVA_OUTER_INIT);
}

static a48d_retain_fn guest_shared_retain(void *base)
{
    return (a48d_retain_fn)(uintptr_t)((uintptr_t)base
                                       + SOBER_A48D_RVA_SHARED_RETAIN);
}

static a48d_store_fn guest_move_store(void *base)
{
    return (a48d_store_fn)(uintptr_t)((uintptr_t)base
                                      + SOBER_A48D_RVA_MOVE_STORE);
}

static a48d_wrapper_fn guest_wrapper(void *base)
{
    return (a48d_wrapper_fn)(uintptr_t)((uintptr_t)base
                                        + SOBER_A48D_RVA_GET_WRAPPER);
}

/* ---- object construction (no publication) ------------------------------- */
void *sober_app488_default_build(void *guest_base)
{
    if (!sober_app488_default_enabled() || !guest_base)
        return NULL;

    unsigned char *base = (unsigned char *)guest_base;
    a48d_alloc_fn alloc = guest_alloc(base);
    a48d_ctor_fn outer_init = guest_outer_init(base);
    a48d_retain_fn retain = guest_shared_retain(base);

    unsigned char *obj = (unsigned char *)alloc(SOBER_A48D_OBJ_SIZE);
    if (!obj) {
        fprintf(stderr,
                "[app488-default] guest allocator %#x(%#x) returned NULL\n",
                (unsigned)SOBER_A48D_RVA_ALLOC,
                (unsigned)SOBER_A48D_OBJ_SIZE);
        return NULL;
    }

    /* Genuine OUTER constructor 0x253c93c(obj): zeroes obj+0x08/+0x10 (the
     * shared use/weak counts), writes the ONLY vptr this module ever stores
     * (base+0x6dfb1c0, a real relocated guest vtable), then invokes the real
     * INNER ctor 0x31a0a52 on obj+0x18.  Running the guest constructor itself
     * is strictly more faithful than reproducing its data writes by hand:
     * field +0x28 built there is what the consumer at 0x31a1360 dereferences.
     * No host function and no synthetic table is ever installed. */
    outer_init(obj);

    uintptr_t expected_vt = (uintptr_t)base
                            + (uintptr_t)SOBER_A48D_RVA_OUTER_VT;
    uintptr_t actual_vt;
    memcpy(&actual_vt, obj + SOBER_A48D_OUTER_VPTR, sizeof(actual_vt));
    if (actual_vt != expected_vt) {
        /* 0x253c93c writes this via a RIP-relative lea; a mismatch means the
         * guest base does not match the engine these RVAs belong to.  Do not
         * publish a wrongly-shaped object -- fail closed. */
        fprintf(stderr,
                "[app488-default] outer-init vptr mismatch: got %#lx expected "
                "%#lx (base %p + RVA %#x)\n",
                (unsigned long)actual_vt, (unsigned long)expected_vt,
                guest_base, (unsigned)SOBER_A48D_RVA_OUTER_VT);
        return NULL;
    }

    unsigned char *inner = obj + SOBER_A48D_INNER_OFF;

    /* Guest shared_ptr back-ref from helper 0x3190869 (0x1dce826): writes the
     * inner object's back-reference to the outer object and bumps the outer
     * use/weak counts.  Genuine guest code on freshly ctor-initialised fields. */
    void *temp[2];
    temp[0] = inner;
    temp[1] = obj;
    retain(temp, inner, inner);

    s_last_base = base;
    s_last_object = obj;
    s_last_inner = inner;
    return obj;
}

/* ---- publication -------------------------------------------------------- */
void *sober_app488_default_install(void *guest_base, void *app)
{
    if (!sober_app488_default_enabled() || !guest_base || !app)
        return NULL;

    unsigned char *slot =
        (unsigned char *)app + SOBER_A48D_APP_SLOT;

    /* Idempotent, matching the guest reuse branch at 0x24af271/0x24af279.
     * The slot is a {inner, outer} pair; the outer object is its second word.
     * (Fall back to word 0 only for a legacy single-pointer occupant.) */
    void *published = *(void **)slot;
    if (published) {
        void *existing = *((void **)slot + 1);
        if (!existing)
            existing = published;
        s_last_base = (unsigned char *)guest_base;
        s_last_app = app;
        s_last_object = existing;
        s_last_inner = sober_app488_default_inner(existing);
        return existing;
    }

    void *obj = sober_app488_default_build(guest_base);
    if (!obj)
        return NULL;

    void *inner = sober_app488_default_inner(obj);

    /* Exactly the 0x24af2e0..0x24af2e6 store: dest = &app->field_0x488,
     * source = temporary {inner, outer}; the genuine guest helper moves the
     * pair into the slot and zeroes the temporary. */
    void *temp[2];
    temp[0] = inner;
    temp[1] = obj;
    (void)guest_move_store(guest_base)(slot, temp);

    if (!*(void **)slot) {
        fprintf(stderr, "[app488-default] guest move/store 0x%x left slot NULL\n",
                (unsigned)SOBER_A48D_RVA_MOVE_STORE);
        return NULL;
    }
    s_last_base = (unsigned char *)guest_base;
    s_last_app = app;
    s_last_object = obj;
    s_last_inner = inner;

    s_installs++;
    fprintf(stderr,
            "[app488-default] installed (default-on) at app+%#x: outer=%p "
            "inner=%p alloc=%#x outer_init=%#x ctor=%#x retain=%#x store=%#x "
            "vptr=%#lx (base %p + %#x), size=%#x; genuine guest code only\n",
            (unsigned)SOBER_A48D_APP_SLOT, obj, inner,
            (unsigned)SOBER_A48D_RVA_ALLOC,
            (unsigned)SOBER_A48D_RVA_OUTER_INIT,
            (unsigned)SOBER_A48D_RVA_CTOR_INNER,
            (unsigned)SOBER_A48D_RVA_SHARED_RETAIN,
            (unsigned)SOBER_A48D_RVA_MOVE_STORE,
            (unsigned long)((uintptr_t)guest_base + SOBER_A48D_RVA_OUTER_VT),
            guest_base, (unsigned)SOBER_A48D_RVA_OUTER_VT,
            (unsigned)SOBER_A48D_OBJ_SIZE);
    return obj;
}

void *sober_app488_default_install_from_wrapper(void *guest_base)
{
    if (!sober_app488_default_enabled() || !guest_base)
        return NULL;

    a48d_wrapper_fn get_wrapper = guest_wrapper(guest_base);
    void *wrapper = get_wrapper();
    if (!wrapper) {
        fprintf(stderr, "[app488-default] static wrapper (0x%x) is NULL\n",
                (unsigned)SOBER_A48D_RVA_GET_WRAPPER);
        return NULL;
    }
    void *app = *(void **)((unsigned char *)wrapper + SOBER_A48D_WRAPPER_APP);
    if (!app) {
        fprintf(stderr, "[app488-default] wrapper+%#x app pointer is NULL\n",
                (unsigned)SOBER_A48D_WRAPPER_APP);
        return NULL;
    }
    return sober_app488_default_install(guest_base, app);
}

/* ---- diagnostics -------------------------------------------------------- */
void *sober_app488_default_slot(void *app)
{
    if (!app)
        return NULL;
    return *(void **)((unsigned char *)app + SOBER_A48D_APP_SLOT);
}

void *sober_app488_default_inner(void *obj)
{
    if (!obj)
        return NULL;
    return (void *)((unsigned char *)obj + SOBER_A48D_INNER_OFF);
}

const char *sober_app488_default_describe(char *buf, size_t cap)
{
    if (!buf || !cap)
        return "";
    snprintf(buf, cap,
             "enabled=%d installs=%llu app=%p outer=%p inner=%p "
             "alloc=%#x(%#x) outer_init=%#x ctor=%#x retain=%#x store=%#x "
             "vptr=%#x env=%s=0(kill)",
             sober_app488_default_enabled(), (unsigned long long)s_installs,
             s_last_app, s_last_object, s_last_inner,
             (unsigned)SOBER_A48D_RVA_ALLOC,
             (unsigned)SOBER_A48D_OBJ_SIZE,
             (unsigned)SOBER_A48D_RVA_OUTER_INIT,
             (unsigned)SOBER_A48D_RVA_CTOR_INNER,
             (unsigned)SOBER_A48D_RVA_SHARED_RETAIN,
             (unsigned)SOBER_A48D_RVA_MOVE_STORE,
             (unsigned)SOBER_A48D_RVA_OUTER_VT,
             SOBER_APP488_DEFAULT_ENV);
    return buf;
}

uint64_t sober_app488_default_installs(void)
{
    return s_installs;
}

void *sober_app488_default_last_object(void)
{
    return s_last_object;
}

void *sober_app488_default_last_inner(void)
{
    return s_last_inner;
}
