# n4 #674 — harness (one page)

Worktree `/tmp/opencode/n4-harness` branch n4-harness, fresh `build-n4`
(inherited stale `build/` left untouched). SOLE flock, cores 0-1, Run D x3
object OFF, logs 149 KB. No device/guest writes/gate edits/seeder/fake returns/
remaps. SHA verified every run:
`[avd-boundary] verified /tmp/opencode/w11-arg0.json bytes sha256=8215519ff31bb79e66baa65baf9b09fe46ea172f64df33d2399db315fd27634b`
Config: `[app-shell] W24 JNI_OnLoad FIRST 0x227af5a -> ver=0x10006`,
`[app-shell] W30 TSD seed REMOVED (default OFF; harness-fabricated state)`.
Evidence: `n4_{1,2,3}.err`, `rund3_n4.sh`.

## FAULTING INSTRUCTION FIRST — PROVEN
`rip=0x00007fa51a6518ff`, bytes `48 8b 47 08` = `mov rax,[rdi+0x8]`,
`si_addr=0x8`, fault rdi=0. Raw:
`[W30_FAULT] rip=0x00007fa51a6518ff si_addr=0x0000000000000008 rsp=0x00007ffde8ed4620 rbp=0x00007ffde8ed4690 insn=0000000000000048 000000000000008b 0000000000000047 0000000000000008 ...`
rc=139 3/3; identical RIP-bytes/si_addr pattern in all 3 runs.

## (a) Maps-aware crash handler — PROVEN, ledger sent
Host-only `/proc/self/maps` reader in `crash_handler` uses **open/read/close +
write(2) only** (fixed buffer, no stdio/malloc). Raw RIP line:
`[W40_RIP_MAP] rip0x00007fa51a6518ff offset=0x00000000000588ff maps='7fa51a5f9000-7fa51a749000 r-xp 00024000 00:17 482353                     /usr/lib64/libc.so.6'`
— library `/usr/lib64/libc.so.6`, mapping-relative offset `0x588ff`
(ELF file offset `0x24000 + 0x588ff = 0x7c8ff`).
**RET64 maps lines: NOT EXERCISED** — the RET64 dump only runs when
`|si_addr - rsp| < 64 KiB`; here `si_addr=0x8` vs `rsp≈0x7ffd…` (delta ≫ 64 KiB),
so `grep -c 'W30_RET64|W40_RET64_MAP' n4_1.err = 0`. Code path is implemented
but no first-3 return addresses were captured. Ledger entry sent (host-only,
owner=harness, DEBT).

## (b) First guest RVA — PROVEN (stack scan, RET64 absent)
`[W23_STACK_SCAN] rsp_base=0x00007ffde8ed4620` — first guest-mapped qword:
`guest+0x00000000072a0818` (then `guest+0x2e61d18`, `guest+0x4c0d2b`).
This is the closest available "first guest RVA" evidence; no RET64 line exists
in these runs (see above).

## (c) Symbol resolution — PROVEN
`nm -D /usr/lib64/libc.so.6` → `000000000007c860 T _IO_fflush@@GLIBC_2.2.5`
(and `W fflush@@GLIBC_2.2.5`). `objdump -d` at `0x7c8ff`:
`7c8ff: 48 8b 47 08 mov rax,QWORD PTR [rdi+0x8]`
`7c903: 74 05 je 7c90a <_IO_fflush@@GLIBC_2.2.5+0xaa>`
**Function = `_IO_fflush@@GLIBC_2.2.5 + 0x3f`** (`0x7c8ff - 0x7c860`),
i.e. the NULL-FILE `fflush` path — not guest code. This confirms the earlier
n3 "host/shared-lib `...08ff`" fault site.

## NOT DONE / UNPROVEN
No ledger edit by me (entry sent); no object build/extension; no device.
UNPROVEN: who passes NULL rdi into `_IO_fflush+0x3f`, and the guest call site
leading there (static-A/B scope). No questions asked; DELETE NOTHING honored.

## Evidence
`/tmp/opencode/n4-harness/n4_{1,2,3}.err`, `report_n4.md`, `rund3_n4.sh`.
