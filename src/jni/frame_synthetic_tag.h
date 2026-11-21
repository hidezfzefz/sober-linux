/*
 * frame_synthetic_tag.h - DEFAULT-ON explicit tag for the host-originated
 *                         phase-3 Route-B swap (logical group C, agent F8).
 *
 * SCOPE: NEW FILES ONLY -- this header and frame_synthetic_tag.c are the only
 * files this agent touches.  The lead owns src/ (including guest_render.c and
 * render_guest.c), every CMakeLists.txt, docs/, tests/ and git; the engine is
 * frozen.  Nothing here edits them.  The lead wires the API below.
 *
 * ---------------------------------------------------------------------------
 * WHAT THIS IS (and what it is NOT)
 * ---------------------------------------------------------------------------
 * In the default phase-3 path the harness calls the guest swap wrapper at
 * RVA 0x649d422 directly from host code with a host-built 0x48-byte context
 * (analysis/R1_zero_tagged_swaps.md section 3, "Route B").  The swap is real
 * (guest PLT/GOT is traversed and the real driver is called), but its return
 * address is in the HOST, so it is a SYNTHETIC swap, never a guest frame-loop
 * swap.  Today the only discriminator is return-address tagging in
 * render_guest.c's `rg_hook_swap()` (RA inside guest .text => guest).  That
 * discriminator is correct but implicit and fragile: any change to the call
 * chain that happens to leave an RA inside guest .text could misclassify our
 * own swap as guest evidence.
 *
 * This module supplies an EXPLICIT, host-controlled tag scoped around that
 * one direct call, so the swap can never be counted as guest evidence even if
 * the RA heuristic were fooled.  It is pure bookkeeping:
 *
 *   - It does NOT emit a marker, does NOT invoke a callback, patches no guest
 *     byte, and writes no guest global (engine frozen).
 *   - It does NOT suppress or skip the actual guest swap: the lead still makes
 *     the 0x649d422 call unconditionally.
 *   - It does NOT touch any guest counter (sober_render_guest_swaps_guest_tagged,
 *     _synthetic, _total, gles_draw_calls, ...).  Those remain exactly as they
 *     are.  `tests/first-frame.sh` and its `guest-driven swaps observed: 1`
 *     line keep their current semantics.
 *
 * The tag is a classification *hint* for the swap counter hook plus a sticky
 * diagnostic flag; the lead decides how to use it in the decision-013 gate.
 * It never by itself lifts or lowers a marker.
 *
 * ---------------------------------------------------------------------------
 * DEFAULT-ON / KILL SWITCH
 * ---------------------------------------------------------------------------
 * Active by default (no opt-in).  Kill switch: SOBER_FRAME_SYNTH_TAG=0 (also
 * "off"/"no"/"false", case-insensitive) makes every entry point inert: no
 * scope is recorded, active() is always 0 and the sticky flag is never set.
 * Disabling the tag is FAIL-SAFE -- the existing RA classifier still tags the
 * Route-B swap synthetic by its host return address, so no false green can be
 * manufactured by killing this module.  This is NOT a diagnostic opt-in and
 * must NOT be added to the seeded_state withhold chain.
 *
 * ---------------------------------------------------------------------------
 * FROZEN ENGINE CONSTANT (informational only; no guest memory is touched)
 * ---------------------------------------------------------------------------
 *   base+0x649d422  flat swap wrapper `mov 0x20(%rdi),%rax; mov 0x28(%rdi),%rsi;
 *                   mov %rax,%rdi; jmp eglSwapBuffers@plt` -- the Route-B call
 *   BuildID 204ad049599c96afb75b8a21d5051063e821d6c9
 *   SHA-256 3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335
 * Evidence: analysis/R1_zero_tagged_swaps.md, analysis/C04-render-integration.md
 * section 4 (E1/E5), analysis/render/render-map.md sections 2-5.
 *
 * ---------------------------------------------------------------------------
 * EXACT LEAD WIRING (lead-owned edits; NOT made here)
 * ---------------------------------------------------------------------------
 * 1. In src/linux/guest_render.c, call_guest_render_entries(), wrap ONLY the
 *    direct Route-B call -- do not gate it, do not change rc handling:
 *
 *      #include "frame_synthetic_tag.h"
 *      ...
 *      sober_frame_synthetic_tag_enter();          <-- host swap scope begins
 *      long src = guest_swap((uintptr_t)ctx);      <-- unchanged 0x649d422 call
 *      sober_frame_synthetic_tag_leave();          <-- host swap scope ends
 *
 * 2. In the swap observation hook (src/jni/render_guest.c rg_hook_swap(),
 *    GOT 0x722a1a8), BEFORE the existing `if (rg_in_guest(ra))` branch:
 *
 *      if (sober_frame_synthetic_tag_active()) {
 *          // Explicit host tag: this swap is ours.  Count it SYNTHETIC only;
 *          // never increment the guest-tagged counter, regardless of RA.
 *          sober_frame_synthetic_tag_note_swap();   // observation-only
 *          rg_bump(&s_swaps_synthetic);
 *          ... forward to the real eglSwapBuffers (unchanged) ...
 *          return;
 *      }
 *
 *    This is defense in depth: with the tag active the swap is forced to the
 *    synthetic bucket even if an RA were observed inside guest .text.
 *
 * 3. Diagnostics (any lead-owned log/gate file):
 *      char b[256];
 *      fprintf(stderr, "%s\n", sober_frame_synthetic_tag_describe(b,sizeof b));
 *    and/or read sober_frame_synthetic_tag_seen() to disqualify a run-level
 *    "guest swap evidence" claim when a host-tagged swap is present.
 *
 * No CMake edit belongs to this module's owner: add frame_synthetic_tag.c to
 * the sober_jni source list in src/jni/CMakeLists.txt yourself (lead-owned).
 */
