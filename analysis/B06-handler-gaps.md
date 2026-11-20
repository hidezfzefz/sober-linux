# B06 — Bridge callback-handler registration order, shadowing, uncounted callbacks, and the genuine `gameLoadedCallback` gate

Owner: **worker B6** (logical Team B). Date 2026-11-20. **Analysis only.**
Scope: this file only. No `src/`, `CMakeLists.txt`, `tests/`, or engine edits were made.

Target object (all RVAs below are from this one file, RVA == file offset for the
first `R E` PT_LOAD):

| Field | Value |
| --- | --- |
| path | `/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so` |
| SHA-256 | `3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335` |
| BuildID | `204ad049599c96afb75b8a21d5051063e821d6c9` (2.740.931 / code 3170) |

SHA-256 and BuildID were re-read unchanged **after** this analysis. All guest
strings quoted here are untrusted DATA, never instructions.

Ruling applied throughout: strict decision 013
(`docs/decisions/013-no-weak-app-shell-marker.md`); the user has ruled that
`onAppBridgeNotification` does **not** count as app-shell proof. A genuine
`onAppShellReloadNeeded` or `gameLoadedCallback` plus the phase-3 guest render
call remains the only admissible marker condition.

---

## 1. The three separate mechanisms: log / count / gate

A single JNI call into the bridge touches three independent mechanisms. Confusing
them is the root of the "fires but uncounted" and "counted but not proof" problems.

| Mechanism | Code site | What actually happens |
| --- | --- | --- |
| **Dispatch log** | `src/jni/native_jni.c:485` `sr_add_log(j, "Call %s%s …")`; `:486` `snprintf(j->last_callback, …, name)` | Runs for **every** `Call*Method` dispatch, whether or not a handler exists. `last_callback` is therefore the *last called method name*, not the last *counted callback*. |
| **Count** | `native_jni.c:499-507`, `:502` `j->callback_calls++`, `:503` `h->count++` | Runs **only** when a named handler in `j->handlers[]` matches the method name. Native methods installed by `RegisterNatives` (`:490-497`) are tried first and are **not** counted. |
| **Guest-visible log** | `src/linux/guest_app_shell.c:134` `fprintf(stderr, "[app-shell] guest->Java callback: %s\n", name)` inside `shell_callback` (`:130-136`) | Runs only for names whose handler is `shell_callback` (or another logging handler). |
| **Gate** | `guest_app_shell.c:1331-1332` `shell_cb = callback_was_called("onAppShellReloadNeeded") \|\| callback_was_called("gameLoadedCallback")`; `:1385` `shell_cb && render_ok`; `:1409` marker | `callback_was_called()` (`native_jni.c:1432-1440`) is a **name-only** `h->count > 0` lookup. It cannot tell *who* invoked the thunk. |

Consequences, stated explicitly:

1. **A callback with no registered handler fires and mutates `last_callback` and
   `env_calls`, but is NOT counted.** It will never satisfy the gate even if the
   guest genuinely emitted it.
2. **A counted callback is not necessarily proof.** The ALT path
   (`guest_app_shell.c:1312-1324`) calls thunk `0x25848e0` itself; that increments
   the same `h->count` and satisfies the same name-only gate. `SOBER_ALT_CALLBACK_PATH`
   is **not** in the withhold chain (`:1337-1375`; grep confirms its only other
   occurrences are `:1155` and `:1405`), which is why verifier report §2.3
   measured a false green with that knob alone.
3. `sober_jni_get_method_id()` (`native_jni.c:972-974`) deduplicates by
   name+sig+class via `sr_find_method_object()` (`:305-313`). Therefore the ALT
   path's method object at `:1316-1317` is **pointer-identical** to the seeded
   cached global at `guest_base+0x72a2fc0`. Method-pointer equality is *not* a
   discriminator between guest-notifier and harness-triggered calls.

