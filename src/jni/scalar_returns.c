/*
 * scalar_returns.c - opt-in, faithful JNIEnv scalar-call slots (analysis B5).
 *
 * Owner: subagent B13, logical Team B (wave 2).  Scope: this file +
 * scalar_returns.h ONLY (NEW files).  native_jni.c / CMakeLists.txt and the
 * engine /tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so (BuildID
 * 204ad049..., SHA 3e498d6494...) are READ ONLY / untouched.
 *
 * Evidence (all static, SHA-bound; see analysis/B05-jnienv-gaps.md):
 *   - G7  CallStaticBooleanMethodV/A  0x3b0/0x3b8 -> fn[114]/fn[115] (stub)
 *   - G8  CallStaticIntMethodV/A      0x410/0x418 -> fn[126]/fn[127] (stub)
 *   - G9  CallStaticLongMethodV/A     0x428/0x430 -> fn[129]/fn[130] (stub)
 *   - G10 CallStaticDoubleMethodV/A   0x458/0x460 -> fn[135]/fn[136] (stub)
 *   - G11 CallStaticIntMethod         0x408        -> fn[125] (result discarded)
 *   fn_index = envfn_offset/8 - 4 (four reserved pointers precede fn[231]).
 *   Cross-checked with offsetof() against the JDK 21 JNINativeInterface_.
 *
 * Fidelity: instance-form V/A wrappers (native_jni.c:559-583) already call
 * sr_dispatch_call() correctly, and the dispatcher does not distinguish a
 * jclass from a jobject (native_jni.c:826-827).  Each function here is a thin,
 * correctly typed JNI-ABI forwarder to the corresponding instance form found
 * through `env` (standard JNI: *(void**)env is the function table).  The guest
 * therefore reaches the SAME dispatcher/handlers as the already-correct plain
 * static calls.  No handler state, callback or marker logic is duplicated.
 *
 * Decision 013: DEFAULT OFF (SOBER_SCALAR_RETURNS=1).  Installing only writes
 * function pointers; it patches no guest byte, emits no callback and touches
 * no marker.  No callbacks are emitted by this module itself.
 */
#define _GNU_SOURCE
#include "scalar_returns.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- canonical JNINativeInterface_ indices of the instance-form delegates *
 * These are the already-installed, correct V/A wrappers in native_jni.c:
 *   bool  V/A = fn[34]/fn[35]   int V/A = fn[46]/fn[47]
 *   long  V/A = fn[49]/fn[50]   double V/A = fn[55]/fn[56]
 * canonical index = fn_index + 4 (offsetof-verified against JDK 21 jni.h). */
#define SR_CANON_BOOL_V    38u   /* CallBooleanMethodV   +0x130 / fn[34] */
#define SR_CANON_BOOL_A    39u   /* CallBooleanMethodA   +0x138 / fn[35] */
#define SR_CANON_INT_V     50u   /* CallIntMethodV       +0x190 / fn[46] */
#define SR_CANON_INT_A     51u   /* CallIntMethodA       +0x198 / fn[47] */
#define SR_CANON_LONG_V    53u   /* CallLongMethodV      +0x1a8 / fn[49] */
#define SR_CANON_LONG_A    54u   /* CallLongMethodA      +0x1b0 / fn[50] */
#define SR_CANON_DOUBLE_V  59u   /* CallDoubleMethodV    +0x1d8 / fn[55] */
#define SR_CANON_DOUBLE_A  60u   /* CallDoubleMethodA    +0x1e0 / fn[56] */

/* ---- delegate function-pointer shapes (JNI-ABI identical) --------------- */
typedef uint8_t (*sr_bool_v_fn)(void *, void *, void *, va_list);
typedef uint8_t (*sr_bool_a_fn)(void *, void *, void *, void *);
typedef int32_t (*sr_int_v_fn)(void *, void *, void *, va_list);
typedef int32_t (*sr_int_a_fn)(void *, void *, void *, void *);
typedef int64_t (*sr_long_v_fn)(void *, void *, void *, va_list);
typedef int64_t (*sr_long_a_fn)(void *, void *, void *, void *);
typedef double  (*sr_double_v_fn)(void *, void *, void *, va_list);
typedef double  (*sr_double_a_fn)(void *, void *, void *, void *);

