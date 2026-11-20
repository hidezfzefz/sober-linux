/*
 * startapp_params.c - faithful guest-native StartAppParams object (task #469).
 *
 * Evidence (frozen engine BuildID 204ad049, SHA 3e498d6494...):
 *   APK smali com/roblox/engine/jni/autovalue/StartAppParams.smali and
 *   AutoValue_StartAppParams.smali  -> the ten getter names and signatures.
 *   APK smali uh/f.G() lines 363-434 -> the real Builder call order/values.
 *   APK smali uh/p.b()              -> appStarterPlace / appStarterScript
 *                                       literal values, account values.
 *   guest 0x26fe80f (nativeAppBridgeV2StartAppWithParams):
 *       0x26fe98f/0x26fec51 install the genuine PlatformParams vtable
 *       0x6dbca20 on a 0x40-byte native object; 0x26feb9b (parser) fills
 *       +0x08 float dpiScale, +0x0c/+0x10 int viewport mm, +0x18=1,+0x28=0
 *       from four JNI getters (name strings 0x535292/0x2c22da/0x3897f6).
 *   guest 0x2e5d420 (generic autovalue dispose helper) reads
 *       rax = *(jobject); disp = *(rax-0x18); then [jobject+disp+0x20] and
 *       [jobject+disp+0x88].  A zero displacement and a non-NULL +0x20
 *       (our SrObject.sig) take the safe branches; guest_sub stays NULL.
 *   analysis/native-object-layouts.md documents the negative-offset header.
 *
 * No guest bytes are patched.  No vtable points at a host function: every
 * vptr is guest_base + a genuine guest vtable RVA.  No callback is emitted,
 * no decision-013 marker touched, and everything is opt-in
 * (SOBER_STARTAPP_PARAMS=1).
 */
#define _GNU_SOURCE
#include "startapp_params.h"
#include "native_jni.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>   /* strcasecmp */

/* ---- private mirror of native_jni.c's SrObject -------------------------
 * Kept byte-identical so sr_check() (magic +0x08, kind +0x10) accepts our
 * handles.  Like jni_native_objects.c we build unregistered views: the bridge
 * must not free our private backing allocation (its A pointer is deliberately
 * offset into a larger block).  Short-lived harness; intentional small leak. */
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
} SpMirror;

#define SP_SR_MAGIC   0x534A4E49u  /* "SJNI" */
#define SP_SR_K_OBJECT 2
#define SP_A_PREFIX   0x20u
#define SP_A_BYTES    0x1200u

/* Native field window of our StartAppParams mirror (documented convention,
 * 0x100 bytes; scales/strings are handles, not raw bytes). */
#define SP_FLD_VPTR            0x00u
#define SP_FLD_SURFACE         0x08u
#define SP_FLD_PLATFORM        0x10u
#define SP_FLD_PLACE           0x18u
#define SP_FLD_SCRIPT          0x20u
#define SP_FLD_USER_ID         0x28u
#define SP_FLD_UNDER13         0x30u
#define SP_FLD_USERNAME        0x38u
#define SP_FLD_MEMBERSHIP      0x40u
#define SP_FLD_THEME           0x48u
#define SP_FLD_VR_CONTEXT      0x50u
#define SP_FLD_SIZE            0x100u

/* ---- module state ------------------------------------------------------ */
typedef struct {
    const char *name;
    const char *sig;
    int         object;   /* 1 = returns a jobject, 0 = returns a scalar */
    uint64_t    calls;
} SpGetter;

static SpGetter s_getters[SOBER_STARTAPP_GETTER_COUNT] = {
    { SOBER_STARTAPP_GET_SURFACE,    SOBER_STARTAPP_SIG_SURFACE,    1, 0 },
    { SOBER_STARTAPP_GET_PLATFORM,   SOBER_STARTAPP_SIG_PLATFORM,   1, 0 },
    { SOBER_STARTAPP_GET_PLACE,      SOBER_STARTAPP_SIG_PLACE,      1, 0 },
    { SOBER_STARTAPP_GET_SCRIPT,     SOBER_STARTAPP_SIG_SCRIPT,     1, 0 },
    { SOBER_STARTAPP_GET_USER_ID,    SOBER_STARTAPP_SIG_USER_ID,    0, 0 },
    { SOBER_STARTAPP_GET_UNDER13,    SOBER_STARTAPP_SIG_UNDER13,    0, 0 },
    { SOBER_STARTAPP_GET_USERNAME,   SOBER_STARTAPP_SIG_USERNAME,   1, 0 },
    { SOBER_STARTAPP_GET_MEMBERSHIP, SOBER_STARTAPP_SIG_MEMBERSHIP, 0, 0 },
    { SOBER_STARTAPP_GET_THEME,      SOBER_STARTAPP_SIG_THEME,      1, 0 },
    { SOBER_STARTAPP_GET_VR_CONTEXT, SOBER_STARTAPP_SIG_VR_CONTEXT, 1, 0 },
};

