/*
 * delegate_default.h - DEFAULT-ON genuine app+0x408 delegate installer
 *                      (Team 2 / worker T2-4, app-state track).
 *
 * Scope: NEW FILES ONLY -- delegate_default.{h,c}.  The lead owns src/linux/,
 * src/jni/CMakeLists.txt, tests, docs and git; the engine is frozen.  No CMake,
 * engine, byte-patch, callback or decision-013 marker edits are made here.
 *
 * Target engine: AppImage libroblox.so, BuildID 204ad049..., SHA-256
 * 3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335.
 * Every RVA below is from that frozen object; all target bytes are DATA.
 *
 * ---------------------------------------------------------------------------
 * WHY THIS MODULE EXISTS
 * ---------------------------------------------------------------------------
 * The genuine consumer in userDidLogin faults at 0x2772161 /
 * 0x320581b (`mov 0x408(%rdi),%rdi; mov (%rdi),%rax; call *0x90(%rax)`)
 * because app+0x408 is NULL: the Java entry that installs it
 * (nativeAppBridgeStartLuaAppDM 0x25448f6) is never reached by the harness.
 * The old SOBER_SEED_APP_DELEGATE fake installs a HOST no-op vtable
 * (sober_noop_virtual) and is disqualified by decision 013.
 *
 * This module makes a genuine guest delegate DEFAULT-ON.  It never invents a
 * vtable: it calls the guest's OWN lazy-singleton helpers and publishes the
 * guest's OWN object, or (as a fallback) allocates 0x50 bytes from the guest
 * allocator and stamps the guest's OWN constructor vptr.  The only host-side
 * operation is the single 8-byte store into app+0x408 that mirrors the guest's
 * own `mov %rax,0x408(%rbx)` at 0x25d7e75 / 0x25f7e7e / 0x24aed3e.
 *
 * ---------------------------------------------------------------------------
 * ENVIRONMENT
 * ---------------------------------------------------------------------------
 *   SOBER_DELEGATE_DEFAULT=0   KILL SWITCH.  Unset or any value != "0" means
 *                              ENABLED (default-ON).  With "0" every entry
 *                              point is a no-op returning SOBER_DD_DISABLED.
 *   SOBER_DELEGATE_SOURCE=A|B|FRESH   Select the object source.  Default AUTO,
 *                              which tries the site-A helper then the fresh
 *                              guest object (see below).
 *   SOBER_DELEGATE_VERIFY=image    Relax verification from executable .text to
 *                              the whole guest image.  Default = strict text.
 *                              Site B needs this relaxation: its +0x38 slot is
 *                              a .data.rel.ro vtable pointer (0x6dbb628), NOT
 *                              executable text (see EVIDENCE).
 *   SOBER_DELEGATE_CALL10=1     Explicit opt-in for the diagnostic slot +0x10
 *                              probe; honoured ONLY for the FRESH object,
 *                              where +0x10 is the pure int getter 0x321a85e.
 *   SOBER_DELEGATE_VERBOSE=1    Per-step stderr diagnostics ([dd] prefix).
 *
 * ---------------------------------------------------------------------------
 * GUEST RVAs (AppImage build only) AND EVIDENCE
 * ---------------------------------------------------------------------------
 *   site A helper 0x1dd55f2 -> static object 0x7d9d100, vptr 0x7220868.
 *     objdump 0x1dd55f2: guard byte 0x7d9d0f8; `lea 0x7220868,%rax` @0x1dd5618,
 *     `mov %rax,0x7d9d100` @0x1dd561f, `lea 0x7d9d100,%rax` @0x1dd5626,
 *     `mov %rax,0x7d9d0f0` @0x1dd562d (published singleton).  Consumer store
 *     `mov %rax,0x408(%rbx)` @0x25d7e75 after `call 1dd55f2` @0x25d7e5f.
 *     Slots (APS2 type-8 relocs, all into guest .text [0x1db9a40,0x6d3145a)):
 *       +0x10 -> 0x1dd56f2   +0x38 -> 0x6d28864   +0x90 -> 0x3281c9e
 *
 *   site B helper 0x25f7ff2(void *out) -> object 0x72a5a88, vptr 0x6dc3840;
 *     it also does a lock-xadd intrusive incref (0x25f806a).  Store
 *     `mov %rax,0x408(%rbx)` @0x25f7e7e.  Slots:
 *       +0x10 -> 0x2f26936   +0x38 -> 0x6dbb628 (.data.rel.ro, not text!)
 *       +0x90 -> 0x2f28426
 *
 *   fresh object: guest allocator 0x1dbab02 (0x50 bytes), ctor 0x252095e sets
 *     vptr 0x6e04ec0 (NOT the type/factory tag 0x6e047b0) and float 1.0 at
 *     +0x48; store `movups %xmm0,0x408(%rbx)` @0x24aed3e.  Slots:
 *       +0x10 -> 0x321a85e (`mov 0x20(%rdi),%eax; ret`)
 *       +0x38 -> 0x321a98c   +0x90 -> 0x276fd08
 *
 * ---------------------------------------------------------------------------
 * SLOT +0x10 CONTRACT (why we do NOT call it by default)
 * ---------------------------------------------------------------------------
 * The installer performs ZERO calls into the delegate.  The guest calls slot
 * +0x10 itself when its own path requires it:
 *   - site A: 0x25d7f45 calls +0x10 (0x1dd56f2) only when byte 0x7cdd149 is
 *     set, and IMMEDIATELY writes through the returned pointer at 0x25d7f55.
 *     Calling it outside that frame is unsafe (it is a factory, not a getter).
 *   - site B: +0x10 (0x2f26936) is a `lock xadd` refcount op that belongs to
 *     the helper's shared-ref frame (0x25f7e98 consumes that ref).
 *   - fresh: +0x10 (0x321a85e) is a pure int getter of field +0x20, the form
 *     used at 0x24aec73; calling it is a benign liveness probe.
 * Therefore sober_delegate_default_probe_slot10() runs a call ONLY when the
 * object is FRESH and SOBER_DELEGATE_CALL10=1 is explicitly set.  No other
 * path dispatches through the delegate from host code.
 *
 * ---------------------------------------------------------------------------
 * EXACT WIRING SIGNATURE FOR THE LEAD
 * ---------------------------------------------------------------------------
 * Include this header and call once, after the app pointer is known (the
 * existing seed_user_controller() site, where the static wrapper getter
 * 0x24ad6fc and wrapper+0x20 give `app`), and BEFORE the app-start RVAs:
 *
 *     #include "delegate_default.h"
 *     int dd_rc = sober_delegate_default_install_from_wrapper((void *)base);
 *
 * Wire it BEFORE the existing SOBER_SEED_APP_DELEGATE hook (or remove that
 * hook).  Both paths are idempotent on a non-NULL slot, so whichever runs
 * first wins; the host-no-op seed must not be the winner.
 *
 * Or with `app` already in hand (equivalent to A source by default):
 *
 *     int dd_rc = sober_delegate_default_install((void *)base, app);
 *
 * Decision 013: this is manufactured guest state, so OR
 * `sober_delegate_default_should_withhold()` into the `seeded_state` chain in
 * guest_app_shell.c.  Since the module is default-ON, that is effectively an
 * unconditional withhold while it is wired; never emit `app shell rendered`.
 * =========================================================================
 */
