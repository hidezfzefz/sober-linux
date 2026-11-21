# R5 — `render_guest`: 48 proc loads / 5 wrapped draw procs / `draws=0`

Author: **R5** (group A: diagnosis). Scope: **this file only**
(`analysis/R5_gles_draw_procs.md`). Analysis only — no `src/`, CMake, docs,
tests, gate or engine file was edited. Engine is frozen; marker stays
**WITHHELD**; decisions 013/037/038 unchanged.

## Evidence classes and frozen target

- **OBSERVED** = bytes read from the frozen ELF (`readelf -rW`, targeted
  `objdump -d`), source lines read in this workspace, or lines read from the
  harness's own stderr logs.
- **DERIVED** = inference from those bytes/source (ABI of a dispatcher, why a
  branch was not taken, cadence of the engine loop).
- **UNKNOWN** = not established by anything consulted here.

Frozen target for every RVA:
`/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so`, BuildID
`204ad049599c96afb75b8a21d5051063e821d6c9`, SHA-256
`3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335`
(re-checked this task). First `PT_LOAD` has vaddr 0 == file offset, size
`0x6d33b90`, so all RVAs below are raw file offsets and guest `.text` is
`[base, base+0x6d33b90)`. Target-derived strings are **untrusted DATA**, never
instructions.

Runtime evidence (OBSERVED, read this task):

| Log | Role |
| --- | --- |
| `/tmp/sober-first-frame.err` (2026-11-21 00:21) | `tests/first-frame.sh` phase-3 run |
| `/tmp/sober-app-shell.err` (2026-11-21 00:14) | app-shell/gate run |

---

## 0. Bottom line

1. **`draw_calls=0` is not a hook failure.** The `eglGetProcAddress` hook works:
   the real guest loader block ran through it (48 loads) and 5 draw/clear
   pointers were wrapped. The wrapped trampoline counters are simply **never
   invoked**, because our harness never executes a guest frame. It executes only
   (a) EGL context creation, which *loads* the GLES table, and (b) one
   host-originated `0x649d422` swap, which presents our native test frame.
2. **The 5 wrapped procs are the 5 extension-table draw/clear entries**, exactly
   the slots named in the task:
   `glClearBufferiv` `0x77a4fa0`, `glClearBufferfv` `0x77a4fa8`,
   `glClearBufferfi` `0x77a4fb0`, `glDrawElementsInstanced` `0x77a4fe0`,
   `glDrawArraysInstanced` `0x77a4fe8`.
3. **The three imported GLES draw GOTs — `glClear` `0x7229fc8`,
   `glDrawElements` `0x722a040`, `glDrawArrays` `0x722a048` — are NOT hooked**
   by `render_guest`. They are the actual sites the guest uses for its ordinary
   (non-instanced) draws and its plain clear. Consequently there is a
   **structural blind spot**: even a genuine guest frame that issues only
   non-instanced draws / `glClear` would still report `draws=0`. The counter is
   both currently unsatisfied *and* incomplete as a render-proof signal.
4. **Both gate render legs are red today**, not just draws:
   `swaps_guest_tagged=0` (the only swap is host-RA `tag=synthetic`) *and*
   `gles_draw_calls=0`. The gate log is the honest
   `marker withheld: callback=0 render=0`.
5. **A real guest GLES draw needs the Lua app frame / engine render loop**:
   `gameLoadedCallback` from notifier `0x27eaf6e`, live app state, a bound
   `ANativeWindow`, and a TaskScheduler-driven frame that reaches the draw
   dispatcher `0x6496d0c` (or the `glClearBuffer*` clear routine) and dispatches
   `call [vtable+0x18]` → `0x649d422`. None of that exists in the harness; the
   app-start chain is still missing (see `analysis/lua-app-start.md §1/§6`).
   Driving those RVAs directly from the harness is a **withhold diagnostic**,
   not proof.

---

## 1. The counting mechanism (source audit)

