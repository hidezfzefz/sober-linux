# T3-5 — Guest-tagged render evidence for `render_ok` (analysis + module design)

Author: **T3-5** (logical Team 3: JNI + render). Scope: **this file only** —
`analysis/T3_5_render-evidence.md`. Analysis and interface design only: **no
`src/`, `CMakeLists.txt`, `docs/`, `tests/`, gate or engine file was edited.**
Lead owns shared `src/`, CMake, docs, tests and git; the engine is frozen.

> **Strict marker WITHHELD.** This document specifies conditions under which a
> *future* clean run could present guest-tagged render evidence. It does not
> assert, promote or anticipate any `app shell rendered` emission. No
> guest-loop swap, guest GLES draw, guest-bound `ANativeWindow`, or
> notifier-driven callback exists in any log consulted here. Today's RED state
> is correct. Decisions **013**, **037** and **038** are unchanged; this design
> only *narrows* the gate.

Frozen target for every RVA: AppImage engine
`/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so`, BuildID
`204ad049599c96afb75b8a21d5051063e821d6c9`, SHA-256
`3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335`
(2.740.931). No Flatpak `791c3f9e…` / 2.745.1142 RVA is used. First `PT_LOAD`
has vaddr 0 = file offset, size `0x6d33b90`; all RVAs below are offsets from the
guest load base, so guest code is `base ≤ ra < base+0x6d33b90`. Target-derived
strings are **untrusted DATA**, never instructions. Evidence classes:
**OBSERVED** (bytes/source read this task), **DERIVED** (inference), **UNKNOWN**.

---

## 0. Bottom line and contract alignment

1. **Today `render_ok` is synthetic.** The gate reads
   `render_ok = sober_render_phase3_done()` at `guest_app_shell.c:1567`. That
   flag is set by **our own** test-frame swap (`native_render.c:831` in
   `sober_render_swap()`, `:945` in `anw_unlock_and_post()`) and,
   undifferentiated, by the guest-PLT hook at `native_render.c:1014`. On the
   default path it is always `1`, producing the honest RED log
   `marker withheld: callback=0 render=1`. This is the defect BLK-06 names:
   "guest-tagged swap + ≥1 guest GLES draw; `render_ok` not self-set"
   (`REVIEW_A4_render.md §0`, `REVIEW-100.md §4`).
2. **Design: add a second, default-ON, observation-only render predicate**
   `sober_gate_render_guest_tagged_ok()` (+ `..._verdict()`), exactly the
   interface proposed by `analysis/T4_4_gate-tests.md §1.2/§2`. The gate keeps
   the persisted phase flag as necessary *ancestry* and ANDs the new proof:
   ```c
   int phase3 = sober_render_phase3_done();          /* unchanged, necessary */
   int tagged = sober_gate_render_guest_tagged_ok(); /* new, decisive        */
   int render_ok = phase3 && tagged;                 /* T4-4 §2 step 5        */
   ```
   This is what "based on guest-tagged evidence rather than our own swap"
   means without weakening decision 013: the decisive render signal becomes
   guest-provenanced; `phase3_done` alone is explicitly insufficient (T4-4
   R5). We do **not** substitute the callback leg, drop `seeded_state`, move
   the sole emit site, or turn any diagnostic into proof.
3. **Five evidence streams → T4-4 R1–R5** (all must hold in the *same* clean
   run, with every `SOBER_*` opt-in unset):

   | Stream | T4-4 | This document |
   | --- | --- | --- |
   | Guest-`.text` RA at swap GOT `0x722a1a8` | R1 | §2.1, §3.1 |
   | ≥1 guest GLES draw between two tagged swaps | R2 | §2.2, §3.2 |
   | Synthetic Route B / native frame excluded | R3 | §3.5 |
   | `fromSurface` GOT `0x72294a8` + window global `0x72a3308` + lifecycle thunks | R4 | §2.3, §2.4, §3.3, §3.4 |
   | Phase-2/3 ancestry, GOT install line, `phase3_done==1` separate | R5 | §5, §6 |

