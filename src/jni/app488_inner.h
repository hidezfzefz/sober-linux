/*
 * app488_inner.h - install the GENUINE app+0x488 object with real guest code
 *                  (game-fault worker A12, logical Team A wave 2).
 *
 * Scope: THIS FILE and app488_inner.c ONLY.  No CMake edits, no engine edits,
 * no byte patches, no decision-013 marker changes, no guest->Java callbacks.
 *
 * WHY THIS MODULE EXISTS (correction to app488_fix.{c,h})
 * -------------------------------------------------------
 * The earlier A1 module shaped a small stack-shaped wrapper: vptr 0x6e02918 at
 * +0x00, app at +0x08, self at +0x20.  That is the *reuse* branch seen at
 * 0x24af288..0x24af2a4 of nativeAppBridgeV2InitWithParams and is NOT the object
 * the genuine CREATE branch stores.  The create path is:
 *
 *   0x24af26a  lea  0x488(%rbx),%r14        ; r14 = &app->field_0x488
 *   0x24af271  cmpq $0x0,0x488(%rbx)
 *   0x24af279  je   0x24af2cc                ; slot empty -> create
 *   0x24af2cc  lea  -0x70(%rbp),%rdi         ; out slot {inner, outer}
 *   0x24af2d0  lea  -0xd0(%rbp),%rsi         ; (stack wrapper, not used here)
 *   0x24af2d7  call 0x3190869                ; genuine make-and-shape helper
 *   0x24af2e0  mov  %r14,%rdi                ; dest = &app->field_0x488
 *   0x24af2e6  call 0x231c124                ; guest move/store into the slot
 *
 *   helper 0x3190869 (frozen engine, BuildID 204ad049...):
 *     0xa8 bytes from guest allocator 0x1dbab02  -> obj  ("outer" object)
 *     0x253c93c(obj):
 *         obj+0x00 = base + 0x6dfb1c0   (genuine OUTER vtable)
 *         obj+0x08 = obj+0x10 = 0       (shared_ptr use/weak counts)
 *         call 0x31a0a52(obj+0x18)      (genuine INNER ctor)
 *     0x1dce826(out, inner, inner)      (guest shared_ptr retain/back-ref)
 *     out[0] = obj+0x18 ("inner"), out[8] = obj ("outer")
 *
 * The game-load consumer faults at 0x31a1360 (`mov 0x28(%rbx),%rax`) when it
 * is handed NULL from `0x320b0ba: mov 0x488(%rbx),%rdi`.  The field +0x28 of
 * the inner object is built by ctor 0x31a0a52 from guest 0x3117a2e.  Therefore
 * the slot must contain a REAL ctor-built inner object, not a synthetic one.
 *
 * This module performs the genuine guest calls of the create path (plus the
 * guest outer-init data and shared_ptr fixup that helper 0x3190869 performs):
 *   1. guest allocator       base + 0x1dbab02   alloc(0xa8)
 *   2. guest inner ctor      base + 0x31a0a52   ctor(obj + 0x18)
 *   3. guest move/store      base + 0x231c124   store {inner, outer} -> app+0x488
 * No host vtables (the only vptr written is the genuine guest RVA 0x6dfb1c0),
 * no byte patches, no callbacks.  Result is idempotent: a non-NULL slot is
 * returned unchanged, exactly like the guest's reuse branch at 0x24af279.
 *
 * DEFAULT OFF: nothing is allocated or written unless
 * getenv("SOBER_APP488_INNER") is exactly "1".
 *
 * ONE-LINE HOOK FOR THE LEAD (guest_app_shell.c, next to the existing
 * SOBER_APP488_FIX hook in seed_user_controller()):
 *
 *   if (getenv("SOBER_APP488_INNER")) {
 *       void *o = sober_app488_inner_install_from_wrapper((void *)base);
 *       fprintf(stderr, "[app-shell] app488_inner -> %p\n", o);
 *   }
 *
 * or with the app pointer already in hand:
 *
 *   void *o = sober_app488_inner_install((void *)base, app);
 *
 * Do NOT enable SOBER_APP488_FIX and SOBER_APP488_INNER at the same time: use
 * one source for app+0x488 so the provenance of the state stays unambiguous.
 * =========================================================================
 */
