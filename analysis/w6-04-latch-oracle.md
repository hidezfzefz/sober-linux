# W6-04 — Crashpad subprocess latch ground truth (task #546)

Owner: worker-4. Analysis / research-oracle only. Date 2026-11-21.
Report path (only file written): `analysis/w6-04-latch-oracle.md`. No `src/`, test,
gate, doc, guest byte or git change. Emulator/KVM is used strictly as a
**research oracle**, never as the final runtime (decisions 013/038).

## Authoritative artifacts (provenance)

| Item | Value |
| --- | --- |
| Installed APK | `/home/user/.local/share/basalt/linux/payloads/3170-204ad049599c96afb75b8a21d5051063e821d6c9/sober_apk/base.apk`, SHA-256 `114a6f8ed6f0f974cf632a9cbec2f5cd45e1d85d7ed251db4f3920140bc6ead4` (+ `split_config.x86_64.apk`) |
| Engine identity in-image | app reports `Version = 2.740.931, Code = 3170`, `BuildVariant = googleProdRelease`, channel `production`; frozen engine BuildID `204ad049…` / SHA `3e498d6494…` (A05/EngineFacts) |
| Rejected evidence | `~/Downloads/basalt-arm64*` (MODDED) — not used |
| Latch RVA | `0x7cdd149` (flags-loaded latch); crashpad writer `0x2ec9fae` in `0x2ec9eed`; JNI entry `0x2eca720`; install-only boot export `0x232d6b8`/`0x232de10` |

Research oracles: x86_64 AVD `Medium_Phone_API_37.0` (production image, API 37,
KVM) for boot observation; x86_64 AVD `W6_04RootOracle` (API 35
`google_apis` **userdebug**, SHA1 of image zip verified
`0103e6dab21290c4b9d16550a3ce99476f884eef`, `adb root` → uid 0) for delivering
a genuine native SIGSEGV. `adb logcat -v threadtime` was the observation channel.
Raw captures: `/tmp/opencode/w6-04-{boot,amcrash,root-boot,native-crash}-logcat.txt`.

## 1. Ground-truth verdict

1. **A real native crash does NOT set the flags-loaded latch `0x7cdd149` in the
   main app process.** The crashpad latch writer (`0x2ec9fae`, inside `0x2ec9eed`,
   reached from `nativeRunCrashpadHandler 0x2eca720` / `CrashpadHandlerMain
   0x2ec9c30`) executes in the **crash-handler subprocess**. In the decisive run
   the crashing game process was PID **3046** and the handler that emitted the
   latch-writer log was PID **3826** — a different address space. The latch is a
   process-private `.bss` global (MAP_PRIVATE/COW; A05 §1), so a subprocess write
   cannot propagate to the main process.
2. **The main-process latch is already 1 at normal boot, and it is NOT set by
   crashpad.** Real boot reaches the native TaskScheduler/RenderJob/FLog stages
   (the `0x23862b0` guard would int3 at `0x6caef67` if the byte were 0) with
   **zero** `crashpad handler:` log lines and no handler subprocess present.
   The boot-side DEX method is `rg/w0.h0()` → `nativeInitCrashpad`, which only
   *installs* the handler (log: `initialized crashpad, plug in the sfirst chance
   exception handler`). This corroborates Craft Anything (`CA01` note), A05 and
   task #525. The clean in-process writer is the client-settings / flag-filter
   path `0x2eb7520` (see §5), not the crashpad path.
3. **Consequence for the harness:** there is no clean boot route through a crash
   or the crashpad subprocess. `flags_latch_default.*` keeps `route=none` (inert,
   no guest call, `should_withhold()==0`); `crashpad_return.*` stays opt-in and
   WITHHELD (`sober_crashpad_return_java_boot_reachable()==0`). No gate weakening,
   marker stays **WITHHELD**, `tests/app-shell.sh` stays RED.

## 2. Empirical evidence (emulator / logcat)

### 2.1 Boot, production API 37 image — main PID 5281

`adb install-multiple base.apk split_config.x86_64.apk` → `Success`; launch
`com.roblox.client/.startup.LauncherAliasMain`.

```
11-21 15:42:53.923  5281  5474 I Roblox : ... [FLog::Graphics] Using TM2 in MipPackStream mode
11-21 15:42:55.615  5281  5474 I Roblox : ... [FLog::SurfaceController] SurfaceController[_:1]::start dataModel(...)
11-21 15:42:55.615  5281  5474 I Roblox : ... [FLog::SurfaceController] SurfaceController::RenderJob[_:1]::RenderJob
11-21 15:42:55.622  5281  5281 I Roblox : ... [FLog::JNIAppBridge] nativeAppBridgeV2UpdateSurfaceAppWithPlatformParams:
```

