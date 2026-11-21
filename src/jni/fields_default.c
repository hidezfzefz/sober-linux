/*
 * fields_default.c - DEFAULT-ON, faithful PlatformParams JNI-field provider
 *                    and corrected guest-shaped native PlatformParams object.
 *                    (Team 3 / agent T3-1; see fields_default.h for evidence.)
 *
 * Evidence (frozen engine BuildID 204ad049, SHA 3e498d6494..., READ ONLY):
 *   guest 0x26feb9b fills a 0x34-byte scratch struct by three JNI FIELD reads:
 *     GetFieldID   envfn+0x2f0, name 0x535292/0x2c22da/0x3897f6,
 *                  sig 0x46d6c0="F" / 0x532c55="I"
 *     GetFloatField envfn+0x330 -> scratch+0x08 (dpiScale)
 *     GetIntField   envfn+0x320 -> scratch+0x0c/+0x10 (width/height mm)
 *   helpers 0x24ac975 (float) / 0x23846d4 (int) call GetFieldID then tail-jump
 *   through GetFloatField / GetIntField; the jfieldID is opaque to the guest.
 *   our SRNativeInterface has 4 reserved ptrs before fn[231], hence
 *     fn_index = (envfn_offset/8) - 4  ->  GetFieldID=90, GetIntField=96,
 *                                          GetFloatField=98 (all sr_stub today).
 *   callers 0x26fe98f/0x26fec51 (StartApp) and 0x277328f/0x2773354 (Update)
 *     allocate new(0x40), install vptr 0x6dbca20 / 0x6dbcb20, and copy the
 *     scratch struct into native+0x08..+0x38 (shift +8), so:
 *       +0x00 vptr, +0x08 ANativeWindow*, +0x10 float dpi,
 *       +0x14/+0x18 int mm, +0x20 u64 flag=1, +0x28 ctx, +0x30/+0x38 0.
 *
 * No guest bytes patched.  No host stub vtable: every vptr is guest_base + a
 * genuine guest RVA.  No callback emitted, no decision-013 marker touched.
 * DEFAULT-ON: kill switch SOBER_FIELDS_DEFAULT=0 makes every entry point inert.
 */
#define _GNU_SOURCE
#include "native_jni.h"
#include "fields_default.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* ---- module state ------------------------------------------------------- */
#define FD_MAX_BINDINGS 16u

typedef struct {
    void *jobj;
    SoberFieldsDefaultValues values;
} FdBinding;

static SoberFieldsDefaultValues g_values;
static FdBinding  g_bindings[FD_MAX_BINDINGS];
static unsigned   g_nbindings;
static int        g_initialised;
static uint64_t   g_calls_dpi;
static uint64_t   g_calls_width;
static uint64_t   g_calls_height;
static uint64_t   g_calls_fieldid;
static uint64_t   g_install_count;

/* ---- shaped, validated jfieldID tokens ---------------------------------- *
 * The guest treats jfieldID as opaque: GetFieldID's result is only ever passed
 * back to Get*Field.  A tagged static descriptor is therefore a faithful
 * stand-in and lets us reject stray pointers without touching SrObject. */
#define FD_FID_MAGIC 0x53464444u  /* "SFDD" */
typedef enum { FD_T_UNKNOWN = 0, FD_T_FLOAT, FD_T_INT } FdType;
typedef struct {
    uint32_t    magic;
    uint32_t    pad;
    FdType      type;
    const char *name;
    const char *sig;
} FdFieldId;

static FdFieldId s_fid_dpi = {
    FD_FID_MAGIC, 0, FD_T_FLOAT,
    SOBER_FIELDS_DEFAULT_FIELD_DPI, SOBER_FIELDS_DEFAULT_SIG_FLOAT
};
static FdFieldId s_fid_width = {
    FD_FID_MAGIC, 0, FD_T_INT,
    SOBER_FIELDS_DEFAULT_FIELD_WIDTH, SOBER_FIELDS_DEFAULT_SIG_INT
};
static FdFieldId s_fid_height = {
    FD_FID_MAGIC, 0, FD_T_INT,
    SOBER_FIELDS_DEFAULT_FIELD_HEIGHT, SOBER_FIELDS_DEFAULT_SIG_INT
};
/* Generic fallbacks for any other field of the same primitive type.  Unknown
 * fields still resolve to 0, matching the previous sr_stub behaviour. */
