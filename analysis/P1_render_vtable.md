# P1 — Full decode of the guest render vtable `0x71912e8`

Author: **P1** (group B: pipeline mapping). Scope: **this file only** —
`analysis/P1_render_vtable.md`. Analysis-only; no `src/`, engine, gate, test or
CMake file was touched. Engine frozen. Marker **WITHHELD** (decisions 013/038).

> **Front-loaded answer.** The render vtable at RVA `0x71912e8` has **exactly
> nine** virtual methods, `[0]..[8]`, at `0x71912e8 + 8*i`. Every slot is an
> `R_X86_64_RELATIVE` APS2 relocation whose addend is the target RVA (type `8`,
> sym `0`). The **only slot that presents a guest frame is `[3]` at
> `0x7191300 = 0x649d422`** — `mov rax,[rdi+0x20]` (EGLDisplay),
> `mov rsi,[rdi+0x28]` (EGLSurface), `mov rdi,rax`, `jmp eglSwapBuffers@plt
> 0x6d334d0`. A real guest frame is therefore a
> `call QWORD PTR [vtable+0x18]` whose return address lands in guest `.text`;
> the GLES *draw* work is not a vtable slot at all — it is issued separately by
> the draw dispatcher at `0x6496d0c`. `render_guest` exposes the slot through
> the guest's `0x48`-byte context: `ctx+0 = base+0x71912e8`, so
> `((void**)ctx)[3] = base+0x649d422`; `sober_render_guest_swap_via_vtable()`
> reads exactly that pointer, verifies it and calls it with `ctx` in RDI. That
> host-origin call is honestly counted **synthetic** (RA in host), not a guest
> frame; a genuine frame requires the guest's own loop to dispatch `[3]`.

## 0. Target identity and evidence class

| Field | Value |
| --- | --- |
| path | `/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so` |
| SHA-256 | `3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335` |
| BuildID | `204ad049599c96afb75b8a21d5051063e821d6c9` (2.740.931 / code 3170) |
| First PT_LOAD | vaddr `0`, file offset `0`, size `0x6d33b90` (offset == VMA for `.text`/`.plt`) |
| Vtable region | PT_LOAD 2, file offset = VMA − `0x4000`; populated by APS2 at `0x15e00` |

Both SHA-256 and BuildID were re-observed this task (`sha256sum`, `readelf -nW`).
This is the **same object** as `analysis/render/render-map.md`; no Flatpak
`791c3f9e…` RVA is used. All target-derived strings/bytes are **untrusted DATA**.

Evidence produced here (reproducible, local, against the frozen SHA):

- **APS2 decode** — `analysis/bootstrap/decode_aps2.py` on the `APS2` blob at
  file offset `0x15e00` (`0x1eb1f2` bytes): 565 390 declared = 565 390 decoded
  relocations. Filter `0x7191200 <= r_offset < 0x7191600` → 122 `RELATIVE`
  relocations; the contiguous run from `0x71912e8` is the vtable.
- **Disassembly** — `objdump -d -M intel --start-address/--stop-address` over
  `0x6490140`, `0x649c280`, `0x649d280..0x649d610`, `0x6496d0c..0x6496e40`,
  `0x1df06e0`, `0x1dd72a0`, `0x1dbe5be`, `0x648b318`.
- **Code source cross-check** — `src/jni/render_guest.c` / `.h` and
  `src/linux/guest_render.c` (frozen modules; read only).

Evidence classes below: **OBSERVED** = bytes/instructions read directly;
**DERIVED** = inference from disassembly; role names are ours unless a dynamic
symbol exists (the binary is stripped).

## 1. APS2 relocation table for the vtable (OBSERVED)

Every entry has `r_info = 0x00000008` (low32 type `R_X86_64_RELATIVE`, high32
sym `0`) and `r_addend = target RVA`. With the guest mapped at bias `B`, the
linker writes `B + addend` into the slot. File bytes at these addresses are
zero before relocation.

