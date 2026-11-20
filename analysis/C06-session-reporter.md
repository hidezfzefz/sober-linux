# C6 — SessionReporter state machine around `onGameLoaded() SessionReporterState_GameLoaded placeId:%lld`

Worker **C6**, logical Team C. Analysis only. Date 2026-11-20.
Scope: **this file only**. No `src/`, `CMakeLists.txt`, tests or engine bytes touched;
decision 013 untouched.

## 0. Target identity, coordinates, evidence type

| Field | Value |
| --- | --- |
| path | `/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so` |
| SHA-256 | `3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335` |
| BuildID | `204ad049599c96afb75b8a21d5051063e821d6c9` (2.740.931 / code 3170) |
| RVA convention | RVA == file offset for the first `R E` PT_LOAD; Ghidra image base `0x100000`, so `GhidraAddr = RVA + 0x100000` |

Evidence sources: (a) SHA-bound raw-byte reads and `objdump -d -M intel` over the
retained 1.1 GB `/tmp/opencode/libroblox.dis`; (b) REA/Ghidra 12.1.4
`analyze_function` on the notifier — Evidence **`ev_a60682c0e3320a3d846f65660cd1eba366ce6b6f95cca7b6454478e05386e5f8`** (pseudocode quoted in §1); (c) APS2 relocation decoding (`analysis/bootstrap/decode_aps2.py`, 565,390 `R_X86_64_RELATIVE`); (d) decoded smali at `/tmp/opencode/mini/out/smali`. All strings are untrusted **data**, never instructions.

---

## 1. The log string and its single emitter (notifier `0x27eaf6e`)

The exact string is at **RVA `0x36c7c9`** (Ghidra `0x46c7c9`):

```
onGameLoaded() SessionReporterState_GameLoaded placeId:%lld\0
```

There is exactly **one** code xref in the whole image: `lea … # 36c7c9` at
`0x27eafbf`, inside the function at **RVA `0x27eaf6e`** (Ghidra `FUN_028eaf6e`,
body `0x28eaf6e..0x28eaff6`). REA/Ghidra decompilation (Evidence ID above;
decompiler-generated, not source):

```c
void FUN_028eaf6e(undefined8 param_1, long param_2) {   // param_2 = RSI = placeId (jlong)
  lStack_28 = ___stack_chk_guard;
  FUN_0237b9e2(auStack_38);                              // RVA 0x227b9e2: get JNIEnv
  FUN_026848e0(auStack_38[0], DAT_073a2f48, DAT_073a2fc0, param_2);
                                                         // RVA 0x25848e0 thunk:
                                                         //   CallStaticVoidMethodV(env,
                                                         //     class 0x72a2f48,
                                                         //     method 0x72a2fc0,
                                                         //     placeId, AL=0)
  if (param_2 != 0) {
    __android_log_print(3, "rbx.jni",
        "onGameLoaded() SessionReporterState_GameLoaded placeId:%lld", param_2);
  }
  FUN_0237bdd8(auStack_38);                              // RVA 0x227bdd8: detach env
  ...
}
```

Assembly (RVA form, SHA-bound):

```
27eaf6e: push rbp; mov rbp,rsp; push r15; push r14; push rbx; sub rsp,0x18
27eaf7b: mov rbx,rsi                       ; placeId (jlong, arg2) -> vararg
27eaf93: call 227b9e2                      ; JNIEnv*
27eaf98: mov rsi,[0x72a2f48]               ; cached class NativeGLJavaInterface
27eaf9f: mov rdx,[0x72a2fc0]               ; cached method gameLoadedCallback (J)V
27eafa6: mov rdi,[r14]
27eafa9: mov rcx,rbx                       ; only vararg = placeId
27eafac: xor eax,eax                       ; AL=0 (no XMM spill)
27eafae: call 25848e0                      ; thunk -> CallStaticVoidMethodV (slot 142/0x470)
27eafb3: test rbx,rbx
27eafb6: je 27eafd5
27eafb8: lea rsi,[550ef3]                  ; "rbx.jni"
27eafbf: lea rdx,[36c7c9]                  ; the SessionReporterState_GameLoaded string
27eafd0: call 6d319f0                      ; __android_log_print@plt
27eafd9: call 227bdd8                      ; detach env
```