#ifndef SOBER_JNI_DELEGATE_DEFAULT_H
#define SOBER_JNI_DELEGATE_DEFAULT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SOBER_DD_BUILDID32 0x204ad049u

/* Environment variables. */
#define SOBER_DD_ENV          "SOBER_DELEGATE_DEFAULT"
#define SOBER_DD_SOURCE_ENV   "SOBER_DELEGATE_SOURCE"
#define SOBER_DD_VERIFY_ENV   "SOBER_DELEGATE_VERIFY"
#define SOBER_DD_CALL10_ENV   "SOBER_DELEGATE_CALL10"
#define SOBER_DD_VERBOSE_ENV  "SOBER_DELEGATE_VERBOSE"

/* Result codes (all entry points return one of these; 0 == success). */
enum {
    SOBER_DD_OK          =   0,
    SOBER_DD_ERR         =  -1,  /* bad arguments / helper failed            */
    SOBER_DD_DISABLED    =  -2,  /* SOBER_DELEGATE_DEFAULT=0                 */
    SOBER_DD_PRESENT     =  -3,  /* app+0x408 already non-NULL: left alone   */
    SOBER_DD_BAD_OBJ     =  -4,  /* helper/allocator produced no object      */
    SOBER_DD_BAD_VTABLE  =  -5,  /* vptr or +0x10/+0x38/+0x90 not genuine    */
    SOBER_DD_NO_APP      =  -6   /* wrapper getter or wrapper+0x20 is NULL   */
};

