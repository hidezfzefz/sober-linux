/*
 * frame_evidence.c - DEFAULT-ON, observation-only render-leg evidence recorder
 *                    with fail-closed guest-window provenance (agent F7).
 *                    See the header for the contract, RVAs and wiring.
 *
 * HARD RULES OBSERVED HERE
 * ------------------------
 *  - NEW FILES ONLY.  No CMake/engine/gate edits.  No marker emission, no JNI
 *    handler/callback invocation, no guest byte or guest global writes.
 *  - Default ON; SOBER_FRAME_EVIDENCE=0 -> observers inert, decisive 0/REJECT.
 *  - One immutable event per observation; counters move by exactly one and only
 *    when the event is committed (no log entry -> no count).
 *  - Mixed swap evidence sets a sticky taint that zeroes decisive counts.
 *  - window_bound is 0 until the G8 observation ledger proves guest provenance;
 *    a host store to 0x72a3308 is always REJECT.
 *  - The G2 normative stderr lines are emitted at most once, only when the
 *    render legs are decisive, with final aggregate N/M quoted everywhere.
 */
#define _GNU_SOURCE
#include "frame_evidence.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Persisted phase-3 flag from the frozen render module.  Weak so this file
 * also links into a target that omits native_render.c; absent => fail closed. */
#if defined(__GNUC__) || defined(__clang__)
extern int sober_render_phase3_done(void) __attribute__((weak));
#else
extern int sober_render_phase3_done(void);
#endif

#define SOBER_FE_LOG_CAP  512u
#define SOBER_FE_WOBS_CAP 256u
#define SOBER_FE_NS_PER_S 1000000000ull

static void fe_maybe_emit_lines(void);  /* forward: recorders trigger it */

/* ---- module state (process global, survives render shutdown) ------------- */
static _Atomic uint64_t  s_seq;
static _Atomic uint64_t  s_events;
static _Atomic uint64_t  s_dropped;
static _Atomic uint64_t  s_swaps_total;
static _Atomic uint64_t  s_swaps_guest;
static _Atomic uint64_t  s_swaps_synthetic;
static _Atomic uint64_t  s_draws_total;
static _Atomic uint64_t  s_draws_guest;
static _Atomic uint64_t  s_draws_synthetic;
static _Atomic uint64_t  s_draws_snapshot;
static _Atomic int       s_have_draw_snapshot;
static _Atomic uint64_t  s_draws_between;
static _Atomic uintptr_t s_loop_ra_first;
static _Atomic int       s_loop_ra_stable;
static _Atomic uintptr_t s_base;
static _Atomic uint32_t  s_text_size = SOBER_FRAME_EVIDENCE_TEXT_SIZE;
static _Atomic uintptr_t s_window;
static _Atomic uint32_t  s_window_gen;
static _Atomic uintptr_t s_window_ra;
static _Atomic uint64_t  s_window_ns;
static _Atomic int       s_window_bound_raw;   /* audit only, NOT decisive   */
static _Atomic int       s_mixed;
static _Atomic int       s_from_surface_installed;
static _Atomic uint64_t  s_first_ns = UINT64_MAX;
static _Atomic uint64_t  s_last_ns;
static _Atomic uintptr_t s_guest_ra_lo = UINTPTR_MAX;
static _Atomic uintptr_t s_guest_ra_hi;
static _Atomic uintptr_t s_draw_ra_lo = UINTPTR_MAX;
static _Atomic uintptr_t s_draw_ra_hi;

static SoberFrameEvidenceEvent s_log[SOBER_FE_LOG_CAP];
static _Atomic int s_log_ready[SOBER_FE_LOG_CAP];

/* ---- G8 window ledger ---------------------------------------------------- */
static SoberFrameWindowObservation s_wobs[SOBER_FE_WOBS_CAP];
static _Atomic int s_wobs_ready[SOBER_FE_WOBS_CAP];
static _Atomic uint64_t s_wobs_used;
static _Atomic uint64_t s_wobs_dropped;
static _Atomic int s_w_integrity_engine;
static _Atomic int s_w_integrity_obs;
static _Atomic int s_w_integrity_layout;
static _Atomic int s_w_host_publish = 1;   /* default: host publish active */
static _Atomic uint64_t s_w_tid;
static _Atomic uint64_t s_w_last_seq;
static _Atomic int s_w_have_seq;
static _Atomic int s_w_tid_mismatch;
static _Atomic int s_w_seq_bad;

static _Atomic int s_lines_emitted;

/* ---- small atomic helpers ------------------------------------------------ */
static void fe_max_u64(_Atomic uint64_t *p, uint64_t v)
{
    uint64_t cur = atomic_load_explicit(p, memory_order_relaxed);
    while (cur < v &&
           !atomic_compare_exchange_weak_explicit(p, &cur, v,
               memory_order_relaxed, memory_order_relaxed)) {
        /* retry */
    }
}

static void fe_min_u64(_Atomic uint64_t *p, uint64_t v)
{
    uint64_t cur = atomic_load_explicit(p, memory_order_relaxed);
    while (cur > v &&
           !atomic_compare_exchange_weak_explicit(p, &cur, v,
               memory_order_relaxed, memory_order_relaxed)) {
        /* retry */
    }
}

static void fe_max_uptr(_Atomic uintptr_t *p, uintptr_t v)
{
    uintptr_t cur = atomic_load_explicit(p, memory_order_relaxed);
    while (cur < v &&
           !atomic_compare_exchange_weak_explicit(p, &cur, v,
               memory_order_relaxed, memory_order_relaxed)) {
        /* retry */
    }
}

