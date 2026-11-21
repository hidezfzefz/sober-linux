/*
 * frame_evidence.h - DEFAULT-ON, observation-only render-leg evidence recorder
 *                    with fail-closed guest-window provenance
 *                    (logical Team 3 / group C, agent F7).
 *
 * SCOPE / HARD RULES
 * ------------------
 *  - NEW FILES ONLY: this header and frame_evidence.c are the only files this
 *    agent touches.  The lead owns shared src/, every CMakeLists.txt, docs/,
 *    tests/ and git; the engine is frozen.  Nothing here edits them.
 *  - Observation only.  This module NEVER emits a decision-013 marker, NEVER
 *    invokes a JNI handler/callback, NEVER patches guest bytes and NEVER writes
 *    a guest global.  It records what a wiring site reports and answers
 *    queries.  No withhold/gate decision is read or changed here.  Neutral
 *    stderr diagnostics are the one exception (see NORMATIVE LOG LINES), and
 *    they are not markers.
 *  - No counter inflation.  Each observer records exactly one immutable event
 *    and moves its counters by exactly one; accessors never inflate state; a
 *    full log drops the event AND its count.  No implicit observations.
 *  - Immutable, auditable records.  Every counted event has a write-once record
 *    with sequence, monotonic timestamp, caller RA, guest base, kind and tag.
 *    Records are exposed by copy-out only and are never rewritten.
 *  - Default ON.  Kill switch SOBER_FRAME_EVIDENCE=0 (exact string) makes every
 *    observer inert and every decisive accessor 0/REJECT.
 *  - Fail-closed on mixed evidence.  A guest swap plus a host/synthetic swap
 *    sets a sticky taint that zeroes the decisive render counts.  A host write
 *    to the window global is likewise never accepted as binding.
 *
 * ---------------------------------------------------------------------------
 * RENDER CONTRACT (analysis/MODULE_INTEGRATION.md section 2)
 * ---------------------------------------------------------------------------
 *     render_ok = sober_render_phase3_done() && guest_render_ok()
 *
 *     guest_render_ok() == phase3_done && swaps_guest>=2 && stable_ra
 *                          && draws>=1 && draws_between>=1 && window_bound
 *
 * stable_ra means the same guest return address recurs on >=2 tagged swaps
 * (T3-5 section 4 / R6 section 4.2); a single tagged swap fails closed.  The
 * phase3 leg comes from the weak sober_render_phase3_done() symbol (absent =>
 * 0).  fromSurface is supporting only: both 'installed' and 'native' are valid.
 *
 * NORMATIVE LOG LINES (analysis/G2_gate_test_contract.md section 3.2)
 * -------------------------------------------------------------------
 * When the render legs first become decisive the module replays its immutable
 * swap records to stderr exactly once (atomic once-guard), so every line quotes
 * the SAME final aggregate N/M:
 *   [render] guest-tagged swap at 0x649d422 ra=base+0x<RVA> tag=guest-loop | guest-tagged swaps: <N> | guest GLES draws between tagged swaps: <M>
 *   [render] guest render verdict: guest-tagged frame confirmed (swaps=<N>, draws=<M>, fromSurface=native|installed, window=bound) -> GUEST-TAGGED
 *   [render] guest render verdict: guest-tagged frame confirmed (swaps=<N> draws=<M> fromSurface=native|installed window=bound) -> GUEST-TAGGED
 * The two verdict spellings cover both G2 patterns (:209 wants commas, :217
 * wants spaces).  The gate-owned '[app-shell] render provenance: ... -> OK'
 * line stays with the lead; emit it with the same N/M from these accessors.
 *
 * ---------------------------------------------------------------------------
 * GUEST-WINDOW PROVENANCE (analysis/G8_guest_window_detection.md)
 * ---------------------------------------------------------------------------
 * A bare non-NULL *(base+0x72a3308) is HOST-FORGEABLE (render_guest.c stores at
 * :417-423, :324-329, :461-462).  `window_bound` is therefore 0 by default and
 * becomes 1 ONLY when the caller feeds a coherent observation ledger through
 * frame_evidence_window_observation() and every G8 condition C0-C10 holds:
 *   C0 integrity engine+observers+layout verified, one tid, monotonic seq
 *   C1 no host-store observation and host publish disabled for the run
 *   C2 exactly one fromSurface observation with RA base+0x26fea7d and a
 *      verified 5-byte `e8 rel32` call to base+0x6d31ad0; no other fromSurface
 *   C3 real Surface: GetObjectClass/GetFieldID(mNativeObject:J)/GetLongField
 *      succeeded (status=1) and mnative != 0
 *   C4 ret != 0, ret == mnative, ret != W_before (different-window branch)
 *   C5 G_now == (g_before + 1) mod 2^32 (guest inc at 0x26fea90)
 *   C6 W_now == ret != 0
 *   C7 if W_before != 0: release observation RA base+0x26feaa0 on W_before
 *   C8 acquire observation RA base+0x649c2db on W_now
 *   C9 getWidth/getHeight observation RA base+0x649c308/0x649c314, positive
 *      dims, coherent layout
 *   C10 guest 'Created ANativeWindow ... (ID:G)' log on W_now with ID == G_now
 * Any missing/unreadable/mixed signal => REJECT with a normative R* token.
 * The observers live in lead-owned hooks; this module only receives their
 * records and never fabricates one.
 *
 * ---------------------------------------------------------------------------
 * FROZEN BUILD / RVA INVENTORY (BuildID 204ad049..., SHA 3e498d64...)
 * ---------------------------------------------------------------------------
 *   [base, base+0x6d33b90)     guest .text window for RA classification
 *   base+0x722a1a8             eglSwapBuffers GOT JUMP_SLOT (swap observer)
 *   base+0x72294a8             ANativeWindow_fromSurface GOT
 *   base+0x72294a0 / 0x72297c0 release / acquire GOT
 *   base+0x72a3308 / 0x72a3310 ANativeWindow* global / generation counter
 *   base+0x26fea78 / 0x26fea7d helper fromSurface call / store-tied RA
 *   base+0x26feaa0             release-old RA / guest store 0x26feaa0
 *   base+0x649c2db / 0x649c308 / 0x649c314 acquire / getWidth / getHeight RA
 */
