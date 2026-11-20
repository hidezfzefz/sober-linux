/*
 * static_object_calls.h - opt-in, faithful replacements for the stubbed JNIEnv
 *                         object-returning static call slots and the typed
 *                         GetStringUTFChars gate (analysis B05, decision 013).
 *
 * OWNER / SCOPE
 * -------------
 * Worker B12, logical Team B (wave 2).  Scope: THIS FILE and
 * static_object_calls.c ONLY.  src/jni/native_jni.c, CMakeLists.txt, the guest
 * engine and the decision-013 marker are owned by others and were NOT edited.
 *
 * WHY THIS EXISTS (SHA-bound evidence, engine READ ONLY)
 * ------------------------------------------------------
 * Engine /tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so
 *   SHA-256  3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335
 *   BuildID  204ad049599c96afb75b8a21d5051063e821d6c9
 *
 *   objdump of the frozen engine:
 *     0x23150f8:  <thunk>  movaps XMM spill ; mov (%rdi),%rax
 *     0x231516e:  call *0x398(%rax)          <-- CallStaticObjectMethodV
 *     A-form sites also present, e.g. 0x2803e89: call *0x3a0(%rax).
 *   Native bridge (src/jni/native_jni.c, read only):
 *     :70-74     SRNativeInterface { reserved[4]; void *fn[231]; }
 *     :777       every fn starts as sr_stub
 *     :828       fn[110] = sr_CallStaticObjectMethod  (plain form only)
 *     :838-839   fn[138]/fn[139] = static void V/A (gameLoadedCallback route)
 *     :847       fn[165] = sr_GetStringUTFChars
 *     :671       sr_GetStringUTFChars requires sr_validate(..., SR_K_STRING)
 *     :512,:1055 callback results are created as SR_K_OBJECT ("java/lang/String")
 *     => fn[111]/fn[112] are sr_stub (all object getters return NULL), and
 *        fn[165] rejects the SR_K_OBJECT jstrings the getters actually return.
 *
 * Slot arithmetic (settled in analysis/B05-jnienv-gaps.md §1): fn index =
 * canonical byte offset / 8 - 4, hence
 *   CallStaticObjectMethodV 0x398 -> fn[111]
 *   CallStaticObjectMethodA 0x3a0 -> fn[112]
 *   GetStringUTFChars       0x548 -> fn[165]
 *
 * WHAT THIS MODULE DOES
 * ---------------------
 * 1. Installs JNI-ABI replacements into fn[111], fn[112] and fn[165] that
 *    delegate to the bridge's ALREADY WIRED instance-form slots fn[31]/fn[32]
 *    (sr_CallObjectMethodV/A, both dispatch with want_object=1) and to the
 *    original fn[165].  The dispatcher deliberately treats jclass and jobject
 *    identically (native_jni.c:826-827), so the delegation is faithful and
 *    returns the same shaped synthetic object as sr_CallStaticObjectMethod.
 * 2. Adds the missing typed-string gate: for a callback-returned SrObject with
 *    class_name "java/lang/String" (kind SR_K_OBJECT, text in `name` because
 *    sr_default_app_shell_handler does not set `payload`) the replacement
 *    returns that text instead of the original empty string.  The original
 *    fn[165] is called FIRST and stays authoritative whenever it yields a
 *    payload; the rescue also covers shaped SR_K_STRING handles that the
 *    bridge's object registry does not know about (e.g. jni_strings.c).
 *
 * DECISION 013 COMPLIANCE
 * -----------------------
 * Default OFF: enabled only by getenv("SOBER_STATIC_OBJECT_CALLS") == "1".
 * No guest byte is patched, no host function is placed in a guest vtable, no
 * callback is fabricated or triggered by this module (only genuine guest calls
 * through the table reach the bridge's existing dispatcher), no counter or
 * marker belonging to the decision-013 gate is modified.  Enabling this module
 * is therefore NOT app-shell proof and must not relax the marker gate.
 *
 * LEAD WIRING (exact; native_jni.c is lead-owned)
 * ------------------------------------------------
 * In sr_build_tables(), AFTER the default SETFN block and AFTER the existing
 * sober_platform_fields_install_slots() call (the delegate slots fn[31]/fn[32]
 * must already hold sr_CallObjectMethodV/A and fn[165] must already hold
 * sr_GetStringUTFChars):
 *
 *     #include "static_object_calls.h"
 *     ...
 *     (void)sober_static_object_calls_install_slots(j->env_table.fn, 231);
 *
 * Add static_object_calls.c to the sober_jni target in src/jni/CMakeLists.txt.
 * The install is a no-op returning SOBER_SOC_DISABLED unless the env var is 1.
 */
