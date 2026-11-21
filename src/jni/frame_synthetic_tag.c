/*
 * frame_synthetic_tag.c - DEFAULT-ON explicit tag for the host-originated
 *                         phase-3 Route-B swap (logical group C, agent F8).
 *
 * See frame_synthetic_tag.h for the contract, the no-marker / no-callback /
 * no-guest-counter rules and the exact lead wiring.  NEW FILE ONLY: this file
 * and its header are the only files this agent touches; guest_render.c,
 * render_guest.c, native_jni.c, CMakeLists, docs, tests and the frozen engine
 * are untouched.
 *
 * Pure bookkeeping: enter()/leave() bracket the harness's direct 0x649d422
 * call, active() tells the swap hook to classify it synthetic, note_swap()
 * counts that classification.  Nothing here gates the swap, changes guest
 * counters, emits a marker or invokes a callback.
 */

#define _GNU_SOURCE
#include "frame_synthetic_tag.h"

#include <ctype.h>
#include <limits.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

/* ---- state -------------------------------------------------------------- */
/* Depth is thread-local: the swap hook runs synchronously on the thread that
 * called 0x649d422, so a per-thread scope is both sufficient and immune to a
 * concurrent guest frame on another thread.  Everything else is atomic so a
 * gate on a different thread can read sticky/counter state safely. */
static _Thread_local int        tl_depth;
static _Atomic int              g_seen;          /* sticky: scope opened      */
static _Atomic uint64_t         g_scope_enters;
static _Atomic uint64_t         g_scope_leaves;
static _Atomic uint64_t         g_tagged_swaps;

/* ---- enable helper (no strcasecmp: avoids any implicit-decl warnings) --- */
static int ci_equal(const char *v, const char *lit)
{
    size_t i = 0;
    for (; v[i] && lit[i]; ++i) {
        int a = tolower((unsigned char)v[i]);
        int b = tolower((unsigned char)lit[i]);
        if (a != b) return 0;
    }
    return v[i] == '\0' && lit[i] == '\0';
}

int sober_frame_synthetic_tag_enabled(void)
{
    const char *v = getenv(SOBER_FRAME_SYNTH_TAG_ENV);
    if (!v) return 1;                       /* default ON, no opt-in needed  */
    if (v[0] == '0' && v[1] == '\0') return 0;
    if (ci_equal(v, "off") || ci_equal(v, "no") || ci_equal(v, "false"))
        return 0;
    return 1;
}

const char *sober_frame_synthetic_tag_env_vars(void)
{
    return SOBER_FRAME_SYNTH_TAG_ENV;
}

/* ---- host swap-path API ------------------------------------------------- */

void sober_frame_synthetic_tag_enter(void)
{
    if (!sober_frame_synthetic_tag_enabled()) return;   /* kill switch: inert */
    if (tl_depth == 0)
        atomic_store_explicit(&g_seen, 1, memory_order_release);
    if (tl_depth < INT_MAX) {
        ++tl_depth;
        atomic_fetch_add_explicit(&g_scope_enters, 1, memory_order_relaxed);
    }
}

void sober_frame_synthetic_tag_leave(void)
{
    if (tl_depth > 0) --tl_depth;           /* always unwind, even if disabled */
    if (sober_frame_synthetic_tag_enabled())
        atomic_fetch_add_explicit(&g_scope_leaves, 1, memory_order_relaxed);
}

int sober_frame_synthetic_tag_active(void)
{
    return sober_frame_synthetic_tag_enabled() && tl_depth > 0;
}

void sober_frame_synthetic_tag_note_swap(void)
{
    if (!sober_frame_synthetic_tag_active()) return;
    atomic_fetch_add_explicit(&g_tagged_swaps, 1, memory_order_relaxed);
}

/* ---- sticky / diagnostics ------------------------------------------------ */

int sober_frame_synthetic_tag_seen(void)
{
    return atomic_load_explicit(&g_seen, memory_order_acquire);
}

uint64_t sober_frame_synthetic_tag_scope_enters(void)
{
    return atomic_load_explicit(&g_scope_enters, memory_order_relaxed);
}

uint64_t sober_frame_synthetic_tag_scope_leaves(void)
{
    return atomic_load_explicit(&g_scope_leaves, memory_order_relaxed);
}

uint64_t sober_frame_synthetic_tag_tagged_swaps(void)
{
    return atomic_load_explicit(&g_tagged_swaps, memory_order_relaxed);
}

void sober_frame_synthetic_tag_reset(void)
{
    tl_depth = 0;
    atomic_store_explicit(&g_seen, 0, memory_order_release);
    atomic_store_explicit(&g_scope_enters, 0, memory_order_relaxed);
    atomic_store_explicit(&g_scope_leaves, 0, memory_order_relaxed);
    atomic_store_explicit(&g_tagged_swaps, 0, memory_order_relaxed);
    /* Re-read the kill switch so a reset reflects a changed environment. */
    (void)sober_frame_synthetic_tag_enabled();
}