static FdFieldId s_fid_any_float = {
    FD_FID_MAGIC, 0, FD_T_FLOAT, "", SOBER_FIELDS_DEFAULT_SIG_FLOAT
};
static FdFieldId s_fid_any_int = {
    FD_FID_MAGIC, 0, FD_T_INT, "", SOBER_FIELDS_DEFAULT_SIG_INT
};

/* ---- default-on gate / defaults / init ---------------------------------- */
int sober_fields_default_enabled(void)
{
    const char *e = getenv(SOBER_FIELDS_DEFAULT_ENV);
    if (!e)
        return 1;                       /* default ON */
    if (strcmp(e, "0") == 0 ||
        strcasecmp(e, "off") == 0 ||
        strcasecmp(e, "no") == 0 ||
        strcasecmp(e, "false") == 0)
        return 0;                       /* explicit kill switch */
    return 1;
}

const char *sober_fields_default_env_vars(void)
{
    return SOBER_FIELDS_DEFAULT_ENV "=0 (kill switch; default ON); "
           "optional value overrides: " SOBER_FIELDS_DEFAULT_ENV_DPI ", "
           SOBER_FIELDS_DEFAULT_ENV_WIDTH ", " SOBER_FIELDS_DEFAULT_ENV_HEIGHT;
}

void sober_fields_default_values(SoberFieldsDefaultValues *v)
{
    if (!v) return;
    memset(v, 0, sizeof(*v));
    /* Dalvik: DisplayMetrics.density (uh/f.I, cl/a.e) and physical mm
     * (cl/a.i: round(px / xdpi * 25.4f)); always positive on a real device. */
    v->dpi_scale          = 2.0f;
    v->viewport_width_mm  = 70;
    v->viewport_height_mm = 150;
    v->native_window      = NULL;       /* no fabricated ANativeWindow */
}

static void fd_apply_env(SoberFieldsDefaultValues *v)
{
    const char *e;
    if ((e = getenv(SOBER_FIELDS_DEFAULT_ENV_DPI)) && *e)
        v->dpi_scale = (float)strtod(e, NULL);
    if ((e = getenv(SOBER_FIELDS_DEFAULT_ENV_WIDTH)) && *e)
        v->viewport_width_mm = (int32_t)strtol(e, NULL, 0);
    if ((e = getenv(SOBER_FIELDS_DEFAULT_ENV_HEIGHT)) && *e)
        v->viewport_height_mm = (int32_t)strtol(e, NULL, 0);
}

static void fd_reset_values(void)
{
    sober_fields_default_values(&g_values);
    fd_apply_env(&g_values);
}

/* Lazy initialisation sets the flag FIRST so it is strictly single-shot. */
static void fd_init_state(void)
{
    if (g_initialised) return;
    g_initialised = 1;                  /* set FIRST: single-shot invariant */
    fd_reset_values();
    g_nbindings = 0;
    g_calls_dpi = g_calls_width = g_calls_height = g_calls_fieldid = 0;
}

static void fd_ensure_state(void)
{
    if (!g_initialised) fd_init_state();
}

int sober_fields_default_set_values(const SoberFieldsDefaultValues *v)
{
    fd_ensure_state();
    if (v) g_values = *v;
    else   fd_reset_values();
    return SOBER_FIELDS_DEFAULT_OK;
}

const SoberFieldsDefaultValues *sober_fields_default_current_values(void)
{
    fd_ensure_state();
    return &g_values;
}

void sober_fields_default_init(void)
{
    fd_init_state();
    fd_reset_values();
    g_nbindings = 0;
    sober_fields_default_reset_counters();
}

/* Value set for a jobject: exact binding first, configured values otherwise.
 * The fallback makes stepping-2-only wiring work with startapp_params.c's
 * existing platformParams object. */
