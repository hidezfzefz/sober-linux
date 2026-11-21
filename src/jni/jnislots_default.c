/*
 * jnislots_default.c - DEFAULT-ON consolidation of the remaining JNIEnv
 *                      function-table gaps (see jnislots_default.h for the
 *                      SHA-bound evidence, exact slot indices and lead wiring).
 *
 * Agent T3-2, logical Team 3 (JNI + render).  Scope: this file and its header
 * ONLY.  No shared file, CMakeLists, guest byte, marker or callback is touched.
 *
 * Kill switch: SOBER_JNISLOTS_DEFAULT=0.  Otherwise install() captures the
 * bridge's real delegates once and writes the 18 slots
 *   111,112,114,115,125,126,127,128,129,130,135,136,165,167,168,169,170,172.
 *
 * One delegate table, one counter array, one array registry: the four legacy
 * opt-in modules' separate state blocks are NOT recreated here.
 */
#define _GNU_SOURCE
#include "jnislots_default.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- ABI self-checks: fn index = JNIEnv byte offset / 8 - 4 ------------ */
_Static_assert(sizeof(void *) == 8u, "x86-64 JNIEnv table required");
_Static_assert((4u + SOBER_JNISLOTS_SLOT_OBJ_V) * sizeof(void *) ==
               SOBER_JNISLOTS_OFF_OBJ_V, "fn[111] != 0x398");
_Static_assert((4u + SOBER_JNISLOTS_SLOT_OBJ_A) * sizeof(void *) ==
               SOBER_JNISLOTS_OFF_OBJ_A, "fn[112] != 0x3a0");
_Static_assert((4u + SOBER_JNISLOTS_SLOT_LONG) * sizeof(void *) ==
               SOBER_JNISLOTS_OFF_LONG, "fn[128] != 0x420");
_Static_assert((4u + SOBER_JNISLOTS_SLOT_UTF8) * sizeof(void *) ==
               SOBER_JNISLOTS_OFF_UTF8, "fn[165] != 0x548");
_Static_assert((4u + SOBER_JNISLOTS_SLOT_NEWBYTEARRAY) * sizeof(void *) ==
               SOBER_JNISLOTS_OFF_NEWBYTEARRAY, "fn[172] != 0x580");

/* ---- private mirror of native_jni.c's SrObject prefix ------------------- *
 * Byte-identical for +0..+0x40 (native_jni.c:115-135).  Only the typed string
 * gate reads through it; nothing is written through this view. */
typedef struct {
    void    *guest_b;      /* +0x00  guest-shaped A block (magic 0xc0dedbad) */
    uint32_t magic;        /* +0x08  SR_OBJ_MAGIC "SJNI" */
    uint32_t reserved;     /* +0x0c */
    int      kind;         /* +0x10  SrKind: 2 = OBJECT, 3 = STRING */
    uint32_t pad0;         /* +0x14 */
    char    *name;         /* +0x18  object name / string payload fallback */
    char    *sig;          /* +0x20 */
    char    *class_name;   /* +0x28 */
    int      refs;         /* +0x30 */
    uint32_t pad1;         /* +0x34 */
    void    *payload;      /* +0x38  canonical jstring payload */
    size_t   payload_size; /* +0x40 */
} SjObjPrefix;

_Static_assert(offsetof(SjObjPrefix, magic) == 0x08u, "SrObject.magic");
_Static_assert(offsetof(SjObjPrefix, kind) == 0x10u, "SrObject.kind");
_Static_assert(offsetof(SjObjPrefix, name) == 0x18u, "SrObject.name");
_Static_assert(offsetof(SjObjPrefix, class_name) == 0x28u, "SrObject.class_name");
_Static_assert(offsetof(SjObjPrefix, payload) == 0x38u, "SrObject.payload");

#define SJD_OBJ_MAGIC      0x534a4e49u  /* "SJNI" = SR_OBJ_MAGIC */
#define SJD_GUEST_A_MAGIC  0xc0dedbadu
#define SJD_K_OBJECT       2
#define SJD_K_STRING       3
#define SJD_CSTR_LIMIT     4096u

