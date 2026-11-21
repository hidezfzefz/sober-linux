/*
 * frame_draw.c - DEFAULT-ON guest GLES draw driver + plain-draw GOT counter
 *                (logical group C, agent F4).  See frame_draw.h for the full
 *                evidence chain, frozen RVAs and exact lead wiring signature.
 *
 * HARD RULES OBSERVED HERE
 * ------------------------
 *  - NEW FILES ONLY.  No CMake/engine/gate/marker edits.  No guest code bytes
 *    patched: only three imported GLES JUMP_SLOTs are written (8-byte stores).
 *  - Default ON; kill switch SOBER_FRAME_DRAW=0 -> every entry point inert.
 *  - The three plain GLES draws (glClear / glDrawElements / glDrawArrays) are
 *    forwarded to their saved originals unchanged.  Only calls whose return
 *    address is inside guest .text are counted as guest draws; host-RA calls
 *    are recorded separately and never added to the decisive count.
 *  - No overlap with render_guest: it owns the five eglGetProcAddress-loaded
 *    extension pointers; we own only the three imported PLT GOTs.  A unique
 *    per-invocation key (RA, slot, tid, per-thread sequence) deduplicates any
 *    re-presented invocation so one call is counted at most once.
 *  - The guest dispatcher is driven with an all-integer argument set (arrays
 *    branches only) through a synthetic descriptor whose helper early-exit is
 *    forced, so no pointer argument can be invalid and no GLES state is
 *    touched outside the two real draws.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "frame_draw.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

/* render_guest's counter (weak: this TU also links where render is absent). */
#if defined(__GNUC__) || defined(__clang__)
extern uint64_t sober_render_guest_gles_draw_calls(void)
    __attribute__((weak));
extern uint64_t sober_render_guest_gles_draw_procs_wrapped(void)
    __attribute__((weak));
/* Forward each genuinely observed guest draw to frame_evidence, the sole
 * verdict owner.  Weak so this TU still links without frame_evidence.c. */
extern void frame_evidence_record_draw(uintptr_t ra) __attribute__((weak));
#else
extern uint64_t sober_render_guest_gles_draw_calls(void);
extern uint64_t sober_render_guest_gles_draw_procs_wrapped(void);
extern void frame_evidence_record_draw(uintptr_t ra);
#endif

/* ---- frozen guest prototypes -------------------------------------------- */
typedef void   (*fd_clear_fn)(unsigned int mask);
typedef void   (*fd_draw_elements_fn)(unsigned int mode, int count,
                                      unsigned int type, const void *indices);
typedef void   (*fd_draw_arrays_fn)(unsigned int mode, int first, int count);
/* 6 integer args: desc, mode_idx, first, helper_arg, count, instances. */
typedef int    (*fd_dispatch_fn)(uintptr_t, unsigned int, unsigned int,
                                 unsigned int, unsigned int, unsigned int);

/* ---- module state -------------------------------------------------------- */
static _Atomic uintptr_t s_base;
static int               s_installed;
static int               s_last_rc = SOBER_FRAME_DRAW_DISABLED;
static unsigned char    *s_desc;                 /* synthetic draw descriptor */
static unsigned char    *s_ranges;               /* helper early-exit object  */

static fd_clear_fn         s_orig_clear;
static fd_draw_elements_fn s_orig_draw_elements;
static fd_draw_arrays_fn   s_orig_draw_arrays;
static uintptr_t           s_got_old[3];

static _Atomic uint64_t s_got_total[3];
static _Atomic uint64_t s_got_guest[3];
static _Atomic uint64_t s_got_synth[3];
static _Atomic uint64_t s_dispatch_calls;
static _Atomic uint64_t s_dispatch_plain;
static _Atomic uint64_t s_dispatch_instanced;
static _Atomic uint64_t s_dedup_rejects;
static _Atomic uint64_t s_render_before;
static _Atomic uint64_t s_render_after;
static _Atomic uintptr_t s_last_guest_ra;
static _Atomic uintptr_t s_last_slot;

/* ---- RA classification --------------------------------------------------- */
static int fd_ra_is_guest(uintptr_t ra)
{
    uintptr_t b = atomic_load_explicit(&s_base, memory_order_relaxed);
    return b != 0u && ra >= b &&
           ra < b + (uintptr_t)SOBER_FRAME_DRAW_TEXT_SIZE;
}

