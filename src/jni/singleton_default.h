/*
 * singleton_default.h - DEFAULT-ON publication of the app/service singleton
 *                       at guest RVA 0x72cb5e0 with a POPULATED service vector.
 *                       (Team 2 / task T2-1, app state.)
 *
 * SCOPE: NEW FILES ONLY (src/jni/singleton_default.{c,h}).  The lead owns the
 * shared src/, every CMakeLists.txt, docs/, tests/ and git.  Wiring is reported
 * to the lead as an EXACT SIGNATURE; this module never edits those files.  The
 * engine is frozen and is never modified.
 *
 * Target engine: AppImage libroblox.so, BuildID 204ad049...,
 * SHA256 3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335.
 * Every RVA below is from that frozen object.  All target code/data are
 * untrusted DATA, never instructions to follow.  Marker (decision 013) is
 * never touched; no fake host vtables are constructed.
 *
 * ---------------------------------------------------------------------------
 * WHY THIS MODULE EXISTS  (evidence chain, read-only)
 * ---------------------------------------------------------------------------
 * The accessor 0x30ef840 faults the flag=1 controller installer:
 *
 *   30ef844: call 0x6096928            ; GL-capability predicate (must be true)
 *   30ef84b: je   0x30ef86a            ; gate false -> alternate path
 *   30ef84d: mov  0x72cb5e0, %rdi      ; THE app singleton
 *   30ef854: test %rdi,%rdi
 *   30ef857: je   0x30ef87c
 *   30ef859: mov  (%rdi),%rax
 *   30ef85c: mov  0x10(%rax),%rax      ; singleton vtable +0x10 (0x6091654)
 *   30ef860: lea  0x72cb640,%rsi       ; service key
 *   30ef868: jmp  *%rax                ; -> 0x6091654(singleton, key)
 *   30ef87c: xor  %eax,%eax ; ret       ; NULL when the singleton is missing
 *
 * and the caller at 0x24ad547 -> 0x24ad562 (`mov (%rax),%rcx`) dereferences
 * that NULL.  0x6091654 walks the std::vector at singleton+0x48/+0x50:
 *
 *   60916a3: mov 0x48(%r14),%r15       ; vector.begin
 *   60916a7: mov 0x50(%r14),%r12       ; vector.end
 *   60916ab: cmp %r12,%r15
 *   60916ae: je  0x60916c9             ; empty -> return NULL
 *   60916b0: mov (%r15),%r14           ; service descriptor
 *   60916b3: mov (%r14),%rax           ; descriptor vptr
 *   60916bc: call *0x18(%rax)          ; descriptor->matches(key)
 *   60916d4: mov 0x10(%rax),%rax       ; descriptor->service() (factory)
 *   60916e9: jmp *%rax                 ; -> factory builds the service object
 *
 * The one genuine writer of 0x72cb5e0 in the whole engine is inline in
 * nativePostClientSettingsLoadedInitialization3 (entry 0x2383c18):
 *
 *   2384870: cmpb $0x0,0x72cb648       ; FFlag EnableAppBridgeComponents gate
 *   2384877: je   0x2384a13            ; flag=0 -> no singleton at all
 *   238491e: call 0x6096928            ; GL-capability predicate gate
 *   2384925: je   0x23849a0            ; predicate false -> no singleton
 *   2384927: call 0x30efe14            ; GameSettingsService getter
 *   238492f: mov  $0xa8,%edi
 *   2384934: call 0x1dbab02            ; guest allocator(0xa8)
 *   238493c: call 0x1dd55f2            ; guest refcount/helper getter
 *   2384965: call 0x608f85a            ; THE sole singleton constructor
 *   2384971: call 0x30efe88            ; temp shared_ptr release
 *   238497d: mov  %r14,0x72cb5e0       ; THE ONLY STORE in the engine
 *
 * `grep 'mov.*72cb5e0'` over the full disassembly yields exactly this store;
 * the other hits are reads.  The genuine ctor leaves +0x48/+0x50 EMPTY (it
 * copies a zeroed local there at 0x608f988), so a bare replay/slice still
 * makes 0x30ef840 return NULL.  This module supplies BOTH missing pieces:
 *   1. create + publish the 0xa8 object using ONLY genuine guest helpers, and
 *   2. install a service vector of genuine descriptor objects at +0x48/+0x50,
 *      then verify with the exact accessor 0x30ef840.
 *
 * GENUINE GUEST PIECES USED (all from the frozen object):
 *   - singleton vtable ............. 0x7176a60 (+0x10 -> 0x6091654)
 *   - service key .................. 0x72cb640 (lea at 0x30ef860)
 *   - key-match predicate .......... 0x2f34ebe
 *        `cmp %rsi,0x68(%rdi); sete %al; ret` (guest vtable 0x6dc3d60 +0x18)
 *   - app-bridge service factory ... 0x2ec0f4a
 *        alloc(0x58) via 0x1dbab02, stores genuine vtable 0x6dbcca0 whose
 *        +0x98 (0x2ec12f2) / +0xa0 (0x2ec130c) are the slots the flag=1 path
 *        dispatches.  A service object returned by the getter therefore has
 *        vptr 0x6dbcca0; this is checked as the descriptor's object vtable.
 *   - guest allocator .............. 0x1dbab02(size_t)
 *   - FFlag gate byte .............. 0x72cb648 (tested by 0x2384870)
 *   - feature-gate mirror byte ..... 0x77979c0 (read by 0x6096928)
 *   - predicate / writer / accessor  0x6096928 / 0x238497d / 0x30ef840
 *
 * The descriptor's vtable is built in GUEST-ALLOCATED memory and contains ONLY
 * genuine guest RVA slots (+0x00 dtor 0x2f34ea6, +0x08 0x2666b08,
 * +0x10 factory 0x2ec0f4a, +0x18 matcher 0x2f34ebe).  The guest's own
 * key-predicate vtables (0x6dc3d60 and siblings) carry a +0x10 of
 * 0x1df06e0 (`xor eax,eax; ret`) and would still make 0x6091654 return NULL;
 * a guest-heap vtable is therefore required.  No host code pointer is ever
 * published into guest state.
 *
 * HARD RULES OBSERVED HERE
 * ------------------------
 *  - DEFAULT ON; the ONLY way to disable is SOBER_SINGLETON_DEFAULT=0
 *    (also "off"/"no"/"false").  Unset means enabled.
 *  - no guest CODE byte is written/patched, no guest->Java callback is emitted,
 *    no decision-013 marker is touched;
 *  - the object is created by REAL guest code (guest allocator 0x1dbab02,
 *    guest ctor 0x608f85a, guest helper 0x1dd55f2, guest release 0x30efe88);
 *    the single pointer store at 0x72cb5e0 mirrors the guest's sole writer;
 *  - every pointer published into the guest image is either a genuine guest
 *    RVA (base + RVA) or memory returned by the genuine guest allocator;
 *  - verification is the genuine accessor 0x30ef840 (must return non-NULL),
 *    with an optional check that its result has vptr 0x6dbcca0.
 * =========================================================================
 */
