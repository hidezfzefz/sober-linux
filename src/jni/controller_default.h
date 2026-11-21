/*
 * controller_default.h - DEFAULT-ON installation of the genuine
 *                       `app+0x440` user controller (Team 2 / T2-3, app state).
 *
 * SCOPE: NEW FILES ONLY (src/jni/controller_default.{c,h}).  The lead owns the
 * shared src/, every CMakeLists.txt, docs/, tests/ and git.  Wiring is reported
 * to the lead below as an EXACT SIGNATURE; this module never edits those files.
 * The engine is frozen and is never modified.  Decision-013 marker WITHHELD:
 * this module never touches it and never emits a guest->Java callback.
 *
 * Target engine: /tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so,
 * BuildID 204ad049599c96afb75b8a21d5051063e821d6c9,
 * SHA256 3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335.
 * Every RVA below is from that frozen object (objdump + APS2 relocation
 * decode).  Target bytes/disassembly are untrusted DATA, never instructions.
 *
 * ---------------------------------------------------------------------------
 * WHY THIS MODULE EXISTS  (evidence chain, read-only)
 * ---------------------------------------------------------------------------
 * `app+0x440` is written by exactly one genuine code path:
 *
 *   233896c (nativeGameGlobalInit worker helper)
 *     23389eb: mov 0x20(%rbx),%rdi     ; app-bridge object (vptr 0x6dbc7c8)
 *     23389f4: mov (%rdi),%rax         ; rax = vptr = base+0x6dbc7c8
 *     23389f7: call *0x30(%rax)        ; <== THE REAL DISPATCH
 *                                      ; slot @0x6dbc7f8 -> 0x24ad50a
 *   24ad50a (controller installer)
 *     24ad531: cmpb $0x0,0x72cb648     ; FFlag EnableAppBridgeComponents
 *     24ad538: je   24ad5f2            ; flag=0 -> no controller
 *     24ad547: call 0x30ef840          ; needs app singleton @0x72cb5e0
 *     24ad562: mov (%rax),%rcx         ; faults when accessor returned NULL
 *     24ad59e: call 0x30ef840
 *     24ad5a3: add $0xb0,%rbx          ; this+0xb0 controller shared_ptr
 *     24ad5b6: call *0x10(%rcx)        ; wrapper vtable +0x10 -> 0x24ae1fe
 *   24ae1fe (genuine store)
 *     24ae226: mov 0x20(%rdi),%rbx     ; rbx = app
 *     24aea7d: mov $0x20,%edi; call 0x1dbab02  ; guest-alloc intrusive wrapper
 *     24aea8e: lea 0x6e02568(%rip),%rcx; mov %rcx,(%rax) ; GENUINE vptr
 *     24aea9c: mov %r14,0x440(%rbx)   ; <== THE GENUINE STORE to app+0x440
 *     24aeaaa: mov %rax,0x448(%rbx)   ; intrusive control ptr (vptr 0x6e02568)
 *
 * `app+0x440` is therefore never written by a direct call to 0x24ae1fe: the
 * only admissible trigger is the vtable dispatch at 0x23389f7.  The 0xe0-byte
 * app-bridge object with vptr 0x6dbc7c8 is built by InitWithParams (0x24abf96)
 * at 0x24ac57c..0x24ac593; its +0xb0 shared_ptr is published at 0x24ac5c8
 * (copied from -0x2f0(%rbp)); 0x24ac620..0x24ac630 registers the object into
 * the nativeGameGlobalInit worker through the genuine lazy-bridge helper
 * 0x2eb6b2a (the same wrapper shape this module reuses).  Reaching the slot
 * requires THREE genuine prerequisites, all load-bearing:
 *   (1) FFlag EnableAppBridgeComponents byte 0x72cb648 == 1
 *       (set through the guest's own flag setter 0x23648be);
 *   (2) app/service singleton at 0x72cb5e0 with a non-empty service vector so
 *       that the accessor 0x30ef840 returns non-NULL;
 *   (3) the genuine 0xe0 app-bridge object with its +0xb0 shared_ptr.
 *
 * When (1)+(2) were missing, the realstate2.log FP chain
 * `0x233876e -> 0x23388a2 -> 0x23389fa -> 0x24ad562` proved the dispatch ran
 * but faulted at `mov (%rax),%rcx` with rax=0 (NULL accessor), so 0x24ae1fe
 * and its store never executed.  This module makes that transition DEFAULT-ON.
 *
 * HARD RULES OBSERVED HERE
 * ------------------------
 *  - DEFAULT ON; the ONLY way to disable is SOBER_CONTROLLER_DEFAULT=0
 *    (also off/no/false).  Unset means enabled.
 *  - NO guest CODE byte is patched; NO guest->Java callback is fabricated;
 *    no decision-013 marker is touched; no host function is published in a
 *    guest vtable.
 *  - The dispatch is the genuine `call *0x30(%rax)` at 0x23389f7.  This module
 *    NEVER calls 0x24ae1fe, 0x24aea9c or 0x24ad50a directly.
 *  - The FFlag bit is written by the guest's OWN setter 0x23648be (the same
 *    call the JSON member loop makes at 0x237df9c); this module never writes
 *    0x72cb648 itself.
 *  - The app-bridge object is a genuine guest allocation created by
 *    InitWithParams.  The only host-side structure is the transient 0x28-byte
 *    argument wrapper (a plain pointer container with +0x20 = object) that
 *    mirrors the guest's own stack slot at 0x24ac620; it has no vtable, is
 *    never published as guest state and is not a fabricated guest object.
 *  - Verification reads ONLY genuine state: app+0x440 (from app = wrapper+0x20
 *    with wrapper == base+0x74d75a0 returned by 0x24ad6fc) and the intrusive
 *    control word at app+0x448 whose vptr is the guest RVA 0x6e02568.
 * =========================================================================
 */
