/*
 * native_jni.c - Native JNI bridge implementation.
 *
 * Worker-2, Phase 4 task #386.  Pure native C.  The JNIEnv/JavaVM tables
 * follow the JDK's JNINativeInterface_ / JNIInvokeInterface_ layout exactly
 * (231 + 5 pointer-sized slots, four reserved slots at the front of each),
 * so the guest's own JNI headers can call through them without adaptation.
 *
 * Every slot defaults to a harmless stub returning 0/NULL; the functions the
 * guest needs for JNI_OnLoad and the app-shell callback path are implemented
 * below with their real signatures.  Native calls and callback dispatches are
 * logged and counted so the harness can gate its completion marker on real
 * guest JNI activity.
 */
#define _GNU_SOURCE
#include "native_jni.h"
#include "platform_fields.h"
#include "static_object_calls.h"
#include "scalar_returns.h"
#include "callback_provenance.h"

/* ---- Phase 8 default modules --------------------------------------------
 * All 15 delivered modules are compiled into sober_jni (CMakeLists.txt).
 * Their per-module installers are called by the harness (guest_app_shell.c)
 * after mapping/relocation, in the modules' documented order.  This file owns
 * only the JNIEnv-slot defaults (jnislots_default + fields_default), the
 * positive provenance hook/query, and the allocator-ops accessor.  Each
 * module self-gates on its own SOBER_*_DEFAULT=0 kill switch (scheduler_guest
 * is the sole opt-in module: SOBER_SCHEDULER_GUEST=1). */
#include "provenance_gate.h"
#include "jnislots_default.h"
#include "fields_default.h"
#include "alloc_default.h"

#include <dlfcn.h>
#include <link.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/uio.h>
#include <sys/syscall.h>
#include <ucontext.h>
#include <linux/perf_event.h>
#include <linux/hw_breakpoint.h>

/* ---- W24 read-only JNIEnv call ring (last 64 calls) --------------------- */
struct w24_ring_ent {
    uint32_t  slot;
    uint32_t  exc_pending;
    uintptr_t arg0, arg1;
    uintptr_t ret;
    char      name[32];
};
static struct w24_ring_ent w24_ring[64];
static volatile unsigned w24_ring_pos;
static void w24_ring_note(uint32_t slot, const char *name,
                          uintptr_t a0, uintptr_t a1, uintptr_t ret,
                          int exc)
{
    unsigned i = __atomic_fetch_add(&w24_ring_pos, 1, __ATOMIC_RELAXED) & 63u;
    struct w24_ring_ent *e = &w24_ring[i];
    e->slot = slot; e->arg0 = a0; e->arg1 = a1; e->ret = ret;
    e->exc_pending = (uint32_t)(exc ? 1 : 0);
    unsigned n = 0;
    if (name) while (n < sizeof(e->name) - 1u && name[n]) {
        e->name[n] = name[n]; ++n;
    }
    e->name[n] = 0;
}
/* Async-safe dump for the harness crash handler: write(2) only, fixed buffer.
 * Returns the number of bytes written. */
long sober_jni_ring_dump_async(int fd)
{
    char buf[8192], *p = buf, *end = buf + sizeof(buf) - 1;
    static const char hx[] = "0123456789abcdef";
    #define WP(s) do { const char *_x=(s); while (*_x && p<end) *p++=*_x++; } while(0)
    #define WH(v) do { uintptr_t _v=(uintptr_t)(v); \
        if (p+2<end){*p++='0';*p++='x';} \
        for(int _i=(int)(sizeof(uintptr_t)*8)-4;_i>=0&&p<end;_i-=4) \
            *p++=hx[(_v>>_i)&0xfu]; } while(0)
    WP("[W24_JNI_RING] last 64 calls (slot,name,arg0,arg1,ret,exc):\n");
    unsigned pos = __atomic_load_n(&w24_ring_pos, __ATOMIC_RELAXED);
    unsigned start = pos > 64u ? pos - 64u : 0u;
    for (unsigned i = start; i < pos; ++i) {
        struct w24_ring_ent *e = &w24_ring[i & 63u];
        WP("["); WH((uintptr_t)(i - start)); WP("] slot="); WH(e->slot);
        WP(" name="); WP(e->name); WP(" a0="); WH(e->arg0);
        WP(" a1="); WH(e->arg1); WP(" ret="); WH(e->ret);
        WP(" exc="); WH(e->exc_pending); WP("\n");
    }
    #undef WP
    #undef WH
    ssize_t u = write(fd, buf, (size_t)(p - buf));
    return (long)(u > 0 ? u : 0);
}

/* ---- W21 read-only perf exec watcher (host-side instrumentation only) ---- *
 * SOBER_W21_WATCH=1 arms hardware EXEC breakpoints (perf_event_open, no guest
 * byte patches) at guest 0x2eb7520 (latch writer) and 0x23115c8 (RSI origin).
 * Handlers only log; they never write guest state.  This is the ACTUAL hit
 * evidence requested by task #602 (not an inference from latch=1). */
static int w21_fd_latch = -1, w21_fd_rsi = -1;
static uintptr_t w21_base;
static long w21_latch_hits;
static long w21_caller_ra_rva;      /* RVA of the caller RA at the latch hit */
static int  w21_latch_before = -1;  /* latch byte sampled inside the handler */
static const char w21_buildid[20] = {
    0x20,0x4a,0xd0,0x49,0x59,0x9c,0x96,0xaf,
    0xb7,0x5b,0x8a,0x21,0xd5,0x05,0x10,0x63,0xe8,0x21,0xd6,0xc9};
static int w21_note(const unsigned char *n, size_t sz)
{
    size_t off = 0;
    while (off + sizeof(Elf64_Nhdr) <= sz) {
        Elf64_Nhdr h; memcpy(&h, n + off, sizeof(h)); off += sizeof(h);
        size_t ns = h.n_namesz, ds = h.n_descsz;
        size_t np = (ns + 3u) & ~3u, dp = (ds + 3u) & ~3u;
        if (ns > 4096u || ds > 4096u || np > sz - off || dp > sz - off - np) return 0;
        if (h.n_type == 3u && ns >= 4u && !memcmp(n + off, "GNU", 4) &&
            ds == 20u && !memcmp(n + off + np, w21_buildid, 20u)) return 1;
        off += np + dp;
    }
    return 0;
}
static int w21_phdr(struct dl_phdr_info *i, size_t sz, void *d)
{
    (void)sz; (void)d;
    for (unsigned j = 0; j < i->dlpi_phnum; ++j) {
        if (i->dlpi_phdr[j].p_type != PT_NOTE) continue;
        const unsigned char *n = (const unsigned char *)
            (i->dlpi_addr + i->dlpi_phdr[j].p_vaddr);
        if (w21_note(n, (size_t)i->dlpi_phdr[j].p_memsz)) {
            w21_base = (uintptr_t)i->dlpi_addr; return 1;
        }
    }
    return 0;
}
static int w21_perf(uintptr_t addr)
{
    struct perf_event_attr a; memset(&a, 0, sizeof(a));
    a.type = PERF_TYPE_BREAKPOINT; a.size = sizeof(a);
    a.bp_type = HW_BREAKPOINT_X; a.bp_addr = (uint64_t)addr;
    a.bp_len = HW_BREAKPOINT_LEN_8; a.disabled = 1;
    a.sample_period = 1; a.wakeup_events = 1;
    a.exclude_kernel = 1; a.exclude_hv = 1;
    return (int)syscall(SYS_perf_event_open, &a, 0, -1, -1, 0);
}
static int w21_arm(int fd, int signo, void (*fn)(int, siginfo_t *, void *))
{
    struct sigaction sa; memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = fn; sa.sa_flags = SA_SIGINFO; sigemptyset(&sa.sa_mask);
    if (sigaction(signo, &sa, NULL) != 0) return -1;
    if (fcntl(fd, F_SETSIG, signo) != 0) return -2;
    if (fcntl(fd, F_SETOWN, getpid()) != 0) return -3;
    if (fcntl(fd, F_SETFL, O_ASYNC) != 0) return -4;
    return ioctl(fd, PERF_EVENT_IOC_ENABLE, 0) != 0 ? -5 : 0;
}
static void w21_latch_sig(int s, siginfo_t *si, void *uc)
{
    (void)s; (void)si;
    ucontext_t *c = (ucontext_t *)uc;
    unsigned long rip = c ? (unsigned long)c->uc_mcontext.gregs[REG_RIP] : 0;
    unsigned long rbp = c ? (unsigned long)c->uc_mcontext.gregs[REG_RBP] : 0;
    unsigned long ra = 0;
    if (rbp) ra = (unsigned long)*(unsigned long *)(rbp + 8);
    w21_latch_before = *(unsigned char *)(w21_base + 0x7cdd149u);
    w21_caller_ra_rva = (long)(ra - w21_base);
    __atomic_fetch_add(&w21_latch_hits, 1, __ATOMIC_RELAXED);
    fprintf(stderr, "[W21_LATCH_HIT] rip=base+0x%lx callerRA=base+0x%lx "
            "latch=0x%x tid=%ld\n",
            rip - w21_base, ra - w21_base,
            w21_latch_before, (long)syscall(SYS_gettid));
}
/* W22 rung-printer credential (READ-ONLY observation, no gate logic):
 * 1 only when a REAL latch hit was seen with latch=0 before, latch=1 after and
 * caller RA == AVD reference 0x2359342. */
int sober_w21_boundary_ok(void)
{
    /* W29 host-only fix: the NULL guard must precede the dereference.  With no
     * watch env w21_init returns early leaving w21_base=0, so the old order
     * faulted at [0x7cdd149] in the no-breakpoint Run D path. */
    if (!w21_base) return 0;
    unsigned char after = *(volatile unsigned char *)(w21_base + 0x7cdd149u);
    return __atomic_load_n(&w21_latch_hits, __ATOMIC_RELAXED) > 0 &&
           w21_latch_before == 0 && after == 1 &&
           w21_caller_ra_rva == 0x2359342L;
}

long sober_w21_latch_hits(void)
{
    return __atomic_load_n(&w21_latch_hits, __ATOMIC_RELAXED);
}

static void w21_rsi_sig(int s, siginfo_t *si, void *uc)
{
    (void)s; (void)si;
    ucontext_t *c = (ucontext_t *)uc;
    unsigned long rax = c ? (unsigned long)c->uc_mcontext.gregs[REG_RAX] : 0;
    uintptr_t ctx = (uintptr_t)rax;
    uintptr_t v0 = ctx ? *(uintptr_t *)ctx : 0u;
    uintptr_t v20 = ctx ? *(uintptr_t *)(ctx + 0x20u) : 0u;
    uintptr_t lo = w21_base + 0x6d37bc0u, hi = w21_base + 0x7220e40u;
    fprintf(stderr, "[W21_RSI_ORIGIN] ctx=%p ctx0=0x%lx ctx20=0x%lx "
            "in_data_rel_ro=%d G=0x%lx\n",
            (void *)ctx, (unsigned long)v0, (unsigned long)v20,
            (v0 >= lo && v0 < hi),
            (unsigned long)*(uintptr_t *)(w21_base + 0x72a3058u));
    /* one-shot; keep armed for repeat activations */
}
extern uintptr_t g_bridge_env_ptr; /* non-static, defined below */   /* defined later with sober_jni_env() */
/* W26: register dump at the host call into 0x24650a9 and at the
 * GetStringUTFChars call site 0x2315556.  Read-only perf exec breakpoints. */
static void w26_regs_sig(int s, siginfo_t *si, void *uc, const char *tag)
{
    (void)s; (void)si;
    ucontext_t *c = (ucontext_t *)uc;
    if (!c) return;
    uintptr_t rdi = (uintptr_t)c->uc_mcontext.gregs[REG_RDI];
    uintptr_t rsi = (uintptr_t)c->uc_mcontext.gregs[REG_RSI];
    uintptr_t ft = 0;
    if (rdi >= 0x10000u && rdi < 0x0000800000000000ULL)
        ft = *(uintptr_t *)rdi;
    fprintf(stderr,
            "[W26_REGS %s] rdi=%p rsi=%p rdx=%p rcx=%p r8=%p r9=%p "
            "fn_table=[rdi]=%p is_bridge_env=%d tid=%ld\n",
            tag, (void *)rdi, (void *)rsi,
            (void *)(uintptr_t)c->uc_mcontext.gregs[REG_RDX],
            (void *)(uintptr_t)c->uc_mcontext.gregs[REG_RCX],
            (void *)(uintptr_t)c->uc_mcontext.gregs[REG_R8],
            (void *)(uintptr_t)c->uc_mcontext.gregs[REG_R9],
            (void *)ft, (rdi && rdi == g_bridge_env_ptr) ? 1 : 0,
            (long)syscall(SYS_gettid));
}
static void w26_entry_sig(int s, siginfo_t *si, void *uc)
{
    w26_regs_sig(s, si, uc, "entry_0x24650a9");
}
static void w26_getstring_sig(int s, siginfo_t *si, void *uc)
{
    w26_regs_sig(s, si, uc, "getstring_0x2315556");
}

/* ---- W28 paired pre/post call hit counters ------------------------------ */
static long w28_hits[4];   /* 0=pre2315 1=post2315 2=pre6ca2 3=post6ca2 */
static void w28_pre(int s, siginfo_t *si, void *uc, int which)
{
    (void)s; (void)si;
    long n = __atomic_fetch_add(&w28_hits[which], 1, __ATOMIC_RELAXED) + 1;
    ucontext_t *c = (ucontext_t *)uc;
    if (!c) return;
    uintptr_t rsi = (uintptr_t)c->uc_mcontext.gregs[REG_RSI];
    uintptr_t val = (rsi >= 0x10000u && rsi < 0x0000800000000000ULL)
                    ? *(uintptr_t *)rsi : 0u;
    fprintf(stderr,
            "[W28_PRE%s #%ld] rsi=%p [rsi]=%p rax=%p tid=%ld\n",
            (which == 0 ? "2315" : "6ca2"), n,
            (void *)rsi, (void *)val,
            (void *)(uintptr_t)c->uc_mcontext.gregs[REG_RAX],
            (long)syscall(SYS_gettid));
}
static void w28_pre_2315(int s, siginfo_t *si, void *uc) { w28_pre(s,si,uc,0); }
static void w28_post_2315(int s, siginfo_t *si, void *uc)
{
    (void)s;(void)si;(void)uc;
    fprintf(stderr, "[W28_POST2315 #%ld]\n",
            __atomic_fetch_add(&w28_hits[1],1,__ATOMIC_RELAXED)+1);
}
static void w28_pre_6ca2(int s, siginfo_t *si, void *uc) { w28_pre(s,si,uc,2); }
static void w28_post_6ca2(int s, siginfo_t *si, void *uc)
{
    (void)s;(void)si;(void)uc;
    fprintf(stderr, "[W28_POST6CA2 #%ld]\n",
            __atomic_fetch_add(&w28_hits[3],1,__ATOMIC_RELAXED)+1);
}

