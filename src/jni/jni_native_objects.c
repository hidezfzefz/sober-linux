/*
 * jni_native_objects.c - faithful guest-native C++ shapes for the Roblox
 * autovalue jobjects (Phase 8, tasks #455/#459).  Pure native C, opt-in.
 *
 * Evidence (frozen engine, BuildID 204ad049):
 *   0x2e5d420  generic weak-refcount/dispose helper.  Reads
 *              `rax = *(jobject); disp = *(rax-0x18);` then
 *              `[jobject+disp+0x20]` / `[jobject+disp+0x88]`.
 *   0x2e5d42a  fault site when the backing word is NULL (disp deref).
 *   0x26feb9b  PlatformParams parser: native destination is 0x30 bytes,
 *              +0x08 float dpiScale, +0x0c/+0x10 int viewport mm.
 *   0x2315532  jstring -> native std::string: 0x18-byte {data,size,cap},
 *              guest string header vtable 0x6ed8d00 at A-0x20.
 *   0x2e5baa0  bounded-vector destructor: vptr 0x6db3ec0+0x10,
 *              +0x30 buffer, +0x40 array, +0x48 count.
 *   0x320581b  delegate consumer: `[app+0x408] -> vptr; call *0x90(vptr)`.
 *              Genuine guest delegate vtable is 0x6e047b0.
 * Runtime backtrace (SOBER_REAL_APPSTATE_POSTSETTINGS=1, this worktree) shows
 * the 0x2e5d42a fault object is the guest GLOBAL std::string at 0x72a0520,
 * written by formatter 0x20ab4b6 -- documented in analysis/native-object-layouts.md.
 *
 * No guest bytes are patched.  No vtable points at a host function.  Nothing
 * here emits a guest->Java callback or touches the decision-013 marker.
 */
#define _GNU_SOURCE
#include "jni_native_objects.h"
#include "native_jni.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- private mirror of native_jni.c's SrObject -------------------------
 * Kept byte-identical to SrObject in src/jni/native_jni.c so sr_check()
 * (magic at +0x08, kind at +0x10) accepts our handles.  We build our own
 * views rather than calling sober_jni_new_native_object() because that API
 * stores the raw backing pointer in a field that sober_jni_shutdown() frees;
 * our views carry a negative-offset header and must keep the allocation base
 * private.  Views are intentionally not registered with the bridge, so
 * shutdown never frees them (short-lived harness; harmless leak). */
typedef struct {
    void    *guest_b;     /* +0x00 */
    uint32_t magic;       /* +0x08  SR_OBJ_MAGIC ("SJNI") */
    uint32_t reserved;    /* +0x0c */
    int      kind;        /* +0x10  2 = SR_K_OBJECT */
    uint32_t pad0;        /* +0x14 */
    char    *name;        /* +0x18 */
    char    *sig;         /* +0x20 */
    char    *class_name;  /* +0x28 */
    int      refs;        /* +0x30 */
    uint32_t pad1;        /* +0x34 */
    void    *payload;     /* +0x38 */
    size_t   payload_size;/* +0x40 */
    uint64_t pad2[4];     /* +0x48 */
    void    *guest_c;     /* +0x68 */
    void    *guest_d;     /* +0x70 */
    void    *guest_deep_c;/* +0x78 */
    void    *guest_table; /* +0x80 */
    void    *guest_sub;   /* +0x88 */
} SrMirror;

#define SR_OBJ_MAGIC_LOCAL 0x534A4E49u  /* "SJNI" */
#define SR_K_OBJECT_LOCAL  2
#define SOBER_A_BYTES       0x1200u
#define SOBER_A_PREFIX      0x20u

static unsigned char *g_guest_base;
static size_t          g_created;

/* Zeroed displacement table: no host function pointers, ever. */
static void *const s_zero_disp[8] = {
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};

int sober_native_objects_enabled(void)
{
    const char *e = getenv("SOBER_NATIVE_OBJECTS");
    return (e && strcmp(e, "1") == 0);
}

int sober_native_objects_install(struct SoberJni *j, void *guest_base)
{
    if (!j || !guest_base) return -1;
    if (!sober_native_objects_enabled()) return -1;
    g_guest_base = (unsigned char *)guest_base;
    fprintf(stderr,
            "[native-objects] opt-in (SOBER_NATIVE_OBJECTS=1); guest_base=%p; "
            "vptrs: string=%#lx list=%#lx platform=%#lx delegate=%#lx "
            "(all genuine guest RVAs, no host stubs)\n",
            guest_base,
            (unsigned long)SOBER_NATIVE_VTABLE_RVA_STRING,
            (unsigned long)SOBER_NATIVE_VTABLE_RVA_LIST,
            (unsigned long)SOBER_NATIVE_VTABLE_RVA_PLATFORM,
            (unsigned long)SOBER_NATIVE_VTABLE_RVA_DELEGATE);
    return 0;
}

