/*
 * provenance_gate.h - DEFAULT-ON positive provenance credential for the
 *                     strict decision-013 `app shell rendered` gate.
 *
 * Owner: Team 4 (callback + assurance) / agent T4-3.  NEW FILE ONLY -- this
 * header and provenance_gate.c are the only files this agent touches.  The
 * lead owns src/jni/native_jni.c, src/linux/guest_app_shell.c, CMakeLists,
 * docs/, tests/ and git; the engine is frozen.  Nothing here edits them.
 *
 * ---------------------------------------------------------------------------
 * WHAT THIS IS (and what it is NOT)
 * ---------------------------------------------------------------------------
 * A *positive* provenance mechanism.  The gate (decision 013/038) currently
 * decides from a name-only callback counter, which cannot distinguish the
 * genuine guest notifier from a harness-triggered thunk (SOBER_ALT_CALLBACK_PATH)
 * or from onAppBridgeNotification.  This module supplies the missing, native,
 * default-ON credential:
 *
 *   A dispatch is certified GENUINE only if, at the moment of the JNI
 *   CallStaticVoidMethodV dispatch, ALL of the following are observed:
 *     (1) the actual return address of `call *0x470(%rax)` in the guest thunk
 *         0x25848e0, i.e. base + 0x2584960, is live on the stack;
 *     (2) the frame directly above it is the gameLoadedCallback notifier's
 *         `call 0x25848e0` return site, i.e. base + 0x27eafb3 (NOT a host
 *         address and NOT the onAppBridgeNotification return site);
 *     (3) the class/method handles passed to the dispatcher are exactly the
 *         guest's cached globals at base + 0x72a2f48 / base + 0x72a2fc0;
 *     (4) the frozen engine build ID matches 204ad049... (fail closed);
 *     (5) the dispatch name is a qualifying name ("gameLoadedCallback");
 *     (6) the FULL guest dispatch chain is live (task #522): walking the frame
 *         chain from the notifier return site must find the descriptor-invoke
 *         return base+0x27eaf32 (the `call [rax+0x30]` at 0x27eaf2f; the
 *         invoke at 0x27eaf4c tail-jmps to the notifier) AND the node dispatch
 *         0x27eaf04 must itself have been entered from guest .text, not from
 *         host code (a host-entered re-drive leaves a harness return address).
 *
 * Classification is fail-closed and disjoint:
 *   GENUINE          -> (1)+(2)+(3)+(4)+(5)+(6): real notifier-driven callback.
 *   HARNESS_THUNK    -> (1) present but (2) is a HOST/unknown return address:
 *                       the ALT thunk called directly from host code.  Never
 *                       genuine; sets a global sticky taint flag.
 *   APPBRIDGE        -> (1) present but the caller return site is (2') the
 *                       onAppBridgeNotification reader, base + 0x2eb4bb1.
 *                       A genuine guest call, but JNI plumbing and explicitly
 *                       rejected as app-shell proof (decision 013 note).  It
 *                       is NEITHER genuine NOR a harness thunk: no false
 *                       positive from that path.
 *   UNCLASSIFIED     -> no thunk return site: nothing is claimed.
 *
 * This module NEVER emits a marker, NEVER calls a JNI handler/callback, never
 * patches guest bytes and never reads or writes a withhold/gate decision.  It
 * only records observations and answers queries; the lead's gate combines
 * `is_genuine()` + `!is_harness_thunk()` with render and clean-state evidence.
 *
 * ---------------------------------------------------------------------------
 * DEFAULT-ON / KILL SWITCH
 * ---------------------------------------------------------------------------
 * Active by default (no opt-in).  Kill switch: SOBER_PROVENANCE_GATE=0 (also
 * "off"/"no"/"false", case-insensitive) makes the recorder inert, so no
 * genuine credential can ever be produced and the marker stays withheld.  The
 * kill switch is therefore FAIL-CLOSED: disabling provenance cannot manufacture
 * a green.  It is NOT a diagnostic opt-in and must NOT be added to the
 * seeded_state withhold chain (the opposite of SOBER_CALLBACK_PROVENANCE).
 *
 * ---------------------------------------------------------------------------
 * FROZEN ENGINE RVAs (BuildID 204ad049599c96afb75b8a21d5051063e821d6c9,
 * SHA-256 3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335)
 * ---------------------------------------------------------------------------
 *   base+0x2584960  return site of `call *0x470(%rax)` (JNIEnv+0x470 == slot
 *                   142 == CallStaticVoidMethodV) inside thunk 0x25848e0.
 *   base+0x27eafb3  notifier 0x27eaf6e `call 0x25848e0` return site: the frame
 *                   directly above 0x2584960 on a GENUINE chain.
 *   base+0x2eb4bb1  onAppBridgeNotification(String,String) reader 0x2eb4b96
 *                   `call 0x25848e0` (at 0x2eb4bac) return site: DISQUALIFIED.
 *   base+0x72a2f48  cached jclass NativeGLJavaInterface (NewGlobalRef).
 *   base+0x72a2fc0  cached jmethodID gameLoadedCallback "(J)V".
 * Evidence: analysis/callback/callback-map.md, analysis/REVIEW_A7_callbacks.md
 * :214-229, src/jni/notifier_probe.c:301-317, src/jni/callback_provenance.c.
 *
 * ---------------------------------------------------------------------------
 * LEAD WIRING (lead-owned edits; NOT made here)
 * ---------------------------------------------------------------------------
 *   #include "provenance_gate.h"          in src/jni/native_jni.c
 *   -- once, after the guest is mapped/relocated (lead has the load bias):
 *       sober_provenance_gate_install(guest_base);
 *   -- in sr_dispatch_call(), right after `name` is resolved (near the existing
 *      sober_callback_provenance_on_dispatch call):
 *       sober_provenance_gate_on_dispatch(name, obj, method);
 *   -- in the decision-013 gate (guest_app_shell.c), replace the name-only
 *      shell_cb test with the positive credential, e.g.:
 *       int shell_cb = sober_provenance_gate_is_genuine("gameLoadedCallback");
 *       if (sober_provenance_gate_is_harness_thunk()) shell_cb = 0;
 *       // then the existing shell_cb && render_ok && !seeded_state gate.
 *   No CMake edit belongs to this module's owner: add provenance_gate.c to the
 *   sober_jni source list yourself (lead-owned CMakeLists.txt).
 */
