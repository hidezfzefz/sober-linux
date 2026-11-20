/*
 * app_singleton_bridge.c - task #465.  See app_singleton_bridge.h for the full
 * rationale, the frozen-engine RVA set, and the exact wiring signature.
 *
 * HARD RULES OBSERVED HERE
 * ------------------------
 *  - default OFF (SOBER_APP_SINGLETON_BRIDGE); no side effect when unset;
 *  - no guest bytes patched; no fake vtables; no guest->Java callbacks;
 *  - the "replay" strategy performs NO writes to guest memory at all -- the
 *    guest's own nativePostClientSettingsLoadedInitialization3 stores the
 *    singleton.  The optional "slice" strategy calls only the guest's own
 *    allocator/constructor helpers and publishes the resulting guest pointer.
 *  - decision-013 marker is never touched here.
 */
#define _GNU_SOURCE
#include "app_singleton_bridge.h"
#include "native_jni.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ASB_ENV        "SOBER_APP_SINGLETON_BRIDGE"
#define ASB_STRATEGY   "SOBER_APP_SINGLETON_STRATEGY"
#define ASB_VERBOSE    "SOBER_APP_SINGLETON_VERBOSE"
#define ASB_RELEASEOLD "SOBER_APP_SINGLETON_RELEASE_OLD"

static int asb_verbose(void) { return getenv(ASB_VERBOSE) != NULL; }
#define ASBV(...) do { if (asb_verbose()) \
    fprintf(stderr, "[asb] " __VA_ARGS__); } while (0)

static unsigned long asb_calls;
static unsigned long asb_replays;
static unsigned long asb_slices;

const char *sober_app_singleton_env_vars(void)
{
    return ASB_ENV "," ASB_STRATEGY "," ASB_VERBOSE "," ASB_RELEASEOLD;
}

int sober_app_singleton_bridge_enabled(void)
{
    return getenv(ASB_ENV) != NULL;
}

void sober_app_singleton_reset(void)
{
    asb_calls = asb_replays = asb_slices = 0;
}

void *sober_app_singleton_ptr(void *guest_base)
{
    if (!guest_base) return NULL;
    return *(void **)((unsigned char *)guest_base + SOBER_ASB_RVA_APP_SINGLETON);
}

const char *sober_app_singleton_describe(void *guest_base, char *buf, size_t cap)
{
    if (!buf || cap == 0) return "";
    if (!guest_base) { buf[0] = '\0'; return buf; }
    unsigned char *b = (unsigned char *)guest_base;
    unsigned char guard = *(volatile unsigned char *)(b + SOBER_ASB_RVA_FFLAG_GUARD);
    void *registry = *(void **)(b + SOBER_ASB_RVA_FFLAG_REGISTRY);
    void *app = sober_app_singleton_ptr(guest_base);
    snprintf(buf, cap,
             "app@0x72cb5e0=%p fflag_guard@0x72a0a30=%u registry@0x72a0520=%p "
             "calls=%lu replay=%lu slice=%lu",
             app, (unsigned)guard, registry, asb_calls, asb_replays, asb_slices);
    return buf;
}

/* Replay the guest's own skipped init_array[0].  The initializer is guarded by
 * a genuine __cxa_guard at 0x72a0a30, so a second invocation is a no-op.  This
 * is real guest state construction, not a byte write and not a callback. */
static int asb_replay_fflag_ctor(void *guest_base)
{
    unsigned char *b = (unsigned char *)guest_base;
    volatile unsigned char *guard = b + SOBER_ASB_RVA_FFLAG_GUARD;
    if (*guard == 0) {
        ASBV("replaying guest ctor 0x%x (guard=%u)\n",
             SOBER_ASB_RVA_FFLAG_CTOR, (unsigned)*guard);
        ((void (*)(void))(b + SOBER_ASB_RVA_FFLAG_CTOR))();
        ASBV("after ctor: guard=%u registry=%p\n", (unsigned)*guard,
             *(void **)(b + SOBER_ASB_RVA_FFLAG_REGISTRY));
    } else {
        ASBV("ctor already done (guard=%u); no replay\n", (unsigned)*guard);
    }
    return *guard ? 0 : -1;
}