#ifndef SOBER_FRAME_SYNTHETIC_TAG_H
#define SOBER_FRAME_SYNTHETIC_TAG_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__GNUC__) || defined(__clang__)
#  define SOBER_FST_API __attribute__((visibility("default")))
#else
#  define SOBER_FST_API
#endif

/* ---- gate --------------------------------------------------------------- */
#define SOBER_FRAME_SYNTH_TAG_ENV  "SOBER_FRAME_SYNTH_TAG"   /* kill switch   */

/* Frozen-engine constants (informational; this module touches no guest RAM). */
#define SOBER_FST_BUILDID32        0x204ad049u
#define SOBER_FST_RVA_SWAP_WRAPPER 0x649d422u   /* Route-B flat swap wrapper  */

/* ---- master enable ------------------------------------------------------ */
/* 1 unless SOBER_FRAME_SYNTH_TAG is exactly 0/off/no/false (case-insensitive).
 * Re-read on every call so the kill switch takes effect immediately. */
SOBER_FST_API int         sober_frame_synthetic_tag_enabled(void);
SOBER_FST_API const char *sober_frame_synthetic_tag_env_vars(void);

/* ---- host swap-path API ------------------------------------------------- */

/* Mark the start of a host-originated (synthetic) swap scope.  No-op when the
 * kill switch is set.  Idempotent/nestable: each enter must be paired with a
 * leave.  Sets the sticky "seen" flag the first time a scope is opened. */
SOBER_FST_API void sober_frame_synthetic_tag_enter(void);

/* End the current scope.  Mirrors enter(); safe to call when disabled. */
SOBER_FST_API void sober_frame_synthetic_tag_leave(void);

/* THE classifier query for the swap hook.  1 only while inside a live,
 * enabled host-swap scope on THIS thread (thread-local depth > 0).  When
 * this returns 1 the current swap MUST be classified SYNTHETIC and MUST NOT
 * be counted as guest evidence.  Always 0 when the kill switch is set. */
SOBER_FST_API int  sober_frame_synthetic_tag_active(void);

/* Observation-only: record that the swap hook classified a swap synthetic
 * because the tag was active.  Increments the tagged-swap diagnostic counter.
 * No effect when the tag is inactive/disabled.  Never touches guest counters. */
SOBER_FST_API void sober_frame_synthetic_tag_note_swap(void);

/* ---- sticky / run-level query ------------------------------------------ */
/* 1 (sticky for the process) once a host-swap scope was opened while the tag
 * was enabled.  A lead-owned gate may use this to disqualify any aggregate
 * "guest swap evidence" claim for a run in which our own swap was tagged.
 * It is a diagnostic bit, not a marker decision. */
SOBER_FST_API int sober_frame_synthetic_tag_seen(void);

/* ---- counters / diagnostics -------------------------------------------- */
SOBER_FST_API uint64_t sober_frame_synthetic_tag_scope_enters(void);
SOBER_FST_API uint64_t sober_frame_synthetic_tag_scope_leaves(void);
SOBER_FST_API uint64_t sober_frame_synthetic_tag_tagged_swaps(void);

/* Clear all state (for tests); re-reads the kill switch. */
SOBER_FST_API void sober_frame_synthetic_tag_reset(void);

typedef struct {
    int      enabled;        /* kill-switch state (default 1)              */
    int      depth;          /* current thread-local scope depth           */
    int      active;         /* enabled && depth > 0                       */
    int      seen;           /* sticky: a scope was opened while enabled   */
    uint64_t scope_enters;   /* total enter() calls while enabled           */
    uint64_t scope_leaves;   /* total leave() calls while enabled           */
    uint64_t tagged_swaps;   /* swaps noted synthetic via this tag          */
} SoberFrameSyntheticTagSnapshot;

SOBER_FST_API void sober_frame_synthetic_tag_snapshot(
    SoberFrameSyntheticTagSnapshot *out);

/* One-line, neutral diagnostic description into buf (never writes stderr,
 * never emits a marker).  Returns buf (buf/cap NULL/0 -> ""). */
SOBER_FST_API const char *sober_frame_synthetic_tag_describe(char *buf,
                                                            size_t cap);

#ifdef __cplusplus
}
#endif
#endif /* SOBER_FRAME_SYNTHETIC_TAG_H */