#ifndef SOBER_JNI_FRAME_EVIDENCE_H
#define SOBER_JNI_FRAME_EVIDENCE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__GNUC__) || defined(__clang__)
#  define SOBER_FRAME_EVIDENCE_API __attribute__((visibility("default")))
#else
#  define SOBER_FRAME_EVIDENCE_API
#endif

/* ---- gate ---------------------------------------------------------------- */
#define SOBER_FRAME_EVIDENCE_ENV "SOBER_FRAME_EVIDENCE"
/* DEFAULT ON: false only when getenv(SOBER_FRAME_EVIDENCE) is exactly "0". */
SOBER_FRAME_EVIDENCE_API int         frame_evidence_enabled(void);
SOBER_FRAME_EVIDENCE_API const char *frame_evidence_env_var(void);

/* ---- frozen constants ---------------------------------------------------- */
#define SOBER_FRAME_EVIDENCE_VERSION      1u
#define SOBER_FRAME_EVIDENCE_TEXT_SIZE    0x6d33b90u  /* .text span for RA tag */
#define SOBER_FRAME_EVIDENCE_G_WINDOW     0x72a3308u
#define SOBER_FRAME_EVIDENCE_G_WINDOW_GEN 0x72a3310u

/* G8 guest-window constants (all RVA from the frozen load bias). */
#define SOBER_FRAME_W_GOT_FROM_SURFACE 0x72294a8u
#define SOBER_FRAME_W_GOT_RELEASE      0x72294a0u
#define SOBER_FRAME_W_GOT_ACQUIRE      0x72297c0u
#define SOBER_FRAME_W_GOT_GET_WIDTH    0x72297d0u
#define SOBER_FRAME_W_GOT_GET_HEIGHT   0x72297c8u
#define SOBER_FRAME_W_PLT_FROM_SURFACE 0x6d31ad0u
#define SOBER_FRAME_W_RA_HELPER        0x26fea7du  /* store-tied fromSurface RA */
#define SOBER_FRAME_W_RA_RELEASE_OLD   0x26feaa0u  /* RA of 0x26fea9b release call */
#define SOBER_FRAME_W_RA_ACQUIRE       0x649c2dbu
#define SOBER_FRAME_W_RA_GET_WIDTH     0x649c308u
#define SOBER_FRAME_W_RA_GET_HEIGHT    0x649c314u
#define SOBER_FRAME_W_RA_LIFECYCLE_1   0x2a20881u  /* must be REJECTed */
#define SOBER_FRAME_W_RA_LIFECYCLE_2   0x2a2090cu

