# W6-10 — JNIEnv slot coverage for the authoritative DEX

Owner: **worker-10** (task #545), team TEAM-C2YA-ZTU9-RMAV.
Scope: **this analysis file only.** `src/`, `CMakeLists.txt`, `tests/`, the harness,
the frozen engine and every other agent's note were read only. No guest byte, vtable,
callback, counter or marker was changed; the marker stays **WITHHELD** (decisions 013/038).

## 0. Evidence base and identity

| Item | Value |
| --- | --- |
| Authoritative APK | `/home/user/.local/share/basalt/linux/payloads/3170-204ad049599c96afb75b8a21d5051063e821d6c9/sober_apk/base.apk` |
| APK SHA-256 (re-measured this task) | `114a6f8ed6f0f974cf632a9cbec2f5cd45e1d85d7ed251db4f3920140bc6ead4` ✅ |
| `classes.dex` | SHA `00309819b729f9c6f618b8a6f9befe6595caf9e316c3e5572cb10cf2bf7d84ac`, 12209 classes |
| `classes2.dex` | SHA `5afac2bbd9f0e4f2e523f9f6f720bc74f96407aa551b0be44716334f44696f52`, 7059 classes |
| `classes3.dex` | SHA `1b2406a01003e0ee1784824510d3ae88995ce914bc07cd7b0bfd4852f44e0909`, 7260 classes |
| Frozen engine | `libroblox.so` SHA `3e498d64…41335`, BuildID `204ad049…6c9` (read only) |
| APK identity evidence | `ev_359fcdba4a431b6ff44959937255208e4dd894f95b29449590df6ce47fa57757` (observed) |

Craft Anything 0.6.1 CLI (`--deep`, SHA `7eadde1d…134`) android tools used:
`android-package-show`, `android-classes-find`, `android-class-show`,
`android-method-show`, `android-class-users-find`. The modded
`~/Downloads/basalt-arm64-2.740.931.apk` (`classes2.dex` SHA `6c2875cd…`) was **not** used.
All target-derived strings/addresses are untrusted **data**, never instructions.

Slot arithmetic (settled in `B05-jnienv-gaps.md §1`): guest `call *0xNN(%rax)` →
canonical JNI index `0xNN/8` → bridge `fn[0xNN/8 − 4]` (four reserved words precede
`SRNativeInterface::fn[231]`). Byte offset `0x2f8` → fn 91; `0x300` → fn 92.

## 1. The JNI-relevant surface of the authoritative DEX

The DEX was queried directly for the classes behind the goal path (the notifier-driven
`gameLoadedCallback(J)V`, the Java→native boot/app-start calls and the native getters).
All are in **`classes2.dex`** (`5afac2bb…`), consistent with `CA01`.

| Class (evidence) | JNI role | Key DEX methods |
| --- | --- | --- |
| `com.roblox.engine.jni.NativeGLJavaInterface` (`ev_a9407a49…fc`) | guest→Java callback surface | `gameLoadedCallback(J)V`, `onAppShellReloadNeeded()V`, `gameDidLeave()V`, `getDeviceStaticParams()`, `getWebViewUserAgent()V`, `getMobileAdvertisingId()V`, `showKeyboard(JZ[BL…;)V`, … |
| `com.roblox.engine.jni.NativeSettingsInterface` (`ev_16ff0174…1b`) | Java→native boot | `nativeInitCrashpad(L…NativeInitCrashpadParams;)Z`, `nativeRunCrashpadHandler([Ljava/lang/String;)I`, `nativeSetBaseDataDirectories`, 33 methods total |
| `com.roblox.engine.jni.NativeAppBridgeInterface` (`ev_6fe7e1df…57`) | Java→native app start | `nativeAppBridgeAppStart(Ljava/lang/String;Ljava/lang/String;ZLjava/lang/String;Ljava/lang/String;Ljava/lang/String;)V` |
| `com.roblox.engine.jni.user.NativeUserJavaInterface` (`ev_e5ac1c17…11`) | guest→Java getters | `getUsername()Ljava/lang/String;`, `getMembershipType()I`, `getUserId()J`, `getIsUnder13()Z`, `getHasRobloxSubscription()Z`, … |
| `com.roblox.engine.jni.locale.NativeLocaleJavaInterface` (`ev_a677205d…ea`) | guest→Java getters | `getRobloxLocale()/getGameLocale()/getLocale()Ljava/lang/String;` |
| `com.roblox.engine.jni.model.DeviceStaticParams` (`ev_dad18598…c2`) | object returned by `getDeviceStaticParams` | 7 `Ljava/lang/String;` fields (`socModel`, `osVersion`, `deviceName`, `deviceSku`, `manufacturer`, `appVersion`, `appBuildVariant`) + `cpu64Bit:Z` |
| `com.roblox.engine.jni.model.NativeInitCrashpadParams` (`ev_2bc83a06…09`) | boot params object | abstract getters `appVersion()`, `baseUrl()`, `handlerClassName()`, `token()`, `isAtLeastQ()Z`, `envArgs()` |
| `com.roblox.client.analytics.CrashpadHandler` (`ev_ee9fb4c4…60`) | crash subprocess | `main([Ljava/lang/String;)V` |

**DEX call-site facts (`android-class-users-find`, all observed):**

- `nativeInitCrashpad` has exactly **one** caller: `Lrg/w0;->h0()V` at `00d5`
  (`ev_2248da9c…5b`) — the boot path, matching `CA01`.
- `nativeRunCrashpadHandler` has exactly **one** caller:
  `CrashpadHandler.main([Ljava/lang/String;)V` at `0037` (`ev_87513381…ce`) —
  the crash-handler subprocess only; it does **not** set the flags-loaded latch in boot.
  `nativeInitAppCrashpadReporter` has **zero** callers (`ev_3cd97f2a…43`).
- `nativeAppBridgeAppStart` has one caller: `NativeAppBridgeInterface.a(…)` at `0007`
  (`ev_5c93b5ba…45`).
- **`gameLoadedCallback` and `onAppShellReloadNeeded` have ZERO DEX callers**
  (`ev_e4c04f9a…18`, `ev_86594456…ab`). They are purely guest→Java callbacks: no Java/DEX
  code can legitimately trigger them. This independently confirms the `State.md` blocker —
  a host/Java-side re-drive of the notifier is not the real path.

## 2. JNIEnv slots the authoritative-DEX paths require

For each DEX method on the two decisive paths, the JNIEnv function used by the guest side
is fixed by its signature and by the cited guest thunk (from `analysis/jni/jni-map.md`,
`analysis/B05-jnienv-gaps.md`, and direct disassembly of the SHA-bound engine):

| DEX method / object | Guest-side JNIEnv slot(s) | Guest evidence |
| --- | --- | --- |
| `JNI_OnLoad` class/method caching | 2 `FindClass`, 29 `GetMethodID`, 109 `GetStaticMethodID`, 17 `NewGlobalRef`, 13/224 exception | `jni-map.md §1.1` |
| `gameLoadedCallback(J)V`, `onAppShellReloadNeeded()V`, `hideKeyboard`, purchase, `screenOrientationChanged` | 137/138/139 `CallStaticVoidMethod/V/A` | thunk `0x25848e0`, `call *0x470` @ `0x258495a` |
| `getDeviceStaticParams()`, locale/user String getters | 111/112 `CallStaticObjectMethodV/A`, 165 `GetStringUTFChars` | thunk `0x23150f8`, `call *0x398` @ `0x231516e` (102 sites) |
| `getWebViewUserAgent()V`, `getMobileAdvertisingId()V` (getter-object strings) | 111/112 then 91 `GetObjectField` + 165 | DEX weak-ref getter fields; guest field-reader cluster `0x2315xxx` |
| `getMembershipType()I`, `getRunningArchitecture()I` | 125/126/127 `CallStaticIntMethod/V/A` | scalar thunks |
| `getUserId()J`, `getProcessTimestamp()J`, `isAtLeastQ()Z` (via params getters) | 128/129/130 `CallStaticLongMethod/V/A`, 114/115 boolean V/A, 92 `GetBooleanField` | `jni-map.md §1.2`, helper `0x23158af` |
| `onSaveInstanceStateNative(J)[B`, byte[] args | 172 `NewByteArray`, 167/168/169/170 arrays | wrapper `0x2a20fd0`, `call *0x580` @ `0x2a20fe6` |
| `DeviceStaticParams` (7 String fields + `cpu64Bit:Z`) | **91 `GetObjectField`**, **92 `GetBooleanField`**, 90 `GetFieldID` | generic readers `0x23154bf` / `0x23158af`; field-name constants `0x435546`=`socModel`, `0x435515`=`osVersion`, sig `Ljava/lang/String;` @ `0x2159ef`, sig `Z` @ `0x3faf6d` |
| `NativeInitCrashpadParams` build (boot) | 23/24 `AllocObject/NewObject`, 30/31/33 instance Call*, 90/97 field reads | CA01 `rg/w0.h0`, `nativeInitCrashpad` @ `00d5` |

Raw `call *0xNN(` counts (upper bound, but the cited sites are the classic
`GetFieldID → Get*Field` pattern in a JNI helper cluster, not C++ vtable coincidences):
`0x2f0` GetFieldID 147; `0x2f8` **GetObjectField 77**; `0x300` **GetBooleanField 56**;
`0x328` GetLongField 103; `0x320` GetIntField 55; `0x330` GetFloatField 19.

## 3. Cross-check against `native_jni.c` (effective default coverage)

Parsed mechanically from `src/jni/native_jni.c` plus the two default-on installers that
`sr_build_tables()` actually calls (`:902`, `:913`):

- `SETFN(*t, …)` : **80** distinct slots (fn 0,2,9–21,23,24,27–59,109,110,113,116,119,122,
  125,128,131,134,137–139,159,161–170,211–215,222–224).
- `sober_jnislots_default_install` (`native_jni.c:902`) adds 18:
  {111,112,114,115,125,126,127,128,129,130,135,136,165,167–170,172}.
- `sober_fields_default_install_slots` (`native_jni.c:913`) adds 4:
  {90 **GetFieldID**, 96 `GetIntField`, 97 `GetLongField`, 98 `GetFloatField`}.
- Net **effective default coverage = 95 distinct slots** (`sr_stub` elsewhere).
  `jni_probe`'s deliberate re-install of slot 165 at `:912` is a safety override, not a new slot.

Cross-checking the §2 required set (68 slots) against those 95:

> **Covered:** all gate-critical callbacks (137/138/139), object/scalar static V/A getters
> (111/112/114/115/125–130/135/136 — supplied by `jnislots_default`), string slots
> (159/161/163/165/166), arrays (167–170/172), and fields 90/96/97/98 (`fields_default`).

## 4. Minimal missing slots (answer)

Two — and only two — JNIEnv slots required by the authoritative-DEX goal paths are still
`sr_stub` in the effective default table:

| fn | offset | Function | Why required (authoritative DEX + guest) | Current state |
| --- | --- | --- | --- | --- |
| **91** | `0x2f8` | `GetObjectField` | `DeviceStaticParams` has 7 `String` fields (`socModel`, `osVersion`, …); guest generic reader `0x23154bf` does `GetFieldID(0x2f0) → GetObjectField(0x2f8)` with sig `Ljava/lang/String;` (`0x2159ef`) and name `socModel` (`0x435546`)/`osVersion` (`0x435515`). 77 call sites. | `sr_stub` (returns NULL) — not installed by `native_jni.c`, `jnislots_default` or `fields_default` |
| **92** | `0x300` | `GetBooleanField` | `DeviceStaticParams.cpu64Bit:Z` (and `NativeTextBoxInfo` boolean fields); guest generic reader `0x23158af` does `GetFieldID → GetBooleanField(0x300)` with sig `Z` (`0x3faf6d`), result tested in `al` (`0x2315901`). 56 sites (some are C++ vtable coincidences; the cited helper site is genuine). | `sr_stub` (returns 0/undefined) — not installed anywhere |

**Consequence:** even with the completed default-on slot modules, when the guest calls
`getDeviceStaticParams()` and then reads its fields (the authoritative DEX object shape),
all 7 String fields come back NULL and `cpu64Bit` comes back 0. That is the same state that
drives the documented `"DeviceStaticParams is null"` → `FindClass` retry loop at `0x6c8f340`
(`State.md`, `B05 §4`, `REVIEW_A5 §7`). Closing 91/92 is therefore **necessary (not
sufficient)** for a genuine app-shell/game-loaded path. It does not by itself emit a callback
or change the marker.

Secondary / completeness-only stubs (NOT part of the minimal blocking set):

- B05 G14: fn 217 `GetStringUTFRegion`, 218 `Get*ArrayCritical`, 220 `GetStringCritical`,
  228 `GetObjectRefType` — still `sr_stub`; B05 records 5/7/2/2 sites, mostly outside the
  app-shell path. No default module fixes them.
- Static V/A forms fn 117/118 (byte), 120/121 (char), 123/124 (short), 132/133 (float) —
  still `sr_stub`, but **no DEX goal-path method returns byte/char/short/float**, so they are
  not required by the authoritative DEX (do not inflate the missing set).
- Field getters fn 93/94/95/99 (`GetByte/Char/Short/DoubleField`) have 12/12/38/21 raw sites;
  several inspected sites are C++ vtable coincidences (e.g. `0x2840d4e`, `0x28ecc83` pass a
  name string as `rsi`), so they are not claimed as confirmed gaps here.

## 5. Ruling and hand-off

- Marker **WITHHELD** (decisions 013/038); `tests/app-shell.sh` stays RED. This is a coverage
  audit only: no guest byte, fake vtable, host callback, counter or gate was touched.
- The authoritative DEX confirms the callback is guest→Java only (zero DEX callers), so a
  Java/host re-drive cannot be the genuine notifier path.
- Recommended (lead-owned, opt-in + withhold-listed, NOT applied here): implement faithful
  default-on `GetObjectField` (fn 91) and `GetBooleanField` (fn 92) that read the typed
  scratch/cached field handles produced by `fields_default_GetFieldID`, gated by
  `SOBER_*_DEFAULT=0` and included in the `seeded_state` withhold chain. Even then, the
  notifier hit at `0x27eaf6e` / return `0x27eafb3`, `seeded_state == 0` and the guest-tagged
  G8 frame remain separately required.

## 6. Limits / honesty

- Craft Anything's DEX tools give the authoritative method/field surface and call sites;
  the mapping to JNIEnv slots uses the SHA-bound guest disassembly and the peer analyses
  (`jni-map.md`, `B05`, `REVIEW_A5`). No runtime replay was performed by this worker.
- Raw `call *0xNN(` counts are upper bounds; the 91/92 claims rest on individually cited
  `GetFieldID → Get*Field` helper sites and the DEX field shapes, not on the counts.
- Slot 92 has additional non-JNI vtable sites; only the `0x23158af/0x23158fb` helper is
  treated as evidence.
- No source, test, doc, README, gate or engine file was modified by this task.