#ifndef SOBER_STATIC_OBJECT_CALLS_H
#define SOBER_STATIC_OBJECT_CALLS_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SoberJni SoberJni;   /* native_jni.h owns the full definition */

/* ---- return codes (numerically identical to SOBER_JNI_OK/ERR) ---------- */
#define SOBER_SOC_OK         0
#define SOBER_SOC_ERR       (-1)
#define SOBER_SOC_DISABLED  (-2)

/* ---- opt-in ------------------------------------------------------------ */
#define SOBER_STATIC_OBJECT_CALLS_ENV "SOBER_STATIC_OBJECT_CALLS"

/* Default OFF.  True only when the env var is exactly "1". */
int         sober_static_object_calls_enabled(void);
const char *sober_static_object_calls_env_vars(void);

/* ---- ABI constants (informational, for diagnostics/verification) -------- */
#define SOBER_SOC_OFF_CALLSTATICOBJECTMETHODV 0x398u
#define SOBER_SOC_OFF_CALLSTATICOBJECTMETHODA 0x3a0u
#define SOBER_SOC_OFF_GETSTRINGUTFCHARS       0x548u

#define SOBER_SOC_SLOT_CALLSTATICOBJECTMETHODV 111u   /* 0x398 */
#define SOBER_SOC_SLOT_CALLSTATICOBJECTMETHODA 112u   /* 0x3a0 */
#define SOBER_SOC_SLOT_GETSTRINGUTFCHARS       165u   /* 0x548 */
/* Delegate slots captured by the installer (bridge's real implementations). */
#define SOBER_SOC_SLOT_CALLOBJECTMETHODV        31u   /* 0x118 */
#define SOBER_SOC_SLOT_CALLOBJECTMETHODA        32u   /* 0x120 */

#define SOBER_SOC_BUILDID32   0x204ad049u
#define SOBER_SOC_THUNK_RVA   0x23150f8u
#define SOBER_SOC_THUNK_CALL  0x231516eu

/* ---- JNI-ABI entry points (installed into the table) ------------------- */
/* isCopy has jboolean* ABI (uint8_t*).  The object-call functions forward a
 * guest-initiated call; they do not create or dispatch a callback on their own. */
void *sober_static_object_calls_CallStaticObjectMethodV(void *env, void *clazz,
                                                        void *method,
                                                        va_list args);
void *sober_static_object_calls_CallStaticObjectMethodA(void *env, void *clazz,
                                                        void *method,
                                                        void *args);
const char *sober_static_object_calls_GetStringUTFChars(void *env, void *str,
                                                        unsigned char *isCopy);

/* ---- installer / state -------------------------------------------------- */
/* `env_fn` is &SoberJni::env_table.fn[0]; `slot_count` is normally 231.
 * Captures the real delegate pointers, then writes fn[111], fn[112] and
 * fn[165].  Returns SOBER_SOC_DISABLED (no writes) unless the opt-in is on;
 * SOBER_SOC_ERR on bad arguments; SOBER_SOC_OK on success.  Idempotent. */
int  sober_static_object_calls_install_slots(void **env_fn, unsigned slot_count);
int  sober_static_object_calls_wired(void);

typedef struct {
    uint64_t installs;            /* successful install calls */
    uint64_t call_static_object_v; /* fn[111] invocations forwarded */
    uint64_t call_static_object_a; /* fn[112] invocations forwarded */
    uint64_t utf8_delegated;      /* fn[165] calls answered by the original */
    uint64_t utf8_typed_rescues;  /* fn[165] calls rescued for SR_K_OBJECT */
} SoberStaticObjectCallsDiag;

void        sober_static_object_calls_diag(SoberStaticObjectCallsDiag *out);
const char *sober_static_object_calls_describe(char *buf, size_t cap);

#ifdef __cplusplus
}
#endif
#endif /* SOBER_STATIC_OBJECT_CALLS_H */