#define SOBER_FRAME_EV_MAGIC 0x53464552u  /* "SFER" */

/* Event kinds. */
#define SOBER_FRAME_EV_SWAP        1u
#define SOBER_FRAME_EV_DRAW        2u
#define SOBER_FRAME_EV_WINDOW      3u
#define SOBER_FRAME_EV_FROMSURFACE 4u

/* Provenance tags.  UNKNOWN (no guest base) is always non-guest. */
#define SOBER_FRAME_TAG_GUEST     1u
#define SOBER_FRAME_TAG_SYNTHETIC 2u
#define SOBER_FRAME_TAG_UNKNOWN   3u

/* ---- immutable audit record --------------------------------------------- */
typedef struct SoberFrameEvidenceEvent {
    uint32_t  magic;
    uint32_t  kind;
    uint32_t  tag;
    uint32_t  reserved;
    uint64_t  seq;
    uint64_t  timestamp_ns;
    uintptr_t ra;
    uintptr_t guest_base;
    uintptr_t arg0;
    uintptr_t arg1;
} SoberFrameEvidenceEvent;

/* ---- G8 window observation ledger --------------------------------------- */
/* Kinds fed by the lead-owned GOT observers.  This module fabricates none. */
#define SOBER_FRAME_WOBS_FROMSURFACE 1u  /* GOT 0x72294a8                   */
#define SOBER_FRAME_WOBS_RELEASE     2u  /* GOT 0x72294a0                   */
#define SOBER_FRAME_WOBS_ACQUIRE     3u  /* GOT 0x72297c0                   */
#define SOBER_FRAME_WOBS_DIMS        4u  /* getWidth/getHeight              */
#define SOBER_FRAME_WOBS_HOST_STORE  5u  /* render_guest install/republish  */
#define SOBER_FRAME_WOBS_SNAPSHOT    6u  /* gate-time W_now/G_now           */
#define SOBER_FRAME_WOBS_GUEST_LOG   7u  /* H6 Created...(ID:G)             */

/* host_kind values for SOBER_FRAME_WOBS_HOST_STORE. */
#define SOBER_FRAME_WOBS_HOST_INSTALL    1u
#define SOBER_FRAME_WOBS_HOST_REPUBLISH  2u
#define SOBER_FRAME_WOBS_HOST_ROLLBACK   3u

typedef struct SoberFrameWindowObservation {
    uint32_t  kind;         /* SOBER_FRAME_WOBS_*                            */
    uint32_t  status;       /* 1 = read/call succeeded; 0 = fault/unreadable */
    uint64_t  seq;          /* caller-supplied monotonic sequence            */
    uint64_t  tid;          /* observing thread id (must match across ledger)*/
    uintptr_t ra;           /* caller return address (absolute)              */
    uintptr_t window;       /* FS: ret; REL/ACQ/DIMS: arg; SNAP: W_now; LOG: ptr */
    uintptr_t surface;      /* FS: jobject                                   */
    uintptr_t mnative;      /* FS: mNativeObject value from JNI              */
    uintptr_t w_before;     /* FS: window at observer entry                  */
    uint32_t  g_before;     /* FS: generation at entry; HOST: before value   */
    uint32_t  g_now;        /* SNAP: G_now; LOG: logged ID; HOST: after value*/
    uint32_t  width;        /* DIMS                                          */
    uint32_t  height;       /* DIMS                                          */
    uint32_t  host_kind;    /* HOST_STORE: SOBER_FRAME_WOBS_HOST_*           */
    uint32_t  reserved;
} SoberFrameWindowObservation;

/* ---- G8 verdict codes / rejection tokens -------------------------------- */
#define SOBER_FRAME_WVERDICT_REJECT 0
#define SOBER_FRAME_WVERDICT_ACCEPT 1

