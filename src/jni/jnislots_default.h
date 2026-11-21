/*
 * jnislots_default.h - DEFAULT-ON consolidation of the remaining JNIEnv
 *                      function-table gaps (object/scalar static calls,
 *                      object arrays + NewByteArray, typed UTF-8 strings).
 *
 * OWNER / SCOPE
 * -------------
 * Agent T3-2, logical Team 3 (JNI + render).  Scope: THIS FILE and
 * jnislots_default.c ONLY (both NEW).  src/jni/native_jni.c, CMakeLists.txt,
 * the frozen engine and the decision-013 marker are owned by others and were
 * NOT edited.  The kill switch is SOBER_JNISLOTS_DEFAULT=0.
 *
 * WHY THIS EXISTS (SHA-bound evidence; engine READ ONLY)
 * ------------------------------------------------------
 * Frozen engine /tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so
 *   SHA-256  3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335
 *   BuildID  204ad049599c96afb75b8a21d5051063e821d6c9
 *
 * analysis/B05-jnienv-gaps.md §3 and analysis/REVIEW_A5_jni.md §3 establish
 * that these slots are sr_stub (native_jni.c:337-341) or semantically wrong
 * by default, even though the guest calls them on the app-shell/lifecycle
 * path.  fn index = JNIEnv byte offset / 8 - 4 (four reserved words precede
 * SRNativeInterface::fn[231], native_jni.c:70-74):
 *
 *   slot  offset  symbol                              state / guest evidence
 *   ----  ------  -------------------------------     -------------------------
 *   111   0x398   CallStaticObjectMethodV             stub; thunk 0x23150f8
 *                                                      calls *0x398 at
 *                                                      0x231516e (102 sites)
 *   112   0x3a0   CallStaticObjectMethodA             stub; 31 A-form sites
 *   114   0x3b0   CallStaticBooleanMethodV            stub; InitWithParams
 *                                                      thunk 0x24ace34
 *   115   0x3b8   CallStaticBooleanMethodA            stub
 *   125   0x408   CallStaticIntMethod                 W: sr_CallStaticIntMethod
 *                                                      discards result (:596)
 *   126   0x410   CallStaticIntMethodV                stub (29 sites)
 *   127   0x418   CallStaticIntMethodA                stub (22 sites)
 *   128   0x420   CallStaticLongMethod                plain J/Android getter
 *   129   0x428   CallStaticLongMethodV               stub (11 sites)
 *   130   0x430   CallStaticLongMethodA                stub
 *   135   0x458   CallStaticDoubleMethodV             stub (44 sites)
 *   136   0x460   CallStaticDoubleMethodA             stub (15 sites)
 *   165   0x548   GetStringUTFChars                   W: sr_validate requires
 *                                                      SR_K_STRING (:671), but
 *                                                      getters return SR_K_OBJECT
 *                                                      (:393, :1055)
 *   167   0x558   GetArrayLength                      W: always 0 (:721)
 *   168   0x560   NewObjectArray                      W: one generic object
 *   169   0x568   GetObjectArrayElement               W: always NULL
 *   170   0x570   SetObjectArrayElement               W: no-op
 *   172   0x580   NewByteArray                        stub; wrapper 0x2a20fd0
 *                                                      calls *0x580 at
 *                                                      0x2a20fe6 then tests RAX
 *
 * The four delivered opt-in modules (static_object_calls.c, scalar_returns.c,
 * jni_arrays.c, jni_strings.c) each repair part of this set, are default OFF,
 * are gated by withhold-listed env vars, and keep four independent state
 * blocks.  THIS module is their DEFAULT-ON counterpart: one installer, one
 * delegate table, one counter array and one array registry.  It is disabled
 * only by SOBER_JNISLOTS_DEFAULT=0 (kill switch, clean no-op).
 *
 * FIDELITY CONTRACT
 * -----------------
 *  * Object/scalar slots are thin, correctly typed ABI forwarders to the
 *    bridge's ALREADY CORRECT instance-form slots (fn[31]/[32], [34]/[35],
 *    [46]/[47], [49]/[50], [55]/[56]), captured at install time.  Those
 *    wrappers call sr_dispatch_call(); the dispatcher does not distinguish a
 *    jclass from a jobject (native_jni.c:826-827).  No handler table, callback
 *    or marker logic is duplicated.
 *  * GetStringUTFChars calls the captured original first (so its isCopy
 *    write, counting and payload handling stay intact) and only rescues a
 *    shaped callback-returned SR_K_OBJECT "java/lang/String" whose text the
 *    original rejects.  No callback is fabricated or triggered here.
 *  * Array slots use ONE small ART-shaped host registry (length at +0x08,
 *    object elements at +0x10, byte elements at +0x0C, identity trailer).
 *    Handles passed in that are not ours are forwarded to the captured
 *    original, so an opt-in jni_arrays.c registry stays readable as well.
 *  * Installing writes function pointers into a host-side table only: no
 *    guest byte is patched, no host function is put into a guest vtable, no
 *    guest->Java callback is emitted, and no decision-013 counter/marker is
 *    touched.  Enabling this module is therefore NOT app-shell proof.
 *
 * LEAD WIRING (exact; lead-owned files, not touched here)
 * -------------------------------------------------------
 * 1. Add src/jni/jnislots_default.c to the sober_jni target in
 *    src/jni/CMakeLists.txt.
 * 2. In sr_build_tables() (native_jni.c), AFTER the default SETFN block and
 *    after the two legacy opt-in calls at :873-874:
 *
 *      #include "jnislots_default.h"
 *      ...
 *      (void)sober_jnislots_default_install(t->fn, 231);
 *
 *    t->fn is the 231-entry void* array (WITHOUT the four reserved words).
 *    The call is a no-op returning SOBER_JNISLOTS_DISABLED when the kill
 *    switch is set, and is idempotent.
 * 3. Alternative from guest_app_shell.c after sober_jni_init():
 *      sober_jnislots_default_install_env(sober_jni_env(j), 231);
 *    This derives the same fn[] array from a live JNIEnv* (and can be used
 *    for a JVM copy of the table installed by javavm_default.c).
 * 4. Diagnostics / verification:
 *      char buf[512];
 *      sober_jnislots_default_describe(buf, sizeof buf);
 *      int all = sober_jnislots_default_verify(t->fn, 231, buf2, sizeof buf2);
 */
