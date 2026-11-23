# OVERNIGHT n1-3 #654 — harness (one page)

Worktree `/tmp/opencode/n1-harness` branch n1-harness. SOLE flock, cores 0-1,
3 Run D, logs 155 KB each. No device/guest writes/gate/seeder/fake returns/path
remaps; TSD seed OFF, JNI_OnLoad-first ON. SHA verified every run:
`[avd-boundary] verified /tmp/opencode/w11-arg0.json bytes sha256=8215519ff31bb79e66baa65baf9b09fe46ea172f64df33d2399db315fd27634b`
Config quotes: `[app-shell] W24 JNI_OnLoad FIRST 0x227af5a -> ver=0x10006`,
`[app-shell] W30 TSD seed REMOVED (default OFF; harness-fabricated state)`.
Evidence: `n13_{1,2,3}.err`, `rund3_n13.sh`.

## FAULTING INSTRUCTION FIRST — PROVEN (stack guard, 3/3)
| run | faulting instruction | si_addr |
|---|---|---|
| 1,2 | `guest+0x2ea6446` `41 54` = **`push r12`** (TLS accessor prologue) | `rsp-8` |
| 3 | `guest+0x6ca7f95` `call 0x2a13350` | `rsp-8` |
Raw: `[W28_FAULT_INSN] rip=0x00007f91ac20a446 guest_rva=0x02ea6446`,
`[W30_FAULT] rip=0x00007f91ac20a446 si_addr=0x00007ffdbd163ff8 rsp=0x00007ffdbd164000 ...`,
`[W28_FAULT_INSN] rip=0x00007fcc2cbdbf95 guest_rva=0x06ca7f95`.
**No termination** — still stack overflow; the exact push/call varies.

## Rule 1 (SA_ONSTACK) — PROVEN
`w21_arm()` now sets `SA_SIGINFO | SA_ONSTACK`; the process altstack was already
installed by `install_crash_handlers()` (`sigaltstack` 64 KB). **All 3 faults
have `guest_rva`** (none inside a perf handler) — the n1-2 handler fault
(host `+0x372ef`, 2/3) is GONE. Ledger entry sent lead.

## Rule 6 (missing natives replay) — PROVEN applied, did NOT terminate
Raw, 3/3:
`[app-shell] N1_RULE6 1 nativeSetPreferencesFile 0x246515d("prefs") -> rc=-6066930334832433271`
`[app-shell] N1_RULE6 2 nativeInitializeNativeFlags 0x2465210(String[4]) -> rc=0`
Flag names are the 4 verbatim tokens from `w12-j2n-300.log:629` (last truncated
in the capture). Path args remain per-run `mkdtemp` temp-root environment config
(unchanged). Ledger entry sent lead.

## Why recursion persists — PROVEN
`[N1_AL #1] tid=1659654 al=0x00 al_dec=0` (al=0 at every sampled hit, 3/3);
ring tail `[00000000000092a7] rax=0x00005557be4119b0 [rax]=0x00007f91a8c0c140 guard=0x0000000000000000 tid=1659654` (all 20 entries identical, guard 0).
fn[224] still flips: `[N1_CALL720] hit=1 ... [rax+0x720]=0x555795dc5750 decode=host+0x000000000002e750` →
`hit=100 ... [rax+0x720]=0x7f91b000bf72 decode=guest+0x0000000006ca7f72`.
Rung `[checkpoint] rung1 PASS-BOUNDARY-REPLAY (DEBT=1)` 3/3 — label only; marker WITHHELD.

## NOT DONE / RETRACTED
No fix beyond the two pre-approved host fixes; no guest bytes/gate/ledger edit
by me (entries sent); rate limit not hit. RETRACTED: any implication that rule 6
terminates the recursion — it does not (3/3 rc=139). UNPROVEN: who writes
fn[224] with the guest self-pointer (static-b proved the writer is guest
`0x6c8e4cc/4d3`), why `prefs` returns the benign bridge sentinel rc, and the
4th flag token's full name (capture truncated). No questions asked.

## Evidence
`/tmp/opencode/n1-harness/n13_{1,2,3}.err`, `report_n13.md`, `rund3_n13.sh`.
