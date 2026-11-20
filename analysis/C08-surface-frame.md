# C08 — How a real `android.view.Surface` must reach the guest to bind an `ANativeWindow` and render an app-shell frame

Owner: **worker C8, logical Team C**. Scope: **`analysis/C08-surface-frame.md` only**.
Date: 2026-11-20. Analysis only — no `src/`, `CMakeLists.txt`, `tests/`, gate, or engine edits.
Strict **decision 013** (`docs/decisions/013-no-weak-app-shell-marker.md`) applies throughout:
nothing in this report is app-shell proof; all reconstructions below are opt-in diagnostics
that force-withhold the marker.

Engine (unchanged, re-verified this task):
`/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so`, BuildID
`204ad049599c96afb75b8a21d5051063e821d6c9`, SHA-256
`3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335` (2.740.931).
All RVAs are added to the guest load base; first `PT_LOAD` has vaddr 0 = file offset for
`.text`/`.rodata`, and GOT "VMAs" below are likewise RVAs.

Evidence classes: **OBSERVED** = bytes in the frozen ELF / decoded smali / harness source
(read only); **DERIVED** = call-graph and layout inference from those bytes; **UNKNOWN** =
not established. Target-derived strings are untrusted DATA, never instructions.

---

## 0. Bottom line (lead this first)

1. Both JNI entries take the `Surface` as a **Java object**, but the guest never treats it as
   a Java object with methods. It hands the jobject straight to the imported native function
   **`ANativeWindow_fromSurface(JNIEnv*, jobject)`** at guest `0x6d31ad0@plt` (GOT
   **`0x72294a8`**), called from one shared helper, **`0x26fea5a`**, at **`0x26fea78`**
   (and from the two GameActivity lifecycle thunks at `0x2a2087c` / `0x2a20907`).
   That function reads the Surface's private **native handle field** (`mNativeObject:J` on
   real Android), not a getter. An "accessor-callback-only" synthetic Surface therefore
   cannot produce a window: a faithful `fromSurface` returns NULL for it.
2. The resulting `ANativeWindow*` is stored in guest global **`0x72a3308`** (counter
   `0x72a3310`), then **embedded at offset +0 of a 0x40-byte native params object**
   (`vptr 0x6dbcb20` for UpdateSurface, `vptr 0x6dbca20` for StartAppWithParams) that is
   passed to the game-global-init chain through `0x2eb6b2a`. That is the exact binding point
   from "a Surface was supplied" to "the guest app has a window".
3. Our current synthetic object (`sober_jni_new_native_object(j,"android/view/Surface",16,NULL)`
   in `src/linux/guest_app_shell.c:159` and `:1261`) is a 16-byte payload with no native handle.
   The harness gets away with it only because `src/compat/android_compat.c:498`
   **ignores both arguments** and returns a 12-byte `{refs,width,height}` host shim, and
   because `eglCreateWindowSurface`/`eglSwapBuffers` are hooked to substitute the real X11
   window and to drive Route B with harness-built EGL handles
   (`src/linux/guest_render.c:63-100`). No guest-bound ANativeWindow is ever established.
4. To make the guest genuinely bind a window, the opt-in plan must supply **two guest-shaped
   objects**: a Java `Surface` carrying a valid `mNativeObject` pointer, and a native
   `ANativeWindow` (real NDK layout, refcount + vtable, backed by `SoberNativeWindow`), plus
   a consistent set of five GOT hooks. Additionally, the bridge JNIEnv is **missing the field
   slots** the guest uses to read `PlatformParams` (`GetFieldID`/`GetFloatField`/`GetIntField`),
   so those must be implemented before `0x2773194` can read real dpi/viewport values.

---

## 1. Exact JNI surface (OBSERVED)

From decoded APK smali (`/tmp/opencode/mini/out/smali/…`) and the guest's own log strings.

| Native RVA | Java declaration (smali) | JNI signature (C args after `env, clazz`) | Surface carrier |
| --- | --- | --- | --- |
| `0x2773194` | `NativeGLInterface.nativeAppBridgeV2UpdateSurfaceAppWithPlatformParams` (`NativeGLInterface.smali:78`) | `(Landroid/view/Surface;Lcom/roblox/engine/jni/model/PlatformParams;)V` → `rdx`=Surface, `rcx`=PlatformParams | jobject in `rdx` |
| `0x26fe80f` | `NativeGLInterface.nativeAppBridgeV2StartAppWithParams` (`NativeGLInterface.smali:72`) | `(Lcom/roblox/engine/jni/autovalue/StartAppParams;)V` → `rdx`=StartAppParams | `startAppParams.surface()` getter result |