/* W29: per-pre-call TLS trace at 0x2315556 and 0x6ca228b. */
uintptr_t g_w29_tsd_state __attribute__((weak));
static void *(*g_w29_getspecific)(unsigned long);
static void w29_tsd_sig(int s, siginfo_t *si, void *uc, const char *tag)
{
    (void)s; (void)si;
    ucontext_t *c = (ucontext_t *)uc;
    if (!c || !w21_base) return;
    uint32_t key = *(volatile uint32_t *)(w21_base + 0x72a1b08u);
    uintptr_t rax = (uintptr_t)c->uc_mcontext.gregs[REG_RAX];
    uintptr_t rsi = (uintptr_t)c->uc_mcontext.gregs[REG_RSI];
    uintptr_t seed = g_w29_tsd_state;
    uintptr_t tls = g_w29_getspecific
                    ? (uintptr_t)g_w29_getspecific((unsigned long)key) : 0u;
    fprintf(stderr,
            "[W29_TLS %s] tid=%ld key=%u tls=%p [tls]=%p "
            "rax=%p rsi=%p seed=%p [seed]=%#lx eq=%d\n",
            tag, (long)syscall(SYS_gettid), key,
            (void *)tls,
            (tls >= 0x10000u && tls < 0x0000800000000000ULL)
                ? (void *)*(uintptr_t *)tls : NULL,
            (void *)rax, (void *)rsi, (void *)seed,
            seed ? (unsigned long)*(unsigned long *)seed : 0ul,
            (seed && tls == seed) ? 1 : 0);
}
static void w29_tsd_2315(int s, siginfo_t *si, void *uc)
{
    w29_tsd_sig(s, si, uc, "0x2315556");
}
static void w29_tsd_6ca2(int s, siginfo_t *si, void *uc)
{
    w29_tsd_sig(s, si, uc, "0x6ca228b");
}

/* ---- W31 instrumentation ------------------------------------------------
 * (a) perf exec breakpoints at 0x2a13350 (entry) and 0x2a1335e (obj ready):
 *     log tid, obj, [obj+0x28] and 64 bytes at obj via process_vm_readv.
 * (b) perf HW WRITE watch on obj+0x28, armed at 0x2a0deba (accessor return).
 * (d) perf exec watch at 0x2eb7520 using the existing w21_latch_sig (produces
 *     the real [W21_LATCH_HIT] line).
 * No guest bytes are written: perf breakpoints are hardware debug registers.
 */
static uintptr_t w31_base;
static long w31_obj_calls;
static long w31_watch_fd = -1;
static uintptr_t w31_watched_obj;

static long w31_read(uintptr_t addr, void *dst, size_t len)
{
    struct iovec li = { dst, len }, ri = { (void *)addr, len };
    return syscall(SYS_process_vm_readv, (long)getpid(), &li, 1UL, &ri, 1UL, 0UL);
}
static void w31_hex(char **p, uint64_t v)
{
    static const char hx[] = "0123456789abcdef";
    for (int i = 60; i >= 0; i -= 4) *(*p)++ = hx[(v >> i) & 0xfu];
}
static char *w31_lim;
static void w31_puts(char **p, const char *s)
{
    while (*s && *p < w31_lim - 1) *(*p)++ = *s++;
}
static void w31_hex_lim(char **p, uint64_t v)
{
    static const char hx[] = "0123456789abcdef";
    for (int i = 60; i >= 0 && *p < w31_lim - 1; i -= 4) *(*p)++ = hx[(v >> i) & 0xfu];
}

/* WRITE-watch handler: report writer RIP, guest RA, value and tid.  All guest
 * memory is read with process_vm_readv; no direct dereference. */
static void w31_write_sig(int s, siginfo_t *si, void *uc)
{
    (void)s;
    ucontext_t *c = (ucontext_t *)uc;
    if (!c) return;
    uintptr_t rip = (uintptr_t)c->uc_mcontext.gregs[REG_RIP];
    uintptr_t rbp = (uintptr_t)c->uc_mcontext.gregs[REG_RBP];
    uintptr_t ra = 0;
    (void)w31_read(rbp + 8u, &ra, 8u);
    uint64_t v28 = 0;
    (void)w31_read(w31_watched_obj + 0x28u, &v28, 8u);
    uint64_t rax = (uint64_t)c->uc_mcontext.gregs[REG_RAX];
    uint64_t rdx = (uint64_t)c->uc_mcontext.gregs[REG_RDX];
    char line[1024], *p = line; w31_lim = line + sizeof(line);
    w31_puts(&p, "[W31_WRITE] tid=");
    { char t[32]; snprintf(t,sizeof(t),"%ld",(long)syscall(SYS_gettid)); w31_puts(&p,t); }
    w31_puts(&p, " si_addr="); { char t[32]; snprintf(t,sizeof(t),"%p", si?si->si_addr:NULL); w31_puts(&p,t); }
    w31_puts(&p, " writer_rip=");
    if (w31_base && rip >= w31_base) { w31_puts(&p,"guest+0x"); w31_hex(&p,(uint64_t)(rip-w31_base)); }
    else { char t[32]; snprintf(t,sizeof(t),"%p",(void*)rip); w31_puts(&p,t); }
    w31_puts(&p, " guest_RA=");
    if (w31_base && ra >= w31_base && ra < w31_base + 0x8000000u) { w31_puts(&p,"guest+0x"); w31_hex(&p,(uint64_t)(ra-w31_base)); }
    else { char t[32]; snprintf(t,sizeof(t),"%p",(void*)ra); w31_puts(&p,t); }
    w31_puts(&p, " [obj+0x28]="); w31_hex(&p, v28);
    w31_puts(&p, " rax="); w31_hex(&p, rax);
    w31_puts(&p, " rdx="); w31_hex(&p, rdx);
    *p++ = '\n';
    ssize_t u = write(STDERR_FILENO, line, (size_t)(p - line)); (void)u;
}

/* (c) allocation site: 0x2ea6534 is the call into guest allocator 0x1dbad62
 * with rdi = requested size; 0x2ea6539 is the return with rax = state base. */
static void w31_alloc_size_sig(int s, siginfo_t *si, void *uc)
{
    (void)s; (void)si;
    ucontext_t *c = (ucontext_t *)uc;
    if (!c) return;
    uint64_t rdi = (uint64_t)c->uc_mcontext.gregs[REG_RDI];
    uint64_t r12 = (uint64_t)c->uc_mcontext.gregs[REG_R12];
    fprintf(stderr, "[W31_ALLOC_SIZE] tid=%ld rdi(size)=%lu r12=%lu\n",
            (long)syscall(SYS_gettid), (unsigned long)rdi, (unsigned long)r12);
}
static void w31_alloc_ret_sig(int s, siginfo_t *si, void *uc)
{
    (void)s; (void)si;
    ucontext_t *c = (ucontext_t *)uc;
    if (!c) return;
    uint64_t rax = (uint64_t)c->uc_mcontext.gregs[REG_RAX];
    fprintf(stderr, "[W31_ALLOC_RET] tid=%ld state=%p\n",
            (long)syscall(SYS_gettid), (void *)(uintptr_t)rax);
}

static void w31_entry_sig(int s, siginfo_t *si, void *uc)
{
    (void)s; (void)si;
    ucontext_t *c = (ucontext_t *)uc;
    if (!c) return;
    long n = __atomic_fetch_add(&w31_obj_calls, 1, __ATOMIC_RELAXED) + 1;
    fprintf(stderr, "[W31_ENTRY #%ld] tid=%ld rip=base+0x%lx rbp=%p\n", n,
            (long)syscall(SYS_gettid),
            (unsigned long)((uintptr_t)c->uc_mcontext.gregs[REG_RIP] - w31_base),
            (void *)(uintptr_t)c->uc_mcontext.gregs[REG_RBP]);
}
static void w31_obj_sig(int s, siginfo_t *si, void *uc)
{
    (void)s; (void)si;
    ucontext_t *c = (ucontext_t *)uc;
    if (!c) return;
    uintptr_t holder = (uintptr_t)c->uc_mcontext.gregs[REG_RAX];
    uintptr_t obj = 0;
    long got = w31_read(holder, &obj, 8u);
    if (got != 8) {
        fprintf(stderr, "[W31_OBJ] holder=%p UNREADABLE tid=%ld\n",
                (void *)holder, (long)syscall(SYS_gettid));
        return;
    }
    uint64_t v28 = 0;
    long g28 = w31_read(obj + 0x28u, &v28, 8u);
    /* Recursion can call this thousands of times: full 64-byte dump only for
     * the first 8 and every 64th thereafter; every call still logs obj/value. */
    long on = __atomic_load_n(&w31_obj_calls, __ATOMIC_RELAXED);
    int full = (on <= 8) || ((on % 64) == 0);
    unsigned char b[64];
    long gb = full ? w31_read(obj, b, sizeof(b)) : 0;
    int ascii = (g28 == 8);
    if (ascii) for (int i = 0; i < 8; ++i) {
        unsigned ch = (unsigned)((v28 >> (i * 8)) & 0xffu);
        if (ch < 0x20u || ch > 0x7eu) { ascii = 0; break; }
    }
    char line[2048], *p = line; w31_lim = line + sizeof(line);
    w31_puts(&p, "[W31_OBJ] tid=");
    { char t[32]; int n = snprintf(t, sizeof(t), "%ld", (long)syscall(SYS_gettid));
      for (int i = 0; i < n; ++i) *p++ = t[i]; }
    w31_puts(&p, " holder="); { char t[32]; snprintf(t,sizeof(t),"%p",(void*)holder); w31_puts(&p,t); }
    w31_puts(&p, " obj="); { char t[32]; snprintf(t,sizeof(t),"%p",(void*)obj); w31_puts(&p,t); }
    w31_puts(&p, " [obj+0x28]=");
    if (g28 == 8) { w31_hex(&p, v28); }
    else w31_puts(&p, "UNREADABLE");
    w31_puts(&p, " ascii28="); *p++ = (char)('0' + ascii);
    w31_puts(&p, " ascii28_text='");
    if (g28 == 8) for (int i = 0; i < 8; ++i) {
        unsigned ch = (unsigned)((v28 >> (i * 8)) & 0xffu);
        *p++ = (ch >= 0x20u && ch <= 0x7eu) ? (char)ch : '.';
    }
    w31_puts(&p, "' bytes64=");
    if (!full) w31_puts(&p, "OMITTED");
    else if (gb > 0) for (long i = 0; i < gb; ++i) {
        w31_hex_lim(&p, b[i]); if (*p < w31_lim - 1) *p++ = ' ';
    } else w31_puts(&p, "UNREADABLE");
    *p++ = '\n';
    ssize_t u = write(STDERR_FILENO, line, (size_t)(p - line)); (void)u;

    /* (b) arm HW WRITE watch on obj+0x28 as soon as the accessor returned. */
    if (getenv("SOBER_W31_WRITE_WATCH") && obj && obj != w31_watched_obj) {
        w31_watched_obj = obj;
        if (w31_watch_fd >= 0) { close(w31_watch_fd); w31_watch_fd = -1; }
        struct perf_event_attr a; memset(&a, 0, sizeof(a));
        a.type = PERF_TYPE_BREAKPOINT; a.size = sizeof(a);
        a.bp_type = HW_BREAKPOINT_W; a.bp_addr = (uint64_t)(obj + 0x28u);
        a.bp_len = HW_BREAKPOINT_LEN_8; a.disabled = 1; a.sample_period = 1;
        a.wakeup_events = 1; a.exclude_kernel = 1; a.exclude_hv = 1;
        int fd = (int)syscall(SYS_perf_event_open, &a, 0, -1, -1, 0);
        if (fd >= 0) {
            struct sigaction sa; memset(&sa, 0, sizeof(sa));
            sa.sa_sigaction = w31_write_sig; sa.sa_flags = SA_SIGINFO;
            sigemptyset(&sa.sa_mask);
            sigaction(SIGRTMIN + 3, &sa, NULL);
            fcntl(fd, F_SETSIG, SIGRTMIN + 3);
            fcntl(fd, F_SETOWN, getpid());
            fcntl(fd, F_SETFL, O_ASYNC);
            ioctl(fd, PERF_EVENT_IOC_ENABLE, 0);
            w31_watch_fd = fd;
            fprintf(stderr, "[W31_WRITE_ARM] obj=%p watch=%p tid=%ld\n",
                    (void *)obj, (void *)(obj + 0x28u), (long)syscall(SYS_gettid));
        } else fprintf(stderr, "[W31_WRITE_ARM] FAILED obj=%p\n", (void *)obj);
    }
}

/* ---- W32 instrumentation ------------------------------------------------
 * (a) HW exec breakpoints on the two static +0x28 writer sites 0x2a0ed45 and
 *     0x2a1362f: hit count, tid, caller RA, and the 0x2a13350 entry counter at
 *     each hit (0 => hit precedes the first predicate entry).
 * (b) compact obj trace at 0x2a1335e: distinct obj values at predicate entry.
 */
static long w32_writer_hits[2];
static void w32_writer_sig(int s, siginfo_t *si, void *uc, int which)
{
    (void)s; (void)si;
    ucontext_t *c = (ucontext_t *)uc;
    if (!c) return;
    long n = __atomic_fetch_add(&w32_writer_hits[which], 1, __ATOMIC_RELAXED) + 1;
    uintptr_t rbp = (uintptr_t)c->uc_mcontext.gregs[REG_RBP];
    uintptr_t ra = 0;
    (void)w31_read(rbp + 8u, &ra, 8u);
    long entries = __atomic_load_n(&w31_obj_calls, __ATOMIC_RELAXED);
    char line[320], *p = line;
    static char *lim;
    lim = line + sizeof(line);
    #define P(x) do { const char *_s=(x); while (*_s && p<lim-1) *p++=*_s++; } while(0)
    P("[W32_WRITER "); P(which == 0 ? "0x2a0ed45" : "0x2a1362f");
    P("] hit="); { char t[32]; snprintf(t,sizeof(t),"%ld",n); P(t); }
    P(" tid="); { char t[32]; snprintf(t,sizeof(t),"%ld",(long)syscall(SYS_gettid)); P(t); }
    P(" guest_RA=");
    if (w31_base && ra >= w31_base && ra < w31_base + 0x8000000u) {
        P("guest+0x"); w31_hex(&p, (uint64_t)(ra - w31_base));
    } else { char t[32]; snprintf(t,sizeof(t),"%p",(void*)ra); P(t); }
    P(" entries_so_far="); { char t[32]; snprintf(t,sizeof(t),"%ld",entries); P(t); }
    P(" precedes_first_entry="); *p++ = (char)('0' + (entries == 0));
    *p++ = '\n';
    ssize_t u = write(STDERR_FILENO, line, (size_t)(p - line)); (void)u;
    #undef P
}
static void w32_writer0(int s, siginfo_t *si, void *uc) { w32_writer_sig(s,si,uc,0); }
static void w32_writer1(int s, siginfo_t *si, void *uc) { w32_writer_sig(s,si,uc,1); }
static void w32_entry_compact(int s, siginfo_t *si, void *uc)
{
    (void)s; (void)si;
    ucontext_t *c = (ucontext_t *)uc;
    if (!c) return;
    long n = __atomic_fetch_add(&w31_obj_calls, 1, __ATOMIC_RELAXED) + 1;
    if (n <= 3 || (n % 10000) == 0)
        fprintf(stderr, "[W32_ENTRY #%ld] tid=%ld rip=base+0x%lx\n", n,
                (long)syscall(SYS_gettid),
                (unsigned long)((uintptr_t)c->uc_mcontext.gregs[REG_RIP] - w31_base));
}
static void w32_obj_compact(int s, siginfo_t *si, void *uc)
{
    (void)s; (void)si;
    ucontext_t *c = (ucontext_t *)uc;
    if (!c) return;
    uintptr_t holder = (uintptr_t)c->uc_mcontext.gregs[REG_RAX];
    uintptr_t obj = 0;
    if (w31_read(holder, &obj, 8u) != 8) return;
    uint64_t v = 0;
    (void)w31_read(obj + 0x28u, &v, 8u);
    long n = __atomic_load_n(&w31_obj_calls, __ATOMIC_RELAXED);
    fprintf(stderr, "[W32_OBJ #%ld] tid=%ld obj=%p [obj+0x28]=%016lx\n",
            n, (long)syscall(SYS_gettid), (void *)obj, (unsigned long)v);
}

