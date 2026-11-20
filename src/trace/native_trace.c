/*
 * native_trace.c - opt-in LD_PRELOAD malloc/calloc/realloc/free tracer.
 *
 * Worker-2, Phase 4b task #394.  Owned by src/trace/.
 *
 * Design goals:
 *  - Transparent when SOBER_TRACE is unset (all hooks forward immediately).
 *  - No allocation inside the hooks: backtraces are sent straight to a fd
 *    using hand-rolled hex formatting and write(2); the bookkeeping table is
 *    static, so the interposer cannot recurse into itself.
 *  - Keep the allocation-site backtrace for every live block.  At free() we
 *    scan the block for the guest's 0xc0dedbad magic (or any configured value)
 *    and, on a hit, print BOTH the allocation and free stacks.  That directly
 *    identifies the allocation that produced the bad guest handle.
 */
#define _GNU_SOURCE
#include "native_trace.h"

#include <dlfcn.h>
#include <errno.h>
#include <execinfo.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <signal.h>
#include <ucontext.h>

#define SR_TABLE_SIZE 16384u
#define SR_MAX_FRAMES 24
#define SR_MAGIC_DEFAULT 0xc0dedbadu

typedef struct {
    volatile uintptr_t ptr;      /* 0 = empty slot */
    uintptr_t          size;
    uint64_t           seq;
    int                nframes;
    uintptr_t          frames[SR_MAX_FRAMES];
} SrEntry;

static SrEntry g_table[SR_TABLE_SIZE];
static volatile int g_table_lock;
static volatile int g_initialising;
static __thread int g_in_hook;

static void *(*real_malloc)(size_t);
static void *(*real_calloc)(size_t, size_t);
static void *(*real_realloc)(void *, size_t);
static void  (*real_free)(void *);
static void *(*real_memalign)(size_t, size_t);
static void *(*real_mmap)(void *, size_t, int, int, int, off_t);
static int   (*real_munmap)(void *, size_t);
static int   (*real_mprotect)(void *, size_t, int);
static int   (*real_prctl)(int, unsigned long, unsigned long, unsigned long, unsigned long);
static void *(*real_dlopen)(const char *, int);
static int   (*real_memfd_create)(const char *, unsigned int);

static SoberTraceConfig g_cfg = {
    .enabled = 0, .log_all = 0, .fd = 2, .depth = 12,
    .min_size = 0, .max_size = 0, .watch_addr = 0,
    .magic = SR_MAGIC_DEFAULT, .scan_bytes = 512, .dump_summary = 0
};
static uint64_t g_seq;
static uint64_t g_last_watch;
static int      g_watch_valid;
/* one-shot auto write watch */
static int      g_watch_auto;
static int      g_watch_auto_armed;
static uintptr_t g_watch_auto_addr;
static void    *g_watch_auto_page;
static size_t   g_watch_auto_page_len;
static int      g_watch_auto_oldprot;
static uintptr_t g_guest_base;
static int      g_armed;
static const char *g_start_file;

/* ---- tiny, allocation-free output ------------------------------------- */
static void sr_write(const char *s, size_t n)
{
    if (!g_cfg.enabled) return;
    size_t off = 0;
    while (off < n) {
        ssize_t w = write(g_cfg.fd, s + off, n - off);
        if (w <= 0) { if (errno == EINTR) continue; break; }
        off += (size_t)w;
    }
}
static void sr_puts(const char *s) { sr_write(s, strlen(s)); }
static void sr_hex(uintptr_t v, int min_digits)
{
    char buf[32];
    int i = (int)sizeof(buf);
    do {
        unsigned d = (unsigned)(v & 0xfu);
        buf[--i] = d < 10 ? (char)('0' + d) : (char)('a' + d - 10);
        v >>= 4;
    } while (v || (sizeof(buf) - i) < (size_t)min_digits);
    sr_write(buf + i, sizeof(buf) - i);
}
static void sr_dec(uint64_t v)
{
    char buf[32];
    int i = (int)sizeof(buf);
    do { buf[--i] = (char)('0' + (v % 10)); v /= 10; } while (v);
    sr_write(buf + i, sizeof(buf) - i);
}