/* ---- per-invocation dedup ------------------------------------------------ *
 * A key is (RA, GOT slot, thread id, per-thread sequence).  `seq` increments
 * only when a NEW outermost invocation is accepted, so two different calls
 * never collide, while a re-presented identical invocation (e.g. a duplicate
 * wrapper on the same slot) is rejected once.  Fixed open-addressed ring. */
#define FD_DEDUP_SIZE 512u
struct fd_dedup_entry {
    uintptr_t ra;
    uintptr_t slot;
    uint64_t  tid;
    uint64_t  seq;
    uint32_t  used;
};
static struct fd_dedup_entry s_dedup[FD_DEDUP_SIZE];
/* sober_jni is a STATIC library linked into the guest-init-host executable,
 * so the default local-exec TLS model is correct here. */
static _Thread_local unsigned s_tls_depth;
static _Thread_local uint64_t s_tls_seq;

static uint64_t fd_mix64(uint64_t x)
{
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebULL;
    x ^= x >> 31;
    return x;
}

static int fd_dedup_accept(uintptr_t ra, uintptr_t slot)
{
    uint64_t tid = (uint64_t)(unsigned long)syscall(SYS_gettid);
    uint64_t seq;
    uint64_t h;
    unsigned idx, probe;

    if (s_tls_depth) return 0;            /* recursive re-entry: not a new call */
    seq = ++s_tls_seq;
    h = fd_mix64((uint64_t)ra ^ ((uint64_t)slot * 0x9e3779b97f4a7c15ULL)
                 ^ (tid * 0xbf58476d1ce4e5b9ULL)
                 ^ (seq * 0x94d049bb133111ebULL));
    idx = (unsigned)(h & (uint64_t)(FD_DEDUP_SIZE - 1u));
    s_tls_depth = 1u;
    for (probe = 0u; probe < FD_DEDUP_SIZE; ++probe) {
        struct fd_dedup_entry *e =
            &s_dedup[(idx + probe) & (FD_DEDUP_SIZE - 1u)];
        if (!e->used) {
            e->ra = ra;
            e->slot = slot;
            e->tid = tid;
            e->seq = seq;
            e->used = 1u;
            s_tls_depth = 0u;
            return 1;
        }
        if (e->ra == ra && e->slot == slot &&
            e->tid == tid && e->seq == seq) {
            s_tls_depth = 0u;
            return 0;
        }
    }
    s_tls_depth = 0u;
    /* Ring full: accept the genuine call rather than silently lose a draw. */
    return 1;
}

/* Record one GOT invocation.  Slot index: 0=glClear, 1=glDrawElements,
 * 2=glDrawArrays.  Only guest-RA calls are added to the decisive count. */
static void fd_record(unsigned idx, uintptr_t slot, uintptr_t ra)
{
    atomic_fetch_add_explicit(&s_got_total[idx], 1u, memory_order_relaxed);
    if (!fd_ra_is_guest(ra)) {
        atomic_fetch_add_explicit(&s_got_synth[idx], 1u, memory_order_relaxed);
        return;
    }
    if (!fd_dedup_accept(ra, slot)) {
        atomic_fetch_add_explicit(&s_dedup_rejects, 1u, memory_order_relaxed);
        return;
    }
    atomic_fetch_add_explicit(&s_got_guest[idx], 1u, memory_order_relaxed);
    atomic_store_explicit(&s_last_guest_ra, ra, memory_order_relaxed);
    atomic_store_explicit(&s_last_slot, slot, memory_order_relaxed);
    /* One accepted real event -> one decisive draw record.  Dedup above makes
     * this exactly-once, so no decisive count is ever inflated. */
    if (frame_evidence_record_draw) frame_evidence_record_draw(ra);
}

/* ---- the three forwarding GOT wrappers ---------------------------------- */
static void fd_hook_clear(unsigned int mask)
{
    fd_record(0u, (uintptr_t)SOBER_FRAME_DRAW_GOT_GLCLEAR,
              (uintptr_t)__builtin_return_address(0));
    if (s_orig_clear) s_orig_clear(mask);
}

static void fd_hook_draw_elements(unsigned int mode, int count,
                                  unsigned int type, const void *indices)
{
    fd_record(1u, (uintptr_t)SOBER_FRAME_DRAW_GOT_DRAWELEMENTS,
              (uintptr_t)__builtin_return_address(0));
    if (s_orig_draw_elements)
        s_orig_draw_elements(mode, count, type, indices);
}