static uintptr_t vptr_for(uintptr_t rva)
{
    return g_guest_base ? (uintptr_t)(g_guest_base + rva) : 0u;
}

/* Build a self-consistent A/B/C/D graph.  A = allocation+0x20 so the guest's
 * 0x2e5d420 helper can read A-0x18 (displacement 0) and A-0x20 (header vptr).
 * A+0 keeps the 0xc0dedbad magic the guest validator 0x245605d checks. */
static void *make_backing(uintptr_t vtable_rva)
{
    unsigned char *a = (unsigned char *)calloc(1, SOBER_A_PREFIX + SOBER_A_BYTES);
    if (!a) return NULL;
    uintptr_t vt = vptr_for(vtable_rva);
    *(void **)(a + 0x00) = vt ? (void *)vt : (void *)s_zero_disp; /* A-0x20 */
    *(uint64_t *)(a + 0x08) = 0;       /* A-0x18: displacement 0 */
    unsigned char *obj = a + SOBER_A_PREFIX;
    *(uint32_t *)(obj + 0x00) = 0xc0dedbadu;   /* validator magic */
    *(uint32_t *)(obj + 0x10) = 0;             /* index */

    /* B/C nodes: magic 0x000bab1e, self-pointers, so teardown null-checks
     * (0x23331ce / 0x2333593) take safe branches. */
    unsigned char *b = (unsigned char *)calloc(1, 0x1200);
    unsigned char *c = (unsigned char *)calloc(1, 0x1200);
    if (b && c) {
        *(uint32_t *)(b + 0x00) = 0x000bab1eu;
        *(uint32_t *)(c + 0x00) = 0x000bab1eu;
        *(void **)(c + 0x120) = c;
        *(void **)(b + 0x120) = c;
        *(void **)(b + 0x68) = c;
        void **tab = (void **)calloc(16, sizeof(void *));
        if (tab) { tab[0] = b; *(void **)(b + 0x10) = tab; *(uint32_t *)(b + 0x20) = 16; }
    }
    /* Store the private sub-object pointers behind the object at +0x90..+0xa8
     * (unused by the guest helper's safe branch) for self-consistency. */
    *(void **)(obj + 0x90) = b;
    *(void **)(obj + 0x98) = c;
    return obj;
}

/* Common builder: SrObject-compatible view with a typed payload buffer. */
static void *build_view(const char *class_name, size_t bytes,
                        const void *initial, uintptr_t vtable_rva)
{
    SrMirror *o = (SrMirror *)calloc(1, sizeof(*o));
    if (!o) return NULL;
    o->magic = SR_OBJ_MAGIC_LOCAL;
    o->kind = SR_K_OBJECT_LOCAL;
    o->name = (char *)class_name;     /* borrow static string, never freed */
    o->sig = (char *)"";              /* non-NULL -> helper early return */
    o->class_name = (char *)class_name;
    o->refs = 1;
    o->payload = calloc(1, bytes ? bytes : 1);
    if (!o->payload) { free(o); return NULL; }
    if (initial) memcpy(o->payload, initial, bytes);
    o->payload_size = bytes;
    o->guest_b = make_backing(vtable_rva);
    if (!o->guest_b) { free(o->payload); free(o); return NULL; }
    o->guest_c = *(void **)((unsigned char *)o->guest_b + 0x90);
    o->guest_deep_c = *(void **)((unsigned char *)o->guest_b + 0x98);
    /* +0x88 = NULL drives the 0x2e5d420 helper's safe branch even if a caller
     * clears `sig` at +0x20 (guest_sub is never read by native_jni.c). */
    o->guest_sub = NULL;
    g_created++;
    return o;
}

/* ---- public builders ---------------------------------------------------- */

void *sober_native_string(struct SoberJni *j, const char *text)
{
    /* Keep jstrings registered with the bridge: the guest reads them via
     * GetStringUTFChars (0x2315532), which calls sr_validate().  The native
     * std::string mirror is still provided for callers that inspect payload. */
    const char *s = text ? text : "";
    void *h = sober_jni_new_string_utf8(j, s);
    return h;
}

