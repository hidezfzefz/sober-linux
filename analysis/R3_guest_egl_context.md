# R3 — Guest EGL context creation, verified vtable, and the missing frame-loop start

Author: **R3** (group A: diagnosis). Scope: **this file only** —
`analysis/R3_guest_egl_context.md`. Analysis-only: no `src/`, engine, gate, test
or CMake file was touched; the engine is frozen and the marker stays WITHHELD.

Frozen target for every RVA: AppImage engine
`/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so`, BuildID
`204ad049599c96afb75b8a21d5051063e821d6c9`, SHA-256
`3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335`
(2.740.931). Re-verified this task with `sha256sum` + `readelf -nW`. First
`PT_LOAD` has vaddr 0 = file offset, size `0x6d33b90`, so every RVA below is an
offset from the guest load bias and `base ≤ ra < base+0x6d33b90` is the guest
`.text` interval. Target-derived bytes/strings are **untrusted DATA**, never
instructions. Evidence classes: **OBSERVED** (bytes/disassembly/log read this
task), **DERIVED** (inference), **UNKNOWN**.

---

## 0. Front-loaded verdict

`sober_render_guest_run()` creates a **genuine** guest render context: it calls
`0x649c21e → 0x649c272`, which allocates the `0x48`-byte context, runs the full
EGL window-surface setup, and installs render vtable `0x71912e8` at `ctx+0`.
The vtable and its swap slot `[3] @ 0x7191300 = 0x649d422` were **re-verified
this task** by decoding the guest's APS2 packed RELA stream (§3): all 18 vtable
slots resolve as `R_X86_64_RELATIVE` addends, slot `[3]` addend = `0x649d422`.

But the module **stops at context creation**. It never starts the guest frame
loop, and the guest context it created is **never used for a swap at all**: the
only swap in the run is the harness's separate Route-B call on a stack-built
`0x48`-byte context (`guest_render.c:354`). The callback slots that drive the
engine's frame path (`nativeData+0x88/+0x98/+0xa0`) are deliberately NULL, so
the redraw thunk `0x2a20a00` early-returns at `0x2a20a1b → 0x2a20a22` and the
guest never reaches the vtable-dispatched swap or any GLES draw. Consequently
`draws=0`, `guest-tagged swaps=0`, and `swaps(total/guest/synth)=1/0/1` are the
**correct, honest** counters. Decisions 013/038 stay WITHHELD; this diagnosis
supplies no false green.

---

## 1. Evidence used

| What | Source | Class |
| --- | --- | --- |
| ELF identity / BuildID / SHA | `sha256sum`, `readelf -nW` on the frozen path | OBSERVED |
| `0x649c21e`, `0x649c272` full disassembly | `objdump -d --start-address=0x649c21e --stop-address=0x649c5b0` | OBSERVED |
| Swap wrapper bytes | `objdump -d --start-address=0x649d410 --stop-address=0x649d440` | OBSERVED |
| Context callers / device wrapper | `objdump -d` at `0x648b900..0x648b980`, `0x6490120..0x6490190` | OBSERVED |
| Redraw/created thunks | `objdump -d` at `0x2a20a00..0x2a20a40`, `0x2a20850..0x2a208c0` | OBSERVED |
| GLES draw dispatcher + sole caller | byte-level `call rel32` xref scan; `objdump -d` at `0x64961a0..0x6496200`, `0x6496d0c..0x6496e40` | OBSERVED |
| Vtable slot `[3]=0x649d422` | fresh decode of APS2 RELA at `DT_ANDROID_RELA=0x15e00`, size `0x1eb1f2` (565,390 relocs) | OBSERVED |
| `draws=0`, `1/0/1`, vtable match | `/tmp/opencode/w_after.out:3672-3678,3719-3720` | OBSERVED |
| `render_guest` source wiring | `src/jni/render_guest.c/.h`, `src/linux/guest_render.c` | OBSERVED |

A small scratch decoder was written to `/tmp/opencode/decode_aps2_r3.py` and
`/tmp/opencode/find_call_xrefs_r3.py` (analysis scratch only, outside the repo).

