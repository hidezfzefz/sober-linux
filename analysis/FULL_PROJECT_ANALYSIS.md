# FULL PROJECT + LIBRARY ANALYSIS — handoff for future agents

Generated from the complete repository at `/home/user/sober-linux` (HEAD `5f52a82`),
the shared Obsidian memory at `/home/user/sober-vault`, the frozen engine, the
SHA-matched APKs, and the installed toolchain. This document is the single
orientation point for any future agent continuing the **first-ever goal**.

---

## 1. Objective and stop condition

Goal: reverse-engineer Roblox/Sober so it runs **natively on Linux** as a real
ELF with **no Android runtime, Wine/Proton, box64, QEMU, translation layer, or
compatibility runtime**.

The marker `app shell rendered` may be emitted **only** when all three legs are
genuine in one run:

1. **Callback** — notifier-driven `gameLoadedCallback(J)V` at `0x27eaf6e`,
   `placeId > 0`, RA pair `0x2584960 → 0x27eafb3`, cached class/method identity
   `0x72a2f48` / `0x72a2fc0`, node dispatch entered from guest `.text`.
2. **Render** — a guest-tagged frame with `>=2` recurring-RA guest swaps at
   `0x649d422`, `>=1` bracketed guest GLES draw, and G8 window proof (C0–C10).
3. **Clean state** — `seeded_state == 0`, no withhold line, exactly one marker.

Until then the marker stays **WITHHELD** under decisions 013/038.
`tests/app-shell.sh` is intentionally RED. Current score: **≈66%**.

---

## 2. Engine identity and authoritative evidence sources

### Frozen engine (the only engine whose RVAs are admissible)
- Path: `/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so`
- Size: `120,041,712` bytes
- SHA-256: `3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335`
- BuildID: `204ad049599c96afb75b8a21d5051063e821d6c9`
- AppImage-side package: `basalt` payload `3170-204ad049…`
- Craft Anything triage: Android x86_64 shared library, stripped, JNI-oriented,
  NX yes, RELRO full, canary yes, 32 sections, 559 dynamic exports, 573 imports.
- Harness invariants: 3,618 constructors replayed; 565,390 APS2 relocations
  applied; private memfd copy changes only `DT_INIT_ARRAYSZ`.

### Authoritative Java evidence
- Base APK:
  `/home/user/.local/share/basalt/linux/payloads/3170-204ad049599c96afb75b8a21d5051063e821d6c9/sober_apk/base.apk`
- SHA-256: `114a6f8ed6f0f974cf632a9cbec2f5cd45e1d85d7ed251db4f3920140bc6ead4`
- Split: `split_config.x86_64.apk`,
  SHA `f42ace3b9428b30b4c06d28330937a2470cc13e7321130a5d27a4e29538b9356`
- Authoritative smali extraction: `/tmp/opencode/auth_smali`
- DEX hashes: classes `00309819…`, classes2 `5afac2bb…`, classes3 `1b2406a0…`

### FORBIDDEN / non-authoritative sources
- `~/Downloads/basalt-arm64-2.740.931.apk` — **modded/cracked**; all three DEX
  differ (classes2 `6c2875cd…`). Never cite.
- `/tmp/opencode/mini/out/smali` — different modded mini APK; obfuscated names
  (`uh/f`, `Q2`) differ from authoritative (`uh/e`, `P2`). Native RVAs agree, but
  DEX claims must be re-derived from authoritative data.
- `analysis/rea/work/lib/x86_64/libroblox.so` and other REA work copies —
  **wrong engine**: `116,461,944` bytes, SHA `8afce3e6…`, BuildID `791c3f9e…`
  (Flatpak 2.745). Any RVA from them is inadmissible.

### Installed toolchain (use what exists)
- JDK (`java`/`javac`, `JAVA_HOME=/home/user/.sdkman/candidates/java/current`),
  `apktool`, `adb`, full Android SDK at `/home/user/Android/Sdk` (platforms 16 and
  37, build-tools 36/37, NDK 27.1, emulator, x86_64 Android 37 system image),
  `/dev/kvm`.
- **Craft Anything 0.6.1** (exactly one instance, shared):
  `/tmp/opencode/craft-anything-0.6.1-linux-x86_64.AppImage`
  SHA `7eadde1d74ab42cc838862fe3f831e3cf3c9fa6ba5e2bf04e2824bbc47146134`.
  Use `--cli --deep <tool> ...`; its built-in native engine does **not** index
  functions in this 120 MB stripped object, but its DEX/Android tools work and
  have repeatedly corroborated our findings.
- Standard: `objdump`, `readelf`, `nm`, `gdb`, `clang`, `gcc`, `cmake`, `python3`,
  `rg`, `node`.

---

## 3. Repository layout and module map