The dispatch entry points that can carry a genuine callback are installed at
`native_jni.c:836-838`: `sr_CallStaticVoidMethod` at `fn[137]`,
`sr_CallStaticVoidMethodV` at `fn[138]` (= global JNI slot 142, offset `0x470`),
`sr_CallStaticVoidMethodA` at `fn[139]`. The guest's variadic thunk `0x25848e0`
issues `call *0x470(%rax)` at `0x258495a`; the notifier `0x27eaf6e` calls the
thunk at `0x27eafae` (return address `0x27eafb3`). See
`analysis/callback/callback-map.md` and `analysis/game-loaded-path.md` §1.

---

## 2. Registration order inside `guest_app_shell_run()` (authoritative timeline)

`sober_jni_set_callback_handler()` (`native_jni.c:1006-1022`) is a **name-keyed
replace**: lines 1010-1014 overwrite `fn`/`user` of an existing handler with the
same name. **Last writer wins.** There is no duplicate-name guard and no "first
registration sticks" rule. The effective handler table is therefore entirely a
function of call order in `guest_app_shell.c`:

| # | Site (`guest_app_shell.c`) | Registers / effect | Shadow impact |
| --- | --- | --- | --- |
| 1 | `:903` `sober_seed_global_string` | object/global seed, no handlers | — |
| 2 | `:924-932` `sober_app_singleton_populate` (opt-in) | `size`/`isEmpty`/`get` → `asb_list_get` (`app_singleton_bridge.c:139-141`) | later overwritten by #3 |
| 3 | `:933-941` `sober_jni_json_populate_app_singleton` (`SOBER_JSON_APP_SINGLETON`) | `size`/`isEmpty`/`get` → `jj_list_get`; `getPackageName`/`getProcessName`/`getExitReason`/`getExitStatus` → `jj_exitinfo_get` (`jni_json_bridge.c:254-260`) | **shadows #2**: `size` 0→1, `isEmpty` 1→0 (`:211-212` vs `asb_list_get :99-100`) |
| 4 | `:945` `sober_native_objects_install` | installs guest-shaped object *views* only; explicitly "nothing here emits a guest→Java callback" (`jni_native_objects.c:22-23`) | no handler shadowing; impacts object shape used by #2/#3 via `sober_native_java_list` |
| 5 | `:951` `sober_jni_install_default_app_shell_handlers` | ~34 names → `sr_default_app_shell_handler` (`native_jni.c:1054-1081`), incl. `onAppShellReloadNeeded`, `gameLoadedCallback`, all user/locale/activity getters | initial owners of those names |
| 6 | `:952` `set("onAppShellReloadNeeded", shell_callback)` | **overrides #5** | count + guest log on genuine arrival |
| 7 | `:953` `set("gameLoadedCallback", shell_callback)` | **overrides #5** | count + guest log on genuine arrival |
| 8 | `:956-958` `set("onAppBridgeNotification", shell_callback)` only under `SOBER_LOG_APP_BRIDGE_NOTIFICATION` | opt-in diagnostic | user ruled this callback does not count; it must stay diagnostic-only |
| 9 | `:981` `sober_jni_seed_guest_jni_state` | seeds the cached jmethodID globals from `sr_cached_methods[]` (`native_jni.c:1282-1345`), including `0x72a2fc0` `gameLoadedCallback(J)V` | makes genuine thunk calls resolvable |
| 10 | `:998` JSON flag bridge opt-in | guest flag setter path, no handlers | — |
| 11 | `:1148` `nativeAppBridgeV2InitWithParams` `0x24abf96` | guest-side registration `0x24ad699: 0x253cb56(1, &0x7234520)` installs the GameLoaded/GameLeave lifecycle slot `0x6dbd860` (`game-loaded-path.md` §2) | **required** for genuine notifier dispatch |
| 12 | `:1199` `set("run", run_callback)` under `SOBER_SCHEDULE_RUNNABLE` | first-frame Runnable task | counted, but explicitly not a completion callback (`:140`) |
| 13 | `:1210` `sober_startapp_params_make` → `sober_startapp_params_register` (`startapp_params.c:357-374`) | all **ten** `StartAppParams` getters → `sp_get` | only when `SOBER_STARTAPP_PARAMS=1`; overwritten next |
| 14 | `:1217-1227` (`SOBER_REAL_APPSTATE_PATH`) or `:1231-1233` (else) | real_path: all ten getters → `real_path_get`; else: `surface`/`platformParams`/`vrContext` → `start_app_get` | **shadows #13**: all 10 (real_path) or 3 of 10 (else). Matches PROGRESS.md:164 "seven StartAppParams getters had no handlers" before the faithful module landed. |
| 15 | `:1270-1273` `SOBER_GAME_LOADED_PATH` | `gameId`/`placeId`/`accessCode`/`username` → inline `game_params_get` (`:861-874`) | shadows the faithful game-params module names **if it were registered** — it is not (see §3) |
| 16 | `:1312-1324` ALT path | harness calls `0x25848e0` directly | **harness-triggered; never app-shell proof** |
| 17 | `:1331-1385` | gate + withhold chain | see §5 |