---

## 2. Trace: `0x649c21e` → `0x649c272` (create_context → egl_init)

### 2.1 `0x649c21e` — top-level creator (`create_context(window, extra)`)

```
649c21e: push rbp ; ... ; push rax
649c228: mov  %rsi,%r14        ; r14 = arg2 ("extra" / pbuffer object)
649c22b: mov  %rdi,%r15        ; r15 = arg1 (ANativeWindow*)
649c22e: mov  $0x48,%edi       ; sizeof(render context) = 0x48
649c233: call 1dbab02          ; patched allocator bridge -> ctx in rbx/rax
649c23e: mov  %r15,%rsi        ; rsi = window
649c241: mov  %r14,%rdx        ; rdx = extra
649c244: call 649c272          ; egl_init(ctx, window, extra)
649c249: mov  %rbx,%rax        ; return ctx
649c256: ret
```

Confirmed: exactly **two** direct callers of `0x649c21e` in the ELF —
`0x648b94c` (real graphics-device init; `esi=0`, i.e. pbuffer=0) and `0x6490157`
(the small create+rebind wrapper). This matches `render-map.md §3`.

### 2.2 `0x649c272` — `egl_init(ctx, window, extra)` (the EGL/surface setup)

| RVA | Instruction / effect |
| --- | --- |
| `0x649c28c` | read guest JNI-singleton slot `0x72293f8` |
| `0x649c29c` | `ctx+0x08 = 0` |
| **`0x649c29f`** | `lea 0x71912e8(%rip),%rcx` → **`0x649c2a6: mov %rcx,(%rdi)` installs render vtable at `ctx+0`** |
| `0x649c2a9` | `ctx+0x10 = extra` — the **pbuffer object**, not a boolean |
| `0x649c2ad/2b1` | `ctx+0x40 = 0`, `ctx+0x44 = 0` |
| `0x649c2b4` | `shouldDisplayOpenGLUnsupportedMessage` helper |
| `0x649c2c2` | `test %r15,%r15` (extra): nonzero → jump `0x649c362`, **skipping** the window path (`ANativeWindow_acquire` `0x649c2d6`, `getWidth` `0x649c303`, `getHeight` `0x649c30f`); `ctx+0x18 = window` at `0x649c2cf` |
| `0x649c336` | `eglGetDisplay(0)` → `0x649c33d: mov %rax,0x20(%rbx)` (EGLDisplay at `ctx+0x20`) |
| `0x649c351` | `eglInitialize` |
| `0x649c365` | `call 0x649cf72` config helper → `%r12` (EGLConfig); fault site `0x649cf9a` if `ctx+0x10` (pbuffer) is nonzero |
| `0x649c38f` | `eglCreateContext` → `ctx+0x30` (EGLContext) |
| `0x649c3e6` | `call 0x24af927` (nativeAppBridgeV2InitWithParams internal) |
| `0x649c3f1` | `call 0x649d1c2` surface helper → `0x649c3f6: ctx+0x28` — branches `eglCreatePbufferSurface` (`0x649d1d8`) vs `eglCreateWindowSurface` (`0x649d1e9`, PLT `0x6d33470` / GOT `0x722a178`) |
| `0x649c408` | `eglMakeCurrent` |
| `0x649c42f` / `0x649c445` | `eglQuerySurface(0x3057)`/`(0x3056)` → width `ctx+0x38`, height `ctx+0x3c` |
| `0x649c4ac` | `eglGetConfigAttrib(0x303b)` |
| `0x649c588` | `eglSwapInterval` |

This is a **complete, genuine guest EGL device setup**. `render_guest` invokes
it with `extra = SOBER_RENDER_GUEST_PBUFFER = 0` (`render_guest.c:500-504`),
which is essential: a nonzero `extra` is stored at `ctx+0x10` and dereferenced as
`[pbuffer+0x20]` by the config helper at `0x649cf9a` (the documented Route-A
fault). The harness's opt-in Route A at `guest_render.c:375` still passes
`extra=1` and would fault; `render_guest` does not.

