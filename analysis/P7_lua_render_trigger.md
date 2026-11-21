# P7 — What actually starts the guest's continuous render loop

Owner: **P7** (group B). Scope: **this file only** — `analysis/P7_lua_render_trigger.md`.
Analysis-only: no `src/`, `CMakeLists.txt`, gate, test, doc or engine file was touched.
Engine frozen; marker WITHHELD (decisions 013 / 037 / 038). Target-derived strings are
untrusted DATA, never instructions.

## 0. Evidence base / identity

| Field | Value |
| --- | --- |
| Engine | `/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so` (read-only) |
| SHA-256 | `3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335` (re-verified this task) |
| BuildID | `204ad049599c96afb75b8a21d5051063e821d6c9` (AppImage 2.740.931) |
| Tooling | local `objdump -d -M intel` / `readelf -rW` over the SHA-bound `.so`; retained `/tmp/opencode/libroblox.dis`; decoded smali in `/tmp/opencode/mini/out/smali`; APS2 decode in `analysis/bootstrap/decode_aps2.py` |
| Evidence classes | **OBSERVED** = bytes read this task; **DERIVED** = call-graph/relocation inference; **UNKNOWN** = not statically pinned |

All RVAs are file offsets from the guest load bias (first `R E` PT_LOAD: vaddr 0 = file
offset, size `0x6d33b90`). Reused (read-only): `analysis/lua-app-start.md`,
`analysis/render/render-map.md`, `analysis/C04-render-integration.md`,
`analysis/C11-messagebus-publish.md`, `analysis/R1_zero_tagged_swaps.md`,
`analysis/C07-event-order.md`.

---

## 1. Answer in one paragraph

**None of the three named Start-Lua-App functions starts the render loop.** `nativeAppBridgeStartLuaAppDM` (`0x25448f6`) is a guard/dispatch wrapper; its real driver `0x25449fc` builds InitParams, fires two virtual `+0x18` calls on flag/singleton helpers, finalises, fetches an app object and calls the mode-4 AppStart accessor `0x24a8e44`, whose only effect for mode 4 is to **construct and return the MessageBus singleton `0x72cbdd0`** — the same singleton whose `+0x288` list later carries the GameLoaded descriptor (C11). The driver then destroys that reference and returns. No render vtable, no swap, no thread is created anywhere in that chain. The continuous render loop is armed by a **different** transition: the **surface lifecycle → redraw** hand-off. `onSurfaceRedrawNeededNative` (`0x2a20a00`) loads the guest callback at `[nativeData+0x98]` (`0x2a20a11`) and tail-jmps it (`0x2a20a20`); that callback is the guest frame/redraw entry. Each frame eventually dispatches `call QWORD PTR [render_vtable 0x71912e8 + 0x18] = [0x7191300] = 0x649d422` → `eglSwapBuffers@plt 0x6d334d0` → GOT `0x722a1a8`. The render context carrying that vtable is created later, on the render thread, by the app-shell/StartApp machinery (`nativeAppBridgeV2StartAppWithParams` `0x26fe80f` and nearby internal functions — graphics-device factory `0x2702268` → `0x649c21e`/`0x649c272`, §4), with the creating thread recorded via `pthread_self()`; a worker thread is spawned at `pthread_create 0x270862e` with payload `0x649e541`. **The missing event in our harness is the genuine Android surface-lifecycle redraw delivery — `NativeGLJavaInterface.onSurfaceRedrawNeededNative(JLandroid/view/Surface;)V` (with the preceding `onSurfaceCreatedNative`/`onSurfaceChangedNative`) — carrying a guest-owned `nativeData` whose callback slots `+0x88/+0x90/+0x98/+0xa0` are installed.** Our harness drives the thunks with a synthetic `nativeData` whose `+0x88/+0x98/+0xa0` are NULL, so `0x2a20a11/0x2a20a20` skip the redraw and the loop is never entered (R1).

---

## 2. The render loop endpoint (what "+0x18" means)

All OBSERVED / from APS2 decode (render-map §3, C04 §2.3, T3-5 §2.1):