`/home/user/sober-linux`: 123 C/C++ source/header files, **44,944 LOC**, 150
analysis markdown reports, 17 test files, private repo
`https://github.com/hidezfzefz/sober-linux` `main`.

### Core native runtime
| Area | Files | Role |
| --- | --- | --- |
| Entry / platform | `src/linux/main.c`, `platform.c`, `platform.h` | host executable, X11/EGL platform |
| Guest loading | `guest_dlopen_host.c`, `android_relocs.c/.h`, `gfx_loader.c/.h` | private memfd mapping, APS2 relocations, GLES/EGL symbol forwarding |
| App shell | `guest_app_shell.c/.h` | **lead-owned orchestration**, strict gate, seeded_state chain, all glue |
| Compat shim | `src/compat/*` | Bionic versioned libc, TSD, signals, `__sF` stdio bridge, GL forwarding |
| Render host | `src/render/native_render.c/.h`, `render_probe.c` | native X11/EGL window, first frame, swap |

### JNI / app-state layer (`src/jni/`)
- **JNI bridge core**: `native_jni.c/.h`, `javavm_semantics.c`, `jni_native_objects.c`,
  `jni_json_bridge.c`, `jni_strings.c`, `jni_arrays.c`, `jnislots_default.c`,
  `fields_default.c`, `platform_fields.c`, `scalar_returns.c`,
  `static_object_calls.c`, `callback_provenance.c`.
- **Default-ON modules** (with `SOBER_*_DEFAULT=0` kill switches): allocator
  (`alloc_default`, `alloc_contract_fix`), TSD (`tsd_default`), JavaVM
  (`javavm_default`), FFlag registry (`fflag_default`), scheduler
  (`scheduler_guest`), singleton services (`singleton_default`,
  `singleton_fix`, `singleton_services`, `app_singleton_bridge`), delegate
  (`delegate_default`), controller (`controller_default`), placeId
  (`placeid_default`), fields (`fields_default`), JNI slots (`jnislots_default`).
- **App-start / shell**: `startapp_params.c`, `startapp_v2.c`, `startgame_v2.c`,
  `game_params.c`, `global_string_seed.c`, `app488_default/fix/inner`.
- **Callback / publish / gate**: `publish_default.c/.h`, `provenance_gate.c/.h`.
- **Latch / crashpad**: `flags_latch_default.c/.h`, `crashpad_return.c/.h`.
- **Frame/render evidence**: `frame_evidence.*`, `frame_surface.*`,
  `frame_dispatch.*`, `frame_draw.*`, `frame_redraw.*`, `frame_thread.*`,
  `frame_synthetic_tag.*`, `frame_callbacks.*`, `render_guest.*`.
- **Diagnostic**: `notifier_probe.*`, `jni_probe.c`.

### Tests / verification
`tests/verify-native.sh`, `first-frame.sh`, `fault-baseline.sh`,
`guest-dlopen.sh`, `D09-withhold-audit.sh`, `app-shell.sh` (strict GREEN-iff
contract), plus D01/D04/D08/D12 reports. `ctest` currently has 2 shallow tests.

### Memory / decisions
- Obsidian vault: `/home/user/sober-vault` (`Home`, `Goal`, `EngineFacts`,
  `Decisions`, `State`, `Ownership`, `ActiveTasks`, `Verification`,
  `AnalysisIndex`, 15+ notes). It is the shared memory for future agents.
- Decisions 013 (strict gate) and 038 (host thunk rejected) are binding.
- Convio team cap is 12 members; larger waves require two linked teams.

---

## 4. What is proven

1. **Native ELF** with no translation layer; `ldd` shows libc + loader only.
2. **Guest load / relocations**: private memfd copy of the frozen engine, 3,618
   constructors and 565,390 APS2 relocations, no guest byte patches.
3. **JNI surface**: 231-slot JNIEnv table, 49 cached jmethodIDs, native JavaVM
   installation, global-string and app-state seeds.
4. **Render diagnostic**: host X11/EGL window plus guest swap `0x649d422`;
   with opt-ins `SOBER_FRAME_EGL_INIT_DRIVE=1`, `SOBER_FRAME_FLOG_SINK=1`,
   `SOBER_FRAME_FLOG_ENABLE=1`, G8 reaches **verdict=1 ACCEPT** (C0–C10), with
   real ACQUIRE/DIMS RAs (`0x649c2db`, `0x649c308`, `0x649c314`) and a real
   guest-formatted H6 log through sink RA `0x232e080`. Default stays
   `R8_NO_ACQUIRE`, withhold=0.
5. **Callback diagnostic**: with opt-in latch + faithful arrays + publish
   re-drive, the genuine node discovery/finder bug was fixed (`bus+0x288` is a
   0x18-byte container; node at `+0x08`) and the guest reaches
   `gameLoadedCallback` (`callbacks=25`, publish rc=0). The strengthened gate
   correctly rejects the host-entered re-drive, so `callback=0` clean.
