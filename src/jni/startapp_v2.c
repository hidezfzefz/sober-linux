/*
 * startapp_v2.c - corrected guest-native StartAppParams (Team B / worker B1).
 *
 * NEW FILE.  See startapp_v2.h for the full evidence and integration contract.
 * In short: register all ten smali-exact getters in one call (with reassert so
 * nothing can shadow surface/vrContext), return the three primitives at their
 * exact JNI widths, and back platformParams with a genuine guest-shaped 0x40
 * native object whose vptr is guest_base + 0x6dbca20.
 *
 * Engine identity (verified before authoring):
 *   SHA256   3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335
 *   BuildID  204ad049599c96afb75b8a21d5051063e821d6c9
 *   objdump  0x26fe98f / 0x26fec51: lea 0x6dbca20(%rip),%rcx
 *            0x26feb9b: parser writes +0x08 float, +0x0c/+0x10 int,
 *                       +0x18=1, +0x28=0
 *   raw bytes 0x535292="dpiScale", 0x2c22da="viewportWidthMm",
 *             0x3897f6="viewportHeightMm"
 *
 * Nothing here patches guest bytes, installs a host vtable, emits a callback
 * or touches the decision-013 marker.  Default OFF; opt-in only.
 */
#define _GNU_SOURCE
#include "startapp_v2.h"
#include "native_jni.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>       /* strcasecmp */

/* ---- private mirror of native_jni.c's SrObject -------------------------
 * Byte-identical so sr_check() (magic +0x08, kind +0x10) accepts our handles.
 * Like jni_native_objects.c we build unregistered views: the bridge must not
 * free our private backing allocation (its A pointer is offset into a larger
 * block).  Short-lived harness; intentional small leak at shutdown. */
typedef struct {
    void    *guest_b;      /* +0x00 */
    uint32_t magic;        /* +0x08 */
    uint32_t reserved;     /* +0x0c */
    int      kind;         /* +0x10 */
    uint32_t pad0;         /* +0x14 */
    char    *name;         /* +0x18 */
    char    *sig;          /* +0x20: non-NULL -> helper 0x2e5d420 early-out */
    char    *class_name;   /* +0x28 */
    int      refs;         /* +0x30 */
    uint32_t pad1;         /* +0x34 */
    void    *payload;      /* +0x38 */
    size_t   payload_size; /* +0x40 */
    uint64_t pad2[4];      /* +0x48 */
    void    *guest_c;      /* +0x68 */
    void    *guest_d;      /* +0x70 */
    void    *guest_deep_c; /* +0x78 */
    void    *guest_table;  /* +0x80 */
    void    *guest_sub;    /* +0x88: NULL -> safe dispose branch */
} Sv2Mirror;

#define SV2_SR_MAGIC    0x534A4E49u  /* "SJNI" */
#define SV2_SR_K_OBJECT 2
#define SV2_A_PREFIX    0x20u
#define SV2_A_BYTES     0x1200u

/* Ten-field native mirror of the StartAppParams autovalue object.  The three
 * primitives keep their EXACT C/JNI types - this is the core of the fix. */
typedef struct {
    void    *vptr;            /* +0x00  string header vtable (guest RVA) */
    void    *surface;         /* +0x08 */
    void    *platform;        /* +0x10 */
    void    *place;           /* +0x18 */
    void    *script;          /* +0x20 */
    int64_t  app_user_id;     /* +0x28  J  (jlong)   */
    uint8_t  is_under_13;     /* +0x30  Z  (jboolean) */
    uint8_t  pad_z[7];        /* +0x31..+0x37 */
    void    *username;        /* +0x38 */
    int32_t  membership_type; /* +0x40  I  (jint)    */
    uint32_t pad_i;           /* +0x44 */
    void    *theme;           /* +0x48 */
    void    *vr_context;      /* +0x50 */
} Sv2Fields;

_Static_assert(offsetof(Sv2Fields, app_user_id)     == 0x28u, "J slot");
_Static_assert(offsetof(Sv2Fields, is_under_13)     == 0x30u, "Z slot");
_Static_assert(offsetof(Sv2Fields, membership_type) == 0x40u, "I slot");

/* ---- module state ------------------------------------------------------ */
typedef struct {
    const char *name;
    const char *sig;
    char        primitive;    /* 'J'/'Z'/'I' or SOBER_STARTAPP_V2_TYPE_OBJECT */
    uint64_t    calls;
    uint64_t    sig_mismatch;
} Sv2Getter;