| RVA / slot | Role |
| --- | --- |
| `0x649c21e` → `0x649c272` | `create_context(window, pbuffer=0)` → EGL init; allocates the `0x48`-byte context and installs vtable `0x71912e8` at `ctx+0` (`0x649c29f`) |
| `0x71912e8` / `[3] @ 0x7191300` | render vtable / **swap vmethod `0x649d422`**. Slot index 3 = byte offset `0x18`, hence `call [vtable+0x18]`. |
| `0x649d422` | flat swap: `mov 0x20(%rdi),%rax; mov 0x28(%rdi),%rsi; mov %rax,%rdi; jmp 6d334d0` — **tail `jmp`, no `call`**, so the RA at the PLT is the caller of the indirect `+0x18` dispatch |
| `0x6d334d0` / GOT `0x722a1a8` | `eglSwapBuffers@plt` / `R_X86_64_JUMP_SLOT` — the observation point |
| `0x6496d0c` | guest GLES draw dispatcher (`0x6496dbc` glDrawElements, `0x6496dec`, `0x6496e1a`, `0x6494ae1` glClear) — entered only by a real frame |

The vtable has **no static code reference** to the loop that dispatches it; exhaustive
`call QWORD PTR [reg+0x18]` scanning in the render cluster (`0x6480000..0x64c0000`)
found none, and image-wide there are ~11,600 such sites (C04 §2.3, R1 §5). Honest
boundary: the exact `call [vtable+0x18]` instruction is a **runtime-RA discriminator**,
not a statically pinned site — capture it from the guest return address at GOT
`0x722a1a8`. This does not affect the trigger analysis below.

---

## 3. Trace: `0x25448f6` → `0x25449fc` → mode-4 `0x24a8e44` (and why it is NOT the trigger)

### 3.1 `0x25448f6` — guard/dispatch export (OBSERVED, `fn_StartLuaAppDM.dis`, size `0xf9`)

Reads canary `0x72293f8`, validates `0x72a3268`, builds a stack guard with vtable
`0x6dbc9a0` and self-pointer at `+0x20`, calls `0x2eb6b2a` → `0x2337cc8`
(game-global-init), then runs guard cleanup through vtable slots `+0x20`
(`0x1dd72a0`, bare `ret`) / `+0x28` (`0x2ec0446`, deleting dtor). Slot `+0x30` of that
guard vtable is the heavy driver `0x25449fc`. **No render / thread code here.**

### 3.2 `0x25449fc` — the "real driver" (OBSERVED, `p7_25449fc.dis`)

```
2544a1a  cmp BYTE [0x72cb648],0            ; FFlag EnableAppBridgeComponents
2544a23  call 0x24acb12                     ; build InitParams on stack
2544a2f  call 0x30ef840 ; 2544a41 call [rcx+0x18]   ; flag provider virtual +0x18
2544a46  call 0x24ad6fc ; 2544a66 call [rax+0x18]   ; wrapper virtual +0x18
2544a70  call 0x2512a70                     ; destruct/finalise helper (OBSERVED: only
                                            ;   destroys optional fields; no thread)
2544a7f  call 0x234e0ca                     ; app-ish object (0x230 bytes); byte +0x108
2544aba  test byte [+0x108] ... je 2544b5a
2544ac3  mov edi,0x4 ; call 0x24a8e44        ; *** MODE-4 APPSTART ***
2544acd  mov rbx,rax
2544af4  call 0x262892c                     ; nativeDestroy(the returned singleton)
2544b2b..b55 release shared_ptr control blocks
2544b70  ret
```

The two `call [..+0x18]` at `0x2544a41`/`0x2544a66` are **flag-provider / wrapper
virtuals**, not the render vtable (`0x71912e8`) — different objects, different
vtables. The byte at `+0x108` gates whether mode-4 AppStart is called at all.

### 3.3 `0x24a8e44` — mode-4 AppStart accessor (OBSERVED, `p7_24a8e44.dis`)

```
24a8e4b  mov ebx,edi                        ; ebx = mode (= 4 from 0x2544ac3)
24a8e4d  test byte [0x72cc270] ...
24a8e8e  lea rdi,[0x72cbdd0]; call 0x30f9aae ; MessageBus ctor (C11 §2)
24a8ebb  lea rdi,[0x72cc278]; call 0x30f9aae ; (second singleton ctor)
24a8e61  cmp ebx,0x3
24a8e64  lea rcx,[0x72cc278]; lea rax,[0x72cbdd0]
24a8e72  cmove rax,rcx                      ; mode==3 -> 0x72cc278; else -> 0x72cbdd0
24a8e7a  ret
```

**Mode 4 is not a "start rendering" command.** It is a singleton accessor/initialiser:
for mode 4 it returns `0x72cbdd0`, the **MessageBus singleton**. C11 proved this is the
object whose `+0x288` subscription list is registered by `nativeAppBridgeV2InitWithParams`
(`0x24abf96 → 0x24ad699 → 0x253cb56(1,&0x7234520)`) and later walked to publish the
GameLoaded event. The caller at `0x2544acd..0x2544af7` immediately passes that pointer to
`nativeDestroy` (`0x262892c`) — i.e. the driver merely *ensures the singleton exists*.
Nothing in `0x24a8e44` touches `0x71912e8`, `0x649d422`, any EGL/GLES PLT, or any
`pthread_create`.