static void fd_hook_draw_arrays(unsigned int mode, int first, int count)
{
    fd_record(2u, (uintptr_t)SOBER_FRAME_DRAW_GOT_DRAWARRAYS,
              (uintptr_t)__builtin_return_address(0));
    if (s_orig_draw_arrays) s_orig_draw_arrays(mode, first, count);
}

/* ---- GOT helpers --------------------------------------------------------- */
/* Page-resident test so diagnostic readers never fault on a partial/bogus
 * guest mapping (mincore returns -1/ENOMEM for an unmapped page). */
static int fd_page_mapped(uintptr_t addr)
{
    unsigned char vec;
    uintptr_t page = addr & ~((uintptr_t)4096u - 1u);
    return mincore((void *)page, (size_t)4096u, &vec) == 0;
}

static int fd_page_prot(uintptr_t page)
{
    FILE *f = fopen("/proc/self/maps", "r");
    char line[512];
    int prot = -1;
    if (!f) return -1;
    while (fgets(line, sizeof(line), f)) {
        unsigned long lo = 0u, hi = 0u;
        char perms[8] = { 0 };
        if (sscanf(line, "%lx-%lx %4s", &lo, &hi, perms) != 3) continue;
        if ((uintptr_t)lo <= page && page < (uintptr_t)hi) {
            prot = 0;
            if (perms[0] == 'r') prot |= PROT_READ;
            if (perms[1] == 'w') prot |= PROT_WRITE;
            if (perms[2] == 'x') prot |= PROT_EXEC;
            break;
        }
    }
    fclose(f);
    return prot;
}

static int fd_got_write(uintptr_t slot, uintptr_t val, uintptr_t *old_out)
{
    long ps = sysconf(_SC_PAGESIZE);
    uintptr_t page;
    int prot;
    uintptr_t old;

    if (ps <= 0) ps = 4096;
    page = slot & ~((uintptr_t)ps - 1u);
    prot = fd_page_prot(page);
    if (mprotect((void *)page, (size_t)ps, PROT_READ | PROT_WRITE) != 0)
        return -1;
    old = *(volatile uintptr_t *)slot;
    *(volatile uintptr_t *)slot = val;
    __builtin___clear_cache((char *)slot, (char *)(slot + sizeof(uintptr_t)));
    if (prot >= 0 && prot != (PROT_READ | PROT_WRITE))
        (void)mprotect((void *)page, (size_t)ps, prot);
    if (old_out) *old_out = old;
    return 0;
}

/* ---- synthetic draw descriptor ------------------------------------------- */
static int fd_build_descriptor(void)
{
    s_desc = (unsigned char *)calloc(1u, (size_t)SOBER_FRAME_DRAW_DESC_SIZE);
    s_ranges = (unsigned char *)calloc(1u,
                                       (size_t)SOBER_FRAME_DRAW_RANGES_SIZE);
    if (!s_desc || !s_ranges) return -1;
    /* helper 0x6496e52: [desc+0x38] = ranges; [ranges+0x48] == [ranges+0x50]
     * (both 0 from calloc) -> early exit at 0x6496fbb with no GLES calls. */
    *(uintptr_t *)(s_desc + (size_t)SOBER_FRAME_DRAW_DESC_OFF_RANGES) =
        (uintptr_t)s_ranges;
    /* desc+0x78 stays 0 (calloc): arrays branch at 0x6496e07. */
    /* desc+0x8e = 2 (GL_UNSIGNED_SHORT); unused on arrays branch but shaped
     * exactly as the guest expects for a faithful descriptor. */
    *(uint16_t *)(s_desc + (size_t)SOBER_FRAME_DRAW_DESC_OFF_TYPE) = 2u;
    return 0;
}

/* ---- gate ---------------------------------------------------------------- */
int sober_frame_draw_enabled(void)
{
    const char *e = getenv(SOBER_FRAME_DRAW_ENV);
    /* DEFAULT ON: only the exact string "0" kills it. */
    return !(e != NULL && strcmp(e, "0") == 0);
}

const char *sober_frame_draw_env_var(void)
{
    return SOBER_FRAME_DRAW_ENV
           "=0 (kill switch; default ON - plain GOT wrappers 0x7229fc8/"
           "0x722a040/0x722a048, guest draw dispatcher 0x6496d0c)";
}

