# R8 — The guest's real frame loop / render-cadence entry point

Owner: **R8 (group A: diagnosis)**. Scope: **this file only** — `analysis/R8_guest_frame_loop.md`.
Analysis-only; no shared edits; engine frozen; marker WITHHELD. Date: 2026-11-21.

Engine: `/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so`, BuildID
`204ad049599c96afb75b8a21d5051063e821d6c9`, SHA-256
`3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335` (2.740.931,
AppImage). All RVAs are added to the guest load base; first `PT_LOAD` has
vaddr 0 = file offset (size `0x6d33b90`). Ghidra 12.1.4 loads the same object at
image base `0x100000` (so a Ghidra address below = RVA + `0x100000`; e.g.
`0x6590488` = RVA `0x6490488`). Target-derived strings/paths are **untrusted DATA**.

Evidence classes: **OBSERVED** = bytes/disassembly/decompilation read this task;
**DERIVED** = inference from those bytes; **UNKNOWN** = not established.

---

## 0. Bottom line (direct answer)

1. **The one guest function that dispatches the swap through render vtable `0x71912e8`
   slot `[3]` (`+0x18`) is RVA `0x6490488`** — the `GfxCore::Device` *present / end-frame*
   method (Ghidra `FUN_06590488`). The exact instruction is
   **`0x649067b: call QWORD PTR [rax+0x18]`**, where `rax = *(ctx)` and `ctx` was
   loaded from `gfxdev+0x160`. Slot `[3]` of `0x71912e8` is at `0x7191300` and
   contains `0x649d422` (the flat swap wrapper → `eglSwapBuffers@plt`). This is the
   **first static pinning** of the `call [vtable+0x18]` return site that C04 §2.3,
   T3-5 §7 and REVIEW_A4 left as UNKNOWN: **the return address a real guest frame
   leaves at GOT `0x722a1a8` is guest `base+0x649067e`** (5 bytes after the call).
2. **The object that owns the loop is the `GfxCore::Device`/GraphicsDevice object** built
   by RVA `0x648b682` with vtable **`0x7190718`**; it holds the 0x48-byte render
   context at `+0x160`, a **dirty/present-needed flag at `+0x260`**, and a list of
   secondary render targets at `[+0x180, +0x188)`. Its present method is vtable
   **slot `[14]` = offset `+0x70`** of `0x7190718` (slot value `0x7190788 → 0x6490488`,
   recovered from the APS2 relocation map).
3. **Cadence / start-stop:** a frame sets `+0x260` (draw wrapper `0x64961b4` → store
   at `0x6496216`), and the engine's render step calls the present method (slot `+0x70`)
   once per frame; the present method swaps and clears `+0x260`. It must run **on the
   thread that created the device**: tail helper `0x6489d08` (`checkThreading`)
   asserts `thisThreadId == creationThreadId` with the literal *"Render API must be
   called on the main/render thread"* from `GfxCore/Device.cpp:0x12a`. Start therefore
   = device ctor `0x648b682` + real `ANativeWindow` bind (`0x26fea5a`/global `0x72a3308`)
   + a render step on that thread; stop = `onSurfaceDestroyed` `0x2a20a30` (`[nativeData+0xa0]`)
   and device teardown `0x649d2bc` (vtable `[0]`; re-seats vptr to `0x7191368` at
   `0x649d332`, releases window at `0x649d32d`).
4. **Why our harness never enters it:** the harness never builds a `0x7190718`
   GraphicsDevice and never calls `0x6490488`. It calls the flat swap wrapper
   `0x649d422` **directly** (Route B) or reads vtable slot `0x7191300` and calls the
   function pointer from **host** code (`render_guest.c:520-546`). Its synthetic
   `nativeData` deliberately leaves callback slots `+0x88/+0x98/+0xa0` NULL, so the
   guest's redraw trigger `jmp *[nativeData+0x98]` (`0x2a20a20`) is a no-op. There is
   no real Java `Surface`, no guest-bound `ANativeWindow`, and no Lua/TaskScheduler
   render step. Consequently **zero real guest frame loops run**, and the GOT hook at
   `0x722a1a8` only ever sees a host return address (correct RED state).
