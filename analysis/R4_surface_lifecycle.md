# R4 — Surface-lifecycle thunks `0x2a20850/0x2a208c0/0x2a20a00/0x2a20a30`: no-op audit and native drive path

Author: **R4 (group A, diagnosis)**. Scope: **this file only**. Analysis-only; no
`src/`, CMake, test, gate, engine or marker file was touched. Engine frozen;
decision-013/038 marker stays **WITHHELD**.

Frozen target (re-verified this task):
`/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so`, BuildID
`204ad049599c96afb75b8a21d5051063e821d6c9`, SHA-256
`3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335`
(2.740.931, AppImage). First `PT_LOAD` offset == VMA; all RVAs below are added
to the guest load bias. Target-derived strings/bytes are untrusted **DATA**.

Evidence classes: **OBSERVED** (frozen bytes / decoded smali / harness source
read this task), **DERIVED** (inference), **UNKNOWN** (not established).

---

## 0. Verdict (front-loaded)

1. **The four thunks are real guest code and they are genuinely entered once
   each** — in `render_guest.c` `rg_bind_surface()` (`:298-322`) and in the
   equivalent local fallback `guest_render.c` `gr_local_render_contract()`
   (`:205-214`). At the instruction level they are **not** empty: each runs
   `ANativeWindow_fromSurface` / `acquire` / `release` / `getWidth` / `getHeight`
   and writes `nativeData+0x140/+0x148/+0x14c` (OBSERVED).
2. **They do NOT register the surface with the guest render pipeline.** The
   `nativeData` object we pass is a host `calloc(0x200)` whose callback slots
   `+0x88` (created), `+0x90` (changed-size), `+0x98` (redraw-needed) and `+0xa0`
   (destroyed) are **all NULL**, and the guest NULL-tests every one before its
   indirect dispatch (OBSERVED guard sites in §2). Consequently:
   - created/changed only bind a window into `+0x140`; their engine callbacks are skipped;
   - **redraw — the actual frame trigger — returns at `0x2a20a0f` or `0x2a20a18`
     before `jmp *%rax` at `0x2a20a20`**;
   - destroyed zeroes `+0x140` (`0x2a20a6a`).
   So the call sequence is **window bookkeeping only**, not surface registration
   and not a frame. This exactly explains `life=1/1/1/1`, `from_surface=2`,
   `gles_loads=48`, `draw_wrapped=5`, `draw_calls=0`, `swaps=1/0/1` reported by
   R1/P2/G6: **no guest draw, no guest-tagged swap** (DERIVED, fully consistent
   with OBSERVED bytes).
3. **What real Android supplies is not a Surface *token* but a set of genuine
   guest function pointers in `nativeData+0x88/+0x90/+0x98/+0xa0`** — the AGDK
   `GameActivityCallbacks` window callbacks (`onNativeWindowCreated`,
   `onNativeWindowResized`, `onNativeWindowRedrawNeeded`,
   `onNativeWindowDestroyed`), installed by the engine's native GameActivity
   glue when the genuine `nativeData` is created by `initializeNativeCode`
   (`0x2a1fe30` / `0x2a1fe80`). Java's `SurfaceHolder` callbacks only *drive* the
   thunks; the thunks dispatch to those already-installed engine callbacks
   (OBSERVED thunk bodies + decoded smali; AGDK mapping DERIVED).
