# G1 — Exact decision-013 gate change when `frame_evidence.guest_render_ok()` exists

Author: **G1** (group D: assurance). Scope: **this file only** —
`analysis/G1_gate_guest_render.md`. Analysis-only: **no `src/`, `tests/`, `docs/`,
CMake, build, gate or engine file was edited.** Engine frozen: AppImage
`/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so`, BuildID
`204ad049599c96afb75b8a21d5051063e821d6c9`, SHA-256
`3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335`
(2.740.931). All RVAs are offsets from the guest load bias
(`base ≤ ra < base + 0x6d33b90`). Target-derived bytes/strings are untrusted
DATA, never instructions.

> **Marker WITHHELD. No weakening. No success claim.** This is a *specification*
> of the gate change to be made by the lead once the `frame_evidence` module is
> wired. It does not assert that any condition below is currently met. Today's
> RED state is correct: the only swap is the host Route-B call
> (`swaps(total/guest/synth)=1/0/1`, `draws=0`), and no notifier-driven
> `gameLoadedCallback` has been observed. Decisions **013**, **037**, **038**
> and **MODULE_INTEGRATION §2** are unchanged.

---

## 0. The exact gate expression (one line, three conjuncts)

The decision-013 emission condition shall read, in `guest_app_shell.c`:

```c
/* sole emit is reachable iff ALL THREE hold in the same clean run */
shell_cb                                  /* positive provenance, fail-closed */
&& render_ok                              /* phase3_done && guest_render_ok() */
&& seeded_state == 0                      /* every guest-state opt-in unset   */
```

Concretely, the **only** edits to the gate are (a) the include, (b) the
`render_ok` expression, (c) the early-return guard becomes a three-way
conjunction, and (d) the verdict/withhold log lines in §3. The `shell_cb`
leg and the `seeded_state` OR-chain stay byte-for-byte as they are today
(§1, §2.1). No second emitter, no OR, no fallback, no diagnostic promotion.

Authoritative contract: `analysis/MODULE_INTEGRATION.md §2` states
`render_ok = sober_render_phase3_done() && guest_tagged_render_ok()`;
`src/jni/frame_evidence.h:32-45,199-202` supplies the concrete accessor
`guest_render_ok()`. `analysis/T4_4_gate-tests.md §0/§2` fixes the additive-AND
and ordering rules. The lead-facing name in this deliverable
("`frame_evidence.guest_render_ok()`") is the C function `guest_render_ok()`
declared in `frame_evidence.h:202` — not a struct method.

---

## 1. Preconditions the gate change assumes (all lead-owned, none in scope here)

The predicate is only sound if the observation wiring exists. If any item is
missing, `guest_render_ok()` fails closed (returns 0) and the marker stays
WITHHELD — which is the correct behaviour, not a defect:

1. `#include "frame_evidence.h"` in `guest_app_shell.c` (beside
   `render_guest.h:49`, `provenance_gate.h:50`) and `frame_evidence.c` added to
   the `sober_jni` source list in `src/jni/CMakeLists.txt`.
2. `frame_evidence_set_guest_base(guest_base)` once after the guest is mapped
   (`frame_evidence.h:62-63,171`). With base 0, `fe_ra_tag()` returns UNKNOWN,
   every swap/draw is synthetic and the predicate is 0
   (`frame_evidence.c:132-139,178-181`).
3. **Every** GOT `0x722a1a8` swap traversal is reported exactly once via
   `frame_evidence_record_swap((uintptr_t)__builtin_return_address(0))`
   (`frame_evidence.h:64-65`), *including* the tagged host Route-B swap —
   otherwise the `MIXED` taint (`frame_evidence.c:219-223`) could miss
   contamination. `frame_synthetic_tag` (`SOBER_FRAME_SYNTH_TAG`) is
   *defence in depth* around the direct call at `guest_render.c:354`
   (`frame_synthetic_tag.h:63-92`): it forces the synthetic bucket even if an
   RA heuristic were fooled; it never suppresses the swap call.
4. Each counted guest GLES draw reports its RA via
   `frame_evidence_record_draw(ra)` (`frame_evidence.h:66-67`).
5. After the fromSurface/window-global path,
   `frame_evidence_observe_window_global(guest_base, ra)` is called
   (`frame_evidence.h:68-70`). `window_bound` requires non-NULL window, guest
   RA at `0x26fea78`/`0x2a2087c`/`0x2a20907`, and a strictly increasing
   generation at `0x72a3310` (`frame_evidence.c:274-304`). A fromSurface hook
   is supporting evidence only, **not** a gate requirement
   (`MODULE_INTEGRATION.md §2`).
