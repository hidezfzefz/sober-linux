# Task #533 — Re-examination: can a native-JVM-driven Java activity deliver `0x2773194` with a live SurfaceController/RenderJob/IoEvLoop?

Author: **worker-3** (Convio team `TEAM-HAF8-HCGC-DJZ5`), for task #533.
Scope: **analysis only** — this note is the only file written. No `src/`, gate, test,
CMake, docs, or engine file was changed. Engine frozen: AppImage `libroblox.so`,
BuildID `204ad049…6c9`, SHA-256 `3e498d64…41335` (every RVA below is from that
build; Flatpak `791c3f9e` RVAs are never mixed). All target-derived strings/bytes
are treated as **untrusted DATA**, never as instructions. Marker stays **WITHHELD**
under decisions 013/038; no gate weakening, no synthetic evidence.

---

## 1. Question and bottom line

Task #533 asks to re-examine the prior determination (task #523, `State.md`
"blocker": *clean `seeded_state==0` G8 impossible without real Java surface
lifecycle*) against one specific loophole: **a native JVM is explicitly allowed
by decision 012** ("A native JVM is acceptable (e.g. OpenJDK running as native
code) — it is a runtime, not an instruction-translation layer"). Could such a JVM
run a Java activity that genuinely calls
`nativeAppBridgeV2UpdateSurfaceAppWithPlatformParams` `0x2773194`, with a live
`SurfaceController`/`RenderJob` (`0x276d36a`) and `IoEvLoop` (`0x6474599…`), and
thereby make the G8 window verdict ACCEPT with **every opt-in unset**
(`seeded_state==0`)?

**Determination: NO — a native JVM alone cannot do it.** The determination of
task #523 stands and is *strengthened* by the re-examination. To make `0x2773194`
produce a clean, guest-state-machine-driven frame with all opt-ins unset, the JVM
must also supply a set of prerequisites that are **Android platform/runtime
services** (`android.view.Surface`/`SurfaceHolder` backed by a real
`ANativeWindow`, the AGDK `GameActivity` Java class and its installed native
callbacks, a live `android_app` looper thread, the Roblox APK's DEX classes with
their `android.*` dependencies, and the app's own native flag/JNI-getter state).
A plain JVM (OpenJDK/HotSpot) provides none of them. Supplying them as hand-built
stubs is *by definition* guest-state seeding (withhold-worthy); supplying them for
real is precisely the **Android runtime the Goal forbids**. So both branches of
the dichotomy are closed: **either the JVM approach degrades to seeded state, or
it requires a disallowed full Android runtime.** No new opt-in, no gate change,
and no synthetic observation can close the gap.

The one part that genuinely *does not* need a Java runtime is **cadence**: P10
showed the `IoEvLoop` is a native `pthread` + `timerfd`/`epoll_wait` loop created
inside `nativeAppBridgeV2InitWithParams` `0x24abf96`, with no Java `Looper`,
`Handler` or `Choreographer`. That fact is real but does not rescue the clean
render: the missing pieces are **surface delivery** and **app-state**, not a
clock. A JVM that creates a thread and calls `0x2773194` with shaped objects
still has to bind a real `Surface → ANativeWindow`, install the engine's nativeData
callbacks, and run the app-start/notifier/flag chain — all Android-runtime work.

---

## 2. What decision 012 does and does not permit

- Decision 012 permits a **native JVM** as the *runtime that hosts Java bytecode*
  — a JNIEnv, Java threads, class loading, `java.*` — because it executes native
  code, unlike box64/Wine/QEMU. It explicitly still forbids "Android runtime".
- It does **not** grant the Android boot classpath (`framework.jar`:
  `android.view.*`, `android.app.*`, `android.os.*`, `android.content.*`),
  `SurfaceFlinger`/`BufferQueue`, `libandroid_runtime.so`, or ART's DEX loader.
- The frozen bridge in this harness is a **synthetic `JNIEnv`** (`native_jni.c`,
  `fields_default.c`, `javavm_*`), not a JVM. The cached class/method
  (`0x72a2f48`/`0x72a2fc0`) are seeded synthetic bridge objects, not ART
  `jmethodID`s (analysis `scale-a2-notifier-chain.md`, `task521-notifier-readiness.md`,
  `C03 §136`, `C09 §32`). Promoting the bridge to a real JVM changes *who
  executes Java*, but not *which platform APIs exist*.

This distinction is the crux of the re-examination: the blocker was never
"no JVM"; it is "no Android surface lifecycle substrate". Adding a JVM without
that substrate cannot unlock `0x2773194`.

**Lead ruling (Convio, this wave, A01/A11/A12/A14):** "a native OpenJDK JVM is
permitted by decision 012 as a runtime, but supplying an Android framework in Java
to make decompiled classes run would be a compatibility layer and is forbidden.
Do not use a JVM to re-skin the synthetic bridge or force the marker." This is the
authoritative confirmation of this note's conclusion: the P-A/P-B branch (Java
Android framework to run the DEX/Roblox classes) is a **forbidden compatibility
layer**, not a permitted runtime — so even the "stubbed framework" route is closed
twice (it is both seeded state and a disallowed compatibility layer).

