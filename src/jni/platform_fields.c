/*
 * platform_fields.c - opt-in, faithful PlatformParams JNI-field provider and
 *                     corrected guest-shaped native PlatformParams (analysis A9).
 *
 * Evidence (frozen engine BuildID 204ad049, SHA 3e498d6494..., READ ONLY):
 *   guest 0x26feb9b fills a 0x34-byte scratch struct by three JNI FIELD reads:
 *     GetFieldID  envfn+0x2f0, name 0x535292/0x2c22da/0x3897f6,
 *                 sig 0x46d6c0="F" / 0x532c55="I"
 *     GetFloatField envfn+0x330 -> scratch+0x08 (dpiScale)
 *     GetIntField   envfn+0x320 -> scratch+0x0c/+0x10 (width/height mm)
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
 * Default OFF: SOBER_PLATFORM_PARAMS_FIELDS=1.
 */
#define _GNU_SOURCE
#include "platform_fields.h"
#include "native_jni.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- module state ------------------------------------------------------- */
#define PF_MAX_BINDINGS 16u

typedef struct {
    void *jobj;
    SoberPlatformFieldValues values;
} PfBinding;

static SoberPlatformFieldValues g_defaults;
static PfBinding    g_bindings[PF_MAX_BINDINGS];
static unsigned     g_nbindings;
static int          g_initialised;
static uint64_t     g_calls_dpi;
static uint64_t     g_calls_width;
static uint64_t     g_calls_height;
static uint64_t     g_calls_fieldid;

/* ---- shaped, validated jfieldID tokens --------------------------------- *
 * The guest treats jfieldID as opaque: GetFieldID's result is only ever passed
 * back to Get*Field.  A tagged static descriptor is therefore a faithful
 * stand-in and lets us reject stray pointers without touching SrObject. */
#define PF_FID_MAGIC 0x50464644u  /* "PFFD" */
typedef enum { PF_T_UNKNOWN = 0, PF_T_FLOAT, PF_T_INT } PfType;
typedef struct {
    uint32_t    magic;
    uint32_t    pad;
    PfType      type;
    const char *name;
    const char *sig;
} PfFieldId;

static PfFieldId s_fid_dpi = {
    PF_FID_MAGIC, 0, PF_T_FLOAT, SOBER_PF_FIELD_DPI, SOBER_PF_SIG_FLOAT
};
static PfFieldId s_fid_width = {
    PF_FID_MAGIC, 0, PF_T_INT, SOBER_PF_FIELD_WIDTH, SOBER_PF_SIG_INT
};
static PfFieldId s_fid_height = {
    PF_FID_MAGIC, 0, PF_T_INT, SOBER_PF_FIELD_HEIGHT, SOBER_PF_SIG_INT
};
/* Generic fallbacks for any other field of the same primitive type.  Unknown
 * fields still resolve to 0, matching the previous sr_stub behaviour. */
static PfFieldId s_fid_any_float = {
    PF_FID_MAGIC, 0, PF_T_FLOAT, "", SOBER_PF_SIG_FLOAT
};
static PfFieldId s_fid_any_int = {
    PF_FID_MAGIC, 0, PF_T_INT, "", SOBER_PF_SIG_INT
};

/* ---- opt-in / defaults / init ------------------------------------------ */
int sober_platform_fields_enabled(void)
{
    const char *e = getenv("SOBER_PLATFORM_PARAMS_FIELDS");
    return (e && strcmp(e, "1") == 0);
}

const char *sober_platform_fields_env_vars(void)
{
    return "SOBER_PLATFORM_PARAMS_FIELDS=1 (enable); optional value overrides: "
           "SOBER_PLATFORM_PARAMS_DPI, SOBER_PLATFORM_PARAMS_WIDTH_MM, "
           "SOBER_PLATFORM_PARAMS_HEIGHT_MM";
}