static Sv2Getter s_getters[SOBER_STARTAPP_V2_GETTER_COUNT] = {
    { SOBER_STARTAPP_V2_GET_SURFACE,    SOBER_STARTAPP_V2_SIG_SURFACE,
      SOBER_STARTAPP_V2_TYPE_OBJECT,   0, 0 },
    { SOBER_STARTAPP_V2_GET_PLATFORM,   SOBER_STARTAPP_V2_SIG_PLATFORM,
      SOBER_STARTAPP_V2_TYPE_OBJECT,   0, 0 },
    { SOBER_STARTAPP_V2_GET_PLACE,      SOBER_STARTAPP_V2_SIG_PLACE,
      SOBER_STARTAPP_V2_TYPE_OBJECT,   0, 0 },
    { SOBER_STARTAPP_V2_GET_SCRIPT,     SOBER_STARTAPP_V2_SIG_SCRIPT,
      SOBER_STARTAPP_V2_TYPE_OBJECT,   0, 0 },
    { SOBER_STARTAPP_V2_GET_USER_ID,    SOBER_STARTAPP_V2_SIG_USER_ID,
      SOBER_STARTAPP_V2_TYPE_LONG,     0, 0 },
    { SOBER_STARTAPP_V2_GET_UNDER13,    SOBER_STARTAPP_V2_SIG_UNDER13,
      SOBER_STARTAPP_V2_TYPE_BOOLEAN,  0, 0 },
    { SOBER_STARTAPP_V2_GET_USERNAME,   SOBER_STARTAPP_V2_SIG_USERNAME,
      SOBER_STARTAPP_V2_TYPE_OBJECT,   0, 0 },
    { SOBER_STARTAPP_V2_GET_MEMBERSHIP, SOBER_STARTAPP_V2_SIG_MEMBERSHIP,
      SOBER_STARTAPP_V2_TYPE_INT,      0, 0 },
    { SOBER_STARTAPP_V2_GET_THEME,      SOBER_STARTAPP_V2_SIG_THEME,
      SOBER_STARTAPP_V2_TYPE_OBJECT,   0, 0 },
    { SOBER_STARTAPP_V2_GET_VR_CONTEXT, SOBER_STARTAPP_V2_SIG_VR_CONTEXT,
      SOBER_STARTAPP_V2_TYPE_OBJECT,   0, 0 },
};

static unsigned char      *g_base;
static SoberStartAppV2Values g_values;
static int                 g_values_valid;
static unsigned            g_built;
static unsigned            g_handles_ok;
static void               *g_h[SOBER_STARTAPP_V2_GETTER_COUNT]; /* cached handles */

/* ---- opt-in / env ------------------------------------------------------ */
int sober_startapp_v2_enabled(void)
{
    const char *a = getenv(SOBER_STARTAPP_V2_ENV);
    const char *b = getenv(SOBER_STARTAPP_V2_ENV_LEGACY);
    return ((a && strcmp(a, "1") == 0) || (b && strcmp(b, "1") == 0));
}

const char *sober_startapp_v2_env_vars(void)
{
    return "SOBER_STARTAPP_V2=1 (enable; legacy alias SOBER_STARTAPP_PARAMS=1); "
           "value overrides: SOBER_STARTAPP_V2_PLACE, _SCRIPT, _USER_ID, "
           "_UNDER13, _USERNAME, _MEMBERSHIP, _THEME, _DPI, _WIDTH_MM, "
           "_HEIGHT_MM";
}

void sober_startapp_v2_defaults(SoberStartAppV2Values *v)
{
    if (!v) return;
    memset(v, 0, sizeof(*v));
    v->surface_class    = NULL;   /* canonical smali names */
    v->platform_class   = NULL;
    v->vr_context_class = NULL;
    /* Java literals from uh/p.b(). */
    v->app_starter_place  = "rbxasset://places/Mobile.rbxl";
    v->app_starter_script = "LuaAppStarterScript";
    /* Decoded default account values (analysis/lua-app-start.md §5). */
    v->app_user_id     = -1;      /* J: logged-out sentinel, dk/c.smali */
    v->is_under_13     = 1;       /* Z: true in the decoded APK */
    v->membership_type = 0;       /* I: default int field */
    v->username        = "";      /* decoded default (empty string) */
    v->selected_theme  = "Light"; /* ml/a.smali default */
    /* PlatformParams parser defaults (0x26feb9b). */
    v->dpi_scale          = 1.0f;
    v->viewport_width_mm  = 0;
    v->viewport_height_mm = 0;
}