4. **Concrete fix (§5):** stop hand-building an all-zero `nativeData`; either
   (A) obtain the guest's own 0x278-byte `nativeData` from `initializeNativeCode`
   (so the callback table is the engine's), or (B) install **genuine guest**
   callback pointers into our synthetic object's `+0x88/+0x90/+0x98/+0xa0`, keep
   `+0x140` non-NULL at redraw time, and drive `created → changed → redraw`
   (never `destroyed` before the frame). Host function pointers must **never** be
   placed in those slots (decision 013 / F10 FP-7). Any such seeding is a
   withheld diagnostic; the marker is not affected.

---

## 1. Evidence base and method

* `readelf -nW` / `sha256sum` of the frozen engine (identity above).
* `objdump -d --start-address/--stop-address` slices of `0x2a20850..0x2a20a80`
  (four thunks), `0x2a1fd00..0x2a20850` / `0x2a1fe30..0x2a20390`
  (`initializeNativeCode`, the real `nativeData` constructor), `0x2a23060..0x2a23420`
  (an unrelated 0x180-byte event object that reuses the same offsets — explicitly
  excluded), plus `readelf -SW` for the `.data.rel.ro`/`.got` vaddr↔file-offset
  delta (`0x4000`).
* Decoded smali `mini/out/smali/com/google/androidgamesdk/GameActivity.smali`
  (native method declarations and the `SurfaceHolder` callbacks).
* Harness source `src/jni/render_guest.c/.h`, `src/linux/guest_render.c` (read only).
* Prior reports used as cross-checks, not re-derived:
  `analysis/render/render-map.md §4`, `analysis/C04-render-integration.md`,
  `analysis/C08-surface-frame.md §2.6`, `analysis/P2_nativeData_layout.md §2/§4`,
  `analysis/R1_zero_tagged_swaps.md`, `analysis/R7_anativewindow_binding.md`,
  `analysis/F9_frame_integration_order.md`, `analysis/F10_frame_false_green.md`.
* No build, debugger run, or REA query was needed; all thunk facts are direct
  byte observations.

---

## 2. Byte-accurate audit of the four thunks (OBSERVED)

Common thunk ABI: `rdi = JNIEnv*`, `rsi = jobject this`, **`rdx = nativeData`
(the `jlong mNativeData` cast to a pointer)**, `rcx = jobject Surface` where
used. `nativeData+0x10` holds a cached `JNIEnv*` passed to `fromSurface`
(`:0x2a20875`, `:0x2a20900`) — it is *not* the thunk's `rdi`.

### 2.1 `0x2a20850` — `onSurfaceCreatedNative(JLandroid/view/Surface;)V`

```
2a20854  test %rdx,%rdx ; je .ret                 ; nativeData NULL guard
2a2085c  mov %rdx,%rbx ; mov 0x140(%rbx),%rdi      ; old ANativeWindow
2a20869  test %rdi,%rdi ; jne 2a2086b               ; release old if present
2a2086b  call ANativeWindow_release@plt (6d31ac0)
2a20870  test %r14,%r14 ; je 2a208a8                ; Surface NULL -> zero +0x140
2a20875  mov 0x10(%rbx),%rdi ; mov %r14,%rsi        ; env, Surface
2a2087c  call ANativeWindow_fromSurface@plt (6d31ad0)
2a20881  mov %rax,0x140(%rbx)                       ; store new window
2a20888  test %rax,%rax ; je .ret                   ; NULL window -> stop
2a2088d  mov 0x88(%rbx),%rcx                        ; <-- created callback
2a20894  test %rcx,%rcx ; je .ret                   ; NULL guard
2a20899  mov %rbx,%rdi ; mov %rax,%rsi              ; (nativeData, window)
2a208a6  jmp *%rcx                                  ; tail-dispatch callback
2a208a8  movq $0,0x140(%rbx)                        ; Surface==NULL path
2a208b3  ret
```
Callback `+0x88` ABI: `f(nativeData, ANativeWindow*)`. Dispatch skipped iff
`+0x88 == NULL` (guard `0x2a20894`).

### 2.2 `0x2a208c0` — `onSurfaceChangedNative(JLandroid/view/Surface;III)V`

```
2a208d6  mov 0x140(%rbx),%r14                       ; old window
2a208e5  call ANativeWindow_acquire@plt (6d32100)   ; hold old
2a208f6  call ANativeWindow_release@plt             ; release old
2a20907  call ANativeWindow_fromSurface@plt         ; new window
2a20910  mov %rax,0x140(%rbx)
2a20917  cmp %rax,%r14 ; je 2a20982                 ; new==old branch
  --- changed-window branch ---
2a20921  mov 0xa0(%rbx),%rcx                        ; destroyed callback
2a2092b  test %rcx,%rcx ; je 2a2093c
2a20933  call *%rcx                                 ; f(nativeData, old_window)
2a20945  mov 0x88(%rbx),%rcx                        ; created callback
2a2094f  test %rcx,%rcx ; je 2a20960
2a20957  call *%rcx                                 ; f(nativeData, new_window)
2a20963  call ANativeWindow_getWidth@plt (6d32120) -> [rbx+0x148]
2a20975  call ANativeWindow_getHeight@plt (6d32110)-> [rbx+0x14c]
  --- same-window branch (our case: fromSurface returns the same object) ---
2a20982/2a20993  getWidth/getHeight; cmp [rbx+0x148]/[rbx+0x14c]
2a209a8  store new width/height
2a209b4  mov 0x90(%rbx),%r8                         ; <-- resized callback
2a209be  test %r8,%r8 ; je 2a209d1
2a209ce  call *%r8                                  ; f(nativeData, window, w, h)
2a209e3  jmp ANativeWindow_release@plt              ; drop the acquired old ref
```
Three callback slots are consulted: `+0xa0` (guard `0x2a2092b`, call `0x2a20933`),
`+0x88` (guard `0x2a2094f`, call `0x2a20957`), `+0x90` (guard `0x2a209be`, call
`0x2a209ce`). With our single synthetic window `new == old`, so only the
`+0x90` size-change path is relevant; it is skipped because the slot is NULL.

### 2.3 `0x2a20a00` — `onSurfaceRedrawNeededNative(JLandroid/view/Surface;)V`

```
2a20a00  test %rdx,%rdx ; je .ret                   ; nativeData NULL guard
2a20a05  mov 0x140(%rdx),%rsi                       ; window holder
2a20a0c  test %rsi,%rsi ; je .ret                   ; <-- gate 1: window non-NULL
2a20a11  mov 0x98(%rdx),%rax                        ; <-- redraw callback
2a20a18  test %rax,%rax ; je .ret                   ; <-- gate 2: callback non-NULL
2a20a1d  mov %rdx,%rdi                              ; (nativeData, window in rsi)
2a20a20  jmp *%rax                                  ; THE frame/redraw dispatch
2a20a22  ret
```
This is the decisive thunk: `0x2a20a20` is the only instruction in the whole
lifecycle set that can enter the guest's frame path. With `+0x98 == NULL` it is
**unreachable**. (Even a non-NULL `+0x98` is gated by `+0x140`, hence the
destroyed-last ordering bug in §3.)

### 2.4 `0x2a20a30` — `onSurfaceDestroyedNative(J)V`

```
2a20a30  test %rdx,%rdx ; je .ret
2a20a39  mov 0x140(%rdx),%rsi ; test ; je 2a20a6a
2a20a45  mov 0xa0(%rbx),%rax                        ; destroyed callback
2a20a4f  test %rax,%rax ; je 2a20a62
2a20a51  mov %rbx,%rdi ; call *%rax                 ; f(nativeData, window)
2a20a62  mov %rsi,%rdi ; call ANativeWindow_release@plt
2a20a6a  movq $0,0x140(%rbx)                        ; <-- unconditionally clears window
2a20a76  ret
```

### 2.5 Consolidated nativeData field map (OBSERVED in these bytes)

| Off | Size | Field | Guard / dispatch (RVA) |
| --- | --- | --- | --- |
| `+0x10` | 8 | cached `JNIEnv*` for `fromSurface` | read `0x2a20875`, `0x2a20900` |
| `+0x88` | 8 | `onNativeWindowCreated(nd, window)` | guard `0x2a20894` / `0x2a2094f`, dispatch `0x2a208a6` / `0x2a20957` |
| `+0x90` | 8 | `onNativeWindowResized(nd, window, w, h)` | guard `0x2a209be`, dispatch `0x2a209ce` |
| `+0x98` | 8 | `onNativeWindowRedrawNeeded(nd, window)` | guard `0x2a20a18`, dispatch `0x2a20a20` (**frame trigger**) |
| `+0xa0` | 8 | `onNativeWindowDestroyed(nd, window)` | guard `0x2a2092b` / `0x2a20a4f`, dispatch `0x2a20933` / `0x2a20a54` |
| `+0xb8` | 8 | `onConfigurationChanged(nd, params)` | checked `0x2a20a97`, tail-jmp `0x2a20b7b` (thunk `0x2a20a80`) |
| `+0x140` | 8 | `ANativeWindow*` holder | set `0x2a20881` / `0x2a20910`, gated `0x2a20a0c`, cleared `0x2a20a6a` |
| `+0x148` | 4 | width | `0x2a20968`, `0x2a209a8` |
| `+0x14c` | 4 | height | `0x2a2097a`, `0x2a209ae` |

This confirms and extends `render_guest.h`'s `ND_OFF_*` set (which omits `+0x90`,
`+0xb8`, `+0x148`, `+0x14c`) and matches `analysis/P2_nativeData_layout.md §2`.