`src/jni/render_guest.c` (canonical module; linked into `build/guest-init-host`
via `src/jni/libsober_jni.a` — confirmed with `nm`), wired by
`src/linux/guest_render.c:230-266` and `src/linux/guest_app_shell.c:1009-1027`.

### 1.1 Three GOT hooks installed (`render_guest.c:432-453`)

| GOT JUMP_SLOT | Hook | Purpose |
| --- | --- | --- |
| `0x72294a8` `ANativeWindow_fromSurface` | `rg_hook_from_surface` (`render_guest.c:239`) | returns the host-built guest-shaped window (`s_anw`); counts `s_from_surface_calls` |
| `0x722a1a8` `eglSwapBuffers` | `rg_hook_swap` (`:255`) | captures `__builtin_return_address(0)`; guest-RA ⇒ `s_swaps_guest`, host-RA ⇒ `s_swaps_synthetic`; forwards to original |
| `0x722a180` `eglGetProcAddress` | `rg_hook_get_proc` (`:267`) | counts GLES loads and wraps draw/clear pointers |

No guest code bytes are patched; only these JUMP_SLOTs and the window global
`0x72a3308`/generation `0x72a3310` are written. `run()` =
`install()` → `bind_surface()` → `create_context()` (`:550-557`).

### 1.2 Load counting and wrapping (`render_guest.c:267-278`)

```c
static void *rg_hook_get_proc(const char *name) {
    uintptr_t ra = (uintptr_t)__builtin_return_address(0);
    rg_get_proc_fn orig = (rg_get_proc_fn)(uintptr_t)s_got_old[2];
    void *real = orig ? orig(name) : NULL;
    if (rg_in_gles_loader(ra)) rg_bump(&s_gles_proc_loads);
    if (real && rg_in_gles_loader(ra) && rg_is_draw_name(name))
        return rg_wrap_draw(name, real);
    return real;
}
```

- `rg_in_gles_loader(ra)` (`:187-192`) = RA inside
  `[s_base+0x649c633, s_base+0x649d300)`.
- `rg_is_draw_name()` (`:168-178`) is an **exact-match** list of 19 names
  (`k_draw_names`, `:153-165`): 14 `glDraw*`/`glMultiDraw*` variants and
  `glClear`, `glClearBufferfv/iv/uiv/fi`.
- `rg_wrap_draw()` (`:110-140`) allocates a 32-byte RWX stub from a 16-slot
  page: `incq <counter@stub+0x12>; movabs %rax,<real>; jmp *%rax`. It bumps
  `s_gles_draw_wrapped` **at wrap time** and returns the stub. RSP and all
  argument registers arrive at the real function unchanged, so the stub is a
  faithful forwarding wrapper.
- `sober_render_guest_gles_draw_calls()` (`:597-600`) sums the per-stub
  counters (`rg_draw_calls()`, `:142-150`) — i.e. **actual invocations**, not
  wraps. This is the value the gate reads.

The related **fallback** in `src/render/native_render.c:1119-1136`
(`sr_hook_egl_get_proc_addr`) is different: it increments `g_gles_draw_calls`
at **wrap time** (`:1131`) and uses a loose substring match (`strstr(name,
"Draw"/"Clear")`, `:1084-1089`). It is not used when the canonical module is
linked (which it is in every harness we inspected), but it would report a
misleading nonzero "draws" for a run that never drew. Worth recording because a
future A/B with a different link could confuse the two.

### 1.3 The gate consumes two independent render legs

`src/linux/guest_app_shell.c:1777-1780`:

```c
int render_ok = sober_render_phase3_done()
             && sober_render_guest_swaps_guest_tagged() >= 1
             && sober_render_guest_gles_draw_calls() >= 1
             && *(void **)(base + 0x72a3308u) != NULL;
```

Phase-3 ancestry is satisfied (native frame), the window global is non-NULL
(our hook publishes it), but **`swaps_guest_tagged=0` and `draws=0`**, so
`render_ok=0`. Additionally `seeded_state` includes
`getenv("SOBER_RENDER_GUEST")` (`:1746`), so the run also prints
`marker withheld: diagnostic guest-state seed active`.