void sober_platform_fields_defaults(SoberPlatformFieldValues *v)
{
    if (!v) return;
    memset(v, 0, sizeof(*v));
    /* Dalvik: DisplayMetrics.density (uh/f.I, cl/a.e) and physical mm
     * (cl/a.i: round(px / xdpi * 25.4f)); always positive on a real device. */
    v->dpi_scale          = 2.0f;
    v->viewport_width_mm  = 70;
    v->viewport_height_mm = 150;
    v->native_window      = NULL;  /* no fabricated ANativeWindow */
}

static void pf_apply_env(SoberPlatformFieldValues *v)
{
    const char *e;
    if ((e = getenv("SOBER_PLATFORM_PARAMS_DPI")) && *e)
        v->dpi_scale = (float)strtod(e, NULL);
    if ((e = getenv("SOBER_PLATFORM_PARAMS_WIDTH_MM")) && *e)
        v->viewport_width_mm = (int32_t)strtol(e, NULL, 0);
    if ((e = getenv("SOBER_PLATFORM_PARAMS_HEIGHT_MM")) && *e)
        v->viewport_height_mm = (int32_t)strtol(e, NULL, 0);
}

static void pf_ensure(void)
{
    if (g_initialised) return;
    sober_platform_fields_defaults(&g_defaults);
    pf_apply_env(&g_defaults);
    g_nbindings = 0;
    g_calls_dpi = g_calls_width = g_calls_height = g_calls_fieldid = 0;
    g_initialised = 1;
}

int sober_platform_fields_init(void)
{
    pf_ensure();
    return sober_platform_fields_enabled() ? SOBER_PF_OK : SOBER_PF_DISABLED;
}

/* Value set for a jobject: exact binding first, configured defaults otherwise.
 * The fallback is what makes this work with startapp_params.c's existing
 * platformParams object when the lead only installs the field functions. */
static const SoberPlatformFieldValues *pf_values_for(void *obj)
{
    pf_ensure();
    for (unsigned i = 0; i < g_nbindings; ++i)
        if (g_bindings[i].jobj == obj) return &g_bindings[i].values;
    return &g_defaults;
}

/* ---- JNI-ABI field functions ------------------------------------------- */
void *sober_platform_fields_GetFieldID(void *env, void *clazz,
                                       const char *name, const char *sig)
{
    (void)env; (void)clazz;
    pf_ensure();
    g_calls_fieldid++;

    int is_f = (sig && sig[0] == SOBER_PF_SIG_FLOAT[0] && sig[1] == '\0');
    int is_i = (sig && sig[0] == SOBER_PF_SIG_INT[0]   && sig[1] == '\0');

    if (name) {
        if (!strcmp(name, SOBER_PF_FIELD_DPI))
            return is_f ? (void *)&s_fid_dpi : (void *)&s_fid_any_float;
        if (!strcmp(name, SOBER_PF_FIELD_WIDTH))
            return is_i ? (void *)&s_fid_width : (void *)&s_fid_any_int;
        if (!strcmp(name, SOBER_PF_FIELD_HEIGHT))
            return is_i ? (void *)&s_fid_height : (void *)&s_fid_any_int;
    }
    if (is_f) return (void *)&s_fid_any_float;
    if (is_i) return (void *)&s_fid_any_int;
    return (void *)&s_fid_any_int;  /* never NULL: keep the guest off a fault */
}

float sober_platform_fields_GetFloatField(void *env, void *obj, void *field_id)
{
    (void)env;
    pf_ensure();
    PfFieldId *f = (PfFieldId *)field_id;
    if (!f || f->magic != PF_FID_MAGIC || f->type != PF_T_FLOAT)
        return 0.0f;
    if (f == &s_fid_dpi) {
        const SoberPlatformFieldValues *v = pf_values_for(obj);
        g_calls_dpi++;
        return v ? v->dpi_scale : 0.0f;
    }
    return 0.0f;  /* unknown float field: previous stub behaviour */
}