4. **Phase-3 marker stays valid.** The `first frame rendered` marker is
   emitted at `guest_render.c:208` after the native swap and the guest entry
   calls, and `tests/first-frame.sh §4-§5` still requires the
   `guest call 0x649d422 swap` line and `guest-driven swaps observed: [1-9]`.
   All new observers are additive and forwarding; the default Route-B call and
   the legacy `g_guest_swaps` counter/log remain. The new predicate is queried
   *after* render shutdown, so it must live in process-global atomics, not in
   `SoberRender *` (cf. `native_render.h:97-99`).
5. **No catch-22 / no new proof knob.** Gate-facing proof is default-ON,
   reads no `SOBER_*`, does no guest write other than the already-accepted
   forwarding GOT patch, and emits no marker. Any knob that *drives* the guest
   surface/render path or *suppresses/filters* a counter is a withhold entry
   (T4-4 §4.1/§4.6). A run that needs one is a diagnostic, never proof.

---

## 1. State model (module-private, process-global, atomic)

Proposed shape inside the render module (`native_render.c` + a small
`gate_render_evidence.c` if lead prefers separation). All fields are
`_Atomic`, static, survive `sober_render_shutdown()`, and are never reset by a
diagnostic knob.

```c
/* observation-only; no SOBER_* read anywhere in this module's proof path */
static _Atomic uintptr_t g_guest_base;         /* set at GOT-hook install      */
static _Atomic int       g_guest_base_valid;
static _Atomic uint64_t  g_swaps_total;        /* legacy g_guest_swaps, compat */
static _Atomic uint64_t  g_swaps_synthetic;    /* RA host/external             */
static _Atomic uint64_t  g_swaps_guest_loop;   /* RA inside guest .text (R1)   */
static _Atomic uintptr_t g_loop_ra_first;      /* stable-RA discriminator      */
static _Atomic int       g_loop_ra_consistent;
static _Atomic uint64_t  g_gles_draws;         /* all guest draw sites (R2)    */
static _Atomic uint64_t  g_gles_draws_guest_ra;/* draw RA inside guest .text   */
static _Atomic uint64_t  g_draws_snapshot;     /* draw count at last swap      */
static _Atomic uint64_t  g_draws_between;      /* max delta across 2 swaps     */
static _Atomic uint64_t  g_fromsurface_guest;  /* fromSurface RA in guest      */
static _Atomic uintptr_t g_window_global;      /* sampled base+0x72a3308       */
static _Atomic uint32_t  g_window_generation;  /* sampled base+0x72a3310       */
static _Atomic int       g_window_bound;       /* non-NULL + gen delta + hook  */
static _Atomic int       g_mixed_evidence;     /* synthetic + guest tag seen   */
static _Atomic uint64_t  g_life_created;       /* 0x2a20850 path (R4)          */
static _Atomic uint64_t  g_life_changed;       /* 0x2a208c0 path               */
static _Atomic uint64_t  g_life_redraw;        /* 0x2a20a00 -> [+0x98]         */
static _Atomic uint64_t  g_life_destroyed;     /* 0x2a20a30                    */
static _Atomic int       g_native_phase3;      /* marker ancestry only         */
```

`sober_render_phase3_done()` keeps returning the native flag (also after
shutdown). The new predicate returns `1` **only** on the conjunction in §4;
its verdict string is built from the same fields so the test can assert the
per-field values, not just a boolean.

---

## 2. Frozen RVA inventory used by the design (all OBSERVED)

### 2.1 Swap / render device

| RVA / slot | Meaning |
| --- | --- |
| `0x649d422` | swap wrapper: `mov 0x20(%rdi),%rax; mov 0x28(%rdi),%rsi; mov %rax,%rdi; jmp 6d334d0` — **tail `jmp`, no `call`**; RA at PLT entry is the caller of `0x649d422`. |
| `0x6d334d0` / GOT `0x722a1a8` | `eglSwapBuffers@plt` / `R_X86_64_JUMP_SLOT` — the observation point. |
| `0x71912e8`, `[3]@0x7191300` | render vtable / swap vmethod (`0x649d422`); a real frame dispatches `call [vtable+0x18]`. |
| `0x649c21e` → `0x649c272` | create_context → EGL init; vtable installed at `ctx+0` (`0x649c29f`); EGLDisplay/EGLSurface at `ctx+0x20/+0x28`. |
| `0x649cf72` / fault `0x649cf9a` | config helper; `extra=1` is stored at `ctx+0x10` and dereferenced as a pbuffer at `0x649cf9a`; pass **`pbuffer=0`** for the window path (C04 §1.4, DERIVED, A/B-verifiable). |
| `0x649d1c2` / GOT `0x722a178` | surface helper / `eglCreateWindowSurface` GOT (substituted by shim). |
| `0x649c633…`, helpers `0x649d24d`/`0x649d285`/`0x649d246` | `eglGetProcAddress` loader block (~70 pointers). |
| `[base, base+0x6d33b90)` | guest `.text` RA classification window (first `PT_LOAD`). |

