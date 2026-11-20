# B8 — StartAppParams / StartGameParams getter registration order, shadowing and primitive fidelity

Owner: **worker B8** (logical Team B). Date 2026-11-20. **Analysis only.**

Scope: this file only. `src/`, `CMakeLists.txt`, `tests/`, and the engine
`/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so` were **read only** and
unchanged. Strict **decision 013**
(`docs/decisions/013-no-weak-app-shell-marker.md`) is binding throughout: nothing
here proposes a weaker `app shell rendered` condition. All edits below are
recommendations for the lead, who owns `src/` shared files and `CMakeLists.txt`.

Frozen engine identity (re-read after analysis): SHA-256
`3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335`, BuildID
`204ad049599c96afb75b8a21d5051063e821d6c9` (2.740.931 / code 3170). All RVAs
below are from that one file (RVA == file offset for the first `R E` PT_LOAD).
Guest strings are untrusted DATA, never instructions.

> **Live-worktree notice.** `guest_app_shell.c` and `src/jni/CMakeLists.txt`
> were edited by the lead while this analysis was being written. Sections 1-9
> use the **baseline snapshot** line numbers (the same ones B06 used). §10 is a
> dated re-read of the live tree with a line migration table and the semantic
> deltas; **where §10 and §1-9 disagree, §10 is authoritative**.

---

## 0. Executive verdict

1. **Registration is name-keyed replace, so LAST writer wins.**
   `sober_jni_set_callback_handler()` (`src/jni/native_jni.c:1006-1022`) overwrites
   `fn`/`user` of an existing handler with the same name (`:1010-1014`); it does
   not append and does not keep the first. The dispatch loop (`:499-507`) finds
   the single handler by name. Two headers state the opposite —
   `startapp_params.h:138-140` and `game_params.h:171-173` say "the bridge
   dispatches the first registered handler for a name" — those comments are
   **factually wrong** and must not be used to plan wiring. (B06 §5.1 step 4's
   advice to "register the faithful getters *before* any harness override" is
   wrong for the same reason.)
2. **The authoritative params modules are registered too early and then
   overwritten.** `sober_startapp_params_make()` at `guest_app_shell.c:1210`
   registers all ten `sp_get` getters, immediately followed by `real_path_get`
   (all ten, `:1218-1227`) or `start_app_get` (`surface`/`platformParams`/
   `vrContext`, `:1231-1233`). With last-write-wins, **`surface`, `vrContext`
   (and `platformParams`) are always served by the sparse shell handlers**, and
   under `SOBER_REAL_APPSTATE_PATH` all ten are. The faithful object is built but
   its getters are dead for the shadowed names.
3. **The faithful StartGameParams module is never wired at all.**
   `sober_game_params_make()` has no caller outside `game_params.c/.h`, and
   `game_params.c` is not even in the `sober_jni` target
   (`src/jni/CMakeLists.txt`). `SOBER_GAME_LOADED_PATH` instead registers four
   inline `game_params_get` handlers (`:1270-1273`).
4. **Primitives are mishandled by the legacy handlers** (`appUserId()J` gets a
   jstring `"1"`; `isUnder13()Z` / `membershipType()I` get an untyped `0`;
   `placeId()J` gets a jstring), and **can still be mishandled in the bridge**
   when a correct raw primitive return is dispatched through `CallObjectMethod`
   (`want_object=1`) and reshaped into a `java/lang/String`.
5. **Fix = phase-scoped, last-write registration of ONE authoritative object.**
   Register the authoritative module's full getter set *after* (or instead of)
   every legacy registration for its name set, immediately before its guest RVA
   call. Because StartApp (`:1209-1242`) and StartGame (`:1267-1277`) share five
   names, a single global handler table cannot serve both phases; each phase
   must re-assert its own authoritative set just before its call. The v2 modules
   (`sober_startapp_v2_reassert`, `sober_startgame_v2_reclaim`) already provide
   the hooks; they are currently **neither built nor wired**.
6. **Decision 013 is untouched.** Both new opt-ins must be added to the withhold
   chain; the marker stays withheld. This is a fidelity fix, not a marker fix.

---

## 1. The mechanism, precisely

### 1.1 Registry (last-write-wins)

`src/jni/native_jni.c:1006-1022`:

