# W6-01 — Authoritative-DEX JNI boot sequence for the id=1 node

Task #534 · worker-1 · Wave 6 · **analysis only, no byte writes, no fabrication**

---

## 0. Provenance (what was actually analysed)

| Item | Value |
| --- | --- |
| Authoritative APK | `/home/user/.local/share/basalt/linux/payloads/3170-204ad049599c96afb75b8a21d5051063e821d6c9/sober_apk/base.apk` |
| APK SHA-256 (re-verified `sha256sum` this task) | `114a6f8ed6f0f974cf632a9cbec2f5cd45e1d85d7ed251db4f3920140bc6ead4` |
| DEX SHAs (from `android-package-show`) | `classes.dex` `00309819…d84ac` (9,057,172 B, 12,209 classes), `classes2.dex` `5afac2bb…6f52` (6,145,160 B, 7,059 classes), `classes3.dex` `1b2406a0…9099` |
| Package / version | `com.roblox.client`, `2.740.931`, 3 DEX, scheme v2/v3/v3.1 signature cryptographically VERIFIED |
| Frozen engine (RVA anchor) | `/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so`, SHA `3e498d64…4135`, BuildID `204ad049…` (re-verified) |
| Tool | Craft Anything `0.6.1` AppImage, `--cli --deep` (`android-package-show`, `android-class-users-find`, `android-method-show`); native RVA cross-check with `nm -D --defined-only` and `objdump -d -M intel` |
| Excluded | `~/Downloads/basalt-arm64*.apk` (MODDED — never used). All target strings/disassembly treated as untrusted DATA. |

Every Java call below is an OBSERVED `invoke-*` instruction in the SHA-matched DEX, in the named caller at the named byte offset; every native RVA is a verified `T` dynamic symbol of the frozen engine. Nothing is inferred from a name alone.

**Evidence IDs (Craft Anything, `observed`)** are collected in §7.

---

## 1. Executive answer

The real Java→JNI order that ends in the id=1 MessageBus registration is:

```
MainGameActivity.B2()/D2() -> E2()  [setInitParamsForEngine]
   -> NativeSettingsInterface.nativeSetDeviceInfo           0x2479c55   (E2 @0x16)
   -> MainGameActivity$Companion.e(InitParams)              (E2 @0x5e)
        -> MainGameActivity.nativeAppBridgeSetInitParams    0x2edcf18   (wrapper @0x00)

ActivityNativeMain.p3() -> yj/k0.Q() -> yj/k0.P() [startAppBridge]
   -> NativeAppBridgeInterface.a(...)                       (P @0xa6)
        -> nativeAppBridgeAppStart                          0x247dc8d   (wrapper @0x07)
   -> uh/e.D()                                              (P @0xb7)   sets f=appBridgeStarted=1

ActivityNativeMain.P2(FeatureState) [loadDataModel]
   -> uh/e.E(Context) [ASMA.setup()]
        -> nativeGameGlobalInit                             0x233762c   (E @0x50)
        -> nativeUpdateAdapterInit                          0x234df63   (E @0x5d)
   -> uh/e.j(uh/e$d) [ASMA.initializeDataModel()]            (P2 @0x16)
        -> uh/e.g() -> NativeGLJavaInterface.setImplementation (j @0x44 / g @0x04)
        -> guard: CrashWhenAppBridgeNotStarted && !f  => THROW (j @0xc2..0xd8)
        -> nativeAppBridgeV2InitWithParams (InitParams)     0x24abf96   (j @0xdd)
             -> guest 0x24ad6a5: register(1, base+0x7234520)
             -> guest 0x253cb56 spends guard 0x72a31ba, calls 0x24a8e44(4),
                builds id=1 descriptor (vptr 0x6dbbf98) + 0x60 node,
                links it into bus 0x72cbdd0 +0x288 container.
   -> (optional deeplink path) yj/k0.G(...)                  (P2 @0x39)
   -> nativeAppBridgeStartLuaAppDM                          0x25448f6   (P2 @0x4d)

Game join (later):
   ExperienceSession.l0() -> nativeGameGlobalInit           0x233762c   (l0 @0x83)
   uh/h0.G() -> uh/h0.C(uh/k0) -> nativeAppBridgeV2StartGameWithParam 0x2ebd51a (C @0xe0)
   uh/e.A(Z) -> uh/e.z(mk/c) -> nativeAppBridgeV2SendAppEventOnGameLoaded 0x2ebca9e (z @0x12)

App singleton (separate, opt-in): uh/e$f.b(Integer)
   -> nativePostClientSettingsLoadedInitialization3         0x2383c18   (@0x0e)
```

