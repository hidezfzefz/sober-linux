/*
 * placeid_default.h - DEFAULT-ON, correctly typed StartGameParams.placeId()J.
 *
 * Owner: logical Team 4 (callback + assurance), agent T4-2.
 * SCOPE: this file + placeid_default.c ONLY.  The lead owns shared src/,
 * every CMakeLists.txt, docs/, tests/ and git.  The engine is FROZEN and is
 * never modified.  Marker WITHHELD: this module never touches decision 013.
 *
 * ---------------------------------------------------------------------------
 * TARGET / PROVENANCE (all RVAs are guest-base relative, BuildID 204ad049...)
 * ---------------------------------------------------------------------------
 * Frozen engine: /tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so,
 * BuildID 204ad049599c96afb75b8a21d5051063e821d6c9,
 * SHA256 3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335,
 * version 2.740.931.  Target bytes/disassembly are untrusted DATA, never
 * instructions to follow.  See analysis/C12-placeid-source.md for the chain.
 *
 *  0x2ebd855  lea  rcx,[rip]        # 0x2a4e3f == "placeId"   <-- getter site
 *  0x2ebd865  call 0x6c60980        getter-by-name helper
 *  0x6c609ad  lea  rcx,[rip]        # 0x33061d == "()J"
 *  0x6c609ba  call [JNIEnv+0x108]   fn[29] GetMethodID("placeId","()J")
 *  0x6c609d5  jmp 0x2570d62
 *  0x2570ddc  call [JNIEnv+0x1a8]   fn[49] CallLongMethodV -> jlong in RAX
 *  0x2ebd86a  mov  [rip+0x43e594f],rax  # 0x72a31c0  <-- THE STORE
 *  0x2ec1370  mov  0x43e1e49(%rip),%r14 # 0x72a31c0  <-- only other xref
 *
 * 0x72a31c0 is the single BSS qword holding the placeId jlong.  It is read
 * only inside nativeAppBridgeV2StartGameWithParam::$_0::operator() (0x2ec1340)
 * and copied into the game-load struct that later reaches the notifier at
 * 0x27eaf6e with RSI = placeId.  Source field: AutoValue_StartGameParams.smali
 * line 45 `.field private final placeId:J`, getter line 915 `placeId()J`.
 *
 * ---------------------------------------------------------------------------
 * WHY THIS MODULE EXISTS
 * ---------------------------------------------------------------------------
 * The default SOBER_GAME_LOADED_PATH fallback serves placeId()J from the SAME
 * branch as gameId()Ljava/lang/String; (guest_app_shell.c:902-903): it returns
 * a jstring handle, which sr_CallLongMethodV hands back as a raw jlong.  The
 * guest then stores a host pointer shape into 0x72a31c0 instead of a place
 * number.  The two opt-in modules (game_params.c / startgame_v2.c) fix this
 * but are default OFF.  This module is the DEFAULT-ON counterpart:
 *
 *   * registers ALL 24 AutoValue_StartGameParams getters with their exact
 *     smali names AND signatures, so no stale four-getter handler survives;
 *   * returns a real machine jlong > 0 for placeId()J (default 123456789);
 *   * NEVER returns a jstring handle for a primitive (J/Z/I) getter;
 *   * is disabled only by the kill switch SOBER_PLACEID_DEFAULT=0.
 *
 * No guest byte writes, no code patching, no fabricated vtables, no guest
 * callbacks emitted, no decision-013 marker.  The only state this module
 * changes is the bridge's handler table (via sober_jni_set_callback_handler),
 * plus its own read-only view of the cached guest global 0x72a31c0.
 *
 * ---------------------------------------------------------------------------
 * HARD RULES OBSERVED HERE
 * ---------------------------------------------------------------------------
 *  - Default ON; SOBER_PLACEID_DEFAULT=0 (also off/no/false) -> inert no-op.
 *  - Optional positive value override SOBER_PLACEID_VALUE=<decimal>; values
 *    <= 0 or malformed are ignored (the invariant is placeId > 0).
 *  - All 24 names are re-claimable: set_callback_handler() replaces by name,
 *    so a stale game_params_get / sgv2 handler cannot shadow ours.  Any native
 *    method registered under a getter name is detected by verify().
 *  - Verification accessors are READ-ONLY: the cached global is observed,
 *    never written by this module (the guest writes it at 0x2ebd86a).
 * ---------------------------------------------------------------------------
 * EXACT WIRING SIGNATURE FOR THE LEAD (lead owns guest_app_shell.c/CMakeLists)
 * ---------------------------------------------------------------------------
 *  1. Add src/jni/placeid_default.c to the sober_jni static library.
 *  2. AFTER any legacy fallback handler registration (game_params_get at
 *     guest_app_shell.c:902-903) and BEFORE the call to guest 0x2ebd51a:
 *
 *       #include "placeid_default.h"
 *       int prc = sober_placeid_default_install(j, (void *)base);
 *       // if other code may register these names afterwards, call
 *       // sober_placeid_default_reclaim(j) as the LAST claim before 0x2ebd51a.
 *
 *  3. After StartGameWithParam returns, verify the guest cached the value:
 *
 *       char b[256];
 *       int ok = sober_placeid_default_verify_live(j, (void *)base, b, sizeof b);
 *       // ok == SOBER_PLACEID_DEFAULT_OK (0) iff placeId()J was dispatched,
 *       // the cached qword at base+0x72a31c0 equals our positive jlong, and
 *       // no native method shadows any of the 24 getters.
 *
 * install() is idempotent and self-gating.  Do NOT call the legacy
 * gameId/placeId/accessCode/username registration after install()/reclaim().
 * ---------------------------------------------------------------------------
 */