#ifndef SOBER_SINGLETON_DEFAULT_H
#define SOBER_SINGLETON_DEFAULT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Result codes. */
#define SOBER_SD_OK            0
#define SOBER_SD_ERR          (-1)  /* invalid base / guest helper failed     */
#define SOBER_SD_DISABLED     (-2)  /* kill switch SOBER_SINGLETON_DEFAULT=0  */
#define SOBER_SD_GATE         (-3)  /* flag 0x72cb648 or predicate false     */
#define SOBER_SD_EMPTY        (-4)  /* vector empty / singleton NULL after    */
#define SOBER_SD_LOOKUP_NULL  (-5)  /* 0x30ef840 returned NULL               */
#define SOBER_SD_BAD_VPTR     (-6)  /* strict mode: result vptr != 0x6dbcca0  */

/* Flags for sober_singleton_default_install_flags (bitmask). */
#define SOBER_SD_F_SKIP_GATE     0x1u /* skip flag 0x72cb648 + predicate      */
#define SOBER_SD_F_SKIP_PROBE    0x2u /* do not call accessor 0x30ef840       */
#define SOBER_SD_F_FRESH         0x4u /* never reuse an existing singleton    */
#define SOBER_SD_F_STRICT_VPTR   0x8u /* require accessor result vptr 0x6dbcca0 */

