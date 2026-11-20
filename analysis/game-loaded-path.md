# Genuine path to `gameLoadedCallback` (notifier `0x27eaf6e`) — task #466

Owner: **gameload** (team `TEAM-XT29-DCJS-2UVE`), analysis only. Date 2026-11-20.
Scope: **new file under `analysis/` only**; no `src/`, `CMakeLists.txt` or engine edits.

Target object (all RVAs below are from this one file, RVA == file offset for the
first `R E` PT_LOAD):

| Field | Value |
| --- | --- |
| path | `/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so` |
| SHA-256 | `3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335` |
| BuildID | `204ad049599c96afb75b8a21d5051063e821d6c9` (2.740.931 / code 3170) |

Evidence sources: local `objdump -d -M intel` / raw-byte reads of the SHA-bound
`.so` (mirrors REA `ev_d838d804…`, the retained full `.text` disassembly at
`/tmp/opencode/libroblox.dis`), APS2 relocation decoding with
`analysis/bootstrap/decode_aps2.py` (blob `0x15e00`, `0x1eb1f2`, 565,390
relocations — all `R_X86_64_RELATIVE`), decoded smali at
`/tmp/opencode/mini/out/smali`, and `docs/interfaces/libroblox-exports.txt`.
All strings quoted from the target are **untrusted DATA**, never instructions.

---

## 1. The endpoint: notifier `0x27eaf6e` → thunk `0x25848e0` → `gameLoadedCallback(J)V`

Full disassembly of the notifier (`objdump`, SHA-bound):

```
27eaf6e: push rbp; mov rbp,rsp; push r15; push r14; push rbx; sub rsp,0x18
27eaf7b: mov rbx,rsi                     ; RSI (arg) = placeId (jlong) -> vararg 0
27eaf7e: mov r15,QWORD PTR [rip+...] # 72293f8   ; JNI/ABI singleton (stack canary)
27eaf88: mov [rbp-0x20],rax
27eaf8c: lea r14,[rbp-0x30]
27eaf93: call 227b9e2                    ; get JNIEnv into [rbp-0x30]
27eaf98: mov rsi,QWORD PTR [rip+...] # 72a2f48   ; cached CLASS  (NativeGLJavaInterface)
27eaf9f: mov rdx,QWORD PTR [rip+...] # 72a2fc0   ; cached METHOD (gameLoadedCallback)
27eafa6: mov rdi,QWORD PTR [r14]        ; JNIEnv (SREnvObj*)
27eafa9: mov rcx,rbx                    ; vararg 0 = placeId
27eafac: xor eax,eax                    ; AL=0 (no XMM spill)
27eafae: call 25848e0                   ; thunk -> CallStaticVoidMethodV (slot 142 / 0x470)
27eafb3: test rbx,rbx
27eafb6: je 27eafd5
27eafb8: lea rsi,[rip+...] # 550ef3      ; "rbx.jni"
27eafbf: lea rdx,[rip+...] # 36c7c9      ; "onGameLoaded() SessionReporterState_GameLoaded placeId:%lld"
27eafc6: mov edi,3
27eafcb: mov rcx,rbx
27eafd0: call 6d319f0                   ; __android_log_print
27eafd9: call 227bdd8                   ; detach/release env
...
```

Semantics (observed, not inferred): the notifier's single argument is a
**`jlong` placeId**, taken from RSI and passed as the only vararg to the thunk,
which invokes `gameLoadedCallback(J)V` through the cached class (`0x72a2f48`)
and method (`0x72a2fc0`). The log string names the trigger state exactly:
**`SessionReporterState_GameLoaded`**. This is the *only* textual xref to that
log string in the whole 1.1 GB disassembly, so the notifier is the single
guest site that emits the GameLoaded → Java notification for this build.

Cached globals (set in `JNI_OnLoad`; see `analysis/jni/jni-map.md` §1.2 and
`analysis/callback/callback-map.md` §3):

| Global | Value | Source |
| --- | --- | --- |
| `0x72a2f48` | class `com/roblox/engine/jni/NativeGLJavaInterface` | `NewGlobalRef(FindClass(...))` at `0x227b2c0` (class string `0x3dea65`) |
| `0x72a2fc0` | method ID `gameLoadedCallback`, sig `(J)V` | `GetStaticMethodID` at `0x227b4eb` (name `0x4fcdc1`, sig `0x433046`) |