Dependent strings/methods (all OBSERVED raw-byte reads, address → bytes):

| RVA | Value | Role |
| --- | --- | --- |
| `0x3fb218` | `nativeAppBridgeV2UpdateSurfaceAppWithPlatformParams` | JNI log tag at `0x2773220` |
| `0x453284` | `nativeAppBridgeV2StartApp` | JNI log tag at `0x26fe8c5` |
| `0x2df08b` | `nativeAppBridgeV2StartApp` | telemetry tag at `0x26fe8d2` |
| `0x2a4f4c` | `surface` | getter name at `0x26fe8b9` |
| `0x250e71` | `()Landroid/view/Surface;` | getter signature at `0x26fe8b0` |
| `0x217cb8` | `platformParams` | getter name at `0x26fe8de` |
| `0x2c2292` | `()Lcom/roblox/engine/jni/model/PlatformParams;` | getter signature |
| `0x2346a4` | `vrContext` | getter name at `0x26fe8fd` |
| `0x2c22c1` | `()Landroid/app/Activity;` | getter signature |
| `0x535292` / `0x46d6c0` | `dpiScale` / `F` | `GetFieldID` name/sig in `0x26feb9b` |
| `0x2c22da` / `0x532c55` | `viewportWidthMm` / `I` | `GetFieldID` name/sig |
| `0x3897f6` / `0x532c55` | `viewportHeightMm` / `I` | `GetFieldID` name/sig |
| `0x3c163c` | `[FLog::JNIAppBridge] {}: Created ANativeWindow {} (ID:{})` | success log, `0x26feaf7` |
| `0x46f987` | `[FLog::JNIAppBridge] {}: Same native window {}. No need to do anything!` | no-op log, `0x26feb58` |
| `0x43554f` / `0x3327b7` | `nativeAppBridgeV2PauseApp/PauseGame: Destroying ANativeWindow at {}` | release logs `0x2ebc91d` / `0x2ebec26` |
| `0x2c426d` | `[FLog::SurfaceController] SurfaceController[{}:{}]::update.` | `0x2773443`, in the params-consuming function |

`com/roblox/engine/jni/model/PlatformParams.smali:7-19` declares real **public fields**
`dpiScale:F`, `viewportWidthMm:I`, `viewportHeightMm:I` (plus unrelated touch flags) — note the guest
uses `GetFieldID`/`Get*Field`, **not** getter methods, for PlatformParams (§2.4).

`StartAppParams.smali:56` declares `surface()Landroid/view/Surface;` as an abstract getter; the
guest reads it with `GetMethodID` + `CallObjectMethodV` (§2.2).

---

## 2. RVA trace: Surface → ANativeWindow → guest app

### 2.1 `0x2773194` — UpdateSurfaceAppWithPlatformParams (OBSERVED, `fn_UpdateSurface.dis`)

| RVA | Instruction / action | Meaning (DERIVED) |
| --- | --- | --- |
| `0x27731a8/ab/ae` | `rbx=rcx` (PlatformParams), `r15=rdx` (Surface), `r14=rdi` (env) | argument split |
| `0x2773220-0x277321b` | crash-guard prologue (global `0x72a3268`, tag `0x3deb31`) | standard JNI guard |
| **`0x277322d`** | `call 0x26fea5a` with `(rdi=env, rsi=Surface)` | **bind ANativeWindow from the Surface** |
| `0x2773232` | `r13 = [0x72a3308]` | read the newly bound window |
| **`0x2773246`** | `call 0x26feb9b(&local, env, PlatformParams)` | parse PlatformParams into native struct |
| `0x277324b` | `local+0x00 = r13` (ANativeWindow*) | window embedded in native params object |
| `0x277328a-0x27732c1` | alloc `0x40`; `vptr=0x6dbcb20`; copy local (`+0x08..+0x38`) into it | native `PlatformParams` object |
| `0x27732cc-0x27732d5` | guard at `rbp-0xa0`; `guard+0x20 = native params`; `call 0x2eb6b2a` | dispatch into game-global-init with the window+params |

