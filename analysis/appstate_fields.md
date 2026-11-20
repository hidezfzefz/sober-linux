# Provenance of `app+0x440` (user controller) and `app+0x408` (delegate) — task #450

Owner: **appstate**. Scope: `analysis/` only. No `src/` edits, no gate changes, no
fabricated callbacks. Engine: AppImage `libroblox.so`, BuildID `204ad049…`.

Evidence: local `objdump -d` / raw-string reads of the frozen engine
(`/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so`), APS2 relocation decode
(`/tmp/opencode/reloc.py`), decoded Dalvik (`/tmp/opencode/mini/out/smali`), and
the lead's live logs (`/tmp/opencode/seedctl*.log`, `default*.log`).

## 1. The real Java call order our harness is not reproducing

`uh/f.smali` is Roblox's app-shell manager (`ASMA`). Its restart/start path
`y(Context, Surface, Activity)` (lines 1317-1351) does, in order:

| # | Java call | Native RVA | Harness calls it? |
| - | - | - | - |
| 1 | `f() -> g() -> v()` = `nativeAppBridgeV2SendAppEventOnAppReady(String,String,String,String)` | `0x2ebcf36` | **NO** |
| 2 | `k(uh/f$d)` = `nativeAppBridgeV2InitWithParams(InitParams)` | `0x24abf96` | yes (synthetic object) |
| 3 | `G(Surface)` = `nativeAppBridgeV2StartAppWithParams(StartAppParams)` | `0x26fe80f` | opt-in (`SOBER_START_APP`), sparse object |
| 4 | `I(Surface,FII)` = `nativeAppBridgeV2UpdateSurfaceAppWithPlatformParams` | `0x2773194` | **NO** |

Separately, setup `F(Context)` calls `nativeGameGlobalInit` (`0x233762c`), and
`ActivityNativeMain.Q2(FeatureState)` calls
`nativeAppBridgeStartLuaAppDM()` (`0x25448f6`) — **also not called by us**.
`nativeAppBridgeAppStart` (`0x247dc8d`) is reached from the activity path, not
from `uh/f.y()`.

The Java param builders show exactly which Java-side values the guest reads:

* `InitParams` (`uh/f.k()`, lines 706-840): `platformParams`, `deviceParams`,
  `baseURL`, `userAgent`, `isTablet`, `pioneerPlaceId`, `buildVariant`,
  `isVrDevice`, `isPotato`.
* `StartAppParams` (`uh/f.G()`, lines 363-434): `surface`, `platformParams`,
  `appStarterPlace`, `appStarterScript`, `appUserId`, `isUnder13`, `username`,
  `membershipType`, `selectedTheme`, `vrContext`.

Our harness passes a zeroed 0x1000 blob with two A-handles at `+0/+0x8/+0x10/+0x18`
as the `InitParams` jobject, and a bare `StartAppParams` object whose only
registered getter handlers are `surface`, `platformParams`, `vrContext`
(`src/linux/guest_app_shell.c:899-901, 978-982`). The seven other StartAppParams
getters (`appStarterPlace`, `appStarterScript`, `appUserId`, `isUnder13`,
`username`, `membershipType`, `selectedTheme`) have **no handler**; the generic
`sr_dispatch_call` fallback (`src/jni/native_jni.c:498-513`) hands the guest a
generic `java/lang/String` instead of the real value.

How the guest reads these getters: helper `0x232dbbc` does
`GetMethodID(class, name, "()Ljava/lang/String;")` (`call *0x108`, global slot 33)
then `0x2315d18`/`0x2315532` (CallObjectMethod family), so the names above are
**method names**, not raw fields. Confirmed getter strings inside `InitWithParams`:
`0x4c3324` "baseURL", `0x2a4f42` "userAgent", `0x3897de` "buildVariant";
inside `StartGameWithParam`: "gameId", "accessCode", "username", "userId",
"isUnder13", "placeId" etc. (all read from the binary).

## 2. `app+0x408` (delegate) — who legitimately writes it

There are **four** writers, all genuine guest code:

