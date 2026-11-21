/*
 * fields_default.h - DEFAULT-ON, faithful PlatformParams JNI-FIELD path and
 *                    corrected 0x40 native PlatformParams object.
 *
 * Owner: logical Team 3 (JNI + render), agent T3-1.
 * SCOPE: this header + fields_default.c ONLY (NEW files).  The lead owns all
 *        existing src/ files, every CMakeLists.txt, docs/, tests/ and git; the
 *        engine is frozen.  Marker WITHHELD: this module never touches it.
 *
 * WHY THIS EXISTS
 * ---------------
 * analysis/A09-platform-params.md proved (OBSERVED bytes + DERIVED slot math)
 * that the frozen engine reads the three PlatformParams numbers with JNI FIELDS,
 * not method getters, in helper 0x26feb9b (called by both
 * nativeAppBridgeV2StartAppWithParams 0x26fe80f and
 * nativeAppBridgeV2UpdateSurfaceAppWithPlatformParams 0x2773194):
 *
 *     GetObjectClass   envfn +0x0f8
 *     GetFieldID       envfn +0x2f0   name "dpiScale"/"viewportWidthMm"/
 *                                      "viewportHeightMm", sig "F"/"I"/"I"
 *     GetFloatField    envfn +0x330   -> scratch +0x08  (dpiScale)
 *     GetIntField      envfn +0x320   -> scratch +0x0c/+0x10 (width/height mm)
 *
 * Our SRNativeInterface has four reserved pointers before fn[231], so a JNIEnv
 * offset maps to fn_index = (offset / 8) - 4:
 *     0x2f0/8 - 4 = 90  (GetFieldID)
 *     0x320/8 - 4 = 96  (GetIntField)
 *     0x330/8 - 4 = 98  (GetFloatField)
 * In sr_build_tables() all three are sr_stub today, so the guest sees 0/NULL
 * and never gets a frame.  This module supplies the three real JNI-ABI
 * functions plus a correct 0x40-byte native object, and is DEFAULT-ON.
 *
 * HARD RULES OBSERVED HERE
 * ------------------------
 *  - No guest byte patches.  No host pointer is put in a vtable: every vptr is
 *    guest_base + a genuine guest RVA (0x6dbca20 Start / 0x6dbcb20 Update).
 *  - No callbacks are emitted and no decision-013 marker is touched.
 *  - The ANativeWindow pointer at native+0x08 is caller-supplied (default
 *    NULL); this module never fabricates Android graphics internals.
 *  - Default ON; kill switch SOBER_FIELDS_DEFAULT=0 makes every entry point an
 *    inert no-op (installer leaves the table untouched, getters return 0/NULL).
 *  - Idempotent installation and lazy initialisation; safe to call repeatedly.
 *
 * EXACT WIRING FOR THE LEAD (lead-owned native_jni.c / src/jni/CMakeLists.txt)
 * ---------------------------------------------------------------------------
 * 1. Add fields_default.c to the sober_jni STATIC sources in
 *    src/jni/CMakeLists.txt.
 * 2. In native_jni.c sr_build_tables(), prefer this over the opt-in
 *    platform_fields.c installer so the field path is on by default:
 *
 *      #include "fields_default.h"
 *      ...
 *      (void)sober_fields_default_install_slots(t->fn, 231);
 *
 *    install_slots writes fn[90], fn[96] and fn[98]; it is a no-op when the
 *    kill switch is set.  Do NOT install both this and platform_fields.c;
 *    fields_default supersedes it (same slots, default-on vs default-off).
 * 3. Optionally, in the platformParams() getter, either return
 *      sober_fields_default_make_object(j, guest_base, &values, for_update)
 *    or bind the existing object with
 *      sober_fields_default_bind_object(handle, &values).
 *    Unknown objects fall back to the configured defaults, so merely installing
 *    the slots (step 2) is already sufficient for the guest's field reads.
 * 4. Call sober_fields_default_selftest() from the harness self-test; it
 *    verifies slots, payload layout and field round-trips without guest writes.
 * ---------------------------------------------------------------------------
 */
#ifndef SOBER_JNI_FIELDS_DEFAULT_H
#define SOBER_JNI_FIELDS_DEFAULT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Root build uses -fvisibility=hidden; publish every entry point so the lead
 * can also resolve them with dlsym if that route is preferred. */
#if defined(__GNUC__) || defined(__clang__)
#  define SOBER_FIELDS_DEFAULT_API __attribute__((visibility("default")))
#else
#  define SOBER_FIELDS_DEFAULT_API
#endif

