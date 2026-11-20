/*
 * startapp_params.h - faithful guest-native StartAppParams object (task #469).
 *
 * Owner: params.  Scope: this file + startapp_params.c only (NEW files).
 *
 * WHAT THIS PROVIDES
 * ------------------
 * The real Android app builds StartAppParams in `uh/f.G()` (smali:
 * StartAppParams$Builder / AutoValue_StartAppParams) and passes the result to
 * `nativeAppBridgeV2StartAppWithParams` (guest RVA 0x26fe80f).  The ten getter
 * names/signatures below are taken verbatim from that smali:
 *
 *   surface()Landroid/view/Surface;
 *   platformParams()Lcom/roblox/engine/jni/model/PlatformParams;
 *   appStarterPlace()Ljava/lang/String;
 *   appStarterScript()Ljava/lang/String;
 *   appUserId()J
 *   isUnder13()Z
 *   username()Ljava/lang/String;
 *   membershipType()I
 *   selectedTheme()Ljava/lang/String;
 *   vrContext()Landroid/app/Activity;
 *
 * This module builds ONE shaped jobject carrying all ten values, registers a
 * getter handler for every one of them (with the exact signature above), and
 * gives `platformParams` a real guest-shaped native C++ backing: a 0x40-byte
 * object whose vptr is the GENUINE guest PlatformParams vtable at
 * guest_base+0x6dbca20 (the one 0x26fe98f/0x26fec51 install), with dpiScale at
 * +0x08 and viewport mm at +0x0c/+0x10 (parser 0x26feb9b).  Every vptr we
 * publish is a guest RVA plus the mapped base: no host stub vtables, no guest
 * byte patches, no callbacks/gate changes.
 *
 * Default field values are the literals the Java builder uses
 * (`uh/p.b()`: appStarterPlace = "rbxasset://places/Mobile.rbxl",
 * appStarterScript = "LuaAppStarterScript"); dynamic account values use
 * neutral stand-ins and can be overridden through the explicit API or env.
 *
 * Opt-in: nothing is built or registered unless SOBER_STARTAPP_PARAMS=1.
 */
#ifndef SOBER_JNI_STARTAPP_PARAMS_H
#define SOBER_JNI_STARTAPP_PARAMS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SoberJni SoberJni;   /* native_jni.h is the full definition */

/* ---- opt-in ------------------------------------------------------------ */
/* Default OFF.  Enabled only when getenv("SOBER_STARTAPP_PARAMS") == "1". */
int         sober_startapp_params_enabled(void);
/* Human-readable list of the env vars this module reads. */
const char *sober_startapp_params_env_vars(void);

/* Exact getter names / signatures as recovered from the APK smali. */
#define SOBER_STARTAPP_GET_SURFACE       "surface"
#define SOBER_STARTAPP_GET_PLATFORM      "platformParams"
#define SOBER_STARTAPP_GET_PLACE         "appStarterPlace"
#define SOBER_STARTAPP_GET_SCRIPT        "appStarterScript"
#define SOBER_STARTAPP_GET_USER_ID       "appUserId"
#define SOBER_STARTAPP_GET_UNDER13       "isUnder13"
#define SOBER_STARTAPP_GET_USERNAME      "username"
#define SOBER_STARTAPP_GET_MEMBERSHIP    "membershipType"
#define SOBER_STARTAPP_GET_THEME         "selectedTheme"
#define SOBER_STARTAPP_GET_VR_CONTEXT    "vrContext"

#define SOBER_STARTAPP_SIG_SURFACE       "()Landroid/view/Surface;"
#define SOBER_STARTAPP_SIG_PLATFORM      "()Lcom/roblox/engine/jni/model/PlatformParams;"
#define SOBER_STARTAPP_SIG_PLACE         "()Ljava/lang/String;"
#define SOBER_STARTAPP_SIG_SCRIPT        "()Ljava/lang/String;"
#define SOBER_STARTAPP_SIG_USER_ID       "()J"
#define SOBER_STARTAPP_SIG_UNDER13       "()Z"
#define SOBER_STARTAPP_SIG_USERNAME      "()Ljava/lang/String;"
#define SOBER_STARTAPP_SIG_MEMBERSHIP    "()I"
#define SOBER_STARTAPP_SIG_THEME         "()Ljava/lang/String;"
#define SOBER_STARTAPP_SIG_VR_CONTEXT    "()Landroid/app/Activity;"

#define SOBER_STARTAPP_GETTER_COUNT 10

