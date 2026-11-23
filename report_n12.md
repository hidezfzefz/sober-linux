# OVERNIGHT n1-2 #649 — harness (one page)

Worktree `/tmp/opencode/n1-harness` branch n1-harness. SOLE flock holder, cores
0-1, 6 Run D with logs 151-154 KB each. No device/captures/guest writes/gate
edits/seeder/fake returns/path remaps; TSD seed OFF, JNI_OnLoad-first ON. DELETE
NOTHING honored (new `n12_*.err/.out`, fresh `build-n1` from n1-1).
SHA verified ALL runs: `[avd-boundary] verified /tmp/opencode/w11-arg0.json bytes sha256=8215519ff31bb79e66baa65baf9b09fe46ea172f64df33d2399db315fd27634b`
Config: `[app-shell] W24 JNI_OnLoad FIRST 0x227af5a -> ver=0x10006` (6/6).
Evidence: `n12_with_{1,2,3}.err`, `n12_without_{1,2,3}.err`, `rund6_n12.sh`.

## FAULTING INSTRUCTION FIRST — PROVEN (varies)
**WITH bps:** only run 1 faults in the guest: `guest+0x2a1335e`, bytes
`48 8b 00` = `mov rax,[rax]`, `si_addr=0x0`:
`[W30_FAULT] rip=0x00007f4b10bbb35e si_addr=0x0000000000000000 rsp=0x00007fff7fdbd6b0 ...`
Runs 2-3 fault INSIDE the perf handler `n1_pred_sig` at host `+0x372ef`
(`mov [rsp+0x30],rbp`), `si_addr=rsp+0x30` — the handler has no SA_ONSTACK and
runs on the exhausted guest stack. **RETRACTED:** "with-bp fault is always
`mov rax,[rax]` / si_addr=0" — 1/3 here.

**WITHOUT bps — distinct faulting instructions (count, si_addr):**
| instruction | count | si_addr |
|---|---|---|
| `guest+0x6ca7f95` `e8 b6 b3 d6 fb` `call 0x2a13350` | 2/3 (without_1,2) | `rsp-8` |
| host/shared-lib `...bef4`, bytes `48 89 74 24 f0` = `mov [rsp-0x10],rsi` | 1/3 (without_3) | `rsp-0x10` |
Raw: `[W28_FAULT_INSN] rip=0x00007fe8e5aebf95 guest_rva=0x06ca7f95` and
`[W30_FAULT] rip=0x00007f05b53beef4 si_addr=0x00007ffe5a9afff8 rsp=0x00007ffe5a9b0008 ...`
**Stack guard IS the sole cause WITHOUT bps (3/3):** every fault is a write
below `rsp` into the guard page (`si_addr = rsp-8` for `call`, `rsp-0x10` for
the prologue); only the instruction differs.

## Pre-call ring (last 20 at 0x2a1335e) — PROVEN
`[N1_RING] last 20 pre-call entries at 0x2a1335e (seq,rax,[rax],[[rax]+0x28],tid):`
`[000000000000928f] rax=0x0000562b91e759b0 [rax]=0x00007f4b0da9a280 guard=0x0000000000000000 tid=1641790`
All 20 entries in each with-bps run are the SAME triple (1 distinct per run):
valid slot ptr, valid obj, guard=0. The last observed pre-call state before the
fault was **not NULL** — the with-bp NULL deref is NOT explained by the ring.

## New first divergence
Guard stays 0 and the predicate returns FALSE (n1-1 `al=0`), keeping the
`0x6ca8014` recursion alive. Without bps it dies by stack-guard write at a
recursion-path instruction; with bps the perf handler perturbs the site (NULL
deref at `0x2a1335e` in 1/3, handler stack fault in 2/3). Ring values stay valid
throughout.

## NOT DONE
No fix applied (read-only); rate limit not hit; who writes fn[224] and why the
with-bp fault shows `si_addr=0` with a valid slot in the ring are UNPROVEN;
without_3's host module not resolved (no guest_rva emitted). No questions asked.

## Evidence
`/tmp/opencode/n1-harness/n12_with_{1,2,3}.err`, `n12_without_{1,2,3}.err`,
`report_n12.md`, `rund6_n12.sh`.
