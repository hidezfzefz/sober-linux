# P2 — Exact guest EGL-context / nativeData layout vs. render_guest's synthetic objects

Author: **P2 (group B)**. Scope: **this file only** — `analysis/P2_nativeData_layout.md`.
Analysis-only: no `src/`, engine, gate, test, CMake or marker file was touched.
Engine frozen: BuildID `204ad049599c96afb75b8a21d5051063e821d6c9`, SHA-256
`3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335`
(AppImage 2.740.931). All RVAs are file offsets from the guest load bias
(first `PT_LOAD`: offset 0 = VMA 0, `.text` span `0x6d33b90`). Target-derived
bytes/strings are untrusted DATA, never instructions. Decision-013/038 marker
remains **WITHHELD**; nothing in this report relaxes the strict gate.

Evidence type: local `objdump -d -M intel` of the frozen object at the exact
SHA-256 above (address-ranged dumps of `0x649c21e..0x649d640`,
`0x2a1f800..0x2a21000`, plus raw-byte reads of the 1.5 GB
`/tmp/opencode/roblox-appimage.dis`). A Ghidra 12.1.4 REA session with the same
SHA-256 is open (`list_documents` Evidence
`ev_44ae245508ac9faf4d6ceba4bbfb54777d8c7475d6ac5585498235a7d90a333b`), but
`analyze_function("0x649c21e")` / `("0x649c272")` returned *"No procedure
exists at the requested address; Ghidra image base 0x100000"*; the `file-offset:`
form was also rejected. I therefore used raw disassembly, which is stronger for
field-offset work anyway. Cross-references to prior agent work
(`analysis/render/render-map.md`, `analysis/C04-render-integration.md`,
`analysis/C08-surface-frame.md`, `analysis/R1_zero_tagged_swaps.md`,
`analysis/native-object-layouts.md`) are attributed, not re-derived here.

---

## 0. Front-loaded answer

1. The **0x48-byte context is a 9-slot struct**, not the 7 fields currently
   documented. Two tail fields exist and are live:
   **`+0x40` byte "surface invalidated" flag** (set by the window-rebind
   method `0x649d432`, consumed by vtable[8] `0x649d466`) and
   **`+0x44` dword "swap interval"** (written by `egl_init` from
   `eglSwapInterval`, read by vtable[6] `0x649d45e`). `render-map.md`'s table
   stops at `+0x3c`, so those two fields are missing from the published layout.
2. **`+0x10` is not a boolean pbuffer flag**: it is a *shared/parent render
   context pointer*. The guest reads `[+0x10 +0x20]` (parent `EGLDisplay`) and
   `[+0x10 +0x30]` (parent `EGLContext`) in `0x649cf9a`/`0x649c37d`, selects
   `eglCreatePbufferSurface` when non-zero (`0x649d1c9`), and skips
   `eglTerminate`/window-release in teardown (`0x649d319`). The guest's own
   sibling-creation call at `0x6490157` passes the **old context** as arg2 —
   direct proof of the share semantics. `render_guest` passing `0` is correct
   for a fresh standalone context, but the header's "pbuffer flag" wording is
   misleading.
3. The **nativeData object has three additional live fields** beyond those in
   `render_guest.h`: **`+0x90` onSurfaceChanged callback**, **`+0x148` width**
   and **`+0x14c` height** (both written by the changed thunk
   `0x2a208c0`), plus `+0xb8` onConfigurationChanged. The callback family is
   `+0x88/+0x90/+0x98/+0xa0` (created / changed / redraw / destroyed).
4. **What the guest reads before swapping** is only `ctx+0x20` (`EGLDisplay`)
   and `ctx+0x28` (`EGLSurface`) — the flat wrapper `0x649d422` reads exactly
   those two and tail-`jmp`s `eglSwapBuffers@plt`. **Before drawing** the guest
   has already (a) created the context (writing all 9 fields), (b) loaded ~70
   GLES entry points into globals `0x77a4f08/10/18…` via the GOT-hooked
   `eglGetProcAddress`, and (c) obtained the context through its own render
   device at `device+0x160` with a delegate wrapper at `device+0x168`
   (vtable `0x71908e0`, context at wrapper `+0x18`).