/* W32 lead mid-wave add: read-only exec bp at 0x6ca800e (call *0x720(%rax)).
 * Logs tid, rdi, rax, [rax], [rax+0x720] with guest/host/other decoding.
 * No fix is applied; all guest reads go through process_vm_readv. */
static long w32_call720_hits;
static uintptr_t w32_call720_last;
static uintptr_t w32_host_base;
static void w32_call720_sig(int s, siginfo_t *si, void *uc)
{
    (void)s; (void)si;
    ucontext_t *c = (ucontext_t *)uc;
    if (!c) return;
    long n = __atomic_fetch_add(&w32_call720_hits, 1, __ATOMIC_RELAXED) + 1;
    uintptr_t rax0 = (uintptr_t)c->uc_mcontext.gregs[REG_RAX];
    uintptr_t probe = 0;
    (void)w31_read(rax0 + 0x720u, &probe, 8u);
    int changed = (probe != w32_call720_last);
    w32_call720_last = probe;
    if (n > 3 && n != 100 && !changed) return;
    uintptr_t rdi = (uintptr_t)c->uc_mcontext.gregs[REG_RDI];
    uintptr_t rax = (uintptr_t)c->uc_mcontext.gregs[REG_RAX];
    uintptr_t tab = 0, fn = 0;
    (void)w31_read(rax, &tab, 8u);
    (void)w31_read(rax + 0x720u, &fn, 8u);
    char line[512], *p = line;
    static char *lim; lim = line + sizeof(line);
    #define P(x) do { const char *_s=(x); while (*_s && p<lim-1) *p++=*_s++; } while(0)
    P("[W32_CALL720] hit="); { char t[32]; snprintf(t,sizeof(t),"%ld",n); P(t); }
    P(" tid="); { char t[32]; snprintf(t,sizeof(t),"%ld",(long)syscall(SYS_gettid)); P(t); }
    P(" rdi="); { char t[32]; snprintf(t,sizeof(t),"%p",(void*)rdi); P(t); }
    P(" rax="); { char t[32]; snprintf(t,sizeof(t),"%p",(void*)rax); P(t); }
    P(" [rax]="); { char t[32]; snprintf(t,sizeof(t),"%p",(void*)tab); P(t); }
    P(" [rax+0x720]="); { char t[32]; snprintf(t,sizeof(t),"%p",(void*)fn); P(t); }
    P(" decode=");
    if (w31_base && fn >= w31_base && fn < w31_base + 0x8000000u) {
        P("guest+0x"); w31_hex(&p, (uint64_t)(fn - w31_base));
    } else if (w32_host_base && fn >= w32_host_base &&
               fn < w32_host_base + 0x200000u) {
        P("host+0x"); w31_hex(&p, (uint64_t)(fn - w32_host_base));
    } else P("other");
    *p++ = '\n';
    ssize_t u = write(STDERR_FILENO, line, (size_t)(p - line)); (void)u;
    #undef P
}

/* ---- N1 overnight: read-only perf EXEC breakpoints, no fix --------------
 * 0x2a1335e: rax, [rax], [[rax]+0x28]
 * 0x6ca7f9a: al (predicate result, logged BEFORE the `test al,al`)
 * 0x6ca800e hits 1,2,3,100: rax, rbx, [rax+0x720] as RVA + bridge env.
 * All guest memory reads use process_vm_readv (w31_read); no guest writes.
 */
static long n1_pred_hits, n1_al_hits, n1_call_hits;
static uintptr_t n1_base, n1_host_base;

static void n1_pred_sig(int s, siginfo_t *si, void *uc)
{
    (void)s; (void)si;
    ucontext_t *c = (ucontext_t *)uc;
    if (!c) return;
    long n = __atomic_fetch_add(&n1_pred_hits, 1, __ATOMIC_RELAXED) + 1;
    if (n > 3 && (n % 10000) != 0) return;
    uintptr_t rax = (uintptr_t)c->uc_mcontext.gregs[REG_RAX];
    uintptr_t obj = 0; uint64_t guard = 0;
    long g1 = w31_read(rax, &obj, 8u);
    long g2 = g1 == 8 ? w31_read(obj + 0x28u, &guard, 8u) : -1;
    fprintf(stderr,
            "[N1_PRED #%ld] tid=%ld rax=%p [rax]=%p [[rax]+0x28]=%s%016lx\n",
            n, (long)syscall(SYS_gettid), (void *)rax, (void *)obj,
            g2 == 8 ? "" : "UNREADABLE-", (unsigned long)guard);
}
static void n1_al_sig(int s, siginfo_t *si, void *uc)
{
    (void)s; (void)si;
    ucontext_t *c = (ucontext_t *)uc;
    if (!c) return;
    long n = __atomic_fetch_add(&n1_al_hits, 1, __ATOMIC_RELAXED) + 1;
    if (n > 3 && (n % 10000) != 0) return;
    unsigned long rax = (unsigned long)c->uc_mcontext.gregs[REG_RAX];
    fprintf(stderr, "[N1_AL #%ld] tid=%ld al=0x%02lx al_dec=%lu\n",
            n, (long)syscall(SYS_gettid), (rax & 0xffu), (rax & 0xffu));
}
static void n1_call720_sig(int s, siginfo_t *si, void *uc)
{
    (void)s; (void)si;
    ucontext_t *c = (ucontext_t *)uc;
    if (!c) return;
    long n = __atomic_fetch_add(&n1_call_hits, 1, __ATOMIC_RELAXED) + 1;
    if (n > 3 && n != 100) return;
    uintptr_t rax = (uintptr_t)c->uc_mcontext.gregs[REG_RAX];
    uintptr_t rbx = (uintptr_t)c->uc_mcontext.gregs[REG_RBX];
    uintptr_t fn = 0;
    (void)w31_read(rax + 0x720u, &fn, 8u);
    char line[512], *p = line; static char *lim; lim = line + sizeof(line);
    #define P(x) do { const char *_q=(x); while (*_q && p<lim-1) *p++=*_q++; } while(0)
    P("[N1_CALL720] hit="); { char t[32]; snprintf(t,sizeof(t),"%ld",n); P(t); }
    P(" tid="); { char t[32]; snprintf(t,sizeof(t),"%ld",(long)syscall(SYS_gettid)); P(t); }
    P(" rax="); { char t[32]; snprintf(t,sizeof(t),"%p",(void*)rax); P(t); }
    P(" rbx="); { char t[32]; snprintf(t,sizeof(t),"%p",(void*)rbx); P(t); }
    P(" [rax+0x720]="); { char t[32]; snprintf(t,sizeof(t),"%p",(void*)fn); P(t); }
    P(" decode=");
    if (n1_base && fn >= n1_base && fn < n1_base + 0x8000000u) {
        P("guest+0x"); w31_hex(&p, (uint64_t)(fn - n1_base));
    } else if (n1_host_base && fn >= n1_host_base &&
               fn < n1_host_base + 0x200000u) {
        P("host+0x"); w31_hex(&p, (uint64_t)(fn - n1_host_base));
    } else P("other");
    P(" bridge_env="); { char t[32]; snprintf(t,sizeof(t),"%p",(void*)(uintptr_t)g_bridge_env_ptr); P(t); }
    P(" rax_is_env="); *p++ = (char)('0' + (rax && rax == g_bridge_env_ptr));
    *p++='\n';
    ssize_t u = write(STDERR_FILENO, line, (size_t)(p - line)); (void)u;
    #undef P
}

static void w21_init(uintptr_t base)
{
    if (getenv("SOBER_N1_TRACE")) {
        n1_base = base;
        {
            Dl_info li;
            if (dladdr((void *)(uintptr_t)&n1_call720_sig, &li) && li.dli_fbase)
                n1_host_base = (uintptr_t)li.dli_fbase;
        }
        static const uintptr_t addrs[3] = {
            0x2a1335eu, 0x6ca7f9au, 0x6ca800eu
        };
        static void (*fns[3])(int, siginfo_t *, void *) = {
            n1_pred_sig, n1_al_sig, n1_call720_sig
        };
        static const int sigs[3] = { SIGUSR1, SIGUSR2, 36 };
        int ok[3];
        for (int i = 0; i < 3; ++i) {
            int fd = w21_perf(n1_base + addrs[i]);
            ok[i] = fd >= 0 ? w21_arm(fd, sigs[i], fns[i]) : -1;
        }
        fprintf(stderr, "[N1_ARM] base=%p host=%p pred=%d al=%d call720=%d\n",
                (void *)n1_base, (void *)n1_host_base, ok[0], ok[1], ok[2]);
        return;
    }
    if (getenv("SOBER_W32_CALL720")) {
        w21_base = w31_base = base;
        {
            Dl_info li;
            if (dladdr((void *)(uintptr_t)&w32_call720_sig, &li) && li.dli_fbase)
                w32_host_base = (uintptr_t)li.dli_fbase;
        }
        int fd = w21_perf(w31_base + 0x6ca800eu);
        int ok = fd >= 0 ? w21_arm(fd, SIGRTMIN + 4, w32_call720_sig) : -1;
        fprintf(stderr, "[W32_CALL720_ARM] base=%p host=%p fd_rc=%d\n",
                (void *)w31_base, (void *)w32_host_base, ok);
        return;
    }
    if (getenv("SOBER_W32_TRACE")) {
        w21_base = w31_base = base;
        static const uintptr_t addrs[4] = {
            0x2a13350u, 0x2a1335eu, 0x2a0ed45u, 0x2a1362fu
        };
        static void (*fns[4])(int, siginfo_t *, void *) = {
            w32_entry_compact, w32_obj_compact, w32_writer0, w32_writer1
        };
        static const int sigs[4] = {
            SIGUSR1, SIGUSR2, 36, 37
        };
        int ok[4];
        for (int i = 0; i < 4; ++i) {
            int fd = w21_perf(w31_base + addrs[i]);
            ok[i] = fd >= 0 ? w21_arm(fd, sigs[i], fns[i]) : -1;
        }
        fprintf(stderr, "[W32_ARM] base=%p entry=%d obj=%d writer0=%d writer1=%d\n",
                (void *)w31_base, ok[0], ok[1], ok[2], ok[3]);
        return;
    }
    if (getenv("SOBER_W31_OBJ_TRACE") || getenv("SOBER_W31_LATCH_WATCH")) {
        w21_base = w31_base = base;
        int a1 = -1, a2 = -1, a3a = -1, a3b = -1;
        if (getenv("SOBER_W31_OBJ_TRACE")) {
            int f1 = w21_perf(w31_base + 0x2a13350u);   /* predicate entry */
            int f2 = w21_perf(w31_base + 0x2a1335eu);   /* obj = *rax ready */
            a1 = f1 >= 0 ? w21_arm(f1, SIGUSR1, w31_entry_sig) : -1;
            a2 = f2 >= 0 ? w21_arm(f2, SIGUSR2, w31_obj_sig) : -1;
        }
        int a3 = -1;
        if (getenv("SOBER_W31_LATCH_WATCH")) {
            w21_fd_latch = w21_perf(w21_base + 0x2eb7520u);
            a3 = w21_fd_latch >= 0
                 ? w21_arm(w21_fd_latch, SIGRTMIN + 2, w21_latch_sig) : -1;
        }
        fprintf(stderr, "[W31_ARM] base=%p entry=%d obj=%d alloc=%d/%d latch=%d\n",
                (void *)w31_base, a1, a2, a3a, a3b, a3);
        return;
    }
    if (getenv("SOBER_W29_TSD_TRACE")) {
        w21_base = base;
        void *stub = dlopen("libc.so", RTLD_NOLOAD | RTLD_LAZY);
        if (stub)
            g_w29_getspecific = (void *(*)(unsigned long))
                dlvsym(stub, "pthread_getspecific", "LIBC");
        int f1 = w21_perf(w21_base + 0x2315556u);
        int f2 = w21_perf(w21_base + 0x6ca228bu);
        int a1 = f1 >= 0 ? w21_arm(f1, SIGUSR1, w29_tsd_2315) : -1;
        int a2 = f2 >= 0 ? w21_arm(f2, SIGUSR2, w29_tsd_6ca2) : -1;
        fprintf(stderr, "[W29_TSD_TRACE] armed=%d/%d base=%p\n", a1, a2,
                (void *)w21_base);
        return;
    }
    if (getenv("SOBER_W28_PAIRS")) {
        w21_base = base;
        static int fds[4] = {-1,-1,-1,-1};
        static const uintptr_t addrs[4] = {
            0x2315556u, 0x231555cu, 0x6ca229cu, 0x6ca22a2u
        };
        static void (*fns[4])(int, siginfo_t *, void *) = {
            w28_pre_2315, w28_post_2315, w28_pre_6ca2, w28_post_6ca2
        };
        static const int sigs[4] = { SIGUSR1, SIGUSR2, 34, 35 };
        int ok = 1;
        for (int i = 0; i < 4; ++i) {
            fds[i] = w21_perf(w21_base + addrs[i]);
            if (fds[i] < 0) { ok = 0; break; }
            if (w21_arm(fds[i], sigs[i], fns[i]) != 0) { ok = 0; break; }
        }
        fprintf(stderr, "[W28_PAIRS] armed=%d base=%p\n", ok, (void *)w21_base);
        return;
    }
    if (!getenv("SOBER_W21_WATCH") && !getenv("SOBER_W26_WATCH")) return;
    w21_base = base;   /* guest already mapped when the seeder runs */
    int w26 = getenv("SOBER_W26_WATCH") != NULL;
    if (w26) {
        w21_fd_latch = w21_perf(w21_base + 0x24650a9u);      /* host call entry */
        w21_fd_rsi   = w21_perf(w21_base + 0x2315556u);      /* call [rax+0x548] */
    } else {
        w21_fd_latch = w21_perf(w21_base + 0x2eb7520u);
        w21_fd_rsi   = w21_perf(w21_base + 0x23115c8u);
    }
    if (w21_fd_latch < 0 || w21_fd_rsi < 0) {
        fprintf(stderr, "[W21_WATCH] perf_event_open FAILED\n"); return;
    }
    int a = w26 ? w21_arm(w21_fd_latch, SIGUSR1, w26_entry_sig)
                : w21_arm(w21_fd_latch, SIGUSR1, w21_latch_sig);
    int b = w26 ? w21_arm(w21_fd_rsi, SIGUSR2, w26_getstring_sig)
                : w21_arm(w21_fd_rsi, SIGUSR2, w21_rsi_sig);
    fprintf(stderr, "[W21_WATCH] mode=%s base=%p fd1=%d fd2=%d\n",
            w26 ? "W26" : "W21", (void *)w21_base, a, b);
}

/* ---- local JNI scalar types (ABI-identical to jni.h) ------------------ */
typedef int32_t  sr_jint;
typedef uint8_t  sr_jboolean;
typedef uint16_t sr_jchar;
typedef int64_t  sr_jlong;
#define SR_JNI_VERSION_1_6 0x00010006

#define SR_OBJ_MAGIC 0x534A4E49u   /* "SJNI" */

/* Published by the native calloc bridge (src/compat/bionic_bootstrap). */
extern void *sober_bootstrap_calloc_handles[16] __attribute__((weak));
extern unsigned sober_bootstrap_calloc_handle_count __attribute__((weak));
static int sr_is_safe_ptr(const void *p);

static SoberJni *g_active_jni;

