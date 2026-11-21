# G9 — Module conflict audit: `frame_*` (F1–F8) vs `render_guest` / `native_render` / `guest_render`

Author: **G9** (group D). Scope: **this file only** —
`analysis/G9_module_conflict_audit.md`. Analysis-only: no `src/`, CMake, build,
gate, test, doc, engine or git file was touched. Engine frozen: BuildID
`204ad049599c96afb75b8a21d5051063e821d6c9`, SHA-256
`3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335`
(AppImage 2.740.931). No Flatpak `791c3f9e` RVA is used. All RVAs are offsets
from the guest load bias; target-derived bytes/strings are untrusted DATA.
**Decision 013/038 marker stays WITHHELD; no success claim.**

> **Snapshot caveat.** The frame sources are being written by other agents while
> this audit runs. At the final snapshot the tree contains `frame_redraw.{c,h}`
> (F2), `frame_dispatch.{c,h}` (F3), `frame_draw.{c,h}` (F4),
> `frame_surface.{c,h}` (F5), `frame_thread.{c,h}` (F6), `frame_evidence.{c,h}`
> (F7) and `frame_synthetic_tag.{c,h}` (F8); only `frame_callbacks` (F1) remains
> a **concern label** (`analysis/F9_frame_integration_order.md §3`). Every
> shipped frame file is untracked and **not in any CMake target** (§5). Several
> files changed mid-snapshot (`frame_evidence.h` now reads
> `swaps_guest >= 2 && stable_ra`; F2/F4/F5 `.c` files arrived after
> F3/F6/F7/F8 were already inventoried), so the lead should re-check exact
> thresholds and any late-arriving `frame_*.c` before applying §4. The conflict
> structure below is stable across those edits.

---

## 0. Front-loaded verdict

1. **There is no single owner for the swap event.** Five counter families and
   five RA classifiers describe the *same* physical GOT `0x722a1a8` swap:
   `render_guest` (`rg_hook_swap`/`rg_in_guest`), `native_render`
   (`sr_hook_egl_swap_buffers`), `frame_dispatch` (`fd_on_swap`/`fd_in_guest`),
   `frame_redraw` (F2, chains the same GOT + its own guest-RA tag), and
   `frame_evidence` (`frame_evidence_record_swap`/`fe_ra_tag`), plus the
   `frame_synthetic_tag` `active()` override. Wiring these as their headers
   propose yields **one swap → 4–5 increments** and a hook chain of uncertain
   order — a structural false-green path (`F10 §1.1/§1.2`).
2. **`native_render` and `render_guest` are already duplicate owners today.**
   They install the same three GOT slots (`0x72294a8`, `0x722a1a8`, `0x722a180`)
   and expose parallel counter APIs. Mutual exclusion is a *runtime conditional*
   in `guest_render.c:335-345` (`s_gr.present`), not a build-time guarantee;
   `guest_render.c:272-277` even sums both families.
3. **The gate verdict has three competing owners:** the live inline conjunction
   at `guest_app_shell.c:1777-1780`, `frame_evidence.guest_render_ok()`, and the
   `sober_gate_render_guest_tagged_ok()` rename in `F9 §5.3` (with a third spec
   in `G1 §2`). Only one may exist.
4. **All shipped frame modules are DEFAULT-ON and none of their knobs are in
   the `seeded_state` withhold chain** (`guest_app_shell.c:1746` has only
   `SOBER_RENDER_GUEST`). `F2`/`F3`/`F4`/`F5`/`F6` are state-changing drivers,
   not passive observers; default-ON + unwired-withhold violates `F9 §6`. F2
   chains **four GOT hooks** and seeds nativeData callback slots with guest
   RVAs; F4 adds three draw GOT writers and drives `0x6496d0c`; F5 adds a GOT
   writer at `0x722a178` and rewrites ctx/nativeData fields `render_guest`
   owns.
5. **Kill-switch syntax is inconsistent:** `SOBER_FRAME_REDRAW`,
   `SOBER_FRAME_EVIDENCE`, `SOBER_FRAME_THREAD`, `SOBER_FRAME_SURFACE`,
   `SOBER_FRAME_DRAW` accept only exact `"0"`; `SOBER_FRAME_SYNTH_TAG` and
   `SOBER_FRAME_DISPATCH` also accept `off/no/false` (case-insensitive). None
   match `F9`'s canonical `SOBER_RENDER_FRAME_EVIDENCE` / `SOBER_EVIDENCE_*` /
   `SOBER_RENDER_SUPPRESS_SYNTHETIC` names.
