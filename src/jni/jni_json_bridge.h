/*
 * jni_json_bridge.h - Faithful jstring + JSON flag-override path (task #454).
 *
 * Worker: flagbridge.  Scope: NEW files src/jni/jni_json_bridge.c/.h only.
 *
 * WHY THIS EXISTS
 * ---------------
 * The guest's Java-side flag override is delivered through
 *   nativePreloadFlagOverrides(String)   RVA 0x2edd859
 * which wraps the jstring with the genuine JNI helper
 *   jstring_to_stdstring                 RVA 0x2315532
 *   -> (*env)->GetStringUTFChars(env, str, 0)   [JNIEnv +0x548, fn[165]]
 *   -> std::string(out, chars)                  [RVA 0x1dc1208]
 *   -> (*env)->ReleaseStringUTFChars(env, ... ) [JNIEnv +0x550, fn[166]]
 * and stores the resulting std::string into the guest globals
 *   0x72a3b20 (24-byte short-string object) / 0x72a3b30 (heap ptr/length).
 *
 * Application happens later in the NativeDM state machine, reached from
 *   nativeRetryInit                      RVA 0x2edd739
 *   -> 0x2ee2766 (state gate: singleton @0x72a3a30, state must be 1 or 0xb)
 *   -> 0x2ee4aaf (reads global string @0x72a3b20)
 *   -> 0x237dc30 (JSON parse driver)
 *   -> 0x236b98e (parser) -> 0x237de09 (member loop)
 *   -> 0x2364466 (flag lookup) -> 0x237df9c / 0x237dfd0 (setter).
 *
 * DIAGNOSIS (verified by A/B in this worktree, see .c header comment):
 *   * Our bridge's GetStringUTFChars is correctly slotted (fn[165] = +0x548)
 *     and returns the exact C payload.  nativePreloadFlagOverrides stores
 *     the JSON correctly in globals 0x72a3b20/0x72a3b30 (confirmed live:
 *     stored='{"EnableAppBridgeComponents":true}', byte0=0x29 -> heap form).
 *     So the jstring bytes are NOT the cause of "flag still 0".
 *   * Two real gates prevented the JSON member loop from writing the FFlag:
 *       (a) the NativeDM state machine (nativeRetryInit only applies when
 *           global 0x72a3a30 is non-NULL and its state is 1 or 0xb), and
 *       (b) decisively, the flag DESCRIPTOR thunk 0x22e567a was never run in
 *           our harness.  The constructor at 0x1e106f4 only builds the
 *           descriptor table; 0x22e567a performs the real registration via
 *           0x227c18e(registry@0x7cdd118, "EnableAppBridgeComponents",
 *           &flag@0x72cb648, default=1, type=4).  Without it the member loop's
 *           lookup at 0x2364466 cannot find the flag, so its setter at
 *           0x237df9c never writes 0x72cb648.
 *     A/B proof: flag stayed 0 through preload+retry+parse driver; after
 *     calling 0x22e567a and the guest's own setter 0x23648be with
 *     name/value short-strings ("EnableAppBridgeComponents"/"True"), the
 *     guest wrote flag=1 (rc=1) -- no direct byte write by us.
 *
 * THIS FILE supplies an OPT-IN path that stays faithful:
 *   1. sober_jni_json_make_string() builds a real java/lang.String SrObject
 *      whose UTF-8 payload is the JSON literal.  The guest wrapper 0x2315532
 *      reaches the bytes exclusively via GetStringUTFChars (fn[165]) -- the
 *      UTF-16 GetStringChars family is NOT used by this path, so the string's
 *      synthetic guest_c graph node is never dereferenced here.
 *   2. sober_jni_json_apply_flag_overrides() replays nativePreloadFlagOverrides
 *      + nativeRetryInit, then (only if the flag is still 0) invokes the
 *      guest's OWN JSON parse driver 0x237dc30 with the stored global string.
 *      The flag bit is set by the guest's own lookup+setter; we never write
 *      0x72cb648 ourselves.  Gate on SOBER_JSON_FLAG_BRIDGE (default OFF).
 *   3. sober_jni_json_populate_app_singleton() replays
 *      nativePostClientSettingsLoadedInitialization3 (0x2383c18) with a
 *      List-shaped jobject backed by real JNI getter handlers, so the guest
 *      creates app singleton 0x72cb5e0 itself.  Gate on
 *      SOBER_JSON_APP_SINGLETON (default OFF).
 *
 * NO direct flag byte writes, NO fabricated callbacks, NO gate changes.
 */
#ifndef SOBER_JNI_JSON_BRIDGE_H
#define SOBER_JNI_JSON_BRIDGE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Result codes for the opt-in JSON bridge. */
#define SOBER_JSON_OK          0
#define SOBER_JSON_ERR        (-1)
#define SOBER_JSON_DISABLED   (-2)  /* env var not set (default OFF) */
#define SOBER_JSON_NOFLAG     (-3)  /* guest parser ran but flag still 0 */

/* Build identity guard: all RVAs in this file are for the AppImage engine
 * BuildID 204ad049… (SHA 3e498d6494…).  The caller MUST verify the guest
 * build before invoking any of these functions; the functions also re-check
 * the 4-byte BuildID note at guest_base + 0x2f0 when available. */
#define SOBER_JSON_GUEST_BUILDID_RVA  0x2f0u   /* ELF note payload (informational) */
#define SOBER_JSON_BUILDID32          0x204ad049u