/* Build identity these RVAs belong to (informational guard only). */
#define SOBER_FIELDS_DEFAULT_BUILDID32 0x204ad049u
#define SOBER_FIELDS_DEFAULT_BUILDID \
    "204ad049599c96afb75b8a21d5051063e821d6c9"
#define SOBER_FIELDS_DEFAULT_SHA256 \
    "3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335"

/* ---- default-on gate / kill switch -------------------------------------- */
#define SOBER_FIELDS_DEFAULT_ENV        "SOBER_FIELDS_DEFAULT"
/* Optional typed value overrides (applied by _set_from_env()/_ensure()). */
#define SOBER_FIELDS_DEFAULT_ENV_DPI    "SOBER_FIELDS_DEFAULT_DPI"
#define SOBER_FIELDS_DEFAULT_ENV_WIDTH  "SOBER_FIELDS_DEFAULT_WIDTH_MM"
#define SOBER_FIELDS_DEFAULT_ENV_HEIGHT "SOBER_FIELDS_DEFAULT_HEIGHT_MM"

/* Return codes shared by every install/init/bind function. */
#define SOBER_FIELDS_DEFAULT_OK         0   /* installed / applied / verified  */
#define SOBER_FIELDS_DEFAULT_DISABLED   1   /* kill switch: inert no-op        */
#define SOBER_FIELDS_DEFAULT_ERR       (-1) /* bad argument (not a gate result)*/

/* DEFAULT ON: returns 0 only when getenv(SOBER_FIELDS_DEFAULT) is exactly "0"
 * (the aliases "off"/"no"/"false", case-insensitive, are also honoured). */
SOBER_FIELDS_DEFAULT_API int sober_fields_default_enabled(void);

/* Human-readable gate/override description (for harness/env dumps). */
SOBER_FIELDS_DEFAULT_API const char *sober_fields_default_env_vars(void);

/* ---- field names / signatures (OBSERVED C-string RVAs) ------------------- */
#define SOBER_FIELDS_DEFAULT_FIELD_DPI    "dpiScale"          /* RVA 0x535292 */
#define SOBER_FIELDS_DEFAULT_FIELD_WIDTH  "viewportWidthMm"   /* RVA 0x2c22da */
#define SOBER_FIELDS_DEFAULT_FIELD_HEIGHT "viewportHeightMm"  /* RVA 0x3897f6 */
#define SOBER_FIELDS_DEFAULT_SIG_FLOAT    "F"                 /* RVA 0x46d6c0 */
#define SOBER_FIELDS_DEFAULT_SIG_INT      "I"                 /* RVA 0x532c55 */
#define SOBER_FIELDS_DEFAULT_CLASS \
    "com/roblox/engine/jni/model/PlatformParams"

/* ---- JNIEnv offsets and our fn[] slots ---------------------------------- */
#define SOBER_FIELDS_DEFAULT_OFF_GETFIELDID   0x2f0u  /* guest GetFieldID      */
#define SOBER_FIELDS_DEFAULT_OFF_GETINTFIELD   0x320u  /* guest GetIntField     */
#define SOBER_FIELDS_DEFAULT_OFF_GETFLOATFIELD 0x330u  /* guest GetFloatField   */
#define SOBER_FIELDS_DEFAULT_SLOT_GETFIELDID   90u    /* 0x2f0/8 - 4           */
#define SOBER_FIELDS_DEFAULT_SLOT_GETINTFIELD  96u    /* 0x320/8 - 4           */
#define SOBER_FIELDS_DEFAULT_SLOT_GETFLOATFIELD 98u   /* 0x330/8 - 4           */
#define SOBER_FIELDS_DEFAULT_ENV_SLOTS         231u   /* SRNativeInterface::fn */

/* ---- frozen-engine RVAs (BuildID 204ad049 / SHA 3e498d6494..., READ ONLY) */
#define SOBER_FIELDS_DEFAULT_RVA_PARSER       0x26feb9bu  /* field-reading fn  */
#define SOBER_FIELDS_DEFAULT_RVA_HELPER_FLOAT 0x24ac975u  /* F: GetFieldID+Float*/
#define SOBER_FIELDS_DEFAULT_RVA_HELPER_INT   0x23846d4u  /* I: GetFieldID+Int  */
#define SOBER_FIELDS_DEFAULT_RVA_START_APP    0x26fe80fu  /* StartApp export    */
#define SOBER_FIELDS_DEFAULT_RVA_UPDATE_APP   0x2773194u  /* UpdateSurface exp  */
#define SOBER_FIELDS_DEFAULT_RVA_START_VT_A   0x26fe98fu  /* install Start vptr  */
#define SOBER_FIELDS_DEFAULT_RVA_START_VT_B   0x26fec51u  /* install Start vptr  */
#define SOBER_FIELDS_DEFAULT_RVA_UPDATE_VT_A  0x277328fu  /* install Update vptr */
#define SOBER_FIELDS_DEFAULT_RVA_UPDATE_VT_B  0x2773354u  /* install Update vptr */
#define SOBER_FIELDS_DEFAULT_RVA_WINDOW_GLOBAL 0x72a3308u  /* ANativeWindow*    */

