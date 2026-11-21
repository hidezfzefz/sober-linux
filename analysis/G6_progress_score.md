# G6 — Updated weighted progress score after the frame work lands (proposal)

Author: **G6** (group D). Scope: **this file only** —
`analysis/G6_progress_score.md`. Analysis-only; no `src/`, engine, gate, test,
CMake, decision or shared doc file was touched. This is a **proposal** for the
lead to consider, not an adopted score and **not a success claim**.

Engine frozen: AppImage BuildID `204ad049599c96afb75b8a21d5051063e821d6c9`,
SHA-256 `3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335`
(2.740.931). Marker **WITHHELD** under decisions 013/038 in **every** scenario
below. No Flatpak (`791c3f9e`) RVA is used.

---

## 0. Scope of the question

`docs/REVIEW-100.md` §2 fixes the rubric and the current score:

| Category | Weight | Current score | Contribution |
| --- | --- | --- | --- |
| Guest init / loader / bootstrap | 20% | 0.95 | 19.00 |
| Native render / frame | 15% | 0.60 | 9.00 |
| JNI bridge / typed fidelity | 15% | 0.85 | 12.75 |
| App state (singleton, controller, delegate, params) | 20% | 0.40 | 8.00 |
| Genuine app-shell callback | 20% | 0.00 | 0.00 |
| Gate / tests / docs / delivery | 10% | 0.85 | 8.50 |
| **Total** | **100%** | — | **57.25 ≈ 57%** |

