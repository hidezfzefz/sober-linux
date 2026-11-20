/*
 * startgame_v2.c - corrected 24-getter StartGameParams module (team B2).
 *
 * Scope: this file + startgame_v2.h only.  No shared src/, CMakeLists.txt,
 * gate marker, or engine bytes are touched.  See the header for the evidence
 * and for the exact one-line hook the lead should install.
 *
 * Implementation notes (what is deliberately different from game_params.c):
 *   - every registration call is a re-claim: set_callback_handler() replaces a
 *     handler by name in native_jni.c, so the last claim of a name wins and a
 *     stale four-getter handler can never survive our register()/reclaim();
 *   - the getter table carries the exact smali signature and the JNI return
 *     kind; J getters return full 64-bit ints, Z returns 0/1, I returns a
 *     sign-correct int32 - never a jobject handle;
 *   - deviceParams has no override path at all: its slot and its raw handler
 *     return are always NULL (uh/j0.smali:411-413 setDeviceParams(null));
 *   - the handle cache is keyed on the full value set, so changing e.g. the
 *     placeId via the env cannot leave stale jstring handles in the mirror.
 *
 * All allocations backing mirrors are owned by this short-lived, unregistered
 * view (the bridge must not free them: their A pointer is offset into a larger
 * block).  Intentional small leak, as in the sibling params modules.
 */
#define _GNU_SOURCE
#include "startgame_v2.h"
#include "native_jni.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>   /* strcasecmp */

/* ---- private mirror of native_jni.c's SrObject -------------------------
 * Byte-identical layout so sr_check() (magic +0x08, kind +0x10) accepts our
 * handles and the object-return path does not reshape them. */
typedef struct {
    void    *guest_b;      /* +0x00 */
    uint32_t magic;        /* +0x08 */
    uint32_t reserved;     /* +0x0c */
    int      kind;         /* +0x10 */
    uint32_t pad0;         /* +0x14 */
    char    *name;         /* +0x18 */
    char    *sig;          /* +0x20: non-NULL -> dispose helper early-out */
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
} Sgv2Mirror;

#define SV_SR_MAGIC   0x534A4E49u  /* "SJNI" */
#define SV_SR_K_OBJECT 2
#define SV_A_PREFIX   0x20u
#define SV_A_BYTES    0x1200u

/* Native field window of the StartGameParams mirror (0x100 bytes, one 8-byte
 * slot per genuine getter; scalars stored inline). */
#define SV_FLD_VPTR   0x00u
#define SV_FLD_SLOT0  0x08u
#define SV_FLD_SIZE   0x100u

/* Getter kinds. */
enum { SV_K_STRING = 0, SV_K_OBJECT = 1, SV_K_SCALAR = 2, SV_K_DEVICE = 3 };

/* Table order MUST match the switch in sgv2_get()/sgv2_fields_payload(). */
enum {
    SV_SURFACE = 0, SV_PLATFORM, SV_DEVICE, SV_PLACE_ID, SV_USER_ID,
    SV_ACCESS_CODE, SV_LINK_CODE, SV_GAME_ID, SV_UNDER13, SV_USERNAME,
    SV_CONVERSATION_ID, SV_RESERVED_CODE, SV_CALL_ID, SV_JOIN_REQUEST_TYPE,
    SV_REFERRAL_PAGE, SV_LAUNCH_DATA, SV_JOIN_CONTEXT, SV_EVENT_ID,
    SV_JOIN_ATTEMPT_ID, SV_JOIN_ATTEMPT_ORIG, SV_ISO_CONTEXT, SV_REFERRED_BY,
    SV_GAME_ID_EXCLUDE, SV_VR_CONTEXT
};

/* Compile-time invariants: table size and 64-bit intptr_t for jlong returns. */
typedef char sgv2_assert_getter_count[(SV_VR_CONTEXT + 1 == 24 &&
                                      SOBER_SGV2_GETTER_COUNT == 24) ? 1 : -1];
typedef char sgv2_assert_intptr64[(sizeof(intptr_t) >= 8) ? 1 : -1];

/* ---- module state ------------------------------------------------------ */
typedef struct {
    const char *name;
    const char *sig;
    int         kind;
    uint64_t    calls;
} Sgv2Getter;

