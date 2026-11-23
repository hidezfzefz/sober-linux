# W30 #636 — harness (one page)

Worktree `/tmp/opencode/w30-harness`, branch w30-harness. Engine SHA `3e498d64`.
flock held for all runs. No device/guest writes/gate edits; NO seeder;
JNI_OnLoad-first ON. NEW RULE: success claims quote raw log lines.
Evidence: `runD_{1,2,3}.err`, `rund3.sh`.

## FAULTING INSTRUCTION FIRST (Run D ×3, identical) — PROVEN
`guest+0x6ca7f95`: `e8 b6 b3 d6 fb` = **`call 0x2a13350`** (the TLS predicate
accessor). Raw line (run 1):
`[W30_FAULT] rip=0x00007fffef8e3f95 si_addr=0x00007fffff7fefc8 rsp=0x00007fffff7fefd0 rbp=0x00007fffff7ff0a0 insn=00000000000000e8 00000000000000b6 00000000000000b3 00000000000000d6 00000000000000fb 0000000000000084 00000000000000c0 0000000000000074`
`[W28_FAULT_INSN] rip=0x00007fffef8e3f95 guest_rva=0x06ca7f95`
**`si_addr - rsp = -8`** (raw: `[W30_SI_RSP_DELTA] 8`): the `call` tried to push
its return address into the guard page at `rsp-8` → **stack overflow**.
Static decode: `0x6ca7f95: call 2a13350; 0x6ca7f9a: test al,al; je 6ca7ffa`.
RA/stack return addresses: `fp#0..fp#23 = guest+0x6ca8014` (all 24):
`[W23_FP] from rbp=0x00007fffff7ff0a0` / `fp#0=guest+0x0000000006ca8014` ...

## (a) Crash handler — PROVEN (raw quotes)
`[W30_SIGALTSTACK] installed sp=0x5555555e3a80 size=65536` — SA_ONSTACK +
sigaltstack active. First emitted line is the W30_FAULT line above (RIP,
si_addr, rsp, rbp, insn bytes). **No guest-pointer dereference remains:**
RIP/RSP/RBP/RDI/FP/stack reads all go through `process_vm_readv`
(`w30_readmem`); unreadable pages print `UNREADABLE` instead of faulting
(raw: `[0]=UNREADABLE[1]=UNREADABLE...`). Ring dumps use host-only buffers.

## (b) TSD seed REMOVED (default OFF) — PROVEN
Raw: `[app-shell] W30 TSD seed REMOVED (default OFF; harness-fabricated state)`.
Ledger wording sent: **"harness-fabricated state, caused the 0x6ca229c fault"**.

## (c) Run D ×3 (full JSON, seed OFF, NO breakpoints) — PROVEN
Raw: `[avd-boundary] verified /tmp/opencode/w11-arg0.json bytes sha256=8215519ff31bb79e66baa65baf9b09fe46ea172f64df33d2399db315fd27634b`
Raw: `[app-shell] AVD boundary pre: arg0_len=1533221 jstr0=0x5555562e2fd0 D08=0 latch=0 ...`
rc=139 ×3. Per-run: fault `guest+0x6ca7f95` `call 0x2a13350`, `si_addr=0x7fffff7fefc8`,
delta 8 (3/3). **0x2eb7520 fires: UNPROVEN** — no watcher armed (no breakpoints);
`grep -c 'W21_LATCH_HIT' runD_*.err = 0/0/0`. Nearest latch raw line:
`[app-shell] AVD boundary nativeInitClientSettings 0x2359277 -> rc=0 D08_len=0 latch=1 CF0=0 72a31b8=0x1`
Raw rung line (all 3): `[checkpoint] rung1 FAIL (DEBT=1)`.

## (d) Stack cycle (si_addr within 64 KiB of rsp: delta=8) — PROVEN
Raw: `[W30_RET64] rsp=0x00007fffff7fefd0`. Index-0..5 = `UNREADABLE` (guard
page ends at `rsp+48 = 0x7fffff7ff000`). Repeating 15-qword pattern at indices
**13–27** and **41–55**: `guest+0x221414, 0x3, guest+0x221414, 0x3,
0x7fffe8c0c76b, 0x5555555addbb8, guest+0x2ea6460, 0x5555555addbb8,
0x55555555e3060, 0x555555590c830, 0xabcdef0123456789, 0x5555555addbb8,
0x55555555e3060, <stack ptr>, guest+0x6ca8014`.
**Cycle period = 28 qwords = 224 bytes/frame; 2 full repetitions in the
64-word window; recursion depth ≥24** (FP chain capped at 24, all `0x6ca8014`).

## NOT DONE
No ledger edit by me (entries sent audit); no device; no guest bytes/gate
edits; recursion root cause UNPROVEN (static scope); 0x2eb7520 firing not
observed (no watcher in this configuration). No success claim without a quote.

## First divergence / next step
With the fabricated TSD state gone, the guest recurses at `0x6ca8014` until
`call 0x2a13350` at `0x6ca7f95` overflows the stack (`si_addr=rsp-8`); W25's
`0x548`/NULL-env fault is RETRACTED for this configuration. Next: static trace
the `call [rax+0x720]` at `0x6ca800e` that re-enters `0x6ca8014`.
