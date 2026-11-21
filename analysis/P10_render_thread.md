# P10 — The guest frame producer: native cadence thread, RenderJob creation site, and a guest-code-only swap/draw experiment

Author: **P10** (group B). Scope: **this file only** — `analysis/P10_render_thread.md`.
Analysis-only: no `src/`, engine, gate, test, CMake or marker file was touched.
Engine frozen; marker **WITHHELD**. All target-derived bytes/strings are treated
as **untrusted DATA**, never as instructions.

**Frozen target for every RVA:** AppImage engine
`/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so`, BuildID
`204ad049599c96afb75b8a21d5051063e821d6c9`, SHA-256
`3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335`
(2.740.931). Re-verified this task with `sha256sum` + `readelf -nW`. First
`PT_LOAD`: file offset == VMA == RVA, size `0x6d33b90`, so guest `.text` is
`base ≤ ra < base+0x6d33b90`. The Flatpak engine `791c3f9e…` in
`analysis/rea/work/` is **not** used here. Evidence classes: **OBSERVED**
(disassembly / bytes / strings read this task), **DERIVED** (inference),
**UNKNOWN** (explicitly not established).

---

## 0. Front-loaded answer

1. **The repeating frame producer is not one function — it is a three-layer
   chain**, all resident in real guest code:
   - **Cadence / clock thread:** the native **`RBX IoEvLoop`** thread created at
     **`0x6474599`** (`pthread_create@plt`), start routine **`0x647461c`**,
     created from inside `nativeAppBridgeV2InitWithParams` (`0x24abf96`) via
     calls at **`0x251e04f` / `0x251e0e1`** to creator `0x6474538`. Its clock is
     a **native `timerfd`** (create `0x251d933`, arm `0x251effa` / `0x251f0cd`)
     multiplexed through **`epoll_wait`** (`0x6d323d0`, call `0x251f19c`) plus
     an `eventfd` (`0x251d8ed`). **This is the "no Java Looper" answer: the
     pacing loop is pure native epoll/timerfd.**
   - **Work / task that runs a frame:** the **`SurfaceController::RenderJob`**
     registered with the engine **TaskScheduler**. Its construction site is
     **`0x276d36a`** (allocates the 0x1e0-byte job object, vtable
     `0x6e041d0`, and emits the `renderjob_creation_end` trace at
     **`0x276d285`**); its `PreRenderJob` is created at `0x276d5ea`. It is
     built during app-start/post-settings init at **`0x321571e`**. The
     TaskScheduler's render phase is profiled under `Scheduler Render: `
     (string `0x243150`, referenced `0x26ddf35` / `0x26de036`). TaskScheduler
     worker threads are named `WorkerThreadTaskFactory::workerThreadLoop`
     (string `0x22ba3b`, installed at `0x24e3a1e`, setname path via `0x24e4dcc`).
   - **The actual per-frame guest instructions:** the render device's
     vtable-dispatched draw and swap. Draw-submit `0x64961b4` sits at vtable
     slot `0x7190ec0` and calls the GLES dispatcher `0x6496d0c`; swap
     `0x649d422` sits at render-vtable slot `0x7191300`. Both are reached
     only through guest `call *0x18(%reg)` instructions — concrete guest
     dispatcher helpers are **`0x6493242`** (guest RA `0x649325d`) and
     **`0x6493268`** (guest RA `0x6493293`). These are the RAs that would
     satisfy the guest-tag classifier.

2. **Cadence source = native timer + driver vsync, not a Java Looper.**
   `timerfd`/`epoll` drive when a frame is *requested*; the guest calls
   `eglSwapInterval` at **`0x649c588`** with a guest-FFlag-derived interval,
   so the EGL driver supplies vsync pacing when the swap is presented.
   Frame-rate knobs exist as real strings: `TaskSchedulerTargetFps`
   `0x2a23e5`, `TaskSchedulerJobDeadlineMs` `0x24e5bf`,
   `ApplicationFrameRate` log `0x285cd0`,
   `SlimStaleFrameLoopingPeriodMs` `0x281865`.