/* ---- strategy "replay": JNI List handlers + the real JNI entry point ---- */

static intptr_t asb_list_get(void *env, void *obj, const char *name,
                             const char *sig, void *args, void *user)
{
    (void)env; (void)obj; (void)sig; (void)args;
    SoberJni *j = (SoberJni *)user;
    if (!j) return 0;
    if (name && !strcmp(name, "size")) return (intptr_t)0;
    if (name && !strcmp(name, "isEmpty")) return (intptr_t)1;
    /* "get" is never reached for an empty list.  Keep a handler anyway so a
     * non-empty guest iteration still receives a real JNI object. */
    if (name && !strcmp(name, "get"))
        return (intptr_t)sober_jni_new_native_object(
            j, "com/roblox/engine/jni/model/ApplicationExitInfoC", 0x200, NULL);
    return 0;
}

static int asb_strategy_replay(SoberJni *j, void *guest_base,
                               void *env, void *clazz)
{
    unsigned char *b = (unsigned char *)guest_base;

    /* Gate 1: the real gate that guards post-settings' singleton writer. */
    int (*predicate)(void) = (int (*)(void))(b + SOBER_ASB_RVA_PREDICATE);
    if (!predicate()) {
        ASBV("guest GL predicate 0x%x false -> no fabricated singleton\n",
             SOBER_ASB_RVA_PREDICATE);
        return SOBER_ASB_PREDICATE;
    }

    /* Gate 2: genuine FFlag registry must exist, otherwise the flag-provider
     * registration in post-settings' tail dereferences a NULL control block
     * at 0x2e7b8e2. */
    if (asb_replay_fflag_ctor(guest_base) != 0) {
        ASBV("FFlag ctor replay failed\n");
        return SOBER_ASB_ERR;
    }

    /* NOTE: the real Java onCreate also drives nativeInitializeNativeFlags(String[])
     * before post-settings.  With our synthetic String[] that entry faults at
     * 0x2465242, so it is deliberately NOT called here; without it the guest's
     * TaskScheduler asserts "flags not loaded" (int3 0x6caef66, globals
     * 0x7cdd149/0x7cef650).  Reported to the lead as the remaining gap. */

    /* Serve the java/util/List getters the guest invokes through JNI.  An
     * EMPTY list is the faithful neutral value: a real device with no
     * ApplicationExitInfo history reaches the same singleton-writer branch. */
    sober_jni_set_callback_handler(j, "size", asb_list_get, j);
    sober_jni_set_callback_handler(j, "isEmpty", asb_list_get, j);
    sober_jni_set_callback_handler(j, "get", asb_list_get, j);

    void *list = NULL;
    /* Prefer the faithful guest-native C++ List shape when available. */
    extern int sober_native_objects_enabled(void);
    extern void *sober_native_java_list(struct SoberJni *, void *const *, size_t);
    if (sober_native_objects_enabled())
        list = sober_native_java_list(j, NULL, 0);
    if (!list)
        list = sober_jni_new_native_object(j, "java/util/List", 0x100, NULL);
    if (!list) return SOBER_ASB_ERR;

    ASBV("calling genuine JNI entry 0x%x(env=%p, clazz=%p, list=%p)\n",
         SOBER_ASB_RVA_POST_SETTINGS, env, clazz, list);
    /* Exact prototype: void (JNIEnv*, jclass, jobject List). */
    ((void (*)(void *, void *, void *))(b + SOBER_ASB_RVA_POST_SETTINGS))(
        env, clazz, list);

    void *app = sober_app_singleton_ptr(guest_base);
    ASBV("after post-settings: singleton=%p\n", app);
    return app ? SOBER_ASB_OK : SOBER_ASB_EMPTY;
}

/* ---- strategy "slice": the guest's own constructor helpers, directly ---- */
/* Mirrors 0x2384927..0x2384999 byte for byte at the C ABI level.  The object
 * (allocator, vptr, ctor, refcount) is created entirely by guest code. */
