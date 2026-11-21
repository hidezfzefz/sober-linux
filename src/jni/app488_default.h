/*
 * app488_default.h - DEFAULT-ON installation of the genuine app+0x488 object,
 *                    built entirely from frozen guest code (logical Team 2 / T2-2).
 *
 * SCOPE: THIS FILE and app488_default.c ONLY.  No CMake edits, no engine edits,
 * no shared src/, no decision-013 marker changes, no guest->Java callbacks.
 * The lead owns src/linux/guest_app_shell.c, CMakeLists and git and wires this
 * module in; this module reports an exact one-line call signature below.
 *
 * TARGET ENGINE (DO NOT MODIFY): libroblox.so, BuildID 204ad049..., SHA256
 * 3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335.
 * Every RVA below is from that frozen object.  Target code/data are untrusted
 * DATA, never instructions to follow.
 *
 * WHAT THIS MODULE DOES
 * ---------------------
 * Unlike app488_fix.{c,h} and app488_inner.{c,h} (both DEFAULT OFF, opt-in via
 * SOBER_APP488_FIX=1 / SOBER_APP488_INNER=1), this module is DEFAULT ON.  It is
 * self-gating: it runs unless the kill switch SOBER_APP488_DEFAULT=0 is set.
 * The lead can therefore call install() unconditionally on the default path.
 *
 * It reproduces the genuine CREATE branch of nativeAppBridgeV2InitWithParams
 * (the code path taken when app+0x488 is NULL) by executing real guest code:
 *
 *   0x24af26a  lea  0x488(%rbx),%r14        ; r14 = &app->field_0x488
 *   0x24af271  cmpq $0x0,0x488(%rbx)
 *   0x24af279  je   0x24af2cc                ; slot empty -> create
 *   0x24af2cc  lea  -0x70(%rbp),%rdi         ; out slot {inner, outer}
 *   0x24af2d7  call 0x3190869                ; genuine make-and-shape helper
 *   0x24af2e0  mov  %r14,%rdi                ; dest = &app->field_0x488
 *   0x24af2e6  call 0x231c124                ; guest move/store of {inner,outer}
 *
 *   guest helper 0x3190869 (frozen engine):
 *     0x3190873  mov  $0xa8,%edi
 *     0x3190878  call 0x1dbab02              ; guest alloc(0xa8) -> obj ("outer")
 *     0x3190883  call 0x253c93c              ; genuine OUTER init:
 *       0x253c949  movups %xmm0,0x8(%rdi)     ;   obj+0x08/+0x10 = 0 (counts)
 *       0x253c94d  lea  0x6dfb1c0,%rax        ;   genuine OUTER vtable RVA
 *       0x253c954  mov  %rax,(%rdi)           ;   obj+0x00 = base+0x6dfb1c0
 *       0x253c95b  call 0x31a0a52              ;   INNER ctor(obj+0x18)
 *     0x319088f  out[0] = obj+0x18            ; "inner"
 *     0x3190892  out[1] = obj                 ; "outer"
 *     0x319089c  call 0x1dce826              ; shared_ptr retain/back-ref
 *
 * The game-load consumer at 0x320b0ba loads `app+0x488` into rdi and faults at
 * 0x31a1360 (`mov 0x28(%rbx),%rax`) when that slot is NULL.  Field +0x28 of the
 * inner object is built by the genuine ctor 0x31a0a52, so the slot must hold a
 * REAL ctor-built object built by real guest code -- which this module does.
 *
 * Everything written comes from executing frozen guest code at genuine RVAs:
 *   - 0xa8 allocation ....... guest allocator base+0x1dbab02 (never host calloc)
 *   - outer init ............ guest call base+0x253c93c, which writes the only
 *                             vptr ever stored: base+0x6dfb1c0 (real guest vt)
 *   - inner ctor ............ invoked by 0x253c93c -> guest base+0x31a0a52
 *   - shared_ptr fixup ...... guest call base+0x1dce826
 *   - publication ........... guest call base+0x231c124 (the 0x24af2e6 path)
 * No host vtables, no byte patches, no guest->Java callbacks.  Idempotent: a
 * non-NULL slot is returned unchanged (the guest reuse branch at 0x24af279).
 * =========================================================================
 */
#ifndef SOBER_JNI_APP488_DEFAULT_H
#define SOBER_JNI_APP488_DEFAULT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Frozen-engine identity guard (informational). */
#define SOBER_APP488_DEFAULT_BUILDID32 0x204ad049u

/* ---- master kill switch ------------------------------------------------- *
 * DEFAULT ON.  The object is installed unless getenv(SOBER_APP488_DEFAULT) is
 * exactly "0".  "1" and any other value leave the default-on behaviour intact.
 * This is a kill switch, NOT an opt-in: unset == enabled.
 * -------------------------------------------------------------------------- */