/* ---- delegate table (captured at install time, JNI-ABI identical) ------- */
typedef void   *(*sjd_obj_v_fn)(void *, void *, void *, va_list);
typedef void   *(*sjd_obj_a_fn)(void *, void *, void *, void *);
typedef uint8_t (*sjd_bool_v_fn)(void *, void *, void *, va_list);
typedef uint8_t (*sjd_bool_a_fn)(void *, void *, void *, void *);
typedef int32_t (*sjd_int_v_fn)(void *, void *, void *, va_list);
typedef int32_t (*sjd_int_a_fn)(void *, void *, void *, void *);
typedef int64_t (*sjd_long_v_fn)(void *, void *, void *, va_list);
typedef int64_t (*sjd_long_a_fn)(void *, void *, void *, void *);
typedef double  (*sjd_double_v_fn)(void *, void *, void *, va_list);
typedef double  (*sjd_double_a_fn)(void *, void *, void *, void *);
typedef const char *(*sjd_utf8_fn)(void *, void *, unsigned char *);
typedef int32_t (*sjd_arrlen_fn)(void *, void *);
typedef void *(*sjd_newarr_fn)(void *, int32_t, void *, void *);
typedef void *(*sjd_getelem_fn)(void *, void *, int32_t);
typedef void (*sjd_setelem_fn)(void *, void *, int32_t, void *);

enum {
    D_OBJ_V = 0, D_OBJ_A,
    D_BOOL_V, D_BOOL_A,
    D_INT_V, D_INT_A,
    D_LONG_V, D_LONG_A,
    D_DOUBLE_V, D_DOUBLE_A,
    D_UTF8,
    D_ARR_LEN, D_ARR_NEW, D_ARR_GET, D_ARR_SET,
    D_COUNT
};
static void *s_delegates[D_COUNT];
static int    s_installed;
static int    s_tables_patched;

/* ---- the ONE counter array (no per-domain duplicate state) ------------- */
enum {
    K_INSTALLS = 0,
    K_OBJ_V, K_OBJ_A, K_BOOL_V, K_BOOL_A,
    K_INT, K_INT_V, K_INT_A,
    K_LONG, K_LONG_V, K_LONG_A,
    K_DOUBLE_V, K_DOUBLE_A,
    K_UTF8, K_UTF8_RESCUE,
    K_ARRAY_LEN, K_ARRAY_NEWOBJ, K_ARRAY_GET, K_ARRAY_SET, K_ARRAY_NEWBYTE,
    K_COUNT
};
static const char *const s_keys[K_COUNT] = {
    "installs",
    "objV", "objA", "boolV", "boolA",
    "int", "intV", "intA",
    "long", "longV", "longA",
    "doubleV", "doubleA",
    "utf8", "utf8Rescue",
    "arrayLen", "arrayNewObj", "arrayGet", "arraySet", "arrayNewByte"
};
static uint64_t s_calls[K_COUNT];

#define SJD_BUMP(k) __atomic_fetch_add(&s_calls[(k)], 1u, __ATOMIC_RELAXED)

uint64_t sober_jnislots_default_calls(const char *key)
{
    if (!key) {
        uint64_t total = 0;
        for (unsigned i = 0; i < K_COUNT; ++i)
            total += __atomic_load_n(&s_calls[i], __ATOMIC_RELAXED);
        return total;
    }
    for (unsigned i = 0; i < K_COUNT; ++i)
        if (!strcmp(key, s_keys[i]))
            return __atomic_load_n(&s_calls[i], __ATOMIC_RELAXED);
    return 0;
}

/* ---- the ONE array registry -------------------------------------------- *
 * ART mirror::Array shape: length_ at +0x08; object elements (uncompressed
 * 8-byte host refs) at +0x10, byte elements at +0x0C; a trailer carries an
 * identity magic plus the length.  No guest pointer is ever dereferenced. */
#define SJD_MAX_ARRAYS    256
#define SJD_MAX_ELEMENTS  65536
#define SJD_LEN_OFF       0x08u
#define SJD_OBJ_ELEMS_OFF 0x10u
#define SJD_BYTE_ELEMS_OFF 0x0cu
#define SJD_TRAILER_SIZE  16u
#define SJD_ARRAY_MAGIC   0x534a4441u  /* "SJDA" */

typedef struct {
    void          *handle;
    unsigned char *base;
    int32_t        length;
    uint8_t        elem_size;   /* 8 = object refs, 1 = bytes */
    int            used;
} SjArray;

static SjArray        s_arrays[SJD_MAX_ARRAYS];
static pthread_mutex_t s_arrays_lock = PTHREAD_MUTEX_INITIALIZER;
static int             s_arrays_used;

