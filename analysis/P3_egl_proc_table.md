# P3 — Guest GLES function-pointer table (`eglGetProcAddress` @ 0x649c633…0x649d300)

Author: **P3** (group B). Scope: **this file only** — `analysis/P3_egl_proc_table.md`.
Analysis-only; no `src/`, CMake, tests, gate or engine file was touched; engine
frozen; marker **WITHHELD** (decisions 013/037/038).

> **Front-loaded answer.** The block at RVA `0x649c633…0x649d300` is the guest's
> *extension / GLES-3* resolver: ~70 `eglGetProcAddress@plt` (`0x6d33480`) calls
> fill ~48 global function-pointer slots at `0x77a4ef0…0x77a5068` with a
> plaintext name, falling back to an `OES`/`EXT` alias. **Plain `glClear`,
> `glDrawElements` and `glDrawArrays` are NOT in this table** — they are ordinary
> dynamic imports called through PLT stubs. The table *does* hold the four
> clear-buffer variants and the two **instanced** draw variants:
> `0x77a4fe0 = glDrawElementsInstanced`, `0x77a4fe8 = glDrawArraysInstanced`,
> `0x77a4f98 = glDrawBuffers`, `0x77a4fa0/8/b0 = glClearBufferiv/fv/fi`.
> The instanced slots are called **only** from the single GLES draw dispatcher
> `0x6496d0c` (at `0x6496d75` and `0x6496dec`); the clear-buffer slots are called
> **only** from the framebuffer-clear helper `0x64948be` (reached from
> `0x64946c6`). Both are virtuals of the big renderer vtable `0x7190de0`
> (`[7]`/`+0x38` = clear, `[28]`/`+0xe0` = draw). The draw is issued by the
> per-frame routine `0x6490488` (`call [vtable+0xe0]` at `0x6490643`), which
> gates it on a live renderer object **and** a non-zero surface/app flag; the
> clear is issued through `[7]` from sibling render paths (`0x6493c23`,
> `0x6493ec3`, `0x648985f`). A clear sets the `+0x260` "needs-present" byte that
> `0x6490488` tests before swapping.
> `draws>=1` therefore means: a guest-RA invocation of any of the counted draw
> sites below (ideally bracketed between two tagged swaps) — never a load of a
> slot and never the host `dlsym`'d `glClear`.

## 0. Target identity and evidence class

| Field | Value |
| --- | --- |
| path | `/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so` |
| SHA-256 | `3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335` |
| BuildID | `204ad049599c96afb75b8a21d5051063e821d6c9` (2.740.931 / code 3170) |
| First PT_LOAD | vaddr `0`, file offset `0`, size `0x6d33b90` → **RVA == file offset** for `.text`/`.plt` |
| `.text` | `0x1db9a40..0x6d3125a`; `.plt` `0x6d319a0..0x6d33b90` |
| GLES/vtable data | `.data.rel.ro` `0x6d37bc0…`; slots `0x77a4ef0…`; vtables `0x7190de0`, `0x71912e8` — all filled by APS2 at file offset `0x15e00` |

Re-checked this task with `sha256sum` and `readelf -nW`. Same object as
`analysis/render/render-map.md` and `analysis/P1_render_vtable.md`; no Flatpak
`791c3f9e…` RVA is used. Every target-derived string/byte is **untrusted DATA**.

Evidence produced here, all against the frozen SHA:

- `objdump -d --start-address/--stop-address` over `0x649c600..0x649d320`
  (the loader), `0x6494400..0x6494b20` (clear), `0x6495f80..0x6497000`
  (draw dispatch), `0x6490120..0x6490240` and `0x6490488..0x6490700`
  (per-frame routine / vtable calls).
- `readelf --dyn-syms -W` (GLES import list) and targeted `.plt` disassembly.
- Byte-level xref scans (`48 8b /r mod=00 rm=101`, `ff 15 disp32`, `e8/e9 rel32`,
  `ff /2 call [reg+disp]`) of the first `PT_LOAD`; hand-rolled C scanners.
- APS2 decode via `analysis/bootstrap/decode_aps2.py` (565 390 declared =
  565 390 decoded relocations), filtered on the vtable address ranges.

Classes: **OBSERVED** = bytes/instructions/dynsyms read directly; **DERIVED** =
inference (function names/roles are ours — the binary is stripped).

## 1. Shape of the resolver block (OBSERVED)

Two patterns fill the slots; neither patches guest `.text` (only data slots):