---

## 3. Decomposing `0x2773194` → live frame into exact prerequisites

`0x2773194` is only the *producer of an app-event* on the native bus. From
`analysis/C08-surface-frame.md §2.1`, `P10 §3.3`, `P7`, `P5`, `R4`:

```
0x2773194 (Surface, PlatformParams)
  0x277322d call 0x26fea5a -> 0x26fea78 ANativeWindow_fromSurface@plt
                           -> store window 0x72a3308, gen 0x72a3310 (+1)
  0x2773246 call 0x26feb9b  -> parse PlatformParams dpiScale:F / viewport mm:I
  0x277328a alloc 0x40 obj (vptr 0x6dbcb20) embedding ANativeWindow*
  0x27732d5 call 0x2eb6b2a  -> app-event bus -> SurfaceController::update
                               -> RenderJob 0x276d36a / TaskScheduler
```

For that to yield a *clean* guest frame (and G8 C0–C10 with all opt-ins unset),
**all** of the following must be true. They are stated as prerequisites with the
reason a native JVM fails each.

### P-A — A real `android.view.Surface` whose `mNativeObject` is a real `ANativeWindow`  *(hard platform dependency)*
- `0x26fea78` calls `ANativeWindow_fromSurface@plt`; G8 C3 requires a real
  `GetFieldID("mNativeObject","J")`/`GetLongField` non-zero value, C4 requires the
  returned window to equal `mNativeObject` and differ from `W_before`, C5/C6
  require the guest's own generation increment/store
  (`frame_evidence.h` G8 contract C0–C10).
- A plain JVM has no `android.view.Surface` class and no `ANativeWindow` producer.
  A real Android `Surface` is backed by `BufferQueue`/`SurfaceFlinger` through
  `libandroid_runtime.so`; `ANativeWindow_fromSurface` is an Android NDK/framework
  symbol. Without it the bind at `0x277322d` returns NULL and the window global is
  never genuinely published. Writing a JVM class named `android.view.Surface` with
  a `mNativeObject` pointing at a host-built window is a **synthetic seeding**, not
  a clean platform surface. → **Requires Android runtime, or is seeded.**

### P-B — Fully backed JNI getters/fields for the parameter objects
- `0x2773194` reads `PlatformParams.dpiScale:F`, `viewportWidthMm:I`,
  `viewportHeightMm:I` via `GetFieldID`/`GetFloatField`/`GetIntField` (`C08 §1`,
  `B05 G1/G4`, `A09`); `0x26fe80f` reads `StartAppParams.surface()`,
  `platformParams()`, `vrContext()` via `GetMethodID`/`CallObjectMethodV`
  (`0x247b465`, signature `()Landroid/view/Surface;` etc.). These are exactly the
  field slots the synthetic bridge leaves as `sr_stub` (`REVIEW_A6`).