### 2.2 Guest GLES draw sites (new, read from the frozen ELF this task)

The 70 extension pointers live in a contiguous 8-byte table
`0x77a4ef0 … 0x77a4ff8` (`__stop_pb_defaults+0x522a38..+0x522b30`). Names read
from the loader's string RVAs (OBSERVED):

| Table slot | Function | Guest indirect call site(s) |
| --- | --- | --- |
| `0x77a4fe0` | `glDrawElementsInstanced` | `0x6496d75` |
| `0x77a4fe8` | `glDrawArraysInstanced` | `0x6496dec` |
| `0x77a4fa0` | `glClearBufferiv` | `0x6494aa3` |
| `0x77a4fa8` | `glClearBufferfv` | `0x6494991`, `0x6494a79` |
| `0x77a4fb0` | `glClearBufferfi` | `0x64949d2` |
| `0x77a4f98` | `glDrawBuffers` | `0x64947ef` |
| `0x77a4f78` | `glBlitFramebuffer` | `0x6494d1b`, `0x6494d53`, `0x6495920`, `0x649bb4c` |

Imported (PLT) GLES calls, with `readelf -rW` GOT slots and the sole guest call
site each (OBSERVED):

| GLES call | PLT | GOT (`R_X86_64_JUMP_SLOT`) | Guest call site |
| --- | --- | --- | --- |
| `glClear` | `0x6d33110` | `0x7229fc8` | `0x6494ae1` |
| `glDrawElements` | `0x6d33200` | `0x722a040` | `0x6496dbc` |
| `glDrawArrays` | `0x6d33210` | `0x722a048` | `0x6496e1a` |

These three sit inside the guest draw dispatcher entered at **`0x6496d0c`**
(the `glDrawElements`/instanced/`glDrawArrays` branches at
`0x6496dbc`/`0x6496dec`/`0x6496e1a`, each followed by `glGetError@plt`
checks). Our native first frame calls its own `dlsym`'d `g.eglSwapBuffers` /
`glClear*` (`native_render.c:803-825`) and therefore **never** traverses any of
these guest GOT/table slots — the discriminator is clean.

### 2.3 `ANativeWindow` binding

| RVA / slot | Meaning |
| --- | --- |
| `0x26fea5a` | shared `bind_window(JNIEnv*, jobject Surface)` helper. |
| `0x26fea78` | `call ANativeWindow_fromSurface@plt` (`0x6d31ad0`); RA here is guest proof of the app-start bind path. |
| `0x26fea80` / `0x26fea90` / `0x26feaa0` | read old global / `incl` generation / store new window. |
| `0x72a3308` / `0x72a3310` | global `ANativeWindow*` / window-generation counter. |
| `0x277322d`, `0x26fe8d2`, `0x2ebd5dd` | helper callers: UpdateSurface `0x2773194`, StartAppWithParams `0x26fe80f`, StartGame path. |
| `0x72294a8` / `0x72294a0` | `ANativeWindow_fromSurface` / `release` GOTs (PLT `0x6d31ad0` / `0x6d31ac0`). Hook body already exists at `native_render.c:975-982` but is **never installed** today. |
| `0x72297c0` / `0x72297c8` / `0x72297d0` | `acquire` / `getHeight` / `getWidth` GOTs — must be hooked **together** with the two above (C08 §4 Phase A step 3). |

### 2.4 Surface lifecycle dispatch

| Thunk | Action / observed anchors |
| --- | --- |
| `0x2a20850` created | release old at `[obj+0x140]` (`0x2a2086b`); fromSurface at `0x2a2087c`; store new at `0x2a20881`; `jmp [obj+0x88]` at `0x2a208a6`. |
| `0x2a208c0` changed | fromSurface at `0x2a20907`; `[obj+0xa0]` at `0x2a20933`; `[obj+0x88]` at `0x2a20957`. |
| `0x2a20a00` redraw-needed | load `[obj+0x98]` at `0x2a20a11`; `jmp *%rax` at `0x2a20a20` — the frame/redraw trigger. |
| `0x2a20a30` destroyed | dispatch `[obj+0xa0]` with the window. |