```c
int sober_jni_set_callback_handler(SoberJni *j, const char *method_name,
                                   SoberJniCallbackFn fn, void *user)
{
    for (i = 0; i < j->n_handlers; ++i)
        if (!strcmp(j->handlers[i].name, method_name)) {
            j->handlers[i].fn = fn; j->handlers[i].user = user;   /* REPLACE */
            return SOBER_JNI_OK;
        }
    ... append new SrHandler ...
}
```

Consequences: one handler slot per method name; the **last** call for a name
determines `fn`/`user`; there is no duplicate and no "first sticks" rule.

### 1.2 Dispatch (`sr_dispatch_call`, `native_jni.c:474-516`)

```
:482  m = sr_check(method, SR_K_METHOD); name = m->name; sig = m->sig;
:490  for each RegisterNatives entry matching name: result = fn(...); break;
:499  for each named handler matching name:
:502      j->callback_calls++; h->count++;
:504      result = h->fn(&env, obj, name, sig, args, h->user); break;
:510  if (want_object)
:511      result = (intptr_t)sr_shaped_or_new(j, (void *)result, "java/lang/String");
```

- Two **independent** loops. A native and a handler with the same name both run;
  the handler's result overwrites the native's (`:499` is not gated on `:490`).
  Not currently reachable: `register_natives=0` in the baseline (B09 §2).
- `want_object=1` only for the `Call*ObjectMethod*` wrappers (`:519-540`,
  `:551-583`); `CallLongMethod`/`CallIntMethod`/`CallBooleanMethod` pass `0`.
- `sr_shaped_or_new` (`:393-400`) returns the handle unchanged only if
  `sr_check()` validates `SR_OBJ_MAGIC`; `sr_check` (`:280-286`) rejects small
  integers via `sr_is_safe_ptr`. So a raw `int64_t`/`int`/`boolean` returned by a
  handler is turned into a fresh `java/lang/String` `SrObject` on a
  `want_object` call. That is the bridge-level primitive hazard (§4.2).

### 1.3 How the guest calls getters

The generic AutoValue/reflection helper `0x232dbbc` issues
`GetMethodID(name, "()Ljava/lang/String;")` (slot 33) then calls through
`0x2315d18`/`0x2315532` — the `CallObjectMethod` family — so most getters,
including the typed primitives, arrive with `want_object=1`
(`analysis/appstate_fields.md §1`, `analysis/lua-app-start.md §5`,
`analysis/B10-stability-synthesis.md §2 #8`). Registering a handler by name is
necessary but **not sufficient**: the returned handle's JNI type must match the
getter signature (lua-app-start.md:243-245).

---

## 2. Complete registration inventory

Line numbers are from the current worktree as read by this worker (they match
B06 §2; other agents may shift them). Knob = gate for the registration.

### 2.1 `src/linux/guest_app_shell.c` (shell-owned, lead-owned file)

| # | Site | Handler | Names registered | Knob |
| --- | --- | --- | --- | --- |
| 1 | `:152-176` (def), `:1231-1233` (install) | `start_app_get` | `surface`, `platformParams`, `vrContext` | `SOBER_START_APP` (else branch of `:1217`) |
| 2 | `:699-724` (def), `:1218-1227` (install) | `real_path_get` | all **ten** StartAppParams names (delegates to `start_app_get` first at `:702`) | `SOBER_REAL_APPSTATE_PATH` |
| 3 | `:861-874` (def), `:1270-1273` (install) | `game_params_get` | `gameId`, `placeId`, `accessCode`, `username` | `SOBER_GAME_LOADED_PATH` |
| 4 | `:951` | `sr_default_app_shell_handler` (via `sober_jni_install_default_app_shell_handlers`) | ~34 generic names (`getUsername`, `getUserId`, `getTheme`, `onAppShellReloadNeeded`, `gameLoadedCallback`, …). **No exact overlap** with the params-getter names (which are `username`, `userId`, `isUnder13`, …) | always |
| 5 | `:952-953` | `shell_callback` (rebind) | `onAppShellReloadNeeded`, `gameLoadedCallback` | always |
| 6 | `:956-958` | `shell_callback` (rebind) | `onAppBridgeNotification` | `SOBER_LOG_APP_BRIDGE_NOTIFICATION` (diagnostic-only; explicitly disqualified) |
| 7 | `:1199` | `run_callback` | `run` | `SOBER_SCHEDULE_RUNNABLE` |
| 8 | `:1210` | `sp_get` (via `sober_startapp_params_make` → `register`) | all ten StartAppParams names | `SOBER_STARTAPP_PARAMS=1` |

