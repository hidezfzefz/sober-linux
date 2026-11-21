# R9 — Virtual/display surface path: can the guest render without a real Android `Surface`, and can our phase-3 EGL window become its actual render target?

Author: **R9** (group A: diagnosis). Scope: **this file only** — `analysis/R9_virtual_display_path.md`.
Analysis-only; no `src/`, `CMakeLists.txt`, test, gate or engine file was edited. Engine frozen.

> **Verdict (front-loaded).**
> 1. **Yes — the guest can create an EGL context and a window surface against a virtual/display surface with no real `android.view.Surface`.** The decisive evidence is the guest's *own* graphics-device init at `0x648b940` (`mov (%r14),%rdi` → ANativeWindow*, `xor %esi,%esi` → pbuffer=0, `call 0x649c21e`). `0x649c21e → 0x649c272` uses the supplied window directly (`ctx+0x18`), and `0x649d1c2` takes the window branch (`eglCreateWindowSurface`, PLT `0x6d33470`, GOT **`0x722a178`**) when `ctx+0x10 == 0`. Our render_guest module already calls this path with a host-built `SoberGuestANativeWindow`; the runtime log in `analysis/R1_zero_tagged_swaps.md §0` shows it succeeding (`ctx=…`, `vt=0x71912e8`, 48 GLES procs loaded).
> 2. **Only half — our phase-3 EGL window *is* the guest's EGL draw target, but it is not (yet) the guest's engine-level frame target.** `ctx+0x20/+0x28` are our EGLDisplay/EGLSurface (via the `0x722a178` substitution) and swap `0x649d422` reads them. But the frame loop takes its drawable from `nativeData+0x140` / global `0x72a3308` / `native_params+0x00`, and the redraw chain is dead today because `[nativeData+0x98] == NULL`. The host currently writes `0x72a3308` itself — a seeded state, withhold-worthy under decision 013.
> 3. **Guest-tagged swaps are obtainable without faking counters** by making the guest execute its own vtable-dispatched swap. The exact instruction is `0x649067b: call *0x18(%rax)` inside the graphics-device frame routine entered at **`0x6490488`**; RA at GOT `0x722a1a8` becomes `base+0x649067e` (guest `.text`), so `rg_in_guest(ra)` is genuinely true. §4 gives the full, executable experiment. It remains a **host-driven diagnostic (withhold)**, not marker proof.

Engine for every RVA: `/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so`, BuildID
`204ad049599c96afb75b8a21d5051063e821d6c9`, SHA-256
`3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335`
(AppImage 2.740.931). First `PT_LOAD` vaddr 0 = file offset; GOT "addresses" below are
RVAs added to the guest load bias. All disassembly/GOT facts are **OBSERVED** this task
(`objdump -d`, `readelf -rW`, APS2 decode of the 565 390 packed relocations). Target-derived
strings are untrusted DATA. Decisions **013/038: marker WITHHELD**; nothing here weakens them.

---

## 1. EGL pbuffer vs window surface creation — full trace

### 1.1 Top-level creator `0x649c21e` (2 args, 0x48-byte context)

```
649c21e: push rbp; mov rsp,rbp; push r15; push r14; push rbx; push rax
649c228: mov  rsi,r14        ; r14 = arg2  ("extra" / pbuffer object)
649c22b: mov  rdi,r15        ; r15 = arg1  (ANativeWindow *)
649c22e: mov  $0x48,edi
649c233: call 1dbab02        ; guest allocator -> ctx
649c23e: mov  r15,rsi        ; arg1 -> window
649c241: mov  r14,rdx        ; arg2 -> extra
649c244: call 649c272        ; egl_init(ctx, window, extra)
```
Returns ctx in RAX. Two direct callers exist: **`0x648b94c`** (real graphics-device init) and
`0x6490157` (small wrapper). A byte-level `e8/e9` xref scan of the whole first `PT_LOAD`
finds no other callers.