static void fe_min_uptr(_Atomic uintptr_t *p, uintptr_t v)
{
    uintptr_t cur = atomic_load_explicit(p, memory_order_relaxed);
    while (cur > v &&
           !atomic_compare_exchange_weak_explicit(p, &cur, v,
               memory_order_relaxed, memory_order_relaxed)) {
        /* retry */
    }
}

static uint64_t fe_now_ns(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0ull;
    return (uint64_t)ts.tv_sec * SOBER_FE_NS_PER_S + (uint64_t)ts.tv_nsec;
}

static int fe_disabled(void)
{
    const char *e = getenv(SOBER_FRAME_EVIDENCE_ENV);
    return e != NULL && strcmp(e, "0") == 0;
}

int frame_evidence_enabled(void)
{
    return !fe_disabled();
}

const char *frame_evidence_env_var(void)
{
    return SOBER_FRAME_EVIDENCE_ENV "=0 (kill switch; DEFAULT ON - 0 makes all "
           "observers inert and all decisive accessors 0/REJECT)";
}

static int fe_tainted(void)
{
    return atomic_load_explicit(&s_mixed, memory_order_relaxed) != 0;
}

static int fe_phase3_done(void)
{
    int (*fn)(void) = sober_render_phase3_done;
    return fn ? fn() : 0;
}

/* ---- classification / call-site verification ---------------------------- */
static int fe_ra_tag(uintptr_t ra, uint32_t *tag_out)
{
    uintptr_t base = atomic_load_explicit(&s_base, memory_order_relaxed);
    uint32_t  size = atomic_load_explicit(&s_text_size, memory_order_relaxed);

    if (base == 0u || size == 0u) {
        if (tag_out) *tag_out = SOBER_FRAME_TAG_UNKNOWN;
        return 0;
    }
    if (ra >= base && (ra - base) < (uintptr_t)size) {
        if (tag_out) *tag_out = SOBER_FRAME_TAG_GUEST;
        return 1;
    }
    if (tag_out) *tag_out = SOBER_FRAME_TAG_SYNTHETIC;
    return 0;
}

int frame_evidence_verify_call_site(uintptr_t ra, uintptr_t target)
{
    unsigned char b[5];
    uintptr_t base = atomic_load_explicit(&s_base, memory_order_relaxed);
    uint32_t  size = atomic_load_explicit(&s_text_size, memory_order_relaxed);
    int32_t   rel;
    uintptr_t computed;

    if (base == 0u || size < 5u) return 0;
    if (ra < base || (ra - base) < 5u || (ra - base) >= (uintptr_t)size)
        return 0;
    memcpy(b, (const void *)(ra - 5u), sizeof(b));
    if (b[0] != 0xe8u) return 0;          /* direct call rel32 */
    memcpy(&rel, b + 1u, sizeof(rel));
    computed = ra + (uintptr_t)rel;       /* negative rel wraps correctly */
    return computed == target;
}

void frame_evidence_set_guest_base(uintptr_t base)
{
    atomic_store_explicit(&s_base, base, memory_order_release);
}

uintptr_t frame_evidence_guest_base(void)
{
    return atomic_load_explicit(&s_base, memory_order_relaxed);
}

int frame_evidence_ra_is_guest(uintptr_t ra)
{
    uint32_t tag = SOBER_FRAME_TAG_UNKNOWN;
    (void)fe_ra_tag(ra, &tag);
    return tag == SOBER_FRAME_TAG_GUEST;
}

/* ---- immutable event log ------------------------------------------------- */
static int fe_commit_event(uint32_t kind, uint32_t tag, uint64_t ns,
                           uintptr_t ra, uintptr_t arg0, uintptr_t arg1)
{
    uint64_t idx = atomic_fetch_add_explicit(&s_seq, 1u, memory_order_relaxed);
    SoberFrameEvidenceEvent *e;

    if (idx >= (uint64_t)SOBER_FE_LOG_CAP) {
        atomic_fetch_add_explicit(&s_dropped, 1u, memory_order_relaxed);
        return 0;
    }
    e = &s_log[(size_t)idx];
    e->magic        = SOBER_FRAME_EV_MAGIC;
    e->kind         = kind;
    e->tag          = tag;
    e->reserved     = 0u;
    e->seq          = idx;
    e->timestamp_ns = ns;
    e->ra           = ra;
    e->guest_base   = atomic_load_explicit(&s_base, memory_order_relaxed);
    e->arg0         = arg0;
    e->arg1         = arg1;
    atomic_store_explicit(&s_log_ready[(size_t)idx], 1, memory_order_release);
    fe_max_u64(&s_events, idx + 1u);
    return 1;
}

static void fe_note_time(uint64_t ns)
{
    fe_min_u64(&s_first_ns, ns);
    fe_max_u64(&s_last_ns, ns);
}

static void fe_note_guest_ra(uintptr_t ra)
{
    fe_min_uptr(&s_guest_ra_lo, ra);
    fe_max_uptr(&s_guest_ra_hi, ra);
}

static void fe_update_mixed(void)
{
    if (atomic_load_explicit(&s_swaps_guest, memory_order_relaxed) > 0u &&
        atomic_load_explicit(&s_swaps_synthetic, memory_order_relaxed) > 0u)
        atomic_store_explicit(&s_mixed, 1, memory_order_relaxed);
}