#ifndef SOBER_PROVENANCE_GATE_H
#define SOBER_PROVENANCE_GATE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__GNUC__) || defined(__clang__)
#  define SOBER_PROV_GATE_API __attribute__((visibility("default")))
#else
#  define SOBER_PROV_GATE_API
#endif

/* ---- environment -------------------------------------------------------- */
#define SOBER_PROVENANCE_GATE_ENV       "SOBER_PROVENANCE_GATE"        /* kill switch */
#define SOBER_PROVENANCE_GATE_BASE_ENV  "SOBER_PROVENANCE_GATE_GUEST_BASE" /* optional, read-only base hint */

/* ---- frozen build identity (informational / fail-closed guard) ---------- */
#define SOBER_PROV_GATE_BUILD_ID_HEX \
    "204ad049599c96afb75b8a21d5051063e821d6c9"
#define SOBER_PROV_GATE_SHA256_HEX \
    "3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335"

/* ---- frozen guest RVAs (relative to the load bias) ---------------------- *
 * Full id=1 MessageBus dispatch chain, verified by raw disassembly of the
 * frozen image (first R E PT_LOAD has RVA == file offset):
 *   0x27eaf04  subscription node dispatch entry
 *   0x27eaf2f  `call QWORD PTR [rax+0x30]` -> descriptor invoke
 *   0x27eaf32  RETURN SITE of that call (proof that leg 0x27eaf04->0x27eaf4c
 *              ran; the invoke tail-jmps to the notifier so the word stays live)
 *   0x27eaf4c  id=1 descriptor invoke (`jmp rcx` at 0x27eaf6b)
 *   0x27eaf6e  notifier entry (loads the cached class/method globals)
 *   0x27eafae  `call 0x25848e0` inside the notifier
 *   0x27eafb3  return site of that call (high word of the RA pair)
 *   0x25848e0  generic JNI thunk entry
 *   0x258495a  `call *0x470(%rax)` (JNI slot 142 = CallStaticVoidMethodV)
 *   0x2584960  return site of that call (low word of the RA pair)
 */