### 2.1 StartAppParams shadowing — precise

`SOBER_STARTAPP_PARAMS=1` makes `sober_startapp_params_make` (`:1210`) register
all ten getters with `sp_get` (names at `startapp_params.h:59-68`: `surface`,
`platformParams`, `appStarterPlace`, `appStarterScript`, `appUserId`, `isUnder13`,
`username`, `membershipType`, `selectedTheme`, `vrContext`). The harness then
immediately re-registers a subset:

- With `SOBER_REAL_APPSTATE_PATH`: all ten are replaced by `real_path_get`
  (`:1218-1227`), so `sp_get` is dead for this configuration.
- Without it (plain `SOBER_START_APP=1`): only `surface`, `platformParams`,
  `vrContext` are replaced by `start_app_get` (`:1231-1233`); the other **seven**
  keep `sp_get`.

This is not a correctness bug by itself (both handlers return shaped objects),
but it means the "faithful" module's call counters (`sp_get`'s `s_getters[idx].calls`,
`startapp_params.c:330`) under-report by 3 or 10, and any diagnostic that assumes
`sp_get` served a getter is wrong.

### 2.2 The faithful StartGameParams module is not wired in

`sober_game_params_make` / `sober_game_params_register` (`game_params.c:405-423`,
`:447-461`) register all **24** `StartGameParams` getters (names at
`game_params.h:50-73`) with `gp_get`. A repo-wide grep finds **no caller outside
`game_params.c` / `game_params.h`** — `guest_app_shell.c` never invokes them.
`SOBER_GAME_LOADED_PATH` instead uses its own four-getter inline handler
(`game_params_get`, `:861-874`) for `gameId`/`placeId`/`accessCode`/`username`.

There is also a signature hazard in that inline handler: `game_params.h:78`
defines `placeId()` as `()J` (scalar jlong, and `gp_get` returns `v->place_id` at
`game_params.c:395`), whereas the harness `game_params_get` returns
`sober_jni_new_string_utf8(j, "123456789")` — a **jstring where the guest expects
a jlong**. Any genuine path that constructs `StartGameParams` through these
handlers should use `sober_game_params_make`, not the inline four-getter stub.

### 2.3 Native objects vs our modules — no handler shadowing, but object-shape coupling

`jni_native_objects.c` installs views only (`:22-23`); it never calls
`sober_jni_set_callback_handler`. Both singleton modules prefer its guest-native
`java.util.List` shape when `SOBER_NATIVE_OBJECTS` is enabled
(`app_singleton_bridge.c:145-150`, `jni_json_bridge.c:266-269`), but the *handler*
for `size`/`isEmpty`/`get` is still whichever module registered last (JSON wins
when both opt-ins are set, per row 3 above). So "native objects" influence the
object being iterated, not which callback handler counts.

---

## 3. Guest→Java callbacks that fire but are uncounted

The bridge seeds 28 cached `jmethodID` globals (`native_jni.c:1282-1329`); the
default handler list (`:1057-1073`) covers only a subset. Any cached callback
without a matching handler increments `env_calls` and `last_callback` but **not**
`callback_calls`, and cannot satisfy the gate. Uncounted by default:

