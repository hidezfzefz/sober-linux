/*
 * guest_dlopen_host.c - Native, instrumented dlopen harness for the Sober guest.
 *
 * Canonical init ordering (lead's DECISION 008):
 *   (1) dlopen a byte-identical memfd copy with DT_INIT_ARRAYSZ=0 (no ctors),
 *   (2) find the guest load bias with dl_iterate_phdr,
 *   (3) apply the Android packed DT_ANDROID_RELA relocations (src/linux/),
 *   (4) worker-1's bootstrap: verify build, seed data, install bridges,
 *   (5) drive init_array constructor ranges [2,3) and [5,3618),
 *   (6) emit the deterministic "guest init complete" signal.
 *
 * The engine's on-disk bytes are never written: the only delta in the private
 * copy is the loader metadata tag DT_INIT_ARRAYSZ, neutralised so that a native
 * bootstrap can take control of init ordering. Code and data are untouched.
 *
 * Owned by worker-3 (task #377).
 */
#define _GNU_SOURCE
#include "android_relocs.h"
#include "android_compat.h"
#include "guest_render.h"
#include "guest_app_shell.h"

#include <dlfcn.h>
#include <execinfo.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <link.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <ucontext.h>
#include <unistd.h>

/* Mirrors src/compat/bionic_bootstrap/bionic_bootstrap.h (worker-1). Declared
 * weak+dlsym so the harness builds before/independently of that library and
 * can still report a precise error if it is not present. */
extern int sober_bionic_bootstrap_verify_build(void *guest_base)
    __attribute__((weak));
extern int sober_bionic_bootstrap_apply(void *guest_base)
    __attribute__((weak));
extern int sober_bionic_bootstrap_replay_constructors(void *guest_base)
    __attribute__((weak));
extern volatile unsigned sober_bootstrap_ctors_done __attribute__((weak));
extern volatile unsigned long long sober_bootstrap_ctors_current_rva
    __attribute__((weak));
extern volatile unsigned sober_bootstrap_seed_index __attribute__((weak));
extern const char *sober_bionic_bootstrap_last_error(void) __attribute__((weak));
extern long sober_jni_ring_dump_async(int fd) __attribute__((weak));
extern long sober_javavm_ring_dump_async(int fd) __attribute__((weak));
extern long sober_n1_ring_dump_async(int fd) __attribute__((weak));
extern void sober_jni_bridge_ptrs(uintptr_t *env, uintptr_t *vm)
    __attribute__((weak));

/* ------------------------------------------------------------------ */
/* globals used by the crash handler (only volatile / async-safe data)  */
/* ------------------------------------------------------------------ */
static volatile uintptr_t   g_guest_base;
static volatile size_t      g_guest_size;
static volatile unsigned    g_ctor_index;       /* index about to be called  */
static volatile uintptr_t   g_ctor_rva;
static volatile uintptr_t   g_init_array_addr;
static volatile size_t      g_init_array_count;
static volatile int         g_in_constructors;
static char                 g_fd_path[64];
static pthread_t            g_main_thread;
unsigned long               g_alloc_site_hits = 0;   /* phase-4b stub counter */
volatile unsigned long      g_handle_log[4] = {0,0,0,0};  /* phase-4b handle log */

static void emit(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > 0) {
        ssize_t unused = write(STDERR_FILENO, buf, (size_t)n);
        (void)unused;
    }
}

/* ------------------------------------------------------------------ */
/* crash instrumentation                                               */
/* ------------------------------------------------------------------ */
/* ---- W23 async-signal-safe crash dump ---------------------------------- *
 * The handler uses ONLY write(2) into a fixed stack buffer plus raw ucontext
 * reads: no printf/vsnprintf, no malloc, no backtrace/dladdr/opendir/getenv,
 * no strsignal.  libc base is precomputed at install time for offset labeling.
 */
static volatile uintptr_t g_libc_lo, g_libc_hi;
static void w24_put(char **p, const char *s)
{
    while (*s) *(*p)++ = *s++;
}
static void w24_u64(char **p, uint64_t v, int with_prefix)
{
    static const char hx[] = "0123456789abcdef";
    if (with_prefix) { *(*p)++ = '0'; *(*p)++ = 'x'; }
    for (int i = 60; i >= 0; i -= 4) *(*p)++ = hx[(v >> i) & 0xfu];
}
static void w24_num(char **p, long v)
{
    char tmp[24]; int n = 0;
    unsigned long u = v < 0 ? (unsigned long)(-v) : (unsigned long)v;
    if (v < 0) *(*p)++ = '-';
    if (!u) *(*p)++ = '0';
    while (u) { tmp[n++] = (char)('0' + (u % 10)); u /= 10; }
    while (n) *(*p)++ = tmp[--n];
}
static void w24_line(char **p, char *base, char *end)
{
    *(*p)++ = '\n';
    if (*p > end - 256) {
        ssize_t u = write(STDERR_FILENO, base, (size_t)(*p - base));
        (void)u; *p = base;
    }
}

/* W30: the ONLY way the handler touches memory that may belong to the guest
 * is process_vm_readv (a raw syscall).  No direct dereference of any
 * guest-derived pointer (RIP, RSP, RBP, RDI, FP chain) is permitted: a fault in
 * that read previously masked the primary fault (see w29 core.1529212). */
static long w30_readmem(uintptr_t addr, void *dst, size_t len)
{
    struct iovec li = { (void *)dst, len };
    struct iovec ri = { (void *)addr, len };
    return syscall(SYS_process_vm_readv, (long)getpid(),
                   &li, 1UL, &ri, 1UL, 0UL);
}

/* n4 HOST-ONLY: /proc/self/maps reader using only open/read/close/write.
 * No stdio, no malloc — safe inside the signal handler.  Prints the whole
 * maps line for an address plus addr - mapping_start as the offset. */
