# src/trace — native allocation tracer (Phase 4b, worker-2)

## Purpose

Opt-in `LD_PRELOAD` interposer that logs `malloc` / `calloc` / `realloc` /
`free` (plus `memalign`, `mmap`, `munmap`) with native backtraces, remembers
the allocation-site stack for every live block, and reports blocks containing
the guest's `0xc0dedbad` magic at free or while still live. This is meant to
identify which allocation produces the bad app-shell handle without any
Android runtime, Wine or translation layer.

It is a **separate DSO**: it is never linked into `sober-native` or
`guest-init-host`, so the normal executable remains `ldd`-clean.

## Build

```sh
gcc -std=gnu17 -Wall -Wextra -Werror -fPIC -shared \
    -fno-omit-frame-pointer -fasynchronous-unwind-tables \
    -Isrc/trace src/trace/native_trace.c -o build/native_trace.so -ldl
# or: add_subdirectory(src/trace) and build target native_trace
```

## Run (opt-in)

```sh
rm -f /tmp/init.marker
SOBER_RENDER=1 SOBER_APP_SHELL=1 SOBER_WINDOW_HOLD_MS=500 \
SOBER_INIT_MARKER=/tmp/init.marker \
SOBER_TRACE=1 SOBER_TRACE_START_FILE=/tmp/init.marker \
SOBER_TRACE_SCANBYTES=1024 SOBER_TRACE_DUMP=1 \
LD_PRELOAD=$PWD/build/native_trace.so \
  ./build/guest-init-host /tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so
```

`SOBER_TRACE_START_FILE` is the important knob: without it the tracer starts
at process load and recording backtraces for every allocation makes the 3618
guest constructors painfully slow. Point it at the harness's
`SOBER_INIT_MARKER` file so tracing arms only for the app-shell phase.

Environment (all optional):
`SOBER_TRACE=1`, `SOBER_TRACE_LOG=1`, `SOBER_TRACE_FD=N`,
`SOBER_TRACE_DEPTH=N`, `SOBER_TRACE_MIN/MAX=N`, `SOBER_TRACE_WATCH=0xADDR`,
`SOBER_TRACE_MAGIC=0x...`, `SOBER_TRACE_SCANBYTES=N`,
`SOBER_TRACE_START_FILE=path`, `SOBER_TRACE_DUMP=1`.

## Output markers

- `[trace] ARMED (start file present)` — tracing started.
- `[trace] MAGIC-FREE magic=... ptr=... size=... off=... alloc#N` followed by
  the allocation-site and free-site stacks — a freed block contained the
  magic.
- `[trace] MAGIC-LIVE ...` — a still-live block first showed the magic; the
  allocation-site stack is printed. This is the key evidence for "which
  allocation produced the bad handle".
- `[trace] mmap len=... ptr=... stack:` for mappings >= 64 KiB (guest arena).
- `[trace] WATCH addr=... old=... new=... stack:` when `SOBER_TRACE_WATCH`
  is set and the u64 at that address changes from inside the hook path.

## Safety

The hooks use only static bookkeeping and `write(2)`; no allocation happens
inside the interposer. Nested allocations (from the unwinder) are forwarded to
the real allocator without logging. When `SOBER_TRACE` is unset every hook is
a direct forward — the DSO is inert.