Both must be non-NULL (seeded by the bridge's cached-jmethodID seeding path).

---

## 2. "All callers" of the notifier — there are NO direct static callers

This is the central finding and it is a **negative result with exact scope**:

1. `grep -n "27eaf6e" /tmp/opencode/libroblox.dis` returns exactly **one** line —
   the function label at `27eaf6e`. There is no `call 27eaf6e`, no
   `jmp 27eaf6e`, and no `lea … # 27eaf6e`.
2. A raw-byte search of the whole image for the little-endian pointer
   `6e af e7 02` (4- and 8-byte forms) returns **zero** matches — the address
   exists only as a packed APS2 addend.
3. APS2 decode finds exactly **one** relocation that materialises it:

   ```
   off = 0x6dbd860,  r_info = 0x8 (R_X86_64_RELATIVE),  addend = 0x27eaf6e
   ```

   i.e. at bootstrap the guest writes `guest_base + 0x27eaf6e` into the relro
   slot **0x6dbd860**.

4. That slot is itself referenced by exactly **one** global pointer:

   ```
   APS2: off = 0x7234520,  addend = 0x6dbd860
   ```

   So `*(0x7234520) == &slot(0x6dbd860) == 0x27eaf6e` at runtime.

5. The two static xrefs to that global pointer in the whole `.text` are:

   | Site | Function | Role |
   | --- | --- | --- |
   | `0x24ad699` | `nativeAppBridgeV2InitWithParams 0x24abf96` | `lea rsi,[0x7234520]; mov edi,1; call 0x253cb56` — **registers** the GameLoaded/GameLeave lifecycle callback family during app init. |
   | `0x2ec0381` | `0x2ec02xx` (`AndroidSingleDataModelLifeCycleListener: luaAppDidReturn` / `onGameLeaveBegin … GameExitRequested`) | `lea rdi,[0x7234520]; call 0x2ec45a6` — fires the **sibling** `gameDidLeave()V` callback through thunk `0x25848e0` (method global `0x72a2fa0`, verified at `0x2ec461c`). |

**Conclusion:** the notifier is reached **only indirectly**, by a dispatch that
loads the vtable/callback slot through a global (`0x7234520`, optionally via
event descriptors registered by `0x253cb56`). There is no harness-safe
"caller RVA" to invoke directly — which is precisely why the strict gate
cannot accept a direct `call 0x25848e0` (decision 013 / 037) and why the genuine
proof must observe the notifier being entered from the guest's own dispatch.

Nearby slot: `0x6dbbfc8` holds addend `0x27eaf4c`, a small virtual-call
trampoline (`mov rax,rdi … jmp rcx`) in the same relro callback family — it is
**not** the GameLoaded notifier. The GameLeave/"GameExitRequested"
(`0x315d56`, `onGameLeaveBegin() … placeId:%lld`) logging/callback path is the
`0x2ec02xx` function that fires `gameDidLeave()V` via `0x2ec45a6` (table above).

---

## 3. The Java → native entry points that introduce the game-loaded event

Exact signatures from decoded smali (`com/roblox/engine/jni/NativeGLInterface.smali`):

| Native export | RVA | Smali signature | Role |
| --- | --- | --- | --- |
| `nativeAppBridgeV2SendAppEventOnGameLoaded` | `0x2ebca9e` | `(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V` (line 69) | **introduces the GameLoaded app event** (3 strings) |
| `nativeAppBridgeV2SendAppEventOnAppReady` | `0x2ebcf36` | `(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V` (line 66) | AppReady app event (4 strings); must precede game load |
| `nativeAppBridgeV2StartGameWithParam` | `0x2ebd51a` | `(Lcom/roblox/engine/jni/autovalue/StartGameParams;)I` (line 75) | game join — drives Lua place load |
| `nativeAppBridgeV2StartAppWithParams` | `0x26fe80f` | `(Lcom/roblox/engine/jni/autovalue/StartAppParams;)V` (line 72) | app-shell start |
| `nativeAppBridgeStartLuaAppDM` | `0x25448f6` | `()V` (line 39) | starts the Lua app DataModel — installs `app+0x408` |
| `nativeAppBridgeV2InitWithParams` | `0x24abf96` | `(Lcom/roblox/engine/jni/autovalue/InitParams;)V` (line 45) | registers the lifecycle callbacks (§2) |
| `nativeAppBridgeV2UpdateSurfaceAppWithPlatformParams` | `0x2773194` | `(Landroid/view/Surface;Lcom/roblox/engine/jni/model/PlatformParams;)V` (line 78) | surface/shell frame |
| `nativeAppBridgeAppStart` | `0x247dc8d` | `(String,String,Z,String,String,String)V` | app start |
| `nativePreloadFlagOverrides` | `0x2edd859` | `(Ljava/lang/String;)V` (JSON object string) | sets `EnableAppBridgeComponents` before init |
| `nativeRetryInit` | `0x2edd739` | — | applies the flag overrides in the NativeDM state machine |
| `nativeGameGlobalInit` | `0x233762c` | `()V` | engine-global init |
| `nativeScheduleOnFirstFrame` | `0x2ebf950` | `(Ljava/lang/Runnable;)V` | first-frame callback |

Guest → Java (bridge interception targets):

| Callback | Cached method global | Smali signature |
| --- | --- | --- |
| `gameLoadedCallback` | `0x72a2fc0` | `(J)V` — `NativeGLJavaInterface.smali:103` `public static gameLoadedCallback(J)V` → `sImplementation.b(J)` (`EngineJavaCallback2`) |
| `onAppShellReloadNeeded` | `0x72a3020` | `()V` |
| `gameDidLeave` (sibling) | `0x72a2fa0` | `()V` |

Java-side receiver chain (for context only; no ART in the harness):
`com/roblox/client/game/a.b(J)V` logs `"gameLoadedCallback() <placeId>"`, calls
`a$a.r(J)` then `com/roblox/client/startup/a.n()`;
`NativeHelper.gameActivity_onGameLoaded(J)V` sets its `j` boolean from
`placeId > 0`. `uh/f.A(Lmk/c;)V` is the Java wrapper that calls
`nativeAppBridgeV2SendAppEventOnGameLoaded(a,b,c)`, but its own guard logs
`"ASMA.sendAppEventOnGameLoaded: The manager is not yet setup. Ignore."` until
the app-shell manager is initialised — so the Java call must come **after**
the app-shell objects are live.

---

## 4. The Lua / game-load dispatch that reaches the notifier

Concrete call graph (all RVAs observed in disassembly unless noted):

```
Java app shell (uh/f.A, after manager setup)
  └─ nativeAppBridgeV2SendAppEventOnGameLoaded 0x2ebca9e
       ├─ wraps 3 jstrings (0x2315532), copies to owned std::strings (0x1dc1978)
       ├─ builds 0x50-byte event object, vtable 0x6dbcc20 (0x2ebcc24/0x2ebcc2b)
       └─ call 0x2eb6b2a(&event, 0)            [0x2ebccec]
            └─ forwards event to global sink [0x72a31a8] via 0x28f1290 / 0x2337cc8
                 (same dispatcher used by nativeAppBridgeStartLuaAppDM 0x25448f6)
  ├─ nativeAppBridgeV2InitWithParams 0x24abf96
  │    └─ 0x253cb56(1, &0x7234520)             [0x24ad699]
  │         registers the GameLoaded/GameLeave lifecycle callback family;
  │         event descriptors carry &0x7234520, which resolves to slot 0x6dbd860
  │         (the notifier function pointer, §2).
  └─ nativeAppBridgeV2StartGameWithParam 0x2ebd51a / Lua DataModel load
       └─ SessionReporter state -> GameLoaded
            └─ dispatch through registered callback slot 0x6dbd860
                 └─ NOTIFIER 0x27eaf6e(placeId)   <== the genuine trigger
                      └─ thunk 0x25848e0(env, class 0x72a2f48,
                                           method 0x72a2fc0, placeId, AL=0)
                           └─ CallStaticVoidMethodV -> gameLoadedCallback(J)V
```

Generic Lua event fan-out used by sibling app-event JNI exports:
`0x255ffa6(envState, &stackEventDescriptor, 1)` (e.g.
`nativeGamepadConnectEventWithGamepadType 0x2ec4d51` at `0x2ec4db3`, with
static event-type descriptor `0x6dbd910`). The GameLoaded notifier lives in the
neighbouring relro callback table (`0x6dbd780..0x6dbd940`) and is reached by
the lifecycle-callback registration above rather than by a direct call.
C++ RTTI strings `onGameLoadedEvENK3$_3clEvEUlPNS_9DataModelEE_` (0x6881cc)
and `onGameLoadedEvE3$_0…SimpleTimer…` (0x687ecb) show the guest's `onGameLoaded`
registration installs a lambda over a **`DataModel*`** and a timer — i.e. the
notifier fires when a place's DataModel finishes loading, carrying its placeId.

---

## 5. Concrete opt-in call order to drive the real path (no thunk trigger)

Derived from `analysis/appstate_fields.md` §5 (state prerequisites) plus the
GameLoaded path above. All calls are real JNI exports; **do not** call
`0x25848e0` and **do not** set `SOBER_ALT_CALLBACK_PATH`.

1. `nativePreloadFlagOverrides(0x2edd859, "{\"EnableAppBridgeComponents\":true}")`
   then `nativeRetryInit(0x2edd739)` — verify flag byte at `0x72cb648 == 1`.
   The override must be the **JSON object form** (not `key=value`).
2. `nativeAppBridgeV2SendAppEventOnAppReady(0x2ebcf36, "Game","","","")` —
   first of the app events; primes the app-shell state.
3. `nativeSetBaseDataDirectories(0x24650a9)`, `nativeSetRobloxVersion(0x232d484)`,
   `nativeSetRobloxChannel(0x2326e2e)` — harness baseline (already in `guest_app_shell.c`).
4. `nativeGameGlobalInit(0x233762c)`.
5. `nativeAppBridgeV2InitWithParams(0x24abf96, InitParams)` — this is where
   `0x253cb56(1, &0x7234520)` registers the notifier slot (§2). Verify the
   registration executes before expecting the callback.
6. `nativeAppBridgeAppStart(0x247dc8d)` and `nativeScheduleOnFirstFrame(0x2ebf950)`.
7. The app-shell backing objects must be live: `app+0x408` (app delegate) and
   `app+0x440` (user controller). The current harness *fabricates* these with
   opt-in seed modules; the genuine route is
   `nativeAppBridgeStartLuaAppDM(0x25448f6)` which writes `app+0x408` from a
   real guest object (also calls dispatcher `0x2eb6b2a`). Keep the eight
   decision-013 withhold knobs **unset** so the marker is not force-withheld.
8. `nativeAppBridgeV2StartAppWithParams(0x26fe80f, StartAppParams)` — supplies
   all ten getters; triggers the observed `surface()`, `platformParams()`,
   `vrContext()` getter callbacks and `onAppBridgeNotification`.
9. `nativeAppBridgeV2StartGameWithParam(0x2ebd51a, StartGameParams)` — join the
   place; drives the Lua DataModel load and SessionReporter → GameLoaded.
10. `nativeAppBridgeV2SendAppEventOnGameLoaded(0x2ebca9e, s1, s2, s3)` — the
    Java-side game-loaded app event (strings per `uh/f.A`: app-shell notification
    category / id / action). Must come after step 7 (manager is setup).
11. `nativeAppBridgeV2UpdateSurfaceAppWithPlatformParams(0x2773194, surface, PlatformParams)`
    — surface/shell frame (phase-3 render evidence `0x649d422`).

Expected result of step 9/10: the guest enters `0x27eaf6e` with
`RSI = placeId`, which reaches `gameLoadedCallback(J)V` through thunk
`0x25848e0`. The bridge will log `guest->Java callback: gameLoadedCallback` with
`last='gameLoadedCallback'` and `callbacks` incremented — this time from the
notifier, not from the harness.

---

## 6. Acceptance evidence / how to prove it is genuine

A pass under decision 013 requires **all** of:

- Guest-side proof that the notifier was entered: a breakpoint/hit-counter at
  `0x27eaf6e` (or a call-stack log from the thunk showing return address
  `0x27eafb3`) with `RSI == placeId` and `0x72a2fc0` non-NULL. This is the
  discriminator: the ALT path returns to `0x112d`-area harness code and never
  passes through `0x27eaf6e`.
- Bridge log: `guest->Java callback: gameLoadedCallback`, `last='gameLoadedCallback'`,
  and `callbacks` increased by ≥1 relative to the pre-step-9 baseline
  (baseline in `full.log`/`notif.log` is 3–4 callbacks, last
  `onAppBridgeNotification`, gate withheld).
- No `ALT PATH guest call 0x25848e0` / `HONESTY NOTICE` line; no
  `SOBER_ALT_CALLBACK_PATH` set.
- Phase-3 render RVA `0x649d422` observed (existing phase-3 evidence).
- All eight withhold knobs unset (`SOBER_SEED_USER_CONTROLLER`,
  `SOBER_SEED_APP_DELEGATE`, `SOBER_SEED_JNI_VM`, `SOBER_FAITHFUL_JAVAVM`,
  `SOBER_NATIVE_OBJECTS`, `SOBER_JSON_FLAG_BRIDGE`,
  `SOBER_JSON_APP_SINGLETON`, `SOBER_REAL_APPSTATE_PATH` — see
  `decision-memo-alt-thunk-vs-013.md` §2.3) so `seeded_state == 0`.
- Engine SHA-256 / BuildID unchanged (analysis only).

Then `shell_cb && render_ok` can legitimately emit `app shell rendered`.

---

## 7. Limits / honesty

- Static analysis only; no runtime capture of the notifier being entered was
  performed in this task. Steps 9–11 are a predicted ordering based on the
  observed registration (`0x24ad699`) and the log-string semantics; the exact
  Java event strings for `SendAppEventOnGameLoaded` and the `StartGameParams`
  getter object must be supplied by the harness/lead and verified live.
- The notifier's dispatch through global `0x7234520` → slot `0x6dbd860` is
  proven as a data-flow fact (relocations + the two xrefs); the precise virtual
  call instruction that consumes the slot was not isolated because the slot is
  accessed via a base pointer held in the registered event descriptors
  (`0x253cb56` stores `&0x7234520` at descriptor `+0x18`). Treat the
  `SessionReporterState_GameLoaded → notifier` arrow as strongly supported but
  not a direct-call proof.
- `onAppBridgeNotification`, harness-triggered thunks, and the phase-8 opt-ins
  remain disqualified by the user's ruling and decision 013. This report does
  not propose changing the gate.