Cached JNI globals (all seeded in `JNI_OnLoad`; cross-check `analysis/jni/jni-map.md`):

| Global | Meaning | RVA of seeding site |
| --- | --- | --- |
| `0x72a2f48` | `NewGlobalRef(FindClass("com/roblox/engine/jni/NativeGLJavaInterface"))`, class string `0x3dea65` | `0x227b2c0` (xrefs `0x227b19b/0x227b28c/0x227b2a3`) |
| `0x72a2fc0` | `GetStaticMethodID(..., "gameLoadedCallback", "(J)V")`; name `0x4fcdc1`, sig `0x433046` | `0x227b4eb` (name xref `0x227b4d4`) |
| `0x72a2fa0` | sibling `gameDidLeave()V` methodID, used by `0x2ec45a6` | — |

**Conclusion (observed):** the notifier is a `void (jlong placeId)` function whose
only side effects are (1) one JNI static call `gameLoadedCallback(J)V` and (2) an
`__android_log_print` that *names the triggering state* `SessionReporterState_GameLoaded`.
The log is emitted **after** the JNI call and **only when `placeId != 0`**.

**Smali receiver chain** (all decoded from the APK, signatures exact):

| Java method | Signature / source |
| --- | --- |
| `NativeGLJavaInterface.gameLoadedCallback(J)V` | `NativeGLJavaInterface.smali:103` → `sImplementation.b(J)` on `EngineJavaCallback2` |
| `EngineJavaCallback2.b(J)` → `com/roblox/client/game/a.b(J)` | `game/a.smali:80`: logs `"gameLoadedCallback() "`, calls `a$a.r(J)`, then `startup/a.n()` |
| `NativeHelper.gameActivity_onGameLoaded(J)V` | `NativeHelper.smali:2829`: logs `"gameActivity_onGameLoaded: placeId = "`, sets field `j = (placeId > 0)` |
| `SessionReporterJavaInterface` | `com/roblox/engine/jni/reporter/SessionReporterJavaInterface.smali`; static `sendSessionReport(String,String)`, `setEventTrackingGoogleAnalytics(String,String,String,J)`, `setImplementation(Lpk/a;)V` |

---

## 2. The SessionReporter state machine: two states, two emitter families

Only **two** `SessionReporterState_*` strings exist in the image, and they are the
two ends of the game-session lifecycle:

| State string | RVA | Emitter(s) | JNI fired |
| --- | --- | --- | --- |
| `onGameLoaded() SessionReporterState_GameLoaded placeId:%lld` | `0x36c7c9` | notifier `0x27eaf6e` (this task) | `gameLoadedCallback(J)V` via thunk `0x25848e0` |
| `onGameLeaveBegin() SessionReporterState_GameExitRequested placeId:%lld` | `0x315d56` | `0x2ec01xx`, `0x2ec0260`, `0x2ec32e7` (xrefs `# 315d56`) | `gameDidLeave()V` via `0x2ec45a6` → thunk `0x25848e0`, method `0x72a2fa0` |

Both families share the same shape — `void (jlong placeId)`, log under tag
`"rbx.jni"` at level 3, placeId guarded nonzero — so "SessionReporter state" in
this build is implemented as a pair of placeId-carrying notifier callbacks, not
as a named C++ `SessionReporter` symbol (the image has no `SessionReporter`
RTTI/export; the only `SessionReporter` strings are the Java interface class at
`0x453445` and the two log strings).

### 2.1 The actual GameLoaded transition site: `dataModelBindings_onGameLoaded` (`0x2ee4838`)

The real guest code that reacts to a place finishing load is at **RVA `0x2ee4838`**;
its only caller is `0x2ee0a86`, which logs
`"[FLog::DataModelBindings] onGameLoaded: placeId:{}."` (string `0x56d327`):

```
2ee0a62: lea rcx,[56d327]        ; "[FLog::DataModelBindings] onGameLoaded: placeId:{}."
2ee0a7a: call 232de10            ; FLog logger
2ee0a7f: mov rdi,[r14+0x10]      ; DataModelBindings-owned placeId
2ee0a83: mov rsi,rbx             ; placeId (jlong) -> arg2
2ee0a86: call 2ee4838            ; dataModelBindings_onGameLoaded(placeId)
```