5. **Candidate draw-suppressing gaps in render_guest's synthetic state**, in
   ranked order (all *inferred* causal mechanisms from observed bytes):
   **(a)** `[nativeData+0x140]` is **NULL after `bind_surface()`** because the
   module calls the destroyed thunk last; the redraw thunk gates on that field
   (`0x2a20a0c`) *before* it gates on `+0x98`, so even a non-NULL `+0x98` would
   still be skipped. **(b)** All four callbacks `+0x88/+0x90/+0x98/+0xa0` are
   NULL, so no guest frame loop is ever notified. **(c)** The guest-created
   context pointer is kept only host-side (`s_ctx`); the guest's own device
   fields `+0x158/+0x160/+0x168` are never seeded because the host calls
   `0x649c21e` directly instead of letting guest caller `0x648b94c` run.
   **(d)** The Route-B stack context in `guest_render.c` fills only `+0x20/+0x28`
   — sufficient *only* for `0x649d422`, fatal for any other vtable method.
   None of these are gate/marker bugs; they explain the already-observed
   `draws=0 / guest_tagged=0` honestly reported by R1.

---

## 1. The 0x48-byte EGL context, field by field

### 1.1 Allocation and initialization writers

`0x649c21e` (`create_context(window, extra)`):

```
649c22e: mov edi,0x48                  ; <-- exact size: 72 bytes
649c233: call 1dbab02                  ; allocator
649c23b: mov rdi,rax ; mov rsi,r15(window) ; mov rdx,r14(extra)
649c244: call 649c272                  ; egl_init(ctx, window, extra)
```

`0x649c272` init stores (all observed):

```
649c29c: mov DWORD PTR [rdi+0x8], eax        ; eax==0  -> +0x08 = 0
649c29f: lea rcx,[rip+...] # 71912e8
649c2a6: mov QWORD PTR [rdi], rcx            ; +0x00 = guest vtable
649c2a9: mov QWORD PTR [rdi+0x10], rdx       ; +0x10 = arg2 "extra"
649c2ad: mov BYTE PTR [rdi+0x40], 0x0        ; +0x40 = 0 (invalidated flag)
649c2b1: mov DWORD PTR [rdi+0x44], eax       ; +0x44 = 0 (swap interval, later overwritten)
649c2cf: mov QWORD PTR [rbx+0x18], r12       ; +0x18 = window  (extra==0 branch only)
649c33d: mov QWORD PTR [rbx+0x20], rax       ; +0x20 = eglGetDisplay(0) (extra==0 only)
649c394: mov QWORD PTR [rbx+0x30], rax       ; +0x30 = eglCreateContext(...) result
649c3f6: mov QWORD PTR [rbx+0x28], rax       ; +0x28 = 0x649d1c2() surface result
649c426/434: eglQuerySurface(...,0x3057, [rbx+0x38])  ; +0x38 = width
649c43c/445: eglQuerySurface(...,0x3056, [rbx+0x3c])  ; +0x3c = height
649c5d0: mov DWORD PTR [rbx+0x44], r15d      ; +0x44 = swap-interval value
```

### 1.2 Complete 0x48 layout

