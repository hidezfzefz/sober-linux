# n2 STEP 3 #669 — harness (one page)

Worktree `/tmp/opencode/n2-harness` branch n2-harness, fresh `build-n2`. SOLE
flock, cores 0-1, Run D x3 ON + x3 OFF, logs 152 KB each. No device/guest
writes/gate edits/seeder/fake returns/path remaps; TSD seed OFF, JNI_OnLoad-first
ON. SHA verified every run:
`[avd-boundary] verified /tmp/opencode/w11-arg0.json bytes sha256=8215519ff31bb79e66baa65baf9b09fe46ea172f64df33d2399db315fd27634b`
Evidence: `obj_on_{1,2,3}.err`, `obj_off_{1,2,3}.err`, `rund6_n2obj.sh`.

## FAULTING INSTRUCTION FIRST — PROVEN (identical ON/OFF)
`rip=...08ff` (host/shared-lib, no guest_rva), bytes `48 8b 47 08` =
`mov rax,[rdi+0x8]`, `si_addr=0x8` — NULL+8 deref after the `size()` handler
returns. Raw ON:
`[W30_FAULT] rip=0x00007f7e54a3d8ff si_addr=0x0000000000000008 rsp=0x00007fffd38ecce0 rbp=0x00007fffd38ecd50 insn=0000000000000048 000000000000008b 0000000000000047 0000000000000008 ...`
rc=139 3/3 both configurations; no termination.

## Object built (DEX-derived, no fabrication) — PROVEN
Types/signatures from authoritative DEX `114a6f8e` (static-A #668); behavior
from `Lyj/l2.a()` bytecode — `Collections.EMPTY_LIST` is an explicit DEX path:
`[N2_OBJ BUILT] DEX-typed java/util/List size=0 get=(I)Ljava/lang/Object; mPid=I class=ApplicationExitInfoCpp list=0x5629eded06f0`
`[N2_OBJ size] List.size()I -> 0 (Collections.EMPTY_LIST DEX path)`
`get()` has a HARD FAIL + abort handler (no silent default): no `N2_OBJ
HARD_FAIL` lines appeared because `size()=0` makes the guest skip the loop at
`0x2383d40: jle 23844e9`. Concrete element/PID values are runtime
`ActivityManager.getHistoricalProcessExitReasons()` data, NOT in DEX — not
fabricated. Env switch `SOBER_N2_JAVA_OBJECT` default OFF. Ledger sent:
owner = harness; removal condition = "a genuine Java runtime supplies this".

## Result: obj+0x28 STILL 0 — STOP, NO EXTENSION
Raw ON, all 3:
`[N1_PRED #1] tid=1845923 rax=... [rax]=... [[rax]+0x28]=0000000000000000`
`[N1_AL #1] tid=1845923 al=0x00 al_dec=0`
Distinct guard values ON/OFF: only `0000000000000000`. Rung line:
`[checkpoint] rung1 PASS-BOUNDARY-REPLAY (DEBT=1)` — label only; gate never
CLEAN; marker WITHHELD. **Recursion NOT gone.** Per lead instruction, STOP:
no extension, no broader object, no additional methods/fields.

## NOT DONE / UNPROVEN
No further build/extensions; no device/captures; no guest writes/gate edits.
UNPROVEN: mPid field read never exercised (no element); why the guest faults at
`[rdi+8]` after `size()` returns 0; the non-empty runtime list contents.
No questions asked; DELETE NOTHING honored.
