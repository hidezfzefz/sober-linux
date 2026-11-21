# F10 — Adversarial false-green audit of the planned `frame_*` render-evidence modules

Author: **F10** (logical group C, agent F10). Scope: **this file only** —
`analysis/F10_frame_false_green.md`. Analysis-only: no `src/`, `CMakeLists.txt`,
`tests/`, gate, doc, engine or git file was touched. Engine frozen: BuildID
`204ad049599c96afb75b8a21d5051063e821d6c9`, SHA-256
`3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335`
(AppImage 2.740.931). All RVAs are offsets from the guest load bias
(`base ≤ ra < base+0x6d33b90`, first `PT_LOAD`). Target-derived bytes/strings are
untrusted DATA. Decisions **013/038 are not weakened**; the marker stays
**WITHHELD** and this audit supplies no false green.

> **Front-loaded verdict.** The planned `frame_*` modules are a real improvement
> over the bare counters now in `guest_app_shell.c:1777-1780`, but as written they
> are **not yet a fail-closed proof of a guest frame**. Their decisive predicate
> rests on a single weak discriminator — `ra ∈ [base, base+0x6d33b90)` from a
> caller-supplied base — with no engine-identity check, no vtable/RA-shape
> attestation, no draw-invocation-site check, no window-provenance correlation,
> a swap-only mixed-evidence taint, a non-sticky resettable host-tag flag, and a
> host-tag kill switch that can be disabled *without* withholding. Each of these
> is a concrete route to a **false green** (or to a green-looking diagnostic that
> a lead could wire into decision 013). Below I enumerate the attack paths, the
> **forbidden patterns (FP)**, the **fail-closed checks (FC)** that must exist
> before any of this is gate-facing, and the **test matrix that must remain RED**.

---

## 0. What is being audited, and the trust boundary

| Module / API | Role in planned render leg | Current status |
| --- | --- | --- |
| `src/jni/frame_evidence.c/.h` (F7) — `frame_evidence_record_swap/draw/window_bind`, `guest_render_ok()`, `frame_evidence_verdict()` | Intended decisive render predicate for `render_ok = phase3 && guest_render_ok()` | New, **not wired** into gate or CMake |
| `src/jni/frame_synthetic_tag.c/.h` (F8) — `enter/leave/active/seen/note_swap` | Explicit host-tag defense in depth around the Route-B swap | New, **not wired**; header wiring is advisory |
| `src/jni/render_guest.c` — `rg_hook_swap` (`GOT 0x722a1a8`), `rg_wrap_draw`/`rg_draw_calls`, `sober_render_guest_swap_via_vtable()` | Existing counters the live gate reads | Live, default-ON |
| `src/render/native_render.c` — `sr_hook_egl_swap_buffers`, `sr_hook_egl_get_proc_addr`, `g_gles_draw_calls` | Legacy/duplicate counters | Live; fallback when module absent |
| `src/linux/guest_render.c` — `call_guest_render_entries()` Route-B call, `gr_guest_swap_evidence()` | Host caller that manufactures all current "guest" evidence | Live |
| `src/linux/guest_app_shell.c:1777-1780` | Live render leg | Bare `guest_tagged>=1 && draws>=1 && window != NULL` |

**Trust boundary:** everything the `frame_*` observers learn arrives through
**host-controlled call sites** (`__builtin_return_address(0)` captured in a host
binary, a host-supplied `guest_base`, host-written guest globals). The guest is
`dlopen`'d `RTLD_NOW|RTLD_LOCAL` into our own process; a host thread can call any
guest RVA, read/write any guest GOT slot, and plant stack return addresses.
Therefore an RA interval test alone is a **label**, not proof of guest
authorship. Any design that treats a label as provenance is exploitable.

---

## 1. Attack paths (adversarial, by named vector)

### 1.1 Host RA mislabeled as guest

