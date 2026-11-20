/*
 * game_params.c - faithful guest-native StartGameParams object (task #473).
 *
 * Evidence (frozen engine BuildID 204ad049, SHA 3e498d6494...):
 *   APK smali com/roblox/engine/jni/autovalue/AutoValue_StartGameParams.smali
 *       -> 24 instance fields and their exact getter names/signatures.
 *   APK smali com/roblox/engine/jni/autovalue/StartGameParams.smali
 *       -> builder() defaults ("" for optional strings, 0 for ids).
 *   APK smali uh/j0.smali lines 360-595 -> real builder call order/values:
 *       surface = SurfaceView.getHolder().getSurface(),
 *       platformParams = real PlatformParams, deviceParams = null,
 *       placeId/userId/accessCode/... from Luh/m0, account values from Ldk/c,
 *       vrContext only when Lrg/a1.E0() is true.
 *   guest 0x2ebd51a (nativeAppBridgeV2StartGameWithParam) fetches every getter
 *       (name literals 0x2a4f4c/0x54fe84/0x4c332c/0x3deb7b/... -> 0x2346a4)
 *       and converts jstrings with 0x1dc1978 / 0x2315532; platformParams is
 *       parsed by 0x26feb9b with the genuine vtable 0x6dbca20 installed at
 *       0x26fe98f/0x26fec51.
 *   guest 0x2e5d420 (generic autovalue dispose helper) reads
 *       rax = *(jobject); disp = *(rax-0x18); then [jobject+disp+0x20] and
 *       [jobject+disp+0x88].  A zero displacement and a non-NULL +0x20
 *       (our GpMirror.sig) take the safe branches; guest_sub stays NULL.
 *   analysis/native-object-layouts.md documents the negative-offset header.
 *
 * No guest bytes are patched.  No vtable points at a host function: every
 * vptr is guest_base + a genuine guest vtable RVA.  No callback is emitted,
 * no decision-013 marker touched, and everything is opt-in
 * (SOBER_GAME_PARAMS=1).
 */
#define _GNU_SOURCE
#include "game_params.h"
#include "native_jni.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>   /* strcasecmp */

/* ---- private mirror of native_jni.c's SrObject -------------------------
 * Kept byte-identical so sr_check() (magic +0x08, kind +0x10) accepts our
 * handles.  Like jni_native_objects.c / startapp_params.c we build
 * unregistered views: the bridge must not free our private backing
 * allocation (its A pointer is deliberately offset into a larger block).
 * Short-lived harness; intentional small leak. */
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
} GpMirror;

#define GP_SR_MAGIC   0x534A4E49u  /* "SJNI" */
#define GP_SR_K_OBJECT 2
#define GP_A_PREFIX   0x20u
#define GP_A_BYTES    0x1200u

/* Native field window of our StartGameParams mirror (documented convention,
 * 0x100 bytes; one 8-byte slot per genuine getter, scalars stored inline).
 * This is a harness-side mirror: the guest consumes the values via its JNI
 * getters, the window merely keeps every genuine field addressable. */
#define GP_FLD_VPTR    0x00u
#define GP_FLD_SLOT0   0x08u
#define GP_FLD_SIZE    0x100u

/* Getter kinds. */
enum { GP_K_STRING = 0, GP_K_OBJECT = 1, GP_K_SCALAR = 2 };

/* ---- module state ------------------------------------------------------ */
typedef struct {
    const char *name;
    const char *sig;
    int         kind;
    uint64_t    calls;
} GpGetter;