Descriptor table (OBSERVED, APS2-relocated): `0x6d37cd8` /
`0x6d37cf0` / `0x6d37d08` / `0x6d37d20`. These are Java→native thunks; a
native-only harness can only drive them with a synthetic Surface token, hence
any such drive is a withheld diagnostic (§5).

---

## 3. Hook design

All hooks forward to the original resolver **unchanged** (rendering behaviour
is not altered), are idempotent, and install with the existing
`mprotect(page, PROT_READ|PROT_WRITE)` → write slot → restore `PROT_READ`
pattern (`guest_render.c:78-84`). No guest `.text` byte is patched; only GOT
JUMP_SLOTs and the GLES function-pointer table (data) are written in our own
process. The guest base is stored in `g_guest_base` when the swap hook is
installed and is a precondition for every RA classification.

### 3.1 RA-split swap observer at GOT `0x722a1a8`

Replace/extend `sr_hook_egl_swap_buffers()` (`native_render.c:1009-1018`) with
a tagging variant; keep the existing install line and legacy counter/log so
`tests/first-frame.sh §4-§5` stays green.

```c
static int sr_hook_egl_swap_buffers_tagged(void *dpy, void *surf)
{
    uintptr_t ra = (uintptr_t)__builtin_return_address(0);
    uintptr_t base = atomic_load(&g_guest_base);
    int guest = atomic_load(&g_guest_base_valid) &&
                ra >= base && ra < base + 0x6d33b90u;   /* R1 */

    int ok = g.eglSwapBuffers ? (int)g.eglSwapBuffers(dpy, surf) : 0; /* forward */

    uint64_t draws = atomic_load(&g_gles_draws);
    uint64_t delta = draws - atomic_exchange(&g_draws_snapshot, draws);
    if (draws > delta && delta > 0) atomic_store(&g_draws_between, delta);

    atomic_fetch_add(&g_swaps_total, 1);                 /* legacy compat      */
    if (guest) {
        atomic_fetch_add(&g_swaps_guest_loop, 1);
        uintptr_t first = atomic_load(&g_loop_ra_first);
        if (!first) atomic_store(&g_loop_ra_first, ra);
        else if (first == ra) atomic_store(&g_loop_ra_consistent, 1);
        fprintf(stderr,
          "[render] guest-tagged swap at 0x649d422 ra=base+0x%llx tag=guest-loop"
          " | guest-tagged swaps: %llu | guest GLES draws between tagged swaps: %llu\n",
          (unsigned long long)(ra - base),
          (unsigned long long)atomic_load(&g_swaps_guest_loop),
          (unsigned long long)atomic_load(&g_draws_between));
    } else {
        atomic_fetch_add(&g_swaps_synthetic, 1);
        /* diagnostic only, never a proof line; no tag=guest-loop wording */
        fprintf(stderr,
          "[render] swap at 0x649d422 ra=%p tag=synthetic (host/external caller;"
          " excluded from guest-tagged count)\n", (void *)ra);
    }
    return ok;
}
```

Notes:

- Because `0x649d422` ends in a tail `jmp`, RA is the **caller of the wrapper**:
  today the host caller at `guest_render.c:97` (tag `synthetic`); in a real
  frame the return site after a guest `call [vtable+0x18]` with
  `[vtable slot 3] = base+0x7191300`. This is the core distinction the existing
  undifferentiated `g_guest_swaps` cannot make (A4 §1.3, C04 §1.3).
- Use `dladdr((void*)ra, …)` only to *label* a non-guest RA (host binary vs
  host lib); the authoritative test is the guest `.text` interval. An RA
  outside the interval is **never** promoted, even if it looks plausible.
- `g_loop_ra_first`/`g_loop_ra_consistent` are a defence against a one-off
  guest-origin swap: a real frame loop should present the same indirect-call
  return site repeatedly. The exact loop site cannot be pinned statically
  (~11,600 `call *0x18(%r..)` sites, C04 §2.3); accept a candidate RA only if
  an offline `objdump` slice shows the 5 bytes before it are an indirect call
  through a `+0x18` vtable slot (acceptance evidence §6, E4).