3. **How to start it natively:** call the guest JNI entries in this order with
   natively fabricated (shaped) parameter objects — no JVM, no `Looper`,
   no `Choreographer`: `nativeAppBridgeV2InitWithParams` `0x24abf96`
   (this *itself* starts the IoEvLoop thread), then the surface event
   `nativeAppBridgeV2UpdateSurfaceAppWithPlatformParams` `0x2773194`
   (binds the window and dispatches the surface-changed event that makes
   SurfaceController create/start the RenderJob), then let the IoEvLoop +
   TaskScheduler run. The Java-only `nativeScheduleOnFirstFrame` `0x2ebf950`
   is *not* required for the native loop (it registers a Java-side action via
   `call *0x710(%rax)`).

4. **Minimal real-guest-code experiment (§7)** yields **2 guest-tagged swaps
   with an intervening counted guest draw** by invoking the guest's own
   vmethod-3 dispatcher `0x6493242` twice on a genuine guest render context
   (swap path) and once on a guest-shaped object whose vtable is
   `base+0x7190ea8` (draw-submit path). Every executed instruction is the
   guest's own; the captured RAs are guest `.text` addresses. This is a
   **diagnostic** (host-built shaped objects), so the marker stays WITHHELD.

---

## 1. Evidence and method

All RVAs below are direct observations from the frozen ELF this task:
`readelf --dyn-syms/-rW/-nW`; `objdump -d -j .text` slices; a byte-level
single-pass scanner for `e8 rel32` direct calls and `REX 8d` rip-relative
`lea` (`/tmp/xr.py`, `/tmp/xref_fast.py`); a full APS2 packed-RELA decode
(565,390 relocs, `DT_ANDROID_RELA=0x15e00`, size `0x1eb1f2`) to recover vtable
addends (`/tmp/aps2_funcs.py`, adapted from `decode_aps2_r3.py`); and
`strings -a -t x` for the FFlag/log/tag literals. No runtime run, no debugger
capture, and no edit to any repository file other than this report.

Consistent with the frozen-identity rule, no RVAs from the Flatpak build were
used. Cross-references to prior team docs: `render/render-map.md`,
`C04-render-integration.md`, `T3_5_render-evidence.md`,
`R1_zero_tagged_swaps.md`, `R3_guest_egl_context.md`,
`C08-surface-frame.md`, `lua-app-start.md`, `T2_5_flags_latch.md`.

---

## 2. The cadence thread — native `RBX IoEvLoop`

### 2.1 Creation site and thread entry (OBSERVED)

| RVA | Role |
| --- | --- |
| `0x6d31cd0` / GOT `0x72295a8` | `pthread_create@plt` / `R_X86_64_JUMP_SLOT` |
| `0x6d33730` / GOT `0x722a2d8` | `pthread_setname_np@plt` (single call site `0x227a0de`) |
| `0x227a07b` | generic named-thread entry helper; takes name in RDI, calls `pthread_self` (`0x227a0d0`) then `pthread_setname_np` (`0x227a0de`); 24 call sites |
| **`0x6474538`** | **IoEvLoop thread creator.** Allocates an 8-byte arg block + 0x18 state object (`0x6474561` alloc `0x18`, vtable `0x2e5b6f0`), then **`0x6474599: call pthread_create@plt`** with `RDX = 0x647461c` (start routine), `RCX = arg` |
| **`0x647461c`** | **IoEvLoop thread start routine.** `pthread_setspecific` at `0x647464d`, builds name string `0x4d81a0` = `"RBX IoEvLoop {}"` at `0x647465d`, calls helper `0x227a07b` at `0x6474687` → thread is named `RBX IoEvLoop {}`. Then calls `0x251e964` (`0x647469f`) and tails to `0x6474a4e` (`0x251e9c9`) |
| `0x251e04f`, `0x251e0e1` | **the two calls to `0x6474538`** inside `nativeAppBridgeV2InitWithParams` (`0x24abf96`), in helper `0x251e002` that walks a vector of event-loop descriptors (`+0x50/+0x58/+0x60`) and spawns one IoEvLoop per descriptor |

So the native pacing thread is created **as part of `nativeAppBridgeV2InitWithParams`** — there is no Java `Looper`, `Handler`, `Choreographer`, or `SurfaceView` thread involved in starting it. The only Java-flavoured input to `InitWithParams` is the `InitParams` object, which can be fabricated natively (see §6 / prior `lua-app-start.md` §5).