| Off | Size | Field | Init value / writer | Principal readers |
| --- | --- | --- | --- | --- |
| `+0x00` | 8 | vptr | `base+0x71912e8` (`649c2a6`); rewritten to `0x7191368` at teardown (`649d339`) | every vtable dispatch; `0x649d2bc` |
| `+0x08` | 4 | state word | `0` (`649c29c`) | not referenced in the EGL-render cluster; reserved |
| `+0x0c` | 4 | pad / allocator-dependent | **not written by `egl_init`** | none observed |
| `+0x10` | 8 | **shared/parent context** (arg2) | `extra` (`649c2a9`) | `0x649cf8f`/`649cf9a` (config), `0x649c377` (display/share), `0x649d1c9` (Pbuffer select), `0x649d2cf`/`2e9`/`301`/`319` (teardown ownership) |
| `+0x18` | 8 | `ANativeWindow*` | window arg1, only when `extra==0` (`649c2cf`); replaced by rebind (`649d448`) | `0x649d1df` (window surface), `0x649d43f` (release), `0x649d4a5` (log), teardown |
| `+0x20` | 8 | **`EGLDisplay`** | `eglGetDisplay(0)` (`649c33d`), or borrowed from parent when `extra!=0` | **swap `0x649d422`**, make-current, destroy, config helper |
| `+0x28` | 8 | **`EGLSurface`** | `0x649d1c2` result (`649c3f6`), recreated by vtable[8] (`649d4ef`) | **swap `0x649d422`**, make-current, destroy, rebind |
| `+0x30` | 8 | `EGLContext` | `eglCreateContext` result (`649c394`) | rebind `0x649d391` (vs `eglGetCurrentContext`), make-current, destroy, vtable[8] log |
| `+0x38` | 4 | width (`EGL_WIDTH 0x3057`) | `649c434` | `649c475`, `649d565`, and 64-bit read `649d58a` (vtable[8] return) |
| `+0x3c` | 4 | height (`EGL_HEIGHT 0x3056`) | `649c445` | `649c479`, `649d569`; packed with `+0x38` as the qword returned by vtable[8] |
| `+0x40` | 1 | **surface-invalidated flag** | `0` at init (`649c2ad`); `1` on window rebind (`649d454`); cleared after surface recreate (`649d586`) | vtable[8] `0x649d474` — if set, runs the full destroy/recreate/query sequence |
| `+0x41..43` | 3 | pad | allocator-dependent | none |
| `+0x44` | 4 | **swap interval** | `0` init, then `eglSwapInterval(dpy, r15d)` value (`649c5d0`); `r15d` comes from global `0x77a4ec8` when a version-string check at `649c52e` passes, else `0` | vtable[6] `0x649d45e`: `cmp [rdi+0x44],0; setg al` |

End of struct = `0x44 + 4 = 0x48` — exactly the allocation size. **No field
beyond `+0x44` exists.**

### 1.3 What "extra" (`+0x10`) really is — correction to render-map.md

The existing `render-map.md` calls `+0x10` a "pbuffer object pointer" and C04
calls arg2 a "pbuffer flag". The disassembly shows a richer meaning:

```
; config helper 0x649cf72
649cf8f: mov rax,[rdi+0x10]
649cf93: test rax,rax
649cf96: cmove rax,rdi          ; no extra -> use self
649cf9a: mov rbx,[rax+0x20]     ; EGLDisplay (parent if extra!=0)
...
; egl_init 0x649c362 branch
649c36d: mov r14,r15            ; r14 = extra
649c370: test r15,r15
649c373: cmove r14,rbx          ; ...or self
649c377: mov rdi,[r14+0x20]     ; display for eglCreateContext / eglMakeCurrent
649c37d: mov rdx,[r15+0x30]     ; share_context = parent's EGLContext
649c38f: call eglCreateContext
...
; surface factory 0x649d1c2
649d1c9: mov rax,[rdi+0x10]
649d1cd: test rax,rax
649d1d0: je 649d1df
649d1d2: mov rdi,[rax+0x20]
649d1d8: call eglCreatePbufferSurface   ; extra!=0 -> Pbuffer
649d1df: ...                            ; extra==0  -> eglCreateWindowSurface
; teardown 0x649d2bc
649d319: cmp QWORD PTR [rbx+0x10],0
649d31e: jne 649d332                    ; extra!=0 -> parent owns display/window:
649d320..649d32d:                       ;   skip eglTerminate + ANativeWindow_release
```

