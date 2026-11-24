# n9 WAVE3 #721 — harness (one page, READ-ONLY LOG REVIEW)

NO new runs (w11-arg0.json missing). Only existing logs read:
`/tmp/opencode/n9-harness/n9_{1,2,3}.err` (allocator patch ON) and
`n9off_{1,2,3}.err` (patch OFF), plus `/tmp/opencode/n8-harness/n8*_*.err`.
All findings are **DIAGNOSTIC-ONLY — NO rung credit**. No guest writes/hand-set
bytes/gate edits/seeder/fake returns/remaps, no 1dbe4fc bridge.

## FAULTING INSTRUCTION FIRST — PROVEN (surviving logs, 6/6)
`guest+0x06caef67` — reported RIP after guest int3 at `0x6caef66`, bytes
`89 c0 e8 62`, `signal=5`, `si_addr=0`, rc=139.
`[W28_FAULT_INSN] rip=0x00007f04ff03af67 guest_rva=0x06caef67` (n9off_1)

## `[0x7cdd149]` dumps — EVERY surviving line (6/6, all ZERO)
```
n9_1.err:3720:[N7_FAULT_BYTES] tid=415380 [0x7cdd149]=0x0000000000000000 [0x7cef650]=0x0000000000000000
n9_2.err:3720:[N7_FAULT_BYTES] tid=415922 [0x7cdd149]=0x0000000000000000 [0x7cef650]=0x0000000000000000
n9_3.err:3720:[N7_FAULT_BYTES] tid=416825 [0x7cdd149]=0x0000000000000000 [0x7cef650]=0x0000000000000000
n9off_1.err:3720:[N7_FAULT_BYTES] tid=422575 [0x7cdd149]=0x0000000000000000 [0x7cef650]=0x0000000000000000
n9off_2.err:3720:[N7_FAULT_BYTES] tid=423081 [0x7cdd149]=0x0000000000000000 [0x7cef650]=0x0000000000000000
n9off_3.err:3720:[N7_FAULT_BYTES] tid=423844 [0x7cdd149]=0x0000000000000000 [0x7cef650]=0x0000000000000000
```
No `[N8_CTOR3] after ...` latch dump exists: ctor3 replay was OFF in all 6
runs (`grep -c N8_CTOR3` = 0). Other latch lines are all zero:
`[flags-latch] route=none (inert; no guest call, latch=0, withhold=0)`,
`[app-shell] flags-latch module -> rc=-2 latch=0`,
`[app-shell] crashpad-return module -> rc=1 latch=0` (6/6 each).

## Latch 0 or 1 per config — PROVEN
| config | `[0x7cdd149]` at fault | latch elsewhere |
|---|---|---|
| patch ON (`n9_*`) | **0** (3/3) | 0 (flags-latch / crashpad-return lines) |
| patch OFF (`n9off_*`) | **0** (3/3) | 0 (same lines) |
**Latch is ZERO in BOTH configs.** Earlier waves reported latch=1 at fault,
but those n8 logs are no longer on disk — see RETRACTION below.

## Fatal message — PROVEN (6/6)
`RBXCRASH: FatalRuntimeError (Can't initialize the TaskScheduler before flags have been loaded)`
(line 3717 in all 6). No mimalloc-HardAssert line survives in these logs.
(Static-B #717 independently RETRACTED the "message switches ON vs OFF" claim.)

## Allocator patch banner — PROVEN
`n9off_{1,2,3}.err:19:[N9_ALLOC_PATCH] OFF (default); guest allocator unpatched; 1dbe4fc NOT bridged`
Patch-ON runs `n9_*.err` have **no such banner** (the line is only emitted on
the OFF path) — banner presence/absence is the config discriminator.

## SHA banner presence/absence — PROVEN
`grep -c 'avd-boundary] verified'` = **0/0/0/0/0/0**; instead, 6/6:
`[avd-boundary] open failed: /tmp/opencode/w11-arg0.json` (line 3705).
The authoritative 1,533,221-byte arg0 input is MISSING; these runs are
**DIAGNOSTIC ONLY, no rung credit**. Any "SHA 8215519f verified" claim from
these logs would be UNSUPPORTED.

## RETRACTED / NOT DONE
`n8-harness/` directory and all `n8*_*.err` are gone (cleanup); no n8 lines
can be quoted in this report. Therefore any earlier latch=1 / mimalloc-
HardAssert claim resting on those files is **RETRACTED as unverifiable from
surviving evidence** (quoted values remain only inside earlier report text).
No new runs, no ledger edit, no device, no object build. No questions asked.

## Evidence
`/tmp/opencode/n9-harness/n9_{1,2,3}.err`, `n9off_{1,2,3}.err`,
`report_n9w3.md`.
