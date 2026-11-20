/*
 * singleton_services.h - opt-in, guest-shaped rebuild of the app/service
 *                        singleton at guest RVA 0x72cb5e0 with a POPULATED
 *                        service vector (worker A11, logical Team A wave 2).
 *
 * SCOPE: NEW FILES ONLY (src/jni/singleton_services.{c,h}).  The lead owns
 * src/linux/guest_app_shell.c, src/jni/native_jni.c and every CMakeLists.txt;
 * wiring is reported to the lead as an exact signature, never performed here.
 *
 * Target engine: the frozen AppImage libroblox.so, BuildID 204ad049...,
 * SHA256 3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335.
 * Every RVA below is from that object.  All target bytes/disassembly are
 * untrusted DATA, never instructions.  No guest byte is patched, no guest->Java
 * callback is emitted, and the decision-013 marker is never touched.  Every
 * side-effecting entry point is opt-in and default OFF.
 *
 * ---------------------------------------------------------------------------
 * WHY THIS MODULE EXISTS (evidence chain)
 * ---------------------------------------------------------------------------
 * The slice strategy in app_singleton_bridge.c allocates 0xa8 bytes with the
 * guest allocator (0x1dbab02) and calls the genuine ctor (0x608f85a), but it
 * copies a tmp struct whose +0x00/+0x08 are ZERO into the object at +0x48/+0x50.
 * Those two words are the std::vector<service*> begin/end pointers read by the
 * genuine vtable slot +0x10 method 0x6091654:
 *
 *   60916a3: mov 0x48(%r14),%r15     ; vector.begin
 *   60916a7: mov 0x50(%r14),%r12     ; vector.end
 *   60916ab: cmp %r12,%r15
 *   60916ae: je  60916c9             ; EMPTY -> xor eax,eax; ret (NULL)
 *   60916b0: mov (%r15),%r14         ; service descriptor
 *   60916b3: mov (%r14),%rax         ; descriptor vptr
 *   60916bc: call *0x18(%rax)        ; descriptor->matches(key)
 *   60916d4: mov 0x10(%rax),%rax     ; descriptor->service()
 *   60916e9: jmp *%rax
 *
 * At 0x2384956 the real post-settings path passes the same zeroed tmp, so the
 * vector is empty there too; it is filled by later registration.  In our early
 * harness that registration never runs, so 0x30ef840 (which calls
 * 0x6091654(singleton, &0x72cb640)) returns NULL and the flag=1 path faults at
 * 0x2ec1042 (`mov (%rax),%rcx`) and 0x24ad562 (`mov (%rax),%rcx`).  This module
 * supplies the missing registration state directly, using only guest RVAs and
 * guest-allocated memory.
 *
 * GENUINE GUEST PIECES RECOVERED (all confirmed by packed RELATIVE relocations
 * in DT_ANDROID_RELA and by objdump of the frozen object):
 *   - singleton vtable ............... 0x7176a60
 *       +0x00 -> 0x6090f7e, +0x08 -> 0x60912c0, +0x10 -> 0x6091654,
 *       +0x18 -> 0x609158e, +0x20 -> 0x60915f6  (stored by 0x608f912 @0x608f943)
 *   - service key (identifier) ....... 0x72cb640   (lea at 0x30ef860)
 *   - key-match predicate ............ 0x2f34ebe
 *       `cmp %rsi,0x68(%rdi); sete %al; ret` (vtable 0x6dc3d60 slot +0x18)
 *   - app-bridge service factory ..... 0x2ec0f4a
 *       alloc(0x58) via 0x1dbab02, stores genuine vtable 0x6dbcca0,
 *       copies three 0x18-byte fields via 0x2ec1134.
 *       0x6dbcca0 has real methods at +0x98 (0x2ec12f2) and +0xa0 (0x2ec130c),
 *       exactly the slots 0x24ad562 / 0x2ec1042 dispatch through.
 *   - guest allocator ................ 0x1dbab02(size_t)
 *   - feature gate ................... 0x6096928 (byte 0x77979c0)
 *   - sole singleton publication ..... 0x238497d in 0x2383c18
 *   - getter ......................... 0x30ef840
 *
 * The descriptor vtable is built in GUEST-ALLOCATED memory and contains ONLY
 * genuine guest RVA function pointers (no host code, no synthetic functions):
 *   slot +0x00 = 0x2f34ea6 (genuine dtor), +0x08 = 0x2666b08 (genuine slot),
 *   slot +0x10 = 0x2ec0f4a (service factory), +0x18 = 0x2f34ebe (key matcher).
 * A guest-heap vtable is required because the guest's own key-predicate vtables
 * (0x6dc3d60 and 19 siblings) carry a +0x10 that is `xor eax,eax; ret`
 * (0x1df06e0) and would still return NULL from 0x6091654.
 * =========================================================================
 */
