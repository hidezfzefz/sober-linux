#define _GNU_SOURCE
#include "hw_breakpoint.h"

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <ucontext.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <link.h>
#include <linux/perf_event.h>
#include <linux/hw_breakpoint.h>
#include <fcntl.h>

static int g_fd = -1;
static void *g_addr;
static uintptr_t g_guest_lo, g_guest_hi;
static volatile int g_dumped;

static void emit(const char *s) { (void)!write(2, s, strlen(s)); }

static char *hx64(char *p, unsigned long v)
{
    static const char h[] = "0123456789abcdef";
    int i;
    *p++ = '0'; *p++ = 'x';
    for (i = 0; i < 16; ++i) *p++ = h[(v >> (60 - 4 * i)) & 0xf];
    return p;
}
static char *put(char *p, const char *s) { while (*s) *p++ = *s++; return p; }

/* Read-only build-ID match, same technique as the Bionic stdio bridge. */
static int note_match(const unsigned char *n, size_t sz)
{
    static const unsigned char id[20] = {
        0x20,0x4a,0xd0,0x49,0x59,0x9c,0x96,0xaf,
        0xb7,0x5b,0x8a,0x21,0xd5,0x05,0x10,0x63,
        0xe8,0x21,0xd6,0xc9
    };
    size_t off = 0;
    while (off + sizeof(Elf64_Nhdr) <= sz) {
        Elf64_Nhdr h;
        memcpy(&h, n + off, sizeof(h));
        off += sizeof(h);
        size_t ns = h.n_namesz, ds = h.n_descsz;
        size_t np = (ns + 3) & ~3u, dp = (ds + 3) & ~3u;
        if (ns > 4096 || ds > 4096 || np > sz - off || dp > sz - off - np) return 0;
        if (h.n_type == 3 && ns >= 4 && !memcmp(n + off, "GNU", 4) &&
            ds == sizeof(id) && !memcmp(n + off + np, id, sizeof(id))) return 1;
        off += np + dp;
    }
    return 0;
}
static int guest_cb(struct dl_phdr_info *i, size_t sz, void *d)
{
    (void)sz; (void)d;
    for (unsigned j = 0; j < i->dlpi_phnum; ++j) {
        if (i->dlpi_phdr[j].p_type != PT_NOTE) continue;
        const unsigned char *n = (const unsigned char *)
            (i->dlpi_addr + i->dlpi_phdr[j].p_vaddr);
        if (note_match(n, (size_t)i->dlpi_phdr[j].p_memsz)) {
            g_guest_lo = (uintptr_t)i->dlpi_addr;
            g_guest_hi = g_guest_lo + 0x8000000u;
            return 1;
        }
    }
    return 0;
}

static void watch_signal(int sig, siginfo_t *si, void *uc)
{
    (void)si; (void)sig;
    if (g_dumped >= 16) return;
    char buf[8192], *p = buf;
    unsigned long rip = 0, rsp = 0, rdi = 0, rsi = 0, rdx = 0, rcx = 0;
    if (uc) {
        ucontext_t *c = (ucontext_t *)uc;
        rip = (unsigned long)c->uc_mcontext.gregs[REG_RIP];
        rsp = (unsigned long)c->uc_mcontext.gregs[REG_RSP];
        rdi = (unsigned long)c->uc_mcontext.gregs[REG_RDI];
        rsi = (unsigned long)c->uc_mcontext.gregs[REG_RSI];
        rdx = (unsigned long)c->uc_mcontext.gregs[REG_RDX];
        rcx = (unsigned long)c->uc_mcontext.gregs[REG_RCX];
    }
    p = put(p, "[HWBP] HIT rip="); p = hx64(p, rip);
    p = put(p, " rdi="); p = hx64(p, rdi);
    p = put(p, " rsi="); p = hx64(p, rsi);
    p = put(p, " rdx="); p = hx64(p, rdx);
    p = put(p, " rcx="); p = hx64(p, rcx);
    p = put(p, " val=");
    p = hx64(p, g_addr ? (unsigned long)*(volatile uintptr_t *)g_addr : 0ul);
    p = put(p, " bytes=");
    for (int i = 0; i < 8; ++i) {
        unsigned char b = *(const unsigned char *)(rip + i);
        static const char h[] = "0123456789abcdef";
        *p++ = h[b >> 4]; *p++ = h[b & 15];
    }
    p = put(p, "\n");
    /* Stack qwords that look like guest-code return addresses. */
    for (int i = 0; i < 64; ++i) {
        uintptr_t v = *(const uintptr_t *)(rsp + (size_t)i * 8);
        if (v >= g_guest_lo && v < g_guest_hi) {
            p = put(p, "  stackq+"); p = hx64(p, (unsigned long)i * 8);
            p = put(p, "=base+0x"); p = hx64(p, (unsigned long)(v - g_guest_lo));
            p = put(p, "\n");
        }
    }
    *p = 0;
    emit(buf);
    ++g_dumped;
}

int sober_hw_watch_install(void *addr)
{
    g_addr = addr;
    g_guest_lo = g_guest_hi = 0;
    (void)dl_iterate_phdr(guest_cb, NULL);

    struct perf_event_attr a;
    memset(&a, 0, sizeof(a));
    a.type = PERF_TYPE_BREAKPOINT;
    a.size = sizeof(a);
    a.bp_type = HW_BREAKPOINT_W;
    a.bp_addr = (uint64_t)(uintptr_t)addr;
    a.bp_len = HW_BREAKPOINT_LEN_8;
    a.disabled = 1;
    a.sample_period = 1;
    a.wakeup_events = 1;
    a.exclude_kernel = 1;
    a.exclude_hv = 1;

    g_fd = (int)syscall(SYS_perf_event_open, &a, 0, -1, -1, 0);
    if (g_fd < 0) {
        fprintf(stderr, "[HWBP] perf_event_open failed: %s\n", strerror(errno));
        return -1;
    }
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = watch_signal;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGUSR2, &sa, NULL) != 0) return -2;
    if (fcntl(g_fd, F_SETSIG, SIGUSR2) != 0 ||
        fcntl(g_fd, F_SETOWN, getpid()) != 0 ||
        fcntl(g_fd, F_SETFL, O_ASYNC) != 0) return -3;
    if (ioctl(g_fd, PERF_EVENT_IOC_ENABLE, 0) != 0) return -4;
    fprintf(stderr, "[HWBP] armed addr=%p guest=[0x%lx,0x%lx)\n",
            addr, (unsigned long)g_guest_lo, (unsigned long)g_guest_hi);
    return 0;
}