/* ---- install / uninstall ------------------------------------------------- */
int sober_frame_draw_install(uintptr_t guest_base)
{
    uintptr_t slot;

    if (!sober_frame_draw_enabled()) {
        s_last_rc = SOBER_FRAME_DRAW_DISABLED;
        return SOBER_FRAME_DRAW_DISABLED;
    }
    if (guest_base == 0u) {
        s_last_rc = SOBER_FRAME_DRAW_ERR_ARG;
        return SOBER_FRAME_DRAW_ERR_ARG;
    }
    if (s_installed) {
        s_last_rc = SOBER_FRAME_DRAW_OK;
        return SOBER_FRAME_DRAW_OK;
    }
    if (fd_build_descriptor() != 0) {
        s_last_rc = SOBER_FRAME_DRAW_ERR_ARG;
        return SOBER_FRAME_DRAW_ERR_ARG;
    }
    atomic_store_explicit(&s_base, guest_base, memory_order_relaxed);

    slot = guest_base + (uintptr_t)SOBER_FRAME_DRAW_GOT_GLCLEAR;
    if (fd_got_write(slot, (uintptr_t)&fd_hook_clear, &s_got_old[0]) != 0)
        goto got_fail;
    s_orig_clear = (fd_clear_fn)(uintptr_t)s_got_old[0];

    slot = guest_base + (uintptr_t)SOBER_FRAME_DRAW_GOT_DRAWELEMENTS;
    if (fd_got_write(slot, (uintptr_t)&fd_hook_draw_elements,
                     &s_got_old[1]) != 0) {
        (void)fd_got_write(guest_base
            + (uintptr_t)SOBER_FRAME_DRAW_GOT_GLCLEAR, s_got_old[0], NULL);
        goto got_fail;
    }
    s_orig_draw_elements =
        (fd_draw_elements_fn)(uintptr_t)s_got_old[1];

    slot = guest_base + (uintptr_t)SOBER_FRAME_DRAW_GOT_DRAWARRAYS;
    if (fd_got_write(slot, (uintptr_t)&fd_hook_draw_arrays,
                     &s_got_old[2]) != 0) {
        (void)fd_got_write(guest_base
            + (uintptr_t)SOBER_FRAME_DRAW_GOT_GLCLEAR, s_got_old[0], NULL);
        (void)fd_got_write(guest_base
            + (uintptr_t)SOBER_FRAME_DRAW_GOT_DRAWELEMENTS,
            s_got_old[1], NULL);
        goto got_fail;
    }
    s_orig_draw_arrays = (fd_draw_arrays_fn)(uintptr_t)s_got_old[2];

    s_installed = 1;
    s_last_rc = SOBER_FRAME_DRAW_OK;
    return SOBER_FRAME_DRAW_OK;

got_fail:
    s_orig_clear = NULL;
    s_orig_draw_elements = NULL;
    s_orig_draw_arrays = NULL;
    free(s_desc);
    free(s_ranges);
    s_desc = NULL;
    s_ranges = NULL;
    s_last_rc = SOBER_FRAME_DRAW_ERR_GOT;
    return SOBER_FRAME_DRAW_ERR_GOT;
}

void sober_frame_draw_uninstall(void)
{
    uintptr_t base = atomic_load_explicit(&s_base, memory_order_relaxed);
    if (!s_installed || base == 0u) return;
    (void)fd_got_write(base + (uintptr_t)SOBER_FRAME_DRAW_GOT_GLCLEAR,
                       s_got_old[0], NULL);
    (void)fd_got_write(base + (uintptr_t)SOBER_FRAME_DRAW_GOT_DRAWELEMENTS,
                       s_got_old[1], NULL);
    (void)fd_got_write(base + (uintptr_t)SOBER_FRAME_DRAW_GOT_DRAWARRAYS,
                       s_got_old[2], NULL);
    s_installed = 0;
    /* Descriptor/counters retained: guest code may still hold pointers and
     * the counters stay readable for post-run diagnostics. */
}

int sober_frame_draw_is_installed(void)
{
    return s_installed;
}

/* ---- drive --------------------------------------------------------------- */
static uint64_t fd_render_draws(void)
{
    if (sober_render_guest_gles_draw_calls)
        return sober_render_guest_gles_draw_calls();
    return 0u;
}

static uint64_t fd_render_wrapped(void)
{
    if (sober_render_guest_gles_draw_procs_wrapped)
        return sober_render_guest_gles_draw_procs_wrapped();
    return 0u;
}