static SjArray *sj_find_array(void *handle)
{
    SjArray *e = NULL;
    if (!handle) return NULL;
    pthread_mutex_lock(&s_arrays_lock);
    for (int i = 0; i < SJD_MAX_ARRAYS; ++i) {
        if (s_arrays[i].used && s_arrays[i].handle == handle) {
            e = &s_arrays[i];
            break;
        }
    }
    pthread_mutex_unlock(&s_arrays_lock);
    return e;
}

static void *sj_new_array(int32_t length, uint8_t elem_size)
{
    if (length < 0 || length > SJD_MAX_ELEMENTS) return NULL;
    const size_t data_off = (elem_size == 1u) ? SJD_BYTE_ELEMS_OFF
                                              : SJD_OBJ_ELEMS_OFF;
    const size_t data_bytes = (size_t)length * (size_t)elem_size;
    unsigned char *base = (unsigned char *)calloc(1u, data_off + data_bytes
                                                  + SJD_TRAILER_SIZE);
    if (!base) return NULL;

    int32_t le = length;
    uint32_t magic = SJD_ARRAY_MAGIC;
    uint32_t count = (uint32_t)length;
    memcpy(base + SJD_LEN_OFF, &le, sizeof(le));         /* length_ at +0x08 */
    unsigned char *trailer = base + data_off + data_bytes;
    memcpy(trailer, &magic, sizeof(magic));
    memcpy(trailer + sizeof(magic), &count, sizeof(count));

    pthread_mutex_lock(&s_arrays_lock);
    int slot = -1;
    for (int i = 0; i < SJD_MAX_ARRAYS; ++i)
        if (!s_arrays[i].used) { slot = i; break; }
    if (slot >= 0) {
        s_arrays[slot].handle = base;
        s_arrays[slot].base = base;
        s_arrays[slot].length = length;
        s_arrays[slot].elem_size = elem_size;
        s_arrays[slot].used = 1;
        s_arrays_used++;
    }
    pthread_mutex_unlock(&s_arrays_lock);

    if (slot < 0) { free(base); return NULL; }
    return base;
}

/* ---- gate / state ------------------------------------------------------- */
int sober_jnislots_default_enabled(void)
{
    const char *v = getenv(SOBER_JNISLOTS_DEFAULT_ENV);
    /* Default ON; the kill switch is exactly "=0". */
    return !(v && v[0] == '0' && v[1] == '\0');
}

const char *sober_jnislots_default_env_vars(void)
{
    return SOBER_JNISLOTS_DEFAULT_ENV "=0 (kill switch; default ON - installs "
           "faithful JNIEnv slots only, no guest bytes, no callbacks, no "
           "decision-013 marker change)";
}

int sober_jnislots_default_is_installed(void)
{
    return s_installed && s_tables_patched;
}

/* ---- installed entry points (forward declarations for self-detection) -- */
static void  *sj_obj_v(void *, void *, void *, va_list);
static void  *sj_obj_a(void *, void *, void *, void *);
static uint8_t sj_bool_v(void *, void *, void *, va_list);
static uint8_t sj_bool_a(void *, void *, void *, void *);
static int32_t sj_int(void *, void *, void *, ...);
static int32_t sj_int_v(void *, void *, void *, va_list);
static int32_t sj_int_a(void *, void *, void *, void *);
static int64_t sj_long(void *, void *, void *, ...);
static int64_t sj_long_v(void *, void *, void *, va_list);
static int64_t sj_long_a(void *, void *, void *, void *);
static double sj_double_v(void *, void *, void *, va_list);
static double sj_double_a(void *, void *, void *, void *);
static const char *sj_utf8(void *, void *, unsigned char *);
static int32_t sj_array_len(void *, void *);
static void *sj_new_obj_array(void *, int32_t, void *, void *);
static void *sj_get_elem(void *, void *, int32_t);
static void sj_set_elem(void *, void *, int32_t, void *);
static void *sj_new_byte_array(void *, int32_t);