static const char *env_or(const char *name, const char *fallback)
{
    const char *e = getenv(name);
    return (e && *e) ? e : fallback;
}

static void sv2_apply_env(SoberStartAppV2Values *v)
{
    v->app_starter_place  = env_or("SOBER_STARTAPP_V2_PLACE",    v->app_starter_place);
    v->app_starter_script = env_or("SOBER_STARTAPP_V2_SCRIPT",   v->app_starter_script);
    v->username           = env_or("SOBER_STARTAPP_V2_USERNAME", v->username);
    v->selected_theme     = env_or("SOBER_STARTAPP_V2_THEME",    v->selected_theme);
    const char *e;
    if ((e = getenv("SOBER_STARTAPP_V2_USER_ID")) && *e)
        v->app_user_id = (int64_t)strtoll(e, NULL, 0);
    if ((e = getenv("SOBER_STARTAPP_V2_UNDER13")) && *e)
        v->is_under_13 = (uint8_t)(strcmp(e, "1") == 0 ||
                                   strcasecmp(e, "true") == 0 ? 1u : 0u);
    if ((e = getenv("SOBER_STARTAPP_V2_MEMBERSHIP")) && *e)
        v->membership_type = (int32_t)strtol(e, NULL, 0);
    if ((e = getenv("SOBER_STARTAPP_V2_DPI")) && *e)
        v->dpi_scale = (float)strtod(e, NULL);
    if ((e = getenv("SOBER_STARTAPP_V2_WIDTH_MM")) && *e)
        v->viewport_width_mm = (int32_t)strtol(e, NULL, 0);
    if ((e = getenv("SOBER_STARTAPP_V2_HEIGHT_MM")) && *e)
        v->viewport_height_mm = (int32_t)strtol(e, NULL, 0);
}

static uintptr_t vptr_for(uintptr_t rva)
{
    return g_base ? (uintptr_t)(uintptr_t)g_base + rva : 0u;
}

static int sv2_index(const char *name)
{
    if (!name) return -1;
    for (int i = 0; i < SOBER_STARTAPP_V2_GETTER_COUNT; ++i)
        if (strcmp(name, s_getters[i].name) == 0) return i;
    return -1;
}

/* ---- guest-shaped backing ---------------------------------------------- *
 * A = allocation+0x20 so A-0x20 holds a GENUINE guest vptr and A-0x18 holds a
 * zero displacement (helper 0x2e5d420).  B/C nodes use the canonical magics
 * so guest teardown null-checks (0x23331ce/0x2333593) take safe branches. */
static void *sv2_make_backing(uintptr_t header_vt, void **out_b, void **out_c)
{
    unsigned char *a = (unsigned char *)calloc(1, SV2_A_PREFIX + SV2_A_BYTES);
    if (!a) return NULL;
    *(void **)(a + 0x00) = (void *)header_vt;  /* A-0x20 */
    *(uint64_t *)(a + 0x08) = 0;               /* A-0x18: displacement 0 */

    unsigned char *obj = a + SV2_A_PREFIX;
    *(uint32_t *)(obj + 0x00) = 0xc0dedbadu;   /* validator 0x245605d */
    *(uint32_t *)(obj + 0x10) = 0;             /* index */

    unsigned char *b = (unsigned char *)calloc(1, SV2_A_BYTES);
    unsigned char *c = (unsigned char *)calloc(1, SV2_A_BYTES);
    if (b && c) {
        *(uint32_t *)(b + 0x00) = 0x000bab1eu;
        *(uint32_t *)(c + 0x00) = 0x000bab1eu;
        *(void **)(c + 0x120) = c;
        *(void **)(b + 0x120) = c;
        *(void **)(b + 0x68)  = c;
        void **tab = (void **)calloc(16, sizeof(void *));
        if (tab) {
            tab[0] = b;
            *(void **)(b + 0x10) = tab;
            *(uint32_t *)(b + 0x20) = 16;
        }
    }
    if (out_b) *out_b = b;
    if (out_c) *out_c = c;
    return obj;
}