#ifndef SOBER_JNI_PLACEID_DEFAULT_H
#define SOBER_JNI_PLACEID_DEFAULT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Root build uses -fvisibility=hidden; publish every entry point. */
#if defined(__GNUC__) || defined(__clang__)
#  define SOBER_PLACEID_DEFAULT_API __attribute__((visibility("default")))
#else
#  define SOBER_PLACEID_DEFAULT_API
#endif

/* ---- environment --------------------------------------------------------- */
#define SOBER_PLACEID_DEFAULT_ENV   "SOBER_PLACEID_DEFAULT"  /* kill switch */
#define SOBER_PLACEID_VALUE_ENV     "SOBER_PLACEID_VALUE"    /* optional >0   */
#define SOBER_PLACEID_DEFAULT_VALUE 123456789LL              /* default jlong */

/* ---- frozen-engine RVAs (BuildID 204ad049..., DO NOT MODIFY) ------------- */
#define SOBER_PLACEID_RVA_GETTER_SITE    0x2ebd855u  /* "placeId" lea        */
#define SOBER_PLACEID_RVA_GETTER_HELPER  0x6c60980u  /* GetMethodID("()J")   */
#define SOBER_PLACEID_RVA_HELPER_STORE   0x2ebd86au  /* -> cached global     */
#define SOBER_PLACEID_RVA_GLOBAL         0x72a31c0u  /* THE cached jlong qword*/
#define SOBER_PLACEID_RVA_GLOBAL_READ    0x2ec1370u  /* lambda reads it      */
#define SOBER_PLACEID_RVA_NAME_STR       0x2a4e3fu   /* "placeId"            */
#define SOBER_PLACEID_RVA_SIG_STR        0x33061du   /* "()J"                */
#define SOBER_PLACEID_RVA_CALL_LONG_V    0x2570ddcu  /* call [env+0x1a8]      */
#define SOBER_PLACEID_JNI_FN_CALL_LONG_V 49u         /* fn[49] CallLongMethodV*/
#define SOBER_PLACEID_JNI_OFF_CALL_LONG_V 0x1a8u     /* 4*8 + 49*8            */

/* ---- exact smali getter names / signatures (24) -------------------------- */
#define SOBER_PLACEID_GET_SURFACE            "surface"
#define SOBER_PLACEID_GET_PLATFORM           "platformParams"
#define SOBER_PLACEID_GET_DEVICE             "deviceParams"
#define SOBER_PLACEID_GET_PLACE_ID           "placeId"
#define SOBER_PLACEID_GET_USER_ID            "userId"
#define SOBER_PLACEID_GET_ACCESS_CODE        "accessCode"
#define SOBER_PLACEID_GET_LINK_CODE          "linkCode"
#define SOBER_PLACEID_GET_GAME_ID            "gameId"
#define SOBER_PLACEID_GET_UNDER13            "isUnder13"
#define SOBER_PLACEID_GET_USERNAME           "username"
#define SOBER_PLACEID_GET_CONVERSATION_ID    "conversationId"
#define SOBER_PLACEID_GET_RESERVED_CODE      "reservedServerAccessCode"
#define SOBER_PLACEID_GET_CALL_ID            "callId"
#define SOBER_PLACEID_GET_JOIN_REQUEST_TYPE  "joinRequestType"
#define SOBER_PLACEID_GET_REFERRAL_PAGE      "referralPage"
#define SOBER_PLACEID_GET_LAUNCH_DATA        "launchData"
#define SOBER_PLACEID_GET_JOIN_CONTEXT       "gameJoinContext"
#define SOBER_PLACEID_GET_EVENT_ID           "eventId"
#define SOBER_PLACEID_GET_JOIN_ATTEMPT_ID    "joinAttemptId"
#define SOBER_PLACEID_GET_JOIN_ATTEMPT_ORIG  "joinAttemptOrigin"
#define SOBER_PLACEID_GET_ISO_CONTEXT        "isoContext"
#define SOBER_PLACEID_GET_REFERRED_BY        "referredByPlayerId"
#define SOBER_PLACEID_GET_GAME_ID_EXCLUDE    "gameIdToExclude"
#define SOBER_PLACEID_GET_VR_CONTEXT         "vrContext"