**Conclusion of the trace:** the Start-Lua-App-DM chain is **app/MessageBus plumbing**.
It prepares the app delegate (`app+0x408` at `0x25d7e75`/`0x25f7e7e`, per
`lua-app-start.md §3.3`) and seeds the MessageBus used for the later GameLoaded
publication. It does **not** arm the continuous render loop and does **not** create the
render thread.

---

## 4. Where the render context / render thread actually come from

These are created by the **app-shell / StartApp** side (`uh/f.G` → export
`nativeAppBridgeV2StartAppWithParams` `0x26fe80f`, size `0x587`, i.e. body
`0x26fe80f..0x26fed96`), not by StartLuaAppDM. The RVAs below at `0x2702xxx`/`0x2708xxx`
are **distinct internal functions in the same image neighbourhood**; objdump labels them
as offsets of that export because it is the nearest exported symbol, but they are outside
the export's own body. All OBSERVED unless marked.

### 4.1 Graphics-device factory `0x2702268` (internal, near `nativeAppBridgeV2StartAppWithParams`)

```
2702275  cmp edi,0x4
2702278  je 270229b                       ; mode 4 -> alt device init
; mode 0 (surface-backed):
2702281  mov edi,0x348; call 0x1dbab02     ; alloc device object
2702294  call 0x648b682                   ; graphics device init (window path)
; mode 4 (alt/headless):
270229b  mov edi,0x880; call 0x1dbab02
27022ae  call 0x2702330                   ; alt device init, vtable 0x71913c0
```

`0x648b682` (OBSERVED, `p7_648b682.dis`) is the real device constructor:
- `call pthread_self@plt 6d31c30; mov [r15+0x10], rax` (`0x648b6af`–`0x648b6b4`) —
  **records the creating ("render") thread**;
- installs vtable `0x7190718` at `[r15]` (`0x648b6e5`–`0x648b6ec`);
- `0x648b940: mov rdi,[r14]; 0x648b94a: xor esi,esi; 0x648b94c: call 0x649c21e` —
  creates the `0x48`-byte render context with **`pbuffer = 0`** (the correct window
  path; `extra != 0` would install a bogus pbuffer pointer and fault at `0x649cf9a`,
  C04 §1.4), then wraps it in a `0x20` object with vtable `0x71908e0` and the context
  at `+0x18` (`0x648b954`–`0x648b971`).