#define SOBER_PROV_GATE_RVA_NODE_DISPATCH   0x27eaf04u
#define SOBER_PROV_GATE_RVA_PUBLISH_CALL    0x27eaf2fu
#define SOBER_PROV_GATE_RVA_DISPATCH_RET    0x27eaf32u
#define SOBER_PROV_GATE_RVA_DESCRIPTOR_INVOKE 0x27eaf4cu
#define SOBER_PROV_GATE_RVA_NOTIFIER        0x27eaf6eu
#define SOBER_PROV_GATE_RVA_THUNK_ENTRY     0x25848e0u
#define SOBER_PROV_GATE_RVA_THUNK_CALL_RET  0x2584960u  /* CallStaticVoidMethodV return */
#define SOBER_PROV_GATE_RVA_NOTIFIER_RET    0x27eafb3u  /* gameLoadedCallback notifier return */
#define SOBER_PROV_GATE_RVA_APPBRIDGE_RET   0x2eb4bb1u  /* onAppBridgeNotification reader return */
#define SOBER_PROV_GATE_RVA_CACHED_CLASS    0x72a2f48u  /* cached NativeGLJavaInterface jclass */
#define SOBER_PROV_GATE_RVA_CACHED_METHOD   0x72a2fc0u  /* cached gameLoadedCallback (J)V methodID */
/* placeId cached by nativeAppBridgeV2StartGameWithParam (0x2ebd86a).  GENUINE
 * requires it > 0 (a real Java StartGameParams.placeId()J), never a fabricated
 * positive value.  Read-only sample taken at dispatch time. */
#define SOBER_PROV_GATE_RVA_PLACE_ID        0x72a31c0u
/* JavaVM* cached by the skipped guest JNI_OnLoad at base+0x7cdec78 (the
 * 0x227b8fb/0x227b92d store).  The notifier's helper 0x227b9e2 calls GetEnv
 * (+0x30) on this pointer; if it is NULL the chain faults before 0x27eafae
 * reaches the thunk.  Read-only sample: a NULL global can only REMOVE a
 * credential (fail closed), never manufacture one. */
#define SOBER_PROV_GATE_RVA_JAVA_VM         0x7cdec78u
/* Guest JavaVM vtable slot +0x30 (GetEnv) - fail-closed identity check. */
#define SOBER_PROV_GATE_RVA_JAVAVM_GETENV   0x6c8e8a8u

/* ---- qualifying dispatch name ------------------------------------------ */
#define SOBER_PROV_GATE_QUALIFYING_NAME     "gameLoadedCallback"

/* ---- compact verdict codes (for describe()/snapshot()) ------------------ */
#define SOBER_PROV_GATE_VERDICT_NONE          0  /* no dispatch observed        */
#define SOBER_PROV_GATE_VERDICT_GENUINE       1  /* notifier-driven, cached IDs */
#define SOBER_PROV_GATE_VERDICT_HARNESS_THUNK 2  /* host return above thunk     */
#define SOBER_PROV_GATE_VERDICT_APPBRIDGE     3  /* onAppBridgeNotification      */
#define SOBER_PROV_GATE_VERDICT_UNCLASSIFIED  4  /* no thunk return site        */

/* ---- evidence snapshot -------------------------------------------------- */
typedef struct {
    uintptr_t guest_base;          /* resolved load bias (0 if unresolved)     */
    int       enabled;             /* kill-switch state (default 1)            */
    int       installed;           /* base resolved + observer ready           */
    int       build_verified;      /* frozen BuildID matched                   */
    uint64_t  dispatch_count;      /* all dispatches with the gate active      */
    uint64_t  genuine_count;       /* genuine observations (qualifying name)   */
    uint64_t  harness_count;       /* host-return thunk observations           */
    uint64_t  appbridge_count;     /* onAppBridgeNotification observations     */
    uintptr_t thunk_return_addr;   /* last base+0x2584960 observed (0 if none) */
    uintptr_t caller_return_addr;  /* last caller RA observed (0 if none)      */
    uintptr_t node_caller_addr;    /* node dispatch 0x27eaf04 caller RA: guest
                                    * .text on a genuine MessageBus dispatch,
                                    * host code on a host-entered re-drive     */
    uintptr_t cached_class;        /* value read at base+0x72a2f48            */
    uintptr_t cached_method;       /* value read at base+0x72a2fc0            */
    int64_t   place_id;            /* read at base+0x72a31c0 at dispatch time  */
    uintptr_t java_vm_global;      /* value read at base+0x7cdec78            */
    int       last_verdict;        /* one of SOBER_PROV_GATE_VERDICT_*         */
} SoberProvenanceGateSnapshot;

