# n5 #682 A/B — harness (one page)

ONE session, SOLE flock, cores 0-1, object OFF. Run D x3 SF_FULL=1 (A) and x3
UNSET (B). SHA verified every run:
`[avd-boundary] verified /tmp/opencode/w11-arg0.json bytes sha256=8215519ff31bb79e66baa65baf9b09fe46ea172f64df33d2399db315fd27634b`
Evidence: `ab_on_{1,2,3}.err`, `ab_off_{1,2,3}.err`, `rund6_ab.sh`.

## FAULTING INSTRUCTION FIRST — PROVEN
**A (SF_FULL=1), 3/3:** `guest+0x6caef67`, bytes `89 c0 e8 62`, `si_addr=0x0`.
`[W30_FAULT] rip=0x00007f2ade0b6f67 si_addr=0x0000000000000000 rsp=0x00007ffe9eb60770 rbp=0x00007ffe9eb607d0 insn=0000000000000089 00000000000000c0 00000000000000e8 ...`
`[W28_FAULT_INSN] rip=0x00007f2ade0b6f67 guest_rva=0x06caef67`
**B (UNSET), 3/3:** host libc, bytes `48 8b 47 08` = `mov rax,[rdi+0x8]`,
`si_addr=0x8` (the `libc+0x7c8ff` defect).
`[W30_FAULT] rip=0x00007f61b035a8ff si_addr=0x0000000000000008 rsp=0x00007ffc9138e3e0 rbp=0x00007ffc9138e450 insn=0000000000000048 000000000000008b 0000000000000047 ...`
`[W28_FAULT_INSN] rip=0x00007f61b035a8ff` (no guest_rva).

## Banners (switch state) — PROVEN
A: `[N6_BANNER] SOBER_BIONIC_SF_FULL=1`
B: `[N6_BANNER] SOBER_BIONIC_SF_FULL=UNSET`
(3/3 each; rc=139 in all 6.)

## Success criteria — NOT SUCCESS
| criterion | result | evidence |
|---|---|---|
| libc+0x7c8ff fault gone under A | **YES** | A faults at guest RVA `0x06caef67` (3/3); B faults at `...08ff` (3/3) |
| 3rd `N14_REPLAY` prints | **NO** | `grep -c 'N14_REPLAY 3/3'` = 0/0/0 in A and 0/0/0 in B |
| faulting instruction differs | **YES** | A `guest+0x6caef67` vs B host `...08ff` |
**Overall: NOT SUCCESS** (criterion 2 fails in both arms). Last-15 lines in
A end with `RBXCRASH: HardAssert (Expect to have mimalloc working, but it was not detected!)`;
B ends after `[N14_REPLAY 2/3] ... / [N14_GUARD after nativeOnResumed] ... guard=0000000000000000`.
#552 never returned in either arm.

## Ledger (EXACT)
`Bionic __sF ABI fidelity, defect removal; DEBT=1; gate/withhold list unchanged`
Sent to lead verbatim. No gate/marker change; marker stays WITHHELD.

## NOT DONE
No ledger edit by me (entry sent); no object build/extension; no guest writes/
gate edits/seeder/fake returns/remaps; no device. UNPROVEN: why the 3rd replay
never prints (both arms) and the mimalloc-detection assert cause (static scope).
No questions asked; DELETE NOTHING honored.
