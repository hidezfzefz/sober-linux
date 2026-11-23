# n3 #672 — harness (one page, READ-ONLY)

Worktree `/tmp/opencode/n3-harness` branch n3-harness, fresh `build-n3`
(inherited stale `build/` left untouched). SOLE flock, cores 0-1, Run D x3,
logs 149 KB. No device/guest writes/gate/seeder/fake returns/path remaps;
object OFF (no build/extension), JNI_OnLoad-first ON, TSD seed OFF. SHA verified
every run:
`[avd-boundary] verified /tmp/opencode/w11-arg0.json bytes sha256=8215519ff31bb79e66baa65baf9b09fe46ea172f64df33d2399db315fd27634b`
Evidence: `n3_{1,2,3}.err`, `rund3_n3.sh`.

## FAULTING INSTRUCTION FIRST — PROVEN
`rip=0x00007fe629ee08ff` (host/shared-lib; no `guest_rva`), bytes
`48 8b 47 08 74 05 48 85` = **`mov rax,[rdi+0x8]; je; test rax,rax`**,
`si_addr=0x8`. Raw:
`[W30_FAULT] rip=0x00007fe629ee08ff si_addr=0x0000000000000008 rsp=0x00007ffeca01aae0 rbp=0x00007ffeca01ab50 insn=0000000000000048 000000000000008b 0000000000000047 0000000000000008 ...`
`[W25_FAULT_ARGS] rdi=0x0000000000000000 rax=0x0000000000000000`
— the inner rdi at the fault is **NULL** (identical 3/3); `si_addr=0x8` is
`[NULL+8]`. rc=139 3/3.

## #552 native entry exec bp — PROVEN
`[N3_ENTRY_ARM] base=0x7fe61acd0000 fd_rc=0`
`[N3_552_ENTRY] tid=1876773 rip=base+0x2383c18 rdi=0x56302bd802d0 rsi=0x56302d66d260 rdx=0x56302c1ba750 rcx=0x56302c1ba750 [rdx]=0x56300e0ebec0 [rdx+8]=0x1 rdi_is_env=1`
At entry `rdi` is the bridge JNIEnv (`rdi_is_env=1`), `rsi` the jclass,
`rdx == rcx` the jobject. **Inner-rdi source: it is NOT the entry `rdi` (env)
nor `rdx`/`[rdx]` (both valid) — the faulting rdi is 0**, so it originates
downstream (static-A tracing the exact producer; UNPROVEN here).

## JNI ring entries naming the object — PROVEN
From `[W24_JNI_RING] last 64 calls (slot,name,arg0,arg1,ret,exc):`, 1/1/1 per run:
`[0x0000000000000020] slot=0x000000000000008e name=size a0=0x000056302c1ba750 a1=0x000056302c185200 ret=0x0000000000000000 exc=0x0000000000000000`
— `a0 == rdx` at #552 entry: the guest **did** call `size()` on the object.
Also present: `name=java/util/List ... ret=0x...` and
`name=com/roblox/engine/jni/model/App ... ret=0x...` (truncated class names),
i.e. FindClass results for the two DEX classes.
**Counts per run:** `size` 1, `get` 0, `GetObjectClass` 0, `GetMethodID` 0
(named ring entries). **RETRACTED:** "zero calls = guest never read the list" —
one `size` call is PROVEN 3/3; `get` and the two class-lookup names were never
called, so the element/PID path was not exercised.

## NOT DONE / UNPROVEN
No ledger edit; no object build/extension; no device/captures. UNPROVEN: exact
producer of the faulting NULL rdi, the host module at `...08ff`, and why `size`
returned 0 while the guest still faulted. No questions asked; DELETE NOTHING
honored.

## Evidence
`/tmp/opencode/n3-harness/n3_{1,2,3}.err`, `report_n3.md`, `rund3_n3.sh`.
