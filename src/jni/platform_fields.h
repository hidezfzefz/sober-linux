/*
 * platform_fields.h - opt-in, faithful PlatformParams JNI-field provider and
 *                     corrected guest-shaped native PlatformParams (analysis A9).
 *
 * Owner: subagent A9.  Scope: this file + platform_fields.c ONLY (NEW files).
 *
 * WHY THIS EXISTS
 * ---------------
 * analysis/A09-platform-params.md proved that the frozen engine reads the three
 * PlatformParams numbers with JNI FIELDS, not method getters:
 *
 *   helper 0x26feb9b (PlatformParams parser, called from both
 *   nativeAppBridgeV2StartAppWithParams 0x26fe80f and
 *   nativeAppBridgeV2UpdateSurfaceAppWithPlatformParams 0x2773194):
 *
 *     GetObjectClass      envfn +0x0f8
 *     GetFieldID          envfn +0x2f0   name "dpiScale"/"viewportWidthMm"/
 *                                        "viewportHeightMm", sig "F"/"I"/"I"
 *     GetFloatField       envfn +0x330   -> scratch +0x08
 *     GetIntField         envfn +0x320   -> scratch +0x0c / +0x10
 *
 * In our native bridge those map to SRNativeInterface fn[90] (GetFieldID),
 * fn[98] (GetFloatField) and fn[96] (GetIntField) - four reserved pointers
 * precede fn[231], so fn_index = (envfn_offset / 8) - 4.  All three are
 * currently sr_stub, so the guest sees zero/default numbers.  This module
 * supplies the three real JNI-ABI functions plus a correct 0x40 native object.
 *
 * DECISION 013 COMPLIANCE
 * ----------------------
 * Default OFF (SOBER_PLATFORM_PARAMS_FIELDS=1).  No guest bytes are patched,
 * no host function is put in a vtable (every vptr is guest_base + a genuine
 * guest RVA), no callback is emitted, and no decision-013 marker is touched.
 * The ANativeWindow pointer at native+0x08 is a caller-supplied placeholder
 * (default NULL); this module never fabricates Android graphics internals.
 *
 * LEAD WIRING (exact)
 * -------------------
 * 1. In native_jni.c sr_build_tables(), after the default SETFN block:
 *
 *      #include "platform_fields.h"
 *      ...
 *      if (sober_platform_fields_enabled())
 *          sober_platform_fields_install_slots(j->env_table.fn, 231);
 *
 *    (j->env_table.fn is the void* fn[231] array; the installer writes
 *     fn[90], fn[96] and fn[98] with the JNI-ABI functions below.)
 * 2. Call sober_platform_fields_init() once before invoking guest startup
 *    RVAs (idempotent; also lazily invoked by every entry point here).
 * 3. For the platformParams() getter, return
 *      sober_platform_fields_make_object(j, guest_base, &values, 0)
 *    OR keep startapp_params.c's object and call
 *      sober_platform_fields_bind_object(handle, &values) on it so the field
 *    reads resolve to these values.  Unknown objects fall back to the
 *    configured defaults, so either path works.
 * 4. Add platform_fields.c to src/jni/CMakeLists.txt (lead-owned).
 */
#ifndef SOBER_JNI_PLATFORM_FIELDS_H
#define SOBER_JNI_PLATFORM_FIELDS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SoberJni SoberJni;   /* native_jni.h is the full definition */

/* ---- return codes (numerically identical to SOBER_JNI_OK/ERR) ---------- */
#define SOBER_PF_OK         0
#define SOBER_PF_ERR       (-1)
#define SOBER_PF_DISABLED  (-2)

/* ---- opt-in ------------------------------------------------------------ */
/* Default OFF. Enabled only when getenv("SOBER_PLATFORM_PARAMS_FIELDS")=="1". */
int         sober_platform_fields_enabled(void);
const char *sober_platform_fields_env_vars(void);

/* Field names / signatures as read by 0x26feb9b (OBSERVED C-string RVAs
 * 0x535292 / 0x2c22da / 0x3897f6 and signature literals 0x46d6c0 / 0x532c55). */
#define SOBER_PF_FIELD_DPI       "dpiScale"
#define SOBER_PF_FIELD_WIDTH     "viewportWidthMm"
#define SOBER_PF_FIELD_HEIGHT    "viewportHeightMm"
#define SOBER_PF_SIG_FLOAT       "F"
#define SOBER_PF_SIG_INT         "I"
#define SOBER_PF_CLASS           "com/roblox/engine/jni/model/PlatformParams"

/* JNIEnv table slots proven by A9 (fn_index = envfn_offset/8 - 4). */
#define SOBER_PF_SLOT_GETFIELDID     90u   /* guest envfn +0x2f0 */
#define SOBER_PF_SLOT_GETINTFIELD    96u   /* guest envfn +0x320 */
#define SOBER_PF_SLOT_GETFLOATFIELD  98u   /* guest envfn +0x330 */

/* Genuine guest vtable RVAs (BuildID 204ad049 / SHA 3e498d6494...). */
#define SOBER_PF_VT_RVA_START   0x6dbca20u  /* installed at 0x26fe98f/0x26fec51 */
#define SOBER_PF_VT_RVA_UPDATE  0x6dbcb20u  /* installed at 0x277328f/0x2773354 */
#define SOBER_PF_BUILDID32      0x204ad049u  /* informational */

