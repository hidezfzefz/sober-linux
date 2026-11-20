/*
 * startgame_v2.h - corrected 24-getter StartGameParams module (team B2).
 *
 * Owner: worker B2.  Scope: this file + startgame_v2.c only (NEW files).
 *
 * WHY THIS EXISTS
 * ---------------
 * The first 24-getter module (game_params.c) is consumed by the guest but the
 * game-load path still faults at guest 0x31a1360.  That fault's real cause is
 * an app+0x488 NULL owned by another worker; this module deliberately does NOT
 * touch that field, the gate, the engine, or any shared source file.  What it
 * does fix, in this module's own scope, are the four client-visible defects of
 * the earlier wiring:
 *
 *   1. ALL 24 getters are claimed, with the exact AutoValue_StartGameParams
 *      smali names/signatures (see below), so the earlier four ad-hoc handlers
 *      (`gameId`, `placeId`, `accessCode`, `username`) cannot be the ones the
 *      bridge dispatches.  Registration is re-assertable (`reclaim`) and the
 *      bridge's set_callback_handler() replace-by-name semantics are relied on
 *      so no stale handler shadows ours.  Native-method shadowing is detected
 *      and reported by verify().
 *   2. Primitive J/Z/I returns are real machine integers, not jobject handles.
 *      In particular placeId()/userId()/conversationId()/referredByPlayerId()
 *      return full 64-bit jlong, isUnder13() returns 0/1 jboolean, and
 *      joinRequestType() returns a sign-correct 32-bit jint.  The earlier
 *      harness handler returned a jstring handle for placeId(), which the
 *      guest consumed as a bogus jlong.
 *   3. deviceParams() is genuinely NULL: the builder calls setDeviceParams(null)
 *      (uh/j0.smali:411-413).  This module NEVER allocates or publishes a
 *      DeviceParams object, has no device-class override, and stores a NULL in
 *      the mirror's device slot.  (The guest's 23 getter-name xrefs at
 *      0x2ebd51a do not include deviceParams; if a caller nevertheless issues a
 *      CallObjectMethod for it, native_jni.c's object-return reshaping - not
 *      this module - is what turns a NULL return into a synthetic object.)
 *   4. platformParams() is guest-shaped: a 0x40-byte native window whose vptr
 *      is guest_base + 0x6dbca20, the genuine PlatformParams vtable installed
 *      by 0x26fe98f/0x26fec51 and parsed by 0x26feb9b; dpi/mm fields as in
 *      startapp_params.h.  No host stub vtables, no guest byte patches, no
 *      callbacks emitted, no decision-013 marker touched.
 *
 * Evidence: engine BuildID 204ad049599c96afb75b8a21d5051063e821d6c9,
 * SHA256 3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335.
 * Getter table verified verbatim against
 * /tmp/opencode/mini/out/smali/com/roblox/engine/jni/autovalue/
 * AutoValue_StartGameParams.smali (24 public getters) and the guest JNI entry
 * nativeAppBridgeV2StartGameWithParam (0x2ebd51a).
 *
 * Opt-in: nothing is registered or built unless SOBER_STARTGAME_V2=1.
 */
#ifndef SOBER_JNI_STARTGAME_V2_H
#define SOBER_JNI_STARTGAME_V2_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SoberJni SoberJni;   /* native_jni.h is the full definition */

/* ---- opt-in ------------------------------------------------------------ */
/* Default OFF.  Enabled only when getenv("SOBER_STARTGAME_V2") == "1".
 * This knob is separate from SOBER_GAME_PARAMS / SOBER_GAME_LOADED_PATH so the
 * lead can A/B the corrected module without touching the old ones. */
int         sober_startgame_v2_enabled(void);
const char *sober_startgame_v2_env_vars(void);

/* ---- exact smali getter names / signatures ----------------------------- */
#define SOBER_SGV2_GET_SURFACE            "surface"
#define SOBER_SGV2_GET_PLATFORM           "platformParams"
#define SOBER_SGV2_GET_DEVICE             "deviceParams"
#define SOBER_SGV2_GET_PLACE_ID           "placeId"
#define SOBER_SGV2_GET_USER_ID            "userId"
#define SOBER_SGV2_GET_ACCESS_CODE        "accessCode"
#define SOBER_SGV2_GET_LINK_CODE          "linkCode"
#define SOBER_SGV2_GET_GAME_ID            "gameId"
#define SOBER_SGV2_GET_UNDER13            "isUnder13"
#define SOBER_SGV2_GET_USERNAME           "username"
#define SOBER_SGV2_GET_CONVERSATION_ID    "conversationId"
#define SOBER_SGV2_GET_RESERVED_CODE      "reservedServerAccessCode"
#define SOBER_SGV2_GET_CALL_ID            "callId"
#define SOBER_SGV2_GET_JOIN_REQUEST_TYPE  "joinRequestType"
#define SOBER_SGV2_GET_REFERRAL_PAGE      "referralPage"
#define SOBER_SGV2_GET_LAUNCH_DATA        "launchData"
#define SOBER_SGV2_GET_JOIN_CONTEXT       "gameJoinContext"
#define SOBER_SGV2_GET_EVENT_ID           "eventId"
#define SOBER_SGV2_GET_JOIN_ATTEMPT_ID    "joinAttemptId"
#define SOBER_SGV2_GET_JOIN_ATTEMPT_ORIG  "joinAttemptOrigin"
#define SOBER_SGV2_GET_ISO_CONTEXT        "isoContext"
#define SOBER_SGV2_GET_REFERRED_BY        "referredByPlayerId"
#define SOBER_SGV2_GET_GAME_ID_EXCLUDE    "gameIdToExclude"
#define SOBER_SGV2_GET_VR_CONTEXT         "vrContext"