/* ---- enable / install --------------------------------------------------- */

/* 1 unless SOBER_PROVENANCE_GATE is exactly 0/off/no/false (case-insensitive). */
SOBER_PROV_GATE_API int sober_provenance_gate_enabled(void);

/* Return codes for install(). */
#define SOBER_PROV_GATE_OK             0
#define SOBER_PROV_GATE_DISABLED      (-1) /* kill switch: inert, not a fault   */
#define SOBER_PROV_GATE_NO_BASE       (-2) /* guest base could not be resolved  */

/* One-time, idempotent install.  guest_base may be NULL to auto-discover via
 * /proc/self/maps (largest r-x mapping, verified against the frozen BuildID);
 * SOBER_PROVENANCE_GATE_GUEST_BASE overrides discovery (read-only hint).
 * Returns SOBER_PROV_GATE_OK when enabled+resolved, negative otherwise.
 * Safe to call on every dispatch; cheap when already installed. */
SOBER_PROV_GATE_API int sober_provenance_gate_install(void *guest_base);

/* Clear all observations/counters (for tests); re-reads the kill switch. */
SOBER_PROV_GATE_API void sober_provenance_gate_reset(void);

/* Read-only: verify a mapped load bias carries the frozen NT_GNU_BUILD_ID. */
SOBER_PROV_GATE_API int sober_provenance_gate_verify_build(void *guest_base);

/* ---- THE HOOK (call from sr_dispatch_call) ------------------------------ */

/* Record one dispatch.  `clazz` is the jclass argument (obj), `method` the
 * jmethodID.  Never calls into guest code, never invokes a handler, never
 * writes a marker/gate.  No-op when the kill switch is set.  Lazily installs
 * (auto-discovering the base) if install() was not called first. */
SOBER_PROV_GATE_API void sober_provenance_gate_on_dispatch(const char *name,
                                                          void *clazz,
                                                          void *method);

/* ---- THE QUERIES (call from the decision-013 gate) ---------------------- */

/* 1 only after a dispatch of `name` with the full positive GENUINE chain:
 * thunk return 0x2584960 + notifier return 0x27eafb3 above it + cached
 * class/method identity + frozen build + qualifying name.  Always 0 for
 * onAppBridgeNotification and for any harness/ALT call. */
SOBER_PROV_GATE_API int sober_provenance_gate_is_genuine(const char *name);

/* 1 (sticky) after a thunk return site was seen with a HOST/unknown caller
 * frame -- the ALT/harness path.  Global taint: the gate should force the
 * marker withheld whenever this is set. */
SOBER_PROV_GATE_API int sober_provenance_gate_is_harness_thunk(void);

/* Diagnostic-only: 1 (sticky) after an onAppBridgeNotification return site
 * (base+0x2eb4bb1) was seen.  Never genuine, never harness thunk. */
SOBER_PROV_GATE_API int sober_provenance_gate_is_appbridge_notification(void);

/* ---- evidence accessors ------------------------------------------------- */
SOBER_PROV_GATE_API uint64_t  sober_provenance_gate_dispatch_count(void);
SOBER_PROV_GATE_API uint64_t  sober_provenance_gate_genuine_count(const char *name);
SOBER_PROV_GATE_API uint64_t  sober_provenance_gate_harness_count(void);
SOBER_PROV_GATE_API uint64_t  sober_provenance_gate_appbridge_count(void);
SOBER_PROV_GATE_API uintptr_t sober_provenance_gate_guest_base(void);
SOBER_PROV_GATE_API uintptr_t sober_provenance_gate_thunk_return_addr(void);
SOBER_PROV_GATE_API uintptr_t sober_provenance_gate_caller_return_addr(void);
/* Return address of whoever entered node dispatch 0x27eaf04 (the guest
 * MessageBus dispatcher on the genuine chain, host code on a re-drive).
 * 0 when no complete notifier->dispatch frame chain was observed. */