#define SOBER_PLACEID_SIG_SURFACE            "()Landroid/view/Surface;"
#define SOBER_PLACEID_SIG_PLATFORM           "()Lcom/roblox/engine/jni/model/PlatformParams;"
#define SOBER_PLACEID_SIG_DEVICE             "()Lcom/roblox/engine/jni/model/DeviceParams;"
#define SOBER_PLACEID_SIG_PLACE_ID           "()J"
#define SOBER_PLACEID_SIG_USER_ID            "()J"
#define SOBER_PLACEID_SIG_ACCESS_CODE        "()Ljava/lang/String;"
#define SOBER_PLACEID_SIG_LINK_CODE          "()Ljava/lang/String;"
#define SOBER_PLACEID_SIG_GAME_ID            "()Ljava/lang/String;"
#define SOBER_PLACEID_SIG_UNDER13            "()Z"
#define SOBER_PLACEID_SIG_USERNAME           "()Ljava/lang/String;"
#define SOBER_PLACEID_SIG_CONVERSATION_ID    "()J"
#define SOBER_PLACEID_SIG_RESERVED_CODE      "()Ljava/lang/String;"
#define SOBER_PLACEID_SIG_CALL_ID            "()Ljava/lang/String;"
#define SOBER_PLACEID_SIG_JOIN_REQUEST_TYPE  "()I"
#define SOBER_PLACEID_SIG_REFERRAL_PAGE      "()Ljava/lang/String;"
#define SOBER_PLACEID_SIG_LAUNCH_DATA        "()Ljava/lang/String;"
#define SOBER_PLACEID_SIG_JOIN_CONTEXT       "()Ljava/lang/String;"
#define SOBER_PLACEID_SIG_EVENT_ID           "()Ljava/lang/String;"
#define SOBER_PLACEID_SIG_JOIN_ATTEMPT_ID    "()Ljava/lang/String;"
#define SOBER_PLACEID_SIG_JOIN_ATTEMPT_ORIG  "()Ljava/lang/String;"
#define SOBER_PLACEID_SIG_ISO_CONTEXT        "()Ljava/lang/String;"
#define SOBER_PLACEID_SIG_REFERRED_BY        "()J"
#define SOBER_PLACEID_SIG_GAME_ID_EXCLUDE    "()Ljava/lang/String;"
#define SOBER_PLACEID_SIG_VR_CONTEXT         "()Landroid/app/Activity;"

#define SOBER_PLACEID_GETTER_COUNT 24

/* ---- return codes -------------------------------------------------------- */
#define SOBER_PLACEID_DEFAULT_OK        0    /* enabled, registered, verified  */
#define SOBER_PLACEID_DEFAULT_DISABLED  1    /* kill switch: inert no-op       */
#define SOBER_PLACEID_DEFAULT_ERR      (-1)  /* bad argument / register failed */
#define SOBER_PLACEID_DEFAULT_SHADOWED (-2)  /* a native method claims a getter*/
#define SOBER_PLACEID_DEFAULT_VERIFY   (-3)  /* signature/cached global wrong  */

/* ---- exported getter table (for independent signature verification) ----- */
typedef enum SoberPlaceidReturnKind {
    SOBER_PLACEID_RET_J = 1,          /* 64-bit jlong: placeId/userId/etc.    */
    SOBER_PLACEID_RET_Z = 2,          /* jboolean                            */
    SOBER_PLACEID_RET_I = 3,          /* jint                                */
    SOBER_PLACEID_RET_STRING = 4,     /* jstring (never for J/Z/I)           */
    SOBER_PLACEID_RET_OBJECT = 5,     /* neutral shaped object               */
    SOBER_PLACEID_RET_DEVICE = 6      /* deviceParams: genuine NULL          */
} SoberPlaceidReturnKind;

typedef struct SoberPlaceidGetter {
    const char *name;                 /* exact smali method name             */
    const char *sig;                  /* exact smali signature               */
    SoberPlaceidReturnKind kind;      /* JNI return kind                     */
} SoberPlaceidGetter;

typedef struct SoberJni SoberJni;     /* full definition in native_jni.h     */