/* ---- render observers ---------------------------------------------------- */
void frame_evidence_record_swap(uintptr_t ra)
{
    uint32_t tag = SOBER_FRAME_TAG_UNKNOWN;
    uint64_t ns, draws;
    int guest;

    if (frame_evidence_enabled() == 0) return;
    guest = fe_ra_tag(ra, &tag);
    ns = fe_now_ns();
    if (!fe_commit_event(SOBER_FRAME_EV_SWAP, tag, ns, ra, 0u, 0u)) return;

    atomic_fetch_add_explicit(&s_swaps_total, 1u, memory_order_relaxed);
    if (guest) {
        atomic_fetch_add_explicit(&s_swaps_guest, 1u, memory_order_relaxed);
        fe_note_guest_ra(ra);

        /* max positive draw delta between two tagged swaps (G2 aggregate M) */
        draws = atomic_load_explicit(&s_draws_guest, memory_order_relaxed);
        if (atomic_load_explicit(&s_have_draw_snapshot, memory_order_relaxed)) {
            uint64_t prev = atomic_load_explicit(&s_draws_snapshot,
                                                  memory_order_relaxed);
            fe_max_u64(&s_draws_between, draws >= prev ? draws - prev : 0ull);
            atomic_store_explicit(&s_draws_snapshot, draws,
                                  memory_order_relaxed);
        } else {
            atomic_store_explicit(&s_draws_snapshot, draws,
                                  memory_order_relaxed);
            atomic_store_explicit(&s_have_draw_snapshot, 1,
                                  memory_order_relaxed);
        }

        /* stable recurring guest RA (T3-5 section 4 / R6 section 4.2) */
        {
            uintptr_t first = atomic_load_explicit(&s_loop_ra_first,
                                                    memory_order_relaxed);
            if (first == 0u)
                atomic_store_explicit(&s_loop_ra_first, ra,
                                      memory_order_relaxed);
            else if (first == ra)
                atomic_store_explicit(&s_loop_ra_stable, 1,
                                      memory_order_relaxed);
        }
    } else {
        atomic_fetch_add_explicit(&s_swaps_synthetic, 1u, memory_order_relaxed);
    }
    fe_note_time(ns);
    fe_update_mixed();
    fe_maybe_emit_lines();
}

void frame_evidence_record_draw(uintptr_t ra)
{
    uint32_t tag = SOBER_FRAME_TAG_UNKNOWN;
    uint64_t ns;
    int guest;

    if (frame_evidence_enabled() == 0) return;
    guest = fe_ra_tag(ra, &tag);
    ns = fe_now_ns();
    if (!fe_commit_event(SOBER_FRAME_EV_DRAW, tag, ns, ra, 0u, 0u)) return;

    atomic_fetch_add_explicit(&s_draws_total, 1u, memory_order_relaxed);
    if (guest) {
        atomic_fetch_add_explicit(&s_draws_guest, 1u, memory_order_relaxed);
        fe_min_uptr(&s_draw_ra_lo, ra);
        fe_max_uptr(&s_draw_ra_hi, ra);
        fe_note_guest_ra(ra);
    } else {
        atomic_fetch_add_explicit(&s_draws_synthetic, 1u, memory_order_relaxed);
    }
    fe_note_time(ns);
    fe_maybe_emit_lines();
}

void frame_evidence_record_from_surface(uintptr_t ra)
{
    uint32_t tag = SOBER_FRAME_TAG_UNKNOWN;
    uint64_t ns;

    if (frame_evidence_enabled() == 0) return;
    (void)fe_ra_tag(ra, &tag);
    ns = fe_now_ns();
    if (!fe_commit_event(SOBER_FRAME_EV_FROMSURFACE, tag, ns, ra, 0u, 0u))
        return;
    /* Supporting label only: never a window-bound or predicate signal. */
    if (tag == SOBER_FRAME_TAG_GUEST)
        atomic_store_explicit(&s_from_surface_installed, 1,
                              memory_order_relaxed);
    fe_note_time(ns);
}

void frame_evidence_record_window_bind(uintptr_t window, uint32_t generation,
                                       uintptr_t ra)
{
    uint32_t tag = SOBER_FRAME_TAG_UNKNOWN;
    uint32_t prev;
    uint64_t ns;
    int guest;

    if (frame_evidence_enabled() == 0) return;
    guest = fe_ra_tag(ra, &tag);
    ns = fe_now_ns();
    prev = atomic_load_explicit(&s_window_gen, memory_order_relaxed);

    /* Raw audit fields only.  This NEVER sets the decisive window_bound: the
     * G8 ledger (frame_evidence_window_observation) is the only authority. */
    if (window == 0u || !guest || generation == 0u ||
        (prev != 0u && generation <= prev)) {
        uint32_t bad = guest ? SOBER_FRAME_TAG_SYNTHETIC : tag;
        if (fe_commit_event(SOBER_FRAME_EV_WINDOW, bad, ns, ra, window,
                            (uintptr_t)generation))
            fe_note_time(ns);
        return;
    }
    if (!fe_commit_event(SOBER_FRAME_EV_WINDOW, SOBER_FRAME_TAG_GUEST, ns, ra,
                         window, (uintptr_t)generation))
        return;

    atomic_store_explicit(&s_window, window, memory_order_relaxed);
    atomic_store_explicit(&s_window_gen, generation, memory_order_relaxed);
    atomic_store_explicit(&s_window_ra, ra, memory_order_relaxed);
    atomic_store_explicit(&s_window_ns, ns, memory_order_relaxed);
    atomic_store_explicit(&s_window_bound_raw, 1, memory_order_relaxed);
    fe_note_time(ns);
}