### 2.2 `src/jni/*.c` modules

| Module | Handler | Getter names | Knob | Status |
| --- | --- | --- | --- | --- |
| `startapp_params.c:357-375` | `sp_get` (`:320-355`) | all ten StartAppParams names (table `:89-100`) | `SOBER_STARTAPP_PARAMS=1` | **Built** (`src/jni/CMakeLists.txt`), called at `guest_app_shell.c:1210`, but shadowed (§3.1) |
| `game_params.c:405-423` | `gp_get` (`:377-403`) | all **24** StartGameParams names (table `:91-116`) | `SOBER_GAME_PARAMS=1` | **Not wired**: no caller; file absent from `src/jni/CMakeLists.txt` (stale `.o` in `build/`) |
| `startapp_v2.c:387-418` (owned by B1) | `sv2_get` (`:340-380`) | all ten StartAppParams names, exact smali sigs + `J/Z/I` primitive tags (header `:87-109`) | `SOBER_STARTAPP_V2=1` (legacy alias `SOBER_STARTAPP_PARAMS=1`) | Provides `sober_startapp_v2_reassert(j)` (`:402-418`) to re-claim last. **Not built, not wired** |
| `startgame_v2.c:481-528` (owned by B2) | `sgv2_get` (`:458-478`) | all 24 StartGameParams names (header `:70-93`) | `SOBER_STARTGAME_V2=1` | Provides `sober_startgame_v2_reclaim(j)` (`:519-528`) to re-claim last. **Not built, not wired** |
| `jni_native_objects.c` | — | none; installs object *views* only (`:194-217`), explicitly no handlers (`:22-23`) | `SOBER_NATIVE_OBJECTS` | No registration |
| `app_singleton_bridge.c:139-141` | `asb_list_get` | `size`, `isEmpty`, `get` | `SOBER_APP_SINGLETON_BRIDGE` | Not params getters; shadowed by JSON bridge (B06 §2 row 3) |
| `jni_json_bridge.c:254-260` | `jj_list_get`, `jj_exitinfo_get` | `size`, `isEmpty`, `get`, `getPackageName`, `getProcessName`, `getExitReason`, `getExitStatus` | `SOBER_JSON_APP_SINGLETON` / flag bridge | Not params getters |
| `jni_probe.c:97/106/120` | `cb`, `cb_narrow` | `onSurfaceCreatedNative`, `getNarrowHandle` | standalone probe only | Not in `guest-init-host` |

**Conclusion:** the *only* modules that register StartApp/StartGame getter names
are the shell's three inline handlers, `startapp_params.c`, `game_params.c`, and
the two (unwired) v2 modules. `jni_native_objects.c`, `app_singleton_bridge.c`
and `jni_json_bridge.c` do not touch these names.

---

## 3. The exact shadowing / order problem

### 3.1 StartApp phase — `surface` / `vrContext` always served by the sparse handler

Chronology inside `guest_app_shell_run()` when `SOBER_START_APP=1` (or
`SOBER_REAL_APPSTATE_PATH`), with `SOBER_STARTAPP_PARAMS=1`:

```
:1210  sober_startapp_params_make()  -> sp_get owns all 10 names
:1218  set("surface", real_path_get)          ...  :1227 all ten -> real_path_get
   OR :1231 set("surface", start_app_get) ... :1233 surface/platformParams/vrContext
:1237  guest 0x26fe80f  nativeAppBridgeV2StartAppWithParams(params)
```

Effective owner at `:1237` (last-write-wins):

| Getter | `SOBER_REAL_APPSTATE_PATH` | plain `SOBER_START_APP` |
| --- | --- | --- |
| `surface` | `real_path_get` | `start_app_get` |
| `platformParams` | `real_path_get` | `start_app_get` |
| `vrContext` | `real_path_get` | `start_app_get` |
| `appStarterPlace`, `appStarterScript`, `appUserId`, `isUnder13`, `username`, `membershipType`, `selectedTheme` | `real_path_get` | `sp_get` |