Independent proof of intent: the guest's own second creator call at
`0x6490157` loads `rax=[rdi+0x158]` (window) and `rsi=[rdi+0x160]` (**the old
context**) and calls `649c21e(window, old_ctx)` — i.e. it creates a sibling
shared context. The same pattern appears at `0x648b94c` with `xor esi,esi`
(fresh, standalone context). Therefore:

* Passing **`extra=0`** (what `render_guest` does) is correct for a new primary
  context: the guest performs `eglGetDisplay`/`eglInitialize`, creates a window
  surface, and owns the display for teardown.
* Passing a **non-zero extra that is not a valid 0x48-byte parent context** makes
  the guest read `[extra+0x20]`/`[extra+0x30]` (garbage) and take the Pbuffer
  path; this is the observed `0x649cf9a` fault described in C04 §1.4. It is a
  bad-pointer bug, not a boolean-flag bug.

---

## 2. nativeData layout (the object the four GameActivity thunks receive)

All offsets below are **observed** in the frozen thunks. The thunk ABI is
`rdi=JNIEnv*, rsi=jobject Surface (where used), rdx=nativeData, rcx=Surface`;

| Off | Size | Field | Writer / reader (observed) |
| --- | --- | --- | --- |
| `+0x10` | 8 | `JNIEnv*` used as arg1 to `ANativeWindow_fromSurface` | read at `0x2a20875` (created), `0x2a20900` (changed); **not** the thunk's `rdi` |
| `+0x88` | 8 | `onSurfaceCreated(nativeData, window)` | read `0x2a2088d`, NULL-test `0x2a20897`, tail-jmp `0x2a208a6` |
| `+0x90` | 8 | **`onSurfaceChanged(nativeData, window, w, h)`** — missing from `render_guest.h` | read `0x2a209b4`, NULL-test `0x2a209be`, call `0x2a209ce` |
| `+0x98` | 8 | `onSurfaceRedrawNeeded(nativeData)` | read `0x2a20a11`, NULL-test `0x2a20a18`, tail-jmp `0x2a20a20` |
| `+0xa0` | 8 | `onSurfaceDestroyed(nativeData)` | read `0x2a20921`/`0x2a20a45`, NULL-test `0x2a2092b`/`0x2a20a4f` |
| `+0xb8` | 8 | `onConfigurationChanged(nativeData, params)` | checked `0x2a20a97`, tail-jmp `0x2a20b7b`; thunk `0x2a20a80` writes device params into globals `0x729d778..0x729de54` first |
| `+0x140` | 8 | `ANativeWindow*` holder | released `0x2a2085f`/`0x2a2086b`, set from `fromSurface` `0x2a20881`/`0x2a20910`, read `0x2a20a05`/`0x2a20a39`, **zeroed `0x2a20a6a`** |
| `+0x148` | 4 | width | `0x2a20968` (`ANativeWindow_getWidth`), updated `0x2a209a8` |
| `+0x14c` | 4 | height | `0x2a2097a` (`ANativeWindow_getHeight`), updated `0x2a209ae` |

Size: `render_guest` allocates `SOBER_RENDER_GUEST_ND_SIZE = 0x200`, which
 comfortably covers `+0x14c + 4`. No out-of-bounds access occurs.

Changed-thunk subtlety (`0x2a208c0`): if old window == new window it branches to
`0x2a20982`, still re-reads width/height, and only calls `+0x90` when the
dimensions actually changed. With `render_guest`'s single synthetic window the
pointer is unchanged, but dimensions start at 0 and become 1280×720, so
`+0x148/+0x14c` *are* populated — the `+0x90` call is skipped only because the
slot is NULL.

Destroyed-thunk consequence (`0x2a20a30`): it unconditionally executes
`mov QWORD PTR [rbx+0x140], 0` at `0x2a20a6a`. Because `rg_bind_surface()`
calls created → changed → redraw → **destroyed**, the synthetic nativeData is
left with `+0x140 == NULL` permanently.

