# R1 — Diagnosis: `swaps(total/guest/synth)=1/0/1` and `draws=0`

Author: **R1** (group A: diagnosis). Scope: **this file only** —
`analysis/R1_zero_tagged_swaps.md`. Analysis-only: no `src/`, engine, gate, test
or CMake file was touched. Engine frozen: BuildID
`204ad049599c96afb75b8a21d5051063e821d6c9`, SHA-256
`3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335`
(AppImage 2.740.931). All RVAs are file offsets from the guest load bias
(`base ≤ ra < base+0x6d33b90`). Target-derived strings are untrusted DATA.

> **Verdict (front-loaded): `swaps=1/0/1` and `draws=0` are the correct, honest
> result of the synthetic Route-B swap.** The one swap is real (guest
> instructions ran and traversed guest PLT), but its **return address is in the
> host**, so the guest-`.text` RA classifier correctly refused to tag it. No
> guest frame loop ran, so no guest GLES draw was issued. `guest_tagged=0` is
> accurate and must **not** be inflated. Decisions **013/038 stay WITHHELD**;
> this diagnosis supplies no false green.

---

## 0. The evidence being explained

Latest harness logs (`/tmp/opencode/w_after.out`, newest at 00:23, identical in
`w_f_on.out`, `w_f_off.out`, `w_latch2/3/4.out`, `w_final.out`, `w_v4.out`, and
`t25_trace_child.err`), lines 3672–3692:

```
[render] enabled=1 installed=1 rc=0 base=0x7f0556700000 ... ctx=0x5637a86f92b0
         vt=0x7f055d8912e8(vt_expect=0x7f055d8912e8) life=1/1/1/1
         from_surface=2 gles_loads=48 draw_wrapped=5 draw_calls=0
         swaps=0 guest=0 synthetic=0 env=SOBER_RENDER_GUEST=0
[render] render_guest counters: source=module from_surface=2 proc_loads=48
         draws=0 swaps(total/guest/synth)=0/0/0 lifecycle=1/1/1/1
[render] installed EGL window-surface hook into compat shim
[render] render_guest owns guest-swap counter hook at GOT 0x722a1a8
[render] guest call 0x649d422 swap(ctx=0x7ffca033fdb0, egl=..., surf=...) -> rc=0
         | guest-driven swaps observed: 1
[render] render_guest counters: source=module from_surface=2 proc_loads=48
         draws=0 swaps(total/guest/synth)=1/0/1 lifecycle=1/1/1/1
first frame rendered
[app-shell] default guest render path -> rc=0 (already installed=1, guest-tagged swaps=0)
[app-shell] marker withheld: diagnostic guest-state seed active (no app-shell frame proven)
[app-shell] marker withheld: callback=0 render=0 (...)
```

Notes on reading these lines:

- `env=SOBER_RENDER_GUEST=0` is the **format string** `env=%s=0` from
  `sober_render_guest_describe()` (`render_guest.c:629,652`), i.e. it names the
  kill switch; it does **not** mean the kill switch is set. `enabled=1
  installed=1` confirm the module is live and all three GOT hooks are installed.
- `guest-driven swaps observed: 1` is `gr_guest_swap_evidence()`
  (`guest_render.c:272-277`) = `sober_render_guest_swap_count(NULL)` (native
  counter, **0**) + module `swaps_total()` (**1**). It is not a guest-tagged
  count; the adjacent `1/0/1` triple is authoritative.
- The gate leg is now (`guest_app_shell.c:1777-1780`):
  `render_ok = phase3_done && swaps_guest_tagged>=1 && gles_draw_calls>=1 &&
  *(void**)(base+0x72a3308)!=NULL`. With tagged=0 and draws=0, `render_ok=0`,
  hence `render=0`. The window-global conjunct is satisfied only because the
  module **host-wrote** its synthetic window at `0x72a3308`
  (`render_guest.c:326-329`).

**Concrete RVAs referenced below**

