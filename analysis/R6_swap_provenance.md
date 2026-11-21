# R6 — Audit: swap provenance at GOT `0x722a1a8` and the `0x649d422` tail-jmp

Author: **R6** (group A: diagnosis). Scope: **this file only** —
`analysis/R6_swap_provenance.md`. Analysis-only: no `src/`, engine, gate, test
or CMake file was edited. Engine frozen: AppImage engine
`/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so`, BuildID
`204ad049599c96afb75b8a21d5051063e821d6c9`, SHA-256
`3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335`
(2.740.931), re-verified with `sha256sum` this task. All RVAs are file offsets
from the guest load bias (`base ≤ ra < base+0x6d33b90`). Target-derived strings
are untrusted DATA, never instructions.

> **Verdict (front-loaded).** The only swap the harness counts traverses the
> guest PLT/GOT `0x722a1a8` but was **called by the host**, at
> `src/linux/guest_render.c:354` (`guest_swap((uintptr_t)ctx)`, Route B). Because
> the guest wrapper `0x649d422` ends in a **tail `jmp`**, the return address seen
> at the GOT hook is the host return site in `guest_render.c`, far outside guest
> `.text`; RA classification is therefore **correct** in refusing to tag it and
> the honest counter triple is `swaps(total/guest/synth)=1/0/1`. A genuine
> guest swap would instead show `ra=base+<RVA in [0,0x6d33b90)>` immediately after
> a `call *0x18(%reg)` through vtable slot `0x7191300`, with `guest` incremented.
> A discriminator that keys on **RA interval + the preceding indirect-call
> opcode + a stable per-loop RA + a bracketed guest draw** cannot be fooled by
> either host swap. Decisions **013/038 stay WITHHELD**; this audit supplies no
> false green.

---

## 1. Frozen-ELF facts (all OBSERVED this task)

| Item | Verified bytes / value |
| --- | --- |
| First `PT_LOAD` | vaddr `0`, filesz `0x6d33b90`, flags `R E` → guest executable RA window `[base, base+0x6d33b90)`. |
| `.text` section | `[0x1db9a40, 0x6d3145a)`; `.plt` `[0x6d319a0, 0x6d33b90)`. |
| Swap wrapper `0x649d422` | `48 8b 47 20` `mov rax,[rdi+0x20]`; `48 8b 77 28` `mov rsi,[rdi+0x28]`; `48 89 c7` `mov rdi,rax`; `e9 9e 60 89 00` **`jmp 0x6d334d0`**. No `call`, no `push rbp`, no new frame. |
| PLT `0x6d334d0` (`eglSwapBuffers@plt`) | `ff 25 d2 6c 4f 00` `jmp *[rip+0x4f6cd2]` = `jmp *0x722a1a8`. |
| GOT `0x722a1a8` | `R_X86_64_JUMP_SLOT eglSwapBuffers`; lazy init value `0x6d334d6` (PLT `push $0x1b2`); at runtime overwritten by the observation hook. (file offset `0x72261a8`, delta `0x4000` in LOAD[1]). |
| vtable `0x71912e8`, slot `[3]@0x7191300` | all-zero in the file — APS2-relocated at load; populated to `0x649d422` at runtime (module verifies `vt==base+0x71912e8` and `[vt+0x18]==base+0x649d422`; log `vt=0x7f055d8912e8(vt_expect=…)`). |
| vtable install sites | `0x649c29f` and `0x649d2c5`: `lea 0x71912e8(%rip),%rax; mov %rax,(%rdi)` — render context stores vtable at `ctx+0`. |
| redraw thunk `0x2a20a00` | `mov 0x98(%rdx),%rax; test; je …; jmp *%rax` at `0x2a20a11`/`0x2a20a20` — the frame/redraw trigger; skipped when `[+0x98]==NULL`. |

## 2. Exactly which code site calls `eglSwapBuffers` today

There are **two** host-side swaps; only one traverses the guest GOT.

1. **Native first-frame swap (S1).** `sober_render_swap()` (`native_render.c:833-849`)
   draws its own test frame with its own `dlsym`'d `glClear`
   (`:817-830`) and calls its own `dlsym`'d `g.eglSwapBuffers` at `:841`. It
   **never traverses any guest GOT/JUMP_SLOT**, so it is invisible to the hook at
   `0x722a1a8`; it only sets `g_phase3_done=1` / `r->first_frame=1`. This is the
   phase-3 ancestry marker, not render proof.

