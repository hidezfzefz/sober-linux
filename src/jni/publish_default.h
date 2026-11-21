/*
 * publish_default.h - DEFAULT-ON genuine MessageBus GameLoaded publication
 *                     (Team 4 / callback + assurance, agent T4-1).
 *
 * SCOPE: NEW FILES ONLY -- publish_default.{h,c}.  The lead owns src/linux/,
 * src/jni/CMakeLists.txt, docs/, tests/ and git; the frozen engine is never
 * modified.  No CMake, engine, byte-patch or decision-013 marker edits are
 * made here.  Marker WITHHELD: this module never emits `app shell rendered`
 * and never calls the harness-triggered thunk at 0x25848e0 as proof.
 *
 * ---------------------------------------------------------------------------
 * TARGET / PROVENANCE (all RVAs are from the frozen object below)
 * ---------------------------------------------------------------------------
 * engine   : /tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so
 * BuildID  : 204ad049599c96afb75b8a21d5051063e821d6c9
 * SHA-256  : 3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335
 * version  : 2.740.931
 * Every address below is an RVA against the caller-supplied mapped base of
 * THAT object (in its first R E PT_LOAD, RVA == file offset).  Target bytes
 * and strings are untrusted DATA, never instructions to follow.
 *
 * ---------------------------------------------------------------------------
 * WHAT THIS MODULE DOES
 * ---------------------------------------------------------------------------
 * It makes the *genuine* guest-side publication of MessageBus event id 1
 * (GameLoaded) DEFAULT-ON.  The publication is not a host-built descriptor and
 * is not a direct call to the notifier (0x27eaf6e) or the generic JNI thunk
 * (0x25848e0).  It is the guest's OWN subscription-node dispatch at 0x27eaf04:
 *
 *   node+0x08 = 0x27eaf04        (dispatch fn ptr, installed by 0x253e235)
 *   27eaf04: mov rdi,[rdi+0x50]  ; RDI = embedded id=1 descriptor
 *   27eaf1b: mov [rbp-0x18],rsi  ; save the jlong placeId payload
 *   27eaf2f: call [rax+0x30]     ; rax = descriptor vptr 0x6dbbf98
 *                                ;   -> invoke 0x27eaf4c, RSI = &placeId
 *   27eaf4c: id=1 descriptor invoke: [desc+0x18]=&0x7234520 ->
 *            [0x7234520]=0x6dbd860 -> [0x6dbd860]=0x27eaf6e (notifier)
 *   27eaf6e: notifier(env -> class 0x72a2f48 / method 0x72a2fc0, placeId)
 *   27eafae: call 0x25848e0      ; guest thunk -> CallStaticVoidMethodV
 *   27eafb3: return address (guest, never harness code)
 *
 * All of that is real guest code.  The host only (a) verifies the relocated
 * chain and the registered node read-only, and (b) calls the genuine dispatch
 * function pointer at base+0x27eaf04 with RDI = the genuine node found in the
 * MessageBus singleton's +0x288 subscription list and RSI = &placeId.  The
 * guest then executes 0x27eaf2f -> 0x27eaf4c -> 0x27eaf6e -> 0x25848e0
 * itself.  We NEVER call 0x27eaf6e, 0x25848e0 or any JNI slot directly.
 *
 * Upstream: the placeId is the jlong returned by StartGameParams.placeId()J
 * inside nativeAppBridgeV2StartGameWithParam (0x2ebd51a), cached by the guest
 * at global 0x72a31c0 (C12) and consumed by the DataModel/SessionReporter
 * game-load lambda (0x2ec1340) before the state reaches GameLoaded.  The
 * lead-supplied start_game()/drive() helpers call that real JNI export first,
 * then publish with the seeded placeId.  No descriptor, node, vtable or JNI
 * object is fabricated by this module.
 *
 * ---------------------------------------------------------------------------
 * ENVIRONMENT
 * ---------------------------------------------------------------------------
 *   SOBER_PUBLISH_DEFAULT=0   KILL SWITCH.  Unset (or any value other than
 *                             "0"/"off"/"no"/"false") means ENABLED.  With a
 *                             falsy value every entry point is an inert
 *                             no-op returning SOBER_PD_DISABLED.
 *
 * ---------------------------------------------------------------------------
 * DECISION 013 / WITHHOLD
 * ---------------------------------------------------------------------------
 * The module is default-ON and does seed a guest publication from host code,
 * so sober_publish_default_should_withhold() is non-zero while enabled; OR it
 * into the `seeded_state` chain in guest_app_shell.c to keep the marker
 * force-withheld.  Enabling this module is NOT proof of `app shell rendered`.
 * RVA-level hit evidence (0x27eaf2f/0x27eaf4c/0x27eaf6e/0x27eafb3) must be
 * corroborated independently (notifier_probe / callback_provenance / gdb);
 * this module only counts the stages it drives and any hits fed to it via
 * sober_publish_default_record_hit().
 *
 * ---------------------------------------------------------------------------
 * EXACT WIRING SIGNATURE FOR THE LEAD (lead-owned edit, not made here)
 * ---------------------------------------------------------------------------
 *   #include "publish_default.h"
 *   -- After the MessageBus id=1 descriptor is registered (0x24abf96 ran)
 *      and StartGameParams 0x2ebd51a / the DataModel state have been driven:
 *   int prc = sober_publish_default_drive((void *)base, env, app_class,
 *                                         start_game_params, 0); // seeded
 *   -- or, if StartGameWithParam was already called by guest_app_shell.c:
 *   int prc = sober_publish_default_publish_seeded((void *)base);
 *   -- or with an explicit positive placeId:
 *   int prc = sober_publish_default_publish((void *)base, 123456789);
 *
 *   -- marker force-withhold (Decision 013):
 *   seeded_state |= sober_publish_default_should_withhold();
 *
 *   -- evidence dump:
 *   SoberPublishSnapshot s; sober_publish_default_snapshot(&s);
 *   char b[256]; sober_publish_default_describe(b, sizeof(b));
 *
 * Add publish_default.c to the sober_jni target in src/jni/CMakeLists.txt
 * (lead-owned); this worker does not edit it.
 */