SOBER_PROV_GATE_API uintptr_t sober_provenance_gate_node_caller_return_addr(void);
SOBER_PROV_GATE_API uintptr_t sober_provenance_gate_cached_class(void);
SOBER_PROV_GATE_API uintptr_t sober_provenance_gate_cached_method(void);
/* placeId sampled read-only from base+0x72a31c0 at the last dispatch. */
SOBER_PROV_GATE_API int64_t   sober_provenance_gate_place_id(void);
/* JavaVM* sampled read-only from base+0x7cdec78 (0 = not live/not sampled). */
SOBER_PROV_GATE_API uintptr_t sober_provenance_gate_java_vm_global(void);

/* ---- clean-state readiness (read-only, dispatch-independent) ------------ */
/* Bitmask of the STATICALLY observable legs of the task #531 readiness
 * predicate.  The remaining legs -- thunk RA 0x2584960 + notifier RA
 * 0x27eafb3, descriptor-invoke RA 0x27eaf32 and node dispatch 0x27eaf04
 * entered from guest .text -- exist only on a live dispatch and are certified
 * per-call by on_dispatch()/is_genuine(); they are deliberately NOT asserted
 * here, so a readiness PASS can never be mistaken for a callback credential. */
#define SOBER_PROV_GATE_READY_BASE        (1u << 0) /* base resolved            */
#define SOBER_PROV_GATE_READY_BUILD       (1u << 1) /* frozen BuildID verified  */
#define SOBER_PROV_GATE_READY_JAVAVM      (1u << 2) /* 0x7cdec78 non-NULL       */
#define SOBER_PROV_GATE_READY_CACHED_CLASS  (1u << 3) /* 0x72a2f48 non-NULL     */
#define SOBER_PROV_GATE_READY_CACHED_METHOD (1u << 4) /* 0x72a2fc0 non-NULL     */
#define SOBER_PROV_GATE_READY_PLACE_ID     (1u << 5) /* 0x72a31c0 > 0           */
#define SOBER_PROV_GATE_READY_ALL  (SOBER_PROV_GATE_READY_BASE | \
                                   SOBER_PROV_GATE_READY_BUILD | \
                                   SOBER_PROV_GATE_READY_JAVAVM | \
                                   SOBER_PROV_GATE_READY_CACHED_CLASS | \
                                   SOBER_PROV_GATE_READY_CACHED_METHOD | \
                                   SOBER_PROV_GATE_READY_PLACE_ID)
/* Lazily installs (auto-discovers the base), samples the five globals
 * read-only, fills buf (when given) with a neutral one-line leg-by-leg report
 * and returns the bitmask above.  NEVER emits a marker/provenance line, never
 * calls into the guest and never fabricates a value.  A kill switch or missing
 * base simply clears the corresponding bits. */
SOBER_PROV_GATE_API uint32_t sober_provenance_gate_readiness(char *buf,
                                                            size_t cap);

/* Emit the gate-owned positive provenance line ONCE, and only when the full
 * credential holds (GENUINE + placeId>0 + no harness taint).  Returns 1 when
 * the line was emitted, 0 when withheld.  This is a diagnostic observation,
 * NEVER a decision-013 marker and NEVER a fabricated callback: the line quotes
 * only values actually observed by the credential.  The lead calls it from
 * the strict gate after checking seeded_state==0. */
SOBER_PROV_GATE_API int sober_provenance_gate_emit_provenance(void);
SOBER_PROV_GATE_API int       sober_provenance_gate_last_verdict(void);
SOBER_PROV_GATE_API const char *sober_provenance_gate_verdict_name(int verdict);
SOBER_PROV_GATE_API void      sober_provenance_gate_snapshot(SoberProvenanceGateSnapshot *out);

/* One-line, neutral diagnostic description into buf (never writes stderr). */
SOBER_PROV_GATE_API const char *sober_provenance_gate_describe(char *buf,
                                                              size_t cap);

#ifdef __cplusplus
}
#endif
#endif /* SOBER_PROVENANCE_GATE_H */
