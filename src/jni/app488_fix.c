/*
 * app488_fix.c - faithful guest-shaped app+0x488 object (game-fault worker A1).
 *
 * Evidence (frozen engine BuildID 204ad049..., SHA 3e498d6494...; disassembly
 * /tmp/opencode/libroblox.dis; reloc check /tmp/opencode/reloc.py):
 *
 *   InitWithParams writer, app+0x488 == NULL branch:
 *     0x24af26a  lea  0x488(%rbx),%r14
 *     0x24af271  cmpq $0x0,0x488(%rbx)
 *     0x24af279  je   0x24af2cc
 *     0x24af2cc  lea  -0x70(%rbp),%rdi
 *     0x24af2d0  lea  -0xd0(%rbp),%rsi
 *     0x24af2d7  call 0x3190869      ; alloc(0xa8) + guest ctor
 *     0x24af2e0  mov  %r14,%rdi
 *     0x24af2e6  call 0x231c124      ; move/store into app+0x488
 *
 *   Reuse branch (slot non-NULL) - the wrapper shape we reproduce:
 *     0x24af288  mov  0x488(%rbx),%rdi
 *     0x24af28f  lea  0x6e02918,%rax
 *     0x24af296  lea  -0xd0(%rbp),%r15
 *     0x24af29d  mov  %rax,(%r15)    ; +0x00 vptr  = guest+0x6e02918
 *     0x24af2a0  mov  %rbx,0x8(%r15) ; +0x08 app
 *     0x24af2a4  mov  %r15,0x20(%r15); +0x20 self
 *
 *   Guest helper 0x3190869:
 *     mov $0xa8,%edi; call 0x1dbab02  (genuine guest allocator)
 *     ... stores {obj+0x18, obj} into the caller's out slot; 0x231c124 moves
 *     those 16 bytes into app+0x488.
 *
 *   reloc.py dump 0x6e02918:
 *     0x6e02918 -> 0x1dd72a0, 0x6e02920 -> 0x320e700,
 *     0x6e02928 -> 0x320e714, 0x6e02930 -> 0x253c978, ...
 *   all genuine guest RVAs.  Slot +0x10 (0x320e714) itself calls 0x1dbab02,
 *   proving this is a real guest vtable and not a host stub.
 *
 * No host stub vtables, no byte patches, no guest->Java callbacks, no
 * decision-013 marker changes.  Default OFF: SOBER_APP488_FIX=1.
 */
#define _GNU_SOURCE
#include "app488_fix.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- module state (diagnostics only) ----------------------------------- */
static uint64_t   s_installs;
static void      *s_last_object;
static void      *s_last_app;
static unsigned char *s_last_base;

/* ---- helpers ----------------------------------------------------------- */
typedef void *(*guest_alloc_fn)(size_t);
typedef void *(*guest_wrapper_fn)(void);

static guest_alloc_fn guest_allocator(void *guest_base)
{
    return (guest_alloc_fn)((unsigned char *)guest_base
                            + SOBER_APP488_RVA_ALLOCATOR);
}

static guest_wrapper_fn guest_static_wrapper(void *guest_base)
{
    return (guest_wrapper_fn)((unsigned char *)guest_base
                              + SOBER_APP488_RVA_GET_WRAPPER);
}

static uintptr_t guest_vptr(void *guest_base)
{
    return (uintptr_t)guest_base + (uintptr_t)SOBER_APP488_RVA_WRAPPER_VT;
}

/* ---- opt-in ------------------------------------------------------------ */
int sober_app488_fix_enabled(void)
{
    const char *e = getenv(SOBER_APP488_FIX_ENV);
    return (e != NULL && strcmp(e, "1") == 0);
}

const char *sober_app488_fix_env_vars(void)
{
    return SOBER_APP488_FIX_ENV "=1 (enable; default OFF, diagnostic only; "
           "never evidence for the decision-013 marker)";
}