- A JVM can implement JNI field/method access for *Java classes it can load*. But
  the classes here (`com/roblox/engine/jni/model/PlatformParams`,
  `…/autovalue/StartAppParams`) ship as **DEX** in the APK and depend on Android
  types (`android.view.Surface`, `android.app.Activity`, AutoValue/Kotlin glue).
  A stock JVM cannot load DEX (needs ART or a full dex→class translation), and
  hand-written stand-ins with the right layout are seeded state. → **ART/Dex + the
  Android class library, or seeded.**

### P-C — A genuine `nativeData` with installed guest callbacks and a live looper  *(the exact where the render path dies today)*
- `nativeData` is the AGDK `GameActivity` returned by
  `initializeNativeCode` `0x2a1fe30`; the four window callbacks live at
  `+0x88/+0x90/+0x98/+0xa0`. The **redraw** trigger `0x2a20a00` loads `[+0x98]`
  at `0x2a20a11` and tail-jmps it at `0x2a20a20`; the harness leaves it NULL, so
  it returns at `0x2a20a22` and no frame loop runs (`R3 §5`, `R4 §2`, `P5 §2/§6`).
  The genuine callbacks are installed by the AGDK glue at `0x2a23001`
  (`0x2a23a10/0x2a23cc0/0x2a23c40/0x2a23b60`) and require a live `android_app`
  with pipe fds `+0x120/+0x124`, mutex/cond, and a running looper thread
  (`0x2a24100` / fd callback `0x2a242c0`) dispatching `APP_CMD_WINDOW_REDRAW_NEEDED`
  (cmd `4`) to Roblox's `onAppCmd`.