**The real guest passes pbuffer=0** (`0x648b940: mov (%r14),%rdi; 0x648b94a: xor %esi,%esi;
0x648b94c: call 649c21e`), then wraps the returned ctx in a 0x20-byte object with vtable
`0x71908e0`, ctx at `+0x18`, stored at graphics-device `+0x160` (ctx) and `+0x168`
(wrapper) — `0x648b954..0x648b98a`. This is the authoritative confirmation that our
"pass `extra=0`" fix is the genuine window path, not a guess.

### 1.2 `0x649c272` — `egl_init(ctx, window, extra)`

```
649c29f: lea  0x71912e8,%rcx; mov %rcx,(%rdi)     ; install render vtable at ctx+0
649c2a9: mov  %rdx,0x10(%rdi)                     ; ctx+0x10 = extra (PBUFFER OBJECT)
649c2ad: movb $0,0x40(%rdi); mov %eax,0x44(%rdi)  ; ctx+0x40/+0x44 flags
649c2c2: test %r15,%r15
649c2c5: jne  649c362                             ; extra!=0 -> SKIP window setup
        ; ---- extra == 0 : the real window path ----
649c2cf: mov  %r12,0x18(%rbx)                     ; ctx+0x18 = ANativeWindow*
649c2d6: call 6d32100 <ANativeWindow_acquire@plt>  ; GOT 0x72297c0
649c303: call 6d32120 <ANativeWindow_getWidth@plt> ; GOT 0x72297d0 -> width
649c30f: call 6d32110 <ANativeWindow_getHeight@plt>; GOT 0x72297c8 -> height
        ; (these three are skipped when extra!=0)
649c336: call 6d333d0 <eglGetDisplay@plt>          ; eglGetDisplay(0)
649c33d: mov  %rax,0x20(%rbx)                     ; ctx+0x20 = EGLDisplay
649c351: call 6d333f0 <eglInitialize@plt>
649c362: call 649cf72                             ; EGL config helper
649c38f: call 6d33400 <eglCreateContext@plt>
649c394: mov  %rax,0x30(%rbx)                     ; ctx+0x30 = EGLContext
649c3f1: call 649d1c2                             ; surface helper (§1.3)
649c3f6: mov  %rax,0x28(%rbx)                     ; ctx+0x28 = EGLSurface
649c408: call 6d33410 <eglMakeCurrent@plt>         ; GOT 0x722a148
649c42f: call 6d33420 <eglQuerySurface@plt>        ; 0x3057 -> ctx+0x38 (width)
649c445: call 6d33420 <eglQuerySurface@plt>        ; 0x3056 -> ctx+0x3c (height)
649c588: call 6d33440 <eglSwapInterval@plt>
```
`extra` is **not** a boolean: it is a pointer stored at `ctx+0x10`. `0x649c2c2` only uses
its null-ness to skip `acquire/getWidth/getHeight`.

### 1.3 `0x649d1c2` — the pbuffer/window branch

```
649d1c9: mov 0x10(%rdi),%rax   ; rax = ctx+0x10 (extra / pbuffer object)
649d1cd: test %rax,%rax
649d1d0: je   649d1df
  ; ---- pbuffer branch (ctx+0x10 != 0) ----
649d1d2: mov 0x20(%rax),%rdi   ; display = [pbuffer+0x20]
649d1d8: call 6d33460 <eglCreatePbufferSurface@plt>  ; GOT 0x722a170
649d1dd: jmp  649d1ee
  ; ---- window branch (ctx+0x10 == 0) : OUR PATH ----
649d1df: mov 0x18(%rdi),%rdx   ; native_window = ctx+0x18
649d1e3: mov 0x20(%rdi),%rdi   ; display       = ctx+0x20
649d1e7: xor  %ecx,%ecx
649d1e9: call 6d33470 <eglCreateWindowSurface@plt>  ; GOT 0x722a178
```

### 1.4 Config helper `0x649cf72` and the `extra=1` fault