6. `sober_provenance_gate_install(base)` and the dispatch hook remain as today
   (`guest_app_shell.c:978-979`, `native_jni.c:513`), so `shell_cb` is the
   *positive* credential, not a name count.

The module is default-ON and reads no `SOBER_*` in its proof path. Its kill
switch `SOBER_FRAME_EVIDENCE=0` makes every decisive accessor return 0 and
`frame_evidence_verdict()` return DISABLED (`frame_evidence.c:329-350,431-444`)
— fail-closed.

---

## 2. Exact code change (lead applies; shown as a diff)

### 2.1 Untouched (re-affirm, do not edit)

`guest_app_shell.c:1595-1597` — the positive callback leg stays exactly:

```c
int shell_cb = sober_provenance_gate_is_genuine("gameLoadedCallback")
            || sober_provenance_gate_is_genuine("onAppShellReloadNeeded");
if (sober_provenance_gate_is_harness_thunk()) shell_cb = 0;
```

`sober_provenance_gate_is_genuine()` returns 0 for any non-qualifying name, for
`onAppBridgeNotification`, for a kill-switched recorder, and — critically —
for a run with **any** HARNESS-THUNK observation, because `is_genuine()` itself
checks the sticky `g_harness` taint (`provenance_gate.c:402-410`). The second
name in the OR is therefore inert today and the explicit
`is_harness_thunk()` override is retained as defence in depth. **Never**
replace this leg with `sober_jni_callback_was_called()`, and never drop the
harness-thunk override.

`guest_app_shell.c:1608-1766` — the `seeded_state` OR-chain is unchanged: all
`getenv(...)` terms, the five prefixes, and the module
`..._should_withhold()` verdicts (including
`sober_notifier_probe_should_withhold()`). The block at `:1764-1768` that logs
`marker withheld: diagnostic guest-state seed active (no app-shell frame
proven)` and sets `shell_cb = 0` is unchanged.

### 2.2 The render leg — replace lines 1777-1780

```diff
-    int render_ok = sober_render_phase3_done()
-                 && sober_render_guest_swaps_guest_tagged() >= 1
-                 && sober_render_guest_gles_draw_calls() >= 1
-                 && *(void **)(base + 0x72a3308u) != NULL;
+    /* DECISION 013 render leg (G1): phase-3 ancestry AND the frame_evidence
+     * guest-tagged predicate.  guest_render_ok() re-reads the persisted phase3
+     * flag through a weak symbol and fails closed on SOBER_FRAME_EVIDENCE=0,
+     * on MIXED evidence and on any missing render leg; the explicit phase3
+     * term is kept as the contract's separate ancestry condition. */
+    int phase3_done = sober_render_phase3_done();   /* unchanged ancestry    */
+    int guest_render = guest_render_ok();           /* decisive, fail-closed */
+    int render_ok = phase3_done && guest_render;
```

Then insert the verdict/withhold lines from §3 (after all predicates are
computed, before the early return).

### 2.3 The decision guard — line 1782

```diff
-    if (!shell_cb || !render_ok) {
+    if (!shell_cb || !render_ok || seeded_state) {
```

The third term is *explicit* even though `seeded_state` already zeroes
`shell_cb` at `:1767`. It is redundant by design: it makes the required
three-way conjunction visible at the emit boundary and cannot turn a withhold
into a green. The body of the `if` (failure log at `:1783-1786`, the
`SOBER_APP_SHELL_DIAGNOSTIC` note, JNI shutdown, `return -4`) is **unchanged**.

### 2.4 The sole emit — unchanged

`guest_app_shell.c:1807` remains the single call site:

```c
emit_marker("app shell rendered\n", "SOBER_SHELL_MARKER");
```

No new `emit_marker`, no new marker token, no diagnostic marker via
`frame_evidence`, `frame_synthetic_tag`, `provenance_gate` or `render_guest`.

### 2.5 Why this is strictly narrowing (no weakening)

