# Decision 027 — Phase 4b: runtime trace of the bad handle

Status: accepted (lead). Fresh Convio team `TEAM-972H-AWF5-BCVG`,
tasks #391-#395. Four read-only local research subagents run in parallel.

## Starting point

Phase 4 remains honestly BLOCKED (decision 026). Every source-level fix has
been empirically ruled out; the remaining unknown is the guest-internal
allocation site that produces the `0xc0dedbad` handle with stale code bytes at
`+0x68`. GDB batch and attach-by-PID failed on loader SIGTRAP / ptrace stops.

## Goal

Use a working runtime tracer on this Gentoo host (perf, valgrind, rr, ltrace,
bpftrace, LD_PRELOAD interposition) to capture the allocation site / writer,
then apply ONE targeted fix. Done condition unchanged from decision 012:
a genuine guest→Java callback must fire and `app shell rendered` must be
gated on it plus the phase-3 render call. No reduced gate, no false marker.

## Ownership

| Path | Owner |
| --- | --- |
| `analysis/trace/` | worker-1 |
| `src/trace/` | worker-2 |
| `tests/`, `src/linux/`, `CMakeLists.txt`, `Makefile`, `build/` | worker-3 |
| `analysis/dispatch/` | worker-4 |
| `docs/`, `README.md`, `PROGRESS.md` | lead |

All experimental guest patches stay OFF by default (decision C). Known facts
from decisions 021-026 must not be re-litigated without new tracer evidence.