**Path A — bogus guest base.** `frame_evidence_set_guest_base()` is public, takes
an arbitrary `uintptr_t`, is never validated against the frozen BuildID/SHA or a
`/proc/self/maps` guest image, and may be called more than once. `fe_ra_tag()`
(frame_evidence.c:111-127) then classifies *any* RA in
`[base, base+0x6d33b90)` as `SOBER_FRAME_TAG_GUEST`. If a wiring bug, an env hint,
or a second mapping makes `base` equal to a host library's load address, the
**host return address of `rg_hook_swap` on the Route-B path** lands in range and
is counted in `s_swaps_guest`. `frame_evidence_observe_window_global()` worsens
this: if `s_base == 0` it silently adopts the *caller-supplied* `guest_base`
(frame_evidence.c:320-334), so any caller picks the classifier window.

**Path B — over-broad interval.** The window is the entire first `PT_LOAD`
(`0x0..0x6d33b90`), which includes `.plt` `[0x6d319a0,0x6d33b90)` and executable
data-adjacent pages, not just guest `.text` `[0x1db9a40,0x6d3145a)`. A host or
gadget RA anywhere in that 114 MB-ish span is "guest".

**Path C — planted return address.** `0x649d422` is a **tail `jmp`** to
`eglSwapBuffers@plt`; it creates no stack frame. The RA captured at GOT
`0x722a1a8` is simply the word on the stack. A host can push a guest RVA (e.g.
the known return site after `call [vtable+0x18]`) and jump to `0x649d422`, so
`__builtin_return_address(0)` is a genuine guest-range value from a wholly host
call chain. The interval test cannot tell them apart. (R6 §1, P1 §0.)

**Required fail-closed response:** FC-1, FC-2, FC-3, FC-12. No gate input may
trust a caller-supplied base/tag; BuildID+SHA must be verified before any
classification; RA must additionally match the exact indirect-call shape and be
stable across ≥2 swaps.

---

### 1.2 Counter double-count

**Path A — two hooks, one slot.** `render_guest.c:rg_hook_swap` and
`native_render.c:sr_hook_egl_swap_buffers` both increment a `swaps_total` and both
do RA tagging. `guest_render.c:call_guest_render_entries()` currently avoids
installing the native one only by a **runtime conditional** (`if (s_gr.present)`
at `:335-345`). If both are ever installed, or a third `frame_evidence` observer
is wired at the same GOT, one physical swap produces two or three increments;
`>=1` thresholds then pass on zero guest frames.

**Path B — summing independent counters.**
`gr_guest_swap_evidence()` (guest_render.c:272-277) computes
`native_total + module_total`. Both count the *same* Route-B swap when both hooks
are live, so the "evidence" counter can read 2 for one host call. Summing
counters from different modules to satisfy a threshold is structurally unsafe.

**Path C — wrap-time vs invocation-time draws.** `native_render.c:sr_hook_egl_get_proc_addr`
increments `g_gles_draw_calls` when a draw pointer is **wrapped**, not when it is
called (`if (wrapped != real) fetch_add(...)`). In today's R1 state that counter
would read **5** while `render_guest.c:rg_draw_calls()` correctly reads **0**
(`draw_wrapped=5, draw_calls=0`). If a lead wires `sober_render_gles_draw_calls()`
into `draws>=1`, the gate passes on functions that never ran.

**Path D — repeated observer calls.** `frame_evidence_record_swap/draw()` are
public and have no dedup key. Two wiring sites calling them for the same physical
event double-count silently. The header promises "one immutable event per
observation, counters move by exactly one" but the code enforces no binding
between one physical event and one observer call.

**Required fail-closed response:** FC-4, FC-5, FC-6, FC-10.

---

### 1.3 Synthetic swap replayed through guest GOT

The default harness already replays a synthetic swap through the guest PLT once
(Route B, `guest_render.c:354`). Replaying it N times with no frame loop changes
no semantics, yet every replay traverses `GOT 0x722a1a8` and calls the observer.
To flip the label, an attacker combines §1.1 Path C (planted guest RA) with:
- **no `frame_synthetic_tag` scope** (call from a different host site than the one
  bracketed in `guest_render.c`), so `active()` is 0 and no `seen` bit is set;