| Callback | Cached method global (guest BSS) | Smali sig | Note |
| --- | --- | --- | --- |
| `gameDidLeave` | `0x72a2fa0` | `()V` | real sibling path: guest `0x2ec45a6` → thunk `0x25848e0` (`game-loaded-path.md` §2); **fires uncounted** |
| `screenOrientationChanged` | `0x72a2fa8` | `(I)V` | — |
| `openNativeOverlay` | `0x72a2fb0` | `(Ljava/lang/String;Ljava/lang/String;)V` | — |
| `saveImageToAlbum` | `0x72a2fd8` | `(Ljava/lang/String;)V` | — |
| `onVrSessionStateUpdate` | `0x72a2fe0` | `(I)V` | — |
| `exitGameWithError` | `0x72a3018` | `(I)V` | — |
| `listenToMotionEvents` | `0x72a2f60` | `(Ljava/lang/String;)V` | — |
| `promptNativePurchase` (×2) | `0x72a2f78`, `0x72a2f80` | purchase sigs | — |
| `promptNativePurchaseWithPayload` | `0x72a2f88` | — | — |
| `promptNativePurchaseWithPaymentSessionId` (×2) | `0x72a2f90`, `0x72a2f98` | — | — |
| `onAppBridgeNotification` | `0x72a3028` | `(Ljava/lang/String;Ljava/lang/String;)V` | thunk `0x3dea91`; counted **only** under `SOBER_LOG_APP_BRIDGE_NOTIFICATION` (`:956-958`); user ruled it does not count |
| `onSaveInstanceStateNative` and 6 other lifecycle names | lifecycle table `0x6d37bd0..0x6d37e08` | see `jni-map.md` §2 | not in the default handler list |

Conversely, these *are* counted but are explicitly **not** app-shell proof:
`surface()` / `platformParams()` / `vrContext()` (`:158-173`), the `run` first-frame
task (`:144-146`), and `get*`/`onDataModelNotificationCallback` served by
`sr_default_app_shell_handler` (`native_jni.c:1024-1052`).

Important for `onAppShellReloadNeeded`: cached global `0x72a3020`, method string
`0x4a702d`. It is stored once by `JNI_OnLoad` at `0x227b432` and has **no native
reader** in this build; worker #467 concluded it cannot genuinely fire from the
native side (`analysis/jni/appshell-reload-trigger.md`). Therefore in practice
the only admissible gate signal is `gameLoadedCallback`.

---

## 4. Why a genuine `gameLoadedCallback` is not gated correctly today

The counting machinery would work for a genuine arrival: `shell_callback` is
registered unconditionally (`:953`), the cached method `0x72a2fc0` is seeded
(`:981`, `native_jni.c:1304`), and `0x24ad699` registers the notifier slot during
`InitWithParams` (`:1148`). A real notifier entry at `0x27eaf6e` would reach
`sr_CallStaticVoidMethodV` (`fn[138]`) and increment both counters.

Two blockers prevent that from being valid evidence:

1. **No origin discrimination.** `callback_was_called()` (`:1432-1440`) is
   name-only. The ALT path (`:1312-1324`) produces an indistinguishable
   `gameLoadedCallback` count, and `SOBER_ALT_CALLBACK_PATH` is absent from the
   withhold chain. This is the false-green mechanism measured in
   `analysis/decision-memo-alt-thunk-vs-013.md` §2.3.
2. **The genuine-path opt-in is force-withheld.** `SOBER_GAME_LOADED_PATH`
   (`:1267`) — the only harness route that drives `StartGameWithParam
   0x2ebd51a` and `SendAppEventOnGameLoaded 0x2ebca9e` — is itself an entry in
   the withhold chain at `:1354`. So even if the guest genuinely entered
   `0x27eaf6e`, `:1376-1380` would set `shell_cb = 0` and emit
   `marker withheld: diagnostic guest-state seed active`. The same is true of
   `SOBER_LUA_APP_START` (`:1355`), `SOBER_STARTAPP_PARAMS` (`:1356`), and the
   four opt-ins at `:1357-1358`.
   (PROGRESS.md also records the current `SOBER_GAME_LOADED_PATH` run faulting at
   guest `0x31a1360` on a TSD-sentinel handle, so no genuine callback has yet
   been observed — but that is a state/robustness gap, separate from the gate
   logic addressed here.)

