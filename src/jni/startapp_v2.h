/*
 * startapp_v2.h - corrected guest-native StartAppParams module (Team B / B1).
 *
 * NEW FILE.  Owned by worker B1.  Does not modify startapp_params.c/.h,
 * native_jni.c, guest_app_shell.c, CMakeLists.txt or the guest engine.
 *
 * WHY THIS EXISTS
 * ---------------
 * The existing `startapp_params` module registers all ten getters, but in the
 * real-path harness its handlers are overwritten: `guest_app_shell.c` calls
 * `sober_startapp_params_make()` and then registers the same names again with
 * `start_app_get` (surface/platformParams/vrContext) or `real_path_get` (all
 * ten).  `sober_jni_set_callback_handler()` replaces a handler by NAME, so the
 * LAST registration for a name wins.  In addition `real_path_get()` answers
 * `appUserId()J` with a jstring "1" instead of a 64-bit integer, and leaves
 * `isUnder13()Z` / `membershipType()I` as an untyped 0.
 *
 * This module fixes both defects:
 *
 *   1. ONE installer registers all ten getters (exact smali names/signatures)
 *      in a single call, and `sober_startapp_v2_reassert()` can be called
 *      immediately before guest RVA 0x26fe80f to guarantee our handlers are
 *      the last ones installed for those names - no shadowing.
 *   2. The three primitive getters return the exact JNI primitive widths the
 *      bridge casts to: appUserId -> int64_t (J), isUnder13 -> uint8_t (Z),
 *      membershipType -> int32_t (I).  Never a jstring, never a narrow/ptr.
 *   3. platformParams is a genuine guest-shaped 0x40-byte native object whose
 *      vptr is `guest_base + 0x6dbca20` (the vtable guest 0x26fe98f /
 *      0x26fec51 install), with the parser 0x26feb9b field layout.
 *
 * EVIDENCE (frozen engine /tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so,
 * BuildID 204ad049599c96afb75b8a21d5051063e821d6c9,
 * SHA256 3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335):
 *   - objdump: 0x26fe98f and 0x26fec51 both `lea 0x6dbca20(%rip),%rcx`.
 *   - objdump: parser 0x26feb9b writes +0x08 (float dpiScale), +0x0c/+0x10
 *     (int viewport mm), +0x18=1, +0x28=0; name strings at 0x535292
 *     ("dpiScale"), 0x2c22da, 0x3897f6 (verified raw bytes).
 *   - analysis/lua-app-start.md §5: exact getter signatures and the three
 *     primitive-width mismatches; analysis/native-object-layouts.md §2/§3:
 *     PlatformParams layout and the negative-offset A/B/C backing graph.
 *   - analysis/native-object-layouts.md §1: helper 0x2e5d420 reads
 *     *(jobject), *(ptr-0x18) and [jobject+disp+0x20]/+0x88.
 *
 * Every vptr published here is `guest_base` + a genuine guest RVA.  No host
 * stub vtables, no guest byte patches, no fabricated callbacks, and the
 * decision-013 marker is never touched.  Default OFF (opt-in).
 *
 * ONE-LINE HOOK FOR THE LEAD
 * --------------------------
 * Replace the sparse object + the `start_app_get` / `real_path_get` block in
 * guest_app_shell.c with:
 *
 *     void *params = sober_startapp_v2_make(j, (void *)base);
 *     ...                       (any other handler registration for these
 *                                ten names may safely happen here)
 *     sober_startapp_v2_reassert(j);   // last-write-wins: kills shadowing
 *     rc = ((long (*)(void*,void*,void*))(base + 0x26fe80fu))(env, app_class, params);
 *
 * `sober_startapp_v2_make()` is a no-op returning NULL unless the opt-in env
 * var is set.  `reassert()` is also opt-in-aware and idempotent.  The lead must
 * add SOBER_STARTAPP_V2 to the decision-013 withhold list alongside the other
 * opt-ins (the legacy alias SOBER_STARTAPP_PARAMS is already in that list).
 */