/* Two distinct genuine guest vtables installed by the callers. */
#define SOBER_FIELDS_DEFAULT_VT_RVA_START  0x6dbca20u  /* StartAppWithParams   */
#define SOBER_FIELDS_DEFAULT_VT_RVA_UPDATE 0x6dbcb20u  /* UpdateSurfaceApp     */

/* ---- corrected 0x40-byte native PlatformParams layout -------------------- *
 * scratch +0x00..+0x30 is copied into native +0x08..+0x38 (shift +8).        */
#define SOBER_FIELDS_DEFAULT_NATIVE_SIZE   0x40u
#define SOBER_FIELDS_DEFAULT_N_VPTR        0x00u  /* guest_base + VT_RVA       */
#define SOBER_FIELDS_DEFAULT_N_WINDOW      0x08u  /* ANativeWindow * (caller)  */
#define SOBER_FIELDS_DEFAULT_N_DPI         0x10u  /* float  dpiScale           */
#define SOBER_FIELDS_DEFAULT_N_WIDTH_MM    0x14u  /* int32  viewportWidthMm    */
#define SOBER_FIELDS_DEFAULT_N_HEIGHT_MM   0x18u  /* int32  viewportHeightMm   */
#define SOBER_FIELDS_DEFAULT_N_FLAG1       0x20u  /* u64 = 1 (parser 0x26febbe)*/
#define SOBER_FIELDS_DEFAULT_N_CTX         0x28u  /* 0 / vrContext global ref  */
#define SOBER_FIELDS_DEFAULT_N_TAIL64_A    0x30u  /* 0 */
#define SOBER_FIELDS_DEFAULT_N_TAIL64_B    0x38u  /* 0 */

/* ---- typed values ------------------------------------------------------- */
typedef struct SoberFieldsDefaultValues {
    float    dpi_scale;           /* default 2.0f (DisplayMetrics.density)   */
    int32_t  viewport_width_mm;   /* default 70  (physical mm, >0)           */
    int32_t  viewport_height_mm;  /* default 150 (physical mm, >0)           */
    void    *native_window;       /* default NULL -> native+0x08 placeholder */
} SoberFieldsDefaultValues;

#ifndef SOBER_NATIVE_JNI_H
typedef struct SoberJni SoberJni;   /* native_jni.h owns the full definition */
#endif

/* Fill v with the Dalvik-derived defaults above (no env overrides). */
SOBER_FIELDS_DEFAULT_API void sober_fields_default_values(
    SoberFieldsDefaultValues *v);

/* Replace the module-wide values (copied).  NULL restores the defaults plus
 * the env overrides.  Always returns SOBER_FIELDS_DEFAULT_OK. */
SOBER_FIELDS_DEFAULT_API int sober_fields_default_set_values(
    const SoberFieldsDefaultValues *v);

/* Apply the optional env overrides to defaults; reset bindings/counters. */
SOBER_FIELDS_DEFAULT_API void sober_fields_default_init(void);

/* Current module-wide values (never NULL after init). */
SOBER_FIELDS_DEFAULT_API const SoberFieldsDefaultValues *
sober_fields_default_current_values(void);

/* ---- JNI-ABI functions (install into the JNIEnv fn table) --------------- *
 * Exact prototypes the guest calls through JNIEnv:
 *   void   *GetFieldID    (JNIEnv*, jclass, const char* name, const char* sig)
 *   float   GetFloatField (JNIEnv*, jobject, jfieldID)
 *   int32_t GetIntField   (JNIEnv*, jobject, jfieldID)
 * Each is usable directly with the bridge's SETFN idiom.  When the kill switch
 * is set they stay callable but return NULL/0 (neutral no-op semantics).
 * ------------------------------------------------------------------------ */
SOBER_FIELDS_DEFAULT_API void *sober_fields_default_GetFieldID(
    void *env, void *clazz, const char *name, const char *sig);
SOBER_FIELDS_DEFAULT_API float sober_fields_default_GetFloatField(
    void *env, void *obj, void *field_id);
