/*
 * static_object_calls.c - opt-in, faithful replacements for the stubbed JNIEnv
 *                         object-returning static call slots and the typed
 *                         GetStringUTFChars gate (analysis B05, decision 013).
 *
 * Worker B12, logical Team B (wave 2).  Scope: this file and its header ONLY.
 * No guest bytes, vtable, marker or callback are touched or fabricated here.
 * Default OFF: SOBER_STATIC_OBJECT_CALLS=1.  See the header for full evidence
 * and the exact lead wiring.
 */
#define _GNU_SOURCE
#include "static_object_calls.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- ABI self-checks ---------------------------------------------------- *
 * The bridge's table has four reserved pointers before fn[231]; the canonical
 * byte offsets below must match the guest's `call *0xNN(%rax)` displacements. */
_Static_assert(sizeof(void *) == 8u, "x86-64 JNIEnv table required");
_Static_assert((4u + SOBER_SOC_SLOT_CALLSTATICOBJECTMETHODV) * sizeof(void *) ==
               SOBER_SOC_OFF_CALLSTATICOBJECTMETHODV, "fn[111] != 0x398");
_Static_assert((4u + SOBER_SOC_SLOT_CALLSTATICOBJECTMETHODA) * sizeof(void *) ==
               SOBER_SOC_OFF_CALLSTATICOBJECTMETHODA, "fn[112] != 0x3a0");
_Static_assert((4u + SOBER_SOC_SLOT_GETSTRINGUTFCHARS) * sizeof(void *) ==
               SOBER_SOC_OFF_GETSTRINGUTFCHARS, "fn[165] != 0x548");

/* ---- private mirror of native_jni.c's SrObject -------------------------
 * Byte-identical prefix to SrObject (native_jni.c:115-135), as also used by
 * jni_native_objects.c:41-60.  Only the fields needed by the typed string gate
 * are mirrored.  Nothing is written through this view. */
typedef struct {
    void    *guest_b;      /* +0x00  guest-shaped A block (magic 0xc0dedbad) */
    uint32_t magic;        /* +0x08  SR_OBJ_MAGIC */
    uint32_t reserved;     /* +0x0c */
    int      kind;         /* +0x10  SrKind: 2 = OBJECT, 3 = STRING */
    uint32_t pad0;         /* +0x14 */
    char    *name;         /* +0x18  callback payload for SR_K_OBJECT strings */
    char    *sig;          /* +0x20 */
    char    *class_name;   /* +0x28 */
    int      refs;         /* +0x30 */
    uint32_t pad1;         /* +0x34 */
    void    *payload;      /* +0x38 */
    size_t   payload_size; /* +0x40 */
} SocObjectPrefix;

_Static_assert(offsetof(SocObjectPrefix, magic) == 0x08u, "SrObject.magic");
_Static_assert(offsetof(SocObjectPrefix, kind) == 0x10u, "SrObject.kind");
_Static_assert(offsetof(SocObjectPrefix, name) == 0x18u, "SrObject.name");
_Static_assert(offsetof(SocObjectPrefix, class_name) == 0x28u, "SrObject.class_name");
_Static_assert(offsetof(SocObjectPrefix, payload) == 0x38u, "SrObject.payload");

#define SOC_OBJ_MAGIC      0x534A4E49u  /* "SJNI" */
#define SOC_GUEST_A_MAGIC  0xc0dedbadu  /* guest-shaped A block at SrObject+0 */
#define SOC_K_OBJECT       2
#define SOC_K_STRING       3
#define SOC_CSTR_LIMIT     4096u

/* ---- delegate types (captured at install time) --------------------------
 * These are the bridge's own, already-wired JNI-ABI functions:
 *   fn[31] sr_CallObjectMethodV  -> sr_dispatch_call(..., want_object=1)
 *   fn[32] sr_CallObjectMethodA  -> sr_dispatch_call(..., want_object=1)
 *   fn[165] sr_GetStringUTFChars -> original UTF-8 string behaviour
 * Capturing them keeps the replacement free of any dependency on the private
 * SREnvObj / SoberJni layouts at call time. */
typedef void   *(*soc_call_v_fn)(void *env, void *obj, void *method, va_list args);
typedef void   *(*soc_call_a_fn)(void *env, void *obj, void *method, void *args);
typedef const char *(*soc_get_utf_fn)(void *env, void *str,
                                      unsigned char *isCopy);