static const SoberFieldsDefaultValues *fd_values_for(void *obj)
{
    fd_ensure_state();
    for (unsigned i = 0; i < g_nbindings; ++i)
        if (g_bindings[i].jobj == obj) return &g_bindings[i].values;
    return &g_values;
}

/* ---- JNI-ABI field functions ------------------------------------------- */
void *sober_fields_default_GetFieldID(void *env, void *clazz,
                                       const char *name, const char *sig)
{
    (void)env; (void)clazz;
    if (!sober_fields_default_enabled()) return NULL;
    fd_ensure_state();
    g_calls_fieldid++;

    int is_f = (sig && sig[0] == SOBER_FIELDS_DEFAULT_SIG_FLOAT[0] && sig[1] == '\0');
    int is_i = (sig && sig[0] == SOBER_FIELDS_DEFAULT_SIG_INT[0]   && sig[1] == '\0');

    if (name) {
        if (!strcmp(name, SOBER_FIELDS_DEFAULT_FIELD_DPI))
            return is_f ? (void *)&s_fid_dpi : (void *)&s_fid_any_float;
        if (!strcmp(name, SOBER_FIELDS_DEFAULT_FIELD_WIDTH))
            return is_i ? (void *)&s_fid_width : (void *)&s_fid_any_int;
        if (!strcmp(name, SOBER_FIELDS_DEFAULT_FIELD_HEIGHT))
            return is_i ? (void *)&s_fid_height : (void *)&s_fid_any_int;
    }
    if (is_f) return (void *)&s_fid_any_float;
    if (is_i) return (void *)&s_fid_any_int;
    return (void *)&s_fid_any_int;      /* never NULL: keep the guest off a fault */
}

float sober_fields_default_GetFloatField(void *env, void *obj, void *field_id)
{
    (void)env;
    if (!sober_fields_default_enabled()) return 0.0f;
    fd_ensure_state();
    FdFieldId *f = (FdFieldId *)field_id;
    if (!f || f->magic != FD_FID_MAGIC || f->type != FD_T_FLOAT)
        return 0.0f;
    if (f == &s_fid_dpi) {
        const SoberFieldsDefaultValues *v = fd_values_for(obj);
        g_calls_dpi++;
        return v ? v->dpi_scale : 0.0f;
    }
    return 0.0f;                        /* unknown float field: stub behaviour */
}

int32_t sober_fields_default_GetIntField(void *env, void *obj, void *field_id)
{
    (void)env;
    if (!sober_fields_default_enabled()) return 0;
    fd_ensure_state();
    FdFieldId *f = (FdFieldId *)field_id;
    if (!f || f->magic != FD_FID_MAGIC || f->type != FD_T_INT)
        return 0;
    const SoberFieldsDefaultValues *v = fd_values_for(obj);
    if (f == &s_fid_width) { g_calls_width++;  return v ? v->viewport_width_mm  : 0; }
    if (f == &s_fid_height) { g_calls_height++; return v ? v->viewport_height_mm : 0; }
    return 0;                           /* unknown int field: stub behaviour */
}

void sober_fields_default_SetFloatField(void *env, void *obj,
                                        void *field_id, float value)
{
    (void)env;
    fd_ensure_state();
    FdFieldId *f = (FdFieldId *)field_id;
    if (!f || f->magic != FD_FID_MAGIC || f->type != FD_T_FLOAT || f != &s_fid_dpi)
        return;
    for (unsigned i = 0; i < g_nbindings; ++i)
        if (g_bindings[i].jobj == obj) {
            g_bindings[i].values.dpi_scale = value;
            return;
        }
    g_values.dpi_scale = value;         /* unbound: update module defaults */
}