int frame_evidence_observe_window_global(uintptr_t guest_base, uintptr_t ra)
{
    uintptr_t waddr, gaddr, window;

    if (frame_evidence_enabled() == 0) return 0;
    if (guest_base == 0u)
        guest_base = atomic_load_explicit(&s_base, memory_order_relaxed);
    if (guest_base == 0u) return 0;
    if (guest_base > UINTPTR_MAX -
            (uintptr_t)SOBER_FRAME_EVIDENCE_G_WINDOW_GEN)
        return 0;
    if (atomic_load_explicit(&s_base, memory_order_relaxed) == 0u)
        frame_evidence_set_guest_base(guest_base);

    waddr = guest_base + (uintptr_t)SOBER_FRAME_EVIDENCE_G_WINDOW;
    gaddr = guest_base + (uintptr_t)SOBER_FRAME_EVIDENCE_G_WINDOW_GEN;
    window = *(volatile uintptr_t *)waddr;
    /* Read-only sample; does not feed the G8 ledger and cannot establish
     * guest provenance (the global may be a host store). */
    frame_evidence_record_window_bind(
        window, *(volatile uint32_t *)gaddr, ra);
    return frame_evidence_window_bound();
}

/* ---- G8 window observation ledger --------------------------------------- */
int frame_evidence_window_observation(const SoberFrameWindowObservation *obs)
{
    uint64_t idx;

    if (!obs || frame_evidence_enabled() == 0) return 0;
    idx = atomic_fetch_add_explicit(&s_wobs_used, 1u, memory_order_relaxed);
    if (idx >= (uint64_t)SOBER_FE_WOBS_CAP) {
        atomic_fetch_add_explicit(&s_wobs_dropped, 1u, memory_order_relaxed);
        return 0;
    }
    s_wobs[(size_t)idx] = *obs;   /* immutable after publication */
    atomic_store_explicit(&s_wobs_ready[(size_t)idx], 1,
                          memory_order_release);

    /* C0 coherence: one tid, monotonic seq. */
    {
        uint64_t tid = atomic_load_explicit(&s_w_tid, memory_order_relaxed);
        if (tid == 0u)
            atomic_store_explicit(&s_w_tid,
                                  obs->tid ? obs->tid : 1u,
                                  memory_order_relaxed);
        else if (tid != obs->tid)
            atomic_store_explicit(&s_w_tid_mismatch, 1, memory_order_relaxed);

        if (atomic_load_explicit(&s_w_have_seq, memory_order_relaxed) &&
            obs->seq <= atomic_load_explicit(&s_w_last_seq,
                                              memory_order_relaxed))
            atomic_store_explicit(&s_w_seq_bad, 1, memory_order_relaxed);
        fe_max_u64(&s_w_last_seq, obs->seq);
        atomic_store_explicit(&s_w_have_seq, 1, memory_order_relaxed);
    }

    /* Supporting G2 label: a successful fromSurface observation in guest .text
     * means our observer is installed.  Never a provenance signal. */
    if (obs->kind == SOBER_FRAME_WOBS_FROMSURFACE && obs->status == 1u &&
        frame_evidence_ra_is_guest(obs->ra))
        atomic_store_explicit(&s_from_surface_installed, 1,
                              memory_order_relaxed);

    fe_maybe_emit_lines();
    return 1;
}

void frame_evidence_window_set_integrity(int engine_verified,
                                         int observers_complete,
                                         int layout_ok)
{
    atomic_store_explicit(&s_w_integrity_engine,
                          engine_verified ? 1 : 0, memory_order_relaxed);
    atomic_store_explicit(&s_w_integrity_obs,
                          observers_complete ? 1 : 0, memory_order_relaxed);
    atomic_store_explicit(&s_w_integrity_layout,
                          layout_ok ? 1 : 0, memory_order_relaxed);
    fe_maybe_emit_lines();
}

void frame_evidence_window_set_host_publish_enabled(int enabled)
{
    atomic_store_explicit(&s_w_host_publish,
                          enabled ? 1 : 0, memory_order_relaxed);
    fe_maybe_emit_lines();
}