static unsigned char      *g_base;
static SoberStartAppParamsValues g_values;
static int                 g_values_valid;
static unsigned            g_built;
static unsigned            g_sig_mismatch;
static int                 g_handles_ok;
static void               *g_h[SOBER_STARTAPP_GETTER_COUNT]; /* cached handles */

/* ---- helpers ----------------------------------------------------------- */
int sober_startapp_params_enabled(void)
{
    const char *e = getenv("SOBER_STARTAPP_PARAMS");
    return (e && strcmp(e, "1") == 0);
}

const char *sober_startapp_params_env_vars(void)
{
    return "SOBER_STARTAPP_PARAMS=1 (enable); optional value overrides: "
           "SOBER_STARTAPP_PLACE, SOBER_STARTAPP_SCRIPT, SOBER_STARTAPP_USER_ID, "
           "SOBER_STARTAPP_UNDER13, SOBER_STARTAPP_USERNAME, "
           "SOBER_STARTAPP_MEMBERSHIP, SOBER_STARTAPP_THEME, "
           "SOBER_STARTAPP_DPI, SOBER_STARTAPP_WIDTH_MM, SOBER_STARTAPP_HEIGHT_MM";
}

void sober_startapp_params_defaults(SoberStartAppParamsValues *v)
{
    if (!v) return;
    memset(v, 0, sizeof(*v));
    /* Java literals from uh/p.b() (the real StartAppParams builder source). */
    v->app_starter_place  = "rbxasset://places/Mobile.rbxl";
    v->app_starter_script = "LuaAppStarterScript";
    /* Neutral stand-ins for Ldk/c.c() account values. */
    v->app_user_id    = 1;
    v->is_under_13    = 0;
    v->username       = "SoberUser";
    v->membership_type = 0;
    v->selected_theme = "default";
    /* PlatformParams native fields (parser 0x26feb9b defaults). */
    v->dpi_scale          = 1.0f;
    v->viewport_width_mm  = 0;
    v->viewport_height_mm = 0;
    /* NULL = use the canonical Java class names. */
    v->surface_class      = NULL;
    v->platform_class     = NULL;
    v->vr_context_class   = NULL;
}

static const char *env_or(const char *name, const char *fallback)
{
    const char *e = getenv(name);
    return (e && *e) ? e : fallback;
}

static void sp_apply_env(SoberStartAppParamsValues *v)
{
    v->app_starter_place  = env_or("SOBER_STARTAPP_PLACE",  v->app_starter_place);
    v->app_starter_script = env_or("SOBER_STARTAPP_SCRIPT", v->app_starter_script);
    v->username           = env_or("SOBER_STARTAPP_USERNAME", v->username);
    v->selected_theme     = env_or("SOBER_STARTAPP_THEME", v->selected_theme);
    const char *e;
    if ((e = getenv("SOBER_STARTAPP_USER_ID")) && *e)
        v->app_user_id = (int64_t)strtoll(e, NULL, 0);
    if ((e = getenv("SOBER_STARTAPP_UNDER13")) && *e)
        v->is_under_13 = (strcmp(e, "1") == 0 || strcasecmp(e, "true") == 0);
    if ((e = getenv("SOBER_STARTAPP_MEMBERSHIP")) && *e)
        v->membership_type = (int32_t)strtol(e, NULL, 0);
    if ((e = getenv("SOBER_STARTAPP_DPI")) && *e)
        v->dpi_scale = (float)strtod(e, NULL);
    if ((e = getenv("SOBER_STARTAPP_WIDTH_MM")) && *e)
        v->viewport_width_mm = (int32_t)strtol(e, NULL, 0);
    if ((e = getenv("SOBER_STARTAPP_HEIGHT_MM")) && *e)
        v->viewport_height_mm = (int32_t)strtol(e, NULL, 0);
}