SOBER_FIELDS_DEFAULT_API int32_t sober_fields_default_GetIntField(
    void *env, void *obj, void *field_id);

/* Matching setters (not installed by default; exposed for harness tests). */
SOBER_FIELDS_DEFAULT_API void sober_fields_default_SetFloatField(
    void *env, void *obj, void *field_id, float value);
SOBER_FIELDS_DEFAULT_API void sober_fields_default_SetIntField(
    void *env, void *obj, void *field_id, int32_t value);

/* ---- installers --------------------------------------------------------- *
 * DEFAULT-ON installers: they install fn[90], fn[96] and fn[98] unless the kill
 * switch is set (then no-op/SOBER_FIELDS_DEFAULT_DISABLED).  Idempotent.
 * ------------------------------------------------------------------------ */
/* `env_fn` is &SoberJni::env_table.fn[0]; slot_count is normally 231.
 * Returns OK on success, DISABLED when killed, ERR on bad arguments. */
SOBER_FIELDS_DEFAULT_API int sober_fields_default_install_slots(
    void **env_fn, unsigned slot_count);

/* Same via a setfn(ctx, slot, fn) callback owned by the caller. */
SOBER_FIELDS_DEFAULT_API int sober_fields_default_install_via(
    void *ctx, void (*setfn)(void *ctx, unsigned slot, void *fn));

/* Convenience: install from a JNIEnv* (SREnvObj begins with a pointer to the
 * SRNativeInterface, then four reserved pointers before fn[0]).  Returns OK /
 * DISABLED / ERR.  Prefer install_slots() inside sr_build_tables(). */
SOBER_FIELDS_DEFAULT_API int sober_fields_default_install_env(
    void *jni_env, unsigned slot_count);

/* ---- 0x40 native object + shaped jobject -------------------------------- */
/* Write the corrected 0x40-byte native object into `out` (no guest writes):
 *   +0x00 vptr = guest_base + (for_update ? 0x6dbcb20 : 0x6dbca20)
 *   +0x08 native_window, +0x10 float, +0x14/+0x18 ints, +0x20 u64=1, rest 0.
 * Returns 0x40 on success, 0 on bad args / insufficient capacity. */
SOBER_FIELDS_DEFAULT_API size_t sober_fields_default_native_object(
    const SoberFieldsDefaultValues *v, void *guest_base, int for_update,
    unsigned char *out, size_t cap);

/* Build a shaped PlatformParams jobject whose payload is that 0x40 object and
 * bind its values so Get*Field resolves on it.  Needs the sober_jni library;
 * NULL when killed / bad args / allocation failure. */
SOBER_FIELDS_DEFAULT_API void *sober_fields_default_make_object(
    SoberJni *j, void *guest_base, const SoberFieldsDefaultValues *v,
    int for_update);

/* Bind typed values to an existing jobject handle.  After this, Get*Field on
 * that handle returns these values; unknown handles use module defaults. */
SOBER_FIELDS_DEFAULT_API int sober_fields_default_bind_object(
    void *jobj, const SoberFieldsDefaultValues *v);

/* ---- verification counters / diagnostics -------------------------------- */
/* Per-field read counts.  name: "dpiScale" / "viewportWidthMm" /
 * "viewportHeightMm" / "GetFieldID" / NULL (= total field-value reads). */
SOBER_FIELDS_DEFAULT_API uint64_t sober_fields_default_calls(const char *name);
/* Times an installer successfully wrote the three slots. */
SOBER_FIELDS_DEFAULT_API uint64_t sober_fields_default_install_count(void);
/* Zero the counters (harness convenience). */
SOBER_FIELDS_DEFAULT_API void sober_fields_default_reset_counters(void);

/* One-line status dump (gate, defaults, bindings, counters); returns buf. */
SOBER_FIELDS_DEFAULT_API const char *sober_fields_default_describe(
    char *buf, size_t cap);

/* Built-in contract check: 0 on success, number of failed checks otherwise;
 * writes one result line into buf when non-NULL.  Checks slot arithmetic,
 * both vptr payload layouts and a GetFieldID -> Get*Field round-trip.  Never
 * writes guest memory and is safe in any gate state.  NOTE: the round-trip
 * increments the read counters (call reset_counters() afterwards if needed). */
SOBER_FIELDS_DEFAULT_API int sober_fields_default_selftest(
    char *buf, size_t cap);

#ifdef __cplusplus
}
#endif
#endif /* SOBER_JNI_FIELDS_DEFAULT_H */