static void sr_frames(uintptr_t *frames, int n)
{
    for (int i = 0; i < n; ++i) {
        sr_puts(" "); sr_hex(frames[i], 8);
    }
    sr_puts("\n");
}

/* ---- bookkeeping ------------------------------------------------------- */
static void sr_lock(void)   { while (__sync_lock_test_and_set(&g_table_lock, 1)) { } }
static void sr_unlock(void) { __sync_lock_release(&g_table_lock); }

static unsigned sr_hash(uintptr_t p) { return (unsigned)((p >> 4) & (SR_TABLE_SIZE - 1u)); }

static SrEntry *sr_find(uintptr_t p)
{
    unsigned h = sr_hash(p);
    for (unsigned i = 0; i < SR_TABLE_SIZE; ++i) {
        SrEntry *e = &g_table[(h + i) & (SR_TABLE_SIZE - 1u)];
        if (e->ptr == p) return e;
        if (e->ptr == 0) return NULL;
    }
    return NULL;
}

static SrEntry *sr_insert(uintptr_t p, size_t size, uintptr_t *frames, int nf)
{
    unsigned h = sr_hash(p);
    for (unsigned i = 0; i < SR_TABLE_SIZE; ++i) {
        SrEntry *e = &g_table[(h + i) & (SR_TABLE_SIZE - 1u)];
        if (e->ptr == 0 || e->ptr == p) {
            e->ptr = p;
            e->size = size;
            e->seq = __sync_add_and_fetch(&g_seq, 1);
            e->nframes = nf;
            memcpy(e->frames, frames, sizeof(uintptr_t) * (size_t)nf);
            return e;
        }
    }
    return NULL;
}

static int sr_capture(uintptr_t *out, int skip)
{
    void *tmp[SR_MAX_FRAMES];
    int n = backtrace(tmp, g_cfg.depth);
    if (n <= 0) return 0;
    if (n > SR_MAX_FRAMES) n = SR_MAX_FRAMES;
    int m = 0;
    for (int i = skip; i < n; ++i) out[m++] = (uintptr_t)tmp[i];
    return m;
}

static int sr_interesting_size(size_t n)
{
    if (n < g_cfg.min_size) return 0;
    if (g_cfg.max_size && n > g_cfg.max_size) return 0;
    return 1;
}

static int sr_ready(void)
{
    if (!g_cfg.enabled) return 0;
    if (g_start_file && !g_armed) {
        /* Check every entry while disarmed; once armed this is skipped. */
        if (access(g_start_file, F_OK) == 0) {
            g_armed = 1;
            sr_puts("[trace] ARMED (start file present)\n");
        }
        return 0;
    }
    return 1;
}

/* Periodically scan live blocks for the magic.  This catches a guest handle
 * as soon as its creator writes 0xc0dedbad into a still-live allocation,
 * while we still have the allocation-site backtrace. */
static void sr_scan_live(void)
{
    static uint64_t counter;
    if (!sr_ready()) return;
    if ((++counter & 63u) != 0) return;
    sr_lock();
    for (unsigned i = 0; i < SR_TABLE_SIZE; ++i) {
        SrEntry *e = &g_table[i];
        if (!e->ptr || e->size > g_cfg.scan_bytes || e->size < 4) continue;
        if (e->seq > g_seq) continue;    /* paranoia */
        const unsigned char *p = (const unsigned char *)e->ptr;
        size_t n = e->size < 64 ? e->size : 64;
        uint32_t v = 0;
        int found = 0;
        for (size_t j = 0; j + 4 <= n; j += 4) {
            __builtin_memcpy(&v, p + j, 4);
            if (v == g_cfg.magic) { found = 1; break; }
        }
        if (found) {
            uintptr_t alloc_ptr = e->ptr;
            size_t alloc_size = e->size;
            uint64_t seq = e->seq;
            int an = e->nframes;
            uintptr_t afr[SR_MAX_FRAMES];
            __builtin_memcpy(afr, e->frames, sizeof(uintptr_t) * (size_t)an);
            e->ptr = 0;   /* report once */
            sr_unlock();
            sr_puts("[trace] MAGIC-LIVE magic=0x"); sr_hex(g_cfg.magic, 8);
            sr_puts(" ptr=0x"); sr_hex(alloc_ptr, 8);
            sr_puts(" size="); sr_dec(alloc_size);
            sr_puts(" alloc#"); sr_dec(seq);
            sr_puts("\n[trace]   alloc stack:"); sr_frames(afr, an);
            uintptr_t fr[SR_MAX_FRAMES];
            int nf = sr_capture(fr, 2);
            sr_puts("[trace]   now   stack:"); sr_frames(fr, nf);
            return;
        }
    }
    sr_unlock();
}