| Old conjunct (`guest_app_shell.c:1777-1780`) | Replacement | Why the replacement is stronger, not weaker |
| --- | --- | --- |
| `sober_render_phase3_done()` | same term **kept**, plus `guest_render_ok()` re-reads it (`frame_evidence.c:135-139,347-352`) | same flag checked twice; nothing removed; absent render module ⇒ 0 |
| `swaps_guest_tagged >= 1` (RA interval only) | `frame_evidence_swaps_guest() >= 1` | same `[base, base+0x6d33b90)` RA test (`frame_evidence.c:141-149`) **plus** a write-once audit record with seq/timestamp/RA/tag, an explicit host-swap tag, and MIXED taint zeroing the decisive count |
| `gles_draw_calls >= 1` (unconditional trampoline sum) | `frame_evidence_draws() >= 1` | counts **only guest-RA** draws; synthetic draws separated; taint zeroing; `draw_calls` is host-forgeable in `render_guest.c:142-150`, the new term is not |
| `*(void **)(base+0x72a3308) != NULL` | `frame_evidence_window_bound()` | old term is satisfied by the host store at `render_guest.c:326-329`; new term additionally needs guest RA at one of three sites and a strictly increasing generation at `0x72a3310` |

Every removed term is subsumed and tightened; no OR is introduced anywhere.
The phase-3-only state that produced the honest RED log `callback=0 render=1`
can never satisfy the new render leg.

---

## 3. Log strings and exact emission conditions

All strings below are emitted by the **gate itself** on `stderr` (the
`frame_evidence` module writes no file/stderr: `frame_evidence.h:234-236`).
Prefixes are fixed: `[app-shell]`, `[render]`, `[callback-provenance]`
(`T4_4_gate-tests.md §1.3`). A green run contains **all** positive lines of
§3.1 and **none** of §3.2/§3.3. Anchor the greps to the prefixes.

### 3.1 Positive (emit only when that leg is proved)

```
[app-shell] clean state: seeded_state=0 (all opt-ins unset)
[app-shell] gate provenance: notifier-driven gameLoadedCallback at 0x27eaf6e placeId=<N> (RSI>0), class/method cached non-NULL, return 0x27eafb3 over thunk 0x2584960 -> GENUINE
[render] guest-tagged swap at 0x649d422 ra=base+<RA_IN_GUEST_TEXT> tag=guest-loop | guest-tagged swaps: <N> | guest GLES draws between tagged swaps: <M>
[render] guest render verdict: guest-tagged frame confirmed (swaps=<N> draws=<M> fromSurface=installed window=bound) -> GUEST-TAGGED
[app-shell] render provenance: guest-tagged frame at 0x649d422 (swaps=<N>, draws=<M>), phase3_done=1 -> OK
```

- `clean state:` is emitted iff `seeded_state == 0` (`T4-4 §2` step 4).
- `gate provenance: … -> GENUINE` is the gate's own decisive provenance line
  (it already satisfies `tests/app-shell.sh:129-133`); `placeId=<N>` must be
  decimal and `> 0`. It must be *emitted by the gate*, never synthesised by the
  test. The authoritative `provenance_gate` currently exposes no placeId
  accessor (`provenance_gate.h:201-217`); the lead must add a read-only getter
  or emit a reduced line that still carries every decisive substring
  (`notifier-driven gameLoadedCallback`, `0x27eaf6e`, `0x27eafb3`,
  `-> GENUINE`). This is a wording/accessor obligation only — it does not
  change the `shell_cb` predicate.
- `render provenance: … phase3_done=1 -> OK` is emitted **iff** `render_ok`
  is true; the literal `1` is then guaranteed by the explicit `phase3_done`
  conjunct. `<N>`/`<M>` come from `frame_evidence_swaps_guest()` and
  `frame_evidence_draws()` (decisive, never raw).
- The two `[render]` lines are normative contract strings (`T4-4 §1.3`); their
  emitter is the wiring site, not the gate. `ra=base+<RA_IN_GUEST_TEXT>` must
  lie in `[base, base+0x6d33b90)`; a host RA or `tag=synthetic` fails R1.

### 3.2 Negative / withheld (must remain available; forbidden in a green run)

Keep unchanged:

```
[app-shell] marker withheld: diagnostic guest-state seed active (no app-shell frame proven)
[app-shell] marker withheld: callback=%d render=%d (env calls=%llu callbacks=%llu last='%s')
[app-shell] DIAGNOSTIC ONLY (no marker): JNI bridge exercised, app shell frame NOT proven
[app-shell] HONESTY NOTICE: SOBER_ALT_CALLBACK_PATH exercised a genuine guest->Java callback via a harness-triggered thunk; NO login/app-shell frame is proven by this run.
```