/* Order MUST match the scalar/handle switch cases below. */
static GpGetter s_getters[SOBER_GAME_PARAMS_GETTER_COUNT] = {
    { SOBER_GAME_GET_SURFACE,           SOBER_GAME_SIG_SURFACE,           GP_K_OBJECT, 0 },
    { SOBER_GAME_GET_PLATFORM,          SOBER_GAME_SIG_PLATFORM,          GP_K_OBJECT, 0 },
    { SOBER_GAME_GET_DEVICE,            SOBER_GAME_SIG_DEVICE,            GP_K_OBJECT, 0 },
    { SOBER_GAME_GET_PLACE_ID,          SOBER_GAME_SIG_PLACE_ID,          GP_K_SCALAR, 0 },
    { SOBER_GAME_GET_USER_ID,           SOBER_GAME_SIG_USER_ID,           GP_K_SCALAR, 0 },
    { SOBER_GAME_GET_ACCESS_CODE,       SOBER_GAME_SIG_ACCESS_CODE,       GP_K_STRING, 0 },
    { SOBER_GAME_GET_LINK_CODE,         SOBER_GAME_SIG_LINK_CODE,         GP_K_STRING, 0 },
    { SOBER_GAME_GET_GAME_ID,           SOBER_GAME_SIG_GAME_ID,           GP_K_STRING, 0 },
    { SOBER_GAME_GET_UNDER13,           SOBER_GAME_SIG_UNDER13,           GP_K_SCALAR, 0 },
    { SOBER_GAME_GET_USERNAME,          SOBER_GAME_SIG_USERNAME,          GP_K_STRING, 0 },
    { SOBER_GAME_GET_CONVERSATION_ID,   SOBER_GAME_SIG_CONVERSATION_ID,   GP_K_SCALAR, 0 },
    { SOBER_GAME_GET_RESERVED_CODE,     SOBER_GAME_SIG_RESERVED_CODE,     GP_K_STRING, 0 },
    { SOBER_GAME_GET_CALL_ID,           SOBER_GAME_SIG_CALL_ID,           GP_K_STRING, 0 },
    { SOBER_GAME_GET_JOIN_REQUEST_TYPE, SOBER_GAME_SIG_JOIN_REQUEST_TYPE, GP_K_SCALAR, 0 },
    { SOBER_GAME_GET_REFERRAL_PAGE,     SOBER_GAME_SIG_REFERRAL_PAGE,     GP_K_STRING, 0 },
    { SOBER_GAME_GET_LAUNCH_DATA,       SOBER_GAME_SIG_LAUNCH_DATA,       GP_K_STRING, 0 },
    { SOBER_GAME_GET_JOIN_CONTEXT,      SOBER_GAME_SIG_JOIN_CONTEXT,      GP_K_STRING, 0 },
    { SOBER_GAME_GET_EVENT_ID,          SOBER_GAME_SIG_EVENT_ID,          GP_K_STRING, 0 },
    { SOBER_GAME_GET_JOIN_ATTEMPT_ID,   SOBER_GAME_SIG_JOIN_ATTEMPT_ID,   GP_K_STRING, 0 },
    { SOBER_GAME_GET_JOIN_ATTEMPT_ORIG, SOBER_GAME_SIG_JOIN_ATTEMPT_ORIG, GP_K_STRING, 0 },
    { SOBER_GAME_GET_ISO_CONTEXT,       SOBER_GAME_SIG_ISO_CONTEXT,       GP_K_STRING, 0 },
    { SOBER_GAME_GET_REFERRED_BY,       SOBER_GAME_SIG_REFERRED_BY,       GP_K_SCALAR, 0 },
    { SOBER_GAME_GET_GAME_ID_EXCLUDE,   SOBER_GAME_SIG_GAME_ID_EXCLUDE,   GP_K_STRING, 0 },
    { SOBER_GAME_GET_VR_CONTEXT,        SOBER_GAME_SIG_VR_CONTEXT,        GP_K_OBJECT, 0 },
};

static unsigned char      *g_base;
static SoberGameParamsValues g_values;
static int                 g_values_valid;
static unsigned            g_built;
static unsigned            g_sig_mismatch;
static int                 g_handles_ok;
static void               *g_h[SOBER_GAME_PARAMS_GETTER_COUNT]; /* cached handles */

/* ---- helpers ----------------------------------------------------------- */
int sober_game_params_enabled(void)
{
    const char *e = getenv("SOBER_GAME_PARAMS");
    return (e && strcmp(e, "1") == 0);
}

const char *sober_game_params_env_vars(void)
{
    return "SOBER_GAME_PARAMS=1 (enable); optional value overrides: "
           "SOBER_GAME_PLACE_ID, SOBER_GAME_USER_ID, SOBER_GAME_GAME_ID, "
           "SOBER_GAME_ACCESS_CODE, SOBER_GAME_USERNAME, SOBER_GAME_UNDER13, "
           "SOBER_GAME_CONVERSATION_ID, SOBER_GAME_JOIN_REQUEST_TYPE, "
           "SOBER_GAME_REFERRED_BY, SOBER_GAME_DPI, SOBER_GAME_WIDTH_MM, "
           "SOBER_GAME_HEIGHT_MM";
}