So the "faithful" object's handler is dead for at least `surface` and
`vrContext` in every configuration, and for all ten on the real path. This
matches PROGRESS.md:164 ("seven `StartAppParams` getters had no handlers") and
B06 §2.1. Side effect: `sp_get`'s per-getter counters (`startapp_params.c:330`)
under-report by 3 or 10, so any diagnostic that assumes `sp_get` served a getter
is wrong. The guest receives `start_app_get`'s synthetic 16-byte
`android/view/Surface` (`:159-161`) and `android/app/Activity` (`:169-171`)
rather than the authoritative object's cached handles (`g_h[0]`, `g_h[9]`).

### 3.2 StartGame phase — authoritative module never participates

```
:1267  if (SOBER_GAME_LOADED_PATH):
:1268     sg = sober_jni_new_native_object(".../StartGameParams", 0x100, NULL)
:1270     set("gameId", game_params_get)
:1271     set("placeId", game_params_get)
:1272     set("accessCode", game_params_get)
:1273     set("username", game_params_get)
:1274     guest 0x2ebd51a nativeAppBridgeV2StartGameWithParam(sg)
```

`sober_game_params_make` / `sober_game_params_register` are never called; the
24-getter `gp_get` never owns a name. Twenty of the 24 getters have **no
handler**, so `sr_dispatch_call` falls through to
`sr_shaped_or_new(..., "java/lang/String")` (B06 §2.2). `game_params.c` is also
missing from `src/jni/CMakeLists.txt`, i.e. it is not linked into `sober_jni`.

### 3.3 Cross-phase collision — one global table, two objects

StartApp and StartGame share five getter names:

```
surface, platformParams, vrContext, isUnder13, username
```

`username` is additionally re-registered by `game_params_get` at `:1273`
after the StartApp phase installed it (`sp_get` at `:1210` or `real_path_get` at
`:1225`). If both phases run, the game-phase handler owns `username` for the
rest of the run. Conversely, if both authoritative modules were wired once at
startup, whichever registered later would own the five shared names for *both*
guest calls. Because the guest uses these name-keyed jmethodIDs only during its
respective call (StartApp `0x26fe80f`, then StartGame `0x2ebd51a`), the correct
model is **phase-scoped re-registration**: the authoritative module for phase N
must be the last writer for its name set *immediately before* phase N's guest
call. The two `make()` calls at `:1210` and a new one before `:1274` naturally
give that ordering; a single unscoped registration cannot.

### 3.4 Why "register before the override" is the wrong fix

Both legacy headers (§0) and B06 §5.1.4 suggest registering the faithful
getters *before* the harness overrides. Under `native_jni.c` replace-by-name
semantics that guarantees the opposite of the intent: the sparse handlers win.
The correct rules are:

- **(a) Suppress** the legacy registration when the authoritative opt-in is
  active (`if (!authoritative_enabled) { set(legacy...) }`), **or**
- **(b) Re-assert** the authoritative set *after* the legacy registrations and
  immediately before the guest call — exactly what
  `sober_startapp_v2_reassert(j)` and `sober_startgame_v2_reclaim(j)` exist for.

Recommendation: do (a) for clarity and (b) as belt-and-braces against future
registrations (both hooks are idempotent and opt-in-aware).

---

## 4. Primitive mishandling

### 4.1 Legacy handler type errors (primary, clearly documented)

| Getter (exact sig) | Legacy handler result | Authoritative result |
| --- | --- | --- |
| `appUserId()J` | `real_path_get` `:710-711` returns jstring `"1"` — **wrong JNI type** | `sp_get` case 4 / `sv2_get` case 4 / `sgv2_get`: full 64-bit `int64_t` |
| `isUnder13()Z` | `real_path_get` `:721-723` returns untyped `0` (decoded default is `true`, lua-app-start.md:221) | `0/1` at `Z` width |
| `membershipType()I` | `real_path_get` `:721-723` returns untyped `0` | `int32_t` at `I` width |
| `placeId()J` (StartGame) | `game_params_get` `:861-874` returns jstring `"123456789"` — **wrong JNI type** (B06 §2.2) | `gp_get` case 3 / `sgv2_get`: full `int64_t` |
| `userId()J`, 20 other StartGame getters | no handler → generic `java/lang/String` | typed by `gp_get` / `sgv2_get` |

Evidence: `analysis/lua-app-start.md:212-233`, `analysis/appstate_fields.md §1`,
`analysis/B10-stability-synthesis.md §2 #8`. Because §3 shows the legacy
handlers *win* registration, these type errors are the ones the guest actually
sees today; fixing the authoritative modules alone changes nothing until the
order is fixed.

