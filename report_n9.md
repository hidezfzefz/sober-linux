# n9 WAVE1 #714 — harness (one page, STOP)

Worktree `/tmp/opencode/n9-harness` branch n9-harness. SOLE flock, cores 0-1,
READ-ONLY perf/pvm reads; no guest writes/hand-set bytes/gate edits/seeder/fake
returns/remaps, NO bridge of `0x1dbe4fc`. Object OFF, SF_FULL=1 (DEBT=1),
ctor3 OFF, seed OFF, JNI_OnLoad-first ON. Guest binary copied from the exact
SHA-match found after `/tmp/opencode/sober-apk/` disappeared:
`3e498d649421115bdbebaa69f78e2b81...` (`n9bin/libroblox.so`).
Evidence: `n9off_{1,2,3}.err`, earlier patched `n9_{1,2,3}.err`, `rund3_n9off.sh`.

## FAULTING INSTRUCTION FIRST — PROVEN (3/3)
`guest+0x6caef67` — reported RIP after guest int3 at `0x6caef66`, bytes
`89 c0 e8 62` (`mov eax,eax; call abort@plt`), `signal=5`, `si_addr=0`, rc=139.
`[W30_FAULT] rip=0x00007f04ff03af67 si_addr=0x0000000000000000 rsp=0x00007ffecde19ed0 rbp=0x00007ffecde19f30 insn=0000000000000089 00000000000000c0 00000000000000e8 ...`
`[W28_FAULT_INSN] rip=0x00007f04ff03af67 guest_rva=0x06caef67`

## KILL SWITCH — allocator patch OFF by default (host-only, ledger sent)
`[N9_ALLOC_PATCH] OFF (default); guest allocator unpatched; 1dbe4fc NOT bridged`
(3/3). Ledger EXACT: **"harness patched guest allocator, defect removal"**,
DEBT=1. `SOBER_ALLOCATOR_PATCH=1` is the only opt-in; not used here.

## Pointer / arena at fault with allocator patch OFF — PROVEN
`[N8G AT_FAULT] [7cd8ff8]=0x0000000000000000 [7cd9048]=0x0000000000000000 [7cd9040]=0x0000000000000000 alloc=0x0000000000000000 ...`
— all three mimalloc private globals are ZERO (arena init was performed by the
host bridge path, now disabled). The fatal message CHANGED:
`RBXCRASH: FatalRuntimeError (Can't initialize the TaskScheduler before flags have been loaded)`
(3/3), instead of the earlier `HardAssert (Expect to have mimalloc working...)`.
With the earlier patch ON, the 1dbe4fc bp was armed (`[N9_PTR_ARM] base=...
fd=4 rc=0`) but had **0 hits** in 3/3 with ctor3 OFF — no pointer was ever
tested by `1dbe4fc` in this configuration, so no caller RVA is available.
UNPROVEN: caller RVA and page-source reach (no bp hit; static-A scope).

## SHA banner — NOT PRESENT (RETRACTED expectation)
`grep -c 'avd-boundary] verified' n9off_*.err = 0/0/0`. With allocator patches
OFF the run faults earlier, before the AVD boundary replay loads arg0, so no
input SHA banner is emitted. The **guest binary** SHA was verified independently
from the exact-match copy (`3e498d649421...`), but that is not the w11-arg0
boundary SHA. No claim of an input SHA banner is made.

## STOP
Per lead: "If it faults, report and stop." All 3 runs faulted — no further
patching, bridging, or extension performed. No questions asked; DELETE NOTHING
honored (the earlier rc=3 runs were not credited; guest copied, not deleted).