---

## 2. Runtime ground truth for `48 / 5 / 0`

Verbatim from `/tmp/sober-first-frame.err`:

```
[render] enabled=1 installed=1 rc=0 base=0x7fca56e38000 anw=0x5602dfed77f0 anw_refs=65537 w=1280 h=720 ctx=0x5602dfecb390 vt=0x7fca5dfc92e8(vt_expect=0x7fca5dfc92e8) got_from=0x72294a8 swap=0x722a1a8 getproc=0x722a180 life=1/1/1/1 from_surface=2 gles_loads=48 draw_wrapped=5 draw_calls=0 swaps=0 guest=0 synthetic=0 env=SOBER_RENDER_GUEST=0
[render] render_guest counters: source=module from_surface=2 proc_loads=48 draws=0 swaps(total/guest/synth)=0/0/0 lifecycle=1/1/1/1
[render] render_guest owns guest-swap counter hook at GOT 0x722a1a8
[render] guest call 0x649d422 swap(ctx=0x7ffd1af2e960, egl=0x5602de29b920, surf=0x5602de2c1450) -> rc=0 | guest-driven swaps observed: 1
[render] render_guest counters: source=module from_surface=2 proc_loads=48 draws=0 swaps(total/guest/synth)=1/0/1 lifecycle=1/1/1/1
```

From `/tmp/sober-app-shell.err`:

```
[app-shell] default guest render path -> rc=0 (already installed=1, guest-tagged swaps=0)
[app-shell] marker withheld: diagnostic guest-state seed active (no app-shell frame proven)
[app-shell] marker withheld: callback=0 render=0 (env calls=69 callbacks=2 last='cachedMethod@72ca1e0')
```

Interpretation:

| Counter | Value | Meaning |
| --- | --- | --- |
| `gles_loads` | **48** | The guest's `eglGetProcAddress` loader block executed 48 calls with RA in `[0x649c633,0x649d300)` — i.e. the GLES table was genuinely populated through our hook during `create_context()` |
| `draw_wrapped` | **5** | Five of those names were in `k_draw_names` and got a counting stub |
| `draw_calls` | **0** | All five stub counters are zero: **no guest code ever called any wrapped draw/clear pointer** |
| `from_surface` | 2 | `onSurfaceCreated`/`onSurfaceChanged` each reach `fromSurface`; redraw/destroy do not |
| `life` | 1/1/1/1 | The four lifecycle thunks were driven once each by the harness |
| `swaps` before direct call | 0/0/0 | No swap of any kind during install/bind/create_context |
| `swaps` after direct call | 1/0/1 | The single `0x649d422` call is correctly tagged **synthetic** (host RA), guest-tagged stays 0 |

So the hook is healthy and the **absence of draws is a harness-behaviour fact,
not a hook bug**.

---

## 3. Tracing the `eglGetProcAddress` block `0x649c633..0x649d300`

### 3.1 Static shape (OBSERVED, frozen ELF)

- The block is inline in the guest's EGL-init function; it starts at
  `0x649c633` (`call 6d33480 <eglGetProcAddress@plt>`, result stored to
  `0x77a4f08`) and ends at the last loader call `0x649d2a9`; `0x649d2bc` is a
  different function (vtable `0x71912e8` + `eglMakeCurrent`/`eglDestroyContext`).
- **70 static `eglGetProcAddress@plt` call sites** across **34 distinct table
  slots** `0x77a4f08 … 0x77a5068`. Each slot has a primary name plus up to two
  `EXT`/`OES` aliases; the loader guards each group with
  `cmpq $0x0,<slot>; jne <skip>` and falls back to an alias only when the first
  name resolves NULL. Hence the **48 observed calls are the subset actually
  executed** in our run (aliases short-circuit once the driver returns a
  pointer); the 70 figure is the static maximum.
- The table begins at `0x77a4ef0`; the five draw/clear slots used by the
  dispatcher and clear routine are those named in the task.