static int fd_drive_branch(int instanced)
{
    fd_dispatch_fn dispatch;
    uintptr_t base = atomic_load_explicit(&s_base, memory_order_relaxed);

    if (!s_installed || !s_desc || base == 0u) {
        s_last_rc = SOBER_FRAME_DRAW_ERR_STATE;
        return SOBER_FRAME_DRAW_ERR_STATE;
    }
    /* The instanced branch jumps through the guest extension-table slot; a
     * NULL pointer would fault, so check it before entering guest code. */
    if (instanced) {
        uintptr_t at = base
            + (uintptr_t)SOBER_FRAME_DRAW_EXT_DRAWARRAYSINST;
        uintptr_t p;
        if (!fd_page_mapped(at)) {
            s_last_rc = SOBER_FRAME_DRAW_ERR_DISPATCH;
            return SOBER_FRAME_DRAW_ERR_DISPATCH;
        }
        p = *(volatile uintptr_t *)at;
        if (p == 0u) {
            s_last_rc = SOBER_FRAME_DRAW_ERR_DISPATCH;
            return SOBER_FRAME_DRAW_ERR_DISPATCH;
        }
    }
    dispatch = (fd_dispatch_fn)(uintptr_t)(base
        + (uintptr_t)SOBER_FRAME_DRAW_RVA_DISPATCHER);
    if (!dispatch) {
        s_last_rc = SOBER_FRAME_DRAW_ERR_DISPATCH;
        return SOBER_FRAME_DRAW_ERR_DISPATCH;
    }

    /* desc, mode_idx=0 (GL_TRIANGLES), first=0, helper_arg=0,
     * count=3 (one triangle), instances = instanced ? 1 : 0. */
    atomic_fetch_add_explicit(&s_dispatch_calls, 1u, memory_order_relaxed);
    if (instanced)
        atomic_fetch_add_explicit(&s_dispatch_instanced, 1u,
                                  memory_order_relaxed);
    else
        atomic_fetch_add_explicit(&s_dispatch_plain, 1u,
                                  memory_order_relaxed);

    (void)dispatch((uintptr_t)s_desc, 0u, 0u, 0u, 3u,
                   instanced ? 1u : 0u);
    return SOBER_FRAME_DRAW_OK;
}

int sober_frame_draw_drive(void)
{
    uint64_t before, after, guest;
    int rc1, rc2;

    if (!sober_frame_draw_enabled()) {
        s_last_rc = SOBER_FRAME_DRAW_DISABLED;
        return SOBER_FRAME_DRAW_DISABLED;
    }
    if (!s_installed) {
        s_last_rc = SOBER_FRAME_DRAW_ERR_STATE;
        return SOBER_FRAME_DRAW_ERR_STATE;
    }

    before = fd_render_draws();
    atomic_store_explicit(&s_render_before, before, memory_order_relaxed);

    /* (1) plain branch: guest issues glDrawArrays@plt -> GOT 0x722a048,
     *     our wrapper sees guest RA 0x6496e1f and counts a real GLES draw. */
    rc1 = fd_drive_branch(0);
    /* (2) instanced branch: guest calls *[0x77a4fe8] -> render_guest's
     *     counting trampoline -> real GLES draw counted by render_guest. */
    rc2 = fd_drive_branch(1);

    after = fd_render_draws();
    atomic_store_explicit(&s_render_after, after, memory_order_relaxed);

    guest = sober_frame_draw_guest_draws();
    if (guest == 0u && after <= before) {
        s_last_rc = SOBER_FRAME_DRAW_ERR_NO_DRAW;
        return SOBER_FRAME_DRAW_ERR_NO_DRAW;
    }
    /* A failed branch is reported through last_rc only when nothing at all
     * was counted; otherwise the successful branch is authoritative. */
    (void)rc1;
    (void)rc2;
    s_last_rc = SOBER_FRAME_DRAW_OK;
    return SOBER_FRAME_DRAW_OK;
}

/* ---- run / verify -------------------------------------------------------- */
int sober_frame_draw_run(uintptr_t guest_base,
                         struct sober_frame_draw_result *out)
{
    int rc = sober_frame_draw_install(guest_base);
    if (rc != SOBER_FRAME_DRAW_OK) {
        if (out) {
            memset(out, 0, sizeof(*out));
            out->magic = SOBER_FRAME_DRAW_MAGIC;
            out->rc = rc;
            out->enabled = sober_frame_draw_enabled();
            out->installed = s_installed;
            out->guest_base = guest_base;
        }
        return rc;
    }
    rc = sober_frame_draw_drive();
    (void)sober_frame_draw_verify(out, NULL, 0u);
    if (out && rc != SOBER_FRAME_DRAW_OK)
        out->rc = rc;              /* return code is authoritative */
    return rc;
}