**The id=1 registration is not a JNI export.** It is one guest call at RVA `0x24ad6a5` inside `nativeAppBridgeV2InitWithParams` (`0x24abf96`): `lea rsi,[rip+…] # 0x7234520; mov edi,1; call 0x253cb56` (objdump verified). Everything a host can legitimately do is create the conditions for that ONE guest call to succeed; it cannot reproduce it without becoming a host re-drive (which the provenance gate correctly rejects).

**Independent corroboration (later in Wave 6, read-only cross-reference):** worker-6's authoritative-DEX call graph (`analysis/w6-06-dex-callgraph.md`) independently reports the same obfuscation-name corrections (`uh/f.k → uh/e.j`, `ActivityNativeMain.Q2 → P2`, `uh/j0.C → uh/h0.C`). worker-3's AVD oracle (`analysis/w6-03-surface-oracle.md`) observed the same runtime order on the authoritative build: AppStart `0x247dc8d` → Init `0x24abf96` → StartLuaAppDM `0x25448f6` → surfaceCreated → StartAppWithParams `0x26fe80f` → surfaceChanged → UpdateSurface `0x2773194` → redraw → destroy `0x2ebc823`. These agree with the DEX-derived order above; the emulator remains a research-only oracle and is not final-runtime evidence.

---

## 2. The exact chain, with DEX evidence

### 2.1 SetInitParams (the earliest native seeding)

`Lcom/roblox/client/startup/MainGameActivity;->E2()V`, signature `public final declared-synchronized void E2()` — log tag `"setInitParamsForEngine"` (`ev` in §7). Observed order:

| DEX offset | Instruction | Native target |
| --- | --- | --- |
| `0x05` | `AtomicBoolean.compareAndSet(false,true)` | one-shot guard (`W` field) |
| `0x0f` | log `"setInitParamsForEngine: initializing"` | — |
| `0x12` | `Luh/o;->c(Context)` → `DeviceParams` | — |
| `0x16` | `invoke-static DeviceParams` | `NativeSettingsInterface.nativeSetDeviceInfo` → **`0x2479c55`** |
| `0x19`–`0x54` | `InitParams.builder()` + `uh/o.b(Context,Activity)` → `uh/e$d`, then `setPlatformParams`, `setDeviceParams`, `setBaseURL` (`rg/w0.g`), `setUserAgent` (`rg/w0.Z0`), `setIsTablet` (`rg/w0.r0`), `setIsPotato` (`rg/w0.t0`), `setIsVrDevice` (`rg/w0.q0`), `build()` | — |
| `0x5e` | `invoke-virtual MainGameActivity$Companion.e(InitParams)` | wrapper |

`MainGameActivity$Companion.e(InitParams)V` is a **2-instruction wrapper** (`ev_d0cfc79f…`): `0x00 invoke-static MainGameActivity.nativeAppBridgeSetInitParams(InitParams)V`, `0x03 return-void`. Export RVA **`0x2edcf18`**.