5. **Minimal real-guest-code opt-in** (§6): construct (or fake) a GraphicsDevice-shaped
   object with `+0x160 = ctx`, `+0x260 = 1`, empty secondary list, call RVA `0x6490488`
   on the device thread. The swap then executes inside guest code with RA
   `base+0x649067e` — a guest-tagged swap produced entirely by the guest's own frame
   path. This is a **diagnostic only**; per decisions 013/037/038 it force-withholds
   the marker.

---

## 1. Build identity, evidence, constraints

| Field | Value |
| --- | --- |
| SHA-256 / BuildID | `3e498d64…41335` / `204ad049…6c9` (re-verified with `sha256sum`/`readelf -nW`) |
| Ghidra image base | `0x100000`; ELF loaded with `x86:LE:64:default:gcc`, auto-analysis complete |
| APS2 relocations | `decode_aps2.py` → 565390 relocs; vtable slots are zero in the file, populated at runtime |
| Disassembly | `/tmp/opencode/libroblox.dis` (objdump, full file), targeted `objdump -d` on the frozen ELF |
| Prior analysis | `analysis/render/render-map.md` §3, `analysis/C04-render-integration.md` §2.3, `analysis/C08-surface-frame.md` §2, `analysis/T3_5_render-evidence.md` §2 |

Everything below is read-only observation of those bytes plus Ghidra decompilation
(evidence IDs: `ev_7300a729…` xrefs, `ev_44ae2455…` document/session, and the
`analyze_function` dossiers for `0x6590488` and `0x6589d08`). No runtime claim is made.

---

## 2. The render vtable and the swap slot

APS2 decode of vtable `0x71912e8` (all OBSERVED from the frozen ELF):

```
[0x71912e8] +0x00 = 0x649d2bc  teardown (eglMakeCurrent(null)/DestroyContext/DestroySurface/Terminate/release)
[0x71912f0] +0x08 = 0x649d356  teardown+free wrapper
[0x71912f8] +0x10 = 0x649d37c  rebind (eglGetCurrentContext / eglMakeCurrent)
[0x7191300] +0x18 = 0x649d422  *** SWAP: mov 0x20(%rdi),%rax; mov 0x28(%rdi),%rsi;
                                        mov %rax,%rdi; jmp 6d334d0 <eglSwapBuffers@plt> ***
[0x7191308] +0x20 = 0x1df06e0  shouldDisplayOpenGLUnsupportedMessage
[0x7191310] +0x28 = 0x649d432  ANativeWindow release/acquire
[0x7191318] +0x30 = 0x649d45e  predicate: return *(int32*)(ctx+0x44) > 0   (NOT a swap)
[0x7191320] +0x38 = 0x1dd72a0  nativeOnFragmentStart
[0x7191328] +0x40 = 0x649d466  surface destroy/recreate (eglMakeCurrent(null)/eglDestroySurface)
[0x7191368]       = 0x649d5e6  post-teardown vtable (installed at 0x649d332)
```

- Only **two** code references to `0x71912e8` exist in all of `.text`:
  `0x649c29f` (`lea … #71912e8; mov %rcx,(%rdi)` — installs vptr at `ctx+0` in EGL
  init `0x649c272`) and `0x649d2c5` (teardown re-installs it before switching to
  `0x7191368`). So the frame loop never names the vtable directly; it loads the
  vptr from the context object. That is why the slot `[3]` call is an indirect
  `call *0x18(%reg)`.
