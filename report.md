# W31 #638 — harness (one page)

Worktree `/tmp/opencode/w31-harness`, branch w31-harness. Engine SHA `3e498d64`.
flock held; no device/guest writes/gate edits/seeder; seed OFF; JNI_OnLoad-first
ON; full JSON SHA verified. Every success claim quotes a raw line.
Evidence: `runD_{1,2,3}.err`, `latchrun.err`, `rund3.sh`.

## FAULTING INSTRUCTION FIRST — PROVEN
No-breakpoint (W30/latchrun): `guest+0x6ca7f95` `e8 b6 b3 d6 fb` =
**`call 0x2a13350`**. Raw `latchrun.err`:
`[W30_FAULT] rip=0x00007fffef8e3f95 si_addr=0x00007fffff7fefb8 rsp=0x00007fffff7fefc0 rbp=0x00007fffff7ff090 insn=00000000000000e8 00000000000000b6 ...`
`si_addr = rsp-8` → the `call` pushed its RA into the guard page (stack overflow).
With W31 breakpoints armed the fault moves to host `0x7ffff7e30017`
(`[W30_FAULT] rip=0x00007ffff7e30017 si_addr=0x00007fffff7fefa8 rsp=0x00007fffff7fefb0 ...`) — still `si_addr=rsp-8`.

## (a) 0x2a13350 entry trace — PROVEN (Run D ×3, identical)
Raw `[W31_ENTRY #1] tid=1585707 rip=base+0x2a13350 rbp=0x7fffffffb980`;
raw last `[W31_ENTRY #37537] tid=1585707 rip=base+0x2a13350 rbp=0x7fffff8007f0`.
**37,537 entries, 37,536 obj observations.** Raw first obj:
`[W31_OBJ] tid=1585707 holder=0x55555564f990 obj=0x7fffe86a08c0 [obj+0x28]=0000000000000000 ascii28=0 ascii28_text='........' bytes64=0000000000000000 ...`
Raw last obj: same `obj=0x7fffe86a08c0 [obj+0x28]=0000000000000000 ascii28=0`.
**`[obj+0x28]` is NOT ASCII — it is all zeros on every call** (distinct values:
`37536 [obj+0x28]=0000000000000000`; `37536 ascii28=0`). First value == overflowing
value (0). The predicate `cmp [obj+0x28],0` always returns FALSE → caller recurses.

## (b) Perf WRITE watch on obj+0x28 — ARMED, NO HIT
Raw `[W31_WRITE_ARM] obj=0x7fffe86a08c0 watch=0x7fffe86a08e8 tid=1585707`.
Across 37,536 calls: **zero `[W31_WRITE]` lines** — no writer RIP/RA/value/tid
observed. Writer identity **UNPROVEN**; W-watch positive control not run. The
value did not change (quote in (a)).

## (c) Allocation provenance — PARTIAL / UNPROVEN
Raw `[W31_OBJ] ... holder=0x55555564f990 obj=0x7fffe86a08c0 ...`.
`holder` is the host-heap state base+0x410; state base = `holder-0x410` =
`0x55555564f580` (host heap). Static: state allocated via guest allocator call
`0x2ea6534 → 0x1dbad62` (TLS-based allocator), size `r12*8`, `r12=(r13+0x11)&~0xf`;
`obj` is the pointer stored at `state+0x410` (`0x2a0deba: add rax,0x410`).
`obj=0x7fffe86a08c0` is below guest base `0x7fffe8c3c000` — separate
mapping. **obj's own allocator, its size, and any overlapping allocation/host
buffer are UNPROVEN**: alloc breakpoints armed too late (`[W31_ALLOC_SIZE]`/
`[W31_ALLOC_RET]` counts 0/0/0; `[W31_ARM] ... alloc=-1/-1 ...`).

## (d) Latch watch on 0x2eb7520 — PROVEN (`runD_1.err` + `latchrun.err`)
Raw `[W21_LATCH_HIT] rip=base+0x2eb7520 callerRA=base+0x2359342 latch=0x0 tid=1585707`
— the real latch writer fires with the AVD reference RA `0x2359342`. Raw rung:
`[checkpoint] rung1 PASS-BOUNDARY-REPLAY (DEBT=1)`. In runs 2-3 the watcher was not armed (raw `[W31_ARM] base=0x7fffe8c3c000
entry=0 obj=0 alloc=-1/-1 latch=-1`) and rung1 FAIL — absence there is a
watcher-configuration effect, not a latch absence. Run 1 has both the hit and
rung1 PASS in the same log.

## NOT DONE
No ledger edit by me (entries sent lead); no guest bytes/gate edits/device;
W-watch positive control unrun; obj allocator/size/overlap UNPROVEN; recursion
root cause UNPROVEN (static scope).

## First divergence / next step
`[obj+0x28]` stays 0 (never ASCII, never written) so the predicate at
`0x2a13350` never terminates the `0x6ca8014` recursion; the 37,537th entry
faults at `call 0x2a13350` with `si_addr=rsp-8`. Next: static identify the
real-boot writer of `*(obj+0x28)` and host-only prove W-watch positive control.