Callers of `E2()`: `MainGameActivity.B2()` at `0x34`/`0x38` and `MainGameActivity.D2(MainGameActivity)` at `0x00` (class-users-find, 3 refs / 2 methods). **Harness omits this call** (already noted in `scale-a3-java-call-order.md` §5.4) — it seeds native InitParams state before the app-shell manager. It does **not** touch the MessageBus getter.

### 2.2 AppBridge start — the prerequisite the real boot satisfies before InitWithParams

`Lyj/k0;->P()V` ("startAppBridge", 91 instructions) observed at:

| DEX offset | Instruction | Effect |
| --- | --- | --- |
| `0x19` | log `"after_set_device_info_in_engine"` | — |
| `0x3a` | `zj/a.c(boolean)`, `0x3e` `NativeAppBridgeInterface.setIsFirstInstall(Z)` | — |
| `0x43`,`0x4a` | `yj/k0.z(Context,I,Z)` ×2 (font/init) | — |
| `0x68` | `zj/c.a(OnAppBridgeNotificationListener)` | — |
| `0x6e` | `NativeGLJavaInterface.setAppBridgeNotificationListener(...)` | — |
| `0x9c` | `zj/a.a(url, ua, isTablet, platform, androidId)` | (candidate path) |
| **`0xa6`** | `NativeAppBridgeInterface.a(String,String,Z,String,String)` | wrapper → `nativeAppBridgeAppStart` **`0x247dc8d`** |
| **`0xb7`** | `uh/e.D()` | sets `uh/e.f` (appBridgeStarted) = 1 at `D @0x01` |

`NativeAppBridgeInterface.a(...)` is an 8-instruction pass-through (`0x07 invoke-static/range nativeAppBridgeAppStart(String,String,Z,String,String,String)`). Callers of `a()`: `yj/k0.P() @0xa6` and `zj/a$a.d(...) @0x00`. `P()` is reached only from `yj/k0.Q() @0x45`; `Q()` only from `ActivityNativeMain.p3() @0x3d`, which is gated by config flags (`yj/k0.v()`, `ti/h.f()`, `startup/a.m()`, `ek/s.e()`).

### 2.3 InitWithParams — where the id=1 node is registered

`ActivityNativeMain.P2(FeatureState)V` ("loadDataModel()", 37 instructions, `ev` §7):

| DEX offset | Instruction | Native target |
| --- | --- | --- |
| `0x04` | log tag `"rbx.appshell"`, `"loadDataModel()"` | — |
| `0x07` | `uh/e.i()` singleton | — |
| `0x0b` | `uh/e.E(Context)` | → nativeGameGlobalInit + nativeUpdateAdapterInit (§2.3a) |
| `0x0e` | `uh/o.a(Context)` → `uh/e$d` params | — |
| `0x16` | `uh/e.j(uh/e$d)` | → `nativeAppBridgeV2InitWithParams` **`0x24abf96`** (§2.3b) |
| `0x19`–`0x39` | cold-start-deeplink path → `yj/k0.G(Activity,String)` | second app-start route (gated) |
| `0x4a` | `ActivityNativeMain.l3(FeatureState)` | — |
| `0x4d` | `nativeAppBridgeStartLuaAppDM()` | **`0x25448f6`** |

`uh/e.E(Context)V` ("ASMA.setup()", 44 instructions): one-shot guard `uh/e.b`; `0x2e rg/w0.V0()`; `0x3f FMOD.init(Context)`; **`0x50 nativeGameGlobalInit 0x233762c`**; `0x5a rg/w0.U0()`; **`0x5d nativeUpdateAdapterInit 0x234df63`**. (Second caller of nativeGameGlobalInit: `ExperienceSession.l0() @0x83`.)

`uh/e.j(uh/e$d)V` ("ASMA.initializeDataModel()", 108 instructions, `ev_1a0ea917…`) — the important body:

- `0x3c`–`0x42`: `if (!hasSettings || isInitialized) return` (fields `c`,`d`).
- **`0x44 uh/e.g()`** → `NativeGLJavaInterface.setImplementation(EngineJavaCallback2)` (`g @0x04`, non-null guard).
- `0x51`–`0xb9`: build `InitParams` — `baseURL` (`rg/w0.g`), `userAgent` (`rg/w0.Z0`), `isTablet` (`rg/w0.r0`), `buildVariant` (`ek/h.a`), `isVrDevice` (`rg/w0.q0`), `isPotato` (`rg/w0.t0`), asset-folder path from `uh/e$d.b.assetFolderPath` (`0xbc`–`0xc0`; null → throw `"Asset folder path is null"` at `0xe1`).
- **`0xc2`–`0xd8`: the app-start guard** — `config.T2()` = `CrashWhenAppBridgeNotStarted`; if true and `uh/e.f == 0` → `throw new RuntimeException("AppBridge is not yet started")` **before** the native call. This is hard DEX proof that `nativeAppBridgeAppStart` (via `yj/k0.P` → `uh/e.D`) must precede InitWithParams on a real device when that flag is on.
- **`0xd9 build()`; `0xdd invoke-static NativeGLInterface.nativeAppBridgeV2InitWithParams(InitParams)V; 0xe0 return-void`.**

Four callers of `uh/e.j`: `ActivityNativeMain.P2 @0x16`, `uh/a.F0(Bundle) @0x43`, `uh/a.N2() @0x2f`, `uh/e.x(Context,Surface,Activity) @0x19` (the surface-driven re-entry).

### 2.4 The guest-internal id=1 registration (native, verified)

`objdump -d -M intel --start-address=0x24ad680 --stop-address=0x24ad6c0` on the frozen `libroblox.so`:

```
24ad699: lea rsi,[rip+0x4d86e80]  # 0x7234520
24ad6a0: mov edi,0x1
24ad6a5: call 0x253cb56          <-- the ONLY guest call to the id=1 registration helper
```

Inside `0x253cb56` (frozen facts, EngineFacts.md / prior notes): one-shot guard `0x72a31ba` tested/spent at `0x253cb84`/`0x253cb91` **before** any descriptor is built; then `call 0x24a8e44` with `edi=4` at `0x253cb9d`; descriptor vptr `0x6dbbf98`, id `1`, slot `&0x7234520`; 0x60-byte node allocated by `0x1dbab02`; linked into `bus 0x72cbdd0 + 0x288` (a 0x18 container whose `+0x08` is the node). All guest-owned; the host supplies at most the two scalar args.

**Correction to the older note:** `analysis/scale-a3-java-call-order.md` cites the call site as `0x24ad699`; that is the `lea rsi`. The `call 0x253cb56` is at **`0x24ad6a5`** (matches `EngineFacts.md` and our `publish_default.h` comment `0x24ad6a5`). Use `0x24ad6a5` for breakpoints/gate citations.

---

## 3. Discrepancy with the older smali-based note (important for the lead)

`scale-a3-java-call-order.md` was derived from `/tmp/opencode/mini/out/smali`, a **different extraction** than the SHA-matched APK. The authoritative DEX uses different obfuscated short names. Trust the authoritative DEX (this note) for class/method names:

| Role | A3 note (old smali) | **Authoritative DEX (this task)** |
| --- | --- | --- |
| App-shell manager class | `uh/f` | **`uh/e`** |
| Data-model init | `uh/f.k(f$d)` | **`uh/e.j(uh/e$d)` @0xdd** |
| Global setup | `uh/f.F(Context)` (global + update init) | **`uh/e.E(Context)` @0x50/@0x5d** (`uh/e.g` is `setImplementation`) |
| Main activity entry | `ActivityNativeMain.Q2(FeatureState)` | **`ActivityNativeMain.P2(FeatureState)`** |
| Start app | `uh/f.G(Surface)` | **`uh/e.F(Surface)` @0x55** |
| StartGameWithParam | `uh/j0.C()` | **`uh/h0.C(uh/k0)` @0xe0** |
| GameLoaded send | `uh/f.A()` | **`uh/e.A(Z) -> uh/e.z(mk/c)` @0x12** |
| AppStart wrapper | `NativeAppBridgeInterface.a` ← `zj/a$a` | same, plus **`yj/k0.P() @0xa6`** as the primary caller |