#define SOBER_FRAME_WR_NO_OBS       0
#define SOBER_FRAME_WR_R0           1   /* R0_UNVERIFIED_ENGINE */
#define SOBER_FRAME_WR_R1           2   /* R1_HOST_PUBLISH */
#define SOBER_FRAME_WR_R2_RA        3   /* R2_RA_NOT_HELPER */
#define SOBER_FRAME_WR_R2_MIXED     4   /* R2_MIXED_FROMSURFACE */
#define SOBER_FRAME_WR_R2_OPCODE    5   /* R2_OPCODE_MISMATCH */
#define SOBER_FRAME_WR_R3           6   /* R3_NO_MNATIVEOBJECT */
#define SOBER_FRAME_WR_R4           7   /* R4_RET_MISMATCH */
#define SOBER_FRAME_WR_R5           8   /* R5_GENERATION */
#define SOBER_FRAME_WR_R6           9   /* R6_WINDOW_MISMATCH */
#define SOBER_FRAME_WR_R7          10   /* R7_NO_RELEASE */
#define SOBER_FRAME_WR_R8          11   /* R8_NO_ACQUIRE */
#define SOBER_FRAME_WR_R9          12   /* R9_LAYOUT */
#define SOBER_FRAME_WR_R10         13   /* R10_NO_LOG */

/* ---- render verdict codes ------------------------------------------------ */
#define SOBER_FRAME_VERDICT_DISABLED     0u
#define SOBER_FRAME_VERDICT_ABSENT       1u
#define SOBER_FRAME_VERDICT_MIXED        2u
#define SOBER_FRAME_VERDICT_INCOMPLETE   3u
#define SOBER_FRAME_VERDICT_GUEST_TAGGED 4u

/* ---- process-global snapshot -------------------------------------------- */
typedef struct SoberFrameEvidenceSnapshot {
    int       enabled;
    uintptr_t guest_base;
    uint32_t  text_size;
    uint64_t  swaps_total;
    uint64_t  swaps_guest_recorded;
    uint64_t  swaps_synthetic;
    uint64_t  draws_total;
    uint64_t  draws_guest_recorded;
    uint64_t  draws_synthetic;
    uint64_t  draws_between;
    uintptr_t loop_ra;
    int       loop_ra_stable;
    uintptr_t window;              /* raw last accepted window (audit only)   */
    uint32_t  window_generation;
    uintptr_t window_ra;
    uint64_t  window_ns;
    int       window_bound;        /* G8 decisive value                       */
    int       window_verdict;      /* SOBER_FRAME_WVERDICT_*                  */
    int       window_reject_code;  /* SOBER_FRAME_WR_*                        */
    int       from_surface_installed;
    int       mixed;
    uint64_t  events;
    uint64_t  dropped;
    uint64_t  first_ns;
    uint64_t  last_ns;
    int       verdict;
    int       guest_render_ok;
} SoberFrameEvidenceSnapshot;

/* ---- setup / classification --------------------------------------------- */
SOBER_FRAME_EVIDENCE_API void      frame_evidence_set_guest_base(uintptr_t base);
SOBER_FRAME_EVIDENCE_API uintptr_t frame_evidence_guest_base(void);
SOBER_FRAME_EVIDENCE_API int       frame_evidence_ra_is_guest(uintptr_t ra);
/* Read-only check: 5 bytes at ra-5 decode as e8 rel32 whose target is exactly
 * `target`.  Used for G8 C2c (store-tied fromSurface call site). */
SOBER_FRAME_EVIDENCE_API int frame_evidence_verify_call_site(uintptr_t ra,
                                                             uintptr_t target);

/* ---- render observers (one immutable event each) ------------------------ */
SOBER_FRAME_EVIDENCE_API void frame_evidence_record_swap(uintptr_t ra);
SOBER_FRAME_EVIDENCE_API void frame_evidence_record_draw(uintptr_t ra);
/* T3-5 supporting evidence: fromSurface RA at one of the three guest sites
 * sets fromSurface=installed; otherwise the mode stays 'native'. */
SOBER_FRAME_EVIDENCE_API void frame_evidence_record_from_surface(uintptr_t ra);
/* Raw window audit only: NEVER sets the decisive window_bound (that requires
 * the G8 ledger).  Accepts only non-NULL/guest-RA/increasing generation for
 * the recorded raw fields. */
SOBER_FRAME_EVIDENCE_API void frame_evidence_record_window_bind(
    uintptr_t window, uint32_t generation, uintptr_t ra);
/* Read-only global sample; forwards to record_window_bind().  Returns the
 * decisive G8 window_bound (0 unless the ledger proves guest provenance). */
SOBER_FRAME_EVIDENCE_API int frame_evidence_observe_window_global(
    uintptr_t guest_base, uintptr_t ra);

/* ---- G8 guest-window observation API (fail-closed ledger) --------------- */
/* Feed one observation from a lead-owned hook.  Returns 1 when stored. */
SOBER_FRAME_EVIDENCE_API int frame_evidence_window_observation(
    const SoberFrameWindowObservation *obs);