#define SOBER_SGV2_SIG_SURFACE            "()Landroid/view/Surface;"
#define SOBER_SGV2_SIG_PLATFORM           "()Lcom/roblox/engine/jni/model/PlatformParams;"
#define SOBER_SGV2_SIG_DEVICE             "()Lcom/roblox/engine/jni/model/DeviceParams;"
#define SOBER_SGV2_SIG_PLACE_ID           "()J"
#define SOBER_SGV2_SIG_USER_ID            "()J"
#define SOBER_SGV2_SIG_ACCESS_CODE        "()Ljava/lang/String;"
#define SOBER_SGV2_SIG_LINK_CODE          "()Ljava/lang/String;"
#define SOBER_SGV2_SIG_GAME_ID            "()Ljava/lang/String;"
#define SOBER_SGV2_SIG_UNDER13            "()Z"
#define SOBER_SGV2_SIG_USERNAME           "()Ljava/lang/String;"
#define SOBER_SGV2_SIG_CONVERSATION_ID    "()J"
#define SOBER_SGV2_SIG_RESERVED_CODE      "()Ljava/lang/String;"
#define SOBER_SGV2_SIG_CALL_ID            "()Ljava/lang/String;"
#define SOBER_SGV2_SIG_JOIN_REQUEST_TYPE  "()I"
#define SOBER_SGV2_SIG_REFERRAL_PAGE      "()Ljava/lang/String;"
#define SOBER_SGV2_SIG_LAUNCH_DATA        "()Ljava/lang/String;"
#define SOBER_SGV2_SIG_JOIN_CONTEXT       "()Ljava/lang/String;"
#define SOBER_SGV2_SIG_EVENT_ID           "()Ljava/lang/String;"
#define SOBER_SGV2_SIG_JOIN_ATTEMPT_ID    "()Ljava/lang/String;"
#define SOBER_SGV2_SIG_JOIN_ATTEMPT_ORIG  "()Ljava/lang/String;"
#define SOBER_SGV2_SIG_ISO_CONTEXT        "()Ljava/lang/String;"
#define SOBER_SGV2_SIG_REFERRED_BY        "()J"
#define SOBER_SGV2_SIG_GAME_ID_EXCLUDE    "()Ljava/lang/String;"
#define SOBER_SGV2_SIG_VR_CONTEXT         "()Landroid/app/Activity;"

#define SOBER_SGV2_GETTER_COUNT 24

/* Genuine guest vtable RVAs (BuildID 204ad049 / SHA 3e498d6494...).  Every
 * published vptr is guest_base + one of these; never a host function. */
#define SOBER_SGV2_VT_RVA_STRING     0x6ed8d00u  /* 0x2315532 string header */
#define SOBER_SGV2_VT_RVA_PLATFORM   0x6dbca20u  /* 0x26fe98f/0x26fec51 */
#define SOBER_SGV2_VT_RVA_PLATFORM_UP 0x6dbcb20u
#define SOBER_SGV2_VT_RVA_LIST       0x6db3ec0u
#define SOBER_SGV2_VT_RVA_DELEGATE   0x6e047b0u

/* Native PlatformParams window (guest parser 0x26feb9b). */
#define SOBER_SGV2_PLATFORM_VPTR      0x00u
#define SOBER_SGV2_PLATFORM_DPI       0x08u
#define SOBER_SGV2_PLATFORM_WIDTH_MM  0x0cu
#define SOBER_SGV2_PLATFORM_HEIGHT_MM 0x10u
#define SOBER_SGV2_PLATFORM_FLAG1     0x18u
#define SOBER_SGV2_PLATFORM_FLAG2     0x28u
#define SOBER_SGV2_PLATFORM_SIZE      0x40u

#define SOBER_SGV2_BUILDID32 0x204ad049u