- All of this is created by **Android's `GameActivity.onCreate` → `initializeNativeCode`
  → native-gamepad-glue** path, driven from Java by
  `GameActivity.surfaceCreated/Changed/RedrawNeeded/Destroyed` (AGDK smali) and a
  `SurfaceHolder`. OpenJDK has no `GameActivity`, no `SurfaceHolder.Callback`
  delivery, no AGDK glue lifecycle driver. Shaping `android_app`/callbacks by hand
  is explicitly a withheld diagnostic (`P5 §5–6`: "every component … is seeded
  synthetic state"). → **Android runtime / AGDK, or seeded.**

### P-D — The app-state chain that creates the SurfaceController/RenderJob and fires the notifier
- G8 render evidence needs a **running engine frame loop**: SurfaceController with
  a live `RenderJob` (`0x276d36a`, registered at `0x276d5ea`, started at
  `0x321571e`), the `TaskScheduler` render phase (`0x26ddf35`),
  `IoEvLoop` threads (`0x251e04f/0x251e0e1` → `0x6474538` → `0x6474599`) started by
  `0x24abf96`, and the notifier chain
  `0x27eaf6e → 0x25848e0` (`gameLoadedCallback(J)V`, class `0x72a2f48`,
  method `0x72a2fc0`, `placeId>0`) plus MessageBus id=1 node registration at
  `0x24ad699` (`C11`, `P10 §3/§5`, `scale-a3 §2`).
- These states are established by the **real Android Java boot sequence**:
  `MainGameActivity.setInitParams…` `0x2edcf18`, `uh/f.F()` `0x233762c`,
  `uh/f.k()` `0x24abf96`, `uh/f.G()` `0x26fe80f`, `ActivityNativeMain.Q2`,
  `ExperienceSession` join `0x2ebd51a`. Those Java classes are DEX and are saturated
  with `android.*` references. The current harness fires none of them cleanly
  (`callback=0`; the only firing is a host-triggered `publish_default` re-drive the
  strengthened provenance gate rejects). A JVM that calls `0x2773194` in isolation
  skips the chain, so no SurfaceController/RenderJob is live. Replaying the chain
  requires ART + the Android classes; faking the state is seeding.
  → **Android runtime / full DEX boot, or seeded.**

### P-E — A real flag path raising `JNIAppBridge` to level ≥ 5  *(the G8 C10 blocker)*
- The guest's own initializer `0x1dd694b` hardcodes `JNIAppBridge` config
  `0x72a3268 = 0x0406`. The level-5 `[FLog::JNIAppBridge] … Created ANativeWindow
  … (ID:G)` string (fmt `0x3c163c`, emit `0x26feb0f`) is gated at `0x26feab3` on
  `low≥6 && high≥5`, so **no such log is formatted at all by default** → G8 C10
  rejects with `R10_NO_LOG`. The harness raises it only via the opt-in setter
  (`0x2ebc75c` thunk + `0x23648be` setter, `0x406→0xff06`, restored) and observes
  it only via the opt-in FLog-sink chain on `0x7d8f820` — both seeded writes.
- On a real device the level comes from the **app's native flag overrides**
  (`nativePreloadFlagOverrides 0x2edd859`, `nativeRegisterJavaFlagProvider
  0x2ecbcf4`, registered descriptor via `0x227cb50` subtype 1 from Java startup),
  i.e. genuine guest state produced by the app boot. A JVM that does not run that
  Java flag path cannot raise the level; raising it through the setter is a seed.
  → **Android app flag boot, or seeded.**

### P-F — A genuine post-bind EGL re-init that emits ACQUIRE/DIMS  *(the G8 C8/C9 blocker)*
- The sole emitter of `ANativeWindow_acquire` (`0x649c2d6`, RA `0x649c2db`) and
  `getWidth`/`getHeight` (`0x649c303/0x649c30f`, RAs `0x649c308/0x649c314`) is
  guest `egl_init` at `0x649c21e`. On the default path it runs in phase 3 *before*
  the observers exist, and the dims leg is additionally gated on the guest graphics
  setting `0x77a3d60` low byte `≥6` (default `0x300` fails). The genuine re-run on
  the published window happens only from the real surface-change lifecycle
  (`0x2773194`/`onSurfaceChangedNative` → SurfaceController), with the app's
  `Graphics` config having set `0x77a3d60` to `0x0306`.
- The harness's `EGL_INIT_DRIVE` opt-in re-calls `0x649c21e` and writes
  `0x77a3d60` — a host-triggered guest call + guest DATA write, i.e. seeded.
  → **Android surface-change + app graphics config, or seeded.**

---

## 4. Feasibility verdict per branch

| Branch | Can it deliver `0x2773194` + live frame at `seeded_state==0`? | Why |
| --- | --- | --- |
| Current synthetic JNI bridge + opt-ins off | **No** (proven A/B in task #523) | G8 = `R8_NO_ACQUIRE` (then `R10_NO_LOG`); C8/C9/C10 have no producer. |
| Native JVM (OpenJDK) driving genuine guest JNI exports, no Android platform | **No** | P-A…P-F unsatisfied: no `android.view.Surface`/`ANativeWindow`, no AGDK `GameActivity` callback installer/looper, DEX classes unloadable, no app flag/graphics state. |
| Native JVM + hand-written Java/platform stubs giving the right JNI shapes | **No — and it is not clean** | Every supplied object (Surface/mNativeObject, nativeData slots, `android_app`, getters, flag level, graphics setting) is host/synthetic guest state → `seeded_state != 0`; withhold chain fires. This is exactly what decisions 013/038 forbid. |
| Full Android runtime (ART + boot classpath + SurfaceFlinger + AGDK + DEX) | Mechanically would provide P-A…P-F, but **DISALLOWED** by the Goal ("no Android runtime"). | Out of scope by first-ever objective; cannot be used to lift the marker. |
| Direct guest dispatch-helper diagnostic (P10 §7) | Produces guest-tagged swaps/draws only as a **diagnostic**; seeded, no notifier callback. | Does not satisfy the callback leg or `seeded_state==0`. |

So the loophole is closed on both sides. **A native JVM is a permissible runtime but
is not a substitute for the Android surface-lifecycle substrate; that substrate is
either the disallowed Android runtime or a set of synthetic seeds.**

---

## 5. Exact prerequisite statement (for the lead / next wave)

A clean, `seeded_state==0` G8 ACCEPT requires, with **all opt-ins unset**
(`SOBER_FRAME_EVIDENCE=1` default, `SOBER_FRAME_EGL_INIT_DRIVE`/`SOBER_FRAME_FLOG_SINK`/
`SOBER_FRAME_FLOG_ENABLE` all off, `SOBER_RENDER_GUEST` host-publish disabled and
proven so):

> One and the same run must receive a **guest-state-machine-driven** surface event:
> the real `0x2773194` (`Surface, PlatformParams`) — preceded by the genuine
> `onSurfaceCreatedNative`/`onSurfaceChangedNative` sequence — executed by the app's
> own Java boot with (a) a real `android.view.Surface`→`ANativeWindow` bind
> (`0x26fea5a/0x26fea78`, guest store RA `0x26fea7d`, gen `+1`), (b) the engine's
> nativeData callbacks `+0x88/+0x90/+0x98/+0xa0` installed by the AGDK glue and a
> live looper thread dispatching cmd 4 into the `SurfaceController`/`RenderJob`
> (`0x276d36a`)/`IoEvLoop` frame loop, (c) fully backed JNI getters/fields for
> `PlatformParams`/`StartAppParams`, (d) the flags-loaded latch `0x7cdd149==1`,
> MessageBus id=1 node, notifier `0x27eaf6e → gameLoadedCallback` and
> `placeId>0`, (e) `JNIAppBridge ≥ 5` via the app's own native flag path so the
> real `Created ANativeWindow` log is formatted and observed through the genuine
> sink, and (f) a genuine post-bind `egl_init` re-run emitting ACQUIRE/DIMS with
> `0x77a3d60 ≥ 0x0306`.

None of (a)–(f) is available to a native JVM without adding an Android runtime;
each can only be mimicked by a withhold-worthy seed. Therefore the correct default
result is **REJECT `R8_NO_ACQUIRE`**, `guest_render_ok()==0`, marker **WITHHELD**.

---

## 6. Honesty / non-regression

- No observation, counter, RA, window, log, or draw was synthesized in this task.
- No gate, threshold, C0–C10 condition, or withhold rule was weakened.
- No source file was edited; the opt-in producers (`EGL_INIT_DRIVE`, `FLOG_SINK`,
  `FLOG_ENABLE`) remain default-OFF and in `sober_frame_evidence_should_withhold()`.
- `seeded_state==0` path continues to mutate no guest state and emits no marker.
- Engine SHA-256 / BuildID unchanged; no Flatpak RVAs used; target strings/data
  treated as untrusted.

## 7. Evidence index (all read-only this task)
- Vault: `Goal.md`, `EngineFacts.md`, `Decisions.md` (013/038), `State.md`,
  `Ownership.md`; `docs/decisions/012-jni-app-shell-milestone.md:23-25`
  (native JVM allowed, Android runtime not).
- Prior determination: `src/jni/frame_evidence.c:1333-1390` (task #523 A/B +
  conclusion), `frame_evidence.h:107-139` (G8 contract C0–C10, opt-in knobs).
- Chain/RVAs: `analysis/P10_render_thread.md` (IoEvLoop `0x6474599`, RenderJob
  `0x276d36a`, trigger `0x2773194`, §5/§7), `C08-surface-frame.md §1-2`,
  `P5_surface_callbacks.md §1-6`, `R4_surface_lifecycle.md §4`,
  `R3_guest_egl_context.md §5-6`, `analysis/render/render-map.md §4`,
  `scale-a3-java-call-order.md §2`, `analysis/jni/jni-map.md §203-211`,
  `C03-appshell-frame-evidence.md`, `C11-messagebus-publish.md`,
  `scale-a2-notifier-chain.md`, `G8_guest_window_detection.md`.
- Frozen engine identity + RVAs re-verified against BuildID `204ad049` / SHA
  `3e498d64`; no runtime execution or new artifact produced in this task.