### 4.2 Bridge-level reshaping of correct raw primitives (secondary caveat)

When such a getter arrives via the generic `CallObjectMethod` helper with sig
`"()Ljava/lang/String;"` (§1.3), `want_object=1`. `sp_get`/`gp_get`/`sv2_get`/
`sgv2_get` return a raw machine integer; `sr_dispatch_call:510-511` calls
`sr_shaped_or_new`, which sees a non-`SrObject` value and allocates a
`java/lang/String` (§1.2). The guest then consumes a String where it expected a
`J`/`Z`/`I`. Both faithful modules *detect* the mismatch into `g_sig_mismatch`
(`startapp_params.c:331`, `game_params.c:387`) but do not act on it. Minimal
lead-side remedy (optional, `native_jni.c` is lead-owned): make the shaping
signature-aware — skip `sr_shaped_or_new` when the method's return descriptor is
a primitive (`()J`, `()Z`, `()I`, `()B`, `()S`, `()C`, `()F`, `()D`, `()V`),
keep it for reference/array returns and for unhandled names. This preserves the
decision 013/014 "never hand the guest NULL for an object return" invariant
(B09 Finding F3) while returning honest neutral `0` for an unhandled primitive
getter. Do not weaken the `sr_in_dispatch` guard (B09 P6).

---

## 5. Explicit getter tables (names / signatures)

Exact sigs from `startapp_params.h:59-80`, `game_params.h:50-98` (identical to
the v2 headers). `S` = shared with the other params object; `P` = primitive.

### 5.1 StartAppParams — 10 getters (`nativeAppBridgeV2StartAppWithParams`, RVA `0x26fe80f`)

| Getter | Signature | Kind |
| --- | --- | --- |
| `surface` | `()Landroid/view/Surface;` | object, **S** — shadowed (§3.1) |
| `platformParams` | `()Lcom/roblox/engine/jni/model/PlatformParams;` | object, **S** — shadowed |
| `appStarterPlace` | `()Ljava/lang/String;` | String |
| `appStarterScript` | `()Ljava/lang/String;` | String |
| `appUserId` | `()J` | **P** (jlong, full 64 bits) |
| `isUnder13` | `()Z` | **P**, **S** |
| `username` | `()Ljava/lang/String;` | String, **S** |
| `membershipType` | `()I` | **P** |
| `selectedTheme` | `()Ljava/lang/String;` | String |
| `vrContext` | `()Landroid/app/Activity;` | object, **S** — shadowed; must be NULL on this build (`rg/a1.E0()` false) |

### 5.2 StartGameParams — 24 getters (`nativeAppBridgeV2StartGameWithParam`, RVA `0x2ebd51a`)

| Getter | Signature | Kind |
| --- | --- | --- |
| `surface` | `()Landroid/view/Surface;` | object, **S** |
| `platformParams` | `()Lcom/roblox/engine/jni/model/PlatformParams;` | object, **S** |
| `deviceParams` | `()Lcom/roblox/engine/jni/model/DeviceParams;` | object; builder sets `null` (startgame_v2.h:27-31) |
| `placeId` | `()J` | **P** |
| `userId` | `()J` | **P** |
| `accessCode` | `()Ljava/lang/String;` | String |
| `linkCode` | `()Ljava/lang/String;` | String |
| `gameId` | `()Ljava/lang/String;` | String |
| `isUnder13` | `()Z` | **P**, **S** |
| `username` | `()Ljava/lang/String;` | String, **S** |
| `conversationId` | `()J` | **P** |
| `reservedServerAccessCode` | `()Ljava/lang/String;` | String |
| `callId` | `()Ljava/lang/String;` | String |
| `joinRequestType` | `()I` | **P** |
| `referralPage` | `()Ljava/lang/String;` | String |
| `launchData` | `()Ljava/lang/String;` | String |
| `gameJoinContext` | `()Ljava/lang/String;` | String |
| `eventId` | `()Ljava/lang/String;` | String |
| `joinAttemptId` | `()Ljava/lang/String;` | String |
| `joinAttemptOrigin` | `()Ljava/lang/String;` | String |
| `isoContext` | `()Ljava/lang/String;` | String |
| `referredByPlayerId` | `()J` | **P** |
| `gameIdToExclude` | `()Ljava/lang/String;` | String |
| `vrContext` | `()Landroid/app/Activity;` | object, **S** |