static soc_call_v_fn s_delegate_object_v;
static soc_call_a_fn s_delegate_object_a;
static soc_get_utf_fn s_delegate_get_utf;
static int s_installed;

static SoberStaticObjectCallsDiag s_diag;

/* ---- opt-in / diagnostics ----------------------------------------------- */
int sober_static_object_calls_enabled(void)
{
    const char *e = getenv(SOBER_STATIC_OBJECT_CALLS_ENV);
    return (e != NULL && strcmp(e, "1") == 0);
}

const char *sober_static_object_calls_env_vars(void)
{
    return SOBER_STATIC_OBJECT_CALLS_ENV "=1 (enable; default OFF; opt-in, "
           "additive, diagnostic only - never evidence for the decision-013 "
           "marker)";
}

int sober_static_object_calls_wired(void)
{
    return s_installed;
}

void sober_static_object_calls_diag(SoberStaticObjectCallsDiag *out)
{
    if (out) *out = s_diag;
}

const char *sober_static_object_calls_describe(char *buf, size_t cap)
{
    if (!buf || cap == 0) return "";
    snprintf(buf, cap,
             "static-object-calls: wired=%d env=%s slots=%u(%#x)/%u(%#x)/"
             "%u(%#x) v=%llu a=%llu utf8-delegated=%llu typed-rescues=%llu",
             s_installed, sober_static_object_calls_enabled() ? "on" : "off",
             SOBER_SOC_SLOT_CALLSTATICOBJECTMETHODV,
             (unsigned)SOBER_SOC_OFF_CALLSTATICOBJECTMETHODV,
             SOBER_SOC_SLOT_CALLSTATICOBJECTMETHODA,
             (unsigned)SOBER_SOC_OFF_CALLSTATICOBJECTMETHODA,
             SOBER_SOC_SLOT_GETSTRINGUTFCHARS,
             (unsigned)SOBER_SOC_OFF_GETSTRINGUTFCHARS,
             (unsigned long long)s_diag.call_static_object_v,
             (unsigned long long)s_diag.call_static_object_a,
             (unsigned long long)s_diag.utf8_delegated,
             (unsigned long long)s_diag.utf8_typed_rescues);
    return buf;
}

/* ---- handle / string helpers ------------------------------------------- */
/* Mirrors native_jni.c:272-279 sr_is_safe_ptr: never dereference a foreign or
 * sentinel-looking handle. */
static int soc_is_safe_ptr(const void *p)
{
    uintptr_t v = (uintptr_t)p;
    if (!p || v < 0x10000u) return 0;
    if (v == 0xabcdef0123456789ULL) return 0;
    if (v > 0x00007fffffffffffULL && v < 0xffff000000000000ULL) return 0;
    return 1;
}

static int soc_is_bounded_cstr(const char *s)
{
    return soc_is_safe_ptr(s) && strnlen(s, SOC_CSTR_LIMIT) < SOC_CSTR_LIMIT;
}

static int soc_is_string_class(const char *cn)
{
    return cn && (!strcmp(cn, "java/lang/String") ||
                  !strcmp(cn, "Ljava/lang/String;"));
}

/* Return the UTF-8 text of a string-shaped SrObject, or NULL when the handle
 * is not ours / not string-shaped.  Two cases are recognised:
 *   - SR_K_OBJECT with class_name "java/lang/String": the callback-returned
 *     strings from sr_default_app_shell_handler (native_jni.c:1055).  The text
 *     lives in `name`, because sr_new_object never allocates `payload` there.
 *   - SR_K_STRING: a genuine shaped jstring.  The original fn[165] stays
 *     authoritative whenever it returns a payload; this path only rescues a
 *     shaped handle the bridge's object registry does not know about (e.g.
 *     jni_strings.c module-local handles), for which it returns "". */