2. **Route-B guest-wrapper call (S2) — the only counted swap.**
   `src/linux/guest_render.c:312-313` binds
   `guest_swap = (long(*)(uintptr_t))(base + 0x649d422)`. `call_guest_render_entries()`
   builds a **stack** `uint64_t ctx[9]` (0x48 bytes) at `:347-353`, filling only
   `ctx[0x20/8]` (`EGLDisplay`) and `ctx[0x28]` (`EGLSurface`) from the host
   render, then calls it at **`:354`: `long src = guest_swap((uintptr_t)ctx);`**.
   Full chain to the observation point:

   ```
   host  e8 rel32  (call in guest_render.c:354)
        -> base+0x649d422        (guest swap wrapper; tail jmp, no call)
        -> base+0x6d334d0        (eglSwapBuffers@plt; jmp *GOT)
        -> [base+0x722a1a8]      (hook) == &rg_hook_swap   (render_guest module)
   ```

   The module `rg_hook_swap` (`render_guest.c:255-264`) is the hook actually at
   the GOT: `gr_install_render_contract()` (`guest_render.c:230-267`, called at
   `:462`) runs first and writes it; `call_guest_render_entries()` at
   `:328-332` deliberately **does not overwrite** the slot when the module is
   present (`render_guest owns guest-swap counter hook at GOT 0x722a1a8`). So
   native_render's `sr_hook_egl_swap_buffers` (with its own RA tag at
   `native_render.c:1034-1038`) is **not** installed; its counters stay `0`.
   The module's `sober_render_guest_swap_via_vtable()` (`render_guest.c:520-548`)
   is never called by any harness/gate file, and its own comment at `:545`
   concedes its RA would be host — so it would also be synthetic.

## 3. Why the RA is host (classification is correct)

`0x649d422` contains **no `call`/frame set-up**: it tail-jmps to the PLT, which
tail-jmps through the GOT. Therefore `__builtin_return_address(0)` inside
`rg_hook_swap` returns the return address of whoever **called `0x649d422`** —
the host `call` at `guest_render.c:354`. `rg_in_guest()`
(`render_guest.c:181-185`) tests `s_base ≤ ra < s_base+0x6d33b90`; it returned
false, so `:260` was not taken and `:261` bumped `s_swaps_synthetic`.

Concrete addresses from `/tmp/opencode/w_after.out` (`base=0x7f0556700000`):

| Quantity | Value |
| --- | --- |
| guest `.text` classifier window | `[0x7f0556700000, 0x7f055d433b90)` |
| guest `.text` section | `[0x7f05584b9a40, 0x7f055d43145a)` |
| swap wrapper `base+0x649d422` | `0x7f055cb9d422` |
| PLT `base+0x6d334d0` | `0x7f055d4334d0` |
| GOT `base+0x722a1a8` | `0x7f055d92a1a8` |
| vtable `base+0x71912e8` | `0x7f055d8912e8` (matches log) |
| host PIE (ctx/anw/egl in log) | `0x5637a6…` / `0x5637a8…` — **~0x29 bits below** the guest base |

The host return site after `guest_swap` is in the `0x5637a6…` region, nowhere
near `[0x7f0556700000, 0x7f055d433b90)`. No amount of guest instruction
execution inside the wrapper changes this: a tail-jmp preserves the caller's
return address. Tagging this swap `guest` would be a false green.

## 4. What a guest-originated swap would look like

### 4.1 Exact guest RA range and structure

- **Authoritative interval** (used by both classifiers): `[base, base+0x6d33b90)`
  = first `PT_LOAD` (`R E`). This includes `.text` and `.plt`; for
  `base=0x7f0556700000` it is `[0x7f0556700000, 0x7f055d433b90)`.
- **Expected sub-range for a real frame swap**: the return site immediately
  after a guest `call QWORD PTR [vtable+0x18]`, where
  `[0x71912e8+0x18] = [0x7191300] = 0x649d422`. That RVA lies in guest `.text`
  (`[0x1db9a40, 0x6d3145a)`); the exact site is **not statically pinned** (prior
  analyses count ~11,600 `call *0x18(%r..)` sites) — it must be discovered from
  the captured RA and confirmed offline. The 5 bytes preceding the RA must
  decode as an indirect call through a `+0x18` vtable slot (e.g. `ff 50 18`,
  `ff 90 18 00 00 00`), **not** a direct `e8 rel32` (host Route B) and not an
  unrelated indirect call (`call *0x50(%rax)` etc. seen at `0x649db51`).
