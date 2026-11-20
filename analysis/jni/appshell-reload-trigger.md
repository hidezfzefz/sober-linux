# Task #467 — `onAppShellReloadNeeded`: can it genuinely fire from the real path?

Owner: **shellcb** (task #467). Date 2026-11-20. Scope: **analysis only**, new file under
`analysis/jni/`. No `src/`, `CMakeLists.txt`, or engine edits.

## 0. Bottom line (lead this first)

**No.** In this engine build there is no genuine native trigger for
`NativeGLJavaInterface.onAppShellReloadNeeded()V`. The cached `jmethodID` is
**`0x72a3020`**, not `0x72a2fc0`; the task premise conflates two adjacent cached
globals. `0x72a3020` has **exactly one occurrence in the entire 20.6 M-line /
1.1 GB `.text` disassembly** — the `JNI_OnLoad` **store** at RVA `0x227b432`.
There is no native reader, no copy, no APS2 relocation addend, no second
`GetStaticMethodID` lookup, and no Java-side caller. The only callback in this
family with a real native call path is `gameLoadedCallback` (`0x72a2fc0`), which
task #466 (gameload) owns.

Consequently:

- A **native-only harness cannot legitimately cause** `onAppShellReloadNeeded`
  to fire: there is no ART/JVM to host the Java method, no `ActivityNativeMain`
  instance to serve as listener, no `Looper` for `runOnUiThread`, and no `Surface`.
  A harness could only *fabricate* the event in its own fake JNI dispatch, which
  proves nothing about the guest or an app-shell frame.
- Under **strict decision 013** this callback must **not** be used as the
  `app shell rendered` gate signal. Its absence is a correct `callback=0` state,
  not a bug to be worked around with seeding or thunk injection.

Engine integrity: SHA-256 `3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335`,
BuildID `204ad049599c96afb75b8a21d5051063e821d6c9`, re-checked unchanged after analysis.

## 1. Address correction — what `0x72a2fc0` and `0x72a3020` actually are

All string RVAs were verified by raw-byte reads from the first `LOAD` segment
(file offset == vaddr, `readelf -lW`: `LOAD 0x0 / vaddr 0x0 / filesz 0x6d33b90`):

| RVA | Raw bytes at that RVA |
| --- | --- |
| `0x4a702d` | `onAppShellReloadNeeded` |
| `0x288017` | `()V` |
| `0x4fcdc1` | `gameLoadedCallback` |
| `0x433046` | `(J)V` |
| `0x3dea65` | `com/roblox/engine/jni/NativeGLJavaInterface` |

`JNI_OnLoad` (`0x227af5a`) disassembly, exact sites:

- `0x227b41b` `lea … # 0x4a702d` (method name `onAppShellReloadNeeded`),
  `0x227b422` `lea … # 0x288017` (sig `()V`),
  `0x227b42c` `call *0x388(%rax)` (`GetStaticMethodID`, JNI slot 113),
  **`0x227b432` `mov %rax,0x5027be7(%rip) # 0x72a3020`** — the onAppShellReloadNeeded ID.
- `0x227b4d4` `lea … # 0x4fcdc1` (method name `gameLoadedCallback`),
  `0x227b4db` `lea … # 0x433046` (sig `(J)V`),
  `0x227b4e5` `call *0x388(%rax)`,
  `0x227b4eb` `mov %rax,0x5027ace(%rip) # 0x72a2fc0` — the gameLoadedCallback ID.
- `0x227b2a3` `FindClass("com/roblox/engine/jni/NativeGLJavaInterface")` and
  `0x227b2ba` `NewGlobalRef` (slot 0xa8) → `0x227b2c0` stores the cached **jclass**
  in `0x72a2f48`.

So the task's "jmethodID `0x72a2fc0` ... JNI_OnLoad write at `0x227b432`" is wrong on
both halves: the `0x227b432` write targets `0x72a3020`, and `0x72a2fc0`'s write is at
`0x227b4eb`. `analysis/jni/jni-map.md:95,101` already maps them correctly.

## 2. Native-side reader search (exhaustive, verify/extend of earlier analysis)

Method: direct text search over `/tmp/opencode/libroblox.dis` (objdump `-d`,
20,622,123 lines), plus APS2 relocation decoding of all 565,390 relocations with
`analysis/bootstrap/decode_aps2.py`, plus raw-string search of the 120 MB ELF.

| Global | Total textual occurrences in disassembly | Sites |
| --- | --- | --- |
| `0x72a3020` (`onAppShellReloadNeeded`) | **1** | `0x227b432` — write only |
| `0x72a2fc0` (`gameLoadedCallback`) | **2** | `0x227b4eb` write; **`0x27eaf9f` read** |

Further negative results for `onAppShellReloadNeeded`:

1. `grep -c "72a3020" libroblox.dis` = **1** (whole file, any form — rip-relative
   `mov`, `lea`, absolute encoding, or objdump comment). No reader anywhere.
2. Immediately after `call *0x388(%rax)` at `0x227b42c`, `%rax` is stored once to
   `0x72a3020` and then overwritten by the next `GetStaticMethodID`; the ID is
   never copied into a second global or struct.
3. The method-name literal `onAppShellReloadNeeded` occurs **once** in the entire
   ELF (file offset `0x4a702d` = RVA `0x4a702d`) and once in the disassembly
   (`0x227b41b`). There is no second copy that a dynamic `FindClass`/
   `GetStaticMethodID` path could use.
4. **APS2 relocations**: zero of 565,390 have addend `0x4a702d`. The 72 relocations
   into the guest→Java callback table `0x6d37bd0..0x6d37e08` carry only the
   lifecycle `*Native` names/signatures and their thunks (e.g. `0x433046` = `(J)V`,
   `0x43304b`/`0x43305e` for `(JZ)V`/`(JI)V`); `onAppShellReloadNeeded()` is not
   registered there.
5. Java side: `grep -rn "onAppShellReloadNeeded" smali/` finds **only** the method
   *definition* at `NativeGLJavaInterface.smali:204`; there is no `invoke-static`
   caller anywhere in the DEX/smali.

Caveat (stated honestly): a reader could in principle reach the BSS slot by a
*computed* address (base + offset) that never prints the literal `0x72a3020`.
No such indexing pattern exists in any observed JNI dispatch code — every cached
class/method global is accessed by its own rip-relative operand — and any
theoretical dynamic lookup would still have to reference the unique string
`0x4a702d`, which occurs only in `JNI_OnLoad`. So the conclusion holds.

Contrast — the real `gameLoadedCallback` path (for #466, not this task):
notifier `0x27eaf6e` loads the cached class `0x72a2f48` at `0x27eaf98` and the
cached method `0x72a2fc0` at `0x27eaf9f`, then calls the generic variadic thunk
`0x25848e0` at `0x27eafae`. The thunk builds a SysV `va_list`
(`gp_offset=0x18`, `fp_offset=0x30`, `movabs $0x3000000018` at `0x258494a`) and
issues `call *0x470(%rax)` at `0x258495a` = JNI slot 142 /
`CallStaticVoidMethodV`. See `analysis/callback/callback-map.md:1-133`. No
equivalent notifier exists for `0x72a3020`.

## 3. Java listener setup and the Java call path (smali line references)

Files: `/tmp/opencode/mini/out/smali/com/roblox/engine/jni/NativeGLJavaInterface.smali`,
`.../NativeGLJavaInterface$OnAppShellReloadNeededListener.smali`,
`.../com/roblox/client/ActivityNativeMain.smali`,
`.../ActivityNativeMain$b.smali`, `.../ActivityNativeMain$l.smali`, `.../uh/f.smali`.

### 3.1 Listener field and setter

- `NativeGLJavaInterface.smali:20` — `private static sAppShellReloadNeededListener :
  Ljava/lang/ref/WeakReference;` with generic signature
  `WeakReference<OnAppShellReloadNeededListener>` (`:21-27`).
- `NativeGLJavaInterface.smali:414-424` — `setAppShellReloadNeededListener(...)`:
  `new WeakReference` (`:417-419`), `sput-object` into the static field (`:421`).
  It does **not** null-check its argument; a `null` argument yields a
  `WeakReference` with a null referent (legal, and later handled).
- `OnAppShellReloadNeededListener.smali:18` — single abstract method `n()V`.
- Registration sites (both in `ActivityNativeMain`, which `.implements` the
  interface at `ActivityNativeMain.smali:12`):
  - `onCreate(Bundle)` begins at `ActivityNativeMain.smali:3904`;
    `:4200` calls `setAppShellReloadNeededListener(this)` (alongside
    `setWebviewUserAgentGetter`/`setMobileAdvertisingIdGetter` at `:4202-4204`).
  - `onDestroy()` begins at `ActivityNativeMain.smali:4722`;
    `:4764` calls `setAppShellReloadNeededListener(null)` (the `const/4 v0, 0x0`
    at `:4762`) — a weak ref with null referent, so the callback becomes a no-op.

### 3.2 The callback itself

`NativeGLJavaInterface.smali:204-221`:

```
:207 sget-object v0, sAppShellReloadNeededListener : WeakReference
:209 invoke-virtual {v0}, Ljava/lang/ref/Reference;->get()Ljava/lang/Object;
:213 check-cast v0, OnAppShellReloadNeededListener
:215 if-eqz v0, :cond_0
:217 invoke-interface {v0}, OnAppShellReloadNeededListener;->n()V
:220 return-void
```

Important: there is **no null check on the `WeakReference` itself** before
`:209`. If `setAppShellReloadNeededListener` was never called, the static field is
`null` and `:209` throws NPE. A live (non-GC'd) `ActivityNativeMain` referent is
also required, because the listener is held **weakly** — after `onDestroy` or GC
the callback silently no-ops.

### 3.3 What `n()` does (the genuine "reload")

- `ActivityNativeMain.smali:3404-3423` `n()`: `isFinishing()` guard (`:3407-3413`);
  otherwise `new ActivityNativeMain$b` (`:3416-3418`) and
  `runOnUiThread` (`:3420`).
- `ActivityNativeMain$b.smali:46-77` `run()`: second `isFinishing()` guard
  (`:51-57`); `ti/b.q()` (`:60`); `new ActivityNativeMain$l` (`:62-66`);
  `executeOnExecutor(AsyncTask.THREAD_POOL_EXECUTOR)` (`:68-74`).
- `ActivityNativeMain$l.smali:40-58` constructor logs tag `rbx.main`,
  message `"Reloading lua app."` (`:45-49`).
- `ActivityNativeMain$l.smali:62-123` `doInBackground()`: gets the `Surface` via
  `ActivityNativeMain.x2()` → `ph/a.d()` → `uh/a.I2()` (`:75-89`), then calls
  `uh/f.j().y(context, surface, activity)` (`:99-103`) or `.x(context, surface)`
  (`:108-112`) depending on `rg/a1.E0()`. `:184-213` `onPreExecute` hides the
  view; `:125-160` `onPostExecute` shows it again on failure.
- `uh/f.smali:1307-1315` `x(ctx, surface)` delegates to `y(ctx, surface, null)`;
  `uh/f.smali:1317-1351` `y()` logs tag `rbx.appshell` / `"ASMA.restart"`
  (`:1320-1324`), calls `f()`, builds params via `uh/p.a|b`, then
  `k(params)` (`:1346`, which reaches `nativeAppBridgeV2InitWithParams` at
  `uh/f.smali:840`) and `G(surface)` (`:1348`, which reaches
  `nativeAppBridgeV2StartAppWithParams` at `uh/f.smali:434`).

So the Java path is real and does a full app-shell/engine restart with a fresh
`Surface`. It is simply **never entered** from this engine build's native side.

## 4. State required for a genuine firing (all absent in a native-only harness)

Even if a guest call were somehow produced, `onAppShellReloadNeeded()` genuinely
firing requires all of:

1. A real `JNIEnv`/ART VM with `NativeGLJavaInterface` loaded from the actual
   APK DEX, so `CallStaticVoidMethod(V|A)` can resolve `onAppShellReloadNeeded()V`
   against the cached class ref `0x72a2f48` / method ID `0x72a3020`.
2. `setAppShellReloadNeededListener` invoked at least once (else NPE at smali
   `:209`) with a **live** `ActivityNativeMain` referent (weak reference).
3. That `Activity` must not be `isFinishing()`, must have a running UI `Looper`
   (`runOnUiThread`), a valid `Surface` from `x2()/ph/a.d()/uh/a.I2()`, and the
   `uh/f` restart path with `nativeAppBridgeV2InitWithParams` /
   `nativeAppBridgeV2StartAppWithParams` available.

A native-only harness (no ART, no `Activity`, no Looper, no Surface, no DEX)
satisfies none of these. The only thing a harness can do is implement a fake
`CallStaticVoidMethod*` that logs "onAppShellReloadNeeded" — that is a synthetic
event, not a guest→Java callback, and must not satisfy decision 013.

## 5. Evidence that would be required to overturn this conclusion

Any ONE of the following would be needed; none exists today:

- a native load/copy of `0x72a3020` passed as `jmethodID` to a `CallStatic*Method`
  site (would appear as a `# 72a3020` operand in `libroblox.dis`);
- a second `GetStaticMethodID` lookup of the string `0x4a702d` caching the ID in
  another global (string would then occur more than once in the disassembly);
- an APS2 relocation with addend `0x4a702d` registering the method in the
  guest→Java callback table at `0x6d37bd0..0x6d37e08`;
- a Java-side `invoke-static {…}, NativeGLJavaInterface;->onAppShellReloadNeeded()V`
  anywhere in smali/DEX;
- a runtime ART trace showing the guest itself issuing the call with a live
  registered listener and the app-shell restart executing (not reproducible in
  the native-only harness).

Until such evidence appears, the correct status is **unknown / not firing**, and
the strict gate should keep `callback=0` for this signal.

## 6. Recommendation to lead

- Do **not** gate decision 013 on `onAppShellReloadNeeded`; it is dead from the
  native side in BuildID `204ad049…`. Keep the marker withheld.
- If a genuine guest→Java callback is needed, the only candidate in this family
  with a real native notifier is `gameLoadedCallback` (`0x72a2fc0` → notifier
  `0x27eaf6e` → thunk `0x25848e0` → `CallStaticVoidMethodV`). #466 is mapping its
  real trigger; even that must be shown to arise without harness injection and
  be paired with the phase-3 render RVA before it can satisfy decision 013.
- No engine or `src/` change was made; the engine SHA-256 is unchanged.