/* Integrity declaration (defaults: 0,0,0 => REJECT).  host_publish defaults
 * to 1 (host stores are active); the caller must declare it disabled. */
SOBER_FRAME_EVIDENCE_API void frame_evidence_window_set_integrity(
    int engine_verified, int observers_complete, int layout_ok);
SOBER_FRAME_EVIDENCE_API void frame_evidence_window_set_host_publish_enabled(
    int enabled);
SOBER_FRAME_EVIDENCE_API int  frame_evidence_window_verdict(void);
SOBER_FRAME_EVIDENCE_API const char *frame_evidence_window_reject_reason(void);

/* ---- five-ANativeWindow-GOT observation hooks (observation only) --------- *
 * Install chained, read-only-forwarding observers on the five imported
 * ANativeWindow GOT JUMP_SLOTs (0x72294a8 fromSurface, 0x72294a0 release,
 * 0x72297c0 acquire, 0x72297c8 getHeight, 0x72297d0 getWidth).  Every call is
 * forwarded unchanged to the genuine previous target; the only side effect is
 * one immutable G8 ledger observation per event (plus a supporting
 * fromSurface label).  No guest code byte, object vtable or guest global is
 * written.  This is the producer for G8 C2/C5/C7/C8/C9/C10; without it
 * `window_bound` stays REJECT and `guest_render_ok()` stays 0 (fail closed).
 *
 * Returns 0 on success, negative on failure; idempotent.  Hooks must be
 * installed TOGETHER: hooking only fromSurface corrupts the compat release
 * path (G8 I0).  `frame_evidence_window_snapshot()` feeds the H5 gate-time
 * W_now/G_now pair.  The caller, not this module, must declare engine/layout
 * verified and host publish disabled via the two setters above -- claiming
 * those without actually disabling render_guest's host stores would be a
 * false green, so the default stays REJECT.
 */
SOBER_FRAME_EVIDENCE_API int  frame_evidence_install_window_observers(
    uintptr_t guest_base);
SOBER_FRAME_EVIDENCE_API void frame_evidence_uninstall_window_observers(void);
SOBER_FRAME_EVIDENCE_API int  frame_evidence_window_observers_installed(void);
SOBER_FRAME_EVIDENCE_API void frame_evidence_window_snapshot(uintptr_t guest_base);

/* Feed the one G8 C2 fromSurface observation from the module that OWNS the
 * 0x72294a8 hook (frame_surface).  A C-level observer chained on top of that
 * hook would hide the true guest RA from it (__builtin_return_address(0) would
 * be the observer's host code), so the fromSurface slot is deliberately NOT
 * replaced by frame_evidence.  `env`/`surface` are the genuine values the
 * guest handed to ANativeWindow_fromSurface; `window` is that hook's return. */
SOBER_FRAME_EVIDENCE_API void frame_evidence_note_from_surface(
    uintptr_t ra, void *env, void *surface, uintptr_t window,
    uintptr_t w_before, uint32_t g_before);
/* Convenience: set guest base + install the five observers.  Does NOT assert
 * engine/layout integrity or host-publish-disabled; those remain the caller's
 * explicit, auditable declarations. */
SOBER_FRAME_EVIDENCE_API int  frame_evidence_attach(uintptr_t guest_base);

/* ---- one-shot genuine render-evidence bringup (lead wires this) ---------- *
 * Call ONCE from guest_app_shell.c after render_guest is confirmed installed,
 * passing the active native window and render size.  Idempotent; 0 on success.
 *
 * Sequence (all fail-closed):
 *   1. verify the frozen engine identity (bootstrap build-ID check);
 *   2. install frame_surface with the GENUINE 0x48-byte render context;
 *   3. install the five frame_evidence window observers ON TOP so the chain is
 *      FE -> frame_surface -> previous GOT target, and the store-tied
 *      fromSurface return address (base+0x26fea7d) is really observed;
 *   4. call the genuine guest bind helper 0x26fea5a(env, surface, logTag)
 *      under a fault guard (three args; the old two-arg call faulted);
 *   5. ONLY if the guest itself wrote 0x72a3308 with a +1 generation and the
 *      store-tied RA, disable render_guest's host stores, declare integrity
 *      (engine/observers/layout) and host_publish=0, and feed the H5 snapshot.
 *
 * Any failure leaves the fail-closed defaults (integrity 0, host_publish 1),
 * so window_bound()==0 and guest_render_ok()==0.  No marker, no callback, no
 * fabricated Surface/window/mNativeObject, no counter inflation.  Diagnostics
 * go to stderr as neutral "[frame-evidence]" lines only.
 */