void sober_fields_default_SetIntField(void *env, void *obj,
                                      void *field_id, int32_t value)
{
    (void)env;
    fd_ensure_state();
    FdFieldId *f = (FdFieldId *)field_id;
    if (!f || f->magic != FD_FID_MAGIC || f->type != FD_T_INT)
        return;
    int is_w = (f == &s_fid_width);
    int is_h = (f == &s_fid_height);
    if (!is_w && !is_h) return;
    for (unsigned i = 0; i < g_nbindings; ++i)
        if (g_bindings[i].jobj == obj) {
            if (is_w) g_bindings[i].values.viewport_width_mm  = value;
            else      g_bindings[i].values.viewport_height_mm = value;
            return;
        }
    if (is_w) g_values.viewport_width_mm  = value;
    else      g_values.viewport_height_mm = value;
}

/* ---- env-table installers (default-on) --------------------------------- */
/* Match native_jni.c's SETFN idiom: (void *)(uintptr_t)(fp). */
int sober_fields_default_install_slots(void **env_fn, unsigned slot_count)
{
    fd_ensure_state();
    if (!sober_fields_default_enabled()) return SOBER_FIELDS_DEFAULT_DISABLED;
    if (!env_fn) return SOBER_FIELDS_DEFAULT_ERR;
    if (slot_count <= SOBER_FIELDS_DEFAULT_SLOT_GETFLOATFIELD)
        return SOBER_FIELDS_DEFAULT_ERR;

    env_fn[SOBER_FIELDS_DEFAULT_SLOT_GETFIELDID] =
        (void *)(uintptr_t)&sober_fields_default_GetFieldID;
    env_fn[SOBER_FIELDS_DEFAULT_SLOT_GETINTFIELD] =
        (void *)(uintptr_t)&sober_fields_default_GetIntField;
    env_fn[SOBER_FIELDS_DEFAULT_SLOT_GETFLOATFIELD] =
        (void *)(uintptr_t)&sober_fields_default_GetFloatField;
    g_install_count++;
    return SOBER_FIELDS_DEFAULT_OK;
}

int sober_fields_default_install_via(
        void *ctx, void (*setfn)(void *ctx, unsigned slot, void *fn))
{
    fd_ensure_state();
    if (!sober_fields_default_enabled()) return SOBER_FIELDS_DEFAULT_DISABLED;
    if (!setfn) return SOBER_FIELDS_DEFAULT_ERR;
    setfn(ctx, SOBER_FIELDS_DEFAULT_SLOT_GETFIELDID,
          (void *)(uintptr_t)&sober_fields_default_GetFieldID);
    setfn(ctx, SOBER_FIELDS_DEFAULT_SLOT_GETINTFIELD,
          (void *)(uintptr_t)&sober_fields_default_GetIntField);
    setfn(ctx, SOBER_FIELDS_DEFAULT_SLOT_GETFLOATFIELD,
          (void *)(uintptr_t)&sober_fields_default_GetFloatField);
    g_install_count++;
    return SOBER_FIELDS_DEFAULT_OK;
}

int sober_fields_default_install_env(void *jni_env, unsigned slot_count)
{
    if (!jni_env) return SOBER_FIELDS_DEFAULT_ERR;
    /* SREnvObj: functions pointer first, then the 4 reserved pointer slots of
     * SRNativeInterface before fn[0].  This derives fn[0] from a JNIEnv*. */
    void *iface = *(void **)jni_env;
    if (!iface) return SOBER_FIELDS_DEFAULT_ERR;
    void **env_fn = (void **)iface + 4u;
    return sober_fields_default_install_slots(env_fn, slot_count);
}

