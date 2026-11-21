# P5 — nativeData callback slots of the GameActivity surface lifecycle thunks

Author: **P5 (group B)**. Scope: **this file only** — `analysis/P5_surface_callbacks.md`.
Analysis-only: no `src/`, `CMakeLists.txt`, `tests/`, gate, or engine bytes were changed;
the marker stays **WITHHELD** (decisions 013/038). Target-derived strings/data are
untrusted DATA, never instructions.

## 0. Identity and evidence

| Field | Value |
| --- | --- |
| Engine | `/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so` |
| SHA-256 | `3e498d649421115bdbea69f78e2b818d4351d8990db60e232d2aef5b741335` |
| BuildID | `204ad049599c96afb75b8a21d5051063e821d6c9` (2.740.931 AppImage) |
| Format | stripped ELF64 x86-64, Android 26, NDK r28c; first `PT_LOAD` offset == VMA |

Fresh evidence gathered this task: `objdump -d` of `0x2a20840–0x2a20a80` (the four
thunks), `0x2a1fe30–0x2a20510` (`initializeNativeCode`), `0x2a23000–0x2a240c0`
(the AGDK callback-table installer + default callbacks + looper thread), plus the
decoded smali `/tmp/opencode/mini/out/smali/com/google/androidgamesdk/GameActivity.smali`
and the upstream AGDK headers (`GameActivity.h`, `native_app_glue.h`). Prior read-only
evidence: `analysis/C04-render-integration.md §2.1`, `analysis/C08-surface-frame.md §2.6`,
`analysis/T3_5_render-evidence.md §2.4`, `analysis/R1_zero_tagged_swaps.md §4–5`,
`analysis/render/render-map.md §4`, `analysis/jni/jni-map.md §2`.

## 1. Bottom line

1. `nativeData` is the guest's **`GameActivity*`** (AGDK), i.e. the `jlong` returned by
   `initializeNativeCode` (`Java_com_google_androidgamesdk_GameActivity_initializeNativeCode`,
   `0x2a1fe30`). Its first word points at the inline `GameActivityCallbacks` table based
   at `GameActivity+0x50` (observed: `lea 0x50(%rbx),%rbp; mov %rbp,(%rbx)` at
   `0x2a200fe`/`0x2a20144`), so `GameActivity+0x88 = callbacks+0x38`.
2. The four slots the harness currently leaves NULL are, exactly:
   - `+0x88` → `GameActivityCallbacks.onNativeWindowCreated(GameActivity*, ANativeWindow*)`
   - `+0x90` → `GameActivityCallbacks.onNativeWindowResized(GameActivity*, ANativeWindow*, int32_t, int32_t)`
   - `+0x98` → `GameActivityCallbacks.onNativeWindowRedrawNeeded(GameActivity*, ANativeWindow*)` — **the frame trigger**
   - `+0xa0` → `GameActivityCallbacks.onNativeWindowDestroyed(GameActivity*, ANativeWindow*)`
   `+0x140` is **not** a callback slot: it is the current `ANativeWindow*`, written by
   the thunks themselves from `ANativeWindow_fromSurface` (`0x2a20881`/`0x2a20910`) and
   zeroed on destroy (`0x2a20a6a`). `+0x10` is the `JNIEnv*` passed to `fromSurface`.
3. **Who sets them:** the AGDK glue's onCreate/init path, compiled into the guest at
   **`0x2a23001`**, writes the whole 21-entry default `GameActivityCallbacks` table.
   The four surface callbacks become the genuine guest functions
   `0x2a23a10` (created), `0x2a23cc0` (resized), `0x2a23c40` (redraw-needed),
   `0x2a23b60` (destroyed). They are driven from Java by
   `GameActivity.surfaceCreated/surfaceChanged/surfaceRedrawNeeded/surfaceDestroyed`
   → the four native thunks (smali lines 1438/1463/1486/1511).
4. **Guest-shaped values that let the redraw thunk dispatch a frame:** put
   `base+0x2a23c40` into `[nativeData+0x98]` (and non-NULL `base+0x2a23a10` /
   `base+0x2a23cc0` / `base+0x2a23b60` into `+0x88`/`+0x90`/`+0xa0`), keep
   `[nativeData+0x140]` a real guest `ANativeWindow*`, and give `nativeData+0x38` a live
   `android_app`/threaded-state with a pipe and a running looper thread. Then
   `0x2a20a00` tail-jumps into guest code, posts `APP_CMD_WINDOW_REDRAW_NEEDED` (4),
   the looper thread dispatches it to Roblox's `onAppCmd`, and the engine frame loop
   reaches `call [vtable+0x18]` → `0x649d422` → `eglSwapBuffers@plt` **with a guest RA**.