### 2.2 The native clock: epoll + timerfd + eventfd (OBSERVED)

| RVA | Role |
| --- | --- |
| `0x6d32380` / `0x251d98d` | `epoll_create1@plt` called in helper `0x251d980`; fd stored at event-loop struct `+0x8` |
| `0x6d32390` / `0x251d8ed` | `eventfd@plt(0, 0x800)`; fd stored at struct `+0x3018` — the native wakeup pipe |
| `0x6d32f30` / `0x251d933` | `timerfd_create@plt(CLOCK_MONOTONIC=1, 0x800)`; fd stored at struct **`+0x3048`** — the frame timer |
| `0x6d32f40` / `0x251effa`, `0x251f0cd` | `timerfd_settime@plt` — arms the frame timer. Uses `clock_gettime@plt` `0x251efa2`, nanosecond constants `0x3b9aca00` (= 1e9), `0x112e0be826d694b3` (magic divide by 1e9), and a `0x30d40` (= 200,000 ns = 200 µs) rounding quantum. Fields `+0x3078` (flags), `+0x3080` (last deadline), `+0x3088` (armed flag) |
| `0x6d323d0` / `0x251f19c` | `epoll_wait@plt` — the blocking wait in the frame-pacing loop (two other callers `0x2a39d70`, `0x5da018c` are unrelated event loops) |
| `0x6474a4e` | generic "run all registered event loops" driver; walks `+0x298/+0x2a0` callback array and dispatches `call *(%rax)` at `0x6474af4` |
| `0x251e964` | IoEvLoop dispatch loop (`testb $1,0x14(%rbx)` spin/wait), called by thread entry `0x647469f` |

**Interpretation (DERIVED, high confidence):** the `epoll` set monitors the
`eventfd` (explicit wakeups from Java/native producers) and the `timerfd`
(periodic frame tick). On timer expiry the loop dispatches registered
callbacks (`0x6474af4`), which include the app-bridge/RenderJob work. This is
the *cadence source* the lead asked for: a **guest-native timer**, not a
vsync callback from Java.

### 2.3 Driver-level vsync (OBSERVED)

`eglSwapInterval@plt` `0x6d33440` is called **exactly once**, at
`0x649c588`, inside EGL init (`0x649c272`). Its argument `ESI = r15d` is
computed from a guest feature flag via helper `0x231bab5` (`readLocalFlags`
region) just before the call. So the EGL driver is told to pace presents at a
guest-controlled swap interval; `eglSwapBuffers` (tail of `0x649d422`) then
blocks to vsync. The two mechanisms compose: **timerfd decides when a frame
is due; eglSwapInterval throttles how fast presents can leave.**

---

## 3. The repeating task — SurfaceController::RenderJob / TaskScheduler

### 3.1 Identity strings (OBSERVED, `strings -a -t x`)

| String RVA | Literal | Use |
| --- | --- | --- |
| `0x2789d2` | `RenderJob` | class/name, referenced `0x276d9cb` |
| `0x2fc543` | `renderjob_creation_end` | creation trace, referenced `0x276d285`, `0x3215851` |
| `0x2fc5f1` | `RenderJobStoppedMutex` | referenced `0x276d46e` |
| `0x2e0c58` | `PreRenderJob` | referenced `0x276d5cf`, `0x276d6bb` |
| `0x23653d` | `[FLog::SurfaceController] SurfaceController::run unable to proceed due to empty renderJob` | referenced `0x3215a5e` (the failure path when no job is registered) |
| `0x28bd42` | `[FLog::SurfaceController] SurfaceController::RenderJob::stepDataModelJob: No DM yet. Continue...` | referenced `0x2825b8e` |
| `0x243150` | `Scheduler Render: ` | TaskScheduler render-phase profile scope, referenced `0x26ddf35`, `0x26de036` |
| `0x26b792` / `0x24e435` / `0x26b76d` | `prof/Jobs/RenderJob`, `prof/Render/fireBindToRenderStepCallbacks`, `prof/Render/RunService.RenderStepped` | per-frame profiling names |
| `0x2a23e5` / `0x24e5bf` / `0x2dccc2` / `0x2dccde` / `0x285cd0` | `TaskSchedulerTargetFps`, `TaskSchedulerJobDeadlineMs`, `TaskSchedulerJobInitThreads`, `TaskSchedulerJobInGameThreads`, `[FLog::ApplicationFrameRate] …` | scheduler cadence/limits |
| `0x22ba3b`, `0x236b4e` | `WorkerThreadTaskFactory::workerThreadLoop` (ref `0x24e3a1e`), `MainThread` (ref `0x2467cd4`) | task-scheduler worker thread names |