1. **Inside `nativeAppBridgeV2InitWithParams` (0x24abf96), at `0x24aed3e`:**
   ```
   24aec0a: cmpq  $0x0,0x408(%rbx)      ; app+0x408
   24aec12: je    24aed0f               ; NULL -> construct delegate
   24aed0f: lea   0x6e047b0(%rip),%rax  ; REAL guest delegate vtable
   24aed27: call  2520892
   24aed3e: movups %xmm0,0x408(%rbx)   ; <-- install delegate
   ```
   Vtable `0x6e047b0` is a real guest class (APS2 slots → `0x1dd72a0`,
   `0x32197c6`, `0x2520892`, `0x2520c1c`). **No fake vtable is needed** — the
   guest builds this object itself. But this write is skipped when the
   `EnableAppBridgeComponents` flag is set (`24aed01: mov 0x72cb648,%cl;
   24aed09: jne 24aed99`). The alternative flag=1 branch writes a different
   object at `app+0x430` with vtable `0x6e48ab0`.

2. **`nativeAppBridgeStartLuaAppDM` (0x25448f6), at `0x25d7e75` and
   `0x25f7e7e`** — both install objects obtained from genuine guest helpers
   (`0x1dd55f2`, `0x25f7ff2`) into `app+0x408`. **This Java-visible entry point is
   never called by our harness.** This is the most credible source of the
   delegate on the flag-enabled path (see §3), and it matches
   `ActivityNativeMain.Q2(FeatureState)` on the real app.

3. `0x2626c94` — device-display handler swaps in a new object at `app+0x408`.

Live state: after our real `InitWithParams` and `AppStart`, `app+0x408` is still
`NULL` (`seedctl*.log`: `after InitWithParams: … +0x408=(nil) +0x440=(nil)`).
Either `InitWithParams` never reaches `0x24aed3e` with our synthetic params
object, or the flag-enabled branch is taken without `StartLuaAppDM` having run.

## 3. `app+0x440` (user controller) — the gated installer

* Confirmed by APS2 relocation: `0x6dbc7f8 -> 0x24ad50a`, i.e. `0x24ad50a` is
  **vtable slot +0x30 of class `0x6dbc7c8`** (vtable base `0x6dbc7c8`).
* `0x24ad531: cmpb $0x0,0x4e1e110(%rip) # 72cb648; je 24ad5f2`. The branch that
  installs the controller is the **flag-nonzero** branch: it reaches
  `24ad5a3: add $0xb0,%rbx; … 24ad5b6: call *0x10(%rcx)` — wrapper vtable `+0x10`
  = `0x24ae1fe`, called with `rdx = this+0xb0`. `0x24ae1fe` reads
  `app = wrapper+0x20` (`24ae226: mov 0x20(%rdi),%rbx`) and stores `rdx` into
  `app+0x440`.
* `this+0xb0` is a guest-owned shared_ptr built inside `InitWithParams` at
  `0x24ac5a9`/`0x24ac5c8` (copied from local `rbp-0x2f0`), via guest allocator
  `0x1dbab02` — again a real guest object, not a fake.
* **The flag is `EnableAppBridgeComponents`** — registered by a constructor that
  writes its default at `0x1e106f4` (name string `0x43650d` =
  `"EnableAppBridgeComponents"`). Live logs show **flag = 0** in our harness
  (`user-controller seed: flag72cb648=0 …`). With flag 0 the installer branch is
  skipped, which exactly explains the NULL at `app+0x440`.

The real Android app sets flags from Java through
`MainGameActivity.nativePreloadFlagOverrides(String)`
(RVA `0x2edd859`; smali `MainGameActivity.b2`, line 555, native declaration at
line 718). Our harness never calls that entry point. So the root cause is not a
missing fake vtable — it is that **we never deliver the Java-side flag overrides
(and never run the full Java call sequence), so the guest never enters the code
that creates both objects.**

## 4. Verdict on the four candidate entry points

| Candidate | Writes either field? | Verdict |
| - | - | - |
| `nativeAppBridgeV2UpdateSurfaceAppWithPlatformParams` `0x2773194` | no (383-byte leaf: reads surface + PlatformParams, dispatches) | needed for a real shell frame, but not a field source |
| `nativeAppBridgeV2SendAppEventOnAppReady` `0x2ebcf36` | no (builds a 0x58 event object, vtable `0x6dbcca0`, dispatches) | needed as the **first** step of the real order; may prime state |
| **`nativeAppBridgeV2StartGameWithParam` `0x2ebd51a`** | apparent hits are **stack locals** `-0x408(%rbp)`/`-0x440(%rbp)`, not app fields | game-join path (`uh/j0`), not the app-shell field source |
| **`nativeAppBridgeStartLuaAppDM` `0x25448f6`** | **YES — `0x25d7e75`, `0x25f7e7e` write `app+0x408`** | **missing entry point; call it** |

