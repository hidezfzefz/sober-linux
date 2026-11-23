# WAVE30 #635 — TLS supplier / 8-of-8 pairs / real-boot terminator

Read-only; no runs/device/phone. Engine SHA `3e498d64…`. No guest writes.
Faulting instruction (named first, W28/W29 raw): `0x6ca229c = call *0x548(%rax)`,
bytes `ff 90 48 05 00 00 00 00`. With seed OFF + pairs bp the primary fault
moves to `0x2a13354: call 0x2a0de60` (`e8 07 ab ff ff`).
Evidence files: `raw_tls_seedoff.txt`, `raw_tls_seedon.txt`, `raw_pairs.txt`,
`raw_capture_order.txt` (each line quoted verbatim from the named log).

## (a) Supplier of the TLS value + 8/8 match — PROVEN (quoted)

The **guest itself** supplies the value; no seed is required. Raw
`/tmp/opencode/w29-harness/seedoff_trace.err:3707`:
> `[W29_TLS 0x6ca228b] tid=1521493 key=3 tls=0x55555568b230 [tls]=0x1 rax=0x5555558e6a28 rsi=0x5555562e0fd0 seed=(nil) [seed]=0 eq=0`

and `seedoff_pairs_1.err:3721`:
> `[app-shell] pre-OnLoad TSD key=3 get=0x55555568b230`

The only guest `setspecific` writers of key 3 are `0x2ea656e` (accessor
`0x2ea6440`, lazy branch: `calloc` state, `movq $1,(%r14)` at `0x2ea6555`) and
`0x2ea6695` (destructor `0x2ea6670`) — static, W29. That matches `[tls]=0x1`
above: the freshly allocated state's +0 is the initial `1`.

Why 8/8 pairs match with seed OFF: all eight `GetStringUTFChars` calls
returned. Raw `seedoff_pairs_1.err:3707,3709,...;3734,...`:
> `[W28_PRE2315 #1] rsi=0x5555563f9850 [rsi]=0x5555563f98f0 rax=0x5555558fe0e8 tid=1521155`
> `[W28_POST6CA2 #1]`
> `[W28_POST2315 #1]`
and PRE/POST #2..#8 follow the same shape (harness report: `pre2315=8,
post2315=8`, contrast seed ON `3 pre / 2 post`). The guest-owned TLS is
therefore sufficient for every observed JNI call pair.

What the guest stores into the TLS value besides initial `1`: a guest string
`0x7265502f666f7270` ("profileP" little-endian) at `[seed+0]`. Raw
`tsdtrace.err:3727` (seed ON, guest replaced the seeded object):
> `[W29_TLS 0x2315556] tid=1519045 key=3 rax=0x5555558fe0e8 [rax]=(nil) rsi=0x5555574b3c30 seed=0x5555559f60f0 [seed]=0x7265502f666f7270 eq=0`

## (b) What terminates recursion in a real boot — PARTLY PROVEN / UNPROVEN

Static terminator: predicate `*(obj+0x28)!=0` at `0x6ca21f4/0x6ca21f9`; when
FALSE, `je 0x6ca2282` takes the direct JNIEnv path and the wrapper returns.
PROVEN from disassembly (quoted instruction bytes in `dis_6ca21c6_6ca22b0.txt`).

Real boot (capture w12) natives before `nativeSetBaseDataDirectories`, quoted
from `/tmp/opencode/w12-j2n-300.log`:
> `J2N #70 t=290171ms com.roblox.engine.jni.NativeSettingsInterface.nativeSetRobloxChannel("")`
> `J2N #534 t=290179ms com.roblox.engine.jni.NativeSettingsInterface.nativeSetRobloxVersion("2.740.931")`
> `J2N #541 t=290202ms com.roblox.engine.jni.NativeGLInterface.nativeGameGlobalInit()`
> `J2N #612 t=290866ms com.roblox.engine.jni.NativeSettingsInterface.nativeSetBaseDataDirectories("/data/user/0/com.roblox.client", "/storage/emulated/0/Android/data/com.roblox.client")`

Which of these inputs/natives/thread makes `*(obj+0x28)==0` is **UNPROVEN**:
no raw line in the capture or harness logs shows a `+0x28` write on those
paths, and no tid is attached to the static writer sites (`0x2a0ed45`,
`0x2a1362f`). With seed OFF the harness shows 8/8 pairs returning and the
primary fault relocated to `0x2a13354` (accessor), so the wrapper recursion
is not observed in that configuration. A specific real-boot terminator input
or thread is therefore not claimed.

## RA (static + log lines, not fault-register inference)
`0x6ca21c6 -> 0x2a13350 (obj+0x28) -> FALSE 0x6ca2282/0x6ca228b -> 0x6ca2290 ->
0x6ca229c call *0x548(%rax) -> 0x6ca22a2`; log anchors `0x2315556`/`0x6ca228b`.

## What NOT done
No diff proposed/applied, no harness edit, no runs/gdb/perf, no device/AVD/
emulator/Frida/phone, no new captures, no guest writes, no gate edits, no
ledger edits, no credentials. Real-boot terminator native/input/thread UNPROVEN.

## First divergence + next step
First divergence: guest-owned TLS (`[tls]=0x1`, 8/8 pairs) vs seeded TLS (guest
replaces it, 3/2 unmatched). Next: harness logs `*(obj+0x28)` at `0x6ca21f4`
with tid, on a real-boot-order replay.