/* Pure G8 verdict over the ledger.  C0-C10 in order; first failure wins. */
static int fe_window_check(int *reason)
{
    const SoberFrameWindowObservation *fs = NULL;
    const SoberFrameWindowObservation *snap = NULL;
    const SoberFrameWindowObservation *rel = NULL;
    const SoberFrameWindowObservation *acq = NULL;
    const SoberFrameWindowObservation *dims = NULL;
    const SoberFrameWindowObservation *glog = NULL;
    uintptr_t base = atomic_load_explicit(&s_base, memory_order_relaxed);
    uint32_t  size = atomic_load_explicit(&s_text_size, memory_order_relaxed);
    uint64_t  i, used = atomic_load_explicit(&s_wobs_used, memory_order_relaxed);
    unsigned  fs_count = 0u;

#define FE_WREJECT(code) do { if (reason) *reason = (code); \
                              return SOBER_FRAME_WVERDICT_REJECT; } while (0)

    if (used > (uint64_t)SOBER_FE_WOBS_CAP) used = (uint64_t)SOBER_FE_WOBS_CAP;
    if (base == 0u || size == 0u) FE_WREJECT(SOBER_FRAME_WR_R0);
    if (base > UINTPTR_MAX - (uintptr_t)SOBER_FRAME_W_PLT_FROM_SURFACE)
        FE_WREJECT(SOBER_FRAME_WR_R0);
    if (!atomic_load_explicit(&s_w_integrity_engine, memory_order_relaxed) ||
        !atomic_load_explicit(&s_w_integrity_obs, memory_order_relaxed) ||
        !atomic_load_explicit(&s_w_integrity_layout, memory_order_relaxed) ||
        atomic_load_explicit(&s_w_tid_mismatch, memory_order_relaxed) ||
        atomic_load_explicit(&s_w_seq_bad, memory_order_relaxed))
        FE_WREJECT(SOBER_FRAME_WR_R0);
    if (atomic_load_explicit(&s_w_host_publish, memory_order_relaxed))
        FE_WREJECT(SOBER_FRAME_WR_R1);

    for (i = 0u; i < used; ++i) {
        const SoberFrameWindowObservation *o;
        if (!atomic_load_explicit(&s_wobs_ready[(size_t)i],
                                  memory_order_acquire))
            continue;
        o = &s_wobs[(size_t)i];
        if (o->status != 1u) FE_WREJECT(SOBER_FRAME_WR_R0); /* unreadable */
        switch (o->kind) {
        case SOBER_FRAME_WOBS_HOST_STORE:
            FE_WREJECT(SOBER_FRAME_WR_R1);                  /* C1 */
        case SOBER_FRAME_WOBS_FROMSURFACE:
            ++fs_count; fs = o; break;
        case SOBER_FRAME_WOBS_SNAPSHOT:
            snap = o; break;
        case SOBER_FRAME_WOBS_RELEASE:
            rel = o; break;
        case SOBER_FRAME_WOBS_ACQUIRE:
            acq = o; break;
        case SOBER_FRAME_WOBS_DIMS:
            dims = o; break;
        case SOBER_FRAME_WOBS_GUEST_LOG:
            glog = o; break;
        default:
            break;
        }
    }

    /* C2: exactly one store-tied fromSurface observation, no other. */
    if (fs_count == 0u) FE_WREJECT(SOBER_FRAME_WR_R2_RA);
    if (fs_count != 1u) FE_WREJECT(SOBER_FRAME_WR_R2_MIXED);
    if (fs->ra != base + (uintptr_t)SOBER_FRAME_W_RA_HELPER)
        FE_WREJECT(SOBER_FRAME_WR_R2_RA);
    if (!frame_evidence_verify_call_site(
            fs->ra, base + (uintptr_t)SOBER_FRAME_W_PLT_FROM_SURFACE))
        FE_WREJECT(SOBER_FRAME_WR_R2_OPCODE);

    /* C3: real Surface / mNativeObject was read successfully. */
    if (fs->mnative == 0u) FE_WREJECT(SOBER_FRAME_WR_R3);

    /* C4: returned window is that native object and is a different window. */
    if (fs->window == 0u || fs->window != fs->mnative ||
        fs->window == fs->w_before)
        FE_WREJECT(SOBER_FRAME_WR_R4);

    if (!snap) FE_WREJECT(SOBER_FRAME_WR_R6);

    /* C5: guest inc, generation delta +1 modulo 2^32. */
    if (snap->g_now != (uint32_t)(fs->g_before + 1u))
        FE_WREJECT(SOBER_FRAME_WR_R5);

    /* C6: the published window is still the returned window. */
    if (snap->window == 0u || snap->window != fs->window)
        FE_WREJECT(SOBER_FRAME_WR_R6);

    /* C7: release PLT on the old window (waived on first bind). */
    if (fs->w_before != 0u &&
        (!rel || rel->ra != base + (uintptr_t)SOBER_FRAME_W_RA_RELEASE_OLD ||
         rel->window != fs->w_before || rel->seq <= fs->seq))
        FE_WREJECT(SOBER_FRAME_WR_R7);

    /* C8: acquire PLT on the published window. */
    if (!acq || acq->ra != base + (uintptr_t)SOBER_FRAME_W_RA_ACQUIRE ||
        acq->window != snap->window || acq->seq <= fs->seq)
        FE_WREJECT(SOBER_FRAME_WR_R8);

    /* C9: coherent layout / positive dimensions. */
    if (!dims || dims->window != snap->window || dims->seq <= fs->seq ||
        dims->width == 0u || dims->height == 0u ||
        (dims->ra != base + (uintptr_t)SOBER_FRAME_W_RA_GET_WIDTH &&
         dims->ra != base + (uintptr_t)SOBER_FRAME_W_RA_GET_HEIGHT))
        FE_WREJECT(SOBER_FRAME_WR_R9);

    /* C10: guest publish log on the same window with the same ID. */
    if (!glog || glog->window != snap->window || glog->g_now != snap->g_now)
        FE_WREJECT(SOBER_FRAME_WR_R10);

#undef FE_WREJECT
    return SOBER_FRAME_WVERDICT_ACCEPT;
}

int frame_evidence_window_verdict(void)
{
    return fe_window_check(NULL);
}

const char *frame_evidence_window_reject_reason(void)
{
    int code = SOBER_FRAME_WR_NO_OBS;
    if (fe_window_check(&code) == SOBER_FRAME_WVERDICT_ACCEPT) return "NONE";
    switch (code) {
    case SOBER_FRAME_WR_R0:       return "R0_UNVERIFIED_ENGINE";
    case SOBER_FRAME_WR_R1:       return "R1_HOST_PUBLISH";
    case SOBER_FRAME_WR_R2_RA:    return "R2_RA_NOT_HELPER";
    case SOBER_FRAME_WR_R2_MIXED: return "R2_MIXED_FROMSURFACE";
    case SOBER_FRAME_WR_R2_OPCODE: return "R2_OPCODE_MISMATCH";
    case SOBER_FRAME_WR_R3:       return "R3_NO_MNATIVEOBJECT";
    case SOBER_FRAME_WR_R4:       return "R4_RET_MISMATCH";
    case SOBER_FRAME_WR_R5:       return "R5_GENERATION";
    case SOBER_FRAME_WR_R6:       return "R6_WINDOW_MISMATCH";
    case SOBER_FRAME_WR_R7:       return "R7_NO_RELEASE";
    case SOBER_FRAME_WR_R8:       return "R8_NO_ACQUIRE";
    case SOBER_FRAME_WR_R9:       return "R9_LAYOUT";
    case SOBER_FRAME_WR_R10:      return "R10_NO_LOG";
    default:                      return "NO_OBSERVATION";
    }
}