- Keep `g_phase3_done` set by the native swap (`:831/:945`) for the phase-3
  marker. In the default path the hook may still set it (legacy behaviour);
  the gate no longer treats that flag as render proof. Mixed evidence fails
  closed (§5).

### 3.2 Guest GLES draw counter (R2)

Install **forwarding** wrappers over the three PLT GOTs in §2.2
(`0x7229fc8`, `0x722a040`, `0x722a048`) and wrap the extension table entries
`0x77a4fe0`, `0x77a4fe8`, `0x77a4fa0/8/0xb0`, `0x77a4f98`, `0x77a4f78`
(after the loader block has populated them; skip NULLs; chain to the original
function pointer, do not substitute a driver). Every wrapper:

1. captures `__builtin_return_address(0)` and counts only when it lies in guest
   `.text` (expected sites from §2.2);
2. `atomic_fetch_add(&g_gles_draws, 1)` (and `_guest_ra` on the RA check);
3. tails to the saved original pointer with the same ABI/arguments.

Correlation: `sr_hook_egl_swap_buffers_tagged()` snapshots the draw counter at
each swap and keeps the maximum positive delta between two guest-tagged swaps
in `g_draws_between`. R2 is true only for `g_swaps_guest_loop ≥ 2 &&
g_gles_draws_guest_ra ≥ 1 && g_draws_between ≥ 1`. A single draw before a
single swap is **not** a frame interval (matches T4-4 R2 and C04 §3 step 2).
No draw is produced by the native test frame, so a false R2 from our own
`glClear` is impossible.

### 3.3 `ANativeWindow` binding observer (R4 part 1)

1. In the observation-only path, install a **forwarding** `ANativeWindow_fromSurface`
   wrapper over GOT `0x72294a8` that records the return address and calls the
   original resolver (`0x6d31ad0` → compat shim today). It must not fabricate a
   window in a proof run. Count `g_fromsurface_guest` only for RA
   `base+0x26fea78` (app-start helper), `base+0x2a2087c` or `base+0x2a20907`
   (lifecycle) — those are the three OBSERVED guest call sites (C08 §2.3/§2.6).
2. The active shim (`sober_render_hook_anative_window_from_surface()`,
   `native_render.c:975-982`) and the five-GOT install
   (`0x72294a8`, `0x72294a0`, `0x72297c0`, `0x72297c8`, `0x72297d0`) are a
   **withheld opt-in** (§5): hooking only `fromSurface` would hand our object to
   the compat `release` at `0x26fea9b` and corrupt it (C08 §4 Phase A step 3).
3. After the bind path runs, read `*(uintptr_t*)(base+0x72a3308)` and
   `*(uint32_t*)(base+0x72a3310)`; require non-NULL window **equal to the
   supplied shape**, generation delta ≥ 1 from the pre-call value, and a guest
   RA at one of the three sites. A 12-byte compat shim pointer or a harness
   write to the global is rejected. `g_window_bound` is set only then.
4. Do **not** call `0x26fea78` directly without a Java Surface (render-map §5;
   C08 §0). In the diagnostic driver, supply a guest-shaped Surface token and
   state plainly that the window is synthetic — that run is withheld.

### 3.4 Surface lifecycle observer (R4 part 2)

Observe, do not fabricate, the four thunks in §2.4. Recording is by RA at
`fromSurface` (`0x2a2087c` / `0x2a20907`) plus read-back of `nativeData+0x140`
and hit-counters on the callback fields `[+0x88]`, `[+0x98]`, `[+0xa0]`; where
we own the synthetic `nativeData`, set those fields to counting trampolines
(**data**, never code patching) and label those counts `synthetic` in the log.
Evidence conditions:

- `g_life_created ≥ 1` with guest RA at `0x2a2087c` and `[obj+0x140] != 0`;
- `g_life_redraw ≥ 1` with the dispatch at `0x2a20a11`/`0x2a20a20` reached;
- changed/destroyed are recorded as applicable, never required as proof of a
  first frame.