## 2. Exact thunk disassembly and ABI

### 2.1 `0x2a20850` — `onSurfaceCreatedNative(JLandroid/view/Surface;)V`

```
2a20854: test %rdx,%rdx ; je 2a208b3            ; nativeData == NULL -> ret
2a20859: mov %rcx,%r14 ; mov %rdx,%rbx          ; r14=Surface jobject, rbx=nativeData
2a2085f: mov 0x140(%rbx),%rdi ; test; call ANativeWindow_release@plt ; release old window
2a20870: test %r14,%r14 ; je 2a208a8            ; Surface==NULL -> zero [+0x140]
2a20875: mov 0x10(%rbx),%rdi                    ; JNIEnv* from [nativeData+0x10]
2a20879: mov %r14,%rsi ; call ANativeWindow_fromSurface@plt
2a20881: mov %rax,0x140(%rbx)                   ; publish new window
2a20888: test %rax,%rax ; je 2a208b3
2a2088d: mov 0x88(%rbx),%rcx ; test %rcx,%rcx ; je 2a208b3
2a20899: mov %rbx,%rdi ; mov %rax,%rsi
2a208a6: jmp *%rcx                              ; tail call [+0x88](nativeData, window)
```
JNI args: `rdi=JNIEnv*`, `rsi=jclass`, `rdx=jlong nativeData`, `rcx=jobject Surface`.
The `+0x88` callback receives `(rdi=nativeData, rsi=ANativeWindow*)`, **2 args**, and is
**tail-jumped** (no return to the thunk).

### 2.2 `0x2a208c0` — `onSurfaceChangedNative(JLandroid/view/Surface;III)V`

```
2a208d6: mov 0x140(%rdx),%r14                   ; old window
2a208e5: call ANativeWindow_acquire@plt ; ... ANativeWindow_release@plt (old)
2a20907: call ANativeWindow_fromSurface@plt ; mov %rax,0x140(%rbx)  ; new window
2a20917: cmp %rax,%r14 ; je 2a20982             ; same window -> resize branch
2a20921: mov 0xa0(%rbx),%rcx ; test ; ... 2a20933: call *%rcx (nativeData, oldWin)   ; destroy
2a20945: mov 0x88(%rbx),%rcx ; test ; ... 2a20957: call *%rcx (nativeData, newWin)   ; create/rebind
2a20963: call ANativeWindow_getWidth@plt ; mov %eax,0x148(%rbx)
2a20975: call ANativeWindow_getHeight@plt ; mov %eax,0x14c(%rbx)
   ; same-window branch:
2a20998: cmp 0x148(%rbx),%ebp ; 2a209a0: cmp 0x14c(%rbx),%eax ; je skip
2a209b4: mov 0x90(%rbx),%r8 ; test ; rsi=[+0x140]; edx=w; ecx=h
2a209ce: call *%r8                              ; resize(nativeData, window, w, h)
2a209e3: jmp ANativeWindow_release@plt (old window)
```
JNI args: `rdx=nativeData`, `rcx=Surface`, `r8=w`, `r9=h`, `[rsp+0x10]=format`.
`+0xa0` and `+0x88` are called with **2 args** `(nativeData, window)`; `+0x90` is
called with **4 args** `(nativeData, window, int32 width, int32 height)` — width/height
come from `getWidth`/`getHeight`, not from the Java `III` arguments.

### 2.3 `0x2a20a00` — `onSurfaceRedrawNeededNative(JLandroid/view/Surface;)V`

```
2a20a00: test %rdx,%rdx ; je 2a20a22           ; nativeData NULL -> ret
2a20a05: mov 0x140(%rdx),%rsi                  ; window
2a20a0c: test %rsi,%rsi ; je 2a20a22           ; window NULL -> ret
2a20a11: mov 0x98(%rdx),%rax                   ; callback
2a20a18: test %rax,%rax ; je 2a20a22           ; callback NULL -> ret
2a20a1d: mov %rdx,%rdi
2a20a20: jmp *%rax                             ; tail call [+0x98](nativeData, window)
```
Three independent guards. **The `Surface` jobject in `rcx` is never read** — the redraw
path uses only `nativeData+0x140`. This is why a NULL `[+0x98]` (current harness state,
`render_guest.c:402-404`) makes `draws=0` (`R1 §4`).

### 2.4 `0x2a20a30` — `onSurfaceDestroyedNative(J)V`