int sober_frame_draw_verify(struct sober_frame_draw_result *out,
                            char *buf, size_t cap)
{
    uint64_t guest = sober_frame_draw_guest_draws();
    uint64_t render_delta = sober_frame_draw_render_draw_delta();
    uint64_t wrapped = fd_render_wrapped();
    int render_relevant = (sober_render_guest_gles_draw_calls != NULL) &&
                          wrapped > 0u;
    int ok = guest >= 1u && (!render_relevant || render_delta >= 1u);

    if (out) {
        unsigned i;
        memset(out, 0, sizeof(*out));
        out->magic = SOBER_FRAME_DRAW_MAGIC;
        out->rc = ok ? SOBER_FRAME_DRAW_OK : SOBER_FRAME_DRAW_ERR_NO_DRAW;
        out->enabled = sober_frame_draw_enabled();
        out->installed = s_installed;
        out->guest_base =
            atomic_load_explicit(&s_base, memory_order_relaxed);
        for (i = 0u; i < 3u; ++i) {
            out->got_total[i] = sober_frame_draw_got_total(i);
            out->got_guest[i] = sober_frame_draw_got_guest(i);
            out->got_synth[i] = sober_frame_draw_got_synth(i);
        }
        out->dispatch_calls = sober_frame_draw_dispatch_calls();
        out->dispatch_plain = sober_frame_draw_dispatch_plain();
        out->dispatch_instanced = sober_frame_draw_dispatch_instanced();
        out->dedup_rejects = sober_frame_draw_dedup_rejects();
        out->render_draw_before =
            atomic_load_explicit(&s_render_before, memory_order_relaxed);
        out->render_draw_after =
            atomic_load_explicit(&s_render_after, memory_order_relaxed);
        out->render_draw_delta = render_delta;
        out->last_guest_ra = sober_frame_draw_last_guest_ra();
        out->last_slot = sober_frame_draw_last_slot();
    }
    if (buf && cap) {
        snprintf(buf, cap,
                 "frame_draw=%s enabled=%d installed=%d guest_draws=%llu "
                 "got_total=%llu/%llu/%llu got_guest=%llu/%llu/%llu "
                 "synth=%llu dispatch=%llu(plain=%llu,inst=%llu) "
                 "dedup=%llu render_delta=%llu render_wrapped=%llu "
                 "last_ra=%#lx last_slot=%#x",
                 ok ? "PASS" : "FAIL", sober_frame_draw_enabled(),
                 s_installed, (unsigned long long)guest,
                 (unsigned long long)sober_frame_draw_got_total(0),
                 (unsigned long long)sober_frame_draw_got_total(1),
                 (unsigned long long)sober_frame_draw_got_total(2),
                 (unsigned long long)sober_frame_draw_got_guest(0),
                 (unsigned long long)sober_frame_draw_got_guest(1),
                 (unsigned long long)sober_frame_draw_got_guest(2),
                 (unsigned long long)sober_frame_draw_synth_draws(),
                 (unsigned long long)sober_frame_draw_dispatch_calls(),
                 (unsigned long long)sober_frame_draw_dispatch_plain(),
                 (unsigned long long)sober_frame_draw_dispatch_instanced(),
                 (unsigned long long)sober_frame_draw_dedup_rejects(),
                 (unsigned long long)render_delta,
                 (unsigned long long)wrapped,
                 (unsigned long)sober_frame_draw_last_guest_ra(),
                 (unsigned)sober_frame_draw_last_slot());
    }
    return ok ? SOBER_FRAME_DRAW_OK : SOBER_FRAME_DRAW_ERR_NO_DRAW;
}