static unsigned long w40_hexval(const char **pp)
{
    const char *q = *pp;
    unsigned long v = 0;
    while (*q) {
        unsigned d;
        if (*q >= '0' && *q <= '9') d = (unsigned)(*q - '0');
        else if (*q >= 'a' && *q <= 'f') d = (unsigned)(*q - 'a' + 10);
        else if (*q >= 'A' && *q <= 'F') d = (unsigned)(*q - 'A' + 10);
        else break;
        v = (v << 4) | d; ++q;
    }
    *pp = q;
    return v;
}
static void w40_maps(char **p, char *base, char *end, uintptr_t addr,
                     const char *tag)
{
    static char maps[65536];
    int fd = open("/proc/self/maps", O_RDONLY);
    if (fd < 0) return;
    size_t total = 0;
    while (total < sizeof(maps) - 1u) {
        ssize_t n = read(fd, maps + total, sizeof(maps) - 1u - total);
        if (n <= 0) break;
        total += (size_t)n;
    }
    close(fd);
    maps[total] = '\0';
    w24_put(p, tag); w24_u64(p, addr, 1); w24_put(p, " ");
    const char *ln = maps, *stop = maps + total;
    int found = 0;
    while (ln < stop) {
        const char *nl = ln;
        while (nl < stop && *nl != '\n') ++nl;
        const char *q = ln;
        unsigned long lo = w40_hexval(&q);
        if (*q == '-') { ++q; unsigned long hi = w40_hexval(&q);
            if (addr >= lo && addr < hi) {
                w24_put(p, "offset=0x"); w24_u64(p, (uint64_t)(addr - lo), 0);
                w24_put(p, " maps='");
                for (const char *c = ln; c < nl && *p < end - 2u; ++c)
                    *(*p)++ = *c;
                w24_put(p, "'");
                found = 1;
            }
        }
        if (found) break;
        ln = (nl < stop) ? nl + 1 : stop;
    }
    if (!found) w24_put(p, "maps=NOT_FOUND");
    w24_line(p, base, end);
}

static void crash_handler(int sig, siginfo_t *si, void *ucontext)
{
    alarm(5);   /* bound the dump itself; SA_RESETHAND prevents recursion */
    char buf[32768], *p = buf, *end = buf + sizeof(buf) - 1;
    ucontext_t *uc = (ucontext_t *)ucontext;
    uintptr_t base = g_guest_base;
    uintptr_t fault = (uintptr_t)(si ? si->si_addr : NULL);
    uintptr_t rip = uc ? (uintptr_t)uc->uc_mcontext.gregs[REG_RIP] : 0;
    uintptr_t rsp = uc ? (uintptr_t)uc->uc_mcontext.gregs[REG_RSP] : 0;
    uintptr_t rbp = uc ? (uintptr_t)uc->uc_mcontext.gregs[REG_RBP] : 0;

    /* W30 FIRST LINE: RIP, si_addr, rsp, rbp, instruction bytes at RIP. */
    unsigned char ib[16];
    long ib_n = (rip >= 0x10000u) ? w30_readmem(rip, ib, sizeof(ib)) : -1;
    w24_put(&p, "[W30_FAULT] rip="); w24_u64(&p, rip, 1);
    w24_put(&p, " si_addr="); w24_u64(&p, fault, 1);
    w24_put(&p, " rsp="); w24_u64(&p, rsp, 1);
    w24_put(&p, " rbp="); w24_u64(&p, rbp, 1);
    w24_put(&p, " insn=");
    if (ib_n >= 8) {
        for (int k = 0; k < 8; ++k) {
            w24_u64(&p, ib[k], 0); w24_put(&p, " ");
        }
    } else w24_put(&p, "UNREADABLE");
    w24_line(&p, buf, end);

    w24_put(&p, "[W23_ASYNC_SAFE] ===== GUEST FAULT =====\n");
    w24_put(&p, "signal="); w24_num(&p, sig);
    w24_put(&p, " tid="); w24_num(&p, (long)syscall(SYS_gettid));
    w24_put(&p, " rip="); w24_u64(&p, rip, 1);
    w24_put(&p, " si_addr="); w24_u64(&p, fault, 1);
    w24_put(&p, " rsp="); w24_u64(&p, rsp, 1); w24_line(&p, buf, end);
    w24_put(&p, "[W28_FAULT_INSN] rip="); w24_u64(&p, rip, 1);
    if (base && rip >= base && rip < base + g_guest_size) {
        w24_put(&p, " guest_rva=0x");
        for (int sh = 28; sh >= 0; sh -= 4)
            if (p < end) *p++ = "0123456789abcdef"[((rip - base) >> sh) & 0xfu];
    }
    w24_line(&p, buf, end);
    w24_put(&p, "[W28_INSN_BYTES] ");
    if (ib_n > 0) {
        for (long k = 0; k < ib_n; ++k) { w24_u64(&p, ib[k], 0); w24_put(&p, " "); }
    } else w24_put(&p, "UNREADABLE");
    w24_line(&p, buf, end);
    if (uc) {
        static const struct { const char *n; int g; } regs[] = {
            {"r15",REG_R15},{"r14",REG_R14},{"r13",REG_R13},{"r12",REG_R12},
            {"rbp",REG_RBP},{"rbx",REG_RBX},{"r11",REG_R11},{"r10",REG_R10},
            {"r9",REG_R9},{"r8",REG_R8},{"rax",REG_RAX},{"rcx",REG_RCX},
            {"rdx",REG_RDX},{"rsi",REG_RSI},{"rdi",REG_RDI},{"rsp",REG_RSP},
            {"rip",REG_RIP}
        };
        for (unsigned i = 0; i < sizeof(regs)/sizeof(regs[0]); ++i) {
            w24_put(&p, " "); w24_put(&p, regs[i].n); w24_put(&p, "=");
            w24_u64(&p, (uint64_t)uc->uc_mcontext.gregs[regs[i].g], 1);
            if ((i & 3u) == 3u) w24_line(&p, buf, end);
        }
        w24_line(&p, buf, end);
    }

    /* 256 stack qwords, read with process_vm_readv only (never deref). */
    w24_put(&p, "[W23_STACK_SCAN] rsp_base="); w24_u64(&p, rsp, 1);
    w24_line(&p, buf, end);
    for (unsigned i = 0; i < 256u; ++i) {
        uintptr_t v = 0;
        long got = rsp ? w30_readmem(rsp + (size_t)i * 8u, &v, 8u) : -1;
        w24_put(&p, "["); w24_num(&p, (long)i); w24_put(&p, "]=");
        if (got != 8) { w24_put(&p, "UNREADABLE"); }
        else if (base && v >= base && v < base + 0x8000000u) {
            w24_put(&p, "guest+0x"); w24_u64(&p, v - base, 0);
        } else if (g_libc_lo && v >= g_libc_lo && v < g_libc_hi) {
            w24_put(&p, "libc+0x"); w24_u64(&p, v - g_libc_lo, 0);
        } else w24_u64(&p, v, 1);
        if ((i & 3u) == 3u) w24_line(&p, buf, end);
    }
    w24_line(&p, buf, end);

    /* FP chain, read with process_vm_readv only. */
    w24_put(&p, "[W23_FP] from rbp="); w24_u64(&p, rbp, 1); w24_line(&p, buf, end);
    uintptr_t fp = rbp;
    for (unsigned i = 0; i < 24u && fp >= 0x10000u; ++i) {
        uintptr_t ret = 0, nxt = 0;
        long gr = w30_readmem(fp + 8u, &ret, 8u);
        long gn = w30_readmem(fp, &nxt, 8u);
        w24_put(&p, " fp#"); w24_num(&p, (long)i); w24_put(&p, "=");
        if (gr != 8) w24_put(&p, "UNREADABLE");
        else if (base && ret >= base && ret < base + 0x8000000u) {
            w24_put(&p, "guest+0x"); w24_u64(&p, ret - base, 0);
        } else w24_u64(&p, ret, 1);
        w24_line(&p, buf, end);
        if (gn != 8 || nxt <= fp || nxt > fp + 0x100000u) break;
        fp = nxt;
    }

    /* W30 stack-return dump when si_addr is within 64 KiB of rsp. */
    long delta = (long)fault - (long)rsp;
    if (delta < 0) delta = -delta;
    w24_put(&p, "[W30_SI_RSP_DELTA] "); w24_num(&p, delta);
    w24_line(&p, buf, end);
    if (delta >= 0 && delta < 65536u) {
        w24_put(&p, "[W30_RET64] rsp="); w24_u64(&p, rsp, 1);
        w24_line(&p, buf, end);
        uintptr_t ret64_first[3]; unsigned nret64 = 0;
        for (unsigned i = 0; i < 64u; ++i) {
            uintptr_t v = 0;
            long got = w30_readmem(rsp + (size_t)i * 8u, &v, 8u);
            w24_put(&p, "["); w24_num(&p, (long)i); w24_put(&p, "]=");
            if (got != 8) w24_put(&p, "UNREADABLE");
            else if (base && v >= base && v < base + 0x8000000u) {
                w24_put(&p, "guest+0x"); w24_u64(&p, v - base, 0);
            } else w24_u64(&p, v, 1);
            if (got == 8 && nret64 < 3u) ret64_first[nret64++] = v;
            if ((i & 3u) == 3u) w24_line(&p, buf, end);
        }
        w24_line(&p, buf, end);
        for (unsigned k = 0; k < nret64; ++k) {
            char tg[32];
            w24_put(&p, "[W40_RET64_MAP #"); w24_num(&p, (long)k);
            w24_put(&p, "] "); w24_line(&p, buf, end);
            w40_maps(&p, buf, end, ret64_first[k], "addr");
        }
    }
    /* RIP mapping line + offset */
    w40_maps(&p, buf, end, rip, "[W40_RIP_MAP] rip");

    /* Fault args: raw values only; RDI bytes via process_vm_readv. */
    uintptr_t rdi = uc ? (uintptr_t)uc->uc_mcontext.gregs[REG_RDI] : 0;
    uintptr_t rax = uc ? (uintptr_t)uc->uc_mcontext.gregs[REG_RAX] : 0;
    w24_put(&p, "[W25_FAULT_ARGS] rdi="); w24_u64(&p, rdi, 1);
    w24_put(&p, " rax="); w24_u64(&p, rax, 1); w24_line(&p, buf, end);
    unsigned char rb[16];
    long rn = (rdi >= 0x10000u) ? w30_readmem(rdi, rb, sizeof(rb)) : -1;
    w24_put(&p, "[W25_RDI16] ");
    if (rn > 0) { for (long k = 0; k < rn; ++k) { w24_u64(&p, rb[k], 0); w24_put(&p, " "); } }
    else w24_put(&p, "UNREADABLE");
    w24_line(&p, buf, end);
    uintptr_t benv = 0, bvm = 0;
    if (sober_jni_bridge_ptrs) sober_jni_bridge_ptrs(&benv, &bvm);
    w24_put(&p, "[W25_BRIDGE] env="); w24_u64(&p, benv, 1);
    w24_put(&p, " vm="); w24_u64(&p, bvm, 1);
    w24_put(&p, " rdi_is_env="); w24_u64(&p, rdi && rdi == benv ? 1 : 0, 0);
    w24_line(&p, buf, end);
    if (rsp) {
        unsigned ne = 0, nv = 0;
        for (unsigned i = 0; i < 256u; ++i) {
            uintptr_t v = 0;
            if (w30_readmem(rsp + (size_t)i * 8u, &v, 8u) != 8) continue;
            if (benv && v == benv) ++ne;
            if (bvm && v == bvm) ++nv;
        }
        w24_put(&p, "[W25_STACK_ENV] env_hits="); w24_u64(&p, ne, 0);
        w24_put(&p, " vm_hits="); w24_u64(&p, nv, 0);
        w24_line(&p, buf, end);
    }
    ssize_t u = write(STDERR_FILENO, buf, (size_t)(p - buf));
    (void)u;
    /* Ring dumps are host-internal fixed buffers, never guest pointers. */
    if (sober_jni_ring_dump_async) sober_jni_ring_dump_async(STDERR_FILENO);
    if (sober_javavm_ring_dump_async) sober_javavm_ring_dump_async(STDERR_FILENO);
    if (sober_n1_ring_dump_async) sober_n1_ring_dump_async(STDERR_FILENO);
    _exit(139);
}