```
2ee4838: push rbp; ... mov rsi,r14        ; r14 = placeId (arg2)
2ee4896: lea rcx,[4357a3]        ; "[FLog::NativeDM] dataModelBindings_onGameLoaded: placeId = {}."
2ee48ae: call 232de10            ; FLog logger
2ee48b3: mov rax,[rbx+0x40]; mov rax,[rax+0x18]; mov rdi,[rax+0x10]
2ee48bf: mov rsi,r14             ; placeId
2ee48c2: call 2eeb32e            ; -> Java NativeHelper.gameActivity_onGameLoaded(J)V
2ee48ca: call 2ee3fa2            ; follow-on native call (logs
                                 ;   "[FLog::NativeDM] nativeActivity_onSurfaceChanged: state:{}"
                                 ;   at 0x2df218; exact role not isolated here)
```

`0x2eeb32e` is the helper that calls back into Java: it does
`GetMethodID(class 0x72a3b88, "gameActivity_onGameLoaded" @0x453577, "(J)V" @0x433046)`
then invokes it with the placeId. Class `0x72a3b88` is proven to be
`com/roblox/client/startup/NativeHelper` — it is cached from
`getNativeHelper()Lcom/roblox/client/startup/NativeHelper;`
(name string `0x4c36e7`, sig `0x26e22c`, cached at `0x2eea76f`), and every method
looked up on it is a `gameActivity_on*` callback.

Supporting RTTI (untrusted data, consistent with the flow):
`ZN3RBX16NativeHelper_JNI25gameActivity_onGameLoadedEP12GameActivitylE...` (`0x6145e3`),
`ZN3RBX16PlayerConfigurer12onGameLoadedEvE3$_0…SimpleTimer…` / `…DataModelEEEE`
(`0x687ecb`, `0x6880c4`, `0x6881cc`) — the guest's onGameLoaded installs
`DataModel*`/timer lambdas, i.e. the transition fires when a place's DataModel
finishes loading.

### 2.2 How the notifier is reached — indirect, via a registered slot

`0x27eaf6e` has **no direct static caller** (`grep 27eaf6e` → one hit, its label;
no raw pointer; Ghidra dossier callers `[]`). It is materialised only as an APS2
addend:

```
APS2: off 0x6dbd860, addend 0x27eaf6e    -> relro slot 0x6dbd860 = &notifier
APS2: off 0x7234520, addend 0x6dbd860    -> global ptr 0x7234520 = &slot(0x6dbd860)
```

`nativeAppBridgeV2InitWithParams` (**RVA `0x24abf96`**) registers that callback
family at `0x24ad699`:

```
24ad699: lea rsi,[7234520]
24ad6a0: mov edi,1
24ad6a5: call 253cb56            ; register event descriptors carrying &0x7234520
```

`0x253cb56` builds descriptors `{vtbl 0x6dbba28, eventId ∈ {0x1,0x9,0x31,0x39,0x41,0x49,0x51,0x59,0x61,0x69,0x71,0x79,0x81,0x89,0x91}, …, family=&0x7234520 @+0x18, self @+0x20}` and installs them into the app-bridge event queues (`0x253d37c`, `0x253d50c`, …). The sibling `gameDidLeave` family is fired through the same global at `0x2ec0381 → 0x2ec45a6` (method `0x72a2fa0`, verified at `0x2ec461c`). The virtual call that consumes slot `0x6dbd860` was **not** isolated (the descriptor stores `&0x7234520` at `+0x18`; the event dispatcher dereferences it), so the arrow `dataModelBindings_onGameLoaded → notifier 0x27eaf6e` is strongly supported but is a data-flow proof, not a direct-call proof.

---

## 3. Which native JNI call moves SessionReporter to GameLoaded, and where placeId comes from

**Answer (concrete, with uncertainty noted):**