```
649cf8f: mov 0x10(%rdi),%rax
649cf93: test %rax,%rax
649cf96: cmove %rdi,%rax        ; null -> use ctx itself
649cf9a: mov 0x20(%rax),%rbx    ; display = [rax+0x20]
```
With the old harness value `extra=1`, `rax=1` and `0x649cf9a` dereferences address `0x21` —
the documented Route-A fault. With `extra=0`, `rax=ctx` and `rbx=ctx+0x20` (a valid
EGLDisplay). **A/B-verifiable**: `extra=1` faults at `0x649cf9a`, `extra=0` does not.
`0x649d4df..0x649d4ea` repeats the same helper for the surface-recreate path (vtable `[8]`
= `0x649d466`, gated by `ctx+0x40`).

### 1.5 Render vtable (APS2-decoded, slots are OBSERVED addends)

`0x71912e8` (EGL ctx): `[0]=0x649d2bc` teardown · `[1]=0x649d356` · `[2]=0x649d37c` rebind ·
**`[3] @ 0x7191300 = 0x649d422` swap** · `[4]=0x1df06e0` · `[5]=0x649d432` window
release/acquire · `[6]=0x649d45e` · `[8]=0x649d466` surface recreate ·
`[0x78]=0x7191330` (post-teardown vtable `0x7191368` starts at `[0]=0x649d5e6`).
Outer wrapper vtable `0x71908e0` and device vtable `0x7190718` were decoded too; the
ctx is reached from the device at `+0x160`.

**Swap wrapper `0x649d422`** (tail-jmp, so RA at GOT `0x722a1a8` is the *caller's* return
site):
```
649d422: mov 0x20(%rdi),%rax   ; EGLDisplay
649d426: mov 0x28(%rdi),%rsi   ; EGLSurface
649d42a: mov %rax,%rdi
649d42d: jmp 6d334d0 <eglSwapBuffers@plt>
```

---

## 2. The `nativeData` fields the guest reads for display size / refresh

`nativeData` is the `jlong nativeData` passed as arg 3 (`%rdx`) to the four GameActivity
JNI→native thunks `0x2a20850/0x2a208c0/0x2a20a00/0x2a20a30`. All offsets below are OBSERVED
from fresh disassembly of those thunks.

| Offset | Field | Guest reads/writes (RVA) |
| --- | --- | --- |
| `+0x10` | `JNIEnv*` passed to `fromSurface` | `0x2a20875`, `0x2a20900` (`mov 0x10(%rbx),%rdi`) |
| `+0x88` | callback: surface created / recreated | load `0x2a2088d`, NULL-test `0x2a20897`, tail-`jmp` `0x2a208a6`; again `0x2a20945/4f/57` |
| `+0x90` | callback: **size changed (w,h)** | load `0x2a209b4`, NULL-test `0x2a209be`, `call` `0x2a209ce` with `(nativeData, window, width, height)` |
| `+0x98` | callback: **redraw / frame trigger** | load `0x2a20a11`, NULL-test `0x2a20a18`, tail-`jmp` `0x2a20a20`; **this is the missing link** |
| `+0xa0` | callback: surface destroyed | load `0x2a20a45`, NULL-test `0x2a20a4f`, `call` `0x2a20a54`; also `0x2a20921/33` |
| `+0x140` | current `ANativeWindow*` | fromSurface store `0x2a20881`/`0x2a20910`; release `0x2a2086b`/`0x2a208e5`/`0x2a209f6`; redraw read `0x2a20a05` |
| `+0x148` | cached **width** | `ANativeWindow_getWidth` at `0x2a20968` → `+0x148`; compared at `0x2a20998`; updated `0x2a209a8` |
| `+0x14c` | cached **height** | `ANativeWindow_getHeight` at `0x2a2097a` → `+0x14c`; compared `0x2a209a0`; updated `0x2a209ae` |

Display-size data therefore reaches the guest through **two independent channels**, both of
which our shim already satisfies with synthetic values:

1. **EGL-init channel** (§1.2): `ANativeWindow_getWidth/getHeight` at `0x649c303/0x649c30f`
   (GOTs `0x72297d0`/`0x72297c8`), taken only on the `extra==0` window path; and
   `eglQuerySurface 0x3057/0x3056` at `0x649c42f/0x649c445` into `ctx+0x38/+0x3c`.