SOBER_FRAME_EVIDENCE_API int sober_render_evidence_bringup(
    uintptr_t guest_base, void *native_window, unsigned width, unsigned height);
SOBER_FRAME_EVIDENCE_API int sober_render_evidence_bringup_rc(void);
/* 0: bringup introduces no new opt-in knob, therefore no withhold entry. */
SOBER_FRAME_EVIDENCE_API int sober_frame_evidence_should_withhold(void);

/* ---- decisive accessors -------------------------------------------------- */
SOBER_FRAME_EVIDENCE_API uint64_t frame_evidence_swaps_guest(void);
SOBER_FRAME_EVIDENCE_API uint64_t frame_evidence_draws(void);
SOBER_FRAME_EVIDENCE_API int      frame_evidence_window_bound(void);
SOBER_FRAME_EVIDENCE_API int      guest_render_ok(void);
/* Emit the G2 normative lines once when the render legs are decisive.  Called
 * automatically by the recorders and by guest_render_ok(); safe to call. */
SOBER_FRAME_EVIDENCE_API void     frame_evidence_emit_lines(void);

/* ---- raw immutable evidence accessors ------------------------------------ */
SOBER_FRAME_EVIDENCE_API uint64_t  frame_evidence_swaps_total(void);
SOBER_FRAME_EVIDENCE_API uint64_t  frame_evidence_swaps_guest_recorded(void);
SOBER_FRAME_EVIDENCE_API uint64_t  frame_evidence_swaps_synthetic(void);
SOBER_FRAME_EVIDENCE_API uint64_t  frame_evidence_draws_total(void);
SOBER_FRAME_EVIDENCE_API uint64_t  frame_evidence_draws_guest_recorded(void);
SOBER_FRAME_EVIDENCE_API uint64_t  frame_evidence_draws_synthetic(void);
SOBER_FRAME_EVIDENCE_API uint64_t  frame_evidence_draws_between(void);
SOBER_FRAME_EVIDENCE_API uintptr_t frame_evidence_loop_ra(void);
SOBER_FRAME_EVIDENCE_API int       frame_evidence_loop_ra_stable(void);
SOBER_FRAME_EVIDENCE_API uintptr_t frame_evidence_window(void);
SOBER_FRAME_EVIDENCE_API uint32_t  frame_evidence_window_generation(void);
SOBER_FRAME_EVIDENCE_API uintptr_t frame_evidence_window_ra(void);
SOBER_FRAME_EVIDENCE_API int       frame_evidence_from_surface_installed(void);
SOBER_FRAME_EVIDENCE_API const char *frame_evidence_from_surface_mode(void);
SOBER_FRAME_EVIDENCE_API int       frame_evidence_mixed(void);
SOBER_FRAME_EVIDENCE_API uint64_t  frame_evidence_event_count(void);
SOBER_FRAME_EVIDENCE_API uint64_t  frame_evidence_events_dropped(void);
SOBER_FRAME_EVIDENCE_API int frame_evidence_copy_event(
    size_t index, SoberFrameEvidenceEvent *out);
SOBER_FRAME_EVIDENCE_API void frame_evidence_guest_ra_range(
    uintptr_t *lo, uintptr_t *hi);
SOBER_FRAME_EVIDENCE_API void frame_evidence_draw_ra_range(
    uintptr_t *lo, uintptr_t *hi);

/* ---- verdict / snapshot / diagnostics ----------------------------------- */
SOBER_FRAME_EVIDENCE_API int         frame_evidence_verdict(void);
SOBER_FRAME_EVIDENCE_API const char *frame_evidence_verdict_name(int verdict);
SOBER_FRAME_EVIDENCE_API const char *frame_evidence_verdict_string(
    char *buf, size_t cap);
SOBER_FRAME_EVIDENCE_API void        frame_evidence_snapshot(
    SoberFrameEvidenceSnapshot *out);
SOBER_FRAME_EVIDENCE_API const char *frame_evidence_describe(
    char *buf, size_t cap);
SOBER_FRAME_EVIDENCE_API int frame_evidence_selftest(char *buf, size_t cap);

#ifdef __cplusplus
}
#endif
#endif /* SOBER_JNI_FRAME_EVIDENCE_H */
