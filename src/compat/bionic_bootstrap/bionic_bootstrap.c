/*
 * bionic_bootstrap.c - native Bionic bootstrap patcher for libroblox.so.
 *
 * OWNER: worker-1 (task #375).  AppImage BuildID 204ad049... ONLY.
 *
 * All external data (ELF headers, notes, init_array entries) is treated as
 * UNTRUSTED DATA: we bounds-check every read against the mapped object and
 * never execute anything until its NT_GNU_BUILD_ID has matched.
 *
 * What this file does, fully natively (no Android runtime, no translation):
 *   1. Gate: verify the mapped guest's build ID and ELF identity.
 *   2. Bridges: write a 12-byte movabs/jmp trampoline at each of the six
 *      validated mimalloc bridge RVAs, redirecting guest calls to native
 *      host allocator functions (mimalloc when dlopen-able, else glibc).
 *   3. Seeds: call the four validated guest initializer functions, which
 *      populate the allocator object, JNI singleton, empty string and pre-JNI
 *      registry slots.  Bridges are installed first because initializers may
 *      allocate.
 *   4. Constructors: optionally replay init_array indices [2,3)+[5,3618)
 *      (mocktail-equivalent schedule) for custom-loader integration.
 */
#define _GNU_SOURCE 1

#include "bionic_bootstrap.h"
#include "bootstrap_profile.h"

#include <elf.h>
#include <link.h>
#include <malloc.h>
#include <dlfcn.h>
#include <errno.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <signal.h>
#include <ucontext.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/* progress markers (for DECISION 008 fault localisation)              */
/* ------------------------------------------------------------------ */
/* Written immediately before each init_array entry is invoked.  A SIGSEGV
 * handler can read them to name the exact index / guest RVA that faulted.
 * Exported with default visibility (CMake builds with -fvisibility=hidden). */
volatile unsigned sober_bootstrap_ctors_done = 0;
volatile unsigned long long sober_bootstrap_ctors_current_rva = 0;

/* Handles allocated by the 8th (runtime calloc) bridge, for JNI-side field
 * transition logging (decision 028 follow-up).  Read-only outside this TU. */
void *sober_bootstrap_calloc_handles[16];
unsigned sober_bootstrap_calloc_handle_count;

/* apply() phase/sub-step markers, for fault localisation in the harness. */
volatile unsigned sober_bootstrap_phase = 0;      /* 1=verify 2=bridges 3=seeds 100=done */
volatile unsigned sober_bootstrap_bridge_index = 0;
volatile unsigned sober_bootstrap_seed_index = 0;

static int bootstrap_debug(void)
{
    const char *v = getenv("SOBER_BOOTSTRAP_DEBUG");
    return v && strcmp(v, "1") == 0;
}
#define BDEBUG(fmt, ...) do {                                              \
    if (bootstrap_debug()) fprintf(stderr, "[bootstrap] " fmt "\n",      \
                                   ##__VA_ARGS__);                        \
} while (0)

/* ------------------------------------------------------------------ */
/* Phase-4 fallback: sanitise the guest app-shell handle's +0x68 field  */
/* ------------------------------------------------------------------ */
/* Lead's decision-019 fallback.  The guest handle consumed at 0x23331ae
 * (rbx = *(rdi)) carries an uninitialised, non-canonical +0x68 pointer.
 * Zeroing our allocator bridges did not help (the handle comes from a
 * guest-internal arena), so intercept the first consumer and zero a
 * non-canonical +0x68 in place.  This is a WRITER-side fix: it only
 * normalises the field before the guest reads it; all guest logic runs. */
void *sober_handle_hook_return_addr = 0;   /* guest base + 0x23331ba */
void *sober_handle_hook_zero_target = 0;   /* guest base + 0x2333588 */

__attribute__((naked)) static void sober_handle_sanitise_stub(void)
{
    /* Replicates the 12 guest bytes we replace at 0x23331ae
     * (`mov (%rdi),%rbx ; test %rbx,%rbx ; je 0x2333588`), repairs a
     * non-canonical [rbx+0x68] in place (value 0), then resumes the guest.
     * Uses absolute indirect jumps: guest and host mappings are >2 GiB
     * apart, so a rel32 jmp cannot bridge them. */
    __asm__ volatile(
        "movq (%rdi), %rbx\n\t"
        "testq %rbx, %rbx\n\t"
        "jz 3f\n\t"
        "movq 0x68(%rbx), %rax\n\t"
        "testq %rax, %rax\n\t"
        "jz 2f\n\t"
        "testq %rax, %rax\n\t"
        "jns 2f\n\t"                  /* canonical user pointer: leave it */
        "movq $0, 0x68(%rbx)\n\t"     /* value to seed: 0 */
        "2:\n\t"
        "jmp *sober_handle_hook_return_addr(%rip)\n\t"
        "3:\n\t"
        "jmp *sober_handle_hook_zero_target(%rip)\n\t"
    );
}

static void install_handle_sanitiser(uintptr_t base)
{
    /* Opt-in: the write to [rbx+0x68] faults in the current harness (the
     * field appears to live in a read-only/RELRO page), so keep this
     * experimental hook OFF by default until that is understood. */
    if (!getenv("SOBER_HANDLE_SANITISER") || getenv("SOBER_NO_HANDLE_SANITISER")) return;
    uintptr_t site = base + 0x23331aeu;
    const unsigned char expect[12] = {
        0x48,0x8b,0x1f, 0x48,0x85,0xdb, 0x0f,0x84,0xce,0x03,0x00,0x00
    };
    if (memcmp((const void *)site, expect, 12) != 0) {
        BDEBUG("handle sanitiser: unexpected bytes at 0x23331ae; skipped");
        return;
    }
    sober_handle_hook_return_addr = (void *)(base + 0x23331bau);
    sober_handle_hook_zero_target = (void *)(base + 0x2333588u);
    long ps = sysconf(_SC_PAGESIZE);
    uintptr_t start = site & ~((uintptr_t)ps - 1);
    uintptr_t end = (site + 12 + ps - 1) & ~((uintptr_t)ps - 1);
    if (mprotect((void *)start, end - start,
                 PROT_READ | PROT_WRITE | PROT_EXEC) != 0) return;
    /* movabs rax, stub ; jmp rax */
    unsigned char j[12];
    j[0] = 0x48; j[1] = 0xb8;
    uintptr_t stub = (uintptr_t)sober_handle_sanitise_stub;
    for (unsigned i = 0; i < 8; ++i) j[2 + i] = (unsigned char)(stub >> (8 * i));
    j[10] = 0xff; j[11] = 0xe0;
    memcpy((void *)site, j, 12);
    __builtin___clear_cache((char *)site, (char *)site + 12);
    if (mprotect((void *)start, end - start, PROT_READ | PROT_EXEC) != 0)
        BDEBUG("handle sanitiser: restore RX failed");
    BDEBUG("handle sanitiser installed at 0x23331ae -> stub %p",
           (void *)(uintptr_t)sober_handle_sanitise_stub);
}

/* diagnostics */
static _Thread_local char g_last_error[512] = {0};

static int set_err(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_last_error, sizeof(g_last_error), fmt, ap);
    va_end(ap);
    return -1;
}

const char *sober_bionic_bootstrap_last_error(void) { return g_last_error; }
const char *sober_bionic_bootstrap_build_id_string(void)
{
    return SOBER_BOOTSTRAP_BUILD_ID_STR;
}
const char *sober_bionic_bootstrap_header_path(void)
{
    return "src/compat/bionic_bootstrap/bionic_bootstrap.h";
}

/* ------------------------------------------------------------------ */
/* native host allocator backing the six bridges                       */
/* ------------------------------------------------------------------ */
/* Prefer a real native mimalloc if the host process already has it; fall
 * back to glibc malloc.  Both are native code on the host ABI (SysV AMD64). */
/* Prefer mimalloc when it is present in the process, but bind it with WEAK
 * symbols resolved at load time — NEVER with dlsym() here.  These functions
 * are reached from guest allocator bridges, and calling dlsym() from that
 * context crashed glibc (si_addr=0) during the very first bridge call in the
 * harness.  Weak refs have zero runtime cost and are safe in this context. */
extern void *mi_malloc(size_t) __attribute__((weak));
extern void *mi_calloc(size_t, size_t) __attribute__((weak));
/* ---- static one-shot write-watch (decision 028 follow-up) -------------- */
static long page_size(void);
static uintptr_t g_guest_base;
extern void *sober_jni_safe_app_shell_handle(void) __attribute__((weak));
extern void *sober_jni_fresh_handle(void) __attribute__((weak));
static int       g_watch_armed;
static uintptr_t g_watch_addr;
static void     *g_watch_page;
static size_t    g_watch_page_len;
static int       g_watch_oldprot;
static struct sigaction g_watch_old_sa;
static int       g_watch_saved_sa;

static void sr_watch_segv(int sig, siginfo_t *info, void *uctx)
{
    if (g_watch_armed && info &&
        (uintptr_t)info->si_addr == g_watch_addr) {
        ucontext_t *uc = (ucontext_t *)uctx;
        uintptr_t rip = (uintptr_t)uc->uc_mcontext.gregs[REG_RIP];
        uint64_t rbp = (uint64_t)uc->uc_mcontext.gregs[REG_RBP];
        fprintf(stderr,
                "[watch] WRITE addr=%#lx rip=%#lx",
                (unsigned long)g_watch_addr, (unsigned long)rip);
        uintptr_t gb = (uintptr_t)g_guest_base;
        if (gb && rip >= gb) fprintf(stderr, " guest_rva=%#lx",
                                     (unsigned long)(rip - gb));
        fprintf(stderr, " value=%#lx rbp=%#lx\n",
                (unsigned long)*(volatile uint64_t *)g_watch_addr,
                (unsigned long)rbp);
        for (int i = 0; i < 10 && rbp > 0x10000; ++i) {
            uint64_t ret = *(uint64_t *)(uintptr_t)(rbp + 8);
            uint64_t nxt = *(uint64_t *)(uintptr_t)rbp;
            if (!ret) break;
            fprintf(stderr, "[watch]   frame %d ret=%#lx", i, (unsigned long)ret);
            if (gb && ret >= gb) fprintf(stderr, " guest_rva=%#lx",
                                         (unsigned long)(ret - gb));
            fprintf(stderr, "\n");
            if (nxt <= rbp) break;
            rbp = nxt;
        }
        if (g_watch_page) mprotect(g_watch_page, g_watch_page_len,
                                   g_watch_oldprot);
        g_watch_armed = 0;
        return;   /* let the faulting store re-execute with RW restored */
    }
    /* Not our watched address: CHAIN to the handler the harness installed. */
    if (g_watch_saved_sa) {
        if (g_watch_old_sa.sa_flags & SA_SIGINFO) {
            if (g_watch_old_sa.sa_sigaction)
                g_watch_old_sa.sa_sigaction(sig, info, uctx);
        } else if (g_watch_old_sa.sa_handler == SIG_IGN) {
            return;
        } else if (g_watch_old_sa.sa_handler && g_watch_old_sa.sa_handler != SIG_DFL) {
            g_watch_old_sa.sa_handler(sig);
        }
    }
}