#ifndef SOBER_JNI_PUBLISH_DEFAULT_H
#define SOBER_JNI_PUBLISH_DEFAULT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__GNUC__) || defined(__clang__)
#  define SOBER_PD_API __attribute__((visibility("default")))
#else
#  define SOBER_PD_API
#endif

/* Informational build guard (first 32 bits of the frozen BuildID). */
#define SOBER_PD_BUILDID32 0x204ad049u

/* Kill switch env var (default-ON; exact "0"/off/no/false disables). */
#define SOBER_PUBLISH_DEFAULT_ENV "SOBER_PUBLISH_DEFAULT"

/* Kill switch for the DIAGNOSTIC 0x253cb56 registration re-drive only
 * (default ENABLED to preserve existing behaviour; exact "0"/off/no/false
 * disables it).  The re-drive seeds guest state, so it is NOT proof and stays
 * in the forced-withhold chain via sober_publish_default_should_withhold(). */
#define SOBER_PUBLISH_NODE_REDRIVE_ENV "SOBER_PUBLISH_NODE_REDRIVE"

/* ---- result codes -------------------------------------------------------- */
typedef enum SoberPublishResult {
    SOBER_PD_OK          =   0,  /* chain verified; genuine dispatch driven   */
    SOBER_PD_ERR         =  -1,  /* bad argument (NULL guest base)            */
    SOBER_PD_DISABLED    =  -2,  /* SOBER_PUBLISH_DEFAULT falsy: no-op        */
    SOBER_PD_NO_NODE     =  -3,  /* id=1 subscription node not found          */
    SOBER_PD_BAD_CHAIN   =  -4,  /* slot/table/vtable resolution wrong        */
    SOBER_PD_NO_STATE    =  -5   /* placeId==0 or cached JID globals NULL     */
} SoberPublishResult;