static void *sv2_build_view(const char *class_name, size_t bytes,
                            const void *initial, uintptr_t header_vt)
{
    void *b = NULL, *c = NULL;
    Sv2Mirror *o = (Sv2Mirror *)calloc(1, sizeof(*o));
    if (!o) return NULL;
    o->magic      = SV2_SR_MAGIC;
    o->kind       = SV2_SR_K_OBJECT;
    o->name       = (char *)class_name;
    o->sig        = (char *)"";       /* non-NULL: helper early-return */
    o->class_name = (char *)class_name;
    o->refs       = 1;
    o->payload    = calloc(1, bytes ? bytes : 1);
    if (!o->payload) { free(o); return NULL; }
    if (initial && bytes) memcpy(o->payload, initial, bytes);
    o->payload_size = bytes;
    o->guest_b = sv2_make_backing(header_vt, &b, &c);
    if (!o->guest_b) { free(o->payload); free(o); return NULL; }
    o->guest_c      = b;
    o->guest_deep_c = c;
    o->guest_sub    = NULL;           /* safe branch if sig ever cleared */
    g_built++;
    return o;
}

/* 0x40-byte PlatformParams native object exactly as 0x26fe98f/0x26fec51 build
 * it and parser 0x26feb9b fills it.  The vptr is a genuine guest RVA. */
static void sv2_platform_payload(unsigned char *p,
                                 const SoberStartAppV2Values *v)
{
    memset(p, 0, SOBER_STARTAPP_V2_PLATFORM_SIZE);
    uintptr_t vt = vptr_for(SOBER_STARTAPP_V2_VT_PLATFORM);
    memcpy(p + SOBER_STARTAPP_V2_PLATFORM_VPTR, &vt, sizeof(vt));
    memcpy(p + SOBER_STARTAPP_V2_PLATFORM_DPI, &v->dpi_scale,
           sizeof(v->dpi_scale));
    memcpy(p + SOBER_STARTAPP_V2_PLATFORM_WIDTH_MM, &v->viewport_width_mm,
           sizeof(v->viewport_width_mm));
    memcpy(p + SOBER_STARTAPP_V2_PLATFORM_HEIGHT_MM, &v->viewport_height_mm,
           sizeof(v->viewport_height_mm));
    p[SOBER_STARTAPP_V2_PLATFORM_FLAG1] = 1;   /* parser 0x26febbe */
    p[SOBER_STARTAPP_V2_PLATFORM_FLAG2] = 0;   /* parser 0x26febc6 */
}

static void sv2_fields_payload(Sv2Fields *f, const SoberStartAppV2Values *v)
{
    memset(f, 0, sizeof(*f));
    uintptr_t vt = vptr_for(SOBER_STARTAPP_V2_VT_STRING);
    f->vptr            = (void *)vt;
    f->surface         = g_h[0];
    f->platform        = g_h[1];
    f->place           = g_h[2];
    f->script          = g_h[3];
    f->app_user_id     = v->app_user_id;      /* J: 64-bit, no truncation */
    f->is_under_13     = v->is_under_13;      /* Z: uint8_t */
    f->username        = g_h[6];
    f->membership_type = v->membership_type;  /* I: int32_t */
    f->theme           = g_h[8];
    f->vr_context      = g_h[9];
}

/* Build the cached object-valued handles once, outside JNI dispatch. */
static int sv2_ensure_handles(SoberJni *j, void *guest_base,
                              const SoberStartAppV2Values *v)
{
    if (!j || !guest_base || !v) return SOBER_JNI_ERR;
    g_base = (unsigned char *)guest_base;
    g_values = *v;                 /* strings are static literals or env-owned */
    g_values_valid = 1;
    if (g_handles_ok) return SOBER_JNI_OK;  /* idempotent */

    unsigned char platform[SOBER_STARTAPP_V2_PLATFORM_SIZE];
    sv2_platform_payload(platform, &g_values);

    g_h[0] = sober_jni_new_native_object(j,
                 g_values.surface_class ? g_values.surface_class
                                        : "android/view/Surface", 0x10, NULL);
    g_h[1] = sv2_build_view(
                 g_values.platform_class ? g_values.platform_class
                                        : "com/roblox/engine/jni/model/PlatformParams",
                 sizeof(platform), platform,
                 vptr_for(SOBER_STARTAPP_V2_VT_PLATFORM));
    g_h[2] = sober_jni_new_string_utf8(j, g_values.app_starter_place);
    g_h[3] = sober_jni_new_string_utf8(j, g_values.app_starter_script);
    g_h[6] = sober_jni_new_string_utf8(j, g_values.username);
    g_h[8] = sober_jni_new_string_utf8(j, g_values.selected_theme);
    g_h[9] = sober_jni_new_native_object(j,
                 g_values.vr_context_class ? g_values.vr_context_class
                                           : "android/app/Activity", 0x10, NULL);

    for (int i = 0; i < SOBER_STARTAPP_V2_GETTER_COUNT; ++i)
        if (s_getters[i].primitive == SOBER_STARTAPP_V2_TYPE_OBJECT && !g_h[i])
            return SOBER_JNI_ERR;
    g_handles_ok = 1;
    return SOBER_JNI_OK;
}

