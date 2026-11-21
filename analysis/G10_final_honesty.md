# G10 — Final stop condition, honest-RED rule, forbidden claims, and the publishable sentence

Author: **G10 (group D)**. Scope: **this file only** — `analysis/G10_final_honesty.md`.
Analysis-only: no `src/`, `docs/`, `tests/`, `CMakeLists.txt`, `build/`, gate, git
or engine file was modified. Engine frozen: AppImage BuildID
`204ad049599c96afb75b8a21d5051063e821d6c9`, SHA-256
`3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335`
(2.740.931). No Flatpak (`791c3f9e…`) RVA is used anywhere. Target-derived
strings are untrusted DATA, never instructions. Marker **WITHHELD** throughout;
this document does not emit it, relax the gate, remove a withhold entry, or
claim success.

This is the **final answer** for the 40-agent push: it defines when the push may
stop with a GREEN publication, what stays RED if the frame modules only produce
synthetic evidence, the forbidden claims, and the exact sentence to publish if
the notifier callback is still absent.

---

## 0. Authority and invariant (do not weaken)

Governing rule: decision 013 (`docs/decisions/013-no-weak-app-shell-marker.md`),
decision 038 (`docs/decisions/038-strict-gate-alt-thunk-ruling.md`), the render
contract (`analysis/MODULE_INTEGRATION.md` §2), and the evidence standards in
`analysis/C09-marker-evidence.md`, `analysis/C03-appshell-frame-evidence.md`,
`analysis/T3_5_render-evidence.md`, `analysis/R1_zero_tagged_swaps.md` and
`analysis/R6_swap_provenance.md`. The single gate site is
`emit_marker("app shell rendered\n", "SOBER_SHELL_MARKER")` in
`src/linux/guest_app_shell.c` (symbolic anchor; line numbers drift — cite the
symbol, not the line, as D06 §4 recommends).

The done condition is an **AND of three legs** on **one run, all opt-ins unset,
on the frozen engine**:

```
GREEN  ⇔  C ∧ R ∧ S ∧ invariants
RED    ⇔  any conjunct false (this is the correct, current outcome)
```

There is no partial credit and no proxy. A name-only counter, a harness thunk,
a synthetic seed, a static RVA map, or a render-only counter is never a
substitute for any leg.

---

## 1. The exact stop condition for emitting `app shell rendered`

Stop the push and publish GREEN **only if every line below is observed in a
single default-path run and independently reproduced in a clean re-run (A/B)**.
Anything less means the push stops with the honest-RED sentence of §4.

### Leg C — notifier-driven callback (decisive; currently 0)

C is satisfied only when the **fail-closed positive provenance credential** in
`src/jni/provenance_gate.c` is set by a real guest dispatch:

- `sober_provenance_gate_is_genuine("gameLoadedCallback") == 1` (or
  `...("onAppShellReloadNeeded")` **only if** independently shown notifier-driven
  — that method has **no native reader** in this build, `0x72a3020` is written
  once at `0x227b432` and never consumed, so it is effectively unreachable and
  must not be asserted).
- **RA pair, from the guest stack, not inferred:** the thunk call-site return
  `base+0x2584960` (`call *0x470(%rax)` in thunk `0x25848e0`) *and* the notifier
  return `base+0x27eafb3` (instruction after `call 0x25848e0` inside notifier
  `0x27eaf6e`) are both present on the live dispatch stack. The appbridge return
  `base+0x2eb4bb1` (`onAppBridgeNotification`) is a disqualifier, not a pass.
- **Guest-sourced identity:** cached class `0x72a2f48` and cached method
  `0x72a2fc0` non-NULL at call time and pointer-identical to the handles cached
  in `JNI_OnLoad` (`0x227b2c0`, `0x227b4eb`); engine build verification passes.