The **native RVAs are identical** in both (all re-verified with `nm -D`), so the lead's native wiring does not change — only the Java-class citations do. The one numeric correction is the id=1 call site (`0x24ad6a5`, §2.4).

---

## 4. Lead-ready wiring interface

### 4.1 Native call order the lead's `guest_app_shell.c` should respect

1. **Before `0x24abf96` (InitWithParams):**
   - `nativeAppBridgeSetInitParams` `0x2edcf18` (and `nativeSetDeviceInfo` `0x2479c55`) — real early InitParams seed; app-state only.
   - `nativeAppBridgeAppStart` `0x247dc8d` — **must precede InitWithParams** when `CrashWhenAppBridgeNotStarted` is on (DEX §2.3b guard); it also sets `appBridgeStarted` via `uh/e.D` and is the direct JNI route to `0x24a8e44(4)` (`0x24a8daf`). Caveat: prior decision-C records re-entry into the broken `0x233319a` object path — treat as a candidate, not a proven clean prime.
   - The genuinely minimal, side-effect-free bus prime is the **internal** `((uintptr_t(*)(int))(base+0x24a8e44))(4)` — already exported by our module as `sober_publish_default_preinit_bus()` (opt-in `SOBER_PREINIT_BUS=1`, forced-withheld). This makes the two `0x24a8e44(4)` calls inside `0x253cb56` idempotent and protects the one-shot window `0x72a31ba`.
   - `nativeGameGlobalInit` `0x233762c` / `nativeUpdateAdapterInit` `0x234df63` (via `uh/e.E`).
2. **At `0x24abf96`:** let the guest execute the single `0x24ad6a5 → 0x253cb56(1,&0x7234520)` registration. Readiness is checkable with our existing `sober_publish_default_preinit_ready()` (bus guards + FFlag `0x72cb648` + app singleton/app+0x488).
3. **After registration:** `nativeAppBridgeV2StartAppWithParams` `0x26fe80f` (via `uh/e.F(Surface)`), `nativeAppBridgeStartLuaAppDM` `0x25448f6`.
4. **Game join:** `nativeAppBridgeV2StartGameWithParam` `0x2ebd51a` (caches placeId at `0x72a31c0`), then `nativeAppBridgeV2SendAppEventOnGameLoaded` `0x2ebca9e`.

### 4.2 Owned-module interface (this worker's files — all read-only/diagnostic or opt-in)

Already available and unchanged by this task:

| Function (file) | Use in the order above |
| --- | --- |
| `sober_publish_default_preinit_bus(base)` (`publish_default.c`) | step 1 bus prime, opt-in `SOBER_PREINIT_BUS=1`, guest state seed → WITHHELD |
| `sober_publish_default_preinit_ready(base)` / `bus_ready(base)` | step 1→2 readiness gate (read-only) |
| `sober_publish_default_selfreg_probe(base,…)` (`publish_default.c`) | proves guest self-registration vs host re-drive (guard=1 AND node!=NULL AND `host_redrive_calls==0`) |
| `sober_publish_default_prereqs(base,…)` | read-only prerequisite report for the id=1 chain |
| `sober_app_singleton_populate_auto(j,base)` / `app488_ready(base)` (`app_singleton_bridge.c`) | app singleton (`0x2383c18` / `0x72cb5e0`) prerequisite, opt-in, WITHHELD |
| `sober_publish_default_should_withhold()` | force-withhold the marker while any seeding knob is active |