/* ---- opt-in / diagnostics ---------------------------------------------- */
int sober_scalar_returns_enabled(void)
{
    const char *e = getenv(SOBER_SCALAR_RETURNS_ENV);
    return (e && strcmp(e, "1") == 0);
}

const char *sober_scalar_returns_env_vars(void)
{
    return SOBER_SCALAR_RETURNS_ENV "=1 (enable; default OFF, diagnostic only; "
           "no guest bytes, no callbacks, no marker change)";
}

enum {
    SR_I_BOOL_V = 0, SR_I_BOOL_A,
    SR_I_INT, SR_I_INT_V, SR_I_INT_A,
    SR_I_LONG_V, SR_I_LONG_A,
    SR_I_DOUBLE_V, SR_I_DOUBLE_A,
    SR_I_COUNT
};
static const char *const sr_counter_keys[SR_I_COUNT] = {
    "boolV", "boolA", "int", "intV", "intA",
    "longV", "longA", "doubleV", "doubleA"
};
static uint64_t g_calls[SR_I_COUNT];

#define SR_BUMP(i) __atomic_fetch_add(&g_calls[(i)], 1, __ATOMIC_RELAXED)

uint64_t sober_scalar_returns_slot_calls(const char *key)
{
    if (!key) {
        uint64_t total = 0;
        for (unsigned i = 0; i < SR_I_COUNT; ++i)
            total += __atomic_load_n(&g_calls[i], __ATOMIC_RELAXED);
        return total;
    }
    for (unsigned i = 0; i < SR_I_COUNT; ++i)
        if (!strcmp(key, sr_counter_keys[i]))
            return __atomic_load_n(&g_calls[i], __ATOMIC_RELAXED);
    return 0;
}

const char *sober_scalar_returns_describe(char *buf, size_t cap)
{
    if (!buf || !cap) return "";
    snprintf(buf, cap,
             "enabled=%d env=%s=1 slots=(boolV=%u,boolA=%u,int=%u,intV=%u,"
             "intA=%u,longV=%u,longA=%u,doubleV=%u,doubleA=%u) "
             "calls=(boolV=%llu,boolA=%llu,int=%llu,intV=%llu,intA=%llu,"
             "longV=%llu,longA=%llu,doubleV=%llu,doubleA=%llu) "
             "(instance-form forwarders; genuine dispatcher; no callbacks "
             "emitted here)",
             sober_scalar_returns_enabled(), SOBER_SCALAR_RETURNS_ENV,
             SOBER_SCALAR_SLOT_BOOL_V, SOBER_SCALAR_SLOT_BOOL_A,
             SOBER_SCALAR_SLOT_INT, SOBER_SCALAR_SLOT_INT_V,
             SOBER_SCALAR_SLOT_INT_A, SOBER_SCALAR_SLOT_LONG_V,
             SOBER_SCALAR_SLOT_LONG_A, SOBER_SCALAR_SLOT_DOUBLE_V,
             SOBER_SCALAR_SLOT_DOUBLE_A,
             (unsigned long long)sober_scalar_returns_slot_calls("boolV"),
             (unsigned long long)sober_scalar_returns_slot_calls("boolA"),
             (unsigned long long)sober_scalar_returns_slot_calls("int"),
             (unsigned long long)sober_scalar_returns_slot_calls("intV"),
             (unsigned long long)sober_scalar_returns_slot_calls("intA"),
             (unsigned long long)sober_scalar_returns_slot_calls("longV"),
             (unsigned long long)sober_scalar_returns_slot_calls("longA"),
             (unsigned long long)sober_scalar_returns_slot_calls("doubleV"),
             (unsigned long long)sober_scalar_returns_slot_calls("doubleA"));
    return buf;
}

/* ---- env -> delegate lookup -------------------------------------------- */
/* JNIEnv* points to a cell holding the JNINativeInterface_ table pointer;
 * the bridge's SREnvObj has that pointer at offset 0.  `canonical_index` is a
 * slot in that table (four reserved pointers, then fn[0..230]). */