/* Corrected 0x40-byte native PlatformParams layout (scratch offset + 8). */
#define SOBER_PF_NATIVE_SIZE    0x40u
#define SOBER_PF_N_VPTR         0x00u   /* guest_base + vtable RVA           */
#define SOBER_PF_N_WINDOW       0x08u   /* ANativeWindow * (caller-supplied)  */
#define SOBER_PF_N_DPI          0x10u   /* float  dpiScale                   */
#define SOBER_PF_N_WIDTH_MM     0x14u   /* int32  viewportWidthMm            */
#define SOBER_PF_N_HEIGHT_MM    0x18u   /* int32  viewportHeightMm           */
#define SOBER_PF_N_FLAG1        0x20u   /* u64 = 1 (parser 0x26febbe)        */
#define SOBER_PF_N_CTX          0x28u   /* 0 / vrContext global ref (0 here) */
#define SOBER_PF_N_TAIL64_A     0x30u   /* 0 */
#define SOBER_PF_N_TAIL64_B     0x38u   /* 0 */

/* ---- values ------------------------------------------------------------ */
typedef struct SoberPlatformFieldValues {
    float    dpi_scale;           /* default 2.0f (DisplayMetrics.density-like, >0) */
    int32_t  viewport_width_mm;   /* default 70  (physical mm, >0) */
    int32_t  viewport_height_mm;  /* default 150 (physical mm, >0) */
    void    *native_window;       /* default NULL -> native+0x08 placeholder */
} SoberPlatformFieldValues;

/* Fill with the Dalvik-derived defaults above. */
void sober_platform_fields_defaults(SoberPlatformFieldValues *v);

/* Apply env overrides to the module defaults; reset state/counters; idempotent.
 * Returns SOBER_PF_OK when enabled, SOBER_PF_DISABLED when the opt-in is off. */
int sober_platform_fields_init(void);

/* ---- JNI-ABI functions (install into the JNIEnv table) ----------------- *
 * Signatures are exactly those the guest calls through JNIEnv:
 *   jobject  GetFieldID  (JNIEnv*, jclass, const char* name, const char* sig)
 *   jfloat   GetFloatField(JNIEnv*, jobject, jfieldID)
 *   jint     GetIntField  (JNIEnv*, jobject, jfieldID)
 * They are usable directly with a SETFN-style assignment, e.g.
 *   SETFN(*t, 90, sober_platform_fields_GetFieldID);
 * ------------------------------------------------------------------------ */
void   *sober_platform_fields_GetFieldID(void *env, void *clazz,
                                         const char *name, const char *sig);
float   sober_platform_fields_GetFloatField(void *env, void *obj,
                                            void *field_id);
int32_t sober_platform_fields_GetIntField(void *env, void *obj,
                                          void *field_id);

/* Matching setters (not installed by default; exposed for completeness). */
void sober_platform_fields_SetFloatField(void *env, void *obj,
                                         void *field_id, float value);
void sober_platform_fields_SetIntField(void *env, void *obj,
                                       void *field_id, int32_t value);

/* Install the three proven field functions into a 231-slot JNIEnv fn table.
 * `env_fn` is &SoberJni::env_table.fn[0]; `slot_count` is normally 231.
 * Writes fn[90], fn[96], fn[98].  No-op/returns SOBER_PF_DISABLED unless the
 * opt-in is on.  Returns SOBER_PF_OK on success, SOBER_PF_ERR on bad args. */
int sober_platform_fields_install_slots(void **env_fn, unsigned slot_count);

/* Alternative for callers that own the table but not its raw pointer:
 * setfn(ctx, slot, fn) writes one slot.  Installs the same three functions. */
int sober_platform_fields_install_via(
        void *ctx, void (*setfn)(void *ctx, unsigned slot, void *fn));

/* ---- 0x40 native object + shaped jobject ------------------------------- *
 * Write the corrected 0x40-byte native PlatformParams object into `out`:
 *   +0x00 vptr = guest_base + (for_update ? 0x6dbcb20 : 0x6dbca20)
 *   +0x08 native_window, +0x10 float, +0x14/+0x18 ints, +0x20 = 1, rest 0.
 * Returns 0x40 on success, 0 on bad args / insufficient capacity. */
size_t sober_platform_fields_native_object(const SoberPlatformFieldValues *v,
                                           void *guest_base, int for_update,
                                           unsigned char *out, size_t cap);

/* Build a shaped PlatformParams jobject whose payload is that 0x40 object and
 * bind its typed values so Get*Field resolves on it.  NULL when disabled or on
 * allocation failure.  `guest_base` must be non-NULL (vptrs are guest RVAs). */
void *sober_platform_fields_make_object(SoberJni *j, void *guest_base,
                                        const SoberPlatformFieldValues *v,
                                        int for_update);

/* Bind typed values to an already-existing jobject handle (e.g. the object
 * returned by startapp_params.c's platformParams() getter).  After this,
 * GetFloatField/GetIntField on that handle return these values. */
int sober_platform_fields_bind_object(void *jobj,
                                      const SoberPlatformFieldValues *v);

/* ---- diagnostics ------------------------------------------------------- */
const char *sober_platform_fields_describe(char *buf, size_t cap);
/* Per-field read counts.  name: "dpiScale" / "viewportWidthMm" /
 * "viewportHeightMm" / "GetFieldID" / NULL (= total field-value reads). */
uint64_t    sober_platform_fields_field_calls(const char *name);

#ifdef __cplusplus
}
#endif
#endif /* SOBER_JNI_PLATFORM_FIELDS_H */