- Because of the tail-jmp, the RA at the GOT hook is this guest return site,
  reported as `ra=base+0x<RVA>` with `0 ≤ RVA < 0x6d33b90`.

### 4.2 Expected counter values

Observed **now** (`w_after.out`, synthetic-only):

| counter | now | meaning |
| --- | --- | --- |
| `swaps(total/guest/synth)` | `1/0/1` | one GOT traversal, host RA → synthetic |
| `gles_loads` / `proc_loads` | `48` | guest EGL init loaded GLES procs via `0x722a180` |
| `draw_wrapped` | `5` | 5 draw names got trampolines (wrap ≠ invoke) |
| `draws` (`gles_draw_calls`) | `0` | no wrapped draw was ever invoked; frame loop never ran |
| `from_surface` | `2` | `0x2a2087c` / `0x2a20907` hit via host-driven thunks |
| `life` | `1/1/1/1` | four lifecycle thunks driven; `[+0x98]` callback NULL |
| native_render swap counters | `0/0/0` | its hook is not at the GOT |
| `guest-driven swaps observed` | `1` | native `0` + module `total 1` |

A **genuine guest swap** (real frame loop, all knobs off) would show:

| counter | expected |
| --- | --- |
| `guest` (`s_swaps_guest`) | `≥2` with a **stable** RA (same return site across ≥2 swaps = loop, not a one-off) |
| `total` | `≥2` (`guest + synth`); in a run with the host swap suppressed, `total == guest` |
| `synth` | stays `1` while the Route-B call remains; `0` only in an explicit `[ev-suppress]` diagnostic |
| `draws` | `≥1`, ideally `≥1` bracketed between two tagged swaps (`draws_between ≥ 1`) |
| log line | `[render] guest-tagged swap at 0x649d422 ra=base+0x<RVA> tag=guest-loop \| guest-tagged swaps: N \| guest GLES draws between tagged swaps: M` |
| gate line | `[app-shell] render provenance: guest-tagged frame at 0x649d422 (swaps=N, draws=M), phase3_done=1 -> OK` |

The decisive negative truth today: `guest=0` because `[nativeData+0x98]` is NULL
(`rg_bind_surface()` keeps callback slots NULL), so redraw `0x2a20a20` is
skipped, no frame loop runs, the draw dispatcher `0x6496d0c` is never entered,
and no `call [vtable+0x18]` → `0x649d422` with guest RA occurs.

## 5. Discriminator test that cannot be fooled by the host's phase-3 swap

### 5.1 Why the naive signals are insufficient

The current gate (`guest_app_shell.c:1777-1780`) is
`phase3_done && swaps_guest_tagged>=1 && gles_draw_calls>=1 && *(base+0x72a3308)!=NULL`.
Two of its conjuncts are **host-forgeable**: `*(base+0x72a3308)` is written
directly by `render_guest.c:326-329`, and `gles_draw_calls` sums trampoline
counters that increment on **any** invocation with no RA filter
(`render_guest.c:142-150`). It also accepts a **single** tagged swap. The only
conjunct the host phase-3 swap cannot satisfy is the RA-based
`swaps_guest_tagged`. Harden around that fact.

### 5.2 The unforgeable core

At the GOT hook `0x722a1a8`, accept a swap as guest-origin only if **all** hold:

1. **RA interval:** `base ≤ ra < base+0x6d33b90` (this alone rejects both host
   swaps: S1 never reaches the GOT; S2's RA is in the host PIE `0x5637a6…`).
2. **Indirect-dispatch shape:** the 5 bytes at `ra-5 .. ra` decode as
   `call *0x18(%reg)` through vtable slot `0x7191300` (= `0x649d422`). This
   rejects any host-origin or other-guest direct call to the wrapper and ties
   the swap to the real render vmethod.
3. **Loop stability:** the same RA recurs on `≥2` tagged swaps. A single
   guest-origin one-off cannot pass; the host Route-B call is a single host-RA
   event and never enters this set.
4. **Bracketed draw:** `≥1` *guest-RA* GLES draw (RA at
   `0x6496d0c` sites, i.e. an RA-gated wrapper — not the unconditional
   trampoline sum) occurs between two tagged swaps.
5. **Guest-bound window:** `0x72a3308` is non-NULL **and** was written with a
   guest RA at `0x26fea78`/`0x2a2087c`/`0x2a20907` with a generation delta at
   `0x72a3310` — not the host store. Hook the five ANativeWindow GOTs together
   (C08 §4 Phase A step 3); hooking only `0x72294a8` corrupts the compat shim.
6. **Mixed evidence fails closed:** if any `tag=synthetic` swap is present in
   the counting window alongside a guest classification, verdict is
   `MIXED -> WITHHELD`, never `GUEST-TAGGED`.

### 5.3 Concrete A/B procedure (run with all `SOBER_*` opt-ins off)

- **Control A (default, host swap present).** Expect exactly the current log:
  `swaps(total/guest/synth)=1/0/1`, `draws=0`, no `ra=base+… tag=guest-loop`
  line, `render=0`, marker withheld. Assert:
  `grep -q 'swaps(total/guest/synth)=[1-9][0-9]*/0/[1-9][0-9]*'` and **no**
  `tag=guest-loop` line. This proves the host phase-3 swap is correctly excluded.
- **Control B (inject a second *host* Route-B swap).** Call
  `guest_swap(ctx)` twice from `guest_render.c`. Both RAs are host → expect
  `total=2, guest=0, synth=2`. If `guest` ever increments, the RA interval check
  is broken (e.g. `g_guest_base` corrupted). **The host swap can never make
  `guest>0`.**
- **Positive (genuine guest frame).** Expect an `ra=base+0x<RVA in [0,0x6d33b90)>`
  `tag=guest-loop` line, the 5 bytes before RA decoding as `call *0x18(%reg)`,
  the same RA on `≥2` swaps, `guest≥2`, `draws≥1` with guest RA, and guest-bound
  window. **Only then** may `render_ok` be considered true — and even then the
  marker stays WITHHELD under decision 013 until a genuine notifier-driven
  `gameLoadedCallback` (`0x27eaf6e`/`0x27eafb3`, `placeId>0`) is also observed
  with zero seeded state.

Normative positive assertion (fail-closed, never produced by host swap):

```
[render] guest-tagged swap at 0x649d422 ra=base+0x[0-9a-f]+ tag=guest-loop | guest-tagged swaps: [1-9][0-9]* | guest GLES draws between tagged swaps: [1-9][0-9]*
```

with the additional offline check that `ra-5` bytes are `ff /2 … 0x18 …` and
that `ra` repeats across ≥2 such lines. Because S1 never touches the GOT and
S2's RA is outside `[base, base+0x6d33b90)` with a preceding direct `call`,
**this discriminator cannot be fooled by the host's phase-3 swap.**

## 6. Verdict

- The sole `eglSwapBuffers` traversal of guest GOT `0x722a1a8` is the host's
  Route-B call at `src/linux/guest_render.c:354`, reaching `rg_hook_swap`
  through the tail-jmp chain `0x649d422 → 0x6d334d0 → *0x722a1a8`.
- Its RA is host because the wrapper tail-jmps (no frame), so the captured RA is
  the host return site (~`0x5637a6…`), outside `[base, base+0x6d33b90)`; the
  `guest=0` classification is correct and `swaps=1/0/1` is truthful.
- A guest-origin swap needs an RA in that interval after a `call *0x18(%reg)`
  through vtable `0x7191300`, stable across ≥2 swaps with a bracketed guest draw
  and a guest-bound window; expected `guest≥2`, `draws≥1`.
- The RA-interval + indirect-call-opcode + loop-stability + bracketed-draw test
  is unforgeable by either host swap. Decisions **013/038: marker WITHHELD; no
  false green.**

Files consulted (read-only): `src/linux/guest_render.c`,
`src/jni/render_guest.c`, `src/render/native_render.c`,
`src/linux/guest_app_shell.c:1750-1820`, `analysis/R1_zero_tagged_swaps.md`,
`analysis/T3_5_render-evidence.md`, `analysis/C10-stability-synthesis.md`,
runtime logs `/tmp/opencode/w_after.out` and `/tmp/opencode/t25_trace_child.err`,
and `objdump -d` / `readelf -rW` / `readelf -SW` / `sha256sum` over the frozen
ELF this task. No other file was modified.