static int sj_is_ours(void *p)
{
    void *const ours[] = {
        (void *)(uintptr_t)&sj_obj_v, (void *)(uintptr_t)&sj_obj_a,
        (void *)(uintptr_t)&sj_bool_v, (void *)(uintptr_t)&sj_bool_a,
        (void *)(uintptr_t)&sj_int, (void *)(uintptr_t)&sj_int_v,
        (void *)(uintptr_t)&sj_int_a, (void *)(uintptr_t)&sj_long,
        (void *)(uintptr_t)&sj_long_v, (void *)(uintptr_t)&sj_long_a,
        (void *)(uintptr_t)&sj_double_v, (void *)(uintptr_t)&sj_double_a,
        (void *)(uintptr_t)&sj_utf8, (void *)(uintptr_t)&sj_array_len,
        (void *)(uintptr_t)&sj_new_obj_array, (void *)(uintptr_t)&sj_get_elem,
        (void *)(uintptr_t)&sj_set_elem, (void *)(uintptr_t)&sj_new_byte_array
    };
    for (unsigned i = 0; i < sizeof(ours) / sizeof(ours[0]); ++i)
        if (ours[i] == p) return 1;
    return 0;
}

/* ---- safe-handle helpers (mirrors native_jni.c:272-279 sr_is_safe_ptr) -- */
static int sj_safe_ptr(const void *p)
{
    uintptr_t v = (uintptr_t)p;
    if (!p || v < 0x10000u) return 0;
    if (v == 0xabcdef0123456789ULL) return 0;
    if (v > 0x00007fffffffffffULL && v < 0xffff000000000000ULL) return 0;
    return 1;
}

static int sj_bounded_cstr(const char *s)
{
    return sj_safe_ptr(s) && strnlen(s, SJD_CSTR_LIMIT) < SJD_CSTR_LIMIT;
}

static int sj_is_string_class(const char *cn)
{
    return cn && (!strcmp(cn, "java/lang/String") ||
                  !strcmp(cn, "Ljava/lang/String;"));
}

/* Return the UTF-8 text of a string-shaped SrObject, or NULL when the handle
 * is not ours / not string-shaped.  Two cases:
 *   - SR_K_OBJECT with class_name "java/lang/String": the callback-returned
 *     strings from sr_default_app_shell_handler (native_jni.c:1055).  The text
 *     lives in `name` because sr_new_object never sets `payload` there.
 *   - SR_K_STRING: a shaped handle the original getter rejects (registry
 *     miss, e.g. jni_strings.c module-local handles); text in payload/name. */
extern int sober_jni_owns_handle(const void *h);

static const char *sj_typed_string_text(void *str)
{
    if (!sj_safe_ptr(str)) return NULL;
    /* Shape checks alone accept jni_probe's deliberate foreign handle. Only
     * objects actually allocated by the bridge may be dereferenced here. */
    if (!sober_jni_owns_handle(str)) return NULL;
    const SjObjPrefix *o = (const SjObjPrefix *)str;
    if (o->magic != SJD_OBJ_MAGIC) return NULL;
    if (!sj_safe_ptr(o->guest_b) ||
        *(const uint32_t *)o->guest_b != SJD_GUEST_A_MAGIC) return NULL;
    if (o->kind != SJD_K_STRING && o->kind != SJD_K_OBJECT) return NULL;
    if (o->kind == SJD_K_OBJECT && !sj_is_string_class(o->class_name))
        return NULL;

    const char *text = (const char *)o->payload;
    if (!sj_bounded_cstr(text)) text = NULL;
    if (!text) {
        text = (const char *)o->name;
        if (!sj_bounded_cstr(text)) text = NULL;
    }
    /* sr_shaped_or_new (native_jni.c:512) wraps a non-object result with
     * name == class_name; that is not a string value. */
    if (o->kind == SJD_K_OBJECT && text && !strcmp(text, "java/lang/String"))
        return NULL;
    return text;
}

/* ---- JNI-ABI replacements ----------------------------------------------- */
static void *sj_obj_v(void *env, void *c, void *m, va_list args)
{
    sjd_obj_v_fn fn = (sjd_obj_v_fn)(uintptr_t)s_delegates[D_OBJ_V];
    SJD_BUMP(K_OBJ_V);
    return fn ? fn(env, c, m, args) : NULL;
}

static void *sj_obj_a(void *env, void *c, void *m, void *args)
{
    sjd_obj_a_fn fn = (sjd_obj_a_fn)(uintptr_t)s_delegates[D_OBJ_A];
    SJD_BUMP(K_OBJ_A);
    return fn ? fn(env, c, m, args) : NULL;
}

