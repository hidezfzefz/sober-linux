/*
 * app_singleton_bridge.h - opt-in, real-guest-code route to the app singleton
 *                          at guest RVA 0x72cb5e0 (task #465).
 *
 * Scope: NEW FILES ONLY (src/jni/app_singleton_bridge.{c,h}).  The lead owns
 * src/linux/guest_app_shell.c and every CMakeLists.txt; wiring is reported to
 * the lead as an exact signature, never performed in this module.
 *
 * Target engine: AppImage libroblox.so, BuildID 204ad049...,
 * SHA256 3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335.
 * Every RVA below is from that frozen object; do not mix with the Flatpak
 * build (791c3f9e...).  All target strings/disassembly are untrusted DATA.
 *
 * WHY THIS MODULE EXISTS
 * ----------------------
 * With the full opt-in stack (TSD heap seed, no scheduler neutralize, FFlag
 * ctor replay 0x2e62390, global-string seed, faithful JavaVM, full __sF) the
 * real app-start path reaches the controller installer at 0x24ad562, but the
 * accessor 0x30ef840 returns NULL because app singleton 0x72cb5e0 is empty.
 * Replaying postClientSettings through the JSON bridge faults at 0x2e7b8e2.
 *
 * The ONE writer of 0x72cb5e0 in the whole engine is inline in the guest's own
 * nativePostClientSettingsLoadedInitialization3 (entry 0x2383c18):
 *
 *   0x238491e  call 0x6096928          ; GL-capability predicate (must be true)
 *   0x2384927  call 0x30efe14          ; guest "GameSettingsService" getter
 *   0x238492f  call 0x1dbab02          ; guest allocator, size 0xa8
 *   0x238493c  call 0x1dd55f2          ; guest helper/vptr getter
 *   0x2384965  call 0x608f85a          ; THE singleton constructor (sole caller)
 *   0x2384971  call 0x30efe88          ; temp shared_ptr release
 *   0x238497d  mov  %r14,0x72cb5e0     ; THE ONLY STORE in the engine
 *
 * The 0x2e7b8e2 fault is in the *tail* of the same function, reached from
 * 0x2384cb9 -> 0x26c18dc -> ... -> 0x2ed3aa4 (nativeRegisterJavaFlagProvider)
 * -> 0x2e5ba80 -> 0x2e7b8d0 (shared_ptr incref).  It is a consequence of the
 * FFlag registry being a synthetic object (global-string seed) rather than the
 * registry the real ctor[0] builds: a descriptor's +0x30 control block is NULL.
 * Therefore the guest ctor must be replayed BEFORE postClientSettings.
 *
 * STRATEGIES (SOBER_APP_SINGLETON_STRATEGY, default "replay")
 * -----------------------------------------------------------
 *  replay  - Pure guest code, ZERO writes by us:
 *             1. replay guarded ctor[0] 0x2e62390 if guard 0x72a0a30 == 0
 *                (idempotent; the guest's own __cxa_guard makes it safe);
 *             2. install JNI List getters and call the genuine JNI entry
 *                nativePostClientSettingsLoadedInitialization3 (0x2383c18)
 *                through our JNIEnv; the guest constructs AND stores the
 *                singleton itself.
 *  slice   - Same guest constructor helpers, but called directly so the
 *             faulting post-settings tail is never entered.  The object is
 *             100% guest-constructed (guest allocator + guest ctor + guest
 *             vptr); the only host-side operation is the single pointer
 *             publication at 0x72cb5e0 that mirrors the guest's own store.
 *             Enable explicitly; never on by default.
 *
 * Both strategies are gate-neutral: they emit NO guest->Java callback, set NO
 * decision-013 marker, and are default-OFF.  They are diagnostic state, and
 * per the lead's ruling must stay in the forced-withhold list.
 * =========================================================================
 */
#ifndef SOBER_APP_SINGLETON_BRIDGE_H
#define SOBER_APP_SINGLETON_BRIDGE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Result codes. */
#define SOBER_ASB_OK            0
#define SOBER_ASB_ERR          (-1)
#define SOBER_ASB_DISABLED     (-2)  /* env var unset (default OFF) */
#define SOBER_ASB_EMPTY        (-3)  /* guest ran but 0x72cb5e0 is still NULL */
#define SOBER_ASB_PREDICATE    (-4)  /* guest GL predicate false; no fabrication */