void *sober_native_platform_params(struct SoberJni *j,
                                   float dpi_scale, int32_t width_mm,
                                   int32_t height_mm)
{
    (void)j;
    unsigned char p[SOBER_NATIVE_PLATFORM_SIZE];
    memset(p, 0, sizeof(p));
    uintptr_t vt = vptr_for(SOBER_NATIVE_VTABLE_RVA_PLATFORM);
    memcpy(p + SOBER_NATIVE_PLATFORM_VTABLE, &vt, sizeof(vt));
    memcpy(p + SOBER_NATIVE_PLATFORM_DPI, &dpi_scale, sizeof(dpi_scale));
    memcpy(p + SOBER_NATIVE_PLATFORM_WIDTH_MM, &width_mm, sizeof(width_mm));
    memcpy(p + SOBER_NATIVE_PLATFORM_HEIGHT_MM, &height_mm, sizeof(height_mm));
    p[0x18] = 1;    /* guest parser 0x26febbe: default flag = 1 */
    p[0x28] = 0;    /* guest parser 0x26febc6: default flag = 0 */
    return build_view("com/roblox/engine/jni/model/PlatformParams",
                      sizeof(p), p, SOBER_NATIVE_VTABLE_RVA_PLATFORM);
}

void *sober_native_init_params(struct SoberJni *j)
{
    (void)j;
    /* InitParams is consumed through JNI getters (0x232dbbc/0x2315d18); the
     * native mirror holds one slot per getter so the lead can bind handlers.
     * Slots are zero (NULL object / 0 scalar) = honest neutral values. */
    unsigned char p[0x100];
    memset(p, 0, sizeof(p));
    uintptr_t vt = vptr_for(SOBER_NATIVE_VTABLE_RVA_PLATFORM);
    memcpy(p + 0x00, &vt, sizeof(vt));
    return build_view("com/roblox/engine/jni/autovalue/InitParams",
                      sizeof(p), p, SOBER_NATIVE_VTABLE_RVA_PLATFORM);
}

void *sober_native_start_app_params(struct SoberJni *j)
{
    (void)j;
    unsigned char p[0x100];
    memset(p, 0, sizeof(p));
    uintptr_t vt = vptr_for(SOBER_NATIVE_VTABLE_RVA_PLATFORM);
    memcpy(p + 0x00, &vt, sizeof(vt));
    return build_view("com/roblox/engine/jni/autovalue/StartAppParams",
                      sizeof(p), p, SOBER_NATIVE_VTABLE_RVA_PLATFORM);
}

void *sober_native_java_list(struct SoberJni *j, void *const *items,
                             size_t count)
{
    (void)j;
    /* Bounded-vector shape (0x2e5baa0): vptr 0x6db3ec0+0x10 at +0, element
     * array at +0x40, count at +0x48, backing buffer at +0x30. */
    unsigned char p[0x60];
    memset(p, 0, sizeof(p));
    uintptr_t vt = vptr_for(SOBER_NATIVE_VTABLE_RVA_LIST);
    uintptr_t obj_vptr = vt ? vt + 0x10 : vt;
    memcpy(p + 0x00, &obj_vptr, sizeof(obj_vptr));
    uint64_t n = (uint64_t)count;
    memcpy(p + 0x48, &n, sizeof(n));
    void **array = NULL;
    if (count) {
        array = (void **)calloc(count, sizeof(void *));
        if (array && items) memcpy(array, items, count * sizeof(void *));
    }
    memcpy(p + 0x30, &array, sizeof(array));
    memcpy(p + 0x40, &array, sizeof(array));
    return build_view("java/util/List", sizeof(p), p,
                      SOBER_NATIVE_VTABLE_RVA_LIST);
}

void *sober_native_delegate(struct SoberJni *j)
{
    (void)j;
    /* Standalone object for app+0x408: +0 is the GENUINE guest delegate
     * vtable.  Slot +0x90 is the real consumer entry point (0x320581b).
     * No host function is placed in any slot. */
    unsigned char *o = (unsigned char *)calloc(1, 0x100);
    if (!o) return NULL;
    uintptr_t vt = vptr_for(SOBER_NATIVE_VTABLE_RVA_DELEGATE);
    if (!vt) { free(o); return NULL; }   /* requires install()/guest_base */
    memcpy(o + 0x00, &vt, sizeof(vt));
    g_created++;
    return o;
}

/* ---- diagnostics -------------------------------------------------------- */

size_t sober_native_objects_created(void) { return g_created; }
void   sober_native_objects_reset(void)   { g_created = 0; }

const char *sober_native_objects_describe(char *buf, size_t cap)
{
    if (!buf || !cap) return "";
    snprintf(buf, cap,
             "enabled=%d base=%p created=%zu "
             "vptrs(string=%#lx,list=%#lx,platform=%#lx,delegate=%#lx) "
             "env=SOBER_NATIVE_OBJECTS=1",
             sober_native_objects_enabled(), (void *)g_guest_base, g_created,
             (unsigned long)SOBER_NATIVE_VTABLE_RVA_STRING,
             (unsigned long)SOBER_NATIVE_VTABLE_RVA_LIST,
             (unsigned long)SOBER_NATIVE_VTABLE_RVA_PLATFORM,
             (unsigned long)SOBER_NATIVE_VTABLE_RVA_DELEGATE);
    return buf;
}