static void crash_handler_legacy_unused(int sig, siginfo_t *si, void *ucontext)
{
    (void)sig; (void)si; (void)ucontext;
    ucontext_t *uc = (ucontext_t *)ucontext;
    uintptr_t rip = 0, rsp = 0, rbp = 0, rdi = 0, r14 = 0;
    if (uc) {
        rip = (uintptr_t)uc->uc_mcontext.gregs[REG_RIP];
        rsp = (uintptr_t)uc->uc_mcontext.gregs[REG_RSP];
        rbp = (uintptr_t)uc->uc_mcontext.gregs[REG_RBP];
        rdi = (uintptr_t)uc->uc_mcontext.gregs[REG_RDI];
        r14 = (uintptr_t)uc->uc_mcontext.gregs[REG_R14];
    }
    uintptr_t base = g_guest_base;
    uintptr_t fault = (uintptr_t)(si ? si->si_addr : NULL);

    emit("\n[harness] ===== GUEST FAULT =====\n");
    emit("[harness] regs: rdi=%p r14=%p rbp=%p rsp=%p\n",
         (void *)rdi, (void *)r14, (void *)rbp, (void *)rsp);
    emit("[harness] signal=%d (%s) si_addr=%p rip=%p rsp=%p rbp=%p\n",
         sig, strsignal(sig), (void *)fault, (void *)rip, (void *)rsp,
         (void *)rbp);
    if (uc) {
        emit("[harness] rdi=%p rsi=%p rdx=%p rcx=%p rax=%p\n",
             (void *)uc->uc_mcontext.gregs[REG_RDI],
             (void *)uc->uc_mcontext.gregs[REG_RSI],
             (void *)uc->uc_mcontext.gregs[REG_RDX],
             (void *)uc->uc_mcontext.gregs[REG_RCX],
             (void *)uc->uc_mcontext.gregs[REG_RAX]);
    }

    /* OPT-IN handle/slot discriminator (lead's request). Never dereference a
     * non-canonical value from inside the signal handler. */
    if (getenv("SOBER_DUMP_HANDLE")) {
        emit("[harness] DUMP alloc-site stub hits = %lu\n", g_alloc_site_hits);
        emit("[harness] DUMP handle log: obj=%p first_dword=0x%08lx "
             "plus20=%p plus68_raw=%p\n",
             (void *)g_handle_log[0], (unsigned long)(uint32_t)g_handle_log[1],
             (void *)g_handle_log[2], (void *)g_handle_log[3]);
        if (base) {
            uintptr_t slot = *(uintptr_t *)(base + 0x726c0c0u);
            emit("[harness] DUMP calloc slot @0x726c0c0 = %p", (void *)slot);
            Dl_info sdi;
            if (dladdr((void *)slot, &sdi) && sdi.dli_fname)
                emit(" (%s+0x%lx)", sdi.dli_fname,
                     (unsigned long)(slot - (uintptr_t)sdi.dli_fbase));
            emit("\n");
        }
        uintptr_t h = uc ? (uintptr_t)uc->uc_mcontext.gregs[REG_RDI] : 0;
        int canonical = (h < 0x0000800000000000ULL) ||
                        (h >= 0xffff800000000000ULL);
        if (!canonical || !h) {
            emit("[harness] DUMP rdi=%p is non-canonical/NULL -- it IS the "
                 "stale field value; cannot dereference\n", (void *)h);
        } else {
            Dl_info hdi;
            if (!dladdr((void *)h, &hdi)) {
                emit("[harness] DUMP rdi=%p not mapped; cannot dereference\n",
                     (void *)h);
            } else {
                emit("[harness] DUMP handle %p: +0x20=%p +0x68=%p "
                     "bytes@+0x68=", (void *)h,
                     *(void **)(h + 0x20), *(void **)(h + 0x68));
                const unsigned char *p = (const unsigned char *)(h + 0x68);
                for (int i = 0; i < 8; ++i) emit("%02x", p[i]);
                emit("\n");
                for (int i = 0; i < 16; ++i)
                    emit("[harness] DUMP +0x%02x = %p\n", i * 8,
                         *(void **)(h + i * 8));
            }
        }
    }
    {
        Dl_info di;
        if (dladdr((void *)rip, &di) && di.dli_fname)
            emit("[harness] rip maps to %s+0x%lx\n", di.dli_fname,
                 (unsigned long)(rip - (uintptr_t)di.dli_fbase));
        if (dladdr((void *)fault, &di) && di.dli_fname)
            emit("[harness] fault addr maps to %s+0x%lx\n", di.dli_fname,
                 (unsigned long)(fault - (uintptr_t)di.dli_fbase));
    }
    if (base) {
        static const struct { const char *n; uintptr_t rva; } watch[] = {
            {"allocator", 0x7834570}, {"arena_guard", 0x7cd8fb0},
            {"arena_table", 0x7cd9040}, {"empty_string", 0x79413b8},
            {"jni_singleton", 0x7d9cf40}, {"registry", 0x7cdd118},
            {"thread_state", 0x721db80}, {"tls_key", 0x7282000},
            {"alloc_flag", 0x783daa8},
            {"assert_flag1", 0x7cdd149}, {"assert_flag2", 0x7cef650},
            {"fflag_global", 0x72a0520},
        };
        for (size_t i = 0; i < sizeof(watch) / sizeof(watch[0]); ++i) {
            uintptr_t v = *(uintptr_t *)(base + watch[i].rva);
            emit("[harness] at fault %-14s @0x%lx = %p%s\n", watch[i].n,
                 (unsigned long)watch[i].rva, (void *)v, v ? "" : " <-- NULL");
        }
        /* Read-only wave11 gate snapshot at fault time. */
        emit("[harness] at fault gate_string hdr(CF0)=%#x len(D08)=%u "
             "flags_latch=%u guard_byte=%#x\n",
             (unsigned)*(volatile unsigned char *)(base + 0x7697cf0u),
             (unsigned)*(volatile unsigned char *)(base + 0x7697d08u),
             (unsigned)*(volatile unsigned char *)(base + 0x7cdd149u),
             (unsigned)*(volatile unsigned char *)(base + 0x23862b0u));
    }
    /* Read-only thread census at fault time (diagnostic, no guest writes). */
    {
        char path[256], comm[128], statbuf[512];
        long self_tid = (long)syscall(SYS_gettid);
        DIR *td = opendir("/proc/self/task");
        emit("[harness] THREADS_AT_FAULT self_tid=<marked below>\n");
        if (td) {
            struct dirent *de;
            while ((de = readdir(td)) != NULL) {
                if (de->d_name[0] < '0' || de->d_name[0] > '9') continue;
                snprintf(path, sizeof(path), "/proc/self/task/%s/comm", de->d_name);
                comm[0] = 0;
                FILE *f = fopen(path, "r");
                if (f) { if (fgets(comm, sizeof(comm), f)) comm[strcspn(comm, "\n")] = 0; fclose(f); }
                snprintf(path, sizeof(path), "/proc/self/task/%s/stat", de->d_name);
                statbuf[0] = 0;
                f = fopen(path, "r");
                if (f) { if (fgets(statbuf, sizeof(statbuf), f)) {
                    char *p = strrchr(statbuf, ')');
                    if (p) { p++; while (*p == ' ') p++; }
                    else p = statbuf;
                    statbuf[0] = *p ? *p : '?'; statbuf[1] = 0;
                } fclose(f); }
                char line[320];
                snprintf(line, sizeof(line),
                         "[harness] THREAD tid=%s%s name=%s state=%s\n",
                         de->d_name, atol(de->d_name) == self_tid ? " <FAULTING>" : "",
                         comm[0] ? comm : "?", statbuf);
                emit(line);
            }
            closedir(td);
        }
    }
    emit("[harness] worker-1 progress: seed_index=%u ctors_done=%u "
         "ctor_rva=0x%llx\n",
         (&sober_bootstrap_seed_index) ? (unsigned)sober_bootstrap_seed_index : 0u,
         (&sober_bootstrap_ctors_done) ? (unsigned)sober_bootstrap_ctors_done : 0u,
         (&sober_bootstrap_ctors_current_rva)
             ? (unsigned long long)sober_bootstrap_ctors_current_rva : 0ull);
    if (base && rip >= base && rip < base + g_guest_size)
        emit("[harness] faulting guest RVA: 0x%lx\n",
             (unsigned long)(rip - base));
    else if (base && fault >= base && fault < base + g_guest_size)
        emit("[harness] faulting guest data RVA: 0x%lx\n",
             (unsigned long)(fault - base));
    if (g_in_constructors) {
        /* worker-1's replay loop owns the authoritative progress counters. */
        unsigned done = (&sober_bootstrap_ctors_done)
                            ? (unsigned)sober_bootstrap_ctors_done
                            : g_ctor_index;
        unsigned long long cur = (&sober_bootstrap_ctors_current_rva)
                                     ? sober_bootstrap_ctors_current_rva
                                     : g_ctor_rva;
        emit("[harness] REPLAY PROGRESS: ctors completed=%u, faulting index=%u, "
             "ctor RVA=0x%llx (of %zu, init_array=%p)\n",
             done, done, cur, g_init_array_count, (void *)g_init_array_addr);
    }
    /* Diagnostic backtrace (caller frames already unwound by glibc). */
    void *frames[24];
    int nf = backtrace(frames, 24);
    for (int i = 0; i < nf; ++i) {
        Dl_info di;
        uintptr_t pc = (uintptr_t)frames[i];
        if (dladdr((void *)pc, &di) && di.dli_fname) {
            unsigned long off = (unsigned long)(pc - (uintptr_t)di.dli_fbase);
            emit("[harness]   #%d %s+0x%lx (pc=%p)\n", i, di.dli_fname, off,
                 (void *)pc);
        } else {
            emit("[harness]   #%d pc=%p\n", i, (void *)pc);
        }
    }
    /* Frame-pointer chain from the saved RBP: works across guest frames that
     * have no unwind tables, unlike backtrace() above. */
    emit("[harness] FP chain from rbp=%p:\n", (void *)rbp);
    for (int i = 0; i < 24 && rbp; ++i) {
        uintptr_t ret = *(uintptr_t *)(rbp + 8);
        uintptr_t nxt = *(uintptr_t *)rbp;
        if (base && ret >= base && ret < base + 0x8000000u)
            emit("[harness]   fp#%-2d rbp=%p ret=guest+0x%lx\n", i,
                 (void *)rbp, (unsigned long)(ret - base));
        else
            emit("[harness]   fp#%-2d rbp=%p ret=%p\n", i,
                 (void *)rbp, (void *)ret);
        if (nxt <= rbp || nxt > rbp + 0x100000u) break;
        rbp = nxt;
    }
    emit("[harness] =======================\n");
    _exit(128 + sig);
}