* The Java→native JNI call that starts the transition is
  **`nativeAppBridgeV2StartGameWithParam` — RVA `0x2ebd51a`**,
  `(Lcom/roblox/engine/jni/autovalue/StartGameParams;)I`
  (`NativeGLInterface.smali:75`). It supplies the place, reads
  `StartGameParams.placeId()J`, and drives the join/DataModel load. The state
  flips to GameLoaded *inside the guest* when that place's DataModel finishes
  loading (`dataModelBindings_onGameLoaded`, `0x2ee4838`), not at the JNI boundary
  itself — so `0x2ebd51a` is the necessary trigger, not the literal write of the
  state enum.
* The companion Java-side announcement
  **`nativeAppBridgeV2SendAppEventOnGameLoaded` — RVA `0x2ebca9e`**,
  `(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V`
  (`NativeGLInterface.smali:69`) must follow. Its code wraps three jstrings via
  `0x2315532`, builds a `0x50`-byte event with vtable `0x6dbcc20`, and dispatches
  through `0x2eb6b2a(&event,0)` → global sink `0x72a31a8` (`0x28f1290` / `0x2337cc8`).
  Java caller: `uh/f.smali:179`. The RTTI
  `…nativeAppBridgeV2SendAppEventOnGameLoadedE3$_0…` (`0x60f029`) shows it installs
  a `std::function<void()>` lambda. **It does not carry a numeric placeId** — its
  arguments are strings, so it cannot be the source of `%lld`.

**placeId provenance (observed data flow):**

1. `StartGameParams.placeId()J` is a **primitive jlong getter**
   (`StartGameParams.smali:132`: `public abstract placeId()J`). This is a key
   fidelity point: the current harness `game_params_get()` in
   `src/linux/guest_app_shell.c:900` returns a **jstring** `"123456789"` for both
   `gameId` and `placeId`, which does not match `()J`.
2. `nativeAppBridgeV2StartGameWithParam` resolves that getter at `0x2ebd855`
   (name string `0x2a4e3f` = `"placeId"`), invokes it, and caches the returned
   jlong into global **`0x72a31c0`** at `0x2ebd86a`
   (`mov %rax,0x… # 72a31c0`). A second consumer of that cache is `0x2ec1370`.
3. The guest joins the place; when the place's DataModel finishes loading, the
   `DataModelBindings::onGameLoaded` path (`0x2ee0a86` → `0x2ee4838`) receives the
   placeId as a `jlong` in RSI, taken from the loaded DataModelBindings object
   (`[obj+0x10]`).
4. That same jlong is the argument to notifier `0x27eaf6e`
   (`27eaf7b: mov rbx,rsi`), which prints it with `%lld` and passes it unchanged
   as the sole vararg of `gameLoadedCallback(J)V` through thunk `0x25848e0`.

So `placeId:%lld` is **the 64-bit place ID of the place joined by StartGameParams**,
read back from the loaded DataModel; it is *not* one of the three strings of the
app event, and it is *not* the `gameId` string. It must be nonzero for the log to
appear (notifier `test rbx,rbx; je …`).

---

## 4. Minimal opt-in real-guest sequence to reach `SessionReporterState_GameLoaded`

All entries are genuine guest JNI exports / guest code paths; none patches the
engine. Under decision 013 every opt-in below force-withholds the marker, so this
is a **diagnostic proof path**, not a gate pass.

**Prerequisites (already delivered as default-OFF opt-ins; see PROGRESS.md Phase 8/9):**

