/*
 * platform.h - Native Linux platform/event-loop facade.
 *
 * Uses epoll + signalfd + timerfd directly. This is the runtime that will
 * drive the ported client once the REA/Ghidra interface map lands.
 * Owned by worker-3 (task #370).
 */
#ifndef SOBER_PLATFORM_H
#define SOBER_PLATFORM_H

#include <stdint.h>

int      sober_platform_init(void);
void     sober_platform_shutdown(void);
int      sober_platform_run(void);        /* returns 0 on clean shutdown */
void     sober_platform_request_quit(void);
uint64_t sober_platform_now_ns(void);

/* Register an additional fd with the central epoll instance (events: IN). */
int      sober_platform_add_fd(int fd, uint32_t events);

#endif /* SOBER_PLATFORM_H */