2. **GameActivity lifecycle channel** (`0x2a208c0`): getWidth/getHeight at
   `0x2a20963/0x2a20975` and `0x2a20985/0x2a20993` into `nativeData+0x148/+0x14c`; the
   `+0x90` callback receives `(nativeData, window, w, h)` when the size changes.

Our compat struct is `struct ANativeWindow { unsigned refs; int32_t width, height; }`
(`src/compat/android_compat.c:490-513`): width at `+4`, height at `+8`, seeded from
`SOBER_WINDOW_WIDTH/SOBER_WINDOW_HEIGHT` (default 1280×720). `SoberGuestANativeWindow`
keeps the same first three members (`render_guest.c`), so getWidth/getHeight/acquire/release
work on it unchanged.

**Refresh** is *not* a `nativeData` field. It is pushed into the guest by JNI entry points:
- `Java_…NativeGLInterface_nativePassCurrentDisplayRefreshRate` = **`0x24783fb`**
  (`cvtss2sd` then tail into a sink at `0x2478404`), and
- `Java_…NativeGLInterface_nativePassSupportedRefreshRates` = **`0x24784d9`**
  (builds a `std::vector<double>` of rates through helpers `0x247872c`/`0x247886e`/`0x24788bc`).
- The EGL pacing knob is `eglSwapInterval` at `0x649c588`; its argument comes from flags
  (`0x77a4e78`, `0x77a4ea0`, `0x77a4ec8`) and a `glGetString(GL_VERSION)` ≥ ES 3 check.
- FFlag name string `max_display_refresh_rate_hz` at `0x22bc80`.

Neither refresh path reads the ANativeWindow. A virtual window needs to supply only
**width and height**; refresh is a separate Java-side input.

---

## 3. Can the phase-3 native EGL window be promoted to the guest's render target?

**Layer 1 — EGL target: YES, already achieved.** After `0x649c21e(window, 0)`:
`ctx+0x18` = our `SoberGuestANativeWindow`, `ctx+0x20/+0x28` = EGLDisplay/EGLSurface created
through PLT `0x6d33470` / GOT `0x722a178`, where our hook substitutes the real X11/Wayland
drawable. Guest swap `0x649d422` presents exactly that surface. All 17 EGL imports resolve
through our compat shim; no Java object is consulted. This is the "virtual/display surface"
answer: **real Android `Surface` is not required**.

**Layer 2 — engine frame target: only seeded, not guest-driven.**
- The engine's window is stored at global `0x72a3308` (generation `0x72a3310`) by the
  guest helper `0x26fea5a` (fromSurface `0x26fea78`, store `0x26feaa0`), called from
  `0x277322d` (UpdateSurface), `0x26fe8d2` (StartAppWithParams) and `0x2ebd5dd` (StartGame).
  It is embedded at `native_params+0x00` and passed through `0x2eb6b2a`.
- The GameActivity path uses `nativeData+0x140` and the `+0x88/+0x90/+0x98/+0xa0` callbacks.
- Today `render_guest.c:326-329` **host-writes** `0x72a3308` and the synthetic `nativeData`
  has `+0x88/+0x98/+0xa0 == NULL` (`render_guest.c:402-404`). `rg_bind_surface()` calls the
  four thunks (`life=1/1/1/1`) but the redraw thunk stops at `0x2a20a18` because `[+0x98]`
  is NULL. Result (R1 log): `from_surface=2`, `gles_loads=48`, `draw_wrapped=5`,
  **`draw_calls=0`, `swaps=1/0/1`** — one synthetic host swap, zero guest tags.

So the window is *nominally* the engine's target, but no guest frame has ever been observed
consuming it. Promoting it into a **guest-driven** render target requires the redraw chain in
§4; that chain is still a withhold seed, not decision-013 proof.

---

## 4. Concrete experiment: guest-tagged swaps with **no counter faking**