Entrypoints driven by the harness (the `call 0x2a20850` etc. themselves) have
host RA and are tagged synthetic; the **internal** guest RA is what counts.
Driving these thunks with a token Surface is an opt-in withheld diagnostic
(C04 §4 E2/E4, C08 §4 Phase D step 7).

### 3.5 Suppression / tagging of the synthetic first-frame swap (R3)

Two distinct synthetic swaps must never satisfy the predicate:

| Source | Mechanism | Treatment |
| --- | --- | --- |
| **S1** our native test frame | `sober_render_swap()` → `g.eglSwapBuffers` at `native_render.c:825`; does **not** traverse guest GOT; sets `g_phase3_done` at `:831` | Keeps the `first frame rendered` marker (phase-3 ancestry, R5). Never enters any guest-tagged counter. Gate no longer equates `phase3_done` with render proof. |
| **S2** Route B direct call | `guest_swap((uintptr_t)ctx)` at `guest_render.c:97` with the stack `0x48`-byte context; **does** traverse GOT `0x722a1a8`, RA in host `guest_render.c` | Default: tagged `tag=synthetic` and excluded from `g_swaps_guest_loop` (§3.1), legacy counter/log preserved so `first-frame.sh` stays green. Evidence mode: may be *suppressed* (skip the call) with an explicit `[ev-suppress]` line, never silently. A suppression knob is withhold entry (T4-4 §4.6). |

Per T4-4 §3.3, `tag=synthetic` may appear only in non-counting diagnostic
lines; a proof run must contain no synthetic swap counted as proof. **Mixed
evidence fails closed**: if any synthetic swap is present in the same run as a
guest-loop classification, the verdict is `MIXED -> WITHHELD`, never
`GUEST-TAGGED`.

---

## 4. Predicate, verdict and normative log lines

```c
int sober_gate_render_guest_tagged_ok(void)
{
    if (!atomic_load(&g_guest_base_valid)) return 0;              /* R1 setup */
    if (atomic_load(&g_swaps_guest_loop) < 2) return 0;           /* R1/R2    */
    if (atomic_load(&g_gles_draws_guest_ra) < 1 ||
        atomic_load(&g_draws_between) < 1) return 0;              /* R2       */
    if (atomic_load(&g_swaps_synthetic) != 0 &&
        atomic_load(&g_mixed_evidence)) return 0;                 /* R3 fail-closed */
    if (!atomic_load(&g_fromsurface_guest) ||
        !atomic_load(&g_window_bound) ||
        !atomic_load(&g_life_created) ||
        !atomic_load(&g_life_redraw)) return 0;                   /* R4       */
    return 1;   /* R5 (phase3_done / ancestry) is ANDed by the gate, not here */
}
```

The verdict string enumerates the failed sub-condition, e.g.
`... -> GUEST-TAGGED` or `... -> ABSENT(R1|R2|R4) -> WITHHELD`; the gate emits
it before the early return (T4-4 §2 step 6). Emit exactly the T4-4 §1.3
normative strings on the positive path (no test-only synthesis):

```
[render] guest-tagged swap at 0x649d422 ra=base+<RA_IN_GUEST_TEXT> tag=guest-loop | guest-tagged swaps: <N> | guest GLES draws between tagged swaps: <M>
[render] guest render verdict: guest-tagged frame confirmed (swaps=<N> draws=<M> fromSurface=installed window=bound) -> GUEST-TAGGED
[app-shell] render provenance: guest-tagged frame at 0x649d422 (swaps=<N>, draws=<M>), phase3_done=1 -> OK
```

Negative lines remain available:
`[app-shell] marker withheld: guest-tagged render absent (phase-3 swap not guest-provenanced)`,
the existing `[app-shell] marker withheld: callback=%d render=%d …`, and
`tag=synthetic` diagnostic lines. The gate's sole
`emit_marker("app shell rendered\n", "SOBER_SHELL_MARKER")` site and
`tests/app-shell.sh §3.3` absence checks are unchanged.

**Open contract question for lead (do not resolve in code):** T4-4 §1.3 fixes
the field `fromSurface=installed`, but a genuinely clean run would use the
real NDK fromSurface and our wrapper would merely observe it. Either (a) emit
`installed` for any observed guest-RA fromSurface path (semantically loose),
or (b) let the test accept `fromSurface=installed|native`. This is a wording
issue only; it must not become a reason to install a synthetic hook in a proof
run. No unilateral test edit is proposed here.