/* ---- decisive accessors -------------------------------------------------- */
uint64_t frame_evidence_swaps_guest(void)
{
    if (frame_evidence_enabled() == 0 || fe_tainted()) return 0ull;
    return atomic_load_explicit(&s_swaps_guest, memory_order_relaxed);
}

uint64_t frame_evidence_draws(void)
{
    if (frame_evidence_enabled() == 0 || fe_tainted()) return 0ull;
    return atomic_load_explicit(&s_draws_guest, memory_order_relaxed);
}

uint64_t frame_evidence_draws_between(void)
{
    if (frame_evidence_enabled() == 0 || fe_tainted()) return 0ull;
    return atomic_load_explicit(&s_draws_between, memory_order_relaxed);
}

uintptr_t frame_evidence_loop_ra(void)
{
    return atomic_load_explicit(&s_loop_ra_first, memory_order_relaxed);
}

int frame_evidence_loop_ra_stable(void)
{
    if (frame_evidence_enabled() == 0 || fe_tainted()) return 0;
    return atomic_load_explicit(&s_loop_ra_stable, memory_order_relaxed) != 0;
}

int frame_evidence_window_bound(void)
{
    if (frame_evidence_enabled() == 0 || fe_tainted()) return 0;
    return fe_window_check(NULL) == SOBER_FRAME_WVERDICT_ACCEPT;
}

int frame_evidence_from_surface_installed(void)
{
    return atomic_load_explicit(&s_from_surface_installed,
                                memory_order_relaxed) != 0;
}

const char *frame_evidence_from_surface_mode(void)
{
    return frame_evidence_from_surface_installed() ? "installed" : "native";
}

int guest_render_ok(void)
{
    int ok = fe_phase3_done() &&
             frame_evidence_swaps_guest() >= 2ull &&
             frame_evidence_loop_ra_stable() &&
             frame_evidence_draws() >= 1ull &&
             frame_evidence_draws_between() >= 1ull &&
             frame_evidence_window_bound();
    if (ok) fe_maybe_emit_lines();
    return ok;
}

/* ---- one-shot G2 normative emission ------------------------------------- */
static void fe_maybe_emit_lines(void)
{
    uint64_t i, used, n, m;
    const char *mode;
    int expected = 0;

    if (frame_evidence_enabled() == 0 || fe_tainted()) return;
    if (!atomic_compare_exchange_strong_explicit(&s_lines_emitted, &expected, 1,
            memory_order_relaxed, memory_order_relaxed))
        return;

    if (frame_evidence_swaps_guest() < 2ull ||
        !frame_evidence_loop_ra_stable() ||
        frame_evidence_draws() < 1ull ||
        frame_evidence_draws_between() < 1ull ||
        !frame_evidence_window_bound()) {
        atomic_store_explicit(&s_lines_emitted, 0, memory_order_relaxed);
        return;
    }

    n = frame_evidence_swaps_guest();
    m = frame_evidence_draws_between();
    mode = frame_evidence_from_surface_mode();

    used = atomic_load_explicit(&s_events, memory_order_relaxed);
    if (used > (uint64_t)SOBER_FE_LOG_CAP) used = (uint64_t)SOBER_FE_LOG_CAP;
    for (i = 0u; i < used; ++i) {
        SoberFrameEvidenceEvent e;
        uintptr_t off;
        if (!frame_evidence_copy_event((size_t)i, &e)) continue;
        if (e.kind != SOBER_FRAME_EV_SWAP ||
            e.tag != SOBER_FRAME_TAG_GUEST)
            continue;
        off = (e.guest_base != 0u && e.ra >= e.guest_base)
                  ? e.ra - e.guest_base : 0u;
        if (off == 0u) continue;
        fprintf(stderr,
                "[render] guest-tagged swap at 0x649d422 ra=base+0x%llx "
                "tag=guest-loop | guest-tagged swaps: %llu | "
                "guest GLES draws between tagged swaps: %llu\n",
                (unsigned long long)off, (unsigned long long)n,
                (unsigned long long)m);
    }

    /* Both G2 spellings: :209 (commas) and :217 (spaces). */
    fprintf(stderr,
            "[render] guest render verdict: guest-tagged frame confirmed "
            "(swaps=%llu, draws=%llu, fromSurface=%s, window=bound) "
            "-> GUEST-TAGGED\n",
            (unsigned long long)n, (unsigned long long)m, mode);
    fprintf(stderr,
            "[render] guest render verdict: guest-tagged frame confirmed "
            "(swaps=%llu draws=%llu fromSurface=%s window=bound) "
            "-> GUEST-TAGGED\n",
            (unsigned long long)n, (unsigned long long)m, mode);
}