### 4.1 Why a naive call can never tag
A byte-level `e8/e9` xref scan of `[0,0x6d33b90)` finds **zero** direct calls/jmps to
`0x649d422`. Every real swap is an indirect `call *0x18(%rax)` through the EGL vtable
`0x71912e8` (slot `0x7191300`). Today's one swap comes from the host calling `0x649d422`
directly (`guest_render.c:354`), so the GOT `0x722a1a8` hook sees a host RA and correctly
tags it `synthetic`. Calling `sober_render_guest_swap_via_vtable()` from the host has the
same problem (its own comment says RA is host).

### 4.2 The actual guest dispatch instruction (new finding)
Fresh disassembly of the graphics-device frame/present routine entered at **`0x6490488`**
(`push rbp; mov rsp,rbp; …; sub $0x100,rsp; mov rdi,rbx`) contains:

```
649064e: mov 0x180(%rbx),%r14     ; extra-context list start
6490655: mov 0x188(%rbx),%r12     ; extra-context list end
6490661: mov 0x8(%r14),%rdi
6490665: mov (%rdi),%rax
6490668: call *0x18(%rax)         ; swap each extra context  (guest RA = 649066b)
6490671: mov 0x160(%rbx),%rdi     ; PRIMARY EGL ctx (set by real init at 0x648b97c)
6490678: mov (%rdi),%rax          ; vtable = 0x71912e8
649067b: call *0x18(%rax)         ; *** GUEST-DISPATCHED SWAP -> 0x649d422 ***
649067e: movb $0x0,0x260(%rbx)
```

Because `0x649d422` ends in a tail-jmp, the RA captured at GOT `0x722a1a8` is the
**return site `base+0x649067e`, which lies inside guest `.text`**. `rg_in_guest(ra)`
therefore becomes true on its own — no counter write, no guest byte patch, no host pointer
in a guest vtable.

### 4.3 Experiment E-GUESTDISPATCH (default-OFF seed → forced WITHHOLD)

Preconditions (all from the disassembly above; `rbx` = a zeroed, host-owned, ~0x400-byte
"graphics-device-shaped" object):

| Field | Value | Why |
| --- | --- | --- |
| `+0x160` | guest ctx returned by `0x649c21e(window, 0)` | `rdi` for the swap call |
| `+0x260` | 1 | `test r14b` at `0x6490649` must be non-zero to reach `0x6490671` |
| `+0x180`, `+0x188` | equal (e.g. both 0) | skip the extra-context loop (`je 0x6490671`) |
| `+0x22f` | 0 | `cmpb $0,0x22f` at `0x64904ab` → jump `0x649060a`, skip heavy init |
| `+0x288` | 1 | skip the `0x64905ef` GLES-init block (`jne 0x649060a`) |
| `+0x298` | 0 | `test rdi` at `0x6490618` → jump `0x6490649` |
| all else | 0 | tail after the swap touches `+0x150/+0xc0/+0xd0/+0xe8/+0x88/+0x8c` only |

Steps (all hooks already exist; no new code in guest memory):
1. Keep `SOBER_RENDER_GUEST=1` (default). Install the three GOT hooks as today:
   GOT `0x72294a8` (fromSurface), `0x722a1a8` (RA-tagging swap), `0x722a180`
   (getProcAddress) — `src/jni/render_guest.c:432-453`. Keep the `0x722a178`
   `eglCreateWindowSurface` substitution installed.
2. `sober_render_guest_create_context()` → guest `0x649c21e(s_anw, pbuffer=0)`; verify
   `ctx+0 == base+0x71912e8` and `[vtable+0x18] == base+0x649d422` (already done today).
3. Build the shaped object above; call guest **`base+0x6490488`** with `rdi = object`.
   The guest executes `0x649067b`, the RA-tagging hook logs an RA of
   `base+0x649067e`, and `s_swaps_guest` increments **because `rg_in_guest(ra)` is true** —
   the counter is observed, never assigned.