static void sr_check_watch(const char *tag)
{
    if (!g_cfg.enabled || !g_cfg.watch_addr) return;
    uintptr_t v = *(volatile uintptr_t *)g_cfg.watch_addr;
    if (!g_watch_valid) { g_last_watch = v; g_watch_valid = 1; return; }
    if (v == g_last_watch) return;
    uintptr_t fr[SR_MAX_FRAMES];
    int nf = sr_capture(fr, 2);
    sr_puts("[trace] WATCH "); sr_puts(tag); sr_puts(" addr=0x");
    sr_hex(g_cfg.watch_addr, 8);
    sr_puts(" old=0x"); sr_hex(g_last_watch, 8);
    sr_puts(" new=0x"); sr_hex(v, 8);
    sr_puts(" stack:");
    sr_frames(fr, nf);
    g_last_watch = v;
}

/* Scan a freed block for the magic; on a hit print allocation + free stacks. */
static void sr_report_magic(SrEntry *e, uintptr_t freed_ptr)
{
    if (!g_cfg.enabled || !g_cfg.scan_bytes) return;
    size_t n = e->size < g_cfg.scan_bytes ? e->size : g_cfg.scan_bytes;
    const unsigned char *p = (const unsigned char *)freed_ptr;
    if (!p) return;
    int hit = 0;
    size_t hit_off = 0;
    for (size_t i = 0; i + 4 <= n; i += 4) {
        uint32_t v;
        memcpy(&v, p + i, sizeof(v));
        if (v == g_cfg.magic) { hit = 1; hit_off = i; break; }
    }
    if (!hit) return;
    sr_puts("[trace] MAGIC-FREE magic=0x"); sr_hex(g_cfg.magic, 8);
    sr_puts(" ptr=0x"); sr_hex(freed_ptr, 8);
    sr_puts(" size="); sr_dec(e->size);
    sr_puts(" off="); sr_dec(hit_off);
    sr_puts(" alloc#"); sr_dec(e->seq);
    sr_puts("\n[trace]   alloc stack:");
    sr_frames(e->frames, e->nframes);
    uintptr_t fr[SR_MAX_FRAMES];
    int nf = sr_capture(fr, 2);
    sr_puts("[trace]   free  stack:");
    sr_frames(fr, nf);
}

/* ---- interposed entry points ------------------------------------------- */
static void *sr_resolve(const char *name, const char *libc_name)
{
    void *p = dlsym(RTLD_NEXT, name);
    if (!p && libc_name) p = dlsym(RTLD_DEFAULT, libc_name);
    return p;
}

static uintptr_t sr_find_guest_base(void)
{
    const char *env = getenv("SOBER_TRACE_GUEST_BASE");
    if (env && *env) return (uintptr_t)strtoull(env, NULL, 0);
    FILE *f = fopen("/proc/self/maps", "r");
    if (!f) return 0;
    char line[1024];
    uintptr_t best = 0;
    unsigned long long bestsz = 0;
    while (fgets(line, sizeof(line), f)) {
        unsigned long long lo, hi;
        char perms[8] = {0};
        if (sscanf(line, "%llx-%llx %7s", &lo, &hi, perms) != 3) continue;
        if (perms[0] == 'r' && perms[2] == 'x' && (hi - lo) > bestsz &&
            (hi - lo) > (100ULL << 20)) {
            bestsz = hi - lo; best = (uintptr_t)lo;
        }
    }
    fclose(f);
    return best;
}