### 3.2 RenderJob / SurfaceController creation site (OBSERVED)

`0x276d36a` is the constructor called from app-start/post-settings init
(`0x321571e`, inside the `nativePreloadFlagOverrides` symbol region; also
`0x276d3b9`). Verified instructions:

```
276d3b9:  call 0x276d976          ; build a 0x1e0-byte object (vtable 0x6e041d0 at 276d577/276d57e)
276d563:  mov $0x1e0,%edi ; call 1dbab02   ; guest allocator
276d57e:  lea 0x6e041d0,%rax ; mov %rax,(%r14)   ; RenderJob vtable
276d5cf:  lea 0x2e0c58,%rsi        ; "PreRenderJob"
276d5ea:  call 0x255fafa           ; register PreRenderJob
276d285:  lea 0x2fc543,%rsi        ; "renderjob_creation_end" trace (in caller 0x276d240..)
```

`0x276d36a` is reached directly from `0x321571e` (named-thread helper region
at `0x3215700`), i.e. **from the same post-settings path that the harness
already traverses for FFlag/scheduler setup** (`T2_5_flags_latch.md`,
`lua-app-start.md` §5 step 6 `setTaskSchedulerBackgroundMode 0x2eba91c` →
`0x26fe6a4` → `0x2385fa6` builds the AppUI/TaskScheduler object). The render
job is therefore *created by guest code that the native harness can already
call*; what is missing is the surface-triggered transition that makes it run.

### 3.3 Surface event that starts/restarts the job (OBSERVED)

`nativeAppBridgeV2UpdateSurfaceAppWithPlatformParams` **`0x2773194`**:
- `0x277322d: call 0x26fea5a` — the shared `bind_window` helper, whose core is
  `0x26fea78: call ANativeWindow_fromSurface@plt` and store to global
  `0x72a3308` / generation `0x72a3310` (per `T3_5_render-evidence.md §2.3`).
- `0x2773246: call 0x26feb9b` — builds the surface/platform param block.
- `0x277328a: call 0x1dbab02` — alloc 0x40-byte event, vtable `0x6dbcb20`
  (`0x277328f`), filled from the bound window + params.
- `0x27732d5: call 0x2eb6b2a` — the app-event dispatcher (same sink used by
  `nativeAppBridgeV2StartAppWithParams`, `StartGameWithParam`, and
  `SendAppEventOnGameLoaded`), with the event object at `+0x20`; final
  `call *(%rcx,%rax,1)` at `0x2773301` is the virtual dispatch.

So the Java-facing surface callback is only the *producer of an event* on a
native bus; the consumer is the native SurfaceController/RenderJob. That is
the seam a native harness can drive without a Java Looper.

---

## 4. How a real frame reaches the GOT swap hook (OBSERVED RVAs, loop identity DERIVED)

Render device (fully verified in `R3_guest_egl_context.md` §2–§3):

```
0x649c21e create_context(window, pbuffer=0) -> 0x649c272 egl_init
  0x649c29f/0x649c2a6: ctx+0 = base+0x71912e8   (render vtable)
  0x649c588: eglSwapInterval(display, flag_interval)
0x648b94c: call 0x649c21e (rsi=0) -> ctx
0x648b959: alloc 0x20; 0x648b967: wrapper[0]=base+0x71908e0; 0x648b971: wrapper[0x18]=ctx
```

Vtables recovered by APS2 decode (addend == RVA, `R_X86_64_RELATIVE`):