static void sr_watch_arm(void *ptr)
{
    if (g_watch_armed || !ptr || !getenv("SOBER_TRACE_WATCH_AUTO")) return;
    long ps = page_size();
    g_watch_addr = (uintptr_t)ptr + 0x68u;
    uintptr_t pg = g_watch_addr & ~((uintptr_t)ps - 1);
    g_watch_page = (void *)pg;
    g_watch_page_len = (size_t)ps;
    g_watch_oldprot = PROT_READ | PROT_WRITE;
    if (mprotect(g_watch_page, g_watch_page_len, PROT_READ) != 0) {
        g_watch_page = NULL;
        return;
    }
    if (!g_watch_saved_sa) {
        sigaction(SIGSEGV, NULL, &g_watch_old_sa);
        g_watch_saved_sa = 1;
    }
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = sr_watch_segv;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, NULL);
    g_watch_armed = 1;
    fprintf(stderr, "[watch] ARMED addr=%#lx page=%#lx\n",
            (unsigned long)g_watch_addr, (unsigned long)pg);
}
extern void  mi_free(void *) __attribute__((weak));
extern void *mi_realloc(void *, size_t) __attribute__((weak));
extern void *mi_aligned_alloc(size_t, size_t) __attribute__((weak));
extern size_t mi_usable_size(const void *) __attribute__((weak));

/* Phase-4 fix (decision 019): guest handles are allocated with an
 * uninitialised +0x68 field, and the guest's lazy linker (0x234fff8) only
 * initialises it when it reads as zero.  Zero every block we hand to guest
 * code so the guard sees 0 and links the canonical backing object. */
static void *host_allocate(size_t n)
{
    if (!n) n = 1;
    void *p = malloc(n);
    if (p) memset(p, 0, n);
    return p;
}

/* ------------------------------------------------------------------ */
/* default allocator contracts (spec: src/jni/alloc_default.h)         */
/* ------------------------------------------------------------------ */
/* Three deviations were identified in the frozen engine (A03/A07):
 *   F1 - the header FORM discriminator is ZERO, not the allocation size.
 *        The guest core 0x231f13a stores 0 at 0x231f1ac/0x231f243; the old
 *        host_calloc wrote `total` into the 16-byte header, which makes the
 *        matched special-free path 0x6a28bf4 dereference a near-zero handle.
 *   F2 - bridge 0x253225e is a PLAIN (headerless) calloc; only 0x231f0e2 is
 *        the 8th, header-form calloc reached through slot 0x726c0c0.
 *   F3 - free / realloc / usable_size must subtract 0x10 ONLY for pointers
 *        that came from the header calloc.  A tiny atomic registry of header
 *        bases distinguishes those from plain host pointers.
 *
 * sober_bootstrap is linked into BOTH sober-native (no sober_jni) and
 * guest-init-host (with sober_jni).  The canonical module is discovered at
 * install time via dlsym(RTLD_DEFAULT); if it is not present in the link we
 * use the local, contract-equivalent primitives below, so the engine always
 * sees the correct behaviour.  No guest code bytes are patched here. */
#define SOBER_BB_HEADER_SIZE 0x10u
#define SOBER_BB_REG_BITS  13u
#define SOBER_BB_REG_SIZE  (1u << SOBER_BB_REG_BITS)
#define SOBER_BB_REG_MASK  ((size_t)(SOBER_BB_REG_SIZE - 1u))
#define SOBER_BB_REG_EMPTY ((uintptr_t)0u)
#define SOBER_BB_REG_TOMB  ((uintptr_t)UINTPTR_MAX)
#define SOBER_BB_REG_NPOS  ((size_t)-1)

static _Atomic uintptr_t s_bb_reg[SOBER_BB_REG_SIZE];
static _Atomic uint64_t s_bb_header_allocs;
static _Atomic uint64_t s_bb_plain_allocs;
static _Atomic uint64_t s_bb_reg_hits;
static _Atomic uint64_t s_bb_reg_misses;
static _Atomic uint64_t s_bb_realloc_moves;
static _Atomic uint64_t s_bb_reg_full;
static int bb_reg_insert(uintptr_t base); /* fwd decl for host_calloc below */

static void bb_bump(_Atomic uint64_t *c)
{
    atomic_fetch_add_explicit(c, 1u, memory_order_relaxed);
}

static size_t bb_reg_hash(uintptr_t p)
{
    uint64_t x = (uint64_t)p >> 4;
    x ^= x >> 33; x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33; x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return (size_t)x & SOBER_BB_REG_MASK;
}

static int bb_reg_insert(uintptr_t base)
{
    if (!base || base == SOBER_BB_REG_TOMB) return 0;
    for (size_t attempt = 0; attempt < SOBER_BB_REG_SIZE; ++attempt) {
        size_t idx = bb_reg_hash(base);
        size_t first_tomb = SOBER_BB_REG_NPOS;
        for (size_t k = 0; k < SOBER_BB_REG_SIZE; ++k) {
            uintptr_t v = atomic_load_explicit(&s_bb_reg[idx],
                                               memory_order_acquire);
            if (v == base) return 1;
            if (v == SOBER_BB_REG_EMPTY) {
                size_t target = (first_tomb != SOBER_BB_REG_NPOS)
                                ? first_tomb : idx;
                uintptr_t expect = (target == idx) ? SOBER_BB_REG_EMPTY
                    : atomic_load_explicit(&s_bb_reg[target],
                                           memory_order_acquire);
                if (expect != SOBER_BB_REG_EMPTY &&
                    expect != SOBER_BB_REG_TOMB) break;
                if (atomic_compare_exchange_strong_explicit(
                        &s_bb_reg[target], &expect, base,
                        memory_order_release, memory_order_acquire))
                    return 1;
                if (expect == base) return 1;
                break;
            }
            if (v == SOBER_BB_REG_TOMB &&
                first_tomb == SOBER_BB_REG_NPOS) first_tomb = idx;
            idx = (idx + 1u) & SOBER_BB_REG_MASK;
        }
    }
    bb_bump(&s_bb_reg_full);
    return 0;
}

static int bb_reg_contains(uintptr_t base)
{
    if (!base) return 0;
    size_t idx = bb_reg_hash(base);
    for (size_t k = 0; k < SOBER_BB_REG_SIZE; ++k) {
        uintptr_t v = atomic_load_explicit(&s_bb_reg[idx],
                                           memory_order_acquire);
        if (v == base) return 1;
        if (v == SOBER_BB_REG_EMPTY) return 0;
        idx = (idx + 1u) & SOBER_BB_REG_MASK;
    }
    return 0;
}

static void bb_reg_remove(uintptr_t base)
{
    if (!base) return;
    size_t idx = bb_reg_hash(base);
    for (size_t k = 0; k < SOBER_BB_REG_SIZE; ++k) {
        uintptr_t v = atomic_load_explicit(&s_bb_reg[idx],
                                           memory_order_acquire);
        if (v == base) {
            uintptr_t expect = base;
            (void)atomic_compare_exchange_strong_explicit(
                &s_bb_reg[idx], &expect, SOBER_BB_REG_TOMB,
                memory_order_release, memory_order_acquire);
            return;
        }
        if (v == SOBER_BB_REG_EMPTY) return;
        idx = (idx + 1u) & SOBER_BB_REG_MASK;
    }
}

static uintptr_t g_guest_base;  /* defined below; used by bridge diagnostics */

/* Eighth bridge (decision 028): fair header-form calloc.  Allocates n+0x10,
 * returns base+0x10, and stores ZERO in the 16-byte header (F1).  The guest's
 * own core 0x231f13a writes zero there; the old `total` value made the matched
 * special-free path 0x6a28bf4 treat it as a custom handle.  We register `base`
 * so the F3 bridges can apply the -0x10 correction only to header-form blocks.
 * Zeroed payload also keeps the lazy-link guard [handle+0x68]==0 happy. */
static void *host_calloc(size_t nmemb, size_t size)
{
    if (!nmemb) nmemb = 1;
    if (!size) size = 1;
    if (nmemb > SIZE_MAX / size) return NULL;
    size_t n = nmemb * size;
    if (n > SIZE_MAX - SOBER_BB_HEADER_SIZE) return NULL;
    size_t total = n + SOBER_BB_HEADER_SIZE;
    unsigned char *base = calloc(1, total);
    if (!base) return NULL;
    memset(base, 0, SOBER_BB_HEADER_SIZE);   /* F1: discriminator, not size */
    if (!bb_reg_insert((uintptr_t)base)) {   /* fail closed if registry full */
        free(base);
        return NULL;
    }
    bb_bump(&s_bb_header_allocs);
    return base + SOBER_BB_HEADER_SIZE;
}

/* Decision-028 side effects, kept identical for both the canonical module and
 * the local fallback path (the wrapper below calls us for either). */
static void bb_calloc_side_effects(void *p, size_t nmemb, size_t size)
{
    if (!p) return;
    if (!nmemb) nmemb = 1;
    if (!size) size = 1;
    size_t n = (nmemb > SIZE_MAX / size) ? 0u : nmemb * size;
    if (n == 0x1100u) {
        sr_watch_arm(p);
        if (sober_bootstrap_calloc_handle_count < 16)
            sober_bootstrap_calloc_handles[sober_bootstrap_calloc_handle_count++] = p;
    }
    if (getenv("SOBER_TRACE_CALLOC")) {
        uintptr_t ret = (uintptr_t)__builtin_return_address(0);
        uintptr_t gb = (uintptr_t)g_guest_base;
        fprintf(stderr,
                "[calloc-bridge] nmemb=%zu size=%zu n=%#zx ptr=%p caller=%#lx",
                nmemb, size, n, p, (unsigned long)ret);
        if (gb && ret >= gb) fprintf(stderr, " guest_rva=%#lx",
                                     (unsigned long)(ret - gb));
        fprintf(stderr, "\n");
    }
}

/* F3: subtract 0x10 only for pointers that really came from the header
 * calloc.  The guest's own thunk 0x231f53a already subtracts 0x10 and hands
 * us `base`; because we register `base` (not base+0x10), that path also lands
 * on the correct glibc pointer (p-0x10 is not in the set -> free as given). */
static void host_free(void *p)
{
    if (!p) return;
    if (getenv("SOBER_TRACE_USABLE")) {
        uintptr_t ret = (uintptr_t)__builtin_return_address(0);
        uintptr_t gb = (uintptr_t)g_guest_base;
        fprintf(stderr, "[free-bridge] ptr=%p", p);
        if (gb && ret >= gb) fprintf(stderr, " caller=%#lx", (unsigned long)(ret - gb));
        fprintf(stderr, "\n");
    }
    uintptr_t base = (uintptr_t)p - (uintptr_t)SOBER_BB_HEADER_SIZE;
    if (bb_reg_contains(base)) {
        bb_reg_remove(base);
        bb_bump(&s_bb_reg_hits);
        free((void *)base);
        return;
    }
    bb_bump(&s_bb_reg_misses);
    free(p);
}

/* F3: header-aware realloc.  A header-form block keeps its 16-byte prefix
 * (zeroed again) and its registry entry after a grow/shrink, so the guest's
 * -0x10 free thunk continues to work.  Plain pointers are passed through. */