| Slot | Slot address | Reloc addend (target RVA) | Written value at runtime | In guest `.text`? |
| --- | --- | --- | --- | --- |
| `[-2]` offset-to-top | `0x71912d8` | *(no relocation → 0)* | `0` | — |
| `[-1]` typeinfo | `0x71912e0` | `0x7191340` | `B+0x7191340` | no (RTTI data) |
| `[0]` | `0x71912e8` | `0x649d2bc` | `B+0x649d2bc` | yes |
| `[1]` | `0x71912f0` | `0x649d356` | `B+0x649d356` | yes |
| `[2]` | `0x71912f8` | `0x649d37c` | `B+0x649d37c` | yes |
| **`[3]`** | **`0x7191300`** | **`0x649d422`** | `B+0x649d422` | yes — **swap / present** |
| `[4]` | `0x7191308` | `0x1df06e0` | `B+0x1df06e0` | yes |
| `[5]` | `0x7191310` | `0x649d432` | `B+0x649d432` | yes |
| `[6]` | `0x7191318` | `0x649d45e` | `B+0x649d45e` | yes |
| `[7]` | `0x7191320` | `0x1dd72a0` | `B+0x1dd72a0` | yes |
| `[8]` | `0x7191328` | `0x649d466` | `B+0x649d466` | yes |
| *(next object)* `[+9]` | `0x7191330` | `0x6db7968` | — | no → **not a method**; end of this vtable |

**Extent proof.** Slots `[0]..[8]` are nine consecutive code addresses. The
next relocation at `0x7191330` has addend `0x6db7968 > 0x6d33b90` (past the
first `PT_LOAD`, i.e. data/RTTI), so it cannot be a virtual method. The vtable
therefore ends at `[8] = 0x7191328`. Its header is the standard Itanium layout:
offset-to-top `0` at `0x71912d8` and typeinfo `0x7191340` at `0x71912e0`
(a `__class_type_info` instance `[0x6db79b8, 0xc92d60]`, **DERIVED**). Note
`0x649d2bc` (slot `[0]`) *reinstalls* `0x71912e8` at `ctx+0`, and after teardown
installs `0x7191368` — a separate "dead state" vtable whose first method is
`0x649d5e6`. So `0x71912e8` is the **live** render-interface vtable.

## 2. Every slot decoded (RVA, argument shape, role)

`this` = the guest's `0x48`-byte render context returned by `0x649c21e`
(layout in §4). SysV AMD64: `this` in RDI, first explicit arg in RSI.

### `[0] 0x649d2bc` — `void teardown(void *this)` (EGL destroy + demote)
OBSERVED `0x649d2bc..0x649d34a`. Installs `0x71912e8` at `ctx+0`; picks the
pbuffer sub-object at `+0x10` if non-NULL else `this`; then:
`eglMakeCurrent(dpy=[+0x20], EGL_NO_SURFACE/EGL_NO_CONTEXT, 0,0,0)`
(`0x649d2e4`), `eglDestroyContext(dpy, ctx=[+0x30])` (`0x649d2fc`),
`eglDestroySurface(dpy, surf=[+0x28])` (`0x649d314`), and when `[+0x10]==0`
also `eglTerminate(dpy)` + `ANativeWindow_release(win=[+0x18])`
(`0x649d320/0x649d32d`). Then writes the dead vtable `0x7191368` to `+0` and
calls `0x648b318` (GL-object cleanup using a function pointer at `0x77a4ef8`).
**Args:** RDI=`this` only. **Role:** destructor/close of the render context.

### `[1] 0x649d356` — `void destroy(void *this)` (teardown + free)
OBSERVED `0x649d356..0x649d372`: `call 0x649d2bc` (slot `[0]`), then
`call 0x1dbe5be` (allocator free bridge). **Args:** RDI=`this`. **Role:**
destructor that also releases the `0x48`-byte allocation.