The second, separate function beginning at `0x27733dc` is the params consumer
(`[FLog::SurfaceController]…::update.` at `0x2773443`): it calls virtual slots
`*0x1b8`, `*0xd8` (dpiScale as float), `*0xe8` (width/height ints), `*0xf0` on the object at
`rbx+0x40`, after a branch through jump table `0x6709dc` keyed by a mode at `r15+0x14`.
This is where dpi/viewport values reach the surface controller.

### 2.2 `0x26fe80f` — StartAppWithParams (OBSERVED, `fn_26fe80f.txt` / `fn_StartAppWithParams.dis`)

| RVA | Instruction / action | Meaning (DERIVED) |
| --- | --- | --- |
| `0x26fe823` | `r14=rdx` (StartAppParams jobject), `rbx=rdi` (env) | argument split |
| `0x26fe8a0` | `call *0xf8(%rax)` on env vtable = **`GetObjectClass(startAppParams)`** → `r15` | JNI index 31 |
| `0x26fe8bd` | `call 0x247b465(env, r15, r14, "surface"@0x2a4f4c, "()Landroid/view/Surface;"@0x250e71)` → Surface jobject | generic getter helper |
| **`0x26fe8d2`** | `call 0x26fea5a(env, Surface)` | **bind ANativeWindow** |
| `0x26fe8d7` | `r13 = [0x72a3308]` (after the call) | read bound window |
| `0x26fe8e5/0x26fe904` | same helper reads `platformParams()` / `vrContext()` → `r12` / `r14` | autovalue getters |
| `0x26fe938` | `call 0x26feb9b(&local, env, r12)` | parse PlatformParams fields |
| `0x26fe93d` | `local+0x00 = r13` (window), `0x26fe940` `local+0x20 = r14` (vrContext) | window into native object |
| `0x26fe98a-0x26fe9c1` | alloc `0x40`; `vptr=0x6dbca20`; copy local into `+0x08..+0x38` | native `StartAppParams` object |
| `0x26fe9d5` | guard; `guard+0x20 = native params`; `call 0x2eb6b2a` | dispatch into app start |

Getter helper **`0x247b465`** (OBSERVED): if `jclass==NULL`, `GetObjectClass` (env `+0xf8`);
then `GetMethodID` (env `+0x108`, index 33), then tail-`jmp 0x2315d18`, which builds a SysV
`va_list` (`gp_offset=0x18`, `fp_offset=0x30`, `movabs $0x3000000018`) and calls
`*0x118(%rax)` = JNI index 35 = **`CallObjectMethodV`**. So `surface()` is a genuine Java
method call through the bridge.

### 2.3 Shared helper `0x26fea5a` — the actual Surface→ANativeWindow conversion (OBSERVED)

Signature: `void 0x26fea5a(JNIEnv *env, jobject surface)` (2 args; `rdx` = Surface, saved to
`r14` at `0x26fea67`). Full relevant body:

```
26fea78  call 6d31ad0 <ANativeWindow_fromSurface@plt>   ; rdi=env, rsi=surface (UNCHANGED)
26fea7d  mov  %rax,%rbx                                  ; new ANativeWindow*
26fea80  mov  [0x72a3308],%rdi                           ; old window
26fea87  cmp  %rdi,%rax
26fea8a  je   26feb16                                    ; same -> 26feb16
26fea90  incl [0x72a3310]                                ; window-generation counter
26fea96  test %rdi,%rdi
26fea99  je   26feaa0
26fea9b  call 6d31ac0 <ANativeWindow_release@plt>        ; release old
26feaa0  mov  %rbx,[0x72a3308]                           ; publish new window
26feaf7  lea  0x3c163c,%rcx ... call 0x232de10           ; "Created ANativeWindow {} (ID:{})"
26feb16  ...  lea 0x46f987 ... call 0x232de10            ; "Same native window. No need ..."
26feb75  test %rbx,%rbx; je 26feb82
26feb7d  call 6d31ac0 <ANativeWindow_release@plt>        ; drop ref from "same" fromSurface call
26feb82  stack-check; ret
```