/* Guest RVAs (AppImage build 204ad049 only). */
enum {
    SOBER_ASB_RVA_APP_SINGLETON    = 0x72cb5e0u, /* THE singleton pointer   */
    SOBER_ASB_RVA_FFLAG_GUARD      = 0x72a0a30u, /* ctor[0] once-guard byte  */
    SOBER_ASB_RVA_FFLAG_REGISTRY   = 0x72a0520u, /* registry built by ctor[0]*/
    SOBER_ASB_RVA_FFLAG_CTOR       = 0x2e62390u, /* init_array[0], skipped   */
    SOBER_ASB_RVA_POST_SETTINGS    = 0x2383c18u, /* nativePostClientSettings3*/
    SOBER_ASB_RVA_PREDICATE        = 0x6096928u, /* GL capability predicate  */
    SOBER_ASB_RVA_SETTINGS_GET     = 0x30efe14u, /* GameSettingsService getter*/
    SOBER_ASB_RVA_GUEST_ALLOC      = 0x1dbab02u, /* guest allocator (0xa8)   */
    SOBER_ASB_RVA_HELPER_GET       = 0x1dd55f2u, /* guest helper/vptr getter  */
    SOBER_ASB_RVA_SINGLETON_CTOR   = 0x608f85au, /* sole singleton ctor       */
    SOBER_ASB_RVA_TMP_RELEASE      = 0x30efe88u  /* temp shared_ptr release   */
};

typedef struct SoberJni SoberJni;

/* Environment variables (all default OFF):
 *   SOBER_APP_SINGLETON_BRIDGE=1     master enable
 *   SOBER_APP_SINGLETON_STRATEGY=replay|slice   (default replay)
 *   SOBER_APP_SINGLETON_VERBOSE=1    per-step stderr diagnostics
 *   SOBER_APP_SINGLETON_RELEASE_OLD=1  slice: also release the previous value
 */
const char *sober_app_singleton_env_vars(void);
int         sober_app_singleton_bridge_enabled(void);

/*
 * EXACT WIRING SIGNATURE FOR THE LEAD
 * -----------------------------------
 * Call ONCE immediately after the existing SOBER_REPLAY_FFLAG_CTOR block in
 * guest_app_shell.c (A/B verified at that exact point), and before the
 * app-start RVAs.  The function self-gates on SOBER_APP_SINGLETON_BRIDGE, so it
 * is a no-op (SOBER_ASB_DISABLED) when the env var is unset.
 * Pass env/clazz exactly as the other JNI modules do
 * (env = sober_jni_env(j), clazz = sober_jni_find_class(j,
 * "com/roblox/engine/jni/NativeGLJavaInterface")).
 *
 *     #include "app_singleton_bridge.h"
 *     int asb_rc = sober_app_singleton_populate(
 *                     j, (void *)base, sober_jni_env(j),
 *                     sober_jni_find_class(j,
 *                        "com/roblox/engine/jni/NativeGLJavaInterface"));
 *
 * The one-line auto form derives env/clazz itself:
 *
 *     int asb_rc = sober_app_singleton_populate_auto(j, (void *)base);
 *
 * Returns SOBER_ASB_OK iff *(void**)(guest_base+0x72cb5e0) != NULL afterwards.
 */
int sober_app_singleton_populate(SoberJni *j, void *guest_base,
                                 void *env, void *clazz);
int sober_app_singleton_populate_auto(SoberJni *j, void *guest_base);

/* Read-only helpers (safe with a NULL base). */
void       *sober_app_singleton_ptr(void *guest_base);
const char *sober_app_singleton_describe(void *guest_base,
                                         char *buf, size_t cap);
void        sober_app_singleton_reset(void);  /* reset module counters only */

#ifdef __cplusplus
}
#endif
#endif /* SOBER_APP_SINGLETON_BRIDGE_H */