#ifndef SOBER_JNI_APP488_INNER_H
#define SOBER_JNI_APP488_INNER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Frozen engine identity guard (informational). */
#define SOBER_APP488_INNER_BUILDID32 0x204ad049u

/* ---- opt-in ------------------------------------------------------------ */
#define SOBER_APP488_INNER_ENV "SOBER_APP488_INNER"

/* True only when getenv(SOBER_APP488_INNER_ENV) == "1" (default OFF). */
int         sober_app488_inner_enabled(void);
const char *sober_app488_inner_env_vars(void);

/* ---- genuine guest RVAs (BuildID 204ad049..., SHA 3e498d6494...) ------- */
enum {
    SOBER_A48I_RVA_ALLOC         = 0x1dbab02u,  /* guest alloc(0xa8)          */
    SOBER_A48I_RVA_CTOR_INNER    = 0x31a0a52u,  /* genuine inner ctor         */
    SOBER_A48I_RVA_OUTER_INIT    = 0x253c93cu,  /* outer init -> calls ctor   */
    SOBER_A48I_RVA_SHARED_RETAIN = 0x1dce826u,  /* shared_ptr retain/back-ref */
    SOBER_A48I_RVA_MOVE_STORE    = 0x231c124u,  /* 0x24af2e6 move/store       */
    SOBER_A48I_RVA_GET_WRAPPER   = 0x24ad6fcu,  /* static wrapper getter      */
    SOBER_A48I_RVA_OUTER_VT      = 0x6dfb1c0u,  /* genuine OUTER vtable       */
    SOBER_A48I_RVA_REUSE_VT      = 0x6e02918u,  /* reuse-branch vt (A1; info) */
    SOBER_A48I_OBJ_SIZE          = 0xa8u,       /* alloc size                */
    SOBER_A48I_INNER_OFF         = 0x18u,       /* inner = obj + 0x18        */
    SOBER_A48I_OUTER_VPTR        = 0x00u,       /* outer vptr field          */
    SOBER_A48I_OUTER_USECOUNT    = 0x08u,       /* outer shared use-count    */
    SOBER_A48I_OUTER_WEAKCOUNT   = 0x10u,       /* outer shared weak-count   */
    SOBER_A48I_APP_SLOT          = 0x488u,      /* app + 0x488               */
    SOBER_A48I_WRAPPER_APP       = 0x20u,       /* static wrapper -> app     */
};

/* ---- lead-facing API --------------------------------------------------- *
 * build():    calls the guest allocator + the genuine inner ctor and the
 *             guest shared_ptr fixups; does NOT touch app+0x488.
 * install():  build(), then move/store {inner, outer} into app+0x488 with the
 *             genuine guest helper 0x231c124.  Idempotent on a non-NULL slot.
 * install_from_wrapper(): resolve the static wrapper via 0x24ad6fc, read app
 *             from wrapper+0x20, then call install().
 * All three return the OUTER object (the 0xa8 guest allocation, i.e. slot[1]),
 * or NULL when disabled, on bad arguments, or on allocation failure.  Use
 * sober_app488_inner_slot(app) for the value the guest actually consumes:
 * app+0x488 is a {inner, outer} pair and its FIRST word is the inner object
 * (that is what 0x320b0ba loads into rdi before calling 0x31a1168).
 * ------------------------------------------------------------------------ */
void *sober_app488_inner_build(void *guest_base);
void *sober_app488_inner_install(void *guest_base, void *app);
void *sober_app488_inner_install_from_wrapper(void *guest_base);

/* ---- diagnostics ------------------------------------------------------- */
/* First word of app+0x488: the inner object the guest consumer reads. */
void        *sober_app488_inner_slot(void *app);
void        *sober_app488_inner_inner(void *obj);  /* obj + 0x18             */
const char  *sober_app488_inner_describe(char *buf, size_t cap);
uint64_t     sober_app488_inner_installs(void);
void        *sober_app488_inner_last_object(void);
void        *sober_app488_inner_last_inner(void);

#ifdef __cplusplus
}
#endif
#endif /* SOBER_JNI_APP488_INNER_H */