/* Genuine guest vtable RVAs (BuildID 204ad049 / SHA 3e498d6494...). */
#define SOBER_STARTAPP_VT_RVA_PLATFORM_START 0x6dbca20u  /* 0x26fe98f, 0x26fec51 */
#define SOBER_STARTAPP_VT_RVA_PLATFORM_UPD   0x6dbcb20u  /* 0x277328f */
#define SOBER_STARTAPP_VT_RVA_STRING         0x6ed8d00u  /* 0x2315532 header     */
#define SOBER_STARTAPP_VT_RVA_LIST           0x6db3ec0u  /* 0x2e5baa0            */
#define SOBER_STARTAPP_VT_RVA_DELEGATE       0x6e047b0u  /* 0x24aed0f            */

/* Native PlatformParams field window (parser 0x26feb9b). */
#define SOBER_STARTAPP_PLATFORM_VPTR      0x00u
#define SOBER_STARTAPP_PLATFORM_DPI       0x08u   /* float  */
#define SOBER_STARTAPP_PLATFORM_WIDTH_MM  0x0cu   /* int32  */
#define SOBER_STARTAPP_PLATFORM_HEIGHT_MM 0x10u   /* int32  */
#define SOBER_STARTAPP_PLATFORM_FLAG1     0x18u   /* = 1    */
#define SOBER_STARTAPP_PLATFORM_FLAG2     0x28u   /* = 0    */
#define SOBER_STARTAPP_PLATFORM_SIZE      0x40u

/* Build identity guard (informational; matches jni_json_bridge.h). */
#define SOBER_STARTAPP_BUILDID32 0x204ad049u

/* ---- values ------------------------------------------------------------ */
typedef struct SoberStartAppParamsValues {
    /* Java literal values from uh/p.b(). */
    const char *app_starter_place;   /* default "rbxasset://places/Mobile.rbxl" */
    const char *app_starter_script;  /* default "LuaAppStarterScript"          */

    /* Dynamic account values (Ldk/c.c() in the APK); neutral stand-ins here. */
    int64_t     app_user_id;         /* default 1    */
    int         is_under_13;         /* default 0    */
    const char *username;            /* default "SoberUser" */
    int32_t     membership_type;     /* default 0    */
    const char *selected_theme;      /* default "default" */

    /* PlatformParams native fields read directly by 0x26feb9b. */
    float       dpi_scale;           /* default 1.0f */
    int32_t     viewport_width_mm;
    int32_t     viewport_height_mm;

    /* Java class names of the object-valued getters.  Leave NULL for the
     * canonical names; no host stub vtable is ever installed for these. */
    const char *surface_class;
    const char *platform_class;
    const char *vr_context_class;
} SoberStartAppParamsValues;

/* Fill `v` with the Java-builder defaults above. */
void sober_startapp_params_defaults(SoberStartAppParamsValues *v);

/* ---- lead-facing API --------------------------------------------------- *
 * ONE-LINE HOOK FOR THE LEAD (replaces the sparse object + three getters at
 * guest_app_shell.c around line 1150):
 *
 *   void *params = sober_startapp_params_make(j, base);
 *
 * It is a no-op (returns NULL) unless SOBER_STARTAPP_PARAMS=1.  When enabled it
 * registers all ten getters and returns the shaped StartAppParams jobject to
 * pass as the 3rd argument of guest 0x26fe80f.  Call it instead of, not after,
 * an existing `start_app_get` / `real_path_get` registration for these ten
 * names (the bridge dispatches the first registered handler for a name).
 * ------------------------------------------------------------------------ */

/* Enabled + defaults + env overrides + register handlers + build object.
 * Returns the shaped jobject, or NULL when disabled / on allocation failure. */
void *sober_startapp_params_make(SoberJni *j, void *guest_base);

/* Same, with explicit values (still requires the opt-in env var). */
void *sober_startapp_params_make_ex(SoberJni *j, void *guest_base,
                                    const SoberStartAppParamsValues *values);

/* Split primitives, for callers that build their own object:
 *   install getters only  -> SOBER_JNI_OK / SOBER_JNI_ERR / -2 disabled
 *   build object only     -> shaped jobject or NULL (also needs guest_base,
 *                            because every vptr must be a genuine guest RVA) */
int   sober_startapp_params_register(SoberJni *j, void *guest_base,
                                     const SoberStartAppParamsValues *values);
void *sober_startapp_params_build(SoberJni *j, void *guest_base,
                                  const SoberStartAppParamsValues *values);

/* ---- diagnostics ------------------------------------------------------- */
const char *sober_startapp_params_describe(char *buf, size_t cap);
uint64_t    sober_startapp_params_getter_calls(const char *name);

#ifdef __cplusplus
}
#endif
#endif /* SOBER_JNI_STARTAPP_PARAMS_H */
