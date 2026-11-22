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
#include <linux/perf_event.h>
#include <linux/hw_breakpoint.h>
#include <fcntl.h>

static int g_fd = -1;
static void *g_addr;

static void emit(const char *s)
{
    (void)!write(2, s, strlen(s));
}

static void hexline(unsigned long v, char *out)
{
    static const char hx[] = "0123456789abcdef";
    int i;
    out[0] = '0'; out[1] = 'x';
    for (i = 0; i < 16; ++i)
        out[2 + i] = hx[(v >> (60 - 4 * i)) & 0xf];
    out[18] = '\n'; out[19] = 0;
}

static void watch_signal(int sig, siginfo_t *si, void *uc)
{
    (void)si;
    char buf[64];
    unsigned long rip = 0;
    if (uc) {
        ucontext_t *c = (ucontext_t *)uc;
        rip = (unsigned long)c->uc_mcontext.gregs[REG_RIP];
    }
    uintptr_t v = g_addr ? *(volatile uintptr_t *)g_addr : 0u;
    /* Only the write that installs the 0x30 token is of interest. */
    if (v != 0x30u) return;
    (void)sig;
    emit("[HWBP] ZERO30_WRITE rip=");
    hexline(rip, buf);
    emit(buf);
}

int sober_hw_watch_install(void *addr)
{
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

    g_addr = addr;
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
    if (sigaction(SIGUSR2, &sa, NULL) != 0) {
        fprintf(stderr, "[HWBP] sigaction failed: %s\n", strerror(errno));
        return -2;
    }
    /* Deliver overflow via SIGUSR2 with siginfo using fcntl async mode
     * (available in older UAPI headers that lack PERF_EVENT_IOC_SET_SIGNAL). */
    if (fcntl(g_fd, F_SETSIG, SIGUSR2) != 0 ||
        fcntl(g_fd, F_SETOWN, getpid()) != 0 ||
        fcntl(g_fd, F_SETFL, O_ASYNC) != 0) {
        fprintf(stderr, "[HWBP] async signal setup failed: %s\n", strerror(errno));
        return -3;
    }
    if (ioctl(g_fd, PERF_EVENT_IOC_ENABLE, 0) != 0) {
        fprintf(stderr, "[HWBP] ENABLE failed: %s\n", strerror(errno));
        return -4;
    }
    fprintf(stderr, "[HWBP] armed addr=%p fd=%d\n", addr, g_fd);
    return 0;
}