static uintptr_t vptr_for(uintptr_t rva)
{
    return g_base ? (uintptr_t)(g_base + rva) : 0u;
}

/* Guest-shaped backing A.  A = allocation+0x20 so A-0x20 holds a GENUINE
 * guest vptr and A-0x18 holds a zero displacement (helper 0x2e5d420). */
static void *sp_make_backing(uintptr_t header_vt, void **out_b, void **out_c)
{
    unsigned char *a = (unsigned char *)calloc(1, SP_A_PREFIX + SP_A_BYTES);
    if (!a) return NULL;
    *(void **)(a + 0x00) = (void *)header_vt;   /* A-0x20 */
    *(uint64_t *)(a + 0x08) = 0;                /* A-0x18: displacement 0 */

    unsigned char *obj = a + SP_A_PREFIX;
    *(uint32_t *)(obj + 0x00) = 0xc0dedbadu;    /* guest validator 0x245605d */
    *(uint32_t *)(obj + 0x10) = 0;              /* index */

    /* B/C nodes with canonical magic and self-pointers: teardown null-checks
     * (0x23331ce / 0x2333593) take safe branches.  All allocations are owned
     * by this short-lived, unregistered view (see note above). */
    unsigned char *b = (unsigned char *)calloc(1, SP_A_BYTES);
    unsigned char *c = (unsigned char *)calloc(1, SP_A_BYTES);
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

static void *sp_build_view(const char *class_name, size_t bytes,
                           const void *initial, uintptr_t header_vt)
{
    void *b = NULL, *c = NULL;
    SpMirror *o = (SpMirror *)calloc(1, sizeof(*o));
    if (!o) return NULL;
    o->magic = SP_SR_MAGIC;
    o->kind = SP_SR_K_OBJECT;
    o->name = (char *)class_name;        /* borrowed static string */
    o->sig = (char *)"";                 /* non-NULL: helper early-return */
    o->class_name = (char *)class_name;
    o->refs = 1;
    o->payload = calloc(1, bytes ? bytes : 1);
    if (!o->payload) { free(o); return NULL; }
    if (initial && bytes) memcpy(o->payload, initial, bytes);
    o->payload_size = bytes;
    o->guest_b = sp_make_backing(header_vt, &b, &c);
    if (!o->guest_b) { free(o->payload); free(o); return NULL; }
    o->guest_c      = b;
    o->guest_deep_c = c;
    o->guest_sub    = NULL;              /* safe branch if sig ever cleared */
    g_built++;
    return o;
}

/* 0x40-byte PlatformParams native object exactly as 0x26fe98f/0x26fec51
 * build it, with the vtable 0x26feb9b's parser fills. */
static void sp_platform_payload(unsigned char *p,
                                const SoberStartAppParamsValues *v)
{
    memset(p, 0, SOBER_STARTAPP_PLATFORM_SIZE);
    uintptr_t vt = vptr_for(SOBER_STARTAPP_VT_RVA_PLATFORM_START);
    memcpy(p + SOBER_STARTAPP_PLATFORM_VPTR, &vt, sizeof(vt));
    memcpy(p + SOBER_STARTAPP_PLATFORM_DPI, &v->dpi_scale,
           sizeof(v->dpi_scale));
    memcpy(p + SOBER_STARTAPP_PLATFORM_WIDTH_MM, &v->viewport_width_mm,
           sizeof(v->viewport_width_mm));
    memcpy(p + SOBER_STARTAPP_PLATFORM_HEIGHT_MM, &v->viewport_height_mm,
           sizeof(v->viewport_height_mm));
    p[SOBER_STARTAPP_PLATFORM_FLAG1] = 1;   /* parser 0x26febbe */
    p[SOBER_STARTAPP_PLATFORM_FLAG2] = 0;   /* parser 0x26febc6 */
}

/* The ten-field native mirror of the StartAppParams autovalue object. */
static void sp_fields_payload(unsigned char *p,
                              const SoberStartAppParamsValues *v)
{
    memset(p, 0, SP_FLD_SIZE);
    uintptr_t vt = vptr_for(SOBER_STARTAPP_VT_RVA_STRING); /* header vtable */
    memcpy(p + SP_FLD_VPTR, &vt, sizeof(vt));
    for (int i = 0; i < SOBER_STARTAPP_GETTER_COUNT; ++i) {
        size_t off = SP_FLD_SURFACE + (size_t)i * 8u;
        if (off + 8u > SP_FLD_SIZE) break;
        if (s_getters[i].object)
            memcpy(p + off, &g_h[i], sizeof(void *));
        else if (i == 4) { /* appUserId()J */
            memcpy(p + off, &v->app_user_id, sizeof(v->app_user_id));
        } else if (i == 5) { /* isUnder13()Z */
            uint64_t b = v->is_under_13 ? 1u : 0u;
            memcpy(p + off, &b, sizeof(b));
        } else if (i == 7) { /* membershipType()I */
            uint64_t n = (uint64_t)(int64_t)v->membership_type;
            memcpy(p + off, &n, sizeof(n));
        }
    }
}

/* Build the cached object-valued handles once, outside JNI dispatch. */
static int sp_ensure_handles(SoberJni *j, void *guest_base,
                             const SoberStartAppParamsValues *v)
{
    if (!j || !guest_base || !v) return SOBER_JNI_ERR;
    g_base = (unsigned char *)guest_base;
    g_values = *v;                 /* strings are static literals or env-owned */
    g_values_valid = 1;
    if (g_handles_ok) return SOBER_JNI_OK;  /* idempotent */

    unsigned char platform[SOBER_STARTAPP_PLATFORM_SIZE];
    sp_platform_payload(platform, &g_values);

    g_h[0] = sober_jni_new_native_object(
        j, g_values.surface_class ? g_values.surface_class : "android/view/Surface",
        0x10, NULL);
    g_h[1] = sp_build_view(
        g_values.platform_class ? g_values.platform_class
                                : "com/roblox/engine/jni/model/PlatformParams",
        sizeof(platform), platform,
        vptr_for(SOBER_STARTAPP_VT_RVA_PLATFORM_START));
    g_h[2] = sober_jni_new_string_utf8(j, g_values.app_starter_place);
    g_h[3] = sober_jni_new_string_utf8(j, g_values.app_starter_script);
    g_h[6] = sober_jni_new_string_utf8(j, g_values.username);
    g_h[8] = sober_jni_new_string_utf8(j, g_values.selected_theme);
    g_h[9] = sober_jni_new_native_object(
        j, g_values.vr_context_class ? g_values.vr_context_class
                                     : "android/app/Activity",
        0x10, NULL);

    for (int i = 0; i < SOBER_STARTAPP_GETTER_COUNT; ++i)
        if (s_getters[i].object && !g_h[i]) return SOBER_JNI_ERR;
    g_handles_ok = 1;
    return SOBER_JNI_OK;
}

/* ---- JNI getter dispatch ----------------------------------------------- */
static intptr_t sp_get(void *env, void *obj, const char *name,
                       const char *sig, void *args, void *user)
{
    (void)env; (void)obj; (void)args;
    int idx = -1;
    for (int i = 0; i < SOBER_STARTAPP_GETTER_COUNT; ++i)
        if (name && !strcmp(name, s_getters[i].name)) { idx = i; break; }
    if (idx < 0) return 0;

    s_getters[idx].calls++;
    if (sig && sig[0] && strcmp(sig, s_getters[idx].sig)) g_sig_mismatch++;

    const SoberStartAppParamsValues *v =
        user ? (const SoberStartAppParamsValues *)user : &g_values;
    if (s_getters[idx].object) {
        void *h = g_h[idx];
        if (!h && g_values_valid) {  /* late build, only if base was installed */
            if (idx == 1) {
                unsigned char platform[SOBER_STARTAPP_PLATFORM_SIZE];
                sp_platform_payload(platform, &g_values);
                h = g_h[idx] = sp_build_view(
                    "com/roblox/engine/jni/model/PlatformParams",
                    sizeof(platform), platform,
                    vptr_for(SOBER_STARTAPP_VT_RVA_PLATFORM_START));
            }
        }
        return (intptr_t)h;
    }
    switch (idx) {
        case 4: return (intptr_t)v->app_user_id;          /* appUserId()J   */
        case 5: return (intptr_t)(v->is_under_13 ? 1 : 0);/* isUnder13()Z   */
        case 7: return (intptr_t)(int64_t)v->membership_type; /* membershipType()I */
        default: return 0;
    }
}

int sober_startapp_params_register(SoberJni *j, void *guest_base,
                                   const SoberStartAppParamsValues *values)
{
    if (!sober_startapp_params_enabled()) return -2;
    if (sp_ensure_handles(j, guest_base, values) != SOBER_JNI_OK)
        return SOBER_JNI_ERR;
    for (int i = 0; i < SOBER_STARTAPP_GETTER_COUNT; ++i) {
        if (sober_jni_set_callback_handler(j, s_getters[i].name, sp_get,
                                           (void *)&g_values) != SOBER_JNI_OK)
            return SOBER_JNI_ERR;
    }
    fprintf(stderr,
            "[startapp-params] opt-in; registered all 10 StartAppParams getters "
            "(platform vptr=%#lx, genuine guest RVA %#x; no host stubs)\n",
            (unsigned long)vptr_for(SOBER_STARTAPP_VT_RVA_PLATFORM_START),
            (unsigned)SOBER_STARTAPP_VT_RVA_PLATFORM_START);
    return SOBER_JNI_OK;
}

void *sober_startapp_params_build(SoberJni *j, void *guest_base,
                                  const SoberStartAppParamsValues *values)
{
    if (!sober_startapp_params_enabled()) return NULL;
    if (sp_ensure_handles(j, guest_base, values) != SOBER_JNI_OK) return NULL;
    unsigned char fields[SP_FLD_SIZE];
    sp_fields_payload(fields, &g_values);
    void *o = sp_build_view("com/roblox/engine/jni/autovalue/StartAppParams",
                            sizeof(fields), fields,
                            vptr_for(SOBER_STARTAPP_VT_RVA_STRING));
    if (o)
        fprintf(stderr,
                "[startapp-params] built StartAppParams %p with %d field slots "
                "(place='%s', script='%s', userId=%lld, under13=%d, "
                "username='%s', membership=%d, theme='%s')\n",
                o, SOBER_STARTAPP_GETTER_COUNT, g_values.app_starter_place,
                g_values.app_starter_script, (long long)g_values.app_user_id,
                g_values.is_under_13, g_values.username,
                g_values.membership_type, g_values.selected_theme);
    return o;
}

void *sober_startapp_params_make_ex(SoberJni *j, void *guest_base,
                                    const SoberStartAppParamsValues *values)
{
    if (!sober_startapp_params_enabled()) return NULL;
    SoberStartAppParamsValues v;
    if (values) v = *values;
    else { sober_startapp_params_defaults(&v); sp_apply_env(&v); }
    if (sober_startapp_params_register(j, guest_base, &v) != SOBER_JNI_OK)
        return NULL;
    return sober_startapp_params_build(j, guest_base, &v);
}

void *sober_startapp_params_make(SoberJni *j, void *guest_base)
{
    return sober_startapp_params_make_ex(j, guest_base, NULL);
}

/* ---- diagnostics ------------------------------------------------------- */
const char *sober_startapp_params_describe(char *buf, size_t cap)
{
    if (!buf || !cap) return "";
    snprintf(buf, cap,
             "enabled=%d base=%p built=%u getters=%d/%d sig_mismatch=%u "
             "place='%s' script='%s' userId=%lld under13=%d username='%s' "
             "membership=%d theme='%s' platform_vt=%#lx "
             "(env %s, %s)",
             sober_startapp_params_enabled(), (void *)g_base, g_built,
             (int)sober_startapp_params_getter_calls(NULL),
             SOBER_STARTAPP_GETTER_COUNT, g_sig_mismatch,
             g_values_valid ? g_values.app_starter_place : "",
             g_values_valid ? g_values.app_starter_script : "",
             (long long)g_values.app_user_id, g_values.is_under_13,
             g_values_valid ? g_values.username : "",
             g_values.membership_type,
             g_values_valid ? g_values.selected_theme : "",
             (unsigned long)vptr_for(SOBER_STARTAPP_VT_RVA_PLATFORM_START),
             "SOBER_STARTAPP_PARAMS=1", "genuine guest RVAs only");
    return buf;
}

uint64_t sober_startapp_params_getter_calls(const char *name)
{
    if (!name) {
        uint64_t total = 0;
        for (int i = 0; i < SOBER_STARTAPP_GETTER_COUNT; ++i)
            total += s_getters[i].calls;
        return total;
    }
    for (int i = 0; i < SOBER_STARTAPP_GETTER_COUNT; ++i)
        if (!strcmp(name, s_getters[i].name)) return s_getters[i].calls;
    return 0;
}