| RVA / slot | Meaning |
| --- | --- |
| `0x649d422` | flat swap wrapper: `mov 0x20(%rdi),%rax; mov 0x28(%rdi),%rsi; mov %rax,%rdi; jmp 6d334d0` — **tail `jmp`, no `call`** |
| `0x6d334d0` / GOT `0x722a1a8` | `eglSwapBuffers@plt` / `R_X86_64_JUMP_SLOT` — the swap observation point |
| `0x722a180` | `eglGetProcAddress` GOT — the proc-load/draw-wrap observation point |
| `0x72294a8` / `0x722a178` | `ANativeWindow_fromSurface` / `eglCreateWindowSurface` GOTs |
| `0x72297c0/8/d0` | `ANativeWindow_acquire/getHeight/getWidth` GOTs |
| `0x649c21e` → `0x649c272` | create_context → egl_init; fault site `0x649cf9a` when pbuffer≠0 |
| `0x649c633..0x649d300` | guest GLES loader RA window (`eglGetProcAddress` call sites) |
| `0x6496d0c` | guest GLES draw dispatcher; `0x6496dbc` (glDrawElements), `0x6496dec`, `0x6496e1a` (glDrawArrays), `0x6494ae1` (glClear) |
| `0x7229fc8` / `0x722a040` / `0x722a048` | imported `glClear` / `glDrawElements` / `glDrawArrays` JUMP_SLOTs |
| `0x77a4fe0/8`, `0x77a4fa0/8/b0`, `0x77a4f98`, `0x77a4f78` | GLES extension-pointer table entries (draw/clear) |
| `0x71912e8`, `[3]@0x7191300 = 0x649d422` | render vtable / swap vmethod; real frame = `call QWORD PTR [vtable+0x18]` |
| `0x2a20850/0x2a208c0/0x2a20a00/0x2a20a30` | surface created/changed/redraw-needed/destroyed thunks; redraw loads `[obj+0x98]` at `0x2a20a11`, tail-jmps at `0x2a20a20` |
| `0x72a3308` / `0x72a3310` | global ANativeWindow / generation; guest bind helper `0x26fea5a`, fromSurface `0x26fea78`, store `0x26feaa0` |
| `0x6d33b90` | guest `.text` span used by `rg_in_guest()` |

---

## 1. Which RA classification failed

There are **two** RA classifiers in the render_guest module, and only one failed.

### 1.1 The one that failed: swap-origin tagging in `rg_hook_swap()`

`src/jni/render_guest.c:255-264`:

```c
static int rg_hook_swap(void *dpy, void *surf)
{
    uintptr_t ra = (uintptr_t)__builtin_return_address(0);  /* :257 */
    ...
    rg_bump(&s_swaps_total);                                 /* :259 -> 1 */
    if (rg_in_guest(ra)) rg_bump(&s_swaps_guest);            /* :260 -> NOT taken */
    else                 rg_bump(&s_swaps_synthetic);        /* :261 -> 1 */
    ...
}
```

`rg_in_guest()` (`render_guest.c:181-185`) is the strict interval test
`s_base ≤ ra < s_base + 0x6d33b90`. It returned **false**, because the captured
RA is the **host return address** of the direct call in
`src/linux/guest_render.c:354` (see §3). Because `0x649d422` ends in a tail
`jmp` to `eglSwapBuffers@plt` (`0x6d334d0`) which itself tail-jumps through GOT
`0x722a1a8`, the stack still holds the RA of whoever called `0x649d422` —
here the harness, not guest code. Therefore:

- `s_swaps_total` = 1 (hook reached once),
- `s_swaps_guest` = 0 (**the failed classification**),
- `s_swaps_synthetic` = 1.

This is exactly the "RA tagging" discriminator specified in
`analysis/C04-render-integration.md §3` item 1 and
`analysis/T3_5_render-evidence.md §3.1` (R1). A host-RA swap is by definition
synthetic; incrementing `s_swaps_guest` here would be a false green.

### 1.2 The one that succeeded: GLES-loader tagging in `rg_hook_get_proc()`