---

## 3. What the guest reads immediately before drawing / swapping

1. **Swap only.** `0x649d422`:
   `mov rax,[rdi+0x20]; mov rsi,[rdi+0x28]; mov rdi,rax; jmp eglSwapBuffers@plt`.
   Two reads, no window, no JNI, no draw. A swap alone never proves a frame.
2. **Vtable methods on the same context** (`0x71912e8`, slots at `+8*i`; slot[3]
   at `0x7191300 = 0x649d422`, per `render-map.md` APS2 addend decode and
   `render_guest.c`'s runtime vtable check):
   * [0] `0x649d2bc` teardown — `+0x00/+0x10/+0x18/+0x20/+0x28/+0x30`;
   * [2] `0x649d37c` rebind — `eglGetCurrentContext()` vs `+0x30`, then
     `+0x10/+0x20/+0x28`;
   * [3] `0x649d422` swap — `+0x20/+0x28`;
   * [5] `0x649d432` window rebind — `+0x18` read/release, `+0x18` write,
     `+0x40 = 1`;
   * [6] `0x649d45e` — `+0x44 > 0`;
   * [8] `0x649d466` — `+0x40`; if 1, destroy/recreate surface, re-query
     `+0x38/+0x3c`, clear `+0x40`, and **return the 64-bit qword at `+0x38`**
     (`649d58a: mov rax,[rbx+0x38]`) — this is how the guest reads the packed
     surface size.
3. **GLES entry points, not context fields.** The loader `0x649c633..0x649d300`
   stores `eglGetProcAddress` results into the extension-pointer globals
   (`0x77a4f08/10/18…`); `0x649d246` is the two-name fallback helper. The draw
   dispatcher `0x6496d0c` later calls through imported JUMP_SLOTs `0x7229fc8`
   (glClear), `0x722a040` (glDrawElements), `0x722a048` (glDrawArrays) and the
   extension table `0x77a4fe0/8`, `0x77a4fa0/8/b0`, `0x77a4f98`, `0x77a4f78`
   (R1 §4). None of these read the 0x48 context directly.
4. **The context pointer itself is reached via the guest's render device.**
   The guest caller at `0x648b94c` stores `ctx` at `device+0x160`, stores the
   `ANativeWindow*` at `device+0x158`, and builds a 0x20-byte delegate at
   `device+0x168` with vptr `0x71908e0` and `delegate+0x18 = ctx`. Accessor
   thunks `0x6490132` (`rdi=[dev+0x160]; jmp [vptr+0x10]` → vtable[2]) and
   `0x6490140` (`window=[dev+0x158], old_ctx=[dev+0x160]` →
   `0x649c21e(window, old_ctx)`, then vtable[2]) show the frame/rebind path
   uses these device fields, not a global context pointer.

---

## 4. Comparison with render_guest / Route-B synthetic state

### 4.1 Context object

* `render_guest.sober_render_guest_create_context()` calls genuine
  `0x649c21e(s_anw, pbuffer=0)` and verifies `ctx+0 == base+0x71912e8`. The
  returned object is **guest-built**, so all 9 fields in §1.2 are present and
  correct — there is no missing context field on this path. GOT hooks let its
  `eglGetDisplay`/`eglCreateContext`/`eglCreateWindowSurface`/`eglMakeCurrent`
  reach the native drawable; `gles_loads=48`, `draw_wrapped=5` confirm it ran
  (R1 §0, §4).
* **But the pointer never enters the guest's own render device.** The host
  keeps it in `s_ctx` and uses it only for `swap_via_vtable()`. The guest's
  `device+0x158/+0x160/+0x168` seeding is performed by guest code at
  `0x648b940..0x648b98a`, which our direct host call bypasses. Any guest frame
  path that starts from `device+0x160` therefore cannot see this context.
  *(Inferred; the exact device-object address is a stack object in the
  `nativeAppBridgeV2StartAppWithParams` caller and was not pinned to a global
  in this pass.)*
* `guest_render.c:347-353` builds a **second, host-side** `uint64_t ctx[9]`
  (0x48 bytes), zeroed, filling only `ctx[4]` (`+0x20`) and `ctx[5]` (`+0x28`),
  then calls `0x649d422` directly. That is safe *only* for the flat swap
  wrapper. If this stack context were ever passed to vtable[0]/[2]/[5]/[6]/[8]
  it would read a NULL vptr and NULL `+0x30`, and vtable[8] would additionally
  see `+0x40 == 0` and return `+0x38 == 0` (packed size 0).
  `render_guest.h` correctly documents `+0x20/+0x28/+0x30/+0x38/+0x3c` for the
  context but omits `+0x08/+0x0c/+0x40/+0x44`.

### 4.2 nativeData object — concrete wrong fields

* **Missing from the header:** `+0x90` (onSurfaceChanged), `+0x148/+0x14c`
  (cached width/height), `+0xb8` (onConfigurationChanged). The struct is
  calloc'd, so these are all zero/NULL; the guest NULL-tests the callbacks, so
  no crash, but the notifications are lost.
* **`+0x140` is NULL after `run()`**: created sets it, changed re-sets it,
  destroyed clears it (`0x2a20a6a`). The redraw thunk checks `+0x140` first
  (`0x2a20a0c..0x2a20a0f`) and only then `+0x98` (`0x2a20a11`). Hence even if
  a genuine guest callback were installed at `+0x98`, driving `0x2a20a00`
  would still return early. This is the sharpest single "wrong field" in the
  synthetic state.
* Ordering: a real lifecycle is *created → changed → redraw … → destroyed at
  teardown*. `rg_bind_surface()` compresses all four into one setup call, so it
  ends in the "destroyed" state before EGL context creation and before the
  host's swap. *(Observed order; "wrong" is an inference about intended
  lifecycle.)*