6. **Build status: they do not build at all.** Zero `frame_*` entries in
   `src/jni/CMakeLists.txt`; adding them wholesale would introduce a generic
   global symbol (`guest_render_ok()`), a `pthread` dependency into the
   `jni_probe` target, and an unresolved-weak-symbol behaviour change (§5).
7. **Recommended end state:** one render-evidence owner (`render_guest`) for
   all hooks/counters/writes; one thin verdict module (`gate_render_evidence`,
   formerly `frame_evidence`) that *reads* those counters; every driver
   opt-in + withhold-listed. Delete/merge F1–F6, F8 (§4).

---

## 1. Inventory at snapshot

| ID | Concern (`F9 §3`) | Source at snapshot | Kill env | Role claimed | Reality |
| --- | --- | --- | --- | --- | --- |
| F1 | `frame_callbacks` | none | — | — | Wrong slot: `provenance_gate` already certifies GENUINE RA pair; `native_jni.c` counts names. Retire. |
| F2 | `frame_redraw` | `src/jni/frame_redraw.{c,h}` (F2) | `SOBER_FRAME_REDRAW` | "guest redraw driver" | Seeds genuine guest callback RVAs `0x2a23d40/0x2a23dc0/0x2a23e70/0x2a23f10` into nativeData `+0x88/+0x90/+0x98/+0xa0`, writes `+0x140`, drives `0x2a20a00`, and chains **four GOT observation hooks** (`0x722a1a8` + three draw slots). This is the genuine fix F9 assigns to `render_guest` as one withheld `drive_redraw()`. Retire as module; merge. |
| F3 | `frame_dispatch` | `src/jni/frame_dispatch.{c,h}` (F3) | `SOBER_FRAME_DISPATCH` | "makes the guest dispatch" | **Installs a 4th GOT `0x722a1a8` observer**, scans guest `.text` for `ff /2 ... 0x18`, attempts sites in a child. State-changing, default-ON. Retire. |
| F4 | `frame_draw` | `src/jni/frame_draw.{c,h}` (F4) | `SOBER_FRAME_DRAW` | "guest GLES draw driver + plain-draw GOT counter" | Installs **three new GOT hooks** (`0x7229fc8` glClear, `0x722a040` glDrawElements, `0x722a048` glDrawArrays), a 6th RA classifier `fd_ra_is_guest`, a new counter family `s_got_total/guest/synth[3]` + dispatch counters, and a default-ON driver `fd_drive_branch()` that executes guest `0x6496d0c` with a *synthetic* descriptor. Exactly the residual work F9 says belongs in `render_guest`. Retire as module; merge withheld. |
| F5 | `frame_surface` | `src/jni/frame_surface.{c,h}` (F5) | `SOBER_FRAME_SURFACE` | "real-EGLSurface binding / guest-published window" | Adds a GOT writer at `0x722a178` plus a 3rd writer at `0x72294a8`, writes ctx `+0x18/+0x28` and nativeData `+0x140`, and carries ~13 counters that overlap `render_guest` (`s_from_surface_calls`, `s_life[4]`). Its guest-RA publish via `0x26fea5a` is a genuine improvement over `render_guest`'s host store, but must be merged, not installed in parallel. Retire as module. |
| F6 | `frame_thread` | `src/jni/frame_thread.{c,h}` (F6) | `SOBER_FRAME_THREAD` | "render-loop thread" | Calls guest `0x2a20a00` in a `pthread` loop; `should_withhold()` is **true in default state**. Merge into `scheduler_guest` / retire. |
| F7 | `frame_evidence` | `src/jni/frame_evidence.{c,h}` (F7) | `SOBER_FRAME_EVIDENCE` | "observation-only recorder" | Duplicates all swap/draw/window counters and a 3rd RA classifier; defines generic `guest_render_ok()`. Keep only as renamed verdict module. |
| F8 | `frame_synthetic_tag` | `src/jni/frame_synthetic_tag.{c,h}` (F8) | `SOBER_FRAME_SYNTH_TAG` | "explicit host tag" | 4th classification path + `g_tagged_swaps` counter; duplicates `render_guest` `s_swaps_synthetic` and its mixed rule. Retire. |
| — | `render_guest` | `src/jni/render_guest.{c,h}` | `SOBER_RENDER_GUEST` (withhold-listed) | canonical guest render module | Live; owns 3 GOT hooks + 7 counters + window publish. **Proposed single render owner.** |
| — | `native_render` | `src/render/native_render.{c,h}` | none | native EGL/GL backend | Also owns a *second* installer for the same 3 GOT slots + parallel counters; active only via `guest_render.c` fallback when module absent. Demote to A/B fallback. |
| — | `guest_render` | `src/linux/guest_render.{c,h}` | `SOBER_RENDER_GUEST` | phase-3 driver | Chooses module vs local fallback (`dlsym` at `gr_resolve_module`), sums both counter families, performs Route-B synthetic swap at `:354`. Keep as driver only. |