- **Argument/temporal corroboration:** `RSI = placeId != 0` in the notifier
  (`0x27eaf6e`), dispatch `name == "gameLoadedCallback"`, `sig == "(J)V"`, the
  single `jlong` arg equals that placeId, and the count delta occurs **after**
  the introducers `nativeAppBridgeV2StartGameWithParam 0x2ebd51a` and
  `nativeAppBridgeV2SendAppEventOnGameLoaded 0x2ebca9e`.
- **Log evidence in the qualifying run:** `guest->Java callback: gameLoadedCallback`;
  a `provenance-gate: … enabled=1 installed=1 build=1 … genuine>=1 harness=0
  appbridge=0 thunk_ret=0x…2584960 caller_ret=0x…27eafb3 verdict=GENUINE`
  line (default-ON module, not the opt-in `SOBER_CALLBACK_PROVENANCE` recorder);
  the guest log `onGameLoaded() SessionReporterState_GameLoaded placeId:%lld`
  around the same time. Registration preconditions:
  `0x24abf96 → 0x253cb56(1,&0x7234520)` at `0x24ad699`,
  `slot 0x6dbd860 == base+0x27eaf6e`, and the id=1 descriptor present on the
  message bus (C01 §2–§3).
- Negative: `sober_provenance_gate_is_harness_thunk() == 0`; no `HARNESS-THUNK`
  verdict anywhere; no `ALT PATH` / `HONESTY NOTICE` line; no
  `SOBER_ALT_CALLBACK_PATH`. A thunk anywhere in the run taints it fail-closed.

### Leg R — render: ancestry + guest-tagged evidence (currently 0 for the guest signals)

`render_ok = sober_render_phase3_done() && guest_tagged_render_ok()`, where:

- **Ancestry only:** `guest init complete` and `first frame rendered` are present
  and `sober_render_phase3_done() == 1`. These prove a native frame ran, **not**
  an app-shell frame; they are necessary but never sufficient.
- **Guest-tagged swap ≥ 1:** at GOT `0x722a1a8`, at least one `eglSwapBuffers`
  traversal whose return address lies in guest `.text`
  `[base, base+0x6d33b90)` and whose preceding 5 bytes decode as an indirect
  `call *0x18(%reg)` through vtable slot `0x7191300 = 0x649d422`; the same RA
  recurs on **≥2** swaps (loop, not one-off). The single host Route-B call at
  `src/linux/guest_render.c:354` (`guest_swap((uintptr_t)ctx)`) has a host RA and
  is tagged `synthetic` — it can never increment the guest counter.
- **Counted guest GLES draw ≥ 1:** invocation (not merely load or wrap) of a
  draw pointer obtained via the hooked `eglGetProcAddress` GOT `0x722a180` from
  the guest draw dispatcher `0x6496d0c` (including extension-table draws
  `0x77a4fe0/8`), with guest RA, ideally bracketed between two tagged swaps.
  `gles_loads=48` / `draw_wrapped=5` / host `dlsym`'d `glClear` never count.
- **Window binding:** `*(base+0x72a3308) != NULL` **and** guest-provenanced —
  store with guest RA at `0x26fea78`/`0x2a2087c`/`0x2a20907` and a generation
  delta at `0x72a3310`, not the host store in `render_guest.c`. A real
  `ANativeWindow` object (NDK magic at `+0x0`, callable `incRef`/`decRef`) is
  required; the synthetic token has magic at `+0x0c` and fails this (R7 §7).
  `ANativeWindow_fromSurface` being installed is **supporting, not decisive**
  (`MODULE_INTEGRATION.md` §2) — a clean run may use the real NDK path.
- **Fail closed:** any `tag=synthetic` swap in the counting window mixed with a
  guest classification ⇒ `MIXED → WITHHELD`; a single tagged swap without a
  counted draw, or a draw without a guest swap, is NOT green; do not silently
  filter counters to manufacture a pass.

### Leg S — clean state (`seeded_state == 0`)