## 5. Concrete opt-in experiment (real guest init, no fake vtables, no gate touch)

Add one opt-in block, e.g. `SOBER_REAL_APPSTATE_PATH=1`, to
`guest_app_shell.c` that replays the real Java order with **properly shaped JNI
objects and handlers** (all calls are real guest entry points, no guest bytes are
patched):

1. `nativePreloadFlagOverrides(String)` `0x2edd859` with the real FFlag override
   string containing `EnableAppBridgeComponents=true`, so `0x72cb648` becomes 1
   through the guest's own flag machinery. (If the override-string grammar cannot
   be reproduced from the decoded APK, a clearly-labelled diagnostic-only second
   variant may set the byte at `0x72cb648` to 1 *before* `InitWithParams` — this
   is a flag seed, **not** a fake vtable and **not** a callback.)
2. `nativeAppBridgeV2SendAppEventOnAppReady` `0x2ebcf36(env, class, "Game","","","")`.
3. `nativeAppBridgeV2InitWithParams` `0x24abf96` with an `InitParams` object whose
   getters `baseURL`, `userAgent`, `platformParams`, `deviceParams`, `isTablet`,
   `pioneerPlaceId`, `buildVariant`, `isVrDevice`, `isPotato` are backed by
   `sober_jni_set_callback_handler` (extend `start_app_get`).
4. `nativeAppBridgeAppStart` `0x247dc8d` (already present).
5. `nativeAppBridgeV2StartAppWithParams` `0x26fe80f` with a `StartAppParams`
   object providing **all ten** getters listed in §1 (the seven currently missing
   ones are the key gap), with realistic values for `appUserId`, `username`,
   `membershipType`, `isUnder13`.
6. **`nativeAppBridgeStartLuaAppDM` `0x25448f6(env, class)`** — the missing Java
   entry point that installs `app+0x408` from guest objects.
7. `nativeAppBridgeV2UpdateSurfaceAppWithPlatformParams` `0x2773194` with a real
   `PlatformParams` carrying `dpiScale`/`viewportWidthMm`/`viewportHeightMm`.

Observe with the existing `SOBER_APP_SLOT_DIAG` logger (`log_app_slots`, lines
722-739) at each step. Success criterion for **this task**: both
`app+0x408` and `app+0x440` become non-NULL via the guest's own writes. Do **not**
emit `app shell rendered` from this experiment — the decision-013 gate stays as
is, and no callback is fabricated. If the two fields become live, the blocker for
the strict gate is reduced to driving `SingleSurfaceApp::userDidLogin`
(`0x2771f6c`) to the point where it genuinely invokes the app-shell callback.

---

## Addendum (follow-up task) — exact override-string grammar and AppReady args

### A. `nativePreloadFlagOverrides(String)` takes a **JSON object string**, not `key=value`

Java call chain (decoded smali):

1. `MainGameActivity.onCreate` line 1851 calls `L2()`.
2. `L2()` (lines 1149-1301) at lines 1283-1297 builds `new Luj/a(applicationContext)`,
   calls `h()`, and passes the result to `MainGameActivity$Companion.c(companion, str)`
   → `Companion.h` (lines 202-207) → `MainGameActivity.b2` (lines 552-558) →
   `nativePreloadFlagOverrides(String)` (native declaration at line 718).
3. `uj/a.h()` (lines 270-304) reads a file, `new JSONObject(text)`, returns
   `jsonObject.toString()`. The file comes from `uj/a.d()` (lines 156-267),
   path `exe/ClientSettings/ClientAppSettings.json` (string at line 263).
   (`uj/a.a()`, lines 23-93, shows the same JSON-object payload convention, with
   every value coerced via `getString`.)

So the argument is a JSON object literal as emitted by `org.json.JSONObject.toString()`:

```
{"EnableAppBridgeComponents":true}
```

Native confirmation:

