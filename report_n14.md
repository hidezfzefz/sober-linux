# OVERNIGHT n1-4 #657 — harness (one page)

Worktree `/tmp/opencode/n1-harness` branch n1-harness. SOLE flock, cores 0-1,
Run D x3 + x3 diagnostic, logs 143-149 KB. No device/guest writes/gate/seeder/
fake returns/path remaps; TSD seed OFF, JNI_OnLoad-first ON. SHA verified every
run: `[avd-boundary] verified /tmp/opencode/w11-arg0.json bytes sha256=8215519ff31bb79e66baa65baf9b09fe46ea172f64df33d2399db315fd27634b`
Config: `[app-shell] W24 JNI_OnLoad FIRST 0x227af5a -> ver=0x10006`;
`[app-shell] W30 TSD seed REMOVED (default OFF; harness-fabricated state)`.
Evidence: `n14_{1,2,3}.err`, `n14s_{1,2,3}.err`, `rund3_n14.sh`.

## FAULTING INSTRUCTION FIRST — PROVEN
**Full 3-native replay (3/3):** fault INSIDE the 3rd native call, before its
log line: `rip=0x00007f6508f078ff` (host/shared-lib, no guest_rva), bytes
`48 8b 47 08 74 05 48 85` = `mov rax,[rdi+0x8]; je; test rax,rax`,
`si_addr=0x8` — NULL+8 deref, **not** a stack-guard fault. Raw:
`[W30_FAULT] rip=0x00007f6508f078ff si_addr=0x0000000000000008 rsp=0x00007fff907cd540 ...`
**Diagnostic, 3rd skipped (3/3):** recursion persists, stack-guard faults:
S1 `guest_rva=0x06ca7f95` (`call 0x2a13350`), S2/S3 `...bef4`
(`mov [rsp-0x10],rsi`), `si_addr=rsp-8/rsp-0x10`. rc=139 in all 6 runs.
**Does recursion terminate? NO.**

## Replay + obj+0x28 guard (read-only process_vm_readv) — PROVEN
Raw, 3/3:
`[N14_REPLAY 1/3] nativeOnPreCreated 0x23239c5("ActivitySplash") -> rc=-6066930334832433271`
`[N14_GUARD after nativeOnPreCreated] slot=0x55ae5fd039b0 obj=0x7f64f95a0140 guard=0000000000000000 tid=1672729`
`[N14_REPLAY 2/3] nativeOnResumed 0x2328066("ActivitySplash") -> rc=-6066930334832433271`
`[N14_GUARD after nativeOnResumed] slot=0x55ae5fd039b0 obj=0x7f64f95a0140 guard=0000000000000000 tid=1672729`
The 3rd native's log line is ABSENT in all 3 runs — it faulted before returning.
Guard stayed 0 after both successful replays: **neither reaches the +0x28
installer chain in this configuration** (or the write does not persist).

## Third native — UNPROVEN reconstruction
`w12-j2n-300.log:567`: `J2N #552 ... nativePostClientSettingsLoadedInitialization3([object Object])`
— the argument is opaque in the capture. A generic `java/lang/Object` and then
the harness's real native-backed `shared_param_obj` were both tried; both fault
at `[rdi+8]` with `si_addr=0x8`. No faithful reconstruction is possible from the
capture; no fake handle was fabricated.

## Ledger + NOT DONE
Ledger entry sent lead: "n1-4 replay of captured pre-BaseDirs natives
0x23239c5/0x2328066 genuine via host bridge; #552 opaque object unreconstructable;
guard stays 0; recursion persists; DEBT". No ledger edit by me; rate limit not
hit. UNPROVEN: the real #552 object's type/native handle, whether a genuine
real-boot #552 would set +0x28, and the host module at `...08ff`. No questions
asked; DELETE NOTHING honored.

## Evidence
`/tmp/opencode/n1-harness/n14_{1,2,3}.err`, `n14s_{1,2,3}.err`, `report_n14.md`.