4. Confirm offline: `objdump -d` the five bytes before the captured RA and require a real
   `call *0x18(%rax)` through the EGL vtable (`0x649067b: ff 50 18`). Require a **stable**
   RA across ≥2 invocations (T3-5 R1 defence). Drive the call twice to get two tagged swaps.
5. Get the draw leg honestly: the guest GLES loader at `0x649c633…` already loaded the draw
   procs through the hooked GOT `0x722a180` (48 loads, 5 draw names wrapped). Cause the
   guest draw dispatcher `0x6496d0c` (`glDrawElements` `0x6496dbc`, `glDrawArrays`
   `0x6496e1a`, `glClear` `0x6494ae1`; imported GOTs `0x722a040/0x722a048/0x7229fc8`,
   extension table `0x77a4fe0/8`, `0x77a4fa0/8/b0`, `0x77a4f98`, `0x77a4f78`) to run
   between the two dispatch calls so a wrapped trampoline counter ticks. Never count the
   host `dlsym`'d `glClear` from `native_render.c:817-830`.
6. Suppress the host Route-B call at `guest_render.c:354` in this evidence run with an
   explicit `[ev-suppress]` line, so the only counted swaps are guest-tagged. Mixed
   synthetic+guest evidence must fail closed (`MIXED → WITHHELD`).

Expected counter transition from the current baseline `swaps=1/0/1, draws=0`:
**`swaps(total/guest/synth)=3/2/1, draws≥1`**, with both guest RAs = `base+0x649067e`.

**Honesty:** this calls a guest function from the host with a synthetic device object. It is
a *diagnostic observation of the guest's own dispatch path*, not a spontaneous app-shell
frame. Under decisions 013/038 it is a **withhold seed** (add an env knob to the
`seeded_state` OR-chain); the sole `app shell rendered` site and the genuine
notifier-driven `gameLoadedCallback` requirement (`0x27eaf6e → 0x25848e0`, class
`0x72a2f48`, method `0x72a2fc0`, `placeId>0`, RA `base+0x27eafb3`) are unchanged. The only
new signal is a correct `guest-tagged render verdict` diagnostic line.

### 4.4 Alternative (higher fidelity, more work): revive the redraw chain
Fill `[nativeData+0x88/+0x90/+0x98/+0xa0]` with **genuine guest callback pointers** taken
from the guest's own live nativeData (or from the site where the guest installs them), then
call `0x2a20a00` so `0x2a20a11/0x2a20a20` dispatches into the engine frame loop. The engine
would then reach `0x6490488 → 0x649067b` naturally and also exercise `nativeData+0x148/+0x14c`.
No host function pointer is placed in a guest vtable; this is the path a real Android frame
uses. It is still host-triggered (withhold), and the callback-installer RVA is not pinned by
this task.

---

## 5. RVA / GOT quick reference (all AppImage 204ad049)