- The app reports `Version = 2.740.931, Code = 3170, Arch = X86_64` — the
  authoritative payload, not the modded download.
- `ps -A` during boot shows **only** `com.roblox.client` (PID 5281, later
  self-restarted to 6196); **no crashpad/sentry subprocess**.
- `grep -i 'crashpad handler' boot-logcat` → **empty**. `grep -i 'RBXCRASH|
  FatalRuntimeError|flags have been loaded' boot-logcat` → **empty**. The
  TaskScheduler gate was passed without any crashpad writer running.

### 2.2 Boot, userdebug API 35 image — main PID 3046

```
11-21 15:59:45.974  3046  3046 I Roblox : ... [FLog::CrashReportLog] Roblox files folder: ...
11-21 15:59:46.043  3046  3046 I Roblox : ... [FLog::CrashReportLog] initialized crashpad, plug in the sfirst chance exception handler
11-21 15:59:46.044  3046  3046 I roblox.config: [w0.h0()-226]: Crashpad monitoring with API unification is ON
```

`w0.h0()` is exactly the boot method CA01 found calls
`nativeInitCrashpad(NativeInitCrashpadParams)` and nothing else. No
`crashpad handler:` line, no handler subprocess at boot. Install-only confirmed
empirically.

### 2.3 Control: Java-level crash (`am crash com.roblox.client`)

```
11-21 15:45:31.731  6196  6196 E AndroidRuntime: FATAL EXCEPTION: main
11-21 15:45:32.052  6196  6196 I Process : Sending signal. PID: 6196 SIG: 9
11-21 15:45:34.101   453   453 I Zygote  : Process 6196 exited due to signal 9 (Killed)
```

`grep -i crashpad` on this window → **empty**. App death by SIG9 does not invoke
the crashpad latch path in the main process.

### 2.4 Decisive: genuine native SIGSEGV in the main process