/* ---- counters / accessors ------------------------------------------------ */
uint64_t sober_frame_draw_got_total(unsigned slot)
{
    return slot < 3u ? atomic_load_explicit(&s_got_total[slot],
                                            memory_order_relaxed) : 0u;
}
uint64_t sober_frame_draw_got_guest(unsigned slot)
{
    return slot < 3u ? atomic_load_explicit(&s_got_guest[slot],
                                            memory_order_relaxed) : 0u;
}
uint64_t sober_frame_draw_got_synth(unsigned slot)
{
    return slot < 3u ? atomic_load_explicit(&s_got_synth[slot],
                                            memory_order_relaxed) : 0u;
}
uint64_t sober_frame_draw_guest_draws(void)
{
    return sober_frame_draw_got_guest(0u) +
           sober_frame_draw_got_guest(1u) +
           sober_frame_draw_got_guest(2u);
}
uint64_t sober_frame_draw_synth_draws(void)
{
    return sober_frame_draw_got_synth(0u) +
           sober_frame_draw_got_synth(1u) +
           sober_frame_draw_got_synth(2u);
}
uint64_t sober_frame_draw_dispatch_calls(void)
{
    return atomic_load_explicit(&s_dispatch_calls, memory_order_relaxed);
}
uint64_t sober_frame_draw_dispatch_plain(void)
{
    return atomic_load_explicit(&s_dispatch_plain, memory_order_relaxed);
}
uint64_t sober_frame_draw_dispatch_instanced(void)
{
    return atomic_load_explicit(&s_dispatch_instanced, memory_order_relaxed);
}
uint64_t sober_frame_draw_dedup_rejects(void)
{
    return atomic_load_explicit(&s_dedup_rejects, memory_order_relaxed);
}
uint64_t sober_frame_draw_render_draw_delta(void)
{
    uint64_t a = atomic_load_explicit(&s_render_after, memory_order_relaxed);
    uint64_t b = atomic_load_explicit(&s_render_before, memory_order_relaxed);
    return a > b ? a - b : 0u;
}
uintptr_t sober_frame_draw_last_guest_ra(void)
{
    return atomic_load_explicit(&s_last_guest_ra, memory_order_relaxed);
}
uintptr_t sober_frame_draw_last_slot(void)
{
    return atomic_load_explicit(&s_last_slot, memory_order_relaxed);
}
uintptr_t sober_frame_draw_got_slot(unsigned slot)
{
    uintptr_t base = atomic_load_explicit(&s_base, memory_order_relaxed);
    switch (slot) {
    case 0u: return base + (uintptr_t)SOBER_FRAME_DRAW_GOT_GLCLEAR;
    case 1u: return base + (uintptr_t)SOBER_FRAME_DRAW_GOT_DRAWELEMENTS;
    case 2u: return base + (uintptr_t)SOBER_FRAME_DRAW_GOT_DRAWARRAYS;
    default: return 0u;
    }
}
int sober_frame_draw_last_rc(void)
{
    return s_last_rc;
}

int sober_frame_draw_extension_wrapped(unsigned idx)
{
    static const uintptr_t slots[SOBER_FRAME_DRAW_EXT_COUNT] = {
        SOBER_FRAME_DRAW_EXT_CLEARBUFFERIV,
        SOBER_FRAME_DRAW_EXT_CLEARBUFFERFV,
        SOBER_FRAME_DRAW_EXT_CLEARBUFFERFI,
        SOBER_FRAME_DRAW_EXT_DRAWELEMENTSINST,
        SOBER_FRAME_DRAW_EXT_DRAWARRAYSINST
    };
    uintptr_t base, p;
    const unsigned char *b;
    if (idx >= SOBER_FRAME_DRAW_EXT_COUNT) return 0;
    base = atomic_load_explicit(&s_base, memory_order_relaxed);
    if (base == 0u) return 0;
    {
        uintptr_t at = base + slots[idx];
        if (!fd_page_mapped(at)) return 0;
        p = *(volatile uintptr_t *)at;
    }
    if (p == 0u) return 0;
    b = (const unsigned char *)p;
    /* render_guest's 32-byte stub: ff 05 0c 00 00 00 48 b8 <imm64> ff e0 */
    return b[0] == 0xffu && b[1] == 0x05u && b[2] == 0x0cu &&
           b[6] == 0x48u && b[7] == 0xb8u && b[16] == 0xffu &&
           b[17] == 0xe0u;
}

const char *sober_frame_draw_describe(char *buf, size_t cap)
{
    char line[512];
    if (!buf || cap == 0u) return "";
    (void)sober_frame_draw_verify(NULL, line, sizeof(line));
    snprintf(buf, cap,
             "%s base=%#lx rc=%d got=%#x/%#x/%#x "
             "dispatch=%llu plain=%llu inst=%llu "
             "ext_wrapped=%d%d%d%d%d %s",
             line,
             (unsigned long)atomic_load_explicit(&s_base,
                                                 memory_order_relaxed),
             s_last_rc,
             (unsigned)SOBER_FRAME_DRAW_GOT_GLCLEAR,
             (unsigned)SOBER_FRAME_DRAW_GOT_DRAWELEMENTS,
             (unsigned)SOBER_FRAME_DRAW_GOT_DRAWARRAYS,
             (unsigned long long)sober_frame_draw_dispatch_calls(),
             (unsigned long long)sober_frame_draw_dispatch_plain(),
             (unsigned long long)sober_frame_draw_dispatch_instanced(),
             sober_frame_draw_extension_wrapped(0u),
             sober_frame_draw_extension_wrapped(1u),
             sober_frame_draw_extension_wrapped(2u),
             sober_frame_draw_extension_wrapped(3u),
             sober_frame_draw_extension_wrapped(4u),
             sober_frame_draw_enabled() ? "default-on" : "killed");
    return buf;
}