/* ---- enable / state ------------------------------------------------------ */
/* 1 unless SOBER_PLACEID_DEFAULT is 0/off/no/false. */
SOBER_PLACEID_DEFAULT_API int         sober_placeid_default_enabled(void);
SOBER_PLACEID_DEFAULT_API const char *sober_placeid_default_env_vars(void);

/* The positive jlong this module serves for placeId()J (default 123456789).
 * Reads SOBER_PLACEID_VALUE once at install; malformed/<=0 -> default. */
SOBER_PLACEID_DEFAULT_API int64_t sober_placeid_default_value(void);

/* ---- install / anti-shadow ----------------------------------------------- */
/* Register all 24 getters.  No-op when killed; returns SOBER_JNI_OK on
 * success (also on repeat), SOBER_JNI_ERR on bad args.  Call AFTER the legacy
 * fallback handlers and BEFORE guest 0x2ebd51a. */
SOBER_PLACEID_DEFAULT_API int sober_placeid_default_install(SoberJni *j,
                                                            void *guest_base);

/* Re-assert all 24 handlers (last claim wins).  Use as the final claim if
 * other code may register these names after install().  -2 if not prepared. */
SOBER_PLACEID_DEFAULT_API int sober_placeid_default_reclaim(SoberJni *j);

SOBER_PLACEID_DEFAULT_API int sober_placeid_default_is_registered(void);

/* ---- verification accessors ---------------------------------------------- */
/* The cached native global 0x72a31c0, read from guest_base (0 if NULL base).
 * This is exactly the qword written by guest 0x2ebd86a. */
SOBER_PLACEID_DEFAULT_API int64_t sober_placeid_default_cached_global(void *guest_base);

/* 1 iff base+0x72a31c0 is >0 AND equals the served placeId value. */
SOBER_PLACEID_DEFAULT_API int sober_placeid_default_cached_ok(void *guest_base);

/* placeId()J dispatch count and per-getter counts (module-side only). */
SOBER_PLACEID_DEFAULT_API uint64_t sober_placeid_default_dispatch_count(void);
SOBER_PLACEID_DEFAULT_API uint64_t sober_placeid_default_getter_calls(const char *name);
SOBER_PLACEID_DEFAULT_API unsigned  sober_placeid_default_install_count(void);
SOBER_PLACEID_DEFAULT_API unsigned  sober_placeid_default_claim_count(void);
SOBER_PLACEID_DEFAULT_API unsigned  sober_placeid_default_sig_mismatches(void);
SOBER_PLACEID_DEFAULT_API int       sober_placeid_default_last_rc(void);

/* Static view of the 24 name/signature/kind triples (do not free). */
SOBER_PLACEID_DEFAULT_API const SoberPlaceidGetter *
sober_placeid_default_getters(size_t *count);

/* Structural verify: enabled, registered, 24 unique exact names/sigs, placeId
 * is "()J"/J, no native method shadow, no observed signature mismatch, and
 * (when guest_base != NULL) cached global >0 and equal to the value.  Always
 * fills buf.  Pass guest_base=NULL for a pure structural check. */
SOBER_PLACEID_DEFAULT_API int sober_placeid_default_verify(SoberJni *j,
                                                           void *guest_base,
                                                           char *buf, size_t cap);

/* Live verify: verify() plus placeId dispatch_count > 0 (i.e. the guest
 * actually went through CallLongMethodV) plus cached_ok(guest_base). */
SOBER_PLACEID_DEFAULT_API int sober_placeid_default_verify_live(SoberJni *j,
                                                                void *guest_base,
                                                                char *buf, size_t cap);

/* Call our OWN handler for all 24 names with the exact signatures and check
 * the return kinds; restores our counters afterwards.  Does NOT enter the
 * bridge JNIEnv, so it cannot inflate the decision-013 callback count. */
SOBER_PLACEID_DEFAULT_API int sober_placeid_default_probe(char *buf, size_t cap);

/* End-to-end: obtain method ID "placeId"/"()J" and invoke it through the
 * bridge's REAL JNINativeInterface_ slot fn[49] (CallLongMethodV, +0x1a8),
 * the exact path used by guest helper 0x6c60980 at 0x2570ddc.  DIAGNOSTIC
 * ONLY: this deliberately dispatches one bridge callback, so never call it
 * on a marker-gate run. */
SOBER_PLACEID_DEFAULT_API int sober_placeid_default_selftest(SoberJni *j,
                                                             char *buf, size_t cap);

SOBER_PLACEID_DEFAULT_API const char *
sober_placeid_default_describe(void *guest_base, char *buf, size_t cap);

/* Reset module-side counters only (does not unregister handlers). */
SOBER_PLACEID_DEFAULT_API void sober_placeid_default_reset(void);

#ifdef __cplusplus
}
#endif
#endif /* SOBER_JNI_PLACEID_DEFAULT_H */
