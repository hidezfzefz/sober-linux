/*
 * jni_arrays.c - faithful Java array objects + JNI array functions.
 *
 * Worker B3 (logical Team B).  NEW FILE ONLY; no shared src file, no CMake,
 * no guest bytes are modified.  Compile with -Wall -Wextra -Werror.
 *
 * EVIDENCE / RECOVERED LAYOUT
 * ---------------------------
 * Engine: /tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so
 *   BuildID 204ad049599c96afb75b8a21d5051063e821d6c9,
 *   SHA-256 3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335
 *   (raw bytes verified with python/seek, not just objdump text):
 *
 *   RVA 0x2465210  export Java_..._FlagJniInterface_nativeInitializeNativeFlags
 *                  signature ([Ljava/lang/String;)L...NativeFlagsInitResult;
 *   RVA 0x2465242  48 8b 07        mov    (%rdi),%rax   <- the reported fault site
 *                                  (rdi = JNIEnv* returned by helper 0x227b9e2)
 *   RVA 0x24652d8  ... GetArrayLength(array) via JNIEnv vtable:
 *   RVA 0x24652e3  ff 90 58 05 00 00   call   *0x558(%rax)   (fn[167])
 *   RVA 0x2465317  ... GetObjectArrayElement(array, i) via JNIEnv vtable:
 *   RVA 0x2465324  ff 90 68 05 00 00   call   *0x568(%rax)   (fn[169])
 *   RVA 0x2465308  test %r12d,%r12d / jle -> len<=0 skips the whole flag loop.
 *
 *   The bridge's SRNativeInterface is { void *reserved[4]; void *fn[231] },
 *   so guest vtable byte offset = 0x20 + 8*slot:
 *       0x558 -> fn[(0x558-0x20)/8] = fn[167] = GetArrayLength
 *       0x568 -> fn[(0x568-0x20)/8] = fn[169] = GetObjectArrayElement
 *   (cross-check: native_jni.c SETFN(*t,167,sr_GetArrayLength) and
 *    SETFN(*t,169,sr_GetObjectArrayElement); the current stubs return 0/NULL.)
 *
 *   Genuine Java array object layout - AOSP art/runtime/mirror/array.h
 *   (android10-release): kFirstElementOffset = 12u; Object header is 8 bytes
 *   (klass_ + monitor_); `int32_t length_` sits at +0x08; data starts at
 *   RoundUp(12, component_size).  For object arrays component_size is a
 *   4-byte compressed HeapReference, i.e. data at +0x0C in ART.  Our bridge
 *   hands out UNCOMPRESSED host pointers, so elements are 8 bytes wide and
 *   start at +0x10; +0x0C is deliberately zero.  length stays at +0x08.
 *
 * REA evidence IDs (guest function dossier, image base 0x100000; the
 * authoritative byte/RVA values above come from the frozen ELF itself):
 *   ev_3d638877b77718ff232b831d60f4d0b46155775b62d7b3dd6a77fe20646b3735
 *   ev_a9eb67f39885340af6455fa7bc4b505a0125a8ad8da80f08417fd500d168ad18
 *
 * No callback is emitted, no decision-013 marker is touched and no guest
 * memory is written.  Default OFF (SOBER_FAITHFUL_JNI_ARRAYS unset).
 */
#define _GNU_SOURCE
#include "jni_arrays.h"
#include "javavm_semantics.h"   /* sober_javavm_is_active / _current_env */

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Local ABI scalars (identical to jni.h). */
typedef int32_t ja_jint;

/* ---- object shape ------------------------------------------------------ */
#define JA_MAGIC        0x4a414252u   /* "JABR" trailer marker */
#define JA_MAX_ARRAYS   256
#define JA_MAX_ELEMENTS 65536
#define JA_TRAILER_SIZE 16u
#define JA_HEADER_SIZE  (SOBER_JNI_ARRAY_ELEMENTS_OFFSET)

typedef struct {
    void          *handle;   /* the jobjectArray pointer handed to the guest */
    unsigned char *base;     /* our own allocation, same value as handle */
    ja_jint        length;
    int            used;
} JaEntry;

static JaEntry        g_arrays[JA_MAX_ARRAYS];
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static int             g_active;
static long            g_created;
static long            g_get_length_calls;
static long            g_get_element_calls;
static long            g_set_element_calls;
static long            g_new_array_calls;