`render_guest.c:267-278` uses `rg_in_gles_loader(ra)` (`:187-192`, the narrower
interval `base+0x649c633 .. base+0x649d300`). It succeeded **48 times**:
`gles_loads=48 / proc_loads=48`. Five loaded names matched `rg_is_draw_name()`
(`render_guest.c:168-178`, `k_draw_names` at `:153-165`) and were wrapped by
`rg_wrap_draw()` → `draw_wrapped=5`. So guest EGL init genuinely loaded GLES
entry points through the hooked `eglGetProcAddress` GOT `0x722a180`.

**But `draw_calls=0`**: the trampoline counter at stub offset `0x12`
(`render_guest.c:142-150`, bumped only when the wrapped function is actually
invoked) was never incremented (§4).

---

## 2. Which GOT hook the swap traversed

**The render_guest module's `rg_hook_swap` at GOT `0x722a1a8` — not
native_render's `sr_hook_egl_swap_buffers`.**

Path: `call 0x649d422` (host) → guest wrapper tail-`jmp 0x6d334d0`
(`eglSwapBuffers@plt`) → `jmp *GOT[0x722a1a8]` → `rg_hook_swap()`.

Evidence and ordering:

1. `s_gr.present == 1` (the module is linked and `sober_render_guest_run` was
   resolved via `dlsym`, `guest_render.c:119-143`). `gr_install_render_contract()`
   (`guest_render.c:230-267`, called at `:462`) runs **before**
   `call_guest_render_entries()` (`:469`). `sober_render_guest_install()`
   (`render_guest.c:432-453`) writes `&rg_hook_swap` into `0x722a1a8`.
2. In `call_guest_render_entries()` the branch at `guest_render.c:328-332`
   deliberately **does not overwrite** the slot when the module is present:
   `render_guest owns guest-swap counter hook at GOT 0x722a1a8` (log line 3675).
   Therefore native_render's `sr_hook_egl_swap_buffers`
   (`native_render.c:1026-1043`, which has its own RA tag at `:1034-1038`) is
   **not** at the slot and `g_guest_swaps_tagged`/`g_guest_swaps_synth` in
   native_render stay 0.
3. The **native first-frame swap** is also not counted anywhere in the guest
   counters: `sober_render_swap()` calls its own `dlsym`'d
   `g.eglSwapBuffers` (`native_render.c:841`), which does **not** traverse any
   guest GOT. That is why the counters read `0/0/0` even though the log says
   `frames swapped: 1`.
4. `rg_hook_swap` forwards to the saved original resolver
   (`s_got_old[1]`, the genuine `eglSwapBuffers`), so the driver behaviour is
   unchanged — the hook is observation-only.

So the single counted swap is a genuine traversal of the guest PLT/GOT, but it
arrived from a host caller and is classified synthetic.

---

## 3. Was `0x649d422` called by the host or by the guest?

**By the host.** Unambiguously.

- `src/linux/guest_render.c:312-313` defines
  `guest_swap = (long (*)(uintptr_t))(base + 0x649d422u)`.
- `call_guest_render_entries()` builds a **stack** `uint64_t ctx[9]` (0x48
  bytes) at `:347-353`, filling only `ctx+0x20` (`EGLDisplay`) and `ctx+0x28`
  (`EGLSurface`) from the host render, then calls it directly at `:354`:
  `long src = guest_swap((uintptr_t)ctx);`. This is worker-1's **Route B**
  (`C04 §1.1`, `REVIEW_A4_render.md §1.2`).
- There is **no** `call [vtable+0x18]` in the picture: the guest render vtable
  `0x71912e8` / slot `0x7191300 = 0x649d422` is never dispatched by guest code
  in this run. The module's `sober_render_guest_swap_via_vtable()`
  (`render_guest.c:520-548`) exists but a whole-tree grep shows it is **never
  called** by any harness/gate file — and even if it were, its own comment at
  `:545` says RA would be host, i.e. still synthetic.
- Consequently the RA seen at GOT `0x722a1a8` is the return address in
  `guest_render.c` after `call guest_swap` — a host address. That is precisely
  why §1.1's `rg_in_guest(ra)` returned false.