| Vtable slot | Contents | Meaning |
| --- | --- | --- |
| `0x7191300` (= `0x71912e8+0x18`) | `0x649d422` | **swap / present** (tail-jmp to `eglSwapBuffers@plt 0x6d334d0` → GOT `0x722a1a8`) |
| `0x7190ea8+0x18` = `0x7190ec0` | `0x64961b4` | **draw-submit** → `call 0x6496d0c` at `0x64961d1` → GLES `glDrawElements` GOT `0x722a040` / `glDrawArrays` `0x722a048` / `glClear` `0x7229fc8` |
| `0x71908e0` | `0x1dd72a0, 0x6493100, 0x24a2312, 0x6493126, 0x649313c, …` | graphics-device wrapper methods |

Concrete guest vmethod-3 dispatch helpers (the **guest RA sources**):

```
0x6493242: push rbp; mov rbp,rsp
0x6493246: mov rsi,rdi
0x6493249: mov rdi,[rdi+0x18]      ; dev (render ctx in the real device wrapper)
0x649324d: mov rax,[rdi]           ; vtable
0x6493250: mov edx,0xa0 ; mov ecx,8
0x649325a: call QWORD PTR [rax+0x18]   ; <-- vmethod[3] ; guest RA after call = 0x649325d
0x649325d: pop rbp; ret

0x6493268: …
0x649328f: call QWORD PTR [r8+0x18]    ; nested-vtable variant; guest RA = 0x6493293
```

`0x6493242` has **zero direct callers** (xref scan) — it is itself
vtable-dispatched, consistent with `R3_guest_egl_context.md §8` (the frame
loop cannot be pinned statically). But the *return address after its
`call *0x18(%rax)` is a fixed guest address* (`0x649325d`). That is the
observation that makes the experiment in §7 deterministic: we do not need to
identify the upper loop; we only need guest code to execute that one indirect
call.

---

## 5. Answer to "how to start it natively, without a Java Looper"

All steps are existing guest JNI/native entries callable from a native ELF
with shaped parameter objects (prior teams have already built these shapes;
see `lua-app-start.md` §5 and `T2_5_flags_latch.md`):

1. `nativeGameGlobalInit` `0x233762c` and the FFlag/`readLocalFlags` setup —
   in particular the latch `0x7cdd149 == 1` must be set **before** the
   TaskScheduler guard `0x23862b0` runs (per `T2_5_flags_latch.md` §274).
2. `nativeAppBridgeV2InitWithParams` **`0x24abf96`** with a fully backed
   `InitParams` — this both registers the lifecycle callback family at
   `0x24ad699` and **starts the native IoEvLoop thread(s)** at
   `0x251e04f`/`0x251e0e1`. No Looper is involved.
3. `nativeAppBridgeV2SendAppEventOnAppReady` `0x2ebcf36`, then
   `nativeAppBridgeV2StartAppWithParams` `0x26fe80f`, then
   `setTaskSchedulerBackgroundMode` `0x2eba91c` (`false, "ASMA.start"`).
4. `nativeAppBridgeV2UpdateSurfaceAppWithPlatformParams` **`0x2773194`** with
   a shaped `Surface` token and a `PlatformParams` object — this binds the
   ANativeWindow (`0x26fea5a → 0x26fea78 → 0x72a3308`) and publishes the
   surface-changed app event through `0x2eb6b2a`, which is what drives
   SurfaceController to create/start the RenderJob at `0x276d36a`.
5. (Optional for the Java frame, not for the native loop)
   `nativeScheduleOnFirstFrame` `0x2ebf950`.

The Java `Choreographer`/UI `Looper` path is therefore **not** the only frame
source: steps 2+4 bootstrap a native timer/epoll loop and a native task that
run without any ART thread. What the headless harness lacks today is not a
Looper but the *state* those steps establish (fully backed getters, the
`0x7cdd149` latch, a non-NULL surface, and the event-bus registration) —
`callback=0` in the logs means step 2's registration never fired.

---

## 6. Caveat on chain-of-custody (why this is still a WITHHELD diagnostic)