static void *sr_env_delegate(void *env, unsigned canonical_index)
{
    if (!env) return NULL;
    void *functions = *(void **)env;
    if (!functions) return NULL;
    return ((void **)functions)[canonical_index];
}

/* ---- JNI-ABI scalar call wrappers -------------------------------------- *
 * Pure forwarders.  They add a local diagnostic count and invoke the existing
 * instance-form wrapper, which calls sr_dispatch_call() and returns the
 * handler's value.  No callback is emitted or counted by this module.
 * ------------------------------------------------------------------------ */
uint8_t sober_scalar_CallStaticBooleanMethodV(void *env, void *c,
                                               void *m, va_list args)
{
    sr_bool_v_fn fn = (sr_bool_v_fn)(uintptr_t)
                      sr_env_delegate(env, SR_CANON_BOOL_V);
    SR_BUMP(SR_I_BOOL_V);
    return fn ? fn(env, c, m, args) : 0;
}

uint8_t sober_scalar_CallStaticBooleanMethodA(void *env, void *c,
                                               void *m, void *args)
{
    sr_bool_a_fn fn = (sr_bool_a_fn)(uintptr_t)
                      sr_env_delegate(env, SR_CANON_BOOL_A);
    SR_BUMP(SR_I_BOOL_A);
    return fn ? fn(env, c, m, args) : 0;
}

int32_t sober_scalar_CallStaticIntMethod(void *env, void *c, void *m, ...)
{
    /* G11: sr_CallStaticIntMethod discarded the dispatcher result and always
     * returned 0.  Forward the varargs to the instance Int V-form, which
     * returns (int32_t)sr_dispatch_call(...). */
    sr_int_v_fn fn = (sr_int_v_fn)(uintptr_t)
                     sr_env_delegate(env, SR_CANON_INT_V);
    SR_BUMP(SR_I_INT);
    if (!fn) return 0;
    va_list ap;
    va_start(ap, m);
    int32_t r = fn(env, c, m, ap);
    va_end(ap);
    return r;
}

int32_t sober_scalar_CallStaticIntMethodV(void *env, void *c,
                                          void *m, va_list args)
{
    sr_int_v_fn fn = (sr_int_v_fn)(uintptr_t)
                     sr_env_delegate(env, SR_CANON_INT_V);
    SR_BUMP(SR_I_INT_V);
    return fn ? fn(env, c, m, args) : 0;
}

int32_t sober_scalar_CallStaticIntMethodA(void *env, void *c,
                                          void *m, void *args)
{
    sr_int_a_fn fn = (sr_int_a_fn)(uintptr_t)
                     sr_env_delegate(env, SR_CANON_INT_A);
    SR_BUMP(SR_I_INT_A);
    return fn ? fn(env, c, m, args) : 0;
}

int64_t sober_scalar_CallStaticLongMethodV(void *env, void *c,
                                           void *m, va_list args)
{
    sr_long_v_fn fn = (sr_long_v_fn)(uintptr_t)
                      sr_env_delegate(env, SR_CANON_LONG_V);
    SR_BUMP(SR_I_LONG_V);
    return fn ? fn(env, c, m, args) : 0;
}

int64_t sober_scalar_CallStaticLongMethodA(void *env, void *c,
                                           void *m, void *args)
{
    sr_long_a_fn fn = (sr_long_a_fn)(uintptr_t)
                      sr_env_delegate(env, SR_CANON_LONG_A);
    SR_BUMP(SR_I_LONG_A);
    return fn ? fn(env, c, m, args) : 0;
}

double sober_scalar_CallStaticDoubleMethodV(void *env, void *c,
                                            void *m, va_list args)
{
    sr_double_v_fn fn = (sr_double_v_fn)(uintptr_t)
                        sr_env_delegate(env, SR_CANON_DOUBLE_V);
    SR_BUMP(SR_I_DOUBLE_V);
    return fn ? fn(env, c, m, args) : 0.0;
}

double sober_scalar_CallStaticDoubleMethodA(void *env, void *c,
                                            void *m, void *args)
{
    sr_double_a_fn fn = (sr_double_a_fn)(uintptr_t)
                        sr_env_delegate(env, SR_CANON_DOUBLE_A);
    SR_BUMP(SR_I_DOUBLE_A);
    return fn ? fn(env, c, m, args) : 0.0;
}