Additive, emitted by the gate on the failing leg only:

```
[app-shell] render evidence: <frame_evidence_verdict_string()>
[app-shell] marker withheld: guest-tagged render absent (phase-3 swap not guest-provenanced)
[app-shell] marker withheld: callback provenance absent (no notifier-driven gameLoadedCallback)
[app-shell] marker withheld: callback provenance tainted (HARNESS-THUNK)
```

- `render evidence:` uses `frame_evidence_verdict_string()` verbatim
  (`frame_evidence.c:457-490`), so the run-level reason is auditable:
  `MIXED -> WITHHELD (guest swaps=… synthetic swaps=…)`,
  `DISABLED (SOBER_FRAME_EVIDENCE=0) -> WITHHELD`,
  `ABSENT(swaps_guest|draws|window) -> WITHHELD`, or
  `INCOMPLETE(swaps=… draws=… window=…) -> WITHHELD`.
  Emit it on **every** run (including green, where it reads
  `GUEST-TAGGED (swaps=… draws=… window=bound)`), or at minimum on every
  withhold; it carries no forbidden substring.
- `callback provenance tainted (HARNESS-THUNK)` is emitted iff
  `sober_provenance_gate_is_harness_thunk()` is set. `tests/app-shell.sh:129-133`
  and `§3.3` forbid the literal `HARNESS-THUNK` in a green run; emitting it on
  the withhold path is exactly right.
- The generic `callback=%d render=%d` line (`:1783-1786`) is unchanged; on the
  current default path it remains the honest `callback=0 render=0`.

### 3.3 Emission order (must be respected)

1. compute `shell_cb`, `seeded_state` (with `shell_cb=0` override),
   `phase3_done`, `guest_render`, `render_ok` — silently;
2. emit `render evidence:` and the positive/negative render lines of §3.1/§3.2;
3. emit `clean state:` iff `seeded_state==0`; emit the harness/absence line for
   the callback leg;
4. evaluate `if (!shell_cb || !render_ok || seeded_state)` → failure log,
   JNI shutdown, `return -4`;
5. only past that guard: the (now unreachable under ALT) HONESTY NOTICE and the
   single `emit_marker(...)`.

No marker decision is made before step 4; no test-only string synthesis.

---

## 4. Negative conditions — mechanism and resulting gate values

### 4.1 HARNESS-THUNK (callback leg)

`SOBER_ALT_CALLBACK_PATH` drives `0x25848e0` from host code
(`guest_app_shell.c:1424-1436`). The provenance recorder classifies the host
caller RA as `HARNESS_THUNK` and sets sticky `g_harness`
(`provenance_gate.c:393-397,412-415`). Consequences: `is_genuine()` returns 0
for **every** name (`provenance_gate.c:406-407`), the gate's explicit override
sets `shell_cb=0`, the knob is the first `seeded_state` term
(`:1608`), and the HONESTY NOTICE at `:1803-1806` is unreachable behind the
withhold. Gate emits the `HARNESS-THUNK` withhold line. Mixed
GENUINE + HARNESS-THUNK ⇒ withhold (T4-4 §0.3), never "one positive wins".
`onAppBridgeNotification` (`verdict=appbridge_disqualified`) and any name-only
`callback_was_called` remain non-qualifying.

### 4.2 Synthetic swap (render leg)

The default Route-B call at `guest_render.c:354` has a **host** RA
(`R1_zero_tagged_swaps.md §3`, `R6_swap_provenance.md §3`): the swap wrapper
`0x649d422` tail-jmps, so the RA at GOT `0x722a1a8` is `guest_render.c`'s
return site. It is therefore recorded as `tag=synthetic`; the native phase-3
swap (S1) never traverses the guest GOT at all. The F8 explicit tag adds a
second, host-controlled synthetic classification in `rg_hook_swap`
(`frame_synthetic_tag.h:74-92`). A synthetic swap **never** increments
`frame_evidence_swaps_guest()`. T4-4 §3.3 forbids `tag=synthetic` in a proof
line; `tests/first-frame.sh` keeps its legacy `guest-driven swaps observed: 1`
line — that counter is ancestry, not proof.

### 4.3 MIXED evidence (both legs, fail closed)