Also note the stale test contract: `tests/app-shell.sh:68` unsets
`SOBER_ALT_CALLBACK_PATH` while `:112-116` requires the
`ALT PATH guest call 0x25848e0` string — mutually exclusive. Fixing that belongs
to the lead/verifier; this worker did not touch `tests/`.

---

## 5. Minimal opt-in registration plan (analysis recommendation — nothing enacted)

Goal: make a genuine `gameLoadedCallback` be **logged**, **counted**, and
**allowed to gate** with **no harness-triggered thunk**, while *strengthening*
decision 013 rather than weakening it. All edits below would be in files owned by
the lead (or a future authorized owner); none were made here.

### 5.1 Handler registration / bridge (minimal)

1. Keep `set("gameLoadedCallback", shell_callback)` at `guest_app_shell.c:953`
   (unconditional). This already guarantees dispatch-time log
   (`native_jni.c:485-486`), count (`:502-503`), and guest log (`:134`).
2. Register a **dedicated origin-checked handler** for `gameLoadedCallback`
   *after* `:953` so it shadows `shell_callback` for that one name. It must:
   - read the first vararg as `jlong placeId` from the `va_list*` delivered by
     `sr_dispatch_call` (`native_jni.c:474-475`, `:532`); the thunk's GP save area
     puts RCX at `+0x18` with `gp_offset=0x18`, so `va_arg(..., jlong)` yields the
     notifier's `RSI` placeId;
   - require `placeId > 0` — the harness ALT path hardcodes `0` at
     `guest_app_shell.c:1321`, while notifier `0x27eaf6e` passes the real placeId
     from `RSI` (`game-loaded-path.md` §1);
   - walk the guest frame chain and require the notifier return address
     `guest_base + 0x27eafb3` (call site `0x27eafae`); the thunk
     (`push rbp; mov rbp,rsp` at `0x25848e0`) and notifier (`0x27eaf6e`) keep
     frame pointers, so this needs no engine byte patch;
   - emit `[app-shell] genuine guest->Java callback: gameLoadedCallback(placeId=…) notifier=0x27eaf6e`,
     increment a **separate** `genuine_game_loaded` counter, and set a bridge flag
     readable by the gate.
   Method-pointer equality is deliberately **not** used (see §1, consequence 3).
3. Do **not** register `onAppBridgeNotification` for gating. If
   `SOBER_LOG_APP_BRIDGE_NOTIFICATION` is used, it stays a counted/logged
   diagnostic only, per the user's ruling.
4. To avoid the §2.1/§2.2 shadowing, build the param objects with the faithful
   modules (`sober_startapp_params_make` / `sober_game_params_make`) and register
   their getters **before** any harness override; do not pass a jstring for
   `placeId()` (sig `()J`, `game_params.h:78`).

### 5.2 Real guest call order (no thunk) — RVAs

Driving the genuine notifier requires the guest to reach
`SessionReporterState_GameLoaded`, not a direct call to `0x25848e0`:

1. `nativePreloadFlagOverrides` `0x2edd859` with the JSON object
   `{"EnableAppBridgeComponents":true}`, then `nativeRetryInit` `0x2edd739`;
   verify flag byte `0x72cb648 == 1`.
2. `nativeGameGlobalInit` `0x233762c`.
3. `nativeAppBridgeV2InitWithParams` `0x24abf96` — confirm `0x24ad699`
   (`0x253cb56(1, &0x7234520)` → slot `0x6dbd860`) executes; this registers the
   notifier family.
4. `nativeAppBridgeV2SendAppEventOnAppReady` `0x2ebcf36`.
5. `nativeAppBridgeStartLuaAppDM` `0x25448f6` (installs `app+0x408`) and
   `nativeAppBridgeV2StartAppWithParams` `0x26fe80f`.
6. `nativeAppBridgeV2StartGameWithParam` `0x2ebd51a` with a faithful
   `StartGameParams` (24 getters via `sober_game_params_make`).
