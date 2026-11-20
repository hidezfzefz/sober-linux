/*
 * jni_arrays.h - faithful Java array objects for the Sober/Roblox guest.
 *
 * Worker B3 (logical Team B).  NEW FILE ONLY: no shared src file, no
 * CMakeLists.txt and no guest bytes are modified by this module.
 *
 * WHY THIS EXISTS
 * ---------------
 * nativeInitializeNativeFlags (guest RVA 0x2465210, signature
 * ([Ljava/lang/String;)Lcom/roblox/client/flags/NativeFlagsInitResult;)
 * is called by the harness with a generic opaque native object standing in
 * for the String[].  The bridge's sr_GetArrayLength / sr_GetObjectArrayElement
 * return 0 / NULL for such a handle, so the guest logs `flagCount = 0` and
 * skips its entire flag-provider loop.  This module supplies:
 *
 *   1. a genuine-shaped Java array object (length + element array), and
 *   2. real GetArrayLength / GetObjectArrayElement JNIEnv implementations that
 *      read that object, installed into the JNIEnv function table opt-in.
 *
 * It emits no callback, sets no completion marker and touches no guest
 * memory, so DECISION 013 stays intact.  The lead must still add the opt-in
 * env var name to the `seeded_state` withhold chain in guest_app_shell.c.
 *
 * Opt-in: everything is inert unless SOBER_FAITHFUL_JNI_ARRAYS=1 is set.
 */
#ifndef SOBER_JNI_ARRAYS_H
#define SOBER_JNI_ARRAYS_H

#include <stddef.h>

#include "native_jni.h"   /* SoberJni, SOBER_JNI_OK / SOBER_JNI_ERR */

#ifdef __cplusplus
extern "C" {
#endif

#define SOBER_FAITHFUL_JNI_ARRAYS_ENV "SOBER_FAITHFUL_JNI_ARRAYS"

/* ---- recovered object layout (host representation of a jobjectArray) ----
 * ART mirror::Array on 64-bit: Object header kludge at +0x00 (klass, monitor),
 * `int32_t length_` at +0x08, first element at +0x0C (4-byte compressed
 * HeapReference).  Our handles are UNCOMPRESSED host pointers, so elements
 * live at +0x10 (8-byte aligned); +0x0C is kept zero.  See jni_arrays.c for
 * the AOSP and guest-disassembly evidence. */
#define SOBER_JNI_ARRAY_LENGTH_OFFSET       0x08u
#define SOBER_JNI_ARRAY_FIRST_ELEM_OFFSET   0x0Cu
#define SOBER_JNI_ARRAY_ELEMENTS_OFFSET     0x10u
#define SOBER_JNI_ARRAY_ELEMENT_SIZE        8u

/* ---- recovered JNIEnv function-table slots (index into fn[231]) ---------
 * The bridge table has four reserved pointers before fn[], so guest byte
 * offset = 0x20 + 8 * slot.  Confirmed against the guest at 0x24652e3
 * (call *0x558(%rax)) and 0x2465324 (call *0x568(%rax)). */
#define SOBER_JNI_ENV_FN_GETARRAYLENGTH         167u  /* vtable +0x558 */
#define SOBER_JNI_ENV_FN_NEWOBJECTARRAY         168u  /* vtable +0x560 */
#define SOBER_JNI_ENV_FN_GETOBJECTARRAYELEMENT  169u  /* vtable +0x568 */
#define SOBER_JNI_ENV_FN_SETOBJECTARRAYELEMENT  170u  /* vtable +0x570 */

/* Install the faithful array functions.  No-op unless the opt-in env var is
 * set.  Call AFTER sober_jni_init() and (if used) AFTER/BEFORE
 * sober_javavm_install() -- both orders are handled.  Idempotent. */
int  sober_jni_arrays_install(SoberJni *j);

/* Non-zero once the install actually patched a JNIEnv table. */
int  sober_jni_arrays_is_active(void);

/* Build a faithful array object.  `elements` may be NULL when count == 0.
 * The returned handle is safe to pass as jobjectArray to guest JNI entries. */
void *sober_jni_new_java_array(SoberJni *j, const char *class_name,
                               void *const *elements, int count);

/* Convenience: [Ljava/lang/String; whose elements are bridge jstrings made
 * from `strings[i]` via sober_jni_new_string_utf8(). */
void *sober_jni_new_string_array(SoberJni *j, const char *const *strings,
                                 int count);

/* Introspection / diagnostics (never dereferences untrusted handles). */
int    sober_jni_array_length(void *array);
void  *sober_jni_array_element(void *array, int index);
int    sober_jni_is_java_array(void *array);
void   sober_jni_arrays_stats(char *buf, size_t cap);

#ifdef __cplusplus
}
#endif
#endif /* SOBER_JNI_ARRAYS_H */