- **no draw, no window correlation** — but the live gate
  (`guest_app_shell.c:1777-1780`) has no bracketed-draw or window-provenance
  check, and `frame_evidence` has no stable-RA requirement beyond `>=1`.

`frame_synthetic_tag` brackets **only** the single direct call
`guest_swap((uintptr_t)ctx)` in `guest_render.c`; the sibling host entry
`sober_render_guest_swap_via_vtable()` (render_guest.c:520-548) is **not**
bracketed, and the header's suggested wiring in `rg_hook_swap` would force a
swap to synthetic **only while `active()` is true**. A replay from any other host
site is unlabeled. Worse, `SOBER_FRAME_SYNTH_TAG=0` makes `enter()` inert and
`active()` always 0 while leaving all decisive render counts untouched — the
header explicitly says this kill switch is *not* a withhold entry. That is a
suppression knob by T4-4 §4.6 and must be forbidden as written.

**Required fail-closed response:** FC-3, FC-8, FC-9, FC-10; require stable RA,
bracketed guest draw, host-scope absence, and single-owner hook.

---

### 1.4 Host vtable call / host-owned function pointer

`sober_render_guest_swap_via_vtable()` is a **host** function that verifies the
frozen vtable (`vt == base+0x71912e8`, `[vt+0x18] == base+0x649d422`) and calls
`swap(s_ctx)` with a host-built `0x48`-byte context. Its RA is in the host, so it
is honestly synthetic today — but its own comment says so, and no structural
guard prevents a lead from wiring it as an "evidence source" or from replacing
the vtable/nativeData callback slots with host functions before a guest dispatch:

- If a **host function pointer** is installed in vtable `[3]` (or in `nativeData
  [+0x88]/[+0x98]/[+0xa0]`) and a guest instruction performs
  `call [vtable+0x18]`, the captured RA is a *guest* return site while the work
  actually executed is host code. Both the interval test and the RA-shape test
  would pass. Decision 013 already forbids host-function vtables; the `frame_*`
  design must enforce this structurally, not just by policy.
- `frame_evidence.c` verifies **nothing** about which vtable/slot produced a
  swap event, and `fe_note_guest_ra()` records only the raw RA range. There is no
  check that the target was `base+0x649d422` or that the vtable base was
  `base+0x71912e8`.

**Required fail-closed response:** FC-3 (vtable/target attestation), FC-7
(forbid host-owned pointers in evidence paths), FC-9 (bracket the vtable API
too), FC-11 (callback leg from the real notifier, not a host call).

---

### 1.5 Mixed evidence

`frame_evidence`'s only taint is `fe_update_mixed()` (frame_evidence.c:203-208):
`s_swaps_guest>0 && s_swaps_synthetic>0`. Gaps:

1. **Draws are never tainted.** `frame_evidence_record_draw()` never calls
   `fe_update_mixed()`. A run with one guest-attributed draw plus any number of
   synthetic/host draws still returns a positive `frame_evidence_draws()`.
2. **Window binds are never tainted.** `s_window_bound` is sticky once set; a
   later host overwrite of `0x72a3308` or a synthetic rebind does not clear it.
3. **Window provenance is not verified.** `frame_evidence_record_window_bind()`
   accepts any `(window != 0, RA in guest range, generation strictly greater than
   previous)`. Today `0x72a3308` is **host-written** and `0x72a3310`
   **host-incremented** by `render_guest.c:417-423` and `:326-329`. A host caller
   can therefore call `frame_evidence_observe_window_global(base, some_guest_RA)`
   and bind the host-written token as "guest-bound". The header even notes a
   fromSurface hook is only supporting evidence — but removing it entirely from
   the decisive path is what makes this forgeable.
4. **Concurrency hole in the taint itself.** Counts and the taint are updated
   with `memory_order_relaxed`. A guest swap and a synthetic swap on two threads
   can each read the other count as 0 at check time and leave `s_mixed == 0`.