1. `SOBER_REPLAY_FFLAG_CTOR` → replay `0x2e62390` (FFlag registry ctor; guard `0x72a0a30`, global `0x72a0520`).
2. `SOBER_SEED_GLOBAL_STRING` (early, before JavaVM seeds).
3. `SOBER_SEED_JNI_VM` + `SOBER_FAITHFUL_JAVAVM` + Bionic TSD heap seed (`SOBER_STUB_TSD_SEED`, no scheduler neutralize) + `SOBER_BIONIC_SF_FULL` + `SOBER_NATIVE_OBJECTS`.
4. `SOBER_JSON_FLAG_BRIDGE` / `SOBER_JSON_APP_SINGLETON` / `SOBER_REAL_APPSTATE_PATH`; flag override JSON `{"EnableAppBridgeComponents":true}` applied by `nativeRetryInit` (`0x2edd739`) before `nativeGameGlobalInit` (`0x233762c`); verify `0x72cb648 == 1`.
5. `SOBER_CLIENT_SETTINGS_INIT` → `nativeInitClientSettings` (`0x2359277`, strings `"{}"`, `"LIVE"`, `"2.740.931"`) → flags-loaded latch `0x7cdd149`; otherwise TaskScheduler init hits the genuine int3 at `0x6caef66`.
6. `nativeAppBridgeV2InitWithParams` (`0x24abf96`) must run — this is where `0x253cb56(1,&0x7234520)` registers the notifier slot (`0x24ad699`). Without it, no notifier can fire.
7. `nativeAppBridgeStartLuaAppDM` (`0x25448f6`) to install `app+0x408`, and the live `app+0x440` controller (currently only via opt-in seeds — the unresolved fidelity gap).

**Game-loaded specific (current `SOBER_GAME_LOADED_PATH`, `guest_app_shell.c:1331`):**

8. Build a faithful `StartGameParams` (24 getters). Critical: `placeId()J`,
   `userId()J`, `conversationId()J`, `referredByPlayerId()J` must return **primitive jlongs**, not jstrings; `gameId`/`accessCode`/`gameJoinContext`/`linkCode`/… are strings.
9. Call **`nativeAppBridgeV2StartGameWithParam` (`0x2ebd51a`, `(StartGameParams)I`)** — this is the JNI call that starts the place load and moves SessionReporter toward GameLoaded.
10. Then call **`nativeAppBridgeV2SendAppEventOnGameLoaded` (`0x2ebca9e`, `(String,String,String)V`)** with the app-event strings from `uh/f.A` (e.g. `"AppShellNotifications"`, `"<id>"`, `"StartConversationWithUserId"`/`"StartConversationWithUserId"`, after the manager is live).

**Current blocker:** per PROGRESS.md Phase 9, `SOBER_GAME_LOADED_PATH` faults at guest `0x31a1360` on a bogus handle (`rax = TSD sentinel`) before reaching the notifier, with both the inline four-getter stub and the faithful 24-getter params. Closing it requires the Java-object/TSD fidelity work, not more direct thunk calls. Do **not** call `0x25848e0` or set `SOBER_ALT_CALLBACK_PATH`.

---

## 5. Decision-013 acceptance evidence (unchanged)

A genuine pass still needs *all* of:

- Guest-side hit at notifier `0x27eaf6e` with `RSI == placeId != 0` and method global `0x72a2fc0` non-NULL; thunk return address `0x27eafb3` (discriminates against the ALT path, which returns to harness code).
- Bridge log `guest->Java callback: gameLoadedCallback`, `last='gameLoadedCallback'`, `callbacks` increased over baseline — this time from the notifier.
- No `ALT PATH guest call 0x25848e0` / `HONESTY NOTICE`; `SOBER_ALT_CALLBACK_PATH` unset.
- Phase-3 render RVA `0x649d422` observed.
- All decision-013 withhold knobs unset (`seeded_state == 0`); engine SHA/BuildID unchanged.

---

## 6. Limits / honesty

- This is **static analysis only** (SHA-bound disassembly + raw-byte reads + one REA/Ghidra decompilation). No runtime capture of the notifier being entered was performed here; §4 steps 8–10 are a predicted ordering, and the `dataModelBindings_onGameLoaded → notifier` arrow is supported by relocation/slot data flow rather than an isolated direct call.
- Ghidra pseudocode is decompiler-generated, not original source; RVA↔Ghidra mapping is `+0x100000`. The REA session was later closed, so only the notifier dossier was retained (Evidence ID in §0).
- The claim "StartGameParams.placeId()J is the placeId source" is an inference from (i) the getter's `()J` signature, (ii) its cache store at `0x2ebd86a` into `0x72a31c0`, and (iii) the notifier's `%lld` argument being the DataModel's placeId; the exact re-load from `0x72a31c0` into the notifier argument was not followed instruction-by-instruction.
- `onAppBridgeNotification`, harness-triggered thunks and the phase-8 opt-ins remain disqualified by decision 013; this report proposes no gate change.