#ifndef SOBER_JNISLOTS_DEFAULT_H
#define SOBER_JNISLOTS_DEFAULT_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Root build uses -fvisibility=hidden; publish every entry point. */
#if defined(__GNUC__) || defined(__clang__)
#  define SOBER_JNISLOTS_API __attribute__((visibility("default")))
#else
#  define SOBER_JNISLOTS_API
#endif

/* ---- gate / return codes ------------------------------------------------ */
#define SOBER_JNISLOTS_DEFAULT_ENV   "SOBER_JNISLOTS_DEFAULT"

/* Default ON.  The ONLY value that disables the installer is exactly "0". */
SOBER_JNISLOTS_API int         sober_jnislots_default_enabled(void);
SOBER_JNISLOTS_API const char *sober_jnislots_default_env_vars(void);

#define SOBER_JNISLOTS_OK          0
#define SOBER_JNISLOTS_ERR       (-1)
#define SOBER_JNISLOTS_DISABLED (-2)

/* ---- ABI constants (informational; fn indices, byte offsets) ----------- */
#define SOBER_JNISLOTS_SLOT_OBJ_V          111u  /* 0x398 */
#define SOBER_JNISLOTS_SLOT_OBJ_A          112u  /* 0x3a0 */
#define SOBER_JNISLOTS_SLOT_BOOL_V         114u  /* 0x3b0 */
#define SOBER_JNISLOTS_SLOT_BOOL_A         115u  /* 0x3b8 */
#define SOBER_JNISLOTS_SLOT_INT            125u  /* 0x408 */
#define SOBER_JNISLOTS_SLOT_INT_V          126u  /* 0x410 */
#define SOBER_JNISLOTS_SLOT_INT_A          127u  /* 0x418 */
#define SOBER_JNISLOTS_SLOT_LONG           128u  /* 0x420 */
#define SOBER_JNISLOTS_SLOT_LONG_V         129u  /* 0x428 */
#define SOBER_JNISLOTS_SLOT_LONG_A         130u  /* 0x430 */
#define SOBER_JNISLOTS_SLOT_DOUBLE_V       135u  /* 0x458 */
#define SOBER_JNISLOTS_SLOT_DOUBLE_A       136u  /* 0x460 */
#define SOBER_JNISLOTS_SLOT_UTF8           165u  /* 0x548 */
#define SOBER_JNISLOTS_SLOT_GETARRAYLEN    167u  /* 0x558 */
#define SOBER_JNISLOTS_SLOT_NEWOBJARRAY    168u  /* 0x560 */
#define SOBER_JNISLOTS_SLOT_GETARRAYELEM   169u  /* 0x568 */
#define SOBER_JNISLOTS_SLOT_SETARRAYELEM   170u  /* 0x570 */
#define SOBER_JNISLOTS_SLOT_NEWBYTEARRAY   172u  /* 0x580 */

