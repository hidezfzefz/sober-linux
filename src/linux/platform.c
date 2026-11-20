/*
 * platform.c - Native Linux event loop: epoll + signalfd + timerfd.
 * Owned by worker-3 (task #370).
 */
#define _GNU_SOURCE
#include "platform.h"

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>

static int     g_epfd = -1;
static int     g_sigfd = -1;
static int     g_timerfd = -1;
static int     g_quit = 0;
static sigset_t g_old_mask;
static int     g_mask_saved = 0;

uint64_t sober_platform_now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

void sober_platform_request_quit(void) { g_quit = 1; }

int sober_platform_add_fd(int fd, uint32_t events)
{
    if (g_epfd < 0) return -1;
    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = events;
    ev.data.fd = fd;
    return epoll_ctl(g_epfd, EPOLL_CTL_ADD, fd, &ev);
}

int sober_platform_init(void)
{
    g_epfd = epoll_create1(EPOLL_CLOEXEC);
    if (g_epfd < 0) return -1;

    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGTERM);
    sigaddset(&set, SIGQUIT);
    if (pthread_sigmask(SIG_BLOCK, &set, &g_old_mask) == 0) g_mask_saved = 1;

    g_sigfd = signalfd(-1, &set, SFD_NONBLOCK | SFD_CLOEXEC);
    if (g_sigfd >= 0) sober_platform_add_fd(g_sigfd, EPOLLIN);

    g_timerfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (g_timerfd >= 0) {
        struct itimerspec its;
        memset(&its, 0, sizeof(its));
        its.it_interval.tv_sec = 1;
        its.it_value.tv_sec = 1;
        timerfd_settime(g_timerfd, 0, &its, NULL);
        sober_platform_add_fd(g_timerfd, EPOLLIN);
    }
    return 0;
}

void sober_platform_shutdown(void)
{
    if (g_sigfd >= 0) { close(g_sigfd); g_sigfd = -1; }
    if (g_timerfd >= 0) { close(g_timerfd); g_timerfd = -1; }
    if (g_epfd >= 0) { close(g_epfd); g_epfd = -1; }
    if (g_mask_saved) { pthread_sigmask(SIG_SETMASK, &g_old_mask, NULL); g_mask_saved = 0; }
    g_quit = 0;
}

int sober_platform_run(void)
{
    if (g_epfd < 0) return -1;
    struct epoll_event events[64];
    while (!g_quit) {
        int n = epoll_wait(g_epfd, events, 64, 250);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        for (int i = 0; i < n; ++i) {
            int fd = events[i].data.fd;
            if (fd == g_sigfd) {
                struct signalfd_siginfo si;
                while (read(g_sigfd, &si, sizeof(si)) == (ssize_t)sizeof(si)) {
                    if (si.ssi_signo == SIGINT || si.ssi_signo == SIGTERM ||
                        si.ssi_signo == SIGQUIT) {
                        g_quit = 1;
                    }
                }
            } else if (fd == g_timerfd) {
                uint64_t ticks = 0;
                while (read(g_timerfd, &ticks, sizeof(ticks)) == (ssize_t)sizeof(ticks)) {
                    /* periodic tick - reserved for client frame pacing */
                }
            }
            /* other registered fds are left for the integration layer */
        }
    }
    return 0;
}