- Clarification of the task's phrase *"render vtable +0x18 / swap +0x30"*:
  the swap is **slot `[3]` at vtable offset `+0x18`** (`0x7191300 → 0x649d422`);
  `+0x30` of the vtable is the predicate `0x649d45e`, and `+0x30` of the 0x48-byte
  **context** is the EGLContext slot (read by the surface helper `0x649d466` at
  `0x649d4a1`). Neither `+0x30` spelling is itself a swap dispatch.

**Context layout** (from `0x649c272`, `0x649d1c2`, `0x649d422`, all OBSERVED):
`+0x00 vptr=0x71912e8`, `+0x10 pbuffer`, `+0x18 ANativeWindow*`, `+0x20 EGLDisplay`,
`+0x28 EGLSurface`, `+0x30 EGLContext`, `+0x38/+0x3c width/height`, `+0x40/+0x44 flags`.

---

## 3. The frame-loop call site — exact RVA and call order

### 3.1 The `GfxCore::Device` object (vtable `0x7190718`)

RVA `0x648b682` is the device constructor (OBSERVED):
- `0x648b6ec`: `lea 0x7190718(%rip),%rax; mov %rax,(%r15)` — installs vptr `0x7190718`.
- `0x648b940`: `mov (%r14),%rdi` — arg2's first word is the `ANativeWindow*`.
- `0x648b94c`: `call 0x649c21e` (`create_context(window, 0)`) → ctx in `r15`.
- `0x648b954-0x648b971`: allocate 0x20 bytes, vptr `0x71908e0`, ctx at `+0x18`
  (the small wrapper object).
- `0x648b97c`: `[device+0x160] = ctx`; `0x648b98a`: `[device+0x168] = wrapper`.

APS2 decode of vtable `0x7190718` (OBSERVED) — slot `[14]` at offset `+0x70`:

```
[0x7190718]+0x00 0x648ef3e   +0x28 0x648fee0   +0x50 0x1df06e0
[0x7190720]+0x08 0x648feaa   +0x30 0x6490048   +0x58 0x1df06e0
[0x7190728]+0x10 0x1dd72a0   +0x38 0x2e5c060   +0x60 0x1dd72a0
[0x7190730]+0x18 0x1dd72a0   +0x40 0x1dd72a0   +0x68 0x6490432
[0x7190738]+0x20 0x1df6700   +0x48 0x6490182   +0x70 0x6490488  <== PRESENT / END-FRAME
                                                      +0x78 0x649087c
```

### 3.2 The present function RVA `0x6490488` (Ghidra `FUN_06590488`, 0x3a8 bytes)

Exact bytes around the dispatch (objdump, OBSERVED):

```
649060a: ...                          ; after one-time init / glGetError checks
649061d: cmpb $0x0,0x2a0(%rbx)        ; optional "needs fence" branch
6490649: test %r14b,%r14b             ; cVar2 = [rbx+0x260] dirty flag
649064e: mov 0x180(%rbx),%r14         ; secondary target list begin
6490655: mov 0x188(%rbx),%r12         ; secondary target list end
649065c: cmp %r12,%r14
649065f: je   6490671
6490661: mov 0x8(%r14),%rdi
6490665: mov (%rdi),%rax
6490668: call *0x18(%rax)             ; swap each secondary render target
649066b: add $0x28,%r14
649066f: jmp 649065c
6490671: mov 0x160(%rbx),%rdi         ; *** load render ctx from device +0x160 ***
6490678: mov (%rdi),%rax              ; rax = vptr = 0x71912e8
649067b: call *0x18(%rax)             ; *** SWAP = [0x7191300] = 0x649d422 ***
649067e: movb $0x0,0x260(%rbx)        ; clear dirty flag
6490685: ...                          ; flip [rbx+0x150], reset per-view scratch
...
64906dd: call 0x6489d08              ; checkThreading (device must be on creation thread)
```

Ghidra decompilation (OBSERVED, condensed — variable names are ours only where marked):

