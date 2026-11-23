<<<<<<< HEAD
# OVERNIGHT n1-5 static-A #661 — #552 jobject from capture alone

Read-only; cores 2-3; no runs/device. Every success claim quotes a raw line.
Evidence: `raw_552_capture.txt`, `raw_shared_param_logs.txt`,
`raw_552_unprov.txt`.

Faulting instruction first (n14 harness): `0x2328066`-class native path; third
native `0x2383c18` faulted inside, `rip=0x…08ff si_addr=0x8`, bytes
`48 8b 47 08 74 05 48 85` = `mov rax,[rdi+8]; je; test rax,rax`.

## 1. What the captures actually contain — PROVEN

Quoted raw line `/tmp/opencode/w12-j2n-300.log:567`:
> `J2N #552 t=290696ms com.roblox.engine.jni.NativeGLInterface.nativePostClientSettingsLoadedInitialization3([object Object])`

That is the **entire** record of the argument: the literal text `[object Object]`
from a JS `toString()`. It contains **no class name, no field names, no values,
no native handle, no address, and no type tag**. Neighbouring lines are equally
opaque (`J2N #555 … setRequestHandlerRaw(..., [object Object])`).

Searches of the other named captures:
- `/tmp/opencode/w11-j2n-first10.txt`: **no** `PostClientSettings` or
  `shared_param` line (it covers only the first ~10 s, entries #1–#45).
- `/tmp/opencode/w11-arg0.json`: no `#552` occurrence — it is the flag JSON
  (arg0 of `nativeInitClientSettings`, quoted as `J2N #551`), not an object dump.

The `shared_param_obj=0x…` lines found in logs are **harness-constructed**, not
captured device state, e.g. quoted:
> `[app-shell] JNI args: base=… ver=… chan=… am=… cfg=… shared_param_obj=0x555556be4eb0 (blob=0x1000)` (w31-harness/runD_1.err:4021)

and the same shape in `analysis/abort/repro-*.err`. They record the harness's
own allocation, with a `blob=0x1000` size, but no guest-side structure.

## 2. Is faithful reconstruction possible from captured data alone — NO

Quoted from the n1-4 harness report (`report_n14.md`, preserved in
`raw_552_unprov.txt`):
> `— the argument is opaque in the capture. A generic java/lang/Object and then`
> `the harness's real native-backed shared_param_obj were both tried; both fault`
> `at [rdi+8] with si_addr=0x8. No faithful reconstruction is possible from the`
> `capture; no fake handle was fabricated.`

Quoted from `FABRICATION-LEDGER.md`:
> `UNPROVEN: #552 opaque object reconstruction (generic Object and real shared_param_obj both fault at [rdi+8] si_addr=0x8), host module at ...08ff; no fake handle fabricated.`

So both candidate reconstructions failed on the guest's `+8` dereference; the
capture gives no type/handle/field information to constrain a faithful object.
Any reconstruction would be a fabricated guess, which is forbidden.

UNPROVEN: the real #552 object's Java type, native handle, field layout, and
whether a genuine real-boot #552 sets `obj+0x28`. No raw line supplies them.

## What NOT done
No diff applied, no patch, no runs/gdb/perf, no device/AVD/Frida/phone, no new
captures, no guest writes, gate edits, seeder, fake handles, ledger edits, no
credentials. Did not read `/tmp/opencode/uploads`.

## First divergence + next step
First divergence: capture records #552 only as `[object Object]` while the
guest dereferences `[rdi+8]`. Next: requires a new genuine capture of the #552
argument's type/fields (out of scope for this read-only wave).
=======
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
>>>>>>> n1-harness