/* Watchdog: if a single constructor spins for too long, dump main's stack. */
static void watchdog_handler(int sig)
{
    (void)sig;
    emit("\n[harness] ===== WATCHDOG: constructor stuck =====\n");
    emit("[harness] progress: ctor_index=%u ctor_rva=0x%lx\n",
         g_ctor_index, (unsigned long)g_ctor_rva);
    void *frames[32];
    int nf = backtrace(frames, 32);
    for (int i = 0; i < nf; ++i) {
        Dl_info di;
        uintptr_t pc = (uintptr_t)frames[i];
        if (dladdr((void *)pc, &di) && di.dli_fname)
            emit("[harness]   #%d %s+0x%lx\n", i, di.dli_fname,
                 (unsigned long)(pc - (uintptr_t)di.dli_fbase));
        else
            emit("[harness]   #%d pc=%p\n", i, (void *)pc);
    }
    _exit(124);
}

static void *watchdog_thread(void *arg)
{
    (void)arg;
    struct timespec ts = { .tv_sec = 12, .tv_nsec = 0 };
    nanosleep(&ts, NULL);
    pthread_kill(g_main_thread, SIGUSR1);
    return NULL;
}

static int install_crash_handlers(void)
{
    /* SIGSTKSZ is dynamic with _GNU_SOURCE; use a fixed, generous buffer. */
    static char altstack[64 * 1024];
    stack_t ss = {0};
    ss.ss_sp = altstack;
    ss.ss_size = sizeof(altstack);
    ss.ss_flags = 0;
    if (sigaltstack(&ss, NULL) != 0) {
        perror("[harness] sigaltstack");
        return -1;
    }
    fprintf(stderr, "[W30_SIGALTSTACK] installed sp=%p size=%zu\n",
            (void *)altstack, sizeof(altstack));
    /* W23: precompute the libc mapping window for async-safe offset labels. */
    {
        Dl_info li;
        if (dladdr((void *)(uintptr_t)&write, &li) && li.dli_fbase) {
            g_libc_lo = (uintptr_t)li.dli_fbase;
            g_libc_hi = g_libc_lo + 0x400000u;   /* generous fixed window */
        }
    }
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = crash_handler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_NODEFER | SA_RESETHAND;
    sigemptyset(&sa.sa_mask);
    int signals[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGTRAP };
    size_t nsignals = sizeof(signals) / sizeof(signals[0]);
    /* Opt-in for worker-1's ptrace tracer: our SA_ONSTACK/SA_NODEFER
     * SIGTRAP handler swallows breakpoint traps. Skip it (and restore the
     * default) so gdb/ptrace breakpoints reach the tracer. Default unchanged. */
    int no_trap = getenv("SOBER_TRACE_NO_SIGTRAP_HANDLER") != NULL;
    if (no_trap) {
        nsignals--;   /* SIGTRAP is last in the array */
        struct sigaction dfl;
        memset(&dfl, 0, sizeof(dfl));
        dfl.sa_handler = SIG_DFL;
        sigemptyset(&dfl.sa_mask);
        sigaction(SIGTRAP, &dfl, NULL);
        fprintf(stderr, "[harness] SIGTRAP handler left at SIG_DFL "
                        "(SOBER_TRACE_NO_SIGTRAP_HANDLER)\n");
    }
    for (size_t i = 0; i < nsignals; ++i)
        if (sigaction(signals[i], &sa, NULL) != 0) return -1;

    struct sigaction wa;
    memset(&wa, 0, sizeof(wa));
    wa.sa_handler = watchdog_handler;
    wa.sa_flags = SA_RESTART;
    sigemptyset(&wa.sa_mask);
    sigaction(SIGUSR1, &wa, NULL);
    return 0;
}

