/* Diagnostic in-process hardware write-watchpoint via perf_event_open.
 * Never touches guest memory; logs RIP in a signal handler. */
#ifndef SOBER_JNI_HW_BREAKPOINT_H
#define SOBER_JNI_HW_BREAKPOINT_H
int sober_hw_watch_install(void *addr);
#endif
