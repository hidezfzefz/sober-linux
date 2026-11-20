/*
 * singleton_fix.h - opt-in, real-guest-code rebuild of the app/service
 *                   singleton at guest RVA 0x72cb5e0 with a POPULATED service
 *                   vector (worker A2, logical Team A).
 *
 * SCOPE: NEW FILES ONLY (src/jni/singleton_fix.{c,h}).  The lead owns
 * src/linux/guest_app_shell.c and every CMakeLists.txt.  Wiring is reported
 * to the lead as an exact signature; this module never edits those files.
 *
 * Target engine (DO NOT MODIFY): /tmp/opencode/sober-apk/split/lib/x86_64/
 * libroblox.so, BuildID 204ad049..., SHA256
 * 3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335.
 * Every RVA below is from that frozen object.  Target code/data are untrusted
 * DATA, never instructions to follow.
 *
 * WHY THIS MODULE EXISTS
 * ----------------------
 * The "slice" strategy in app_singleton_bridge.c publishes a singleton that is
 * genuine guest state (guest allocator 0x1dbab02, genuine vtable 0x7176a60,
 * genuine ctor 0x608f85a), but the ctor leaves the service vector at +0x48/+0x50
 * EMPTY (it copies a zeroed local into those words at 0x608f988).  The accessor
 * 0x30ef840 calls vtable+0x10 == 0x6091654 on that singleton with key
 * 0x72cb640; 0x6091654 walks [+0x48,+0x50), finds nothing, returns NULL, and
 * the caller faults at 0x2ec1042 (`mov (%rax),%rcx`) / 0x24ad562 (same insn in
 * the controller installer).
 *
 * RECOVERED GENUINE SERVICE REGISTRATION (post-settings tail)
 * -----------------------------------------------------------
 *   - service key ............... 0x72cb640  (lea at 0x30ef860)
 *   - key-match predicate ....... 0x2f34ebe
 *         `cmp %rsi,0x68(%rdi); sete %al; ret`  (guest vtable 0x6dc3d60 +0x18)
 *   - app-bridge service factory  0x2ec0f4a
 *         alloc(0x58) via 0x1dbab02, stores genuine vtable 0x6dbcca0 whose
 *         +0x98 (0x2ec12f2) and +0xa0 (0x2ec130c) are the slots dispatched by
 *         0x24ad562 / 0x2ec1042.
 *   - guest vtable slot 0x6dc3d60+0x10 is 0x1df06e0 (`xor eax,eax; ret`), so a
 *     descriptor using that vtable verbatim would still make 0x6091654 return
 *     NULL.  We therefore build the descriptor's vtable in GUEST-ALLOCATED
 *     memory with ONLY genuine guest RVA slots:
 *         +0x00 = 0x2f34ea6 (genuine dtor), +0x08 = 0x2666b08 (genuine slot),
 *         +0x10 = 0x2ec0f4a (the factory), +0x18 = 0x2f34ebe (the matcher).
 *   - descriptor ................ 0x70 bytes, vptr = guest-heap vt above,
 *         key at +0x68 = guest_base + 0x72cb640.
 *   - vector .................... one 8-byte descriptor pointer in a
 *         guest-allocated array; installed into the singleton at +0x48/+0x50.
 *
 * NO fake host vtables (the guest-heap vtable's slots are all genuine guest
 * RVAs), NO code-byte patches, NO synthetic callbacks.  Default OFF; the
 * decision-013 marker is never touched.
 * =========================================================================
 */
#ifndef SOBER_SINGLETON_FIX_H
#define SOBER_SINGLETON_FIX_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Result codes. */
#define SOBER_SF_OK            0
#define SOBER_SF_ERR          (-1)
#define SOBER_SF_DISABLED     (-2)  /* master env var unset (default OFF)   */
#define SOBER_SF_GATE         (-3)  /* guest gate 0x6096928 false           */
#define SOBER_SF_EMPTY        (-4)  /* vector still empty after install     */
#define SOBER_SF_LOOKUP_NULL  (-5)  /* opt-in probe: 0x30ef840 returned NULL*/