---

## 3. Why the current calls are no-ops for rendering (DERIVED from OBSERVED facts)

`render_guest.c:402-415` allocates `s_nd = calloc(1, 0x200)`, writes only
`+0x10 = s_dummy_env`, and comments that `+0x88/+0x98/+0xa0` deliberately stay
NULL. `guest_render.c:189-190` does the same. The local fallback's call order is
`created → changed → redraw → destroyed` (`guest_render.c:206-214`), and the
canonical module's is identical (`render_guest.c:298-322`).

Consequences, in call order:

| Call | What actually runs | What is skipped |
| --- | --- | --- |
| created `0x2a20850` | release old (NULL on first call), `fromSurface` GOT hook returns `s_anw`, store to `+0x140`, getWidth/getHeight never reached here | `jmp [nd+0x88]` at `0x2a208a6` — NULL |
| changed `0x2a208c0` | acquire/release old, `fromSurface` again, same-window branch, width/height (`0x2a20963/0x20975`) cached at `+0x148/+0x14c` | `call [+0xa0]` `0x2a20933`, `call [+0x88]` `0x2a20957`, `call [+0x90]` `0x2a209ce` — all NULL |
| redraw `0x2a20a00` | `+0x140` is still `s_anw` (non-NULL) because destroyed runs last | `test [+0x98]` at `0x2a20a18` fails → **`jmp *%rax` at `0x2a20a20` never executes** |
| destroyed `0x2a20a30` | `+0xa0` NULL-skipped; `ANativeWindow_release` on `s_anw`; **`+0x140 = 0`** at `0x2a20a6a` | engine "window gone" notification |

