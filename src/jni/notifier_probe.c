/*
 * notifier_probe.c - opt-in, observation-only notifier instrumentation.
 *
 * Frozen engine: BuildID 204ad049599c96afb75b8a21d5051063e821d6c9,
 * SHA-256 3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335
 * (RVA == file offset in the first R E PT_LOAD).  See notifier_probe.h and
 * analysis/C09-marker-evidence.md sections 2.2-2.4, 6.1-6.2.
 *
 * Mechanism (x86-64):
 *   1. memcmp the first N bytes of each site against the exact expected,
 *      position-independent prologue.  FAIL CLOSED on any mismatch.
 *   2. Also verify the `call rel32` at 0x27eafae targets 0x25848e0, so the
 *      recorded return address base+0x27eafb3 is a checked fact.
 *   3. mmap an executable near page (within +/-2GB, for rel32) near each site.
 *   4. Install a 5-byte E9 rel32 detour at the site (mprotect RWX -> write ->
 *      clear-cache -> restore RX).  The trampoline saves all caller-saved regs
 *      + rflags, calls a C recorder (counters only, NO guest->Java callback,
 *      NO thunk call), restores them, replays the displaced prologue bytes,
 *      and jumps back to site+N.
 *   5. Uninstall restores the exact saved bytes and munmaps the trampolines.
 *
 * Nothing here calls emit_marker, changes the gate, or invokes any JNI slot.
 * Enabling this patch is a decision-013 withhold knob owned by the lead.
 */
#define _GNU_SOURCE
#include "notifier_probe.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif

/* ---- module state (diagnostics only; never guest-visible) -------------- */
enum { NP_SITE_NOTIFIER = 0, NP_SITE_DESCRIPTOR = 1, NP_NSITES = 2 };

typedef struct {
    uintptr_t          site;        /* base + rva                          */
    const unsigned char *expect;    /* expected prologue bytes             */
    unsigned           len;         /* patch/replay window                 */
    unsigned char      saved[32];   /* original bytes (for restoration)   */
    unsigned char     *tramp;       /* mmap'd trampoline                   */
    size_t             tramp_maplen;/* mmap length                         */
    int                live;        /* detour installed at site            */
} np_site_t;

static const unsigned char np_expect_notifier[SOBER_NP_NOTIFIER_PROLOGUE_LEN] = {
    0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x53
};
static const unsigned char np_expect_descriptor[SOBER_NP_DESCRIPTOR_PROLOGUE_LEN] = {
    0x48, 0x89, 0xf8, 0x48, 0x8b, 0x4f, 0x08
};

static np_site_t g_sites[NP_NSITES];
static uint64_t  g_hits[NP_NSITES];
static uint64_t  g_last_place_id;
static uint64_t  g_last_descriptor_rsi;
static uintptr_t g_base;
static uintptr_t g_thunk_ret;
static int       g_installed;
static __thread int g_in_handler;

/* ---- opt-in ------------------------------------------------------------ */
int sober_notifier_probe_enabled(void)
{
    const char *e = getenv(SOBER_NOTIFIER_PROBE_ENV);
    return (e != NULL && strcmp(e, "1") == 0);
}

const char *sober_notifier_probe_env_vars(void)
{
    return SOBER_NOTIFIER_PROBE_ENV "=1 (enable, default OFF; observation "
           "only; patches guest bytes so the lead MUST force-withhold the "
           "decision-013 marker while this is active; never a callback)";
}

/* ---- the recorder: counters only, no callbacks, no marker writes ------- */
static void np_record(uintptr_t rsi_val, uintptr_t site_idx)
{
    if (site_idx >= NP_NSITES)
        return;
    if (g_in_handler) {                 /* re-entrancy: still count the hit */
        __atomic_fetch_add(&g_hits[site_idx], 1, __ATOMIC_RELAXED);
        return;
    }
    g_in_handler = 1;
    __atomic_fetch_add(&g_hits[site_idx], 1, __ATOMIC_RELAXED);
    if (site_idx == NP_SITE_NOTIFIER)
        g_last_place_id = (uint64_t)rsi_val;
    else
        g_last_descriptor_rsi = (uint64_t)rsi_val;
    g_in_handler = 0;
    /* Deliberately no fprintf here: a detour must not take guest locks. */
}