### 3.2 Exact name → loader call → table slot → guest call site mapping (OBSERVED)

Extracted from the `lea <name>,%rdi; call eglGetProcAddress@plt; mov %rax,<slot>`
triplets and cross-checked against the indirect call sites in `.text`:

| Name (loader-call RVA) | Table slot | Guest indirect call site(s) | In `k_draw_names`? |
| --- | --- | --- | --- |
| `glClearBufferiv` (`0x649c97f`) | **`0x77a4fa0`** | `0x6494aa3` | yes → wrapped |
| `glClearBufferfv` (`0x649c9b4`) | **`0x77a4fa8`** | `0x6494991`, `0x6494a79` | yes → wrapped |
| `glClearBufferfi` (`0x649c9e9`) | **`0x77a4fb0`** | `0x64949d2` | yes → wrapped |
| `glDrawElementsInstanced` (`0x649caf1`) | **`0x77a4fe0`** | `0x6496d75` | yes → wrapped |
| `glDrawArraysInstanced` (`0x649cb26`) | **`0x77a4fe8`** | `0x6496dec` | yes → wrapped |
| `glBlitFramebuffer` (`0x649c8e1`) | `0x77a4f78` | `0x6494d1b`, `0x6494d53`, `0x6495920`, `0x649bb4c` | **no** (a blit, not a draw/clear) |

`glDrawElementsInstancedEXT` / `glDrawArraysInstancedEXT` /
`glClearBuffer*EXT` aliases resolve into the same slots; only the primary name
is normally loaded. **This accounts for exactly the 5 wrapped procs** — no
other one of the 48 loads matches `k_draw_names`. (A Ghidra `xrefs` query on
the relocated table slots returned partial/misaligned results for this
APS2-relocated data; the `objdump` bytes above are authoritative.)

### 3.3 The three imported GLES draw GOTs (OBSERVED)

`readelf -rW` and `objdump -d`:

| GLES call | PLT | GOT JUMP_SLOT | Sole guest call site | Hooked by `render_guest`? |
| --- | --- | --- | --- | --- |
| `glClear` | `0x6d33110` | **`0x7229fc8`** | `0x6494ae1` | **no** |
| `glDrawElements` | `0x6d33200` | **`0x722a040`** | `0x6496dbc` | **no** |
| `glDrawArrays` | `0x6d33210` | **`0x722a048`** | `0x6496e1a` | **no** |

These are imported symbols resolved by the dynamic linker at load time, not
obtained through `eglGetProcAddress`, so they never pass through
`rg_hook_get_proc`. `grep` over `src/` confirms no hook is installed over
`0x7229fc8`, `0x722a040`, `0x722a048` anywhere in our code.

### 3.4 The guest draw dispatcher `0x6496d0c` (OBSERVED bytes, DERIVED ABI)

```
6496d0c push rbp; ...
6496d1a mov %r9d,%r15d        ; r15 = 6th int arg = "instanced" flag
6496d1d mov %r8d,%r14d        ; r14 = 5th arg
6496d20 mov %edx,%r13d        ; r13 = 3rd arg
6496d23 mov %esi,%r12d        ; r12 = 2nd arg (table index at 0x214840)
6496d26 mov %rdi,%rbx         ; rbx = draw-descriptor object
6496d2b call 0x6496e52        ; derive GLenum-ish value, saved at [rbp-0x2c]
6496d33 movzwl 0x8e(%rbx),%eax; ax = descriptor+0x8e (index/type selector)
6496d40 mov 0x78(%rbx),%rdx   ; rdx = descriptor+0x78 (index buffer; NULL => arrays)
...
6496d75 call *0x77a4fe0       ; r9!=0 AND [rbx+0x78]!=NULL -> InstancedElements (WRAPPED)
6496dbc call glDrawElements@plt; r9==0 AND [rbx+0x78]!=NULL -> GOT 0x722a040 (NOT counted)
6496dec call *0x77a4fe8       ; r9!=0 AND [rbx+0x78]==NULL -> InstancedArrays  (WRAPPED)
6496e1a call glDrawArrays@plt ; r9==0 AND [rbx+0x78]==NULL -> GOT 0x722a048 (NOT counted)
```