static void sr_log_rbp_chain(uint64_t rbp)
{
    for (int i = 0; i < 12 && rbp > 0x10000; ++i) {
        uint64_t ret = *(uint64_t *)(uintptr_t)(rbp + 8);
        uint64_t next = *(uint64_t *)(uintptr_t)rbp;
        if (!ret) break;
        sr_puts(" "); sr_hex((uintptr_t)ret, 8);
        if (g_guest_base && ret >= g_guest_base && ret - g_guest_base < (400u << 20)) {
            sr_puts("(guest+0x"); sr_hex(ret - g_guest_base, 6); sr_puts(")");
        }
        if (next <= rbp) break;
        rbp = next;
    }
    sr_puts("\n");
}

static void sr_segv_handler(int sig, siginfo_t *info, void *ucontext)
{
    (void)sig;
    if (!g_watch_auto_armed || !info) return;   /* let the harness handle it */
    ucontext_t *uc = (ucontext_t *)ucontext;
    uintptr_t rip = (uintptr_t)uc->uc_mcontext.gregs[REG_RIP];
    uint64_t rbp = (uint64_t)uc->uc_mcontext.gregs[REG_RBP];

    /* One-shot: restore writable access so the faulting store can complete. */
    if (g_watch_auto_page) mprotect(g_watch_auto_page, g_watch_auto_page_len,
                                    g_watch_auto_oldprot);
    g_watch_auto_armed = 0;

    if ((uintptr_t)info->si_addr != g_watch_auto_addr) return;  /* unrelated fault */
    sr_puts("[trace] WATCH-AUTO WRITE addr=0x"); sr_hex(g_watch_auto_addr, 8);
    sr_puts(" rip=0x"); sr_hex(rip, 8);
    if (g_guest_base && rip >= g_guest_base && rip - g_guest_base < (400u << 20)) {
        sr_puts(" guest_rva=0x"); sr_hex(rip - g_guest_base, 6);
    }
    sr_puts(" rbp=0x"); sr_hex((uintptr_t)rbp, 8);
    sr_puts(" stack:");
    sr_log_rbp_chain(rbp);
}

static void sr_arm_auto_watch(void *ptr)
{
    if (!g_watch_auto || g_watch_auto_armed) return;
    long ps = sysconf(_SC_PAGESIZE);
    if (ps <= 0) ps = 4096;
    g_watch_auto_addr = (uintptr_t)ptr + 0x68u;
    uintptr_t page = g_watch_auto_addr & ~((uintptr_t)ps - 1);
    g_watch_auto_page = (void *)page;
    g_watch_auto_page_len = (size_t)ps;
    g_watch_auto_oldprot = PROT_READ | PROT_WRITE;
    if (mprotect(g_watch_auto_page, g_watch_auto_page_len, PROT_READ) != 0) {
        g_watch_auto_page = NULL;
        return;
    }
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = sr_segv_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, NULL);
    g_watch_auto_armed = 1;
    sr_puts("[trace] WATCH-AUTO armed on 0x"); sr_hex(g_watch_auto_addr, 8);
    sr_puts(" (page 0x"); sr_hex(page, 8); sr_puts(")\n");
}