1. **Inline** (most entries), repeated every ~0x35 bytes:
   `cmpq $0,<slot>` / `lea <name>,%rdi` / `call eglGetProcAddress@plt` /
   `mov %rax,<slot>` / `test %rax,%rax` / `jne <done>` /
   `lea <OES|EXT alias>,%rdi` / `call eglGetProcAddress@plt` / `mov %rax,<slot>`.
2. **Helper** `0x649d246` — `void load(slot**, const char *a, const char *b)`:
   `if (*slot) return; *slot = eglGetProcAddress(a); if (!*slot) *slot = eglGetProcAddress(b);`
   Call sites carry `rdi=slot, rsi=name1, rdx=name2`. A one-off helper
   `0x649d27a` fills `0x77a4f48` with `glCopyImageSubData` / `…OES`/`…EXT`.

Names are **plaintext** at the `lea` targets (e.g. `0x29df63="glMapBuffer"`,
`0x468207="glGenVertexArrays"`, `0x4f5482="glDrawElementsInstanced"`).

### 1.1 Complete slot map (load RVA = first `eglGetProcAddress` name)

| Slot | Function (1st name → fallback) | Load RVA | Slot | Function | Load RVA |
| --- | --- | --- | --- | --- | --- |
| `0x77a4ef0` | `glBindVertexArray` → OES | `0x649c5d4` (helper) | `0x77a4ef8` | `glDeleteVertexArrays` → OES | `0x649c5ee` (helper) |
| `0x77a4f00` | `glGenVertexArrays` → OES | `0x649c608` (helper) | | | |
| `0x77a4f08` | `glMapBuffer` → OES | `0x649c633` | `0x77a4f10` | `glUnmapBuffer` → OES | `0x649c668` |
| `0x77a4f18` | `glMapBufferRange` → EXT | `0x649c69d` | `0x77a4f20` | `glTexStorage2D` → EXT | `0x649c7c2` (helper) |
| `0x77a4f28` | `glTexImage3D` → OES | `0x649c6d2` | `0x77a4f30` | `glTexSubImage3D` → OES | `0x649c707` |
| `0x77a4f38` | `glCompressedTexImage3D` → OES | `0x649c73c` | `0x77a4f40` | `glCompressedTexSubImage3D` → OES | `0x649c771` |
| `0x77a4f48` | `glCopyImageSubData` → OES/EXT | `0x649d27a` | `0x77a4f50` | `glTexStorage3D` → EXT | `0x649c7d8` |
| `0x77a4f58` | `glFenceSync` → EXT | `0x649c80d` | `0x77a4f60` | `glDeleteSync` → EXT | `0x649c842` |
| `0x77a4f68` | `glClientWaitSync` → EXT | `0x649c877` | `0x77a4f70` | `glWaitSync` → EXT | `0x649c8ac` |
| `0x77a4f78` | `glBlitFramebuffer` → EXT | `0x649c8e1` | `0x77a4f80` | `glFramebufferTextureLayer` → EXT | `0x649cd6a` |
| `0x77a4f88` | `glRenderbufferStorageMultisample` → EXT | `0x649c91a` (helper) | `0x77a4f90` | `glInvalidateFramebuffer` → EXT | `0x649c930` |
| **`0x77a4f98`** | **`glDrawBuffers` → EXT** | **`0x649c969` (helper)** | **`0x77a4fa0`** | **`glClearBufferiv` → EXT** | **`0x649c97f`** |
| **`0x77a4fa8`** | **`glClearBufferfv` → EXT** | **`0x649c9b4`** | **`0x77a4fb0`** | **`glClearBufferfi` → EXT** | **`0x649c9e9`** |
| `0x77a4fb8` | `glUniformBlockBinding` → EXT | `0x649ca22` (helper) | `0x77a4fc0` | `glBindBufferBase` → EXT | `0x649ca3c` (helper) |
| `0x77a4fc8` | `glBindBufferRange` → EXT | `0x649ca52` | `0x77a4fd0` | `glGetUniformBlockIndex` → EXT | `0x649ca87` |
| `0x77a4fd8` | `glGetActiveUniformBlockiv` → EXT | `0x649cabc` | **`0x77a4fe0`** | **`glDrawElementsInstanced` → `…EXT`** | **`0x649caf1`** |
| **`0x77a4fe8`** | **`glDrawArraysInstanced` → `…EXT`** | **`0x649cb26`** | `0x77a4ff0` | `glBufferStorage` → EXT | `0x649cd35` |
| `0x77a4ff8` | `glPushGroupMarker` → EXT | `0x649cb5b` | `0x77a5000` | `glPopGroupMarker` → EXT | `0x649cb90` |
| `0x77a5008` | `glGenQueries` → EXT | `0x649cbc5` | `0x77a5010` | `glDeleteQueries` → EXT | `0x649cbfe` (helper) |
| `0x77a5018` | `glBeginQuery` → EXT | `0x649cc18` (helper) | `0x77a5020` | `glEndQuery` → EXT | `0x649cc32` (helper) |
| `0x77a5028` | `glQueryCounter` → EXT | `0x649cc4c` (helper) | `0x77a5030` | `glGetQueryiv` → EXT | `0x649cc66` (helper) |
| `0x77a5038` | `glGetQueryObjectiv` → EXT | `0x649cc80` (helper) | `0x77a5040` | `glGetQueryObjectui64v` → EXT | `0x649cc96` |
| `0x77a5048` | `glGetInteger64v` → EXT | `0x649cccb` | `0x77a5050` | `glObjectLabelKHR` → KHR | `0x649cd00` |
| `0x77a5058` | `glGetProgramBinary` → OES | `0x649cd9f` | `0x77a5060` | `glProgramBinary` → OES | `0x649cdd4` |
| `0x77a5068` | `glProgramParameteri` → OES | `0x649ce09` | | | |