#ifndef SOBER_JNI_STARTAPP_V2_H
#define SOBER_JNI_STARTAPP_V2_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SoberJni SoberJni;   /* native_jni.h is the full definition */

/* ---- opt-in ------------------------------------------------------------ */
/* Default OFF.  Enabled when getenv("SOBER_STARTAPP_V2") == "1"; the legacy
 * alias getenv("SOBER_STARTAPP_PARAMS") == "1" is also accepted so an existing
 * harness configuration keeps working after the module swap. */
#define SOBER_STARTAPP_V2_ENV  "SOBER_STARTAPP_V2"
#define SOBER_STARTAPP_V2_ENV_LEGACY "SOBER_STARTAPP_PARAMS"

int         sober_startapp_v2_enabled(void);
const char *sober_startapp_v2_env_vars(void);

/* ---- exact smali names / signatures (AutoValue_StartAppParams) ---------- */
#define SOBER_STARTAPP_V2_GET_SURFACE       "surface"
#define SOBER_STARTAPP_V2_GET_PLATFORM      "platformParams"
#define SOBER_STARTAPP_V2_GET_PLACE         "appStarterPlace"
#define SOBER_STARTAPP_V2_GET_SCRIPT        "appStarterScript"
#define SOBER_STARTAPP_V2_GET_USER_ID       "appUserId"
#define SOBER_STARTAPP_V2_GET_UNDER13       "isUnder13"
#define SOBER_STARTAPP_V2_GET_USERNAME      "username"
#define SOBER_STARTAPP_V2_GET_MEMBERSHIP    "membershipType"
#define SOBER_STARTAPP_V2_GET_THEME         "selectedTheme"
#define SOBER_STARTAPP_V2_GET_VR_CONTEXT    "vrContext"

#define SOBER_STARTAPP_V2_SIG_SURFACE       "()Landroid/view/Surface;"
#define SOBER_STARTAPP_V2_SIG_PLATFORM      "()Lcom/roblox/engine/jni/model/PlatformParams;"
#define SOBER_STARTAPP_V2_SIG_PLACE         "()Ljava/lang/String;"
#define SOBER_STARTAPP_V2_SIG_SCRIPT        "()Ljava/lang/String;"
#define SOBER_STARTAPP_V2_SIG_USER_ID       "()J"
#define SOBER_STARTAPP_V2_SIG_UNDER13       "()Z"
#define SOBER_STARTAPP_V2_SIG_USERNAME      "()Ljava/lang/String;"
#define SOBER_STARTAPP_V2_SIG_MEMBERSHIP    "()I"
#define SOBER_STARTAPP_V2_SIG_THEME         "()Ljava/lang/String;"
#define SOBER_STARTAPP_V2_SIG_VR_CONTEXT    "()Landroid/app/Activity;"

#define SOBER_STARTAPP_V2_GETTER_COUNT 10

/* Primitive JNI type tag returned by sober_startapp_v2_getter_primitive():
 * 'J' = jlong (int64_t), 'Z' = jboolean (uint8_t), 'I' = jint (int32_t),
 * 0 = object-returning getter. */
#define SOBER_STARTAPP_V2_TYPE_LONG     'J'
#define SOBER_STARTAPP_V2_TYPE_BOOLEAN  'Z'
#define SOBER_STARTAPP_V2_TYPE_INT      'I'
#define SOBER_STARTAPP_V2_TYPE_OBJECT   0

/* ---- genuine guest RVAs (BuildID 204ad049 / SHA 3e498d6494...) ---------- */
#define SOBER_STARTAPP_V2_VT_PLATFORM   0x6dbca20u  /* lea @ 0x26fe98f/0x26fec51 */
#define SOBER_STARTAPP_V2_VT_STRING     0x6ed8d00u  /* guest string header @ 0x2315532 */
#define SOBER_STARTAPP_V2_BUILDID32     0x204ad049u