__attribute__((constructor)) static void sr_init(void)
{
    g_initialising = 1;
    const char *v;
    v = getenv("SOBER_TRACE");
    g_cfg.enabled = (v && strcmp(v, "1") == 0);
    v = getenv("SOBER_TRACE_LOG");       g_cfg.log_all = (v && strcmp(v, "1") == 0);
    v = getenv("SOBER_TRACE_FD");        if (v) g_cfg.fd = atoi(v);
    v = getenv("SOBER_TRACE_DEPTH");     if (v) { int d = atoi(v); if (d >= 4 && d <= SR_MAX_FRAMES) g_cfg.depth = d; }
    v = getenv("SOBER_TRACE_MIN");       if (v) g_cfg.min_size = (size_t)strtoull(v, NULL, 0);
    v = getenv("SOBER_TRACE_MAX");       if (v) g_cfg.max_size = (size_t)strtoull(v, NULL, 0);
    v = getenv("SOBER_TRACE_WATCH");     if (v && *v) g_cfg.watch_addr = (uintptr_t)strtoull(v, NULL, 0);
    v = getenv("SOBER_TRACE_MAGIC");     if (v) g_cfg.magic = (uint32_t)strtoull(v, NULL, 0);
    v = getenv("SOBER_TRACE_SCANBYTES"); if (v) { size_t s = (size_t)strtoull(v, NULL, 0); if (s <= 4096) g_cfg.scan_bytes = s; }
    v = getenv("SOBER_TRACE_DUMP");      g_cfg.dump_summary = (v && strcmp(v, "1") == 0);
    v = getenv("SOBER_TRACE_WATCH_AUTO"); if (v && strcmp(v, "1") == 0) g_watch_auto = 1;
    g_guest_base = sr_find_guest_base();
    v = getenv("SOBER_TRACE_START_FILE"); if (v && *v) { g_start_file = v; g_cfg.start_file = v; g_armed = 0; }
    else g_armed = 1;

    real_malloc  = sr_resolve("malloc", "__libc_malloc");
    real_calloc  = sr_resolve("calloc", "__libc_calloc");
    real_realloc = sr_resolve("realloc", "__libc_realloc");
    real_free    = sr_resolve("free", "__libc_free");
    real_memalign= sr_resolve("memalign", "__libc_memalign");
    real_mmap    = sr_resolve("mmap", "__mmap");
    real_munmap  = sr_resolve("munmap", "__munmap");
    real_mprotect= sr_resolve("mprotect", "__mprotect");
    real_prctl   = sr_resolve("prctl", "prctl");
    real_dlopen  = sr_resolve("dlopen", "__libc_dlopen_mode");
    real_memfd_create = sr_resolve("memfd_create", "memfd_create");
    g_initialising = 0;
    if (g_cfg.enabled) {
        sr_puts("[trace] native allocation tracer enabled fd=");
        sr_dec((uint64_t)g_cfg.fd);
        sr_puts(" magic=0x"); sr_hex(g_cfg.magic, 8); sr_puts("\n");
    }
}

__attribute__((destructor)) static void sr_fini(void)
{
    if (g_cfg.dump_summary) sober_trace_dump();
}

/* Nested allocations (e.g. from the unwinder) still go to the real
 * allocator; they are simply not logged, so backtrace() can work. */
#define SR_GUARD if (g_initialising || !real_malloc) return NULL; \
    int sr_nested = g_in_hook; g_in_hook = 1;
#define SR_EXIT do { g_in_hook = sr_nested; } while (0)

void *malloc(size_t n)
{
    SR_GUARD;
    sr_scan_live();
    void *p = real_malloc(n);
    if (p && !sr_nested && sr_ready() && sr_interesting_size(n)) {
        uintptr_t fr[SR_MAX_FRAMES];
        int nf = sr_capture(fr, 2);
        sr_lock();
        sr_insert((uintptr_t)p, n, fr, nf);
        sr_unlock();
        if (g_cfg.log_all) {
            sr_puts("[trace] malloc size="); sr_dec(n);
            sr_puts(" ptr=0x"); sr_hex((uintptr_t)p, 8);
            sr_puts(" stack:"); sr_frames(fr, nf);
        }
        sr_check_watch("malloc");
    }
    SR_EXIT;
    return p;
}

void *calloc(size_t a, size_t b)
{
    SR_GUARD;
    sr_scan_live();
    void *p = real_calloc(a, b);
    size_t n = a * b;
    if (p && !sr_nested && sr_ready() && sr_interesting_size(n)) {
        uintptr_t fr[SR_MAX_FRAMES];
        int nf = sr_capture(fr, 2);
        sr_lock(); sr_insert((uintptr_t)p, n, fr, nf); sr_unlock();
        if (g_cfg.log_all) {
            sr_puts("[trace] calloc size="); sr_dec(n);
            sr_puts(" ptr=0x"); sr_hex((uintptr_t)p, 8);
            sr_puts(" stack:"); sr_frames(fr, nf);
        }
        sr_check_watch("calloc");
        if (n == 0x1100u) sr_arm_auto_watch(p);
    }
    SR_EXIT;
    return p;
}