---

## 6. Proposed opt-in registration order (one authoritative object per phase)

Definition: an *authoritative params object* = the jobject returned by one
module's `make()` plus that module's complete getter handler set. The object
and its handlers must come from the **same** module: never pass module A's
object while module B's handlers win the names.

### 6.1 Universal ordering rule

For each guest call, the authoritative module's handlers must be the **last**
writers for its complete name set before that call. Concretely:

```
[early]   sober_jni_install_default_app_shell_handlers(j)        (:951)
          ... legacy / singleton registrations (non-overlapping) ...
PHASE A:  void *params = <StartApp authoritative make>(j, base);  // registers 10
          [legacy surface/... registration: SKIP if authoritative enabled]
          <StartApp authoritative reassert>(j);                   // optional belt-and-braces
          call guest 0x26fe80f(env, app_class, params)
PHASE B:  void *sg = <StartGame authoritative make>(j, base);     // registers 24
          [legacy 4-getter registration: SKIP if authoritative enabled]
          <StartGame authoritative reclaim>(j);                  // optional belt-and-braces
          call guest 0x2ebd51a(env, app_class, sg)
```

Phase B's registration of the five shared names after Phase A's guest call is
correct and required; it must not be moved earlier (that would give StartGame
values to the StartApp call).

### 6.2 Minimal variant using the existing v2 hooks (preferred)

- StartApp: `sober_startapp_v2_make(j, base)` → `...` → `sober_startapp_v2_reassert(j)` → `0x26fe80f`.
- StartGame: `sober_startgame_v2_make(j, base)` → `...` → `sober_startgame_v2_reclaim(j)` → `0x2ebd51a`.
- `/reassert`/`/reclaim` are opt-in-aware, idempotent, and exist precisely
  because `set_callback_handler()` is last-write-wins
  (`startapp_v2.h:20-24, 54-56`; `startgame_v2.h:194-198, 213-215`).

### 6.3 Decision 013 invariants (unchanged)

- Add `SOBER_STARTAPP_V2`, `SOBER_STARTGAME_V2`, and `SOBER_GAME_PARAMS` (if the
  legacy module is wired) to the withhold chain at
  `guest_app_shell.c:1337-1375`. `SOBER_STARTAPP_PARAMS` is already there
  (`:1357`); `SOBER_GAME_LOADED_PATH` is already there (`:1354`).
- Keep `shell_cb && render_ok` (`:1331-1332`, `:1384`) and the sole marker at
  `:1410` untouched. No getter registration may ever satisfy the gate.
- The v2 module headers explicitly ask for this (startapp_v2.h:60-62); honour it.

---

## 7. Minimal lead-side wiring change list (analysis recommendation; NOT enacted)

All files here are lead-owned; B8 changed none of them.

1. **`src/jni/CMakeLists.txt`** — add to `add_library(sober_jni STATIC ...)`:
   `game_params.c`, `startapp_v2.c`, `startgame_v2.c`. (Today only
   `startapp_params.c` of the four params modules is listed; the v2 files and
   `game_params.c` are unbuilt.)
2. **`src/linux/guest_app_shell.c` includes (near `:16`)** — add
   `#include "game_params.h"`, `#include "startapp_v2.h"`,
   `#include "startgame_v2.h"`.
3. **StartApp block `:1209-1242`** — when `sober_startapp_v2_enabled()`:
   - replace `sober_startapp_params_make(j, base)` at `:1210` with
     `sober_startapp_v2_make(j, base)`;
   - omit the opaque-object fallback at `:1214-1216` (the authoritative make
     supplies the object);
   - guard both legacy registration branches (`:1217-1234`) with
     `if (!sober_startapp_v2_enabled() && !sober_startapp_params_enabled())`;
   - call `sober_startapp_v2_reassert(j)` immediately before the `0x26fe80f`
     call at `:1237`;
   - pass the authoritative `params` to that call.
