# W6-07 — AVD / KVM emulator oracle feasibility (worker-7)

**Status:** analysis complete. **Scope:** analysis-only. **No source, test, gate, or docs files changed.**
**Decision impact:** none — this report is research-oracle feasibility only. The emulator is **never** the final runtime; decision 013/038 stands and the marker remains **WITHHELD**.

## 1. Question and authoritative sources

Task #548 asks: is the installed Android SDK 37 x86_64 image + KVM usable as a **bounded research oracle** for the genuine Java/surface-lifecycle/notifier sequence, and can an AVD be created if needed? It explicitly forbids making it the final runtime.

Evidence sources used (all read-only):
- Authoritative base APK: `/home/user/.local/share/basalt/linux/payloads/3170-204ad049599c96afb75b8a21d5051063e821d6c9/sober_apk/base.apk`, SHA-256 `114a6f8ed6f0f974cf632a9cbec2f5cd45e1d85d7ed251db4f3920140bc6ead4` (re-verified this session).
- Companion split: `split_config.x86_64.apk` in the same directory, SHA-256 `f42ace3b9428b30b4c06d28330937a2470cc13e7321130a5d27a4e29538b9356`.
- Frozen engine: `/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so`, SHA-256 `3e498d649421115bdbea69f78e2b818d4351d8990db60e232d2aef5b741335`, 120,041,712 bytes (BuildID `204ad049…`).
- Host SDK: `/home/user/Android/Sdk`, AVD home `/home/user/.android/avd`.
- Vault: Home/Goal/EngineFacts/Decisions/State/Ownership/ActiveTasks and `notes/CA01-craft-anything-verification.md`.
- The `~/Downloads/basalt-arm64*` APK is modded and was **not** used.

## 2. Verified environment facts

### 2.1 KVM / hardware acceleration — FEASIBLE as-is

```
$ ls -l /dev/kvm
crw-rw-rw- 1 root kvm 10, 232 ... /dev/kvm
$ lscpu | grep -Ei 'Virtualization'
Virtualization: VT-x          # /proc/cpuinfo exposes 'vmx'
$ /home/user/Android/Sdk/emulator/emulator-check accel
accel:
KVM (version 12) is installed and usable.
```

`/dev/kvm` is mode `0666`, so the unprivileged `user` account (uid 1000; not in the `kvm` group) can still open it. `kvm-ok` is not installed, but `emulator-check accel` is the authoritative emulator-side check and it passes. **No setup needed.**

### 2.2 Emulator / ADB — FEASIBLE as-is

```
$ emulator -version
Android emulator version 37.2.12.0 (build_id 16428233) (CL:N/A)
$ adb version
Android Debug Bridge 1.0.41, Version 37.0.1-15733141
$ emulator -list-avds
Medium_Phone_API_37.0
```

`ANDROID_HOME` / `ANDROID_SDK_ROOT` are **unset** in the shell; all commands must use absolute SDK paths or export the variables first. `qemu-system-x86_64-headless`, `qemu-img`, `netsimd`, `crashpad_handler` are present.

### 2.3 Installed system image and AVD — FEASIBLE for boot

`system-images/android-37.0/google_apis_playstore/x86_64/source.properties`:

```
AndroidVersion.ApiLevel=37.0
AndroidVersion.ExtensionLevel=22
SystemImage.Abi=x86_64
SystemImage.TagId=google_apis_playstore,ai_glasses_compatible
SystemImage.GpuSupport=true
```

AVD `Medium_Phone_API_37.0` (`~/.android/avd/Medium_Phone.avd/config.ini`):

```
abi.type = x86_64; hw.cpu.arch = x86_64; hw.cpu.ncore = 4
hw.ramSize = 2048; vm.heapSize = 336
hw.lcd.width/height = 1080x2400; density 420
hw.gpu.enabled = yes; hw.gpu.mode = swiftshader (hardware-qemu.ini)
disk.dataPartition.size = 6442450944 (6 GiB); sdcard 512M
PlayStore.enabled = true; tag google_apis_playstore
image.sysdir.1 = system-images/android-37.0/google_apis_playstore/x86_64/
```

This AVD was **already booted this session** by another worker: `emu-launch-params.txt` records
`-avd Medium_Phone_API_37.0 -no-snapshot -no-audio -gpu swiftshader -netdelay none -netspeed full`,
and `/tmp/opencode/w6-04-emulator.log` shows a real headless launch
(`qemu-system-x86_64-headless … -no-window -no-boot-anim -gpu swiftshader -accel on -port 5554`)
reaching display init `1080x2400 dpi 420x420`. Boot was still in progress at the time of inspection (~60 s in, cold boot because the `default_boot` snapshot was missing — see §4).

### 2.4 Authoritative APK / engine pairing — FEASIBLE to install