Render: `fe_update_mixed()` (`frame_evidence.c:219-223`) sets sticky `s_mixed`
when both a guest-RA swap and a synthetic swap are recorded in the run; the
decisive accessors then return 0 and `guest_render_ok()==0`
(`:329-352`). The raw counts remain in the audit log for diagnosis only.
Callback: sticky HARNESS-THUNK taint as in §4.1. Neither taint can be cleared
by a later positive observation. The gate has no "mixed is acceptable"
branch and no suppression path.

### 4.4 Kill switches and clean-state escape attempts

- `SOBER_FRAME_EVIDENCE=0` → `frame_evidence` inactive, verdict DISABLED,
  `guest_render_ok()==0` (`frame_evidence.c:329-350`).
- `SOBER_FRAME_SYNTH_TAG=0` → the explicit host tag is inert, but the RA
  classifier and `frame_evidence`'s own guest/synthetic split remain; no green
  can be manufactured (`frame_synthetic_tag.h:42-50`).
- `SOBER_PROVENANCE_GATE=0` → no GENUINE credential possible
  (`provenance_gate.h:50-58`).
- These three are **fail-closed kill switches, not guest-state seeds**. They
  deliberately stay **outside** the `seeded_state` OR-chain, exactly as the
  provenance gate is treated today (`guest_app_shell.c:1756-1757`). Adding them
  to the chain would be harmlessly stronger but would mislabel "disabled
  evidence" as "manufactured state"; omitting them cannot create a false green
  because each independently forces the relevant leg to 0. **No new opt-in may
  be introduced as a way to obtain proof.** Any future knob that can
  *suppress, filter, redirect or fake* an observation must join `seeded_state`
  in the same change (`T4-4 §4.1`, `MODULE_INTEGRATION.md §3`).
- `SOBER_CALLBACK_PROVENANCE`, `SOBER_NOTIFIER_PROBE` and every synthetic flag
  must remain withhold entries; they are diagnostics, never proof.

---

## 5. The sole `emit_marker` remains valid

- **Uniqueness:** repo-wide grep shows exactly one emitter of the
  `app shell rendered` marker: `src/linux/guest_app_shell.c:1807`,
  `emit_marker("app shell rendered\n", "SOBER_SHELL_MARKER")`, using the
  static helper at `:61-72` (stdout write + optional marker file). The other
  `emit_marker` in `guest_render.c:26,477` emits the distinct
  `first frame rendered` / `SOBER_FRAME_MARKER` pair and is untouched. No new
  module (`frame_evidence`, `frame_synthetic_tag`, `provenance_gate`,
  `render_guest`) calls `emit_marker` or writes `SOBER_SHELL_MARKER`.
- **Control flow:** `emit_marker` at `:1807` is after the single
  `if (!shell_cb || !render_ok || seeded_state) return -4;` guard (`:1782`,
  body through `:1800`). With the new three-way conjunction there is no path
  that reaches the emitter with any conjunct false. `alt_path` is set only by
  `SOBER_ALT_CALLBACK_PATH` (`:1400`), which is in the withhold chain (`:1608`)
  and zeroes `shell_cb`, so the HONESTY NOTICE just before the emit is
  unreachable whenever ALT is set — the strengthened state today.
- **Observables unchanged:** `tests/app-shell.sh §4` still requires `rc==0`,
  marker file present and exactly one `^app shell rendered$` on stdout; the
  script's own trailing `echo` remains a banner, not a second emission
  (`T4-4 §3.4`). All negative controls (default, ALT, any opt-in, MIXED,
  DISABLED, HARNESS-THUNK) emit **zero** markers.
- **Diagnostic isolation (038 §4):** no second token/file/function is
  introduced; diagnostic lines in §3 are `fprintf(stderr, …)` only.

---

## 6. No-weakening checklist / invariants to re-check after integration

1. Engine BuildID/SHA unchanged; no Flatpak RVA used; `ldd` libc-only; native
   PIE; `ctest` 2/2; no new diagnostic registered in ctest/CMake/verify-native.
2. `guest init complete` and `first frame rendered` still green on the default
   path; `guest call 0x649d422` and `guest-driven swaps observed: 1` preserved.
3. All three gate conjuncts present **simultaneously**; no OR substitution;
   `shell_cb` still positive provenance with the harness-thunk override;
   `seeded_state` chain, its five prefixes, alias pairs (`STARTAPP_V2` /
   `STARTAPP_PARAMS`, `HELPER_REPAIR` / `_V2`) and every module
   `..._should_withhold()` verdict intact.