static Sgv2Getter s_getters[SOBER_SGV2_GETTER_COUNT] = {
    { SOBER_SGV2_GET_SURFACE,           SOBER_SGV2_SIG_SURFACE,           SV_K_OBJECT,  0 },
    { SOBER_SGV2_GET_PLATFORM,          SOBER_SGV2_SIG_PLATFORM,          SV_K_OBJECT,  0 },
    { SOBER_SGV2_GET_DEVICE,            SOBER_SGV2_SIG_DEVICE,            SV_K_DEVICE,  0 },
    { SOBER_SGV2_GET_PLACE_ID,          SOBER_SGV2_SIG_PLACE_ID,          SV_K_SCALAR,  0 },
    { SOBER_SGV2_GET_USER_ID,           SOBER_SGV2_SIG_USER_ID,           SV_K_SCALAR,  0 },
    { SOBER_SGV2_GET_ACCESS_CODE,       SOBER_SGV2_SIG_ACCESS_CODE,       SV_K_STRING,  0 },
    { SOBER_SGV2_GET_LINK_CODE,         SOBER_SGV2_SIG_LINK_CODE,         SV_K_STRING,  0 },
    { SOBER_SGV2_GET_GAME_ID,           SOBER_SGV2_SIG_GAME_ID,           SV_K_STRING,  0 },
    { SOBER_SGV2_GET_UNDER13,           SOBER_SGV2_SIG_UNDER13,           SV_K_SCALAR,  0 },
    { SOBER_SGV2_GET_USERNAME,          SOBER_SGV2_SIG_USERNAME,          SV_K_STRING,  0 },
    { SOBER_SGV2_GET_CONVERSATION_ID,   SOBER_SGV2_SIG_CONVERSATION_ID,   SV_K_SCALAR,  0 },
    { SOBER_SGV2_GET_RESERVED_CODE,     SOBER_SGV2_SIG_RESERVED_CODE,     SV_K_STRING,  0 },
    { SOBER_SGV2_GET_CALL_ID,           SOBER_SGV2_SIG_CALL_ID,           SV_K_STRING,  0 },
    { SOBER_SGV2_GET_JOIN_REQUEST_TYPE, SOBER_SGV2_SIG_JOIN_REQUEST_TYPE, SV_K_SCALAR,  0 },
    { SOBER_SGV2_GET_REFERRAL_PAGE,     SOBER_SGV2_SIG_REFERRAL_PAGE,     SV_K_STRING,  0 },
    { SOBER_SGV2_GET_LAUNCH_DATA,       SOBER_SGV2_SIG_LAUNCH_DATA,       SV_K_STRING,  0 },
    { SOBER_SGV2_GET_JOIN_CONTEXT,      SOBER_SGV2_SIG_JOIN_CONTEXT,      SV_K_STRING,  0 },
    { SOBER_SGV2_GET_EVENT_ID,          SOBER_SGV2_SIG_EVENT_ID,          SV_K_STRING,  0 },
    { SOBER_SGV2_GET_JOIN_ATTEMPT_ID,   SOBER_SGV2_SIG_JOIN_ATTEMPT_ID,   SV_K_STRING,  0 },
    { SOBER_SGV2_GET_JOIN_ATTEMPT_ORIG, SOBER_SGV2_SIG_JOIN_ATTEMPT_ORIG, SV_K_STRING,  0 },
    { SOBER_SGV2_GET_ISO_CONTEXT,       SOBER_SGV2_SIG_ISO_CONTEXT,       SV_K_STRING,  0 },
    { SOBER_SGV2_GET_REFERRED_BY,       SOBER_SGV2_SIG_REFERRED_BY,       SV_K_SCALAR,  0 },
    { SOBER_SGV2_GET_GAME_ID_EXCLUDE,   SOBER_SGV2_SIG_GAME_ID_EXCLUDE,   SV_K_STRING,  0 },
    { SOBER_SGV2_GET_VR_CONTEXT,        SOBER_SGV2_SIG_VR_CONTEXT,        SV_K_OBJECT,  0 },
};

static unsigned char             *g_base;
static SoberStartGameV2Values      g_values;
static void                       *g_h[SOBER_SGV2_GETTER_COUNT];
static int                         g_state_valid;
static int                         g_registered;
static unsigned                    g_builds;
static unsigned                    g_claims;
static unsigned                    g_sig_mismatch;
static int                         g_last_sig_mismatch = -1;
static unsigned                    g_generation;
static unsigned                    g_native_shadows;

/* ---- opt-in / defaults ------------------------------------------------- */
int sober_startgame_v2_enabled(void)
{
    const char *e = getenv("SOBER_STARTGAME_V2");
    return (e && strcmp(e, "1") == 0);
}