4. **StartGame block `:1267-1277`** — when `sober_startgame_v2_enabled()`:
   - replace `sober_jni_new_native_object(...)` at `:1268` with
     `sober_startgame_v2_make(j, base)`;
   - guard the four `set(..., game_params_get, ...)` at `:1270-1273` with
     `if (!sober_startgame_v2_enabled() && !sober_game_params_enabled())`;
   - call `sober_startgame_v2_reclaim(j)` immediately before the `0x2ebd51a`
     call at `:1274`;
   - pass the authoritative `sg` to that call.
   (If the lead prefers the legacy faithful module, use `sober_game_params_make`
   and its `gp_get` identically; it has no re-assert hook, so the legacy four
   registrations must be suppressed rather than merely followed.)
5. **Withhold chain `:1337-1375`** — add the three new knobs from §6.3. Pure
   strengthening of decision 013.
6. **Optional bridge fidelity (`src/jni/native_jni.c:510-511`, lead-owned)** —
   make `want_object` shaping signature-aware as described in §4.2. Keep the
   change behaviour-preserving for object returns and do not touch the
   `sr_in_dispatch` guard or any counter/gate. If the lead defers this, document
   that typed primitive getters must be invoked through typed `Call*Method`
   entries (or the v2 `verify()`/`getter_primitive()` introspection used) to
   avoid String reshaping.
7. **Do not** change `sober_jni_set_callback_handler` to "first registration
   wins": that would break the singleton/JSON and v2 re-assert semantics. Fix
   the misleading comments in `startapp_params.h:138-140` and
   `game_params.h:171-173` instead (one-line comment edit, lead-owned).

---

## 8. Acceptance evidence for the wiring change

- Logs show `[startapp-v2] registered all 10 ...` / `[startgame-v2] claimed
  24/24 ...` **after** any legacy `real-appstate: registered all ten ...` /
  game-handler lines, and immediately before the `0x26fe80f` / `0x2ebd51a`
  guest-call lines.
- `sober_startapp_v2_getter_calls("surface")` and
  `sober_startapp_v2_getter_calls("vrContext")` ≥ 1, and the equivalent
  StartGame counters for all 24 names ≥ 1, i.e. no shadowed name is served by
  `start_app_get` / `real_path_get` / `game_params_get`.
- `sig_mismatch == 0` for both modules; no generic `java/lang/String` handed to
  a `J`/`Z`/`I` getter; `appUserId` and `placeId` observed as 64-bit integers.
- `vrContext` is NULL on this build unless explicitly overridden.
- Decision 013 unchanged: with every opted-in knob set, the gate logs
  `marker withheld: diagnostic guest-state seed active` and emits **zero**
  `app shell rendered` markers; no ALT path / HONESTY NOTICE.
- Engine SHA-256 / BuildID unchanged; `ldd` and ctest invariants unaffected.

---

## 9. Limits / honesty

- This is static analysis of the frozen engine and the current worktree sources.
  No getter dispatch was captured live in this task; the guest-side call-family
  claim (§1.3) rests on the cited binary/analysis evidence, not a fresh trace.
- The worktree is moving: `startapp_v2.c/.h`, `startgame_v2.c/.h` and
  `jni_strings.h` appeared while this analysis was in progress (owned by B1/B2).
  The inventory in §2 reflects them as present-but-unwired; re-check before the
  lead applies §7.
- Every proposed change in §6-§7 is a recommendation to the lead. **No source,
  CMake, test, gate, or engine byte was modified by this worker**; only this
  analysis file was written.

---

## 10. Live-worktree snapshot (re-read at end of this task)

The lead applied part of the §6/§7 wiring while B8 was writing. This section is
a factual re-read; it does **not** claim those edits were made by this worker.

### 10.1 What changed since the baseline (§1-9)

