# n8 WAVE4 #712 — harness (one page)

Worktree `/tmp/opencode/n8-harness` branch n8-harness. SOLE flock, cores 0-1,
Run D x3 with `SOBER_N8_CTOR3=1` + early-armed (pre-ctor-replay) READ-ONLY perf
exec bps. No guest writes/hand-set bytes/gate/seeder/fake returns/remaps.
SHA banner every run:
`[avd-boundary] verified /tmp/opencode/w11-arg0.json bytes sha256=8215519ff31bb79e66baa65baf9b09fe46ea172f64df33d2399db315fd27634b`
Evidence: `n8e_{1,2,3}.err`, `rund3_n8e.sh`.

## FAULTING INSTRUCTION FIRST — PROVEN
`guest+0x6caef67` — reported RIP after guest int3 at `0x6caef66` (HardAssert),
`signal=5`, `si_addr=0`, rc=139 3/3.
`[W28_FAULT_INSN] rip=0x00007f034b572f67 guest_rva=0x06caef67`
`RBXCRASH: HardAssert (Expect to have mimalloc working, but it was not detected!)`

## Arming (perf limit: 4 of 7) — PROVEN
`[N8E_ARM] base=0x7f03448c4000 fds=4/5/6/7/-1/-1/-1 rc=0/0/0/0/-1/-1/-1`
(3/3): SIZE_RET `0x238463c`, AFTER_SIZE `0x2383d42`, PRINTER `0x239d215`,
FLAG_TEST `0x239eb14` armed; FLAG_BR `0x239eb1b`, AL_TEST `0x23a1422`,
ASSERT_CALL `0x23a146e` FAILED (hardware slot limit).

## Hit order / tid / values — PROVEN (3/3, identical order)
```
3748:[N8E SIZE_RET 238463c] hit=1 tid=2756178 rax=0x0000000000000000 al=0 ZF=1 CF=0
3749:[N8E AFTER_SIZE 2383d42] hit=1 tid=2756178 rax=0x0000000000000000 al=0 ZF=1 CF=0
3750:[N8E PRINTER 239d215] hit=1 tid=2756178 rax=0x0000000000000002 al=2 ZF=1 CF=0
3751:[N8E FLAG_TEST 239eb14] hit=1 tid=2756178 rax=0xabcdef0123456701 al=1 ZF=0 CF=0
```
then `RBXCRASH: HardAssert ...` and `W30_FAULT`, all on tid 2756178.

## Which edge after `size()==0`? — PROVEN
- `SIZE_RET` shows the size helper returned **0**; `AFTER_SIZE` at `0x2383d42`
  shows `rax=0` and **ZF=1** (from `test eax,eax` at `0x2383d40`) ⇒ the
  **`jle 0x23844e9` is TAKEN** — a normal early return (canary check + `ret`),
  **not** a throw/exception path. This RETRACTS any claim that an exception is
  raised directly by `size()==0`.
- The fatal path is reached LATER and separately: `PRINTER 0x239d215` then
  `FLAG_TEST 0x239eb14` (all before the HardAssert on the same tid). The flag
  is ZERO (`[N8_CTOR3] after 0x7cdede0=0x00`), so per static polarity
  `cmpb $0,[0x7cdede0]` sets ZF=1 and **`je 0x23a1422` is the taken edge**,
  leading onward to the HardAssert call `0x23a146e` → int3.
- Caveat: the bp at the branch `0x239eb1b` itself was unarmed, so the edge
  `je 0x23a1422` is inferred from the observed zero flag byte + static decode,
  but the earlier `jle 0x23844e9` is directly supported by the logged ZF=1.

## NOT DONE
No ledger edit; no device; no object build. UNPROVEN: direct hit evidence for
FLAG_BR / AL_TEST / ASSERT_CALL (unarmed), and the exact guest flow between
the early normal return and the later printer call. No questions asked;
DELETE NOTHING honored.

## Evidence
`/tmp/opencode/n8-harness/n8e_{1,2,3}.err`, `report_n8e.md`, `rund3_n8e.sh`.