7. `nativeAppBridgeV2SendAppEventOnGameLoaded` `0x2ebca9e` (after app-shell
   manager setup). Expected: guest enters `0x27eaf6e(placeId)`, loads cached
   class `0x72a2f48` and method `0x72a2fc0`, calls thunk `0x25848e0` → JNI slot
   142 `CallStaticVoidMethodV` → `gameLoadedCallback(J)V`.
8. `nativeAppBridgeV2UpdateSurfaceAppWithPlatformParams` `0x2773194` for the
   phase-3 render evidence (`guest call 0x649d422`).
9. **Never** call `0x25848e0` from the harness and **never** set
   `SOBER_ALT_CALLBACK_PATH`.

This is a superset of `analysis/game-loaded-path.md` §5; the only addition is the
origin-checked handler in §5.1.

### 5.3 Gate changes (minimal, strictly 013)

1. Replace the name-only `shell_cb` at `:1331-1332` with the origin-verified
   flag from §5.1 (`onAppShellReloadNeeded` remains unconditionally
   disqualified by worker #467's negative result). Keep `render_ok` (`:1383`)
   and the `shell_cb && render_ok` gate (`:1385`).
2. **Add `SOBER_ALT_CALLBACK_PATH` to the withhold chain** (`:1337-1375`). It is
   currently missing; adding it closes the measured false green and is a
   *strengthening*, fully consistent with decision 013.
3. **Do not** simply delete `SOBER_GAME_LOADED_PATH` from the withhold chain.
   That knob currently fabricates `StartGameParams` through the inline
   four-getter stub (§2.2) and is therefore synthetic state. The correct minimal
   route is a **new, narrowly scoped opt-in** (e.g. `SOBER_B6_GAME_LOADED_REAL`)
   that (a) is absent from the withhold chain, (b) executes only §5.2's real
   guest exports with faithful param objects, and (c) sets no guest-state seed.
   If the lead instead rules that *any* opt-in is manufactured state, then the
   honest outcome is unchanged: the genuine callback is logged and counted but
   the marker stays withheld. Removing withhold entries without a lead ruling
   would weaken the gate and is out of scope here.
4. Keep the ALT-path honesty notice (`:1403-1408`) and the diagnostic-only
   `SOBER_APP_SHELL_DIAGNOSTIC` branch (`:1393-1396`) untouched.

### 5.4 Acceptance evidence for a genuine pass

- Guest-side: notifier `0x27eaf6e` entered with `RSI = placeId > 0`, return
  address `0x27eafb3` observed in the frame chain, cached method `0x72a2fc0`
  non-NULL.
- Bridge: `[app-shell] genuine guest->Java callback: gameLoadedCallback(placeId=…)`
  and `genuine_game_loaded >= 1`; `callbacks` increased by ≥1 over the pre-call
  baseline (baseline `3–4`, last `onAppBridgeNotification`, gate withheld).
- No `ALT PATH guest call 0x25848e0`, no `HONESTY NOTICE`,
  `SOBER_ALT_CALLBACK_PATH` unset.
- `guest call 0x649d422` (phase-3 render) present; `shell_cb && render_ok` true.
- No withhold knob active; `SOBER_LOG_APP_BRIDGE_NOTIFICATION` may be set only as
  the explicitly non-qualifying diagnostic.
- Engine SHA-256 / BuildID unchanged.

---

## 6. Limits / honesty

- This is static analysis of the frozen engine plus the current worktree sources;
  no genuine notifier entry was captured at runtime in this task. Step §5.2 is a
  predicted ordering, same confidence caveat as `analysis/game-loaded-path.md` §7.
- The exact bridge edits in §5 are recommendations for the file owners; **no
  code, gate, withhold list, test, CMake, or engine bytes were changed** by this
  worker.
- `onAppBridgeNotification` remains disqualified by the user's ruling, and
  harness-triggered thunks remain disqualified by decision 013. This report does
  not propose any change to that policy — only origin discrimination and closing
  the missing `SOBER_ALT_CALLBACK_PATH` withhold entry.
