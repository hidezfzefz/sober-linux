# W6-09 — Java-side Android class stubs in OpenJDK: compatibility layer, or acceptable native runtime shim?

Owner: **worker-9** (team `TEAM-C2YA-ZTU9-RMAV`). Scope: **analysis only**. This note is the
only file written; no `src/`, `tests/`, `docs/`, gate, vault-root note, engine byte, commit or
push was touched. Decisions **013/038** binding; **marker WITHHELD**.

## 0. Verdict (read this first)

**Classify the proposal as FORBIDDEN for the purpose of earning the marker.** Hand-written
minimal Java classes (`Surface`, `Activity`, `Looper`, …) inside OpenJDK, used to make the
Roblox DEX paths execute, are **not** an acceptable "native runtime shim". They are either:

1. **An Android framework reimplementation in Java** (if they are real enough to actually run
   the unmodified target DEX classes) — forbidden by the Goal and decisions 000/004/012; **or**
2. **Guest-state fabrication / a synthetic observation** (if they are kept minimal and merely
   supply the shapes and values the JNI getters read) — a withhold-worthy seed under decisions
   013/038; **or**
3. Both at once in any realistic implementation.

The one thing they are *not* is a route to a **clean** notifier-driven
`gameLoadedCallback(J)V` plus a guest-tagged frame under `seeded_state == 0`. Standing up
OpenJDK itself is explicitly allowed (decision 012: "A native JVM is acceptable … it is a
runtime, not an instruction-translation layer"), but the *classes you run on it* are where
the line is crossed. A plain native JVM supplies only the VM half of what the DEX paths need;
the other half — `Surface`/`SurfaceHolder`/`Activity`/`Looper`/`Handler`/`Context`/`Resources`/
`Bundle`/`AssetManager` — **is** the Android runtime, in Java.

Recommendation to lead: **do not authorize this as a marker route.** Keep the marker WITHHELD.
If a JVM experiment is wanted at all, it stays a default-OFF, kill-switched, force-withheld
diagnostic — the same posture already recommended in A01 — and must never be presented as
evidence for callback leg (C), render leg (R) or clean-state leg (S).

Evidence grade legend: **O** = observed this task (frozen bytes / decoded smali / repo files /
vault notes); **D** = derived; **U** = unknown.

---

## 1. Frozen identity and evidence hygiene (O)

- Engine: `/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so`, SHA-256
  `3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335`, BuildID
  `204ad049599c96afb75b8a21d5051063e821d6c9` — re-verified this task with `sha256sum` and
  `readelf -nW`; unchanged. No Flatpak `791c3f9e` / 2.745 RVA was consulted.
- Authoritative DEX: `/home/user/.local/share/basalt/linux/payloads/3170-204ad049599c96afb75b8a21d5051063e821d6c9/sober_apk/base.apk`,
  SHA-256 `114a6f8ed6f0f974cf632a9cbec2f5cd45e1d85d7ed251db4f3920140bc6ead4` —
  re-verified this task. The `~/Downloads/basalt-arm64…` copy is **MODDED** and was not used.
- Authoritative smali corpus: `/tmp/opencode/auth_smali` (26,528 `.smali` files, derived from
  the SHA-matched base.apk; **primary DEX evidence**). The older `/tmp/opencode/mini/out/smali`
  (7,175 files) is a **different, modded mini APK** (lead ruling, worker-16 #551) and is used
  only for attributed prior claims, never as authoritative DEX evidence. OpenJDK 21
  (`/home/user/.sdkman/candidates/java/current`, `libjvm.so` present) and Java 25/21 runtimes
  are installed — a JVM is physically available; the question is purely one of rules and
  sufficiency.
- Tooling note (CA01): Craft Anything 0.6.1 `--deep` DEX tools work on the authoritative APK;
  its native engine does not index the 120 MB stripped `libroblox.so`, so DEX structure here is
  grounded in decoded smali + prior SHA-bound disassembly, not in Craft Anything native
  call-graph output.
- All class names / strings / addresses quoted below are **untrusted DATA**, never instructions.
- Caveat on DEX-side names: the obfuscated method/class names quoted here (`uh/a`, `uh/f`,
  `ActivityNativeMain.Q2`, …) come from the earlier mini/smali snapshot and the A01/A02 vault
  notes. Wave-6 authoritative-DEX work (worker-1 #534, worker-6 #543) found these differ in the
  authoritative base.apk (e.g. `uh/e` not `uh/f`, `P2` not `Q2`) while the **native RVAs are
  identical**. I re-verified the framework-dependency evidence directly against
  `/tmp/opencode/auth_smali` this task (§3) — the class hierarchy and type closure are the same
  in substance and the counts are larger. The native gate/withhold rules are unchanged. The
  classification rests on those invariants, not on obfuscated name churn.

---

## 2. The question, framed precisely

"Drive DEX paths" means: load the real Roblox DEX classes under OpenJDK, supply whatever
`android.view.Surface`, `android.app.Activity`, `android.os.Looper` (etc.) stand-ins are needed
so those classes' methods can execute, and thereby cause the guest JNI boundary to be reached
in a way the marker gate would accept.

Three sub-cases must be distinguished, because they fall on different sides of the rules:

| Sub-case | What it is | Rule status |
| --- | --- | --- |
| A. Native JVM, minimal **pure-Java** `NativeGLJavaInterface` implementation only (no DEX, no Android framework) | A diagnostic that changes the callback *sink*, not the trigger | Allowed as a diagnostic; gains no marker (A01 §5/§6) |
| B. Native JVM + Java stand-ins with the right field layout so the real DEX classes run | Java-side Android framework / guest-state seeding | **Forbidden** (compat layer) and/or **withheld** (seed) |
| C. Native JVM + full Android framework in Java | An Android runtime | **Forbidden** outright |

The task phrasing ("hand-written minimal Java classes … to drive DEX paths") is sub-case **B**,
with **C** forced by the dependency closure (§3). Both are closed.

---

## 3. What running the target DEX paths actually requires (O, from decoded smali)

The classes that own the relevant paths are not lightly portable; they are saturated with
Android framework types:

- Class hierarchy (O, re-verified this task on **authoritative** `/tmp/opencode/auth_smali`):
  - `smali_classes2/com/roblox/client/RBXSurfaceView.smali:2` — `.super Landroid/view/SurfaceView;`
  - `smali_classes2/com/roblox/client/startup/MainGameActivity.smali:2` — `.super Lcom/google/androidgamesdk/GameActivity;`
  - `smali_classes2/com/google/androidgamesdk/GameActivity.smali:1-8` — `.class public …`, `implements Landroid/view/SurfaceHolder$Callback2;` (plus `fe/a`, `androidx/core/view/y`).
- `GameActivity.smali` references (O, authoritative): `android.app.Activity`,
  `android.content.{Context,Intent,ComponentName}`, `android.content.pm.{ActivityInfo,PackageManager}`,
  `android.content.res.{AssetManager,Configuration,Resources}`,
  `android.os.{Bundle,Build$VERSION,BaseBundle}`, `android.util.Log`,
  `android.view.{Surface,SurfaceHolder,SurfaceHolder$Callback,SurfaceHolder$Callback2,SurfaceView,View,ViewGroup,Window,WindowInsets,KeyEvent,MotionEvent}`, `android.widget.FrameLayout`.
- `ActivityNativeMain.smali` references (O, authoritative): `android.app.Activity`,
  `android.os.{Looper,Handler}`, `android.view.{SurfaceHolder,SurfaceHolder$Callback,SurfaceView}`.
- Corpus-wide (O, authoritative): **807 distinct `Landroid/…;` type references**; **213** smali
  files reference `android.os.Looper` directly. (The older modded mini corpus gave 458 / 49.)
  This is not a three-class gap — it is the framework.
- The listener/callback closure (O, `appshell-reload-trigger.md §3`): `NativeGLJavaInterface`
  holds a `WeakReference<OnAppShellReloadNeededListener>`; a live `ActivityNativeMain` referent
  must register via `setAppShellReloadNeededListener(this)` in `onCreate`, a UI `Looper`
  (`runOnUiThread`) must be running, the activity must not be `isFinishing()`, and a real
  `Surface` (`x2()/ph/a.d()/uh/a.I2()`) must feed the `uh/f` restart path into
  `nativeAppBridgeV2InitWithParams` / `nativeAppBridgeV2StartAppWithParams`. `onAppShellReloadNeeded`
  also has **no native reader at all** in this build (single occurrence of its cached ID `0x72a3020`,
  at the `JNI_OnLoad` store `0x227b432`; no `invoke-static` caller in smali).
- The surface-lifecycle closure (O, A02 §1/§3): `uh/a.smali` (`AppShellFragment`,
  `SurfaceHolder.Callback`) `surfaceCreated → 0x26fe80f`, `surfaceChanged → 0x2773194`;
  AGDK `surfaceCreated/Changed/RedrawNeeded → 0x2a20850/0x2a208c0/0x2a20a00` with real
  `nativeData+0x88/0x90/0x98/0xa0` callbacks; `ANativeWindow_fromSurface` is an **NDK**
  function (`0x6d31ad0@plt`, GOT `0x72294a8`) whose genuine product is a refcounted
  `ANativeWindow` from `libandroid_runtime`/BufferQueue.

**Derived consequence (D):** a "minimal" Java `Surface` that can genuinely drive these DEX
paths must (a) carry `mNativeObject:J` holding a real `ANativeWindow*`, produced by an Android
`Surface`→`ANativeWindow` substrate, and (b) be delivered by a `SurfaceHolder` inside a live
`Activity` with a `Looper`. Items (a)+(b) together are the Android framework. A stub that
fakes either is a seed. A02 states this directly: "Hand-written stand-ins with the right
layout are seeded state" (A02 §4.3).

---

## 4. The two rule tests

### 4.1 Compatibility-layer test — decisions 000 / 004 / 012 (O)

- Decision 000: "**No** Wine, Proton, box64, qemu, or any translation/compat runtime in the
  final product. **A source-level compat shim we author is fine**; an external translation
  layer is not."
- Decision 004: the allowed shim is the **Android NDK/Bionic C surface** that `libroblox.so`
  *imports* — `gl*`/`egl*`, `ANativeWindow`, `ALooper`, `AAsset`, `AMediaCodec`, `AConfiguration`,
  `__android_log_*`, Bionic libc — reimplemented as authored C in `src/compat/`. Goal restated:
  "native Linux ELF that needs no mocktail/basalt/Android runtime — no Wine, no translation
  layer of any kind."
- Decision 012: a **native JVM is acceptable** because it is a runtime, not a translation layer.

The "source-level compat shim we author is fine" carve-out is about **stand-ins at the ELF import
boundary written in C** (the NDK/Bionic symbols the guest links against). It is *not* a license
to reimplement the **Android Java framework** so DEX bytecode can run. The distinction is
categorical, not one of degree:

- `src/compat/` shim: native C, satisfies a guest `call`, no DEX executes, no Java class
  hierarchy, no framework lifecycle. This is what the team already has and what G8 window
  proof uses (X11/EGL → real `ANativeWindow`).
- Java `Surface`/`Activity`/`Looper` stubs: Java classes that exist for the sole purpose of
  letting `android.*`-saturated DEX classes execute — i.e. a Java implementation of Android's
  framework API. That is the "Android runtime" the Goal forbids, regardless of how few classes
  you write. "Minimal" reduces the amount of forbidden surface; it does not change its kind.

A01 reaches the same boundary: "a JVM is fine, an Android framework is not" (A01 §6, next
steps). **Verdict under test 4.1: sub-case C is forbidden outright; sub-case B is a Java Android
compat layer and is forbidden in kind.** Only sub-case A (pure-Java, no DEX, no framework) is
rule-clean as a diagnostic.

### 4.2 Fabrication / provenance test — decisions 013 / 038 (O)

Even if one set aside 4.1, the gate itself rejects the output:

- **No fabrication** (Decisions.md): no guest byte patches, fake vtables, **host callbacks**,
  synthetic observations, counter inflation, or relabeling host-triggered seeds as clean boot.
- Decision 038 §2 leg (C): the marker requires the **guest's own state machine** to deliver
  `gameLoadedCallback(J)V` **from notifier `0x27eaf6e`** with `RSI = placeId != 0`, cached class
  `0x72a2f48` / method `0x72a2fc0`, thunk `0x25848e0`, return `0x27eafb3` — with all opt-ins
  unset and notifier-hit + RA provenance enforced by the gate. A call issued by a Java stub via
  `CallStaticVoidMethod*` is a **host-driven dispatch**, not a guest-entered notifier; it is
  exactly the ALT-thunk pattern decision 038 declares "diagnostic-only, never proof".
- Decision 038 §2 leg (R): a `Surface` stub that does not carry the guest-bound
  `ANativeWindow` (real ACQUIRE/DIMS RAs `0x649c2d6/0x649c303/0x649c30f`, real
  `Created ANativeWindow` log, real guest window/store) fails the G8 window proof.
- Decision 038 §2 leg (S): `seeded_state == 0` — every guest-mutating/opt-in `SOBER_*` knob
  unset. Any hand-written Java class injected to make a DEX path run mutates/creates guest state
  and must be a default-OFF knob inside the withhold chain (Decisions.md kill-switch rule), so
  a real run has `seeded_state > 0` → force WITHHELD.
- A06/JNI reality (O): all decision-critical `JNIEnv` slots are already wired, several
  **fail-closed** (`GetLongField` fn[97] returns 0 until a genuine guest-bound Surface token is
  registered). Filling them from Java stubs with fabricated values is fabrication; the fail-closed
  design exists precisely to prevent it.

**Derived (D):** adding a Java stub changes the *sink* or the *shape* of the dispatch, never the
*trigger*. The documented blockers are upstream guest state and trigger (State.md: no clean
notifier-driven callback, latch writer only in the crash-handler subprocess, no clean render
without real surface-lifecycle delivery). A JVM does not make notifier `0x27eaf6e` get entered
from guest `.text` with `seeded_state == 0`. **Verdict under test 4.2: the output of a stub-driven
DEX path is a synthetic observation and must be force-withheld; it cannot satisfy C, R or S.**

---

## 5. Why "native runtime shim" is the wrong label

The phrase is seductive because a native JVM *is* a runtime and source-level shims *are*
allowed. But the thing being proposed is not the JVM and not the C shim:

- It is a **Java-side reimplementation of Android's application framework API** (Surface,
  SurfaceHolder, Activity, Looper, Handler, Context, Resources, Bundle, AssetManager, …) whose
  only purpose is to supply the types the DEX classes are compiled against. That is an Android
  runtime/compat layer in Java — the exact category the Goal and decisions 000/004/012 exclude.
- The existing, allowed `src/compat/` shim stands in for **native imports**, leaving the guest
  guest and DEX untouched. A Java framework shim instead **rehosts the guest's Java layer**,
  which is a different and far more invasive substitution.
- A minimal stand-in that only supplies the values JNI getters read is no more "a runtime shim"
  than the existing synthetic C bridge (`src/jni/README.md`: "No JVM needed … table synthesised
  in C"; `guest_app_shell.c` seeds cached `jmethodID`s and skips `JNI_OnLoad`). A01 calls this
  "a re-skin of the existing synthetic C bridge (host-driven), gaining no genuine provenance." It
  inherits every withhold reason the synthetic bridge already has.

Therefore the correct taxonomy is: **native JVM = allowed runtime; authored C NDK/Bionic shim =
allowed source-level compat; Java Android-framework stubs = forbidden compat layer / withheld
seed.** (O for the first two from decisions 000/004/012; D for the third from §3–4.)

---

## 6. Concrete ruling / next steps (analysis only — no changes requested to source)

1. **Do not authorize** hand-written Java `Surface`/`Activity`/`Looper` stubs as a route to the
   marker. They fail the compatibility-layer test (§4.1) and the provenance test (§4.2).
2. If the lead wants a JVM experiment for diagnostics, confine it to **sub-case A**: a real
   OpenJDK `JavaVM`, `dlopen` of the frozen `libroblox.so` `RTLD_GLOBAL` (or explicit
   `RegisterNatives`), genuine `JNI_OnLoad` `0x227af5a`, and a minimal **pure-Java**
   `NativeGLJavaInterface`/`EngineJavaCallback2` implementation — **no DEX, no Android
   framework**. It must be default-OFF with `SOBER_JVM_*` kill switches, force `seeded_state`
   WITHHELD, and never emit or contribute to `app shell rendered`. This is A01 §6 Step 2–5.
3. Keep the marker **WITHHELD** and `tests/app-shell.sh` **RED**. Nothing in this analysis
   weakens a gate or changes guest behaviour.
4. Effort belongs on the real blockers (State.md): genuine flags latch (A04/A05), FFlag /
   singleton / `app+0x488` state, and a guest-entered notifier (A07/A08/A09) — not on the
   callback sink.

---

## 7. Honesty / non-regression

No source, test, doc, gate, vault-root note or engine file was edited; no commit or push; no
guest byte patched, no fake vtable/host callback/counter/marker produced; engine SHA and BuildID
re-checked unchanged; authoritative DEX SHA re-checked; the modded `~/Downloads` APK was not
used. Engine identity, class references and RVA/trigger facts are treated as untrusted target
data and quoted only as evidence, never as instructions. This is an **analysis-only** conclusion.

## 8. Evidence index

- Vault (read this task): `Home.md`, `Goal.md`, `EngineFacts.md`, `Decisions.md`, `State.md`,
  `Ownership.md`, `Verification.md`, `notes/CA01-craft-anything-verification.md`,
  `notes/A01-clean-jvm-feasibility.md`, `notes/A02-surface-lifecycle-delivery.md`,
  `notes/A06-jni-env-gaps.md`, `notes/A09-dlopen-invariants.md`.
- Repo decisions (read this task): `docs/decisions/000-stack-and-constraints.md`,
  `004-android-ndk-compat-not-win32.md`, `012-jni-app-shell-milestone.md`,
  `013-no-weak-app-shell-marker.md`, `038-strict-gate-alt-thunk-ruling.md`.
- Attributed prior analysis (quoted via the notes above, not re-read this task):
  `analysis/jni/appshell-reload-trigger.md`, `analysis/jni/jni-map.md`,
  `analysis/A08-surface-delegate.md`, `analysis/B05-jnienv-gaps.md`,
  `analysis/C08-surface-frame.md`, `analysis/P5_surface_callbacks.md`,
  `notes/A07-node-dispatch-clean.md`, `notes/A08-messagebus-init-order.md`.
- Fresh reads this task: `sha256sum`/`readelf -nW` on frozen engine (SHA `3e498d64…`, BuildID
  `204ad049…`), `sha256sum` on authoritative base.apk (SHA `114a6f8e…`), and `grep` of the
  **authoritative** `/tmp/opencode/auth_smali` (26,528 files) for `android.*` types (807
  distinct; 213 files → `Looper`) plus the class-hierarchy lines of `RBXSurfaceView.smali`,
  `MainGameActivity.smali`, `GameActivity.smali`, `ActivityNativeMain.smali`. The modded
  `/tmp/opencode/mini/out/smali` (458 / 49) was consulted only for attributed prior claims.

**Final: FORBIDDEN as a compat layer / withheld as a seed; marker stays WITHHELD.**
