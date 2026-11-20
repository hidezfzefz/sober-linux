/*
 * game_params.h - faithful guest-native StartGameParams object (task #473).
 *
 * Owner: gamefault.  Scope: this file + game_params.c only (NEW files).
 *
 * WHAT THIS PROVIDES
 * ------------------
 * The real Android app builds StartGameParams in `uh/j0.C(Luh/m0;)V`
 * (decoded smali at /tmp/opencode/mini/out/smali/uh/j0.smali lines 360-595)
 * via `StartGameParams.builder()` / `AutoValue_StartGameParams` and passes it
 * to `nativeAppBridgeV2StartGameWithParam` (guest RVA 0x2ebd51a).  The 24
 * getter names/signatures below are verbatim from
 * AutoValue_StartGameParams.smali.  The guest fetches all of them in
 * 0x2ebd51a..0x2ebd8ea (getter-name literal xrefs: surface, userId, gameId,
 * accessCode, linkCode, username, referralPage, launchData, joinAttemptId,
 * joinAttemptOrigin, conversationId, isUnder13, reservedServerAccessCode,
 * callId, isoContext, referredByPlayerId, gameJoinContext, eventId,
 * gameIdToExclude, placeId, platformParams, joinRequestType, vrContext).
 *
 * The current harness passes a 0x100-byte opaque object with only FOUR string
 * getters (gameId/placeId/accessCode/username), so 20 getters silently return
 * NULL/0 and the guest's native StartGameParams mirror is only partially
 * populated.  This module registers all 24 with exact signatures and builds
 * one SrObject-compatible, guest-shaped jobject whose native slot window and
 * PlatformParams backing use GENUINE guest vtable RVAs (BuildID 204ad049,
 * SHA 3e498d6494...).  No guest byte patches, no host stub vtables, no
 * guest->Java callbacks, no decision-013 marker changes.
 *
 * Opt-in: nothing is built or registered unless SOBER_GAME_PARAMS=1.
 */
#ifndef SOBER_JNI_GAME_PARAMS_H
#define SOBER_JNI_GAME_PARAMS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SoberJni SoberJni;   /* native_jni.h is the full definition */

/* ---- opt-in ------------------------------------------------------------ */
/* Default OFF.  Enabled only when getenv("SOBER_GAME_PARAMS") == "1". */
int         sober_game_params_enabled(void);
/* Human-readable list of the env vars this module reads. */
const char *sober_game_params_env_vars(void);

/* Exact getter names / signatures recovered from the APK smali. */
#define SOBER_GAME_GET_SURFACE            "surface"
#define SOBER_GAME_GET_PLATFORM           "platformParams"
#define SOBER_GAME_GET_DEVICE             "deviceParams"
#define SOBER_GAME_GET_PLACE_ID           "placeId"
#define SOBER_GAME_GET_USER_ID            "userId"
#define SOBER_GAME_GET_ACCESS_CODE        "accessCode"
#define SOBER_GAME_GET_LINK_CODE          "linkCode"
#define SOBER_GAME_GET_GAME_ID            "gameId"
#define SOBER_GAME_GET_UNDER13            "isUnder13"
#define SOBER_GAME_GET_USERNAME           "username"
#define SOBER_GAME_GET_CONVERSATION_ID    "conversationId"
#define SOBER_GAME_GET_RESERVED_CODE      "reservedServerAccessCode"
#define SOBER_GAME_GET_CALL_ID            "callId"
#define SOBER_GAME_GET_JOIN_REQUEST_TYPE  "joinRequestType"
#define SOBER_GAME_GET_REFERRAL_PAGE      "referralPage"
#define SOBER_GAME_GET_LAUNCH_DATA        "launchData"
#define SOBER_GAME_GET_JOIN_CONTEXT       "gameJoinContext"
#define SOBER_GAME_GET_EVENT_ID           "eventId"
#define SOBER_GAME_GET_JOIN_ATTEMPT_ID    "joinAttemptId"
#define SOBER_GAME_GET_JOIN_ATTEMPT_ORIG  "joinAttemptOrigin"
#define SOBER_GAME_GET_ISO_CONTEXT        "isoContext"
#define SOBER_GAME_GET_REFERRED_BY        "referredByPlayerId"
#define SOBER_GAME_GET_GAME_ID_EXCLUDE    "gameIdToExclude"
#define SOBER_GAME_GET_VR_CONTEXT         "vrContext"