/* ---- object construction ----------------------------------------------- */
void *sober_app488_fix_build(void *guest_base, void *app)
{
    if (!sober_app488_fix_enabled() || !guest_base || !app)
        return NULL;

    guest_alloc_fn alloc = guest_allocator(guest_base);
    if (!alloc)
        return NULL;

    unsigned char *obj = (unsigned char *)alloc(SOBER_APP488_OBJ_SIZE);
    if (!obj) {
        fprintf(stderr, "[app488-fix] guest allocator %#x(%#x) failed\n",
                (unsigned)SOBER_APP488_RVA_ALLOCATOR,
                (unsigned)SOBER_APP488_OBJ_SIZE);
        return NULL;
    }
    memset(obj, 0, SOBER_APP488_OBJ_SIZE);

    /* The three genuine fields from InitWithParams 0x24af29d..0x24af2a4.
     * The vptr is a real guest vtable RVA plus the mapped base - never a
     * host function or a synthetic stub table. */
    uintptr_t vt = guest_vptr(guest_base);
    memcpy(obj + SOBER_APP488_OBJ_VPTR, &vt, sizeof(vt));
    memcpy(obj + SOBER_APP488_OBJ_APP, &app, sizeof(app));
    memcpy(obj + SOBER_APP488_OBJ_SELF, &obj, sizeof(obj));

    s_last_base = (unsigned char *)guest_base;
    s_last_app = app;
    s_last_object = obj;
    return obj;
}

void *sober_app488_fix_install(void *guest_base, void *app)
{
    if (!sober_app488_fix_enabled() || !guest_base || !app)
        return NULL;

    void **slot = (void **)((unsigned char *)app + SOBER_APP488_APP_SLOT);
    if (*slot) {
        /* Idempotent: genuine InitWithParams also reuses a non-NULL slot
         * (0x24af271/0x24af279 -> 0x24af288 path). */
        s_last_base = (unsigned char *)guest_base;
        s_last_app = app;
        s_last_object = *slot;
        return *slot;
    }

    void *obj = sober_app488_fix_build(guest_base, app);
    if (!obj)
        return NULL;

    *slot = obj;
    s_installs++;
    fprintf(stderr,
            "[app488-fix] installed guest-shaped object at app+%#x: obj=%p "
            "vptr=%#lx (= base %p + RVA %#x) app=%p self=%p size=%#x; "
            "no host stubs / no byte patches\n",
            (unsigned)SOBER_APP488_APP_SLOT, obj,
            (unsigned long)guest_vptr(guest_base), guest_base,
            (unsigned)SOBER_APP488_RVA_WRAPPER_VT, app, obj,
            (unsigned)SOBER_APP488_OBJ_SIZE);
    return obj;
}

void *sober_app488_fix_install_from_wrapper(void *guest_base)
{
    if (!sober_app488_fix_enabled() || !guest_base)
        return NULL;

    guest_wrapper_fn get_wrapper = guest_static_wrapper(guest_base);
    if (!get_wrapper) {
        fprintf(stderr, "[app488-fix] no static wrapper getter at RVA %#x\n",
                (unsigned)SOBER_APP488_RVA_GET_WRAPPER);
        return NULL;
    }
    void *wrapper = get_wrapper();
    if (!wrapper) {
        fprintf(stderr, "[app488-fix] guest static wrapper is NULL\n");
        return NULL;
    }
    void *app = *(void **)((unsigned char *)wrapper
                           + SOBER_APP488_SLOT_WRAPPER);
    if (!app) {
        fprintf(stderr, "[app488-fix] wrapper+%#x app pointer is NULL\n",
                (unsigned)SOBER_APP488_SLOT_WRAPPER);
        return NULL;
    }
    return sober_app488_fix_install(guest_base, app);
}

/* ---- diagnostics ------------------------------------------------------- */
const char *sober_app488_fix_describe(char *buf, size_t cap)
{
    if (!buf || !cap)
        return "";
    snprintf(buf, cap,
             "enabled=%d installs=%llu base=%p app=%p obj=%p "
             "vptr=%#lx (RVA %#x) alloc=%#lx (%#x RVA) "
             "env=%s, genuine guest RVAs only",
             sober_app488_fix_enabled(), (unsigned long long)s_installs,
             (void *)s_last_base, s_last_app, s_last_object,
             s_last_base ? (unsigned long)(s_last_base
                           + SOBER_APP488_RVA_WRAPPER_VT) : 0UL,
             (unsigned)SOBER_APP488_RVA_WRAPPER_VT,
             s_last_base ? (unsigned long)(s_last_base
                           + SOBER_APP488_RVA_ALLOCATOR) : 0UL,
             (unsigned)SOBER_APP488_RVA_ALLOCATOR,
             SOBER_APP488_FIX_ENV "=1");
    return buf;
}

uint64_t sober_app488_fix_installs(void)
{
    return s_installs;
}

void *sober_app488_fix_last_object(void)
{
    return s_last_object;
}
