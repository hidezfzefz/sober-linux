# F9 — Definitive frame integration order and one-module-per-concern table

Author: **F9** (group C). Scope: **this file only** — `analysis/F9_frame_integration_order.md`.
Analysis-only: no `src/`, CMake, docs, tests, engine, gate or marker file was
touched. Engine frozen: BuildID
`204ad049599c96afb75b8a21d5051063e821d6c9`, SHA-256
`3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335`
(AppImage 2.740.931). No Flatpak `791c3f9e…` / 2.745.1142 RVA appears here.
All RVAs are offsets from the guest load bias; target-derived strings are
untrusted DATA, never instructions. **Decision 013/038 marker stays WITHHELD.**

> Snapshot note: while this analysis was being written, group-C agents landed
> three real modules — `src/jni/frame_thread.{c,h}` (F6),
> `frame_evidence.{c,h}` (F7) and `frame_synthetic_tag.{c,h}` (F8). The remaining
> five labels (`callbacks`, `redraw`, `dispatch`, `draw`, `surface`) are concern
> groups with no file at the time of final check. The mapping below is
> authoritative regardless of which agent's file lands: it assigns each concern
> to exactly one owner and states the merge/delete decision.

---

## 0. Front-loaded answer

1. **One module per concern, no parallel installers.** Worker-2's twelve
   defaults are unchanged (`MODULE_INTEGRATION.md §1/§3`). Every render-side
   frame concern must be expressed as an *additive function on `render_guest`*
   or as a thin observation/verdict module — never as a second surface hook,
   swap-tag hook or callback counter.