6. **Latch diagnostic**: `crashpad_return.c` supplies real AF_UNIX
   `SOCK_SEQPACKET` IPC with `SO_PASSCRED`, a 32-byte `CheckCredentials` peer
   message and a Bionic `__sF+0x130` `fputs`/`fflush` bridge;
   `nativeRunCrashpadHandler 0x2eca720` returns with `latch=1` and no SIGSEGV.
7. **AVD research oracle** (not the final runtime): authoritative APK boot shows
   the main process sets the latch without crashpad and uses
   `ActivityNativeMain`/`RBXSurfaceView` → JNIAppBridge surface lifecycle.

---

## 5. Full technical model of the remaining blockers

### 5.1 Flags-loaded latch — `0x7cdd149`
- TaskScheduler guard: `0x23862b0` compares the latch; zero → RBXCRASH string
  `0x36a389`, int3 at `0x6caef67`.
- Six `movb $1` writers in the frozen engine:
  `0x2eb7520` (client-settings/flag-filter), `0x2ec9fae` (crashpad),
  `0x56d31a3/3386/6a4b/6b80` (dialog-dismiss family).
- AVD evidence shows **main-process boot sets the latch without crashpad**, so
  the clean writer is `0x2eb7520`, reached when the client-settings/flag-filter
  path supplies the real Java FFlag state.
- In our harness that writer is gated off by `0x7697d08 == 1` and
  `0x7697cf0 bit0 == 1` (observed `0x7697cf0 = 0x21`). Twelve candidate external
  JSON payloads (`analysis/w7-01-gate-state.md`) all left bit0 set.
- Exact guest state that clears the gate in real boot is **unresolved**. A GDB
  hardware watchpoint on the byte was started but did not complete.
- No byte patches are permitted; any synthesized byte would be a seed.

### 5.2 App-shell boot order (authoritative DEX)
`MainGameActivity.E2` → `nativeSetDeviceInfo 0x2479c55` and
`nativeAppBridgeSetInitParams 0x2edcf18` → `uh/f.P2` → `0x233762c` +
`0x234df63` → `uh/e.j` guard → `nativeAppBridgeV2InitWithParams 0x24abf96` →
`0x26fe80f` / `0x25448f6` → join: `0x2ebd51a` StartGameWithParam +
`0x2ebca9e` SendGameLoaded. `nativeAppBridgeAppStart 0x247dc8d` must precede
InitWithParams; DEX throws otherwise when CrashWhenAppBridgeNotStarted is set.
One-shot id=1 registration: `0x24ad6a5 → 0x253cb56(1,&0x7234520)`, guard
`0x72a31ba`.

### 5.3 id=1 MessageBus node / callback
- Node dispatch `0x27eaf04` has **no static caller**; it is invoked only
  virtually via `node+0x08`, in a list whose container lives at `bus+0x288`.
- `0x27eaf04 → call [vptr+0x30] @0x27eaf2f (RA 0x27eaf32) → invoke 0x27eaf4c →
  notifier 0x27eaf6e → thunk 0x25848e0 → CallStaticVoidMethodV (slot 142) →
  gameLoadedCallback(J)V`, RA pair `0x2584960 → 0x27eafb3`.
- DEX has **no publisher** of event 1; Java only supplies `placeId` and consumes
  the callback. The genuine publisher is the guest game-load state machine
  (SessionReporterState_GameLoaded → virtual publish), absent in the harness.
- Provenance gate requires: name, frozen BuildID, cached class/method identity,
  placeId>0 at `0x72a31c0`, RA pair, `[rbp_not+8]==base+0x27eaf32`, node caller
  RA in guest `.text`, JavaVM vtable identity (`GetEnv 0x6c8e8a8`), and no
  harness taint. Host re-drive fails the 3-frame walk.

### 5.4 Surface lifecycle / render
- Authoritative default path: `ActivityNativeMain`/`RBXSurfaceView` → JNIAppBridge,
  **not** AGDK `onSurface*Native`. Forcing `EnableGameActivity10` applies the flag
  but still launches ActivityNativeMain; the DEX GameActivity branch is gated by
  `rh/i.x1()`, not that flag.
- Surface created → `0x26fe80f` (bind `0x26fea5a`, fromSurface RA `0x26fea7d`,
  window store `0x72a3308`); changed → `0x2773194`; redraw → frame loop;
  destroyed → `0x2ebc823` "Destroying ANativeWindow".
- G8 prerequisites: real `Surface→ANativeWindow` (`mNativeObject:J`), AGDK
  nativeData callbacks `+0x88/+0x90/+0x98/+0xa0`, live android_app looper,
  IoEvLoop (`pthread_create 0x6474599`, timerfd/epoll), RenderJob `0x276d36a`,
  JNIAppBridge >=5 for H6, post-bind `egl_init 0x649c21e`, graphics word
  `0x77a3d60 >= 0x0306`, and guest swap `0x649d422` via vtable `0x71912e8`
  slot `0x7191300`. All are absent or seed-only in the native harness.