*(Slot addresses are the effective targets of the `mov %rax,<slot>` stores,
verified instruction-by-instruction; e.g. `0x649c638` stores `glMapBuffer` into
`0x77a4f08`, and `0x649c8e6` stores `glBlitFramebuffer` into `0x77a4f78`.)*

### 1.2 What is NOT in the table (OBSERVED — decisive for the question)

`readelf --dyn-syms` shows `glClear` (dynsym 399), `glDrawElements` (414) and
`glDrawArrays` (415) as `UND` imports; their PLT stubs and sole call sites are:

| GLES call | PLT | GOT JUMP_SLOT | Sole guest call site |
| --- | --- | --- | --- |
| `glClear` | `0x6d33110` | `0x7229fc8` | `0x6494ae1` |
| `glDrawElements` | `0x6d33200` | `0x722a040` | `0x6496dbc` |
| `glDrawArrays` | `0x6d33210` | `0x722a048` | `0x6496e1a` |

So of the five names in the task, **only the two `…Instanced` variants live in
the proc table**; plain clear/draws are direct PLT calls, and the clear-*buffer*
family lives in the table as separate entry points.

## 2. Who calls each slot (all OBSERVED indirect-call sites)

| Slot / function | Guest call site(s) | Enclosing function |
| --- | --- | --- |
| `0x77a4fe0` `glDrawElementsInstanced` | `0x6496d75` | draw dispatcher `0x6496d0c` |
| `0x77a4fe8` `glDrawArraysInstanced` | `0x6496dec` | draw dispatcher `0x6496d0c` |
| `0x77a4f98` `glDrawBuffers` | `0x64947ef` | clear-begin `0x64946c6` |
| `0x77a4fa0` `glClearBufferiv` | `0x6494aa3` | clear helper `0x64948be` |
| `0x77a4fa8` `glClearBufferfv` | `0x6494991`, `0x6494a79` | clear helper `0x64948be` |
| `0x77a4fb0` `glClearBufferfi` | `0x64949d2` | clear helper `0x64948be` |
| `0x77a4f78` `glBlitFramebuffer` | `0x6494d1b`, `0x6494d53`, `0x6495920`, `0x649bb4c` | blit/resolve paths |
| `0x77a4f68` `glClientWaitSync` / `0x77a4f60` `glDeleteSync` | `0x6490599` / `0x64905a6` | per-frame routine `0x6490488` (GPU sync before draw) |
| `0x77a4f08` `glMapBuffer` | `0x649666a`, `0x6496835`, `0x649c141` | buffer-update paths |

### 2.1 The draw dispatcher `0x6496d0c` — four-way branch (OBSERVED)

