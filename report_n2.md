# n2 STEP 1 #665 — harness (one page, CORRECTED)

Worktree `/tmp/opencode/n2-harness` branch n2-harness, fresh `build-n2`
(inherited stale `build/` left untouched). SOLE flock, cores 0-1. READ-ONLY
exec bps; no object built, no guest writes/gate/seeder/fake returns/path remaps.
SHA verified every run:
`[avd-boundary] verified /tmp/opencode/w11-arg0.json bytes sha256=8215519ff31bb79e66baa65baf9b09fe46ea172f64df33d2399db315fd27634b`
Config: `[app-shell] W24 JNI_OnLoad FIRST 0x227af5a -> ver=0x10006`;
`[app-shell] W30 TSD seed REMOVED (default OFF; harness-fabricated state)`.
Evidence: `n2_{1,2,3}.err` (pre-fix), `n2x_{1,2,3}.err` (post-hygiene-fix).

## FAULTING INSTRUCTION FIRST — PROVEN
`rip=...08ff` (host/shared-lib, no guest_rva), bytes `48 8b 47 08` =
`mov rax,[rdi+0x8]`, `si_addr=0x8` — the n1-4 #552 opaque-object fault
(rc=139 3/3, unchanged by the hygiene fix). These exec bps are only the table
test; no fault-site fix was attempted.

## RESULT — RETRACTED: NOT an in-place patch (audit verdict)
My initial `bridge_patched=1` conclusion is **RETRACTED**. The pre-fix raw line
`[N2_TABLE call720 0x6ca800e] tid=1814063 rax=0x55c5b91c63e0 bridge_table=0x55c5b900c078 rax_is_bridge_table=0 [rax+0x720]=0x55c585a89fd0 [bridge_table+0x720]=0x7f8671c6ff72 expect=0x7f8671c6ff72 bridge_patched=1`
shows the ORIGINAL table `rax=0x55c5b91c63e0` still held the host bridge fn
(`[rax+0x720]=0x55c585a89fd0`) — **not** `base+0x6ca7f72`. `n2_bridge_table()`
re-read `*g_bridge_env_ptr` on each hit and therefore followed the pointer
REPLACEMENT (`6c8e512: mov %r14,(%rbx)`, static-B #664) to the guest-private
copy. Audit + static-B PROVEN: `r14@0x6c8e4d3` comes from
`6c8d84d call 6c89e90` (key `0x7282088`) → `6c8d85b mov %rax,%r14` — a
guest-private COPY, distinct from the original bridge table. **KILL SWITCH DID
NOT FIRE.**

## Host-only hygiene fix (pre-approved, STAYS; ledger sent)
`sr_GetEnv` returns `guest_env_obj` whose `functions` = disposable copy
`guest_env_table`; `sober_jni_env()` still exposes pristine `env_table`.
Ledger: owner = harness; removal condition = "a genuine runtime supplies fn[224]
/ guest builder stops replacing the holder's table pointer". Post-fix raw:
`[N2_TABLE call720 0x6ca800e] tid=1821202 rax=0x55b804596b38 bridge_table=0x55b8045963e0 rax_is_bridge_table=0 guest_copy=0x55b804596b38 rax_is_guest_copy=1 [rax+0x720]=0x55b7d8363fd0 [bridge_table+0x720]=0x55b7d8363fd0 expect=0x7f1aca3abf72 bridge_patched=0`
(`rax_is_guest_copy=1` and `bridge_patched=0` in 3/3). Builder raw:
`[N2_TABLE builder 0x6c8e4d3] ... r14=0x55b8043dc078 bridge_table=0x55b8045963e0 r14_is_bridge_table=0 guest_copy=0x55b804596b38 r14_is_guest_copy=0 ...`

## NOT DONE / UNPROVEN
No object built (per lead — awaiting static-A method list); no device/captures;
no guest writes/gate edits. UNPROVEN: the `si_addr=0x8` fault's module, other
table-replacement paths. No questions asked; DELETE NOTHING honored.
