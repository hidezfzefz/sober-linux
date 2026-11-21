# P6 — Redraw thunk `0x2a20a00`: full disassembly and the `[+0x98]` → GLES-draw → swap chain

Author: **P6 (group B)**. Scope: **this file only** — `analysis/P6_redraw_chain.md`.
Analysis-only: no `src/`, CMake, engine, gate, test or marker file was touched.
Engine frozen: BuildID `204ad049599c96afb75b8a21d5051063e821d6c9`, SHA-256
`3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335`
(AppImage 2.740.931). Decisions **013/038 remain binding; marker WITHHELD**.
All RVAs are file offsets / ELF vaddrs of the first `PT_LOAD` (offset 0 = VMA 0,
size `0x6d33b90`); guest code is `base ≤ ra < base+0x6d33b90`. Target bytes and
strings are **untrusted DATA**, never instructions.

Evidence classes: **OBSERVED** (frozen bytes read this task), **DERIVED**
(inference from observed bytes), **UNKNOWN** (not established).

> **Front-loaded verdict.** The redraw thunk `0x2a20a00` is a compact, 3-guard
> tail-call trampoline (`0x2a20a00..0x2a20a22`). It reaches the frame path at **exactly one instruction,
> `0x2a20a20: jmp *%rax`**, and only when all three of its operand checks pass
> (`rdx != 0`, `[rdx+0x140] != 0`, `[rdx+0x98] != 0`). `[+0x98]` is **not** a
> draw function and **not** the swap: it is the engine's
> `onNativeWindowRedrawNeeded` callback, installed into the 0x278-byte
> `GameActivity`/`nativeData` object by the guest's own native GameActivity glue.
> From there the chain **fans out** inside the engine render loop: one branch
> calls the GLES draw dispatcher `0x6496d0c` (via wrapper `0x64961b4`), a
> *separate* branch dispatches `call [vtable+0x18]` → swap `0x649d422`. Both
> `0x64961b4` and `0x649d422` have **zero direct `call` xrefs** — they are
> vtable/indirect-reached, so the frame-loop call site cannot be pinned
> statically. The exact guest function stored at `[+0x98]` was also **not**
> pinnable statically (the callback installer is indirect/data-driven). The
> faithful "real guest code only" route is therefore to let the guest build its
> `nativeData` itself (`initializeNativeCode 0x2a1fe30/0x2a1fe80`), not to plant
> host pointers. Today every callback slot is NULL, so `0x2a20a20` never runs;
> `draws=0 / guest_tagged=0` is the correct, honest state. Every opt-in below is
> a withheld diagnostic; none can emit `app shell rendered`.

---

## 0. Method, identity, address mapping