/* ---- frozen-engine RVAs (BuildID 204ad049 / SHA 3e498d6494...) ----------- */
typedef enum SoberPublishRva {
    /* the genuine publish function and the call inside it */
    SOBER_PD_RVA_NODE_DISPATCH   = 0x27eaf04u, /* subscription node dispatch  */
    SOBER_PD_RVA_PUBLISH_CALL    = 0x27eaf2fu, /* call QWORD [rax+0x30]        */
    /* embedded GameLoaded descriptor (vptr/id/slot) */
    SOBER_PD_RVA_DESC_VPTR       = 0x6dbbf98u, /* id=1 descriptor vtable      */
    SOBER_PD_RVA_INVOKE_SLOT     = 0x6dbbfc8u, /* vptr+0x30 -> invoke (APS2)   */
    SOBER_PD_RVA_DESC_INVOKE     = 0x27eaf4cu, /* descriptor invoke            */
    SOBER_PD_RVA_SLOT_GLOBAL     = 0x7234520u, /* global ptr to notifier table */
    SOBER_PD_RVA_NOTIFIER_TABLE  = 0x6dbd860u, /* table base; [0]=notifier     */
    SOBER_PD_RVA_NOTIFIER        = 0x27eaf6eu, /* GameLoaded notifier          */
    SOBER_PD_RVA_THUNK_CALL      = 0x27eafaeu, /* guest `call 0x25848e0`       */
    SOBER_PD_RVA_THUNK_RET       = 0x27eafb3u, /* guest return address         */
    SOBER_PD_RVA_THUNK           = 0x25848e0u, /* generic JNI thunk (NEVER called by us) */
    /* cached JNI state, seeded by JNI_OnLoad */
    SOBER_PD_RVA_CLASS_GLOBAL    = 0x72a2f48u, /* NativeGLJavaInterface jclass */
    SOBER_PD_RVA_METHOD_GLOBAL   = 0x72a2fc0u, /* gameLoadedCallback (J)V      */
    /* StartGameParams -> placeId -> SessionReporter state */
    SOBER_PD_RVA_PLACE_ID_GLOBAL = 0x72a31c0u, /* placeId cached @0x2ebd86a    */
    SOBER_PD_RVA_START_GAME      = 0x2ebd51au, /* nativeAppBridgeV2StartGameWithParam */
    /* MessageBus singleton and subscription list */
    SOBER_PD_RVA_BUS_DEFAULT     = 0x72cbdd0u, /* default singleton object     */
    SOBER_PD_RVA_BUS_MODE3       = 0x72cc278u, /* mode-3 singleton object      */
    SOBER_PD_RVA_BUS_GUARD_A     = 0x72cc270u, /* one-shot guard A             */
    SOBER_PD_RVA_BUS_GUARD_B     = 0x72cc718u, /* one-shot guard B             */
    SOBER_PD_RVA_CANARY_GLOBAL   = 0x72293f8u, /* stack canary used by dispatch*/
    /* log evidence strings (data only) */
    SOBER_PD_RVA_LOG_TAG         = 0x550ef3u, /* "rbx.jni"                    */
    SOBER_PD_RVA_LOG_FMT         = 0x36c7c9u, /* "onGameLoaded() SessionReporterState_GameLoaded placeId:%lld" */
    /* node builder (evidence only, never called directly) */
    SOBER_PD_RVA_NODE_BUILDER    = 0x253e21eu, /* fills node+0x08/+0x20/+0x50 */
    /* Genuine guest registration entry point for the id=1 GameLoaded
     * descriptor + subscription node.  nativeAppBridgeV2InitWithParams calls
     * it at 0x24ad6a5 as `register(1, base+0x7234520)`.  The guest builds the
     * descriptor (vptr 0x6dbbf98, id 1, slot &0x7234520) and the node with its
     * OWN allocator (0x1dbab02) and inserts it into the MessageBus +0x288
     * list.  Calling it is a genuine guest call: no host byte write, no
     * fabricated object, no direct notifier/thunk call. */
    SOBER_PD_RVA_NODE_REGISTER   = 0x253cb56u,
    /* One-shot guard consumed at the TOP of 0x253cb56 (test 0x253cb84, set
     * 0x253cb91), BEFORE any descriptor/node is registered.  If it is already
     * 1 while no id=1 node exists, the guest's single registration attempt ran
     * before its prerequisites were ready and a re-drive is permanently inert.
     * Read-only here; this module never writes it. */
    SOBER_PD_RVA_NODE_GUARD      = 0x72a31bau,
    /* App singleton read by the 0x30ef840 accessor on the game-load path. */
    SOBER_PD_RVA_APP_SINGLETON   = 0x72cb5e0u,
    /* GL capability predicate at the head of 0x30ef840 (0x6096928). */
    SOBER_PD_RVA_GL_PREDICATE    = 0x6096928u
} SoberPublishRva;