The swap wrapper itself issues **no GLES draw**, creates/binds **no surface**,
and reads only `ctx+0x20/+0x28` — so a single `1/0/1` can never by itself
represent a composed frame (C04 §1.3, A4 §1.3).

---

## 4. Why `draws=0` even though `gles_loads=48` and `draw_wrapped=5`

These three counters measure different things:

- `proc_loads=48`: 48 `eglGetProcAddress` calls whose RA was in
  `base+0x649c633..0x649d300`. These occur during guest EGL device creation —
  `sober_render_guest_create_context()` calls the genuine `0x649c21e(window,
  pbuffer=0)` (`render_guest.c:486-518`, `:504`), which reaches `egl_init`
  `0x649c272` and loads the ~70 GLES extension pointers. The log confirms
  success: `ctx=0x...92b0 vt=0x7f055d8912e8(vt_expect=0x7f055d8912e8)`.
- `draw_wrapped=5`: of those loaded pointers, 5 names matched `k_draw_names`
  and got counting tail-call trampolines (`render_guest.c:110-140`). This is a
  *wrap* count, not an invocation count.
- `draw_calls=0`: `rg_draw_calls()` (`:142-150`) sums the per-trampoline
  counters, which are incremented only when guest code **calls** a wrapped
  draw function. Nothing called them.

Root cause — the frame loop is never entered:

1. `rg_bind_surface()` (`render_guest.c:289-331`) drives the four lifecycle
   thunks `0x2a20850 / 0x2a208c0 / 0x2a20a00 / 0x2a20a30`
   (`life=1/1/1/1`) with a calloc'd synthetic `nativeData` whose callback slots
   `+0x88 / +0x98 / +0xa0` are deliberately **NULL**
   (`:402-404`, and the header's evidence note at `render_guest.h:15-18`:
   the guest NULL-checks each at `0x2a20897`, `0x2a2092b`, `0x2a2094f`,
   `0x2a20a18`, `0x2a20a4f`).
2. In particular the redraw thunk `0x2a20a00` loads `[obj+0x98]` at `0x2a20a11`
   and tail-jmps it at `0x2a20a20` — the documented **frame/redraw trigger**
   (C04 §2.1, T3-5 §2.4). With `[+0x98]==NULL`, the guest skips it.
3. No redraw callback ⇒ no guest frame loop ⇒ the GLES draw dispatcher at
   `0x6496d0c` (sites `0x6496dbc`, `0x6496dec`, `0x6496e1a`, `0x6494ae1`) is
   never entered, and no `call [vtable+0x18]` → `0x649d422` occurs with a guest
   RA. The only swap remains the host's Route-B call from §3.
4. The draw sites that *would* register are: imported JUMP_SLOTs
   `0x7229fc8` (glClear), `0x722a040` (glDrawElements), `0x722a048`
   (glDrawArrays), and extension-table pointers `0x77a4fe0/8`, `0x77a4fa0/8/b0`,
   `0x77a4f98`, `0x77a4f78` (T3-5 §2.2). The native first frame uses its own
   `dlsym`'d `glClear` (`native_render.c:817-830`) and traverses none of these,
   so no false `draws` can come from our test frame.

`from_surface=2` is consistent with this picture: the fromSurface GOT hook
`0x72294a8` *is* installed and *was* hit twice — once by `0x2a2087c` (created)
and once by `0x2a20907` (changed) — i.e. the surface-bind path is exercised by
host-driven thunks but terminates at the NULL callback chain.

---

## 5. What would make `guest_tagged > 0`

Necessary and sufficient condition at the counter: `rg_hook_swap` must capture
an RA with `base ≤ ra < base+0x6d33b90`. Concretely, `0x649d422` must be
reached by a **guest** instruction sequence — the real frame dispatch
`call QWORD PTR [vtable+0x18]` with `[0x71912e8+0x18] = [0x7191300] = 0x649d422`
(C04 §2.3, T3-5 §2.1). Because the wrapper tail-jmps, the RA recorded at GOT
`0x722a1a8` is the guest return site immediately after that indirect call.

The prerequisite chain is already partly satisfied in this run:

| Link | State in the log | RVA / note |
| --- | --- | --- |
| Swap GOT hook installed | yes | `0x722a1a8`, `rg_hook_swap` (C04 §1.2) |
| fromSurface GOT hook installed + hit | yes, 2× | `0x72294a8`; guest call sites `0x26fea78`, `0x2a2087c`, `0x2a20907` |
| Window global published | yes, **by host store** | `0x72a3308` / gen `0x72a3310` (`render_guest.c:326-329`) |
| Guest render device / vtable | yes | `0x649c21e(pbuffer=0)` → vtable `0x71912e8` at `ctx+0` verified; `eglCreateWindowSurface` substitution at `0x722a178` |
| GLES procs loaded + 5 draws wrapped | yes, 48 / 5 | `0x722a180` hook; loader `0x649c633..0x649d300` |
| `[nativeData+0x98]` redraw callback | **NO — NULL** | thunk `0x2a20a00` → `[+0x98]` load `0x2a20a11`, jmp `0x2a20a20` skipped |
| Guest frame loop / `call [vtable+0x18]` | **NO** | exact site not statically pinned (~11,600 `call *0x18(%r..)` sites, C04 §2.3) |
| GLES draw actually issued | **NO** | `0x6496d0c` dispatcher never entered |
| Guest-RA swap at `0x722a1a8` | **NO** | `guest-tagged swaps=0` |

So the single missing behavioural link is the **redraw → frame-loop →
vtable-dispatched swap**. Making `[+0x98]` (and realistically `[+0x88]`/
`[+0xa0]`) point at genuine guest callbacks instead of NULL, and driving
`0x2a20a00` so the guest reaches `0x2a20a11`/`0x2a20a20`, would let the guest
frame loop run; a `call [vtable+0x18]` would then dispatch `0x649d422` with a
guest RA and `s_swaps_guest` would increment. The gate additionally requires
`gles_draw_calls≥1`, so a counted guest draw must occur in the same window.

**Honesty caveat (decision 013/038):** every action above seeds synthetic guest
state (shaped ANativeWindow, token Surface, host-driven thunks, host-written
`0x72a3308`). The log already shows the consequence:
`marker withheld: diagnostic guest-state seed active (no app-shell frame
proven)` (line 3719). Even if `guest_tagged` and `draws` became positive this
way, the marker would still be withheld; it cannot be used as proof without the
genuine notifier-driven callback (notifier `0x27eaf6e` → thunk `0x25848e0`,
class `0x72a2f48`, method `0x72a2fc0`, `placeId>0`, RA `base+0x27eafb3`) and all
knobs off. `guest_tagged=0` is **not** a bug to paper over — it is the truthful
answer for the current synthetic Route-B run.

---

## 6. Ranked fix list (for *guest-tagged render evidence*, not for the marker)

Ranked by directness to `guest_tagged>0` / `draws>0`, then by honesty risk.