#define SOBER_JNISLOTS_OFF_OBJ_V           0x398u
#define SOBER_JNISLOTS_OFF_OBJ_A           0x3a0u
#define SOBER_JNISLOTS_OFF_BOOL_V          0x3b0u
#define SOBER_JNISLOTS_OFF_BOOL_A          0x3b8u
#define SOBER_JNISLOTS_OFF_INT             0x408u
#define SOBER_JNISLOTS_OFF_INT_V           0x410u
#define SOBER_JNISLOTS_OFF_INT_A           0x418u
#define SOBER_JNISLOTS_OFF_LONG            0x420u
#define SOBER_JNISLOTS_OFF_LONG_V          0x428u
#define SOBER_JNISLOTS_OFF_LONG_A          0x430u
#define SOBER_JNISLOTS_OFF_DOUBLE_V        0x458u
#define SOBER_JNISLOTS_OFF_DOUBLE_A        0x460u
#define SOBER_JNISLOTS_OFF_UTF8            0x548u
#define SOBER_JNISLOTS_OFF_GETARRAYLEN     0x558u
#define SOBER_JNISLOTS_OFF_NEWOBJARRAY     0x560u
#define SOBER_JNISLOTS_OFF_GETARRAYELEM    0x568u
#define SOBER_JNISLOTS_OFF_SETARRAYELEM    0x570u
#define SOBER_JNISLOTS_OFF_NEWBYTEARRAY    0x580u

#define SOBER_JNISLOTS_EXPECTED_BUILD_ID32 0x204ad049u
#define SOBER_JNISLOTS_SLOT_COUNT          231u

/* ---- installers / state ------------------------------------------------- */
/* `env_fn` points at fn[0] of the 231-slot JNIEnv function array (i.e.
 * &SoberJni::env_table.fn[0]), NOT at the four reserved header words.
 * Captures the real delegates first, then writes the 18 slots listed above.
 * Returns SOBER_JNISLOTS_DISABLED (no writes) when SOBER_JNISLOTS_DEFAULT=0,
 * SOBER_JNISLOTS_ERR on bad arguments, SOBER_JNISLOTS_OK otherwise.  Idempotent.
 * Installing a second (e.g. JavaVM-copy) table reuses the first delegates. */
SOBER_JNISLOTS_API int sober_jnislots_default_install(void **env_fn,
                                                      unsigned slot_count);

/* Convenience: derive fn[] from a live JNIEnv* (*env is the interface table
 * with four reserved words, so fn[] = (void **)*env + 4).  `slot_count` is
 * normally 231.  NULL env / short table -> SOBER_JNISLOTS_ERR. */
SOBER_JNISLOTS_API int sober_jnislots_default_install_env(void *env,
                                                          unsigned slot_count);

/* Non-zero once at least one table has actually been patched. */
SOBER_JNISLOTS_API int sober_jnislots_default_is_installed(void);

/* Returns 1 only when every one of the 18 slots of `env_fn` holds this
 * module's entry point; always fills buf (when cap > 0) with a one-line
 * report.  Returns 0 for a short/NULL table or a missing slot. */
SOBER_JNISLOTS_API int sober_jnislots_default_verify(void **env_fn,
                                                     unsigned slot_count,
                                                     char *buf, size_t cap);

/* ---- counters / diagnostics (single shared counter array) --------------- *
 * Keys: "installs", "objV", "objA", "boolV", "boolA", "int", "intV",
 * "intA", "long", "longV", "longA", "doubleV", "doubleA", "utf8",
 * "utf8Rescue", "arrayLen", "arrayNewObj", "arrayGet", "arraySet",
 * "arrayNewByte".  NULL = total of every counter.  Unknown key = 0. */
SOBER_JNISLOTS_API uint64_t sober_jnislots_default_calls(const char *key);
SOBER_JNISLOTS_API const char *sober_jnislots_default_describe(char *buf,
                                                              size_t cap);
/* Zero the counters and release the array registry (table wiring stays). */
SOBER_JNISLOTS_API void sober_jnislots_default_reset(void);

#ifdef __cplusplus
}
#endif
#endif /* SOBER_JNISLOTS_DEFAULT_H */
