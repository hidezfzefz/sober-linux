# OVERNIGHT n1-5 #660 — harness (one page)

Worktree `/tmp/opencode/n1-harness` branch n1-harness. SOLE flock, cores 0-1,
8 read-only scan runs, logs <160 KB each. No device/guest writes/gate/seeder/
fake returns/path remaps; TSD seed OFF, JNI_OnLoad-first ON. SHA verified every
run: `[avd-boundary] verified /tmp/opencode/w11-arg0.json bytes sha256=8215519ff31bb79e66baa65baf9b09fe46ea172f64df33d2399db315fd27634b`
Config: `[app-shell] W24 JNI_OnLoad FIRST 0x227af5a -> ver=0x10006`;
`[app-shell] W30 TSD seed REMOVED (default OFF; harness-fabricated state)`.
Evidence: `n15scan{,2,3,4,5,6,7,8}.err`, `report_n15.md`.

## FAULTING INSTRUCTION FIRST — PROVEN (varies, stack guard)
8 read-only scans, all rc=139; distinct sites:
`guest+0x06ca7f95` (`call 0x2a13350`) — scans 2,3,7,8 (4/8);
`guest+0x02a13354` (`call 0x2a0de60`) — scans 1,4,6 (3/8);
`guest+0x02ea6446` (`push r12`) — scan 5 (1/8).
Raw: `[W28_FAULT_INSN] rip=0x00007f918fa9f354 guest_rva=0x02a13354`,
`[W28_FAULT_INSN] rip=0x00007f6a259e7f95 guest_rva=0x06ca7f95`,
`[W28_FAULT_INSN] rip=0x00007f1c63e76446 guest_rva=0x02ea6446`.
Same stack-guard class as n1-3/n1-4; no termination.

## BLOCKED — no genuine closure pointer found
Read-only scanner (`process_vm_readv`) walked the live obj returned by the
genuine guest slot getter `0x2a0de60`, over `obj-0x2000 .. obj+0x2000` (8-byte
steps), applying the contract from static-A #656 / static-B #653:
`value & 0x0000ffffffffffff` canonical, `[p]` = fn ptr, `[p+8]` = ctx ptr.
Raw scan header:
`[N15_SCAN] obj=0x7f1c60878140 scanning qwords obj-0x2000..obj+0x2000 tid=1684513`
**Result:** a purely structural candidate appears in 3/8 scans at `obj-224`:
`[N15_CLOSURE] obj-224 raw=0x17f5bdf370380 masked=0x7f5bdf370380 fn=0x7f5bdf326140 ctx=0x7f5be249a1d0`
but it FAILS the genuineness check: obj's mapping is anonymous RW data
`[N15_MAP obj] 7fe91b973000-7fe91bf57000 rw-p 00000000 00:00 0`, and `fn`
is **0x755ec0 BELOW the guest load base** (computed from the same run's
`rip guest_rva=0x06ca7f95`) — i.e. anonymous RW heap, **not executable guest
text**. It is a data/vtable-like pair, not a closure function pointer. No
candidate at all in 5/8 scans.

**BLOCKED per task rule: no genuine closure/task pointer exists in the scanned
guest state. NO installer call was made and NO handle was fabricated.**

## NOT DONE / UNPROVEN
No ledger entry (none warranted — no fix applied); no call to `0x2a0e8c0`; no
guest writes/gate edits/seeder/fake returns/path remaps; rate limit not hit.
UNPROVEN: closure pointers outside the scanned ±0x2000 window, and whether a
real-boot closure would appear only after a missing capture-era event. No
questions asked; DELETE NOTHING honored.

## Evidence
`/tmp/opencode/n1-harness/n15scan{,2,3,4,5,6,7,8}.err`, `report_n15.md`.