/* ---- raw immutable evidence accessors ------------------------------------ */
uint64_t frame_evidence_swaps_total(void)
{
    return atomic_load_explicit(&s_swaps_total, memory_order_relaxed);
}
uint64_t frame_evidence_swaps_guest_recorded(void)
{
    return atomic_load_explicit(&s_swaps_guest, memory_order_relaxed);
}
uint64_t frame_evidence_swaps_synthetic(void)
{
    return atomic_load_explicit(&s_swaps_synthetic, memory_order_relaxed);
}
uint64_t frame_evidence_draws_total(void)
{
    return atomic_load_explicit(&s_draws_total, memory_order_relaxed);
}
uint64_t frame_evidence_draws_guest_recorded(void)
{
    return atomic_load_explicit(&s_draws_guest, memory_order_relaxed);
}
uint64_t frame_evidence_draws_synthetic(void)
{
    return atomic_load_explicit(&s_draws_synthetic, memory_order_relaxed);
}
uintptr_t frame_evidence_window(void)
{
    return atomic_load_explicit(&s_window, memory_order_relaxed);
}
uint32_t frame_evidence_window_generation(void)
{
    return atomic_load_explicit(&s_window_gen, memory_order_relaxed);
}
uintptr_t frame_evidence_window_ra(void)
{
    return atomic_load_explicit(&s_window_ra, memory_order_relaxed);
}
int frame_evidence_mixed(void)
{
    return fe_tainted();
}
uint64_t frame_evidence_event_count(void)
{
    return atomic_load_explicit(&s_events, memory_order_relaxed);
}
uint64_t frame_evidence_events_dropped(void)
{
    return atomic_load_explicit(&s_dropped, memory_order_relaxed);
}

int frame_evidence_copy_event(size_t index, SoberFrameEvidenceEvent *out)
{
    if (!out || index >= (size_t)SOBER_FE_LOG_CAP) return 0;
    if (atomic_load_explicit(&s_log_ready[index], memory_order_acquire) == 0)
        return 0;
    *out = s_log[index];
    return out->magic == SOBER_FRAME_EV_MAGIC;
}

void frame_evidence_guest_ra_range(uintptr_t *lo, uintptr_t *hi)
{
    uintptr_t l = atomic_load_explicit(&s_guest_ra_lo, memory_order_relaxed);
    uintptr_t h = atomic_load_explicit(&s_guest_ra_hi, memory_order_relaxed);
    if (lo) *lo = (l == UINTPTR_MAX) ? 0u : l;
    if (hi) *hi = h;
}

void frame_evidence_draw_ra_range(uintptr_t *lo, uintptr_t *hi)
{
    uintptr_t l = atomic_load_explicit(&s_draw_ra_lo, memory_order_relaxed);
    uintptr_t h = atomic_load_explicit(&s_draw_ra_hi, memory_order_relaxed);
    if (lo) *lo = (l == UINTPTR_MAX) ? 0u : l;
    if (hi) *hi = h;
}

/* ---- verdict / snapshot / diagnostics ----------------------------------- */
int frame_evidence_verdict(void)
{
    int tagged, draws, bound, stable;

    if (frame_evidence_enabled() == 0) return SOBER_FRAME_VERDICT_DISABLED;
    if (fe_tainted()) return SOBER_FRAME_VERDICT_MIXED;
    tagged = frame_evidence_swaps_guest() >= 2ull;
    stable = frame_evidence_loop_ra_stable();
    draws  = frame_evidence_draws() >= 1ull &&
             frame_evidence_draws_between() >= 1ull;
    bound  = frame_evidence_window_bound();
    if (tagged && stable && draws && bound)
        return SOBER_FRAME_VERDICT_GUEST_TAGGED;
    if (!tagged && !stable && !draws && !bound)
        return SOBER_FRAME_VERDICT_ABSENT;
    return SOBER_FRAME_VERDICT_INCOMPLETE;
}

const char *frame_evidence_verdict_name(int verdict)
{
    switch (verdict) {
    case SOBER_FRAME_VERDICT_DISABLED:     return "DISABLED";
    case SOBER_FRAME_VERDICT_ABSENT:       return "ABSENT";
    case SOBER_FRAME_VERDICT_MIXED:        return "MIXED";
    case SOBER_FRAME_VERDICT_INCOMPLETE:   return "INCOMPLETE";
    case SOBER_FRAME_VERDICT_GUEST_TAGGED: return "GUEST-TAGGED";
    default:                               return "UNKNOWN";
    }
}

const char *frame_evidence_verdict_string(char *buf, size_t cap)
{
    int v;
    if (!buf || cap == 0u) return "";
    v = frame_evidence_verdict();
    if (v == SOBER_FRAME_VERDICT_GUEST_TAGGED) {
        (void)snprintf(buf, cap,
                       "GUEST-TAGGED (swaps=%llu draws=%llu fromSurface=%s "
                       "window=bound)",
                       (unsigned long long)frame_evidence_swaps_guest(),
                       (unsigned long long)frame_evidence_draws_between(),
                       frame_evidence_from_surface_mode());
        return buf;
    }
    if (v == SOBER_FRAME_VERDICT_MIXED) {
        (void)snprintf(buf, cap,
                       "MIXED -> WITHHELD (guest swaps=%llu synthetic swaps=%llu)",
                       (unsigned long long)frame_evidence_swaps_guest_recorded(),
                       (unsigned long long)frame_evidence_swaps_synthetic());
        return buf;
    }
    if (v == SOBER_FRAME_VERDICT_DISABLED) {
        (void)snprintf(buf, cap, "DISABLED (%s=0) -> WITHHELD",
                       SOBER_FRAME_EVIDENCE_ENV);
        return buf;
    }
    if (v == SOBER_FRAME_VERDICT_ABSENT) {
        (void)snprintf(buf, cap, "ABSENT(swaps_guest|draws|window) -> WITHHELD");
        return buf;
    }
    (void)snprintf(buf, cap,
                   "INCOMPLETE(swaps=%llu draws=%llu window=%d) -> WITHHELD",
                   (unsigned long long)frame_evidence_swaps_guest(),
                   (unsigned long long)frame_evidence_draws(),
                   frame_evidence_window_bound());
    return buf;
}