/* ---- tiny machine-code emitter (x86-64, SysV) -------------------------- */
typedef struct {
    unsigned char *p;
    size_t         cap;
    size_t         n;
    int            ok;
} np_code_t;

static void np_emit(np_code_t *c, const unsigned char *b, size_t n)
{
    if (!c->ok || c->n + n > c->cap) { c->ok = 0; return; }
    memcpy(c->p + c->n, b, n);
    c->n += n;
}
static void np_emit_u32(np_code_t *c, uint32_t v)
{
    unsigned char b[4];
    memcpy(b, &v, sizeof(b));
    np_emit(c, b, sizeof(b));
}
static void np_emit_u64(np_code_t *c, uint64_t v)
{
    unsigned char b[8];
    memcpy(b, &v, sizeof(b));
    np_emit(c, b, sizeof(b));
}

/*
 * Build the trampoline.  Layout (all saved values are restored before we
 * replay the displaced prologue, so guest semantics are unchanged):
 *
 *   push rax / rcx / rdx / rsi / rdi / r8 / r9 / r10 / r11 / pushfq
 *   sub rsp,8                       ; SysV 16-byte alignment for our call
 *   mov rdi, rsi                    ; arg0 = captured RSI (placeId)
 *   mov esi, site_index             ; arg1 = site id
 *   movabs r11, np_record
 *   call r11                        ; counters only
 *   add rsp,8
 *   popfq / pop r11 / r10 / r9 / r8 / rdi / rsi / rdx / rcx / rax
 *   <displaced original prologue bytes>
 *   jmp rel32 -> site + len
 */
static int np_build_trampoline(np_site_t *s, uintptr_t handler)
{
    np_code_t c;
    unsigned char b[16];
    size_t need = 64u + (size_t)s->len;  /* emitted bytes are 62 + len */

    if (!s->tramp) return -1;
    c.p = s->tramp;
    c.cap = s->tramp_maplen;
    c.n = 0;
    c.ok = (c.cap >= need);

    /* save caller-saved regs + flags */
    b[0] = 0x50; np_emit(&c, b, 1);                       /* push rax */
    b[0] = 0x51; np_emit(&c, b, 1);                       /* push rcx */
    b[0] = 0x52; np_emit(&c, b, 1);                       /* push rdx */
    b[0] = 0x56; np_emit(&c, b, 1);                       /* push rsi */
    b[0] = 0x57; np_emit(&c, b, 1);                       /* push rdi */
    b[0] = 0x41; b[1] = 0x50; np_emit(&c, b, 2);          /* push r8  */
    b[0] = 0x41; b[1] = 0x51; np_emit(&c, b, 2);          /* push r9  */
    b[0] = 0x41; b[1] = 0x52; np_emit(&c, b, 2);          /* push r10 */
    b[0] = 0x41; b[1] = 0x53; np_emit(&c, b, 2);          /* push r11 */
    b[0] = 0x9c; np_emit(&c, b, 1);                       /* pushfq  */

    b[0] = 0x48; b[1] = 0x83; b[2] = 0xec; b[3] = 0x08;
    np_emit(&c, b, 4);                                    /* sub rsp,8 */

    b[0] = 0x48; b[1] = 0x89; b[2] = 0xf7; np_emit(&c, b, 3); /* mov rdi,rsi */
    b[0] = 0xbe; np_emit(&c, b, 1);
    np_emit_u32(&c, (uint32_t)(uintptr_t)(s - g_sites));  /* mov esi, site */

    b[0] = 0x49; b[1] = 0xbb; np_emit(&c, b, 2);          /* movabs r11, imm64 */
    np_emit_u64(&c, (uint64_t)handler);
    b[0] = 0x41; b[1] = 0xff; b[2] = 0xd3; np_emit(&c, b, 3); /* call r11 */

    b[0] = 0x48; b[1] = 0x83; b[2] = 0xc4; b[3] = 0x08;
    np_emit(&c, b, 4);                                    /* add rsp,8 */

    b[0] = 0x9d; np_emit(&c, b, 1);                       /* popfq   */
    b[0] = 0x41; b[1] = 0x5b; np_emit(&c, b, 2);          /* pop r11 */
    b[0] = 0x41; b[1] = 0x5a; np_emit(&c, b, 2);          /* pop r10 */
    b[0] = 0x41; b[1] = 0x59; np_emit(&c, b, 2);          /* pop r9  */
    b[0] = 0x41; b[1] = 0x58; np_emit(&c, b, 2);          /* pop r8  */
    b[0] = 0x5f; np_emit(&c, b, 1);                       /* pop rdi */
    b[0] = 0x5e; np_emit(&c, b, 1);                       /* pop rsi */
    b[0] = 0x5a; np_emit(&c, b, 1);                       /* pop rdx */
    b[0] = 0x59; np_emit(&c, b, 1);                       /* pop rcx */
    b[0] = 0x58; np_emit(&c, b, 1);                       /* pop rax */

    np_emit(&c, s->saved, s->len);                        /* replay prologue */

    b[0] = 0xe9; np_emit(&c, b, 1);                       /* jmp rel32 */
    {
        uintptr_t next = s->site + (uintptr_t)s->len;
        int64_t rel = (int64_t)next - (int64_t)((uintptr_t)s->tramp + c.n + 4);
        if (rel > 0x7fffffffLL || rel < -0x80000000LL) { c.ok = 0; return -1; }
        np_emit_u32(&c, (uint32_t)(int32_t)rel);
    }

    if (!c.ok) return -1;
    __builtin___clear_cache((char *)s->tramp, (char *)s->tramp + c.n);
    return 0;
}