Consequences (DERIVED):
- The guest **requires `ANativeWindow_fromSurface` to return a valid, refcounted
  `ANativeWindow*`**. It immediately calls `ANativeWindow_release` on the previous value and
  may call it again on the `same-window` branch; the pointer must survive those calls.
- Globals: window `0x72a3308`; generation counter `0x72a3310` (logged as `ID`).
- This helper is called from **three** places: `0x277322d` (UpdateSurface), `0x26fe8d2`
  (StartAppWithParams), `0x2ebd5dd` (StartGameWithParam path, string `0x4df87a`).

### 2.4 Shared helper `0x26feb9b` — PlatformParams parser (OBSERVED)

`void 0x26feb9b(out_struct*, JNIEnv *env, jobject platformParams)`:

```
26febb2-26febca  zero out[0..0x17], out[0x20]=0; out[0x18]=1; out[0x28]=0; out[0x2c]=0
26febd7  call *0xf8(%rax)                       ; GetObjectClass(platformParams) -> jclass r12
26febf0  call 0x24ac975(env, r12, params, "dpiScale"@0x535292)
             -> env+0x2f0 = GetFieldID(name, "F"@0x46d6c0)
             -> env+0x330 = GetFloatField  ; stores float at out+0x08
26fec0a  call 0x23846d4(env, r12, params, "viewportWidthMm"@0x2c22da)
             -> env+0x2f0 = GetFieldID(name, "I"@0x532c55)
             -> env+0x320 = GetIntField    ; stores int at out+0x0c
26fec22  call 0x23846d4(..., "viewportHeightMm"@0x3897f6) ; stores int at out+0x10
```

Native `PlatformParams` destination (before the caller installs vptr `0x6dbcb20`):
`+0x00 = ANativeWindow*`, `+0x08 = float dpiScale`, `+0x0c = int viewportWidthMm`,
`+0x10 = int viewportHeightMm`, `+0x18 = 1`, `+0x28/+0x2c = 0`.

### 2.5 Downstream consumers of global `0x72a3308` (OBSERVED)

| Xref RVA | Use |
| --- | --- |
| `0x2ebc8fb`, `0x2ebec04` | log "Destroying ANativeWindow at {}", then `ANativeWindow_release` and zero it (PauseApp / PauseGame) |
| `0x2ebd5e2` | copied to local `-0x428(%rbp)` in the StartGameWithParam path |
| `0x2ebed4b`, `0x2ebef4d` | loaded into `r13` for the same path |
| embedded at `native_params+0x00` (§2.1/§2.2) | passed through `0x2eb6b2a` into the app object |

There is **no direct read of `0x72a3308` in the `0x649c…` EGL cluster**; the render code receives
its window as an argument (`0x649c21e(native_window, extra)` → `0x649c272` →
`eglCreateWindowSurface`, `render-map.md §3`). The app-shell frame therefore depends on the
window embedded in the app/params object and on the GameActivity lifecycle slot below.

### 2.6 Alternate (real Android) lifecycle path (OBSERVED)

`0x2a20850` = `onSurfaceCreatedNative(JLandroid/view/Surface;)V`, `0x2a208c0` =
`onSurfaceChangedNative(JLandroid/view/Surface;III)V`, `0x2a20a00` =
`onSurfaceRedrawNeededNative(JLandroid/view/Surface;)V` (callback table `0x6d37cd8/0x6d37cf0/
0x6d37d08`, `jni-map.md §2`, `render-map.md §4`).

```
2a20850: old window at [nativeData+0x140] -> ANativeWindow_release (0x2a2086b)
2a20875: rdi=[nativeData+0x10] (JNIEnv), rsi=Surface jobject
2a2087c: call ANativeWindow_fromSurface@plt -> [nativeData+0x140]
2a2088d: call *[nativeData+0x88](nativeData, window)   ; engine surface-created callback
2a20907: same in onSurfaceChanged; 2a20963/2a20975/2a20985/2a20993
         = ANativeWindow_getWidth/getHeight (PLT 0x6d32120 / 0x6d32110)
```

This is the path Android actually uses. It is Java→native and requires a live GameActivity;
a native-only harness can only drive it by calling the thunks with a real Surface jobject.

### 2.7 GOT slots needed (OBSERVED, `readelf -rW`)