As root, `kill -SEGV` was delivered to TID 3047 of the game main PID 3046
(kernel: `Fatal signal 11 (SIGSEGV), code 0 (SI_USER from pid 3824, uid 0) in tid
3047 (Signal Catcher), pid 3046 (m.roblox.client)` — a genuine guest SIGSEGV,
handled by the app's own installed crashpad client).

```
11-21 16:00:15.407  3046  3047 I Roblox  : ... [FLog::CrashReportLog] Run book keeping for signal 11
11-21 16:00:15.456  3046  3047 I chromium: [3046:3047:...:INFO crashpad_client_linux.cc:337] Handle a real crash
```

Immediately afterwards a **separate process, PID 3826**, appears and emits every
handler/latch-writer log line:

```
11-21 16:00:18.535  3826  3826 I rbx.JNIRobloxSettings: CrashpadHandler is running in-process
11-21 16:00:18.535  3826  3826 I rbx.JNIRobloxSettings: crashpad handler: invoke crashpad::HandlerMain with arguments
11-21 16:00:18.535  3826  3826 I rbx.JNIRobloxSettings:     --database=/data/user/0/com.roblox.client/cache/crashpad
11-21 16:00:18.535  3826  3826 I rbx.JNIRobloxSettings:     --metrics-dir=/data/user/0/com.roblox.client/cache/crashpad
11-21 16:00:18.536  3826  3826 I rbx.JNIRobloxSettings:     --url=https://uploads.backtrace.rbx.com/post
11-21 16:00:18.536  3826  3826 I rbx.JNIRobloxSettings:     --annotation=UseCrashpad=True
11-21 16:00:18.536  3826  3826 I rbx.JNIRobloxSettings: crashpad handler: real crash.     <-- latch-writer signature
11-21 16:00:22.203  3826  3826 I rbx.JNIRobloxSettings: crashpad handler: succeed
```

Then the game main process dies:

```
11-21 16:00:24.541  3956  3956 F DEBUG   : signal 11 (SIGSEGV), code 0 (SI_USER from pid 3824, uid 0), fault addr --------
11-21 16:00:25.020   382   382 I Zygote  : Process 3046 exited due to signal 11 (Segmentation fault)
11-21 16:00:25.203   603  2408 I ActivityManager: Process com.roblox.client (pid 3046) has died: fg TOP
```

Process separation is the whole finding:

| Role | PID | Log evidence |
| --- | --- | --- |
| Game main process (crashes) | **3046** | `Handle a real crash` under `[3046:3047]`; later `Fatal signal 11 … pid 3046` / `Process 3046 exited due to signal 11` |
| Crash-handler subprocess (writes latch) | **3826** | `crashpad handler: real crash.`, `invoke crashpad::HandlerMain`, `crashpad handler: succeed` — all under `3826` |

`grep -n 'crashpad handler:'` over the whole capture returns **only PID 3826
lines**. The latch-setting log signature never appears under the game main PID.
`CrashpadHandler is running in-process` means "inside the handler subprocess's
own Java `main`" (the `com.roblox.client` CrashpadHandler process), not "inside
the game process" — the PID column proves it.

## 3. Static corroboration

- **CA01 / Craft Anything (authoritative DEX):** `com.roblox.client.analytics.CrashpadHandler`
  has 3 bytecode refs across 3 DEX; the only boot-side one is `Lrg/w0;->h0()V`
  at offset `00af` (`const-class CrashpadHandler`). `android-method-show
  rg/w0.h0` (108 instructions) calls only `nativeInitCrashpad(...)` at `00d5`,
  never `nativeRunCrashpadHandler`, and contains no latch write.
- **A05:** `0x7cdd149` is a private `.bss` global (NOBITS, third LOAD,
  MAP_PRIVATE/COW); a child's `movb $1` touches only the child's page. The boot
  export `nativeInitCrashpad 0x232d6b8` has `cmpb $0x0,0x7cdd149` (read/test)
  but no `movb $1` and no call to `0x2eca720` / `0x2ec9eed`.
- **task #525 / `crashpad_return.h`:** `nativeRunCrashpadHandler` has exactly one
  Java caller — `CrashpadHandler.main(String[])` — the crash-handler subprocess
  entry; boot only registers that class. `sober_crashpad_return_java_boot_reachable()`
  is deliberately 0.
- The emulator observation (PID 3826 handler vs PID 3046 game) is the runtime
  confirmation of all three static findings.

## 4. Implication for my owned modules (no edits made)

- `src/jni/flags_latch_default.c/.h`: the `none` route remains the default —
  install() is active but makes no guest call, `should_withhold()` stays 0,
  `latch()` just reads the guest byte. The opt-in `crashpad-jni` /
  `crashpad-valid` / `crashpad-main` / `narrow` routes all invoke the
  crashpad writer **in the harness's main process**, i.e. they are exactly the
  host-initiated in-process replay that a real crash never performs. They are
  seeds and must stay in `seeded_state`/WITHHELD. No route promoted to default.
- `src/jni/crashpad_return.c/.h`: stays opt-in (`SOBER_CRASHPAD_RETURN=1`,
  faithful JNI arrays required), `java_boot_reachable()` stays 0, withhold stays
  1 on any active run. The emulator result confirms this is a subprocess-only
  mechanism being replayed in-process by the harness.
- No byte patch, fake callback, host callback, counter inflation or gate change.
  Marker WITHHELD; default run must not silently seed state.

## 5. Hand-off note for the lead (not a claim, not a change)

The emulator shows the clean, in-process writer *does* exist in real boot: the
main-process latch is set by the client-settings / flag-filter path
`0x2eb7520` (A15/A04) once the real flag pipeline supplies its override payload;
in our harness default that writer is gated off by `0x7697cf0` bit0 (`=0x31`)
and `0x7697d08=1`. Reconstructing that genuine Java-supplied FFlag state would
be a *separate* clean-latch research task — nothing here establishes it, and no
seed or edit was made toward it. The crashpad subprocess is not that route.

## 6. Evidence class / limits

| Claim | Class |
| --- | --- |
| Main PID 3046 handled SIGSEGV; subprocess PID 3826 emitted all `crashpad handler:` lines including `real crash.` and `succeed` | OBSERVED (`logcat -v threadtime`, raw capture retained) |
| Boot installed crashpad via `w0.h0()` and no handler ran at boot; no RBXCRASH | OBSERVED (API 37/35 captures) |
| `am crash` is a Java exception → SIG9, no crashpad | OBSERVED |
| Crashpad latch writer cannot set the main-process latch | DERIVED (observed cross-process PID separation + private `.bss` from A05) |
| Main-process latch == 1 during real boot | DERIVED (TaskScheduler/RenderJob/FLog progression past the hard `0x23862b0` guard); a direct `/proc/<pid>/mem` byte read was planned but the root AVD exited before the read, so it is **not** claimed as observed |
| Emulator API 35/37 ≠ frozen harness; logcat proves log/provenance, not byte values | LIMITATION — oracle only |

No guest memory was modified and no marker was emitted by this work.