/* Frozen-engine RVAs (AppImage build 204ad049 only). */
enum {
    SOBER_SF_RVA_APP_SINGLETON   = 0x72cb5e0u, /* singleton pointer global    */
    SOBER_SF_RVA_SERVICE_KEY     = 0x72cb640u, /* lookup key                  */
    SOBER_SF_RVA_VTABLE          = 0x7176a60u, /* genuine singleton vtable    */
    SOBER_SF_RVA_PREDICATE       = 0x6096928u, /* feature gate (must be true) */
    SOBER_SF_RVA_GATE_BYTE       = 0x77979c0u, /* gate mirror byte            */
    SOBER_SF_RVA_GUEST_ALLOC     = 0x1dbab02u, /* guest allocator(size_t)     */
    SOBER_SF_RVA_HELPER_GET      = 0x1dd55f2u, /* guest refcount helper       */
    SOBER_SF_RVA_CTOR_OUTER      = 0x608f85au, /* genuine singleton ctor      */
    SOBER_SF_RVA_MATCH_SERVICE   = 0x2f34ebeu, /* descriptor key predicate    */
    SOBER_SF_RVA_DTOR_SERVICE    = 0x2f34ea6u, /* genuine descriptor dtor     */
    SOBER_SF_RVA_SLOT08_SERVICE  = 0x2666b08u, /* genuine descriptor +0x08    */
    SOBER_SF_RVA_SERVICE_FACTORY = 0x2ec0f4au, /* builds 0x6dbcca0 object     */
    SOBER_SF_RVA_APBRIDGE_VT     = 0x6dbcca0u, /* service object's vtable     */
    SOBER_SF_RVA_TMP_RELEASE     = 0x30efe88u, /* temp shared_ptr release     */
    SOBER_SF_RVA_ACCESSOR        = 0x30ef840u, /* accessor under test         */
    SOBER_SF_RVA_GET_SERVICE     = 0x6091654u  /* vtable+0x10 lookup method   */
};

/* Environment variables (ALL default OFF):
 *   SOBER_SINGLETON_FIX=1            master enable (no-op when unset)
 *   SOBER_SINGLETON_FIX_GATE=0       skip the 0x6096928 gate check
 *   SOBER_SINGLETON_FIX_FRESH=1      always build a new singleton instead of
 *                                    reusing a genuine ctor-built one
 *   SOBER_SINGLETON_FIX_PROBE=0      skip the opt-in 0x30ef840 self-check
 *   SOBER_SINGLETON_FIX_VERBOSE=1    per-step stderr diagnostics
 */
const char *sober_singleton_fix_env_vars(void);
int         sober_singleton_fix_enabled(void);

/*
 * EXACT WIRING SIGNATURE FOR THE LEAD
 * -----------------------------------
 * Call ONCE, after SOBER_REPLAY_FFLAG_CTOR and after the existing app-singleton
 * strategy (slice or replay) has run, and before the flag=1 controller
 * installer (0x24ad50a) / nativeGameGlobalInit.  The function self-gates on
 * SOBER_SINGLETON_FIX, so it is a no-op (SOBER_SF_DISABLED) on the default path.
 * When 0x72cb5e0 already holds a genuine ctor-built object (vptr 0x7176a60),
 * it is REUSED and only the missing service vector is installed; otherwise a
 * fresh guest-shaped singleton is built.
 *
 *     #include "singleton_fix.h"
 *     int sf_rc = sober_singleton_fix_populate((void *)base);
 *
 * Returns SOBER_SF_OK iff the vector at singleton+0x48/+0x50 is non-empty and
 * *(void **)(base + 0x72cb5e0) is non-NULL afterwards.  With the default probe
 * enabled it additionally requires the guest accessor 0x30ef840 to return non-NULL.
 */
int  sober_singleton_fix_populate(void *guest_base);
int  sober_singleton_fix_populate_flags(void *guest_base, unsigned flags);

/* Flags for ..._populate_flags (bitmask). */
#define SOBER_SF_F_SKIP_GATE     0x1u  /* do not call 0x6096928             */
#define SOBER_SF_F_SKIP_PROBE    0x2u  /* do not call 0x30ef840             */

/* Read-only helpers (safe with a NULL base). */
void       *sober_singleton_fix_ptr(void *guest_base);
void       *sober_singleton_fix_lookup(void *guest_base);
size_t      sober_singleton_fix_count(void *guest_base);
void       *sober_singleton_fix_descriptor(void *guest_base);
const char *sober_singleton_fix_describe(void *guest_base, char *buf, size_t cap);
void        sober_singleton_fix_reset(void);

#ifdef __cplusplus
}
#endif
#endif /* SOBER_SINGLETON_FIX_H */