**Consequence:** only the *instanced* branches enter our wrapped pointers.
A genuine frame that draws through the ordinary `glDrawElements` /
`glDrawArrays` PLT path is invisible to `draw_calls`. This is the structural
blind spot named in §0.3.

The clear routine (`0x64949xx`) is the other source of wrapped calls:

```
6494991 call *0x77a4fa8  ; glClearBufferfv — gated by [obj+0x18]+0x228 != 0, 4-bit mask
64949d2 call *0x77a4fb0  ; glClearBufferfi — depth/stencil mask bits 0x30
6494a79 call *0x77a4fa8  ; glClearBufferfv — alternate depth path
6494aa3 call *0x77a4fa0  ; glClearBufferiv — stencil bit 0x20
6494ae1 call glClear@plt ; GOT 0x7229fc8 (NOT counted)
```

---

## 4. Does the guest ever call these in our harness? — No

The harness sequence (`src/linux/guest_render.c`) is:

1. `gr_install_render_contract()` (`:230-266`) → module `run()`:
   - install the three GOT hooks, publish window at `0x72a3308`;
   - `bind_surface()` drives `0x2a20850` / `0x2a208c0` / `0x2a20a00` /
     `0x2a20a30` with a synthetic Surface token and **NULL** callback slots
     `+0x88/+0x98/+0xa0` (`render_guest.c:402-415`). These thunks call
     `ANativeWindow_fromSurface/getWidth/getHeight` (hence `from_surface=2`)
     but **no GLES draw**. The redraw thunk `0x2a20a00` tail-calls `[nd+0x98]`,
     which is NULL, so the chain stops before any render work.
   - `create_context()` calls genuine `0x649c21e(window, pbuffer=0)`; EGL init
     runs the loader block → 48 proc loads, 5 wraps. EGL setup itself issues no
     draw.
2. `call_guest_render_entries()` (`:310-362`) calls `0x649d422` **directly** with
   a host-built `0x48`-byte context (`display@+0x20`, `surface@+0x28`). This is a
   genuine guest *entry point*, but the RA at the swap GOT is in host code, so
   it is tagged `synthetic` (`swaps=1/0/1`). It invokes only
   `eglSwapBuffers` — no draw.
3. The synthetic first frame (`native_render.c:814-830`) calls its own
   `dlsym`'d `glClear`/`glClearColor`, **not** the guest GOTs
   `0x7229fc8/0x722a040/0x722a048`.

There is **no** invocation of `0x6496d0c`, of the `0x64949xx` clear routine, of
the extension-table pointers `0x77a4fe0/8/fa0/8/b0`, or of the three PLT draw
GOTs anywhere on this path. This is exactly why `draw_calls=0` after all 48
loads and 5 wraps. It also explains `swaps_guest_tagged=0`: there is no
`call [vtable+0x18]` from a guest frame loop — only the host's direct wrapper
call.

---

## 5. What triggers a real guest GLES draw

### 5.1 Guest-side chain (DERIVED from OBSERVED bytes + prior maps)

```
Lua/app frame (engine render thread, TaskScheduler / Heartbeat / RenderStepped cadence)
  └─ onSurfaceRedrawNeededNative 0x2a20a00
       └─ [nativeData+0x98] @0x2a20a11/0x2a20a20  (engine redraw callback, non-NULL in a real app)
            └─ engine render loop
                 ├─ draw dispatcher 0x6496d0c   -> 0x77a4fe0/8 (instanced) or 0x722a040/8 (plain)
                 ├─ clear routine 0x64949xx     -> 0x77a4fa0/8/b0 or 0x7229fc8
                 └─ call [render-vtable+0x18] (0x7191300) -> 0x649d422 -> GOT 0x722a1a8 (swap)
```