| Import | PLT | GOT (add guest base) | Used at |
| --- | --- | --- | --- |
| `ANativeWindow_fromSurface` | `0x6d31ad0` | **`0x72294a8`** | `0x26fea78`, `0x2a2087c`, `0x2a20907` |
| `ANativeWindow_release` | `0x6d31ac0` | **`0x72294a0`** | `0x26fea9b`, `0x26feb7d`, `0x2a2086b`, pause paths |
| `ANativeWindow_acquire` | `0x6d32100` | `0x72297c0` | `0x649c2c2..0x649c2db` (Route A) |
| `ANativeWindow_getHeight` | `0x6d32110` | `0x72297c8` | `0x2a20975`, `0x2a20993`, `0x649c30f` |
| `ANativeWindow_getWidth` | `0x6d32120` | `0x72297d0` | `0x2a20963`, `0x2a20985`, `0x649c303` |

---

## 3. Why the current synthetic Surface cannot bind a window

1. **`ANativeWindow_fromSurface` is not a Java getter.** On Android it is an NDK function in
   `libandroid.so` that resolves `android.view.Surface`'s private native-handle field
   (`mNativeObject:J` in the decoded platform) to an `android::Surface*` (an `ANativeWindow`
   subclass with a live `IGraphicBufferProducer`), takes a strong ref, and returns it as
   `ANativeWindow*`. It does **not** call `surface()`-style accessors. A synthetic object whose
   only behavior is JNI accessor callbacks has no such field/valid pointer, so a faithful
   implementation returns NULL. (Implementation detail of the Android library is
   **INFERRED** from NDK semantics; the guest-side requirement — a valid returned pointer —
   is **OBSERVED** in §2.3.)
2. **What we actually supply today** (OBSERVED harness source, read only):
   - `guest_app_shell.c:159` returns a 16-byte `android/view/Surface` payload from the
     `start_app_get` `"surface"` handler; `guest_app_shell.c:1261` creates another for the
     `0x2773194` call; neither contains a native handle. The log line
     `guest->Java callback: surface() -> %p` is exactly the "accessor callbacks only" symptom.
   - `src/compat/android_compat.c:498-511` ignores `(env, surface)` and `calloc`s a 12-byte
     `{unsigned refs; int32_t width, height;}`; that is not an `ANativeWindow` (no magic,
     vtable, or buffer ops). `guest_app_shell.c`'s window works only because
     `src/linux/guest_render.c:63-67` installs `sober_render_hook_egl_create_window_surface`
     (which substitutes the real X11 handle) and because the deterministic frame uses
     `render-map.md` Route B with harness-built EGL handles (`guest_render.c:103-120`).
   - The existing `sober_render_hook_anative_window_from_surface()` API
     (`src/render/native_render.h:144-149`, implementation `native_render.c:972-985`) is
     **defined but never installed** in `src/linux/guest_render.c` — grep finds only the EGL
     and swap hooks. So the guest's `fromSurface` GOT still resolves to the dumb compat shim.