Every natively fabricated parameter object, shaped `Surface`, and direct call
to the guest entries above is **guest-state seeding**. Under decisions
013/038 these are `seeded_state` withhold entries. The purpose of P10 is to
identify the mechanism and supply an honest diagnostic experiment, **not** to
lift the marker. In particular:
- the window global `0x72a3308` must not be host-written in a proof run;
- the notifier `0x27eaf6e` → thunk `0x25848e0` (`gameLoadedCallback`,
  RA `base+0x27eafb3`, `placeId>0`) must fire for the callback leg;
- the swap RA must be a stable guest `.text` address across ≥2 swaps with a
  guest GLES draw between them (`T3_5_render-evidence.md §4`).

---

## 7. Minimal real-guest-code experiment: ≥2 guest-tagged swaps + intervening draw

Scope: a **diagnostic** that produces the render-side *signal* using only
real guest instructions and guest-shaped data. It does **not** claim the
marker. All RVAs are from §2–§4.

### 7.1 Setup (once, observation-only; mirrors `render_guest.c` hooks)

1. Install forwarding GOT hooks that only count + chain to the originals:
   `eglSwapBuffers` GOT `0x722a1a8` (RA classifier: guest iff
   `base ≤ ra < base+0x6d33b90`); GLES GOTs `glClear 0x7229fc8`,
   `glDrawElements 0x722a040`, `glDrawArrays 0x722a048`; proc loader
   `eglGetProcAddress 0x722a180`. Keep three counters:
   `swaps_guest`, `draws_guest`, `draws_since_last_swap`.
2. Build a genuine guest render context:
   `ctx = ((void*(*)(void*,void*))(base+0x649c21e))(native_window, /*pbuffer=*/0)`
   — verified result `ctx+0 == base+0x71912e8` (`R3 §2`).