Net: a real guest instruction sequence ran, a refcounted window was published
into `nativeData+0x140` (and the host separately rewrote global `0x72a3308` at
`render_guest.c:326-329`), but **no engine callback was ever notified**, so:

* no surface was bound into the render pipeline via the GameActivity path;
* no frame loop was started (`0x2a20a20` not reached), hence no
  `call [vtable+0x18]` → `0x649d422` → `eglSwapBuffers@plt` guest-tagged swap;
* no guest GLES draw dispatcher (`0x6496d0c`) was entered, hence
  `draw_calls = 0` even though our `eglGetProcAddress` hook wrapped draw procs.

The only swap counted is the host-origin Route-B call
(`sober_render_guest_swap_via_vtable()` / `guest_render.c` direct `0x649d422`),
whose return address is in the host → `swaps_synthetic = 1`,
`swaps_guest_tagged = 0`. This is the honest RED state; the marker is correctly
withheld. (Causal chain DERIVED; field states OBSERVED.)

Two additional state defects compound it (both already noted by P2):
1. **Ordering**: real Android calls `destroyed` at teardown, not during setup.
   Calling it last leaves `+0x140 == NULL` after `run()`. A later standalone
   redraw would bail at `0x2a20a0f` *before* even testing `+0x98`.
2. **Guest render device never seeded**: `render_guest` calls
   `0x649c21e(s_anw, 0)` directly and keeps the returned 0x48-byte ctx in host
   `s_ctx`. The guest's own caller `0x648b94c` (which stores window at
   `device+0x158`, ctx at `device+0x160`, delegate at `device+0x168` with vtable
   `0x71908e0`) is bypassed, so a guest frame loop walking `device+0x160` cannot
   find our context (P2 §4.2; OBSERVED call sites, DERIVED consequence).

---

## 4. What real Android / GameActivity sets those fields (OBSERVED + DERIVED)

### 4.1 The Java input (OBSERVED, decoded smali)

`com/google/androidgamesdk/GameActivity.smali`:

* `initializeNativeCode(String,String,String,AssetManager,byte[],Configuration)J`
  (`:522`) is called in `onCreate` (`:805`) and its result is stored in static
  `P:J` (`:809`) — this is `mNativeData`.
* `surfaceCreated(SurfaceHolder)` (`:1443`) →
  `onSurfaceCreatedNative(P, holder.getSurface())` (`:1463`);
  `surfaceChanged` (`:1438`) → `onSurfaceChangedNative(P, surface, w, h, format)`;
  `surfaceRedrawNeeded(SurfaceHolder)` (`:1491`) →
  `onSurfaceRedrawNeededNative(P, surface)` (`:1511`);
  `surfaceDestroyed` (`:1468`) → `onSurfaceDestroyedNative(P)` (`:1486`).