### `[2] 0x649d37c` — `void rebind(void *this)` (EGL make-current)
OBSERVED `0x649d37c..0x649d420`. `eglGetCurrentContext()` vs `[this+0x30]`;
if different → `eglMakeCurrent([+0x20], [+0x28], [+0x30], ctx)` (`0x649d3b0`);
on failure logs through `0x231bab5` with `eglGetError()` and tail-jumps to
`0x648b364`. **Args:** RDI=`this` only. **Role:** re-establish this context as
current. Real guest-side dispatch of this slot is OBSERVED at `0x6490140`:
`call QWORD PTR [rax+0x10]` (= `vtable[2]`) after `0x649c21e` returns the ctx —
proof that these slots are invoked with `this` in RDI by guest code.

### `[3] 0x649d422` — `EGLBoolean swap(void *this)` — **FRAME / PRESENT ENTRY**
OBSERVED, exact bytes: `48 8b 47 20 / 48 8b 77 28 / 48 89 c7 / e9 … 6d334d0`.
Reads EGLDisplay from `ctx+0x20` into RDI, EGLSurface from `ctx+0x28` into RSI,
then **tail-jumps** (`jmp`, not `call`) to `eglSwapBuffers@plt` `0x6d334d0`, whose
`R_X86_64_JUMP_SLOT` is `0x722a1a8`. **Args:** RDI=`this` only; no JNIEnv, no
window at call time; all presentation state lives in the `0x48` context.
**Role:** the sole present/frame-boundary method of this interface. Because it
is a tail `jmp`, the return address seen at the `eglSwapBuffers` GOT hook is
whoever called `0x649d422` — guest frame loop (genuine) or our harness (synthetic).

### `[4] 0x1df06e0` — `bool shouldDisplayOpenGLUnsupportedMessage(...)`
OBSERVED: `xor eax,eax; ret` — constant **false**. Carries the dynamic symbol
`Java_com_roblox_engine_jni_NativeGLInterface_shouldDisplayOpenGLUnsupportedMessage`
(the stub at the symbol entry). **Args:** ignored. **Role:** capability/UI
query stub; no rendering effect.

### `[5] 0x649d432` — `void setWindow(void *this, void *new_window)`
OBSERVED `0x649d432..0x649d45d`: `ANativeWindow_release([+0x18])`, store RSI
into `+0x18`, `ANativeWindow_acquire(RSI)`, set byte `[+0x40] = 1`. **Args:**
RDI=`this`, RSI=new `ANativeWindow*`. **Role:** swap/replace the backing native
window and flag that the surface must be recreated (`+0x40`).

### `[6] 0x649d45e` — `bool queryFlag(void *this)`
OBSERVED: `cmp DWORD PTR [rdi+0x44],0; setg al; ret` → returns `[+0x44] > 0`.
**Args:** RDI=`this`. **Role:** boolean state query over the context's `+0x44`
counter/flag.

### `[7] 0x1dd72a0` — `void noop(...)`
OBSERVED: bare `ret` — no side effects, returns whatever is in RAX. (objdump
attaches the dynsym label `Java_com_roblox_client_LocalStorageManager_initStorageManagerNative`
to this address; the first instruction of that address range is `ret`, so the
label is misleading — treat as a no-op vmethod.) **Args:** ignored.
**Role:** no-op hook (e.g. fragment-start notification). No draw/present effect.

### `[8] 0x649d466` — `int recreateSurface(void *this)` (surface update)
OBSERVED `0x649d466..0x649d596`. If byte `[+0x40]==0`, returns width `[+0x38]`
immediately. Otherwise: (optional log), `eglMakeCurrent(dpy,0,0,0)` to unbind,
`eglDestroySurface(dpy, surf=[+0x28])`, `0x649cf72` to choose a config,
`0x649d1c2(this, config)` to build a new surface → store at `+0x28`,
`eglMakeCurrent(dpy, surf, surf, ctx=[+0x30])`, then `eglQuerySurface` width
`0x3057` → `+0x38` and height `0x3056` → `+0x3c`, clear `[+0x40]=0`, return
width. **Args:** RDI=`this`. **Role:** recreate/rebind the EGLSurface after a
window change; returns new width. This is the "surface" half of the pipeline;
it does **not** present a frame.

## 3. Frame vs. draw: which slot actually produces a guest frame