/* ---- registry lookups (never dereference an unverified handle) --------- */
static JaEntry *ja_find(void *handle)
{
    JaEntry *e = NULL;
    if (!handle) return NULL;
    pthread_mutex_lock(&g_lock);
    for (int i = 0; i < JA_MAX_ARRAYS; ++i) {
        if (g_arrays[i].used && g_arrays[i].handle == handle) {
            e = &g_arrays[i];
            break;
        }
    }
    pthread_mutex_unlock(&g_lock);
    return e;
}

static ja_jint ja_length_of(void *handle)
{
    JaEntry *e = ja_find(handle);
    return e ? e->length : 0;
}

static void *ja_element_of(void *handle, ja_jint index)
{
    JaEntry *e = ja_find(handle);
    if (!e || index < 0 || index >= e->length) return NULL;
    void **elements = (void **)(void *)(e->base + SOBER_JNI_ARRAY_ELEMENTS_OFFSET);
    return elements[index];
}

/* The first two fields of SREnvObj (native_jni.c) and FaithfulEnv
 * (javavm_semantics.c) are identical: +0 = functions table, +8 = SoberJni*. */
static SoberJni *ja_owner(void *env)
{
    if (!env) return NULL;
    return *(SoberJni **)((unsigned char *)env + 8);
}

/* ---- faithful JNIEnv array functions ----------------------------------- */
static ja_jint ja_GetArrayLength(void *env, void *array)
{
    (void)env;
    g_get_length_calls++;
    return ja_length_of(array);
}

static void *ja_GetObjectArrayElement(void *env, void *array, ja_jint index)
{
    (void)env;
    g_get_element_calls++;
    return ja_element_of(array, index);
}

static void ja_SetObjectArrayElement(void *env, void *array, ja_jint index,
                                     void *value)
{
    (void)env;
    g_set_element_calls++;
    JaEntry *e = ja_find(array);
    if (!e || index < 0 || index >= e->length) return;
    void **elements = (void **)(void *)(e->base + SOBER_JNI_ARRAY_ELEMENTS_OFFSET);
    elements[index] = value;
}

static void *ja_NewObjectArray(void *env, ja_jint length, void *clazz,
                               void *initial)
{
    SoberJni *j = ja_owner(env);
    g_new_array_calls++;
    (void)clazz;
    void *array = sober_jni_new_java_array(j, "[Ljava/lang/Object;", NULL,
                                           (int)length);
    if (array && initial && length > 0) {
        for (ja_jint i = 0; i < length; ++i)
            ja_SetObjectArrayElement(env, array, i, initial);
    }
    return array;
}

/* ---- JNIEnv table patching --------------------------------------------- */
static void ja_patch_table(void *table)
{
    if (!table) return;
    void **fn = (void **)table;
    /* four reserved pointer slots precede fn[] in SRNativeInterface */
    fn[4u + SOBER_JNI_ENV_FN_GETARRAYLENGTH] =
        (void *)(uintptr_t)&ja_GetArrayLength;
    fn[4u + SOBER_JNI_ENV_FN_NEWOBJECTARRAY] =
        (void *)(uintptr_t)&ja_NewObjectArray;
    fn[4u + SOBER_JNI_ENV_FN_GETOBJECTARRAYELEMENT] =
        (void *)(uintptr_t)&ja_GetObjectArrayElement;
    fn[4u + SOBER_JNI_ENV_FN_SETOBJECTARRAYELEMENT] =
        (void *)(uintptr_t)&ja_SetObjectArrayElement;
}

int sober_jni_arrays_install(SoberJni *j)
{
    if (!getenv(SOBER_FAITHFUL_JNI_ARRAYS_ENV)) {
        fprintf(stderr,
                "[jni-arrays] disabled (%s unset); JNI array semantics "
                "unchanged\n", SOBER_FAITHFUL_JNI_ARRAYS_ENV);
        return SOBER_JNI_OK;              /* default OFF, no-op */
    }
    if (!j) return SOBER_JNI_ERR;

    pthread_mutex_lock(&g_lock);
    if (g_active) { pthread_mutex_unlock(&g_lock); return SOBER_JNI_OK; }

    /* Main bridge env: functions at +0. */
    void *env = sober_jni_env(j);
    void *bridge_table = env ? *(void **)env : NULL;
    ja_patch_table(bridge_table);

    /* Faithful JavaVM keeps its own COPY of the bridge table.  If it is
     * already installed, patch that copy too; if it is installed later its
     * memcpy picks up our patched bridge table automatically. */
    if (sober_javavm_is_active()) {
        void *faithful_env = sober_javavm_current_env(j);
        if (faithful_env && faithful_env != env)
            ja_patch_table(*(void **)faithful_env);
    }

    g_active = 1;
    pthread_mutex_unlock(&g_lock);

    fprintf(stderr,
            "[jni-arrays] INSTALLED (%s=1): bridge table=%p, "
            "GetArrayLength fn[%u] @+0x558, GetObjectArrayElement fn[%u] "
            "@+0x568; no guest bytes touched\n",
            SOBER_FAITHFUL_JNI_ARRAYS_ENV, bridge_table,
            SOBER_JNI_ENV_FN_GETARRAYLENGTH,
            SOBER_JNI_ENV_FN_GETOBJECTARRAYELEMENT);
    return SOBER_JNI_OK;
}