* All four are declared `native` (`:1268-1277`), registered by the guest through
  JNINativeMethod descriptors in `.data.rel.ro`:
  `0x6d37cd8` / `0x6d37cf0` / `0x6d37d08` / `0x6d37d20` → thunks
  `0x2a20850` / `0x2a208c0` / `0x2a20a00` / `0x2a20a30`
  (file bytes zero; APS2-relocated — OBSERVED in `render-map.md §4`; raw qword
  search for the thunk addresses in the frozen file returned no hits, confirming
  they are relocation-filled).

So on Android the **input is the SurfaceHolder lifecycle plus the `mNativeData`
long**; the Java `Surface` object itself is only the argument to
`ANativeWindow_fromSurface` (C08 §1/§2.3).

### 4.2 The native object that owns the callback slots (OBSERVED)

`initializeNativeCode` (wrapper `0x2a1fe30`, real body `0x2a1fe80`) constructs the
genuine `nativeData`:

```
2a1feb0  mov $0x278,%edi ; call allocator           ; 0x278-byte object (rbx)
2a1febd  mov $0x148,%edx ; memset(rbx,0,0x148)      ; +0x00..+0x147 zeroed,
                                                   ; INCLUDING +0x88/+0x90/+0x98/+0xa0
2a20166  mov %r14,0x10(%rbx)                        ; cache JNIEnv at +0x10
...
```
`0x2a1fecc..0x2a1ff2a` zero-fill `+0x190..+0x260`; other fields are populated
with AAssetManager (`+0x40` at `0x2a20291`), saved state (`+0x20/+0x28`), pipe
fds, etc. The four window-callback slots are **not** written inside
`initializeNativeCode` itself (searched the whole `0x2a1fe80..0x2a20a80` body:
only the thunk reads at `0x2a2088d/0x2a20921/0x2a20945/0x2a209b4/0x2a20a45`
appear). They are installed **afterwards by the engine's own GameActivity native
callback registration** — the C++ side that fills the AGDK callback table during
native app startup — which is the piece our harness never runs. (The exact
installer RVA was not pinned in this pass: an exhaustive `objdump` scan found
**7,851** stores to `+0x88/+0x90/+0x98/+0xa0` across `.text` (e.g.
`0x2a5d0ee`, `0x2a8522c`, `0x2befd34`, `0x2c02437`, plus thousands of unrelated
float/struct stores); none could be uniquely attributed to the 0x278
`nativeData` without a runtime type/pointer trace — **UNKNOWN**, stated
honestly. `0x2a230d5` writes the same offsets in an unrelated 0x180-byte event
object whose `+0x88..+0xa0` handlers write tags `0x11/0x05/0x06/0x12` to a pipe
— explicitly **excluded**.)

### 4.3 Mapping to AGDK (DERIVED, high confidence)

The offsets line up one-for-one with Android's `GameActivityCallbacks` window
callbacks (the slot order is confirmed by the thunk dispatch ABIs in §2):

| nativeData slot | AGDK callback | Thunk that dispatches it |
| --- | --- | --- |
| `+0x88` | `onNativeWindowCreated(GameActivity*, ANativeWindow*)` | `0x2a20850` / `0x2a208c0` |
| `+0x90` | `onNativeWindowResized(GameActivity*, ANativeWindow*, int32,int32)` | `0x2a208c0` |
| `+0x98` | `onNativeWindowRedrawNeeded(GameActivity*, ANativeWindow*)` | `0x2a20a00` |
| `+0xa0` | `onNativeWindowDestroyed(GameActivity*, ANativeWindow*)` | `0x2a208c0` / `0x2a20a30` |

Real sequence: Java `onCreate` → `initializeNativeCode` returns nativeData →
game's native glue installs its window callbacks into `+0x88/+0x90/+0x98/+0xa0` →
`SurfaceHolder.surfaceCreated/Changed/RedrawNeeded` call the JNI thunks →
thunks' indirect dispatch enters the **engine's** handlers, which bind the
`ANativeWindow` into the render device and run the frame loop → GLES draws →
`call [vtable+0x18]` → `0x649d422` → GOT `0x722a1a8`.