- **Present = slot `[3]`.** `0x7191300 = 0x649d422 = call [vtable+0x18]` →
  `eglSwapBuffers@plt 0x6d334d0` → GOT `0x722a1a8`. This is the *only* vtable
  slot that crosses the frame boundary. Slots `[0]`/`[1]` destroy, `[2]` rebind,
  `[5]`/`[8]` reconfigure the surface, `[4]`/`[6]`/`[7]` are queries/no-ops.
- **Draw is not a vtable slot.** GLES work comes from the ~70 `eglGetProcAddress`
  loads at `0x649c633…` and the separate dispatcher `0x6496d0c`. OBSERVED at
  `0x6496dbc` → `glDrawElements@plt 0x6d33200`, `0x6496e1a` →
  `glDrawArrays@plt 0x6d33210`, plus function-pointer calls `0x77a4fe0/8`
  (glDrawElements variants) and `0x6494ae1`/`0x77a4f78` (glClear family, RVAs
  per `analysis/R1_zero_tagged_swaps.md §0`, not re-disassembled here); the
  dispatcher takes ~6 integer args (RDI, ESI, EDX, ECX, R8D, R9D) describing
  draw mode/count/type and ends with a `glGetError` check. A real frame issues
  these draws, then dispatches vtable `[3]` to present.
- **Genuine vs synthetic discriminator is the RA.** Because `0x649d422` tail-`jmp`s
  to the PLT/GOT, the RA captured in `rg_hook_swap()` (`src/jni/render_guest.c`)
  is the *caller of the wrapper*. A guest frame loop doing
  `call [vtable+0x18]` leaves RA in guest `.text` (`base ≤ ra < base+0x6d33b90`)
  → `guest-tagged` swap. Our harness calling `0x649d422` directly leaves RA in
  the host → `synthetic`. This matches `analysis/R1_zero_tagged_swaps.md` and
  `analysis/C04-render-integration.md §2.3` exactly.
- **Honest limit (UNKNOWN):** the exact guest function containing the
  `call [vtable+0x18]` for slot `[3]` is not statically pinned — the binary has
  ~11,600 `call *0x18(%reg)` sites and the vtable has no direct code xref. The
  one real guest vtable dispatch we did pin is slot `[2]` at `0x6490165`.
  Discovering the frame-loop call site requires runtime RA capture (T4-4 step
  E4 / T3-5 §4), not a static guess. No guest frame loop is proven to run in
  any current log; `swaps(total/guest/synth)=1/0/1`, `draws=0` is the correct
  synthetic outcome.

## 4. How `render_guest`'s context pointer exposes the swap slot

OBSERVED from `src/jni/render_guest.c` / `.h` (frozen modules):

1. `sober_render_guest_create_context()` calls genuine guest
   `0x649c21e(anw, pbuffer=0)`; guest `0x649c272` installs the vtable at
   `0x649c29f` (`lea rcx,[0x71912e8]; mov [rdi],rcx`) and returns a
   `0x48`-byte context. The module verifies `*(void**)ctx == base+0x71912e8`
   (`SOBER_RENDER_GUEST_ERR_VTABLE` otherwise).
2. Guest context layout (render-map.md §3b, re-confirmed by `0x649d422`,
   `0x649d2bc`, `0x649d466` accesses):

   ```
   +0x00  vtable* = base+0x71912e8        -> [3] at vtable+0x18 = base+0x649d422
   +0x10  pbuffer object* (0 => window surface)
   +0x18  ANativeWindow*
   +0x20  EGLDisplay       (used by swap)
   +0x28  EGLSurface       (used by swap)
   +0x30  EGLContext
   +0x38  width    +0x3c  height   +0x40  surface-dirty flag  +0x44  counter
   ```

3. Accessors expose the chain: `sober_render_guest_context()` → `s_ctx`,
   `sober_render_guest_context_vtable()` → `s_ctx_vt = *(uintptr_t*)ctx`,
   `sober_render_guest_base()` → `s_base`.