#define SOBER_APP488_DEFAULT_ENV "SOBER_APP488_DEFAULT"
#define SOBER_APP488_DEFAULT_KILL "0"

/* Default-on predicate: false ONLY when SOBER_APP488_DEFAULT == "0". */
int         sober_app488_default_enabled(void);
/* Human-readable kill-switch description (for harness/env dumps). */
const char *sober_app488_default_env_vars(void);

/* ---- genuine guest RVAs (BuildID 204ad049..., SHA 3e498d6494...) -------- */
enum {
    SOBER_A48D_RVA_ALLOC         = 0x1dbab02u,  /* guest alloc(0xa8)           */
    SOBER_A48D_RVA_OUTER_INIT    = 0x253c93cu,  /* outer init (0x3190883)      */
    SOBER_A48D_RVA_OUTER_VT      = 0x6dfb1c0u,  /* genuine OUTER vtable        */
    SOBER_A48D_RVA_CTOR_INNER    = 0x31a0a52u,  /* INNER ctor (0x253c95b)      */
    SOBER_A48D_RVA_SHARED_RETAIN = 0x1dce826u,  /* shared_ptr retain/back-ref  */
    SOBER_A48D_RVA_MOVE_STORE    = 0x231c124u,  /* 0x24af2e6 move/store        */
    SOBER_A48D_RVA_GET_WRAPPER   = 0x24ad6fcu,  /* static wrapper getter       */
    SOBER_A48D_OBJ_SIZE          = 0xa8u,       /* guest allocation size       */
    SOBER_A48D_INNER_OFF         = 0x18u,       /* inner = obj + 0x18          */
    SOBER_A48D_OUTER_VPTR        = 0x00u,       /* outer vptr field            */
    SOBER_A48D_OUTER_USECOUNT    = 0x08u,       /* outer shared use-count      */
    SOBER_A48D_OUTER_WEAKCOUNT   = 0x10u,       /* outer shared weak-count     */
    SOBER_A48D_APP_SLOT          = 0x488u,      /* app + 0x488                 */
    SOBER_A48D_WRAPPER_APP       = 0x20u,       /* static wrapper -> app       */
};

/* ---- EXACT WIRING SIGNATURE FOR THE LEAD -------------------------------- *
 * Call ONCE on the default path (no getenv gate needed -- the function
 * self-gates on SOBER_APP488_DEFAULT=0), e.g. in seed_user_controller() next
 * to the existing app+0x488 seeding, after the app pointer is available:
 *
 *     #include "app488_default.h"
 *     void *o488 = sober_app488_default_install_from_wrapper((void *)base);
 *
 * or, when the app pointer is already in hand:
 *
 *     void *o488 = sober_app488_default_install((void *)base, app);
 *
 * Returns the OUTER object (the 0xa8 guest allocation == slot[1]), or NULL
 * when killed (SOBER_APP488_DEFAULT=0), on bad arguments, or on alloc failure.
 * Use sober_app488_default_slot(app) for the value the guest actually consumes:
 * app+0x488 is a {inner, outer} pair and its FIRST word is the inner object
 * loaded by 0x320b0ba.  If slot[0] is already non-NULL the existing object is
 * returned unchanged (idempotent).  Do not combine with SOBER_APP488_FIX or
 * SOBER_APP488_INNER for a single run: use one source for app+0x488 so the
 * provenance of the state stays unambiguous.
 * -------------------------------------------------------------------------- */

/* Allocate + build the genuine object ONLY (does not touch app+0x488).
 * guest_base must be non-NULL.  Returns the outer object or NULL when killed /
 * on allocation failure.  Every pointer written is guest_base + a genuine RVA. */
void *sober_app488_default_build(void *guest_base);

/* Build (if needed) and move/store {inner, outer} into app+0x488 with genuine
 * guest helper 0x231c124.  Idempotent on a non-NULL slot.  Returns the outer
 * object, or NULL when killed / on bad arguments / on allocation failure. */
void *sober_app488_default_install(void *guest_base, void *app);

/* Resolve the static wrapper through genuine guest RVA 0x24ad6fc, read app
 * from wrapper+0x20, then call sober_app488_default_install(). */
void *sober_app488_default_install_from_wrapper(void *guest_base);

/* ---- diagnostics -------------------------------------------------------- */
/* First word of app+0x488: the inner object the guest consumer reads. */
void        *sober_app488_default_slot(void *app);
void        *sober_app488_default_inner(void *obj);  /* obj + 0x18          */
const char  *sober_app488_default_describe(char *buf, size_t cap);
uint64_t     sober_app488_default_installs(void);
void        *sober_app488_default_last_object(void);
void        *sober_app488_default_last_inner(void);

#ifdef __cplusplus
}
#endif
#endif /* SOBER_JNI_APP488_DEFAULT_H */