Our harness supplies only the *outer* half (thunk entry + a window token); the
*inner* half (the engine callbacks at those four slots) is missing. That is the
root cause of "each called once yet no guest draws."

---

## 5. Concrete native fix (diagnostic; withhold stays)

Goal: make `0x2a20a20` actually dispatch into **guest** code and let the guest
frame loop run. All options below seed guest state under decision 013/038 and
must be opt-in + in the force-withhold OR-chain; none can emit the marker.

### Option A — faithful: use the guest's own nativeData (preferred)

1. Drive guest `initializeNativeCode` (`0x2a1fe30` wrapper → `0x2a1fe80` body)
   with the bridge `JNIEnv*` and real Java arguments (`String×3`, `AssetManager`,
   `byte[]`, `Configuration`) so it returns the genuine 0x278-byte nativeData.
   Its `+0x88/+0x90/+0x98/+0xa0` will be the engine's installed callbacks (verify
   non-NULL before use). The existing JNI slot work (231 slots, 49 cached IDs,
   `fields_default`, `jnislots_default`) is a prerequisite.
2. Call the four thunks with that pointer and a guest-shaped `Surface`
   (`mNativeObject:J` holding the ANativeWindow) so `ANativeWindow_fromSurface`
   returns the real object. Install **all five** ANativeWindow GOTs together —
   `fromSurface 0x72294a8`, `release 0x72294a0`, `acquire 0x72297c0`,
   `getHeight 0x72297c8`, `getWidth 0x72297d0` — to avoid freeing our window at
   `0x2a209e3`/`0x2a20a62` (C08 §4 Phase A step 3).
3. Call in true lifecycle order: `created` → `changed` → `redraw`; call
   `destroyed` only at teardown. Assert `nativeData+0x140 != 0` immediately
   before `redraw` (`0x2a20a0c`) and that `[+0x98] != 0` (`0x2a20a18`).

### Option B — minimal diagnostic: install genuine guest callbacks in our object

4. Locate the engine's callback installer (or replay the guest's own
   registration) and apply it to our synthetic `s_nd`, setting `+0x88/+0x90/
   +0x98/+0xa0` to **genuine guest function pointers** (RVAs inside
   `[base, base+0x6d33b90)`). **Never** put a host trampoline in these slots:
   F10 FP-7 / T3-5 §3.4 forbid host code in nativeData/vtables as evidence, and
   a guest-origin dispatch into host code would be a false green.
5. Re-order `rg_bind_surface()` (and the local fallback) to
   `created → changed → redraw`, dropping the setup-time `destroyed` call (or
   re-publishing `+0x140` before redraw). This makes `0x2a20a20` reachable.
6. Seed the guest render device so the frame loop finds the context: replay the
   guest path at `0x648b940..0x648b98a` (caller `0x648b94c` → `0x649c21e`),
   which writes window `@device+0x158`, ctx `@device+0x160`, delegate `@device+0x168`
   (vptr `0x71908e0`), instead of calling `0x649c21e` directly from the host
   (P2 §4.2). Use `pbuffer/extra = 0`; a non-zero value faults at `0x649cf9a`.

### Verification / success criteria for the diagnostic (not the marker)

