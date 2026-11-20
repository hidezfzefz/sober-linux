# Decision 030 — The faulting handle is not one of our zeroed blocks

Status: accepted (lead).

## Decisive zero-signal field log (worker-2)

With `SOBER_TRACE_HANDLE_FIELDS=1` and no LD_PRELOAD/signals, at every JNI
entry point the three 0x1100 handles from the 8th calloc bridge show:
- handle #0: `+0x20=0`, `+0x68=0`
- handle #1: `+0x20=0`, `+0x68=0x7f1b480711e0` (a valid guest heap pointer — the guest linked its backing object)
- handle #2: not logged before the fault

The fault still reports `rdi=0xffffff3085894cff`, which is **neither** handle's
`+0x68` value. Therefore the `0xc0dedbad` object consumed at `0x23331ae` is
not one of the zeroed blocks, and no allocation/zeroing fix can address it.

The chained static write-watch confirmed the handle base write at `0x231f34b`
but saw no write to `+0x68` on the zeroed blocks, consistent with a different
object being consumed.

## Consequence

Stop all allocator/field work. The next decisive experiment is task #399: a
custom ptrace tracer that breaks at `0x23331ae`, reads the actual `rbx` object,
and arms a hardware write watchpoint on `rbx+0x68`, reporting the writer RIP.

Strict DECISION 013 gate unchanged: no false `app shell rendered`.