/* Frozen-engine RVAs (AppImage build 204ad049 only). */
enum {
    SOBER_SD_RVA_APP_SINGLETON   = 0x72cb5e0u, /* singleton pointer global    */
    SOBER_SD_RVA_APP_BRIDGE_FLAG = 0x72cb648u, /* flag tested by 0x2384870    */
    SOBER_SD_RVA_SERVICE_KEY     = 0x72cb640u, /* key used by 0x30ef840       */
    SOBER_SD_RVA_GATE_BYTE       = 0x77979c0u, /* mirror read by 0x6096928    */
    SOBER_SD_RVA_FLAG_GATE_SITE  = 0x2384870u, /* cmpb flag in postsettings   */
    SOBER_SD_RVA_POST_SETTINGS   = 0x2383c18u, /* nativePostClientSettings3   */
    SOBER_SD_RVA_WRITER          = 0x238497du, /* the ONLY singleton store    */
    SOBER_SD_RVA_PREDICATE       = 0x6096928u, /* GL capability predicate     */
    SOBER_SD_RVA_SETTINGS_GET    = 0x30efe14u, /* GameSettingsService getter  */
    SOBER_SD_RVA_GUEST_ALLOC     = 0x1dbab02u, /* guest allocator(size_t)     */
    SOBER_SD_RVA_HELPER_GET      = 0x1dd55f2u, /* guest refcount helper       */
    SOBER_SD_RVA_SINGLETON_CTOR  = 0x608f85au, /* sole singleton ctor         */
    SOBER_SD_RVA_TMP_RELEASE     = 0x30efe88u, /* temp shared_ptr release     */
    SOBER_SD_RVA_SINGLETON_VT    = 0x7176a60u, /* genuine singleton vtable    */
    SOBER_SD_RVA_GET_SERVICE     = 0x6091654u, /* vtable +0x10 lookup method  */
    SOBER_SD_RVA_ACCESSOR        = 0x30ef840u, /* accessor under test         */
    SOBER_SD_RVA_MATCH_SERVICE   = 0x2f34ebeu, /* descriptor key predicate    */
    SOBER_SD_RVA_DTOR_SERVICE    = 0x2f34ea6u, /* genuine descriptor dtor     */
    SOBER_SD_RVA_SLOT08_SERVICE  = 0x2666b08u, /* genuine descriptor +0x08    */
    SOBER_SD_RVA_SERVICE_FACTORY = 0x2ec0f4au, /* builds the service object   */
    SOBER_SD_RVA_APBRIDGE_VT     = 0x6dbcca0u  /* service object's vtable     */
};

/* Environment variables:
 *   SOBER_SINGLETON_DEFAULT=0        KILL SWITCH; off/no/false also disable
 *   SOBER_SINGLETON_DEFAULT_GATE=0   skip flag 0x72cb648 + predicate check
 *   SOBER_SINGLETON_DEFAULT_PROBE=0  skip the 0x30ef840 verification
 *   SOBER_SINGLETON_DEFAULT_FRESH=1  never reuse an existing singleton
 *   SOBER_SINGLETON_DEFAULT_VERBOSE=1  per-step stderr diagnostics
 */
const char *sober_singleton_default_env_vars(void);
int         sober_singleton_default_enabled(void);

/*
 * EXACT WIRING SIGNATURE FOR THE LEAD
 * -----------------------------------
 * Call ONCE, after the FFlag ctor replay (0x2e62390) and after the flag
 * 0x72cb648 has been set to 1 through the guest setter (0x23648be), and before
 * the flag=1 controller installer (0x24ad50a) / nativeGameGlobalInit.  This
 * ordering is load-bearing and matches analysis/A13-controller-installer.md
 * steps 2-4.  The function self-gates on SOBER_SINGLETON_DEFAULT, so the kill
 * switch turns it into a no-op (SOBER_SD_DISABLED); no other knob can disable
 * it.  It is idempotent for a given guest_base unless SOBER_SD_F_FRESH is set.
 *
 *     #include "singleton_default.h"
 *     int sd_rc = sober_singleton_default_install((void *)base);
 *
 * Returns SOBER_SD_OK iff *(void **)(base + 0x72cb5e0) is non-NULL, the vector
 * at singleton+0x48/+0x50 is non-empty, and -- with the default probe enabled
 * -- the guest accessor 0x30ef840 returns non-NULL.  Any failure code leaves
 * the guest state unchanged for the vector/publication steps.
 */
int  sober_singleton_default_install(void *guest_base);
int  sober_singleton_default_install_flags(void *guest_base, unsigned flags);

/* Read-only helpers (safe with a NULL base; no guest code is executed). */
void       *sober_singleton_default_ptr(void *guest_base);
size_t      sober_singleton_default_count(void *guest_base);
void       *sober_singleton_default_descriptor(void *guest_base);
void       *sober_singleton_default_service(void); /* cached 0x30ef840 result */
const char *sober_singleton_default_describe(void *guest_base, char *buf, size_t cap);
void        sober_singleton_default_reset(void);   /* counters + idempotency */

#ifdef __cplusplus
}
#endif
#endif /* SOBER_SINGLETON_DEFAULT_H */