/* Allocate a W+X page within rel32 range of the site.  No MAP_FIXED: the hint
 * is advisory so a guest mapping is never unmapped. */
static unsigned char *np_alloc_near(uintptr_t site, size_t maplen, size_t *out_len)
{
    static const uintptr_t deltas[] = {
        0x00800000u, 0x02000000u, 0x08000000u, 0x20000000u, 0x40000000u
    };
    long ps_l = sysconf(_SC_PAGESIZE);
    size_t ps = ps_l > 0 ? (size_t)ps_l : 4096u;
    unsigned i, s;

    for (i = 0; i < sizeof(deltas) / sizeof(deltas[0]); ++i) {
        for (s = 0; s < 2u; ++s) {
            uintptr_t hint = s ? (site + deltas[i]) : (site - deltas[i]);
            int64_t d;
            unsigned char *p;
            hint &= ~(uintptr_t)(ps - 1u);
            p = (unsigned char *)mmap((void *)hint, maplen,
                     PROT_READ | PROT_WRITE | PROT_EXEC,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (p == MAP_FAILED) continue;
            d = (int64_t)((uintptr_t)p - site);
            if (d > -0x78000000LL && d < 0x78000000LL) {
                *out_len = maplen;
                return p;
            }
            munmap(p, maplen);
        }
    }
    return NULL;
}

/* Write/restore helpers.  Each flips exactly one page RWX, writes, then
 * restores the page to RX (matches the phase-2 bridge pattern). */
static int np_write_bytes(np_site_t *s, const unsigned char *src, size_t n)
{
    long ps_l = sysconf(_SC_PAGESIZE);
    size_t ps = ps_l > 0 ? (size_t)ps_l : 4096u;
    uintptr_t page = s->site & ~((uintptr_t)ps - 1u);
    if (n > s->len) return -1;
    if (mprotect((void *)page, ps, PROT_READ | PROT_WRITE | PROT_EXEC) != 0)
        return -1;
    memcpy((void *)s->site, src, n);
    __builtin___clear_cache((char *)s->site, (char *)s->site + n);
    if (mprotect((void *)page, ps, PROT_READ | PROT_EXEC) != 0) return -1;
    return 0;
}

static int np_install_detour(np_site_t *s)
{
    unsigned char detour[8];
    int64_t rel;
    size_t i;

    rel = (int64_t)((uintptr_t)s->tramp - (s->site + 5u));
    if (rel > 0x7fffffffLL || rel < -0x80000000LL) return -1;

    memset(detour, 0x90, sizeof(detour));
    detour[0] = 0xe9;                                     /* jmp rel32 */
    {
        uint32_t r = (uint32_t)(int32_t)rel;
        memcpy(detour + 1, &r, sizeof(r));
    }
    if (np_write_bytes(s, detour, s->len) != 0) return -1;
    for (i = 0; i < s->len; ++i) {
        if ((unsigned char)(*(volatile unsigned char *)(s->site + i)) != detour[i])
            return -1;                                    /* write verify */
    }
    s->live = 1;
    return 0;
}

static int np_restore_site(np_site_t *s)
{
    if (!s->live) return 0;
    if (np_write_bytes(s, s->saved, s->len) != 0) return -1;
    s->live = 0;
    return 0;
}

static void np_rollback(void)
{
    unsigned i;
    for (i = 0; i < NP_NSITES; ++i) {
        (void)np_restore_site(&g_sites[i]);
        if (g_sites[i].tramp) {
            munmap(g_sites[i].tramp, g_sites[i].tramp_maplen);
            g_sites[i].tramp = NULL;
            g_sites[i].tramp_maplen = 0;
        }
        g_sites[i].live = 0;
    }
    g_installed = 0;
}

/* Verify at 0x27eafae that `call rel32` really targets 0x25848e0, and that
 * the following instruction begins at 0x27eafb3.  This makes the recorded
 * thunk return address a checked fact rather than a hard-coded claim. */
static int np_verify_thunk_context(uintptr_t base)
{
    const unsigned char *call = (const unsigned char *)(base + SOBER_NP_RVA_THUNK_CALL);
    static const unsigned char expect_call[5] = { 0xe8, 0x2d, 0x99, 0xd9, 0xff };
    int32_t rel;
    uintptr_t target;

    if (memcmp(call, expect_call, sizeof(expect_call)) != 0) return -1;
    memcpy(&rel, call + 1, sizeof(rel));
    target = (base + SOBER_NP_RVA_THUNK_RET) + (uintptr_t)(int64_t)rel;
    if (target != base + SOBER_NP_RVA_THUNK) return -1;
    g_thunk_ret = base + SOBER_NP_RVA_THUNK_RET;
    return 0;
}

/* ---- lead-facing API --------------------------------------------------- */
int sober_notifier_probe_install(void *guest_base)
{
    long ps_l;
    size_t ps, maplen, out_len;
    uintptr_t base;
    unsigned i;

    if (!sober_notifier_probe_enabled()) return SOBER_NP_DISABLED;
    if (!guest_base) return SOBER_NP_ERR;
    if (g_installed) return SOBER_NP_OK;
    base = (uintptr_t)guest_base;

    ps_l = sysconf(_SC_PAGESIZE);
    ps = ps_l > 0 ? (size_t)ps_l : 4096u;
    maplen = (ps < 4096u) ? 4096u : ps;

    memset(g_sites, 0, sizeof(g_sites));
    g_sites[NP_SITE_NOTIFIER].site = base + SOBER_NP_RVA_NOTIFIER_ENTRY;
    g_sites[NP_SITE_NOTIFIER].expect = np_expect_notifier;
    g_sites[NP_SITE_NOTIFIER].len = SOBER_NP_NOTIFIER_PROLOGUE_LEN;
    g_sites[NP_SITE_DESCRIPTOR].site = base + SOBER_NP_RVA_DESCRIPTOR_INVOKE;
    g_sites[NP_SITE_DESCRIPTOR].expect = np_expect_descriptor;
    g_sites[NP_SITE_DESCRIPTOR].len = SOBER_NP_DESCRIPTOR_PROLOGUE_LEN;

    /* 1. FAIL-CLOSED verification of all prologues + the thunk call context. */
    if (np_verify_thunk_context(base) != 0) {
        fprintf(stderr, "[notifier-probe] thunk context mismatch at 0x%x; "
                        "refusing to install\n", (unsigned)SOBER_NP_RVA_THUNK_CALL);
        np_rollback();
        return SOBER_NP_ERR;
    }
    for (i = 0; i < NP_NSITES; ++i) {
        np_site_t *s = &g_sites[i];
        if (memcmp((const void *)s->site, s->expect, s->len) != 0) {
            fprintf(stderr, "[notifier-probe] prologue mismatch at 0x%lx; "
                            "refusing to patch\n", (unsigned long)(s->site - base));
            np_rollback();
            return SOBER_NP_ERR;
        }
        memcpy(s->saved, (const void *)s->site, s->len);
    }

    /* 2. Allocate + build both trampolines before touching guest bytes. */
    for (i = 0; i < NP_NSITES; ++i) {
        np_site_t *s = &g_sites[i];
        s->tramp = np_alloc_near(s->site, maplen, &out_len);
        if (!s->tramp) {
            fprintf(stderr, "[notifier-probe] no near trampoline page for "
                            "0x%lx\n", (unsigned long)(s->site - base));
            np_rollback();
            return SOBER_NP_ERR;
        }
        s->tramp_maplen = out_len;
        if (np_build_trampoline(s, (uintptr_t)np_record) != 0) {
            fprintf(stderr, "[notifier-probe] trampoline build failed at "
                            "0x%lx\n", (unsigned long)(s->site - base));
            np_rollback();
            return SOBER_NP_ERR;
        }
    }

    /* 3. Install detours (mprotect RWX -> write -> clear-cache -> RX). */
    for (i = 0; i < NP_NSITES; ++i) {
        if (np_install_detour(&g_sites[i]) != 0) {
            fprintf(stderr, "[notifier-probe] detour install failed at "
                            "0x%lx\n", (unsigned long)(g_sites[i].site - base));
            np_rollback();
            return SOBER_NP_ERR;
        }
    }

    g_base = base;
    g_installed = 1;
    fprintf(stderr,
        "[notifier-probe] installed at 0x%x (notifier) and 0x%x (descriptor); "
        "thunk return 0x%x verified; diagnostic only, marker force-withheld\n",
        (unsigned)SOBER_NP_RVA_NOTIFIER_ENTRY,
        (unsigned)SOBER_NP_RVA_DESCRIPTOR_INVOKE,
        (unsigned)SOBER_NP_RVA_THUNK_RET);
    return SOBER_NP_OK;
}

int sober_notifier_probe_uninstall(void)
{
    if (!g_installed) return SOBER_NP_OK;
    np_rollback();                       /* preserves counters + thunk_ret */
    fprintf(stderr, "[notifier-probe] uninstalled; original prologues restored\n");
    return SOBER_NP_OK;
}

int sober_notifier_probe_active(void) { return g_installed; }

int sober_notifier_probe_should_withhold(void)
{
    return sober_notifier_probe_enabled() || g_installed ||
           (__atomic_load_n(&g_hits[0], __ATOMIC_RELAXED) != 0) ||
           (__atomic_load_n(&g_hits[1], __ATOMIC_RELAXED) != 0);
}

uint64_t sober_notifier_probe_hits_notifier(void)
{
    return __atomic_load_n(&g_hits[NP_SITE_NOTIFIER], __ATOMIC_RELAXED);
}
uint64_t sober_notifier_probe_hits_descriptor(void)
{
    return __atomic_load_n(&g_hits[NP_SITE_DESCRIPTOR], __ATOMIC_RELAXED);
}
uint64_t sober_notifier_probe_last_place_id(void) { return g_last_place_id; }
uintptr_t sober_notifier_probe_thunk_return_addr(void) { return g_thunk_ret; }

void sober_notifier_probe_snapshot(SoberNotifierProbeSnapshot *out)
{
    if (!out) return;
    out->hits_notifier = sober_notifier_probe_hits_notifier();
    out->hits_descriptor = sober_notifier_probe_hits_descriptor();
    out->last_place_id = g_last_place_id;
    out->last_descriptor_rsi = g_last_descriptor_rsi;
    out->guest_base = g_base;
    out->thunk_ret_addr = g_thunk_ret;
    out->installed = g_installed;
}

const char *sober_notifier_probe_describe(char *buf, size_t cap)
{
    if (!buf || cap == 0) return buf ? buf : "";
    snprintf(buf, cap,
        "[notifier-probe] enabled=%d active=%d notifier_hits=%llu "
        "descriptor_hits=%llu placeId=%llu thunk_ret=0x%llx "
        "(0x%x verified -> 0x%x)",
        sober_notifier_probe_enabled(), g_installed,
        (unsigned long long)sober_notifier_probe_hits_notifier(),
        (unsigned long long)sober_notifier_probe_hits_descriptor(),
        (unsigned long long)g_last_place_id,
        (unsigned long long)g_thunk_ret,
        (unsigned)SOBER_NP_RVA_NOTIFIER_ENTRY,
        (unsigned)SOBER_NP_RVA_THUNK_RET);
    return buf;
}