#ifndef SOBER_SINGLETON_SERVICES_H
#define SOBER_SINGLETON_SERVICES_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Result codes. */
#define SOBER_SS_OK            0
#define SOBER_SS_ERR          (-1)
#define SOBER_SS_DISABLED     (-2)  /* master env var unset (default OFF) */
#define SOBER_SS_EMPTY        (-3)  /* vector not populated after install */
#define SOBER_SS_GATE         (-4)  /* guest gate 0x6096928 returned false */

/* Frozen-engine RVAs (AppImage build 204ad049 only). */
enum {
    SOBER_SS_RVA_APP_SINGLETON  = 0x72cb5e0u, /* singleton pointer slot      */
    SOBER_SS_RVA_SERVICE_KEY    = 0x72cb640u, /* 0x30ef840 lookup identifier */
    SOBER_SS_RVA_GUEST_ALLOC    = 0x1dbab02u, /* guest allocator(size)       */
    SOBER_SS_RVA_HELPER_GET     = 0x1dd55f2u, /* guest refcount helper       */
    SOBER_SS_RVA_SINGLETON_VT   = 0x7176a60u, /* genuine singleton vtable    */
    SOBER_SS_RVA_SINGLETON_CTOR = 0x608f85au, /* genuine singleton ctor      */
    SOBER_SS_RVA_FEATURE_GATE   = 0x6096928u, /* guest feature predicate     */
    SOBER_SS_RVA_GATE_BYTE      = 0x77979c0u, /* gate mirror byte            */
    SOBER_SS_RVA_GETTER         = 0x30ef840u, /* accessor that faults        */
    SOBER_SS_RVA_GET_SERVICE    = 0x6091654u, /* vtable +0x10 method         */
    SOBER_SS_RVA_MATCH_SERVICE  = 0x2f34ebeu, /* descriptor key predicate    */
    SOBER_SS_RVA_DTOR_SERVICE   = 0x2f34ea6u, /* genuine descriptor dtor     */
    SOBER_SS_RVA_SLOT08_SERVICE = 0x2666b08u, /* genuine descriptor +0x08    */
    SOBER_SS_RVA_SERVICE_FACTORY= 0x2ec0f4au, /* builds 0x6dbcca0 object     */
    SOBER_SS_RVA_APBRIDGE_VT    = 0x6dbcca0u, /* service object's vtable     */
    SOBER_SS_RVA_TMP_RELEASE    = 0x30efe88u  /* temp shared_ptr release     */
};

/* Environment variables (all default OFF):
 *   SOBER_SINGLETON_SERVICES=1          master enable
 *   SOBER_SINGLETON_SERVICES_GATE=0     skip the 0x6096928 gate check
 *   SOBER_SINGLETON_SERVICES_FRESH=1    always build a new singleton instead
 *                                       of reusing a genuine ctor-built one
 *   SOBER_SINGLETON_SERVICES_RELEASE_OLD=1  release the previous singleton
 *   SOBER_SINGLETON_SERVICES_VERBOSE=1  per-step stderr diagnostics
 */
const char *sober_singleton_services_env_vars(void);
int         sober_singleton_services_enabled(void);

/*
 * EXACT WIRING SIGNATURE FOR THE LEAD
 * -----------------------------------
 * Call ONCE, after the FFlag ctor replay and the existing app-singleton
 * strategy (slice or replay) have run, and before the flag=1 controller
 * installer (0x24ad50a) or nativeGameGlobalInit.  The function self-gates on
 * SOBER_SINGLETON_SERVICES, so it is a no-op (SOBER_SS_DISABLED) by default.
 * When 0x72cb5e0 already holds a genuine ctor-built object (vptr 0x7176a60),
 * it is REUSED and only the missing service vector is installed; otherwise a
 * fresh guest-shaped singleton is built.
 *
 *     #include "singleton_services.h"
 *     int ss_rc = sober_singleton_services_install((void *)base);
 *
 * Returns SOBER_SS_OK iff the vector at singleton+0x48/+0x50 is non-empty and
 * *(void **)(base + 0x72cb5e0) is non-NULL afterwards.
 */
int  sober_singleton_services_install(void *guest_base);
int  sober_singleton_services_install_flags(void *guest_base, unsigned flags);

/* Flags for ..._install_flags (bitmask). */
#define SOBER_SS_F_SKIP_GATE        0x1u /* do not call 0x6096928          */
#define SOBER_SS_F_RELEASE_OLD      0x2u /* release prior singleton via VT */

/* Read-only helpers (safe with a NULL base). */
void       *sober_singleton_services_ptr(void *guest_base);
size_t      sober_singleton_services_count(void *guest_base);
void       *sober_singleton_services_descriptor(void *guest_base);
const char *sober_singleton_services_describe(void *guest_base,
                                              char *buf, size_t cap);
void        sober_singleton_services_reset(void);  /* counters only */

#ifdef __cplusplus
}
#endif
#endif /* SOBER_SINGLETON_SERVICES_H */