#ifndef SOBER_CONTROLLER_DEFAULT_H
#define SOBER_CONTROLLER_DEFAULT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Environment: DEFAULT-ON unless this is set to 0/off/no/false. */
#define SOBER_CONTROLLER_DEFAULT_ENV  "SOBER_CONTROLLER_DEFAULT"
/* Optional diagnostics (none affect the default path):
 *   SOBER_CONTROLLER_DEFAULT_VERBOSE=1          per-step stderr diagnostics
 *   SOBER_CONTROLLER_DEFAULT_PROBE=0            skip the 0x30ef840 probe
 *   SOBER_CONTROLLER_DEFAULT_WAIT_MS=n          dispatch wait budget (200 ms)
 *   SOBER_CONTROLLER_DEFAULT_STRICT_VPTR=1      require the 0x6e02568 vptr on
 *                                               app+0x440 itself, not only on
 *                                               the intrusive word at +0x448
 */
#define SOBER_CONTROLLER_DEFAULT_VERBOSE_ENV "SOBER_CONTROLLER_DEFAULT_VERBOSE"
#define SOBER_CONTROLLER_DEFAULT_PROBE_ENV   "SOBER_CONTROLLER_DEFAULT_PROBE"
#define SOBER_CONTROLLER_DEFAULT_WAIT_ENV    "SOBER_CONTROLLER_DEFAULT_WAIT_MS"
#define SOBER_CONTROLLER_DEFAULT_STRICT_ENV  "SOBER_CONTROLLER_DEFAULT_STRICT_VPTR"

/* Result codes. */
#define SOBER_CD_OK          0
#define SOBER_CD_ERR       (-1)  /* bad argument / guest helper unavailable */
#define SOBER_CD_DISABLED  (-2)  /* SOBER_CONTROLLER_DEFAULT=0: inert         */
#define SOBER_CD_NO_FLAG   (-3)  /* FFlag 0x72cb648 still 0                  */
#define SOBER_CD_NO_SINGLETON (-4) /* 0x72cb5e0 / 0x30ef840 still unusable   */
#define SOBER_CD_NO_BRIDGE (-5)  /* no genuine 0x6dbc7c8 app-bridge object    */
#define SOBER_CD_NO_APP    (-6)  /* wrapper+0x20 (app) is NULL                */
#define SOBER_CD_NO_DISPATCH (-7) /* dispatch helper unavailable              */
#define SOBER_CD_VERIFY    (-8)  /* app+0x440 missing / wrong vptr            */

