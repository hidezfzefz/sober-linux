# W6-08 — Bounded `adb logcat` / Frida capture plan (worker-8)

**Status:** analysis complete — **plan only, no capture executed.**
**Scope:** analysis-only. This file is the only file written by this worker. No `src/`,
test, gate, CMake, docs, vault note or engine byte was changed; nothing was committed
(lead owns commits). **No device-mutating or process-instrumenting step below was run.**
**Decision impact:** none. Decision 013/038 is untouched; marker stays **WITHHELD** and
`tests/app-shell.sh` stays RED. Every observation this plan would produce is
**ANDROID-RUNTIME RESEARCH / ORACLE** evidence, never a clean native-harness pass.
**Team note:** `team_join` for role `worker-8` was rejected ("The team is full (12
agents)"): current roster is lead + workers 1,3,4,6,9,10,11,12,14,15,16. The lead was
notified via Convio; this report was produced anyway so task #536 is not blocked.

---

## 1. Ask and authoritative sources

Task #536: *design a bounded capture of genuine boot, surface lifecycle, FFlag values
and notifier using installed adb / Craft Anything / Frida tools against the
authoritative APK; no execution without lead approval.*

Sources (all read-only this session; re-verified where stated):

| Source | Identity / digest | Note |
| --- | --- | --- |
| Authoritative base APK | `/home/user/.local/share/basalt/linux/payloads/3170-204ad049599c96afb75b8a21d5051063e821d6c9/sober_apk/base.apk` — `sha256sum` **re-verified** = `114a6f8ed6f0f974cf632a9cbec2f5cd45e1d85d7ed251db4f3920140bc6ead4` | sole allowed DEX source |
| Companion x86_64 split | same dir `split_config.x86_64.apk` — `f42ace3b9428b30b4c06d28330937a2470cc13e7321130a5d27a4e29538b9356` (re-verified) | ships the byte-identical frozen engine |
| Frozen engine | `libroblox.so` in that split / `/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so` — SHA `3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335`, BuildID `204ad049599c96afb75b8a21d5051063e821d6c9` | never mix Flatpak `791c3f9e` / 2.745 RVAs |
| Modded APK | `~/Downloads/basalt-arm64*` (`classes2.dex` `6c2875cd…`) | **excluded**; never evidence (CA01) |
| Package | `com.roblox.client`, versionCode `3170`, versionName `2.740.931`, minSdk 26 (re-verified with `aapt dump badging`) | launcher activity was **not** published by `aapt dump badging`; resolve it from DEX before `am start` (w6-07's `/.RobloxAppActivity` is a placeholder) |
| Vault | Home / Goal / EngineFacts / Decisions / State / Ownership / ActiveTasks / `notes/CA01-craft-anything-verification.md` | read before work |

Tool inventory (read-only checks this session):

* `adb` 1.0.41 (Version 36.0.1-android-tools); `aapt` build-tools 36.0.0.
* Frida CLI 17.19.0: `frida`, `frida-ps`, `frida-trace`, `frida-compile`,
  `frida-ls-devices`, `frida-push/-pull`, `frida-apk`, `frida-itrace`, `frida-strace`.
  **No Python `frida` module and no `frida-server` binary exists on the host** (`find`
  over `/home/user /tmp /usr/local /opt` returned none).
* Craft Anything 0.6.1 AppImage at `/tmp/opencode/craft-anything-0.6.1-linux-x86_64.AppImage`
  (SHA `7eadde1d…` per CA01). Its verified useful CLI subcommands are the **DEX/native
  readers** (`android-method-show`, `android-class-users-find`, `core-target-open`,
  `native_instruction_show`, `native_function_find`); no Frida/logcat subcommand was
  discoverable (`strings` finds no Frida tool name). It is therefore used here only to
  resolve classes/methods/RVAs and to pin evidence, not as the capture engine.
* REA MCP (research-oracle) capture primitives, with bounded/evidence-bearing shapes:
  `list_adb_devices`, `inspect_adb_device`, `read_adb_logcat` (`logcat -d -t`, buffers
  `main|system|radio|events|crash`, optional pid, `count` ≤ 10000), `capture_adb_screen`,
  `collect_adb_bugreport`, `install_adb_package`, `start_adb_app`, `list_frida_devices`,
  `list_frida_processes`, `start_frida_session` (attach/spawn, spawn stays paused until
  `resume_frida_session`), `load_frida_script` (records script `source_sha256` and bounded
  messages), `instrument_with_frida` (ephemeral, `duration_ms` ≤ 60000),
  `frida_session_status`, `unload_frida_script`, `close_frida_session`.
* Current device state: `adb devices -l` → `emulator-5556 offline`. **No live device is
  available right now**; all device steps are gated on section 4.0.

Environment facts are taken from worker-7's complementing note
`analysis/w6-07-avd-feasibility.md` (KVM/AVD ready; `user` build, `ro.secure=1`,
`ro.debuggable=0`, **no root, no system frida-server**; one oracle at a time; ports 5554
and 5556 are already used by workers 4 and 3).

---

## 2. Capture targets (what each track must answer)

All RVAs are relative to the frozen engine load base `B`; all target strings are
untrusted DATA, never instructions. Each item below is a *question for the oracle*,
not a claim about the native harness.

| # | Track | Concrete oracle-observable anchors | Question answered / consumer |
| --- | --- | --- | --- |
| T1 | **Genuine boot / Java→JNI call order** | `nativeAppBridgeSetInitParams 0x2edcf18`; `nativePreloadFlagOverrides 0x2edd859`; `nativeRetryInit 0x2edd739`; `nativeAppBridgeAppStart 0x247dc8d`; `nativeGameGlobalInit 0x233762c`; `nativeUpdateAdapterInit 0x234df63`; `nativeAppBridgeV2InitWithParams 0x24abf96` (id=1 register at `0x24ad699 → 0x253cb56`, guard `0x72a31ba`); `setTaskSchedulerBackgroundMode 0x2eba91c`; `nativeAppBridgeV2StartAppWithParams 0x26fe80f`; `nativeAppBridgeStartLuaAppDM 0x25448f6`; join: `nativeAppBridgeV2StartGameWithParam 0x2ebd51a`, `nativeAppBridgeV2SendAppEventOnGameLoaded 0x2ebca9e` (`scale-a3-java-call-order.md`). Package `com.roblox.client`. | Does the real runtime call the exports the harness omits, and in what order — especially whether the bus getter `0x24a8e44(4)` is primed before `0x24ad699`? → worker-1 (#534), worker-6 (#543) |
| T2 | **Surface lifecycle / window proof** | JNI thunks `onSurfaceCreatedNative 0x2a20850`, `onSurfaceChangedNative 0x2a208c0`, `onSurfaceRedrawNeededNative 0x2a20a00`, `onSurfaceDestroyedNative 0x2a20a30`; genuine AGDK callbacks `0x2a23a10/0x2a23cc0/0x2a23c40/0x2a23b60` (cmd `0x01/0x03/0x04/0x02`); `nativeData+0x88/0x90/0x98/0xa0/0x140`; `ANativeWindow_fromSurface` RA `0x26fea7d`; log format `0x3c163c` = `[FLog::JNIAppBridge] {}: Created ANativeWindow {} (ID:{})` emitted via `0x232de10 → call *%rax @0x232e07e` (true RA `B+0x232e080`); gen word `0x72a3310`, window word `0x72a3308`, log gate word `0x72a3268` (`0x0406`); graphics word `0x77a3d60`; EGL init `0x649c21e`, ACQUIRE `0x649c2d6`, getWidth `0x649c303`, getHeight `0x649c30f`. | Exact created→changed→redraw ordering, window/generation values, whether the `Created ANativeWindow` line is genuinely formatted and which tag/config it uses. → worker-3 (#538), H6/C10 owner |
| T3 | **FFlag values** | `DebugForceTerminateOnRbxCrash` byte `0x7cef650`, thunk `0x227d511` → generic registration `0x227c18e` (name `0x3a30af`) → vtable writer `0x2365467`; registry global `0x7cdd118` (sole writer `0x227c092` in `JNI_OnLoad+0xeba`); `EnableAppBridgeComponents` thunk `0x22e567a` → out-byte `0x72cb648`; `ShadowFValuesEnabled` `0x7cdd0d8` / thunk `0x227e2c6`; JNIAppBridge flag `0x72a3268`; crash latch `0x7cdd149` (six writers, e.g. `0x2ec9fae` `CrashpadHandlerMain`); `nativeInitializeNativeFlags 0x2465210`, `nativeRegisterJavaFlagProvider 0x2ecbcf4`, `nativePreloadFlagOverrides 0x2edd859`. | What values a real Java flag source supplies, and whether `nativeRunCrashpadHandler` ever runs in the main process (cross-check worker-4/A05/CA01). → worker-4 (#546), worker-15 (#550) |
| T4 | **Notifier / gameLoadedCallback** | chain `0x27eaf04 → (slot +0x30) 0x27eaf4c → 0x27eaf6e → 0x25848e0`; table `0x6dbd860`, slot `0x7234520`, descriptor vtable `0x6dbbf98+0x30`; RA pair `0x2584960 → 0x27eafb3`, next guest RA `~0x27eaf32`; cached class `0x72a2f48` / method `0x72a2fc0` (`com/roblox/engine/jni/NativeGLJavaInterface`, `gameLoadedCallback (J)V`); placeId global `0x72a31c0`; JavaVM global `0x7cdec78`; **logcat line** tag `rbx.jni` (`0x550ef3`), text `onGameLoaded() SessionReporterState_GameLoaded placeId:%lld` (`0x36c7c9`), emitted by `__android_log_print 0x6d319f0` only when placeId ≠ 0. | Does a real boot ever publish MessageBus event id=1 with `placeId>0`, and does the notifier run with the exact RA pair? → worker-1, worker-2 (#539), worker-16 (#551) |

**Primary vs conditional mechanisms.** T1/T2/T4 have a logcat/`dumpsys` surface that
needs no root and is feasible today; T3 and the *provenance* parts of T2/T4 (register
reads, return addresses, argument values) need Frida, which the installed `user` image
cannot host. That split drives sections 4.2–4.6.

---

## 3. Boundaries (non-negotiable)

1. **No execution without explicit lead approval.** Everything in §4 is a proposal.
2. **Oracle only.** The emulator + authoritative APK is a research instrument; no
   observation may be used to satisfy a native-harness gate predicate or to relabel a
   seeded run as clean (negative-A/B risk, worker-11 #540).
3. **Artifact integrity.** Only the SHA-matched `base.apk` + `split_config.x86_64.apk`
   above. No repackaging, no Frida gadget embedded in the APK (that is a modded APK and
   is excluded by CA01), no `~/Downloads` APK, no byte patches, no fake vtables, no host
   callbacks, no counter inflation, no gate weakening.
4. **Observe-only hooks.** Frida scripts must `Interceptor.attach` and `send()` only —
   no `Interceptor.replace`, no argument/return-value mutation, no forced calls, no
   writes to guest globals from the observer. Any state change is a separately declared,
   lead-owned seed and therefore withhold-listed.
5. **Bounded time/count.** One AVD at a time; boot wait ≤ 180 s; logcat window ≤ 120 s;
   Frida `duration_ms` ≤ 60000 per script, ≤ 4 scripts per run; `read_adb_logcat count`
   ≤ 2000 per buffer; ≤ 2 screencaps; total wall clock ≤ 10 min/run including boot.
6. **Provenance of evidence.** Every artifact gets a run id, SHA-256, tool/version,
   device serial, and an `ORACLE / ANDROID RUNTIME — research only` label at capture
   time; script sources are hashed by `load_frida_script`/`instrument_with_frida`.
7. **Ports/resources.** Use a fresh AVD copy and a unique port (5558+); do not touch
   worker-4's 5554 or worker-3's 5556 AVD/log/port. Re-verify `/dev/kvm`, free RAM, and
   the offline `emulator-5556` before anything else.

---

## 4. Bounded capture procedure (NOT executed)

### 4.0 Approval gate — all boxes must be checked by the lead before any step below

- [ ] Explicit lead approval for this run, with a named owner and time window.
- [ ] Port/AVD coordination confirmed with workers 3, 4 and 7 (5554 / 5556 reserved).
- [ ] Device present and `online`; if an AVD must be launched, the isolated-copy recipe
      in `w6-07-avd-feasibility.md §5` is used verbatim with port ≥ 5558.
- [ ] APK + split digests re-verified against §1; launcher activity resolved from DEX
      (it is **not** published by `aapt dump badging`).
- [ ] For any Frida step (§4.5/§4.6): lead approves **either** downloading the
      `google_apis` **userdebug** x86_64 image and creating a second AVD, **or** supplying
      a device-compatible `frida-server`; none is installed today.
- [ ] `logcat -c` (a device mutation) is approved once at window start, or the plan runs
      with the unbounded pre-existing buffer and uses only `-d -t` reads.

### 4.1 Preflight (read-only; safe before approval except where noted)

```
sha256sum <base.apk> <split_config.x86_64.apk>                 # must equal §1
adb devices -l                                                # expect one 'device'
tools.rea.list_adb_devices()                                  # serial, state, kind
tools.rea.inspect_adb_device({serial})                        # ro.build.type/secure/debuggable
tools.rea.list_frida_devices()                                # confirm no usable device
Craft Anything: android-class-users-find / android-method-show on <base.apk>
  to resolve the launcher activity and native method declarations for T1/T3.
```
Record everything into the run ledger (§5). If `ro.debuggable=0` is confirmed, §4.5 is
**SKIPPED**, not silently downgraded.

### 4.2 Track A — logcat boot + notifier (primary, no root; T1 + T4)

Install (device-mutating, inside approved window) with
`tools.rea.install_adb_package({serial, apk_path:<base>})` and then the split —
prefer one `adb install-multiple base.apk split_config.x86_64.apk` as in w6-07 §5 — and
start the resolved launcher via `tools.rea.start_adb_app({serial, package:'com.roblox.client'})`.

Capture, bounded and non-streaming (each call is evidence-bearing):

```
# buffers main, system, events, crash; newest 2000 lines each; optional pid from frida-ps/logcat
tools.rea.read_adb_logcat({serial, buffer:'main',  count:2000, pid?:<app_pid>})
tools.rea.read_adb_logcat({serial, buffer:'system',count:2000, pid?:<app_pid>})
tools.rea.read_adb_logcat({serial, buffer:'events',count:2000})
tools.rea.read_adb_logcat({serial, buffer:'crash', count:2000, pid?:<app_pid>})
```
Repeat the four reads at t0 (launch), t+20 s and t+120 s (window end); keep each result's
`evidence_id`, `lines`, `coverage` and `total_bytes`. Optional
`tools.rea.collect_adb_bugreport({serial, output_directory:'/tmp/opencode/w6-08-<runid>/'})`
once at the end if the lead wants kernel/`dumpsys` context (archive is SHA-digested).

Post-processing (host-side, on captured text only):

* order-stamp any JNI/log markers matching the T1 export names and the T4 line
  `rbx.jni` + `onGameLoaded() SessionReporterState_GameLoaded placeId:`
  → extract the placeId and its first-appearance timestamp;
* check for `Created ANativeWindow` / `EGL` / `ACQUIRE` / `DIMS` markers for T2;
* check whether `nativeRunCrashpadHandler` appears anywhere (expected: absent — cross-check
  CA01/worker-4);
* absence of a T4 line is a **result** (real boot did not reach the notifier in this
  window), never a reason to inject one.

### 4.3 Track B — surface lifecycle (logcat + dumpsys; T2)

Within the same ≤120 s window:

```
adb -s <serial> shell dumpsys SurfaceFlinger   > /tmp/opencode/w6-08-<runid>/sf-t0.txt
adb -s <serial> shell dumpsys window windows   > /tmp/opencode/w6-08-<runid>/window-t0.txt
tools.rea.capture_adb_screen({serial, output_directory:'/tmp/opencode/w6-08-<runid>/'})  # ≤2
... repeat the two dumpsys at t+20 s ...
```
Digest every output. Extract the created→changed→redraw ordering, surface dimensions,
window/session identity, and any FLog `Created ANativeWindow` line. Because the AVD uses
SwiftShader, use this only for **call order and provenance shape**, never frame timing or
a "native GLES draw" assertion (w6-07 §4.3).

### 4.4 Track C — FFlag values via log only (fallback; T3)

Logcat will only expose flags the guest logs. Search the Track A corpus for
`DebugForceTerminateOnRbxCrash`, `EnableAppBridgeComponents`, `ShadowFValuesEnabled`,
`ClientSettings`/FFlag entries. Report values **only if the guest actually printed
them**; otherwise record `NO_LOG_EVIDENCE` and hand T3 to §4.5. Do **not** synthesize a
flag value from a register read and present it as "the flag is loaded" (cf. A03 §5).

### 4.5 Track D — Frida on the oracle (CONDITIONAL: needs userdebug image; T1–T4 deep fields)

Only after the §4.0 Frida box is checked. Discover the target with
`tools.rea.list_frida_processes({device_id})`, then use
`tools.rea.instrument_with_frida({mode:'attach', device_id, pid, source_kind:'inline',
source:<script>, duration_ms:60000})` (or `start_frida_session` → `load_frida_script` →
`resume_frida_session` → `frida_session_status` → `close_frida_session` for a spawn-before-exec
capture of early boot). All scripts below are observe-only and use `Module.findBaseAddress('libroblox.so')`.

Script shapes (to be written verbatim into `source` at execution time, each ≤ one concern):

1. **boot (T1):** enumerate `Module.enumerateExports('libroblox.so')` for names containing
   `Java_` and the T1 method names; `Interceptor.attach` each, `send({ev:'jni',name,
   tid:Process.getCurrentThreadId(), ts:Date.now()})`. No argument writes. This yields the
   real export invocation order and relative timing.
2. **surface (T2):** attach by base+offset to `0x2a20850`, `0x2a208c0`, `0x2a20a00`,
   `0x2a20a30` and to `0x2a23a10/0x2a23cc0/0x2a23c40/0x2a23b60`; in `onEnter`, read
   `nativeData` (the jlong arg) and *read-only* `[nativeData+0x88/0x90/0x98/0xa0/0x140]`
   with `ptr.readPointer()` guarded by `try/catch`. Only report what was actually read;
   never fill a NULL slot.
3. **flags (T3):** attach to `0x227c18e` (name = C string at arg1, out-byte pointer =
   arg2); in `onLeave` read that byte. Attach to `0x2365467` and record the out-byte value
   produced by the genuine parse. Attach to `0x2465210` / `0x2ecbcf4` and, via the Java
   bridge (`Java.use(...)` on the resolved `FlagJniInterface`), log the actual `String[]`
   overrides delivered. Cross-check that `0x7cdd118` is non-NULL in the real process.
4. **notifier (T4):** attach to `0x27eaf4c` (read `event_id = arg0.add(8).readU64()`,
   `slot_ptr = arg0.add(0x18).readPointer()`, `placeId = arg1.readU64()`), to `0x27eaf6e`
   (SysV arg1/RSI = placeId), and to `0x25848e0` (record
   `this.returnAddress.sub(base)`; the genuine value is `0x27eafb3`). Read-only checks:
   `[B+0x7234520] == B+0x6dbd860`, `[B+0x6dbd860] == B+0x27eaf6e`,
   `[B+0x72a2f48]/[B+0x72a2fc0]` non-NULL, `*(int64*)(B+0x72a31c0) > 0`.
   Correlate any notifier hit with the `rbx.jni` log line from Track A.

Message budgets: one `send()` per event, cap counts in the script (e.g. ≤ 200 per site,
then detach), and record `messages_truncated` plus the tool's `source_sha256` and
`evidence_id`. If a hook cannot be resolved, report the miss — do not fall back to
calling the address directly.

### 4.6 Track E — host-side Frida on the native harness (SEPARATE, opt-in; not oracle)

`frida` 17.19.0 is installed on the host, so a **local** process running the frozen
engine could be instrumented without any device. This is a different evidence class
(NATIVE HARNESS, not Android-runtime oracle) and is proposed only if the lead asks for a
live check of guest globals/RAs: attach to the running harness PID and reuse scripts
2–4 above with the harness's module base. It must still be observe-only, must not enable
any `SOBER_*` seed, and any hit is subject to the same gate predicates (placeId > 0, RA
pair `0x2584960` over `0x27eafb3`, no host taint). A host-driven hit remains a
diagnostic and **never** emits the marker under decisions 013/038.

### 4.7 Teardown

`close_frida_session` (which resumes a paused spawn before detaching),
`unload_frida_script` for any long-lived script, `adb -s <serial> emu kill` **only for this
run's port**, and `sha256sum` + manifest of every file under
`/tmp/opencode/w6-08-<runid>/`. No other worker's device is touched.

---

## 5. Evidence ledger and interpretation rules

Per-run manifest fields: run id; UTC start/end; serial and AVD copy; `getprop` fingerprint;
base/split/engine SHAs; adb/frida/Craft Anything/REA versions; each tool call's
`evidence_id`; captured file paths + SHA-256; each Frida `source_sha256`; buffers/counts;
truncation flags; and the label `ORACLE / ANDROID RUNTIME — research only`.

Interpretation (fail-closed):

* A T1 call-order timeline is ground truth for **this build's Java→JNI order only**. It
  is a hypothesis generator for the native harness; it is not proof the harness can
  supply the same calls.
* A T2 `Created ANativeWindow` line, if seen, is a genuine guest-formatted message, but
  it is Android-runtime evidence. It can confirm format/tag/window/ID/ordering; it
  cannot make the harness's C8/C10 pass or turn a seeded run clean.
* A T4 `rbx.jni` line with `placeId>0` proves the real runtime can publish event id 1;
  it does **not** satisfy gate G5–G8 (those require the guest-entered RA pair and clean
  state in *this* harness).
* T3 values that differ from harness defaults identify a fidelity gap; writing those
  values into the harness is a seed and belongs on the withhold chain.
* Every capture is quarantined from the native-harness gate and from worker-11's
  negative matrix except as an explicitly labelled *oracle* comparison.

---

## 6. Limitations and risks

* **Frida is blocked as installed**: `user` build, `ro.debuggable=0`, no root, and no
  `frida-server` on the host (§1). Section 4.5 is conditional on an approved userdebug
  image; otherwise only logcat/dumpsys fallback (§4.2–4.4) is possible and register/RA
  ground truth is unavailable.
* **No live device now**: `emulator-5556` is `offline`; nothing in §4 can be demonstrated
  at plan time.
* **Noise/fidelity**: Play Store/GMS flows, SwiftShader software GLES, cold-boot
  snapshots warning, and shared ADB daemon/ports (w6-07 §4).
* **Coverage**: logcat may contain no FFlag values; the notifier may never fire within
  120 s; a launcher activity is not published by `aapt` and must be resolved from DEX.
* **Instrumentation perturbation**: Frida hooks change timing; observe-only scripts
  mitigate but do not remove this. Hooking is research, not a shipped runtime.
* **No execution by this worker**: all claims above are tool-inventory/static-fact
  observations; nothing was captured, no gate or marker was touched.

---

## 7. Hand-off (what the lead routes after an approved run)

| Consumer | Deliverable from this plan's capture |
| --- | --- |
| worker-1 (#534), worker-6 (#543) | T1 timestamped JNI export order; whether `0x24a8e44(4)` is primed before `0x24ad699` |
| worker-3 (#538), H6/C10 owner | T2 created/changed/redraw order, `Created ANativeWindow` text/window/ID, config word `0x72a3268` |
| worker-4 (#546) | T3 presence/absence of `nativeRunCrashpadHandler`, flag byte values, `0x7cdd118` state |
| worker-2 (#539), worker-16 (#551) | T4 notifier hit with placeId and the `0x2584960`/`0x27eafb3`/`0x27eaf32` RA triad, or its honest absence |
| worker-15 (#550) | T3 FFlag/ClientSettings values actually observed (with `NO_LOG_EVIDENCE` where none) |
| worker-11 (#540), worker-14 (#549) | oracle-labelled evidence manifest + SHAs; confirms no oracle evidence is used as clean native proof |
| lead (#544) | run manifest, decision impact statement, and the unchanged-gate assertion |

## 8. Bottom line

A bounded, root-free capture of genuine boot, surface lifecycle and notifier log lines is
feasible with the authoritative split set and the installed adb/REA tooling, using one
isolated AVD on a fresh port and ≤120 s windows. Frida deep-field capture (FFlag bytes,
RA provenance, descriptor state) is **not** feasible on the installed `user` image and is
conditional on lead approval for a userdebug image; it is never a substitute for the
native harness. No step here was executed, no artifact was modified, and decision
013/038 is unchanged: **marker WITHHELD, `tests/app-shell.sh` RED.**
