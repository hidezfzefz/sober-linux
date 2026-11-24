# n10 WAVE1 (cont.) #725 — harness (one page)

Worktree `/tmp/opencode/n10-harness`. HOST-ONLY diagnostic opt-in added:
`SOBER_N10_MODIFIED_INPUT=1` (default OFF) permits a mismatched SHA to proceed
**only** when set, printing `MODIFIED-INPUT (no rung)`; `g_w29_arg0_sha_ok`
stays 0 so no rung printer can emit PASS-BOUNDARY-REPLAY. Default remains
fail-closed (baseline rc=9, see `report_n10w1.md`). Ledger DEBT=1, owner=harness,
gate/withhold list unchanged. A/B: allocator patch OFF x3 / ON x3, cores 0-1,
flock, SF_FULL=1, ctor3 OFF, seed OFF, object OFF, no probe bridge.
Input line (6/6, verbatim):
`[avd-boundary] MODIFIED-INPUT (no rung): diagnostic opt-in SOBER_N10_MODIFIED_INPUT=1; sha256=4d50a0ec2eb3f10a8c2c4a987cfc2c3d305e2bac8e0e49f33566b35565296453 want=8215519ff31bb79e66baa65baf9b09fe46ea172f64df33d2399db315fd27634b`
Evidence: `ab_{off,on}_{1,2,3}.err`, `rund6_ab.sh`. ALL RUNS DIAGNOSTIC-ONLY,
NO rung credit.

## FAULTING INSTRUCTION FIRST — PROVEN (6/6)
`guest+0x06caef67` — reported RIP after guest int3 at `0x6caef66`, bytes
`89 c0 e8 62`, `signal=5`, `si_addr=0`, rc=139.
`[W28_FAULT_INSN] rip=... guest_rva=0x06caef67` (off_1/on_1 identical)
`RBXCRASH: HardAssert (Expect to have mimalloc working, but it was not detected!)`

## Latch bytes at fault — PROVEN (6/6)
```
off_1 [N7_FAULT_BYTES] tid=511649 [0x7cdd149]=0x0000000000000001 [0x7cef650]=0x0000000000000000
on_1  [N7_FAULT_BYTES] tid=511998 [0x7cdd149]=0x0000000000000001 [0x7cef650]=0x0000000000000000
```
(off_2/3 and on_2/3 identical pattern; tids 511764/511891, 512111/512239.)
LATCH = **1** with modified input, in both patch arms; `0x7cef650 = 0`.
This differs from the earlier input-missing runs where latch was 0 — the
boundary replay now runs and flips the latch.

## `0x2eb7520` fires? — YES (6/6)
`[N10_LATCH_ARM] base=... fd=4 rc=0` (armed at guest-base hook) and
`[N10_LATCH_HIT 0x2eb7520] hit=1 tid=511649 guest_RA=guest+0x0000000002359342`
— the AVD-reference caller RA `0x2359342`, one hit per run in all 6.

## `N14_REPLAY 3/3` prints? — NO (0/6)
`grep -c 'N14_REPLAY 3/3'` = 0/0/0/0/0/0 — the 3rd native is entered but never
returns, as in earlier waves.

## A/B discriminator — PROVEN
`[N9_ALLOC_PATCH] OFF (default); guest allocator unpatched; 1dbe4fc NOT bridged`
present in `ab_off_{1,2,3}.err` (1/1/1), absent in `ab_on_*` (0/0/0). **No
behavioral difference**: same fault site, same latch bytes, same HardAssert,
same missing 3rd replay in both arms (consistent with static-B #717's
RETRACTED message-switch claim).

## NOT DONE
No ledger edit by me (entry sent); no device/captures/object build; no guest
writes/hand-set bytes/gate edits/seeder/fake returns/probe bridge. The verified
-SHA success path remains UNPROVEN (authoritative w11-arg0.json missing).
No questions asked; DELETE NOTHING honored.

## Evidence
`/tmp/opencode/n10-harness/ab_{off,on}_{1,2,3}.err`, `report_n10w1b.md`,
`rund6_ab.sh`.
