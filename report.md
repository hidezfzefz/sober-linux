# OVERNIGHT n1 #645 — harness (one page)

Worktree `/tmp/opencode/n1-harness` branch n1-harness (fresh `build-n1`; inherited
`build/` cache left untouched — DELETE NOTHING). Cores 0-1, flock held for all 6
runs. No device/captures/guest writes/gate edits/seeder/fake returns/path remaps;
TSD seed OFF, JNI_OnLoad-first ON. SHA verified EVERY run:
`[avd-boundary] verified /tmp/opencode/w11-arg0.json bytes sha256=8215519ff31bb79e66baa65baf9b09fe46ea172f64df33d2399db315fd27634b`
Config proof (ALL 6 runs, quoted): `[app-shell] W24 JNI_OnLoad FIRST 0x227af5a -> ver=0x10006`
and `[app-shell] W30 TSD seed REMOVED (default OFF; harness-fabricated state)`;
script `rund6.sh:9` exports `SOBER_SEEDER_DISABLE=1 SOBER_JNI_ONLOAD_FIRST=1`.
Evidence: `with_{1,2,3}.err`, `without_{1,2,3}.err`, `rund6.sh`. Logs 151 KB each.

## FAULTING INSTRUCTION FIRST — PROVEN, CONFIGURATION-DEPENDENT
**WITH bps (3/3):** `guest+0x2a1335e`, bytes `48 8b 00` = **`mov rax,[rax]`**,
`si_addr=0x0` (NULL). Raw with_1:
`[W30_FAULT] rip=0x00007faa0fca735e si_addr=0x0000000000000000 rsp=0x00007fffd71ba630 rbp=0x00007fffd71ba630 insn=0000000000000048 000000000000008b 0000000000000000 ...`
`[W28_FAULT_INSN] rip=0x00007faa0fca735e guest_rva=0x02a1335e`
**WITHOUT bps (3/3): non-deterministic call site, all `si_addr = rsp-8`**
(stack-guard push): without_1 `guest_rva=0x06ca7f95` (`call 0x2a13350`),
without_2 `guest_rva=0x02ea645b` (`call pthread_getspecific@plt`), without_3
`guest_rva=0x02a0de6b` (`call 6c89e70`). Raw without_1:
`[W30_FAULT] rip=0x00007fea4abb7f95 si_addr=0x00007fff4ff2eff8 rsp=0x00007fff4ff2f000 ...`
**RETRACTED:** earlier waves' "the faulting instruction is always
`0x6ca7f95`" — that is only the no-bp configuration (1/3 here).

## Perf exec bps (WITH runs) — PROVEN
`[N1_ARM] base=0x7faa0d294000 host=0x55a82eb50000 pred=0 al=0 call720=0`.
**0x2a1335e** raw: `[N1_PRED #1] tid=1624646 rax=0x55a84a0ac9b0 [rax]=0x7faa0cb86280 [[rax]+0x28]=0000000000000000`
(also #2,#3,#10000,#20000,#30000 — identical; 6 sampled lines, 3/3 runs).
**0x6ca7f9a** raw: `[N1_AL #1] tid=1624646 al=0x00 al_dec=0` — **al=0 at every
sampled hit** (#1..#30000, all 3 runs): the predicate always returns FALSE.
**0x6ca800e** hits 1,2,3,100 raw:
`[N1_CALL720] hit=1 tid=1624646 rax=0x55a84a3dfe80 rbx=0x55a84a3e0618 [rax+0x720]=0x55a82eb7e690 decode=host+0x000000000002e690 bridge_env=0x55a84a3e0618 rax_is_env=0`
`[N1_CALL720] hit=100 tid=1624646 rax=0x7fa9f4001f18 rbx=0x55a84a3e0618 [rax+0x720]=0x7faa13f3bf72 decode=guest+0x0000000006ca7f72 bridge_env=0x55a84a3e0618 rax_is_env=0`
fn[224] flips host bridge → **`guest+0x6ca7f72`** (the function prologue
containing the `call [rax+0x720]`); `rbx == bridge_env` constant, `rax` is not
the env at any logged hit. 3/3 runs identical pattern.

## New first divergence
`[[rax]+0x28]=0` and `al=0` at every sampled predicate hit → FALSE branch at
`0x6ca7f9c` recurses; fn[224] becomes the self-pointer `guest+0x6ca7f72`. The
breakpoint set changes the fault site: bps → NULL deref at `mov rax,[rax]`
(`0x2a1335e`, `si_addr=0`); no bps → stack-guard push at whichever `call` in
the recursion reaches the guard page first (varies per run, always
`si_addr=rsp-8`).

## NOT DONE
No fix applied (read-only); no ledger edit; why with-bps rax at fault is
`0x55a84a0ac9b0` while `si_addr=0` is UNPROVEN; who writes fn[224] UNPROVEN;
total predicate hit count not logged (sampled 1,2,3,10k,20k,30k). No questions
asked. Uploads untouched.

## Evidence files
`/tmp/opencode/n1-harness/with_{1,2,3}.err`, `without_{1,2,3}.err`, `rund6.sh`.