The "frame work" is the render/frame module set now landed in `analysis/`
(`P1_render_vtable.md`, `R1_zero_tagged_swaps.md`, `R2_got_hook_reachability.md`,
`R6_swap_provenance.md`, `P8_anw_guest_consumption.md`) plus the `render_guest`
GOT-hook / lifecycle wiring described in `docs/REVIEW-100.md` §6 (worker-3 #478)
and `analysis/MODULE_INTEGRATION.md` §2. Its runtime result is the only thing
this proposal lets move: the **Native render / frame** leg.

**Held constant (explicit, per the task framing):**

- **Callback leg = 0.00.** No genuine notifier-driven `gameLoadedCallback(J)V`
  is observed (notifier `0x27eaf6e`, RA `base+0x27eafb3`, `placeId>0`). Nothing
  in the frame work touches Leg C. Marker cannot lift on render evidence alone.
- **App state = 0.40** — "unchanged" by instruction; the frame work does not
  populate singleton / controller / delegate / `app+0x488` on the clean path.
- **Guest init 0.95, JNI 0.85, Gate/tests/docs/delivery 0.85** — unchanged;
  these are out of scope for this proposal and I do not credit the frame work
  with cross-category gains (e.g. no positive-provenance bump to the gate leg).

Constant subtotal = 19.00 + 12.75 + 8.00 + 0.00 + 8.50 = **48.25**.
Only `15 × render_score` is varied below.

---

## 1. What "render leg" actually measures here

The render contract (`MODULE_INTEGRATION.md` §2, `guest_app_shell.c:1777-1780`)
is `render_ok = phase3_done && guest_tagged_swaps ≥ 1 && gles_draw_calls ≥ 1 &&
*(base+0x72a3308) != NULL`. Two **decisive guest signals** are currently zero:

1. **guest-tagged swap** — a `call QWORD PTR [vtable+0x18]` (slot `0x7191300`
   = `0x649d422`) leaving a guest `.text` RA at GOT `0x722a1a8`;
2. **counted guest GLES draw** — invocation of a wrapped proc from the
   `0x6496d0c` dispatcher, not the host `dlsym`'d `glClear`.

Current honest state is `swaps(total/guest/synth)=1/0/1`, `draws=0`,
`gles_loads=48`, `draw_wrapped=5`, `from_surface=2`, `life=1/1/1/1`
(`R1`, `R2`, `R6`, `P8`, REVIEW §6). The single swap is the host Route-B call at
`guest_render.c:354`; its RA is in the host PIE, so tagging it `guest` would be a
false green. The window global is **host-written** (`render_guest.c:326-329`),
and the lifecycle callbacks `[+0x88]/[+0x98]/[+0xa0]` are NULL, so no guest frame
loop runs. That is why 0.60 — not higher — is the honest baseline.

The three scenarios are indexed by how many of the two decisive guest signals
become non-zero at runtime, **with fail-closed semantics preserved** (mixed
synthetic/guest evidence never counts as guest; a synthetic Route-B swap can
never make `guest>0`, per `R6` §5).

---

## 2. Three scenarios

All rows keep the five constant categories above. Contributions are weight ×
score; totals are contributions summed.

| Scenario | Init 20% | Render 15% | JNI 15% | App 20% | Cb 20% | Gate 10% | Render contrib | **Total** |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| **A — frame modules fail** (swaps 1/0/1, draws 0) | 0.95 | **0.60** | 0.85 | 0.40 | 0.00 | 0.85 | 9.00 | **57.25 ≈ 57%** |
| **B — partially work** (exactly one guest signal positive; other 0; gate fails closed) | 0.95 | **0.70** | 0.85 | 0.40 | 0.00 | 0.85 | 10.50 | **58.75 ≈ 59%** |
| **C — fully work** (guest-tagged swap ≥1 **and** counted guest draw ≥1; seed-driven, window host-written) | 0.95 | **0.85** | 0.85 | 0.40 | 0.00 | 0.85 | 12.75 | **61.00 ≈ 61%** |

### Scenario A — frame modules fail — 57.25 ≈ 57% (no change)

The landed frame modules remain diagnostic-only: `guest-tagged swaps = 0`,
`draws = 0`, the only GOT swap is the host Route-B call, `[nativeData+0x98]` is
still NULL, and the window global is still host-written. The render leg earns
no credit beyond the already-proven narrow claims (native frame, guest first
frame at `0x649d422`, GOT hooks/lifecycle installed, vtable decoded). **Total
stays at the REVIEW-100 baseline.** This is the expected outcome if the frame
work is analysis-only and no default-path behaviour changes.

### Scenario B — partially work — 58.75 ≈ 59%

Exactly one decisive guest signal turns positive while the other stays zero —
e.g. the redraw callback is made non-NULL and a **guest-tagged swap** appears
(`guest ≥ 1`, RA in guest `.text` after `call *0x18(%reg)`), but no counted
guest GLES draw occurs; **or** a guest draw is issued but no guest-RA swap is
captured. Because the contract is an AND, `render_ok` is still **0** and the
gate still fails closed (`render=0`). I credit a modest render bump 0.60 → 0.70
(+1.5 pts total) because *one* of the two guest render mechanics is now
demonstrated, but not the composed frame. Honest limit: a single tagged swap
could be a one-off (R6 requires loop stability across ≥2 swaps and a bracketed
draw), and a draw without a guest swap proves no frame boundary. No marker, no
success claim.

### Scenario C — fully work — 61.00 ≈ 61%

Both decisive signals are positive: a stable guest-tagged swap (same RA across
≥2 swaps, `call *0x18(%reg)` opcode verified at `ra-5`) **and** ≥1 guest-RA
GLES draw bracketed between tagged swaps. The render mechanics are then proven
to work end-to-end, so the render leg rises meaningfully, 0.60 → 0.85
(+3.75 pts total). I deliberately **cap at 0.85, not 1.00**, because:

- the frame is still driven by **seeded** nativeData callback pointers, not the
  guest's own GameActivity-built callbacks under clean state;
- `*(base+0x72a3308)` remains a **host-written** false-green conjunct
  (`R1` §5 item 3, `P8` §6 item 2) — window provenance is not guest-bound;
- the fromSurface hook returns a synthetic `SoberGuestANativeWindow` and the
  gate's diagnostic seed-withhold line must keep firing;
- the genuine notifier callback (Leg C) is still absent, so this is
  *observability of a guest frame path*, not proof of a login/app-shell frame.

Even here the total reaches only **61%**. Under the held-constant assumptions
the arithmetic ceiling with callback = 0 and app state = 0.40 is 68%
(20+15+15+8+0+10); the remaining headroom is gated by Leg C and clean app
state, which the frame work does not address. This is consistent with
REVIEW-100 §2's point that the callback and app-state legs dominate the goal.

---

## 3. Cross-scenario caveats (why these are proposals, not claims)

1. **Marker WITHHELD in A, B and C.** Decision 013/038 requires genuine
   notifier-driven callback + guest render + clean state. No render-side result
   alone lifts it; the seed-withhold line must keep firing in B and C.
2. **Fail-closed counting is assumed.** If the implementation were to tag the
   host Route-B swap as `guest`, or count the host `dlsym`'d `glClear`, the
   counters would be false greens and these scores would be invalid — they must
   be rejected, not scored upward (`R1` §1, `R6` §5, `R2` §9).
3. **These are upper-bound estimates for the render leg**, contingent on the
   frame modules actually producing the stated runtime evidence with all
   `SOBER_*` opt-ins off and the A/B kill-switch controls passing
   (`MODULE_INTEGRATION.md` §1; `R2` §8 T0–T4). If the "full work" in C is
   achieved only with a seed knob ON, the honest render score is B or lower.
4. **Nothing here changes the headline verdict of REVIEW-100 §1:** the goal
   (native ELF + genuine notifier-driven guest callback + rendered app-shell
   frame under clean state) remains **NOT PROVEN**. The highest proposed score,
   61%, still carries a hard 0% on the decisive callback leg.
5. **Suggested presentation to the lead:** adopt A as the current standing
   score (57%); hold B/C as conditional projections pending the final
   verification task (#480) and a real A/B log showing the counter triples. Do
   not publish a number without that log.

---

## 4. Bottom line (final answer)

Using the REVIEW-100 §2 rubric and holding every leg but render fixed:

- **Frame modules fail:** render 0.60 → total **57.25 ≈ 57%** (unchanged).
- **Frame modules partially work** (one guest signal positive, gate fails
  closed): render 0.70 → total **58.75 ≈ 59%**.
- **Frame modules fully work** (guest-tagged swap + guest draw, but seeded and
  host-written window): render 0.85 → total **61.00 ≈ 61%**.

In all three the callback leg is 0.00 and **no success is claimed**; the marker
stays **WITHHELD**. The decisive gap (genuine notifier callback, clean guest
state) is untouched by the frame work.