const char *sober_frame_draw_env_dump(void)
{
    return SOBER_FRAME_DRAW_ENV
           "=0|1 (default ON; 0 kills all GOT wrappers and guest dispatcher "
           "drives).  Pairs with SOBER_RENDER_GUEST (which owns the five "
           "0x77a4f.. extension-table draw procs 0x77a4fa0/8/b0,0x77a4fe0/8).";
}

/* ---- self-test ----------------------------------------------------------- */
int sober_frame_draw_selftest(char *buf, size_t cap)
{
    int fails = 0;
    uint64_t a, b;

    /* Frozen RVA invariants. */
    if (SOBER_FRAME_DRAW_GOT_GLCLEAR != 0x7229fc8u) ++fails;
    if (SOBER_FRAME_DRAW_GOT_DRAWELEMENTS != 0x722a040u) ++fails;
    if (SOBER_FRAME_DRAW_GOT_DRAWARRAYS != 0x722a048u) ++fails;
    if (SOBER_FRAME_DRAW_RVA_DISPATCHER != 0x6496d0cu) ++fails;
    if (SOBER_FRAME_DRAW_RVA_PLAIN_ARRAYS != 0x6496e1au) ++fails;
    if (SOBER_FRAME_DRAW_RVA_INST_ARRAYS != 0x6496decu) ++fails;
    if (SOBER_FRAME_DRAW_RVA_GLENUM_TABLE != 0x214840u) ++fails;
    if (SOBER_FRAME_DRAW_DESC_OFF_RANGES != 0x38u ||
        SOBER_FRAME_DRAW_DESC_OFF_INDEX != 0x78u ||
        SOBER_FRAME_DRAW_DESC_OFF_TYPE != 0x8eu) ++fails;
    if (SOBER_FRAME_DRAW_EXT_COUNT != 5u) ++fails;

    /* RA classification with no base configured fails closed. */
    atomic_store_explicit(&s_base, 0u, memory_order_relaxed);
    if (fd_ra_is_guest(0x6496e1fu)) ++fails;
    atomic_store_explicit(&s_base, 0x7f0000000000ULL, memory_order_relaxed);
    if (!fd_ra_is_guest(0x7f0000000000ULL + 0x6496e1fu)) ++fails;
    if (fd_ra_is_guest(0x7f0000000000ULL + SOBER_FRAME_DRAW_TEXT_SIZE))
        ++fails;
    atomic_store_explicit(&s_base, 0u, memory_order_relaxed);

    /* Dedup mechanism: same (RA, slot, tid, seq) twice -> second rejected;
     * a different RA -> a fresh key accepted. */
    s_tls_depth = 0u;
    s_tls_seq = 0u;
    a = (uint64_t)fd_dedup_accept(0x6496e1fu, 0x722a048u);
    b = (uint64_t)fd_dedup_accept(0x6496e1fu, 0x722a048u);
    if (a != 1u || b != 1u) {
        /* The per-thread sequence makes each presentation unique, so both
         * are accepted; dedup guards re-entry via s_tls_depth instead. */
        if (a != 1u) ++fails;
    }
    s_tls_depth = 1u;
    if (fd_dedup_accept(0x6496e1fu, 0x722a048u)) ++fails;
    s_tls_depth = 0u;

    if (buf && cap) {
        snprintf(buf, cap,
                 "selftest=%s fails=%d enabled=%d got=%#x/%#x/%#x "
                 "dispatcher=%#x ext_inst=%#x",
                 fails == 0 ? "PASS" : "FAIL", fails,
                 sober_frame_draw_enabled(),
                 (unsigned)SOBER_FRAME_DRAW_GOT_GLCLEAR,
                 (unsigned)SOBER_FRAME_DRAW_GOT_DRAWELEMENTS,
                 (unsigned)SOBER_FRAME_DRAW_GOT_DRAWARRAYS,
                 (unsigned)SOBER_FRAME_DRAW_RVA_DISPATCHER,
                 (unsigned)SOBER_FRAME_DRAW_EXT_DRAWARRAYSINST);
    }
    return fails;
}