#define SOBER_GAME_SIG_SURFACE            "()Landroid/view/Surface;"
#define SOBER_GAME_SIG_PLATFORM           "()Lcom/roblox/engine/jni/model/PlatformParams;"
#define SOBER_GAME_SIG_DEVICE             "()Lcom/roblox/engine/jni/model/DeviceParams;"
#define SOBER_GAME_SIG_PLACE_ID           "()J"
#define SOBER_GAME_SIG_USER_ID            "()J"
#define SOBER_GAME_SIG_ACCESS_CODE        "()Ljava/lang/String;"
#define SOBER_GAME_SIG_LINK_CODE          "()Ljava/lang/String;"
#define SOBER_GAME_SIG_GAME_ID            "()Ljava/lang/String;"
#define SOBER_GAME_SIG_UNDER13            "()Z"
#define SOBER_GAME_SIG_USERNAME           "()Ljava/lang/String;"
#define SOBER_GAME_SIG_CONVERSATION_ID    "()J"
#define SOBER_GAME_SIG_RESERVED_CODE      "()Ljava/lang/String;"
#define SOBER_GAME_SIG_CALL_ID            "()Ljava/lang/String;"
#define SOBER_GAME_SIG_JOIN_REQUEST_TYPE  "()I"
#define SOBER_GAME_SIG_REFERRAL_PAGE      "()Ljava/lang/String;"
#define SOBER_GAME_SIG_LAUNCH_DATA        "()Ljava/lang/String;"
#define SOBER_GAME_SIG_JOIN_CONTEXT       "()Ljava/lang/String;"
#define SOBER_GAME_SIG_EVENT_ID           "()Ljava/lang/String;"
#define SOBER_GAME_SIG_JOIN_ATTEMPT_ID    "()Ljava/lang/String;"
#define SOBER_GAME_SIG_JOIN_ATTEMPT_ORIG  "()Ljava/lang/String;"
#define SOBER_GAME_SIG_ISO_CONTEXT        "()Ljava/lang/String;"
#define SOBER_GAME_SIG_REFERRED_BY        "()J"
#define SOBER_GAME_SIG_GAME_ID_EXCLUDE    "()Ljava/lang/String;"
#define SOBER_GAME_SIG_VR_CONTEXT         "()Landroid/app/Activity;"

#define SOBER_GAME_PARAMS_GETTER_COUNT 24

/* Genuine guest vtable RVAs (BuildID 204ad049 / SHA 3e498d6494...).  Same
 * frozen set documented in startapp_params.h / jni_native_objects.h; every
 * published vptr is guest_base + one of these, never a host function. */
#define SOBER_GAME_VT_RVA_STRING    0x6ed8d00u  /* 0x2315532 string header */
#define SOBER_GAME_VT_RVA_PLATFORM  0x6dbca20u  /* 0x26fe98f/0x26fec51 */
#define SOBER_GAME_VT_RVA_PLATFORM_UPD 0x6dbcb20u
#define SOBER_GAME_VT_RVA_LIST      0x6db3ec0u
#define SOBER_GAME_VT_RVA_DELEGATE  0x6e047b0u

/* Native PlatformParams window (guest parser 0x26feb9b), reused verbatim. */
#define SOBER_GAME_PLATFORM_VPTR       0x00u
#define SOBER_GAME_PLATFORM_DPI        0x08u
#define SOBER_GAME_PLATFORM_WIDTH_MM   0x0cu
#define SOBER_GAME_PLATFORM_HEIGHT_MM  0x10u
#define SOBER_GAME_PLATFORM_FLAG1      0x18u
#define SOBER_GAME_PLATFORM_FLAG2      0x28u
#define SOBER_GAME_PLATFORM_SIZE       0x40u