* `+0x10` holds `s_dummy_env` (a 16-byte host buffer), used as the `JNIEnv*`
  for `ANativeWindow_fromSurface`. The GOT hook ignores that argument, so this
  is harmless today, but it is not a real JNIEnv if any unhooked guest path
  uses it.

### 4.3 Guest-shaped ANativeWindow

`SoberGuestANativeWindow`'s first three fields match the compat shim
(`refs/width/height`), and `refs=0x10000` keeps it alive across the replayed
release calls — both correct. One cosmetic/type error: `vtable` is set to
`base+0x71912e8`, which is the **EGL render-context** vtable, not an
`ANativeWindow` vtable. It is never dereferenced because all four
ANativeWindow entry points (`fromSurface/acquire/release/getWidth/getHeight`)
are GOT-interposed, but it becomes a real wrong field if an unhooked method is
reached.

---

## 5. Ranked candidate causes of suppressed guest draws

All causal claims are **inferred**; the underlying field states are **observed**
in disassembly and in `render_guest.c`. These diagnose observability of a guest
frame path, not eligibility of any marker.

| # | Gap | Evidence | Effect |
| --- | --- | --- | --- |
| 1 | `nativeData+0x140 == NULL` after `bind_surface()` | `0x2a20a6a` zeroes it; `render_guest.c` calls destroyed last | Redraw thunk returns at `0x2a20a0f` before reaching `+0x98`; no guest frame trigger |
| 2 | `+0x88/+0x90/+0x98/+0xa0 == NULL` | calloc'd synthetic object; guard sites `0x2a20897`, `0x2a209be`, `0x2a20a18`, `0x2a20a4f` | No engine notification of surface created/changed/redraw/destroyed ⇒ no frame loop, no `call [vtable+0x18]` ⇒ `swaps_guest_tagged` stays 0 (matches R1 §4) |
| 3 | Guest device fields `+0x158/+0x160/+0x168` never seeded | `0x648b943..0x648b98a` is guest code the host call bypasses | Guest frame code walking `device+0x160` cannot locate the real context |
| 4 | Route-B `ctx[9]` fills only `+0x20/+0x28` | `guest_render.c:347-353` | Safe for `0x649d422` only; any other vmethod sees NULL vptr/context/size |
| 5 | Context `+0x40/+0x44` undocumented | this report §1.2 | Latent only: a hand-built context that sets `+0x40=1` would trigger a full surface recreate; `+0x44` drives vtable[6] |
| 6 | `+0x10` share semantics misunderstood | `0x649cf9a`, `0x649c37d`, `0x649d1c9`, `0x649d319`, `0x6490157` | Any future attempt to pass a non-zero "pbuffer" must supply a real parent context pointer with valid `+0x20`/`+0x30` |