void *realloc(void *old, size_t n)
{
    SR_GUARD;
    sr_scan_live();
    SrEntry *e = NULL;
    if (!sr_nested && sr_ready()) { sr_lock(); e = sr_find((uintptr_t)old); sr_unlock(); }
    void *p = real_realloc(old, n);
    if (p && !sr_nested && sr_ready() && sr_interesting_size(n)) {
        uintptr_t fr[SR_MAX_FRAMES];
        int nf = sr_capture(fr, 2);
        sr_lock();
        if (e) { e->ptr = 0; e = NULL; }
        sr_insert((uintptr_t)p, n, fr, nf);
        sr_unlock();
        if (g_cfg.log_all) {
            sr_puts("[trace] realloc old=0x"); sr_hex((uintptr_t)old, 8);
            sr_puts(" new=0x"); sr_hex((uintptr_t)p, 8);
            sr_puts(" size="); sr_dec(n);
            sr_puts(" stack:"); sr_frames(fr, nf);
        }
        sr_check_watch("realloc");
    }
    SR_EXIT;
    return p;
}

void free(void *p)
{
    if (g_initialising || !real_free) return;
    int sr_nested = g_in_hook; g_in_hook = 1;
    if (!sr_nested) sr_scan_live();
    if (!sr_nested && sr_ready() && p) {
        SrEntry *e;
        sr_lock();
        e = sr_find((uintptr_t)p);
        if (e) {
            SrEntry copy = *e;   /* copy out so we can log without the lock */
            e->ptr = 0;
            sr_unlock();
            sr_report_magic(&copy, (uintptr_t)p);
            if (g_cfg.log_all) {
                sr_puts("[trace] free ptr=0x"); sr_hex((uintptr_t)p, 8);
                sr_puts(" size="); sr_dec(copy.size); sr_puts("\n");
            }
        } else sr_unlock();
        sr_check_watch("free");
    }
    real_free(p);
    SR_EXIT;
}

void *memalign(size_t alignment, size_t n)
{
    SR_GUARD;
    sr_scan_live();
    void *p = real_memalign ? real_memalign(alignment, n) : NULL;
    if (p && !sr_nested && sr_ready() && sr_interesting_size(n)) {
        uintptr_t fr[SR_MAX_FRAMES];
        int nf = sr_capture(fr, 2);
        sr_lock(); sr_insert((uintptr_t)p, n, fr, nf); sr_unlock();
        if (g_cfg.log_all) {
            sr_puts("[trace] memalign size="); sr_dec(n);
            sr_puts(" ptr=0x"); sr_hex((uintptr_t)p, 8);
            sr_puts(" stack:"); sr_frames(fr, nf);
        }
    }
    SR_EXIT;
    return p;
}

void *mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off)
{
    if (g_initialising || !real_mmap) {
        return real_mmap ? real_mmap(addr, len, prot, flags, fd, off) : MAP_FAILED;
    }
    int nested = g_in_hook; g_in_hook = 1;
    void *p = real_mmap(addr, len, prot, flags, fd, off);
    if (!nested && p != MAP_FAILED && sr_ready() && (g_cfg.log_all || len >= 0x10000u)) {
        uintptr_t fr[SR_MAX_FRAMES];
        int nf = sr_capture(fr, 2);
        sr_puts("[trace] mmap len="); sr_dec(len);
        sr_puts(" prot="); sr_hex((uintptr_t)prot, 1);
        sr_puts(" ptr=0x"); sr_hex((uintptr_t)p, 8);
        sr_puts(" stack:"); sr_frames(fr, nf);
    }
    g_in_hook = nested;
    return p;
}

int munmap(void *addr, size_t len)
{
    if (g_initialising || !real_munmap) return real_munmap ? real_munmap(addr, len) : 0;
    int nested = g_in_hook; g_in_hook = 1;
    if (!nested && sr_ready() && (g_cfg.log_all || len >= 0x10000u)) {
        uintptr_t fr[SR_MAX_FRAMES];
        int nf = sr_capture(fr, 2);
        sr_puts("[trace] munmap ptr=0x"); sr_hex((uintptr_t)addr, 8);
        sr_puts(" len="); sr_dec(len);
        sr_puts(" stack:"); sr_frames(fr, nf);
    }
    int rc = real_munmap(addr, len);
    g_in_hook = nested;
    return rc;
}