/* Flags for sober_controller_default_install_flags (bitmask). */
#define SOBER_CD_F_SKIP_FLAG       0x1u /* do not invoke the guest flag setter  */
#define SOBER_CD_F_SKIP_SINGLETON  0x2u /* do not invoke sibling singleton mod  */
#define SOBER_CD_F_SKIP_PROBE      0x4u /* skip the 0x30ef840 probe              */
#define SOBER_CD_F_FORCE           0x8u /* re-dispatch even if app+0x440 is set  */
#define SOBER_CD_F_STRICT_VPTR    0x10u /* require *(app+0x440)==base+0x6e02568  */

/* Frozen-engine RVAs (BuildID 204ad049 only). */
enum {
    SOBER_CD_RVA_FLAG_BYTE        = 0x72cb648u, /* FFlag byte                   */
    SOBER_CD_RVA_FLAG_REGISTRY    = 0x7cdd118u, /* flag registry singleton slot  */
    SOBER_CD_RVA_FLAG_NAME        = 0x43650du,  /* "EnableAppBridgeComponents"   */
    SOBER_CD_RVA_FLAG_TRUE        = 0x3a6b57u,  /* "True" boolean spelling       */
    SOBER_CD_RVA_FLAG_SETTER      = 0x23648beu, /* guest setter (0x237df9c loop) */
    SOBER_CD_RVA_APP_SINGLETON    = 0x72cb5e0u, /* app/service singleton         */
    SOBER_CD_RVA_ACCESSOR         = 0x30ef840u, /* returns service or NULL       */
    SOBER_CD_RVA_STATIC_WRAPPER   = 0x74d75a0u, /* 0x24ad6fc return; +0x20 = app  */
    SOBER_CD_RVA_STATIC_GETTER    = 0x24ad6fcu, /* genuine static wrapper getter  */
    SOBER_CD_RVA_BRIDGE_VT        = 0x6dbc7c8u, /* app-bridge vtable             */
    SOBER_CD_RVA_BRIDGE_SLOT      = 0x6dbc7f8u, /* slot +0x30 -> installer        */
    SOBER_CD_RVA_INSTALLER        = 0x24ad50au, /* controller installer          */
    SOBER_CD_RVA_WRAPPER_STORE    = 0x24ae1feu, /* wrapper vtable +0x10 (never
                                                  called directly by us)      */
    SOBER_CD_RVA_GENUINE_STORE    = 0x24aea9cu, /* mov %r14,0x440(%rbx)          */
    SOBER_CD_RVA_CTRL_VPTR        = 0x6e02568u, /* intrusive wrapper vptr        */
    SOBER_CD_RVA_INIT_WITH_PARAMS = 0x24abf96u, /* builds object + its +0xb0      */
    SOBER_CD_RVA_REGISTER_BRIDGE  = 0x2eb6b2au, /* genuine lazy-bridge register   */
    SOBER_CD_RVA_DISPATCH_SITE    = 0x23389f7u, /* call *0x30(%rax)               */
    SOBER_CD_RVA_DISPATCH_HELPER  = 0x233896cu, /* helper containing that call    */
    SOBER_CD_RVA_TASK_MAP         = 0x72cef28u, /* worker deque chunk map         */
    SOBER_CD_RVA_TASK_BASE        = 0x72cef40u, /* deque first-element index      */
    SOBER_CD_RVA_TASK_COUNT       = 0x72cef48u, /* deque element count            */
    SOBER_CD_RVA_WORKER_CV        = 0x72cef78u, /* worker condition variable       */
    SOBER_CD_RVA_WORKER_WAKE      = 0x2e5a5c0u, /* pthread_cond_signal wrapper    */
    SOBER_CD_RVA_INIT_ARRAY_GUARD = 0x72a0a30u  /* FFlag ctor once-guard (info)   */
};

/* Object geometry (bytes), recovered from the disassembly. */
#define SOBER_CD_OFF_BRIDGE_SHARED   0xb0u  /* 0x24ac5a9/0x24ac5c8             */
#define SOBER_CD_OFF_APP             0x20u  /* wrapper+0x20 -> app              */
#define SOBER_CD_OFF_CONTROLLER      0x440u /* app+0x440 genuine controller     */
#define SOBER_CD_OFF_CONTROL         0x448u /* intrusive wrapper (vptr 6e02568) */