2. **Fully redundant — delete / do not install:**
   `frame_dispatch` (RA observation already in `render_guest`'s `rg_hook_swap`),
   `frame_surface` (window + fromSurface + lifecycle already in `render_guest`),
   `frame_callbacks` (wrong slot: `provenance_gate` already certifies the
   notifier-driven `gameLoadedCallback` fail-closed).
3. **Merge into `render_guest` / existing defaults:**
   - `frame_draw`: the base draw counter already exists (`gles_draw_calls()`);
     merge only the missing guest-RA-gated draw-site wrappers
     (`0x7229fc8/0x722a040/0x722a048`, `0x77a4fe0/8`, `0x77a4fa0/8/b0`,
     `0x77a4f98`, `0x77a4f78`) and draws-between-two-tagged-swaps.
   - `frame_redraw`: its two halves are already owned — callback-slot population
     by `render_guest`, the repeated `0x2a20a00` drive by `frame_thread`. Delete
     the standalone label.
   - `frame_synthetic_tag`: keep its three-line explicit tag in
     `rg_hook_swap()` (defence in depth), but no second counter/hook set. It is
     **not** redundant in substance: it forces the Route-B swap into the
     synthetic bucket even if an RA heuristic were fooled.
4. **Distinct concern — keep, but wire correctly:**
   - `frame_thread` (F6) is the **render-loop thread driver**, distinct from
     `scheduler_guest`'s nesting prime (`FunctionMarshaller`) and from
     `tsd_default`'s TLS substrate. It must be started only in the withheld
     drive phase, and `sober_frame_thread_should_withhold()` plus
     `SOBER_FRAME_THREAD` must join the chain.
   - `frame_evidence` (F7) is the **sole verdict owner**; its
     `guest_render_ok()` is exactly `guest_tagged_render_ok()` in
     `MODULE_INTEGRATION.md §2`. Do **not** create `gate_render_evidence` as a
     second verdict module; `frame_evidence` already occupies that slot.
5. **Boot order does not change.** Worker-2's twelve defaults stay in place.
   The frame concerns attach at three fixed points: **install** at the phase-3
   GOT-hook site (`guest_render.c:458–465`) and the Route-B call
   (`guest_render.c:354`), **drive** (withheld) after placeId/publish
   (`guest_app_shell.c:1538`), **verdict** at the gate
   (`guest_app_shell.c:1777–1780`).
6. **Withhold chain:** add `SOBER_FRAME_THREAD` +
   `sober_frame_thread_should_withhold()`, and the render-evidence driver knobs
   (`SOBER_RENDER_FRAME_EVIDENCE`, `SOBER_EVIDENCE_SURFACE`,
   `SOBER_EVIDENCE_DEVICE`, `SOBER_EVIDENCE_REDRAW`,
   `SOBER_RENDER_SUPPRESS_SYNTHETIC`). Do **not** withhold the fail-safe kill
   switches `SOBER_FRAME_EVIDENCE=0`, `SOBER_FRAME_SYNTH_TAG=0`, nor
   `SOBER_PROVENANCE_GATE=0`, marker paths, or `SOBER_RENDER`/`SOBER_APP_SHELL`.

---

## 1. Authority and rules applied

- `analysis/MODULE_INTEGRATION.md §1` is authoritative for "one module per
  bridge slot": `render_guest` owns "guest render / surface"; `provenance_gate`
  owns positive callback provenance; `scheduler_guest` owns scheduler nesting;
  `publish_default` owns MessageBus publish; etc. Retired modules stay for A/B
  but are not installed by default.
- `MODULE_INTEGRATION.md §2`: `render_ok = sober_render_phase3_done() &&
  guest_tagged_render_ok()`. Phase-3 is ancestry only; a synthetic first-frame
  swap is never sufficient; mixed synthetic/guest evidence fails closed.
- `MODULE_INTEGRATION.md §3`: the twelve-default boot order; this document only
  extends it with frame attach points.
- `T3_5_render-evidence.md §3–§6`, `R1_zero_tagged_swaps.md §5–§6`,
  `P8_anw_guest_consumption.md §6`: RVAs and withhold knobs for the attach
  points. `D02-withhold-audit.md §5–§6`: marker paths and legitimate gates stay
  out of `seeded_state`.
- Every authoritative module self-gates; lead wiring calls it unconditionally.
  No guest `.text` byte is patched. The only writes are the accepted GOT
  JUMP_SLOT swaps, GLES data-table wrappers and the accepted window global
  `0x72a3308`/generation `0x72a3310`. Anything that *drives* or *suppresses*
  guest state is a withhold entry; anything that merely *observes* and fails
  closed is not.

---

## 2. Definitive boot order (worker-2 defaults + frame attach points)

`#` = worker-2 default; `[F]` = frame concern attach point. Line numbers are
from `src/linux/guest_app_shell.c` unless noted.

| # | Phase / module | Exact wiring point | Frame concern served |
| --- | --- | --- | --- |
| 0 | *pre-stage*: phase-3 native render + `first frame rendered` | `guest_render.c:440–477`; marker at `:477` | ancestry only (`phase3_done`) |
| 1 | `tsd_default_install()` | `:974` | **thread** substrate (TLS key 3 / `TLS+0x410`) |
| 2 | `javavm_default_install(j, base)` | `:976` | JavaVM attach site |
| 3 | `provenance_gate_install(base)` | `:978` | **callbacks** (positive credential) |
| 4 | `fflag_default_replay(base)` → `controller_default_ensure_flag(base)` | `:983`, `:990–991` | — |
| 5 | `scheduler_guest_install(base)` | `:997`; nesting drive `:1398` `on_attach("FunctionMarshaller")` | worker nesting (distinct from frame thread) |
| 6 | `singleton_default_install(base)` | `:1002` | — |
| **[F-a]** | **`render_guest` install (GOT hooks + window + lifecycle object)** | `guest_render.c:458–465` (`gr_install_render_contract`, before direct `0x649d422` at `:469`); reused/no-op at `guest_app_shell.c:1009–1027` | **surface**, **synthetic tag** (base RA tag), **draw** (base counters), **evidence** raw counters |
| **[F-a2]** | **`frame_synthetic_tag` scope around the one Route-B call** | `guest_render.c:354`: `sober_frame_synthetic_tag_enter();` → `guest_swap(...)` → `sober_frame_synthetic_tag_leave();`; tag check inside `rg_hook_swap()` before `rg_in_guest()` | **synthetic tag** (explicit host scope) |
| **[F-a3]** | **`frame_evidence` observers** | `frame_evidence_set_guest_base(base)` after mapping; `record_swap(ra)` in `rg_hook_swap()`; `record_draw(ra)` in draw wrappers; `observe_window_global()` after the window path | **evidence** (immutable records + verdict) |
| 7 | crashpad flags latch `0x7cdd149` | `:1034–1053` | — |
| 8 | `app488_default_install_from_wrapper()` → `delegate_default_install_from_wrapper()` → `controller_default_install()` | `:1381`, `:1383`, `:1385` | — |
| 9 | `placeid_default_install(j, base)` → `StartGameWithParam 0x2ebd51a` → `publish_default_publish_seeded(base)` | `:1527`, `:1530`, `:1537` | **callbacks** drive (notifier registration `0x24abf96 → 0x24ad699 → 0x253cb56(1,&0x7234520)`) |
| **[F-b]** | **frame drive — withheld**: `frame_thread` (render-loop thread) + redraw driver | after `:1538` (and after `0x2773194` UpdateSurface at `:1498` when used): `sober_frame_thread_install(&cfg)` / `_start()`; only with genuine `nativeData+0x98/+0x140` | **thread**, **redraw**, **dispatch** (guest frame loop), residual **draw** |
| 10 | **gate verdict**: `render_ok = phase3_done && guest_render_ok()` | `:1777–1780` → `guest_render_ok()` from `frame_evidence` | **evidence** (sole decisive owner) |
| 11 | marker emit (sole site, unchanged) | `:1807` | — |

### 2.1 Why install is early and drive/verdict are late

`render_guest.h:97–101` requires the GOT hooks before the harness's direct
`0x649d422` call so all GLES procs load through the hooked `eglGetProcAddress`
GOT `0x722a180`; `guest_render.c:458–465` does exactly that, and
`guest_app_shell.c:1022–1023` deliberately does not re-run (avoids the
`0x649c21e` re-entry fault). `MODULE_INTEGRATION.md §3` step 7's "render last"
therefore means *render evidence is read last*, not that hooks are installed
last. The guest frame drive needs the app object, delegate, controller, placeId
and publication live (`:1381–1538`) plus a genuine `nativeData`; the first safe
point is `[F-b]`. Evidence is read at `:1777–1780` after shutdown from
process-global atomics (`T3_5 §1`), so the verdict survives
`sober_render_get_active()` going NULL.

---

## 3. One-module-per-concern table (the eight `frame_*` concerns)

| Concern | Authoritative owner (definitive) | Redundant with `render_guest`? | Required action | Kill switch / withhold knob | API the lead reads |
| --- | --- | --- | --- | --- | --- |
| **callbacks** (`frame_callbacks`) | `provenance_gate` (default-ON); `notifier_probe` (withheld diagnostic) | **No** — different slot | **DELETE label.** Do not re-count names. `provenance_gate` certifies the exact notifier chain: RA pair `base+0x2584960`/`base+0x27eafb3`, class `0x72a2f48`, method `0x72a2fc0`, placeId>0. `notifier_probe` (patches guest bytes) stays default-OFF + forced withheld. | `SOBER_PROVENANCE_GATE=0` = fail-closed kill switch, **NOT** withhold; `SOBER_NOTIFIER_PROBE` + `..._should_withhold()` = withheld | `sober_provenance_gate_is_genuine("gameLoadedCallback")`, `..._is_harness_thunk()` |
| **redraw** (`frame_redraw`) | `render_guest` (slot population) + `frame_thread` (drive) | **Partly** — `rg_bind_surface()` already drives `0x2a20a00` with NULL slots | **DELETE label.** Populate `nativeData+0x88/+0x98/+0xa0` with genuine guest pointers in `render_guest`; drive `0x2a20a00` via `frame_thread`. | `SOBER_EVIDENCE_REDRAW` / `SOBER_FRAME_THREAD` — withheld | `sober_render_guest_lifecycle_calls(2)`, `sober_frame_thread_frames()` |
| **dispatch** (`frame_dispatch`) | `render_guest` `rg_hook_swap` RA tag + stable-RA discriminator | **Yes** — fully | **DELETE.** The guest `call [vtable+0x18]` (`[0x7191300]=0x649d422`) is observed by the GOT `0x722a1a8` hook; a guest RA yields `swaps_guest_tagged`. The loop is guest code we can only observe. | none of its own; inherits `SOBER_RENDER_GUEST` | `sober_render_guest_swaps_guest_tagged()`, `..._swaps_synthetic()` |
| **draw** (`frame_draw`) | `render_guest` (extend draw-site wrappers) | **Mostly** — `0x722a180` wraps 5 names; `gles_draw_calls()` exists | **MERGE residual** (guest-RA forwarding wrappers over the 3 GOTs + 7 extension slots + draws-between). Delete standalone module. If separated at all, rename `render_draw_observer` and remove draw counting from `render_guest` — one owner only. | `SOBER_RENDER_FRAME_EVIDENCE` / `SOBER_EVIDENCE_DEVICE` — withheld | `sober_render_guest_gles_draw_calls()`, `frame_evidence_draws()` |
| **surface** (`frame_surface`) | `render_guest` | **Yes** — fully | **DELETE.** `render_guest` owns the shaped window, `0x72a3308`/`0x72a3310`, fromSurface `0x72294a8`, four lifecycle thunks. Optional five-GOT hook extension (`0x72294a8/0x72294a0/0x72297c0/0x72297c8/0x72297d0`) belongs inside `render_guest` under `SOBER_EVIDENCE_SURFACE` (`P8 §6 item 3`). | `SOBER_EVIDENCE_SURFACE` — withheld | `sober_render_guest_from_surface_calls()`, `sober_render_guest_window()` |
| **thread** (`frame_thread`, F6) | **`frame_thread`** (render-loop driver) + `scheduler_guest` (nesting) + `tsd_default` (TLS) | **No** — distinct from `render_guest` and from scheduler nesting | **KEEP the F6 module**, but wire only in `[F-b]` with genuine `nativeData+0x98/+0x140`; it calls the guest `0x2a20a00` thunk, never the host Route-B call. Add its withhold entries. Do **not** merge into `scheduler_guest` — that module's `on_attach` is the `FunctionMarshaller` nesting prime, a different concern. | `SOBER_FRAME_THREAD` + `sober_frame_thread_should_withhold()` — **withheld**; kill switch is not fail-closed, so it must be in the chain | `sober_frame_thread_frames()`, `_skips()`, `_verify()`, `_stats()` |
| **evidence** (`frame_evidence`, F7) | **`frame_evidence`** (sole verdict owner) | **No** — verdict, not counters | **KEEP the F7 module; do not create a second verdict module.** Its `guest_render_ok()` is `guest_tagged_render_ok()`. It records immutable, RA-tagged events and fails closed on mixed evidence; it reads no `SOBER_*` in the proof path and uses a weak `sober_render_phase3_done()` for ancestry. | `SOBER_FRAME_EVIDENCE=0` = fail-closed kill switch, **NOT** withhold (`frame_evidence_enabled()`); the *driver* knob `SOBER_RENDER_FRAME_EVIDENCE` is withheld | `guest_render_ok()`, `frame_evidence_verdict()` / `_verdict_string()`, `frame_evidence_swaps_guest()`, `_draws()`, `_window_bound()`, `_mixed()` |
| **synthetic tag** (`frame_synthetic_tag`, F8) | `render_guest` RA tag + F8 explicit host-scope tag | **Not redundant in substance** — additive defence in depth | **KEEP the three-line F8 tag** at `guest_render.c:354` + `rg_hook_swap()` before the `rg_in_guest()` branch: force the Route-B swap into the synthetic bucket regardless of RA. No second counter, no suppression of the swap. | `SOBER_FRAME_SYNTH_TAG=0` = fail-safe (existing RA classifier still tags it synthetic), **NOT** withhold | `sober_frame_synthetic_tag_active()`, `_seen()`, `_tagged_swaps()` |

### 3.1 Consolidation summary

- **Delete / never install:** `frame_callbacks`, `frame_dispatch`, `frame_surface`,
  `frame_redraw` (its work is split between `render_guest` and `frame_thread`).
- **Merge into `render_guest`:** `frame_draw` residual wrappers; keep the F8
  explicit tag as a few additive lines in `rg_hook_swap()`.
- **Keep as distinct modules:** `frame_thread` (F6, withheld driver) and
  `frame_evidence` (F7, sole verdict owner).
- **Net new source files:** the three already landed (`frame_thread`,
  `frame_evidence`, `frame_synthetic_tag`). No second surface hook, no second
  callback counter, no second verdict module.

---

## 4. Redundancy findings in detail (with the RVAs that settle them)

### 4.1 `frame_surface` — redundant (delete)
`render_guest.h:52–76` already builds/publishes the shaped window and drives the
four thunks. `P8 §3` proves the guest never dereferences a window vtable: every
operation is one of five PLT/JUMP_SLOT GOTs. A new NDK-vtable module would be
inert and, if it changed the refcount layout, would corrupt the compat shim at
`0x26fea9b`. Only the all-five-GOT extension is legitimate, inside
`render_guest`.

### 4.2 `frame_synthetic_tag` — additive, not a duplicate (keep the 3 lines)
`render_guest.c:255–264` already classifies by RA (`rg_in_guest`, `:181–185`);
`R1 §1` confirms the sole observed swap was correctly tagged synthetic because
its caller was the host (`guest_render.c:354`). F8 does **not** duplicate that
classification: it scopes the known host call and forces the synthetic bucket if
the RA heuristic were ever fooled. Its kill switch is fail-safe (the RA
classifier still works), so `SOBER_FRAME_SYNTH_TAG` must **not** be withheld.
Wiring: `enter()`/`leave()` around the unchanged `guest_swap()` call, and the
`active()` check before `if (rg_in_guest(ra))` in `rg_hook_swap()`. No counter is
suppressed, so `tests/first-frame.sh` keeps its semantics.

### 4.3 `frame_dispatch` — redundant (delete)
The guest dispatch is `call QWORD PTR [vtable+0x18]` with
`[0x71912e8+0x18] = [0x7191300] = 0x649d422` (`T3_5 §2.1`, `R1 §5`). The loop
site cannot be installed by us (~11,600 `call *0x18(%r..)` candidates, `C04
§2.3`); it is discovered by RA capture. The stable-RA discriminator belongs in
`frame_evidence`/`render_guest`, not a new driver.

### 4.4 `frame_draw` — mostly redundant (merge residual)
`render_guest.c:110–150` wraps 5 loaded names and exposes `gles_draw_calls()`,
but the trampoline counter is **not RA-gated** and the imported GLES JUMP_SLOTs /
extension table are unwrapped. `R1 §4` explains `draw_wrapped=5` yet
`draw_calls=0`: no guest frame loop. Add those sites with RA gating to
`render_guest`; never count the host `dlsym`'d `glClear`
(`native_render.c:817–830`). One owner for draw evidence.

### 4.5 `frame_redraw` — split between two owners (delete label)
`rg_bind_surface()` drives `0x2a20a00` but leaves `nativeData+0x88/+0x98/+0xa0`
NULL (`render_guest.c:402–404`), so the guest skips `0x2a20a11/0x2a20a20`.
Populating those slots with genuine guest pointers is a `render_guest`
extension; *repeatedly* driving the thunk is exactly what F6 `frame_thread`
does. A standalone `frame_redraw` would duplicate both.

### 4.6 `frame_callbacks` — wrong slot (delete, fold into `provenance_gate`)
The only qualifying callback is the notifier-driven `gameLoadedCallback(J)V`
(notifier `0x27eaf6e`, RSI=placeId, class `0x72a2f48`, method `0x72a2fc0`, thunk
`0x25848e0`, return `0x27eafb3`, registration `0x24abf96 → 0x24ad699 →
0x253cb56(1,&0x7234520)`). `provenance_gate` already certifies this RA pair
fail-closed; `notifier_probe` is the byte-patching withheld diagnostic. A new
name-counting module is exactly what decision 038 rejects as proof.

### 4.7 `frame_thread` — distinct from scheduler nesting (keep, wire late)
F6's module starts a dedicated thread that calls the guest `0x2a20a00` thunk
with a caller-supplied `nativeData`; it never calls the host Route-B `0x649d422`
and never installs a host function pointer in a guest slot. This is the
**render-loop driver** concern. `scheduler_guest` instead primes `obj+0x70` via
guest `0x2a132c0` on `FunctionMarshaller` attach (`:1398`) to avoid the
`257f718-Context,20` underflow; `tsd_default` supplies key-3 backing. These are
three different concerns — do not merge them. Because driving a guest frame is
seeded state, the drive is withheld and inert unless `nativeData+0x98/+0x140`
are genuine guest values (`sober_frame_thread_verify()`).

### 4.8 `frame_evidence` — the single verdict owner (keep)
F7 implements the contract in `MODULE_INTEGRATION.md §2` directly:
`guest_render_ok() == phase3_done && swaps_guest >= 1 && draws >= 1 &&
window_bound`, with immutable RA-tagged event records, guest-draw/window
classification (`0x722a1a8`, `0x72a3308`, `0x26fea78`,
`0x2a2087c/0x2a20907`) and fail-closed mixed-evidence tainting. It reads no
`SOBER_*` in the proof path and its kill switch (`SOBER_FRAME_EVIDENCE=0`) is
fail-closed. This occupies the `gate_render_evidence` role; a second verdict
module would create two disagreeing predicates. It must not be renamed into a
second file — the lead simply calls `guest_render_ok()` at the gate.

---

## 5. Exact wiring points for the lead

### 5.1 `[F-a]` — `guest_render.c:458–465`
No change to the existing hook install. Additively: `frame_evidence_set_guest_base(base)`
once; call `frame_evidence_record_swap((uintptr_t)__builtin_return_address(0))`
inside `rg_hook_swap()`; call `frame_evidence_record_draw(ra)` from each counted
draw wrapper; call `frame_evidence_observe_window_global(base, ra)` after the
window path. Do not add a second surface/swap hook.

### 5.2 `[F-a2]` — `guest_render.c:354` and `rg_hook_swap()`
```c
#include "frame_synthetic_tag.h"
sober_frame_synthetic_tag_enter();
long src = guest_swap((uintptr_t)ctx);   /* unchanged 0x649d422 call */
sober_frame_synthetic_tag_leave();
```
In `rg_hook_swap()`, before `if (rg_in_guest(ra))`:
```c
if (sober_frame_synthetic_tag_active()) {
    sober_frame_synthetic_tag_note_swap();
    rg_bump(&s_swaps_synthetic);          /* force synthetic, never guest */
} else if (rg_in_guest(ra)) { ... }
```

### 5.3 `[F-b]` — after `guest_app_shell.c:1538` (withheld)
```c
#include "frame_thread.h"
struct sober_frame_thread_config cfg;
memset(&cfg, 0, sizeof(cfg));
cfg.guest_base = (void *)base;
cfg.native_data = nativeData;      /* guest object with +0x98/+0x140 genuine */
cfg.jni_env = env; cfg.jni_class = app_class;
cfg.interval_us = 16667; cfg.prime_on_start = 1;
if (getenv("SOBER_FRAME_THREAD") || getenv("SOBER_EVIDENCE_REDRAW")) {
    sober_frame_thread_install(&cfg);
    sober_frame_thread_start();
    fprintf(stderr, "[app-shell] frame thread -> %llu frames\n",
            (unsigned long long)sober_frame_thread_frames());
}
```
Start only here: the redraw slots depend on the app/delegate/controller/placeId
path (`:1381–1538`) and on the surface path (`:1498`). Never start it at `[F-a]`.

### 5.4 `[F-c]` — gate (`guest_app_shell.c:1777–1780`)
```c
#include "frame_evidence.h"
/* render_ok = phase3_done && guest_tagged_render_ok() (MODULE_INTEGRATION §2) */
int render_ok = sober_render_phase3_done() && guest_render_ok();
fprintf(stderr, "%s\n", frame_evidence_verdict_string(buf, sizeof(buf)));
/* frame_evidence_verdict() is the numeric code; _verdict_name() names it. */
```
`guest_render_ok()` already requires ≥1 guest-tagged swap, ≥1 guest draw, a
stable window binding, and no mixed evidence; keep those conjuncts there and do
not duplicate them in `guest_app_shell.c`. The sole `emit_marker` (`:1807`) and
`!shell_cb || !render_ok` early return (`:1782`) are unchanged. Callback leg
stays `sober_provenance_gate_is_genuine("gameLoadedCallback")` with the harness-
thunk disqualifier (`:978`); thread nesting stays at `scheduler_guest_on_attach`
(`:1398`).

---

## 6. Withhold-chain additions (exact)

At `guest_app_shell.c`, extend the `seeded_state` OR-chain in the task-#477
block (near `SOBER_RENDER_GUEST` at `:1745–1747`) and the module-verdict block
(`:1759–1763`):

```c
                        /* F9 frame concerns: drivers that drive or suppress
                         * guest state make the run harness-tuned. */
                        getenv("SOBER_RENDER_FRAME_EVIDENCE") ||
                        getenv("SOBER_EVIDENCE_SURFACE") ||
                        getenv("SOBER_EVIDENCE_DEVICE") ||
                        getenv("SOBER_EVIDENCE_REDRAW") ||
                        getenv("SOBER_RENDER_SUPPRESS_SYNTHETIC") ||
                        getenv("SOBER_FRAME_THREAD") ||
                        ...
                        sober_frame_thread_should_withhold();
```

Canonical knobs (unify the `C04 §5` / `T3_5 §6` spellings; keep any alias in the
chain too):

| Knob | Concern / action | Withhold? |
| --- | --- | --- |
| `SOBER_RENDER_FRAME_EVIDENCE` | E1: RA-split swap tag / diagnostic draw counters (note: **not** the module kill switch) | **Yes** |
| `SOBER_EVIDENCE_SURFACE` | E3: five-GOT window bind + lifecycle drive | **Yes** |
| `SOBER_EVIDENCE_DEVICE` | E4: `0x649c21e(window, pbuffer=0)` device path | **Yes** |
| `SOBER_EVIDENCE_REDRAW` | E4: drive `0x2a20a00` → `[+0x98]` | **Yes** |
| `SOBER_RENDER_SUPPRESS_SYNTHETIC` | E5: skip Route-B host swap with `[ev-suppress]` | **Yes** |
| `SOBER_FRAME_THREAD` | F6 render-loop thread driver | **Yes** + `sober_frame_thread_should_withhold()` |
| `SOBER_EVIDENCE_FRAME` | `C04 E5` "fire the gate callback" diagnostic, if retained | **Yes**, diagnostic only |
| `SOBER_FRAME_EVIDENCE=0` | F7 evidence module kill switch | **No** — fail-closed; disabling cannot make a green |
| `SOBER_FRAME_SYNTH_TAG=0` | F8 synthetic-tag kill switch | **No** — fail-safe; RA classifier still tags Route-B synthetic |
| `SOBER_PROVENANCE_GATE=0` | callback credential kill switch | **No** — fail-closed |
| `SOBER_SHELL_MARKER` / `SOBER_FRAME_MARKER` / `SOBER_INIT_MARKER` | marker output paths | **No** (`D02 §5`) |
| `SOBER_RENDER` / `SOBER_APP_SHELL` | legitimate real-path enables | **No** (`D02 §6`) |

Note the near-name collision: `SOBER_RENDER_FRAME_EVIDENCE` (withheld E1 driver)
and `SOBER_FRAME_EVIDENCE` (fail-safe module kill switch) are different knobs;
the lead must add only the former to `seeded_state`.

---

## 7. Honesty invariants (unchanged)

- No render-side change lifts the marker. Every driver in §6 is synthetic seed
  state; `marker withheld: diagnostic guest-state seed active` must keep firing
  whenever any of them is set.
- Guest-tagged and synthetic evidence in the same run ⇒ fail closed. F7's
  `frame_evidence_mixed()` / F8's tag enforce this; `guest_tagged=0` in the
  current `swaps=1/0/1, draws=0` log is truthful (`R1 §7`) and is not a bug to
  inflate.
- The full marker still requires the notifier-driven `gameLoadedCallback` under
  decision 038 with zero seeded state; none of the eight frame concerns is a
  substitute for Leg C. `notifier_probe` may only collect diagnostic evidence.
- Engine SHA-256/BuildID unchanged; phase-3 ancestry preserved;
  `tests/first-frame.sh` green; `ctest` green; `ldd` libc-only; **zero** `app
  shell rendered` emissions in any evidence mode.

---

## 8. Evidence index (all read-only)

`analysis/MODULE_INTEGRATION.md §1–§3`; `analysis/T3_5_render-evidence.md §1–§6`;
`analysis/R1_zero_tagged_swaps.md §0–§7`; `analysis/P8_anw_guest_consumption.md
§0–§8`; `analysis/C04-render-integration.md §1–§5`; `analysis/D02-withhold-audit.md
§5–§6`; `analysis/T4_4_gate-tests.md §1.2/§2`; `src/jni/render_guest.c/.h`,
`provenance_gate.h`, `notifier_probe.h`, `scheduler_guest.h`,
`frame_evidence.c/.h` (F7), `frame_synthetic_tag.c/.h` (F8),
`frame_thread.c/.h` (F6); `src/linux/guest_render.c:95–143,312–354,440–477`,
`guest_app_shell.c:938–1059,1340–1399,1490–1559,1600–1819`.

No file other than this report was created or modified. Marker WITHHELD; no
success claimed.