3. **Missing JNIEnv field slots (OBSERVED).** The guest reads `PlatformParams` through
   `GetFieldID`/`GetFloatField`/`GetIntField` at env offsets `0x2f0/0x330/0x320`
   (§2.4). The bridge table (`src/jni/native_jni.c:774-824`) sets `Call*Method` and
   `GetMethodID` but **not** the field accessors (standard indices 94/100/101/102 → this
   table's `SETFN` 90/96/97/98); those remain `sr_stub` (`:337-341`, returns NULL). With a
   synthetic PlatformParams the guest therefore reads zeros for dpi/viewport. Any opt-in
   reconstruction must add `GetFieldID`, `GetIntField`, `GetFloatField`, and (for a JNI-emulated
   `fromSurface`) `GetLongField`.
4. **Consequence:** today's `0x2773194`/`0x26fe80f` calls log `Created ANativeWindow …` with a
   fake pointer, embed it in the native params object, and no guest-bound drawable exists.
   Under decision 013 this is plumbing only; the marker is correctly withheld.

---

## 4. Opt-in reconstruction plan (guest-shaped objects) — NOT enacted

All items are default-OFF diagnostics. Per existing policy
(`decision-memo-alt-thunk-vs-013.md §2.3`, `guest_app_shell.c:1230-1246`) a new seed env var
must be added to the `seeded_state` force-withhold list; this plan therefore **cannot** turn
the `app shell rendered` marker green. Lead must approve any code change and any gate policy
change separately.

### Phase A — guest-shaped native `ANativeWindow`
1. Extend `SoberNativeWindow` (`src/render/native_render.h`) with a leading **real NDK
   `ANativeWindow` header** (common base: `magic`, `version`, `reserved[2]`, `incRef`,
   `decRef`; `flags`, `handles`, `format`, `min/maxSwapInterval`; the `setSwapInterval`,
   `dequeueBuffer`, `lockBuffer`, `queueBuffer`, `cancelBuffer`, `query`, `connect`,
   `perform` slots) followed by the existing X11/Wayland handle and EGL state. Back the
   buffer ops with `sober_render_anw_lock`/`unlock_and_post` (already implemented in
   `native_render.c`) so `eglCreateWindowSurface` can be given the object directly.
2. Implement bridge-side `ANativeWindow_acquire/release/getWidth/getHeight` wrappers that
   operate on **this** object (refcount in the header), not on the 12-byte compat struct.
3. Install the five GOT hooks at `guest_base + 0x72294a8`, `0x72294a0`, `0x72297c0`,
   `0x72297c8`, `0x72297d0` with `mprotect` (same pattern as `guest_render.c:78-92` for
   `eglSwapBuffers`). Hook **all five together**; hooking only `fromSurface` would send our
   object into the compat `ANativeWindow_release` at `0x26fea9b` and corrupt/free it.

### Phase B — guest-shaped Java `Surface`
4. Extend `sober_jni_new_native_object` payload for class `android/view/Surface` to a layout
   with a `mNativeObject:J` field holding `(jlong)(uintptr_t)` of the Phase-A window object.
   Register the class/field in `FindClass`/`GetFieldID` so a faithful `fromSurface`
   emulation (`GetFieldID("mNativeObject","J")` → `GetLongField`) succeeds; keep
   `IsInstanceOf(surface, android/view/Surface)` consistent.
   Alternative (no real ART): keep the existing PLT hook at `0x72294a8` and have it return the
   Phase-A object while **ignoring** the jobject — document clearly that the Surface is then
   only a token, not a real Android Surface. This is the current design intent of
   `native_render.h:146`, but it does not prove a real Surface was supplied.

### Phase C — guest-shaped `PlatformParams` / `StartAppParams`
5. Implement `GetFieldID` (SETFN 90), `GetIntField` (96), `GetFloatField` (98) and
   `GetLongField` (97) in `src/jni/native_jni.c`; back them with the 0x40-byte native shapes
   from `analysis/native-object-layouts.md` (vptr `0x6dbcb20` for PlatformParams,
   `0x6dbca20` for StartAppParams). Serve `dpiScale:F`, `viewportWidthMm:I`,
   `viewportHeightMm:I` with non-zero, environment-derived values (e.g. 1280×720, dpi 2.0).
6. Keep `StartAppParams.surface()` returning the **same** Phase-B object in the
   `start_app_get` handler (`guest_app_shell.c:159`); all ten getters must be correctly typed
   (`analysis/lua-app-start.md §5`).

### Phase D — wiring and call order
7. New opt-in, e.g. `SOBER_SURFACE_FRAME=1`, performing the Java-order replay from
   `analysis/lua-app-start.md §6`, steps 6–10:
   `0x24abf96` (InitParams) → `0x247dc8d`/`0x24a8e44` (app start) →
   `0x26fe80f(env, clazz, startAppParams)` → `0x25448f6` (StartLuaAppDM) →
   `0x2773194(env, clazz, surface, platformParams)`.
   Additionally call lifecycle thunks `0x2a20850` and `0x2a208c0` with the same Surface to
   populate `nativeData+0x140` (§2.6), which is the slot the real Android surface-created
   callback uses.
8. After each call, read back guest global `0x72a3308` and generation `0x72a3310` and log
   them; require `[FLog::JNIAppBridge] Created ANativeWindow 0x… (ID:n)` with a **Phase-A**
   pointer before proceeding.
9. Then call guest `0x649c21e(window, extra=1)` (Route A, render-map §5) so the guest itself
   creates the EGL window surface, or continue Route B while logging that the window was
   embedded in `native_params+0x00`. Verify guest swap `0x649d422` → `eglSwapBuffers@plt`
   `0x6d334d0` count via the existing hook.

### Phase E — verification and honesty
10. Diagnostics to log: hook installs, `fromSurface` return pointers, window global before/
    after, `GetFieldID`/`Get*Field` values, phase-3 guest swap count, and whether
    `0x2eb6b2a`/`0x27733dc` were reached.
11. **Decision 013:** every one of these objects is synthetic; a green "surface bound" log is
    evidence of JNI plumbing and guest window binding only. It must not emit `app shell
    rendered`. The marker still requires a genuine guest-driven `gameLoadedCallback(J)V`
    (notifier `0x27eaf6e`, cached ID `0x72a2fc0`, thunk `0x25848e0`) — or a genuine
    `onAppShellReloadNeeded()V`, which `analysis/jni/appshell-reload-trigger.md` rules out for
    a native-only harness — **plus** phase-3 render `0x649d422`, with all seeds off.
12. No guest bytes are patched; hooks are installed in the harness's own GOT copies at load
    time. New env knob must join the force-withhold list. Engine SHA/BuildID re-verified after.

---

## 5. Limits and unknowns (honest)

- The precise internal implementation of Android's `ANativeWindow_fromSurface` (field names,
  validation, refcount) is **INFERRED** from NDK semantics; it is an imported symbol, not guest
  bytes. The guest-side contract in §2.3 is OBSERVED.