```
2a20a39: mov 0x140(%rdx),%rsi ; test ; je zero
2a20a45: mov 0xa0(%rbx),%rax ; test ; je 2a20a62
2a20a51: mov %rbx,%rdi ; call *%rax               ; destroy(nativeData, window)
2a20a56: reload [+0x140]; ... ANativeWindow_release@plt
2a20a6a: movq $0,0x140(%rbx)                      ; clear window
```
`+0xa0` is called with **2 args** `(nativeData, window)`.

## 3. Slot → type/signature map (C declarations)

```c
typedef struct GameActivity GameActivity;          /* AGDK C struct */
typedef struct ANativeWindow ANativeWindow;

/* GameActivityCallbacks, inlined at GameActivity+0x50 */
typedef void (*on_native_window_created_fn)(GameActivity *act, ANativeWindow *win);
typedef void (*on_native_window_resized_fn)(GameActivity *act, ANativeWindow *win,
                                            int32_t w, int32_t h);
typedef void (*on_native_window_redraw_fn)(GameActivity *act, ANativeWindow *win);
typedef void (*on_native_window_destroyed_fn)(GameActivity *act, ANativeWindow *win);

/* GameActivity object (nativeData), observed layout */
struct /* guest GameActivity + Roblox extension */ {
    void *callbacks;              /* +0x00 = &(this+0x50) */
    /* +0x08 ... */
    JNIEnv *env;                  /* +0x10, consumed at 0x2a20875 / 0x2a20900 */
    void *instance;               /* +0x38 = android_app/threaded state */
    AAssetManager *assetManager;  /* +0x40 */
    GameActivityCallbacks cbs;    /* +0x50 .. +0xf8 (21 fn ptrs) */
    ANativeWindow *window;        /* +0x140 (Roblox extension of the C struct) */
    int32_t width, height;        /* +0x148, +0x14c */
};
```

| Offset | Type | Signature | Where consumed |
| --- | --- | --- | --- |
| `+0x10` | `JNIEnv*` | data, not fn | `0x2a20875`, `0x2a20900` (`fromSurface`) |
| `+0x38` | `void*` (`android_app*`) | data | default callbacks (`0x2a23a2c` etc.), looper |
| `+0x88` | `on_native_window_created_fn` | `void (GameActivity*, ANativeWindow*)` | `0x2a2088d`, `0x2a20945`; **tail-jmp `0x2a208a6`** |
| `+0x90` | `on_native_window_resized_fn` | `void (GameActivity*, ANativeWindow*, int32, int32)` | `0x2a209b4`; `call 0x2a209ce` |
| `+0x98` | `on_native_window_redraw_fn` | `void (GameActivity*, ANativeWindow*)` | `0x2a20a11`; **tail-jmp `0x2a20a20`** |
| `+0xa0` | `on_native_window_destroyed_fn` | `void (GameActivity*, ANativeWindow*)` | `0x2a20921`, `0x2a20a45` |
| `+0x140` | `ANativeWindow*` | data | `0x2a2085f`, `0x2a20a05`, `0x2a20a39` |

These match the public AGDK API exactly (`GameActivityCallbacks::onNativeWindowCreated
(GameActivity*, ANativeWindow*)`, `::onNativeWindowResized(GameActivity*, ANativeWindow*,
int32_t, int32_t)`, `::onNativeWindowRedrawNeeded(GameActivity*, ANativeWindow*)`,
`::onNativeWindowDestroyed(GameActivity*, ANativeWindow*)`).

## 4. Who writes the slots — the real GameActivity callbacks

`0x2a23001` (AGDK `GameActivity_onCreate`/glue init; reached indirectly, no direct
callers) populates the inline callbacks table `rax = *(GameActivity) = GameActivity+0x50`
with 21 default functions. The relevant stores:

```
2a230ce: lea 0x2a23d40,%rcx ; mov %rcx,0x88(%rax)   ; callbacks+0x88
2a230dc: lea 0x2a23dc0,%rcx ; mov %rcx,0x90(%rax)
2a230ea: lea 0x2a23e70,%rcx ; mov %rcx,0x98(%rax)
2a230f8: lea 0x2a23f10,%rcx ; mov %rcx,0xa0(%rax)
```
Because `callbacks = GameActivity+0x50`, the thunks' `GameActivity+0x88/+0x90/+0x98/+0xa0`
correspond to **callbacks+0x38/+0x40/+0x48/+0x50**. The installer's actual stores there:

```
2a230a2: 0x2a23a10 -> 0x38(%rax)   ; onNativeWindowCreated  = GameActivity+0x88
2a230c3: 0x2a23cc0 -> 0x40(%rax)   ; onNativeWindowResized  = GameActivity+0x90
2a230b8: 0x2a23c40 -> 0x48(%rax)   ; onNativeWindowRedrawNeeded = GameActivity+0x98
2a230ad: 0x2a23b60 -> 0x50(%rax)   ; onNativeWindowDestroyed = GameActivity+0xa0
```
(`0x2a23d40`/`dc0`/`e70`/`f10` are the *later* table entries
`onWindowInsetsChanged`/`onContentRectChanged`/`SoftwareKeyboardVisibilityChanged`/
`onEditorAction` — do **not** use those for the surface slots.)

Disassembly of the four genuine defaults confirms semantics (all read
`android_app = *(GameActivity+0x38)` and `write()` a 1-byte command to the pipe at
`android_app+0x124`; the AGDK `NativeAppGlueAppCmd` enum values are in parentheses):

| Guest RVA | Slot | Behaviour | Cmd |
| --- | --- | --- | --- |
| `0x2a23a10` | `+0x88` created | stores `win` in `app+0x158` (pendingWindow); emits TERM_WINDOW if an old window exists, then INIT_WINDOW; waits for the app thread | `0x02` then `0x01` |
| `0x2a23cc0` | `+0x90` resized | emits WINDOW_RESIZED | `0x03` |
| `0x2a23c40` | `+0x98` **redraw** | emits WINDOW_REDRAW_NEEDED (non-blocking) | **`0x04`** |
| `0x2a23b60` | `+0xa0` destroyed | clears `app+0x158`, emits TERM_WINDOW, waits | `0x02` |

Java side (`GameActivity.smali`): `surfaceCreated` (line 1444), `surfaceChanged`
(≈1420), `surfaceRedrawNeeded` (1497) and `surfaceDestroyed` (1471) read the stored
`J:J` native handle (`P`) and call `onSurfaceCreatedNative(J,Surface)`,
`onSurfaceChangedNative(J,Surface,III)`, `onSurfaceRedrawNeededNative(J,Surface)`,
`onSurfaceDestroyedNative(J)` — these are the four JNI thunks. JNI signatures in
`GameActivity.smali:1268–1277` match the disassembly exactly.

`initializeNativeCode` also establishes `nativeData+0x10 = JNIEnv*` (`0x2a20166`),
`+0x40 = AAssetManager*` (`0x2a20291`), and `+0 = &(+0x50)`; it **memsets the whole
0x278-byte object first** (`0x2a1fec7`, `memset(...,0,0x148)` plus zeroing `+0x190..+0x270`),
so all callback slots begin NULL until the glue installer runs. A scan of the entire
`.text` for `lea <code>; mov ...,0x88/0x90/0x98/0xa0(%reg)` found `0x2a23001` as the
only GameActivity-region writer — no Roblox override of these four slots exists in
this build.

## 5. Guest-shaped values that let the redraw thunk dispatch a frame

`0x2a20a00` requires three conditions; supply them with **guest addresses/data**:

```c
uintptr_t base = guest_load_bias;              /* e.g. 0x7f0556700000 */
void *nativeData = calloc(1, 0x180);           /* GameActivity-shaped object */
*(void **)(nativeData + 0x00) = (char*)nativeData + 0x50;   /* callbacks pointer */
*(void **)(nativeData + 0x88) = (void *)(base + 0x2a23a10); /* created  */
*(void **)(nativeData + 0x90) = (void *)(base + 0x2a23cc0); /* resized  */
*(void **)(nativeData + 0x98) = (void *)(base + 0x2a23c40); /* REDRAW   */
*(void **)(nativeData + 0xa0) = (void *)(base + 0x2a23b60); /* destroyed*/
*(void **)(nativeData + 0x10) = sr_env_obj;                 /* JNIEnv*  */
*(void **)(nativeData + 0x38) = threaded_state;             /* android_app* */
*(void **)(nativeData + 0x140) = guest_anative_window;      /* from GOT 0x72294a8 */
```
where `threaded_state` is a live 0x180-byte `android_app` with real pipe fds at
`+0x120/+0x124`, a valid `mutex` at `+0xc8`/`cond` at `+0xf0`, `onAppCmd` at `+0x8`,
and the looper thread entered at `0x2a24100` (fd callback `0x2a242c0`) actually running.