New, purely declarative addition for this task (no guest call, no write, no behaviour change): `SoberBootStep` table + `sober_publish_default_boot_order(&count)` and `sober_publish_default_boot_order_name(id)` in `publish_default.{h,c}`. This gives the lead a single machine-readable copy of the order above (RVA, Java caller, DEX offset, role) so the wiring cannot drift from the DEX evidence.

### 4.3 What the host must NOT do

- Do **not** call `0x253cb56` from host code to "make" the node: that spends the one-shot guard with a host return address and is correctly rejected as a seed (`SOBER_PUBLISH_NODE_REDRIVE`, forced-withheld).
- Do **not** call the notifier `0x27eaf6e`, the generic thunk `0x25848e0`, or any JNI slot directly. Publication must enter the guest's own node dispatch at `0x27eaf04` with RDI = the genuine node found via `bus+0x288 → hdr+0x08` and RSI = `&place_id` (see `publish_default.h` §WHAT THIS MODULE DOES).
- Do **not** fabricate descriptor/node/vtable, patch bytes, or weaken gates 013/038.

---

## 5. Why this still does not open the clean path

This note establishes the *real* order and the precise native hand-off for the id=1 node; it does **not** supply the missing lifecycle event. Wave 5 / State.md already record: the guest reaches `0x27eaf04` only from a MessageBus event-1 publish by the lifecycle listener `0x24ad50a` (vtable `0x6dbc7c8+0x30`), which the harness never invokes; the app-start guard (`uh/e.f`) and the surface path (`uh/e.F(Surface)`) are Java/lifecycle-driven and absent in the native harness. Re-driving from the host is a seed and is force-withheld. Therefore:

- marker `app shell rendered` stays **WITHHELD** (decisions 013/038);
- `tests/app-shell.sh` stays RED;
- no byte patches, fake callbacks, host callbacks, or gate weakening were introduced.

---

## 6. Commands run (reproducible)

```sh
APK=/home/user/.local/share/basalt/linux/payloads/3170-.../sober_apk/base.apk
CA=/tmp/opencode/craft-anything-0.6.1-linux-x86_64.AppImage
sha256sum "$APK"
$CA --cli --deep android-package-show --path "$APK" --format text
$CA --cli --deep android-class-users-find --class-name 'Lcom/roblox/engine/jni/NativeGLInterface;' --method-name nativeAppBridgeV2InitWithParams --path "$APK" --format text
$CA --cli --deep android-class-users-find --class-name 'Lcom/roblox/client/startup/MainGameActivity;' --method-name nativeAppBridgeSetInitParams --path "$APK" --format text
$CA --cli --deep android-method-show --class-name 'Luh/e;' --method-name j --path "$APK" --format text
$CA --cli --deep android-method-show --class-name 'Lcom/roblox/client/startup/MainGameActivity$Companion;' --method-name e --path "$APK" --format text
$CA --cli --deep android-method-show --class-name 'Lcom/roblox/client/ActivityNativeMain;' --method-name P2 --path "$APK" --format text
# plus class-users-find / method-show for the other classes in §7
nm -D --defined-only /tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so | grep Java_com_roblox
objdump -d -M intel --start-address=0x24ad680 --stop-address=0x24ad6c0 .../libroblox.so
```

Note: the AppImage entry point requires `--cli` before `--deep` to run the CLI binary (its `AppRun` only execs `craft-anything-cli` when `$1 == --cli`); bare `AppImage --deep …` silently does nothing.

---

## 7. Evidence index

Craft Anything evidence IDs (`observed`, all from the SHA-matched base.apk):