* `0x2edd859` wraps the jstring (`call 0x2315532`, line 2edd87c) and stores it into
  the global std::string at `0x72a3b20`/`0x72a3b30` via `0x2ee549f`
  (`mov 0x10(%rbx),%rax; mov %rax,0x72a3b30`, `movups %xmm0,0x72a3b20`).
* `0x2ee4aaf` reads that global and calls `0x237dc30`, which invokes the JSON
  parser `0x236b98e` (`strlen` at 237dc60, parse at 237dc76), checks the parsed
  value is an object (`cmpl $0x0,0x58(%rax)` at 237dc82), then iterates members
  in the loop at `0x237de09` (member stride 0x20, advance at 0x237e006-0x237e01b).
* Each member is looked up by **key** with `call 0x2364466` (0x237df3f) and the
  value is passed to a setter via `call *%rbx` at `0x237df9c` (args: key ptr/len
  in rsi/rdx, value ptr/len in rcx/r8). Boolean JSON values are converted to the
  strings `"True"` / `"False"` — literal addresses `0x3a6b57` and `0x470a81`,
  loaded at `0x237de7e`/`0x237de85` — before being handed to the setter.
* The FFlag itself: name string `0x43650d` = `"EnableAppBridgeComponents"`,
  length `0x19` (25) stored at `0x1e106fb-0x1e10709`, value byte at `0x72cb648`.
  It is registered into the flag registry at `0x22e567a` with
  `rsi = 0x43650d`, `rdx = 0x72cb648`, `ecx = 1`, `r8d = 4` (flag type 4, bool),
  tail-call `0x227c18e`.

**Important caveat (why an earlier attempt saw no effect):** `0x2edd859` only
*stores* the string. Application happens later in the NativeDM init state machine
`0x2ee2766` (log strings `[FLog::NativeDM] initialize: state:{}. areFlagsLoaded:{}.`
at `0x332a03` and `ignoring call in state:{}` at `0x56d35b`), which is reachable
from `nativeRetryInit` `0x2edd739` (`call 0x2ee2766` at 0x2edd760) and from the
parser-builder path at `0x2edc6ca`/`0x2edce8c`. In the existing
`/tmp/opencode/realpath*.log` experiments a `nativePreloadFlagOverrides` call
left `flag 0x72cb648 = 0` (log line 3689). The opt-in experiment should therefore
call `0x2edd859` with the JSON string **before** `nativeGameGlobalInit`
(matching `onCreate` → `L2()`) and then force `nativeRetryInit` (`0x2edd739`) to
apply it; verify `0x72cb648` before continuing. `"EnableAppBridgeComponents=true"`
is **not** the accepted grammar; the JSON-object form above is.

### B. Realistic arguments for `nativeAppBridgeV2SendAppEventOnAppReady` (`0x2ebcf36`)

Java source: `uh/f.u(Vector<mk/b>)` (lines 1154-1234). For each pending event it
logs `a + ", " + b + ", " + c + ", " + d` (lines 1180-1218) and calls at line 1228:

```
nativeAppBridgeV2SendAppEventOnAppReady(mk_b.a, mk_b.b, mk_b.c, mk_b.d)
```

`mk/b` (`mk/b.smali`) extends `mk/c`; its only constructors in the decoded APK are
`ph/a.b(J)` and `ph/a.c(J)` (`ph/a.smali:43-61` and `:63-81`):

```
a = "AppShellNotifications"
b = <decimal id string, e.g. "123456789">
c = "StartConversationWithId"  (or "StartConversationWithUserId")
d = "Chat"
```

Native `0x2ebcf36` wraps all four strings via `0x2315532` (lines 2ebcf64-2ebcf97),
then parses the **4th** (`r9` → `rbx`) into an event-type integer by comparing its
length (4/5/12) and content against `Game` (`0x656d6147`, 2ebd007), `Games`
(2ebd00c), `Home` (`0x656d6f48`, 2ebd051), `Chat` (`0x74616843`, 2ebd059), `More`
(`0x65726f4d`, 2ebd063) and `AvatarEditor` (0x2ebd021/0x2ebd028). So `d` must be
one of those tokens; `""` (our current harness value) is invalid on the real path.

Concrete opt-in test value:

```
0x2ebcf36(env, class,
  "AppShellNotifications", "123456789", "StartConversationWithUserId", "Chat")
```

Use `"Game"` instead of `"Chat"` as the 4th argument if the goal is the game
event type rather than the chat notification path.