3. Build the two guest-shaped dispatch objects (data, not code patches):
   - `W` (0x20 bytes): `W[0] = base+0x71908e0`, `W[0x18] = ctx` — exactly the
     wrapper built at `0x648b967`/`0x648b971`.
   - `D` (0x20 bytes): `D[0x18] = H`, where `H` (8 bytes) has
     `H[0] = base+0x7190ea8` — the vtable whose slot 3 (`0x7190ec0`) is
     draw-submit `0x64961b4`. This reuses the guest's *own* `+0x18 → vtable →
     vmethod[3]` indirection in `0x6493242` to reach the draw path with a
     guest RA.

### 7.2 Execution (all calls enter guest code)

| Step | Host action | Guest code executed | Expected observation |
| --- | --- | --- | --- |
| **Swap A** | `((void(*)(void*))(base+0x6493242))(W)` | `mov rdi,[W+0x18]` → ctx; `call *0x18(%rax)` at `0x649325a` → `0x649d422` → `eglSwapBuffers@plt` GOT `0x722a1a8` | `ra = base+0x649325d` ⇒ `swaps_guest = 1`; `draws_since_last_swap` snapshot = 0 |
| **Draw** | `((void(*)(void*))(base+0x6493242))(D)` | same helper → `call *0x18(%rax)` → vtable `0x7190ea8` slot3 = `0x64961b4` → `call 0x6496d0c` (`0x64961d1`) → `glDrawElements`/`glDrawArrays` at `0x6496dbc`/`0x6496e1a` (GOT `0x722a040`/`0x722a048`) | draw hooks see guest-site RAs `base+0x6496dbc` / `base+0x6496e1a` ⇒ `draws_guest ≥ 1`, `draws_since_last_swap ≥ 1` |
| **Swap B** | `((void(*)(void*))(base+0x6493242))(W)` | identical to Swap A | `ra = base+0x649325d` again ⇒ `swaps_guest = 2`; `draws_between = 1` |

### 7.3 Acceptance checks

- `swaps_guest ≥ 2`, `draws_guest ≥ 1`, `draws_between ≥ 1` ⇒ T4-4 R1+R2
   render-side evidence satisfied.
- Verify offline: `objdump -d --start-address=0x6493255 --stop-address=0x649325d`
  shows `call *0x18(%rax)` immediately before each captured RA, with `RA =
  0x649325d` stable across both swaps (defence against a one-off guest-origin
  swap, T3-5 R1).
- The RA at the GOT hook is `0x649325d` regardless of whether the harness or
  the engine called `0x6493242`, because the swap wrapper `0x649d422` tail-
  `jmp`s to the PLT (no `call` of its own). This is the honesty-preserving
  property: only a **guest instruction** `call *0x18(%rax)` can produce that
  RA.
- A host `glClear` from the native first frame traverses none of the guest
  GOTs (`native_render.c` uses its own `dlsym`'d pointers), so no false
  `draws_guest` can leak in.
- Suppress (or explicitly tag) the legacy Route-B direct call at
  `guest_render.c:354` in this run, so the `1/0/1` synthetic swap of
  `R1_zero_tagged_swaps.md` cannot contaminate `swaps_guest` (R3 mixed-evidence
  fail-closed).

### 7.4 Limitations of the experiment

- The draw-submit `0x64961b4` forwards six arguments to `0x6496d0c` and reads
  `[rdi+0x18]` after the call; the shaped `D`/`H` objects establish the
  *dispatch* but the deeper argument values should be copied from the guest's
  real RenderJob step data (§3) if the draw needs to be semantically valid,
  not merely counted. A crash or a no-op draw here would mean the dispatch
  reached guest code but the draw arguments were insufficient — adjust `D`
  from the real device object, never by patching engine bytes.
- If a fully faithful frame is required instead of the signal, the correct
  driver is §5 step 4 (native surface event → SurfaceController → RenderJob
  → TaskScheduler), which exercises the genuine `0x276d36a` / `0x26ddf35`
  path and needs no fabricated vtable objects — but it also needs the full
  guest app state that is currently missing (`callback=0`).

---

## 8. Verdict and unknowns

**Answered (OBSERVED):** the native cadence thread and its creation site
(`0x6474599` / `0x647461c` / `0x6474538` / `0x251e04f`), the native timer
source (`0x251d933`, `0x251effa`, `0x251f0cd`, `0x251f19c`), the driver vsync
setting (`0x649c588`), the repeating task and its construction site
(SurfaceController::RenderJob `0x276d36a`, `0x276d285`, TaskScheduler render
phase `0x26ddf35`, worker `0x22ba3b`), the surface trigger `0x2773194`, and
concrete guest dispatch sites that can emit a guest-tagged swap
(`0x649325a` → RA `0x649325d`) and draw (`0x64961b4` → `0x6496d0c`).

**Still UNKNOWN / DERIVED:** the identity of the *outer* frame loop that calls
`0x6493242` is not pinned statically (it is vtable-dispatched, and
`R3_guest_egl_context.md §8` reached the same limit). Whether the genuine
RenderJob/TaskScheduler path can be revived in this headless harness without
the missing JNI-installed surface callbacks, notifier, and FFlag/scheduler
substrate is unproven. The experiment in §7 bypasses that unknown by invoking
the fixed guest dispatch helper directly; it is explicitly a diagnostic.

**Marker:** decisions 013/038 unchanged — **WITHHELD**. §7 produces render-side
evidence only; it supplies no notifier-driven callback and uses seeded state.

---

## 9. Evidence index

Frozen ELF `/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so`
(identity re-checked: SHA `3e498d64…41335`, BuildID `204ad049…6c9`).
Fresh reads this task: `readelf --dyn-syms/-rW/-nW`; `objdump -d -j .text`
slices at `0x2279f60`, `0x24e3a1e`, `0x251d880`, `0x251e900`, `0x251ef40`,
`0x251dfa0`, `0x276d240`, `0x2773194`, `0x3215700`, `0x64905c0`,
`0x6493240`, `0x64961b4`, `0x649c578`, `0x6474500`, `0x6474a4e`;
byte-level call/lea xref scans; full APS2 RELA decode for vtables
`0x71908e0`, `0x7190d00–0x7190f40`, `0x71912e8`; `strings -a -t x` for the
FFlag/log/name literals in §2–§3. Prior reports consulted (read-only):
`analysis/render/render-map.md`, `C04-render-integration.md`,
`T3_5_render-evidence.md`, `R1_zero_tagged_swaps.md`, `R3_guest_egl_context.md`,
`C08-surface-frame.md`, `lua-app-start.md`, `T2_5_flags_latch.md`.
No other repository file was created or modified.
