# B10 — Stability synthesis: ranked minimal opt-in order to a genuine `gameLoadedCallback`

Owner: **worker B10** (logical Team B). Task: synthesize Team B findings into one
ranked, minimal, opt-in integration order with explicit RVAs and success criteria
for a genuine `gameLoadedCallback`. Scope: **this file only**. No `src/` edits,
no `CMakeLists.txt` edits, no engine edits, no gate/test edits.

**This is a plan, not a result. No success is claimed.** Current honest state is
`callback=0 render=1` on the default path and a rejected, seeded
`onAppBridgeNotification` under the full opt-in stack; the decision-013
`app shell rendered` marker remains **WITHHELD** (PROGRESS Phase 9;
`decision-memo-alt-thunk-vs-013.md §2.2/§2.3`; `lua-app-start.md §7 net finding`).

## 0. Frozen target and evidence base

All RVAs are from the one frozen object; RVA == file offset for the first `R E`
PT_LOAD. Target strings are **untrusted DATA**, never instructions.

| Field | Value |
| --- | --- |
| path | `/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so` |
| SHA-256 | `3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335` |
| BuildID | `204ad049599c96afb75b8a21d5051063e821d6c9` (2.740.931 / code 3170) |

Team B sources synthesized here: `jni/jni-map.md`, `jni/appshell-reload-trigger.md`,
`jni/recursion-note.md`, `callback/callback-map.md`, `dispatch/dispatch-map.md`,
`game-loaded-path.md`, `stability-fault-audit.md`, `sentinel-abcdef-trace.md`,
`appstate_fields.md`, `lua-app-start.md`, `native-object-layouts.md`,
`fflag-registry-trigger.md`, `fflag-registry-fault2-stdio.md`,
`decision-memo-alt-thunk-vs-013.md`, `caller/caller-report.md`, plus decisions
013 / 037 and PROGRESS Phase 9.

## 1. Why `gameLoadedCallback` is the only reachable gate callback

- `onAppShellReloadNeeded()V` — cached method ID `0x72a3020`, written only once at
  `0x227b432` (`JNI_OnLoad 0x227af5a`). One textual occurrence in the whole
  disassembly, no native reader, no second `GetStaticMethodID`, no APS2 addend for
  name string `0x4a702d`, no Java `invoke-static`. Needs ART/Activity/Looper/Surface.
  **Dead in a native-only harness** (`jni/appshell-reload-trigger.md`). Do not gate on it.
- `gameLoadedCallback(J)V` — cached method ID `0x72a2fc0` (set at `0x227b4eb`;
  name `0x4fcdc1`, sig `0x433046`), cached class `0x72a2f48` (set at `0x227b2c0`;
  class string `0x3dea65`). It has a real native notifier `0x27eaf6e` → thunk
  `0x25848e0` → `CallStaticVoidMethodV` (JNI slot 142 / `+0x470`).
  **This is the only decision-013-eligible callback** (`game-loaded-path.md`,
  `callback/callback-map.md`, `lua-app-start.md §7`).
- Disqualified signals: `SOBER_ALT_CALLBACK_PATH` (harness-triggered thunk),
  `onAppBridgeNotification` (`0x72a3028` → thunk `0x3dea91`, JNI plumbing),
  accessor callbacks `surface()/platformParams()/vrContext()`. None may emit the marker.

## 2. Ranked minimal opt-in integration order

Each row is one opt-in/action, default **OFF**, one new variable per A/B run.
Rank = dependency order: a later step is only meaningful once the earlier
success criterion holds. All are real guest-code routes or guest-shaped-object /
ABI plumbing — **no guest byte patches, no fake vtable, no synthetic callback**.
Ownership of any `src/` change below belongs to lead/the named module owner;
worker B10 changes nothing.