int32_t sober_platform_fields_GetIntField(void *env, void *obj, void *field_id)
{
    (void)env;
    pf_ensure();
    PfFieldId *f = (PfFieldId *)field_id;
    if (!f || f->magic != PF_FID_MAGIC || f->type != PF_T_INT)
        return 0;
    const SoberPlatformFieldValues *v = pf_values_for(obj);
    if (f == &s_fid_width) { g_calls_width++;  return v ? v->viewport_width_mm  : 0; }
    if (f == &s_fid_height) { g_calls_height++; return v ? v->viewport_height_mm : 0; }
    return 0;  /* unknown int field: previous stub behaviour */
}

void sober_platform_fields_SetFloatField(void *env, void *obj,
                                         void *field_id, float value)
{
    (void)env;
    pf_ensure();
    PfFieldId *f = (PfFieldId *)field_id;
    if (!f || f->magic != PF_FID_MAGIC || f->type != PF_T_FLOAT || f != &s_fid_dpi)
        return;
    for (unsigned i = 0; i < g_nbindings; ++i)
        if (g_bindings[i].jobj == obj) {
            g_bindings[i].values.dpi_scale = value;
            return;
        }
}

void sober_platform_fields_SetIntField(void *env, void *obj,
                                       void *field_id, int32_t value)
{
    (void)env;
    pf_ensure();
    PfFieldId *f = (PfFieldId *)field_id;
    if (!f || f->magic != PF_FID_MAGIC || f->type != PF_T_INT)
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
}

/* ---- env-table installers ---------------------------------------------- */
/* Match native_jni.c's SETFN idiom: (void *)(uintptr_t)(fp). */
int sober_platform_fields_install_slots(void **env_fn, unsigned slot_count)
{
    pf_ensure();
    if (!sober_platform_fields_enabled()) return SOBER_PF_DISABLED;
    if (!env_fn) return SOBER_PF_ERR;
    if (slot_count <= SOBER_PF_SLOT_GETFLOATFIELD) return SOBER_PF_ERR;

    env_fn[SOBER_PF_SLOT_GETFIELDID] =
        (void *)(uintptr_t)&sober_platform_fields_GetFieldID;
    env_fn[SOBER_PF_SLOT_GETINTFIELD] =
        (void *)(uintptr_t)&sober_platform_fields_GetIntField;
    env_fn[SOBER_PF_SLOT_GETFLOATFIELD] =
        (void *)(uintptr_t)&sober_platform_fields_GetFloatField;
    return SOBER_PF_OK;
}

int sober_platform_fields_install_via(
        void *ctx, void (*setfn)(void *ctx, unsigned slot, void *fn))
{
    pf_ensure();
    if (!sober_platform_fields_enabled()) return SOBER_PF_DISABLED;
    if (!setfn) return SOBER_PF_ERR;
    setfn(ctx, SOBER_PF_SLOT_GETFIELDID,
          (void *)(uintptr_t)&sober_platform_fields_GetFieldID);
    setfn(ctx, SOBER_PF_SLOT_GETINTFIELD,
          (void *)(uintptr_t)&sober_platform_fields_GetIntField);
    setfn(ctx, SOBER_PF_SLOT_GETFLOATFIELD,
          (void *)(uintptr_t)&sober_platform_fields_GetFloatField);
    return SOBER_PF_OK;
}

/* ---- native object / shaped jobject ------------------------------------ */
size_t sober_platform_fields_native_object(const SoberPlatformFieldValues *v,
                                           void *guest_base, int for_update,
                                           unsigned char *out, size_t cap)
{
    pf_ensure();
    if (!out || cap < SOBER_PF_NATIVE_SIZE) return 0;
    if (!v) v = &g_defaults;

    memset(out, 0, SOBER_PF_NATIVE_SIZE);

    uintptr_t vt = 0;
    if (guest_base)
        vt = (uintptr_t)guest_base +
             (for_update ? SOBER_PF_VT_RVA_UPDATE : SOBER_PF_VT_RVA_START);
    memcpy(out + SOBER_PF_N_VPTR, &vt, sizeof(vt));
    memcpy(out + SOBER_PF_N_WINDOW, &v->native_window, sizeof(void *));
    memcpy(out + SOBER_PF_N_DPI, &v->dpi_scale, sizeof(v->dpi_scale));
    memcpy(out + SOBER_PF_N_WIDTH_MM, &v->viewport_width_mm,
           sizeof(v->viewport_width_mm));
    memcpy(out + SOBER_PF_N_HEIGHT_MM, &v->viewport_height_mm,
           sizeof(v->viewport_height_mm));
    out[SOBER_PF_N_FLAG1] = 1;   /* parser literal 0x26febbe */
    /* +0x28 / +0x30 / +0x38 stay zero (memset above). */
    return SOBER_PF_NATIVE_SIZE;
}