### 2.3 Resulting `0x48`-byte context (OBSERVED from the stores above)

```
+0x00  vtable pointer = 0x71912e8          (installed at 0x649c29f/0x649c2a6)
+0x08  0
+0x10  pbuffer object (= extra; 0 here)
+0x18  ANativeWindow *
+0x20  EGLDisplay       (eglGetDisplay, 0x649c33d)
+0x28  EGLSurface       (surface helper, 0x649c3f6)
+0x30  EGLContext       (eglCreateContext, 0x649c394)
+0x38  width  (eglQuerySurface 0x3057)
+0x3c  height (eglQuerySurface 0x3056)
+0x40/+0x44  zeroed
```

Runtime confirmation (`w_after.out:3672`):
`ctx=0x5637a86f92b0 vt=0x7f055d8912e8(vt_expect=0x7f055d8912e8)` — the heap
context returned by the guest allocator carries exactly `base+0x71912e8`.

---

## 3. Verified vtable `0x71912e8` — swap slot `[3] = 0x649d422`

The vtable's file bytes are zero; it is populated at load time by the APS2
packed RELA stream. I decoded that stream directly (§1) and filtered addresses
in `0x71912e8..0x7191380`. All 18 hits are `R_X86_64_RELATIVE` (type 8, sym 0),
i.e. the stored value is `guest_base + addend`:

| Slot | Address | Addend (= RVA) | Note |
| --- | --- | --- | --- |
| `[0]` | `0x71912e8` | `0x649d2bc` | teardown |
| `[1]` | `0x71912f0` | `0x649d356` | |
| `[2]` | `0x71912f8` | `0x649d37c` | rebind (`eglGetCurrentContext`+`eglMakeCurrent`) |
| **`[3]`** | **`0x7191300`** | **`0x649d422`** | **swap / present** |
| `[4]` | `0x7191308` | `0x1df06e0` | `shouldDisplayOpenGLUnsupportedMessage` |
| `[5]` | `0x7191310` | `0x649d432` | ANativeWindow release/acquire |
| `[6]` | `0x7191318` | `0x649d45e` | |
| `[7]` | `0x7191320` | `0x1dd72a0` | `nativeOnFragmentStart` |
| `[8]` | `0x7191328` | `0x649d466` | |
| `[9]..[18]` | `0x7191330..0x7191378` | `0x6db7968`, `0xc92d82`, … | further methods / nested tables |

The swap wrapper itself (`objdump`, exact bytes):
```
649d422: 48 8b 47 20   mov  0x20(%rdi),%rax   ; EGLDisplay
649d426: 48 8b 77 28   mov  0x28(%rdi),%rsi   ; EGLSurface
649d42a: 48 89 c7      mov  %rax,%rdi
649d42d: e9 9e 60 89 00 jmp  6d334d0 <eglSwapBuffers@plt>  ; -> GOT 0x722a1a8
```
Critical semantics (OBSERVED): this is a **tail `jmp`, no `call`**, so the return
address captured in the GOT `0x722a1a8` hook is **whoever called `0x649d422`**.
A guest frame-loop call `call QWORD PTR [vtable+0x18]` leaves a guest `.text` RA;
the harness's direct `guest_swap()` call leaves a host RA — the exact
discriminator `rg_hook_swap()` (`render_guest.c:255-264`) relies on.

