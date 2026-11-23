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