/* ---- native object / shaped jobject ------------------------------------ */
size_t sober_fields_default_native_object(const SoberFieldsDefaultValues *v,
                                          void *guest_base, int for_update,
                                          unsigned char *out, size_t cap)
{
    fd_ensure_state();
    if (!out || cap < SOBER_FIELDS_DEFAULT_NATIVE_SIZE) return 0;
    if (!v) v = &g_values;

    memset(out, 0, SOBER_FIELDS_DEFAULT_NATIVE_SIZE);

    uintptr_t vt = 0;
    if (guest_base)
        vt = (uintptr_t)guest_base +
             (for_update ? SOBER_FIELDS_DEFAULT_VT_RVA_UPDATE
                         : SOBER_FIELDS_DEFAULT_VT_RVA_START);
    memcpy(out + SOBER_FIELDS_DEFAULT_N_VPTR, &vt, sizeof(vt));
    memcpy(out + SOBER_FIELDS_DEFAULT_N_WINDOW, &v->native_window,
           sizeof(v->native_window));
    memcpy(out + SOBER_FIELDS_DEFAULT_N_DPI, &v->dpi_scale,
           sizeof(v->dpi_scale));
    memcpy(out + SOBER_FIELDS_DEFAULT_N_WIDTH_MM, &v->viewport_width_mm,
           sizeof(v->viewport_width_mm));
    memcpy(out + SOBER_FIELDS_DEFAULT_N_HEIGHT_MM, &v->viewport_height_mm,
           sizeof(v->viewport_height_mm));
    out[SOBER_FIELDS_DEFAULT_N_FLAG1] = 1;   /* parser literal 0x26febbe */
    /* +0x28 / +0x30 / +0x38 stay zero (memset above). */
    return SOBER_FIELDS_DEFAULT_NATIVE_SIZE;
}

void *sober_fields_default_make_object(SoberJni *j, void *guest_base,
                                       const SoberFieldsDefaultValues *v,
                                       int for_update)
{
    fd_ensure_state();
    if (!sober_fields_default_enabled() || !j || !guest_base) return NULL;
    if (!v) v = &g_values;

    unsigned char native[SOBER_FIELDS_DEFAULT_NATIVE_SIZE];
    if (sober_fields_default_native_object(v, guest_base, for_update,
                                           native, sizeof(native)) == 0)
        return NULL;

    /* sober_jni_new_native_object() returns a fully shaped SrObject with the
     * A/B/C/D guest graph and stores our 0x40 bytes as its payload. */
    void *h = sober_jni_new_native_object(
        j, SOBER_FIELDS_DEFAULT_CLASS, SOBER_FIELDS_DEFAULT_NATIVE_SIZE, native);
    if (h) sober_fields_default_bind_object(h, v);
    return h;
}

int sober_fields_default_bind_object(void *jobj,
                                     const SoberFieldsDefaultValues *v)
{
    fd_ensure_state();
    if (!jobj || !v) return SOBER_FIELDS_DEFAULT_ERR;
    for (unsigned i = 0; i < g_nbindings; ++i)
        if (g_bindings[i].jobj == jobj) {
            g_bindings[i].values = *v;
            return SOBER_FIELDS_DEFAULT_OK;
        }
    if (g_nbindings >= FD_MAX_BINDINGS) return SOBER_FIELDS_DEFAULT_ERR;
    g_bindings[g_nbindings].jobj = jobj;
    g_bindings[g_nbindings].values = *v;
    g_nbindings++;
    return SOBER_FIELDS_DEFAULT_OK;
}

/* ---- verification counters / diagnostics -------------------------------- */
uint64_t sober_fields_default_calls(const char *name)
{
    if (!name)
        return g_calls_dpi + g_calls_width + g_calls_height;
    if (!strcmp(name, SOBER_FIELDS_DEFAULT_FIELD_DPI))    return g_calls_dpi;
    if (!strcmp(name, SOBER_FIELDS_DEFAULT_FIELD_WIDTH))  return g_calls_width;
    if (!strcmp(name, SOBER_FIELDS_DEFAULT_FIELD_HEIGHT)) return g_calls_height;
    if (!strcmp(name, "GetFieldID"))                      return g_calls_fieldid;
    return 0;
}

uint64_t sober_fields_default_install_count(void)
{
    return g_install_count;
}

void sober_fields_default_reset_counters(void)
{
    g_calls_dpi = g_calls_width = g_calls_height = g_calls_fieldid = 0;
    g_install_count = 0;
}