static void *host_reallocate(void *p, size_t n)
{
    /* glibc realloc(p, 0) frees p and returns NULL; the guest's allocator
     * contract expects a usable pointer, so request one byte instead. */
    if (!n) n = 1;
    uintptr_t base = p ? (uintptr_t)p - (uintptr_t)SOBER_BB_HEADER_SIZE : 0u;
    int header = (p != NULL) && bb_reg_contains(base);
    uintptr_t alloc_base = header ? base : (uintptr_t)p;

    if (header && n > SIZE_MAX - SOBER_BB_HEADER_SIZE) return NULL;
    size_t req = header ? n + SOBER_BB_HEADER_SIZE : n;
    size_t old_usable = alloc_base ? malloc_usable_size((void *)alloc_base) : 0u;
    void *q = realloc((void *)alloc_base, req);
    if (!q) return NULL;

    if (!header) {
        if (n > old_usable)
            memset((char *)q + old_usable, 0, n - old_usable);
        return q;
    }

    bb_reg_remove(base);
    memset(q, 0, SOBER_BB_HEADER_SIZE);   /* F1 again */
    if (!bb_reg_insert((uintptr_t)q)) {
        free(q);
        return NULL;
    }
    if ((uintptr_t)q != base) bb_bump(&s_bb_realloc_moves);

    size_t new_usable = malloc_usable_size(q);
    size_t old_payload = old_usable > SOBER_BB_HEADER_SIZE
                         ? old_usable - SOBER_BB_HEADER_SIZE : 0u;
    size_t new_payload = new_usable > SOBER_BB_HEADER_SIZE
                         ? new_usable - SOBER_BB_HEADER_SIZE : 0u;
    if (n > old_payload && new_payload > old_payload) {
        size_t z = n - old_payload;
        size_t avail = new_payload - old_payload;
        if (z > avail) z = avail;
        memset((char *)q + SOBER_BB_HEADER_SIZE + old_payload, 0, z);
    }
    return (unsigned char *)q + SOBER_BB_HEADER_SIZE;
}

static void *host_aligned_allocate(size_t alignment, size_t n)
{
    if (!n) n = 1;
    if (alignment == 0) alignment = sizeof(void *);
    /* glibc aligned_alloc requires power-of-two alignment. */
    size_t a = 1;
    while (a < alignment && a < (SIZE_MAX >> 1)) a <<= 1;
    if (a == 0) a = sizeof(void *);
    void *p = aligned_alloc(a, ((n + a - 1) / a) * a);
    if (p) memset(p, 0, n);
    return p;
}

static size_t host_usable_size(void *p)
{
    if (getenv("SOBER_TRACE_USABLE")) {
        uintptr_t ret = (uintptr_t)__builtin_return_address(0);
        uintptr_t gb = (uintptr_t)g_guest_base;
        fprintf(stderr, "[usable-bridge] ptr=%p", p);
        if (gb && ret >= gb) fprintf(stderr, " caller=%#lx", (unsigned long)(ret - gb));
        fprintf(stderr, "\n");
    }
    if (!p) return 0;
    /* F3: the 16 bytes at base are the header, not guest payload. */
    uintptr_t base = (uintptr_t)p - (uintptr_t)SOBER_BB_HEADER_SIZE;
    if (bb_reg_contains(base)) {
        bb_bump(&s_bb_reg_hits);
        size_t u = malloc_usable_size((void *)base);
        return u > SOBER_BB_HEADER_SIZE ? u - SOBER_BB_HEADER_SIZE : 0u;
    }
    bb_bump(&s_bb_reg_misses);
    return malloc_usable_size(p);
}

/* F2: plain (headerless) calloc for bridge 0x253225e.  The guest thunk
 * tail-jumps to memset and returns the arena block itself -- no +0x10, no
 * registry entry. */
static void *host_calloc_plain(size_t nmemb, size_t size)
{
    if (!nmemb) nmemb = 1;
    if (!size) size = 1;
    void *p = calloc(nmemb, size);
    if (p) bb_bump(&s_bb_plain_allocs);
    return p;
}

/* ---- installable ops table (layout mirrors alloc_default.h) -------------- */
struct bb_alloc_ops {
    void *(*allocate)(size_t);
    void *(*alloc_small)(size_t);
    void (*free)(void *);
    size_t (*usable_size)(void *);
    void *(*realloc)(void *, size_t);
    void *(*aligned_alloc)(size_t, size_t);
    void *(*calloc_plain)(size_t, size_t);
    void *(*calloc_header)(size_t, size_t);
};

static struct bb_alloc_ops g_bb_ops;
static int g_bb_ops_from_module;
static const char *(*g_bb_describe)(char *, size_t);

/* Resolve the canonical module once, at install time (normal host context,
 * never from inside a bridge).  guest-init-host is -rdynamic and the module
 * exports its entry points with default visibility, so dlsym finds them when
 * they are linked in.  sober-native has no sober_jni -> local fallback. */
static void bb_resolve_alloc_ops(void)
{
    static int resolved;
    if (resolved) return;
    resolved = 1;

    g_bb_ops.allocate      = host_allocate;
    g_bb_ops.alloc_small   = host_allocate;
    g_bb_ops.free          = host_free;
    g_bb_ops.usable_size   = host_usable_size;
    g_bb_ops.realloc       = host_reallocate;
    g_bb_ops.aligned_alloc = host_aligned_allocate;
    g_bb_ops.calloc_plain  = host_calloc_plain;
    g_bb_ops.calloc_header = host_calloc;

    typedef const struct bb_alloc_ops *(*ops_fn)(void);
    ops_fn fn = (ops_fn)(uintptr_t)dlsym(RTLD_DEFAULT, "sober_alloc_default_ops");
    const struct bb_alloc_ops *o = fn ? fn() : NULL;
    if (o) {
        g_bb_ops = *o;
        g_bb_ops_from_module = 1;
        g_bb_describe = (const char *(*)(char *, size_t))(uintptr_t)
            dlsym(RTLD_DEFAULT, "sober_alloc_default_describe");
    } else {
        /* Never silently mask a missing module (lead integration constraint). */
        fprintf(stderr,
                "[boot-alloc] WARNING: alloc_default module not present in "
                "this image; using contract-identical local fallback "
                "(header=0, plain@%#x, header@%#x, slot=%#x)\n",
                SOBER_BRIDGE_CALLOC_RVA, SOBER_BRIDGE_CALLOC_RUNTIME_RVA,
                0x726c0c0u);
    }
    BDEBUG("allocator contracts: %s (header=0 plain@%#x header@%#x slot=%#x)",
           g_bb_ops_from_module ? "alloc_default module" : "local fallback",
           SOBER_BRIDGE_CALLOC_RVA, SOBER_BRIDGE_CALLOC_RUNTIME_RVA,
           0x726c0c0u);
}

/* Installed calloc entry point: contract implementation + the decision-028
 * side effects (write-watch arming, handle snapshot, trace). */
static void *bb_calloc_header_installed(size_t nmemb, size_t size)
{
    void *p = g_bb_ops.calloc_header(nmemb, size);
    bb_calloc_side_effects(p, nmemb, size);
    return p;
}

static void *bb_calloc_plain_installed(size_t nmemb, size_t size)
{
    return g_bb_ops.calloc_plain(nmemb, size);
}

/* One-line diagnostics; atexit so both harness binaries report it. */
static void bb_alloc_contract_report(void)
{
    char buf[512];
    if (g_bb_describe) {
        fprintf(stderr, "[boot-alloc] %s\n", g_bb_describe(buf, sizeof(buf)));
    } else {
        snprintf(buf, sizeof(buf),
                 "local header_allocs=%llu plain_allocs=%llu hits=%llu "
                 "misses=%llu moves=%llu reg_full=%llu",
                 (unsigned long long)s_bb_header_allocs,
                 (unsigned long long)s_bb_plain_allocs,
                 (unsigned long long)s_bb_reg_hits,
                 (unsigned long long)s_bb_reg_misses,
                 (unsigned long long)s_bb_realloc_moves,
                 (unsigned long long)s_bb_reg_full);
        fprintf(stderr, "[boot-alloc] %s\n", buf);
    }
}

/* ------------------------------------------------------------------ */
/* ELF / build-ID helpers (defensive, untrusted input)                  */
/* ------------------------------------------------------------------ */
static int build_id_matches(const uint8_t *desc, uint32_t descsz)
{
    static const uint8_t expected[SOBER_BOOTSTRAP_BUILD_ID_LEN] =
        SOBER_BOOTSTRAP_BUILD_ID_BYTES;
    return descsz == SOBER_BOOTSTRAP_BUILD_ID_LEN &&
           memcmp(desc, expected, SOBER_BOOTSTRAP_BUILD_ID_LEN) == 0;
}

/* Read NT_GNU_BUILD_ID from PT_NOTE of the object mapped at `base`.
 * Returns 1 if a build ID was found (result in out), 0 otherwise. */
static int read_build_id(uintptr_t base, uint8_t out[SOBER_BOOTSTRAP_BUILD_ID_LEN],
                         int *found)
{
    *found = 0;
    ElfW(Ehdr) *ehdr = (ElfW(Ehdr) *)base;
    if (!ehdr || ehdr->e_ident[EI_MAG0] != ELFMAG0 ||
        ehdr->e_ident[EI_MAG1] != ELFMAG1 ||
        ehdr->e_ident[EI_MAG2] != ELFMAG2 ||
        ehdr->e_ident[EI_MAG3] != ELFMAG3)
        return 0;

    ElfW(Phdr) *phdr = (ElfW(Phdr) *)(base + ehdr->e_phoff);
    for (unsigned i = 0; i < ehdr->e_phnum; ++i) {
        if (phdr[i].p_type != PT_NOTE) continue;
        uintptr_t cur = base + phdr[i].p_vaddr;
        uintptr_t end = cur + phdr[i].p_memsz;
        while (cur + 12 <= end) {
            uint32_t namesz = *(const uint32_t *)(cur + 0);
            uint32_t descsz = *(const uint32_t *)(cur + 4);
            uint32_t type   = *(const uint32_t *)(cur + 8);
            uintptr_t name = cur + 12;
            uintptr_t desc = name + ((namesz + 3u) & ~3u);
            if (desc > end || descsz > (uint32_t)(end - desc)) break;
            if (type == NT_GNU_BUILD_ID && namesz >= 4 &&
                memcmp((const void *)name, "GNU", 4) == 0) {
                memcpy(out, (const void *)desc,
                       descsz < SOBER_BOOTSTRAP_BUILD_ID_LEN
                           ? descsz : SOBER_BOOTSTRAP_BUILD_ID_LEN);
                *found = 1;
                return 1;
            }
            uintptr_t next = desc + ((descsz + 3u) & ~3u);
            if (next <= cur) break;
            cur = next;
        }
    }
    return 0;
}

SOBER_BOOTSTRAP_EXPORT int sober_bionic_bootstrap_verify_build(void *guest_base)
{
    uintptr_t base = (uintptr_t)guest_base;
    if (!base) {
        base = (uintptr_t)sober_bionic_bootstrap_find_guest();
        if (!base) return set_err("guest libroblox.so not mapped in this process");
    }
    ElfW(Ehdr) *ehdr = (ElfW(Ehdr) *)base;
    if (ehdr->e_ident[EI_CLASS] != SOBER_BOOTSTRAP_ELF_CLASS ||
        ehdr->e_ident[EI_DATA] != SOBER_BOOTSTRAP_ELF_DATA ||
        ehdr->e_type != SOBER_BOOTSTRAP_ELF_TYPE ||
        ehdr->e_machine != SOBER_BOOTSTRAP_ELF_MACHINE)
        return set_err("guest ELF identity mismatch (not x86-64 ET_DYN)");

    uint8_t id[SOBER_BOOTSTRAP_BUILD_ID_LEN];
    int found = 0;
    read_build_id(base, id, &found);
    if (!found) return set_err("guest has no NT_GNU_BUILD_ID note");
    if (!build_id_matches(id, SOBER_BOOTSTRAP_BUILD_ID_LEN))
        return set_err("build ID mismatch: guest is not AppImage 204ad049... "
                       "(refusing to patch a different build)");
    return SOBER_BIONIC_OK;
}