```c
void device_present(long gfxdev) {                 // RVA 0x6490488
  if (*(char *)(gfxdev + 0x22f)) {                  // one-time init enabled
    if (*(char *)(gfxdev + 0x288)) { ... fence/GLES sync ... }
    if (!*(char *)(gfxdev + 0x288))                // 0x9117 fence object
      *(u64 *)(gfxdev + 0x280) = (*fence_fn)(0x9117,0);
  }
  char dirty = *(char *)(gfxdev + 0x260);
  if (gfxdev+0x298 && *(char *)(gfxdev + 0x2a0))   // optional pre-swap hook
    dirty = (*(...) (**  (gfxdev+0x298) + 0xf8))();
  if (dirty) {
    for (p = [gfxdev+0x180]; p != [gfxdev+0x188]; p += 0x28)
      (**(code **)(**(long **)(p+8) + 0x18))();     // secondary swap
    (**(code **)(**(long **)(gfxdev+0x160) + 0x18))(); // *** MAIN SWAP ***
    *(char *)(gfxdev + 0x260) = 0;
  }
  ... reset [gfxdev+0x150] view toggle and scratch ...
  checkThreading(gfxdev, 0);                       // RVA 0x6489d08
  *(int *)(gfxdev+0x8c) = *(int *)(gfxdev+0x88);
}
```

**Call order for one real frame (OBSERVED/DERIVED):**

```
render step (Engine/TaskScheduler, on device thread)
  -> call [gfxdev_vtable + 0x70]            = slot[14] @ 0x7190788 -> 0x6490488
     -> (draws already executed, [gfxdev+0x260] = 1)
     -> 0x6490671  ctx = [gfxdev+0x160]
     -> 0x6490678  vptr = [ctx] = 0x71912e8
     -> 0x649067b  call [vptr+0x18] -> 0x649d422
        -> 0x649d42d  jmp 0x6d334d0 <eglSwapBuffers@plt> -> GOT 0x722a1a8
     <- return address at GOT hook = base + 0x649067e   (GUEST .text)
     -> 0x649067e  [gfxdev+0x260] = 0
     -> 0x6489d08  checkThreading (main/render thread assert)
```

This is the exact call chain that the prior reports could not pin. A scan of all
11,626 `call *0x18(%…)` sites found **exactly one** whose preceding instruction is a
`+0x160` load from a base register that is the device object: `0x6490671/0x649067b`.
(Other `+0x160` matches are unrelated struct/stack offsets — false positives.)

### 3.3 Threading constraint (`checkThreading`, RVA `0x6489d08`, Ghidra `FUN_06589d08`)

OBSERVED decompilation:

```c
void checkThreading(long *gfxdev, char param2) {
  if (flag_at_0x77a4048) {                      // DAT_078a4048 (thread-check enable)
    char *name = (*(code **)(*gfxdev + 0xd8))(); // vmethod returns "thread id" string
    if (param2 == 0 && *name == '\0') {
      pthread_t self = pthread_self();
      if (gfxdev[2] != self) {                   // gfxdev+0x10 = creationThreadId
        assert_fail("thisThreadId == creationThreadId && "
                    "\"Render API must be called on the main/render thread\"",
                    "…/game-engine/Client/Rendering/GfxCore/Device.cpp", 0x12a,
                    "checkThreading");
        swi(3);                                  // int3 / abort
      }
    }
  }
}
```

Consequences (DERIVED): the present function must be invoked on the thread that
constructed the device (`gfxdev+0x10 = pthread_self()` from `0x648b6af`); a harness
opt-in must either run on that thread or ensure the thread-check flag at RVA
`0x77a4048` is zero. The embedded source path identifies the class as Roblox's
`GfxCore::Device` (data, not a symbol).

---

## 4. What starts / stops the cadence (thread, scheduler, surface, Lua state)

All items OBSERVED unless marked.