const char *sober_startgame_v2_env_vars(void)
{
    return "SOBER_STARTGAME_V2=1 (enable); optional value overrides: "
           "SOBER_SGV2_GAME_ID, SOBER_SGV2_ACCESS_CODE, SOBER_SGV2_USERNAME, "
           "SOBER_SGV2_PLACE_ID, SOBER_SGV2_USER_ID, SOBER_SGV2_UNDER13, "
           "SOBER_SGV2_CONVERSATION_ID, SOBER_SGV2_JOIN_REQUEST_TYPE, "
           "SOBER_SGV2_REFERRED_BY, SOBER_SGV2_DPI, SOBER_SGV2_WIDTH_MM, "
           "SOBER_SGV2_HEIGHT_MM; SOBER_STARTGAME_V2_VERBOSE=1 (log claims)";
}

void sober_startgame_v2_defaults(SoberStartGameV2Values *v)
{
    if (!v) return;
    memset(v, 0, sizeof(*v));
    /* StartGameParams.builder() literals (uh/j0.C + builder()). */
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
    v->place_id             = 123456789;     /* J */
    v->user_id              = 1;             /* J */
    v->is_under_13          = 0;             /* Z */
    v->conversation_id      = 0;             /* J */
    v->join_request_type    = 0;             /* I */
    v->referred_by_player_id = 0;            /* J */
    /* PlatformParams native fields (parser 0x26feb9b defaults). */
    v->dpi_scale          = 1.0f;
    v->viewport_width_mm  = 0;
    v->viewport_height_mm = 0;
    /* NULL = canonical Java class names.  No device class field by design. */
    v->surface_class      = NULL;
    v->platform_class     = NULL;
    v->vr_context_class   = NULL;
}

static const char *env_or(const char *name, const char *fallback)
{
    const char *e = getenv(name);
    return (e && *e) ? e : fallback;
}

static void sgv2_apply_env(SoberStartGameV2Values *v)
{
    v->game_id     = env_or("SOBER_SGV2_GAME_ID",     v->game_id);
    v->access_code = env_or("SOBER_SGV2_ACCESS_CODE", v->access_code);
    v->username    = env_or("SOBER_SGV2_USERNAME",    v->username);
    const char *e;
    if ((e = getenv("SOBER_SGV2_PLACE_ID")) && *e)
        v->place_id = (int64_t)strtoll(e, NULL, 0);
    if ((e = getenv("SOBER_SGV2_USER_ID")) && *e)
        v->user_id = (int64_t)strtoll(e, NULL, 0);
    if ((e = getenv("SOBER_SGV2_UNDER13")) && *e)
        v->is_under_13 = (strcmp(e, "1") == 0 || strcasecmp(e, "true") == 0);
    if ((e = getenv("SOBER_SGV2_CONVERSATION_ID")) && *e)
        v->conversation_id = (int64_t)strtoll(e, NULL, 0);
    if ((e = getenv("SOBER_SGV2_JOIN_REQUEST_TYPE")) && *e)
        v->join_request_type = (int32_t)strtol(e, NULL, 0);
    if ((e = getenv("SOBER_SGV2_REFERRED_BY")) && *e)
        v->referred_by_player_id = (int64_t)strtoll(e, NULL, 0);
    if ((e = getenv("SOBER_SGV2_DPI")) && *e)
        v->dpi_scale = (float)strtod(e, NULL);
    if ((e = getenv("SOBER_SGV2_WIDTH_MM")) && *e)
        v->viewport_width_mm = (int32_t)strtol(e, NULL, 0);
    if ((e = getenv("SOBER_SGV2_HEIGHT_MM")) && *e)
        v->viewport_height_mm = (int32_t)strtol(e, NULL, 0);
}

static void sgv2_prepare_values(const SoberStartGameV2Values *values,
                                SoberStartGameV2Values *out)
{
    if (values) {
        *out = *values;
    } else {
        sober_startgame_v2_defaults(out);
        sgv2_apply_env(out);
    }
}

/* NULL and "" are equivalent for string comparison here. */
static int streq0(const char *a, const char *b)
{
    if (!a) a = "";
    if (!b) b = "";
    return strcmp(a, b) == 0;
}