void frame_evidence_snapshot(SoberFrameEvidenceSnapshot *out)
{
    uint64_t first;
    int reason = SOBER_FRAME_WR_NO_OBS;

    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->enabled = frame_evidence_enabled();
    out->guest_base = frame_evidence_guest_base();
    out->text_size = atomic_load_explicit(&s_text_size, memory_order_relaxed);
    out->swaps_total = frame_evidence_swaps_total();
    out->swaps_guest_recorded = frame_evidence_swaps_guest_recorded();
    out->swaps_synthetic = frame_evidence_swaps_synthetic();
    out->draws_total = frame_evidence_draws_total();
    out->draws_guest_recorded = frame_evidence_draws_guest_recorded();
    out->draws_synthetic = frame_evidence_draws_synthetic();
    out->draws_between = frame_evidence_draws_between();
    out->loop_ra = frame_evidence_loop_ra();
    out->loop_ra_stable = frame_evidence_loop_ra_stable();
    out->window = frame_evidence_window();
    out->window_generation = frame_evidence_window_generation();
    out->window_ra = frame_evidence_window_ra();
    out->window_ns = atomic_load_explicit(&s_window_ns, memory_order_relaxed);
    out->window_bound = frame_evidence_window_bound();
    out->window_verdict = frame_evidence_window_verdict();
    (void)fe_window_check(&reason);
    out->window_reject_code = reason;
    out->from_surface_installed = frame_evidence_from_surface_installed();
    out->mixed = frame_evidence_mixed();
    out->events = frame_evidence_event_count();
    out->dropped = frame_evidence_events_dropped();
    first = atomic_load_explicit(&s_first_ns, memory_order_relaxed);
    out->first_ns = (first == UINT64_MAX) ? 0ull : first;
    out->last_ns = atomic_load_explicit(&s_last_ns, memory_order_relaxed);
    out->verdict = frame_evidence_verdict();
    out->guest_render_ok = guest_render_ok();
}

const char *frame_evidence_describe(char *buf, size_t cap)
{
    if (!buf || cap == 0u) return "";
    (void)snprintf(buf, cap,
                   "enabled=%d base=%#lx swaps=%llu(g=%llu,s=%llu) "
                   "draws=%llu(g=%llu,s=%llu,between=%llu) stable=%d "
                   "window=%#lx gen=%u bound=%d wverdict=%d wreason=%s "
                   "fromSurface=%s mixed=%d events=%llu verdict=%s ok=%d",
                   frame_evidence_enabled(),
                   (unsigned long)frame_evidence_guest_base(),
                   (unsigned long long)frame_evidence_swaps_total(),
                   (unsigned long long)frame_evidence_swaps_guest_recorded(),
                   (unsigned long long)frame_evidence_swaps_synthetic(),
                   (unsigned long long)frame_evidence_draws_total(),
                   (unsigned long long)frame_evidence_draws_guest_recorded(),
                   (unsigned long long)frame_evidence_draws_synthetic(),
                   (unsigned long long)frame_evidence_draws_between(),
                   frame_evidence_loop_ra_stable(),
                   (unsigned long)frame_evidence_window(),
                   (unsigned)frame_evidence_window_generation(),
                   frame_evidence_window_bound(),
                   frame_evidence_window_verdict(),
                   frame_evidence_window_reject_reason(),
                   frame_evidence_from_surface_mode(),
                   frame_evidence_mixed(),
                   (unsigned long long)frame_evidence_event_count(),
                   frame_evidence_verdict_name(frame_evidence_verdict()),
                   guest_render_ok());
    return buf;
}

/* ---- pure self-test (no counters touched) -------------------------------- */
int frame_evidence_selftest(char *buf, size_t cap)
{
    const uintptr_t test_base = (uintptr_t)0x10000000u;
    uintptr_t saved = frame_evidence_guest_base();
    int fails = 0;
    char tmp[128];

    if (SOBER_FRAME_EV_MAGIC == 0u ||
        SOBER_FRAME_EVIDENCE_TEXT_SIZE == 0u ||
        SOBER_FRAME_W_RA_HELPER == 0u ||
        SOBER_FRAME_W_RA_RELEASE_OLD == 0u ||
        SOBER_FRAME_W_GOT_FROM_SURFACE != 0x72294a8u ||
        sizeof(SoberFrameEvidenceEvent) < 8u * sizeof(uintptr_t))
        ++fails;

    frame_evidence_set_guest_base(test_base);
    if (!frame_evidence_ra_is_guest(test_base + 0x100u)) ++fails;
    if (frame_evidence_ra_is_guest(test_base
            + (uintptr_t)SOBER_FRAME_EVIDENCE_TEXT_SIZE)) ++fails;
    if (frame_evidence_ra_is_guest(0x1234u)) ++fails;
    frame_evidence_set_guest_base(saved);

    if (strcmp(frame_evidence_verdict_name(SOBER_FRAME_VERDICT_MIXED),
               "MIXED") != 0) ++fails;
    if (frame_evidence_verdict_string(tmp, sizeof(tmp)) == tmp &&
        tmp[0] == '\0') ++fails;
    if (frame_evidence_window_reject_reason()[0] == '\0') ++fails;

    if (buf && cap) {
        (void)snprintf(buf, cap, "frame_evidence selftest=%s fails=%d "
                       "enabled=%d", fails ? "FAIL" : "OK", fails,
                       frame_evidence_enabled());
    }
    return fails;
}