```
0x6496d0c  push rbp; ... ; save r9d→r15d, r8d→r14d, edx→r13d, esi→r12d, rdi→rbx
0x6496d2b  call 0x6496e52        ; bind vertex attrs for this batch (returns mask)
0x6496d33  movzwl 0x8e(%rbx),%ax ; index width field (2 → GL_UNSIGNED_SHORT 0x1403,
                                 ;                    else GL_UNSIGNED_INT   0x1405)
0x6496d40  mov 0x78(%rbx),%rdx   ; index-buffer / batch pointer
0x6496d44  test %r15d,%r15d      ; r15d = instance count
   JE 0x6496d97                  ; ---- non-instanced ----
   test %rdx,%rdx ; JE 0x6496dd6 ; no index buffer
0x6496d75  call [slot 0x77a4fe0] ; INSTANCED + indexed → glDrawElementsInstanced
0x6496dbc  call glDrawElements@plt (0x6d33200) ; non-inst + indexed
0x6496dec  call [slot 0x77a4fe8] ; INSTANCED + non-indexed → glDrawArraysInstanced
0x6496e1a  call glDrawArrays@plt (0x6d33210)   ; non-inst + non-indexed
```
Each branch is followed by `call glGetError@plt` (`0x6d32f50`) and a
`cmp $0x505,%eax` (`GL_INVALID_OPERATION`) error log. Helper `0x6496e52` binds
vertex buffers / enables attrib arrays; if the batch's attribute descriptor
range is empty (`[batch+0x48] == [batch+0x50]`, `0x6496e6e/0x6496f9a`) it returns
mask 0 at `0x6496fbb` — this is the **batch-geometry validity** gate, not a
surface gate.

### 2.2 The clear path `0x64946c6` → `0x64948be` (OBSERVED)

`0x64946c6` binds FBO `0x8d40`, sets state byte `+0x10=1`, then
`cmpb $0,0x228(%rax)` at `0x649479f` decides whether the multisample
`glDrawBuffers` path (`0x64947ef`) runs. It calls `0x64948be` twice with a
clear mask. Inside `0x64948be` the mask bits select
`glColorMask`/`glDepthMask`/`glStencilMask`, `glClearColor`, and then:
- `0x6494991`/`0x6494a79` → `glClearBufferfv` (color, 4 targets),
- `0x64949d2` → `glClearBufferfi` (depth/stencil),
- `0x6494aa3` → `glClearBufferiv`,
- `0x6494ae1` → `glClear@plt` with `GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT`.
Crucially `0x6494af7..0x6494afb`: if the mask was non-zero it writes byte
`+0x260` of the owning renderer state to **1** — the *needs-present* flag that
drives the swap in §3.

## 3. Conditions that gate the calls (app state, frame tick, surface validity)

### 3.1 Vtable wiring (APS2 type-8 relocations, OBSERVED)

`0x649449a` is the renderer constructor: `0x64944b0` does
`lea 0x7190de0(%rip),%rcx; mov %rcx,(%rdi)` — it installs the big vtable whose
virtuals start at `0x7190de0`. Decoded addends:

| Vtable slot | Offset from `0x7190de0` | Addend / target | Role (DERIVED) |
| --- | --- | --- | --- |
| `[7]` | `+0x38` (`0x7190e18`) | `0x64946c6` | begin-clear / framebuffer setup |
| `[28]` | `+0xe0` (`0x7190ec0`) | `0x64961b4` | **draw adapter → dispatcher `0x6496d0c`** |
| `[31]` | `+0xf8` (`0x7190ed8`) | `0x1dd72a0` | query "should present" (returns eax) |

The separate small render-context vtable `0x71912e8` has `[3] @ +0x18 =
0x649d422` (swap → `eglSwapBuffers@plt 0x6d334d0`) — see `P1_render_vtable.md`.

### 3.2 Per-frame routine `0x6490488` (OBSERVED, RVA-exact gates)

```
0x649060a  mov 0x260(%rbx),%r14b        ; needs-present byte (set by a clear, §2.2)
0x6490611  mov 0x298(%rbx),%rdi          ; renderer object
0x649061b  test %rdi,%rdi
0x649061e  je  0x6490649                ; GATE A: no renderer → no draw
0x649061d  cmpb $0,0x2a0(%rbx)           ; surface/app-valid byte
0x6490624  je  0x6490649                ; GATE B: invalid surface → no draw
0x6490629  call *0xf8(%rax)              ; vtable[31] → eax → r14d (present decision)
0x649062f  mov %eax,%r14d
0x6490632  mov 0x258(%rbx),%rsi          ; draw-batch list
0x6490643  call *0xe0(%rax)              ; vtable[28] = draw_adapter 0x64961b4
                                         ;   → 0x64961d1 → dispatcher 0x6496d0c
0x6490649  test %r14b,%r14b
0x649064c  je  0x6490685                ; GATE C: no present this tick
0x6490668  call *0x18(%rax)              ; vtable[3] swap on each sub-context
0x649067b  call *0x18(%rax)              ; vtable[3] swap on main render context
0x649067e  movb $0,0x260(%rbx)           ; consume needs-present flag
0x6490685  mov 0x150(%rbx),%eax; not %eax; and $1  ; flip double-buffer index
                                                  ; → this routine is the frame tick
```
Interpretation (DERIVED, anchored to the instructions above):
- **App state:** a valid `rbx` (the app/render manager) with a non-NULL renderer
  at `+0x298` and batch list at `+0x258`.