4. `render_ok` requires `phase3_done` **and** `guest_render_ok()`; neither leg
   alone can emit; `guest_render_ok()` itself fails closed on DISABLED, MIXED,
   ABSENT and INCOMPLETE (`frame_evidence.c:329-352,431-444`).
5. One run, one conjunction: notifier provenance, guest-tagged swap/draw and
   clean state all observed in the **same** run on the frozen build
   (`T4-4 §0.4`).
6. `tests/app-shell.sh §3.2` positive greps and `§3.3` absence checks
   (including `HARNESS-THUNK`, `ALT PATH`, `HONESTY NOTICE`, seed-withhold,
   `tag=synthetic` as proof) all hold; the test stays RED until the real lines
   exist. No test-only string synthesis.
7. Exactly one marker emitter and one marker string/token; all withhold
   controls emit zero markers.
8. Withhold coverage audit (`D09`) extended to the new TUs that read `SOBER_*`
   (`frame_evidence`, `frame_synthetic_tag`, `provenance_gate`); exit 0 alone
   is not evidence (`T4-4 §4.9`).

### Open items for the lead (not resolved here)

- **Route-B vs MIXED:** the host Route-B swap is currently unconditional
  (`frame_synthetic_tag.h:31-35`). If **every** GOT traversal is reported to
  `frame_evidence_record_swap`, a genuine guest frame in the same run would be
  classified MIXED and withheld; if the lead instead scopes recording so the
  tagged host swap is excluded, that exclusion must be explicit and logged,
  and any knob driving it must be a withhold entry (`T3-5` §3.5/§5 E5,
  `T4-4` §5). Either ruling leaves the gate expression in §0 unchanged and
  keeps MIXED ⇒ WITHHELD; do not resolve it by ignoring the taint in the gate.
- **`fromSurface=installed` wording:** F7 treats fromSurface as supporting
  evidence (`MODULE_INTEGRATION §2`), so no gate conjunct is added for it.
  T4-4 §4's fixed field is a wording issue only; it must not motivate
  installing a synthetic hook in a proof run (`T3-5 §4` note).
- **Phase3 double read:** the explicit `phase3_done` term is intentionally
  redundant with `guest_render_ok()`'s internal weak read. Do not remove it in
  this change; if a later lead change does, document that `guest_render_ok()`
  still enforces phase3.

---

## 7. Evidence index

| Claim | Source (read-only) |
| --- | --- |
| Current gate expression, sole emit, withhold chain | `src/linux/guest_app_shell.c:61-72,978-979,1400,1595-1597,1608-1768,1777-1782,1803,1807` |
| Positive provenance API / HARNESS-THUNK taint | `src/jni/provenance_gate.h:19-58,184-199`; `src/jni/provenance_gate.c:393-415` |
| `guest_render_ok()` semantics, MIXED, verdict strings | `src/jni/frame_evidence.h:22-45,191-236`; `src/jni/frame_evidence.c:135-149,219-223,274-352,431-490` |
| Explicit host-swap tag | `src/jni/frame_synthetic_tag.h:24-50,63-96,137-153` |
| Render evidence contract | `analysis/MODULE_INTEGRATION.md §2,§3`; `analysis/T3_5_render-evidence.md §4-§5`; `analysis/T4_4_gate-tests.md §0,§1.3,§1.4,§2,§3,§4` |
| Diagnoses of today's synthetic-only state | `analysis/R1_zero_tagged_swaps.md §1-§5`; `analysis/R6_swap_provenance.md §2-§6` |
| Gate/test invariants and negative greps | `tests/app-shell.sh:105-140`; `tests/D12-final-invariants.md`; `analysis/REVIEW_A8_gate.md §5-§8` |
| Rulings | `docs/decisions/013`, `037`, `038`; `analysis/REVIEW_A8_gate.md §0` |

---

## 8. Honesty statement

No login/app-shell frame has been proven. No notifier-driven
`gameLoadedCallback`, no guest-tagged swap and no guest GLES draw exist in any
log consulted here; the only gate-relevant observation is the host Route-B
swap (`1/0/1`, `draws=0`). The `app shell rendered` marker is **WITHHELD** and
that is the correct state. This deliverable proposes no bypass, no OR, no
second emitter, no test-only string, no removal from the withhold chain and no
success claim. It only specifies the exact, strictly narrowing gate change to
be applied once `frame_evidence.guest_render_ok()` is wired, and records the
negative conditions that must keep the marker withheld until a genuinely clean
run proves every conjunct.