/* dl_iterate_phdr callback: find libroblox.so and verify its build ID. */
struct guest_search {
    uintptr_t found_base;
    int saw_name_mismatch;
};

static int guest_search_cb(struct dl_phdr_info *info, size_t size, void *data)
{
    (void)size;
    struct guest_search *s = (struct guest_search *)data;
    if (!info->dlpi_name || s->found_base) return 0;
    const char *slash = strrchr(info->dlpi_name, '/');
    const char *name = slash ? slash + 1 : info->dlpi_name;
    if (strcmp(name, "libroblox.so") != 0) return 0;

    uint8_t id[SOBER_BOOTSTRAP_BUILD_ID_LEN];
    int found = 0;
    read_build_id((uintptr_t)info->dlpi_addr, id, &found);
    if (found && build_id_matches(id, SOBER_BOOTSTRAP_BUILD_ID_LEN)) {
        s->found_base = (uintptr_t)info->dlpi_addr;
        return 1; /* stop iterating */
    }
    s->saw_name_mismatch = 1; /* a libroblox.so with the wrong build ID */
    return 0;
}

SOBER_BOOTSTRAP_EXPORT void *sober_bionic_bootstrap_find_guest(void)
{
    struct guest_search s = {0, 0};
    dl_iterate_phdr(guest_search_cb, &s);
    if (!s.found_base) {
        if (s.saw_name_mismatch)
            set_err("found libroblox.so but its build ID != 204ad049...");
        else
            set_err("libroblox.so is not mapped in this process");
        return NULL;
    }
    return (void *)s.found_base;
}

/* ------------------------------------------------------------------ */
/* memory protection helpers                                           */
/* ------------------------------------------------------------------ */
static long page_size(void)
{
    static long ps;
    if (!ps) ps = sysconf(_SC_PAGESIZE);
    return ps > 0 ? ps : 4096;
}

/* Make [addr, addr+len) writable (+exec if exec==1).  Returns 0 on success. */
static int make_writable(void *addr, size_t len, int exec)
{
    long ps = page_size();
    uintptr_t start = (uintptr_t)addr & ~((uintptr_t)ps - 1);
    uintptr_t end = ((uintptr_t)addr + len + ps - 1) & ~((uintptr_t)ps - 1);
    int prot = PROT_READ | PROT_WRITE | (exec ? PROT_EXEC : 0);
    if (mprotect((void *)start, end - start, prot) != 0)
        return set_err("mprotect(%#lx, %zu, RW%s) failed: %s",
                       (unsigned long)start, (size_t)(end - start),
                       exec ? "X" : "", strerror(errno));
    return 0;
}

/* Diagnostic-only: wrap the guest's runtime allocator-free slot so the exact
 * pointer handed to free can be observed.  Installed only behind
 * SOBER_TRACE_FREE_SLOT. */
static void (*g_free_slot_target)(void *);
static void sober_free_slot_hook(void *p)
{
    uintptr_t ret = (uintptr_t)__builtin_return_address(0);
    uintptr_t gb = (uintptr_t)g_guest_base;
    fprintf(stderr, "[free-slot] ptr=%p target=%p", p,
            (void *)(uintptr_t)g_free_slot_target);
    if (gb && ret >= gb)
        fprintf(stderr, " caller=%#lx", (unsigned long)(ret - gb));
    fprintf(stderr, "\n");
    g_free_slot_target(p);
}

SOBER_BOOTSTRAP_EXPORT int
sober_bionic_bootstrap_trace_free_slot(void *guest_base)
{
    uintptr_t base = (uintptr_t)guest_base;
    unsigned char *slot = (unsigned char *)(base + 0x726c0b8u);
    if (make_writable(slot, sizeof(void *), 0) != 0) return -1;
    memcpy(&g_free_slot_target, slot, sizeof(void *));
    void *target = (void *)(uintptr_t)&sober_free_slot_hook;
    memcpy(slot, &target, sizeof(target));
    __builtin___clear_cache((char *)slot, (char *)slot + sizeof(target));
    fprintf(stderr, "[free-slot] installed hook=%p (was %p)\n", target,
            (void *)(uintptr_t)g_free_slot_target);
    return 0;
}

/* ------------------------------------------------------------------ */
/* (a) six mimalloc bridges                                            */
/* ------------------------------------------------------------------ */
struct bridge_spec {
    uint32_t rva;
    const uint8_t *fp;
    void (*host)(void); /* native target (cast to fn ptr of right signature) */
    const char *label;
};

static int install_bridge(uintptr_t base, const struct bridge_spec *b, unsigned idx)
{
    sober_bootstrap_bridge_index = idx;
    BDEBUG("bridge[%u] %s @ %#x site=%p host=%p", idx, b->label, b->rva,
           (void *)(base + b->rva), (void *)(uintptr_t)b->host);
    const uint8_t *site = (const uint8_t *)(base + b->rva);
    /* Fingerprint check against bytes measured from the AppImage engine. */
    if (memcmp(site, b->fp, SOBER_BRIDGE_FP_LEN) != 0) {
        char hex[SOBER_BRIDGE_FP_LEN * 2 + 1];
        for (unsigned i = 0; i < SOBER_BRIDGE_FP_LEN; ++i)
            snprintf(hex + i * 2, 3, "%02x", site[i]);
        return set_err("bridge %s @ %#x: prologue mismatch (got %s)",
                       b->label, b->rva, hex);
    }
    if (make_writable((void *)site, SOBER_BRIDGE_TRAMPOLINE_LEN, 1) != 0)
        return -1;

    /* movabs rax, imm64 ; jmp rax  -- absolute jump to the native function. */
    uint8_t tramp[SOBER_BRIDGE_TRAMPOLINE_LEN];
    tramp[0] = 0x48; tramp[1] = 0xb8;
    uint64_t target = (uint64_t)(uintptr_t)b->host;
    for (unsigned i = 0; i < 8; ++i) tramp[2 + i] = (uint8_t)(target >> (8 * i));
    tramp[10] = 0xff; tramp[11] = 0xe0;
    memcpy((void *)site, tramp, sizeof(tramp));
    __builtin___clear_cache((char *)site, (char *)site + sizeof(tramp));

    /* Restore read+exec (drop writable) on the page(s) we touched. */
    long ps = page_size();
    uintptr_t start = (uintptr_t)site & ~((uintptr_t)ps - 1);
    uintptr_t end = ((uintptr_t)site + SOBER_BRIDGE_TRAMPOLINE_LEN + ps - 1)
                    & ~((uintptr_t)ps - 1);
    if (mprotect((void *)start, end - start, PROT_READ | PROT_EXEC) != 0)
        return set_err("bridge %s: restore RX failed: %s", b->label,
                       strerror(errno));
    return 0;
}

static int install_bridges(uintptr_t base)
{
    bb_resolve_alloc_ops();
    static const uint8_t fp_alloc_small[]   = SOBER_BRIDGE_FP_ALLOC_SMALL;
    static const uint8_t fp_usable[]        = SOBER_BRIDGE_FP_USABLE_SIZE;
    static const uint8_t fp_realloc[]       = SOBER_BRIDGE_FP_REALLOCATE;
    static const uint8_t fp_alloc[]         = SOBER_BRIDGE_FP_ALLOCATE;
    static const uint8_t fp_aligned[]       = SOBER_BRIDGE_FP_ALIGNED_ALLOC;
    static const uint8_t fp_free[]          = SOBER_BRIDGE_FP_FREE;
    static const uint8_t fp_calloc[]        = SOBER_BRIDGE_FP_CALLOC;
    static const uint8_t fp_calloc_rt[]     = SOBER_BRIDGE_FP_CALLOC_RUNTIME;

    /* Pointer order matches struct sober_alloc_default_ops / g_bb_ops.
     * F2: 0x253225e is the PLAIN calloc; F1: 0x231f0e2 is the header form. */
    const struct bridge_spec bridges[] = {
        { SOBER_BRIDGE_ALLOC_SMALL_RVA,  fp_alloc_small,
          (void (*)(void))g_bb_ops.alloc_small,   "small-allocate" },
        { SOBER_BRIDGE_USABLE_SIZE_RVA,  fp_usable,
          (void (*)(void))g_bb_ops.usable_size,   "usable-size" },
        { SOBER_BRIDGE_REALLOCATE_RVA,   fp_realloc,
          (void (*)(void))g_bb_ops.realloc,       "reallocate" },
        { SOBER_BRIDGE_ALLOCATE_RVA,     fp_alloc,
          (void (*)(void))g_bb_ops.allocate,      "allocate" },
        { SOBER_BRIDGE_ALIGNED_ALLOC_RVA,fp_aligned,
          (void (*)(void))g_bb_ops.aligned_alloc, "aligned-allocate-direct" },
        { SOBER_BRIDGE_FREE_RVA,         fp_free,
          (void (*)(void))g_bb_ops.free,          "free" },
        { SOBER_BRIDGE_CALLOC_RVA,       fp_calloc,
          (void (*)(void))&bb_calloc_plain_installed,  "calloc-plain" },
        { SOBER_BRIDGE_CALLOC_RUNTIME_RVA, fp_calloc_rt,
          (void (*)(void))&bb_calloc_header_installed, "calloc-header" },
    };
    for (size_t i = 0; i < sizeof(bridges) / sizeof(bridges[0]); ++i)
        if (install_bridge(base, &bridges[i], (unsigned)i) != 0) return -1;
    return 0;
}

/* Decision 028 follow-up: the calloc slot's runtime value can differ from the
 * APS2 addend (a constructor may rebind it).  Patch the SLOT itself as well
 * as the target function so every indirect call through 0x726c0c0 reaches our
 * zeroing host_calloc. */
static int patch_calloc_slot(uintptr_t base)
{
    bb_resolve_alloc_ops();
    unsigned char *slot = (unsigned char *)(base + 0x726c0c0u);
    if (make_writable(slot, sizeof(void *), 0) != 0) return -1;
    /* The slot is the runtime HEADER-form calloc (same as 0x231f0e2). */
    void *target = (void *)(uintptr_t)&bb_calloc_header_installed;
    memcpy(slot, &target, sizeof(target));
    __builtin___clear_cache((char *)slot, (char *)slot + sizeof(target));
    BDEBUG("calloc slot 0x726c0c0 -> %p (header contract)", target);
    return 0;
}

/* ------------------------------------------------------------------ */
/* (b) four data seeds                                                 */
/* ------------------------------------------------------------------ */
struct seed_spec {
    uint32_t slot_rva;
    uint32_t init_rva;
    uint32_t slot_size;
    uint32_t zero_size;
    int      call_init; /* 0 when the initializer is also a replayed ctor */
    const char *label;
};

