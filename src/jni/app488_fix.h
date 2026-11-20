/*
 * app488_fix.h - faithful guest-shaped app+0x488 object (game-fault worker A1).
 *
 * Scope: this file + app488_fix.c only (NEW files).  No CMake, no engine,
 * no marker, no guest byte patches.
 *
 * WHAT THIS FIXES
 * ---------------
 * The game-load path calls guest 0x31a1168 with
 *
 *     rdi = *(app + 0x488)          (guest 0x320b0ba)
 *
 * and faults at 0x31a1360 (`mov 0x28(%rbx),%rax`, si_addr = 0x28) when that
 * slot is NULL.  The genuine writer is `nativeAppBridgeV2InitWithParams`:
 *
 *     0x24af26a  lea  0x488(%rbx),%r14       ; r14 = &app->field_0x488
 *     0x24af271  cmpq $0x0,0x488(%rbx)
 *     0x24af279  je   0x24af2cc               ; create branch
 *     ... reuse branch (slot non-NULL):
 *     0x24af288  mov  0x488(%rbx),%rdi
 *     0x24af28f  lea  0x6e02918,%rax          ; genuine vtable RVA
 *     0x24af296  lea  -0xd0(%rbp),%r15        ; guest-shaped wrapper
 *     0x24af29d  mov  %rax,(%r15)              ; +0x00 = vptr (0x6e02918)
 *     0x24af2a0  mov  %rbx,0x8(%r15)           ; +0x08 = app
 *     0x24af2a4  mov  %r15,0x20(%r15)          ; +0x20 = self
 *     ... create branch (slot NULL):
 *     0x24af2cc  lea  -0x70(%rbp),%rdi
 *     0x24af2d0  lea  -0xd0(%rbp),%rsi
 *     0x24af2d7  call 0x3190869                ; guest alloc(0xa8) + ctor
 *     0x24af2e0  mov  %r14,%rdi                ; dest = &app->field_0x488
 *     0x24af2e6  call 0x231c124                ; guest move/store into slot
 *
 * Relocation check (`python3 /tmp/opencode/reloc.py dump 0x6e02918`) confirms
 * 0x6e02918 is a genuine relocated guest vtable (slots 0x320e700, 0x320e714,
 * ...; the 0x320e714 slot itself calls the guest allocator 0x1dbab02).
 *
 * This module reproduces that object faithfully using GENUINE guest RVAs:
 *   - allocated with the guest allocator at base+0x1dbab02 (never host calloc),
 *   - vptr = base+0x6e02918 (a real guest vtable, never a host stub table),
 *   - +0x08 = the guest app pointer, +0x20 = the object's own address,
 *   - installed into app+0x488 only when that slot is NULL (idempotent),
 *   - no guest byte patches, no callbacks, no decision-013 marker changes.
 *
 * All engine addresses are RVAs against the caller-supplied guest base for
 * the frozen engine (BuildID 204ad049..., SHA 3e498d6494...).
 *
 * DEFAULT OFF: nothing is allocated or written unless
 * getenv("SOBER_APP488_FIX") is exactly "1".
 */
#ifndef SOBER_JNI_APP488_FIX_H
#define SOBER_JNI_APP488_FIX_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Build identity of the engine these RVAs belong to (informational guard). */
#define SOBER_APP488_FIX_BUILDID32 0x204ad049u

/* ---- opt-in ------------------------------------------------------------ */
#define SOBER_APP488_FIX_ENV "SOBER_APP488_FIX"

/* Default OFF: true only when getenv(SOBER_APP488_FIX_ENV) == "1". */
int         sober_app488_fix_enabled(void);
/* Human-readable opt-in description (for harness/env dumps). */
const char *sober_app488_fix_env_vars(void);

/* ---- genuine guest RVAs (BuildID 204ad049 / SHA 3e498d6494...) --------- */
#define SOBER_APP488_RVA_ALLOCATOR   0x1dbab02u  /* guest allocate(size_t)   */
#define SOBER_APP488_RVA_WRAPPER_VT  0x6e02918u  /* vtable from 0x24af28f    */
#define SOBER_APP488_RVA_GET_WRAPPER 0x24ad6fcu  /* static wrapper getter    */

/* Genuine allocation size: helper 0x3190869 does `mov $0xa8,%edi; call
 * 0x1dbab02` for the object whose wrapper is stored at app+0x488. */
#define SOBER_APP488_OBJ_SIZE        0xa8u

/* Field offsets (see writer above). */
#define SOBER_APP488_APP_SLOT        0x488u      /* app + 0x488 = slot       */
#define SOBER_APP488_SLOT_WRAPPER    0x20u       /* static wrapper -> app    */
#define SOBER_APP488_OBJ_VPTR        0x00u
#define SOBER_APP488_OBJ_APP         0x08u
#define SOBER_APP488_OBJ_SELF        0x20u

/* ---- lead-facing API --------------------------------------------------- *
 * ONE-LINE HOOK FOR THE LEAD (in guest_app_shell.c, after the app pointer is
 * available, e.g. next to the existing SOBER_SEED_APP_488 call):
 *
 *   void *o = sober_app488_fix_install_from_wrapper(base);
 *
 * or, when the app pointer is already in hand:
 *
 *   void *o = sober_app488_fix_install(base, app);
 *
 * Both are no-ops returning NULL unless SOBER_APP488_FIX=1.  When enabled they
 * return the shaped object (or the already-installed object if the slot is
 * non-NULL).  Do not combine with the inline SOBER_SEED_APP_488 seed for A/B;
 * use one or the other so the source of the state is unambiguous.
 * ------------------------------------------------------------------------ */

/* Allocate + shape the object ONLY (does not touch app+0x488).  Both
 * `guest_base` and `app` must be non-NULL.  Returns NULL when disabled or on
 * allocation failure.  Every pointer written is guest_base + a genuine RVA. */
void *sober_app488_fix_build(void *guest_base, void *app);

/* Build (if needed) and store into app+0x488.  Idempotent: if the slot is
 * already non-NULL it is returned unchanged and no new allocation is made.
 * Returns NULL when disabled, on bad arguments, or on allocation failure. */
void *sober_app488_fix_install(void *guest_base, void *app);

/* Convenience for the call site in guest_app_shell.c: obtain the static
 * wrapper through genuine guest RVA 0x24ad6fc, read the app pointer from
 * wrapper+0x20, then call sober_app488_fix_install().  Returns NULL when
 * disabled, when the wrapper/app is NULL, or on allocation failure. */
void *sober_app488_fix_install_from_wrapper(void *guest_base);

/* ---- diagnostics ------------------------------------------------------- */
/* Fill `buf` with a one-line status dump; returns buf. */
const char *sober_app488_fix_describe(char *buf, size_t cap);
/* Number of successful installations this process (0 when never enabled). */
uint64_t    sober_app488_fix_installs(void);
/* Address of the last built/installed object (NULL if none). */
void       *sober_app488_fix_last_object(void);

#ifdef __cplusplus
}
#endif
#endif /* SOBER_JNI_APP488_FIX_H */