The observed run already records the downstream symptoms:
`life=1/1/1/1`, `from_surface=2`, `gles_loads=48`, `draw_wrapped=5`,
`draw_calls=0`, `swaps=1/0/1` (R1 §0). This report explains *why* the chain
stops at the callback/window link; it does not change the counts, install
callbacks, or claim a frame was rendered.

---

## 6. Limits / open items

* Ghidra `analyze_function` could not resolve the RVAs (image base `0x100000`,
  no procedure at `0x649c21e`/`0x749c21e`); all field facts here come from
  targeted raw `objdump`, which is exact but un-named — labels such as
  `create_context`, `egl_init`, `onSurfaceRedrawNeeded` are descriptive, not
  recovered symbols.
* The vtable slot map (`0x71912e8`, `[3]@0x7191300 = 0x649d422`, `[8]=0x649d466`)
  is **attributed prior evidence** (APS2 packed-RELA addend decode in
  `render-map.md`, runtime check in `render_guest.c`); its file bytes are zero
  and I did not re-decode the packed relocations in this pass.
* `+0x08` has no observed reader in this cluster; `+0x0c` is not initialized by
  `egl_init`. Both are reported as "unobserved", not as "unused".
* Whether a non-NULL `+0x98` pointing at a genuine guest callback would by
  itself start a frame loop was **not** runtime-tested; doing so would seed
  synthetic guest state and remains subject to decision 013/038 withhold.
* The render device object holding `+0x158/+0x160/+0x168` is stack-allocated in
  a large constructor beginning near `0x648b4a9`; I did not pin a stable global
  address for it. Seeding it may require replaying that guest constructor
  rather than a single pointer store.

## 7. Final answer

The guest EGL context is exactly **0x48 bytes / 9 slots**:
`+0x00` vptr `0x71912e8`, `+0x08` dword 0, `+0x10` *shared-parent context
pointer* (not a pbuffer boolean), `+0x18` `ANativeWindow*`, `+0x20` `EGLDisplay`,
`+0x28` `EGLSurface`, `+0x30` `EGLContext`, `+0x38`/`+0x3c` width/height,
**`+0x40` byte surface-invalidated flag** and **`+0x44` dword swap interval**
(the last two missing from existing docs). The swap wrapper reads only
`+0x20/+0x28`; the other vmethods read `+0x10/+0x18/+0x30/+0x38/+0x3c/+0x40/+0x44`,
and the guest reaches the context through its device at `+0x160` (window at
`+0x158`, delegate at `+0x168`). The nativeData object uses
`+0x10` JNIEnv, callbacks `+0x88/+0x90/+0x98/+0xa0` (plus `+0xb8`),
`+0x140` `ANativeWindow*`, and `+0x148/+0x14c` width/height.

`render_guest`'s *context* is genuine and complete, but its *nativeData* ends in
a destroyed state with `+0x140 == NULL` and all callbacks NULL, and the
guest-side device context pointer is never seeded; the Route-B stack context is
valid only for the flat swap. Those are the observed-state gaps that most
plausibly suppress guest draws — consistent with, and explanatory of, R1's
honest `draws=0 / guest_tagged=0`. No marker eligibility changes; withhold stands.