static int sgv2_values_equal(const SoberStartGameV2Values *a,
                             const SoberStartGameV2Values *b)
{
    if (!a || !b) return 0;
    return streq0(a->surface_class, b->surface_class) &&
           streq0(a->platform_class, b->platform_class) &&
           streq0(a->vr_context_class, b->vr_context_class) &&
           streq0(a->access_code, b->access_code) &&
           streq0(a->link_code, b->link_code) &&
           streq0(a->game_id, b->game_id) &&
           streq0(a->username, b->username) &&
           streq0(a->reserved_server_code, b->reserved_server_code) &&
           streq0(a->call_id, b->call_id) &&
           streq0(a->referral_page, b->referral_page) &&
           streq0(a->launch_data, b->launch_data) &&
           streq0(a->game_join_context, b->game_join_context) &&
           streq0(a->event_id, b->event_id) &&
           streq0(a->join_attempt_id, b->join_attempt_id) &&
           streq0(a->join_attempt_origin, b->join_attempt_origin) &&
           streq0(a->iso_context, b->iso_context) &&
           streq0(a->game_id_to_exclude, b->game_id_to_exclude) &&
           a->place_id == b->place_id &&
           a->user_id == b->user_id &&
           a->is_under_13 == b->is_under_13 &&
           a->conversation_id == b->conversation_id &&
           a->join_request_type == b->join_request_type &&
           a->referred_by_player_id == b->referred_by_player_id &&
           a->dpi_scale == b->dpi_scale &&
           a->viewport_width_mm == b->viewport_width_mm &&
           a->viewport_height_mm == b->viewport_height_mm;
}

static uintptr_t vptr_for(uintptr_t rva)
{
    return g_base ? (uintptr_t)(g_base + rva) : 0u;
}

