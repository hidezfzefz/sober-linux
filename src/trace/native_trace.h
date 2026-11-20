/*
 * native_trace.h - opt-in native allocation tracer for the Sober guest.
 *
 * Worker-2, Phase 4b task #394.  Pure native C, no translation layer.
 *
 * This is an LD_PRELOAD DSO, NOT linked into sober-native/guest-init-host,
 * so `ldd build/sober-native` stays libc-only when tracing is off.  It logs
 * malloc/calloc/realloc/free with backtraces, remembers the allocation-site
 * backtrace for every live block, and reports blocks that contain the guest's
 * 0xc0dedbad handle magic when they are freed.  It can also watch a fixed
 * address for value changes from inside the hook path.
 *
 * Usage:
 *   gcc -shared -fPIC -O2 -o build/native_trace.so src/trace/native_trace.c -ldl
 *   SOBER_TRACE=1 SOBER_TRACE_LOG=1 \
 *     LD_PRELOAD=$PWD/build/native_trace.so \
 *     build/guest-init-host <engine>          # or tests/app-shell.sh
 *
 * Environment:
 *   SOBER_TRACE=1          enable (default OFF: hooks are transparent)
 *   SOBER_TRACE_LOG=1      log every alloc/free (default: only interesting)
 *   SOBER_TRACE_FD=N       output fd (default 2 = stderr)
 *   SOBER_TRACE_DEPTH=N    backtrace depth 4..24 (default 12)
 *   SOBER_TRACE_MIN=N      only record blocks with size >= N
 *   SOBER_TRACE_MAX=N      only record blocks with size <= N (0 = no max)
 *   SOBER_TRACE_WATCH=0x.. also log changes of the u64 at this virtual address
 *   SOBER_TRACE_MAGIC=0x.. magic to scan for at free (default 0xc0dedbad)
 *   SOBER_TRACE_SCANBYTES=N bytes to scan at free (default 512, max 4096)
 *   SOBER_TRACE_WATCH_AUTO=1  on the first 0x1100-byte calloc, arm a one-shot
 *                          write watch on returned_ptr+0x68 and log the
 *                          first writer's RIP/backtrace
 *   SOBER_TRACE_GUEST_BASE=0x.. optional guest load base for RVA logging
 *   SOBER_TRACE_START_FILE=path  stay disarmed until this file exists (e.g.
 *                          the harness's SOBER_INIT_MARKER), then start
 *                          capturing — avoids tracing all 3618 ctors
 *   SOBER_TRACE_DUMP=1     dump a summary from the atexit handler
 */
#ifndef SOBER_NATIVE_TRACE_H
#define SOBER_NATIVE_TRACE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Runtime configuration (normally set from the environment in the library
 * constructor; exposed so a harness can inspect/override it). */
typedef struct {
    int        enabled;
    int        log_all;
    int        fd;
    int        depth;
    size_t     min_size;
    size_t     max_size;
    uintptr_t  watch_addr;    /* 0 = none */
    uint32_t   magic;
    size_t     scan_bytes;
    int        dump_summary;
    const char *start_file;   /* if set, tracing stays disarmed until it exists */
} SoberTraceConfig;

/* Called directly by the native calloc bridge when LD_PRELOAD cannot see the
 * guest allocator's libc calls (decision 028 follow-up). */
void sober_trace_watch_auto(void *ptr);
int  sober_trace_config(SoberTraceConfig *out);   /* copy current config */
void sober_trace_dump(void);                      /* print summary to fd */
/* Returns non-zero if a live block was allocated with the given address. */
int  sober_trace_lookup(void *ptr, uintptr_t **frames_out, int *depth_out);

#ifdef __cplusplus
}
#endif
#endif /* SOBER_NATIVE_TRACE_H */
