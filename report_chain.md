# OVERNIGHT n1-3 static-A #655 — what runs the `+0x28` installer

Read-only; cores 2-3; no runs/device. Every success claim quotes a raw line.
Evidence: `dis_tailcaller.txt`, `raw_chain_655.txt`, `raw_capture_entries.txt`.

Faulting instruction first: `0x6ca7f95: call 0x2a13350` (`si_addr=rsp-8`).

## 1. Chain from installer `0x2a0ed45` back to entries — PROVEN links

Quoted disassembly (forward direction):
> `2a0ed45: mov %r15,0x28(%rbx)`      — the installer, in fn `0x2a0e8c0`
> `2a0e83b: call 2a0e8c0`             — only direct caller, in fn `0x2a0e7c0`
> `2a14330: movq $0x0,(%rdi)`         — tiny thunk
> `2a14337: jmp 2a0e7c0`              — tail-jump from that thunk into `0x2a0e7c0`
> `23c1136 / 251e4ce / 251fb4d / 29cb5c5 / 29f91c9 / 3187fc7 / 36ddccc /
   447547b / 49677fe / …: call 2a14330` — many direct callers of the thunk

`0x2a14330` has **no raw 8-byte pointer references** in the engine (binary
pointer scan found 0), so it is reached by direct calls, not via a function
pointer table. The containing JNI entry points of those call sites
(nearest preceding symbol header, quoted from the disassembly index) are:
- `0x23c1136` in `Java_com_roblox_engine_jni_NativeGLInterface_nativePostClientSettingsLoadedInitialization3`
- `0x251e4ce`/`0x251fb4d` in `Java_com_roblox_engine_jni_NativeGLInterface_nativeAppBridgeV2InitWithParams`
- `0x29cb5c5`/`0x29f91c9` in `JNIActivityLifecycleCallbacks_native…`
- `0x3187fc7` in `MainGameActivity_nativePreloadFlagOverrides` (crashpad path)
- `0x36ddccc` in `IPlatformExternalIdentity…`, `0x447547b`/`0x49677fe` in
  `IPlatformSystemDialogHandler…`

So the installer is reachable from ordinary JNI calls on real-boot threads —
not exclusively from a constructor, JNI_OnLoad, or a queued task. PROVEN for
the call links and entry functions; the tid of the writer is UNPROVEN.

## 2. Capture-order evidence — which of these run before BaseDirs — PROVEN

Quoted from `/tmp/opencode/w12-j2n-300.log`:
> `J2N #67 t=2406ms … JNIActivityLifecycleCallbacks.nativeOnPreCreated("ActivitySplash")`
> `J2N #105 t=20511ms … JNIActivityLifecycleCallbacks.nativeOnResumed("ActivitySplash")`
> `J2N #552 t=290696ms … NativeGLInterface.nativePostClientSettingsLoadedInitialization3([object Object])`
> `J2N #612 t=290866ms … nativeSetBaseDataDirectories(…)`

So lifecycle callbacks and `nativePostClientSettingsLoadedInitialization3`
run BEFORE `nativeSetBaseDataDirectories`; they are plausible real-boot
callers of the chain above. `nativeAppBridgeV2InitWithParams` was not found
before BaseDirs in the quoted lines (UNPROVEN whether it appears later).

## 3. Which real-boot native/thread sets `+0x28` nonzero — UNPROVEN

The installer `0x2a0ed45` writes a masked pointer from
`2a0ed0f: mov (%rax),%r15` / `2a0ed12: and %r14,%r15` into `obj+0x28`, but no
raw capture or harness line identifies the specific native/tid that makes it
nonzero on a real device. Harness W32 (quoted from its report) recorded
`0x2a0ed45` **0 hits** and `[obj+0x28]=0` always. The related documented
real-boot scheduler action is on the `FunctionMarshaller` worker: A04
(`A04-tsd-key-audit.md:315-317`) says
> `call genuine guest 0x2a132c0(edi=1) once on the real FunctionMarshaller worker immediately after its AttachCurrentThread, so the guest itself writes obj+0x70 = (obj+0x70 & ~0x3f) | 2`

— that is `+0x70` (nesting), **not** `+0x28`. No equivalent quoted line for a
`+0x28` writer was found; the specific real-boot native/thread remains
**UNPROVEN**.

## What NOT done
No diff applied, no patch, no runs/gdb/perf, no device/AVD/Frida/phone, no new
captures, no guest writes, gate edits, seeder, ledger edits, no credentials.
Writer tid and the exact real-boot `+0x28`-setting native are UNPROVEN.

## First divergence + next step
First divergence: harness `+0x28` installer never fires (0 hits) while
capture-order lifecycle/post-settings natives do run before BaseDirs. Next:
harness entry breakpoint at `0x2a0ed45` on the FunctionMarshaller thread.