/* ------------------------------------------------------------------ */
/* memfd copy + loader-metadata patch                                  */
/* ------------------------------------------------------------------ */
static int copy_guest_to_memfd(const char *path, int *out_fd,
                               uint8_t **out_bytes, size_t *out_size,
                               struct sober_guest_elf *out_elf)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror("[harness] fopen guest"); return -1; }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return -1; }
    long sz = ftell(f);
    if (sz <= 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return -1; }
    size_t size = (size_t)sz;
    uint8_t *bytes = malloc(size);
    if (!bytes) { fclose(f); return -1; }
    if (fread(bytes, 1, size, f) != size) {
        fprintf(stderr, "[harness] short read of guest\n");
        free(bytes); fclose(f); return -1;
    }
    fclose(f);

    if (sober_guest_elf_parse(bytes, size, out_elf) != 0) {
        fprintf(stderr, "[harness] guest is not the expected Android ELF64\n");
        free(bytes); return -1;
    }
    if (!out_elf->init_array_size || !out_elf->init_arraysz_file_off) {
        fprintf(stderr, "[harness] no DT_INIT_ARRAY metadata to patch\n");
        free(bytes); return -1;
    }

    int fd = memfd_create("sober-guest", MFD_CLOEXEC);
    if (fd < 0) { perror("[harness] memfd_create"); free(bytes); return -1; }
    size_t written = 0;
    while (written < size) {
        ssize_t n = write(fd, bytes + written, size - written);
        if (n <= 0) { perror("[harness] memfd write"); close(fd); free(bytes); return -1; }
        written += (size_t)n;
    }

    /* THE ONLY BYTE DELTA: DT_INIT_ARRAYSZ -> 0. No code or data is changed. */
    uint64_t zero = 0;
    if (pwrite(fd, &zero, sizeof(zero), out_elf->init_arraysz_file_off) !=
        (ssize_t)sizeof(zero)) {
        perror("[harness] DT_INIT_ARRAYSZ patch");
        close(fd); free(bytes); return -1;
    }
    memset(bytes + out_elf->init_arraysz_file_off, 0, sizeof(zero));
    fprintf(stderr,
        "[harness] memfd copy %zu bytes; only delta is DT_INIT_ARRAYSZ@%#lx "
        "(%zu -> 0)\n",
        size, (unsigned long)out_elf->init_arraysz_file_off,
        out_elf->init_array_size);

    *out_fd = fd;
    *out_bytes = bytes;
    *out_size = size;
    return 0;
}