- JNIEnv missing slots for authoritative DEX: fn91 `GetObjectField` (`0x2f8`) and
  fn92 `GetBooleanField` (`0x300`) for DeviceStaticParams; follow-up #562.

### 5.5 Crashpad route (diagnostic only)
`0x2ec9eed` sets the latch before its fatal tail. Valid JNI argv removes the
stdio `Usage` abort; `crashpad_return.c` supplies real IPC so the writer returns.
But real Java boot never calls `nativeRunCrashpadHandler` (only
`CrashpadHandler.main` in the crash-handler subprocess), so this is a host seed.

---

## 6. Fail-closed gate and negative controls

- `seeded_state` is the live OR-chain in `guest_app_shell.c` with ~90 env checks
  plus prefix families; new guest-mutating knobs must be added there.
- `tests/app-shell.sh` GREEN-iff criteria: notifier callback + clean state +
  render verdict + gate-owned provenance + exactly one marker; forbidden strings
  include ALT PATH, HONESTY NOTICE, HARNESS-THUNK, withhold, synthetic swap proof.
- N1–N30 negative A/B cases fail closed (host thunk, fake callback, synthetic
  draw, missing placeId, wrong RA, modded DEX, emulator-oracle relabeling, JVM
  JNIEnv, seed relabeling, log splicing).
- Withhold audit GAP LIST is `(none)`; SKIP exits 1; inherited `SOBER_*` seeds
  are scrubbed before the test run.

---

## 7. Verification recipe (run before any claim)

From `/home/user/sober-linux`:
```
cmake --build build -j4
bash tests/verify-native.sh
ctest --test-dir build --output-on-failure
SOBER_D09_WITHHOLD_AUDIT=1 bash tests/D09-withhold-audit.sh
LD_LIBRARY_PATH=build/ndk-stubs ./build/jni_probe
bash tests/first-frame.sh
bash tests/fault-baseline.sh
bash tests/guest-dlopen.sh
bash tests/app-shell.sh            # must stay RED until genuine
sha256sum /tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so
readelf -n ... | grep 'Build ID'   # 3e498d64… / 204ad049…
```
If `first-frame.sh` fails with XIO error 22, kill stale
`guest-init-host`/`gdb` processes and rerun.

---

## 8. Future-agent playbook

1. **Read memory first**: vault `Home/Goal/EngineFacts/Decisions/State/Ownership/
   ActiveTasks/Verification/AnalysisIndex`, then this document, then
   `README.md`, `docs/REVIEW-100.md`, `docs/decisions/013`, `038`.
2. **Never** use a forbidden evidence source (§2). Confirm engine SHA/BuildID at
   the start of every session.
3. **One Craft Anything CLI only**; share evidence IDs. AVD/emulator is a
   research oracle, never part of the final runtime.
4. **No fabrication**: no guest byte patches, fake vtables, host callbacks,
   synthetic observations, counter inflation, or relabeled seeds.
5. **Ownership**: lead owns `guest_app_shell.c`, CMake, tests, docs, README,
   vault root notes and git. Workers own specific `src/jni` modules or one
   `analysis/wN-*.md` note. Never edit another owner's files.
6. New knobs need a `SOBER_*_DEFAULT=0` kill switch and must be in `seeded_state`
   before any wiring; re-run N1–N30 after every change.
7. Commit/push only by the lead to `hidezfzefz/sober-linux` `main`.

---

## 9. Recommended next experiments (ordered)

1. Complete the **hardware watchpoint** on `guest_base+0x7697cf0` (or an
   equivalent runtime trace) to find the genuine writer/clearing condition for
   the client-settings latch gate; no byte patches.
2. Correlate the AVD-run `ClientAppSettings.json` payload and flag-provider state
   with the guest's parse of `0x7697cf0`; identify the exact key/value that
   clears bit0.
3. Drive the guest's DataModel/GameLoaded state machine on the AVD to capture the
   genuine MessageBus event-1 publish and notifier frames; then reproduce the
   sequence natively without seeds.
4. Re-evaluate the callback gate only after 1–3; keep marker WITHHELD.
5. Close JNIEnv fn91/fn92 (#562) with a fail-closed module and wiring.
6. Continue the negative A/B suite and withhold audit after every module change.

---

## 10. Honest bottom line

Infrastructure is strong and the seeded diagnostics prove the guest machinery
can be reached. The decisive claims are still at zero: no **clean**
notifier-driven `gameLoadedCallback(J)V`, no **clean** guest-tagged app-shell
frame, and therefore no marker. Score ≈66%; marker WITHHELD under 013/038.
