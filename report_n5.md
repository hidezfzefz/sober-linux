# n5 #678 — harness (one page)

Worktree `/tmp/opencode/n5-harness` branch n5-harness, fresh `build-n5`. SOLE
flock, cores 0-1, Run D x3 with `SOBER_BIONIC_SF_FULL=1`, object OFF, logs
~149 KB. No device/guest writes/gate edits/seeder/fake returns/remaps; SHA
verified every run:
`[avd-boundary] verified /tmp/opencode/w11-arg0.json bytes sha256=8215519ff31bb79e66baa65baf9b09fe46ea172f64df33d2399db315fd27634b`
Evidence: `n5_{1,2,3}.err`, `rund3_n5.sh`, prior baseline `n4_*.err`.

## FAULTING INSTRUCTION FIRST — PROVEN (new fault; libc fault GONE)
`guest+0x6caef67`, bytes `89 c0 e8 62 ...`, `si_addr=0x0`, **signal=5 (SIGTRAP,
guest assert)**. Raw:
`[W30_FAULT] rip=0x00007f12da5d6f67 si_addr=0x0000000000000000 rsp=0x00007ffd99cc9420 rbp=0x00007ffd99cc9480 insn=0000000000000089 00000000000000c0 00000000000000e8 ...`
`[W28_FAULT_INSN] rip=0x00007f12da5d6f67 guest_rva=0x06caef67`
`signal=5 tid=1952583 rip=0x00007f12da5d6f67 si_addr=0x0000000000000000 ...`
3/3 identical. **libc+0x7c8ff fault is GONE** (was the n3/n4 fault); the new
fault is in guest code at a different site.

## (e) SF_FULL confirmed — PROVEN + ledger sent
Script line: `export SOBER_BIONIC_SF_FULL=1` (`rund3_n5.sh:10`). Runtime:
`[bionic-sf] full __sF stdio bridge installed (host __sF=0x55829d0d5140, size=0x200)`
`[bionic-sf] stdio GOT bound: fwrite 0x7229748 ...; fflush 0x7229750 ... (guest base 0x7f12d3928000)`
Ledger entry sent: **"Bionic __sF ABI fidelity, defect removal"** (host-only,
owner=harness, DEBT).

## (a) Shim ring implemented but ZERO entries — PROVEN
Ring in host executable (`sober_stdio_ring_note`), weak-called from
fflush/fwrite/fclose shims, dumped by crash handler. Dump header present 3/3:
`[W40_STDIO_RING] last 32 fflush/fwrite/fclose calls (seq,tid,FILE*,caller,guest_rva,in_sF):`
but **0 entry lines** (`grep -c ' in_sF=' = 0`): SF_FULL rebinds the guest GOT
to its own `sf_fflush`/`sf_fwrite`, bypassing these shims — so no tid/FILE*/RA
was recorded. Ring mechanism is host-only and functional in construction;
under SF_FULL it is not exercised. UNPROVEN: exact bypass path (static scope).

## (b) RET64 every fault — PROVEN
Unconditional RET64 scan now runs: `grep -c W30_RET64 n5_{1,2,3}.err = 1/1/1`
(was 0/0/0 in n4). First guest RVA in RET64: `guest+0x00000000003a30e8`
(then `guest+0x2ea6460`, `guest+0x4c0d2b`).

## (c) Last 15 stderr lines before W30_FAULT — PROVEN
Include `[N14_REPLAY 1/3] nativeOnPreCreated 0x23239c5("ActivitySplash") -> rc=-6066930334832433271`,
`[N14_REPLAY 2/3] nativeOnResumed 0x2328066("ActivitySplash") -> rc=...`, both
guards with `guard=0000000000000000`, then:
`RBXCRASH: HardAssert (Expect to have mimalloc working, but it was not detected!)`
**3rd `N14_REPLAY` did NOT print** — the assert fires before it. #552 had not
returned in these runs.

## (d) libc disassembly — PROVEN
`objdump -d /usr/lib64/libc.so.6` at `0x7c8e8`: `mov rdi,QWORD PTR [rdi+0x88]`
(loads FILE._lock), `0x7c8ff: mov rax,QWORD PTR [rdi+0x8]` — NULL when the
tiny `__sF` had a zero `_lock` at +0x88; `0x7c905: test rax,rax` with a NULL
branch. SF_FULL maps to a real host FILE* with a valid `_lock`, removing it.

## NOT DONE
No ledger edit by me (sent); no object build/extension; no device. UNPROVEN:
the guest assert's exact RVA 0x6caef67 meaning and the mimalloc-detection
failure's cause (static scope). No questions asked; DELETE NOTHING honored.