---

## 2. One-owner tables per hook / counter / state

Legend: **C** = conflict (two or more live writers/classifiers), **D** = dormant
duplicate (parallel code, mutually exclusive today but unsound), **S** = single
owner already. "Owner after plan" is the recommendation of §4.

### 2.1 Hooks (writes to guest GOT / callback slots)

| Hook / write site | Current owners (with evidence) | Status | Owner after plan | Required action |
| --- | --- | --- | --- | --- |
| `eglSwapBuffers` GOT `0x722a1a8` | `render_guest.c:255-264` `rg_hook_swap` (module path); `native_render.c:1026-1043` `sr_hook_egl_swap_buffers` (installed by `guest_render.c:196` local fallback); `frame_dispatch.c:565-578` installs a chained RWX observer; `frame_evidence.h:64-65` asks for a 4th observer call at the same site | **C** | `render_guest` `rg_hook_swap` | Remove `frame_dispatch` GOT install entirely; delete native_render installer from default path (keep A/B only); have the single hook call the verdict module's *observer* once if the verdict is kept as a separate module — but never a second GOT write. |
| `eglGetProcAddress` GOT `0x722a180` | `render_guest.c:267-278` `rg_hook_get_proc`; `native_render.c:1119-1137` `sr_hook_egl_get_proc_addr` via the same fallback installer | **D** | `render_guest` | Same mutual-exclusion problem; no frame module adds a third, but any future one must be forbidden. |
| GLES draw GOTs `0x7229fc8`/`0x722a040`/`0x722a048` | **F4** `frame_draw.c:295-324` installs three new hooks; **F2** `frame_redraw.h:152-155` also chains all three; `render_guest.c` wraps these procs at the *proc-address* level via the `0x722a180` hook, not at these GOT slots | **C** | `render_guest` | F9 §4.4 explicitly assigns these residual sites to `render_guest`. Two frame modules already fight over the same three GOTs; move the technique there under one withheld knob; never run both paths — that double-counts guest draws. |
| `ANativeWindow_fromSurface` GOT `0x72294a8` | `render_guest.c:239-253` `rg_hook_from_surface`; `native_render.c:991-999` `sr_hook_anw_from_surface` (installed by fallback); **F5** `frame_surface.h:57-63` would install a third writer; `native_render.c:1002-1012` `sr_hook_egl_create_window_surface` is **passed as a pointer** by `guest_render.c:254` to the module path | **C** | `render_guest` | Note the cross-module pointer hand-off (`guest_render.c:254` → `render_guest.c:cfg.egl_create_window_surface_hook`): the "owner" of that hook body is `native_render` while the *installer* is `render_guest`. Accept as one deliberate exception or move the body into `render_guest`; **do not add F5's writer** — merge its guest-RA publish technique into `render_guest.rg_hook_from_surface` instead. |
| `eglCreateWindowSurface` GOT `0x722a178` | **F5** `frame_surface.h:14,60-65` (new writer, chains to native_render's real-EGL hook); `render_guest.c:426-430` writes the same pointer value as a *pass-through*, not a GOT install; `native_render.c:1002-1012` holds the body | **C** (proposed) | `render_guest` (install) + `native_render` (body, as today) | Do not let F5 install a second writer for this slot; if F5's real-EGLSurface path is adopted, move the install into `render_guest` and keep one body. |
| Guest window global `0x72a3308` / generation `0x72a3310` | `render_guest.c:326-329` writes host-built `s_anw`; `guest_render.c:186-189` writes its local `s_gr_anw`; `frame_evidence.c:307-324` `observe_window_global()` **reads** both and binds | **C** (two writers, one observer conflating provenance) | `render_guest` (writer) + verdict module (read-only observer) | Keep one writer (module path); drop the local-fallback write from the default path; make the observer read-only and require guest-RA provenance (`F10 §1.5`). |
| `nativeData+0x88/+0x98/+0xa0` callback slots | `render_guest.c:402-404` writes NULL (host); `guest_render.c:188` leaves NULL; `frame_thread.c:218-220` only *reads* them; F2 (proposed) would write genuine guest pointers | **C** (proposed) | `render_guest` via one withheld `drive_redraw()` | No frame module may install host function pointers here (`decision 013`; `F10 §1.4`). |
| render ctx `+0x18`/`+0x28`, nativeData `+0x140` | `render_guest.c` builds host ctx/nativeData; **F5** `frame_surface.h:15-17,74-85` re-asserts `+0x140` and mirrors guest stores at ctx `+0x18/+0x28`; guest itself writes them via `0x649c2cf`/`0x649c3f6` | **C** (proposed) | guest code, observed by `render_guest` | Prefer F5's "guest writes its own fields" technique over host stores; merge into `render_guest` under a withheld knob. Never leave two modules re-asserting the same slots. |
| Lifecycle thunks `0x2a20850/0x2a208c0/0x2a20a00/0x2a20a30` | `render_guest.c:299-322` `rg_bind_surface()`; `guest_render.c:207-214` local fallback; `frame_thread.c:110-140` loops on `0x2a20a00`; `frame_dispatch.c:396-453` invokes *discovered* sites | **C** | `render_guest` (one withheld driver) | Retire `frame_thread` and `frame_dispatch` drivers; keep fallback only for A/B. |

### 2.2 RA classifiers (all using the same `0x6d33b90` first-PT_LOAD span)

| Classifier | Owner | Status | Owner after plan | Action |
| --- | --- | --- | --- | --- |
| `rg_in_guest(ra)` `render_guest.c:181-185` | render_guest | **D** (live) | render_guest | Keep; add engine-identity + RA-shape + stable-RA checks (`F10 FC-1/2/3`), and ignore calls when `frame_synthetic_tag` scope is active only if that module is retained (preferred: it is not). |
| inline `ra >= g_guest_ra_base && ra < +SR_GUEST_TEXT_SIZE` `native_render.c:1030-1037`, plus `SR_GLES_LOADER_LO/HI` at `:1121` | native_render | **D** (counter stays 0 in default path) | none | Delete/neutralise in default build; keep only in A/B fallback. |
| `fe_ra_tag(ra)` `frame_evidence.c:111-127` | frame_evidence | **C** (3rd classifier; public `set_guest_base` can retarget it at runtime) | verdict module (read-only classification of *already-tagged* events) | Do not reclassify a swap; consume the single owner's tag. Validate base against frozen BuildID before use (`F10 Path A/B`). |
| `fd_in_guest(ra)` `frame_dispatch.c:97-100` | frame_dispatch | **C** (4th classifier) | none | Remove with `frame_dispatch`. |
| `sober_frame_synthetic_tag_active()` `frame_synthetic_tag.c:84-86` | frame_synthetic_tag | **C** (overrides the above before `rg_in_guest`) | none | Remove with `frame_synthetic_tag`; fold the explicit host tag into `render_guest` as a local flag if defence-in-depth is required. |
| `fd_ra_is_guest()` `frame_draw.c:82-86`; `fs_in_guest()` `frame_surface.c:183-186` (+ narrower `fs_in_bind_helper`/`fs_in_surface_helper`); F2's swap/draw RA tag | F2, F4, F5 | **C** (6th–8th classifiers, same span) | none | Remove with F2/F4/F5; keep the narrow guest-RA bind-helper check inside `render_guest` as provenance evidence only, not a new classification owner. |

### 2.3 Counters

| Counter family | Current owners | Status | Owner after plan | Action |
| --- | --- | --- | --- | --- |
| swap total | `s_swaps_total` (`render_guest.c:64`); `g_guest_swaps` (`native_render.c:160`); `s_swaps_total` (`frame_evidence.c:41`); `s_swaps_total` (`frame_dispatch.c:52`) | **C** | `render_guest.s_swaps_total` | Delete the other three; if verdict module needs a total, expose a read-only accessor on `render_guest`. |
| swap guest-tagged | `s_swaps_guest` (`render_guest.c:65`); `g_guest_swaps_tagged` (`native_render.c:166`); `s_swaps_guest` (`frame_evidence.c:42`); `s_swaps_guest` (`frame_dispatch.c:53`) | **C** | `render_guest.s_swaps_guest` | Same. `gr_guest_swap_evidence()` (`guest_render.c:272-277`) must stop summing native+module totals. |
| swap synthetic | `s_swaps_synthetic` (`render_guest.c:66`); `g_guest_swaps_synth` (`native_render.c:167`); `s_swaps_synthetic` (`frame_evidence.c:43`); `g_tagged_swaps` (`frame_synthetic_tag.c:37`); `s_swaps_host` (`frame_dispatch.c:54`) | **C** | `render_guest.s_swaps_synthetic` | Same; `frame_synthetic_tag` `note_swap()` must not become a 5th bucket. |
| draw count | `rg_draw_calls()` from `s_gles_draw_wrapped` trampolines (`render_guest.c:142-150`, invocation-time); `g_gles_draw_calls` (`native_render.c:170`, **wrap-time at `:1128-1132`** — reads 5 when 0 draws ran); `s_draws_*` (`frame_evidence.c:44-46`); `gr draws` dlsym (`guest_render.c:135`) | **C** | `render_guest` (invocation-time only) | Never wire `sober_render_gles_draw_calls()` to `draws>=1` (`F10 Path C`). Add missing draw-site wrappers + guest-RA gating to `render_guest` (F4 merge). |
| fromSurface calls | `s_from_surface_calls` (`render_guest.c:61`); `g_from_surface_calls` (`native_render.c:168`) | **D** | `render_guest` | Keep one. |
| GLES proc loads | `s_gles_proc_loads` (`render_guest.c:62`); `g_gles_proc_loads` (`native_render.c:169`) | **D** | `render_guest` | Keep one. |
| lifecycle calls | `s_life[4]` (`render_guest.c:67`); `g_life_calls[4]` (`native_render.c:171`) | **D** | `render_guest` | Keep one. |
| frame-thread frames/skips/errors | `s_frames/s_skips/s_errors` (`frame_thread.c:48-50`) | **C** (new, and `frames` duplicates a guest `0x2a20a00` dispatch count) | none | Retire with `frame_thread`; `render_guest.s_life[2]` already counts redraw dispatch. |
| dispatch site counters | `s_site_candidates/attempts/verified` (`frame_dispatch.c:55-57`) | **C** (new diagnostic; attempts execute guest code) | none | Retire with `frame_dispatch`. |
| synthetic-scope counters | `g_scope_enters/leaves`, `g_seen`, `g_tagged_swaps` (`frame_synthetic_tag.c:32-35`) | **C** | none | Retire; if retained, `g_tagged_swaps` must not be summed into guest swaps. |
| surface / window-bind / lifecycle counters (F5) | `frame_surface.c:83-96` ~13 counters (`s_from_surface_calls`, `s_from_surface_guest_ra`, `s_life[4]`, `s_create_calls`, `s_bind_helper_calls`, `s_reasserts`, …) | **C** | `render_guest` | F5's counters overlap `render_guest.s_from_surface_calls` / `s_life[4]`; do not keep a second set. Preserve only the guest-RA + generation provenance check, folded into the verdict. |
| plain-draw GOT counters (F4) | `s_got_total[3]/s_got_guest[3]/s_got_synth[3]`, `s_dispatch_calls`, `s_dispatch_plain/instanced`, `s_dedup_rejects`, `s_render_before/after` (`frame_draw.c:69-77`) | **C** | `render_guest` | F4's guest-RA gate might look stronger than `render_guest`'s trampoline counter, but it counts a *different* set of sites and would double-count if both are wired. Move the technique into `render_guest`, keep one family. |
| redraw counter family (F2) | `sober_frame_redraw_drives()`, `..._swaps_total/guest_tagged/synthetic()`, `..._gles_dispatch_draws()`, `..._gles_guest_draws()` (`frame_redraw.h:217-222`) | **C** | `render_guest` | These are a third swap-counter family and a second draw family, both on GOTs F2 chains. Retire the module; the genuine callback-seeding/drive technique moves into `render_guest` with a withhold knob. |

### 2.4 State machines / verdicts

| State | Current owners | Status | Owner after plan | Action |
| --- | --- | --- | --- | --- |
| phase-3 done | `g_phase3_done` (`native_render.c:177`, accessor `:976`); weak-read by `frame_evidence.c:29,137` | **S** (read-only elsewhere) | `native_render` | Fine; keep the weak read fail-closed. |
| install state / last RC | `render_guest` `s_installed/s_last_rc`; `native_render` `s_sr_got_installed`; `frame_dispatch` `s_installed/s_last_rc`; `frame_thread` `s_installed/s_started/s_running`; `frame_synthetic_tag` `tl_depth/g_seen` | **C** (4 independent lifecycles for one render path) | `render_guest` + gate verdict only | Collapse lifecycles; no module may own a separate "installed" flag for the same GOT. |
| guest base | `render_guest.s_base`; `native_render.g_guest_ra_base`; `frame_evidence.s_base` (publicly settable); `frame_dispatch.s_base`; `frame_thread.s_cfg.guest_base` | **C** | one validated base shared by reference | Validate BuildID once; never let a caller-supplied base retarget a classifier mid-run. |
| render verdict | inline `render_ok` (`guest_app_shell.c:1777-1780`); `guest_render_ok()` (`frame_evidence.c:347-351`); `sober_gate_render_guest_tagged_ok()` (`F9 §5.3`); G1 spec (`G1 §2`) | **C** | one verdict module (`gate_render_evidence`) called by the lead gate | Pick ONE name (`sober_gate_render_guest_tagged_ok()` recommended), delete the others; keep the sole `emit_marker` at `guest_app_shell.c:1807`. |
| `mixed` taint | `frame_evidence.s_mixed` (swap-only, `fe_update_mixed():219-223`); `render_guest` has no explicit taint but splits buckets; `frame_synthetic_tag.g_seen` is a separate non-sticky-by-default bit | **C** | verdict module, derived from the single owner's buckets | Extend taint to draws and window binds (`F10 §1.5`); host-tag kill switch must force withhold, not merely disable the tag. |

---

## 3. Default-ON / withhold conflicts

| Module | Knob | Accepted OFF values | In `seeded_state` chain at `:1746`? | Conflict |
| --- | --- | --- | --- | --- |
| `render_guest` | `SOBER_RENDER_GUEST` | `"0"` only | **Yes** | OK. Fail-closed (`rg_install` returns DISABLED, counters inert). |
| `native_render` | none | — | n/a | No knob; its duplicate hooks are only reachable via `guest_render.c` fallback. |
| `guest_render` | `SOBER_RENDER_GUEST` via `gr_contract_enabled():113-117`; also `SOBER_GUEST_ROUTE_A`, `SOBER_RENDER_BACKEND`, `SOBER_WINDOW_HOLD_MS` | `"0"` only | partly (`SOBER_RENDER_GUEST` yes; others no) | `SOBER_GUEST_ROUTE_A` switches the swap path without withhold — a driver knob that is not in the chain. |
| F2 `frame_redraw` | `SOBER_FRAME_REDRAW` | `"0"` only | **No** | Default-ON module that seeds nativeData callback slots with guest RVAs, drives `0x2a20a00` and chains four GOTs — a seeded-state driver that must be withheld (`F9 §6`). |
| F3 `frame_dispatch` | `SOBER_FRAME_DISPATCH` | `0`, `off`, `no`, `false` (ci) | **No** | State-changing default-ON driver outside withhold chain; violates `F9 §6`. |
| F5 `frame_surface` | `SOBER_FRAME_SURFACE` | `"0"` only | **No** | Default-ON module that installs two GOT hooks and writes ctx/nativeData fields — a driver, not a passive observer. Withhold or retire (§4 step 4). |
| F4 `frame_draw` | `SOBER_FRAME_DRAW` | `"0"` only | **No** | Default-ON module that installs three GOT hooks and drives guest `0x6496d0c` with a synthetic descriptor (`fd_drive_branch`) — a driver, not a passive observer. Withhold or retire (§4 step 5). |
| F6 `frame_thread` | `SOBER_FRAME_THREAD` | `"0"` only | **No** | Its `should_withhold()` is **true whenever default-ON and installed** (`frame_thread.c:261-265`), so a default wiring permanently forces withhold unless the lead adds the knob. |
| F7 `frame_evidence` | `SOBER_FRAME_EVIDENCE` | `"0"` only | **No** | Proof-path knob: `F9 §5.3` says the verdict reads no `SOBER_*`; `G1 §1` accepts the knob but then must fail closed. Pick one policy. |
| F8 `frame_synthetic_tag` | `SOBER_FRAME_SYNTH_TAG` | `0`, `off`, `no`, `false` (ci) | **No; header says "must NOT be added to withhold chain"** | Its kill switch disables the only explicit host tag while all decisive counts stay live — a suppression knob (`F10 §1.3`). Must be withhold-listed or removed. |

**Canonical-name mismatch.** `F9 §6` would add `SOBER_RENDER_FRAME_EVIDENCE`,
`SOBER_EVIDENCE_SURFACE/DEVICE/REDRAW`, `SOBER_FRAME_REDRAW_DRIVE`,
`SOBER_RENDER_SUPPRESS_SYNTHETIC`. The shipped modules use different names
(`SOBER_FRAME_EVIDENCE`, `SOBER_FRAME_THREAD`, `SOBER_FRAME_DISPATCH`,
`SOBER_FRAME_SYNTH_TAG`). Adding only F9's list would leave every real knob
un-withheld. Whichever names survive must be the ones in the chain.

---

## 4. Merge / retire plan (lead-facing)

Ordered so that each step is independently buildable and testable.

1. **Freeze the hook graph.** Declare `render_guest.rg_hook_swap` the sole owner
   of GOT `0x722a1a8`. Reject `frame_dispatch.c` (F3) outright: its GOT observer
   is a 4th writer and its site-attempt loop executes guest code from a child —
   unacceptable in a default-ON module. If any of its site-verification logic is
   valuable, lift only the *read-only* opcode/stability check into the verdict
   module (F7) as a diagnostic, with no GOT write.
2. **Retire F8 `frame_synthetic_tag`.** Delete both files from the build. Keep
   one explicit host-scope bit *inside* `render_guest`, set around
   `guest_render.c:354` and consumed before `rg_in_guest()`. Add
   `SOBER_RENDER_SUPPRESS_SYNTHETIC` (if kept) to `seeded_state`; a suppression
   knob must never merely switch off classification.
3. **Retire F6 `frame_thread`.** Delete both files from the build. The genuine
   render-thread/nesting concern belongs to `scheduler_guest` `on_attach`
   (`guest_app_shell.c:1398`) + `tsd_default` key 3 (`F9 §3`); adding the attach
   point there avoids a second thread and the `257f718-Context,20` nesting bug.
4. **Retire F1 `frame_callbacks` and F3 `frame_dispatch`; merge F5
   `frame_surface` into `render_guest`.** No F1 file exists — do not create it;
   `provenance_gate` owns callback provenance. F5's *technique* is worth
   preserving: publish the window via the guest's own helper `0x26fea5a` so
   `0x72a3308`/`0x72a3310` are guest-written (a real fix for the current
   host-written false-green), and verify with a guest-RA check at
   `0x26fea78..0x26feaa0` plus a generation delta. Move that into
   `render_guest` under a withheld knob; do not keep F5's two GOT writers, its
   nativeData `+0x140` re-assert loop, or its parallel counters.
5. **Merge F2 `frame_redraw` and F4 `frame_draw` into `render_guest`.** F2's
   callback RVAs are genuinely useful: seed `[+0x88]/[+0x90]/[+0x98]/[+0xa0]`
   with the guest's own `0x2a23d40/0x2a23dc0/0x2a23e70/0x2a23f10` inside one
   withheld `sober_render_guest_drive_redraw()`, then drive `0x2a20a00`.
   Add forwarding, guest-RA-gated draw wrappers
   for the residual GLES JUMP_SLOTs `0x7229fc8/0x722a040/0x722a048` and
   extension table `0x77a4fe0/8`, `0x77a4fa0/8/b0`, `0x77a4f98`, `0x77a4f78`
   (`F9 §3/§4.4`). Both functions default inert and are withhold-listed. Never
   count the host `dlsym`'d `glClear` (`native_render.c:817-830`).
6. **Demote `native_render`'s duplicate GOT layer.** Keep it only as the
   documented A/B fallback for `SOBER_RENDER_GUEST=0` (the local path in
   `guest_render.c:148-228`). Stop summing its counters in
   `gr_guest_swap_evidence()`; report sources separately. This removes the
   "two owners chosen by a runtime conditional" hazard without deleting the
   native backend.
7. **Keep F7 `frame_evidence` only as the verdict module.** Rename to
   `gate_render_evidence.{c,h}` and expose exactly
   `sober_gate_render_guest_tagged_ok()` + `sober_gate_render_guest_tagged_verdict()`
   (`F9 §3`). It must **read** `render_guest` counters/RA evidence, not own
   `s_swaps_*`/`s_draws_*`/`fe_ra_tag`/`guest_render_ok()`. Extend the mixed
   taint to draws and window binds; validate the guest base against the frozen
   BuildID; require stable RA and a guest-RA draw bracketed by two tagged swaps.
   Retire the generic global `guest_render_ok()` name in favour of the
   `sober_gate_*` name so there is one gate-facing predicate (`G1` and
   `F9 §5.3` must converge on the same symbol).
8. **Withhold-chain cleanup.** Replace the F9 canonical list with the names
   that actually survive (§3). Ensure `seeded_state` forces withhold whenever
   any driver or classification knob is set, while leaving marker paths
   (`SOBER_SHELL_MARKER`, `SOBER_FRAME_MARKER`), fail-closed credentials
   (`SOBER_PROVENANCE_GATE`) and legitimate real-path enables
   (`SOBER_RENDER`, `SOBER_APP_SHELL`) out of the chain (`D02 §5-6`, `F9 §6`).

**Net source result:** one new verdict module (`gate_render_evidence.{c,h}`),
zero new hook installers, zero new counter families, additive functions only in
`render_guest.{c,h}`. `tests/first-frame.sh`, `ctest`, `ldd` libc-only and zero
`app shell rendered` emissions remain the acceptance invariants.

---

## 5. CMake target implications and build risk

Current linking: `sober_jni` is a STATIC lib (`src/jni/CMakeLists.txt:12-59`)
linked into `guest-init-host` (root `CMakeLists.txt:171-181`) and into
`jni_probe` (`:65-66`). `sober_render` is STATIC and linked PUBLIC with
`pthread` (`src/render/CMakeLists.txt:19`). `guest_render.c` is compiled
directly into `guest-init-host` (`:157`). **No `frame_*` file is in any target**
(verified: zero `frame_` matches in all three CMakeLists).

| Risk | Severity | Detail / mitigation |
| --- | --- | --- |
| Sources silently not built | **High** | Adding the F2–F8 `.c` files to `sober_jni` is a one-line change each, but today they are untracked and invisible to CMake: a lead may believe a module is "live" when it is not. Adding them all at once creates the duplicate owners of §2. Add only the single surviving verdict module after §4. |
| `jni_probe` lacks `pthread` | **Medium** | `frame_thread.c` calls `pthread_create`/`pthread_join`; `jni_probe` links only `sober_jni`, and `sober_jni` does not link `pthread` (unlike `sober_compat`/`sober_platform`). On glibc ≥2.34 this resolves from libc; on older glibc it is an undefined-reference link failure. Retiring F6 removes the risk; otherwise add `pthread` to `sober_jni` PUBLIC. |
| Weak `sober_render_phase3_done()` in `jni_probe` | **Low/Medium** | `frame_evidence.c:29` declares it weak; `guest-init-host` links `sober_render` so it resolves, but `jni_probe` does not, so `guest_render_ok()` is always 0 there. Fail-closed and correct, but any test expecting a green verdict from `jni_probe` would be wrong. |
| Generic global `guest_render_ok()` | **Medium** | All `SOBER_*_API` functions get default visibility and `-rdynamic` publishes them into `RTLD_DEFAULT`. A short generic name risks accidental interposition by another DSO or a future module. Rename to `sober_gate_render_guest_tagged_ok()` (§4 step 7). |
| Selftest `main` symbols | **Low** | `frame_thread.c:273` and `frame_synthetic_tag.c:171` guard `main` behind `SOBER_FRAME_THREAD_SELFTEST` / `SOBER_FRAME_SYNTH_TAG_SELFTEST`; safe unless someone adds those defines to a target. `frame_dispatch.c` has no `main`. Adding selftest defines to a library target would produce a duplicate `main` at link time. |
| Duplicate counter/hook symbols at link | **High** | No symbol currently collides by name (verified: `sober_render_swaps_guest_tagged` exists only in `native_render.c`, `sober_render_guest_swaps_guest_tagged` only in `render_guest.c`), so the failure mode is *semantic*, not a link error: both families link cleanly and disagree at runtime. This is why a build/ctest PASS is not evidence of single ownership. |
| `dlsym(RTLD_DEFAULT)` owner selection | **Medium** | `guest_render.c:119-143` chooses module vs fallback at runtime; `frame_dispatch.c` chains GOT hooks by saving `s_saved_target`. Hook order therefore depends on install order, which F9 §2 fixes but CMake does not enforce. Removing the extra installers removes the order sensitivity. |
| Build/test regression surface | **Low** | Acceptance after §4: `cmake --build build`, `ctest --test-dir build` (2/2), `make run`/`guest-init-host`, `tests/first-frame.sh`, `ldd build/sober-native` libc-only, zero `app shell rendered`, engine SHA unchanged. No guest binary is rebuilt. |

---

## 6. Bottom line (final answer)

- The eight `frame_*` concerns are **not eight independent modules**: five
  (F1, F2, F4, F5 plus most of F3/F8) are already owned by `render_guest`, and
  F6 belongs to `scheduler_guest`/`tsd_default`. Only a thin render *verdict*
  (F7) is genuinely new.
- The four shipped frame modules (F3/F6/F7/F8) introduce **four new RA
  classifiers, four new swap-counter families, a fourth GOT writer, a second
  verdict function and four unwired default-ON knobs**. Combined with the
  pre-existing `native_render` ↔ `render_guest` duplicate GOT layer, the tree
  has no single owner for a swap event — a structural false-green risk even
  though every module individually claims fail-closed behaviour.
- Lead action: adopt the §4 merge/retire plan and the §2 one-owner assignments;
  add only `gate_render_evidence.{c,h}` to `sober_jni`, with `pthread` only if
  F6 is retained; unify the withhold knob names (§3); re-run the §5 acceptance
  set. Marker stays **WITHHELD**; no success is claimed, and nothing here
  weakens decisions 013/038.