| # | Opt-in / action | Explicit RVAs | What it provides | Read-only success criterion |
| --- | --- | --- | --- | --- |
| **1** | **Baseline + identity.** Confirm frozen SHA/BuildID; record default run (`callback=0 render=1`, fault `0x2eb4b65`), all opt-ins unset. | engine SHA `3e498d6494…`; baseline fault `0x2eb4b65` | Trustworthy starting state; proves the strict gate withholds correctly. | SHA/BuildID unchanged; zero `app shell rendered` emissions; gate log `callback=0 render=1`. |
| **2** | **Bionic TSD GOT rebinding** — `SOBER_BIONIC_TSD_GOT=1`. Resolve `pthread_{get,set}specific` with `dlvsym(stub,"…","LIBC")` (or post-patch GOT), not `dlsym(RTLD_DEFAULT,…)`. | GOT `0x72298b0` (getspecific) + setspecific sibling; key globals `0x7282000` (key 2), `0x72a1b08` (key 3); consumers `0x1e47927`, `0x227aafa`, `0x2ea6440`; recursion wrappers `0x6ca7f72`/`0x6ca7f95`, `0x6c8f340`/`0x6c8f368`; dispatcher `0x2a12e80`, singleton `0x2a0de60` | Decision-013 root cause: guest TSD keys land in the private Bionic array, so `JNI_OnLoad` can return and cached class/method globals live; removes scheduler-gated `ExceptionCheck`/`FindClass` re-entrancy. | GOT word == stub `getspecific`; `pthread_getspecific(2)` returns NULL; `0x1e47927` takes `0x1e4795c`; no `0x6ca8014`/`0x6c8f408` stack-overflow frames; `JNI_OnLoad` reaches `0x72a2f48`/`0x72a2fc0` populated. Also make `ExceptionCheck` pure/non-reentrant (return 0), zero scheduler `obj+0x28` and `obj+0x70` once. |
| **3** | **Full Bionic `__sF`** — `SOBER_BIONIC_SF_FULL=1`. Export `__sF` ≥ `0x198` bytes with valid `_lock` at `+0x88`, or redirect only `__sF+0x98`/`+0x130` in `fwrite`/`fflush` trampolines. | `__sF` GLOB_DAT `0x72281d8`; `fflush` JUMP_SLOT `0x7229750`; logger `0x2e61ca0` (`fwrite 0x2e61cf1`, `fflush 0x2e61d13`); formatters `0x2e61750`/`0x2e617e0` (`+0x40` = FILE*) | Guest logger/flag-provider task survives ctor[0]; clears the stdio ABI fault seen after step 4. | No `libc+0x7c8ff / si_addr=0x8`; deferred flag-provider registration `0x6caed7c` completes. |
| **4** | **Replay FFlag `ctor[0]`** — `SOBER_REPLAY_FFLAG_CTOR=1`: call `guest_base+0x2e62390` after APS2 RELA/bootstrap seeds and **before any postsettings/JNI replay**. Narrow alternative `0x2e618b0`; never call `0x2e61280` directly. | `init_array[0] 0x2e62390` → `0x2e618b0` (guard `0x72a0a30`; acquire `0x2a0d6c0`, release `0x2a0d7d0`) → `0x2e61280` → `0x2e5bc10` → `0x2e7b850` (lazy `0x2e79af0`→`0x2e77400`, sentinel `0x72a0b88`, block `0x72a0cd0`/`0x72a0cc0`, guard `0x72a0cd8`); handle `0x72a0520`, control block at `+0x38` | Root of the fault chain: real shared_ptr control block in FFlag registry; kills fault #1 `0x2e7b8e2` and `0x2e5d42a`. | Guard `0x72a0a30` set (observed `0x101`/257); `*(0x72a0520)==base+0x6db4008`; `*(0x72a0520+0x38)` non-NULL and != sentinel; `0x2e7b8e2` and `0x2e5d42a` gone. |
| **5** | **Wiring order fix** (lead-owned, no new mechanism): move the `SOBER_JSON_APP_SINGLETON` postsettings block to **after** the `SOBER_REPLAY_FFLAG_CTOR` block (`guest_app_shell.c:858` currently precedes `:875`). | `0x2383c18` postsettings replay; `0x2e62390` ctor replay | Postsettings no longer reaches fault #1 first. | Log shows ctor-replay line before postsettings line; `0x2e7b8e2` not reached by JSON path. |
| **6** | **Populate app singleton via real guest code** — `SOBER_JSON_APP_SINGLETON=1` + `SOBER_NATIVE_OBJECTS=1`; List must be `sober_native_java_list()` (vptr `0x6db3ec0+0x10`), with `+0x30` control block (refcount 1 at `+8`, destroy slot from `0x72a0b88`) and `+0x40` element array both from guest allocator `0x1dbab02`, `+0x48` count. Do **not** use plain zeroed `0x40 java/util/List`. | sole writer `0x238497d` (alloc `0x1dbab02`, ctor `0x608f85a`); gate `0x6096928`, byte `0x77979c0`; fault #2 `0x24ad562`; destructor `0x2e5baa0`, retain `0x2e7b8d0` | Kills fault #2; enables flag=1 controller installer. | `*(0x72cb5e0)` non-NULL and attributable to `0x238497d`; `0x24ad562` gone; gate byte/result logged. |
| **7** | **Flag overrides + AppReady + InitWithParams**, in Java order. `nativePreloadFlagOverrides 0x2edd859` with JSON-object string `{"EnableAppBridgeComponents":true}` (not `key=value`) → `nativeRetryInit 0x2edd739` → state machine `0x2ee2766` (call at `0x2edd760`); verify flag `0x72cb648==1`. Then `nativeGameGlobalInit 0x233762c`; `nativeAppBridgeV2SendAppEventOnAppReady 0x2ebcf36` with 4th token one of Game/Games/Home/Chat/More/AvatarEditor (e.g. `("AppShellNotifications","123456789","StartConversationWithUserId","Chat")`); then `InitParams 0x24abf96` with all getters backed (`baseURL`, `userAgent`, `platformParams`, `deviceParams`, `isTablet`, `pioneerPlaceId`, `buildVariant`, `isVrDevice`, `isPotato`). | flag name `0x43650d`; 4th-arg tokens `0x656d6147` Game, `0x2ebd00c`, `0x656d6f48`, `0x74616843`, `0x65726f4d`, `0x2ebd021`; getter helper `0x232dbbc` → `GetMethodID` `call *0x108` (slot 33) → `0x2315d18`/`0x2315532`; **notifier registration** `0x24ad699` → `0x253cb56(1,&0x7234520)`, slot `0x6dbd860` APS2→`0x27eaf6e` | Sets the flag through the guest setter, primes app state, and — critically — registers the GameLoaded/GameLeave callback family whose slot is the notifier. | `0x72cb648==1`; `0x72cb5e0` non-NULL; `0x6dbd860 == base+0x27eaf6e` and `*(0x7234520)==&slot(0x6dbd860)`; InitParams getters dispatched by name with correct types. |
| **8** | **Close handler gaps / JNI types** — use `SOBER_STARTAPP_PARAMS=1` (`src/jni/startapp_params.c`, `sober_startapp_params_make`, guest vptr `base+0x6dbca20`, parser `0x26feb9b`) instead of sparse object + `start_app_get`/`real_path_get`. Back **all ten** StartAppParams getters with correct JNI types/values; seven currently have no handler and fall through `sr_dispatch_call` (`native_jni.c:498-513`) to a generic `java/lang/String`. Give object/String-returning callbacks non-NULL typed values; make JNIEnv table complete. | wrong-type cases: `appUserId()J` (long, -1 until login) wrongly returned as jstring `"1"`; `membershipType()I`, `isUnder13()Z` primitives; `selectedTheme` default `"Light"` (harness `"default"`); `appStarterPlace` `"rbxasset://places/Mobile.rbxl"` (harness `"0"`); `appStarterScript` `"LuaAppStarterScript"`; `username` `""`; `vrContext` must be NULL (`rg/a1.E0()` false). Non-NULL returns needed: `getDeviceStaticParams` (`DeviceStaticParams` object), `getWebViewUserAgent`, `getMobileAdvertisingId`, locale/user getters. Required JNIEnv slots: `FindClass 0x30`(6), `GetStaticMethodID 0x388`(113), `NewGlobalRef 0xa8`(21), `ExceptionCheck 0x720`(228), `ExceptionClear 0x88`(17), `CallStaticVoidMethodV 0x470`(142), Call*Method family, `DeleteLocalRef`. | Removes the `DeviceStaticParams is null` warning-driven FindClass loop (`0x6c8f340`) and the generic-String substitution that corrupts getter reads. | No generic `java/lang/String` handed to a typed getter; all ten getters log correct JNI types; no `0x6c8f340` recursion. |
| **9** | **Real delegate + user controller** — `StartAppWithParams 0x26fe80f` (faithful params from #8) → `nativeAppBridgeStartLuaAppDM 0x25448f6` (writes `app+0x408` at `0x25d7e75`/`0x25f7e7e`; reaches mode-4 driver `0x25449fc` via `0x2eb6b2a`/`0x2337cc8`) → dispatch genuine controller installer vtable slot `0x6dbc7f8 → 0x24ad50a → 0x24ae1fe` (writes `app+0x440`), with `this+0xb0` shared_ptr built at `0x24ac5a9`/`0x24ac5c8` (allocator `0x1dbab02`). Remove `SOBER_SEED_USER_CONTROLLER`/`SOBER_SEED_APP_DELEGATE` once guest writes are seen. | delegate vptr `0x6e047b0` (consumer `0x320581b`, `call *0x90`); `userDidLogin 0x2771f6c` → `0x2772384` at `0x2772129`, fault #3 `0x2772403` (`cmpl $0,0x140(%r14)`); controller flag `0x72cb648` | Kills fault #3; reaches `SingleSurfaceApp::userDidLogin` completion. | `app+0x408` and `app+0x440` non-NULL, guest-heap pointers, written by guest RVAs above; `0x2772403` and `0x320581b` gone. |
| **10** | **Handle/pointer fidelity for the app-start consumer** (only if the AppStart `0x247dc8d` / mode-4 `0x24a8e44` route is used; avoid it if `StartGameWithParam` alone suffices — `recursion-note.md §4` warns AppStart re-enters the broken `0x233319a`). Make handles guest-shaped: A `+0=0xc0dedbad`, `+0x10=0`, `+0x20` canonical, `+0x68→B`, `+0xf90→D`; B `+0=0xbab1e`, `+0x10` table, `+0x20` cap≥16, `+0x120→C`; C≥16 bytes; D i32 at `+8=0`. Fix **pointer provenance**, not `+0x68`: at `0x2333164` write the canonical result of nullary getter `0x231f294` into `[rbp-0x48]`, then continue `call 0x233319a` at `0x2333168`. Also fill app `+0x488` (NULL deref at `0x31a1360` comes from caller `0x320b0ba mov 0x488(%rbx),%rdi`; `rax=0xabcdef0123456789` is only the canary from GOT `0x72293f8`). | singleton getter `0x231f294`; ctor `0x231f31f` (`allocate 0x231f33d`, magic `0x231f34b` — type-defining, **not app-shell reachable**); alloc slot `0x726c0c0` (APS2 addend `0x253225e`, runtime rebound to `0x231f0e2` at `0x231e915`); free bridge `0x726c0b8 → 0x1dbe559` (**do not patch**, DECISION C); consumer `0x233319a` via `0x23330e7` (magic check `0x2333109`, slot `[rbp-0x48]` at `0x2333100`); bogus value `0x233263f` (stale return address); validator `0x245605d` (success return 4 — **do not byte-patch**, decision 014). | Removes the `0x2456079`/`0x23331ae` garbage-pointer faults without bypass patches. | No `0x2456079`/`0x31a1360` fault; consumer receives a canonical guest-arena handle; no `xor/ret` patch installed. |
| **11** | **Drive the genuine GameLoaded event** — `nativeAppBridgeV2StartGameWithParam 0x2ebd51a` with a faithful `StartGameParams` (getters `gameId`, `accessCode`, `username`, `userId`, `isUnder13`, `placeId`), then `nativeAppBridgeV2SendAppEventOnGameLoaded 0x2ebca9e` with three jstrings (after manager setup). Optionally `nativeScheduleOnFirstFrame 0x2ebf950`. This drives Lua DataModel load → `SessionReporterState_GameLoaded` → dispatch via `0x7234520 → 0x6dbd860` → notifier `0x27eaf6e(placeId)`. | event wrap `0x2315532`/copy `0x1dc1978`; event object vtable `0x6dbcc20`; dispatch `call 0x2eb6b2a` at `0x2ebccec`; sink `0x72a31a8` via `0x28f1290`/`0x2337cc8`; notifier log string `0x36c7c9`, tag `0x550ef3`; phase-3 render `0x649d422` | Produces the guest-side callback the gate needs. | **Guest hit-counter at `0x27eaf6e`** (or call-stack return address `0x27eafb3`) with `RSI==placeId`, `0x72a2fc0` non-NULL, and slot `0x6dbd860 == base+0x27eaf6e`. This is the discriminator vs the ALT path (which returns to `0x112d`-area harness code). |
| **12** | **Strict decision-013 reconciliation** (lead-owned). A callback seen only under the diagnostic opt-ins above **cannot** emit the marker: 17/17 documented opt-ins plus 14 guest-mutating knobs force-withhold (`guest_app_shell.c:1150-1162`). Either demonstrate the genuine callback with every disqualifying knob unset after real guest state is produced and the knobs removed, or obtain an explicit lead ruling on which real-path enablers may count. Recommended verifier option B+C: delete stale `tests/app-shell.sh:112-116` ALT-thunk assertion, keep genuine-callback + phase-3-render gate, park the ALT thunk in a default-OFF labelled diagnostic. | marker emission sole site `guest_app_shell.c:1280`/`:1191` gated on `shell_cb && render_ok`; phase-3 render `0x649d422`; withhold OR-chain `:1150-1162`; eight phase-8 knobs (`decision-memo §2.3`); stale test assertion `app-shell.sh:112-116` | Keeps the gate honest; prevents a false green. | `shell_cb && render_ok` with `seeded_state==0`, render `0x649d422` observed, engine SHA unchanged, no `ALT PATH guest call 0x25848e0` / `HONESTY NOTICE`, `SOBER_ALT_CALLBACK_PATH` unset. |

### 1a. JNI surface prerequisites (cross-cutting, folded into #2/#8/#11)

- **Class objects:** `com/roblox/engine/jni/NativeGLJavaInterface` (`0x3dea65`) must resolve to cached global `0x72a2f48` (class string `0x3dea65`, `FindClass` `0x227b2a3`, `NewGlobalRef` `0x227b2ba`, store `0x227b2c0`). Other classes `JNI_OnLoad` requires: `LoggingProtocol`, `NativeLocaleJavaInterface`, `NativeUserJavaInterface`, `NetworkUtils`, `memstorage/Connection`, `messagebus/Connection`, `NativeTextBoxInfo` (`jni-map.md §4`).
- **Strings:** `getWebViewUserAgent`/`getMobileAdvertisingId`/locale/user getters must return non-NULL `jstring`s or app-shell startup fails; `nativePreloadFlagOverrides` needs a real JSON-object `jstring`; wrapper `0x2315532`.
- **Arrays:** JNI array shapes needed for the lifecycle table (`onSaveInstanceStateNative (J)[B`, `onExtendedAnalyticsRecvCallback ([BI)V`, `nativeInitializeNativeFlags ([Ljava/lang/String;)`, and `java/util/List` element array `+0x40`/count `+0x48`). The `sober_native_java_list()` backing words must be guest-allocated (#6) or the destructor `0x2e5baa0`/retain `0x2e7b8d0` misinterpret them.
- **Getter dispatch:** most getters are read by name via `0x232dbbc` → `GetMethodID` (`call *0x108`, global slot 33) → `CallObjectMethod` family `0x2315d18`/`0x2315532`; registering a handler by name is necessary but **not sufficient** — the returned handle's JNI type must match the getter signature.

## 3. Genuine `gameLoadedCallback` — success criteria (all must hold)

A pass is legitimate only if **every** item is true; any one false ⇒ no pass, marker stays WITHHELD.

1. **Guest-side notifier proof (decisive discriminator):** hit-counter/breakpoint at `0x27eaf6e` entered — or thunk call-stack return address `0x27eafb3` — with `RSI == placeId` and cached method `0x72a2fc0` non-NULL. Return address in the `0x112d`-area harness code = ALT path = fail.
2. **Thunk ABI:** `0x25848e0` called with `RDI = SREnvObj*` (first word = JNINativeInterface table), `RSI = jclass 0x72a2f48`, `RDX = jmethodID 0x72a2fc0`, `RCX = jlong placeId`, `AL=0`; bridge slot 142 = `fn[138]` (`native_jni.c:831` → `sr_CallStaticVoidMethodV`) with SysV va_list `gp_offset=0x18`, `fp_offset=0x30` (`0x258494a`), dispatch `call *0x470(%rax)` at `0x258495a`.
3. **Registration proven:** `0x24ad699` executed `0x253cb56(1,&0x7234520)`; `*(0x7234520)==&slot(0x6dbd860)` and slot `0x6dbd860 == base+0x27eaf6e` (APS2 addends `0x6dbd860→0x27eaf6e`, `0x7234520→0x6dbd860`).
4. **Bridge log:** `guest->Java callback: gameLoadedCallback`, `last='gameLoadedCallback'`, `callbacks` increased ≥1 over the pre-step-11 baseline (baseline 3–4, last `onAppBridgeNotification`).
5. **No disqualified source:** no `ALT PATH guest call 0x25848e0`, no `HONESTY NOTICE`, `SOBER_ALT_CALLBACK_PATH` unset; not `onAppBridgeNotification`, not accessor callbacks.
6. **Real guest state:** `0x72cb648==1`, `*(0x72cb5e0)` non-NULL (via `0x238497d`), `app+0x408` and `app+0x440` non-NULL and guest-written; no fake vtable, no byte patch, no synthetic controller/delegate.
7. **Phase-3 render:** guest render RVA `0x649d422` observed (existing phase-3 evidence).
8. **Withhold chain clean:** all eight phase-8 knobs plus the additional guest-mutating knobs unset ⇒ `seeded_state==0`; `shell_cb && render_ok` legitimately true.
9. **Invariants green:** build clean, `verify-native.sh` PASS, ctest 2/2, `ldd` libc/ld-linux only, default fault unchanged `0x2eb4b65`, engine SHA/BuildID unchanged before and after.
10. **Test contract reconciled (lead ruling):** stale `tests/app-shell.sh:112-116` ALT assertion removed or explicitly overridden; marker may be emitted only from the genuine path.

## 4. Non-goals / honesty constraints

- No `src/`, CMake, gate, test, decision or engine edits by this worker; every change above is a **recommendation** to lead/the owning module author.
- No byte patches to bypass faults (`0x245605d → mov eax,4; ret`, `0x23335c0`/`0x2358475`/`0x23335a0` xor/ret) may be used as gate evidence — decision 014/017 patches are diagnostics only.
- Do not zero or replace `__stack_chk_guard` (`0x72293f8`), do not widen `sr_is_safe_ptr` to accept the sentinel, do not patch guest canary checks.
- `0x31a1360`'s `rax=0xabcdef0123456789` is the frame canary, **not** an object field; `sentinel-abcdef-trace.md §4` found no carrier field, and `sr_is_safe_ptr`/`javavm_semantics` already reject that value. The real blockers there are NULL `+0x488` and the unbound TSD GOT (steps #2/#10), not the sentinel.
- `0x231f33d`/`0x231f34b` are type-defining but **not app-shell reachable** (hit counter 0); the consumed object comes from singleton getter `0x231f294`. Do not treat allocation/zeroing of `+0x68` as the fix (DECISION C, `dispatch-map.md §9`).
- Static analysis synthesis only; no runtime capture of the notifier being entered was performed here. Steps #9–#11 are a predicted ordering from observed registration and log-string semantics; exact strings and `StartGameParams` getter object must be supplied by the harness/lead and verified live.
- **No success is claimed.** Until a real gate callback plus phase-3 render is observed with `seeded_state==0`, `app-shell.sh` remaining red (`callback=0 render=1`) is the correct outcome.