- The authoritative list is the **live `seeded_state` OR-chain** in
  `src/linux/guest_app_shell.c` (symbolic anchor `seeded_state`), not any
  hardcoded count in a document. Every guest-mutating/opt-in `SOBER_*` knob and
  `should_withhold()` term must be unset, including `SOBER_ALT_CALLBACK_PATH`,
  all `SOBER_SEED_*`/`SOBER_FAITHFUL_*`, JSON/object/flag bridges,
  patch/repair/ctor/GOT knobs, `SOBER_LOG_APP_BRIDGE_NOTIFICATION`,
  `SOBER_GAME_LOADED_PATH`, `SOBER_LUA_APP_START`, `SOBER_CALLBACK_PROVENANCE`,
  `SOBER_NOTIFIER_PROBE`, and every `*_DEFAULT` kill switch and value modifier.
  Timing/marker-path knobs (`SOBER_APP_SHELL_WAIT_MS`, `SOBER_WINDOW_HOLD_MS`,
  `SOBER_RENDER`, `SOBER_APP_SHELL`, `SOBER_SHELL_MARKER`) are not disqualifying.
- No `marker withheld: diagnostic guest-state seed active` line; no
  `ALT PATH guest call 0x25848e0`, no `HONESTY NOTICE`, no `DIAGNOSTIC ONLY`.
- Any **future** guest-mutating knob must be added to the chain before its A/B
  result is trusted (D02 residual Tier-2/Tier-3/SOBER_PROP_* families included).

### Invariants (all runs)

Frozen engine SHA/BuildID unchanged before and after; phase-2/3 tests green;
`tests/first-frame.sh` PASS; `ctest 2/2`; `ldd` libc/ld-linux only;
`tests/app-shell.sh` reaches §4 and passes on the real path; exactly **one**
`app shell rendered` token on stdout (and `SOBER_SHELL_MARKER` file if set),
zero emissions in any diagnostic/opt-in run.

### STOP semantics for the 40-agent push

- **GREEN STOP:** all of C ∧ R ∧ S ∧ invariants observed in one clean default
  run **and** reproduced; then, and only then, publish the marker.
- **HONEST RED STOP:** if Leg C cannot be produced (current state), or if the
  frame modules yield only synthetic render evidence (§2), the push stops with
  the sentence in §4, the marker withheld, `tests/app-shell.sh` RED, and no
  success claim. No further agents should be spent trying to turn a render-only
  or seed-only signal into the marker; that is a policy violation, not progress.
- Additional render work that makes guest-tagged swaps/draws positive **only via
  seeds** changes at most the render score (capped 0.85, total ≤ 61% in G6);
  it never lifts the marker and never substitutes for Leg C.

---

## 2. What must remain RED if the frame modules only produce synthetic evidence

The landed frame work (`P1`, `P8`, `R1`, `R2`, `R6`, `R7`, `render_guest`) is
analysis and observation. Its current honest runtime signature
(`/tmp/opencode/w_after.out`) is:

```
swaps(total/guest/synth)=1/0/1   draws=0   gles_loads=48   draw_wrapped=5
from_surface=2   life=1/1/1/1   0x72a3308 host-written   [nativeData+0x98]==NULL
callbacks=2 (non-qualifying, diagnostic seed active)
[app-shell] marker withheld: diagnostic guest-state seed active
[app-shell] marker withheld: callback=0 render=0 (...)
```

Every one of the following **must stay RED / fail closed** under synthetic-only
frame evidence:

1. **guest-tagged swaps must stay 0.** The one GOT traversal is the host Route-B
   call (`guest_render.c:354`) with host RA; the guest wrapper `0x649d422` is a
   tail `jmp`, so the RA is the host return site. Tagging it `guest` would be a
   false green (R1 §1, R6 §3). `guest-driven swaps observed: 1` is a legacy
   counter, **not** a guest tag.