| ID | What |
| --- | --- |
| `ev_359fcdba4a431b6ff44959937255208e4dd894f95b29449590df6ce47fa57757` | package/DEX inventory |
| `ev_4cfb67bf2f7c1572e3998fe6f8ddbf5dfd4ba9f6bd3519baafb108f82814f44c` | sole caller of `nativeAppBridgeV2InitWithParams` = `uh/e.j @0xdd` |
| `ev_46ba6760269a5f7242a6a0bd17b5f04fea6de34261a8243a14cc0e7905268876` | sole caller of `nativeAppBridgeSetInitParams` = `MainGameActivity$Companion.e @0x00` |
| `ev_d0cfc79fc0b2acb121ae7cc451a1161b1f5ed99ec96430d9e5e5b478fef309d2` | `MainGameActivity$Companion.e` bytecode (2 instr) |
| `ev_1a0ea917187f282b21bd230c075ad96e6e1aa87bd366b759a4340cca26e03d25` | `uh/e.j` bytecode (108 instr, guard + InitWithParams @0xdd) |
| `ev_3c277d434610fb1f12f759253088aff2b1061a7ceadfb9f988b61f291a82ae18` | callers of `uh/e.g` (setImplementation) |
| `ev_93d170264e4d99f09af678929b9617e069c4a415ad21e5cf7b797e214088a168` | callers of `nativeGameGlobalInit` |
| `ev_44d851ecd510148ed08ee9a096ca95efec022af22d3e213e38ec5cca79fbdfdf` | caller of `nativeAppBridgeAppStart` (via `NativeAppBridgeInterface.a`) |
| `ev_ff1b7a2d1bc40b14d43035c90a24ee1bce5907cd6262ad9ed9f09d7f28e1ac51` | callers of `NativeAppBridgeInterface.a` |
| `ev_cbe2f9a90d9a730b04e0e3d804c9011975c43718e04a8d6e440224db1e7a8021` | `uh/e.D` sets `f` (appBridgeStarted) from `yj/k0.P @0xb7` |
| `ev_00134cd8d749afc814ecfb2b3cab398bff088c7eb4282729f3516a12e48112e2` | `yj/k0.P` caller = `yj/k0.Q @0x45` |
| `ev_54faef36d232419b9a21add00dc17688179aebaf8d9c6468cab0a1e373cf4e77` | `yj/k0.Q` caller = `ActivityNativeMain.p3 @0x3d` |
| `ev_097d85cf63ead9d364b5dd5bb964fccc34877dbc5e09483c2688efa9876f2336` | callers of `MainGameActivity.E2` (SetInitParams) |
| `ev_258f7e8fd2cd8df13bc50ebba93f3aeda86e945146aee168d99be08467139e84` | `MainGameActivity.E2` calls `Companion.e @0x5e` |
| `ev_6bcf0f4225b42fa7254e517a7128317f838dff462534fa0addf92cf4f428b02e` | 4 callers of `uh/e.j` |
| `ev_6baebd0d9f69d2c3467466654dbb98382f9a407bcfedf7d2e0e10c5a08cb821f` | callers of `uh/e.F(Surface)` (StartAppWithParams) |
| `ev_561f2e2adc4dc602e140a4b7a5a6f5bacc66f7055` | `uh/h0.C -> nativeAppBridgeV2StartGameWithParam @0xe0` |
| `ev_93f5f485e74f641a687560826581f543cdb2973d9f83a31b34d2f0192eef72f5` | `uh/e.z -> nativeAppBridgeV2SendAppEventOnGameLoaded @0x12` |

Native evidence: `nm -D --defined-only` export RVAs listed in §1/§4; `objdump -d -M intel` slice `0x24ad680..0x24ad6c0` showing `call 0x253cb56` at `0x24ad6a5`.

Prior read-only notes: `analysis/scale-a3-java-call-order.md` (old smali; §3 corrections), `analysis/callback/task507-preinit-ordering.md` (bus getter window), `analysis/wave4-messagebus-preinit.md`, `sober-vault/{EngineFacts,Decisions,State,Ownership}.md`, `sober-vault/notes/CA01-craft-anything-verification.md`.

No `src/linux/`, CMake, test, docs, gate or other agent's file was modified by this task.