static int seed_one(uintptr_t base, const struct seed_spec *s, unsigned idx)
{
    sober_bootstrap_seed_index = idx;
    BDEBUG("seed[%u] %s slot=%#x init=%#x call_init=%d", idx, s->label,
           s->slot_rva, s->init_rva, s->call_init);
    uint8_t *slot = (uint8_t *)(base + s->slot_rva);
    /* Slots live in the RW BSS-like LOAD.  worker-2 confirmed zero packed
     * relocations target any seed slot (nor +/- 0x20), so per-slot zeroing is
     * safe.  Sizes are exact: the registry slot in particular is ONE pointer;
     * zeroing a larger span would clobber adjacent BSS globals.  The guest
     * initializers allocate their objects separately through the bridges. */
    if (make_writable(slot, s->slot_size, 0) != 0) return -1;
    if (s->zero_size) memset(slot, 0, s->zero_size);

    if (s->slot_rva == SOBER_SEED_EMPTY_STRING_SLOT_RVA &&
        !getenv("SOBER_NO_DIRECT_SEED_EMPTY")) {
        /* Fallback: reproduce 0x225dd2a's data effects directly instead of
         * executing the guest initializer (which calls strlen@plt/memmove@plt
         * and currently faults in the harness's relocated guest).  Allocate
         * the 0x50-byte string object through the installed bridge and write
         * the exact pointer fields the guest initializer stores. */
        void *p = host_allocate(0x50);
        if (!p) return set_err("empty-string direct seed: allocation failed");
        memset(p, 0, 0x50);
        uintptr_t P = (uintptr_t)p;
        *(uintptr_t *)(base + 0x7941360u) = P;
        *(uintptr_t *)(base + 0x79413c0u) = P;
        *(uintptr_t *)(base + 0x79413c8u) = P + 0x18;
        *(uintptr_t *)(base + 0x79413b8u) = P + 0x30;
        *(uintptr_t *)(base + 0x7941380u) = P + 0x30;
        *(uintptr_t *)(base + 0x79413b0u) = P + 0x49;
        /* The original initializer also constructs the global std::string at
         * 0x7941368 from the literal at 0x49b356 via 0x1dc1208.  That helper
         * is a proper function (full prologue), so call it normally. */
        void (*str_ctor)(void *, const char *) =
            (void (*)(void *, const char *))(base + 0x1dc1208u);
        str_ctor((void *)(base + 0x7941368u), (const char *)(base + 0x49b356u));
        BDEBUG("empty-string direct seed: object=%p", p);
        return 0;
    }

    if (!s->call_init) return 0; /* e.g. JNI singleton: zeroed here, init is ctor[5] */

    /* Call the guest initializer (SysV AMD64).  Disassembly of the validated
     * initializers shows they take no meaningful arguments, but
     * 0x2251b2a (allocator object) stores ECX into a header field, so we
     * explicitly zero all six integer argument registers rather than leave
     * caller garbage in them.  6 zero args is harmless for the others. */
    typedef void (*guest_init_fn)(uintptr_t, uintptr_t, uintptr_t,
                                  uintptr_t, uintptr_t, uintptr_t);
    guest_init_fn init_fn = (guest_init_fn)(base + s->init_rva);
    init_fn(0, 0, 0, 0, 0, 0);
    return 0;
}