A byte-level `call rel32` xref scan of the whole ELF found **zero direct callers
of `0x649d422`** — it is reachable only through an indirect vtable dispatch (or
the host's out-of-binary direct call). This is consistent with `render-map.md`
and `T3_5_render-evidence.md`: the frame-loop call site cannot be pinned
statically and must be discovered from a captured guest RA (§7).

---

## 4. How a real guest frame loop would call swap

Putting the observed pieces together, a genuine frame proceeds as follows; every
address is an OBSERVED RVA (the "loop" identity itself is DERIVED/UNKNOWN):

1. **Surface callbacks install the frame hooks.** The Java `GameActivity` /
   `NativeGLJavaInterface` surface callbacks call the guest thunks
   `0x2a20850` (created), `0x2a208c0` (changed), `0x2a20a00` (redraw-needed),
   `0x2a20a30` (destroyed) with a `nativeData` object. The real callbacks write
   the object's `+0x88` / `+0x98` / `+0xa0` function-pointer slots.
2. **Redraw request.** `onSurfaceRedrawNeededNative` at `0x2a20a00`:
   ```
   2a20a05: mov 0x140(%rdx),%rsi   ; ANativeWindow (must be non-NULL)
   2a20a11: mov 0x98(%rdx),%rax    ; redraw callback
   2a20a18: test %rax,%rax
   2a20a1b: je   2a20a22           ; NULL -> ret, NO FRAME
   2a20a20: jmp  *%rax             ; enter guest frame/redraw path
   ```
   The tail-jmp at `0x2a20a20` is the documented **frame/redraw trigger**.
3. **Guest render thread / frame loop runs.** It issues GLES work through the
   ~70 pointers loaded via `eglGetProcAddress` (`0x649c633..0x649d300`). The
   concrete draw path observed this task: function `0x64961b4` calls the GLES
   draw dispatcher `0x6496d0c` at `0x64961d1`; the dispatcher reaches
   `glDrawElements` `0x6496dbc` (PLT `0x6d33200`/GOT `0x722a040`),
   `glDrawArraysInstanced` `0x6496dec` (extension ptr `0x77a4fe8`),
   `glDrawArrays` `0x6496e1a` (PLT `0x6d33210`/GOT `0x722a048`), and the
   instanced extension `0x77a4fe0` at `0x6496d75`.
   **Neither `0x64961b4` nor `0x649d422` has any direct `call` xref in the
   ELF** (OBSERVED) — both are vtable/indirect-dispatched from the engine loop.
4. **Present.** The loop dispatches the render-context vmethod
   `call [vtable+0x18]` with `vtable = 0x71912e8`, slot `0x7191300 = 0x649d422`
   (device wrappers at `0x648b94c` wrap the ctx in a `0x20` object with vtable
   `0x71908e0`, ctx at `+0x18`; `0x6490140`/`0x649013c` dispatch the rebind via
   `jmp *0x10(%rax)`). The swap tail-jumps to `eglSwapBuffers@plt 0x6d334d0` →
   GOT `0x722a1a8`, where the RA is a stable guest return site after the
   indirect call. That is what would increment `s_swaps_guest`.

---

## 5. What `render_guest` creates today — and why `draws=0`

`sober_render_guest_run()` = `install()` + `bind_surface()` + `create_context()`
(`render_guest.c:550-557`). Concretely it does:

- installs three GOT hooks: `fromSurface 0x72294a8`, `swap 0x722a1a8`,
  `eglGetProcAddress 0x722a180`;
- host-publishes its shaped window at global `0x72a3308` (+ generation
  `0x72a3310`) and drives the four lifecycle thunks (`life=1/1/1/1`,
  `from_surface=2`);
- calls guest `0x649c21e(window, pbuffer=0)` and verifies the returned context's
  vtable equals `base+0x71912e8`.

It does **not** call `sober_render_guest_swap_via_vtable()` (`render_guest.c:520`)
and a whole-tree grep confirms that function — and `sober_render_guest_context()`
(`:580`) — are **never called by any harness or gate file**. So the genuine
guest context it built is **idle**: created, vtable-verified, never presented.

The only swap is from `guest_render.c:347-362`: a *separate* stack `uint64_t
ctx[9]` (0x48 bytes) with only `+0x20` (EGLDisplay) and `+0x28` (EGLSurface)
filled, passed directly to `0x649d422` from host code (RA host → `synthetic`).
That wrapper issues no draw, creates no surface, and does not use the
vtable-verified context.

The counter triple is explained exactly:

| Counter | Value | Cause (OBSERVED) |
| --- | --- | --- |
| `gles_loads / proc_loads` | `48` | guest EGL init loads ~70 GLES pointers through the hooked `0x722a180` (RA inside `0x649c633..0x649d300`) |
| `draw_wrapped` | `5` | 5 loaded names matched `k_draw_names` and got counting trampolines — a *wrap* count, not invocations |
| `draw_calls` / `draws` | `0` | the wrapped draw functions are **never called**: the guest draw path (`0x64961b4 → 0x6496d0c`) requires a running frame loop |
| `swaps(total/guest/synth)` | `1/0/1` | one host Route-B call; `rg_in_guest(ra)` false because the caller is the host |

The decisive broken link is in `rg_bind_surface()` (`render_guest.c:402-404`):
the synthetic `nativeData`'s callback slots `+0x88/+0x98/+0xa0` are deliberately
**NULL** (no JVM; honesty rule — the guest NULL-checks each before an indirect
call). Therefore the redraw thunk `0x2a20a00` takes `0x2a20a1b → 0x2a20a22`
(`ret`) and never executes `0x2a20a20` (`jmp *%rax`). The `destroyed` thunk
`0x2a20a30` further zeroes `nativeData+0x140`, so even the host-written window
global at `0x72a3308` is republished manually (`render_guest.c:326-329`) rather
than being held by the guest's own bind helper `0x26fea5a`/`0x26fea78`.

---

## 6. The exact missing call / thread / state

Stated as precisely as the evidence allows (all RVAs OBSERVED; mechanism of the
engine loop itself is the UNKNOWN noted in §7):

1. **Missing state — the frame callback object.** A genuine, non-NULL
   `[nativeData+0x98]` (and realistically `+0x88`, `+0xa0`) pointing at the
   engine's own redraw/create/destroy callbacks. These are installed by the real
   Java surface lifecycle through JNI. With no JVM they stay NULL, so the redraw
   thunk `0x2a20a00` stops at `0x2a20a1b → 0x2a20a22`. This is the **exact
   instruction where the guest render path dies today**.
2. **Missing thread/call — the engine render loop.** The guest graphics/render
   task that, on the redraw callback, runs a frame and dispatches
   `call QWORD PTR [vtable+0x18]` → `0x649d422` (and draws via `0x64961b4 →
   0x6496d0c`). It is only started by the completed app-start/game-loaded
   sequence: `nativeAppBridgeV2StartAppWithParams 0x26fe80f`,
   `nativeAppBridgeV2StartGameWithParam 0x2ebd51a`, notifier `0x27eaf6e` → thunk
   `0x25848e0` (`gameLoadedCallback`), and `nativeScheduleOnFirstFrame
   0x2ebf950`. In the current run `callback=0`, the FFlag/singleton gate returns
   `-3` and late guest workers fault (`0x2e5d42a` family), so that chain never
   completes and **no engine render thread exists** for the harness to use.
3. **Missing consequence — counted guest draws.** With no loop, `0x6496d0c` is
   never entered, so `gles_draw_calls` stays 0 and the gate's draw conjunct can
   never pass.

No single "jump to the frame loop" RVA exists: the frame path is reached by a
chain of genuine guest state (JNI-installed callbacks + a live engine render
thread), all of which the headless harness currently lacks. Supplying it with
host-built callbacks or a host-driven loop would be a **withheld seed**, not
proof (§7).

---

## 7. Acceptance evidence (what would count, and what is true today)

Gate contract (`MODULE_INTEGRATION.md §2`, `T3_5_render-evidence.md §4`):
`render_ok = sober_render_phase3_done() && guest_tagged_render_ok()`. A genuine
guest frame requires **all** of, in the same clean run with every opt-in unset:

| # | Required observation | RVA / counter | Today |
| --- | --- | --- | --- |
| R1 | ≥2 swaps whose RA at GOT `0x722a1a8` lies in `[base, base+0x6d33b90)`; stable RA after a `call [vtable+0x18]` | `0x7191300→0x649d422`; `s_swaps_guest` | **0** |
| R2 | ≥1 counted guest GLES draw between two tagged swaps (guest RA at draw sites) | `0x6496d0c`, `0x6496dbc/dec/e1a`, `0x77a4fe0/8`; `gles_draw_calls` | **0** |
| R3 | no synthetic swap counted as proof; mixed evidence fails closed | `s_swaps_synthetic=1` today | needs suppression |
| R4 | window bound with guest RA + generation delta; fromSurface/lifecycle path | `0x72a3308/0x72a3310`, `0x26fea78`, `0x2a2087c/0x2a20907` | host-written only |
| R5 | phase-3 ancestry preserved | `first frame rendered` | present |

Current authoritative log (`w_after.out:3672-3678`):
```
[render] enabled=1 installed=1 rc=0 ... ctx=0x5637a86f92b0
         vt=0x7f055d8912e8(vt_expect=0x7f055d8912e8) life=1/1/1/1
         from_surface=2 gles_loads=48 draw_wrapped=5 draw_calls=0
         swaps=0 guest=0 synthetic=0 ...
[render] render_guest counters: ... draws=0 swaps(total/guest/synth)=0/0/0 ...
[render] guest call 0x649d422 swap(...) | guest-driven swaps observed: 1
[render] render_guest counters: ... draws=0 swaps(total/guest/synth)=1/0/1 ...
first frame rendered
[app-shell] marker withheld: diagnostic guest-state seed active (no app-shell frame proven)
[app-shell] marker withheld: callback=0 render=0 (...)
```

To turn this diagnostic green (not the marker): make the frame path actually run
so that `rg_hook_swap` records a guest `.text` RA and the wrapped draw
trampolines are invoked; then pin the RA offline — `objdump` the 5 bytes before
each captured RA and require an indirect `call *0x18(%r..)` through slot
`0x7191300`, and require a **stable** RA across ≥2 tagged swaps (T3-5 R1
defence).

**Honesty caveat (decisions 013/038, binding):** every way to drive the above
from this harness (host-built `nativeData` with non-NULL callback fields, token
`Surface`, host-driven thunks, host-written `0x72a3308`, suppressed synthetic
swap) is synthetic state and enters the `seeded_state` withhold chain. Even a
positive `guest-tagged` verdict would remain `marker withheld`; a true green
additionally needs the genuine notifier-driven `gameLoadedCallback`
(`0x27eaf6e → 0x25848e0`, `placeId>0`, RA `base+0x27eafb3`) with zero opt-ins.
This report recommends **no** gate relaxation and claims **no** success.

---

## 8. Limits / unknowns

- The exact guest frame-loop function that executes `call [vtable+0x18]` cannot
  be pinned statically: `0x649d422` and the draw-submit function `0x64961b4`
  have zero direct call xrefs (indirect/vtable dispatch only). It must be
  discovered from a captured guest RA (§7).
- Whether the engine render thread can be revived in this headless harness
  without a JVM is **UNKNOWN**; the state it depends on (JNI-installed surface
  callbacks, notifier-driven game-loaded, scheduler/FFlag substrate) is exactly
  the unresolved BLK-01/BLK-06/BLK-07 work.
- `render_guest` is DEFAULT-ON with kill switch `SOBER_RENDER_GUEST=0`; the
  `env=SOBER_RENDER_GUEST=0` text in `sober_render_guest_describe()` is the
  format string naming the switch, not a statement that it is set.

---

## 9. Evidence index

Read only: `src/jni/render_guest.c/.h`, `src/linux/guest_render.c`,
`analysis/render/render-map.md §2-5`, `analysis/C04-render-integration.md §1-3`,
`analysis/T3_5_render-evidence.md §2-4`, `analysis/R1_zero_tagged_swaps.md`,
`analysis/MODULE_INTEGRATION.md §2`, `analysis/C09-marker-evidence.md §3`,
`docs/REVIEW-100.md §6`, and runtime log `/tmp/opencode/w_after.out`.
Fresh RE work this task: `objdump -d` slices at `0x649c21e`, `0x649d422`,
`0x648b94c`, `0x6490140`, `0x2a20a00`, `0x2a20850`, `0x64961b4`, `0x6496d0c`;
APS2 RELA decode (`DT_ANDROID_RELA=0x15e00`) for vtable `0x71912e8`; byte-level
`call rel32` xref scan; `sha256sum`/`readelf -nW` identity. No other file was
created or modified inside the repository; marker WITHHELD.
