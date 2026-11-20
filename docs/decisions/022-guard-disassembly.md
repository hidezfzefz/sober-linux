# Decision 022 — Guard at 0x2350044 and cross-run value check

Status: accepted (lead). This supersedes speculation about Looper/Binder/JNI
until the GDB watchpoint lands.

## Guard disassembly (objdump, AppImage build 204ad049)

Function starting `0x234fff8`:

```
0x2350014: cmpl $0xbab1e,(%rdi)      ; arg1 r14 must be backing object
0x2350032: cmpl $0xc0dedbad,(%rbx)   ; arg2 rbx must be target object
0x2350044: cmpq $0x0,0x68(%rbx)      ; <-- THE GUARD
0x2350049: jne  0x2350270            ; non-zero -> bail out
...
0x23500c6: mov  %r14,0x68(%rbx)      ; link backing object into target
```

So the guard checks **one qword of memory**, `[rbx+0x68]`, where `rbx` is the
second argument and carries magic `0xc0dedbad`. It is a link/attach guard on
a RefBase-like back-pointer: zero means "not linked, safe to link"; non-zero
means "already linked, bail". It is not obviously a Looper or Binder check.
Our value `0xffffff3085894cff` makes the guard fail, so `0x23500c6` never
executes and the consumer later dereferences the stale field.

## Cross-run consistency (lead, 2026-11-19)

`tests/app-shell.sh` run twice with different guest base addresses:

| Run | rdi at `0x2456079` | Guest RIP base |
| --- | --- | --- |
| 1 | `0xffffff3085894cff` | `0x7f1508546079` |
| 2 | `0xffffff3085894cff` | `0x7ff6698d2079` |

The value is **identical and ASLR-invariant** — a fixed/deterministic value,
not a guest-base-relative pointer. Bit pattern: high32 `0xffffff30` (-208),
low32 `0x85894cff`.

A byte search found 42 matches in the guest file, but disassembly showed they
are all stack-save encodings (`4c 89 85 30 ff ff ff` = `mov %r8,-0xd0(%rbp)`);
there is **no** `movabs` of this constant. Do not treat that search as evidence
of a static immediate.

## worker-1 Ghidra confirmation (2026-11-19)

`analysis/handle/guard-analysis.md` confirms the guard is a **pure null-check**
on `[A+0x68]` — no Looper, Binder or JNI-state check. The user's post-GDB
decision tree therefore does not trigger any of those branches.

Crucially, the constant comes from **stale unaligned machine-code bytes**:
the little-endian bytes `ff 4c 89 85 30 ff ff ff` are the tail of the x86-64
`mov %r8,-0xd0(%rbp)` stack-save (`4c 89 85 30 ff ff ff`) plus a neighbouring
`0xff`; that sequence occurs 42 times in guest `.text` (core form 245 times),
with no `movabs` of the value. It is neither a pointer nor an Android
`sp<>`/`wp<>` invalid-handle sentinel (`INITIAL_STRONG_VALUE`, lifetime flags,
`0xdeadbaad`/`0xdead0000` fills were checked). ASLR-invariance follows from it
being code *content*, and in-place writes faulted because the field aliases
read-only bytes.

Correct field state is either **0** (guest `0x2333593` takes the safe null
early-exit) or **canonical B** (linked by `0x23500c6`). No graph, ALooper or
Binder fix applies; the GDB watchpoint is still needed to find the site that
produced this code pointer as a handle.

## Open

- worker-1's GDB watchpoint run is being iterated (stray SIGTRAPs during the
  3618-constructor replay needed a Python stop-hook). It has not yet landed.
- No synthetic-object-graph changes until GDB proves the write fires
  (user directive).
- Post-GDB decision tree: Looper -> fake ALooper registration; Binder -> stop
  and escalate (kernel infrastructure); JNI object state -> targeted field fix.