/* Zero-signal field transition logger: called from hot JNI entry points. */
static int sr_log_fields_enabled;
static void sr_log_handle_fields(const char *tag)
{
    if (!sr_log_fields_enabled) return;
    if (!&sober_bootstrap_calloc_handles || !&sober_bootstrap_calloc_handle_count) return;
    unsigned n = sober_bootstrap_calloc_handle_count;
    if (n > 16) n = 16;
    for (unsigned i = 0; i < n; ++i) {
        unsigned char *h = (unsigned char *)sober_bootstrap_calloc_handles[i];
        if (!h || !sr_is_safe_ptr(h)) continue;
        fprintf(stderr, "[handle-fields] %s #%u ptr=%p +0x20=%#lx +0x68=%#lx\n",
                tag, i, (void *)h,
                (unsigned long)*(uint64_t *)(h + 0x20),
                (unsigned long)*(uint64_t *)(h + 0x68));
    }
}

#define SR_MAX_CLASSES   256
#define SR_MAX_METHODS   768
#define SR_MAX_REFS     1024
#define SR_MAX_NATIVES   512
#define SR_MAX_HANDLERS  256
#define SR_LOG_LINES      96
#define SR_LOG_WIDTH     192

/* ---- function table shapes -------------------------------------------- */
typedef struct {
    void *reserved0, *reserved1, *reserved2, *reserved3;
    void *fn[231];            /* JNINativeInterface_ order */
} SRNativeInterface;

typedef struct {
    void *reserved0, *reserved1, *reserved2;
    void *fn[5];              /* JNIInvokeInterface_ order */
} SRInvokeInterface;

typedef struct SoberJni SoberJni;

/* The first field is the pointer the guest dereferences with (*env)->... ;
 * extra fields are invisible to the guest but let our stubs find their owner. */
typedef struct {
    SRNativeInterface *functions;
    SoberJni          *owner;
} SREnvObj;

typedef struct {
    SRInvokeInterface *functions;
    SoberJni          *owner;
} SRVmObj;

/* ---- synthetic objects ------------------------------------------------- */
typedef enum { SR_K_CLASS, SR_K_METHOD, SR_K_OBJECT, SR_K_STRING } SrKind;

/* Guest-shaped objects: the engine validator at 0x245605d checks
 * *(jobj) -> object B with magic 0xc0dedbad, and *(B+0x68) -> object C with
 * magic 0x000bab1e.  Our SrObject therefore keeps a pointer to B at offset 0
 * and our own magic at +8 (sr_check reads that). */
typedef struct {
    uint32_t magic;   /* 0xc0dedbad */
    uint32_t reserved;
    uint64_t pad[12];
    void    *child;   /* +0x68 */
} SrGuestB;

typedef struct {
    uint32_t magic;   /* 0x000bab1e */
    uint32_t reserved;
    uint64_t pad[14];
} SrGuestC;

typedef struct {
    void    *guest_b;     /* +0x00: pointer to A (engine treats us as a slot) */
    uint32_t magic;       /* +0x08: SR_OBJ_MAGIC */
    uint32_t reserved;
    SrKind   kind;
    uint32_t pad0;
    char    *name;        /* class name / method name / string bytes */
    char    *sig;
    char    *class_name;
    int      refs;
    uint32_t pad1;
    void    *payload;
    size_t   payload_size;
    uint64_t pad2[4];
    void    *guest_c;     /* +0x68: pointer to B */
    /* Extended graph (decision 017 / worker-1 Addendum 8); all canonical. */
    void    *guest_d;
    void    *guest_deep_c;
    void    *guest_table;
    void    *guest_sub;
} SrObject;

typedef struct {
    char *class_name;
    char *method_name;
    char *sig;
    void *fn;
} SrNative;

typedef struct {
    char *name;
    SoberJniCallbackFn fn;
    void *user;
    uint64_t count;
} SrHandler;

struct SoberJni {
    SoberJniOptions opts;
    SRNativeInterface env_table;
    SRInvokeInterface vm_table;
    SREnvObj env_obj;
    SRVmObj  vm_obj;
    int      initialised;

    SrObject *classes[SR_MAX_CLASSES];  int n_classes;
    SrObject *methods[SR_MAX_METHODS];  int n_methods;
    SrObject *objects[SR_MAX_REFS];     int n_objects;
    SrNative  natives[SR_MAX_NATIVES];  int n_natives;
    SrHandler handlers[SR_MAX_HANDLERS];int n_handlers;

    uint64_t env_calls;
    uint64_t callback_calls;
    uint64_t register_calls;
    char     last_callback[SR_LOG_WIDTH];
    char     log[SR_LOG_LINES][SR_LOG_WIDTH];
    int      log_head;
    int      log_count;
};

/* ---- helpers ----------------------------------------------------------- */
/* Re-entrancy guard: a native callback must never be able to re-enter the
 * JNI dispatcher through the guest's exception/scheduler wrappers. */
static _Thread_local int sr_in_dispatch;