static uint8_t sj_bool_v(void *env, void *c, void *m, va_list args)
{
    sjd_bool_v_fn fn = (sjd_bool_v_fn)(uintptr_t)s_delegates[D_BOOL_V];
    SJD_BUMP(K_BOOL_V);
    return fn ? fn(env, c, m, args) : 0u;
}

static uint8_t sj_bool_a(void *env, void *c, void *m, void *args)
{
    sjd_bool_a_fn fn = (sjd_bool_a_fn)(uintptr_t)s_delegates[D_BOOL_A];
    SJD_BUMP(K_BOOL_A);
    return fn ? fn(env, c, m, args) : 0u;
}

static int32_t sj_int(void *env, void *c, void *m, ...)
{
    /* G11: sr_CallStaticIntMethod discarded the dispatcher result.  Forward
     * the varargs to the instance Int V-form, which returns it faithfully. */
    sjd_int_v_fn fn = (sjd_int_v_fn)(uintptr_t)s_delegates[D_INT_V];
    SJD_BUMP(K_INT);
    if (!fn) return 0;
    va_list ap;
    va_start(ap, m);
    int32_t r = fn(env, c, m, ap);
    va_end(ap);
    return r;
}

static int32_t sj_int_v(void *env, void *c, void *m, va_list args)
{
    sjd_int_v_fn fn = (sjd_int_v_fn)(uintptr_t)s_delegates[D_INT_V];
    SJD_BUMP(K_INT_V);
    return fn ? fn(env, c, m, args) : 0;
}

static int32_t sj_int_a(void *env, void *c, void *m, void *args)
{
    sjd_int_a_fn fn = (sjd_int_a_fn)(uintptr_t)s_delegates[D_INT_A];
    SJD_BUMP(K_INT_A);
    return fn ? fn(env, c, m, args) : 0;
}

static int64_t sj_long(void *env, void *c, void *m, ...)
{
    sjd_long_v_fn fn = (sjd_long_v_fn)(uintptr_t)s_delegates[D_LONG_V];
    SJD_BUMP(K_LONG);
    if (!fn) return 0;
    va_list ap;
    va_start(ap, m);
    int64_t r = fn(env, c, m, ap);
    va_end(ap);
    return r;
}

static int64_t sj_long_v(void *env, void *c, void *m, va_list args)
{
    sjd_long_v_fn fn = (sjd_long_v_fn)(uintptr_t)s_delegates[D_LONG_V];
    SJD_BUMP(K_LONG_V);
    return fn ? fn(env, c, m, args) : 0;
}

static int64_t sj_long_a(void *env, void *c, void *m, void *args)
{
    sjd_long_a_fn fn = (sjd_long_a_fn)(uintptr_t)s_delegates[D_LONG_A];
    SJD_BUMP(K_LONG_A);
    return fn ? fn(env, c, m, args) : 0;
}

static double sj_double_v(void *env, void *c, void *m, va_list args)
{
    sjd_double_v_fn fn = (sjd_double_v_fn)(uintptr_t)s_delegates[D_DOUBLE_V];
    SJD_BUMP(K_DOUBLE_V);
    return fn ? fn(env, c, m, args) : 0.0;
}

static double sj_double_a(void *env, void *c, void *m, void *args)
{
    sjd_double_a_fn fn = (sjd_double_a_fn)(uintptr_t)s_delegates[D_DOUBLE_A];
    SJD_BUMP(K_DOUBLE_A);
    return fn ? fn(env, c, m, args) : 0.0;
}

static const char *sj_utf8(void *env, void *str, unsigned char *isCopy)
{
    const char *original = "";
    sjd_utf8_fn fn = (sjd_utf8_fn)(uintptr_t)s_delegates[D_UTF8];
    SJD_BUMP(K_UTF8);
    if (fn) {
        original = fn(env, str, isCopy);
    } else if (isCopy) {
        *isCopy = 0;
    }
    if (original && original[0]) return original;

    const char *typed = sj_typed_string_text(str);
    if (typed) {
        SJD_BUMP(K_UTF8_RESCUE);
        return typed;
    }
    return original ? original : "";
}

static int32_t sj_array_len(void *env, void *array)
{
    SJD_BUMP(K_ARRAY_LEN);
    SjArray *e = sj_find_array(array);
    if (e) return e->length;
    sjd_arrlen_fn fn = (sjd_arrlen_fn)(uintptr_t)s_delegates[D_ARR_LEN];
    return fn ? fn(env, array) : 0;
}