/* Offsets inside the MessageBus singleton / subscription node / descriptor. */
#define SOBER_PD_OFF_BUS_LIST        0x288u  /* list head in singleton        */
#define SOBER_PD_NODE_OFF_DISPATCH   0x08u   /* node -> dispatch fn ptr       */
#define SOBER_PD_NODE_OFF_NEXT       0x20u   /* node -> next (intrusive)      */
#define SOBER_PD_NODE_OFF_DESC_SELF  0x50u   /* node -> descriptor self ptr   */
#define SOBER_PD_NODE_SIZE           0x60u   /* guest alloc size @0x253e1ac   */
#define SOBER_PD_DESC_OFF_VPTR       0x00u
#define SOBER_PD_DESC_OFF_ID         0x08u
#define SOBER_PD_DESC_OFF_DELTA      0x10u
#define SOBER_PD_DESC_OFF_SLOT       0x18u
#define SOBER_PD_DESC_OFF_SELF       0x20u
#define SOBER_PD_DESC_ID_GAMELOADED  1u
#define SOBER_PD_MAX_NODE_WALK       4096u   /* fail-closed list walk bound  */

/* Hit-stage indices for sober_publish_default_record_hit()/hits(). */
typedef enum SoberPublishStage {
    SOBER_PD_STAGE_DISPATCH     = 0,  /* entered 0x27eaf04                  */
    SOBER_PD_STAGE_PUBLISH_CALL = 1,  /* reached 0x27eaf2f call [rax+0x30]   */
    SOBER_PD_STAGE_INVOKE       = 2,  /* entered 0x27eaf4c                  */
    SOBER_PD_STAGE_NOTIFIER     = 3,  /* entered 0x27eaf6e                  */
    SOBER_PD_STAGE_THUNK_RET    = 4,  /* thunk returned to 0x27eafb3        */
    SOBER_PD_STAGE_CALLBACK     = 5,  /* gameLoadedCallback JNI dispatch    */
    SOBER_PD_STAGE_COUNT        = 6
} SoberPublishStage;

/* Immutable-ish snapshot of counters/state (safe to copy). */
typedef struct SoberPublishSnapshot {
    uint64_t   hits[SOBER_PD_STAGE_COUNT]; /* per-stage hit counters         */
    uint64_t   publish_calls;     /* genuine dispatch invocations performed */
    uint64_t   chain_ok;          /* full chain verified before a dispatch */
    int64_t    last_place_id;     /* placeId used on the last publish       */
    int64_t    observed_place_id; /* last placeId fed with a hit record     */
    uintptr_t  guest_base;        /* base of the last verified publish      */
    uintptr_t  node;              /* genuine id=1 node address              */
    uintptr_t  descriptor;        /* genuine embedded descriptor address    */
    int        last_rc;           /* last publish/drive result code         */
    int        verified;          /* 1 when the last verify() passed        */
} SoberPublishSnapshot;

/* ---- latch-ready prerequisite report (task #500) ------------------------ *
 * Read-only discovery of every upstream condition the genuine chain needs.
 * No guest calls, no guest writes, no callbacks: it is a diagnostic query,
 * not a publish.  It is deliberately separate from verify() so a missing
 * upstream prerequisite cannot be confused with a broken APS2 relocation. */
#define SOBER_PD_PREREQ_CHAIN     (1u << 0)  /* canary + APS2 relocs live       */
#define SOBER_PD_PREREQ_JNI       (1u << 1)  /* cached class + method non-NULL */
#define SOBER_PD_PREREQ_BUS       (1u << 2)  /* MessageBus guards + object live*/
#define SOBER_PD_PREREQ_NODE      (1u << 3)  /* genuine id=1 node registered   */
#define SOBER_PD_PREREQ_PLACE_ID  (1u << 4)  /* guest placeId 0x72a31c0 > 0    */
#define SOBER_PD_PREREQ_APP       (1u << 5)  /* app singleton 0x72cb5e0 non-NULL*/
#define SOBER_PD_PREREQ_ALL       0x3fu

typedef struct SoberPublishPrereqs {
    uint32_t  flags;          /* bitmask of SOBER_PD_PREREQ_* met             */
    uint8_t   node_guard;     /* raw 0x72a31ba one-shot guard                */
    uint8_t   bus_guard_a;    /* raw 0x72cc270 (MessageBus ctor guard A)     */
    uint8_t   bus_guard_b;    /* raw 0x72cc718 (MessageBus ctor guard B)     */
    void     *bus;            /* 0x72cbdd0 default MessageBus object          */
    void     *app;            /* 0x72cb5e0 app singleton                      */
    void     *node;           /* discovered id=1 node (NULL when absent)      */
    int64_t   place_id;       /* sampled 0x72a31c0                            */
    int       rc;             /* first failing SoberPublishResult, or OK      */
} SoberPublishPrereqs;