---

## 5. Decision 013 / phase-3 safety (no weakening)

1. **Additive AND only.** `shell_cb` stays the name-OR, `seeded_state ⇒
   shell_cb = 0` stays before the early return, the withhold chain, the
   `!shell_cb || !render_ok` return `-4`, and the sole marker site stay
   unchanged (`guest_app_shell.c:1567-1594`). T4-3's provenance leg is ANDed
   with `shell_cb`; it is never replaced by a name count.
2. **Default-ON proof.** The predicate and its observers read no `SOBER_*` and
   are active with all opt-ins unset. Enabling a knob can never be how proof
   is obtained (T4-4 §0 rule 2, §4.2). Forwarding GOT hooks are observation,
   not guest-state manufacture; the existing swap hook already has this status.
3. **Withhold wins / fail closed.** Any opt-in that supplies a window, drives
   a lifecycle thunk, calls `0x2773194`/`0x26fe80f`/Route A with `pbuffer=0`,
   wraps a GLES table entry with a substituting driver, or suppresses a
   counter must join the `seeded_state` OR-chain *in the same change* and log
   `marker withheld: diagnostic guest-state seed active`. Proposed new names
   (all withhold entries if introduced): `SOBER_RENDER_FRAME_EVIDENCE`,
   `SOBER_EVIDENCE_SURFACE`, `SOBER_EVIDENCE_DEVICE`, `SOBER_EVIDENCE_REDRAW`,
   `SOBER_RENDER_SUPPRESS_SYNTHETIC`.
4. **Mixed evidence.** GENUINE callback + any `HARNESS-THUNK`, or guest-loop
   swap + any unexcluded synthetic swap ⇒ withhold. The render predicate
   returns `0` in that case (§4).
5. **Phase-3 marker unaffected.** `first frame rendered` and
   `guest init complete` remain green on the default path; the legacy
   `g_guest_swaps` and `guest call 0x649d422` log line are preserved. Engine
   SHA-256/BuildID unchanged; `ldd` libc-only; `ctest` 2/2.
6. **No success claim.** Even if every render condition is observed under an
   opt-in, the marker stays WITHHELD because that opt-in is in the withhold
   chain. A clean green still additionally requires the genuine
   notifier-driven `gameLoadedCallback` (notifier `0x27eaf6e`, `placeId>0`,
   RA pair `0x2584960`/`base+0x27eafb3`) from BLK-07/T4-3.

---

## 6. Acceptance evidence (A/B matrix; all opt-ins OFF unless named)

| Step | Knob | Expected observation | Gate / marker |
| --- | --- | --- | --- |
| **E0 baseline** | none | One swap through GOT `0x722a1a8` with `ra=<host>` → `tag=synthetic`; `guest-tagged swaps: 0`; `guest GLES draws between tagged swaps: 0`; `0x72a3308` not guest-bound; no lifecycle counts. `first frame rendered` still green, `g_guest_swaps==1` legacy line present. | `render_ok=0`; `marker withheld: callback=0 render=0` (was `render=1`) — correct RED, and now honest. |
| **E1 RA split** | default-ON hook | E0 RA classified host; after a guest-loop swap, `ra=base+0x…` inside `[base,base+0x6d33b90)`, `tag=guest-loop`; offline `objdump` shows `call *0x18(%r..)` immediately before RA. | No marker from E1 alone. |
| **E2 draw counter** | default-ON wrappers | Guest draw sites (§2.2) counted with guest RA; native `glClear` not counted; `draws_between ≥ 1` only when two tagged swaps bracket a draw. | No marker. |
| **E3 window bind** | `SOBER_EVIDENCE_SURFACE=1` (withheld) | Five GOTs installed; `fromSurface` RA `0x26fea78`/`0x2a2087c`; window global `0x72a3308` non-NULL and matching shape; generation `0x72a3310` incremented. A/B: hooking only `0x72294a8` corrupts/frees the shim at `0x26fea9b`. | `marker withheld: diagnostic guest-state seed active`; no `app shell rendered`. |
| **E4 device / redraw** | `SOBER_EVIDENCE_DEVICE=1`, `SOBER_EVIDENCE_REDRAW=1` (withheld) | `0x649c21e(window, pbuffer=0)` succeeds, no fault at `0x649cf9a`, vtable `0x71912e8` at `ctx+0`; `eglCreateWindowSurface` reaches GOT `0x722a178`; `0x2a20a00` reaches `[+0x98]` at `0x2a20a11/0x2a20a20`; ≥2 tagged swaps with ≥1 draw. A/B: `pbuffer=1` reproduces `0x649cf9a`. | Diagnostic line only; marker withheld. |
| **E5 suppression** | `SOBER_RENDER_SUPPRESS_SYNTHETIC=1` (withheld) | `[ev-suppress]` line; no `tag=synthetic` swap in the counting window; `tag=guest-loop` counter unambiguous; mixed-evidence control (both tags) → `MIXED -> WITHHELD`. | Marker withheld (knob in chain). |