void sober_game_params_defaults(SoberGameParamsValues *v)
{
    if (!v) return;
    memset(v, 0, sizeof(*v));
    /* StartGameParams.builder() literals (uh/j0.C caller + builder()). */
    v->access_code          = "";
    v->link_code            = "";
    v->game_id              = "123456789";   /* neutral stand-in */
    v->username             = "SoberUser";   /* Ldk/c.c() stand-in */
    v->reserved_server_code = "";
    v->call_id              = "";
    v->referral_page        = "";
    v->launch_data          = "";
    v->game_join_context    = "";
    v->event_id             = "";
    v->join_attempt_id      = "";
    v->join_attempt_origin  = "";
    v->iso_context          = "";
    v->game_id_to_exclude   = "";
    v->place_id             = 123456789;     /* neutral stand-in */
    v->user_id              = 1;
    v->is_under_13          = 0;
    v->conversation_id      = 0;
    v->join_request_type    = 0;
    v->referred_by_player_id = 0;
    /* PlatformParams native fields (parser 0x26feb9b defaults). */
    v->dpi_scale          = 1.0f;
    v->viewport_width_mm  = 0;
    v->viewport_height_mm = 0;
    /* NULL = use the canonical Java class names. */
    v->surface_class      = NULL;
    v->platform_class     = NULL;
    v->device_class       = NULL;
    v->vr_context_class   = NULL;
}

static const char *env_or(const char *name, const char *fallback)
{
    const char *e = getenv(name);
    return (e && *e) ? e : fallback;
}

static void gp_apply_env(SoberGameParamsValues *v)
{
    v->game_id   = env_or("SOBER_GAME_GAME_ID",   v->game_id);
    v->access_code = env_or("SOBER_GAME_ACCESS_CODE", v->access_code);
    v->username  = env_or("SOBER_GAME_USERNAME",  v->username);
    const char *e;
    if ((e = getenv("SOBER_GAME_PLACE_ID")) && *e)
        v->place_id = (int64_t)strtoll(e, NULL, 0);
    if ((e = getenv("SOBER_GAME_USER_ID")) && *e)
        v->user_id = (int64_t)strtoll(e, NULL, 0);
    if ((e = getenv("SOBER_GAME_UNDER13")) && *e)
        v->is_under_13 = (strcmp(e, "1") == 0 || strcasecmp(e, "true") == 0);
    if ((e = getenv("SOBER_GAME_CONVERSATION_ID")) && *e)
        v->conversation_id = (int64_t)strtoll(e, NULL, 0);
    if ((e = getenv("SOBER_GAME_JOIN_REQUEST_TYPE")) && *e)
        v->join_request_type = (int32_t)strtol(e, NULL, 0);
    if ((e = getenv("SOBER_GAME_REFERRED_BY")) && *e)
        v->referred_by_player_id = (int64_t)strtoll(e, NULL, 0);
    if ((e = getenv("SOBER_GAME_DPI")) && *e)
        v->dpi_scale = (float)strtod(e, NULL);
    if ((e = getenv("SOBER_GAME_WIDTH_MM")) && *e)
        v->viewport_width_mm = (int32_t)strtol(e, NULL, 0);
    if ((e = getenv("SOBER_GAME_HEIGHT_MM")) && *e)
        v->viewport_height_mm = (int32_t)strtol(e, NULL, 0);
}

static uintptr_t vptr_for(uintptr_t rva)
{
    return g_base ? (uintptr_t)(g_base + rva) : 0u;
}

/* Guest-shaped backing A.  A = allocation+0x20 so A-0x20 holds a GENUINE
 * guest vptr and A-0x18 holds a zero displacement (helper 0x2e5d420). */