static void *sj_new_obj_array(void *env, int32_t length, void *clazz,
                              void *initial)
{
    SJD_BUMP(K_ARRAY_NEWOBJ);
    void *array = sj_new_array(length, 8u);
    if (!array) {
        sjd_newarr_fn fn = (sjd_newarr_fn)(uintptr_t)s_delegates[D_ARR_NEW];
        return fn ? fn(env, length, clazz, initial) : NULL;
    }
    if (initial && length > 0)
        for (int32_t i = 0; i < length; ++i)
            sj_set_elem(env, array, i, initial);
    return array;
}

static void *sj_get_elem(void *env, void *array, int32_t index)
{
    (void)env;
    SJD_BUMP(K_ARRAY_GET);
    SjArray *e = sj_find_array(array);
    if (!e || e->elem_size != 8u || index < 0 || index >= e->length)
        return NULL;
    void **elements = (void **)(void *)(e->base + SJD_OBJ_ELEMS_OFF);
    return elements[index];
}

static void sj_set_elem(void *env, void *array, int32_t index, void *value)
{
    (void)env;
    SJD_BUMP(K_ARRAY_SET);
    SjArray *e = sj_find_array(array);
    if (!e || e->elem_size != 8u || index < 0 || index >= e->length) return;
    void **elements = (void **)(void *)(e->base + SJD_OBJ_ELEMS_OFF);
    elements[index] = value;
}

static void *sj_new_byte_array(void *env, int32_t length)
{
    (void)env;
    SJD_BUMP(K_ARRAY_NEWBYTE);
    /* fn[172] was a stub; the guest wrapper 0x2a20fd0 tests RAX for NULL. */
    return sj_new_array(length, 1u);
}

/* ---- installer ---------------------------------------------------------- */
int sober_jnislots_default_install(void **env_fn, unsigned slot_count)
{
    if (!sober_jnislots_default_enabled())
        return SOBER_JNISLOTS_DISABLED;
    if (!env_fn || slot_count <= SOBER_JNISLOTS_SLOT_NEWBYTEARRAY)
        return SOBER_JNISLOTS_ERR;

    /* Capture the bridge's real implementations BEFORE overwriting any slot.
     * Never recapture from a table we already patched, and never adopt one of
     * our own entry points as a delegate (that would recurse). */
    if (!s_installed) {
        s_delegates[D_OBJ_V]    = env_fn[31u];
        s_delegates[D_OBJ_A]    = env_fn[32u];
        s_delegates[D_BOOL_V]   = env_fn[34u];
        s_delegates[D_BOOL_A]   = env_fn[35u];
        s_delegates[D_INT_V]    = env_fn[46u];
        s_delegates[D_INT_A]    = env_fn[47u];
        s_delegates[D_LONG_V]   = env_fn[49u];
        s_delegates[D_LONG_A]   = env_fn[50u];
        s_delegates[D_DOUBLE_V] = env_fn[55u];
        s_delegates[D_DOUBLE_A] = env_fn[56u];
        s_delegates[D_UTF8]     = env_fn[SOBER_JNISLOTS_SLOT_UTF8];
        s_delegates[D_ARR_LEN]  = env_fn[SOBER_JNISLOTS_SLOT_GETARRAYLEN];
        s_delegates[D_ARR_NEW]  = env_fn[SOBER_JNISLOTS_SLOT_NEWOBJARRAY];
        s_delegates[D_ARR_GET]  = env_fn[SOBER_JNISLOTS_SLOT_GETARRAYELEM];
        s_delegates[D_ARR_SET]  = env_fn[SOBER_JNISLOTS_SLOT_SETARRAYELEM];
        for (unsigned i = 0; i < D_COUNT; ++i)
            if (sj_is_ours(s_delegates[i])) s_delegates[i] = NULL;
    }

#define SJD_SET(slot, fn) \
    env_fn[(slot)] = (void *)(uintptr_t)&(fn)
    SJD_SET(SOBER_JNISLOTS_SLOT_OBJ_V,        sj_obj_v);
    SJD_SET(SOBER_JNISLOTS_SLOT_OBJ_A,        sj_obj_a);
    SJD_SET(SOBER_JNISLOTS_SLOT_BOOL_V,       sj_bool_v);
    SJD_SET(SOBER_JNISLOTS_SLOT_BOOL_A,       sj_bool_a);
    SJD_SET(SOBER_JNISLOTS_SLOT_INT,          sj_int);
    SJD_SET(SOBER_JNISLOTS_SLOT_INT_V,        sj_int_v);
    SJD_SET(SOBER_JNISLOTS_SLOT_INT_A,        sj_int_a);
    SJD_SET(SOBER_JNISLOTS_SLOT_LONG,         sj_long);
    SJD_SET(SOBER_JNISLOTS_SLOT_LONG_V,       sj_long_v);
    SJD_SET(SOBER_JNISLOTS_SLOT_LONG_A,       sj_long_a);
    SJD_SET(SOBER_JNISLOTS_SLOT_DOUBLE_V,     sj_double_v);
    SJD_SET(SOBER_JNISLOTS_SLOT_DOUBLE_A,     sj_double_a);
    SJD_SET(SOBER_JNISLOTS_SLOT_UTF8,         sj_utf8);
    SJD_SET(SOBER_JNISLOTS_SLOT_GETARRAYLEN,  sj_array_len);
    SJD_SET(SOBER_JNISLOTS_SLOT_NEWOBJARRAY,  sj_new_obj_array);
    SJD_SET(SOBER_JNISLOTS_SLOT_GETARRAYELEM, sj_get_elem);
    SJD_SET(SOBER_JNISLOTS_SLOT_SETARRAYELEM, sj_set_elem);
    SJD_SET(SOBER_JNISLOTS_SLOT_NEWBYTEARRAY, sj_new_byte_array);
#undef SJD_SET

    s_installed = 1;
    s_tables_patched++;
    SJD_BUMP(K_INSTALLS);
    return SOBER_JNISLOTS_OK;
}