/* ---- JNI getter dispatch ----------------------------------------------- *
 * The three primitives return intptr_t values that the bridge casts to the
 * exact JNI type: sr_CallLongMethod -> sr_jlong (int64_t),
 * sr_CallBooleanMethod -> sr_jboolean (uint8_t), sr_CallIntMethod -> sr_jint
 * (int32_t).  Doing otherwise (e.g. handing CallLongMethod a jstring pointer)
 * is the defect this module corrects. */
static intptr_t sv2_get(void *env, void *obj, const char *name,
                        const char *sig, void *args, void *user)
{
    (void)env; (void)obj; (void)args;
    int idx = sv2_index(name);
    if (idx < 0) return 0;

    s_getters[idx].calls++;
    if (sig && sig[0] && strcmp(sig, s_getters[idx].sig) != 0)
        s_getters[idx].sig_mismatch++;

    const SoberStartAppV2Values *v =
        user ? (const SoberStartAppV2Values *)user : &g_values;

    switch (idx) {
    /* Object-returning getters: cached, shaped handles. */
    case 0: case 1: case 2: case 3: case 6: case 8: case 9: {
        void *h = g_h[idx];
        if (!h && idx == 1 && g_values_valid) {
            /* Late build if platformParams was requested before make(). */
            unsigned char platform[SOBER_STARTAPP_V2_PLATFORM_SIZE];
            sv2_platform_payload(platform, &g_values);
            h = g_h[idx] = sv2_build_view(
                "com/roblox/engine/jni/model/PlatformParams",
                sizeof(platform), platform,
                vptr_for(SOBER_STARTAPP_V2_VT_PLATFORM));
        }
        return (intptr_t)h;
    }
    case 4: {   /* appUserId()J  -> jlong, full 64 bits */
        int64_t id = v->app_user_id;
        return (intptr_t)id;
    }
    case 5: {   /* isUnder13()Z  -> jboolean, one byte */
        uint8_t b = v->is_under_13 ? 1u : 0u;
        return (intptr_t)b;
    }
    case 7: {   /* membershipType()I -> jint, sign-correct 32 bits */
        int32_t n = v->membership_type;
        return (intptr_t)(int64_t)n;
    }
    default:
        return 0;
    }
}

/* ---- registration ------------------------------------------------------ */
static int sv2_register_impl(SoberJni *j, void *guest_base,
                             const SoberStartAppV2Values *values)
{
    if (!sober_startapp_v2_enabled()) return -2;
    if (!j) return SOBER_JNI_ERR;
    if (sv2_ensure_handles(j, guest_base, values) != SOBER_JNI_OK)
        return SOBER_JNI_ERR;
    for (int i = 0; i < SOBER_STARTAPP_V2_GETTER_COUNT; ++i) {
        if (sober_jni_set_callback_handler(j, s_getters[i].name, sv2_get,
                                           (void *)&g_values) != SOBER_JNI_OK)
            return SOBER_JNI_ERR;
    }
    return SOBER_JNI_OK;
}

int sober_startapp_v2_reassert(SoberJni *j)
{
    if (!sober_startapp_v2_enabled()) return -2;
    if (!j) return SOBER_JNI_ERR;
    if (!g_values_valid) {
        /* Standalone use: seed defaults so the handlers remain well-defined. */
        sober_startapp_v2_defaults(&g_values);
        sv2_apply_env(&g_values);
        g_values_valid = 1;
    }
    for (int i = 0; i < SOBER_STARTAPP_V2_GETTER_COUNT; ++i) {
        if (sober_jni_set_callback_handler(j, s_getters[i].name, sv2_get,
                                           (void *)&g_values) != SOBER_JNI_OK)
            return SOBER_JNI_ERR;
    }
    return SOBER_JNI_OK;
}