1. **[Highest] Make the redraw callback chain non-NULL so a guest frame loop
   actually dispatches.** In `rg_bind_surface()` (`render_guest.c:289-331`) the
   `nativeData` callback slots `+0x88/+0x98/+0xa0` are NULL. Provide genuine
   guest callback pointers (from the guest's own lifecycle object) and drive
   `0x2a20a00` so `0x2a20a11`/`0x2a20a20` executes. A guest `call
   [vtable+0x18]` → `0x649d422` then yields an RA in
   `[base, base+0x6d33b90)` and `s_swaps_guest` increments. This is *the* change
   that flips `0/1` to `1/1` — and it is a **withheld seed**, not proof.
2. **[High] Get a counted guest GLES draw in the same window.** The gate needs
   `gles_draw_calls≥1`. Either route the guest draw dispatcher
   `0x6496d0c` through the pointers loaded via the already-hooked
   `eglGetProcAddress` (`0x722a180`; extension table `0x77a4fe0/8`,
   `0x77a4fa0/8/b0`, `0x77a4f98`, `0x77a4f78`), or add *forwarding*, guest-RA-
   gated wrappers over the imported draw JUMP_SLOTs `0x7229fc8`, `0x722a040`,
   `0x722a048`. Never count the host `dlsym`'d `glClear` from
   `native_render.c:817-830`. Target: `draws≥1` and ideally ≥1 draw bracketed by
   two tagged swaps (T3-5 R2).
3. **[High] Verify the window global is guest-bound, not host-written.** Today
   `0x72a3308` is set by `render_guest.c:326-329`, which trivially satisfies the
   gate's `window != NULL` conjunct with synthetic state. Require a guest RA at
   `0x26fea78`/`0x2a2087c`/`0x2a20907` for the store and a generation delta at
   `0x72a3310`, with `ANativeWindow_acquire/getWidth/getHeight`
   (`0x72297c0/8/d0`) hooked together with `fromSurface` `0x72294a8`
   (C08 §4 Phase A step 3: hooking only `fromSurface` corrupts the compat shim
   at `0x26fea9b`). This removes a false-green conjunct.
4. **[Medium] Pin and validate the frame-loop call site.** From the captured
   guest RA, `objdump` the 5 bytes before it and require an indirect
   `call *0x18(%r..)` through vtable `0x7191300`; require a **stable** RA across
   ≥2 tagged swaps (T3-5 R1 defence). Mixed synthetic+guest evidence fails
   closed (`MIXED → WITHHELD`).
5. **[Medium] Make the synthetic Route-B call unambiguous in evidence runs.**
   Gate the direct `guest_swap((uintptr_t)ctx)` at `guest_render.c:354`
   (`SOBER_RENDER_SUPPRESS_SYNTHETIC`-style) with an explicit `[ev-suppress]`
   line so `guest_tagged` is not contaminated by the one host swap; keep the
   legacy `guest-driven swaps observed: 1` line for first-frame ancestry. Any
   such knob enters the withhold chain.
6. **[Required for any marker, highest honesty weight] Close Leg C.** Pair the
   guest-tagged render with a genuine notifier-driven `gameLoadedCallback`
   (notifier `0x27eaf6e` → thunk `0x25848e0`, class `0x72a2f48`, method
   `0x72a2fc0`, `placeId>0`, RA pair `0x2584960`/`base+0x27eafb3`) under
   decision 013 with **zero** seeded state. The current run has `callback=0`
   and the seed withhold line already firing; no render-side change alone can
   lift the marker.

**Bottom line on the fix list:** items 1–5 are diagnostic observability of a
guest frame path, not proof. Under 013/038 the marker remains **WITHHELD** in
every one of them; success is a correct `guest-tagged render verdict` diagnostic
line, never `app shell rendered`.

---

## 7. Verdict

- `swaps(total/guest/synth)=1/0/1` is **correct**: one swap traversed guest GOT
  `0x722a1a8` via the module's `rg_hook_swap`; the `rg_in_guest(ra)` classifier
  failed because the sole caller was the **host** (`guest_render.c:354`, Route B
  with a stack `0x48`-byte context), so it was tagged synthetic.
- `draws=0` is **correct**: 48 GLES procs were loaded and 5 draw names wrapped
  via the `0x722a180` hook during guest EGL init, but the redraw thunk
  `0x2a20a00` had a NULL `[nativeData+0x98]` callback, so no guest frame loop,
  no GLES draw dispatcher at `0x6496d0c`, and no trampoline invocation.
- `guest_tagged=0` is the honest state and must be preserved; the gate's
  `render=0` and the seed-withhold line are truthful.
- **Decision 013/038: marker WITHHELD. No false green.**

Files consulted (read-only): `src/jni/render_guest.c/.h`,
`src/render/native_render.c`, `src/linux/guest_render.c`,
`src/linux/guest_app_shell.c:995-1030,1755-1800`,
`analysis/MODULE_INTEGRATION.md §2`,
`analysis/REVIEW_A4_render.md`, `analysis/C04-render-integration.md`,
`analysis/T3_5_render-evidence.md`, and latest harness logs
`/tmp/opencode/w_after.out` (plus identical `w_f_on/off`, `w_latch2/3/4`,
`w_final`, `w_v4`, `t25_trace_child.err`). No other file was modified.