int sober_jnislots_default_install_env(void *env, unsigned slot_count)
{
    if (!env) return SOBER_JNISLOTS_ERR;
    void *interface_table = *(void **)env;
    if (!interface_table) return SOBER_JNISLOTS_ERR;
    /* A JNIEnv interface table starts with four reserved words; the array
     * passed to install() begins immediately after them. */
    void **fn = (void **)interface_table + 4u;
    return sober_jnislots_default_install(fn, slot_count);
}

/* ---- verification / diagnostics ---------------------------------------- */
int sober_jnislots_default_verify(void **env_fn, unsigned slot_count,
                                  char *buf, size_t cap)
{
    if (buf && cap) buf[0] = '\0';
    if (!env_fn || slot_count <= SOBER_JNISLOTS_SLOT_NEWBYTEARRAY) {
        if (buf && cap)
            snprintf(buf, cap, "jnislots-default: bad table (count=%u)",
                     slot_count);
        return 0;
    }
    static const struct { unsigned slot; void *fn; } k_expected[] = {
        { SOBER_JNISLOTS_SLOT_OBJ_V,        (void *)(uintptr_t)&sj_obj_v },
        { SOBER_JNISLOTS_SLOT_OBJ_A,        (void *)(uintptr_t)&sj_obj_a },
        { SOBER_JNISLOTS_SLOT_BOOL_V,       (void *)(uintptr_t)&sj_bool_v },
        { SOBER_JNISLOTS_SLOT_BOOL_A,       (void *)(uintptr_t)&sj_bool_a },
        { SOBER_JNISLOTS_SLOT_INT,          (void *)(uintptr_t)&sj_int },
        { SOBER_JNISLOTS_SLOT_INT_V,        (void *)(uintptr_t)&sj_int_v },
        { SOBER_JNISLOTS_SLOT_INT_A,        (void *)(uintptr_t)&sj_int_a },
        { SOBER_JNISLOTS_SLOT_LONG,         (void *)(uintptr_t)&sj_long },
        { SOBER_JNISLOTS_SLOT_LONG_V,       (void *)(uintptr_t)&sj_long_v },
        { SOBER_JNISLOTS_SLOT_LONG_A,       (void *)(uintptr_t)&sj_long_a },
        { SOBER_JNISLOTS_SLOT_DOUBLE_V,     (void *)(uintptr_t)&sj_double_v },
        { SOBER_JNISLOTS_SLOT_DOUBLE_A,     (void *)(uintptr_t)&sj_double_a },
        { SOBER_JNISLOTS_SLOT_UTF8,         (void *)(uintptr_t)&sj_utf8 },
        { SOBER_JNISLOTS_SLOT_GETARRAYLEN,  (void *)(uintptr_t)&sj_array_len },
        { SOBER_JNISLOTS_SLOT_NEWOBJARRAY,  (void *)(uintptr_t)&sj_new_obj_array },
        { SOBER_JNISLOTS_SLOT_GETARRAYELEM, (void *)(uintptr_t)&sj_get_elem },
        { SOBER_JNISLOTS_SLOT_SETARRAYELEM, (void *)(uintptr_t)&sj_set_elem },
        { SOBER_JNISLOTS_SLOT_NEWBYTEARRAY, (void *)(uintptr_t)&sj_new_byte_array }
    };
    unsigned good = 0;
    for (unsigned i = 0; i < sizeof(k_expected) / sizeof(k_expected[0]); ++i)
        if (env_fn[k_expected[i].slot] == k_expected[i].fn) good++;
    const unsigned total = (unsigned)(sizeof(k_expected) / sizeof(k_expected[0]));
    if (buf && cap)
        snprintf(buf, cap,
                 "jnislots-default: enabled=%d installed=%d tables=%d "
                 "slots=%u/%u build32=%#x (%s)",
                 sober_jnislots_default_enabled(), s_installed,
                 s_tables_patched, good, total,
                 SOBER_JNISLOTS_EXPECTED_BUILD_ID32,
                 good == total ? "OK" : "PARTIAL");
    return good == total;
}