static int asb_strategy_slice(SoberJni *j, void *guest_base)
{
    (void)j;
    unsigned char *b = (unsigned char *)guest_base;

    int (*predicate)(void) = (int (*)(void))(b + SOBER_ASB_RVA_PREDICATE);
    if (!predicate()) {
        ASBV("guest GL predicate false -> no fabricated singleton\n");
        return SOBER_ASB_PREDICATE;
    }
    if (asb_replay_fflag_ctor(guest_base) != 0)
        return SOBER_ASB_ERR;

    void *(*settings_get)(void) =
        (void *(*)(void))(b + SOBER_ASB_RVA_SETTINGS_GET);
    void *(*alloc)(size_t) =
        (void *(*)(size_t))(b + SOBER_ASB_RVA_GUEST_ALLOC);
    void *(*helper_get)(void) =
        (void *(*)(void))(b + SOBER_ASB_RVA_HELPER_GET);
    void (*ctor)(void *, void *, void *, unsigned) =
        (void (*)(void *, void *, void *, unsigned))
        (b + SOBER_ASB_RVA_SINGLETON_CTOR);
    void (*tmp_release)(void *) =
        (void (*)(void *))(b + SOBER_ASB_RVA_TMP_RELEASE);

    void *settings = settings_get();
    void *obj = alloc(0xa8);
    void *helper = helper_get();
    if (!settings || !obj || !helper) {
        ASBV("guest helper/alloc failed: settings=%p obj=%p helper=%p\n",
             settings, obj, helper);
        return SOBER_ASB_ERR;
    }

    /* Guest temp struct at rbp-0x80: +0x00 zeroed (movapd xmm0),
     * +0x10 = 0 (movq $0), +0x18 = helper (mov %rax). */
    uint64_t tmp[4] = {0, 0, 0, (uint64_t)(uintptr_t)helper};
    ASBV("guest ctor: obj=%p settings=%p helper=%p tmp=%p\n",
         obj, settings, helper, (void *)tmp);
    ctor(obj, settings, tmp, 0u);

    /* Guest releases the temp via 0x30efe88(&local) where local[0] = tmp. */
    uint64_t tmp_ref[2] = {(uint64_t)(uintptr_t)tmp, 0};
    tmp_release(tmp_ref);

    /* Publish exactly the way the guest does at 0x238497d.  The pointer we
     * store was produced by the guest's own allocator and constructor; no
     * synthetic vtable or hand-built object is involved. */
    volatile void **slot =
        (volatile void **)(b + SOBER_ASB_RVA_APP_SINGLETON);
    void *old = (void *)*slot;
    *slot = obj;
    ASBV("published singleton: old=%p new=%p\n", old, obj);
    if (old && getenv(ASB_RELEASEOLD)) {
        void ***vpp = (void ***)old;
        void **vtable = **vpp ? **vpp : NULL;
        if (vtable && vtable[1]) {
            ASBV("releasing old singleton through guest vtable +8\n");
            ((void (*)(void *))vtable[1])(old);
        }
    }
    return sober_app_singleton_ptr(guest_base) ? SOBER_ASB_OK : SOBER_ASB_EMPTY;
}

int sober_app_singleton_populate(SoberJni *j, void *guest_base,
                                 void *env, void *clazz)
{
    if (!sober_app_singleton_bridge_enabled())
        return SOBER_ASB_DISABLED;
    if (!j || !guest_base || !env)
        return SOBER_ASB_ERR;
    ++asb_calls;

    const char *strategy = getenv(ASB_STRATEGY);
    int slice = strategy && !strcasecmp(strategy, "slice");
    int rc = slice ? asb_strategy_slice(j, guest_base)
                   : asb_strategy_replay(j, guest_base, env, clazz);
    if (slice) ++asb_slices; else ++asb_replays;

    char desc[256];
    ASBV("strategy=%s rc=%d %s\n", slice ? "slice" : "replay", rc,
         sober_app_singleton_describe(guest_base, desc, sizeof(desc)));
    return rc;
}

int sober_app_singleton_populate_auto(SoberJni *j, void *guest_base)
{
    if (!sober_app_singleton_bridge_enabled())
        return SOBER_ASB_DISABLED;
    if (!j || !guest_base) return SOBER_ASB_ERR;
    return sober_app_singleton_populate(
        j, guest_base, sober_jni_env(j),
        sober_jni_find_class(j, "com/roblox/engine/jni/NativeGLJavaInterface"));
}