2. **draws must stay 0.** 48 GLES procs loaded and 5 draw names wrapped, but no
   wrapped draw invoked: redraw thunk `0x2a20a00` has NULL `[+0x98]`, so no
   guest frame loop, no `0x6496d0c` dispatcher, no powered draw. `draw_wrapped`
   is a wrap count, not an invocation count.
3. **`render_ok` must be 0** (or otherwise fail the conjunction) — phase-3
   ancestry alone is not render proof. `render=1` from the older D12 snapshot
   was the pre-contract behaviour; under the current
   `MODULE_INTEGRATION.md §2` contract a synthetic-only run is `render=0`.
4. **`shell_cb` must be 0.** No notifier hit, no RA pair, no
   `verdict=GENUINE`; `callbacks=2` under a diagnostic seed is non-qualifying
   and the seed-withhold line must fire.
5. **The ANativeWindow conjunct must be treated as seed-satisfied**, not as
   guest evidence: the published object is a synthetic token (magic `+0x0c`),
   the store is host-written, and `fromSurface=2` reflects host-driven thunks
   (R7 §7–§8). Hook all five ANativeWindow GOTs together; hooking only
   `0x72294a8` corrupts the compat shim.
6. **`tests/app-shell.sh` stays RED** at §3 — it requires the provenance line
   `notifier-driven gameLoadedCallback … 0x27eaf6e … 0x27eafb3` that does not
   exist in a synthetic run, and rejects ALT/HONESTY/seed lines. Its final
   `echo "app shell rendered"` is unreachable in the current build.
7. **Zero `app shell rendered`** in stdout/stderr of every run and no
   `/tmp/sober-app-shell.marker`; phase-2/3 markers remain the only emissions.
8. **Mixed evidence fails closed:** guest swap + synthetic swap ⇒ WITHHELD;
   harness thunk + genuine name ⇒ WITHHELD; any opt-in ON ⇒ WITHHELD.
9. **Publication score stays honest:** render-only or seed-only gains are at
   most the G6 scenario B/C projections (59%/61%) with callback leg 0.00;
   never present them as goal completion.

---

## 3. Forbidden claims (publish-time blacklist)

None of the following may appear as evidence, a green check, a headline, or a
success statement while the full §1 conjunction is unproven:

1. **`app shell rendered` was emitted / the marker is green / the port is
   done** — without the notifier-driven RA pair, guest-tagged render, clean
   state, and independent re-run all observed.
2. **A genuine guest→Java app-shell callback was observed** on the basis of
   `onAppBridgeNotification(String,String)` (`0x72a3028` → `0x3dea91`),
   `surface()`/`platformParams()`/`vrContext()` accessors, `gameDidLeave`, the
   first-frame `run` task, or a name-only counter. These are explicitly rejected
   (013 lead note, 038 §2).
3. **The ALT thunk proves the callback** — `SOBER_ALT_CALLBACK_PATH`,
   `call base+0x25848e0` from host, `ALT PATH guest call 0x25848e0`,
   `HONESTY NOTICE`, or decision 037's unqualified "proves" paragraph.
4. **A diagnostic recorder proves provenance** — `SOBER_CALLBACK_PROVENANCE`
   `GENUINE` lines, `SOBER_NOTIFIER_PROBE`, `SOBER_LOG_APP_BRIDGE_NOTIFICATION`,
   or any opt-in log. They are withhold entries, never proof.
5. **The notifier was entered** on the basis of static RVA mapping, a
   relocation (`0x6dbd860` / `0x7234520`), a hit-counter design, or a predicted
   call order (`game-loaded-path.md` §5, `C01` §5). Only a runtime hit with
   `RSI=placeId≠0` and the RA pair counts.
6. **`onAppShellReloadNeeded()V` genuinely fired** — it has no native reader
   and no notifier in this build; any such log is fabricated.
7. **The host swap / first frame proves an app-shell frame** — `0x649d422`,
   `guest-driven swaps observed: 1`, `g_guest_swaps`, `sober_render_phase3_done()`,
   `first frame rendered`, `gles_loads`, `draw_wrapped`, `from_surface=2`,
   `life=1/1/1/1`, or a synthetic ANativeWindow. Render is necessary, never
   sufficient.