**Primary driver:** the engine's **render step** calls the device present vmethod
(slot `+0x70`) once per frame. In the guest this is the `Render` profiler category —
`0x6490488` itself opens `FUN_0237aab8("Render","waitOnGpu"/"waitUntilCompleted",…)`.
Ghidra found **no direct caller** (`callers: []`, xrefs empty): the call is virtual,
matching an engine scheduler/task invoking the vtable slot. The exact scheduler task
that issues `call [gfxdev+0x70]` per frame is **UNKNOWN** statically (the 635
`call *0x70(%…)` sites are generic and none sits in an obvious render loop); the
decisive point for the gate is that the device present method at slot `+0x70` is the
function that dispatches the swap, and its RA (`base+0x649067e`) is the frame-loop
fingerprint.

**Start preconditions:**
1. Device construction `0x648b682` (vptr `0x7190718`, `creationThreadId` at `+0x10`).
2. Real context: `create_context` `0x649c21e` → `0x649c272` installs vtable
   `0x71912e8` at `ctx+0` (`0x649c29f`), EGLDisplay/EGLSurface at `+0x20/+0x28`.
   Pass `pbuffer = 0`; `extra=1` faults at `0x649cf9a` (C04 §1.4).
3. Real `ANativeWindow`: `bind_window` `0x26fea5a` → `ANativeWindow_fromSurface@plt`
   `0x6d31ad0` at `0x26fea78`, publishes global `0x72a3308` (`0x26feaa0`), generation
   `0x72a3310`; called from UpdateSurface `0x277322d`, StartAppWithParams `0x26fe8d2`,
   StartGame `0x2ebd5dd`.
4. Surface lifecycle fills `nativeData+0x140` (window) and installs engine callbacks
   in `nativeData+0x88/+0x98/+0xa0`; Android redraw path `onSurfaceRedrawNeededNative`
   `0x2a20a00` → `jmp *[nativeData+0x98]` at `0x2a20a20`.
5. Draw commands set the dirty flag: draw wrapper `0x64961b4` → `0x6496216`
   `movb $0x1, <device>+0x260` (the object at `[rbx+0x18]` is the device).

**Stop / teardown:**
- `onSurfaceDestroyedNative` `0x2a20a30` → `call *[nativeData+0xa0]` (`0x2a20a45+`).
- Device teardown vmethod `0x649d2bc` (vtable `[0]`): `eglMakeCurrent(null)` →
  `eglDestroyContext/Surface` → `eglTerminate` → `ANativeWindow_release@plt`
  (`0x649d32d`); re-seats vptr to `0x7191368` at `0x649d332`.
- Pause paths release/zero global `0x72a3308` (`0x2ebc8fb`, `0x2ebec04`).

---

## 5. Why our harness never enters the frame loop

OBSERVED from harness source (read-only):

1. **Direct swap, not present.** `src/linux/guest_render.c` Route B builds a 0x48-byte
   stack context and calls `0x649d422` directly; `src/jni/render_guest.c:520-546`
   `sober_render_guest_swap_via_vtable()` manually loads slot `0x7191300` from memory
   and calls the function pointer. Both are **host** callers: RA at the GOT hook is in
   the host binary, so even the "via vtable" option never runs `0x6490488` and can
   never produce RA `base+0x649067e`.
2. **No GraphicsDevice is ever constructed.** No reference to `0x648b682`,
   `0x7190718`, `0x6490488`, `+0x160`, or `+0x260` anywhere in `src/`. There is no
   object with vtable `0x7190718` and no device present vmethod call.
3. **Callbacks deliberately NULL.** `guest_render.c:164-166` and
   `render_guest.c:402-404` leave `nativeData+0x88/+0x98/+0xa0` NULL, with the explicit
   comment "the guest NULL-checks all of them before an indirect call, so no host code
   enters a vtable." Therefore `onSurfaceRedrawNeeded` `0x2a20a00` reaches
   `0x2a20a11/0x2a20a20` but its `jmp *[+0x98]` is a no-op. No guest frame trigger fires.