static void *gp_make_backing(uintptr_t header_vt, void **out_b, void **out_c)
{
    unsigned char *a = (unsigned char *)calloc(1, GP_A_PREFIX + GP_A_BYTES);
    if (!a) return NULL;
    *(void **)(a + 0x00) = (void *)header_vt;   /* A-0x20 */
    *(uint64_t *)(a + 0x08) = 0;                /* A-0x18: displacement 0 */

    unsigned char *obj = a + GP_A_PREFIX;
    *(uint32_t *)(obj + 0x00) = 0xc0dedbadu;    /* guest validator 0x245605d */
    *(uint32_t *)(obj + 0x10) = 0;              /* index */

    /* B/C nodes with canonical magic and self-pointers: teardown null-checks
     * (0x23331ce / 0x2333593) take safe branches.  All allocations are owned
     * by this short-lived, unregistered view (see note above). */
    unsigned char *b = (unsigned char *)calloc(1, GP_A_BYTES);
    unsigned char *c = (unsigned char *)calloc(1, GP_A_BYTES);
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

static void *gp_build_view(const char *class_name, size_t bytes,
                           const void *initial, uintptr_t header_vt)
{
    void *b = NULL, *c = NULL;
    GpMirror *o = (GpMirror *)calloc(1, sizeof(*o));
    if (!o) return NULL;
    o->magic = GP_SR_MAGIC;
    o->kind = GP_SR_K_OBJECT;
    o->name = (char *)class_name;        /* borrowed static string */
    o->sig = (char *)"";                 /* non-NULL: helper early-return */
    o->class_name = (char *)class_name;
    o->refs = 1;
    o->payload = calloc(1, bytes ? bytes : 1);
    if (!o->payload) { free(o); return NULL; }
    if (initial && bytes) memcpy(o->payload, initial, bytes);
    o->payload_size = bytes;
    o->guest_b = gp_make_backing(header_vt, &b, &c);
    if (!o->guest_b) { free(o->payload); free(o); return NULL; }
    o->guest_c      = b;
    o->guest_deep_c = c;
    o->guest_sub    = NULL;              /* safe branch if sig ever cleared */
    g_built++;
    return o;
}

/* 0x40-byte PlatformParams native object exactly as 0x26fe98f/0x26fec51
 * build it, with the vtable 0x26feb9b's parser fills. */
static void gp_platform_payload(unsigned char *p,
                                const SoberGameParamsValues *v)
{
    memset(p, 0, SOBER_GAME_PLATFORM_SIZE);
    uintptr_t vt = vptr_for(SOBER_GAME_VT_RVA_PLATFORM);
    memcpy(p + SOBER_GAME_PLATFORM_VPTR, &vt, sizeof(vt));
    memcpy(p + SOBER_GAME_PLATFORM_DPI, &v->dpi_scale, sizeof(v->dpi_scale));
    memcpy(p + SOBER_GAME_PLATFORM_WIDTH_MM, &v->viewport_width_mm,
           sizeof(v->viewport_width_mm));
    memcpy(p + SOBER_GAME_PLATFORM_HEIGHT_MM, &v->viewport_height_mm,
           sizeof(v->viewport_height_mm));
    p[SOBER_GAME_PLATFORM_FLAG1] = 1;   /* parser 0x26febbe */
    p[SOBER_GAME_PLATFORM_FLAG2] = 0;   /* parser 0x26febc6 */
}

/* The 24-field native mirror of the StartGameParams autovalue object. */
static void gp_fields_payload(unsigned char *p,
                              const SoberGameParamsValues *v)
{
    memset(p, 0, GP_FLD_SIZE);
    uintptr_t vt = vptr_for(SOBER_GAME_VT_RVA_STRING); /* header vtable */
    memcpy(p + GP_FLD_VPTR, &vt, sizeof(vt));
    for (int i = 0; i < SOBER_GAME_PARAMS_GETTER_COUNT; ++i) {
        size_t off = GP_FLD_SLOT0 + (size_t)i * 8u;
        if (off + 8u > GP_FLD_SIZE) break;
        uint64_t n = 0;
        switch (i) {
            case 3:  n = (uint64_t)v->place_id; break;
            case 4:  n = (uint64_t)v->user_id; break;
            case 8:  n = (uint64_t)(v->is_under_13 ? 1u : 0u); break;
            case 10: n = (uint64_t)v->conversation_id; break;
            case 13: n = (uint64_t)(int64_t)v->join_request_type; break;
            case 21: n = (uint64_t)v->referred_by_player_id; break;
            default:
                if (s_getters[i].kind != GP_K_SCALAR)
                    memcpy(p + off, &g_h[i], sizeof(void *));
                continue;
        }
        memcpy(p + off, &n, sizeof(n));
    }
}

/* Build the cached object/string handles once, outside JNI dispatch. */
static int gp_ensure_handles(SoberJni *j, void *guest_base,
                             const SoberGameParamsValues *v)
{
    if (!j || !guest_base || !v) return SOBER_JNI_ERR;
    g_base = (unsigned char *)guest_base;
    g_values = *v;                 /* strings are static literals or env-owned */
    g_values_valid = 1;
    if (g_handles_ok) return SOBER_JNI_OK;  /* idempotent */

    unsigned char platform[SOBER_GAME_PLATFORM_SIZE];
    gp_platform_payload(platform, &g_values);

    g_h[0] = sober_jni_new_native_object(
        j, g_values.surface_class ? g_values.surface_class : "android/view/Surface",
        0x10, NULL);
    g_h[1] = gp_build_view(
        g_values.platform_class ? g_values.platform_class
                                : "com/roblox/engine/jni/model/PlatformParams",
        sizeof(platform), platform,
        vptr_for(SOBER_GAME_VT_RVA_PLATFORM));
    /* deviceParams is genuinely NULL in uh/j0.C (setDeviceParams(null));
     * g_h[2] stays NULL.  An explicit device_class override is available for
     * callers that need a stand-in, but the faithful default is NULL. */
    if (g_values.device_class)
        g_h[2] = sober_jni_new_native_object(j, g_values.device_class, 0x10, NULL);

    g_h[5]  = sober_jni_new_string_utf8(j, g_values.access_code);
    g_h[6]  = sober_jni_new_string_utf8(j, g_values.link_code);
    g_h[7]  = sober_jni_new_string_utf8(j, g_values.game_id);
    g_h[9]  = sober_jni_new_string_utf8(j, g_values.username);
    g_h[11] = sober_jni_new_string_utf8(j, g_values.reserved_server_code);
    g_h[12] = sober_jni_new_string_utf8(j, g_values.call_id);
    g_h[14] = sober_jni_new_string_utf8(j, g_values.referral_page);
    g_h[15] = sober_jni_new_string_utf8(j, g_values.launch_data);
    g_h[16] = sober_jni_new_string_utf8(j, g_values.game_join_context);
    g_h[17] = sober_jni_new_string_utf8(j, g_values.event_id);
    g_h[18] = sober_jni_new_string_utf8(j, g_values.join_attempt_id);
    g_h[19] = sober_jni_new_string_utf8(j, g_values.join_attempt_origin);
    g_h[20] = sober_jni_new_string_utf8(j, g_values.iso_context);
    g_h[22] = sober_jni_new_string_utf8(j, g_values.game_id_to_exclude);

    g_h[23] = sober_jni_new_native_object(
        j, g_values.vr_context_class ? g_values.vr_context_class
                                     : "android/app/Activity",
        0x10, NULL);

    for (int i = 0; i < SOBER_GAME_PARAMS_GETTER_COUNT; ++i) {
        if (i == 2) continue;              /* deviceParams: faithful NULL */
        if (s_getters[i].kind != GP_K_SCALAR && !g_h[i]) return SOBER_JNI_ERR;
    }
    g_handles_ok = 1;
    return SOBER_JNI_OK;
}

/* ---- JNI getter dispatch ----------------------------------------------- */
static intptr_t gp_get(void *env, void *obj, const char *name,
                       const char *sig, void *args, void *user)
{
    (void)env; (void)obj; (void)args;
    int idx = -1;
    for (int i = 0; i < SOBER_GAME_PARAMS_GETTER_COUNT; ++i)
        if (name && !strcmp(name, s_getters[i].name)) { idx = i; break; }
    if (idx < 0) return 0;

    s_getters[idx].calls++;
    if (sig && sig[0] && strcmp(sig, s_getters[idx].sig)) g_sig_mismatch++;

    const SoberGameParamsValues *v =
        user ? (const SoberGameParamsValues *)user : &g_values;
    if (s_getters[idx].kind != GP_K_SCALAR)
        return (intptr_t)g_h[idx];

    switch (idx) {
        case 3:  return (intptr_t)v->place_id;
        case 4:  return (intptr_t)v->user_id;
        case 8:  return (intptr_t)(v->is_under_13 ? 1 : 0);
        case 10: return (intptr_t)v->conversation_id;
        case 13: return (intptr_t)(int64_t)v->join_request_type;
        case 21: return (intptr_t)v->referred_by_player_id;
        default: return 0;
    }
}

int sober_game_params_register(SoberJni *j, void *guest_base,
                               const SoberGameParamsValues *values)
{
    if (!sober_game_params_enabled()) return -2;
    if (gp_ensure_handles(j, guest_base, values) != SOBER_JNI_OK)
        return SOBER_JNI_ERR;
    for (int i = 0; i < SOBER_GAME_PARAMS_GETTER_COUNT; ++i) {
        if (sober_jni_set_callback_handler(j, s_getters[i].name, gp_get,
                                           (void *)&g_values) != SOBER_JNI_OK)
            return SOBER_JNI_ERR;
    }
    fprintf(stderr,
            "[game-params] opt-in; registered all %d StartGameParams getters "
            "(platform vptr=%#lx, genuine guest RVA %#x; no host stubs)\n",
            SOBER_GAME_PARAMS_GETTER_COUNT,
            (unsigned long)vptr_for(SOBER_GAME_VT_RVA_PLATFORM),
            (unsigned)SOBER_GAME_VT_RVA_PLATFORM);
    return SOBER_JNI_OK;
}

void *sober_game_params_build(SoberJni *j, void *guest_base,
                              const SoberGameParamsValues *values)
{
    if (!sober_game_params_enabled()) return NULL;
    if (gp_ensure_handles(j, guest_base, values) != SOBER_JNI_OK) return NULL;
    unsigned char fields[GP_FLD_SIZE];
    gp_fields_payload(fields, &g_values);
    void *o = gp_build_view("com/roblox/engine/jni/autovalue/StartGameParams",
                            sizeof(fields), fields,
                            vptr_for(SOBER_GAME_VT_RVA_STRING));
    if (o)
        fprintf(stderr,
                "[game-params] built StartGameParams %p with %d field slots "
                "(gameId='%s', placeId=%lld, accessCode='%s', userId=%lld, "
                "under13=%d, username='%s')\n",
                o, SOBER_GAME_PARAMS_GETTER_COUNT, g_values.game_id,
                (long long)g_values.place_id, g_values.access_code,
                (long long)g_values.user_id, g_values.is_under_13,
                g_values.username);
    return o;
}

void *sober_game_params_make_ex(SoberJni *j, void *guest_base,
                                const SoberGameParamsValues *values)
{
    if (!sober_game_params_enabled()) return NULL;
    SoberGameParamsValues v;
    if (values) v = *values;
    else { sober_game_params_defaults(&v); gp_apply_env(&v); }
    if (sober_game_params_register(j, guest_base, &v) != SOBER_JNI_OK)
        return NULL;
    return sober_game_params_build(j, guest_base, &v);
}

void *sober_game_params_make(SoberJni *j, void *guest_base)
{
    return sober_game_params_make_ex(j, guest_base, NULL);
}

/* ---- diagnostics ------------------------------------------------------- */
const char *sober_game_params_describe(char *buf, size_t cap)
{
    if (!buf || !cap) return "";
    snprintf(buf, cap,
             "enabled=%d base=%p built=%u getters=%d/%d sig_mismatch=%u "
             "gameId='%s' placeId=%lld accessCode='%s' userId=%lld "
             "under13=%d username='%s' platform_vt=%#lx "
             "(env %s, %s)",
             sober_game_params_enabled(), (void *)g_base, g_built,
             (int)sober_game_params_getter_calls(NULL),
             SOBER_GAME_PARAMS_GETTER_COUNT, g_sig_mismatch,
             g_values_valid ? g_values.game_id : "",
             (long long)g_values.place_id,
             g_values_valid ? g_values.access_code : "",
             (long long)g_values.user_id, g_values.is_under_13,
             g_values_valid ? g_values.username : "",
             (unsigned long)vptr_for(SOBER_GAME_VT_RVA_PLATFORM),
             "SOBER_GAME_PARAMS=1", "genuine guest RVAs only");
    return buf;
}

uint64_t sober_game_params_getter_calls(const char *name)
{
    if (!name) {
        uint64_t total = 0;
        for (int i = 0; i < SOBER_GAME_PARAMS_GETTER_COUNT; ++i)
            total += s_getters[i].calls;
        return total;
    }
    for (int i = 0; i < SOBER_GAME_PARAMS_GETTER_COUNT; ++i)
        if (!strcmp(name, s_getters[i].name)) return s_getters[i].calls;
    return 0;
}