int sober_jni_arrays_is_active(void)
{
    return g_active;
}

/* ---- faithful array construction --------------------------------------- */
void *sober_jni_new_java_array(SoberJni *j, const char *class_name,
                               void *const *elements, int count)
{
    (void)j;                 /* class_name is descriptive only for now */
    (void)class_name;
    if (count < 0 || count > JA_MAX_ELEMENTS) return NULL;

    size_t n = (size_t)count;
    size_t bytes = JA_HEADER_SIZE + n * SOBER_JNI_ARRAY_ELEMENT_SIZE
                   + JA_TRAILER_SIZE;
    unsigned char *base = (unsigned char *)calloc(1, bytes);
    if (!base) return NULL;

    /* ART-shaped header: klass/monitor zero, length at +0x08. */
    *(int32_t *)(void *)(base + SOBER_JNI_ARRAY_LENGTH_OFFSET) = count;
    *(uint32_t *)(void *)(base + SOBER_JNI_ARRAY_FIRST_ELEM_OFFSET) = 0u;
    if (elements && n) {
        void **dest = (void **)(void *)(base + SOBER_JNI_ARRAY_ELEMENTS_OFFSET);
        memcpy(dest, elements, n * sizeof(void *));
    }
    /* Trailer lets a forensic dump identify one of our arrays. */
    uint32_t *trailer = (uint32_t *)(void *)(base + JA_HEADER_SIZE
                          + n * SOBER_JNI_ARRAY_ELEMENT_SIZE);
    trailer[0] = JA_MAGIC;
    trailer[1] = (uint32_t)count;

    pthread_mutex_lock(&g_lock);
    int slot = -1;
    for (int i = 0; i < JA_MAX_ARRAYS; ++i) {
        if (!g_arrays[i].used) { slot = i; break; }
    }
    if (slot >= 0) {
        g_arrays[slot].handle = base;
        g_arrays[slot].base = base;
        g_arrays[slot].length = (ja_jint)count;
        g_arrays[slot].used = 1;
        g_created++;
    }
    pthread_mutex_unlock(&g_lock);

    if (slot < 0) { free(base); return NULL; }
    return base;
}

void *sober_jni_new_string_array(SoberJni *j, const char *const *strings,
                                 int count)
{
    if (!j || count < 0 || count > JA_MAX_ELEMENTS) return NULL;
    if (count == 0) return sober_jni_new_java_array(j, "[Ljava/lang/String;",
                                                    NULL, 0);

    void **elements = (void **)calloc((size_t)count, sizeof(void *));
    if (!elements) return NULL;
    for (int i = 0; i < count; ++i)
        elements[i] = sober_jni_new_string_utf8(j, strings ? strings[i] : "");

    void *array = sober_jni_new_java_array(j, "[Ljava/lang/String;",
                                           elements, count);
    free(elements);
    return array;
}

/* ---- introspection ------------------------------------------------------ */
int sober_jni_array_length(void *array)
{
    return (int)ja_length_of(array);
}

void *sober_jni_array_element(void *array, int index)
{
    return ja_element_of(array, (ja_jint)index);
}

int sober_jni_is_java_array(void *array)
{
    return ja_find(array) != NULL;
}

void sober_jni_arrays_stats(char *buf, size_t cap)
{
    if (!buf || cap == 0) return;
    int used = 0;
    pthread_mutex_lock(&g_lock);
    for (int i = 0; i < JA_MAX_ARRAYS; ++i)
        if (g_arrays[i].used) used++;
    pthread_mutex_unlock(&g_lock);
    snprintf(buf, cap,
             "active=%d arrays=%d created=%ld get_len=%ld get_elem=%ld "
             "set_elem=%ld new_array=%ld",
             g_active, used, g_created, g_get_length_calls,
             g_get_element_calls, g_set_element_calls, g_new_array_calls);
}