Invariants to re-check after **every** step: engine SHA
`3e498d64…41335` / BuildID `204ad049…6c9` unchanged; `guest init complete`
and `first frame rendered` green on the default path; `tests/first-frame.sh`
PASS; `ctest` 2/2; `ldd` libc-only; **zero** `app shell rendered` emissions in
any evidence mode; every new knob present in the withhold OR-chain and in
`tests/app-shell.sh`'s unset list.

This module's own acceptance is the diagnostic correctness above (E0–E5) plus
**no false green**; it is not the app-shell marker. The full green exit
criterion remains `analysis/T4_4_gate-tests.md §3.4` and is not claimed here.

---

## 7. Risks, unknowns and honesty limits

- **UNKNOWN:** the exact guest frame-loop call site of `call [vtable+0x18]`
  cannot be pinned statically; the design discovers it via E1 RA capture and
  confirms it offline (§6 E1). A guest-origin one-off swap without a stable RA
  must not satisfy R1.
- **UNKNOWN:** whether the native EGL backend accepts our shaped
  `ANativeWindow` in Route A; whether the app-shell draws through global
  `0x72a3308` or `nativeData+0x140` (both are seeded together). Route A's
  `pbuffer=0` fix is DERIVED and A/B-verifiable, not yet observed.
- **RISK:** GOT-page permission restoration, lazy PLT binding, and GLES-table
  NULL slots must be handled; wrappers must chain to the *original resolver*
  and never substitute a driver in the proof path.
- **RISK:** all evidence must be read after render shutdown; keep state in
  process-global atomics (§1), not in `SoberRender *`.
- **CONTRACT RISK:** T4-4's fixed `fromSurface=installed` field (§4) and the
  requirement that gate proof be default-ON need a lead wording ruling; no
  unilateral test/gate edit is proposed.
- No build, harness run, debugger capture or REA query was performed for this
  document; the GLES slots/call sites in §2.2 were read from the frozen ELF and
  its retained disassembly this task. No evidence line in §4/§6 has been
  observed at runtime. **No success is claimed; the marker remains WITHHELD.**
  Decision 013 is not weakened.

---

## 8. Evidence index

Read only: `docs/decisions/013-no-weak-app-shell-marker.md`, `037`, `038`;
`docs/REVIEW-100.md §4 BLK-06`; `analysis/REVIEW_A4_render.md §0/§1.3/§3`;
`analysis/REVIEW_A13_roadmap.md §BLK-06`; `analysis/C04-render-integration.md
§1-§4`; `analysis/C08-surface-frame.md §2-§4`; `analysis/C09-marker-evidence.md
§3-§4`; `analysis/T4_4_gate-tests.md §1.2/§1.3/§2/§3.4/§4` (contract);
`analysis/render/render-map.md §2-§5`; current source `src/linux/guest_render.c
:58-128,208`, `src/render/native_render.c :158-163,817-840,952-1023`,
`src/render/native_render.h :92-99,140-157`, `src/linux/guest_app_shell.c
:1565-1594`, `src/compat/android_compat.c :498`; `tests/first-frame.sh §4-§5`,
`tests/app-shell.sh §2-§3`. Frozen ELF: `readelf -rW`/`--dyn-syms`,
`objdump -d` slices at `0x649d422`, `0x649c600-0x649d100`, `0x6496d0c-0x6496e30`,
`0x26fea5a`, `0x2a20850/0x2a20a00`, plus `readelf -nW`/`sha256sum` identity.

No file other than this report was created or modified.