4. **`sober_render_guest_swap_via_vtable()` is the concrete exposure of slot
   `[3]`:** it re-verifies `s_ctx_vt == base+0x71912e8`, reads
   `slot = *(uintptr_t*)(vt + 0x18)` (`SOBER_RENDER_GUEST_VTABLE_SWAP_OFF =
   0x18`), re-verifies `slot == base+0x649d422`, then calls
   `((int(*)(uintptr_t))slot)(s_ctx)`. So the swap entry is reachable *only*
   via the guest's own vtable pointer embedded at `ctx+0`; `render_guest`
   never fabricates a vtable and never installs a host function pointer in one.
5. **Honesty boundary:** that call originates in the host, so the GOT hook
   tags it `synthetic` — it exercises the real guest slot with real guest/EGL
   handles but is **not** a guest frame. `render_guest` deliberately does not
   emit the marker, and its counters are diagnostics. A genuine guest frame
   requires the guest's own render/redraw loop to call `[vtable+0x18]`; the
   redraw thunk `0x2a20a00` currently has a NULL `[nativeData+0x98]` callback,
   so no such loop runs, and decisions 013/038 stay WITHHELD.

## 5. Slot-to-pipeline map (compact)

```
0x649c21e create_context(window, 0) -> 0x48B ctx; [ctx+0] = 0x71912e8
   |
   +-- [0] 0x649d2bc  destroy EGL ctx/surface/terminate + install dead vt 0x7191368
   +-- [1] 0x649d356  [0] + free
   +-- [2] 0x649d37c  eglMakeCurrent(rebind)        <- guest dispatch pinned @0x6490165 (call [vt+0x10])
   +-- [3] 0x649d422  SWAP/PRESENT                  <- FRAME: call [vt+0x18]
   |             mov rdi,[ctx+0x20] (EGLDisplay); mov rsi,[ctx+0x28] (EGLSurface)
   |             jmp 0x6d334d0 eglSwapBuffers@plt -> GOT 0x722a1a8 (RA tag here)
   +-- [4] 0x1df06e0  shouldDisplayUnsupportedMessage -> 0 (stub)
   +-- [5] 0x649d432  setWindow(win): release/acquire +0x18, dirty=1 @+0x40
   +-- [6] 0x649d45e  query [+0x44] > 0
   +-- [7] 0x1dd72a0  no-op ret
   +-- [8] 0x649d466  recreateSurface: eglDestroy/CreateWindowSurface, query w/h, return width
```

Separate draw path (not a vtable method):
`0x6496d0c` dispatcher → `glDrawElements 0x6d33200` / `glDrawArrays 0x6d33210`
/ pointer tables `0x77a4fe0/8`, plus `glClear` ~`0x6494ae1`. GLES pointers
loaded via `eglGetProcAddress` GOT `0x722a180` (`0x649c633..0x649d300`).

## 6. Conclusion (final answer)

- **Vtable `0x71912e8` = 9 methods `[0]..[8]`**, all APS2 `R_X86_64_RELATIVE`
  (addend = target RVA), with Itanium header offset-top `0` / typeinfo
  `0x7191340`. Verified against the frozen SHA/BuildID.
- **Slot that produces a guest frame: `[3] @ 0x7191300 = 0x649d422`**, the
  flat swap wrapper that tail-jumps `eglSwapBuffers@plt`. It is the only
  present method; the draw work is the separate `0x6496d0c` GLES dispatcher.
- **Exposure:** the guest writes the vtable pointer at `ctx+0`; render_guest's
  `sober_render_guest_context()` returns that `0x48`-byte ctx and
  `sober_render_guest_swap_via_vtable()` loads `((void**)ctx)[3]` (i.e.
  `*(vt+0x18)`), verifies it equals `base+0x649d422`, and calls it with `ctx`.
- **Gate consequence:** calling that pointer from the host is a synthetic swap
  (RA in host) and does **not** prove a guest frame. Only a guest `.text` RA at
  the `eglSwapBuffers` GOT `0x722a1a8` (i.e. a guest `call [vtable+0x18]` after
  real GLES draws) qualifies; none is observed. Marker stays **WITHHELD**.