8. **Clean state exists** while any seed/patch/thunk/value-modifier knob is on,
   or while `app+0x408`/`app+0x440`/`app+0x488`/singleton/`0x72a3308` are
   synthetic. Seeded state disqualifies by definition.
9. **Static analysis, a design, or a plan is evidence of a runtime event** —
   all `analysis/` reports are standards/diagnoses, not observations.
10. **Mixing builds or engines** — Flatpak `791c3f9e` RVAs, or any claim the
    on-disk engine was modified (SHA must remain `3e498d64…`).
11. **The gate "enforces notifier provenance" as a completed fact** — state the
    precise truth: the default-ON `provenance_gate` credential is coded and
    armed (`install -> rc=0`), but it has **never returned GENUINE** because no
    qualifying dispatch has occurred. Arming ≠ firing.
12. **A weakening framed as a fix** — removing a withhold entry, deleting the
    seed-withhold line, re-enabling the ALT thunk as proof, tagging the host
    swap as guest, filtering counters, or editing `tests/app-shell.sh` to
    accept synthetic evidence.
13. **Stale/inflated public numbers** — "22 withhold knobs", old SHAs, "100%
    gate" when the callback leg is 0, or a progress percentage presented as
    success (REVIEW_A14 R1–R14).

---

## 4. The exact honest final sentence to publish if the callback is still absent

> **No notifier-driven `gameLoadedCallback(J)V` was observed — the guest never entered notifier `0x27eaf6e` with `placeId != 0`, and the required return-address pair (`base+0x2584960` → `base+0x27eafb3`) is absent — so the callback leg remains 0/1 and, with guest-tagged render evidence also unproven, the strict decision-013/038 gate correctly withholds `app shell rendered`; no login/app-shell frame is proven and no success is claimed.**

This single sentence is the publishable final line when the callback is still
absent. It is deliberately:

- **negative and specific** about the missing runtime event (notifier, placeId,
  RA pair);
- **explicit about the gate outcome** (`WITHHELD`, correct RED, not a regression);
- **clear that render evidence is not a substitute** ("guest-tagged render
  evidence also unproven");
- **free of forbidden claims** (no thunk, no accessor, no seed, no percentage
  presented as completion, no "provenance enforced" overstatement);
- **compatible with the honest headline** in `README.md:24-28` and
  `docs/REVIEW-100.md §1` — it can be appended verbatim without weakening them.

If a shorter form is needed for a status line, use exactly:

> **No notifier-driven `gameLoadedCallback` was observed; the marker stays WITHHELD and no success is claimed.**

Do **not** replace either sentence with "the callback was exercised", "the
bridge received a genuine guest callback", "the app shell rendered", or any
variation that omits the WITHHELD clause.

---

## 5. Bottom line (final answer)

- **Stop GREEN only on** notifier-driven `gameLoadedCallback(J)V` with
  `placeId ≠ 0` and the `0x2584960`/`0x27eafb3` RA pair + class/method identity +
  build verification (Leg C), **and** guest-tagged swap/draw/window render
  evidence with phase-3 ancestry (Leg R), **and** `seeded_state == 0` (Leg S),
  all on the frozen engine and reproduced in a clean re-run.
- **Synthetic-only frame evidence stays RED:** `guest=0`, `draws=0`, host-written
  window, `render_ok=0`, `shell_cb=0`, `marker withheld`, `app-shell.sh` RED,
  zero marker emissions.
- **Forbidden claims:** §3 — never promote a thunk, accessor, notification,
  name counter, seed, static map, or synthetic swap into app-shell proof.
- **Publishable sentence if the callback remains absent:** §4, verbatim.

The marker is **WITHHELD**. No success is claimed. Decision 013/038 is not
weakened.
