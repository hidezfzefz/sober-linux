/*
 * scalar_returns.h - opt-in, faithful JNIEnv scalar-call slots (analysis B5).
 *
 * Owner: subagent B13, logical Team B (wave 2).  Scope: this header and
 * scalar_returns.c ONLY (NEW files).  native_jni.c / CMakeLists.txt and the
 * frozen engine are NOT edited by this worker.
 *
 * WHY THIS EXISTS
 * ---------------
 * analysis/B05-jnienv-gaps.md proved that, in the frozen 231-slot JNIEnv table,
 * the static scalar-return call slots are either sr_stub (call with no value
 * handed back) or semantically wrong:
 *
 *   G7  CallStaticBooleanMethodV  envfn +0x3b0 -> fn[114]   (stub)
 *       CallStaticBooleanMethodA  envfn +0x3b8 -> fn[115]   (stub)
 *   G8  CallStaticIntMethodV      envfn +0x410 -> fn[126]   (stub)
 *       CallStaticIntMethodA      envfn +0x418 -> fn[127]   (stub)
 *   G9  CallStaticLongMethodV     envfn +0x428 -> fn[129]   (stub)
 *       CallStaticLongMethodA     envfn +0x430 -> fn[130]   (stub)
 *   G10 CallStaticDoubleMethodV   envfn +0x458 -> fn[135]   (stub)
 *       CallStaticDoubleMethodA   envfn +0x460 -> fn[136]   (stub)
 *   G11 CallStaticIntMethod       envfn +0x408 -> fn[125]   (wired, but the
 *                                  current sr_CallStaticIntMethod discards
 *                                  sr_dispatch_call()'s result and returns 0)
 *
 * Because int/long/double/bool getters (membershipType()I,
 * getProcessTimestamp()J, double getters, AppDelegate boolean getters) reach
 * the guest through these slots, a stub/zero return means the guest reads a
 * default value even when a handler produced a real one.
 *
 * FIDELITY STRATEGY (no new dispatcher, no duplicated handler state)
 * ------------------------------------------------------------------
 * The bridge's instance-form V/A wrappers (native_jni.c:559-583) already call
 * sr_dispatch_call() correctly and the dispatcher explicitly does NOT
 * distinguish a jclass from a jobject (native_jni.c:826-827 comment).  Each
 * function exported here is therefore a thin, correctly-typed JNI-ABI wrapper
 * that forwards to the corresponding *instance* form found in the JNIEnv
 * function table reached through `env` (standard JNI: `*(void**)env` is the
 * JNINativeInterface_ table).  The guest's static call then takes the exact
 * same dispatcher path as the already-correct plain static calls.  No handler
 * table, callback or marker logic is duplicated.
 *
 * DECISION 013 COMPLIANCE
 * -----------------------
 * Default OFF.  Enabled only when getenv("SOBER_SCALAR_RETURNS") == "1".
 * Installing the slots writes function pointers only: it patches no guest
 * byte, emits no callback, changes no counter/marker and is inert while the
 * opt-in is off.  A genuine guest->Java callback, if the guest makes one, is
 * produced solely by the existing dispatcher via the forwarded call.
 *
 * LEAD WIRING (exact; lead-owned files, not touched here)
 * -------------------------------------------------------
 * 1. In native_jni.c sr_build_tables(), immediately after the default SETFN
 *    block and beside the platform_fields install (around :865-867):
 *
 *      #include "scalar_returns.h"
 *      ...
 *      if (sober_scalar_returns_enabled())
 *          sober_scalar_returns_install_slots(t->fn, 231);
 *
 *    (t->fn is the void* fn[231] array; the installer overwrites fn[114],
 *     fn[115], fn[125], fn[126], fn[127], fn[129], fn[130], fn[135], fn[136]
 *     with the wrappers below and leaves every other slot untouched.)
 * 2. Call AFTER the default SETFN block: the forwarded-to instance V/A slots
 *    (fn[34/35/46/47/49/50/55/56]) must already be installed.  No init call is
 *    required; the wrappers are stateless apart from diagnostics counters.
 * 3. Add scalar_returns.c to src/jni/CMakeLists.txt (lead-owned).
 */
#ifndef SOBER_JNI_SCALAR_RETURNS_H
#define SOBER_JNI_SCALAR_RETURNS_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- return codes (numerically identical to SOBER_JNI_OK/ERR) ---------- */
#define SOBER_SCALAR_OK          0
#define SOBER_SCALAR_ERR       (-1)
#define SOBER_SCALAR_DISABLED  (-2)

