/*
 * javavm_default.h - DEFAULT-ON faithful JavaVM install for the guest.
 *
 * Substrate team T1-3.  NEW FILE ONLY: nothing in src/jni/native_jni.c/.h,
 * src/jni/javavm_semantics.c/.h, src/linux/guest_app_shell.c, the shared
 * CMakeLists.txt or src/jni/CMakeLists.txt is modified by this module.
 *
 * WHY THIS EXISTS
 * ---------------
 * The default run faults at guest RVA 0x2eb4b65 because JNI_OnLoad is skipped,
 * leaving the JNI_OnLoad-cached JavaVM* global at guest RVA 0x7cdec78 NULL.
 * The consumer helper 0x227b9e2 calls JNIInvokeInterface_::GetEnv (+0x30) on
 * that global and the caller at 0x2eb4b65 issues JNIEnv::NewStringUTF (+0x538).
 *
 * The existing faithful VM (javavm_semantics.c, SOBER_FAITHFUL_JAVAVM) and the
 * plain bridge seed (native_jni.c, SOBER_SEED_JNI_VM) are both OPT-IN / WITHHELD
 * and default OFF.  This module is the DEFAULT-ON counterpart: it installs the
 * SAME genuine two-level JavaVM/JNIEnv shape into 0x7cdec78 with NO opt-in env
 * var required.  It is disabled only by the kill switch SOBER_JAVAVM_DEFAULT=0.
 *
 * HONESTY / FIDELITY CONTRACT
 * ---------------------------
 *   * The JNIEnv function table is a verbatim copy of the bridge's fully built
 *     231-slot JNINativeInterface_ table (so every entry is a real bridge JNI
 *     function).  The one override, GetJavaVM, returns THIS VM - an honest
 *     accessor, not a fabricated callback.
 *   * The JavaVM function table is a real JNIInvokeInterface_ table whose five
 *     slots point at real C functions (Destroy/Attach/Detach/GetEnv/AttachDaemon).
 *   * Guest objects keep the native_jni.c SREnvObj layout (+0 functions,
 *     +8 SoberJni* owner); no host vtable pointer is fabricated.
 *   * NO guest->Java callback is emitted or triggered.  No completion marker
 *     is set or cleared.  The only guest bytes written are the single JavaVM
 *     pointer slot at 0x7cdec78 (plus the cached-ID seed done via the existing
 *     sober_jni_seed_guest_jni_state() bridge API).
 *   * If the opt-in faithful VM is already installed (SOBER_FAITHFUL_JAVAVM),
 *     this module ADOPTS it instead of clobbering it, so A/B runs stay valid.
 *
 * WIRING (lead owns guest_app_shell.c / CMakeLists.txt)
 * -----------------------------------------------------
 *   1. Add src/jni/javavm_default.c to the sober_jni static library.
 *   2. Call sober_javavm_default_install(j, (void *)base) ONCE, immediately
 *      after sober_jni_init() and BEFORE ANY guest RVA is entered (it is
 *      idempotent and can safely be called alongside the existing seed calls).
 *   3. In diagnostics call sober_javavm_default_verify(base, buf, sizeof buf);
 *      buf reports global / GetEnv / cached-ID status and the return value is
 *      1 only when all three checks pass.
 */
#ifndef SOBER_JAVAVM_DEFAULT_H
#define SOBER_JAVAVM_DEFAULT_H

#include <stddef.h>

#include "native_jni.h"   /* SoberJni, SOBER_JNI_OK / SOBER_JNI_ERR */

#ifdef __cplusplus
extern "C" {
#endif

/* Kill switch.  Install is ON unless this variable is exactly "0". */
#define SOBER_JAVAVM_DEFAULT_ENV "SOBER_JAVAVM_DEFAULT"

/* Guest RVAs (AppImage build 204ad049). */
#define SOBER_JAVAVM_DEFAULT_VM_GLOBAL_RVA     0x7cdec78u
#define SOBER_JAVAVM_DEFAULT_CACHED_CLASS_RVA  0x72a2f48u  /* NativeGLJavaInterface jclass */
#define SOBER_JAVAVM_DEFAULT_CACHED_METHOD_RVA 0x72a2fc0u  /* gameLoadedCallback (J)V */

/* Env var consumed by this module (for the lead's withhold-chain audit). */
const char *sober_javavm_default_env_vars(void);

/* Non-zero when the default install is enabled (kill switch not "0"). */
int sober_javavm_default_enabled(void);

/* Install the genuine two-level VM (default ON; no-op when killed).
 * Returns SOBER_JNI_OK on success or when disabled (no-op), SOBER_JNI_ERR on
 * bad arguments.  Idempotent and safe to call more than once. */
int sober_javavm_default_install(SoberJni *j, void *guest_base);

/* Non-zero only after sober_javavm_default_install actually installed/adopted
 * a VM (i.e. enabled and valid arguments).  0 when killed or before install. */
int sober_javavm_default_is_active(void);

/* The JavaVM pointer installed/adopted into the guest global, or NULL when
 * inactive.  (Cached from the last successful install.) */
void *sober_javavm_default_vm(void);

/* The JNIEnv handed out by the installed VM's GetEnv, or NULL when inactive. */
void *sober_javavm_default_env(void);

/* Raw value of guest global 0x7cdec78 (non-NULL once installed). */
void *sober_javavm_default_global(void *guest_base);

/* Call GetEnv through the installed VM's REAL JNIInvokeInterface_ vtable slot
 * (+0x30) with version 0x10006.  On success returns SOBER_JNI_OK (0) and sets
 * *env_out to a live JNIEnv.  Returns JNI error codes otherwise. */
int sober_javavm_default_getenv(void *guest_base, void **env_out);

/* Non-zero when BOTH cached IDs (0x72a2f48 class, 0x72a2fc0 method) are
 * non-NULL in the guest image. */
int sober_javavm_default_cached_ids_live(void *guest_base);

/* Combined verification: returns 1 iff the global is non-NULL AND GetEnv
 * returns a live env AND both cached IDs are live; 0 otherwise.  Always fills
 * buf (when given) with a one-line human-readable report. */
int sober_javavm_default_verify(void *guest_base, char *buf, size_t cap);

/* One-line description (same report as verify(), independent of pass/fail). */
const char *sober_javavm_default_describe(void *guest_base, char *buf, size_t cap);

/* Counters for the lead's A/B report (no callbacks, no markers). */
void sober_javavm_default_stats(char *buf, size_t cap);

#ifdef __cplusplus
}
#endif
#endif /* SOBER_JAVAVM_DEFAULT_H */