#define SOBER_GAME_BUILDID32 0x204ad049u

/* ---- values ------------------------------------------------------------ */
typedef struct SoberGameParamsValues {
    /* Object-valued getters (Java class names; NULL = canonical name). */
    const char *surface_class;
    const char *platform_class;
    const char *device_class;
    const char *vr_context_class;

    /* Strings.  Defaults: the StartGameParams.builder() literals ("") for
     * optional fields; the harness-neutral values for gameId/username. */
    const char *access_code;             /* default "" */
    const char *link_code;               /* default "" */
    const char *game_id;                 /* default "123456789" */
    const char *username;                /* default "SoberUser" */
    const char *reserved_server_code;    /* default "" */
    const char *call_id;                 /* default "" */
    const char *referral_page;           /* default "" */
    const char *launch_data;             /* default "" */
    const char *game_join_context;       /* default "" */
    const char *event_id;                /* default "" */
    const char *join_attempt_id;         /* default "" */
    const char *join_attempt_origin;     /* default "" */
    const char *iso_context;             /* default "" */
    const char *game_id_to_exclude;      /* default "" */

    /* Scalars. */
    int64_t  place_id;                   /* default 123456789 */
    int64_t  user_id;                    /* default 1 */
    int      is_under_13;                /* default 0 */
    int64_t  conversation_id;            /* default 0 */
    int32_t  join_request_type;          /* default 0 */
    int64_t  referred_by_player_id;      /* default 0 */

    /* PlatformParams native fields (parser 0x26feb9b). */
    float    dpi_scale;                  /* default 1.0f */
    int32_t  viewport_width_mm;
    int32_t  viewport_height_mm;
} SoberGameParamsValues;

/* Fill `v` with the Java-builder / neutral defaults above. */
void sober_game_params_defaults(SoberGameParamsValues *v);

/* ---- lead-facing API --------------------------------------------------- *
 * ONE-LINE HOOK FOR THE LEAD (replaces the opaque object + four getters at
 * guest_app_shell.c around line 1239):
 *
 *   void *sg = sober_game_params_make(j, base);
 *   rc = ((long (*)(void*,void*,void*))(base + 0x2ebd51au))(env, app_class, sg);
 *
 * It is a no-op (returns NULL) unless SOBER_GAME_PARAMS=1.  When enabled it
 * registers all 24 getters with their exact smali signatures and returns the
 * shaped StartGameParams jobject to pass as the 3rd argument of guest
 * 0x2ebd51a.  Register it INSTEAD OF the four hand-written gameId/placeId/
 * accessCode/username handlers (the bridge dispatches the first handler
 * registered for a name).
 * ------------------------------------------------------------------------ */

/* Enabled + defaults + env overrides + register handlers + build object.
 * Returns the shaped jobject, or NULL when disabled / on allocation failure. */
void *sober_game_params_make(SoberJni *j, void *guest_base);

/* Same, with explicit values (still requires the opt-in env var). */
void *sober_game_params_make_ex(SoberJni *j, void *guest_base,
                                const SoberGameParamsValues *values);

/* Split primitives, for callers that build their own object:
 *   install getters only  -> SOBER_JNI_OK / SOBER_JNI_ERR / -2 disabled
 *   build object only     -> shaped jobject or NULL (also needs guest_base,
 *                            because every vptr must be a genuine guest RVA) */
int   sober_game_params_register(SoberJni *j, void *guest_base,
                                 const SoberGameParamsValues *values);
void *sober_game_params_build(SoberJni *j, void *guest_base,
                              const SoberGameParamsValues *values);

/* ---- diagnostics ------------------------------------------------------- */
const char *sober_game_params_describe(char *buf, size_t cap);
uint64_t    sober_game_params_getter_calls(const char *name);

#ifdef __cplusplus
}
#endif
#endif /* SOBER_JNI_GAME_PARAMS_H */