```
$ aapt dump badging base.apk | head -1
package: name='com.roblox.client' versionCode='3170' versionName='2.740.931' ...
sdkVersion:'26' targetSdkVersion:'35' compileSdkVersion='36'
$ unzip -l base.apk | grep classes.*dex        # classes.dex, classes2.dex, classes3.dex
$ unzip -l split_config.x86_64.apk | grep 'lib/x86_64/libroblox.so'
120041712 ... lib/x86_64/libroblox.so
$ sha256sum <extracted split libroblox.so>
3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335  ← identical to frozen engine
```

The split ships **the byte-identical frozen `libroblox.so`**. This is the strongest argument for the oracle's value: the emulator would execute the same native engine we are reversing, so the Java→JNI call order, surface lifecycle, and notifier behaviour observed there are ground truth for *this* BuildID — not for a different build. It is still an Android-runtime observation and can **only** guide the native harness; it cannot be relabelled as a clean native run.

Install is a standard split install: `adb install-multiple base.apk split_config.x86_64.apk` (base alone is not sufficient). The Play Store image supplies GMS that the manifest references (`com.google.android.gms.permission.AD_ID`, billing).

### 2.5 AVD *creation* — FEASIBLE but needs setup

```
$ find /home/user/Android/Sdk -name sdkmanager        # (none)
$ ls /home/user/Android/Sdk/cmdline-tools             # does not exist
$ cat /home/user/Android/Sdk/licenses/android-sdk-license
24333f8a63b6825ea9c5514f83c2829b004d1fee            # license already accepted
$ timeout 12 curl -sSI https://dl.google.com/android/repository/repository2-1.xml
HTTP/2 200 ...
```

There is **no `sdkmanager` / `avdmanager`** installed, so creating a *new* AVD from scratch is not possible with the current SDK layout. Network access to `dl.google.com` works and the SDK license is accepted, so installing command-line-tools (then `avdmanager create avd …`) is feasible if the lead approves the download. For most experiments the existing AVD can simply be **copied** (see §5), which avoids the download entirely.

### 2.6 Privileges / deep instrumentation — NOT FEASIBLE on the installed image

```
$ grep -E 'ro.build.type|ro.secure|ro.debuggable' <system image>/build.prop
ro.build.type=user
ro.secure=1
ro.debuggable=0
```

This is a production-style `user` image: **no `adb root`, no `remount`, no system-level frida-server**. The Roblox release APK is not `debuggable`, so `ptrace`-based hooking of its process from the `shell` user will fail. Unfiltered `adb logcat` and `dumpsys` *do* work from the shell user and need no root.

If deep hooking is genuinely required, the lead would need to approve downloading the **`google_apis` (non-Play) userdebug** system image (`sdkmanager "system-images;android-37;google_apis;x86_64"`) and creating a second AVD. Repackaging the authoritative APK with a frida gadget is **not** acceptable oracle evidence — it would be a modded APK, the same category CA01 excludes.

### 2.7 Host resources — FEASIBLE with care

```
/dev/kvm ok; CPU VT-x; 49 GiB free on /; 23 GiB RAM total (7.6 GiB available),
8 GiB swap (2.4 GiB free). /dev/dri/renderD128 present; Mesa libGL/libGLES installed,
but the AVD is configured for SwiftShader software GLES.
```

Memory is the tightest constraint: the AVD requests 2 GiB RAM plus SwiftShader CPU rendering (observed ~1.3 GiB RSS for the headless QEMU process). Several concurrent emulator instances (see §4) risk swapping. One oracle at a time is the safe limit; allocate unique console/ADB port pairs.

## 3. Feasibility verdict matrix

| Dimension | Verdict | Notes |
| --- | --- | --- |
| KVM acceleration | **FEASIBLE (ready)** | `/dev/kvm` 0666, VT-x, `emulator-check accel` OK |
| Boot existing AVD 37 x86_64 | **FEASIBLE (observed)** | Cold boot in progress this session on port 5554 |
| Install authoritative APK (base + split) | **FEASIBLE** | `adb install-multiple`; byte-identical frozen engine inside split |
| Capture surface lifecycle / JNI logs | **FEASIBLE (bounded)** | `logcat` + `dumpsys` from shell; SwiftShader GLES |
| Create a *new* AVD | **FEASIBLE WITH SETUP** | need cmdline-tools/avdmanager download; license already accepted; or copy existing AVD |
| Root / frida / system hooks | **NOT FEASIBLE as installed** | `user` build, `ro.debuggable=0`; need userdebug `google_apis` image + lead approval |
| Use as final native runtime | **FORBIDDEN** | Goal + decision 013/038: no Android runtime; marker stays WITHHELD |

## 4. Risks and contention (important for the lead)