void sober_frame_synthetic_tag_snapshot(SoberFrameSyntheticTagSnapshot *out)
{
    if (!out) return;
    out->enabled      = sober_frame_synthetic_tag_enabled();
    out->depth        = tl_depth;
    out->active       = out->enabled && tl_depth > 0;
    out->seen         = atomic_load_explicit(&g_seen, memory_order_acquire);
    out->scope_enters = atomic_load_explicit(&g_scope_enters,
                                             memory_order_relaxed);
    out->scope_leaves = atomic_load_explicit(&g_scope_leaves,
                                             memory_order_relaxed);
    out->tagged_swaps = atomic_load_explicit(&g_tagged_swaps,
                                             memory_order_relaxed);
}

const char *sober_frame_synthetic_tag_describe(char *buf, size_t cap)
{
    if (!buf || cap == 0u) return "";
    snprintf(buf, cap,
             "frame-synthetic-tag: enabled=%d active=%d depth=%d seen=%d "
             "enters=%llu leaves=%llu tagged_swaps=%llu "
             "swap_rva=0x%x env=%s=%s",
             sober_frame_synthetic_tag_enabled(),
             sober_frame_synthetic_tag_active(),
             tl_depth,
             atomic_load_explicit(&g_seen, memory_order_acquire),
             (unsigned long long)atomic_load_explicit(&g_scope_enters,
                                                      memory_order_relaxed),
             (unsigned long long)atomic_load_explicit(&g_scope_leaves,
                                                      memory_order_relaxed),
             (unsigned long long)atomic_load_explicit(&g_tagged_swaps,
                                                      memory_order_relaxed),
             (unsigned)SOBER_FST_RVA_SWAP_WRAPPER,
             SOBER_FRAME_SYNTH_TAG_ENV,
             sober_frame_synthetic_tag_enabled() ? "1" : "0");
    return buf;
}

/* ---- optional self-test ------------------------------------------------- *
 * gcc -std=gnu17 -Wall -Wextra -Werror -Isrc/jni \
 *     -DSOBER_FRAME_SYNTH_TAG_SELFTEST \
 *     src/jni/frame_synthetic_tag.c -o /tmp/fst_selftest && /tmp/fst_selftest
 */
#ifdef SOBER_FRAME_SYNTH_TAG_SELFTEST
#include <assert.h>

int main(void)
{
    char b[256];
    SoberFrameSyntheticTagSnapshot s;

    /* Default ON (test env is assumed not to set the kill switch). */
    unsetenv(SOBER_FRAME_SYNTH_TAG_ENV);
    sober_frame_synthetic_tag_reset();
    assert(sober_frame_synthetic_tag_enabled() == 1);
    assert(sober_frame_synthetic_tag_active() == 0);
    assert(sober_frame_synthetic_tag_seen() == 0);
    assert(sober_frame_synthetic_tag_tagged_swaps() == 0);

    /* Scope brackets the host swap: active only inside, seen sticky after. */
    sober_frame_synthetic_tag_enter();
    assert(sober_frame_synthetic_tag_active() == 1);
    assert(sober_frame_synthetic_tag_seen() == 1);
    sober_frame_synthetic_tag_note_swap();       /* simulate hook classification */
    assert(sober_frame_synthetic_tag_tagged_swaps() == 1);
    sober_frame_synthetic_tag_leave();
    assert(sober_frame_synthetic_tag_active() == 0);
    sober_frame_synthetic_tag_note_swap();      /* outside scope: no effect */
    assert(sober_frame_synthetic_tag_tagged_swaps() == 1);

    /* Nestable. */
    sober_frame_synthetic_tag_enter();
    sober_frame_synthetic_tag_enter();
    assert(sober_frame_synthetic_tag_active() == 1);
    sober_frame_synthetic_tag_leave();
    assert(sober_frame_synthetic_tag_active() == 1);
    sober_frame_synthetic_tag_leave();
    assert(sober_frame_synthetic_tag_active() == 0);

    sober_frame_synthetic_tag_snapshot(&s);
    assert(s.enabled == 1 && s.seen == 1 && s.active == 0 && s.depth == 0);
    assert(s.scope_enters == 3u && s.scope_leaves == 3u);
    assert(s.tagged_swaps == 1u);
    assert(sober_frame_synthetic_tag_describe(b, sizeof(b)) == b);
    assert(b[0] != '\0');

    /* Kill switch (lower/upper variants) makes everything inert. */
    setenv(SOBER_FRAME_SYNTH_TAG_ENV, "0", 1);
    assert(sober_frame_synthetic_tag_enabled() == 0);
    sober_frame_synthetic_tag_reset();
    sober_frame_synthetic_tag_enter();
    assert(sober_frame_synthetic_tag_active() == 0);
    assert(sober_frame_synthetic_tag_seen() == 0);
    sober_frame_synthetic_tag_note_swap();
    assert(sober_frame_synthetic_tag_tagged_swaps() == 0);
    sober_frame_synthetic_tag_leave();

    setenv(SOBER_FRAME_SYNTH_TAG_ENV, "OFF", 1);
    assert(sober_frame_synthetic_tag_enabled() == 0);
    setenv(SOBER_FRAME_SYNTH_TAG_ENV, "false", 1);
    assert(sober_frame_synthetic_tag_enabled() == 0);
    setenv(SOBER_FRAME_SYNTH_TAG_ENV, "1", 1);
    assert(sober_frame_synthetic_tag_enabled() == 1);

    puts("frame_synthetic_tag selftest OK");
    return 0;
}
#endif /* SOBER_FRAME_SYNTH_TAG_SELFTEST */