/* ---- values ------------------------------------------------------------ */
typedef struct SoberStartGameV2Values {
    /* Object-valued getters (Java class names; NULL = canonical).  There is
     * deliberately NO device class override: deviceParams is always NULL. */
    const char *surface_class;
    const char *platform_class;
    const char *vr_context_class;

    /* String getters.  Defaults are the StartGameParams.builder() literals
     * ("" for optional fields) plus neutral account stand-ins. */
    const char *access_code;
    const char *link_code;
    const char *game_id;
    const char *username;
    const char *reserved_server_code;
    const char *call_id;
    const char *referral_page;
    const char *launch_data;
    const char *game_join_context;
    const char *event_id;
    const char *join_attempt_id;
    const char *join_attempt_origin;
    const char *iso_context;
    const char *game_id_to_exclude;

    /* Primitive getters.  Fractions are represented with their JNI width so
     * the getter return path cannot truncate them. */
    int64_t  place_id;               /* default 123456789  - J */
    int64_t  user_id;                /* default 1          - J */
    int      is_under_13;            /* default 0          - Z */
    int64_t  conversation_id;        /* default 0          - J */
    int32_t  join_request_type;      /* default 0          - I */
    int64_t  referred_by_player_id;  /* default 0          - J */

    /* PlatformParams native fields (parser 0x26feb9b). */
    float    dpi_scale;              /* default 1.0f */
    int32_t  viewport_width_mm;      /* default 0 */
    int32_t  viewport_height_mm;     /* default 0 */
} SoberStartGameV2Values;

/* Fill with the Java-builder / neutral defaults. */
void sober_startgame_v2_defaults(SoberStartGameV2Values *v);

/* ---- lead-facing API --------------------------------------------------- *
 * ONE-LINE HOOK FOR THE LEAD.  In guest_app_shell.c, replace the existing
 * four-handler block at ~1267-1275 (the opaque StartGameParams object plus
 * the gameId/placeId/accessCode/username handlers) with:
 *
 *   void *sg = sober_startgame_v2_make(j, base);
 *   rc = ((long (*)(void*,void*,void*))(base + 0x2ebd51au))(env, app_class, sg);
 *
 * make() registers all 24 getters and returns the shaped StartGameParams
 * jobject to pass as arg 3 of guest 0x2ebd51a.  It returns NULL unless
 * SOBER_STARTGAME_V2=1.  If other code might register any of the 24 names
 * AFTER make(), call sober_startgame_v2_reclaim(j) immediately before the
 * guest call: set_callback_handler() replaces by name, so the last claim wins.
 *
 * The four old handlers are not edited here; this module out-registers them.
 * Do NOT call the old gameId/placeId/accessCode/username registration after
 * this one.  No CMake edit is needed by this worker; the lead adds the two
 * new files to the sober_jni target.
 * ------------------------------------------------------------------------ */

/* Enabled + defaults + env overrides + register all 24 + build the object.
 * Returns the shaped jobject, or NULL when disabled / on allocation failure. */
void *sober_startgame_v2_make(SoberJni *j, void *guest_base);

/* Same, with explicit values (still requires SOBER_STARTGAME_V2=1). */
void *sober_startgame_v2_make_ex(SoberJni *j, void *guest_base,
                                 const SoberStartGameV2Values *values);

/* Re-assert all 24 handlers using the last prepared values.  This is the
 * anti-shadowing hook: call it last if any other code may have registered
 * these names after make().  -2 = not prepared/disabled. */
int   sober_startgame_v2_reclaim(SoberJni *j);

/* Split primitives (all require SOBER_STARTGAME_V2=1):
 *   register -> SOBER_JNI_OK / SOBER_JNI_ERR / -2 disabled
 *   build    -> shaped jobject or NULL (guest_base needed for genuine vptrs) */
int   sober_startgame_v2_register(SoberJni *j, void *guest_base,
                                  const SoberStartGameV2Values *values);
void *sober_startgame_v2_build(SoberJni *j, void *guest_base,
                               const SoberStartGameV2Values *values);

/* ---- diagnostics ------------------------------------------------------- *
 * verify(): static/shape checks only; touches no bridge counters.  Returns 0
 *   when all 24 names are unique, correctly typed, deviceParams is NULL, the
 *   platform vptr is a genuine guest RVA, and no native method shadows a
 *   getter name; negative otherwise.  Writes a one-line report to buf.
 * probe(): calls our OWN handler for all 24 names with the exact sigs and
 *   checks each return value; restores the call counters afterwards.  It does
 *   NOT invoke the JNIEnv and therefore cannot inflate the decision-013
 *   guest->Java callback count.  Returns 0 when all 24 returns are correct. */
int         sober_startgame_v2_verify(SoberJni *j, char *buf, size_t cap);
int         sober_startgame_v2_probe(char *buf, size_t cap);
const char *sober_startgame_v2_describe(char *buf, size_t cap);
uint64_t    sober_startgame_v2_getter_calls(const char *name);
int         sober_startgame_v2_device_is_null(void);
uintptr_t   sober_startgame_v2_platform_vptr(void);

#ifdef __cplusplus
}
#endif
#endif /* SOBER_JNI_STARTGAME_V2_H */