The alternative mode-4 device `0x2702330` likewise calls `pthread_self` (`0x270235d`)
and installs vtable `0x71913c0` (`0x2702393`). Either way the device records the thread
it was built on — this is the thread-affinity the guest later enforces ("Render API
must be called on the main/render thread").

### 4.2 The spawned worker thread (`pthread_create 0x270862e`, OBSERVED)

In the internal function at `0x27085ce..` (same StartApp image region, outside the
`0x26fe80f` export body):
```
27085e9  call 0x2e5b6f0                ; thread bookkeeping
27085fe  lea rcx,[0x649e541]           ; payload stored at thread-arg +0x8
270862e  call pthread_create@plt 6d31cd0
```
The payload `0x649e541` is a render/graphics job-processing worker located in the
render cluster (`0x649…`): it loops over work queues (`[rbx+0xd0]/+0x130`,
`+0x148/+0x150`, `+0x160`), calls `0x64a0150` to process jobs and `0x649da91` for
device work, and is entered by the generic thread trampoline `0x64a113b` via
`call [rbx+0x8]` at `0x64a1175`. **Confidence: strongly supported (DERIVED)** that this
is the render worker thread; I did not statically isolate the single
`call [vtable+0x18]` instruction inside it (see §2 boundary). Two other nearby
`pthread_create`s (`0x6474599` payload `0x647461c`, `0x64b31f1` payload `0x64b642c`)
create other engine workers and are not on this path.

---

## 5. The actual transition that starts the continuous loop

### 5.1 The surface-lifecycle dispatch table (OBSERVED; render-map §4)

Guest registers these Java→native thunks (descriptor table `0x6d37c80..0x6d37d78`,
filled by APS2):

| Callback | Thunk | Guest action |
| --- | --- | --- |
| `onSurfaceCreatedNative(JLandroid/view/Surface;)V` | `0x2a20850` | release old `[obj+0x140]`; `ANativeWindow_fromSurface` (`0x2a2087c`); store new window `0x2a20881`; call `[obj+0x88]` (`0x2a208a6`) |
| `onSurfaceChangedNative(JLandroid/view/Surface;III)V` | `0x2a208c0` | acquire/rebind; call `[obj+0xa0]` (`0x2a20933`, destroy) and `[obj+0x88]` (`0x2a20957`, create/rebind); on size change call `[obj+0x90]` (`0x2a209ce`) |
| **`onSurfaceRedrawNeededNative(JLandroid/view/Surface;)V`** | **`0x2a20a00`** | `rsi=[obj+0x140]` (window); load `[obj+0x98]` at `0x2a20a11`; **tail-jmp it at `0x2a20a20`** — the frame/redraw trigger |
| `onSurfaceDestroyedNative(J)V` | `0x2a20a30` | call `[obj+0xa0]` (`0x2a20a54`) |

Each dispatch is guarded by a NULL check of the callback pointer: `+0x88 @0x2a20897`,
`+0xa0 @0x2a2092b` / `@0x2a20a4f`, `+0x98 @0x2a20a18`, `+0x90 @0x2a209be`.

### 5.2 The state transition (DERIVED, high confidence)

The continuous loop starts when the **NativeGL app has a bound ANativeWindow at
`[nativeData+0x140]` *and* the guest has installed a real frame/redraw callback at
`[nativeData+0x98]`**, and Android delivers `surfaceRedrawNeeded`. The chain is:

```
Java SurfaceHolder.Callback.surfaceRedrawNeeded(Surface)
  -> NativeGLJavaInterface.onSurfaceRedrawNeededNative(JLSurface;)V   [thunk 0x2a20a00]
     -> 0x2a20a11  rax = [nativeData+0x98]        (NULL-check @0x2a20a18)
     -> 0x2a20a20  jmp rax                        (guest frame callback)
        -> guest frame loop / GLES draw dispatcher 0x6496d0c
           -> call QWORD PTR [render_vtable 0x71912e8 + 0x18]  (= 0x649d422)
              -> 0x6d334d0 eglSwapBuffers@plt -> GOT 0x722a1a8
```

In JNI call order (C07 §1) this is the **app-shell surface-ready transition**, driven by
`nativeAppBridgeV2StartAppWithParams` (`0x26fe80f`, `uh/f.G`) and
`nativeAppBridgeV2UpdateSurfaceAppWithPlatformParams` (`0x2773194`, `uh/f.I`,
"ASMA.updateWindowHandle") with a real `Surface`. The app-start bind helper `0x26fea5a`
calls `ANativeWindow_fromSurface@plt` at `0x26fea78` and stores the window in global
`0x72a3308` (`0x26feaa0`) — the prerequisite for any frame. It is **not** the
StartLuaAppDM call.

---

## 6. The missing event in our harness (the deliverable)

R1 (`analysis/R1_zero_tagged_swaps.md`) establishes the live symptom:
`swaps(total/guest/synth)=1/0/1`, `draws=0`, `guest_tagged=0`. Root cause, restated in
this task's terms:

1. The harness drives all four lifecycle thunks (`0x2a20850/0x2a208c0/0x2a20a00/
   0x2a20a30`) with a **calloc'd synthetic `nativeData` whose callbacks `+0x88`, `+0x98`,
   `+0xa0` are deliberately NULL** (`src/jni/render_guest.c:402-404` per R1). The guest
   NULL-checks each, so `onSurfaceRedrawNeededNative` reaches `0x2a20a11` but skips the
   `jmp` at `0x2a20a20`.
2. No redraw callback ⇒ **no guest frame loop** ⇒ the draw dispatcher `0x6496d0c` is
   never entered and no `call [vtable+0x18]` → `0x649d422` occurs with a guest RA. The
   one observed swap is the host's Route-B direct call at `src/linux/guest_render.c:354`
   (RA in host) — correctly classified synthetic.
3. The render device/vtable itself is fine (`ctx` created via `0x649c21e(pbuffer=0)`,
   `vt=0x71912e8`, GLES procs loaded). What is missing is purely the **redraw →
   frame-loop → vtable-dispatched swap** event.

**Missing JNI/native event (precise):**
> A genuine `onSurfaceRedrawNeededNative(JLandroid/view/Surface;)V` delivery to the
> guest — preceded by `onSurfaceCreatedNative`/`onSurfaceChangedNative` — with
> `[nativeData+0x140]` bound to an ANativeWindow and `[nativeData+0x98]` holding a
> **real guest frame/redraw callback** (and, for a coherent lifecycle, `+0x88`/`+0x90`/
> `+0xa0` also guest-owned). That callback is what enters the continuous loop whose
> frames dispatch `call [render_vtable+0x18]` → `0x649d422`.

Why the harness cannot get it for free:
- The event is a **Java `SurfaceHolder.Callback`** delivered by Android/ART with a real
  `android.view.Surface`. A native-only harness has no JVM/Looper, so it cannot be
  generated by the real caller; `onAppShellReloadNeeded` is likewise dead from the
  native side in this build (C11 / shellcb #467).
- The nearest native substitutes already present — Route-B `call 0x649d422` and even
  `sober_render_guest_swap_via_vtable()` (`render_guest.c:520-548`) — produce a **host
  RA**, so they can never yield a guest-tagged swap (R1 §3).
- Providing the callbacks requires guest-owned state: a window genuinely bound through
  `0x26fea78`/`0x2a2087c` (global `0x72a3308`) and a guest-built `nativeData` whose
  `+0x98` points into the guest's own frame code. Seeding it from the host is a
  **decision-013 withhold** (R1 §5-§6): even a positive `guest_tagged`/`draws` would
  still print `marker withheld: diagnostic guest-state seed active`.

**Ranked, minimal fix direction for lead (analysis only):**
1. Make `[nativeData+0x98]` (and realistically `+0x88`/`+0xa0`) point to genuine guest
   callbacks obtained from the guest's own lifecycle object, and drive `0x2a20a00` so
   `0x2a20a11`/`0x2a20a20` execute — this is the single change that can flip
   `guest_tagged 0→1`. Do it as a labelled, withheld diagnostic.
2. In the same window, get a counted guest GLES draw through the hooked
   `eglGetProcAddress` (`0x722a180`) pointers / `0x6496d0c` dispatcher (gate also needs
   `gles_draw_calls≥1`).
3. Require the window global `0x72a3308` to be **guest-written** (RA at `0x26fea78` /
   `0x2a2087c` / `0x2a20907`), not the current host store (`render_guest.c:326-329`).
4. Leg C remains decisive: none of the above can lift the marker without a genuine
   notifier-driven `gameLoadedCallback(J)V` (notifier `0x27eaf6e` → thunk `0x25848e0`,
   class `0x72a2f48`, method `0x72a2fc0`, `placeId>0`, RA `base+0x27eafb3`) with zero
   seeded state. The trace in §3 is exactly the plumbing that must precede it.

---

## 7. Confidence summary

| Claim | Confidence |
| --- | --- |
| `0x25448f6` is a guard/dispatch; `0x25449fc` is the driver; mode-4 `0x24a8e44` initialises/returns MessageBus `0x72cbdd0` | OBSERVED (disassembly) |
| Start-Lua-App chain does NOT create a render thread or dispatch the render vtable | OBSERVED (no such call in the chain) + strongly supported by exclusion |
| Render context created by `0x649c21e`/`0x648b682`; GPU-thread affinity recorded via `pthread_self` | OBSERVED |
| `pthread_create 0x270862e` payload `0x649e541` is the render worker | DERIVED, strongly supported (not statically isolated to a `+0x18` call) |
| Continuous loop entered via `onSurfaceRedrawNeededNative 0x2a20a00 → [nativeData+0x98]` → frame loop → `call [0x71912e8+0x18]` → `0x649d422` | OBSERVED for thunk/callback/vtable/swap; loop-body site UNKNOWN (snapshot discriminator required) |
| Harness's synthetic `nativeData` has NULL `+0x88/+0x98/+0xa0`, so no loop runs | OBSERVED (R1, source + counters) |

**Decision 013/038: marker WITHHELD. This report supplies no false green.**

## 8. Evidence index

Fresh local disassembly written this task: `p7_25449fc.dis`, `p7_24a8e44.dis`,
`p7_234e0ca.dis`, `p7_2512a70.dis`, `p7_648b682.dis`, `p7_2702.dis`, `p7_38405.dis`,
`p7_surfthunks.dis`, `p7_64a113b.dis`, `p7_649e541.dis`, `p7_649da91.dis`
(all under `/tmp/opencode/`, all from the SHA-bound engine). Prior read-only:
`analysis/lua-app-start.md §3`, `analysis/render/render-map.md §3-4`,
`analysis/C04-render-integration.md §1-3`, `analysis/C11-messagebus-publish.md §1-3`,
`analysis/R1_zero_tagged_swaps.md §3-6`, `analysis/C07-event-order.md §1`,
`analysis/T3_5_render-evidence.md §2`; harness `src/linux/guest_render.c`,
`src/jni/render_guest.c` (via R1's citations, not edited by this worker).