void *sober_platform_fields_make_object(SoberJni *j, void *guest_base,
                                        const SoberPlatformFieldValues *v,
                                        int for_update)
{
    pf_ensure();
    if (!sober_platform_fields_enabled() || !j || !guest_base) return NULL;
    if (!v) v = &g_defaults;

    unsigned char native[SOBER_PF_NATIVE_SIZE];
    if (sober_platform_fields_native_object(v, guest_base, for_update,
                                            native, sizeof(native)) == 0)
        return NULL;

    /* sober_jni_new_native_object() returns a fully shaped SrObject with the
     * A/B/C/D guest graph and stores our 0x40 bytes as its payload. */
    void *h = sober_jni_new_native_object(j, SOBER_PF_CLASS,
                                          SOBER_PF_NATIVE_SIZE, native);
    if (h) sober_platform_fields_bind_object(h, v);
    return h;
}

int sober_platform_fields_bind_object(void *jobj,
                                      const SoberPlatformFieldValues *v)
{
    pf_ensure();
    if (!jobj || !v) return SOBER_PF_ERR;
    for (unsigned i = 0; i < g_nbindings; ++i)
        if (g_bindings[i].jobj == jobj) {
            g_bindings[i].values = *v;
            return SOBER_PF_OK;
        }
    if (g_nbindings >= PF_MAX_BINDINGS) return SOBER_PF_ERR;
    g_bindings[g_nbindings].jobj = jobj;
    g_bindings[g_nbindings].values = *v;
    g_nbindings++;
    return SOBER_PF_OK;
}

/* ---- diagnostics ------------------------------------------------------- */
const char *sober_platform_fields_describe(char *buf, size_t cap)
{
    if (!buf || !cap) return "";
    pf_ensure();
    snprintf(buf, cap,
             "enabled=%d init=%d bindings=%u/%u base_vt=%#x/%#x "
             "dpi=%.3f w=%d h=%d window=%p "
             "calls(GetFieldID=%llu,dpi=%llu,w=%llu,h=%llu) "
             "(env %s; genuine guest RVAs only)",
             sober_platform_fields_enabled(), g_initialised, g_nbindings,
             PF_MAX_BINDINGS,
             (unsigned)SOBER_PF_VT_RVA_START, (unsigned)SOBER_PF_VT_RVA_UPDATE,
             (double)g_defaults.dpi_scale, (int)g_defaults.viewport_width_mm,
             (int)g_defaults.viewport_height_mm, g_defaults.native_window,
             (unsigned long long)g_calls_fieldid,
             (unsigned long long)g_calls_dpi,
             (unsigned long long)g_calls_width,
             (unsigned long long)g_calls_height,
             "SOBER_PLATFORM_PARAMS_FIELDS=1");
    return buf;
}

uint64_t sober_platform_fields_field_calls(const char *name)
{
    if (!name)
        return g_calls_dpi + g_calls_width + g_calls_height;
    if (!strcmp(name, SOBER_PF_FIELD_DPI))    return g_calls_dpi;
    if (!strcmp(name, SOBER_PF_FIELD_WIDTH))  return g_calls_width;
    if (!strcmp(name, SOBER_PF_FIELD_HEIGHT)) return g_calls_height;
    if (!strcmp(name, "GetFieldID"))          return g_calls_fieldid;
    return 0;
}