- The 0x40-byte native params layouts are DERIVED from the copy sequences in `0x2773194` /
  `0x26fe80f` and the parser `0x26feb9b`; field offsets beyond those named are not fully
  established. The vptrs (`0x6dbcb20`, `0x6dbca20`) are OBSERVED.
- No runtime test was run by this worker (analysis only). Route A's acceptance of our shaped
  `ANativeWindow` by the host EGL remains **UNKNOWN**; `render-map.md §6` already notes the
  existing Route A fault at `0x649cf9a`.
- Whether the app-shell frame actually consumes `0x72a3308` versus `nativeData+0x140` as its
  drawable is **UNKNOWN**; both paths are mapped in §2.5/§2.6 and should be seeded together.
- `onAppShellReloadNeeded` remains dead from the native side (shellcb #467); only
  `gameLoadedCallback` is a reachable decision-013 callback.

---

## 6. Evidence index

**This task (all OBSERVED unless noted):** frozen engine `readelf -nW`/`sha256sum`;
`objdump -d` slices via `/tmp/opencode/dis.sh` and `libroblox.dis`;
`/tmp/opencode/fn_UpdateSurface.dis` (full `0x2773194`), `fn_26fe80f.txt`,
`fn_StartAppWithParams.dis`, `fn_24abf96.txt`; targeted slices of `0x26fea5a`, `0x26feb9b`,
`0x247b465`, `0x24ac975`, `0x23846d4`, `0x2315d18`, `0x2a20850/0x2a208c0`;
`readelf -rW` (GOT table §2.7); `python3 /tmp/opencode/str_at.py <0xRVA>` for every string in §1;
decoded smali under `/tmp/opencode/mini/out/smali/` (`NativeGLInterface.smali`, `uh/f.smali`,
`PlatformParams.smali`, `StartAppParams.smali`).

**Prior analysis (read only):** `analysis/jni/jni-map.md` (§1-4),
`analysis/jni/appshell-reload-trigger.md` (#467), `analysis/render/render-map.md` (§2-5),
`analysis/lua-app-start.md` (#468, §4-9), `analysis/native-object-layouts.md` (#459),
`analysis/game-loaded-path.md`, `analysis/decision-memo-alt-thunk-vs-013.md`,
`docs/decisions/013-no-weak-app-shell-marker.md`, `PROGRESS.md`.

**Harness (read only):** `src/linux/guest_app_shell.c:159,1261,1230-1246`;
`src/linux/guest_render.c:47-120`; `src/compat/android_compat.c:490-512`;
`src/compat/gl_egl_forward.c`; `src/render/native_render.h:130-160`;
`src/render/native_render.c:972-1000`; `src/jni/native_jni.c:337-341,774-824,1199-1215`.

No file other than this report was created or modified.