4. **No real surface / no scheduler render step.** No Java `Surface`: the compat shim
   `android_compat.c:498` ignores args and returns a 12-byte fake; no genuine
   `ANativeWindow` is bound at `0x72a3308`; no Lua app state or TaskScheduler render job
   runs. The four lifecycle thunks are invoked directly with a synthetic token and
   the guest's one GLES context is created via `0x649c21e` with a synthetic window,
   but nothing sets `+0x260` and nothing calls the present method.

Net: the GOT `0x722a1a8` hook sees exactly one **synthetic** swap per run (RA in host),
which is why `REVIEW_A4` records `g_guest_swaps==1`, "no frame loop proven", and the
marker is correctly WITHHELD.

---

## 6. Minimal real-guest-code opt-in experiment (diagnostic, marker WITHHELD)

Goal: make the **guest's own** present function execute the swap, so the GOT hook sees
an RA inside guest `.text`. No guest bytes are patched — we only build a device-shaped
object in our own memory and call a real guest RVA. Add this knob (e.g.
`SOBER_FRAME_LOOP_EVIDENCE=1`) to the `seeded_state` force-withhold chain in
`guest_app_shell.c` before using it.

1. Use the existing guest EGL context `s_ctx` from `0x649c21e(window, pbuffer=0)`;
   verify `[s_ctx] == base+0x71912e8` and `[base+0x7191300] == base+0x649d422`.
2. Allocate a zeroed blob `dev` of at least `0x300` bytes (the present function reads
   up to `+0x298/0x2a0`; `checkThreading` reads `dev[0]` as a vptr at `*dev+0xd8`).
   Populate only:
   - `*(u64*)(dev+0x160) = s_ctx`  (render context),
   - `*(u8*)(dev+0x260) = 1`       (dirty → forces the swap block),
   - `*(u64*)(dev+0x180) = *(u64*)(dev+0x188) = 0`  (empty secondary list),
   - `*(u64*)(dev+0x298) = 0`      (skip optional pre-swap hook),
   - `*(u8*)(dev+0x22f) = 0`       (skip one-time GL init branch).
   - For `checkThreading`: set `dev` vptr to a small host-owned vtable whose `+0xd8`
     returns a non-empty string, **or** ensure RVA `0x77a4048` (thread-check enable) is
     0; run the call on `dev+0x10 == pthread_self()` if checks are enabled.
3. Call **RVA `0x6490488`** with `rdi = dev` (`((void(*)(void*))(base+0x6490488))(dev)`).
   Guest code then executes `0x6490671 → 0x649067b → 0x649d422 → eglSwapBuffers@plt`.
4. In the GOT `0x722a1a8` hook, record `__builtin_return_address(0)`. Expected:
   `ra == base+0x649067e`, inside `[base, base+0x6d33b90)` → `tag=guest-loop`,
   `guest_loop_swaps++`. A/B: with the knob OFF the RA is the host caller of
   `0x649d422`/the manual vtable call → `tag=synthetic`.
5. (Higher-fidelity alternative) instead of a fake blob, call the real device ctor
   `0x648b682(this, &{window})` with the five `ANativeWindow_*` GOT hooks
   (`0x72294a8/0x72294a0/0x72297c0/0x72297c8/0x72297d0`) installed, then set
   `+0x260 = 1` and call `*(void**)(base+0x7190788)(this)` (slot `+0x70`). This enters
   the full `GfxCore::Device` path and also exercises `checkThreading` correctly.

**Honesty:** this supplies a synthetic device/window and an explicit host call — it is
a *guest-origin swap* provenance test, **not** a real app-shell frame. Logging must be
`[ev-*]` only; every knob joins the withhold chain; no `app shell rendered` emission;
engine SHA unchanged. Even a perfect `ra=base+0x649067e` result leaves the marker
WITHHELD until a genuine notifier-driven `gameLoadedCallback` (notifier `0x27eaf6e`,
thunk `0x25848e0`) plus clean state is also proven.