const char *sober_fields_default_describe(char *buf, size_t cap)
{
    if (!buf || !cap) return "";
    fd_ensure_state();
    snprintf(buf, cap,
             "enabled=%d init=%d bindings=%u/%u slots=%u/%u/%u "
             "vt_rva=%#x/%#x dpi=%.3f w=%d h=%d window=%p "
             "installs=%llu calls(GetFieldID=%llu,dpi=%llu,w=%llu,h=%llu) "
             "(env %s; genuine guest RVAs only)",
             sober_fields_default_enabled(), g_initialised, g_nbindings,
             FD_MAX_BINDINGS,
             SOBER_FIELDS_DEFAULT_SLOT_GETFIELDID,
             SOBER_FIELDS_DEFAULT_SLOT_GETINTFIELD,
             SOBER_FIELDS_DEFAULT_SLOT_GETFLOATFIELD,
             (unsigned)SOBER_FIELDS_DEFAULT_VT_RVA_START,
             (unsigned)SOBER_FIELDS_DEFAULT_VT_RVA_UPDATE,
             (double)g_values.dpi_scale, (int)g_values.viewport_width_mm,
             (int)g_values.viewport_height_mm, g_values.native_window,
             (unsigned long long)g_install_count,
             (unsigned long long)g_calls_fieldid,
             (unsigned long long)g_calls_dpi,
             (unsigned long long)g_calls_width,
             (unsigned long long)g_calls_height,
             SOBER_FIELDS_DEFAULT_ENV "=0 disables");
    return buf;
}