| RVA / slot | Meaning |
| --- | --- |
| `0x649c21e` → `0x649c272` | create_context → egl_init; **arg2 (pbuffer) MUST be 0** |
| `0x648b940/0x648b94c` | real guest caller: `window=[…], esi=0, call 0x649c21e` |
| `0x649cf72` / fault `0x649cf9a` | EGL config helper / bogus pbuffer deref when extra≠0 |
| `0x649d1c2` | surface helper: `ctx+0x10!=0` → pbuffer; else → window surface |
| `0x6d33470` / GOT `0x722a178` | `eglCreateWindowSurface` PLT / JUMP_SLOT (our substitution) |
| `0x6d33460` / GOT `0x722a170` | `eglCreatePbufferSurface` PLT / JUMP_SLOT (avoided) |
| `0x6d33420` / GOT `0x722a150` | `eglQuerySurface` → width `ctx+0x38`, height `ctx+0x3c` |
| `0x6d32100/110/120` / GOT `0x72297c0/c8/d0` | `ANativeWindow_acquire/getHeight/getWidth` |
| `0x6d31ad0` / GOT `0x72294a8` | `ANativeWindow_fromSurface` (hook installed, hit 2×) |
| `0x6d31ac0` / GOT `0x72294a0` | `ANativeWindow_release` (hook these five slots together) |
| `0x649d422` / GOT `0x722a1a8` | swap wrapper (tail-jmp) / `eglSwapBuffers` observation point |
| `0x6d33480` / GOT `0x722a180` | `eglGetProcAddress` (GLES load + draw-wrap observation) |
| `0x71912e8`, `[3]@0x7191300` | EGL ctx vtable / swap `0x649d422` |
| `0x71908e0`, `0x7190718` | ctx wrapper vtable / graphics-device vtable (APS2-decoded) |
| **0x6490488** | **graphics-device frame/present routine (entry)** |
| **0x6490671/0x649067b/0x649067e** | **ctx load / `call *0x18(rax)` swap / guest RA return site** |
| `0x6496d0c` (+ `0x6496dbc/e1a`, `0x6494ae1`) | guest GLES draw dispatcher |
| `0x2a20850/0xc0/0xa00/0xa30` | nativeData surface created/changed/redraw/destroyed thunks |
| `nativeData+0x88/+0x90/+0x98/+0xa0/+0x140/+0x148/+0x14c` | callbacks / window / width / height (§2) |
| `0x72a3308`, `0x72a3310` | global ANativeWindow*, window generation |
| `0x26fea5a` (call `0x26fea78`, store `0x26feaa0`) | guest Surface→window bind helper |
| `0x24783fb`, `0x24784d9` | JNI current/supported refresh-rate sinks (not window fields) |

---

## 6. Risks, unknowns, honesty limits

- **UNKNOWN:** the exact app-shell frame loop that calls `0x6490488` under normal Android
  operation was not pinned (the indirect-call graph is large; 11 626 `call *0x18(%r..)`
  sites). The experiment invokes the guest's own dispatch directly instead — sufficient to
  prove the RA mechanism, insufficient to claim an app-shell frame.
- **RISK:** the tail of `0x6490488` after the swap calls guest helpers `0x6489d08` and
  `0x29f9520`; a minimally shaped device object may fault there. The swap at `0x649067b`
  executes *first*, so the tagged counter is already updated even if the tail needs a
  SIGSEGV handler or a fuller object for a clean return.
- **DERIVED (A/B-verifiable):** `extra=0` reaches the window path; confirmed against the
  guest's own init at `0x648b94a` and the runtime success in R1's log.
- **No runtime test was run by R9**; all counters quoted are from the existing R1 log.
  The experiment in §4 is a specification, not an observed green.
- Every action that seeds a window, a device object or a thunk is withhold-worthy under
  decision 013. **`guest-tagged=0` today is correct and must not be inflated.** Marker
  **WITHHELD**.

## 7. Evidence index

Fresh this task: `objdump -d` slices at `0x649c21e-0x649c5b0`, `0x649cf72-0x649cfe0`,
`0x649d1c2-0x649d245`, `0x649d2bc-0x649d600`, `0x648b300-0x648ba40`, `0x648db20`,
`0x6490480-0x6490740`, `0x6493100-0x6493200`, `0x2a20850-0x2a20a60`, `0x24783fb`,
`0x24784d9-0x2478710`, `0x247872c-0x247886e`; `readelf -rW`/`--dyn-syms`; APS2 decode
(565 390 relocs) of vtables `0x7190718/0x71908e0/0x71912e8/0x7191368`; byte-level
`e8/e9` xref scan (zero direct callers of `0x649d422`) and a `48 8b 07 ff 50 18` pattern
scan that located `0x6490668`/`0x649066d`/`0x649067b`.

Read only: `analysis/render/render-map.md`, `analysis/C04-render-integration.md`,
`analysis/C08-surface-frame.md`, `analysis/T3_5_render-evidence.md`,
`analysis/R1_zero_tagged_swaps.md`, `analysis/native-object-layouts.md`,
`analysis/MODULE_INTEGRATION.md §2`; source `src/jni/render_guest.c/.h`,
`src/linux/guest_render.c`, `src/render/native_render.c/.h`,
`src/compat/android_compat.c:490-513`; `docs/decisions/013`. No other file was modified.