/* ---------------------------------------------------------------------------
 * EXACT WIRING SIGNATURE FOR THE LEAD
 * ---------------------------------------------------------------------------
 * Call ONCE, after the FFlag registry ctor and the app singleton are in place
 * (fflag_default / singleton_default) and after nativeGameGlobalInit
 * (0x233762c) + InitWithParams (0x24abf96) have been replayed.  The module
 * self-gates on SOBER_CONTROLLER_DEFAULT, so the kill switch turns it into an
 * inert no-op (SOBER_CD_DISABLED); no other knob can disable it.
 *
 *     #include "controller_default.h"
 *     int cd_rc = sober_controller_default_install((void *)base);
 *
 * Returns SOBER_CD_OK iff app+0x440 is non-NULL and its genuine intrusive
 * control word (app+0x448) carries vptr base+0x6e02568 after the real
 * 0x23389f7 dispatch.  SOBER_CD_F_STRICT_VPTR additionally requires the
 * controller slot itself to carry that vptr.
 *
 * Richer entry point (use when the app-bridge object is not discoverable in
 * the worker deque): pass the JNI env/jclass and the same InitParams jobject
 * used for the harness InitWithParams replay.  The module then re-enters the
 * genuine JNI entry 0x24abf96 to build a fresh 0xe0 object with its +0xb0
 * shared_ptr and registers it through 0x2eb6b2a before waking the worker.
 *
 *     int cd_rc = sober_controller_default_install_with_jni(
 *         (void *)base, env, app_class, param_obj, 0);
 *
 * Both entry points are idempotent: an already-populated slot short-circuits
 * the trigger unless SOBER_CD_F_FORCE is set; verification still checks the
 * genuine 0x6e02568 intrusive vptr, so a fake SOBER_SEED_USER_CONTROLLER
 * object fails with SOBER_CD_VERIFY and must be disabled by the lead.
 * `sober_controller_default_reset` clears only module counters, never guest
 * state.  OR sober_controller_default_should_withhold() into the lead's
 * decision-013 seeded_state chain (this module is default-ON).
 */

/* Master enable (default ON; SOBER_CONTROLLER_DEFAULT=0|off|no|false disables). */
int         sober_controller_default_enabled(void);
const char *sober_controller_default_env_vars(void);

/* One-shot: ensure prerequisites, trigger the real dispatch, verify. */
int sober_controller_default_install(void *guest_base);
int sober_controller_default_install_flags(void *guest_base, unsigned flags);

/* Richer one-shot: optionally re-run genuine InitWithParams when no genuine
 * app-bridge object is found.  env/jclass/init_params may be NULL. */
int sober_controller_default_install_with_jni(void *guest_base,
                                              void *jni_env, void *jclass,
                                              void *init_params,
                                              unsigned flags);

/* Individual stages (all self-gating except where noted): */
int  sober_controller_default_ensure_flag(void *guest_base);
int  sober_controller_default_ensure_singleton(void *guest_base);
void *sober_controller_default_find_bridge(void *guest_base); /* read-only */
int  sober_controller_default_trigger(void *guest_base, void *bridge_object);
int  sober_controller_default_verify(void *guest_base, char *buf, size_t cap);

/* Read-only accessors (safe with a NULL base). */
void       *sober_controller_default_app(void *guest_base);
void       *sober_controller_default_controller(void *guest_base);
void       *sober_controller_default_control(void *guest_base);
uintptr_t   sober_controller_default_controller_vptr(void *guest_base);
const char *sober_controller_default_describe(void *guest_base,
                                              char *buf, size_t cap);

/* Diagnostics / test hooks (do not affect the guest). */
void          sober_controller_default_reset(void);
uint64_t      sober_controller_default_dispatches(void);
uint64_t      sober_controller_default_verifications(void);
int           sober_controller_default_last_rc(void);
int           sober_controller_default_should_withhold(void);

#ifdef __cplusplus
}
#endif
#endif /* SOBER_CONTROLLER_DEFAULT_H */