/* Built-in, read-only (no guest writes) contract check. */
int sober_fields_default_selftest(char *buf, size_t cap)
{
    int fails = 0;
    fd_ensure_state();

    /* 1. slot arithmetic matches the guest's JNIEnv offsets. */
    if ((SOBER_FIELDS_DEFAULT_OFF_GETFIELDID / 8u - 4u) !=
            SOBER_FIELDS_DEFAULT_SLOT_GETFIELDID ||
        (SOBER_FIELDS_DEFAULT_OFF_GETINTFIELD / 8u - 4u) !=
            SOBER_FIELDS_DEFAULT_SLOT_GETINTFIELD ||
        (SOBER_FIELDS_DEFAULT_OFF_GETFLOATFIELD / 8u - 4u) !=
            SOBER_FIELDS_DEFAULT_SLOT_GETFLOATFIELD)
        fails++;

    /* 2. both 0x40 payload layouts (vptr / window / typed values / flag). */
    unsigned char base[16];
    memset(base, 0, sizeof(base));
    SoberFieldsDefaultValues v = g_values;
    v.dpi_scale          = 2.5f;
    v.viewport_width_mm  = 80;
    v.viewport_height_mm = 160;
    v.native_window      = (void *)(uintptr_t)0x1234u;  /* placeholder only */
    for (int update = 0; update <= 1; ++update) {
        unsigned char obj[SOBER_FIELDS_DEFAULT_NATIVE_SIZE];
        memset(obj, 0xa5, sizeof(obj));
        size_t n = sober_fields_default_native_object(
            &v, base, update, obj, sizeof(obj));
        if (n != SOBER_FIELDS_DEFAULT_NATIVE_SIZE) { fails++; continue; }

        uintptr_t got_vt = 0, want_vt = (uintptr_t)base +
            (update ? SOBER_FIELDS_DEFAULT_VT_RVA_UPDATE
                    : SOBER_FIELDS_DEFAULT_VT_RVA_START);
        void *got_window = NULL;
        float got_dpi = 0.0f;
        int32_t got_w = 0, got_h = 0;
        uint64_t got_flag = 0, got_ctx = 1, got_tail_a = 1, got_tail_b = 1;
        memcpy(&got_vt, obj + SOBER_FIELDS_DEFAULT_N_VPTR, sizeof(got_vt));
        memcpy(&got_window, obj + SOBER_FIELDS_DEFAULT_N_WINDOW,
               sizeof(got_window));
        memcpy(&got_dpi, obj + SOBER_FIELDS_DEFAULT_N_DPI, sizeof(got_dpi));
        memcpy(&got_w, obj + SOBER_FIELDS_DEFAULT_N_WIDTH_MM, sizeof(got_w));
        memcpy(&got_h, obj + SOBER_FIELDS_DEFAULT_N_HEIGHT_MM, sizeof(got_h));
        memcpy(&got_flag, obj + SOBER_FIELDS_DEFAULT_N_FLAG1, sizeof(got_flag));
        memcpy(&got_ctx, obj + SOBER_FIELDS_DEFAULT_N_CTX, sizeof(got_ctx));
        memcpy(&got_tail_a, obj + SOBER_FIELDS_DEFAULT_N_TAIL64_A,
               sizeof(got_tail_a));
        memcpy(&got_tail_b, obj + SOBER_FIELDS_DEFAULT_N_TAIL64_B,
               sizeof(got_tail_b));
        if (got_vt != want_vt || got_window != v.native_window ||
            got_dpi != v.dpi_scale || got_w != v.viewport_width_mm ||
            got_h != v.viewport_height_mm || got_flag != 1u ||
            got_ctx != 0u || got_tail_a != 0u || got_tail_b != 0u)
            fails++;
    }
    /* too-small capacity must fail closed. */
    unsigned char tiny[8];
    if (sober_fields_default_native_object(&v, base, 0, tiny, sizeof(tiny)) != 0)
        fails++;

    /* 3. gate semantics + GetFieldID -> Get*Field round-trip. */
    const SoberFieldsDefaultValues *cur = sober_fields_default_current_values();
    if (sober_fields_default_enabled()) {
        void *fid_f = sober_fields_default_GetFieldID(
            NULL, NULL, SOBER_FIELDS_DEFAULT_FIELD_DPI,
            SOBER_FIELDS_DEFAULT_SIG_FLOAT);
        void *fid_w = sober_fields_default_GetFieldID(
            NULL, NULL, SOBER_FIELDS_DEFAULT_FIELD_WIDTH,
            SOBER_FIELDS_DEFAULT_SIG_INT);
        void *fid_h = sober_fields_default_GetFieldID(
            NULL, NULL, SOBER_FIELDS_DEFAULT_FIELD_HEIGHT,
            SOBER_FIELDS_DEFAULT_SIG_INT);
        if (!fid_f || !fid_w || !fid_h) fails++;
        if (fid_f && sober_fields_default_GetFloatField(NULL, NULL, fid_f)
                         != cur->dpi_scale) fails++;
        if (fid_w && sober_fields_default_GetIntField(NULL, NULL, fid_w)
                        != cur->viewport_width_mm) fails++;
        if (fid_h && sober_fields_default_GetIntField(NULL, NULL, fid_h)
                        != cur->viewport_height_mm) fails++;
    } else {
        /* Kill switch: field functions must be inert. */
        void *fid = sober_fields_default_GetFieldID(
            NULL, NULL, SOBER_FIELDS_DEFAULT_FIELD_DPI,
            SOBER_FIELDS_DEFAULT_SIG_FLOAT);
        if (fid != NULL) fails++;
        if (sober_fields_default_GetFloatField(NULL, NULL, (void *)&s_fid_dpi)
                != 0.0f) fails++;
        if (sober_fields_default_GetIntField(NULL, NULL, (void *)&s_fid_width)
                != 0) fails++;
    }

    if (buf && cap)
        snprintf(buf, cap,
                 "fields_default selftest: %d failure(s); enabled=%d "
                 "slots=%u/%u/%u vt=%#x/%#x native=%#x installs=%llu",
                 fails, sober_fields_default_enabled(),
                 SOBER_FIELDS_DEFAULT_SLOT_GETFIELDID,
                 SOBER_FIELDS_DEFAULT_SLOT_GETINTFIELD,
                 SOBER_FIELDS_DEFAULT_SLOT_GETFLOATFIELD,
                 SOBER_FIELDS_DEFAULT_VT_RVA_START,
                 SOBER_FIELDS_DEFAULT_VT_RVA_UPDATE,
                 (unsigned)SOBER_FIELDS_DEFAULT_NATIVE_SIZE,
                 (unsigned long long)g_install_count);
    return fails;
}