---

## 7. RVA quick reference (AppImage 204ad049)

| RVA / slot | Meaning |
| --- | --- |
| **`0x649067b`** / return **`0x649067e`** | **`call [vtable+0x18]` = swap dispatch inside device present; the frame-loop RA** |
| **`0x6490488`** | device present / end-frame (Ghidra `FUN_06590488`), vtable `0x7190718` slot `[14]` (`+0x70`) |
| `0x6489d08` | `checkThreading` — "must be called on the main/render thread", `GfxCore/Device.cpp:0x12a` |
| `0x648b682` | device constructor; vptr `0x7190718`, ctx at `+0x160`, thread id at `+0x10` |
| `0x7190718` / slot `[14]` @ `0x7190788` | device vtable / present method (`0x6490488`) |
| `0x71912e8` / slot `[3]` @ `0x7191300` | render vtable / swap (`0x649d422`) |
| `0x649c21e` → `0x649c272` | `create_context` → EGL init; `0x649c29f` installs `0x71912e8` at `ctx+0` |
| `0x649d422` / PLT `0x6d334d0` / GOT `0x722a1a8` | flat swap wrapper / `eglSwapBuffers@plt` / observation point |
| `0x649d2bc` / `0x649d332` | device teardown (vtable `[0]`) / re-seat vptr `0x7191368` |
| `0x64961b4` → `0x6496216` | draw wrapper sets device dirty flag `+0x260` |
| `0x26fea5a` → `0x26fea78`; global `0x72a3308` / `0x72a3310` | bind ANativeWindow from Surface; window global / generation |
| `0x2a20850`/`0x2a208c0`/`0x2a20a00`/`0x2a20a30` | surface created/changed/redraw/destroyed thunks; `+0x98` redraw → `0x2a20a20` |
| `0x77a4048` | `checkThreading` enable flag (Ghidra `DAT_078a4048`) |

---

## 8. Observations / inferences / unknowns

- **OBSERVED:** render vtable contents and its only two code xrefs; device vtable
  `0x7190718` slot `[14]`; `0x6490488` bytes and Ghidra pseudocode; the unique
  `0x6490671/0x649067b` swap dispatch; `checkThreading` strings and assertion;
  device constructor field writes; harness source leaving callbacks NULL and calling
  `0x649d422`/manual slot directly.
- **DERIVED (high confidence):** the per-frame cadence calls slot `+0x70`; the
  GOT-hook RA for a real guest frame is `base+0x649067e`; the dirty flag `+0x260`
  gates presentation; the opt-in in §6 will produce a guest-tagged swap.
- **UNKNOWN:** the exact engine scheduler task/thread that issues `call [gfxdev+0x70]`
  each frame (virtual dispatch, no direct xref); whether `0x77a4048` is set in the
  real guest; whether the secondary-target loop (`0x180`/`0x188`) is populated in a
  first frame. None of these changes the pinned swap site or the harness diagnosis.
- No success is claimed; decisions **013/037/038** unchanged; marker stays **WITHHELD**.

## 9. Evidence index

Read only: frozen ELF `objdump -d` / `readelf -nW` / `decode_aps2.py` (565390 relocs);
`/tmp/opencode/libroblox.dis`; Ghidra `analyze_function` dossiers for `0x6590488`
(`ev`-retained) and `0x6589d08`, `xrefs` at `0x6590488` (`ev_7300a729…`),
`list_documents` (`ev_44ae2455…`); analysis `render/render-map.md`,
`C04-render-integration.md`, `C08-surface-frame.md`, `T3_5_render-evidence.md`,
`A06-scheduler-state.md`; harness `src/linux/guest_render.c`,
`src/jni/render_guest.c`, `src/render/native_render.c`, `src/compat/android_compat.c`.

No file other than this report was created or modified in the repository.