static int seed_slots(uintptr_t base)
{
    const struct seed_spec seeds[] = {
        { SOBER_SEED_ALLOCATOR_SLOT_RVA,     SOBER_SEED_ALLOCATOR_INIT_RVA,
          SOBER_SEED_ALLOCATOR_SLOT_SIZE,     SOBER_SEED_ALLOCATOR_ZERO_SIZE,
          1, "allocator object" },
        /* JNI singleton initializer 0x1dba834 IS ctor[5].  Zero the 1024 B
         * here but let the replayed ctor[5] call the non-idempotent xchg —
         * calling it in apply() as well would run it twice. */
        { SOBER_SEED_JNI_SINGLETON_SLOT_RVA, SOBER_SEED_JNI_SINGLETON_INIT_RVA,
          SOBER_SEED_JNI_SINGLETON_SLOT_SIZE, SOBER_SEED_JNI_SINGLETON_ZERO_SIZE,
          0, "JNI singleton (init = ctor[5])" },
        { SOBER_SEED_EMPTY_STRING_SLOT_RVA,  SOBER_SEED_EMPTY_STRING_INIT_RVA,
          SOBER_SEED_EMPTY_STRING_SLOT_SIZE,  SOBER_SEED_EMPTY_STRING_ZERO_SIZE,
          1, "empty string" },
        { SOBER_SEED_REGISTRY_SLOT_RVA,      SOBER_SEED_REGISTRY_INIT_RVA,
          SOBER_SEED_REGISTRY_SLOT_SIZE,      SOBER_SEED_REGISTRY_ZERO_SIZE,
          1, "pre-JNI registry" },
    };
    unsigned mask = 0xf;
    const char *m = getenv("SOBER_SEED_MASK");
    if (m) mask = (unsigned)strtoul(m, NULL, 0);
    for (size_t i = 0; i < sizeof(seeds) / sizeof(seeds[0]); ++i) {
        if (!(mask & (1u << i))) { BDEBUG("seed[%u] SKIPPED by mask", (unsigned)i); continue; }
        if (seed_one(base, &seeds[i], (unsigned)i) != 0)
            return set_err("failed to seed %s", seeds[i].label);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* (c) constructor range replay                                        */
/* ------------------------------------------------------------------ */
/* Read the authoritative DT_INIT_ARRAY / DT_INIT_ARRAYSZ from PT_DYNAMIC.
 * 0x7220e58 is a VIRTUAL ADDRESS (added directly to the load base), not a
 * file offset; its file offset is 0x721ce58.  We cross-check the tag against
 * the validated profile value and fail loudly on mismatch. */
static int read_init_array_vma(uintptr_t base, uint64_t *out_rva,
                               unsigned int *out_count)
{
    ElfW(Ehdr) *ehdr = (ElfW(Ehdr) *)base;
    ElfW(Phdr) *phdr = (ElfW(Phdr) *)(base + ehdr->e_phoff);
    int saw_tag = 0;
    *out_rva = SOBER_INIT_ARRAY_RVA;
    *out_count = SOBER_INIT_ARRAY_COUNT;
    for (unsigned i = 0; i < ehdr->e_phnum; ++i) {
        if (phdr[i].p_type != PT_DYNAMIC) continue;
        const ElfW(Dyn) *dyn = (const ElfW(Dyn) *)(base + phdr[i].p_vaddr);
        size_t n = phdr[i].p_memsz / sizeof(*dyn);
        for (size_t j = 0; j < n && dyn[j].d_tag != DT_NULL; ++j) {
            if (dyn[j].d_tag == DT_INIT_ARRAY) {
                uint64_t tag_rva = (uint64_t)(uintptr_t)dyn[j].d_un.d_ptr;
                /* Normally a link-time VMA; if a loader/packed-RELA pass made
                 * it absolute, normalise it back to a VMA. */
                if (base && tag_rva >= (uint64_t)base)
                    tag_rva -= (uint64_t)base;
                saw_tag = 1;
                if (tag_rva != SOBER_INIT_ARRAY_RVA)
                    return set_err("DT_INIT_ARRAY %#llx != validated %#x",
                                   (unsigned long long)tag_rva,
                                   SOBER_INIT_ARRAY_RVA);
                *out_rva = tag_rva;
            } else if (dyn[j].d_tag == DT_INIT_ARRAYSZ) {
                uint64_t sz = (uint64_t)dyn[j].d_un.d_val;
                /* A harness that suppresses loader-driven ctors sets this tag
                 * to 0; that is expected.  Always use the validated count. */
                if (sz == 0 ||
                    sz != (uint64_t)SOBER_INIT_ARRAY_COUNT * SOBER_INIT_ARRAY_ENTRY_SIZE)
                    BDEBUG("DT_INIT_ARRAYSZ=%#llx (expected %#x); using profile 3618",
                           (unsigned long long)sz,
                           SOBER_INIT_ARRAY_COUNT * SOBER_INIT_ARRAY_ENTRY_SIZE);
                *out_count = SOBER_INIT_ARRAY_COUNT;
            }
        }
    }
    if (!saw_tag)
        return set_err("no DT_INIT_ARRAY tag (expected %#x)", SOBER_INIT_ARRAY_RVA);
    return 0;
}

SOBER_BOOTSTRAP_EXPORT int sober_bionic_bootstrap_init_array_info(
    void *guest_base, unsigned long long *out_rva, unsigned int *out_count)
{
    uintptr_t base = (uintptr_t)guest_base;
    if (!base) base = (uintptr_t)sober_bionic_bootstrap_find_guest();
    if (!base) return SOBER_BIONIC_ERR_NO_GUEST;
    if (sober_bionic_bootstrap_verify_build((void *)base) != 0) return -1;
    uint64_t rva; unsigned int count;
    if (read_init_array_vma(base, &rva, &count) != 0) return -1;
    if (out_rva) *out_rva = (unsigned long long)rva;
    if (out_count) *out_count = count;
    return SOBER_BIONIC_OK;
}

/* Highest guest VMA end (PT_LOAD 3: 0x722e540 + 0xb6ecdc = 0x7d9d21c). */
#define SOBER_GUEST_VA_SPAN 0x08000000u

static int replay_range(uintptr_t base, uint64_t array_rva,
                        uint32_t begin, uint32_t end)
{
    for (uint32_t i = begin; i < end; ++i) {
        const uint64_t *entry =
            (const uint64_t *)(base + array_rva + (uint64_t)i * SOBER_INIT_ARRAY_ENTRY_SIZE);
        uint64_t raw = *entry;
        if (raw == 0) continue; /* NULL entries are allowed by ELF */
        /* Packed RELATIVE relocations (decision 008) write base+VMA, i.e. an
         * ABSOLUTE address, into each init_array slot.  A plain ET_DYN image
         * would instead hold the relative VMA.  Accept both: if the value
         * already lies inside the guest mapping, use it directly; otherwise
         * add the load base.  Adding the base twice jumps to garbage. */
        uintptr_t fn;
        if (raw >= (uint64_t)base &&
            raw < (uint64_t)base + SOBER_GUEST_VA_SPAN)
            fn = (uintptr_t)raw;
        else
            fn = (uintptr_t)(base + raw);
        uint64_t fn_rva = (fn > (uintptr_t)base) ? (uint64_t)(fn - (uintptr_t)base) : raw;
        /* Publish progress BEFORE the call so a SIGSEGV handler can name the
         * exact init_array index and guest-side constructor RVA. */
        sober_bootstrap_ctors_done = i;
        sober_bootstrap_ctors_current_rva = (unsigned long long)fn_rva;
        BDEBUG("ctor[%u] raw=%#llx fn=%p rva=%#llx", i,
               (unsigned long long)raw, (void *)fn,
               (unsigned long long)fn_rva);
        ((void (*)(void))fn)();
    }
    return 0;
}

SOBER_BOOTSTRAP_EXPORT int sober_bionic_bootstrap_replay_constructors(void *guest_base)
{
    uintptr_t base = (uintptr_t)guest_base;
    if (!base) base = (uintptr_t)sober_bionic_bootstrap_find_guest();
    if (!base) return -1;
    if (sober_bionic_bootstrap_verify_build((void *)base) != 0) return -1;

    uint64_t array_rva = SOBER_INIT_ARRAY_RVA;
    unsigned int array_count = 0;
    if (read_init_array_vma(base, &array_rva, &array_count) != 0) return -1;

    if (replay_range(base, array_rva, SOBER_CTOR_RANGE0_BEGIN,
                     SOBER_CTOR_RANGE0_END) != 0)
        return set_err("constructor replay failed in range [2,3)");
    if (bootstrap_debug()) {
        uintptr_t slb = (uintptr_t)*(void **)(base + 0x7229430u) - 0x86d7u;
        BDEBUG("stub libc base=%p fn_sysconf=%p fn_memmove=%p",
               (void *)slb, *(void **)(slb + 0xeef8u),
               *(void **)(slb + 0xeca0u));
    }
    /* Arena init 0x1dbb178 is self-guarded (lock cmpxchg on 0x7cd8fb0) and is
     * idempotent.  ctor[2] is supposed to reach it via 0x1dbe475, but the
     * harness's global-slot dump shows arena_guard@0x7cd8fb0 and
     * arena_table@0x7cd9040 still NULL after ctor[2].  Call it explicitly
     * here (after ctor[2], before thread init) so the arena is populated. */
    BDEBUG("replaying arena init 0x%x", SOBER_ARENA_INIT_RVA);
    /* If ctor[2] set the guard but left the table NULL (observed in the
     * harness), clear the guard so the self-guarded initializer retries. */
    if (!*(uintptr_t *)(base + 0x7cd9040u))
        *(uintptr_t *)(base + 0x7cd8fb0u) = 0;
    ((void (*)(void))(base + SOBER_ARENA_INIT_RVA))();
    BDEBUG("after arena init: guard=%p table=%p alloc_flag=%p jni=%p",
           *(void **)(base + 0x7cd8fb0u), *(void **)(base + 0x7cd9040u),
           *(void **)(base + 0x783daa8u), *(void **)(base + 0x7d9cf40u));
    /* Profile / worker-2: allocator thread init must run after ctor[2]. */
    ((void (*)(void))(base + SOBER_ALLOCATOR_THREAD_INIT_RVA))();
    if (replay_range(base, array_rva, SOBER_CTOR_RANGE1_BEGIN,
                     SOBER_CTOR_RANGE1_END) != 0)
        return set_err("constructor replay failed in range [5,3618)");
    return SOBER_BIONIC_OK;
}

/* ------------------------------------------------------------------ */
/* public apply / run                                                  */
/* ------------------------------------------------------------------ */
static atomic_int g_applied = 0;
/* forward-declared near the top for bridge diagnostics */
static uintptr_t  g_guest_base;

/* ---- opt-in caller-side pointer fix (decision 4c / worker-1) ---------- *
 * At guest RVA 0x6a26e78 the instruction `mov %rax,%r14` captures the result
 * of getter 0x231f294.  In the failing app-shell path that result is a CODE
 * address (guest RVA 0x233263f), which is later passed as the 0xc0dedbad
 * object to consumer 0x23330e7.  Patch the 5 bytes at 0x6a26e78 with a near
 * jmp to a native stub that validates/substitutes RAX, executes the two
 * displaced instructions, and returns to 0x6a26e7e.
 */
#define SOBER_CALLER_PATCH_RVA  0x6a26e78u
#define SOBER_CALLER_RETURN_RVA 0x6a26e7eu

static unsigned g_caller_fix_hits;
static unsigned g_caller_fix_subs;
static uintptr_t sr_valid_caller_handle(uintptr_t v)
{
    g_caller_fix_hits++;
    if (g_caller_fix_hits <= 20)
        fprintf(stderr, "[caller-fix] hit %u v=%#lx\n", g_caller_fix_hits,
                (unsigned long)v);
    uintptr_t gb = (uintptr_t)g_guest_base;
    int canonical = (v < 0x0000800000000000ULL) ||
                    (v >= 0xffff800000000000ULL);
    if (getenv("SOBER_CALLER_PTR_FORCE") || !canonical || !v ||
        (gb && v >= gb && v < gb + 0x8000000u)) {
        void *sub = sober_jni_safe_app_shell_handle();
        g_caller_fix_subs++;
        if (sub) return (uintptr_t)sub;
    }
    return v;
}

static void sr_caller_fix_report(void)
{
    fprintf(stderr, "[caller-fix] hits=%u substitutions=%u\n",
            g_caller_fix_hits, g_caller_fix_subs);
}

/* Decision 033: the live app-shell path reaches F_inner 0x23330e7 from caller
 * B at 0x2457721.  Its object comes from the stale curl easy-handle holder at
 * [bigobj+0x460] and is copied into RDI at 0x245771e (`mov %rbx,%rdi`).  Zero
 * RDI there so F_inner takes its existing null-pointer safe bail. */
/* Diagnostic: one-shot int3 at F_inner entry.  The harness SIGTRAP handler
 * prints a frame-pointer backtrace; at function entry RBP still belongs to
 * the caller, so the first return address identifies the real caller. */
/* Decision 034: app-shell caller 0x249c3f2 loads the JNI singleton from
 * global 0x72293f8.  JNI_OnLoad normally initializes it, but we skip that
 * function, leaving a stale code pointer that becomes the bogus object.
 * Seed the global with a shaped native handle before app-shell startup. */
static void *g_valid_guest_handle;

/* Decision 036: acquire a guest-owned handle from normal harness context,
 * before writing any host object into the guest singleton global. */
SOBER_BOOTSTRAP_EXPORT int sober_bionic_bootstrap_acquire_valid_handle(void *guest_base)
{
    uintptr_t base = guest_base ? (uintptr_t)guest_base : g_guest_base;
    if (!base) return set_err("acquire handle: no guest base");
    if (g_valid_guest_handle) return 0;
    void *(*getter)(void) = (void *(*)(void))(base + 0x231f294u);
    void *h = getter();
    if (!h) return set_err("acquire handle: getter returned NULL");
    if (*(uint32_t *)h != 0xc0dedbadu)
        return set_err("acquire handle: bad magic %#x", *(uint32_t *)h);
    if ((uintptr_t)h >= base &&
        (uintptr_t)h - base < (uintptr_t)SOBER_GUEST_VA_SPAN)
        return set_err("acquire handle: getter returned guest-code pointer");
    g_valid_guest_handle = h;
    fprintf(stderr, "[acquire-handle] guest-owned %p magic=%#x\n", h, *(uint32_t *)h);
    return 0;
}

SOBER_BOOTSTRAP_EXPORT int sober_bionic_bootstrap_seed_jni_singleton(void *guest_base)
{
    uintptr_t base = (uintptr_t)guest_base;
    if (!getenv("SOBER_SEED_JNI_SINGLETON")) return 0;
    if (!&sober_jni_safe_app_shell_handle)
        return set_err("singleton seed: JNI bridge not linked");
    void *h = sober_jni_safe_app_shell_handle();
    if (!h) return set_err("singleton seed: no handle");
    uint8_t **slot = (uint8_t **)(base + 0x72293f8u);
    if (make_writable(slot, sizeof(void *), 0) != 0) return -1;
    *slot = (uint8_t *)h;
    fprintf(stderr, "[singleton-seed] 0x72293f8 <- %p\n", h);
    /* Obtain the guest's own canonical handle from normal harness context
     * (calling guest code from inside the logger stub is unsafe). */
    if (!g_valid_guest_handle) {
        void *(*getter)(void) = (void *(*)(void))(base + 0x231f294u);
        g_valid_guest_handle = getter();
        fprintf(stderr, "[singleton-seed] guest getter handle = %p\n",
                g_valid_guest_handle);
    }
    return 0;
}

/* Decision 035: non-perturbing F_inner entry logger.  No int3, no signals. */
static unsigned long g_finner_log_hits;

void sober_finner_log(unsigned long obj, unsigned long val68, unsigned long ret,
                      unsigned long slot)
{
    g_finner_log_hits++;
    fprintf(stderr, "[finner-log] hit=%lu obj=%#lx +0x68=%#lx ret=%#lx slot=%#lx\n",
            g_finner_log_hits, obj, val68, ret, slot);
    if ((getenv("SOBER_HELPER_REPAIR") || getenv("SOBER_HELPER_REPAIR_V2")) &&
        slot && obj && g_valid_guest_handle) {
        /* find_guest() matches dlpi_name "libroblox.so", but our guest is a
         * memfd dlopen'd as /proc/self/fd/N, so it always returns NULL here.
         * Use the authoritative bias captured by apply() instead. */
        uintptr_t base = g_guest_base;
        if (base && obj >= base && obj - base < SOBER_GUEST_VA_SPAN) {
            void *fresh = NULL;
            if (&sober_jni_fresh_handle) fresh = sober_jni_fresh_handle();
            void *sub = fresh ? fresh : g_valid_guest_handle;
            *(void **)slot = sub;
            fprintf(stderr, "[helper-repair] bogus %#lx -> fresh=%p substitute=%p\n",
                    obj, fresh, sub);
        }
    }
}

__attribute__((naked)) static void sober_finner_logger_stub(void)
{
    __asm__ volatile (
        "pushfq\n\t"
        "subq $0x60, %%rsp\n\t"
        "movq %%rax, 0x00(%%rsp)\n\t"
        "movq %%rcx, 0x08(%%rsp)\n\t"
        "movq %%rdx, 0x10(%%rsp)\n\t"
        "movq %%rsi, 0x18(%%rsp)\n\t"
        "movq %%rdi, 0x20(%%rsp)\n\t"
        "movq %%r8,  0x28(%%rsp)\n\t"
        "movq %%r9,  0x30(%%rsp)\n\t"
        "movq %%r10, 0x38(%%rsp)\n\t"
        "movq %%r11, 0x40(%%rsp)\n\t"
        "stmxcsr 0x48(%%rsp)\n\t"
        /* sober_finner_log(obj, val68, ret_addr, slot): at this stub's entry
         * RDI is &slot, so load the object and the QWORD at +0x68, and pass
         * the saved RETURN ADDRESS VALUE (not its address) in RDX. */
        "movq 0x20(%%rsp), %%rax\n\t"
        "movq (%%rax), %%rdi\n\t"          /* arg1 = object */
        "xorq %%rsi, %%rsi\n\t"            /* arg2 default 0 */
        "movabsq $0x0000000100000000, %%rax\n\t"
        "cmpq %%rax, %%rdi\n\t"
        "jb 1f\n\t"
        "movabsq $0x0000800000000000, %%rax\n\t"
        "cmpq %%rax, %%rdi\n\t"
        "jae 1f\n\t"
        "movq 0x68(%%rdi), %%rsi\n\t"      /* arg2 = [obj+0x68] */
        "1:\n\t"
        "movq 0x68(%%rsp), %%rdx\n\t"      /* arg3 = return address value */
        "movq 0x20(%%rsp), %%rcx\n\t"      /* arg4 = &slot */
        "movabsq $sober_finner_log, %%rax\n\t"
        "call *%%rax\n\t"
        "ldmxcsr 0x48(%%rsp)\n\t"
        "movq 0x40(%%rsp), %%r11\n\t"
        "movq 0x38(%%rsp), %%r10\n\t"
        "movq 0x30(%%rsp), %%r9\n\t"
        "movq 0x28(%%rsp), %%r8\n\t"
        "movq 0x20(%%rsp), %%rdi\n\t"
        "movq 0x18(%%rsp), %%rsi\n\t"
        "movq 0x10(%%rsp), %%rdx\n\t"
        "movq 0x08(%%rsp), %%rcx\n\t"
        "movq 0x00(%%rsp), %%rax\n\t"
        "addq $0x60, %%rsp\n\t"
        "popfq\n\t"
        "pushq %%rbp\n\t"
        "movq %%rsp, %%rbp\n\t"
        "pushq %%r14\n\t"
        "jmp *%%rax\n\t"  /* patched below: replaced with rel32 jump to G+6 */
        : : : "memory");
}

__attribute__((naked)) static void sober_helper_logger_stub(void)
{
    __asm__ volatile (
        "pushfq\n\t"
        "subq $0x60, %%rsp\n\t"
        "movq %%rax, 0x00(%%rsp)\n\t"
        "movq %%rcx, 0x08(%%rsp)\n\t"
        "movq %%rdx, 0x10(%%rsp)\n\t"
        "movq %%rsi, 0x18(%%rsp)\n\t"
        "movq %%rdi, 0x20(%%rsp)\n\t"
        "movq %%r8,  0x28(%%rsp)\n\t"
        "movq %%r9,  0x30(%%rsp)\n\t"
        "movq %%r10, 0x38(%%rsp)\n\t"
        "movq %%r11, 0x40(%%rsp)\n\t"
        "stmxcsr 0x48(%%rsp)\n\t"
        "movq %%r12, 0x50(%%rsp)\n\t"      /* PRESERVE guest callee-saved r12 */
        "movq 0x20(%%rsp), %%rdi\n\t"      /* slot address */
        "movq (%%rdi), %%rsi\n\t"          /* object */
        "xorq %%rdx, %%rdx\n\t"            /* field default 0 */
        "movabsq $0x0000000100000000, %%rax\n\t"
        "cmpq %%rax, %%rsi\n\t"
        "jb 1f\n\t"
        "movabsq $0x0000800000000000, %%rax\n\t"
        "cmpq %%rax, %%rsi\n\t"
        "jae 1f\n\t"
        "movq 0x68(%%rsi), %%rdx\n\t"      /* field */
        "1:\n\t"
        "movq %%rdi, %%r12\n\t"            /* stash slot across the call */
        "movq %%rsi, %%rdi\n\t"            /* arg1 = object */
        "movq %%rdx, %%rsi\n\t"            /* arg2 = field */
        "movq 0x68(%%rsp), %%rdx\n\t"      /* arg3 = RETURN ADDRESS VALUE */
        "movq %%r12, %%rcx\n\t"            /* arg4 = slot address */
        "movabsq $sober_finner_log, %%rax\n\t"
        "call *%%rax\n\t"
        "movq %%r12, %%rdi\n\t"            /* restore slot pointer into rdi */
        "movq 0x50(%%rsp), %%r12\n\t"      /* restore guest r12 */
        "ldmxcsr 0x48(%%rsp)\n\t"
        "movq 0x40(%%rsp), %%r11\n\t"
        "movq 0x38(%%rsp), %%r10\n\t"
        "movq 0x30(%%rsp), %%r9\n\t"
        "movq 0x28(%%rsp), %%r8\n\t"
        "movq 0x20(%%rsp), %%rdi\n\t"
        "movq 0x18(%%rsp), %%rsi\n\t"
        "movq 0x10(%%rsp), %%rdx\n\t"
        "movq 0x08(%%rsp), %%rcx\n\t"
        "movq 0x00(%%rsp), %%rax\n\t"
        "addq $0x60, %%rsp\n\t"
        "popfq\n\t"
        "movq (%%rdi), %%rbx\n\t"          /* replay 48 8b 1f */
        "testq %%rbx, %%rbx\n\t"           /* replay 48 85 db */
        "jmp *%%rax\n\t"                   /* patched to G+6 */
        : : : "memory");
}

static int install_helper_logger(uintptr_t base)
{
    if (!getenv("SOBER_HELPER_LOG")) return 0;
    uint8_t *site = (uint8_t *)(base + 0x23331aeu);
    static const uint8_t orig[5] = { 0x48,0x8b,0x1f,0x48,0x85 };
    if (memcmp(site, orig, sizeof(orig)) != 0)
        return set_err("helper logger: prologue mismatch at 0x23331ae");
    uint8_t *src = (uint8_t *)&sober_helper_logger_stub;
    uint8_t *jmp = NULL;
    for (uint8_t *p = src; p < src + 256 - 2; ++p)
        if (p[0] == 0xff && p[1] == 0xe0) jmp = p;
    if (!jmp) return set_err("helper logger: stub jump not found");
    size_t len = (size_t)(jmp - src) + 2;
    long ps = page_size();
    uintptr_t near = (uintptr_t)site & ~((uintptr_t)ps - 1);
    uint8_t *stub = NULL;
    for (long k = -100000; k <= 100000 && !stub; ++k) {
        void *cand = (void *)(near + (uintptr_t)k * (uintptr_t)ps);
        void *m = mmap(cand, (size_t)ps, PROT_READ|PROT_WRITE|PROT_EXEC,
                       MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE, -1, 0);
        if (m != MAP_FAILED) stub = (uint8_t *)m;
    }
    if (!stub) return set_err("helper logger: no near page");
    memcpy(stub, src, len);
    uint8_t *cj = stub + (jmp - src);
    long d1 = (long)((uintptr_t)(base + 0x23331b4u) - (uintptr_t)(cj + 5));
    if (d1 < INT32_MIN || d1 > INT32_MAX) return set_err("helper logger: ret range");
    cj[0] = 0xe9; uint32_t dd = (uint32_t)(int32_t)d1; memcpy(cj+1, &dd, 4);
    if (make_writable(site, 5, 1) != 0) return -1;
    long sd = (long)((uintptr_t)stub - (uintptr_t)(site + 5));
    if (sd < INT32_MIN || sd > INT32_MAX) return set_err("helper logger: stub range");
    uint32_t sd32 = (uint32_t)(int32_t)sd;
    site[0] = 0xe9; memcpy(site+1, &sd32, 4);
    fprintf(stderr, "[helper-log] installed at 0x23331ae -> %p\n", (void *)stub);
    return 0;
}

static int install_finner_logger(uintptr_t base)
{
    if (!getenv("SOBER_FINNER_LOG")) return 0;
    uint8_t *site = (uint8_t *)(base + 0x23330e7u);
    static const uint8_t orig[5] = { 0x55, 0x48, 0x89, 0xe5, 0x41 };
    if (memcmp(site, orig, sizeof(orig)) != 0)
        return set_err("finner logger: prologue mismatch at 0x23330e7");
    /* The naked stub ends with an indirect jump through RAX (clobbered by the
     * logger call); overwrite its final 2 bytes with a rel32 jump to G+6. */
    uint8_t *src = (uint8_t *)&sober_finner_logger_stub;
    uint8_t *jmp = NULL;
    for (uint8_t *p = src; p < src + 256 - 2; ++p) {
        if (p[0] == 0xff && p[1] == 0xe0) jmp = p;
    }
    if (!jmp) return set_err("finner logger: stub jump not found");
    size_t stub_len = (size_t)(jmp - src) + 2;
    long ps = page_size();
    uintptr_t near = (uintptr_t)site & ~((uintptr_t)ps - 1);
    uint8_t *stub = NULL;
    for (long k = -100000; k <= 100000 && !stub; ++k) {
        void *cand = (void *)(near + (uintptr_t)k * (uintptr_t)ps);
        void *m = mmap(cand, (size_t)ps, PROT_READ | PROT_WRITE | PROT_EXEC,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
        if (m != MAP_FAILED) stub = (uint8_t *)m;
    }
    if (!stub) return set_err("finner logger: no near page");
    memcpy(stub, src, stub_len);
    uint8_t *cjmp = stub + (jmp - src);
    long disp = (long)((uintptr_t)(base + 0x23330edu) - (uintptr_t)(cjmp + 5));
    if (disp < INT32_MIN || disp > INT32_MAX)
        return set_err("finner logger: return displacement out of range");
    cjmp[0] = 0xe9;
    uint32_t d = (uint32_t)(int32_t)disp;
    memcpy(cjmp + 1, &d, 4);
    if (make_writable(site, 5, 1) != 0) return -1;
    long sd = (long)((uintptr_t)stub - (uintptr_t)(site + 5));
    if (sd < INT32_MIN || sd > INT32_MAX)
        return set_err("finner logger: stub displacement out of range");
    uint32_t sd32 = (uint32_t)(int32_t)sd;
    site[0] = 0xe9;
    memcpy(site + 1, &sd32, 4);
    fprintf(stderr, "[finner-log] installed at 0x23330e7 -> near stub %p (len=%zu)\n",
            (void *)stub, stub_len);
    return 0;
}

static int install_finner_breakpoint(uintptr_t base)
{
    if (!getenv("SOBER_FINNER_BP")) return 0;
    uint8_t *site = (uint8_t *)(base + 0x23330e7u);
    if (*site != 0x55)
        return set_err("F_inner breakpoint: prologue mismatch at 0x23330e7");
    if (make_writable(site, 1, 1) != 0) return -1;
    *site = 0xcc;
    fprintf(stderr, "[finner-bp] int3 installed at 0x23330e7\n");
    return 0;
}

static int install_curl_holder_fix(uintptr_t base)
{
    if (!getenv("SOBER_CURL_HOLDER_FIX")) return 0;
    uint8_t *site = (uint8_t *)(base + 0x245771eu);
    static const uint8_t orig[3] = { 0x48, 0x89, 0xdf };
    if (memcmp(site, orig, sizeof(orig)) != 0)
        return set_err("curl-holder fix: prologue mismatch at 0x245771e");
    if (make_writable(site, sizeof(orig), 1) != 0) return -1;
    site[0] = 0x31;             /* xor edi, edi */
    site[1] = 0xff;
    site[2] = 0x90;             /* nop */
    long ps = page_size();
    uintptr_t start = (uintptr_t)site & ~((uintptr_t)ps - 1);
    uintptr_t end = ((uintptr_t)site + sizeof(orig) + ps - 1) & ~((uintptr_t)ps - 1);
    if (mprotect((void *)start, end - start, PROT_READ | PROT_EXEC) != 0)
        return set_err("curl-holder fix: restore RX failed: %s", strerror(errno));
    fprintf(stderr, "[curl-holder-fix] patched 0x245771e: mov %%rbx,%%rdi -> xor %%edi,%%edi; nop\n");

    /* Broader variant: zero the stale holder at its READ site.  Any caller
     * through wrapper 0x2457702 then receives RSI == 0. */
    uint8_t *rd = (uint8_t *)(base + 0x234ddc0u);
    static const uint8_t rdorig[7] = { 0x48,0x8b,0xb3,0x60,0x04,0x00,0x00 };
    if (memcmp(rd, rdorig, sizeof(rdorig)) != 0)
        return set_err("curl-holder fix: read-site prologue mismatch at 0x234ddc0");
    if (make_writable(rd, sizeof(rdorig), 1) != 0) return -1;
    rd[0] = 0x31; rd[1] = 0xf6;                       /* xor esi, esi */
    for (unsigned i = 2; i < sizeof(rdorig); ++i) rd[i] = 0x90;
    start = (uintptr_t)rd & ~((uintptr_t)ps - 1);
    end = ((uintptr_t)rd + sizeof(rdorig) + ps - 1) & ~((uintptr_t)ps - 1);
    if (mprotect((void *)start, end - start, PROT_READ | PROT_EXEC) != 0)
        return set_err("curl-holder fix: read-site restore RX failed: %s", strerror(errno));
    fprintf(stderr, "[curl-holder-fix] patched 0x234ddc0: mov 0x460(%%rbx),%%rsi -> xor %%esi,%%esi; nops\n");
    return 0;
}

static int install_caller_ptr_fix(uintptr_t base)
{
    if (!getenv("SOBER_CALLER_PTR_FIX")) return 0;
    fprintf(stderr, "[caller-fix] install start base=%p\n", (void *)base);
    if (!&sober_jni_safe_app_shell_handle) return set_err("caller fix: JNI bridge not linked");
    uint8_t *site = (uint8_t *)(base + SOBER_CALLER_PATCH_RVA);
    static const uint8_t orig[5] = { 0x49,0x89,0xc6,0x48,0x89 };
    if (memcmp(site, orig, 5) != 0) {
        fprintf(stderr, "[caller-fix] prologue got %02x %02x %02x %02x %02x %02x %02x %02x\n",
                site[0],site[1],site[2],site[3],site[4],site[5],site[6],site[7]);
        return set_err("caller patch: prologue mismatch at %#x", SOBER_CALLER_PATCH_RVA);
    }
    long ps = page_size();
    uintptr_t near = (uintptr_t)site & ~((uintptr_t)ps - 1);
    void *stub = NULL;
    /* MAP_FIXED_NOREPLACE finds a free page within +/-400 MB of the guest so
     * a rel32 jmp from the patch site reaches it. */
    for (long k = -100000; k <= 100000 && !stub; ++k) {
        void *cand = (void *)(near + (uintptr_t)k * (uintptr_t)ps);
        void *m = mmap(cand, (size_t)ps, PROT_READ|PROT_WRITE|PROT_EXEC,
                       MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE, -1, 0);
        if (m != MAP_FAILED) stub = m;
    }
    if (!stub) {
        fprintf(stderr, "[caller-fix] no near page (errno=%d %s)\n", errno, strerror(errno));
        return set_err("caller patch: no near page available");
    }
    fprintf(stderr, "[caller-fix] stub=%p\n", stub);

    uint8_t *p = (uint8_t *)stub;
    #define EMIT1(b) (*p++ = (uint8_t)(b))
    EMIT1(0x50); EMIT1(0x51); EMIT1(0x52); EMIT1(0x56); EMIT1(0x57);
    EMIT1(0x41); EMIT1(0x50); EMIT1(0x41); EMIT1(0x51);
    EMIT1(0x41); EMIT1(0x52); EMIT1(0x41); EMIT1(0x53);
    EMIT1(0x53); EMIT1(0x55);
    EMIT1(0x41); EMIT1(0x54); EMIT1(0x41); EMIT1(0x55);
    EMIT1(0x41); EMIT1(0x56); EMIT1(0x41); EMIT1(0x57);
    /* mov %rax,%rdi */
    EMIT1(0x48); EMIT1(0x89); EMIT1(0xc7);
    /* movabs $helper,%r11 ; call *%r11 */
    EMIT1(0x49); EMIT1(0xbb);
    uint64_t helper = (uint64_t)(uintptr_t)&sr_valid_caller_handle;
    for (int i = 0; i < 8; ++i) EMIT1(helper >> (8*i));
    EMIT1(0x41); EMIT1(0xff); EMIT1(0xd3);
    /* restore regs (RAX kept as helper result) */
    EMIT1(0x41); EMIT1(0x5f); EMIT1(0x41); EMIT1(0x5e);
    EMIT1(0x41); EMIT1(0x5d); EMIT1(0x41); EMIT1(0x5c);
    EMIT1(0x5d); EMIT1(0x5b);
    EMIT1(0x41); EMIT1(0x5b); EMIT1(0x41); EMIT1(0x5a);
    EMIT1(0x41); EMIT1(0x59); EMIT1(0x41); EMIT1(0x58);
    EMIT1(0x5f); EMIT1(0x5e); EMIT1(0x5a); EMIT1(0x59);
    EMIT1(0x48); EMIT1(0x83); EMIT1(0xc4); EMIT1(0x08);  /* add $8,%rsp */
    /* displaced: mov %rax,%r14 ; mov %rax,(%rbx) */
    EMIT1(0x49); EMIT1(0x89); EMIT1(0xc6);
    EMIT1(0x48); EMIT1(0x89); EMIT1(0x03);
    /* movabs $return,%r11 ; jmp *%r11 */
    EMIT1(0x49); EMIT1(0xbb);
    uint64_t ret = (uint64_t)(base + SOBER_CALLER_RETURN_RVA);
    for (int i = 0; i < 8; ++i) EMIT1(ret >> (8*i));
    EMIT1(0x41); EMIT1(0xff); EMIT1(0xe3);
    #undef EMIT1
    __builtin___clear_cache((char *)stub, (char *)p);

    if (make_writable(site, 5, 1) != 0) {
        fprintf(stderr, "[caller-fix] make_writable failed\n");
        return -1;
    }
    int32_t rel = (int32_t)((uintptr_t)stub - ((uintptr_t)site + 5));
    site[0] = 0xe9;
    memcpy(site + 1, &rel, 4);
    __builtin___clear_cache((char *)site, (char *)site + 5);
    fprintf(stderr, "[caller-fix] patched %#x -> stub %p (rel=%d)\n",
            SOBER_CALLER_PATCH_RVA, stub, (int)rel);
    atexit(sr_caller_fix_report);
    return 0;
}

/* n9 WAVE1 KILL SWITCH (host-only, ledgered): the 8 bridge patches are all
 * allocator entry points, never the page source; they do not make 1dbe4fc see
 * a registered pointer.  Default OFF; opt in only with SOBER_ALLOCATOR_PATCH=1.
 * 1dbe4fc is NEVER bridged. */
static int bb_alloc_patch_enabled(void)
{
    const char *e = getenv("SOBER_ALLOCATOR_PATCH");
    return (e && e[0] == '1' && e[1] == '\0') ? 1 : 0;
}

static int do_apply(uintptr_t base)
{
    if (bootstrap_debug()) {
        void *mm = *(void **)(base + 0x7229848u); /* guest GOT -> stub memmove */
        uintptr_t sb = (uintptr_t)mm - 0x8d9du;   /* stub libc load bias */
        BDEBUG("stub libc base=%p fn_memmove=%p fn_memcmp=%p",
               (void *)sb, *(void **)(sb + 0xeca0u), *(void **)(sb + 0xefc8u));
    }
    sober_bootstrap_phase = 1;
    if (sober_bionic_bootstrap_verify_build((void *)base) != 0) return -1;
    {   /* default allocator contract diagnostics (one-shot) */
        static int bb_report_registered;
        if (!bb_report_registered) {
            atexit(bb_alloc_contract_report);
            bb_report_registered = 1;
        }
    }
    /* Phase-4 decision 019: install the writer-side handle sanitiser. */
    install_handle_sanitiser(base);
    /* Install bridges first: the data-seed initializers may allocate. */
    sober_bootstrap_phase = 2;
    BDEBUG("installing 6 bridges");
    if (!getenv("SOBER_BOOTSTRAP_NO_BRIDGES") && bb_alloc_patch_enabled()) {
        if (install_bridges(base) != 0) return set_err("failed to install mimalloc bridges");
        if (patch_calloc_slot(base) != 0) return set_err("failed to patch calloc slot 0x726c0c0");
    } else {
        fprintf(stderr, "[N9_ALLOC_PATCH] OFF (default); guest allocator "
                        "unpatched; 1dbe4fc NOT bridged\n");
        BDEBUG("BRIDGES DISABLED (default OFF or env)");
    }
    sober_bootstrap_phase = 3;
    BDEBUG("seeding 4 slots");
    if (bootstrap_debug()) {
        /* Diagnostic: guest PLT GOT slots must have been resolved by dlopen. */
        static const uint32_t gots[] = {0x7229400u, 0x7229408u, 0x7229410u,
                                        0x7229430u, 0x7229438u, 0x7229440u,
                                        0x7229840u, 0x7229848u, 0x7229850u};
        for (unsigned i = 0; i < 9; ++i)
            BDEBUG("GOT[%#x]=%p", gots[i], *(void **)(base + gots[i]));
        /* Flag every unresolved (NULL) PLT GOT slot in .got.plt. */
        for (uint32_t off = 0x7229418u; off < 0x7229a00u; off += 8) {
            void *v = *(void **)(base + off);
            if (!v) BDEBUG("UNRESOLVED PLT GOT %#x", off);
        }
    }
    if (!getenv("SOBER_BOOTSTRAP_NO_SEEDS")) {
        if (seed_slots(base) != 0) return -1;
    } else BDEBUG("SEEDS DISABLED by env");
    /* Belt-and-braces (lead decision 009): force sane guest page size. */
    *(uintptr_t *)(base + 0x7281fe0u) = 0x1000u;
    *(uintptr_t *)(base + 0x7281fe8u) = 0x1000u;
    if (install_caller_ptr_fix(base) != 0) return -1;
    if (install_curl_holder_fix(base) != 0) return -1;
    if (install_finner_breakpoint(base) != 0) return -1;
    if (install_finner_logger(base) != 0) return -1;
    if (install_helper_logger(base) != 0) return -1;
    sober_bootstrap_phase = 100;
    BDEBUG("apply complete");
    return SOBER_BIONIC_OK;
}

SOBER_BOOTSTRAP_EXPORT int sober_bionic_bootstrap_apply(void *guest_base)
{
    if (atomic_load_explicit(&g_applied, memory_order_acquire)) return SOBER_BIONIC_OK;

    uintptr_t base = (uintptr_t)guest_base;
    if (!base) base = (uintptr_t)sober_bionic_bootstrap_find_guest();
    if (!base) return SOBER_BIONIC_ERR_NO_GUEST;

    int expected = 0;
    if (!atomic_compare_exchange_strong_explicit(&g_applied, &expected, 1,
                                                 memory_order_acq_rel,
                                                 memory_order_acquire)) {
        /* Another thread won the race.  If it is still in progress, wait a
         * bounded number of spins for completion. */
        for (int spin = 0; spin < 1000000 && !g_guest_base; ++spin) { __asm__ __volatile__("pause"); }
        return g_guest_base ? SOBER_BIONIC_OK : SOBER_BIONIC_ERR_ALREADY_DIRTY;
    }

    if (do_apply(base) != 0) {
        atomic_store_explicit(&g_applied, 0, memory_order_release);
        return -1;
    }
    g_guest_base = base;
    return SOBER_BIONIC_OK;
}

SOBER_BOOTSTRAP_EXPORT int sober_bionic_bootstrap_apply_only(void *guest_base)
{
    return sober_bionic_bootstrap_apply(guest_base);
}

SOBER_BOOTSTRAP_EXPORT int sober_bionic_bootstrap_run(void *guest_base)
{
    int rc = sober_bionic_bootstrap_apply(guest_base);
    if (rc != 0) return rc;
    uintptr_t base = g_guest_base ? g_guest_base : (uintptr_t)guest_base;
    if (!base) base = (uintptr_t)sober_bionic_bootstrap_find_guest();
    if (!base) return SOBER_BIONIC_ERR_NO_GUEST;
    return sober_bionic_bootstrap_replay_constructors((void *)base);
}

SOBER_BOOTSTRAP_EXPORT int sober_bionic_bootstrap_is_applied(void)
{
    return atomic_load_explicit(&g_applied, memory_order_acquire) != 0;
}

/*
 * Helper for MODE A integration: call this from a constructor in one of the
 * guest's NDK stub DSOs (e.g. liblog.so / libandroid.so).  Stub constructors
 * run before the guest's init_array, so applying here puts the seeds/bridges
 * in place before the guest constructors execute.  Exported so worker-3 can
 * call it without pulling in the whole header.
 */
SOBER_BOOTSTRAP_EXPORT void sober_bionic_bootstrap_from_stub_constructor(void)
{
    /* Opt-in only.  worker-3's harness calls the symbols explicitly (weak
     * refs / dlsym), so this auto-path MUST stay inert there or it would
     * double-apply.  Set SOBER_BOOTSTRAP_INTERPOSE=1 to enable. */
    const char *opt = getenv("SOBER_BOOTSTRAP_INTERPOSE");
    if (!opt || strcmp(opt, "1") != 0) return;
    (void)sober_bionic_bootstrap_apply(NULL);
}