/* ---- gate / diagnostics -------------------------------------------------- */
/* Default ON: disabled only by a falsy SOBER_PUBLISH_DEFAULT. */
SOBER_PD_API int         sober_publish_default_enabled(void);
SOBER_PD_API const char *sober_publish_default_env_vars(void);
/* Non-zero while enabled (Decision 013 force-withhold signal for the lead). */
SOBER_PD_API int         sober_publish_default_should_withhold(void);

/* ---- read-only verification / discovery --------------------------------- */
/* Verify the relocated chain, the registered id=1 node and the cached JNI
 * globals.  Performs NO guest writes and NO calls into guest code.  Returns
 * SOBER_PD_OK only when everything the dispatch needs is genuine and live;
 * writes a one-line report to buf when non-NULL. */
SOBER_PD_API int sober_publish_default_verify(void *guest_base,
                                               char *buf, size_t cap);

/* Locate the genuine id=1 subscription node in the MessageBus +0x288 list
 * (default and mode-3 singletons).  Read-only.  Returns NULL when absent. */
SOBER_PD_API void *sober_publish_default_find_node(void *guest_base);

/* Latch-ready prerequisite report (task #500).  Read-only: no guest call, no
 * guest write, no callback and no publish.  Returns SOBER_PD_OK only when all
 * SOBER_PD_PREREQ_* flags are set; otherwise the first failing result code.
 * `out` may be NULL; `buf` receives a one-line diagnostic when non-NULL. */
SOBER_PD_API int sober_publish_default_prereqs(void *guest_base,
                                               SoberPublishPrereqs *out,
                                               char *buf, size_t cap);

/* Kill-switch name for the diagnostic 0x253cb56 re-drive (report to lead). */
SOBER_PD_API const char *sober_publish_default_node_redrive_env(void);

/* Read-only component accessors (0/NULL with a NULL base or missing node). */
SOBER_PD_API void  *sober_publish_default_descriptor(void *guest_base, void *node);
SOBER_PD_API int64_t sober_publish_default_seeded_place_id(void *guest_base);

/* ---- lead-facing publish API -------------------------------------------- */
/* Drive the genuine subscription-node dispatch (real guest code) with an
 * explicit positive placeId.  Verifies the chain first; on success returns
 * SOBER_PD_OK and increments publish_calls + the DISPATCH hit counter. */
SOBER_PD_API int sober_publish_default_publish(void *guest_base, int64_t place_id);

/* Same, using the placeId the guest cached at 0x72a31c0 from StartGameParams.
 * Requires a non-zero seeded placeId (SOBER_PD_NO_STATE otherwise). */
SOBER_PD_API int sober_publish_default_publish_seeded(void *guest_base);

/* One-line hook: optionally call the genuine JNI export 0x2ebd51a first
 * (when env, jclass and start_game_params are all non-NULL), then publish with
 * place_id_override if != 0, else with the guest-seeded 0x72a31c0 value. */
SOBER_PD_API int sober_publish_default_drive(void *guest_base,
                                             void *jni_env, void *jclass,
                                             void *start_game_params,
                                             int64_t place_id_override);

/* Record a genuine hit observed by an external probe at one of the RVAs in
 * SoberPublishStage (e.g. from notifier_probe / callback_provenance).  This
 * does NOT emit a callback or touch the gate; it only bumps a counter. */
SOBER_PD_API void sober_publish_default_record_hit(unsigned stage);
SOBER_PD_API void sober_publish_default_record_hit_place(unsigned stage,
                                                         int64_t place_id);

/* ---- hit counters / diagnostics ------------------------------------------ */
SOBER_PD_API uint64_t sober_publish_default_hits(unsigned stage);
SOBER_PD_API uint64_t sober_publish_default_publish_calls(void);
SOBER_PD_API int64_t  sober_publish_default_last_place_id(void);
SOBER_PD_API int      sober_publish_default_last_rc(void);
SOBER_PD_API void     sober_publish_default_snapshot(SoberPublishSnapshot *out);
SOBER_PD_API const char *sober_publish_default_describe(char *buf, size_t cap);
SOBER_PD_API void     sober_publish_default_reset(void);

#ifdef __cplusplus
}
#endif
#endif /* SOBER_JNI_PUBLISH_DEFAULT_H */