void *dlopen(const char *file, int mode)
{
    if (g_initialising || !real_dlopen)
        return real_dlopen ? real_dlopen(file, mode) : NULL;
    int nested = g_in_hook; g_in_hook = 1;
    void *p = real_dlopen(file, mode);
    if (!nested && sr_ready() && file) {
        uintptr_t fr[SR_MAX_FRAMES];
        int nf = sr_capture(fr, 2);
        sr_puts("[trace] dlopen "); sr_puts(file);
        sr_puts(" -> 0x"); sr_hex((uintptr_t)p, 8);
        sr_puts(" stack:"); sr_frames(fr, nf);
    }
    g_in_hook = nested;
    return p;
}

int memfd_create(const char *name, unsigned int flags)
{
    if (g_initialising || !real_memfd_create)
        return real_memfd_create ? real_memfd_create(name, flags) : -1;
    int nested = g_in_hook; g_in_hook = 1;
    int fd = real_memfd_create(name, flags);
    if (!nested && sr_ready()) {
        uintptr_t fr[SR_MAX_FRAMES];
        int nf = sr_capture(fr, 2);
        sr_puts("[trace] memfd_create name="); sr_puts(name ? name : "(null)");
        sr_puts(" fd="); sr_dec((uint64_t)fd);
        sr_puts(" stack:"); sr_frames(fr, nf);
    }
    g_in_hook = nested;
    return fd;
}

int mprotect(void *addr, size_t len, int prot)
{
    if (g_initialising || !real_mprotect)
        return real_mprotect ? real_mprotect(addr, len, prot) : -1;
    int nested = g_in_hook; g_in_hook = 1;
    int rc = real_mprotect(addr, len, prot);
    /* mprotect is extremely frequent during guest ctor replay: log only in
     * explicit full-log mode, otherwise just forward. */
    if (!nested && g_cfg.log_all && sr_ready() && (prot & PROT_WRITE) && len >= 0x1000u) {
        uintptr_t fr[SR_MAX_FRAMES];
        int nf = sr_capture(fr, 2);
        sr_puts("[trace] mprotect RW addr=0x"); sr_hex((uintptr_t)addr, 8);
        sr_puts(" len="); sr_dec(len);
        sr_puts(" stack:"); sr_frames(fr, nf);
    }
    g_in_hook = nested;
    return rc;
}

int prctl(int option, unsigned long a2, unsigned long a3,
          unsigned long a4, unsigned long a5)
{
    if (g_initialising || !real_prctl) return real_prctl ? real_prctl(option,a2,a3,a4,a5) : -1;
    int nested = g_in_hook; g_in_hook = 1;
    int rc = real_prctl(option, a2, a3, a4, a5);
    if (!nested && sr_ready() && g_cfg.log_all) {
        uintptr_t fr[SR_MAX_FRAMES];
        int nf = sr_capture(fr, 2);
        sr_puts("[trace] prctl opt="); sr_dec((uint64_t)option);
        sr_puts(" a2=0x"); sr_hex(a2, 8);
        sr_puts(" stack:"); sr_frames(fr, nf);
    }
    g_in_hook = nested;
    return rc;
}

/* ---- exported helpers -------------------------------------------------- */
__attribute__((visibility("default")))
void sober_trace_watch_auto(void *ptr) { sr_arm_auto_watch(ptr); }

int sober_trace_config(SoberTraceConfig *out)
{
    if (!out) return 0;
    *out = g_cfg;
    return 1;
}

int sober_trace_lookup(void *ptr, uintptr_t **frames_out, int *depth_out)
{
    if (!ptr) return 0;
    sr_lock();
    SrEntry *e = sr_find((uintptr_t)ptr);
    if (!e) { sr_unlock(); return 0; }
    if (frames_out) *frames_out = e->frames;
    if (depth_out) *depth_out = e->nframes;
    sr_unlock();
    return 1;
}

void sober_trace_dump(void)
{
    if (!g_cfg.enabled) return;
    uint64_t live = 0, total = g_seq;
    sr_lock();
    for (unsigned i = 0; i < SR_TABLE_SIZE; ++i) if (g_table[i].ptr) live++;
    sr_unlock();
    sr_puts("[trace] SUMMARY allocations="); sr_dec(total);
    sr_puts(" live="); sr_dec(live); sr_puts("\n");
}