* `life=1/1/1/0` at frame time (destroyed deferred);
* `from_surface` RA at guest `0x2a2087c` / `0x2a20907`;
* `nativeData+0x140 != 0` at `0x2a20a0c`, and dispatch actually taken at
  `0x2a20a20` (instrument a counter on the genuine callback or read the guest
  handler's first instruction hit);
* `gles_draw_calls >= 1` from guest-RA draw procs and
  `swaps_guest_tagged >= 2` with RA inside guest `.text`
  (T3-5 §3.1/§3.2, `render_ok = phase3 && guest_tagged`);
* engine SHA/BuildID unchanged; zero `app shell rendered` emissions; every new
  knob in the withhold chain.

---

## 6. RVA quick reference

| RVA / slot | Meaning |
| --- | --- |
| `0x2a20850` | `onSurfaceCreatedNative` → `[nd+0x88]` (guard `0x2a20894`, jmp `0x2a208a6`) |
| `0x2a208c0` | `onSurfaceChangedNative` → `[nd+0xa0]` (`0x2a2092b`/`0x2a20933`), `[nd+0x88]` (`0x2a2094f`/`0x2a20957`), `[nd+0x90]` (`0x2a209be`/`0x2a209ce`) |
| `0x2a20a00` | `onSurfaceRedrawNeededNative` → window gate `0x2a20a0c`, `[nd+0x98]` guard `0x2a20a18`, **dispatch `0x2a20a20`** |
| `0x2a20a30` | `onSurfaceDestroyedNative` → `[nd+0xa0]` (`0x2a20a4f`/`0x2a20a54`), clears `+0x140` at `0x2a20a6a` |
| `0x2a1fe30` / `0x2a1fe80` | `initializeNativeCode` wrapper / real body (alloc `0x278` at `0x2a1feb0`, memset `0x148` at `0x2a1fec7`, JNIEnv → `+0x10` at `0x2a20166`) |
| `0x6d37cd8/0xcf0/0xd08/0xd20` | JNI native-method descriptors for the four thunks (APS2-relocated) |
| `0x6d31ad0` / GOT `0x72294a8` | `ANativeWindow_fromSurface` (called `0x2a2087c`, `0x2a20907`) |
| `0x6d31ac0` / GOT `0x72294a0` | `ANativeWindow_release` |
| `0x6d32100` / `0x6d32110` / `0x6d32120` | `acquire` / `getHeight` / `getWidth` (GOT `0x72297c0/8/d0`) |
| `0x72a3308` / `0x72a3310` | guest global ANativeWindow* / generation |
| `0x649c21e` → `0x649c272` | create render context (extra/pbuffer **must be 0**, fault `0x649cf9a`) |
| `0x648b94c` | guest caller that seeds render device `+0x158/+0x160/+0x168` |
| `0x71912e8`, slot[3] `0x7191300` | render vtable / swap `0x649d422` |
| `0x6d334d0` / GOT `0x722a1a8` | `eglSwapBuffers` PLT / guest-tagged swap observation point |

---

## 7. Limits and honesty

* The **fact** that the four thunks NULL-test `+0x88/+0x90/+0x98/+0xa0` and that
  our `nativeData` leaves them NULL is OBSERVED. The conclusion "therefore no
  guest draw" is DERIVED but is corroborated by the recorded counters
  (`draw_calls=0`, `guest_tagged=0`, `life=1/1/1/1`).
* The mapping of `+0x88/+0x90/+0x98/+0xa0` to AGDK `GameActivityCallbacks` is
  DERIVED from ABI/offset order; the AGDK header itself is an external API, not
  guest bytes. The exact guest function that installs those pointers was **not**
  pinned (UNKNOWN); the fix does not depend on its address because Option A lets
  the guest install them itself.
* No runtime test, debugger capture, or guest-code patch was performed. Whether
  a genuine guest `[+0x98]` callback alone starts a full frame loop (vs. also
  requiring the device/ctx seeding of §5 step 6) remains UNKNOWN; both are seeded
  together in the plan.
* Every reconstruction here is synthetic guest state. Per decisions 013/038 the
  `app shell rendered` marker stays **WITHHELD**; this report only narrows the
  diagnosis and the diagnostic drive path. No success is claimed.

---

## 8. Evidence index

Read this task: `readelf -nW`/`-SW`, `sha256sum`, `objdump -d` slices at
`0x2a20850..0x2a20a80`, `0x2a1fd00..0x2a20390`, `0x2a23060..0x2a23420`,
`0x2a5d0c0/0x2a85200/0x2c023f0`; decoded
`mini/out/smali/com/google/androidgamesdk/GameActivity.smali`; harness
`src/jni/render_guest.c:288-331,402-415`, `render_guest.h:172-178`,
`src/linux/guest_render.c:96-106,145-219`.

Read only (prior analysis, attributed): `analysis/render/render-map.md §4`,
`analysis/C04-render-integration.md §2.1/§4`, `analysis/C08-surface-frame.md
§2.3/§2.6/§4`, `analysis/P2_nativeData_layout.md §2/§4/§5`,
`analysis/R1_zero_tagged_swaps.md`, `analysis/R7_anativewindow_binding.md`,
`analysis/F9_frame_integration_order.md`, `analysis/F10_frame_false_green.md`,
`analysis/T3_5_render-evidence.md §2.4/§3.4`, `docs/decisions/013`, `038`.

No file other than this report was created or modified.