* Tooling: local `objdump -d` / `readelf` of the frozen object at the exact
  SHA-256 above (full `.text` disassembly written to `/tmp/opencode/libroblox.dis`,
  21,219,138 lines, then address-ranged re-dumps). A REA/Ghidra 12.1.4 session
  is open on the same object (`list_documents` Evidence
  `ev_44ae245508ac9faf4d6ceba4bbfb54777d8c7475d6ac5585498235a7d90a333b`;
  segments `ev_ccdcfbfe62af4962c0313972cf3cd52968da2eaa98a86dc26dcd6658bc51d28a`).
  `analyze_function` could **not** resolve any of `0x2a20a00`, `0x6496d0c`,
  `0x649d422` as a procedure entry ("No procedure exists at the requested
  address; Ghidra image base `0x100000`"), so raw disassembly — exact for
  field/offset work — is the evidence basis, as in P2/R3/R4.
* **Ghidra VA = analysis RVA + `0x100000`.** Confirmed by `address_name`:
  RVA `0x6d334d0` (the `eglSwapBuffers@plt` tail of `0x649d422`) maps to Ghidra
  VA `0x6e334d0` = `<EXTERNAL>::eglSwapBuffers` (Evidence
  `ev_e96c544c3e4cd111379b7fecca3d21e4cefab65dbf2120963326be93f554092a`).
  All numbers in this report are **RVAs** (objdump vaddrs), which is what the
  harness adds to `guest_base`.
* `readelf -l`: first `PT_LOAD` off `0x0` vaddr `0x0` filesz `0x6d33b90` R E —
  so `.text`/`.plt` offsets equal VMAs. `.got.plt 0x7229400`; `.data.rel.ro
  0x6e37bc0` (Ghidra) = RVA `0x6d37bc0` (render-vtable region, APS2-populated).

## 1. The redraw thunk `0x2a20a00` — complete disassembly (OBSERVED)

```
2a20a00: 48 85 d2           test  %rdx,%rdx          ; GUARD A: nativeData (rdx) non-NULL
2a20a03: 74 1d              je    2a20a22            ;   NULL -> ret (no frame)
2a20a05: 48 8b b2 40 01 ..  mov   0x140(%rdx),%rsi   ; rsi = [nativeData+0x140] ANativeWindow*
2a20a0c: 48 85 f6           test  %rsi,%rsi          ; GUARD B: window non-NULL
2a20a0f: 74 11              je    2a20a22            ;   NULL -> ret (no frame)
2a20a11: 48 8b 82 98 00 ..  mov   0x98(%rdx),%rax    ; rax = [nativeData+0x98] redraw callback
2a20a18: 48 85 c0           test  %rax,%rax          ; GUARD C: callback non-NULL
2a20a1b: 74 05              je    2a20a22            ;   NULL -> ret (no frame)
2a20a1d: 48 89 d7           mov   %rdx,%rdi          ; rdi = nativeData (arg to callback)
2a20a20: ff e0              jmp   *%rax              ; *** THE FRAME/REDRAW DISPATCH ***
2a20a22: c3                 ret
```

ABI (Java→native JNI callback, confirmed against the sibling thunks and the
descriptor table): `rdi = JNIEnv*`, `rsi = jobject this`, **`rdx = nativeData`
(the `jlong mNativeData` cast to a pointer)**, `rcx = jobject Surface`. The
callback invoked at `0x2a20a20` is called as `f(rdi=nativeData,
rsi=[nativeData+0x140] /* the ANativeWindow */)`.

**Every guard, in order (all three must pass):**

| # | Instruction | Test | Failure target | Meaning |
|---|---|---|---|---|
| A | `0x2a20a00` / `0x2a20a03` | `rdx != 0` | `0x2a20a22 ret` | nativeData pointer must be non-NULL |
| B | `0x2a20a0c` / `0x2a20a0f` | `[rdx+0x140] != 0` | `0x2a20a22 ret` | an ANativeWindow must already be bound |
| C | `0x2a20a18` / `0x2a20a1b` | `[rdx+0x98] != 0` | `0x2a20a22 ret` | the engine redraw callback must be installed |

There is **no other branch** in the thunk: with all three true it *always*
tails to `[+0x98]`. Note the ordering that defeats naive seeding: **window gate B
is checked before callback gate C**, so a non-NULL `[+0x98]` is useless while
`[+0x140] == NULL`.

Sibling thunks (OBSERVED, same object, from the full disassembly) share the
guard idiom and define the complete callback field set:

| Thunk | Callback slot(s) | Guard site(s) | Dispatch site(s) | Side effect on `+0x140` |
|---|---|---|---|---|
| `0x2a20850` created | `+0x88` | `0x2a20894` | `0x2a208a6 jmp *%rcx` | release old (`0x2a2086b`), store new from `fromSurface` (`0x2a20881`) |
| `0x2a208c0` changed | `+0xa0`, `+0x88`, `+0x90` | `0x2a2092b`, `0x2a2094f`, `0x2a209be` | `0x2a20933`, `0x2a20957`, `0x2a209ce` (`call`) | rebind (`0x2a20910`); dims → `+0x148/+0x14c` |
| **`0x2a20a00` redraw** | **`+0x98`** | **`0x2a20a18`** | **`0x2a20a20 jmp *%rax`** | none (reads `+0x140`) |
| `0x2a20a30` destroyed | `+0xa0` | `0x2a20a4f` | `0x2a20a54 call *%rax` | **`movq $0,0x140` at `0x2a20a6a`** |

`ANativeWindow_fromSurface` is reached at guest RAs `0x2a2087c` (created) and
`0x2a20907` (changed) via GOT `0x72294a8`; `release`/`acquire`/`getWidth`/
`getHeight` via GOT `0x72294a0/0x72297c0/0x72297d0/0x72297c8`. Descriptor
table (JNINativeMethod, APS2-relocated): `0x6d37cd8/0x6d37cf0/0x6d37d08/
0x6d37d20` → the four thunks (RVA quick-ref in `render-map.md §4`, `R4 §2`).

## 2. What `[+0x98]` actually is (and why it cannot be pinned statically)

`[+0x98]` is the AGDK `GameActivityCallbacks.onNativeWindowRedrawNeeded`
function pointer, one of four consecutive slots `+0x88/+0x90/+0x98/+0xa0` in
the 0x278-byte `nativeData` object that `initializeNativeCode`
(`0x2a1fe30` wrapper → `0x2a1fe80` body) allocates (R4 §4.2: `mov $0x278,%edi`
at `0x2a1feb0`; `memset` of `0x148` at `0x2a1fec7` zeroes all four slots;
JNIEnv cached to `+0x10` at `0x2a20166`). The engine's native GameActivity glue
installs these four pointers **after** `initializeNativeCode` returns — that
installation is a data-driven/indirect write, not a directly xref'd call.

I searched the whole frozen `.text` for a function storing all four slots:
hundreds of generic `mov reg,0x88/0x90/0x98/0xa0(%reg)` clusters exist (e.g.
`0x559673b`, `0x55ee72c`, `0x58dfaba`, `0x5c48fd7`, …), but **none could be
uniquely attributed to the 0x278-byte `nativeData`** without a runtime
type/pointer trace. Likewise a raw qword search for the thunk addresses in the
file returns no hits (they are relocation-filled). Therefore:

* **OBSERVED:** the thunk NULL-tests the slot and tails to it; the slot holds a
  code pointer by ABI; the object is 0x278 bytes from `initializeNativeCode`.
* **DERIVED (high confidence):** the four slots are AGDK
  `onNativeWindowCreated/Resized/RedrawNeeded/Destroyed` (offset order + ABI
  match), and `[+0x98]` is the engine's redraw handler, not a draw or swap.
* **UNKNOWN:** the exact RVA of that handler and of its installer. Do **not**
  fabricate a value for `[+0x98]`. Planting a host trampoline there is a
  decision-013 false green (F10 FP-7); the only faithful option is to let the
  guest install its own pointer (§7 route A).

## 3. Trace A — from the `[+0x98]` redraw callback to the GLES dispatcher `0x6496d0c`

This link is **inside the engine frame loop**, which is vtable-driven and has no
direct xref. What is directly observable is the dispatcher and its immediate
caller.

### 3.1 GLES draw dispatcher `0x6496d0c` — full disassembly structure (OBSERVED)

Entry `0x6496d0c` (prologue `push rbp; mov rsp,rbp; push r15,r14,r13,r12,rbx;
push rax`). Register/argument map at entry: `rdi = draw object (rbx)`,
`esi → r12d`, `edx → r13d`, `ecx`, `r8d → r14d`, `r9d → r15d`.

```
0x6496d2b  call 0x6496e52          ; state-sync helper; rdi=draw obj, esi=arg4
0x6496d30  mov [rbp-0x2c],eax       ; keep helper result for return value
0x6496d33  movzwl 0x8e(%rbx),%eax   ; uint16 field at obj+0x8e
0x6496d3c  imul %r13d,%ecx          ; ecx = (uint16)obj.8e * arg3
0x6496d40  mov 0x78(%rbx),%rdx      ; rdx = [obj+0x78]  <-- INDEX/element-buffer ptr
0x6496d44  test %r15d,%r15d         ; arg6 (r9d): instanced vs plain
           ; ---- arg6 != 0 : INSTANCED ----
0x6496d49    test %rdx,%rdx ; je 0x6496dd6
0x6496d75      call *0x77a4fe0      ; glDrawElementsInstanced (extension ptr)
           ; ---- arg6 == 0 : PLAIN ----
0x6496d97    test %rdx,%rdx ; je 0x6496e07
0x6496dbc      call 0x6d33200       ; glDrawElements@plt -> GOT 0x722a040
0x6496dd6   (instanced, index ptr NULL)
0x6496dec      call *0x77a4fe8      ; glDrawArraysInstanced (extension ptr)
0x6496e07   (plain, index ptr NULL)
0x6496e1a      call 0x6d33210       ; glDrawArrays@plt -> GOT 0x722a048
           ; each draw immediately followed by:
           call 0x6d32f50            ; glGetError@plt
           cmp $0x505,%eax           ; GL_OUT_OF_MEMORY
           jne 0x6496e2b             ; normal -> epilogue
           ; ==0x505 -> log error string, then same epilogue
0x6496e2b  mov [rbp-0x2c],%esi ; ... pop ... ; jmp 0x6497022
```

**Guards / prerequisites inside the dispatcher (OBSERVED):**

| Guard | Site | Effect |
|---|---|---|
| `[obj+0x78]` (index/element-buffer pointer) | `0x6496d49`, `0x6496d97` | **Branch selector, not an early return**: non-NULL → indexed `glDrawElements*`; NULL → `glDrawArrays*`. Must be a valid pointer or a deliberate NULL. |
| `arg6` (`r9d`) | `0x6496d44` | selects instanced (`*0x77a4fe0`/`*0x77a4fe8`) vs plain (`glDrawElements`/`glDrawArrays`). |
| extension pointers `*0x77a4fe0` / `*0x77a4fe8` | `0x6496d75`, `0x6496dec` | loaded by the `eglGetProcAddress` block `0x649c633..0x649d300`; **must be non-NULL** or the indirect call faults. |
| imported draw GOTs `0x722a040` / `0x722a048` | `0x6496dbc`, `0x6496e1a` | `R_X86_64_JUMP_SLOT`s for `glDrawElements`/`glDrawArrays`; must resolve to real driver entry points. |
| helper `0x6496e52` reads `[obj+0x38]` → `[+0x48]` vs `[+0x50]` | `0x6496e66..0x6496e76` | draw object must carry a valid state sub-object at `+0x38` (fields `+0x48/+0x50` compared; a 0x10-stride loop runs when they differ). |
| `glGetError == 0x505` (`GL_OUT_OF_MEMORY`) | `0x6496d80`, `0x6496dc6`, `0x6496df7`, `0x6496e24` | diagnostic log only; does not abort the frame. |

**Immediate caller** (OBSERVED): wrapper `0x64961b4` calls the dispatcher at
`0x64961d1`; it reads `[rbx+0x18]` (a sub-object), indexes a 0x60-stride table
via `[rax+0x150]` (`0x64961da..0x64961e8`), checks a flag byte at `+0xea`, and
sets `movb $1,0x260(%rax)` at `0x6496216` after the draw. **Both `0x64961b4` and
`0x6496d0c` have zero direct `call` xrefs** — the frame loop reaches the draw
path indirectly (vtable/function pointer). This is the honest boundary of static
analysis: the redraw callback → dispatcher hop is engine-internal and not
statically pinnable; the dispatcher bytes above are what executes once reached.

Draw sites counted by the render module (T3-5 §2.2): imported slots `0x7229fc8`
(glClear, call `0x6494ae1`), `0x722a040`, `0x722a048`; extension table
`0x77a4fe0/8`, `0x77a4fa0/8/b0`, `0x77a4f98`, `0x77a4f78`. None are traversed
by the host's `dlsym`'d first-frame `glClear`, so `draw_calls ≥ 1` is a clean
discriminator.

## 4. Trace B — from the frame loop to the swap `0x649d422`

The dispatcher does **not** call the swap. The frame loop performs a separate
vtable dispatch; both actions are siblings under the same engine loop.

* Render context: **0x48 bytes**, layout OBSERVED (P2 §1.2): `+0x00` vptr
  `base+0x71912e8`; `+0x10` shared-parent ctx (must be 0 for a fresh primary);
  `+0x18` ANativeWindow; `+0x20` EGLDisplay; `+0x28` EGLSurface; `+0x30`
  EGLContext; `+0x38/+0x3c` width/height; `+0x40` invalidated byte; `+0x44`
  swap interval. Created by `0x649c21e(window, extra=0)` → `0x649c272 egl_init`.
* Render vtable **`0x71912e8`** is APS2-populated at load (file bytes zero);
  slot `[3] @ 0x7191300 = 0x649d422` (swap). The frame loop issues
  `call QWORD PTR [vtable+0x18]` with `rdi = ctx`.
* Swap wrapper at `0x649d422` (OBSERVED, exact bytes):
  ```
  649d422: 48 8b 47 20   mov 0x20(%rdi),%rax   ; EGLDisplay
  649d426: 48 8b 77 28   mov 0x28(%rdi),%rsi   ; EGLSurface
  649d42a: 48 89 c7      mov %rax,%rdi
  649d42d: e9 9e 60 89 00 jmp 6d334d0 <eglSwapBuffers@plt>
  ```
  It reads **only** `+0x20/+0x28`, performs no draw, and **tail-jumps** (no
  `call`) — so the return address seen at GOT `0x722a1a8` is whoever called
  `0x649d422`. A guest `call [vtable+0x18]` leaves a guest `.text` RA
  (`base ≤ ra < base+0x6d33b90`) → `s_swaps_guest` increments; the host
  Route-B direct call leaves a host RA → `s_swaps_synthetic` (the honest
  `1/0/1` today). Zero direct callers of `0x649d422` exist in the ELF (OBSERVED).
* The guest finds this context through its **render device**: caller
  `0x648b940..0x648b98a` stores window `@device+0x158`, ctx `@device+0x160`,
  and a 0x20-byte delegate `@device+0x168` with vptr `0x71908e0` and ctx at
  `+0x18`; accessor `0x6490132`/`0x6490140` dispatch rebind via `jmp *0x10(%rax)`.
  A host-only `s_ctx` (what `render_guest.c` keeps) is invisible to this loop.

## 5. Exact state required for a guest draw + guest-tagged swap (checklist)

Every line is a guard or data dependency established above; all must hold
**in the same run** for `draws ≥ 1` and `swaps_guest_tagged ≥ 1`.

1. **Guest mapped + relocated**: APS2 packed RELA applied so vtable
   `0x71912e8`, extension table `0x77a4ef0..0x77a4ff8`, and all GOT PLT slots
   are live (phase-2 bootstrap done). `base` known for RA tagging.
2. **Real EGL drawable** (host X11/Wayland + libEGL) with forwarding GOT hooks:
   `eglGetDisplay/CreateContext/CreateWindowSurface/MakeCurrent` reach it.
3. **Guest context created** via `0x649c21e(window, extra=0)` (non-zero extra
   dereferences a bogus parent at `0x649cf9a`): ctx `+0x00 = base+0x71912e8`,
   `+0x18` window, `+0x20/+0x28/+0x30` EGL handles, `+0x38/+0x3c` dims.
4. **GLES procs loaded** through `0x722a180` (`0x649c633..0x649d300`) so the
   dispatcher's extension pointers `0x77a4fe0/8` and draw GOTs `0x722a040/
   0x722a048/0x7229fc8` are non-NULL and real.
5. **Context registered in the guest render device** — replay `0x648b94c` so
   `device+0x158/+0x160/+0x168` are set; otherwise the frame loop cannot find
   the context. (P2 §4.2, R4 §5 step 6.)
6. **nativeData object correct**: `+0x10 = JNIEnv*`, `+0x140 = ANativeWindow`
   (non-NULL at redraw time), and — critically — `+0x88/+0x90/+0x98/+0xa0`
   hold **genuine guest engine callbacks**, installed by the guest, never host
   pointers; dims at `+0x148/+0x14c`.
7. **Engine render thread running** — requires the app-start/game-loaded chain
   (`0x26fe80f` StartAppWithParams, `0x2ebd51a` StartGameWithParam, notifier
   `0x27eaf6e` → thunk `0x25848e0` `gameLoadedCallback`, `0x2ebf950`
   `nativeScheduleOnFirstFrame`). Without it there is no loop to dispatch.
8. **Lifecycle driven in true order**: `0x2a20850` (created) → `0x2a208c0`
   (changed) → `0x2a20a00` (redraw); **defer `0x2a20a30` (destroyed) to
   teardown** because it zeroes `+0x140` (`0x2a20a6a`) and would make redraw
   fail guard B before guard C.
9. **Redraw dispatch taken**: `0x2a20a20 jmp *%rax` executes with `[+0x98]`
   guest and `rsi = [+0x140]` valid.
10. **Draw executed**: dispatcher `0x6496d0c` reached (via `0x64961b4`) with a
    valid draw object (`+0x38` state sub-object, `+0x78` index pointer,
    `+0x8e` mode) and populated GLES pointers → a counted guest draw.
11. **Swap dispatched by guest**: frame loop executes `call [vtable+0x18]`
    (`[0x7191300] = 0x649d422`) with a context whose `+0x20/+0x28` are valid →
    GOT `0x722a1a8` sees a **guest .text RA** → `s_swaps_guest` increments.
12. **Correlation**: ideally `swaps_guest_tagged ≥ 2` with `draws ≥ 1` between
    them (T4-4 R2); mixed synthetic + guest evidence fails closed.

## 6. Opt-in call sequence using **real guest code only** (all WITHHELD)

These calls are all genuine guest RVAs; the only host-side elements are the
native drawable and the forwarding GOT hooks (standard PLT interposition, no
guest `.text` patch and no host pointer in a guest object/vtable). Every step is
a decision-013/038 withheld seed; the expected output is a *diagnostic*
guest-tagged render verdict, **never** `app shell rendered`.

**Route A — faithful (preferred; guest builds and installs its own callbacks):**

1. Install forwarding GOT hooks together (do not hook `fromSurface` alone —
   `0x26fea9b` would corrupt the shim): `fromSurface 0x72294a8`, `release
   0x72294a0`, `acquire 0x72297c0`, `getHeight 0x72297c8`, `getWidth 0x72297d0`,
   `swap 0x722a1a8` (RA-tag), `getProcAddr 0x722a180` (count/wrap),
   `createWindowSurface 0x722a178`.
2. Drive guest `initializeNativeCode` `0x2a1fe30` (→ body `0x2a1fe80`) with a
   genuine bridge `JNIEnv*` and real Java args (String×3, AssetManager, byte[],
   Configuration) so it returns the genuine 0x278-byte `nativeData`; **verify
   `+0x88/+0x90/+0x98/+0xa0` are non-NULL engine (guest-RVA) pointers**. This is
   the only way to satisfy §5.6 with guest code; it needs the JNI slot/cached-ID
   prerequisites (231 slots / 49 IDs) and a Java `Surface` carrying a guest
   `ANativeWindow`.
3. Replay the guest device seed `0x648b940..0x648b98a` (→ `0x649c21e(window,0)`)
   so `device+0x158/+0x160/+0x168` are populated (§5.5). Confirm ctx `+0x00 =
   base+0x71912e8`.
4. Start the engine render thread through the genuine app-start/game-loaded
   chain: `0x26fe80f`, `0x2ebd51a`, notifier `0x27eaf6e` → `0x25848e0`
   (`gameLoadedCallback`, class `0x72a2f48`, method `0x72a2fc0`, `placeId>0`),
   `0x2ebf950`. (Today `callback=0`; this chain is the missing Leg C.)
5. Drive lifecycle with that `nativeData` and a guest-shaped Surface token:
   `0x2a20850` → `0x2a208c0` (fromSurface RAs `0x2a2087c`/`0x2a20907`) →
   `0x2a20a00`. Assert `[nd+0x140] != 0` at `0x2a20a0c` and `[nd+0x98] != 0`
   at `0x2a20a18`; confirm dispatch at `0x2a20a20`. **Do not call `0x2a20a30`
   before the frame.**
6. Observe the guest loop: draws via `0x64961b4 → 0x6496d0c` (count at
   `0x722a040/0x722a048/0x7229fc8` and `0x77a4fe0/8`), then
   `call [vtable+0x18]` → `0x649d422` → PLT `0x6d334d0` → GOT `0x722a1a8`
   with a stable guest RA; verify `swaps_guest_tagged ≥ 1 (ideally ≥2)` and a
   counted draw between two swaps.
7. Capture the swap RA and offline-`objdump` the 5 bytes before it to confirm an
   indirect `call *0x18(%reg)` through vtable `0x7191300` (pins the frame-loop
   site that static analysis cannot).

**Route B — minimal diagnostic (only if guest-built nativeData is unavailable):
install genuine guest callback RVAs into a synthetic nativeData.** This is
*strictly* weaker: the exact engine-handler RVAs for `+0x88/+0x90/+0x98/+0xa0`
are **UNKNOWN** (§2) and must be discovered by a runtime type/pointer trace of
the guest's own registration; **never** substitute a host trampoline (F10 FP-7).
Combine with steps 3–7 above. Keep `+0x140` non-NULL through redraw and defer
destroyed.

**What the current harness does (contrast, read-only):** `sober_render_guest_run`
= `install + bind_surface + create_context` (`render_guest.c:550-557`).
`rg_bind_surface()` (`:289-331`) calls the four thunks with a `calloc`'d
`nativeData` whose `+0x88/+0x98/+0xa0` are deliberately NULL (`:402-404`) and
calls **destroyed during setup** (`:319-322`, zeroing `+0x140`), then
host-writes the window global `0x72a3308` (`:326-329`). `s_ctx` from
`0x649c21e` is never registered in the guest device and
`sober_render_guest_swap_via_vtable()` (`:520-548`) is never called. The only
swap is the host Route-B `guest_swap((uintptr_t)ctx)` in `guest_render.c`
(stack `0x48`-byte ctx, `+0x20/+0x28` filled) — RA host → `swaps_synthetic=1`.
That is why `life=1/1/1/1`, `gles_loads=48`, `draw_wrapped=5`, `draw_calls=0`,
`swaps=1/0/1`. Route A above changes exactly these gaps.

## 7. Unknowns, limits, honesty

* **OBSERVED:** all bytes in §§1–4 (redraw thunk, sibling thunks, dispatcher,
  swap wrapper, vtable slot value via prior APS2 decode, GOT slots); the
  zero-direct-xref status of `0x64961b4`/`0x6496d0c`/`0x649d422`; the current
  harness source and counters.
* **DERIVED:** AGDK mapping of `+0x88/+0x90/+0x98/+0xa0`; the frame loop being
  the common parent of draw + swap; the causal explanation of `draws=0`.
* **UNKNOWN:** the exact RVA of the engine callback installed at `[+0x98]` and
  of its installer (hundreds of generic store clusters, none uniquely
  attributable); the precise frame-loop `call *0x18(%reg)` site; whether a
  non-NULL guest `[+0x98]` alone starts a loop vs. also needing §5.5/§5.7.
  These are engine-internal, vtable/data-driven links, not guessable from static
  bytes; discover them by the RA-capture + runtime-trace steps in §6.
* **No runtime proof** was produced here; every §6 step that drives the guest
  seeds synthetic state. Under decisions 013/038 the `app shell rendered` marker
  remains **WITHHELD** in all of them; success is a correct *guest-tagged render*
  diagnostic line only. `guest_tagged=0 / draws=0` today is the truthful result
  of the NULL callback chain and must not be inflated.

## 8. Evidence index

Fresh this task: `objdump -d` slices `0x2a20850..0x2a20a80` (four thunks),
`0x6496d00..0x6496e90` (dispatcher, epilogue, helper), `0x6496100..0x6496320`
(draw wrapper/callers), `0x64931f0..0x6493400` (vtable `call *0x18` region),
`0x649d410..0x649d440` (swap wrapper); `readelf -l`; full `.text` disassembly
`/tmp/opencode/libroblox.dis`; REA `list_documents`, `list_segments`,
`address_name` (Ghidra image base `0x100000`, `eglSwapBuffers` mapping), the
failed `analyze_function` attempts on all three targets. Engine identity:
`sha256sum` / `readelf -nW`.

Attributed prior analysis (not re-derived): `analysis/render/render-map.md §2-5`,
`analysis/C04-render-integration.md §1-4`, `analysis/T3_5_render-evidence.md
§2-3`, `analysis/R1_zero_tagged_swaps.md`, `analysis/R3_guest_egl_context.md
§4-7`, `analysis/R4_surface_lifecycle.md §2-7`, `analysis/P2_nativeData_layout.md
§1-4`, `analysis/P8_anw_guest_consumption.md §5-7`, `analysis/MODULE_INTEGRATION.md
§2`, and the harness `src/jni/render_guest.c/.h` + `src/linux/guest_render.c`.

No file other than this report was created or modified.