/* Frozen-engine RVAs and section bounds (AppImage build 204ad049 only). */
enum {
    SOBER_DD_RVA_GET_WRAPPER = 0x24ad6fcu, /* static app wrapper getter       */
    SOBER_DD_WRAPPER_APP     = 0x20u,       /* wrapper +0x20 -> app            */
    SOBER_DD_APP_SLOT        = 0x408u,      /* delegate slot inside app        */
    SOBER_DD_RVA_HELPER_A    = 0x1dd55f2u,  /* site A lazy singleton getter    */
    SOBER_DD_RVA_OBJ_A       = 0x7d9d100u,  /* site A static object            */
    SOBER_DD_RVA_VT_A        = 0x7220868u,  /* site A genuine vtable           */
    SOBER_DD_RVA_HELPER_B    = 0x25f7ff2u,  /* site B lazy singleton getter    */
    SOBER_DD_RVA_OBJ_B       = 0x72a5a88u,  /* site B static object            */
    SOBER_DD_RVA_VT_B        = 0x6dc3840u,  /* site B genuine vtable           */
    SOBER_DD_RVA_ALLOC       = 0x1dbab02u,  /* guest allocator(size_t)         */
    SOBER_DD_OBJ_SIZE        = 0x50u,       /* ctor 0x252095e object size      */
    SOBER_DD_RVA_VT_FRESH    = 0x6e04ec0u,  /* genuine factory object vptr     */
    SOBER_DD_RVA_TEXT_LO     = 0x1db9a40u,  /* .text start                     */
    SOBER_DD_RVA_TEXT_HI     = 0x6d3145au,  /* .text end (start+0x4f77a1a)     */
    SOBER_DD_RVA_IMAGE_END   = 0x7d9d220u   /* past last PT_LOAD (BSS incl.)   */
};

typedef enum SoberDelegateSource {
    SOBER_DD_SOURCE_AUTO  = 0,  /* site A, then fresh fallback            */
    SOBER_DD_SOURCE_A     = 1,  /* helper 0x1dd55f2 -> 0x7d9d100         */
    SOBER_DD_SOURCE_B     = 2,  /* helper 0x25f7ff2 -> 0x72a5a88         */
    SOBER_DD_SOURCE_FRESH = 3   /* guest alloc(0x50) + vptr 0x6e04ec0    */
} SoberDelegateSource;

typedef enum SoberDelegateVerify {
    SOBER_DD_VERIFY_TEXT  = 0,  /* strict: slots inside executable .text  */
    SOBER_DD_VERIFY_IMAGE = 1   /* relaxed: anywhere in the guest image   */
} SoberDelegateVerify;

/* ---- enable / diagnostics ------------------------------------------------ */
/* Default ON; only the exact value "0" of SOBER_DELEGATE_DEFAULT disables. */
int         sober_delegate_default_enabled(void);
const char *sober_delegate_default_env_vars(void);
/* Non-zero while this module is wired and enabled; OR into seeded_state. */
int         sober_delegate_default_should_withhold(void);
const char *sober_delegate_default_source_name(SoberDelegateSource source);
const char *sober_delegate_default_describe(char *buf, size_t cap);
uint64_t    sober_delegate_default_installs(void);
void       *sober_delegate_default_last_object(void);
void        sober_delegate_default_reset(void);

/* ---- lead-facing install API -------------------------------------------- */
/* install():   env-driven source/verify, default install path.
 * install_ex(): explicit source/verify; optional bounded trace buffer.
 * install_from_wrapper(): resolve wrapper via 0x24ad6fc, app = wrapper+0x20,
 *               then install().  All return SOBER_DD_* codes, never a pointer.
 * A non-NULL pre-existing app+0x408 is returned as SOBER_DD_PRESENT, untouched. */
int sober_delegate_default_install(void *guest_base, void *app);
int sober_delegate_default_install_ex(void *guest_base, void *app,
                                      SoberDelegateSource source,
                                      SoberDelegateVerify verify,
                                      char *log, size_t cap);
int sober_delegate_default_install_from_wrapper(void *guest_base);

/* ---- read-only / verification helpers ----------------------------------- */
void *sober_delegate_default_ptr(void *app);

/* Strict (guest-.text) verification of an existing object; returns SOBER_DD_OK
 * when vptr matches the source and slots +0x10/+0x38/+0x90 are guest text. */
int sober_delegate_default_check(void *guest_base, void *obj,
                                 SoberDelegateSource source,
                                 char *log, size_t cap);

/* Conditional slot +0x10 call.  Calls into the delegate ONLY for a FRESH
 * object (pure getter 0x321a85e) AND SOBER_DELEGATE_CALL10=1; returns 1 when a
 * call was made (value in *out), 0 otherwise.  Never called by install(). */
int sober_delegate_default_probe_slot10(int *out_value);

#ifdef __cplusplus
}
#endif
#endif /* SOBER_JNI_DELEGATE_DEFAULT_H */