static char *sr_strdup(const char *s)
{
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *p = (char *)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

static void sr_add_log(SoberJni *j, const char *fmt, ...)
{
    if (!j) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(j->log[j->log_head], sizeof(j->log[0]), fmt, ap);
    va_end(ap);
    j->log_head = (j->log_head + 1) % SR_LOG_LINES;
    if (j->log_count < SR_LOG_LINES) j->log_count++;
    if (j->opts.verbose) fprintf(stderr, "[sober-jni] %s\n", j->log[j->log_head]);
}

static SrObject *sr_new_object(SoberJni *j, SrKind kind,
                               const char *name, const char *sig,
                               const char *class_name)
{
    SrObject *o = (SrObject *)calloc(1, sizeof(*o));
    if (!o) return NULL;

    /* A: magic 0xc0dedbad, index 0, B* at +0x68, D* at +0xf90.
     * B: magic 0x000bab1e, 16-entry table at +0x10 (entry[0]=B),
     *    capacity 16 at +0x20, C* at +0x120.  D/C are plain zeroed nodes. */
    /* >= 0x1100: the guest creator for 0xc0dedbad handles writes fields up to
     * +0x10xx; an undersized block lets a late copier land in stale bytes. */
    unsigned char *A = (unsigned char *)calloc(1, 0x1200);
    unsigned char *Bv = (unsigned char *)calloc(1, 0x1200);
    unsigned char *Cv = (unsigned char *)calloc(1, 0x1200);
    unsigned char *Dv = (unsigned char *)calloc(1, 64);
    void **table = (void **)calloc(16, sizeof(void *));
    if (!A || !Bv || !Cv || !Dv || !table) {
        free(A); free(Bv); free(Cv); free(Dv); free(table); free(o);
        return NULL;
    }
    *(uint32_t *)(A + 0x00) = 0xc0dedbadu;
    *(uint32_t *)(A + 0x10) = 0u;                 /* index */
    *(uint32_t *)(Bv + 0x00) = 0x000bab1eu;
    table[0] = Bv;
    *(void **)(Bv + 0x10) = table;
    *(uint32_t *)(Bv + 0x20) = 16u;               /* capacity */
    *(int32_t *)(Dv + 0x08) = 0;
    /* Leave +0x68 and +0x20 ZERO on any object carrying 0xc0dedbad: the
     * guest's lazy-link guard (0x2350044) and teardown null-checks (0x23331ce
     * / 0x2333593) then take their safe zero branches instead of consuming
     * stale code bytes.  Bv is kept in our private field for internal use. */
    memset(A + 0x20, 0, 8);
    memset(A + 0x68, 0, 8);
    *(void **)(A + 0xf90) = Dv;
    *(void **)(Bv + 0x120) = Cv;
    /* Second-level +0x68 (guest 0x2333593 reads [B+0x68] then [result+0x120]).
     * Close the gap at construction time: every level is a canonical pointer
     * into this same self-consistent graph. */
    *(uint32_t *)(Cv + 0x00) = 0x000bab1eu;
    *(void **)(Cv + 0x120) = Cv;          /* self pointer: always canonical */
    *(void **)(Bv + 0x68) = Cv;

    o->guest_b = A;
    o->guest_c = Bv;
    o->guest_sub = Bv;   /* internal back-reference; not at A+0x68 */
    o->guest_d = Dv;
    o->guest_deep_c = Cv;
    o->guest_table = table;
    o->magic = SR_OBJ_MAGIC;
    o->kind = kind;
    o->name = sr_strdup(name ? name : "");
    o->sig = sr_strdup(sig ? sig : "");
    o->class_name = sr_strdup(class_name ? class_name : "");
    o->refs = 1;
    SrObject **slot = NULL;
    int *count = NULL;
    switch (kind) {
        case SR_K_CLASS:  slot = j->classes; count = &j->n_classes; break;
        case SR_K_METHOD: slot = j->methods; count = &j->n_methods; break;
        default:          slot = j->objects; count = &j->n_objects; break;
    }
    if (*count < (kind == SR_K_CLASS ? SR_MAX_CLASSES :
                  kind == SR_K_METHOD ? SR_MAX_METHODS : SR_MAX_REFS)) {
        slot[(*count)++] = o;
    }
    return o;
}

/* The guest can hand us its own stack-guard sentinel (0xABCDEF...) or other
 * bogus jobject values.  Never dereference an untrusted handle: validate the
 * pointer shape first, then the magic word. */
static int sr_is_safe_ptr(const void *p)
{
    uintptr_t v = (uintptr_t)p;
    if (!p || v < 0x10000u) return 0;
    if (v == 0xabcdef0123456789ULL) return 0;
    if (v > 0x00007fffffffffffULL && v < 0xffff000000000000ULL) return 0;
    return 1;
}

static SrObject *sr_check(void *h, SrKind kind)
{
    if (!sr_is_safe_ptr(h)) return NULL;
    SrObject *o = (SrObject *)h;
    if (o->magic != SR_OBJ_MAGIC) return NULL;
    if (kind != (SrKind)-1 && o->kind != kind) return NULL;
    return o;
}

static SoberJni *sr_owner_from_env(void *env)
{
    SREnvObj *e = (SREnvObj *)env;
    return e ? e->owner : NULL;
}

static SrObject *sr_find_class_object(SoberJni *j, const char *name)
{
    if (!j || !name) return NULL;
    for (int i = 0; i < j->n_classes; ++i) {
        SrObject *o = j->classes[i];
        if (o && o->name && !strcmp(o->name, name)) return o;
    }
    return sr_new_object(j, SR_K_CLASS, name, NULL, name);
}

static SrObject *sr_find_method_object(SoberJni *j, SrObject *clazz,
                                       const char *name, const char *sig)
{
    if (!j || !name) return NULL;
    for (int i = 0; i < j->n_methods; ++i) {
        SrObject *m = j->methods[i];
        if (m && m->name && !strcmp(m->name, name) &&
            (!sig || !m->sig || !strcmp(m->sig, sig)) &&
            (!clazz || !m->class_name || !strcmp(m->class_name, clazz->name)))
            return m;
    }
    return sr_new_object(j, SR_K_METHOD, name, sig,
                         clazz ? clazz->name : "");
}

/* Never dereference an arbitrary "canonical-looking" pointer handed to us by
 * the guest.  Only objects we actually allocated may be inspected. */
static int sr_is_our_object(SoberJni *j, const void *h)
{
    if (!j || !h) return 0;
    for (int i = 0; i < j->n_objects; ++i) if ((void *)j->objects[i] == h) return 1;
    for (int i = 0; i < j->n_classes; ++i) if ((void *)j->classes[i] == h) return 1;
    for (int i = 0; i < j->n_methods; ++i) if ((void *)j->methods[i] == h) return 1;
    return 0;
}
/* Public membership check for default modules that must not dereference a
 * canonical-looking foreign handle (jni_probe deliberately supplies one). */
int sober_jni_owns_handle(const void *h)
{
    return sr_is_our_object(g_active_jni, h);
}

static SrObject *sr_validate(SoberJni *j, void *h, SrKind kind)
{
    if (!sr_is_our_object(j, h)) return NULL;
    return sr_check(h, kind);
}

/* ---- default stub & real JNIEnv entry points -------------------------- */
static void *sr_stub(void *env, ...)
{
    (void)env;
    return NULL;
}

static sr_jint sr_GetVersion(void *env)
{
    SoberJni *j = sr_owner_from_env(env);
    if (j) { j->env_calls++; sr_add_log(j, "GetVersion"); }
    return SR_JNI_VERSION_1_6;
}

static void *sr_FindClass(void *env, const char *name)
{
    sr_log_handle_fields("FindClass");
    SoberJni *j = sr_owner_from_env(env);
    if (!j) return NULL;
    j->env_calls++;
    SrObject *o = sr_find_class_object(j, name);
    sr_add_log(j, "FindClass(%s) -> %p", name ? name : "(null)", (void *)o);
    w24_ring_note(2u, name, (uintptr_t)name, 0u, (uintptr_t)o, 0);
    return o;
}

static void *sr_GetObjectClass(void *env, void *obj)
{
    SoberJni *j = sr_owner_from_env(env);
    if (!j) return NULL;
    j->env_calls++;
    SrObject *o = sr_check(obj, (SrKind)-1);
    const char *cn = o ? (o->class_name && o->class_name[0] ? o->class_name : o->name) : "java/lang/Object";
    SrObject *clazz = sr_find_class_object(j, cn);
    sr_add_log(j, "GetObjectClass -> %s", cn);
    return clazz;
}

static void *sr_GetMethodID_impl(SoberJni *j, void *clazz,
                                 const char *name, const char *sig)
{
    sr_log_handle_fields("GetMethodID");
    if (!j) return NULL;
    j->env_calls++;
    SrObject *c = sr_check(clazz, SR_K_CLASS);
    SrObject *m = sr_find_method_object(j, c, name, sig);
    sr_add_log(j, "GetMethodID(%s.%s%s) -> %p",
               c && c->name ? c->name : "?", name ? name : "?",
               sig ? sig : "", (void *)m);
    return m;
}

static void *sr_GetMethodID(void *env, void *clazz, const char *name, const char *sig)
{ return sr_GetMethodID_impl(sr_owner_from_env(env), clazz, name, sig); }
static void *sr_GetStaticMethodID(void *env, void *clazz, const char *name, const char *sig)
{ return sr_GetMethodID_impl(sr_owner_from_env(env), clazz, name, sig); }

/* A handle handed back to the guest must always be shaped and canonical. */
static void *sr_shaped_or_new(SoberJni *j, void *h, const char *name)
{
    SrObject *o = sr_check(h, (SrKind)-1);
    if (o) return h;
    return sr_new_object(j, SR_K_OBJECT,
                         name ? name : "java/lang/Object", NULL,
                         name ? name : "java/lang/Object");
}

static void *sr_NewGlobalRef(void *env, void *obj)
{
    SoberJni *j = sr_owner_from_env(env);
    if (j) { j->env_calls++; sr_add_log(j, "NewGlobalRef(%p)", obj); }
    void *safe = sr_shaped_or_new(j, obj, NULL);
    SrObject *o = sr_check(safe, (SrKind)-1);
    if (o) o->refs++;
    return safe;
}
static void sr_DeleteGlobalRef(void *env, void *obj)
{
    SoberJni *j = sr_owner_from_env(env);
    if (j) { j->env_calls++; sr_add_log(j, "DeleteGlobalRef(%p)", obj); }
    SrObject *o = sr_check(obj, (SrKind)-1);
    if (o && o->refs > 0) o->refs--;
}
static void *sr_NewLocalRef(void *env, void *obj) { return sr_NewGlobalRef(env, obj); }
static void sr_DeleteLocalRef(void *env, void *obj) { sr_DeleteGlobalRef(env, obj); }
static void *sr_NewWeakGlobalRef(void *env, void *obj) { return sr_NewGlobalRef(env, obj); }
static void sr_DeleteWeakGlobalRef(void *env, void *obj) { sr_DeleteGlobalRef(env, obj); }
static sr_jboolean sr_IsSameObject(void *env, void *a, void *b)
{
    SoberJni *j = sr_owner_from_env(env);
    if (j) j->env_calls++;
    return (sr_jboolean)(a == b);
}
static sr_jboolean sr_IsInstanceOf(void *env, void *obj, void *clazz)
{
    (void)obj; (void)clazz;
    SoberJni *j = sr_owner_from_env(env);
    if (j) j->env_calls++;
    return 1;
}

/* ---- RegisterNatives --------------------------------------------------- */
typedef struct {
    char *name;
    char *signature;
    void *fnPtr;
} SRJNINativeMethod;

static sr_jint sr_RegisterNatives(void *env, void *clazz,
                                  const SRJNINativeMethod *methods,
                                  sr_jint count)
{
    SoberJni *j = sr_owner_from_env(env);
    if (!j) return -1;
    j->env_calls++;
    j->register_calls++;
    SrObject *c = sr_check(clazz, SR_K_CLASS);
    const char *cn = c && c->name ? c->name : "?";
    sr_add_log(j, "RegisterNatives(%s, %d methods)", cn, (int)count);
    for (sr_jint i = 0; methods && i < count; ++i) {
        if (j->n_natives >= SR_MAX_NATIVES) break;
        SrNative *n = &j->natives[j->n_natives++];
        n->class_name = sr_strdup(cn);
        n->method_name = sr_strdup(methods[i].name ? methods[i].name : "");
        n->sig = sr_strdup(methods[i].signature ? methods[i].signature : "");
        n->fn = methods[i].fnPtr;
        sr_add_log(j, "  native %s.%s%s -> %p", cn, n->method_name, n->sig, n->fn);
    }
    return 0;
}
static sr_jint sr_UnregisterNatives(void *env, void *clazz)
{
    (void)clazz;
    SoberJni *j = sr_owner_from_env(env);
    if (j) { j->env_calls++; sr_add_log(j, "UnregisterNatives"); }
    return 0;
}

/* ---- method-call dispatch --------------------------------------------- */
static intptr_t sr_dispatch_call(SoberJni *j, void *obj, void *method,
                                 void *args, int is_va, int want_object)
{
    if (!j) return 0;
    if (sr_in_dispatch) return 0;   /* block recursive callback re-entry */
    sr_in_dispatch = 1;
    (void)is_va;   /* args are always passed through opaquely to handlers */
    j->env_calls++;
    SrObject *m = sr_check(method, SR_K_METHOD);
    const char *name = m && m->name ? m->name : "?";
    const char *sig = m && m->sig ? m->sig : "";
    sr_add_log(j, "Call %s%s (obj=%p)", name, sig, obj);
    snprintf(j->last_callback, sizeof(j->last_callback), "%s", name);
    /* B11: opt-in provenance recorder; no-op unless SOBER_CALLBACK_PROVENANCE. */
    sober_callback_provenance_on_dispatch(name, obj, method);
    /* Phase 8: DEFAULT-ON positive provenance credential.  Fail-closed; this
     * only records the dispatch for the decision-013 gate.  It never invokes
     * a handler, emits a guest->Java callback, or writes a marker.  The gate
     * queries it with sober_jni_is_genuine_callback()/is_genuine(). */
    sober_provenance_gate_on_dispatch(name, obj, method);

    intptr_t result = 0;
    /* Registered native method first (what RegisterNatives installed). */
    for (int i = 0; i < j->n_natives; ++i) {
        SrNative *n = &j->natives[i];
        if (n->fn && n->method_name && !strcmp(n->method_name, name)) {
            intptr_t (*fn)(void *, void *, ...) = (intptr_t (*)(void *, void *, ...))n->fn;
            result = fn(&j->env_obj, obj, name, sig, args);
            break;
        }
    }
    /* Named callback handler (the app-shell surface worker-1 maps). */
    for (int i = 0; i < j->n_handlers; ++i) {
        SrHandler *h = &j->handlers[i];
        if (h->fn && h->name && !strcmp(h->name, name)) {
            j->callback_calls++;
            h->count++;
            result = h->fn(&j->env_obj, obj, name, sig, args, h->user);
            break;
        }
    }
    /* Object-returning calls must NEVER hand the guest a NULL, a narrow int,
     * or a sign-extended 32-bit handle (worker-1/lead decision 013+014). */
    if (want_object) {
        void *safe = sr_shaped_or_new(j, (void *)result, "java/lang/String");
        result = (intptr_t)safe;
    }
    sr_in_dispatch = 0;
    w24_ring_note(142u, name ? name : "?", (uintptr_t)obj, (uintptr_t)method,
                  (uintptr_t)result, 0);
    return result;
}

/* Plain (varargs) forms: forward a va_list* to handlers. */
#define SR_DEFINE_CALL_PLAIN(fname, ret, want_obj)                          \
    static ret fname(void *env, void *obj, void *method, ...) {            \
        SoberJni *j = sr_owner_from_env(env);                               \
        va_list ap; va_start(ap, method);                                   \
        intptr_t r = sr_dispatch_call(j, obj, method, &ap, 1, want_obj);    \
        va_end(ap);                                                         \
        return (ret)r;                                                      \
    }

/* V forms: `args` is already a va_list; pass its address. */
#define SR_DEFINE_CALL_V(fname, ret, want_obj)                              \
    static ret fname(void *env, void *obj, void *method, va_list args) {    \
        SoberJni *j = sr_owner_from_env(env);                               \
        return (ret)sr_dispatch_call(j, obj, method, &args, 1, want_obj);   \
    }

/* A forms: `args` is a jvalue array; pass it verbatim. */
#define SR_DEFINE_CALL_A(fname, ret, want_obj)                              \
    static ret fname(void *env, void *obj, void *method, void *args) {      \
        SoberJni *j = sr_owner_from_env(env);                               \
        return (ret)sr_dispatch_call(j, obj, method, args, 0, want_obj);    \
    }

SR_DEFINE_CALL_PLAIN(sr_CallObjectMethod, void *, 1)
SR_DEFINE_CALL_PLAIN(sr_CallBooleanMethod, sr_jboolean, 0)
SR_DEFINE_CALL_PLAIN(sr_CallByteMethod, int8_t, 0)
SR_DEFINE_CALL_PLAIN(sr_CallCharMethod, sr_jchar, 0)
SR_DEFINE_CALL_PLAIN(sr_CallShortMethod, int16_t, 0)
SR_DEFINE_CALL_PLAIN(sr_CallIntMethod, sr_jint, 0)
SR_DEFINE_CALL_PLAIN(sr_CallLongMethod, sr_jlong, 0)
SR_DEFINE_CALL_PLAIN(sr_CallFloatMethod, float, 0)
SR_DEFINE_CALL_PLAIN(sr_CallDoubleMethod, double, 0)
static void sr_CallVoidMethod(void *env, void *obj, void *method, ...)
{
    SoberJni *j = sr_owner_from_env(env);
    va_list ap; va_start(ap, method);
    (void)sr_dispatch_call(j, obj, method, &ap, 1, 0);
    va_end(ap);
}
SR_DEFINE_CALL_V(sr_CallObjectMethodV, void *, 1)
SR_DEFINE_CALL_V(sr_CallBooleanMethodV, sr_jboolean, 0)
SR_DEFINE_CALL_V(sr_CallByteMethodV, int8_t, 0)
SR_DEFINE_CALL_V(sr_CallCharMethodV, sr_jchar, 0)
SR_DEFINE_CALL_V(sr_CallShortMethodV, int16_t, 0)
SR_DEFINE_CALL_V(sr_CallIntMethodV, sr_jint, 0)
SR_DEFINE_CALL_V(sr_CallLongMethodV, sr_jlong, 0)
SR_DEFINE_CALL_V(sr_CallFloatMethodV, float, 0)
SR_DEFINE_CALL_V(sr_CallDoubleMethodV, double, 0)
static void sr_CallVoidMethodV(void *env, void *obj, void *method, va_list args)
{
    (void)sr_dispatch_call(sr_owner_from_env(env), obj, method, &args, 1, 0);
}
SR_DEFINE_CALL_A(sr_CallObjectMethodA, void *, 1)
SR_DEFINE_CALL_A(sr_CallBooleanMethodA, sr_jboolean, 0)
SR_DEFINE_CALL_A(sr_CallByteMethodA, int8_t, 0)
SR_DEFINE_CALL_A(sr_CallCharMethodA, sr_jchar, 0)
SR_DEFINE_CALL_A(sr_CallShortMethodA, int16_t, 0)
SR_DEFINE_CALL_A(sr_CallIntMethodA, sr_jint, 0)
SR_DEFINE_CALL_A(sr_CallLongMethodA, sr_jlong, 0)
SR_DEFINE_CALL_A(sr_CallFloatMethodA, float, 0)
SR_DEFINE_CALL_A(sr_CallDoubleMethodA, double, 0)
static void sr_CallVoidMethodA(void *env, void *obj, void *method, void *args)
{
    (void)sr_dispatch_call(sr_owner_from_env(env), obj, method, args, 0, 0);
}

/* Static object form must RETURN the synthetic object the dispatcher made;
 * dropping it is what produced "DeviceStaticParams is null" in the guest. */
static void *sr_CallStaticObjectMethod(void *env, void *c, void *m, ...)
{
    sr_log_handle_fields("CallStaticObjectMethod");
    SoberJni *j = sr_owner_from_env(env);
    va_list ap; va_start(ap, m);
    intptr_t r = sr_dispatch_call(j, c, m, &ap, 1, 1);
    va_end(ap);
    return (void *)r;
}
static sr_jint sr_CallStaticIntMethod(void *env, void *c, void *m, ...)
{ SoberJni *j = sr_owner_from_env(env); va_list ap; va_start(ap,m);
  (void)sr_dispatch_call(j,c,m,&ap,1,0); va_end(ap); return 0; }
static void sr_CallStaticVoidMethod(void *env, void *c, void *m, ...)
{ sr_log_handle_fields("CallStaticVoidMethod"); SoberJni *j = sr_owner_from_env(env); va_list ap; va_start(ap,m);
  (void)sr_dispatch_call(j,c,m,&ap,1,0); va_end(ap); }

/* Phase 5: the guest calls the cached gameLoadedCallback jmethodID through
 * CallStaticVoidMethodV (JNIEnv fn[138], global slot 142, offset 0x470) or
 * the A form at fn[139].  Without these, both slots stay NULL stubs and a
 * genuine guest callback can never be observed. */
static void sr_CallStaticVoidMethodV(void *env, void *c, void *m, va_list args)
{
    sr_log_handle_fields("CallStaticVoidMethodV");
    (void)sr_dispatch_call(sr_owner_from_env(env), c, m, &args, 1, 0);
}
static void sr_CallStaticVoidMethodA(void *env, void *c, void *m, void *args)
{
    sr_log_handle_fields("CallStaticVoidMethodA");
    (void)sr_dispatch_call(sr_owner_from_env(env), c, m, args, 0, 0);
}

/* ---- strings / exceptions / arrays ------------------------------------ */
/* UTF-16 family at the canonical JNI slots 159/161/162. Our synthetic string
 * payload is UTF-8; these provide correctly typed, non-NULL behaviour. */
static void *sr_NewString(void *env, const uint16_t *chars, int32_t len)
{
    SoberJni *j = sr_owner_from_env(env);
    if (!j) return NULL;
    size_t n = len > 0 ? (size_t)len : 0;
    char *buf = (char *)calloc(n + 1, 1);
    if (!buf) return NULL;
    for (size_t i = 0; i < n; ++i)
        buf[i] = chars[i] < 128 ? (char)chars[i] : '?';
    SrObject *o = sr_new_object(j, SR_K_STRING, buf, NULL, "java/lang/String");
    free(buf);
    if (!o) return NULL;
    free(o->payload);
    o->payload = sr_strdup(o->name ? o->name : "");
    o->payload_size = strlen(o->name ? o->name : "") + 1;
    sr_add_log(j, "NewString(len=%d)", (int)len);
    return o;
}
static const uint16_t *sr_GetStringChars(void *env, void *str, sr_jboolean *isCopy)
{
    SoberJni *j = sr_owner_from_env(env);
    if (j) j->env_calls++;
    if (isCopy) *isCopy = 0;
    SrObject *o = sr_validate(j, str, SR_K_STRING);
    if (!o || !o->guest_c) return NULL;
    return (const uint16_t *)o->guest_c;
}
static void sr_ReleaseStringChars(void *env, void *str, const uint16_t *chars)
{ (void)str; (void)chars; SoberJni *j = sr_owner_from_env(env); if (j) j->env_calls++; }

static void *sr_NewStringUTF(void *env, const char *bytes)
{
    SoberJni *j = sr_owner_from_env(env);
    if (!j) return NULL;
    j->env_calls++;
    SrObject *o = sr_new_object(j, SR_K_STRING, bytes ? bytes : "", NULL,
                                "java/lang/String");
    if (o && bytes) o->payload = sr_strdup(bytes);
    if (getenv("SOBER_JNI_STRING_DEBUG"))
        fprintf(stderr, "[jni-str] NewStringUTF -> %p payload='%s' len=%zu\n",
                (void *)o, bytes ? bytes : "", bytes ? strlen(bytes) : 0);
    sr_add_log(j, "NewStringUTF(\"%.48s\")", bytes ? bytes : "");
    return o;
}
static const char *sr_GetStringUTFChars(void *env, void *str, sr_jboolean *isCopy)
{
    SoberJni *j = sr_owner_from_env(env);
    if (j) j->env_calls++;
    if (isCopy) *isCopy = 0;
    SrObject *o = sr_validate(j, str, SR_K_STRING);
    if (getenv("SOBER_JNI_STRING_DEBUG"))
        fprintf(stderr, "[jni-str] GetStringUTFChars(%p) -> o=%p payload='%s'\n",
                str, (void *)o, o && o->payload ? (const char *)o->payload : "(null)");
    if (o && o->payload) {
        w24_ring_note(165u, "GetStringUTFChars", (uintptr_t)str,
                      (uintptr_t)isCopy, (uintptr_t)o->payload, 0);
        return (const char *)o->payload;
    }
    /* Unknown / foreign handle: hand back a safe static empty C string. */
    static const char empty[] = "";
    w24_ring_note(165u, "GetStringUTFChars", (uintptr_t)str,
                  (uintptr_t)isCopy, (uintptr_t)empty, 0);
    return empty;
}
static void sr_ReleaseStringUTFChars(void *env, void *str, const char *chars)
{
    (void)str; (void)chars;
    SoberJni *j = sr_owner_from_env(env);
    if (j) j->env_calls++;
}
static sr_jint sr_GetStringUTFLength(void *env, void *str)
{
    SoberJni *j = sr_owner_from_env(env);
    if (j) j->env_calls++;
    SrObject *o = sr_validate(j, str, SR_K_STRING);
    return o && o->name ? (sr_jint)strlen(o->name) : 0;
}
/* Exception state is deliberately PURE: no callbacks, no logging, no counter
 * writes.  The guest's 0x6ca7f72/0x2a12e80 wrapper calls slot 0x720 on every
 * scheduler lambda, so any side effect here can re-enter that path. */
static void *sr_ExceptionOccurred(void *env)
{ (void)env; return NULL; }
static sr_jboolean sr_ExceptionCheck(void *env)
{ (void)env; return 0; }
static void sr_ExceptionClear(void *env)
{ (void)env; }
static sr_jint sr_Throw(void *env, void *t)
{ (void)t; SoberJni *j = sr_owner_from_env(env); if (j) j->env_calls++; return 0; }
static sr_jint sr_ThrowNew(void *env, void *c, const char *msg)
{ (void)c; SoberJni *j = sr_owner_from_env(env); if (j && msg) sr_add_log(j, "ThrowNew: %s", msg); return 0; }
static sr_jint sr_PushLocalFrame(void *env, sr_jint cap)
{ (void)cap; SoberJni *j = sr_owner_from_env(env); if (j) j->env_calls++; return 0; }
static void *sr_PopLocalFrame(void *env, void *result)
{ SoberJni *j = sr_owner_from_env(env); if (j) j->env_calls++; return result; }
static sr_jint sr_MonitorEnter(void *env, void *o)
{ (void)o; SoberJni *j = sr_owner_from_env(env); if (j) j->env_calls++; return 0; }
static sr_jint sr_MonitorExit(void *env, void *o)
{ (void)o; SoberJni *j = sr_owner_from_env(env); if (j) j->env_calls++; return 0; }
static sr_jint sr_GetJavaVM(void *env, void ***vm)
{
    SoberJni *j = sr_owner_from_env(env);
    if (!j || !vm) return -1;
    j->env_calls++;
    *vm = (void **)&j->vm_obj;
    return 0;
}
static sr_jint sr_GetArrayLength(void *env, void *arr)
{ (void)arr; SoberJni *j = sr_owner_from_env(env); if (j) j->env_calls++; return 0; }
static void *sr_NewObjectArray(void *env, sr_jint len, void *clazz, void *init)
{ (void)len; (void)clazz; (void)init; SoberJni *j = sr_owner_from_env(env);
  if (!j) return NULL;
  j->env_calls++;
  return sr_new_object(j, SR_K_OBJECT, "java/lang/Object", "[Ljava/lang/Object;", clazz ? "java/lang/Object" : NULL); }
static void *sr_GetObjectArrayElement(void *env, void *arr, sr_jint idx)
{ (void)arr; (void)idx; SoberJni *j = sr_owner_from_env(env); if (j) j->env_calls++; return NULL; }
static void sr_SetObjectArrayElement(void *env, void *arr, sr_jint idx, void *val)
{ (void)arr; (void)idx; (void)val; SoberJni *j = sr_owner_from_env(env); if (j) j->env_calls++; }
static void *sr_AllocObject(void *env, void *clazz)
{
    SoberJni *j = sr_owner_from_env(env);
    if (!j) return NULL;
    j->env_calls++;
    SrObject *c = sr_check(clazz, SR_K_CLASS);
    return sr_new_object(j, SR_K_OBJECT, c && c->name ? c->name : "java/lang/Object",
                         NULL, c && c->name ? c->name : "java/lang/Object");
}
static void *sr_NewObject(void *env, void *clazz, void *method, ...)
{
    (void)method;
    SoberJni *j = sr_owner_from_env(env);
    if (!j) return NULL;
    j->env_calls++;
    SrObject *c = sr_check(clazz, SR_K_CLASS);
    sr_add_log(j, "NewObject(%s)", c && c->name ? c->name : "?");
    return sr_new_object(j, SR_K_OBJECT, c && c->name ? c->name : "java/lang/Object",
                         NULL, c && c->name ? c->name : "java/lang/Object");
}

/* ---- JavaVM table ------------------------------------------------------ */
static sr_jint sr_GetEnv(void *vm, void **env_out, sr_jint version)
{
    (void)version;
    SRVmObj *v = (SRVmObj *)vm;
    if (!env_out || !v || !v->owner) return -1;
    *env_out = &v->owner->env_obj;
    return 0;
}
static sr_jint sr_AttachCurrentThread(void *vm, void **env_out, void *args)
{ (void)args; return sr_GetEnv(vm, env_out, 0); }
static sr_jint sr_DetachCurrentThread(void *vm) { (void)vm; return 0; }
static sr_jint sr_DestroyJavaVM(void *vm) { (void)vm; return 0; }
static sr_jint sr_AttachCurrentThreadAsDaemon(void *vm, void **env_out, void *args)
{ return sr_AttachCurrentThread(vm, env_out, args); }

/* ---- table construction ------------------------------------------------ */
#define SETFN(tab, idx, fp) do { (tab).fn[(idx)] = (void *)(uintptr_t)(fp); } while (0)

static void sr_build_tables(SoberJni *j)
{
    SRNativeInterface *t = &j->env_table;
    memset(t, 0, sizeof(*t));
    for (int i = 0; i < 231; ++i) t->fn[i] = (void *)(uintptr_t)&sr_stub;

    SETFN(*t,   0, sr_GetVersion);
    SETFN(*t,   2, sr_FindClass);
    SETFN(*t,   9, sr_Throw);
    SETFN(*t,  10, sr_ThrowNew);
    SETFN(*t,  11, sr_ExceptionOccurred);
    SETFN(*t,  13, sr_ExceptionClear);
    SETFN(*t,  15, sr_PushLocalFrame);
    SETFN(*t,  16, sr_PopLocalFrame);
    SETFN(*t,  17, sr_NewGlobalRef);
    SETFN(*t,  18, sr_DeleteGlobalRef);
    SETFN(*t,  19, sr_DeleteLocalRef);
    SETFN(*t,  20, sr_IsSameObject);
    SETFN(*t,  21, sr_NewLocalRef);
    SETFN(*t,  27, sr_GetObjectClass);
    SETFN(*t,  28, sr_IsInstanceOf);
    SETFN(*t,  29, sr_GetMethodID);
    SETFN(*t,  30, sr_CallObjectMethod);
    SETFN(*t,  31, sr_CallObjectMethodV);
    SETFN(*t,  32, sr_CallObjectMethodA);
    SETFN(*t,  33, sr_CallBooleanMethod);
    SETFN(*t,  34, sr_CallBooleanMethodV);
    SETFN(*t,  35, sr_CallBooleanMethodA);
    SETFN(*t,  36, sr_CallByteMethod);
    SETFN(*t,  37, sr_CallByteMethodV);
    SETFN(*t,  38, sr_CallByteMethodA);
    SETFN(*t,  39, sr_CallCharMethod);
    SETFN(*t,  40, sr_CallCharMethodV);
    SETFN(*t,  41, sr_CallCharMethodA);
    SETFN(*t,  42, sr_CallShortMethod);
    SETFN(*t,  43, sr_CallShortMethodV);
    SETFN(*t,  44, sr_CallShortMethodA);
    SETFN(*t,  45, sr_CallIntMethod);
    SETFN(*t,  46, sr_CallIntMethodV);
    SETFN(*t,  47, sr_CallIntMethodA);
    SETFN(*t,  48, sr_CallLongMethod);
    SETFN(*t,  49, sr_CallLongMethodV);
    SETFN(*t,  50, sr_CallLongMethodA);
    SETFN(*t,  51, sr_CallFloatMethod);
    SETFN(*t,  52, sr_CallFloatMethodV);
    SETFN(*t,  53, sr_CallFloatMethodA);
    SETFN(*t,  54, sr_CallDoubleMethod);
    SETFN(*t,  55, sr_CallDoubleMethodV);
    SETFN(*t,  56, sr_CallDoubleMethodA);
    SETFN(*t,  57, sr_CallVoidMethod);
    SETFN(*t,  58, sr_CallVoidMethodV);
    SETFN(*t,  59, sr_CallVoidMethodA);
    SETFN(*t, 109, sr_GetStaticMethodID);
    /* Static Call* variants share the instance wrappers' ABI shape; the
     * dispatcher does not distinguish a jclass from a jobject. */
    SETFN(*t, 110, sr_CallStaticObjectMethod);   /* CallStaticObjectMethod    */
    SETFN(*t, 113, sr_CallBooleanMethod);        /* CallStaticBooleanMethod   */
    SETFN(*t, 116, sr_CallByteMethod);           /* CallStaticByteMethod      */
    SETFN(*t, 119, sr_CallCharMethod);           /* CallStaticCharMethod      */
    SETFN(*t, 122, sr_CallShortMethod);          /* CallStaticShortMethod     */
    SETFN(*t, 125, sr_CallStaticIntMethod);      /* CallStaticIntMethod       */
    SETFN(*t, 128, sr_CallLongMethod);           /* CallStaticLongMethod      */
    SETFN(*t, 131, sr_CallFloatMethod);          /* CallStaticFloatMethod     */
    SETFN(*t, 134, sr_CallDoubleMethod);         /* CallStaticDoubleMethod    */
    SETFN(*t, 137, sr_CallStaticVoidMethod);     /* CallStaticVoidMethod      */
    SETFN(*t, 138, sr_CallStaticVoidMethodV);    /* slot 142 / offset 0x470   */
    SETFN(*t, 139, sr_CallStaticVoidMethodA);    /* CallStaticVoidMethodA     */
    /* Canonical jni.h slots (verified against worker-1's finding): 159/161/162
     * are the UTF-16 NewString/GetStringChars/ReleaseStringChars family; the
     * UTF-8 family we actually implement lives at 163/165/166. */
    SETFN(*t, 159, sr_NewString);
    SETFN(*t, 161, sr_GetStringChars);
    SETFN(*t, 162, sr_ReleaseStringChars);
    SETFN(*t, 163, sr_NewStringUTF);
    SETFN(*t, 165, sr_GetStringUTFChars);
    SETFN(*t, 166, sr_ReleaseStringUTFChars);
    SETFN(*t, 164, sr_GetStringUTFLength);
    SETFN(*t, 167, sr_GetArrayLength);
    SETFN(*t, 168, sr_NewObjectArray);
    SETFN(*t, 169, sr_GetObjectArrayElement);
    SETFN(*t, 170, sr_SetObjectArrayElement);
    SETFN(*t, 211, sr_RegisterNatives);
    SETFN(*t, 212, sr_UnregisterNatives);
    SETFN(*t, 213, sr_MonitorEnter);
    SETFN(*t, 214, sr_MonitorExit);
    SETFN(*t, 215, sr_GetJavaVM);
    SETFN(*t, 222, sr_NewWeakGlobalRef);
    SETFN(*t, 223, sr_DeleteWeakGlobalRef);
    SETFN(*t, 224, sr_ExceptionCheck);
    SETFN(*t,  23, sr_AllocObject);
    SETFN(*t,  24, sr_NewObject);

    /* Phase 8: one installer per JNIEnv-slot responsibility.  jnislots_default
     * covers the 18 slots formerly split across static_object_calls,
     * scalar_returns, jni_arrays and jni_strings; fields_default covers the
     * three field slots formerly owned by platform_fields.  The old opt-in
     * modules are deliberately NOT installed here (no duplicate pairs); they
     * remain in the CMake target only because other harness objects still
     * reference their symbols. */
    (void)sober_jnislots_default_install(t->fn, 231);
    /* Safety override for JNIEnv slot 165 (GetStringUTFChars).  The module's
     * sj_utf8 falls back to sj_typed_string_text() when its delegate returns
     * an empty string; that helper only checks pointer SHAPE and dereferences
     * arbitrary "canonical-looking" handles.  The headless jni_probe self-test
     * deliberately passes 0x55f800000000 and faults there.  The bridge's own
     * sr_GetStringUTFChars uses sr_validate() -> sr_is_our_object() membership
     * before dereferencing and returns a safe empty C string for foreign
     * handles.  Re-install it here (the module file is not owned by this
     * worker): same safety outcome, no fabricated callback, no marker. */
    SETFN(*t, 165, sr_GetStringUTFChars);
    (void)sober_fields_default_install_slots(t->fn, 231);
    /* A5 audit fix: the module self-gates on SOBER_CALLBACK_PROVENANCE; calling
     * it through is_active() first made the installer unreachable (dead code). */
    (void)sober_callback_provenance_install(NULL);

    SRInvokeInterface *v = &j->vm_table;
    memset(v, 0, sizeof(*v));
    for (int i = 0; i < 5; ++i) v->fn[i] = (void *)(uintptr_t)&sr_stub;
    SETFN(*v, 0, sr_DestroyJavaVM);
    SETFN(*v, 1, sr_AttachCurrentThread);
    SETFN(*v, 2, sr_DetachCurrentThread);
    SETFN(*v, 3, sr_GetEnv);
    SETFN(*v, 4, sr_AttachCurrentThreadAsDaemon);

    j->env_obj.functions = &j->env_table;
    j->env_obj.owner = j;
    j->vm_obj.functions = &j->vm_table;
    j->vm_obj.owner = j;
    j->initialised = 1;
}

/* ---- public API -------------------------------------------------------- */
#define SR_API __attribute__((visibility("default")))

SR_API int sober_jni_probe(SoberJniProbe *out)
{
    SoberJniProbe p;
    memset(&p, 0, sizeof(p));
    p.bridge_ready = 1;
    p.env_slots = 231;
    p.vm_slots = 5;
    /* Optional native JVM discovery: file check only, never starts a JVM. */
    const char *cands[4];
    char homebuf[512];
    const char *home = getenv("JAVA_HOME");
    int n = 0;
    if (home && *home) {
        snprintf(homebuf, sizeof(homebuf), "%s/lib/server/libjvm.so", home);
        cands[n++] = homebuf;
    }
    cands[n++] = "/usr/lib/jvm/default-java/lib/server/libjvm.so";
    cands[n++] = "/usr/lib64/libjvm.so";
    cands[n++] = "/usr/lib/jvm/java-21-openjdk-amd64/lib/server/libjvm.so";
    for (int i = 0; i < n; ++i) {
        if (access(cands[i], R_OK) == 0) {
            p.jvm_available = 1;
            p.jvm_path = cands[i] == homebuf ? "JAVA_HOME/lib/server/libjvm.so" : cands[i];
            break;
        }
    }
    if (out) *out = p;
    return SOBER_JNI_OK;
}

SR_API const char *sober_jni_probe_describe(char *buf, size_t cap)
{
    if (!buf || !cap) return "";
    SoberJniProbe p;
    sober_jni_probe(&p);
    snprintf(buf, cap,
             "bridge=ready env_slots=%u vm_slots=%u native_jvm=%d jvm_path=%s",
             p.env_slots, p.vm_slots, p.jvm_available,
             p.jvm_path ? p.jvm_path : "");
    return buf;
}

SR_API int sober_jni_create(const SoberJniOptions *opts, SoberJni **out)
{
    if (out) *out = NULL;
    SoberJni *j = (SoberJni *)calloc(1, sizeof(*j));
    if (!j) return SOBER_JNI_ERR;
    if (opts) j->opts = *opts;
    sr_log_fields_enabled = (getenv("SOBER_TRACE_HANDLE_FIELDS") != NULL ||
                             getenv("SOBER_POLL_FIELDS") != NULL);
    sr_build_tables(j);
    g_active_jni = j;
    sr_add_log(j, "native JNI bridge created");
    if (out) *out = j;
    return SOBER_JNI_OK;
}

SR_API int sober_jni_init(SoberJni *j)
{
    if (!j) return SOBER_JNI_ERR;
    if (!j->initialised) sr_build_tables(j);
    return SOBER_JNI_OK;
}

SR_API void sober_jni_shutdown(SoberJni *j)
{
    if (!j) return;
    if (g_active_jni == j) g_active_jni = NULL;
    for (int i = 0; i < j->n_classes; ++i) { SrObject *o=j->classes[i]; free(o->name); free(o->sig); free(o->class_name); free(o->payload); free(o->guest_d); free(o->guest_deep_c); free(o->guest_table); free(o->guest_b); free(o->guest_c); free(o); }
    for (int i = 0; i < j->n_methods; ++i) { SrObject *o=j->methods[i]; free(o->name); free(o->sig); free(o->class_name); free(o->payload); free(o->guest_d); free(o->guest_deep_c); free(o->guest_table); free(o->guest_b); free(o->guest_c); free(o); }
    for (int i = 0; i < j->n_objects; ++i) { SrObject *o=j->objects[i]; free(o->name); free(o->sig); free(o->class_name); free(o->payload); free(o->guest_d); free(o->guest_deep_c); free(o->guest_table); free(o->guest_b); free(o->guest_c); free(o); }
    for (int i = 0; i < j->n_natives; ++i) { SrNative *n=&j->natives[i]; free(n->class_name); free(n->method_name); free(n->sig); }
    for (int i = 0; i < j->n_handlers; ++i) free(j->handlers[i].name);
    memset(j, 0, sizeof(*j));
    free(j);
}

uintptr_t g_bridge_env_ptr, g_bridge_vm_ptr;
void sober_jni_bridge_ptrs(uintptr_t *env, uintptr_t *vm)
{
    if (env) *env = g_bridge_env_ptr;
    if (vm) *vm = g_bridge_vm_ptr;
}
SR_API void *sober_jni_java_vm(SoberJni *j)
{
    void *p = j ? (void *)&j->vm_obj : NULL;
    if (p) g_bridge_vm_ptr = (uintptr_t)p;
    return p;
}
SR_API void *sober_jni_env(SoberJni *j)
{
    void *p = j ? (void *)&j->env_obj : NULL;
    if (p) g_bridge_env_ptr = (uintptr_t)p;
    return p;
}
SR_API int sober_jni_attach(SoberJni *j, void **env_out)
{
    if (!j || !env_out) return SOBER_JNI_ERR;
    *env_out = &j->env_obj;
    j->env_calls++;
    return SOBER_JNI_OK;
}

SR_API void *sober_jni_find_class(SoberJni *j, const char *name)
{ return sr_find_class_object(j, name); }
SR_API void *sober_jni_get_method_id(SoberJni *j, void *clazz,
                                     const char *name, const char *sig)
{ return sr_find_method_object(j, sr_check(clazz, SR_K_CLASS), name, sig); }
SR_API void *sober_jni_new_global_ref(SoberJni *j, void *obj)
{ (void)j; SrObject *o = sr_check(obj,(SrKind)-1); if (o) o->refs++; return obj; }
SR_API void sober_jni_delete_global_ref(SoberJni *j, void *obj)
{ (void)j; SrObject *o = sr_check(obj,(SrKind)-1); if (o && o->refs) o->refs--; }

SR_API int sober_jni_register_native(SoberJni *j, const char *class_name,
                                     const char *method_name, const char *sig,
                                     void *fn)
{
    if (!j || !method_name || j->n_natives >= SR_MAX_NATIVES) return SOBER_JNI_ERR;
    SrNative *n = &j->natives[j->n_natives++];
    n->class_name = sr_strdup(class_name ? class_name : "");
    n->method_name = sr_strdup(method_name);
    n->sig = sr_strdup(sig ? sig : "");
    n->fn = fn;
    sr_add_log(j, "register native %s.%s%s", n->class_name, n->method_name, n->sig);
    return SOBER_JNI_OK;
}
SR_API int sober_jni_is_native_registered(SoberJni *j, const char *class_name,
                                          const char *method_name)
{
    if (!j) return 0;
    for (int i = 0; i < j->n_natives; ++i) {
        SrNative *n = &j->natives[i];
        if (n->method_name && !strcmp(n->method_name, method_name ? method_name : "") &&
            (!class_name || !n->class_name || !strcmp(n->class_name, class_name)))
            return 1;
    }
    return 0;
}

SR_API int sober_jni_set_callback_handler(SoberJni *j, const char *method_name,
                                          SoberJniCallbackFn fn, void *user)
{
    if (!j || !method_name) return SOBER_JNI_ERR;
    for (int i = 0; i < j->n_handlers; ++i) {
        if (j->handlers[i].name && !strcmp(j->handlers[i].name, method_name)) {
            j->handlers[i].fn = fn; j->handlers[i].user = user;
            return SOBER_JNI_OK;
        }
    }
    if (j->n_handlers >= SR_MAX_HANDLERS) return SOBER_JNI_ERR;
    SrHandler *h = &j->handlers[j->n_handlers++];
    h->name = sr_strdup(method_name);
    h->fn = fn;
    h->user = user;
    return SOBER_JNI_OK;
}

static intptr_t sr_default_app_shell_handler(void *env, void *obj,
                                             const char *name, const char *sig,
                                             void *args, void *user)
{
    (void)obj; (void)args; (void)user;
    SoberJni *j = sr_owner_from_env(env);
    if (j) sr_add_log(j, "app-shell callback satisfied: %s%s", name, sig ? sig : "");
    if (!j || !name) return 0;

    /* Object-returning getters must be non-NULL and plausibly typed: worker-1
     * observed the guest warning "DeviceStaticParams is null" and looping in
     * the scheduler-gated FindClass wrapper 0x6c8f340. */
    int object_return = (sig && strstr(sig, ")L") != NULL);
    if (!object_return) return 0;
    const char *payload = "native";
    if (strstr(name, "Locale")) payload = "en-US";
    else if (strstr(name, "UserAgent")) payload = "SoberNative/1.0";
    else if (strstr(name, "AdvertisingId")) payload = "00000000-0000-0000-0000-000000000000";
    else if (strstr(name, "Username") || strstr(name, "DisplayName")) payload = "player";
    else if (strstr(name, "UserId") || strstr(name, "MembershipType")) payload = "0";

    const char *class_name = "java/lang/String";
    if (!strcmp(name, "getDeviceStaticParams")) {
        class_name = "com/roblox/engine/jni/model/DeviceStaticParams";
        payload = "";
    }
    SrObject *o = sr_new_object(j, SR_K_OBJECT, payload, sig, class_name);
    return (intptr_t)o;
}

SR_API int sober_jni_install_default_app_shell_handlers(SoberJni *j)
{
    if (!j) return SOBER_JNI_ERR;
    static const char *const names[] = {
        /* app-shell specific (worker-1 §4) */
        "onAppShellReloadNeeded", "gameLoadedCallback", "getDeviceStaticParams",
        "getWebViewUserAgent", "getMobileAdvertisingId", "showKeyboard",
        "hideKeyboard", "getRobloxLocale", "getGameLocale", "getUsername",
        "getDisplayName", "getUserId", "getMembershipType", "getTheme",
        "getProcessTimestamp", "onDataModelNotificationCallback",
        "onLuaTextBoxChangedCallback", "onLuaTextBoxPropertyChangedCallback",
        "onExtendedAnalyticsRecvCallback",
        /* activity/lifecycle (worker-1 §2 + §4) */
        "onStartNative", "onResumeNative", "onPauseNative", "onStopNative",
        "onSurfaceCreatedNative", "onSurfaceChangedNative",
        "onSurfaceDestroyedNative", "onSurfaceRedrawNeededNative",
        "onWindowFocusChangedNative", "onConfigurationChangedNative",
        "onTrimMemoryNative", "onTouchEventNative", "onKeyDownNative",
        "onKeyUpNative", "initializeNativeCode",
        NULL
    };
    for (int i = 0; names[i]; ++i)
        (void)sober_jni_set_callback_handler(j, names[i],
                                             sr_default_app_shell_handler, NULL);
    sr_add_log(j, "installed %zu default app-shell handlers",
               sizeof(names) / sizeof(names[0]) - 1);
    return SOBER_JNI_OK;
}

/* Locate the versioned NDK libc stub (build/ndk-stubs/libc.so), NOT glibc's
 * libc.so.6.  The guest's pthread_getspecific@LIBC reads the stub's private
 * 512-slot array, so seeding via glibc's pthread_setspecific has no effect. */
struct SrStubScan { char path[512]; };
static int sr_scan_stub(struct dl_phdr_info *info, size_t size, void *data)
{
    (void)size;
    if (!info || !info->dlpi_name || !data) return 0;
    const char *n = info->dlpi_name;
    /* Match "libc.so" but not "libc.so.6" or other libcs. */
    size_t len = strlen(n);
    if (len >= 8 && !strcmp(n + len - 8, "/libc.so")) {
        struct SrStubScan *s = (struct SrStubScan *)data;
        snprintf(s->path, sizeof(s->path), "%s", n);
        return 1;
    }
    return 0;
}

static void *sr_stub_libc(void)
{
    static void *cached;
    static int tried;
    if (tried) return cached;
    tried = 1;
    const char *env = getenv("SOBER_NDK_STUB_LIBC");
    if (env && *env) {
        cached = dlopen(env, RTLD_LAZY | RTLD_LOCAL);
        if (cached) return cached;
    }
    struct SrStubScan scan;
    memset(&scan, 0, sizeof(scan));
    if (dl_iterate_phdr(sr_scan_stub, &scan) && scan.path[0])
        cached = dlopen(scan.path, RTLD_LAZY | RTLD_LOCAL);
    if (!cached) cached = dlopen("libc.so", RTLD_LAZY | RTLD_LOCAL);
    return cached;
}

/* ---- shared_ptr teardown seeds ---------------------------------------- */
SR_API void sober_jni_noop_shared_dtor(void *control_block)
{
    (void)control_block;    /* native no-op: guest keeps its own logic */
}

/* Slots: +0x10 = virtual release/destroy, +0x20/+0x28 = destructor variants. */
static void *const sr_shared_vtable[6] = {
    NULL, NULL,
    (void *)(uintptr_t)&sober_jni_noop_shared_dtor,   /* +0x10 */
    NULL,
    (void *)(uintptr_t)&sober_jni_noop_shared_dtor,   /* +0x20 */
    (void *)(uintptr_t)&sober_jni_noop_shared_dtor    /* +0x28 */
};

SR_API void *sober_jni_shared_vtable(void)
{
    return (void *)sr_shared_vtable;
}

typedef struct {
    void    *vtable;
    int64_t  refcount;
} SrSharedControl;

SR_API void *sober_jni_new_shared_control(SoberJni *j)
{
    (void)j;
    SrSharedControl *c = (SrSharedControl *)calloc(1, sizeof(*c));
    if (!c) return NULL;
    c->vtable = (void *)sr_shared_vtable;
    c->refcount = 1;
    return c;
}

/* Separate shared_ptr-style wrapper (worker-1's attachment ruling): NEVER
 * overwrite A+0 (validator magic) or B+0 (backing magic). */
#define SR_SHARED_PAYLOAD_OFF 0x40

SR_API void *sober_jni_new_shared_param(SoberJni *j, const char *back_pointer,
                                        size_t bytes, const void *initial)
{
    (void)j;
    if (bytes == 0) bytes = 1;
    unsigned char *w = (unsigned char *)calloc(1, SR_SHARED_PAYLOAD_OFF + bytes);
    if (!w) return NULL;
    *(void **)(w + 0x00) = (void *)sr_shared_vtable;
    *(int64_t *)(w + 0x08) = 1;
    *(void **)(w + 0x10) = (void *)(uintptr_t)back_pointer;
    *(size_t *)(w + 0x18) = bytes;
    if (initial) memcpy(w + SR_SHARED_PAYLOAD_OFF, initial, bytes);
    return w;
}

SR_API unsigned char *sober_jni_shared_param_data(void *wrapper, size_t *cap)
{
    if (!wrapper) { if (cap) *cap = 0; return NULL; }
    if (cap) *cap = *(size_t *)((unsigned char *)wrapper + 0x18);
    return (unsigned char *)wrapper + SR_SHARED_PAYLOAD_OFF;
}

SR_API void *sober_jni_new_string_utf8(SoberJni *j, const char *text)
{
    if (!j) return NULL;
    const char *s = text ? text : "";
    SrObject *o = sr_new_object(j, SR_K_STRING, s, NULL, "java/lang/String");
    if (!o) return NULL;
    free(o->payload);
    o->payload = sr_strdup(s);
    o->payload_size = strlen(s) + 1;
    return o;
}
SR_API const char *sober_jni_string_utf8(SoberJni *j, void *str)
{
    (void)j;
    SrObject *o = sr_check(str, SR_K_STRING);
    return o && o->payload ? (const char *)o->payload : "";
}
SR_API void *sober_jni_new_native_object(SoberJni *j, const char *class_name,
                                         size_t bytes, const void *initial)
{
    if (!j || bytes == 0) return NULL;
    SrObject *o = sr_new_object(j, SR_K_OBJECT,
                                class_name ? class_name : "java/lang/Object",
                                NULL, class_name ? class_name : "java/lang/Object");
    if (!o) return NULL;
    void *p = calloc(1, bytes);
    if (!p) return NULL;
    if (initial) memcpy(p, initial, bytes);
    free(o->payload);
    o->payload = p;
    o->payload_size = bytes;
    return o;
}
SR_API unsigned char *sober_jni_native_object_data(SoberJni *j, void *obj,
                                                   size_t *cap)
{
    (void)j;
    SrObject *o = sr_check(obj, SR_K_OBJECT);
    if (!o || !o->payload) { if (cap) *cap = 0; return NULL; }
    if (cap) *cap = o->payload_size;
    return (unsigned char *)o->payload;
}

SR_API int sober_jni_seed_guest_tls(void *guest_base, uintptr_t key_rva,
                                    size_t state_bytes)
{
    if (!guest_base || !key_rva) return SOBER_JNI_ERR;
    if (state_bytes == 0) state_bytes = 4096;
    uint32_t key = 0;
    memcpy(&key, (const unsigned char *)guest_base + key_rva, sizeof(key));
    if (key == 0 || key >= 512) return SOBER_JNI_ERR;

    void *stub = sr_stub_libc();
    if (!stub) { fprintf(stderr, "[sober-jni] NDK libc stub not loaded\n"); return SOBER_JNI_ERR; }
    int (*stub_set)(unsigned int, const void *) =
        (int (*)(unsigned int, const void *))dlvsym(stub, "pthread_setspecific", "LIBC");
    if (!stub_set) {
        fprintf(stderr, "[sober-jni] stub pthread_setspecific@@LIBC unavailable\n");
        return SOBER_JNI_ERR;
    }
    void *state = calloc(1, state_bytes);
    if (!state) return SOBER_JNI_ERR;
    if (stub_set(key, state) != 0) { free(state); return SOBER_JNI_ERR; }
    fprintf(stderr,
            "[sober-jni] seeded STUB TSD key %u (rva %#lx) -> %p (%zu bytes)\n",
            (unsigned)key, (unsigned long)key_rva, state, state_bytes);
    return SOBER_JNI_OK;
}

SR_API int sober_jni_neutralize_guest_scheduler(void *guest_base,
                                                uintptr_t singleton_rva)
{
    if (!guest_base || !singleton_rva) return SOBER_JNI_ERR;
    unsigned char *p = (unsigned char *)guest_base + singleton_rva;
    memset(p + 0x28, 0, 16);   /* callback pointer (+0x28) and neighbour */
    /* Task #464: zeroing +0x70 destroys the guest's packed nesting word and
     * trips the scheduler assertion at 0x2a13315 -> RSL panic -> RBXCRASH int3.
     * Keep the guest's own ctor-initialized value when opted in; +0x28 still
     * breaks the FindClass recursion that motivated the neutralize. */
    int keep_r70 = getenv("SOBER_SCHED_R70_KEEP") != NULL;
    int nest_one = getenv("SOBER_SCHED_NEST_ONE") != NULL;
    if (nest_one) {
        uint64_t *w = (uint64_t *)(p + 0x70);
        *w = (*w & ~0x3fULL) | 1ULL;  /* guest scheduler nesting low 6 bits */
    } else if (!keep_r70) memset(p + 0x70, 0, 8);
    fprintf(stderr,
            "[sober-jni] neutralised guest scheduler at rva %#lx "
            "(+0x28%s)\n", (unsigned long)singleton_rva,
            keep_r70 ? " only, +0x70 kept" : ",+0x70");
    return SOBER_JNI_OK;
}

/* Cached jmethodID globals from analysis/jni/jni-map.md §1.2 (AppImage 204ad049). */
typedef struct {
    uintptr_t   rva;
    const char *class_name;
    const char *method;
    const char *sig;
} SrCachedMethod;

static const SrCachedMethod sr_cached_methods[] = {
    { 0x72a3008u, "com/roblox/universalapp/logging/LoggingProtocol",
      "getProcessTimestamp", "()J" },
    { 0x72a2f68u, "com/roblox/engine/jni/NativeGLJavaInterface",
      "showKeyboard", "(JZ[BLcom/roblox/engine/jni/model/NativeTextBoxInfo;)V" },
    { 0x72a2f70u, "com/roblox/engine/jni/NativeGLJavaInterface", "hideKeyboard", "()V" },
    { 0x72a2f78u, "com/roblox/engine/jni/NativeGLJavaInterface",
      "promptNativePurchase", "(JLjava/lang/String;Ljava/lang/String;)V" },
    { 0x72a2f80u, "com/roblox/engine/jni/NativeGLJavaInterface",
      "promptNativePurchase", "(JLjava/lang/String;)V" },
    { 0x72a2f88u, "com/roblox/engine/jni/NativeGLJavaInterface",
      "promptNativePurchaseWithPayload", "(JLjava/lang/String;Ljava/lang/String;)V" },
    { 0x72a2f90u, "com/roblox/engine/jni/NativeGLJavaInterface",
      "promptNativePurchaseWithPaymentSessionId", "(JLjava/lang/String;Ljava/lang/String;Ljava/lang/String;)V" },
    { 0x72a2f98u, "com/roblox/engine/jni/NativeGLJavaInterface",
      "promptNativePurchaseWithPaymentSessionId", "(JLjava/lang/String;Ljava/lang/String;)V" },
    { 0x72a2fa0u, "com/roblox/engine/jni/NativeGLJavaInterface", "gameDidLeave", "()V" },
    { 0x72a2fa8u, "com/roblox/engine/jni/NativeGLJavaInterface", "screenOrientationChanged", "(I)V" },
    { 0x72a2fb0u, "com/roblox/engine/jni/NativeGLJavaInterface",
      "openNativeOverlay", "(Ljava/lang/String;Ljava/lang/String;)V" },
    { 0x72a2fb8u, "com/roblox/engine/jni/NativeGLJavaInterface",
      "onDataModelNotificationCallback", "(Ljava/lang/String;Ljava/lang/String;)V" },
    { 0x72a2fc0u, "com/roblox/engine/jni/NativeGLJavaInterface", "gameLoadedCallback", "(J)V" },
    { 0x72a2fc8u, "com/roblox/engine/jni/NativeGLJavaInterface",
      "onLuaTextBoxChangedCallback", "(Ljava/lang/String;)V" },
    { 0x72a2fd0u, "com/roblox/engine/jni/NativeGLJavaInterface",
      "onLuaTextBoxPropertyChangedCallback", "()V" },
    { 0x72a2fd8u, "com/roblox/engine/jni/NativeGLJavaInterface",
      "saveImageToAlbum", "(Ljava/lang/String;)V" },
    { 0x72a2fe0u, "com/roblox/engine/jni/NativeGLJavaInterface", "onVrSessionStateUpdate", "(I)V" },
    { 0x72a3018u, "com/roblox/engine/jni/NativeGLJavaInterface", "exitGameWithError", "(I)V" },
    { 0x72a3020u, "com/roblox/engine/jni/NativeGLJavaInterface",
      "onAppShellReloadNeeded", "()V" },
    { 0x72a3028u, "com/roblox/engine/jni/NativeGLJavaInterface",
      "onAppBridgeNotification", "(Ljava/lang/String;Ljava/lang/String;)V" },
    { 0x72a3030u, "com/roblox/engine/jni/NativeGLJavaInterface",
      "getWebViewUserAgent", "()Ljava/lang/String;" },
    { 0x72a3038u, "com/roblox/engine/jni/NativeGLJavaInterface",
      "getMobileAdvertisingId", "()Ljava/lang/String;" },
    { 0x72a3040u, "com/roblox/engine/jni/NativeGLJavaInterface",
      "onExtendedAnalyticsRecvCallback", "([BI)V" },
    { 0x72a2f60u, "com/roblox/engine/jni/NativeGLJavaInterface",
      "listenToMotionEvents", "(Ljava/lang/String;)V" },
    { 0x72ca188u, "com/roblox/engine/jni/locale/NativeLocaleJavaInterface",
      "getRobloxLocale", "()Ljava/lang/String;" },
    { 0x72ca190u, "com/roblox/engine/jni/locale/NativeLocaleJavaInterface",
      "getGameLocale", "()Ljava/lang/String;" },
};

SR_API int sober_jni_seed_guest_jni_state(SoberJni *j, void *guest_base)
{
    if (!j || !guest_base) return SOBER_JNI_ERR;
    w21_init((uintptr_t)guest_base);   /* W21 read-only exec watcher (opt-in) */
    int seeded = 0;

    /* Named entries first (worker-1 jni-map §1.2). */
    for (size_t i = 0; i < sizeof(sr_cached_methods) / sizeof(sr_cached_methods[0]); ++i) {
        const SrCachedMethod *cm = &sr_cached_methods[i];
        SrObject *clazz = sr_find_class_object(j, cm->class_name);
        SrObject *m = sr_find_method_object(j, clazz, cm->method, cm->sig);
        if (!m) continue;
        void **slot = (void **)((unsigned char *)guest_base + cm->rva);
        *slot = m;
        seeded++;
    }

    /* 0x72a2f48 is a CACHED jclass (NewGlobalRef(FindClass(NativeGLJavaInterface))),
     * NOT a jmethodID.  The generic range fill below would otherwise seed it
     * with a synthetic method object; the notifier 0x27eaf98 loads it into RSI
     * as the class argument of the guest thunk 0x25848e0.  Seed it first with
     * a real class object so the range fill leaves it alone. */
    {
        void **slot = (void **)((unsigned char *)guest_base + 0x72a2f48u);
        SrObject *clazz = sr_find_class_object(
            j, "com/roblox/engine/jni/NativeGLJavaInterface");
        if (clazz) { *slot = clazz; seeded++; }
    }

    /* Fill the complete cached-ID ranges worker-1/lead confirmed.  Unknown
     * slots get a generic synthetic method object; never leave NULL, because
     * the startup path calls through every cached ID. */
    static const uintptr_t ranges[][2] = {
        { 0x72a2f48u, 0x72a3058u },
        { 0x72ca178u, 0x72ca1e0u },
    };
    /* W20 approved host-side defect removal (ledger: "harness seeder clobbered
     * guest FFlag ctx global; defect removal").  The first range used to end
     * INCLUSIVELY at 0x72a3058, which is ALSO the guest FFlag context global
     * consumed at 0x23232ab -> 0x23115c8.  Seeding it with a synthetic
     * "cachedMethod@72a3058" object made genuine guest code read host memory
     * and produce RSI=0x30.  Default behaviour now EXCLUDES that one slot
     * (Run A).  SOBER_SEEDER_DISABLE=1 disables the whole cached-ID seeder
     * (Run B); default off.  No guest byte is patched and no gate is touched.
     */
    const int seeder_disabled =
        getenv("SOBER_SEEDER_DISABLE") != NULL;   /* any presence disables */
    if (!seeder_disabled) {
      for (size_t r = 0; r < sizeof(ranges) / sizeof(ranges[0]); ++r) {
        for (uintptr_t a = ranges[r][0]; a <= ranges[r][1]; a += 8u) {
            if (a == 0x72a3058u) continue;      /* guest FFlag ctx global */
            void **slot = (void **)((unsigned char *)guest_base + a);
            if (*slot == NULL) {
                char name[64];
                snprintf(name, sizeof(name), "cachedMethod@%lx", (unsigned long)a);
                void *v = sr_find_method_object(j, NULL, name, "()V");
                *slot = v;
                seeded++;
                /* W20 diagnostic: log every first-range slot we write. */
                if (r == 0u)
                    fprintf(stderr,
                            "[SEEDER_WRITE] rva=0x%lx value=%p\n",
                            (unsigned long)a, v);
            }
        }
      }
    } else {
      fprintf(stderr, "[SEEDER_WRITE] SOBER_SEEDER_DISABLE: cached-ID seeder "
                      "fully disabled\n");
    }

    /* Both JNI_OnLoad completion flags (worker-1/lead decision 013). */
    *(uint32_t *)((unsigned char *)guest_base + 0x72a3000u) = 1u;
    *(uint32_t *)((unsigned char *)guest_base + 0x7d8fb60u) = 1u;
    sr_add_log(j, "seeded %d guest cached jmethodIDs (2 ranges) + OnLoad flags", seeded);
    fprintf(stderr,
            "[sober-jni] seeded %d guest cached jmethodIDs (0x72a2f48-0x72a3058, "
            "0x72ca178-0x72ca1e0), flags 0x72a3000/0x7d8fb60 = 1\n", seeded);

    /* Phase 7 / task #449: the skipped JNI_OnLoad also never cached the
     * JavaVM pointer that the string helper 0x227b9e2 needs.  The opt-in
     * seed is a separate API (sober_jni_seed_guest_onload_globals) that the
     * harness wires explicitly behind SOBER_SEED_JNI_VM. */

    /* Phase 8 note: the *_default modules are installed by the harness
     * (guest_app_shell.c) after mapping/relocation, in their documented order
     * (substrate -> flags/scheduler -> singleton/render -> app+0x488/delegate/
     * controller -> placeId/publish).  native_jni.c deliberately does NOT
     * install them here: doing so would pre-empt those idempotent installers
     * with incomplete context (notably render_guest's real config).  The
     * JNIEnv-slot defaults and the provenance hook are wired in this file. */
    return SOBER_JNI_OK;
}

/* ---- opt-in JavaVM global seed (phase 7 / task #449) -------------------
 * Reverse-engineered construction path (build 204ad049):
 *   0x227af6e  JNI_OnLoad saves its first argument (JavaVM*) into RBX
 *   0x227af7f  calls 0x227b8fb with RDI = that JavaVM*
 *   0x227b912  CAS-guarded init; 0x227b946 installs the JNIInvokeInterface_
 *              vtable (function ptrs at 0x7cdec38/+0x18 .. 0x7cdec58/+0x38)
 *              into the object; 0x227b92d stores it in global 0x7cdec78.
 * Consumer 0x227b9e2: GetEnv(vm, out, 0x10006) via vtable +0x30, then the
 * caller at 0x2eb4b65 issues JNIEnv NewStringUTF via vtable +0x538.  The
 * EVERSION fallback goes through 0x2ea6440 / 0x227aecc.  Our JavaVM has the
 * exact JNIInvokeInterface_ slot order, so this is a faithful seed; there is
 * no need to fabricate the guest's scheduler-gated vtable wrappers, and no
 * guest->Java callback is emitted or triggered by this function. */
SR_API int sober_jni_seed_guest_onload_globals(SoberJni *j, void *guest_base)
{
    const uintptr_t vm_global_rva = 0x7cdec78u;

    if (!j || !guest_base) return SOBER_JNI_ERR;
    if (!getenv("SOBER_SEED_JNI_VM")) return SOBER_JNI_OK;   /* default OFF */

    void *vm = sober_jni_java_vm(j);
    if (!vm) return SOBER_JNI_ERR;
    void **slot = (void **)((unsigned char *)guest_base + vm_global_rva);
    void *old = *slot;
    *slot = vm;
    fprintf(stderr,
            "[sober-jni] seeded guest JavaVM global 0x%lx -> %p (was %p): "
            "GetEnv(+0x30)/Attach(+0x20) resolve to our JNIEnv; no callbacks\n",
            (unsigned long)vm_global_rva, vm, old);
    sr_add_log(j, "seeded guest JavaVM global 0x%lx", (unsigned long)vm_global_rva);
    return SOBER_JNI_OK;
}

SR_API uint64_t sober_jni_env_call_count(SoberJni *j) { return j ? j->env_calls : 0; }
SR_API uint64_t sober_jni_callback_count(SoberJni *j) { return j ? j->callback_calls : 0; }
SR_API uint64_t sober_jni_register_natives_count(SoberJni *j) { return j ? j->register_calls : 0; }
SR_API const char *sober_jni_last_callback(SoberJni *j)
{ return j && j->last_callback[0] ? j->last_callback : ""; }

SR_API int sober_jni_callback_was_called(SoberJni *j, const char *method_name)
{
    if (!j || !method_name) return 0;
    for (int i = 0; i < j->n_handlers; ++i) {
        SrHandler *h = &j->handlers[i];
        if (h->fn && h->name && !strcmp(h->name, method_name))
            return h->count > 0;
    }
    return 0;
}

/* ---- Phase 8: positive provenance credential for the decision-013 gate ---
 * The name-only counter above cannot distinguish a genuine notifier-driven
 * callback from a harness/ALT thunk.  These wrappers expose the default-ON,
 * fail-closed provenance_gate result to the gate owner.  A thunk taint
 * anywhere in the run forces WITHHELD.  Neither wrapper emits a callback nor
 * writes a marker; the gate alone decides whether to emit. */
SR_API int sober_jni_is_genuine_callback(const char *name)
{
    if (sober_provenance_gate_is_harness_thunk()) return 0;
    return sober_provenance_gate_is_genuine(name);
}

SR_API int sober_jni_is_harness_thunk(void)
{
    return sober_provenance_gate_is_harness_thunk();
}

/* Expose the default allocator ops table so the bootstrap can bind its eight
 * bridges + calloc slot (0x726c0c0) without installing alloc_contract_fix. */
SR_API const struct sober_alloc_default_ops *sober_jni_default_alloc_ops(void)
{
    return sober_alloc_default_ops();
}

SR_API const char *sober_jni_describe_activity(SoberJni *j, char *buf, size_t cap)
{
    if (!buf || !cap) return "";
    if (!j) { buf[0] = '\0'; return buf; }
    snprintf(buf, cap,
             "env_calls=%llu callbacks=%llu register_natives=%llu last=%s",
             (unsigned long long)j->env_calls,
             (unsigned long long)j->callback_calls,
             (unsigned long long)j->register_calls,
             j->last_callback);
    return buf;
}

/* Opt-in caller-side substitute (Phase 4c): a shaped JNI object handle whose
 * backing A has magic 0xc0dedbad at +0, +0x20/+0x68 zero, and >=0x1200 bytes
 * from the unified glibc allocator family (host_calloc/host_allocate). */
SR_API void *sober_jni_safe_app_shell_handle(void)
{
    SoberJni *j = g_active_jni;
    if (!j) return NULL;
    return sr_find_class_object(j, "com/roblox/engine/jni/NativeGLJavaInterface");
}

/* Decision 038: a genuinely FRESH handle for the real app-shell path.
 * Unlike sober_jni_safe_app_shell_handle(), this never reuses a cached class
 * from j->classes and never aliases guest global 0x72293f8 (__stack_chk_guard
 * GOT). Each call builds a new self-consistent A/B/C/D graph. */
SR_API void *sober_jni_fresh_handle(void)
{
    SoberJni *j = g_active_jni;
    if (!j) return NULL;
    SrObject *o = sr_new_object(j, SR_K_OBJECT,
                                "com/roblox/engine/jni/NativeGLJavaInterface",
                                NULL, "java/lang/Object");
    return o;
}