1. **Shared emulator/ADB resource.** At inspection time an `adb fork-server` was running and `adb devices` reported `emulator-5554 offline` while worker-4's cold boot was in flight. Worker-3 announced on the team channel that they are launching an isolated AVD copy on port **5556** for surface-lifecycle capture. My proposed experiment must use a further unique port (**5558+**) and must not touch 5554/5556 or either worker's AVD files.
2. **Snapshot misconfiguration.** The AVD has a `snapshots/default_boot` directory containing only an 11-byte `snapshot.pb`, and the log warns `Device 'userdata' does not have the requested snapshot 'default_boot'` → `Failed to load snapshot` → cold boot every time. Launch with `-no-snapshot` explicitly (as the other workers do) so this warning is expected and not mistaken for failure; boot time is ~1–3 min with SwiftShader.
3. **SwiftShader fidelity.** GLES calls and `ANativeWindow`/AGDK callbacks are genuine but rendered in software; frame timing, GPU driver quirks and some extensions differ from the frozen native harness. Use the oracle for **call order and provenance shape only**, never for frame-performance or "native GLES draw" assertions.
4. **Google Play image behaviour.** Play Store/GMS can trigger account/update flows and network calls that perturb boot. Bound the capture window, keep `-netdelay none -netspeed full`, and avoid signing in.
5. **Evidence contamination.** Any observation from the emulator is Android-runtime evidence. It must be stored in an oracle-labelled log and never used to satisfy a native-harness gate predicate or to relabel a seeded run as clean (negative A/B risk tracked by worker-11 in #540).
6. **Modded APK exclusion.** Only the SHA-matched `base.apk` + `split_config.x86_64.apk` above may be used. The `~/Downloads` copy is modded.

## 5. Proposed bounded research experiment (NOT executed by me)

Run only after lead approval and after coordinating ports with workers 3 and 4.

```
# 0) isolate: copy the AVD so no worker's userdata is touched
cp -a ~/.android/avd/Medium_Phone.avd ~/.android/avd/W7Oracle.avd
# create ~/.android/avd/W7Oracle.ini with path=.../W7Oracle.avd, target=android-37.0

# 1) launch headless, unique port, software GLES, no snapshots, bounded
export ANDROID_SDK_ROOT=/home/user/Android/Sdk
nohup $ANDROID_SDK_ROOT/emulator/emulator \
  -avd W7Oracle -no-window -no-audio -no-boot-anim \
  -gpu swiftshader -accel on -no-snapshot -port 5558 \
  > /tmp/opencode/w7-oracle-emulator.log 2>&1 &

# 2) wait for boot with a hard timeout (<=180 s), then install authoritative split set
adb -s emulator-5558 wait-for-device
timeout 180 bash -c 'until [ "$(adb -s emulator-5558 shell getprop sys.boot_completed | tr -d "\r")" = 1 ]; do sleep 3; done'
adb -s emulator-5558 install-multiple \
  .../sober_apk/base.apk .../sober_apk/split_config.x86_64.apk

# 3) bounded capture window (<=120 s): logcat + surface/window state around app start
adb -s emulator-5558 logcat -c
adb -s emulator-5558 shell am start -n com.roblox.client/.RobloxAppActivity   # verify activity name from DEX first
timeout 120 adb -s emulator-5558 logcat -b main,system,events -v threadtime \
  > /tmp/opencode/w7-oracle-logcat.txt &
adb -s emulator-5558 shell dumpsys SurfaceFlinger > /tmp/opencode/w7-sf-before.txt
sleep 20
adb -s emulator-5558 shell dumpsys SurfaceFlinger > /tmp/opencode/w7-sf-after.txt
adb -s emulator-5558 shell dumpsys window windows > /tmp/opencode/w7-window.txt
wait

# 4) teardown this instance only
adb -s emulator-5558 emu kill
```

Evidence to extract from the capture (to hand to workers 3/8/1):
- `Created ANativeWindow` / EGL init / `ACQUIRE`/DIMS sequence and its caller order.
- `surfaceCreated` → `surfaceChanged` → `surfaceRedrawNeeded` callback ordering.
- JNI exports actually invoked (`nativeInitCrashpad`, `nativeInit*`, `nativeRunCrashpadHandler`) with timestamps — cross-checks worker-4's latch conclusion and worker-1's boot sequence.
- H6 log format `0x3c163c` / notifier `0x27eaf6e` / `gameLoadedCallback` appearance, if any, in an unseeded boot.
- FFlag/ClientSettings values visible in logs (worker-15's #550).

Acceptance limits: no root, no repackaged APK, no host callbacks, no gate edits; total runtime bounded (~10 min including cold boot); all output labelled `ORACLE / ANDROID RUNTIME — research only`; no conclusion from it may assert a clean native-harness run.

## 6. Bottom line

The installed toolchain is **oracle-ready today** for KVM-accelerated, unprivileged, headless capture of the genuine Android boot/surface/JNI sequence using the SHA-matched APK whose native library is byte-identical to the frozen engine. The only real gaps are (a) no `avdmanager` for net-new AVDs (work around by copying the AVD, or download cmdline-tools with approval) and (b) no root/frida on the `user` build (would need an approved userdebug image download). Resource and port contention with workers 3 and 4 must be managed, and every observation must remain quarantined as Android-runtime research evidence. This changes nothing about the gate: **marker WITHHELD, `tests/app-shell.sh` RED, decision 013/038 intact.**