| Area | Baseline (§1-9) | Live tree |
| --- | --- | --- |
| Includes (`guest_app_shell.c:9-19`) | only `startapp_params.h` | adds `game_params.h`, `startgame_v2.h`, `jni_strings.h` (`:18-20`); **no** `startapp_v2.h` |
| StartApp block | `sober_startapp_params_make` then **unconditional** `real_path_get`/`start_app_get` (`:1218-1233`) | same make at `:1227`; legacy ten/getter registrations now guarded by `if (!params)` / `else if (!params)` (`:1234-1251`) |
| StartGame block | opaque object + four inline handlers (`:1267-1273`) | unchanged at `:1284-1290` |
| v2 call sites | none | **still none**: `sober_startapp_v2_*` / `sober_startgame_v2_*` appear only in includes/declarations, never called |
| `src/jni/CMakeLists.txt` | only `startapp_params.c` of the params modules | adds `game_params.c`, `startgame_v2.c`, `jni_strings.c` (`:20-22`); **still no** `startapp_v2.c` |
| Withhold chain | 40 knobs, no ALT / no `SOBER_START_APP` | adds `SOBER_ALT_CALLBACK_PATH` (`:1354`) and `SOBER_START_APP` (`:1355`) — closing the B06 false-green; **no** `SOBER_STARTAPP_V2` / `SOBER_STARTGAME_V2` / `SOBER_GAME_PARAMS` yet |
| Gate / marker | `shell_cb` `:1331-1332`, withhold `:1337-1375`, gate `:1384`, marker `:1410` | `shell_cb` `:1348-1349`, withhold `:1354-1395`, `if (!shell_cb \|\| !render_ok)` `:1405`, marker `:1426` |

### 10.2 Line migration (baseline → live)

| Anchor | Baseline | Live |
| --- | --- | --- |
| `start_app_get` / `real_path_get` / `game_params_get` defs | `:152` / `:699` / `:861` | `:155` / `:702` / `:878` |
| `sober_jni_install_default_app_shell_handlers` | `:951` | `:968` |
| `sober_startapp_params_make` | `:1210` | `:1227` |
| legacy `real_path_get` registrations | `:1218-1227` | `:1235-1246` (now `if (!params)`) |
| legacy `start_app_get` registrations | `:1231-1233` | `:1248-1250` (now `else if (!params)`) |
| guest `0x26fe80f` | `:1237` | `:1254` |
| guest `0x2ebd51a` | `:1274` | `:1291` |
| `gameId/placeId/accessCode/username` → `game_params_get` | `:1270-1273` | `:1287-1290` |
| `int shell_cb = …` | `:1331-1332` | `:1348-1349` |
| withhold chain | `:1337-1375` | `:1354-1395` |
| `if (!shell_cb \|\| !render_ok)` | `:1384-1385` | `:1405` |
| `emit_marker("app shell rendered…")` | `:1410` | `:1426` |

### 10.3 Semantic impact on the verdict

- **StartApp shadowing (§3.1) is resolved in the live tree for the enabled
  configuration.** `sober_startapp_params_make()` returning non-NULL (i.e.
  `SOBER_STARTAPP_PARAMS=1` and allocation success) makes both legacy branches
  false (`:1234`, `:1247`), so `sp_get` is the sole owner of all ten names at
  `:1254`. This is exactly §6 rule (a). Caveat: on allocation failure `make`
  returns NULL and the legacy handlers install — an honest fallback, but still a
  shadow path if `sp_get` partially registered before failing (it registers only
  after `sp_ensure_handles` succeeds, so no partial registration in practice).
- **StartGame shadowing (§3.2) persists unchanged.** `:1284-1290` still passes
  an opaque `StartGameParams` and four inline `game_params_get` handlers;
  `gp_get` / `sgv2_get` never own a name. The primitive type errors (§4.1) and
  the 20 unhandled getters remain. `startgame_v2.c` is now compiled but never
  called, so `sober_startgame_v2_reclaim()` is available and simply needs the
  §7 step 4 wiring.
- **Cross-phase `username` collision (§3.3) persists** until the game block's
  four registrations are suppressed/guarded as in §7.
- **Decision 013 is stronger**, not weaker: adding `SOBER_ALT_CALLBACK_PATH`
  and `SOBER_START_APP` to the withhold list is the correct strengthening B06
  asked for. The three new opt-ins still need adding when they become live.
- **Remaining §7 work shrinks to:** (1) add `startapp_v2.c` to CMake **if** the
  lead switches StartApp to v2 (optional — the `!params` guard already fixes
  shadowing with `sp_get`); (2) include `startapp_v2.h` only if v2 is chosen;
  (3) wire `sober_startgame_v2_make` + `sober_startgame_v2_reclaim` into
  `:1284-1292` and guard the four inline handlers; (4) add
  `SOBER_STARTAPP_V2`/`SOBER_STARTGAME_V2`/`SOBER_GAME_PARAMS` to the withhold
  chain as they are enabled; (5) optional bridge primitive-shaping fix (§4.2);
  (6) correct the two misleading "first registered handler" header comments.
  No getter registration may be moved so late that it could affect the gate;
  the gate itself stays untouched.