/* ------------------------------------------------------------------ */
/* locate the guest load bias                                          */
/* ------------------------------------------------------------------ */
static int find_base_cb(struct dl_phdr_info *info, size_t sz, void *data)
{
    (void)sz;
    uintptr_t *base = data;
    const char *name = info->dlpi_name ? info->dlpi_name : "";
    if (strstr(name, "sober-guest") || strstr(name, g_fd_path)) {
        *base = (uintptr_t)info->dlpi_addr;
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* state snapshots (lead's three-point dump)                          */
/* ------------------------------------------------------------------ */
static void dump_state(uintptr_t base, const char *point)
{
    static const struct { const char *n; uintptr_t rva; } w[] = {
        {"arena_guard", 0x7cd8fb0},  {"arena_table", 0x7cd9040},
        {"arena_slot50", 0x7cd9050}, {"page_size", 0x7281fe0},
        {"clamp_cl", 0x7281f40},     {"pb_783d960", 0x783d960},
        {"pb_783d968", 0x783d968},   {"pb_783d970", 0x783d970},
        {"pb_783d980", 0x783d980},   {"pb_783d988", 0x783d988},
        {"pb_783daa8", 0x783daa8},   {"pb_783dab0", 0x783dab0},
    };
    fprintf(stderr, "[harness] STATE %s:\n", point);
    for (size_t i = 0; i < sizeof(w) / sizeof(w[0]); ++i) {
        uintptr_t v = *(uintptr_t *)(base + w[i].rva);
        fprintf(stderr, "[harness]   %-12s @0x%lx = %p%s\n", w[i].n,
                (unsigned long)w[i].rva, (void *)v, v ? "" : " <-- NULL");
    }
}

/* ------------------------------------------------------------------ */
/* constructor driving                                                 */
/* ------------------------------------------------------------------ */
static int drive_constructors(uintptr_t base,
                              const struct sober_guest_elf *elf,
                              int use_bootstrap_replay)
{
    (void)use_bootstrap_replay;  /* we always drive ourselves for instrumentation */
    g_init_array_addr = base + elf->init_array_rva;
    g_init_array_count = elf->init_array_count;
    g_in_constructors = 1;
    g_main_thread = pthread_self();
    if (getenv("SOBER_WATCHDOG")) {
        pthread_t wd;
        pthread_create(&wd, NULL, watchdog_thread, NULL);
        pthread_detach(wd);
    }

    const char *range_env = getenv("SOBER_CTOR_RANGES");
    int native_ranges = range_env && !strcmp(range_env, "native");
    int all_ranges = range_env && !strcmp(range_env, "all");
    uintptr_t *array = (uintptr_t *)g_init_array_addr;
    const unsigned std_ranges[][2] = { {2, 3}, {5, 3618} };
    const unsigned native_range[][2] = { {2, 3618} };
    const unsigned all_range[][2] = { {0, 3618} };
    const unsigned (*ranges)[2] = all_ranges ? all_range
                                  : (native_ranges ? native_range : std_ranges);
    size_t nranges = (native_ranges || all_ranges) ? 1 : 2;
    unsigned done = 0;

    for (size_t r = 0; r < nranges; ++r) {
        for (unsigned i = ranges[r][0]; i < ranges[r][1] && i < elf->init_array_count; ++i) {
            /* Lead's ACTION (b): protobuf once-guard ctor[1] before ctor[6]. */
            if (i == 6 && getenv("SOBER_CTOR0")) {
                fprintf(stderr, "[harness] injecting ctor[0] @0x2e62390\n");
                g_ctor_rva = 0x2e62390;
                ((void (*)(void))(base + 0x2e62390))();
                dump_state(base, "AFTER ctor[0] injection");
            }
            /* Diagnostic opt-in only: the canonical profile skips ctor[1].
             * Kept behind SOBER_CTOR1 for A/B comparison during triage. */
            if (i == 6 && !all_ranges && getenv("SOBER_CTOR1")) {
                dump_state(base, "BEFORE ctor[1] injection");
                fprintf(stderr, "[harness] injecting ctor[1] @0x1dba8f3 "
                                "(protobuf once-guard)\n");
                g_ctor_index = 1;
                g_ctor_rva = 0x1dba8f3;
                if (&sober_bootstrap_ctors_current_rva)
                    sober_bootstrap_ctors_current_rva = 0x1dba8f3;
                ((void (*)(void))(base + 0x1dba8f3))();
                dump_state(base, "AFTER ctor[1] injection");
            }
            if (i == 6) dump_state(base, "BEFORE ctor[6]");

            if ((native_ranges || all_ranges) && i == 3) {
                fprintf(stderr, "[harness] allocator thread init after ctor[2]\n");
                ((void (*)(void))(base + 0x1e47927))();
                dump_state(base, "AFTER ctor[2] + thread init");
            }
            uintptr_t fn = array[i];
            g_ctor_index = i;
            g_ctor_rva = fn ? fn - base : 0;
            fprintf(stderr, "[harness] ctor[%u] rva=0x%lx\n", i,
                    (unsigned long)g_ctor_rva);
            if (&sober_bootstrap_ctors_done)
                sober_bootstrap_ctors_done = done;
            if (&sober_bootstrap_ctors_current_rva)
                sober_bootstrap_ctors_current_rva = g_ctor_rva;
            if (!fn) {
                fprintf(stderr, "[harness] NULL constructor at index %u\n", i);
                g_in_constructors = 0;
                return -1;
            }
            ((void (*)(void))fn)();
            ++done;
            if (i == 2) dump_state(base, "AFTER ctor[2]");
        }
    }
    g_in_constructors = 0;
    return 0;
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */
int main(int argc, char **argv)
{
    const char *guest = (argc > 1) ? argv[1]
                        : getenv("SOBER_GUEST_PATH");
    if (!guest) guest = "/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so";

    fprintf(stderr, "[harness] guest: %s\n", guest);
    if (install_crash_handlers() != 0) return 2;

    int fd = -1;
    uint8_t *bytes = NULL;
    size_t size = 0;
    struct sober_guest_elf elf;
    if (copy_guest_to_memfd(guest, &fd, &bytes, &size, &elf) != 0) return 3;
    g_guest_size = size;

    snprintf(g_fd_path, sizeof(g_fd_path), "/proc/self/fd/%d", fd);
    fprintf(stderr, "[harness] opening memfd as %s\n", g_fd_path);

    /* The compat shim publishes the NDK/Android symbols via -rdynamic. */
    sober_android_init(argc, argv);

    /* libm exports 17 of the guest's GOT symbols (atan/exp/pow/log/...).
     * The guest's DT_NEEDED libm.so is our empty SONAME stub, so bring the
     * real libm into the global scope before resolving packed GLOB_DATs. */
    void *libm = dlopen("libm.so.6", RTLD_NOW | RTLD_GLOBAL);
    if (!libm) fprintf(stderr, "[harness] WARNING: libm.so.6 dlopen: %s\n",
                       dlerror());

    void *handle = dlopen(g_fd_path, RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        fprintf(stderr, "[harness] dlopen FAILED: %s\n", dlerror());
        return 4;
    }
    fprintf(stderr, "[harness] guest mapped natively (no translation layer)\n");

    uintptr_t base = 0;
    dl_iterate_phdr(find_base_cb, &base);
    if (!base) {
        fprintf(stderr, "[harness] could not locate guest load bias\n");
        return 5;
    }
    g_guest_base = base;
    elf.base = base;
    fprintf(stderr, "[harness] guest base=%p\n", (void *)base);
    sober_guest_elf_describe(&elf);

    size_t applied = 0, relative = 0, symbolic = 0, unknown = 0, unresolved = 0;
    if (sober_android_relocs_apply(&elf, &applied, &relative, &symbolic,
                                   &unknown, &unresolved) != 0) {
        fprintf(stderr, "[harness] packed relocation application FAILED\n");
        return 6;
    }

    /* Verify the 17 libm GLOB_DAT slots actually hold resolved addresses. */
    static const struct { const char *name; uintptr_t rva; } math_gots[] = {
        {"atan", 0x72281b8}, {"atan2", 0x7228260}, {"acos", 0x72282f8},
        {"exp", 0x7228308},  {"pow", 0x7228310},   {"fmod", 0x7228318},
        {"log", 0x7228320},  {"log2", 0x7228328},  {"log10", 0x7228330},
        {"round", 0x7228338},{"asin", 0x7228348},  {"sin", 0x7228350},
        {"sinh", 0x7228358}, {"cos", 0x7228360},   {"cosh", 0x7228368},
        {"tan", 0x7228370},  {"tanh", 0x7228378},
    };
    unsigned math_null = 0;
    for (size_t i = 0; i < sizeof(math_gots) / sizeof(math_gots[0]); ++i) {
        uintptr_t value = *(uintptr_t *)(base + math_gots[i].rva);
        if (!value) {
            fprintf(stderr, "[harness] libm GOT slot %s @ %#lx is NULL\n",
                    math_gots[i].name, (unsigned long)math_gots[i].rva);
            ++math_null;
        }
    }
    fprintf(stderr, "[harness] libm GOT verification: %zu slots checked, %u NULL\n",
            sizeof(math_gots) / sizeof(math_gots[0]), math_null);

    /* The guest's arena init calls mmap/mprotect/prctl through JUMP_SLOT PLT
     * relocations (542 of them, resolved by glibc at BIND_NOW load). Verify the
     * GOT slots are populated, something the 82 GLOB_DAT check does not cover. */
    static const struct { const char *name; uintptr_t got; } plt_gots[] = {
        {"sysconf", 0x7229578}, {"mmap", 0x7229580},
        {"mprotect", 0x7229588}, {"prctl", 0x7229590},
    };
    for (size_t i = 0; i < sizeof(plt_gots) / sizeof(plt_gots[0]); ++i) {
        uintptr_t v = *(uintptr_t *)(base + plt_gots[i].got);
        Dl_info di;
        const char *where = "unmapped";
        if (v && dladdr((void *)v, &di) && di.dli_sname) where = di.dli_sname;
        fprintf(stderr, "[harness] PLT GOT %-9s @0x%lx = %p (%s)%s\n",
                plt_gots[i].name, (unsigned long)plt_gots[i].got,
                (void *)v, where, v ? "" : " <-- NULL");
    }

    /* Resolve worker-1's bootstrap entry points without a hard link. */
    int (*verify_build)(void *) = sober_bionic_bootstrap_verify_build
        ? sober_bionic_bootstrap_verify_build
        : (int (*)(void *))dlsym(RTLD_DEFAULT, "sober_bionic_bootstrap_verify_build");
    int (*apply)(void *) = sober_bionic_bootstrap_apply
        ? sober_bionic_bootstrap_apply
        : (int (*)(void *))dlsym(RTLD_DEFAULT, "sober_bionic_bootstrap_apply");
    int (*replay)(void *) = sober_bionic_bootstrap_replay_constructors
        ? sober_bionic_bootstrap_replay_constructors
        : (int (*)(void *))dlsym(RTLD_DEFAULT,
                                 "sober_bionic_bootstrap_replay_constructors");

    fprintf(stderr, "[harness] bootstrap symbols: verify=%p apply=%p replay=%p\n",
            (void *)verify_build, (void *)apply, (void *)replay);
    if (!apply) {
        fprintf(stderr, "[harness] worker-1 bootstrap NOT LINKED "
                        "(sober_bionic_bootstrap_apply missing)\n");
        return 7;
    }
    if (verify_build) {
        fprintf(stderr, "[harness] calling verify_build(%p)\n", (void *)base);
        int vrc = verify_build((void *)base);
        fprintf(stderr, "[harness] verify_build rc=%d: %s\n", vrc,
                (sober_bionic_bootstrap_last_error)
                    ? sober_bionic_bootstrap_last_error() : "?");
        if (vrc != 0) {
            fprintf(stderr, "[harness] build-ID verification FAILED\n");
            return 8;
        }
    }
    fprintf(stderr, "[harness] calling bootstrap apply(%p)\n", (void *)base);
    if (apply((void *)base) != 0) {
        fprintf(stderr, "[harness] bootstrap apply FAILED\n");
        return 9;
    }
    fprintf(stderr, "[harness] bootstrap seeds/bridges applied\n");

    /* Diagnostic snapshot of the seeded globals right before ctor replay. */
    static const struct { const char *name; uintptr_t rva; } seeds[] = {
        {"allocator_object", 0x7834570}, {"arena_guard", 0x7cd8fb0},
        {"arena_table", 0x7cd9040},      {"empty_string", 0x79413b8},
        {"jni_singleton", 0x7d9cf40},    {"pre_jni_registry", 0x7cdd118},
        {"alloc_flag_783daa8", 0x783daa8}, {"ctor6_flag_7239570", 0x7239570},
    };
    for (size_t i = 0; i < sizeof(seeds) / sizeof(seeds[0]); ++i) {
        uintptr_t v = *(uintptr_t *)(base + seeds[i].rva);
        fprintf(stderr, "[harness] global %-20s @0x%lx = %p%s\n",
                seeds[i].name, (unsigned long)seeds[i].rva, (void *)v,
                v ? "" : "  <-- NULL");
    }

    const char *range_env = getenv("SOBER_CTOR_RANGES");
    int use_native_ranges = range_env &&
        (!strcmp(range_env, "native") || !strcmp(range_env, "all"));
    (void)replay;  /* we drive ctors ourselves so progress can be instrumented */
    (void)use_native_ranges;
    int rc = drive_constructors(base, &elf, 0);
    if (rc != 0) {
        fprintf(stderr, "[harness] constructor replay FAILED (rc=%d)\n", rc);
        return 10;
    }

    /* Deterministic completion: only after every required ctor returned. */
    const char complete[] = "guest init complete\n";
    ssize_t unused_out = write(STDOUT_FILENO, complete, sizeof(complete) - 1);
    (void)unused_out;
    const char *marker = getenv("SOBER_INIT_MARKER");
    if (marker) {
        int mf = open(marker, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (mf >= 0) {
            ssize_t unused_mark = write(mf, complete, sizeof(complete) - 1);
            (void)unused_mark;
            close(mf);
        }
    }

    /* Phase 3 (task #380): opt-in so every phase-2 check is unchanged.
     * Creates a native window + EGL context, drives the guest render path,
     * and prints "first frame rendered" after a successful buffer swap. */
    if (getenv("SOBER_RENDER")) {
        fprintf(stderr, "[harness] phase 3: native render phase starting\n");
        int rrc = guest_render_run(base);
        if (rrc != 0) {
            fprintf(stderr, "[harness] render phase FAILED (rc=%d)\n", rrc);
            free(bytes);
            close(fd);
            return 11;
        }
    }

    /* Phase 4 (task #385): opt-in JNI bridge + app-shell startup. Requires the
     * render phase (native window/context) to have run first. */
    if (getenv("SOBER_APP_SHELL")) {
        if (!getenv("SOBER_RENDER")) {
            fprintf(stderr, "[harness] SOBER_APP_SHELL requires SOBER_RENDER\n");
            free(bytes);
            close(fd);
            return 12;
        }
        /* Opt-in GDB attach window (lead's request): pause here, after guest
         * init + phase-3 render but before the app-shell startup RVAs, so a
         * debugger can attach with `gdb -p <pid>` and set a watchpoint without
         * replaying the 3618 constructors under ptrace. No default change. */
        const char *pause_env = getenv("SOBER_GDB_PAUSE_MS");
        if (pause_env) {
            long ms = atol(pause_env);
            if (ms > 0) {
                fprintf(stderr,
                        "[harness] GDB attach window: pid=%ld, pausing %ld ms "
                        "before app-shell startup\n",
                        (long)getpid(), ms);
                struct timespec pts = { .tv_sec = ms / 1000,
                                        .tv_nsec = (ms % 1000) * 1000000L };
                nanosleep(&pts, NULL);
                fprintf(stderr, "[harness] GDB attach window: resuming\n");
            }
        }
        fprintf(stderr, "[harness] phase 4: JNI app-shell phase starting\n");
        /* W24(c) host-only 512MB-stack experiment: run the whole guest-entry
         * app-shell phase on a pthread with a 512 MiB stack.  No guest bytes,
         * no gate change; opt-in so the default path is unchanged. */
        int arc = 0;
        if (getenv("SOBER_W24_BIGSTACK")) {
            struct w24_arg { uintptr_t base; int rc; } wa = { base, 0 };
            void *(*th)(void *) = (void *(*)(void *))guest_app_shell_run;
            pthread_attr_t at; pthread_t thid;
            if (pthread_attr_init(&at) == 0 &&
                pthread_attr_setstacksize(&at, (size_t)512u * 1024u * 1024u) == 0 &&
                pthread_create(&thid, &at, th, (void *)(uintptr_t)base) == 0) {
                pthread_join(thid, NULL);
                fprintf(stderr, "[harness] W24 bigstack guest entry joined\n");
                arc = 0;   /* thread return value not classified by guest_app_shell_run */
            } else {
                fprintf(stderr, "[harness] W24 bigstack pthread FAILED; "
                                "falling back to main thread\n");
                arc = guest_app_shell_run(base);
            }
            pthread_attr_destroy(&at);
            (void)wa;
        } else {
            arc = guest_app_shell_run(base);
        }
        if (arc != 0) {
            fprintf(stderr, "[harness] app-shell phase FAILED (rc=%d)\n", arc);
            free(bytes);
            close(fd);
            return 13;
        }
    }

    free(bytes);
    close(fd);
    return 0;
}