/* ---- opt-in ------------------------------------------------------------ */
#define SOBER_SCALAR_RETURNS_ENV "SOBER_SCALAR_RETURNS"

/* Default OFF. True only when SOBER_SCALAR_RETURNS=1. */
int         sober_scalar_returns_enabled(void);
const char *sober_scalar_returns_env_vars(void);

/* ---- JNIEnv fn[] indices (fn_index = envfn_offset/8 - 4) ---------------- */
#define SOBER_SCALAR_SLOT_BOOL_V     114u  /* CallStaticBooleanMethodV 0x3b0 */
#define SOBER_SCALAR_SLOT_BOOL_A     115u  /* CallStaticBooleanMethodA 0x3b8 */
#define SOBER_SCALAR_SLOT_INT        125u  /* CallStaticIntMethod      0x408 */
#define SOBER_SCALAR_SLOT_INT_V      126u  /* CallStaticIntMethodV     0x410 */
#define SOBER_SCALAR_SLOT_INT_A      127u  /* CallStaticIntMethodA     0x418 */
#define SOBER_SCALAR_SLOT_LONG_V     129u  /* CallStaticLongMethodV    0x428 */
#define SOBER_SCALAR_SLOT_LONG_A     130u  /* CallStaticLongMethodA    0x430 */
#define SOBER_SCALAR_SLOT_DOUBLE_V   135u  /* CallStaticDoubleMethodV  0x458 */
#define SOBER_SCALAR_SLOT_DOUBLE_A   136u  /* CallStaticDoubleMethodA  0x460 */

/* ---- JNI-ABI functions (install into the JNIEnv table) ----------------- *
 * jboolean=juint8, jint=int32, jlong=int64, jdouble=double (ABI-identical).
 * Each wrapper locates its instance-form delegate through `env` and forwards;
 * it never fabricates a return value when a delegate is available.
 * ------------------------------------------------------------------------ */
uint8_t  sober_scalar_CallStaticBooleanMethodV(void *env, void *clazz,
                                                void *method, va_list args);
uint8_t  sober_scalar_CallStaticBooleanMethodA(void *env, void *clazz,
                                                void *method, void *args);
int32_t  sober_scalar_CallStaticIntMethod(void *env, void *clazz,
                                          void *method, ...);
int32_t  sober_scalar_CallStaticIntMethodV(void *env, void *clazz,
                                           void *method, va_list args);
int32_t  sober_scalar_CallStaticIntMethodA(void *env, void *clazz,
                                           void *method, void *args);
int64_t  sober_scalar_CallStaticLongMethodV(void *env, void *clazz,
                                            void *method, va_list args);
int64_t  sober_scalar_CallStaticLongMethodA(void *env, void *clazz,
                                            void *method, void *args);
double   sober_scalar_CallStaticDoubleMethodV(void *env, void *clazz,
                                              void *method, va_list args);
double   sober_scalar_CallStaticDoubleMethodA(void *env, void *clazz,
                                              void *method, void *args);

/* ---- env-table installers ---------------------------------------------- *
 * `env_fn` is &SoberJni::env_table.fn[0]; `slot_count` is normally 231.
 * Writes ONLY the nine slots listed above.  No-op / SOBER_SCALAR_DISABLED
 * unless SOBER_SCALAR_RETURNS=1.  SOBER_SCALAR_ERR on bad args.
 * install_via() is the setfn-callback alternative for table owners that do
 * not hand out the raw array.
 * ------------------------------------------------------------------------ */
int sober_scalar_returns_install_slots(void **env_fn, unsigned slot_count);
int sober_scalar_returns_install_via(
        void *ctx, void (*setfn)(void *ctx, unsigned slot, void *fn));

/* ---- diagnostics ------------------------------------------------------- */
const char *sober_scalar_returns_describe(char *buf, size_t cap);
/* Per-slot call counts. Key: "boolV"/"boolA"/"int"/"intV"/"intA"/
 * "longV"/"longA"/"doubleV"/"doubleA"; NULL = total forwarded calls. */
uint64_t    sober_scalar_returns_slot_calls(const char *key);

#ifdef __cplusplus
}
#endif
#endif /* SOBER_JNI_SCALAR_RETURNS_H */