/* Native PlatformParams field window (parser 0x26feb9b, 0x40-byte object). */
#define SOBER_STARTAPP_V2_PLATFORM_VPTR      0x00u
#define SOBER_STARTAPP_V2_PLATFORM_DPI       0x08u  /* float  */
#define SOBER_STARTAPP_V2_PLATFORM_WIDTH_MM  0x0cu  /* int32  */
#define SOBER_STARTAPP_V2_PLATFORM_HEIGHT_MM 0x10u  /* int32  */
#define SOBER_STARTAPP_V2_PLATFORM_FLAG1     0x18u  /* = 1    */
#define SOBER_STARTAPP_V2_PLATFORM_FLAG2     0x28u  /* = 0    */
#define SOBER_STARTAPP_V2_PLATFORM_SIZE      0x40u

/* ---- values ------------------------------------------------------------ */
typedef struct SoberStartAppV2Values {
    /* Canonical Java class names of the object-valued getters.  NULL = use the
     * smali names.  No host stub vtable is ever installed for these. */
    const char *surface_class;      /* default "android/view/Surface" */
    const char *platform_class;     /* default "com/roblox/engine/jni/model/PlatformParams" */
    const char *vr_context_class;   /* default "android/app/Activity" */

    /* Java literals from uh/p.b() (decoded APK). */
    const char *app_starter_place;  /* default "rbxasset://places/Mobile.rbxl" */
    const char *app_starter_script; /* default "LuaAppStarterScript" */

    /* Three PRIMITIVE getters - note the exact JNI widths. */
    int64_t     app_user_id;        /* J : default -1 (logged-out, dk/c.smali) */
    uint8_t     is_under_13;        /* Z : default 1  (true, dk/c.smali) */
    int32_t     membership_type;    /* I : default 0  (dk/c.smali) */

    const char *username;           /* default "" (decoded default) */
    const char *selected_theme;     /* default "Light" (ml/a.smali) */

    /* PlatformParams native fields read directly by parser 0x26feb9b. */
    float       dpi_scale;          /* default 1.0f */
    int32_t     viewport_width_mm;  /* default 0 */
    int32_t     viewport_height_mm; /* default 0 */
} SoberStartAppV2Values;

/* Fill `v` with the decoded-APK defaults above. */
void sober_startapp_v2_defaults(SoberStartAppV2Values *v);

/* ---- lead-facing API --------------------------------------------------- */
/* Enabled + defaults + env overrides + register ALL TEN getters + build the
 * shaped object.  Returns the StartAppParams jobject for guest 0x26fe80f, or
 * NULL when disabled / on allocation failure. */
void *sober_startapp_v2_make(SoberJni *j, void *guest_base);
void *sober_startapp_v2_make_ex(SoberJni *j, void *guest_base,
                                const SoberStartAppV2Values *values);

/* Re-register the ten getters with our handler.  Because the bridge replaces
 * handlers by name, calling this LAST (e.g. right before guest 0x26fe80f)
 * guarantees no earlier or later `start_app_get`/`real_path_get` registration
 * can shadow surface/vrContext.  Opt-in aware; idempotent; SOBER_JNI_OK/ERR.
 * Returns -2 when disabled. */
int   sober_startapp_v2_reassert(SoberJni *j);

/* Split primitives for callers that build their own object. */
int   sober_startapp_v2_register(SoberJni *j, void *guest_base,
                                 const SoberStartAppV2Values *values);
void *sober_startapp_v2_build(SoberJni *j, void *guest_base,
                              const SoberStartAppV2Values *values);

/* Introspection: exact signature and primitive tag for a getter name. */
const char *sober_startapp_v2_getter_sig(const char *name);
char        sober_startapp_v2_getter_primitive(const char *name);

/* ---- diagnostics ------------------------------------------------------- */
const char *sober_startapp_v2_describe(char *buf, size_t cap);
uint64_t    sober_startapp_v2_getter_calls(const char *name);

#ifdef __cplusplus
}
#endif
#endif /* SOBER_JNI_STARTAPP_V2_H */