- `0x2a20a00` → `[nativeData+0x98]` is the redraw/frame trigger documented in
  `analysis/C04-render-integration.md §2.1/§4 E4`. Our harness deliberately
  leaves `[+0x98]` NULL, so even the redraw thunk we *do* call stops early.
- The frame loop's exact `call *0x18(%r..)` site cannot be pinned statically
  (~11,600 such sites; `analysis/C04-render-integration.md §2.3`;
  `analysis/T3_5_render-evidence.md §7`). It must be discovered by capturing the
  swap-hook RA in a run where a real frame occurs.
- **Cadence / thread:** in a real client the draws happen once per frame on the
  engine's render thread, paced by the TaskScheduler / Lua `RenderStepped`
  heartbeat and presented via `eglSwapBuffers`. In this native harness there is
  no Lua VM app, no `gameLoadedCallback` from notifier `0x27eaf6e`, no live
  `SingleSurfaceApp::userDidLogin` (`0x2771f6c`), and therefore no frame loop to
  tick. The only swap is the one-shot host call in `guest_render.c:354`.

### 5.2 Why the app path never starts here (OBSERVED in prior analysis)

`analysis/lua-app-start.md §1/§6` lists the missing default-path steps:
`nativePreloadFlagOverrides` + `nativeRetryInit` (flag `0x72cb648`),
FFlag-ctor replay ordering, `setTaskSchedulerBackgroundMode` `0x2eba91c`,
`nativeGameGlobalInit`, `SendAppEventOnAppReady`, `InitWithParams`,
`AppStart` `0x25449fc`/`0x24a8e44`, `StartAppWithParams` `0x26fe80f` with a
faithful `StartAppParams`, `StartLuaAppDM` `0x25448f6`, and
`UpdateSurfaceAppWithPlatformParams` `0x2773194`. Even with all opt-ins the
path only reaches a *rejected* `onAppBridgeNotification`; never
`gameLoadedCallback`. The gate log `callback=0` is consistent with that.

---

## 6. How to make `draws >= 1` through guest code

### 6.1 Genuine (the only path that could count as evidence)

Complete the app-start chain above so a Lua place actually loads and the
engine's render thread runs frames. Concretely, the run must reach:

1. guest-driven notifier `0x27eaf6e` → `gameLoadedCallback(J)V` thunk
   `0x25848e0` (class `0x72a2f48`, method `0x72a2fc0`, `placeId>0`,
   RA pair `0x2584960`/`base+0x27eafb3`) — that is the callback leg;
2. a real `ANativeWindow` bound at `0x72a3308` with generation `0x72a3310`
   advanced by the guest's own `0x26fea5a` path;
3. the engine frame loop executing `0x6496d0c` and/or `0x64949xx`. Since our
   counter covers only extension-table procs, at least one frame must issue an
   **instanced** draw (`r9d != 0` at `0x6496d0c`) or a `glClearBuffer*`
   clear with the flags that enable those branches;
4. the same loop dispatching `call [vtable+0x18]` → `0x649d422` so
   `swaps_guest_tagged >= 1` (two such swaps bracket a draw for a full frame
   interval).

All of this must happen with every `SOBER_*` opt-in unset; any knob that
supplies the window, drives a thunk, or wraps a substituting driver is seeded
state and force-withholds the marker (`guest_app_shell.c:1608-1766`). **Today
this is not achieved**, so this is a design target, not a claim.

### 6.2 Diagnostic (will make the counter nonzero, but must be withheld)

If the immediate goal is simply to observe `draw_calls >= 1` in a diagnostic
run, the minimum guest-code actions are:

1. **Direct dispatcher drive (guest code):** call `0x6496d0c` with a shaped
   descriptor whose `+0x78` is NULL (arrays path) or non-NULL (elements path),
   and set the 6th integer argument (`r9d`) **non-zero** so the branch at
   `0x6496d44` takes the `call *0x77a4fe8` / `call *0x77a4fe0` path. That enters
   our counting stub with the stub's RA in guest code and bumps `draw_calls`.
   The table at `0x214840` indexed by the 2nd argument supplies the first
   argument (`GLenum`-like), and `descriptor+0x8e` selects index type
   (`cmp $0x2`), so those fields need shaping too. Treat this as a new knob in
   the `seeded_state` OR-chain.
2. **Clear-path drive:** reach `0x6494991`/`0x64949d2`/`0x6494a79`/`0x6494aa3`
   with a render-state object whose flags (`[obj+0x18]+0x228`, bits
   `0x1/0x10/0x20/0x30` of the mask argument) enable the `glClearBuffer*`
   branches. Same withhold rule.
3. **Close the measurement blind spot:** also install forwarding counting
   wrappers over GOT `0x7229fc8`, `0x722a040`, `0x722a048` (the T3-5 design,
   `analysis/T3_5_render-evidence.md §2.2/§3.2`), so ordinary non-instanced
   draws and the plain clear are counted. Without this, a real frame that only
   uses the PLT path still reads `draws=0` even though draws happened.
4. **Redraw chain:** give the synthetic `nativeData` a real `[+0x98]` callback
   that reaches `0x6496d0c`, then call `0x2a20a00`; this exercises more of the
   genuine chain than a raw dispatcher call, but it is still harness-supplied
   guest state.

**Do not** "make draws=1" by calling the wrapped function pointer from host
code: the trampoline has no RA filter, so that would bump the counter while
proving nothing, and would be a false green. Likewise do not use the
`native_render.c` fallback counter (it increments at wrap time).

---

## 7. Honesty limits

- The numbers `48 / 5 / 0`, `swaps=1/0/1`, `callback=0 render=0` are OBSERVED
  from the named runtime logs; no new harness run was performed for this
  report. Engine SHA/BuildID were re-read and are unchanged.
- The exact reason 48 (rather than 70) loader calls executed is DERIVED from
  the guard/alias structure of the loader block and the host driver's
  `eglGetProcAddress` responses; the per-slot success pattern was not logged.
- Dispatcher ABI, clear-routine gating, and frame-loop/cadence claims are
  DERIVED from observed disassembly plus the cited prior analyses; the frame
  loop's indirect-call site remains UNKNOWN (discover via RA capture).
- No render evidence is promoted and no marker is emitted. `render_ok=0` and
  the WITHHELD marker are correct today. Decisions 013/037/038 are untouched.

## 8. Evidence index

- Frozen ELF: `readelf -rW` (GOT `0x7229fc8`, `0x722a040`, `0x722a048`,
  `0x722a180`, `0x722a1a8`, `0x72294a8`); `objdump -d` slices
  `0x649c630..0x649d310` (70 loader calls / 34 slots / 5 names),
  `0x6496d00..0x6496e60` (dispatcher), `0x6494960..0x6494b00` (clear).
  `sha256sum` / `readelf -nW` identity re-checked.
- Source: `src/jni/render_guest.c:110-178,239-278,350-469,486-600,620-654`;
  `src/jni/render_guest.h:139-181,248-265`;
  `src/linux/guest_render.c:119-143,230-266,310-362`;
  `src/linux/guest_app_shell.c:1009-1027,1608-1766,1777-1799`;
  `src/render/native_render.c:814-830,1051-1190`;
  `src/compat/gl_egl_forward.c` (`eglGetProcAddress` resolves to real driver).
- Runtime: `/tmp/sober-first-frame.err`, `/tmp/sober-app-shell.err` (quoted in
  §2). `nm build/guest-init-host | grep render_guest` (module linked in).
- Prior analysis: `analysis/T3_5_render-evidence.md §2.2/§3.2`,
  `analysis/C04-render-integration.md §2.1-§2.3/§4`,
  `analysis/lua-app-start.md §1/§6/§7`,
  `analysis/C03-appshell-frame-evidence.md §0/§1`,
  `analysis/MODULE_INTEGRATION.md §2`.