/* ---- guest-shaped backing / views -------------------------------------- */
static void *sgv2_make_backing(uintptr_t header_vt, void **out_b, void **out_c)
{
    unsigned char *a = (unsigned char *)calloc(1, SV_A_PREFIX + SV_A_BYTES);
    if (!a) return NULL;
    /* A-0x20: genuine guest vptr; A-0x18: zero displacement (helper 0x2e5d420). */
    *(void **)(a + 0x00) = (void *)header_vt;
    *(uint64_t *)(a + 0x08) = 0;

    unsigned char *obj = a + SV_A_PREFIX;
    *(uint32_t *)(obj + 0x00) = 0xc0dedbadu;    /* guest validator 0x245605d */
    *(uint32_t *)(obj + 0x10) = 0;              /* index */

    unsigned char *b = (unsigned char *)calloc(1, SV_A_BYTES);
    unsigned char *c = (unsigned char *)calloc(1, SV_A_BYTES);
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

static void *sgv2_build_view(const char *class_name, size_t bytes,
                             const void *initial, uintptr_t header_vt)
{
    void *b = NULL, *c = NULL;
    Sgv2Mirror *o = (Sgv2Mirror *)calloc(1, sizeof(*o));
    if (!o) return NULL;
    o->magic = SV_SR_MAGIC;
    o->kind = SV_SR_K_OBJECT;
    o->name = (char *)class_name;        /* borrowed static string */
    o->sig = (char *)"";                 /* non-NULL: dispose helper early-out */
    o->class_name = (char *)class_name;
    o->refs = 1;
    o->payload = calloc(1, bytes ? bytes : 1);
    if (!o->payload) { free(o); return NULL; }
    if (initial && bytes) memcpy(o->payload, initial, bytes);
    o->payload_size = bytes;
    o->guest_b = sgv2_make_backing(header_vt, &b, &c);
    if (!o->guest_b) { free(o->payload); free(o); return NULL; }
    o->guest_c      = b;
    o->guest_deep_c = c;
    o->guest_sub    = NULL;              /* safe branch if sig ever cleared */
    g_builds++;
    return o;
}

/* 0x40-byte PlatformParams native window exactly as 0x26fe98f/0x26fec51
 * build it, with the vtable fields 0x26feb9b's parser expects. */
static void sgv2_platform_payload(unsigned char *p, uintptr_t plat_vt,
                                  const SoberStartGameV2Values *v)
{
    memset(p, 0, SOBER_SGV2_PLATFORM_SIZE);
    memcpy(p + SOBER_SGV2_PLATFORM_VPTR, &plat_vt, sizeof(plat_vt));
    memcpy(p + SOBER_SGV2_PLATFORM_DPI, &v->dpi_scale, sizeof(v->dpi_scale));
    memcpy(p + SOBER_SGV2_PLATFORM_WIDTH_MM, &v->viewport_width_mm,
           sizeof(v->viewport_width_mm));
    memcpy(p + SOBER_SGV2_PLATFORM_HEIGHT_MM, &v->viewport_height_mm,
           sizeof(v->viewport_height_mm));
    p[SOBER_SGV2_PLATFORM_FLAG1] = 1;   /* parser 0x26febbe */
    p[SOBER_SGV2_PLATFORM_FLAG2] = 0;   /* parser 0x26febc6 */
}

/* The 24-slot native mirror of the StartGameParams autovalue object. */
static void sgv2_fields_payload(unsigned char *p,
                                const SoberStartGameV2Values *v)
{
    memset(p, 0, SV_FLD_SIZE);
    uintptr_t vt = vptr_for(SOBER_SGV2_VT_RVA_STRING);  /* header vtable */
    memcpy(p + SV_FLD_VPTR, &vt, sizeof(vt));
    for (int i = 0; i < SOBER_SGV2_GETTER_COUNT; ++i) {
        size_t off = SV_FLD_SLOT0 + (size_t)i * 8u;
        if (off + 8u > SV_FLD_SIZE) break;
        uint64_t n = 0;
        switch (i) {
            case SV_PLACE_ID:         n = (uint64_t)v->place_id; break;
            case SV_USER_ID:          n = (uint64_t)v->user_id; break;
            case SV_UNDER13:          n = (uint64_t)(v->is_under_13 ? 1u : 0u); break;
            case SV_CONVERSATION_ID:  n = (uint64_t)v->conversation_id; break;
            case SV_JOIN_REQUEST_TYPE: n = (uint64_t)(int64_t)v->join_request_type; break;
            case SV_REFERRED_BY:      n = (uint64_t)v->referred_by_player_id; break;
            default:
                if (s_getters[i].kind != SV_K_SCALAR)
                    memcpy(p + off, &g_h[i], sizeof(void *));
                continue;
        }
        memcpy(p + off, &n, sizeof(n));
    }
}

/* Build (or reuse) all handles.  Rebuild when values change so a jstring
 * handle can never disagree with the scalar the getter returns. */
static int sgv2_ensure_state(SoberJni *j, void *guest_base,
                             const SoberStartGameV2Values *values)
{
    if (!j || !guest_base || !values) return SOBER_JNI_ERR;
    if (g_state_valid && g_base == (unsigned char *)guest_base &&
        sgv2_values_equal(&g_values, values))
        return SOBER_JNI_OK;

    SoberStartGameV2Values nv = *values;
    unsigned char *nb = (unsigned char *)guest_base;
    void *nh[SOBER_SGV2_GETTER_COUNT];
    memset(nh, 0, sizeof(nh));

    uintptr_t plat_vt = (uintptr_t)nb + SOBER_SGV2_VT_RVA_PLATFORM;
    unsigned char platform[SOBER_SGV2_PLATFORM_SIZE];
    sgv2_platform_payload(platform, plat_vt, &nv);

    nh[SV_SURFACE] = sober_jni_new_native_object(
        j, nv.surface_class ? nv.surface_class : "android/view/Surface",
        0x10, NULL);
    nh[SV_PLATFORM] = sgv2_build_view(
        nv.platform_class ? nv.platform_class
                          : "com/roblox/engine/jni/model/PlatformParams",
        sizeof(platform), platform, plat_vt);
    /* SV_DEVICE stays NULL: no allocation, no override, faithful to the
     * builder's setDeviceParams(null). */
    nh[SV_ACCESS_CODE]      = sober_jni_new_string_utf8(j, nv.access_code);
    nh[SV_LINK_CODE]        = sober_jni_new_string_utf8(j, nv.link_code);
    nh[SV_GAME_ID]          = sober_jni_new_string_utf8(j, nv.game_id);
    nh[SV_USERNAME]         = sober_jni_new_string_utf8(j, nv.username);
    nh[SV_RESERVED_CODE]    = sober_jni_new_string_utf8(j, nv.reserved_server_code);
    nh[SV_CALL_ID]          = sober_jni_new_string_utf8(j, nv.call_id);
    nh[SV_REFERRAL_PAGE]    = sober_jni_new_string_utf8(j, nv.referral_page);
    nh[SV_LAUNCH_DATA]      = sober_jni_new_string_utf8(j, nv.launch_data);
    nh[SV_JOIN_CONTEXT]     = sober_jni_new_string_utf8(j, nv.game_join_context);
    nh[SV_EVENT_ID]         = sober_jni_new_string_utf8(j, nv.event_id);
    nh[SV_JOIN_ATTEMPT_ID]  = sober_jni_new_string_utf8(j, nv.join_attempt_id);
    nh[SV_JOIN_ATTEMPT_ORIG]= sober_jni_new_string_utf8(j, nv.join_attempt_origin);
    nh[SV_ISO_CONTEXT]      = sober_jni_new_string_utf8(j, nv.iso_context);
    nh[SV_GAME_ID_EXCLUDE]  = sober_jni_new_string_utf8(j, nv.game_id_to_exclude);
    nh[SV_VR_CONTEXT] = sober_jni_new_native_object(
        j, nv.vr_context_class ? nv.vr_context_class : "android/app/Activity",
        0x10, NULL);

    for (int i = 0; i < SOBER_SGV2_GETTER_COUNT; ++i) {
        if (i == SV_DEVICE) continue;             /* faithful NULL */
        if (s_getters[i].kind != SV_K_SCALAR && !nh[i])
            return SOBER_JNI_ERR;                 /* old state left untouched */
    }

    g_values = nv;
    g_base = nb;
    memcpy(g_h, nh, sizeof(nh));
    g_state_valid = 1;
    g_generation++;
    g_sig_mismatch = 0;
    g_last_sig_mismatch = -1;
    return SOBER_JNI_OK;
}

/* ---- JNI getter dispatch ----------------------------------------------- */
static int sgv2_find(const char *name)
{
    if (!name) return -1;
    for (int i = 0; i < SOBER_SGV2_GETTER_COUNT; ++i)
        if (!strcmp(name, s_getters[i].name)) return i;
    return -1;
}

static intptr_t sgv2_scalar(int idx, const SoberStartGameV2Values *v)
{
    switch (idx) {
        case SV_PLACE_ID:          return (intptr_t)v->place_id;              /* J */
        case SV_USER_ID:           return (intptr_t)v->user_id;               /* J */
        case SV_UNDER13:           return (intptr_t)(v->is_under_13 ? 1 : 0);  /* Z */
        case SV_CONVERSATION_ID:   return (intptr_t)v->conversation_id;       /* J */
        case SV_JOIN_REQUEST_TYPE: return (intptr_t)(int64_t)v->join_request_type; /* I */
        case SV_REFERRED_BY:       return (intptr_t)v->referred_by_player_id; /* J */
        default:                   return 0;
    }
}

static intptr_t sgv2_get(void *env, void *obj, const char *name,
                         const char *sig, void *args, void *user)
{
    (void)env; (void)obj; (void)args;
    int idx = sgv2_find(name);
    if (idx < 0) return 0;

    s_getters[idx].calls++;
    if (sig && sig[0] && strcmp(sig, s_getters[idx].sig)) {
        g_sig_mismatch++;
        g_last_sig_mismatch = idx;
    }

    const SoberStartGameV2Values *v =
        (user == (void *)&g_values) ? (const SoberStartGameV2Values *)user
                                    : &g_values;
    if (idx == SV_DEVICE) return 0;              /* genuine NULL */
    if (s_getters[idx].kind != SV_K_SCALAR)
        return (intptr_t)g_h[idx];
    return sgv2_scalar(idx, v);
}

/* ---- registration / construction --------------------------------------- */
static int sgv2_claim(SoberJni *j)
{
    if (!j) return SOBER_JNI_ERR;
    int ok = 0;
    /* set_callback_handler() replaces by name in native_jni.c.  Re-claiming
     * all 24 on every call therefore kills any earlier four-getter handler
     * (gameId/placeId/accessCode/username) and cannot leave a shadow. */
    for (int i = 0; i < SOBER_SGV2_GETTER_COUNT; ++i) {
        if (sober_jni_set_callback_handler(j, s_getters[i].name, sgv2_get,
                                           (void *)&g_values) == SOBER_JNI_OK)
            ok++;
    }
    g_claims++;
    return (ok == SOBER_SGV2_GETTER_COUNT) ? SOBER_JNI_OK : SOBER_JNI_ERR;
}

int sober_startgame_v2_register(SoberJni *j, void *guest_base,
                                const SoberStartGameV2Values *values)
{
    if (!sober_startgame_v2_enabled()) return -2;
    SoberStartGameV2Values v;
    sgv2_prepare_values(values, &v);
    if (sgv2_ensure_state(j, guest_base, &v) != SOBER_JNI_OK)
        return SOBER_JNI_ERR;
    int rc = sgv2_claim(j);
    if (rc == SOBER_JNI_OK) {
        g_registered = 1;
        fprintf(stderr,
                "[startgame-v2] opt-in; claimed %d/%d getters with exact smali "
                "sigs (deviceParams=NULL, platform vptr=%#lx from RVA %#x, "
                "J/Z/I primitives native-width; no shadow)\n",
                SOBER_SGV2_GETTER_COUNT, SOBER_SGV2_GETTER_COUNT,
                (unsigned long)vptr_for(SOBER_SGV2_VT_RVA_PLATFORM),
                (unsigned)SOBER_SGV2_VT_RVA_PLATFORM);
    }
    return rc;
}

int sober_startgame_v2_reclaim(SoberJni *j)
{
    if (!g_state_valid || !sober_startgame_v2_enabled()) return -2;
    int rc = sgv2_claim(j);
    if (rc == SOBER_JNI_OK) g_registered = 1;
    if (getenv("SOBER_STARTGAME_V2_VERBOSE"))
        fprintf(stderr, "[startgame-v2] re-claimed all %d getters (gen %u)\n",
                SOBER_SGV2_GETTER_COUNT, g_generation);
    return rc;
}

void *sober_startgame_v2_build(SoberJni *j, void *guest_base,
                               const SoberStartGameV2Values *values)
{
    if (!sober_startgame_v2_enabled()) return NULL;
    SoberStartGameV2Values v;
    sgv2_prepare_values(values, &v);
    if (sgv2_ensure_state(j, guest_base, &v) != SOBER_JNI_OK) return NULL;
    unsigned char fields[SV_FLD_SIZE];
    sgv2_fields_payload(fields, &g_values);
    void *o = sgv2_build_view("com/roblox/engine/jni/autovalue/StartGameParams",
                              sizeof(fields), fields,
                              vptr_for(SOBER_SGV2_VT_RVA_STRING));
    if (o)
        fprintf(stderr,
                "[startgame-v2] built StartGameParams %p (%d slots, gen %u; "
                "gameId='%s' placeId=%lld userId=%lld under13=%d "
                "joinRequestType=%d username='%s' deviceParams=NULL)\n",
                o, SOBER_SGV2_GETTER_COUNT, g_generation, g_values.game_id,
                (long long)g_values.place_id, (long long)g_values.user_id,
                g_values.is_under_13, (int)g_values.join_request_type,
                g_values.username);
    return o;
}

void *sober_startgame_v2_make_ex(SoberJni *j, void *guest_base,
                                 const SoberStartGameV2Values *values)
{
    if (!sober_startgame_v2_enabled()) return NULL;
    SoberStartGameV2Values v;
    sgv2_prepare_values(values, &v);
    if (sober_startgame_v2_register(j, guest_base, &v) != SOBER_JNI_OK)
        return NULL;
    return sober_startgame_v2_build(j, guest_base, &v);
}

void *sober_startgame_v2_make(SoberJni *j, void *guest_base)
{
    return sober_startgame_v2_make_ex(j, guest_base, NULL);
}

/* ---- diagnostics ------------------------------------------------------- */
uint64_t sober_startgame_v2_getter_calls(const char *name)
{
    if (!name) {
        uint64_t total = 0;
        for (int i = 0; i < SOBER_SGV2_GETTER_COUNT; ++i)
            total += s_getters[i].calls;
        return total;
    }
    int idx = sgv2_find(name);
    return idx < 0 ? 0 : s_getters[idx].calls;
}

int sober_startgame_v2_device_is_null(void)
{
    return g_state_valid && g_h[SV_DEVICE] == NULL;
}

uintptr_t sober_startgame_v2_platform_vptr(void)
{
    return vptr_for(SOBER_SGV2_VT_RVA_PLATFORM);
}

int sober_startgame_v2_verify(SoberJni *j, char *buf, size_t cap)
{
    int distinct = 1;
    for (int i = 0; i < SOBER_SGV2_GETTER_COUNT && distinct; ++i) {
        if (!s_getters[i].name || !s_getters[i].sig ||
            s_getters[i].sig[0] != '(' || s_getters[i].sig[1] != ')') {
            distinct = 0;
            break;
        }
        for (int k = i + 1; k < SOBER_SGV2_GETTER_COUNT; ++k)
            if (!strcmp(s_getters[i].name, s_getters[k].name)) {
                distinct = 0;
                break;
            }
    }

    int n_scalar = 0, n_device = 0;
    for (int i = 0; i < SOBER_SGV2_GETTER_COUNT; ++i) {
        if (s_getters[i].kind == SV_K_SCALAR) n_scalar++;
        if (s_getters[i].kind == SV_K_DEVICE) n_device++;
    }

    if (j) {   /* j==NULL (describe()) keeps the last real measurement */
        unsigned shadows = 0;
        for (int i = 0; i < SOBER_SGV2_GETTER_COUNT; ++i)
            if (sober_jni_is_native_registered(j, NULL, s_getters[i].name))
                shadows++;
        g_native_shadows = shadows;
    }

    uintptr_t plat_vt = vptr_for(SOBER_SGV2_VT_RVA_PLATFORM);
    int ok = sober_startgame_v2_enabled() && g_state_valid && g_registered &&
             distinct && n_scalar == 6 && n_device == 1 &&
             sober_startgame_v2_device_is_null() &&
             g_h[SV_PLATFORM] != NULL &&
             plat_vt == (uintptr_t)g_base + SOBER_SGV2_VT_RVA_PLATFORM &&
             g_native_shadows == 0 && g_sig_mismatch == 0;

    if (buf && cap)
        snprintf(buf, cap,
                 "enabled=%d state=%d registered=%d unique_names=%d "
                 "scalars=%d device_null=%d native_shadows=%u sig_mismatch=%u "
                 "(last=%s) platform_vt=%#lx gen=%u claims=%u builds=%u",
                 sober_startgame_v2_enabled(), g_state_valid, g_registered,
                 distinct, n_scalar, sober_startgame_v2_device_is_null(),
                 g_native_shadows, g_sig_mismatch,
                 g_last_sig_mismatch >= 0 ? s_getters[g_last_sig_mismatch].name
                                          : "none",
                 (unsigned long)plat_vt, g_generation, g_claims, g_builds);
    return ok ? 0 : -1;
}

/* Direct call of OUR handler for every getter, with exact smali sigs.  No
 * JNIEnv invocation: the bridge's guest->Java callback counters are untouched
 * and the per-getter counters are restored before returning. */
int sober_startgame_v2_probe(char *buf, size_t cap)
{
    if (!g_state_valid) {
        if (buf && cap) snprintf(buf, cap, "no state; call make() first");
        return -2;
    }
    uint64_t saved[SOBER_SGV2_GETTER_COUNT];
    for (int i = 0; i < SOBER_SGV2_GETTER_COUNT; ++i)
        saved[i] = s_getters[i].calls;

    int fails = 0;
    char first_bad[64] = "";
    for (int i = 0; i < SOBER_SGV2_GETTER_COUNT; ++i) {
        intptr_t got = sgv2_get(NULL, NULL, s_getters[i].name,
                                s_getters[i].sig, NULL, (void *)&g_values);
        intptr_t want = (i == SV_DEVICE) ? 0 :
                        (s_getters[i].kind == SV_K_SCALAR)
                            ? sgv2_scalar(i, &g_values)
                            : (intptr_t)g_h[i];
        if (got != want) {
            fails++;
            if (!first_bad[0])
                snprintf(first_bad, sizeof(first_bad), "%s", s_getters[i].name);
        }
    }
    for (int i = 0; i < SOBER_SGV2_GETTER_COUNT; ++i)
        s_getters[i].calls = saved[i];

    if (buf && cap)
        snprintf(buf, cap, "probe %d/%d returns correct%s%s",
                 SOBER_SGV2_GETTER_COUNT - fails, SOBER_SGV2_GETTER_COUNT,
                 fails ? "; first bad: " : "", first_bad);
    return fails ? -1 : 0;
}

const char *sober_startgame_v2_describe(char *buf, size_t cap)
{
    if (!buf || !cap) return "";
    char vbuf[384];
    vbuf[0] = '\0';
    sober_startgame_v2_verify(NULL, vbuf, sizeof(vbuf));
    snprintf(buf, cap,
             "enabled=%d base=%p builds=%u getters=%llu/%d sig_mismatch=%u "
             "device_null=%d claims=%u gen=%u gameId='%s' placeId=%lld "
             "userId=%lld under13=%d conversationId=%lld joinRequestType=%d "
             "referredBy=%lld platform_vt=%#lx (%s)",
             sober_startgame_v2_enabled(), (void *)g_base, g_builds,
             (unsigned long long)sober_startgame_v2_getter_calls(NULL),
             SOBER_SGV2_GETTER_COUNT, g_sig_mismatch,
             sober_startgame_v2_device_is_null(), g_claims, g_generation,
             g_state_valid ? g_values.game_id : "",
             (long long)g_values.place_id, (long long)g_values.user_id,
             g_values.is_under_13, (long long)g_values.conversation_id,
             (int)g_values.join_request_type,
             (long long)g_values.referred_by_player_id,
             (unsigned long)vptr_for(SOBER_SGV2_VT_RVA_PLATFORM), vbuf);
    return buf;
}