static const char *soc_typed_string_text(void *str)
{
    if (!soc_is_safe_ptr(str)) return NULL;
    const SocObjectPrefix *o = (const SocObjectPrefix *)str;
    if (o->magic != SOC_OBJ_MAGIC) return NULL;
    if (!soc_is_safe_ptr(o->guest_b) ||
        *(const uint32_t *)o->guest_b != SOC_GUEST_A_MAGIC) return NULL;

    if (o->kind != SOC_K_STRING && o->kind != SOC_K_OBJECT) return NULL;
    if (o->kind == SOC_K_OBJECT && !soc_is_string_class(o->class_name))
        return NULL;

    const char *text = (const char *)o->payload;
    if (!soc_is_bounded_cstr(text)) text = NULL;
    if (!text) {
        text = (const char *)o->name;
        if (!soc_is_bounded_cstr(text)) text = NULL;
    }
    /* sr_shaped_or_new (native_jni.c:512) wraps a non-object result with
     * name == class_name; that is not a string value, so reject it for the
     * SR_K_OBJECT case (a genuine SR_K_STRING stores its class name in `name`
     * and the text in `payload`, so the check must not apply there). */
    if (o->kind == SOC_K_OBJECT && text && !strcmp(text, "java/lang/String"))
        return NULL;
    return text;
}

/* ---- installer ---------------------------------------------------------- */
int sober_static_object_calls_install_slots(void **env_fn, unsigned slot_count)
{
    if (!env_fn || slot_count <= SOBER_SOC_SLOT_GETSTRINGUTFCHARS)
        return SOBER_SOC_ERR;
    if (!sober_static_object_calls_enabled()) return SOBER_SOC_DISABLED;

    /* Capture the real delegates BEFORE overwriting any slot.  Reject a
     * previous installation of ours so repeated installs cannot recurse. */
    void *dv = env_fn[SOBER_SOC_SLOT_CALLOBJECTMETHODV];
    void *da = env_fn[SOBER_SOC_SLOT_CALLOBJECTMETHODA];
    void *du = env_fn[SOBER_SOC_SLOT_GETSTRINGUTFCHARS];
    if (!s_installed) {
        s_delegate_object_v = (soc_call_v_fn)(uintptr_t)dv;
        s_delegate_object_a = (soc_call_a_fn)(uintptr_t)da;
        if (du != (void *)(uintptr_t)&sober_static_object_calls_GetStringUTFChars)
            s_delegate_get_utf = (soc_get_utf_fn)(uintptr_t)du;
    }

    env_fn[SOBER_SOC_SLOT_CALLSTATICOBJECTMETHODV] =
        (void *)(uintptr_t)&sober_static_object_calls_CallStaticObjectMethodV;
    env_fn[SOBER_SOC_SLOT_CALLSTATICOBJECTMETHODA] =
        (void *)(uintptr_t)&sober_static_object_calls_CallStaticObjectMethodA;
    env_fn[SOBER_SOC_SLOT_GETSTRINGUTFCHARS] =
        (void *)(uintptr_t)&sober_static_object_calls_GetStringUTFChars;

    s_installed = 1;
    s_diag.installs++;
    return SOBER_SOC_OK;
}

/* ---- JNI-ABI replacements ----------------------------------------------- */
/* Faithful V-form of CallStaticObjectMethod: the bridge's instance-form
 * CallObjectMethodV already dispatches with want_object=1 and returns the same
 * shaped object as sr_CallStaticObjectMethod (native_jni.c:588-595); the
 * dispatcher does not distinguish jclass from jobject (:826-827). */
void *sober_static_object_calls_CallStaticObjectMethodV(void *env, void *clazz,
                                                        void *method,
                                                        va_list args)
{
    s_diag.call_static_object_v++;
    if (!s_delegate_object_v) return NULL;
    return s_delegate_object_v(env, clazz, method, args);
}

void *sober_static_object_calls_CallStaticObjectMethodA(void *env, void *clazz,
                                                        void *method,
                                                        void *args)
{
    s_diag.call_static_object_a++;
    if (!s_delegate_object_a) return NULL;
    return s_delegate_object_a(env, clazz, method, args);
}

/* Typed UTF-8 getter.  Always call the original fn[165] first so its counting,
 * isCopy write and empty-string fallback stay byte-for-byte unchanged; only a
 * callback-returned SR_K_OBJECT string overrides the empty result. */
const char *sober_static_object_calls_GetStringUTFChars(void *env, void *str,
                                                        unsigned char *isCopy)
{
    const char *original = "";
    if (s_delegate_get_utf) {
        original = s_delegate_get_utf(env, str, isCopy);
        s_diag.utf8_delegated++;
    } else if (isCopy) {
        *isCopy = 0;
    }
    if (original && original[0]) return original;

    const char *typed = soc_typed_string_text(str);
    if (typed) {
        s_diag.utf8_typed_rescues++;
        return typed;
    }
    return original ? original : "";
}