5. **Cross-module mixing.** The live gate reads `render_guest` counters directly;
   the planned gate would read `frame_evidence::guest_render_ok()`. If both sets
   of conjuncts are ANDed/ORed together, an attacker can satisfy different legs
   with different evidence sources.

**Required fail-closed response:** FC-6, FC-8, FC-10, FC-12. Taint must be
computed from all event kinds at decision time (not only a sticky swap flag),
window binding must be correlated to a guest-RA `fromSurface` returning the same
pointer, and there must be exactly one authoritative render source.

### 1.6 `frame_thread` (F6) can manufacture a "stable guest RA" on demand

A sibling planned module, `src/jni/frame_thread.c/.h`, starts a **host** thread
that repeatedly calls the genuine guest redraw thunk `0x2a20a00` with a
caller-supplied `nativeData` (`[+0x98]` frame callback / `[+0x140]` window slots).
It never calls `0x649d422` itself and publishes
`sober_frame_thread_should_withhold()`. This makes an important point for my
FC-3: **a stable, repeated guest return site at GOT `0x722a1a8` is no longer a
sufficient discriminator.** A host-driven loop through a guest thunk produces
exactly the "stable RA across ≥2 swaps" signal that T3-5/R6 propose as the anti-
one-off defence — from a synthetic driver with a host-chosen `nativeData`. If a
lead supplies synthetic callback pointers in those slots (or F6's own honesty
note is ignored), `frame_evidence` sees guest-RA swaps, guest-RA draws and a
guest-RA window bind, all manufactured. The module is correctly labelled a
withheld diagnostic; the false green appears only if that flag is not in the
`seeded_state` chain or if its evidence is consumed as proof. Its
`should_withhold()` returning false after stop/uninstall would also erase a
run-level taint (FP-6 class).

**Required fail-closed response:** FC-3(d) must be augmented with guest
*authorship* of the driver (not merely a stable RA) — i.e. the notifier-driven
callback leg (FC-11) and zero seeded state — and F6's withhold bit must be
sticky for the process once any frame was driven.

---

## 2. Forbidden patterns (FP) — reject any planned patch containing them

| # | Forbidden pattern | Why it is a false-green route |
| --- | --- | --- |
| FP-1 | Trusting a caller-supplied `guest_base` or a pre-classified tag as proof; classifying before BuildID/SHA verification | §1.1 A/C; base choice alone flips host→guest |
| FP-2 | Using the bare interval `[base, base+0x6d33b90)` as the *sole* swap/draw discriminator | §1.1 B/C; includes `.plt`, cannot see RA shape |
| FP-3 | Counting wrap/registration events (e.g. `native_render.c:g_gles_draw_calls`, `draw_wrapped`) as draws | §1.2 C; current run would read 5 draws with zero invocations |
| FP-4 | OR-ing or summing counters from different modules (`gr_guest_swap_evidence()`), or calling an observer twice for one physical event | §1.2 A/B/D; one event → multiple "proofs" |
| FP-5 | A knob that disables/filters/suppresses a classifier, observer or counter without being in the `seeded_state` withhold chain **and** failing closed | `SOBER_FRAME_SYNTH_TAG=0` as specified is exactly this; T4-4 §4.6 |
| FP-6 | Resetting/clearing taint, base or counters after install (`sober_frame_synthetic_tag_reset()`, any future `frame_evidence_reset()`) | erases host-swap evidence; `g_seen` must be monotonic |
| FP-7 | Host-owned function pointers in the guest vtable, `nativeData` callbacks, or draw pointers used as evidence; `sober_render_guest_swap_via_vtable()` as an evidence source | §1.4; guest RA with host work |
| FP-8 | Mixed guest/synthetic evidence tolerated for any event kind; taint only on swaps | §1.5 1–4 |
| FP-9 | Window binding from a bare non-NULL global or a host-written pointer; host RA at the bind site | §1.5 3; the live gate's bare `!= NULL` conjunct is already this |
| FP-10 | Any single leg as `render_ok`: `phase3_done` alone, `swaps>=1` alone, draws alone, or window alone | decision 038 §2.2; phase3 is ancestry only |
| FP-11 | Treating any `frame_*` positive as sufficient for the marker without notifier-driven `gameLoadedCallback` + `seeded_state==0` | decision 013/038; render alone never qualifies |
| FP-12 | A second marker emitter, a diagnostic marker reusing `app shell rendered`/`SOBER_SHELL_MARKER`, or test-only string synthesis | decision 038 §3–§4; one emitter, emit exactly one line |
| FP-13 | Silently suppressing the Route-B swap (no `[ev-suppress]` line, no withhold entry) or changing `rc`/frame handling to force a green log | T3-5 §3.5/E5, T4-4 §4.6 |
| FP-14 | A public self-test that mutates live global state (`frame_evidence_selftest()` writes `s_base` then restores it) being callable after real config | races/corrupts classification; selftest must be pure-local |
| FP-15 | Treating a host-driven `frame_thread` loop (or any host-supplied `nativeData` with guest callback slots) as guest-authoured evidence; non-sticky `sober_frame_thread_should_withhold()` | §1.6; manufactures stable guest RAs and a "frame loop" on demand |