Then drive the redraw thunk with its genuine JNI shape:
`((void(*)(void*,void*,uintptr_t,void*))(base+0x2a20a00))(env, clazz, (uintptr_t)nativeData, surfaceToken)`.
`rcx`/`surface` may be a token because the thunk never reads it; the window comes
from `[+0x140]`. Execution: `0x2a20a11` loads `base+0x2a23c40`, `0x2a20a20` tail-jumps
into guest code with `(rdi=nativeData, rsi=window)`, `0x2a23c40` writes command `0x04`
to the pipe, and the looper thread's `0x2a242c0` reads it and calls `onAppCmd`
(`call *0x8(%rbx)` at `0x2a24367`) → Roblox's redraw handler → engine frame loop →
`call QWORD PTR [vtable+0x18]` with `[0x7191300]=0x649d422` → `eglSwapBuffers@plt`
`0x6d334d0` → GOT `0x722a1a8`. Because `0x649d422` ends in a tail `jmp`, the RA at
the GOT hook is the **guest** return site after the indirect vtable call, so
`rg_in_guest(ra)` (`render_guest.c:181`) is satisfied and `s_swaps_guest` increments.

The same three populated slots also make the created/changed/destroyed thunks dispatch
genuinely: created posts INIT_WINDOW (`0x01`) and stores `pendingWindow`, changed posts
WINDOW_RESIZED (`0x03`), destroyed posts TERM_WINDOW (`0x02`).

## 6. Why `guest_tagged=0` today / honesty constraints

- The harness `rg_bind_surface()` (`render_guest.c:289–331`) drives all four thunks with
  a calloc'd synthetic `nativeData` whose `+0x88/+0x98/+0xa0` are **NULL**, exactly as
  documented in `render_guest.h` and observed at the NULL-test sites
  `0x2a20897/0x2a2092b/0x2a2094f/0x2a20911/0x2a20a18/0x2a20a4f`. Redraw therefore
  skips `0x2a20a20`; no guest command is posted, no frame loop, no guest draw
  (`R1 §4`, `swaps=1/0/1`).
- Installing a **host** function pointer into `+0x98` would (a) violate the "no host
  callback in a guest object" rule and (b) produce a host RA, i.e. exactly the
  synthetic classification we must not fake. Only the guest addresses above are
  legitimate.
- Merely writing `base+0x2a23c40` into a calloc'd object is also insufficient (and will
  fault at `0x2a23c52`/`0x2a23c5b`): the callback dereferences `nativeData+0x38` and a
  pipe fd at `+0x124`. A faithful shaped `android_app` plus a running looper thread is
  required. Until that exists, `draws=0`/`guest=0` remain the truthful counters.
- Even with all of it working, every component (token Surface, shaped ANativeWindow,
  host-driven thunks, synthetic looper state) is **seeded synthetic state**. Under
  decision 013/038 the marker stays **WITHHELD**; the only legitimate output is a
  diagnostic "guest-tagged render" line, and no new knob may relax the strategy.

## 7. Unknowns / limits

- The exact Roblox `onAppCmd` handler for `APP_CMD_WINDOW_REDRAW_NEEDED` (cmd 4) and the
  precise engine function that turns it into a vtable-dispatched frame were not pinned
  statically (~11,600 `call *0x18(%r..)` sites; C04 §2.3). Discover the stable guest RA
  by capturing `__builtin_return_address(0)` at GOT `0x722a1a8` after driving cmd 4.
- The `+0x140` window field is a Roblox extension beyond the published AGDK
  `GameActivity` C struct; its provenance (guest vs host-written) is not settled here —
  in the current harness it is host-published (`render_guest.c:326-329`), a false-green
  conjunct flagged by `R1 §6` item 3.
- Whether a shaped `ANativeWindow` is accepted by the host EGL backend remains unknown
  (the `0x649cf9a` pbuffer fault path, C04 §1.4).

## 8. Evidence index

Fresh (this task): `objdump -d` of `0x2a20840–0x2a20a80`, `0x2a1fe30–0x2a20510`,
`0x2a21020–0x2a21300`, `0x2a23000–0x2a240c0`, `0x2a24100–0x2a24520`;
`readelf -sW` dyn-syms (`Java_com_google_androidgamesdk_GameActivity_initializeNativeCode`
at `0x2a1fe30`; no exported `GameActivity_onCreate`); smali
`com/google/androidgamesdk/GameActivity.smali:1268–1277,1420–1511`; upstream AGDK
`GameActivity.h` (callback signatures) and `native_app_glue.h` (`APP_CMD_*` enum);
`sha256sum`/`readelf -nW` identity. Prior (read only): `analysis/C04-render-integration.md`,
`analysis/C08-surface-frame.md`, `analysis/R1_zero_tagged_swaps.md`,
`analysis/T3_5_render-evidence.md`, `analysis/render/render-map.md`,
`analysis/jni/jni-map.md`, `src/jni/render_guest.c/.h`. No other file was created or modified.