const char *sober_jnislots_default_describe(char *buf, size_t cap)
{
    if (!buf || cap == 0) return "";
    snprintf(buf, cap,
             "jnislots-default: enabled=%d installed=%d tables=%d "
             "env=%s=0 disables; calls=(objV=%llu,objA=%llu,boolV=%llu,"
             "boolA=%llu,int=%llu,intV=%llu,intA=%llu,long=%llu,longV=%llu,"
             "longA=%llu,doubleV=%llu,doubleA=%llu,utf8=%llu,rescues=%llu,"
             "arrLen=%llu,newObjArr=%llu,get=%llu,set=%llu,newByte=%llu) "
             "arrays=%d (no guest bytes, no callbacks emitted)",
             sober_jnislots_default_enabled(), s_installed, s_tables_patched,
             SOBER_JNISLOTS_DEFAULT_ENV,
             (unsigned long long)sober_jnislots_default_calls("objV"),
             (unsigned long long)sober_jnislots_default_calls("objA"),
             (unsigned long long)sober_jnislots_default_calls("boolV"),
             (unsigned long long)sober_jnislots_default_calls("boolA"),
             (unsigned long long)sober_jnislots_default_calls("int"),
             (unsigned long long)sober_jnislots_default_calls("intV"),
             (unsigned long long)sober_jnislots_default_calls("intA"),
             (unsigned long long)sober_jnislots_default_calls("long"),
             (unsigned long long)sober_jnislots_default_calls("longV"),
             (unsigned long long)sober_jnislots_default_calls("longA"),
             (unsigned long long)sober_jnislots_default_calls("doubleV"),
             (unsigned long long)sober_jnislots_default_calls("doubleA"),
             (unsigned long long)sober_jnislots_default_calls("utf8"),
             (unsigned long long)sober_jnislots_default_calls("utf8Rescue"),
             (unsigned long long)sober_jnislots_default_calls("arrayLen"),
             (unsigned long long)sober_jnislots_default_calls("arrayNewObj"),
             (unsigned long long)sober_jnislots_default_calls("arrayGet"),
             (unsigned long long)sober_jnislots_default_calls("arraySet"),
             (unsigned long long)sober_jnislots_default_calls("arrayNewByte"),
             s_arrays_used);
    return buf;
}

void sober_jnislots_default_reset(void)
{
    for (unsigned i = 0; i < K_COUNT; ++i)
        __atomic_store_n(&s_calls[i], 0u, __ATOMIC_RELAXED);
    pthread_mutex_lock(&s_arrays_lock);
    for (int i = 0; i < SJD_MAX_ARRAYS; ++i) {
        if (s_arrays[i].used) free(s_arrays[i].base);
        memset(&s_arrays[i], 0, sizeof(s_arrays[i]));
    }
    s_arrays_used = 0;
    pthread_mutex_unlock(&s_arrays_lock);
}