---

## 3. Fail-closed checks (FC) required before any `frame_*` output is gate-facing

1. **FC-1 Engine identity.** Before RA classification, verify BuildID
   `204ad049…6c9` **and** SHA-256 `3e498d64…41335` of the running guest image
   (mirror `provenance_gate`'s `build_verified`). Mismatch/verification failure ⇒
   all decisive accessors 0, verdict `ABSENT/DISABLED`. No Flatpak `791c3f9e…`
   RVA set may be classified.
2. **FC-2 Base lock.** `frame_evidence_set_guest_base()` must be single-assignment,
   callable only by the lead wiring after mapping; reject a second differing
   value; reject 0 as "disabled"; never adopt a caller argument in
   `observe_window_global`. `frame_evidence_selftest()` must classify with a
   local helper and not touch `s_base`/counters (fix FP-14).
3. **FC-3 RA attestation for swaps.** Require (a) RA in **guest `.text`**
   `[base+0x1db9a40, base+0x6d3145a)`, tighter than the PT_LOAD window; (b) the
   bytes immediately preceding RA decode to an indirect
   `call QWORD PTR [reg+0x18]`, (c) the resolved target is `base+0x649d422`
   reached through vtable `base+0x71912e8` slot `[3]@0x7191300`, and (d) the same
   RA recurs across ≥2 tagged swaps (R6/T3-5 R1 `stable-RA` defence). A ROP-planted
   RA fails (b)/(d). **Stability alone is not authorship** (§1.6): a
   `frame_thread`-driven loop also yields a stable RA, so (d) is only admissible
   together with FC-11 (notifier-driven callback) and zero seeded state.
4. **FC-4 Single GOT owner.** Exactly one hook at `GOT 0x722a1a8`. If
   `rg_hook_swap` and `sr_hook_egl_swap_buffers` (or any third observer) are both
   installed, fail closed: decisive counts 0, log `MIXED/CONFLICT -> WITHHELD`.
   The current `if (s_gr.present)` conditional is not a proof of this invariant.
5. **FC-5 One event = one count.** Dedup by (kind, RA, monotonic timestamp within
   a small window) and bind each observer to a single, named call site. Counts
   only move on a committed, deduplicated record; overruns (`dropped>0`) force a
   fail-closed verdict rather than a partial count.
6. **FC-6 Draw attestation + bracketing.** Count only **invocations** whose RA is
   one of the observed guest draw sites (`0x6496dbc`, `0x6496dec`, `0x6496e1a`,
   `0x6494ae1`, or the extension-table indirect sites from T3-5 §2.2), never
   registration; require ≥1 such draw between two FC-3-attested swaps
   (`draws_between ≥ 1`). Forbid `sober_render_gles_draw_calls()` (wrap-time
   counter) as a gate input.
7. **FC-7 Window attestation.** `window_bound` requires a guest-RA
   `ANativeWindow_fromSurface` at `0x26fea78`, `0x2a2087c` or `0x2a20907`
   returning the **same pointer** read from `0x72a3308`, plus a generation delta
   at `0x72a3310` attributable to that guest path. A host store of `s_anw` never
   satisfies it. Hook all five ANativeWindow GOTs together (C08/P8 warning).
8. **FC-8 Total mixed taint.** Taint on **any** guest+synthetic mixture across
   swaps, draws and window events; recompute from raw counts in every decisive
   accessor (defeats the relaxed-atomic race in §1.5.4); sticky and monotonic for
   the process; `MIXED ⇒ decisive counts 0`. `window_bound` must also be cleared
   if a later non-guest bind occurs.
9. **FC-9 Host-tag discipline.** `SOBER_FRAME_SYNTH_TAG=0` must either (a) force
   `guest_render_ok()==0` (fail closed) or (b) be added to the `seeded_state`
   withhold chain — as written it is neither and is forbidden (FP-5). The scope
   must wrap **all** host→guest swap entries, including
   `sober_render_guest_swap_via_vtable()`. `g_seen`/`tagged_swaps` must be
   monotonic; no public post-run `reset()` may clear them. Any host scope seen in
   the run disqualifies aggregate guest-swap evidence unless an explicit,
   withheld `[ev-suppress]` mode removed that swap.
10. **FC-10 Single authoritative render source.** `render_ok` must be exactly
    `phase3_done && guest_render_ok()` from one module. The live conjuncts
    `sober_render_guest_swaps_guest_tagged()`,
    `sober_render_guest_gles_draw_calls()` and the bare
    `*(void**)(base+0x72a3308)!=NULL` in `guest_app_shell.c:1777-1780` must be
    **replaced**, not supplemented; all raw counters remain log-only. No OR/sum
    across sources (FP-4).
11. **FC-11 Gate ordering unchanged.** name callback → default-ON positive
    callback provenance (notifier `0x27eaf6e`, `placeId>0`, RA pair
    `0x2584960`/`base+0x27eafb3`, cached class `0x72a2f48`/method `0x72a2fc0`)
    → `shell_cb = name && prov` → `seeded_state` override wins → `phase3 &&
    guest_render_ok` → verdict lines → `!shell_cb || !render_ok` early return →
    the **sole** `emit_marker` site. Every `frame_*` kill switch is default-ON
    and fail-closed. No new opt-in may become proof.
12. **FC-12 Withhold/test parity.** Every new knob
    (`SOBER_FRAME_EVIDENCE`, `SOBER_FRAME_SYNTH_TAG`, any `SOBER_EVIDENCE_*`/
    `SOBER_RENDER_SUPPRESS_SYNTHETIC`) is enumerated in `guest_app_shell.c`'s
    OR-chain and the `tests/app-shell.sh` unset list if it can alter evidence;
    a diagnostics-only recorder is default-ON with no opt-in. Extend the
    withhold audit (D09) to all TUs reading these envs.

---

## 4. Test cases that must remain RED (negative controls)

All on the frozen engine, all `SOBER_*` opt-ins unset unless named. "RED" means
`guest_render_ok()==0` / verdict `ABSENT|INCOMPLETE|MIXED|DISABLED` and **zero**
`app shell rendered` emissions. A control that flips green is a false-green bug.

| # | Control / injection | Required RED observation | Current-code status |
| --- | --- | --- | --- |
| RED-1 | **E0 default baseline** | One Route-B swap, RA host → `tag=synthetic`; `guest swaps=0`, draw invocations=0 (`draw_wrapped=5`); window host-written; verdict `ABSENT`; `render=0`; `first frame rendered` still green; `tests/app-shell.sh` RED at its notifier leg | Holds today (`swaps=1/0/1, draws=0`) |
| RED-2 | `SOBER_FRAME_EVIDENCE=0` with any injected "guest-looking" swap/draw/window | all decisive accessors 0; verdict `DISABLED`; no marker | Holds in `frame_evidence.c`; must be re-checked after wiring |
| RED-3 | `SOBER_FRAME_SYNTH_TAG=0` **plus** a planted guest-range RA on the Route-B swap | still `guest_render_ok()==0` (FC-1/3/9); no marker — disabling a classifier must not help | **At risk**: kill switch is currently not fail-closed (§1.3) |
| RED-4 | Host replays `0x649d422` N times with a planted guest RA, no draw, no lifecycle | RA-shape/stable-RA/window checks fail ⇒ `INCOMPLETE`; no marker | **At risk**: interval-only tagging would mark all N guest |
| RED-5 | `sober_render_guest_swap_via_vtable()` (host vtable call, host ctx) | RA host ⇒ synthetic; `guest_render_ok()==0`; never wire as evidence | Holds in `render_guest.c`; must stay non-evidence |
| RED-6 | Wrap-only draws (R1 state: 5 procs wrapped, 0 invoked) and/or gate fed `sober_render_gles_draw_calls()` | `draws_guest==0`; `guest_render_ok()==0` | **At risk** if native wrap-time counter is wired |
| RED-7 | Double observation: both swap hooks installed, or observer called twice for one physical swap; `gr_guest_swap_evidence()` sums both | single-owner/dedup check fails closed; `>=1` never reached from one event | **At risk**: summing and runtime conditional are structural gaps |
| RED-8 | Mixed: 1 guest swap + 1 synthetic swap; separately 1 guest draw + 1 synthetic draw; separately guest-RA caller + host-written window | verdict `MIXED`, decisive counts 0, window unbound | **Partially holds**: only swap-swap taint exists (§1.5) |
| RED-9 | Wrong base (set to host lib address / env hint) or non-frozen engine SHA | engine-identity/base-lock check ⇒ no guest classification; verdict `ABSENT`; no marker | **At risk**: no identity check; base adopts caller value |
| RED-10 | Synthetic guest-tagged render manufactured under any opt-in, callback leg absent | `seeded_state!=0` and/or prov absent ⇒ marker WITHHELD; diagnostic line only | Holds per decision 013/038; must not be relaxed for frame_* |
| RED-11 | `sober_frame_synthetic_tag_reset()` after a host swap | `g_seen`/`tagged_swaps` remain sticky; run still disqualified | **At risk**: public reset clears both (§2 FP-6) |
| RED-12 | `frame_evidence_selftest()` invoked after real base config | no change to `s_base`/counters/verdict; no artifact green | **At risk**: selftest writes/restores `s_base` (FP-14) |
| RED-13 | Test/CI parity: `tests/first-frame.sh` PASS (ancestry), `tests/app-shell.sh` RED, `ctest 2/2`, `ldd` libc-only, engine SHA unchanged, zero diagnostic marker in ctest/CMake/verify-native | all hold; no new diagnostic script gating CI | Holds; re-run after every integration step |
| RED-14 | Positive path simulated with synthetic seed (`SOBER_EVIDENCE_*`) producing "GUEST-TAGGED" line | marker still WITHHELD; `marker withheld: diagnostic guest-state seed active` present; no `app shell rendered` | Must be enforced by FC-11/FC-12 |
| RED-15 | `frame_thread` drives ≥2 redraws through a host-supplied `nativeData` (guest callback/window slots), producing stable guest RA + draws + window; callback leg absent | stable RA is **not** authorship: verdict not `GUEST-TAGGED` as proof; `sober_frame_thread_should_withhold()` sticky=1; marker WITHHELD | **At risk**: stable-RA defence alone would pass §1.6 |

**Positive path remains captive to Leg C.** Even if every FC above is implemented
and a future clean run emits
`[render] guest render verdict: … -> GUEST-TAGGED`, that line is **necessary but
not sufficient**. The marker still requires a notifier-driven
`gameLoadedCallback(J)V` (notifier `0x27eaf6e`, `RSI=placeId>0`, cached class
`0x72a2f48`/method `0x72a2fc0`, RA pair `base+0x2584960` over
`base+0x27eafb3`) with `seeded_state==0` and no ALT/HONESTY lines (decision 038
§2). No such callback exists in any log consulted; the marker stays WITHHELD.

---

## 5. Relationship to decisions 013/038 — explicit non-weakening

- This audit **only narrows** the gate. It proposes no removal of `shell_cb`, the
  withhold chain, the `!shell_cb || !render_ok` early return, or the sole marker
  site. It does not promote the ALT thunk, `onAppBridgeNotification`, a name
  counter, a phase-3 test frame, or any diagnostic line to proof.
- `frame_synthetic_tag`'s header claim that its kill switch "must NOT be added to
  the seeded_state withhold chain" is **rejected** as written: a knob that can
  disable a guest-evidence discriminator is a suppression knob under T4-4 §4.6
  and must either fail closed (FC-9a) or join the chain (FC-9b). This is a
  tightening, not a relaxation.
- `frame_evidence`'s `fromSurface`-optional wording (MODULE_INTEGRATION §2) is
  acceptable **only** with FC-7 (guest-RA correlation to the same window pointer);
  a bare non-NULL global is a false-green seed and is forbidden (FP-9).
- The mixed-evidence semantics (MODULE_INTEGRATION §2, T3-5 §3.5) must be extended
  from swaps-only to all event kinds (FC-8). Again: tightening only.

---

## 6. Residual risks / open questions for the lead

1. The exact guest frame-loop call site of `call [vtable+0x18]` is not statically
   pinned (~11,600 `call *0x18(%r..)` sites, C04 §2.3). FC-3's RA-shape check must
   be discovered from a *captured real* guest-tagged swap and confirmed offline;
   until then, no positive render predicate can be trusted.
2. Whether the app-shell frame consumes global `0x72a3308` or
   `nativeData+0x140` remains UNKNOWN (P8 §7). FC-7 should map/attest both before
   declaring a window bound.
3. `sober_render_phase3_done()` is set by our own native swap; it is ancestry
   only. Any design that lets it alone drive `render_ok` reverts to the exact
   defect BLK-06 names.
4. The two `frame_*` modules are not in CMake or the gate. A future wiring that
   ORs their positive output with the live bare counters would create a mixed
   evidence source; FC-10 forbids this.
5. Audit scope: D09 parses only `guest_app_shell.c`; the new `SOBER_FRAME_*`
   envs must be included before claiming "0 unclassified gaps".

---

## 7. Evidence index (read-only)

`src/jni/frame_evidence.c/.h` (F7), `src/jni/frame_synthetic_tag.c/.h` (F8),
`src/jni/frame_thread.c/.h` (F6),
`src/jni/render_guest.c/.h`, `src/render/native_render.c`,
`src/linux/guest_render.c`, `src/linux/guest_app_shell.c:1750-1810`,
`src/jni/provenance_gate.h`, `src/jni/callback_provenance.c`,
`analysis/MODULE_INTEGRATION.md §2`, `analysis/T3_5_render-evidence.md`,
`analysis/T4_4_gate-tests.md`, `analysis/R1_zero_tagged_swaps.md`,
`analysis/R2_got_hook_reachability.md`, `analysis/R6_swap_provenance.md`,
`analysis/R7_anativewindow_binding.md`, `analysis/P1_render_vtable.md`,
`analysis/P8_anw_guest_consumption.md`, `analysis/C03-appshell-frame-evidence.md`,
`analysis/C04-render-integration.md`, `analysis/C08-surface-frame.md`,
`analysis/REVIEW_A4_render.md`, `tests/app-shell.sh`, `tests/first-frame.sh`,
`docs/decisions/013-no-weak-app-shell-marker.md`,
`docs/decisions/038-strict-gate-alt-thunk-ruling.md`.

No file other than this report was created or modified. Marker withheld; no
success claimed.