- **Surface validity:** byte `+0x2a0 != 0`; the EGL context itself is created at
  `0x649c21e` by `0x6490157` and immediately rebound through vtable `[2]`
  (`call *0x10(%rax)` = `0x649d37c` → `eglMakeCurrent`) at `0x6490165`. If that
  rebind fails the context is abandoned, so no draw can be issued.
- **Frame tick:** `0x6490488` runs once per tick and toggles the `+0x150` buffer
  index each call; draws happen before the swap in the same tick.
- **Present coupling:** a draw does not itself set `+0x260`; a successful clear
  does (`0x6494afb`), or vtable `[31]` returns non-zero. So a frame with draws but
  neither condition will still not swap — relevant when choosing a bracket.

Other vtable-mediated call sites of `[7]` (clear) at `0x6493c23`, `0x6493ec3`
and `0x648985f`, and of `[28]` (draw) at `0x6489e7d`, corroborate the same two
methods being the only GLES-issuing virtuals; they are alternate render paths.

## 4. Criterion for `draws >= 1` (decisive, host-observable)

Count a draw **only** when a forwarding wrapper over one of the counted entry
points is entered with a **guest return address** (RA in `[base, base+0x6d33b90)`),
i.e. the guest itself issued the call, not merely loaded or wrapped the pointer.
The accepted RA set (all OBSERVED above) is:

```
0x6494ae1  glClear@plt
0x6496dbc  glDrawElements@plt
0x6496e1a  glDrawArrays@plt
0x6496d75  glDrawElementsInstanced  (slot 0x77a4fe0)
0x6496dec  glDrawArraysInstanced    (slot 0x77a4fe8)
0x6494991 / 0x6494a79  glClearBufferfv (slot 0x77a4fa8)
0x6494aa3  glClearBufferiv  (slot 0x77a4fa0)
0x64949d2  glClearBufferfi  (slot 0x77a4fb0)
0x64947ef  glDrawBuffers    (slot 0x77a4f98)
```

Formal predicate, to be evaluated in one clean run with all `SOBER_*` opt-ins
unset (matches T3-5 R2 / T4-4 R2 / G7 §5):

```
draws_guest_ra >= 1
  AND  (each counted invocation's RA is in the set above)
  AND  (ideally) draws_between >= 1, i.e. >= 1 such draw between two
       guest-tagged swaps at GOT 0x722a1a8 (RA in guest .text, indirect
       call *0x18(%reg) through 0x7191300 = 0x649d422, recurring >=2x).
```

**Explicit non-criteria** (must not increment the counter):
- `gles_loads` — the 70 `eglGetProcAddress` resolutions at `0x649c633…`;
- a non-NULL slot value by itself (a wrapped pointer is not a draw);
- the native first frame's own `dlsym`'d `glClear*`/`eglSwapBuffers`
  (`native_render.c`), which never traverses any guest PLT/table slot;
- the host Route-B call at `guest_render.c` (`guest_swap(ctx)` → `0x649d422`),
  whose RA is in the host and whose swap must be tagged `synthetic`;
- a draw whose RA is not in the set (e.g. some unrelated wrapper).

Today no such guest-RA draw exists in any consulted log (host-only swaps,
`gles_loads=48`, `draw_wrapped=5`): `draws_guest_ra = 0`, so the honest result is
`draws >= 1` **false**, and the marker stays **WITHHELD**.

## 5. Limits / open items

- Function names/roles are labels (stripped binary); RVAs, slot addresses,
  names passed to `eglGetProcAddress`, and gating bytes are OBSERVED.
- The loader emits ~70 `eglGetProcAddress` calls (base names plus `OES`/`EXT`
  aliases), populating roughly 50 distinct slots from `0x77a4ef0` to
  `0x77a5068`; every draw/clear slot named above is exact and was verified from
  the store instruction, not inferred from code order.
- Which concrete caller drives `0x6490488` each tick is reached through further
  function-pointer indirection (no direct `e8` xref found); the per-frame
  double-buffer toggle at `0x6490685` is the evidence that it is a frame body.
- This document only narrows where a draw can be *observed*; it does not assert
  that any guest draw, guest swap or app-shell frame has occurred.