/* Guest RVAs used by this bridge (AppImage 204ad049). */
enum {
    SOBER_JSON_RVA_PRELOAD_FLAG   = 0x2edd859u, /* nativePreloadFlagOverrides */
    SOBER_JSON_RVA_RETRY_INIT     = 0x2edd739u, /* nativeRetryInit            */
    SOBER_JSON_RVA_JSTRING_HELPER = 0x2315532u, /* jstring -> std::string      */
    SOBER_JSON_RVA_STORE_GLOBAL   = 0x2ee549fu, /* store std::string to globals*/
    SOBER_JSON_RVA_PARSE_DRIVER   = 0x237dc30u, /* JSON parse/apply driver     */
    SOBER_JSON_RVA_JSON_PARSER    = 0x236b98eu, /* JSON parser                 */
    SOBER_JSON_RVA_MEMBER_LOOP    = 0x237de09u, /* member iteration            */
    SOBER_JSON_RVA_FLAG_LOOKUP    = 0x2364466u, /* flag registry lookup        */
    SOBER_JSON_RVA_FLAG_SETTER    = 0x237df9cu, /* indirect setter call        */
    SOBER_JSON_RVA_POST_SETTINGS  = 0x2383c18u, /* nativePostClientSettings…3  */
    SOBER_JSON_RVA_FLAG_BYTE      = 0x72cb648u, /* FFlag EnableAppBridge…      */
    SOBER_JSON_RVA_APP_SINGLETON  = 0x72cb5e0u, /* app singleton pointer       */
    SOBER_JSON_RVA_STORED_STRING  = 0x72a3b20u, /* stored std::string (24B)    */
    SOBER_JSON_RVA_PARSE_CTX      = 0x7697cb0u, /* parse-driver output context */
    SOBER_JSON_RVA_PARSE_CTX_FLAG = 0x7697d08u  /* u16 flag set around parse   */
};

typedef struct SoberJni SoberJni;

/* ---- opt-in environment variables ------------------------------------- *
 * SOBER_JSON_FLAG_BRIDGE   enable the flag-override path (default OFF)
 * SOBER_JSON_APP_SINGLETON enable the app-singleton path (default OFF)
 * SOBER_JSON_VERBOSE       print per-step diagnostics to stderr
 * ----------------------------------------------------------------------- */
const char *sober_json_env_vars(void);  /* human-readable summary */

/* Build the jstring consumed by nativePreloadFlagOverrides.  `text` is a
 * NUL-terminated UTF-8 JSON literal; the returned java/lang.String SrObject
 * carries those bytes in SrObject.payload, which sr_GetStringUTFChars (fn[165])
 * returns verbatim.  This is the only accessor the guest wrapper 0x2315532
 * calls, so the JSON reaches the guest's std::string unchanged.  (The generic
 * SrObject also has a synthetic guest_c graph node, but the UTF-16 family is
 * not used by this path.)  Returns NULL only on allocation failure. */
void *sober_jni_json_make_string(SoberJni *j, const char *text);

/* Apply the JSON flag override.  Opt-in: returns SOBER_JSON_DISABLED when
 * getenv("SOBER_JSON_FLAG_BRIDGE") is NULL.  Sequence:
 *   jstring(JSON) -> nativePreloadFlagOverrides(0x2edd859)
 *   -> nativeRetryInit(0x2edd739)
 *   -> if flag still 0: guest JSON parse driver 0x237dc30(parse_ctx, json)
 * Returns SOBER_JSON_OK if FFlag 0x72cb648 != 0 afterwards,
 * SOBER_JSON_NOFLAG if the guest ran but the flag stayed 0,
 * SOBER_JSON_ERR on bad arguments.  `json` defaults to the canonical
 * {"EnableAppBridgeComponents":true} when NULL.  No guest bytes are patched;
 * no callback is fabricated. */
int sober_jni_json_apply_flag_overrides(SoberJni *j, void *guest_base,
                                        void *env, void *clazz,
                                        const char *json);

/* Convenience wrapper: derives env via sober_jni_env(j) and clazz via
 * sober_jni_find_class(j, "com/roblox/engine/jni/NativeGLJavaInterface"),
 * then delegates with json == NULL (canonical override literal).  This is the
 * one-line wiring hook for the lead.  Same opt-in gate and return codes. */
int sober_jni_json_apply_flag_overrides_auto(SoberJni *j, void *guest_base);

/* Populate the app singleton through the guest's own
 * nativePostClientSettingsLoadedInitialization3 (0x2383c18).  Opt-in:
 * returns SOBER_JSON_DISABLED when getenv("SOBER_JSON_APP_SINGLETON") is NULL.
 * The List argument is a real JNI object whose `size`/`get` (and the
 * ApplicationExitInfoC getters) are served by registered native handlers, so
 * the guest iterates it and creates singleton 0x72cb5e0 itself.
 * Returns SOBER_JSON_OK if *(void**)(guest_base+0x72cb5e0) != NULL. */
int sober_jni_json_populate_app_singleton(SoberJni *j, void *guest_base,
                                          void *env, void *clazz);

/* Non-mutating diagnosis: report the stored global string, the flag byte and
 * the app singleton pointer.  Safe to call anytime; returns a static buffer. */
const char *sober_jni_json_diagnose(SoberJni *j, void *guest_base);

/* Return the canonical JSON literal used by the real Android app. */
const char *sober_jni_json_default(void);

#ifdef __cplusplus
}
#endif
#endif /* SOBER_JNI_JSON_BRIDGE_H */