/* ---- env-table installers ---------------------------------------------- */
int sober_scalar_returns_install_slots(void **env_fn, unsigned slot_count)
{
    if (!sober_scalar_returns_enabled()) return SOBER_SCALAR_DISABLED;
    if (!env_fn) return SOBER_SCALAR_ERR;
    if (slot_count <= SOBER_SCALAR_SLOT_DOUBLE_A) return SOBER_SCALAR_ERR;

    env_fn[SOBER_SCALAR_SLOT_BOOL_V] =
        (void *)(uintptr_t)&sober_scalar_CallStaticBooleanMethodV;   /* 0x3b0 */
    env_fn[SOBER_SCALAR_SLOT_BOOL_A] =
        (void *)(uintptr_t)&sober_scalar_CallStaticBooleanMethodA;   /* 0x3b8 */
    env_fn[SOBER_SCALAR_SLOT_INT] =
        (void *)(uintptr_t)&sober_scalar_CallStaticIntMethod;        /* 0x408 */
    env_fn[SOBER_SCALAR_SLOT_INT_V] =
        (void *)(uintptr_t)&sober_scalar_CallStaticIntMethodV;       /* 0x410 */
    env_fn[SOBER_SCALAR_SLOT_INT_A] =
        (void *)(uintptr_t)&sober_scalar_CallStaticIntMethodA;       /* 0x418 */
    env_fn[SOBER_SCALAR_SLOT_LONG_V] =
        (void *)(uintptr_t)&sober_scalar_CallStaticLongMethodV;      /* 0x428 */
    env_fn[SOBER_SCALAR_SLOT_LONG_A] =
        (void *)(uintptr_t)&sober_scalar_CallStaticLongMethodA;      /* 0x430 */
    env_fn[SOBER_SCALAR_SLOT_DOUBLE_V] =
        (void *)(uintptr_t)&sober_scalar_CallStaticDoubleMethodV;    /* 0x458 */
    env_fn[SOBER_SCALAR_SLOT_DOUBLE_A] =
        (void *)(uintptr_t)&sober_scalar_CallStaticDoubleMethodA;    /* 0x460 */
    return SOBER_SCALAR_OK;
}

int sober_scalar_returns_install_via(
        void *ctx, void (*setfn)(void *ctx, unsigned slot, void *fn))
{
    if (!sober_scalar_returns_enabled()) return SOBER_SCALAR_DISABLED;
    if (!setfn) return SOBER_SCALAR_ERR;

    setfn(ctx, SOBER_SCALAR_SLOT_BOOL_V,
          (void *)(uintptr_t)&sober_scalar_CallStaticBooleanMethodV);
    setfn(ctx, SOBER_SCALAR_SLOT_BOOL_A,
          (void *)(uintptr_t)&sober_scalar_CallStaticBooleanMethodA);
    setfn(ctx, SOBER_SCALAR_SLOT_INT,
          (void *)(uintptr_t)&sober_scalar_CallStaticIntMethod);
    setfn(ctx, SOBER_SCALAR_SLOT_INT_V,
          (void *)(uintptr_t)&sober_scalar_CallStaticIntMethodV);
    setfn(ctx, SOBER_SCALAR_SLOT_INT_A,
          (void *)(uintptr_t)&sober_scalar_CallStaticIntMethodA);
    setfn(ctx, SOBER_SCALAR_SLOT_LONG_V,
          (void *)(uintptr_t)&sober_scalar_CallStaticLongMethodV);
    setfn(ctx, SOBER_SCALAR_SLOT_LONG_A,
          (void *)(uintptr_t)&sober_scalar_CallStaticLongMethodA);
    setfn(ctx, SOBER_SCALAR_SLOT_DOUBLE_V,
          (void *)(uintptr_t)&sober_scalar_CallStaticDoubleMethodV);
    setfn(ctx, SOBER_SCALAR_SLOT_DOUBLE_A,
          (void *)(uintptr_t)&sober_scalar_CallStaticDoubleMethodA);
    return SOBER_SCALAR_OK;
}