int sober_startapp_v2_register(SoberJni *j, void *guest_base,
                               const SoberStartAppV2Values *values)
{
    int rc = sv2_register_impl(j, guest_base, values);
    if (rc == SOBER_JNI_OK)
        fprintf(stderr,
                "[startapp-v2] opt-in; registered all 10 StartAppParams getters "
                "(platform vptr=%#lx from genuine guest RVA %#x; primitives "
                "J/Z/I at exact widths; no host stubs)\n",
                (unsigned long)vptr_for(SOBER_STARTAPP_V2_VT_PLATFORM),
                (unsigned)SOBER_STARTAPP_V2_VT_PLATFORM);
    return rc;
}

void *sober_startapp_v2_build(SoberJni *j, void *guest_base,
                              const SoberStartAppV2Values *values)
{
    if (!sober_startapp_v2_enabled()) return NULL;
    if (sv2_ensure_handles(j, guest_base, values) != SOBER_JNI_OK) return NULL;
    Sv2Fields fields;
    sv2_fields_payload(&fields, &g_values);
    void *o = sv2_build_view("com/roblox/engine/jni/autovalue/StartAppParams",
                             sizeof(fields), &fields,
                             vptr_for(SOBER_STARTAPP_V2_VT_STRING));
    if (o)
        fprintf(stderr,
                "[startapp-v2] built StartAppParams %p (10 getters; place='%s', "
                "script='%s', userId=%lld [J], under13=%u [Z], "
                "username='%s', membership=%d [I], theme='%s')\n",
                o, g_values.app_starter_place, g_values.app_starter_script,
                (long long)g_values.app_user_id,
                (unsigned)g_values.is_under_13, g_values.username,
                (int)g_values.membership_type, g_values.selected_theme);
    return o;
}

void *sober_startapp_v2_make_ex(SoberJni *j, void *guest_base,
                                const SoberStartAppV2Values *values)
{
    if (!sober_startapp_v2_enabled()) return NULL;
    SoberStartAppV2Values v;
    if (values) v = *values;
    else { sober_startapp_v2_defaults(&v); sv2_apply_env(&v); }
    if (sv2_register_impl(j, guest_base, &v) != SOBER_JNI_OK) return NULL;
    return sober_startapp_v2_build(j, guest_base, &v);
}

void *sober_startapp_v2_make(SoberJni *j, void *guest_base)
{
    return sober_startapp_v2_make_ex(j, guest_base, NULL);
}

/* ---- introspection / diagnostics --------------------------------------- */
const char *sober_startapp_v2_getter_sig(const char *name)
{
    int idx = sv2_index(name);
    return idx < 0 ? NULL : s_getters[idx].sig;
}

char sober_startapp_v2_getter_primitive(const char *name)
{
    int idx = sv2_index(name);
    return idx < 0 ? (char)0 : s_getters[idx].primitive;
}

const char *sober_startapp_v2_describe(char *buf, size_t cap)
{
    if (!buf || !cap) return "";
    snprintf(buf, cap,
             "enabled=%d base=%p built=%u getters=%d/%d sig_mismatch=%llu "
             "place='%s' script='%s' userId=%lld[J] under13=%u[Z] "
             "username='%s' membership=%d[I] theme='%s' "
             "platform_vt=%#lx (RVA %#x, genuine; %s; %s)",
             sober_startapp_v2_enabled(), (void *)g_base, g_built,
             (int)sober_startapp_v2_getter_calls(NULL),
             SOBER_STARTAPP_V2_GETTER_COUNT,
             (unsigned long long)(s_getters[4].sig_mismatch +
                                  s_getters[5].sig_mismatch +
                                  s_getters[7].sig_mismatch),
             g_values_valid ? g_values.app_starter_place : "",
             g_values_valid ? g_values.app_starter_script : "",
             (long long)g_values.app_user_id,
             (unsigned)g_values.is_under_13,
             g_values_valid ? g_values.username : "",
             (int)g_values.membership_type,
             g_values_valid ? g_values.selected_theme : "",
             (unsigned long)vptr_for(SOBER_STARTAPP_V2_VT_PLATFORM),
             (unsigned)SOBER_STARTAPP_V2_VT_PLATFORM,
             SOBER_STARTAPP_V2_ENV "=1", "genuine guest RVAs only");
    return buf;
}

uint64_t sober_startapp_v2_getter_calls(const char *name)
{
    if (!name) {
        uint64_t total = 0;
        for (int i = 0; i < SOBER_STARTAPP_V2_GETTER_COUNT; ++i)
            total += s_getters[i].calls;
        return total;
    }
    int idx = sv2_index(name);
    return idx < 0 ? 0u : s_getters[idx].calls;
}
