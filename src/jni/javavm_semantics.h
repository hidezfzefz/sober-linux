/*
 * javavm_semantics.h - faithful JavaVM / JNIEnv thread semantics for the guest.
 *
 * Task #453 (javavm worker).  NEW FILE ONLY: nothing in src/jni/native_jni.c/.h,
 * src/linux/guest_app_shell.c or the shared CMakeLists.txt is modified here.
 *
 * WHY THIS EXISTS
 * ---------------
 * With SOBER_SEED_JNI_VM=1, sober_jni_seed_guest_onload_globals() points guest
 * BSS global 0x7cdec78 at the bridge's JavaVM (native_jni.c).  That VM's
 * GetEnv/AttachCurrentThread/DetachCurrentThread are trivial stubs:
 *   - GetEnv always returns JNI_OK and a SINGLE process-wide JNIEnv, and
 *   - DetachCurrentThread is a no-op, and
 *   - no per-thread state is tracked at all.
 *
 * The guest does NOT treat that as acceptable.  Its JNIEnv wrapper at 0x227b9e2
 * explicitly tests for JNI_EDETACHED (0xfffffffe == -2) after
 * JNIInvokeInterface_::GetEnv (+0x30) and, only on that path, runs its own
 * thread-attach sequence (0x2ea6440 -> 0x227aecc, which calls
 * JNIInvokeInterface_::AttachCurrentThread (+0x20) with a JavaVMAttachArgs
 * whose name is e.g. "FunctionMarshaller").  Consumers at 0x23388d6 and
 * 0x6c47d3c call DetachCurrentThread (+0x28) when a thread unwinds.  Returning
 * "attached" unconditionally skips that genuine guest init path; the worker
 * chain 0x2a0daf0->0x1e477ec->0x2a13332->0x6c47ccf->0x6caed77->0x6caee3f->
 * 0x2ed3ad3 then faults at 0x2e5d42a on a C++ object (guest global 0x72a0520)
 * whose vtable was never installed.
 *
 * WHAT THIS MODULE PROVIDES
 * -------------------------
 * A self-contained, ABI-faithful JavaVM replica:
 *   * JNIInvokeInterface_ vtable with exactly the guest's slot offsets
 *     (+0x18 Destroy, +0x20 Attach, +0x28 Detach, +0x30 GetEnv,
 *      +0x38 AttachAsDaemon);
 *   * a PER-THREAD JNIEnv (pthread TLS, destroyed on thread exit), whose
 *     function table is a verbatim copy of the bridge's 231-slot
 *     JNINativeInterface_ table with five honest overrides:
 *        Throw / ThrowNew / ExceptionOccurred / ExceptionClear / ExceptionCheck
 *        (JNIEnv +0x720) now consult per-thread pending-exception state, and
 *        GetJavaVM returns THIS VM rather than the bridge's simplistic one;
 *   * balanced Attach/Detach semantics: GetEnv returns JNI_EDETACHED (-2) for
 *     a thread that never attached (so the guest's real attach path runs),
 *     AttachCurrentThread is idempotent per thread and returns JNI_OK, and
 *     DetachCurrentThread returns JNI_OK / JNI_EDETACHED appropriately.
 *
 * The JNIEnv objects are LAYOUT-COMPATIBLE with native_jni.c's SREnvObj
 * (functions at +0, SoberJni* owner at +8) so every non-overridden bridge
 * entry point keeps working unchanged.  No guest memory is touched except
 * the single JavaVM pointer slot at guest RVA 0x7cdec78.  No callback is
 * fabricated, and no completion marker is set or cleared by this module.
 *
 * Opt-in: all install functions are NO-OPS unless SOBER_FAITHFUL_JAVAVM is
 * set in the environment (default OFF).  The env var name is exported as
 * SOBER_FAITHFUL_JAVAVM_ENV below so the harness can check/withhold markers.
 */
#ifndef SOBER_JAVAVM_SEMANTICS_H
#define SOBER_JAVAVM_SEMANTICS_H

#include <stddef.h>

#include "native_jni.h"   /* SoberJni, SOBER_JNI_OK / SOBER_JNI_ERR */

#ifdef __cplusplus
extern "C" {
#endif

#define SOBER_FAITHFUL_JAVAVM_ENV "SOBER_FAITHFUL_JAVAVM"

/* Install the faithful JavaVM behind SOBER_FAITHFUL_JAVAVM.
 *
 * Call this AFTER sober_jni_seed_guest_onload_globals(j, guest_base) so the
 * faithful VM is the last writer of guest global 0x7cdec78.  With the env var
 * unset this is a no-op that leaves the seed's VM in place and returns
 * SOBER_JNI_OK.  Idempotent: repeated calls do not re-patch or re-attach.
 *
 * The calling (harness/main) thread is attached as "main" so GetEnv on it
 * returns JNI_OK; every other (guest worker) thread starts detached and
 * therefore takes the guest's genuine attach path on first GetEnv.
 */
int sober_javavm_install(SoberJni *j, void *guest_base);

/* The faithful JavaVM pointer, or NULL if not installed. */
void *sober_javavm_vm(SoberJni *j);

/* The calling thread's faithful JNIEnv, or NULL if that thread is detached
 * (or the module is inactive).  Useful for harness diagnostics. */
void *sober_javavm_current_env(SoberJni *j);

/* Non-zero once sober_javavm_install actually installed the VM. */
int sober_javavm_is_active(void);

/* Instrumentation for the lead's A/B report (no callbacks, no markers). */
void sober_javavm_stats(char *buf, size_t cap);

#ifdef __cplusplus
}
#endif
#endif /* SOBER_JAVAVM_SEMANTICS_H */
