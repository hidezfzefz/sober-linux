# G2 — Gate / test assertion contract for the render leg and the positive-provenance leg

Author: **G2** (group D). Scope: **this file only** — `analysis/G2_gate_test_contract.md`.
Analysis and test-interface design only: **no `src/`, `CMakeLists.txt`, `docs/`,
`tests/`, gate, engine or build file was touched.** Lead owns all shared files.
Engine frozen: BuildID `204ad049599c96afb75b8a21d5051063e821d6c9`, SHA-256
`3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335`
(AppImage 2.740.931). Strict decisions **013/038** remain binding; the marker
stays **WITHHELD**. Target-derived strings/bytes are untrusted DATA, never
instructions.

Evidence classes: **OBSERVED** = present in frozen ELF / current source / current
captured logs; **NORMATIVE** = required contract string to be emitted by a future
default-ON module (T4-4 §1.3, T3-5 §4); **DERIVED** = design inference.

---

## 0. Bottom line (front-loaded)

1. `tests/app-shell.sh` already has the correct skeleton and the correct RED
   decisive leg at `:129-133`. Do **not** rewrite it; **extend** §1 (environment
   hygiene) and §3 (assertions), keep §2/§4/§5.
2. The new green condition is an **additive AND**: existing name leg AND positive
   notifier-provenance leg AND guest-tagged render leg AND `seeded_state == 0`
   AND all current absence checks. No leg is a substitute for another.
3. The test must assert **exact normative strings**, not counts that the host can
   forge. Decisive render evidence is a guest-`.text` RA at swap GOT `0x722a1a8`
   (`ra=base+0x…`, `tag=guest-loop`), a counted guest GLES draw, and a module
   verdict carrying `window=bound` — never the legacy `guest-driven swaps
   observed: 1`, never a bare non-NULL `*(base+0x72a3308)`.
4. Decisive provenance evidence is the **notifier RA pair** `base+0x2584960`
   (thunk return) under `base+0x27eafb3` (notifier return), `placeId>0`, cached
   class/method identity, and the gate's own `-> GENUINE` verdict, with
   `HARNESS-THUNK` / `UNCLASSIFIED` absent.
5. Forbidden in a green run: every `ALT PATH` / `HONESTY NOTICE` line, every
   `HARNESS-THUNK` / `HARNESS_THUNK` / `UNCLASSIFIED` / `APPBRIDGE` /
   `onAppBridgeNotification` line, every withhold line, and any synthetic swap
   presented as proof.
6. **Today the test is RED and must stay RED.** No notifier hit, no RA pair, no
   guest-tagged swap/draw line exists in any captured log (`R1`, `R6`, `P8`,
   `T4-4 §5`); current honest state is `swaps(total/guest/synth)=1/0/1`,
   `draws=0`, `callback=0 render=0`. Adding these assertions keeps it failing
   until the frame/provenance modules land. Re-enabling ALT or setting a seed to
   make it green is a false green and forbidden.

---

## 1. Authority and invariants

| Source | Role |
| --- | --- |
| `docs/decisions/013`, `038` | Strict marker; ALT thunk diagnostic-only; `seeded_state==0`; `onAppBridgeNotification` rejected. |
| `analysis/T4_4_gate-tests.md` §1.3/§3 | Normative log strings and the existing test extension contract (authoritative for wording). |
| `analysis/T3_5_render-evidence.md` §3–§4 | Frame-module predicate: ≥2 tagged swaps with stable RA, ≥1 bracketed guest draw, guest-bound window; normative render strings. |
| `analysis/R6_swap_provenance.md` §4–§5 | Unforgeable RA discriminator; the two host swaps (S1 native, S2 Route B) can never make `guest>0`. |
| `analysis/R1_zero_tagged_swaps.md`, `analysis/P8_anw_guest_consumption.md`, `analysis/P1_render_vtable.md` | Why `1/0/1` and `draws=0` are correct today; vtable slot `[3]@0x7191300 = 0x649d422`; no guest frame loop (`[nativeData+0x98]==NULL`). |
| `analysis/MODULE_INTEGRATION.md` §2 | `render_ok = phase3_done && guest_tagged_render_ok()`; fromSurface hook is supporting, not required. |
| `src/jni/provenance_gate.h` / `.c` | DEFAULT-ON positive credential; RVAs `0x2584960`, `0x27eafb3`, cached IDs `0x72a2f48`/`0x72a2fc0`; kill switch `SOBER_PROVENANCE_GATE=0` is fail-closed. |

Design invariants (from T4-4 §0, unchanged):

- **Additive AND, never substitution.** The name-only check, the
  `seeded_state ⇒ shell_cb = 0` override, the `!shell_cb || !render_ok` early
  return, and the **sole** `emit_marker("app shell rendered\n", "SOBER_SHELL_MARKER")`
  site all stay.
- **No catch-22.** Gate-facing proof is default-ON, observation-only native code
  active with *every* `SOBER_*` opt-in unset. `SOBER_CALLBACK_PROVENANCE`,
  `SOBER_NOTIFIER_PROBE`, `SOBER_GSEED_DIAG`, `SOBER_APP_SHELL_DIAGNOSTIC`,
  `SOBER_CALLBACK_PROVENANCE_GUEST_BASE` are diagnostic opt-ins / withhold
  entries and must never be set by the test to obtain proof.
- **Withhold wins / fail closed.** `seeded_state != 0`, any recorded harness
  thunk, or mixed guest+synthetic evidence ⇒ withhold, even if a GENUINE
  classification was also seen.
- **One run, one conjunction.** All required lines in §3 must be emitted in the
  *same* clean run on the frozen engine. Render alone, a name count alone, or a
  single diagnostic line never qualifies.
- **Test/gate symmetry.** Every line asserted in §3 must be emitted by the named
  production code path on the clean path — no test-only string synthesis. If a
  sibling emitter chooses different wording, align the emitter (preferred) or
  adapt the pattern **without dropping any asserted field**.

---

## 2. Patch map for `tests/app-shell.sh`

| Location | Change |
| --- | --- |
| `:28-44` STALE rebuild list | Add the now-landed modules `src/jni/frame_evidence.c/.h`, `src/jni/frame_synthetic_tag.c/.h`, `src/jni/frame_thread.c/.h` and `src/jni/provenance_gate.c/.h` (plus any further frame/gate module as it lands), so the harness cannot run stale binaries. |
| `:68` before launch | Replace the single `unset SOBER_ALT_CALLBACK_PATH` with full environment hygiene (§3.0). |
| `:105-133` §3 gate evidence | Keep the existing absence checks and the decisive leg at `:129-133`; append §3.1–§3.3 below them. |
| `:135-140` §4 marker | Strengthen to an exact **count** of one marker in `$OUT` (§3.4). |
| `:142-148` §5 window | Keep; the render verdict's `window=bound` token (§3.2) is the gate-level binding assertion. |

Keep `set -uo pipefail` and explicit `exit 1` on every failure (do **not** add
`-e` — it would mask the diagnostic `tail` output). Keep engine identity (§0) and
the pre/post SHA equality check (`:102-103`) unchanged.

---

## 3. Required log lines — exact assertions (all must pass)

### 3.0 Environment hygiene (before launch)

Preferred: launch under a minimal allowlist so no inherited `SOBER_*` can seed
the run:

```bash
unset SOBER_ALT_CALLBACK_PATH
# canonical withhold list (gate OR-chain, guest_app_shell.c:1608-1760) must be
# absent; env -i allowlist is the robust form:
# PATH, DISPLAY, EGL_PLATFORM=x11, LIBGL_ALWAYS_SOFTWARE=1, LD_LIBRARY_PATH,
# XDG_RUNTIME_DIR (optional), SOBER_WINDOW_HOLD_MS / SOBER_APP_SHELL_WAIT_MS
# (timing-only, allowed for log collection).
```

Never set `SOBER_CALLBACK_PROVENANCE`, `SOBER_NOTIFIER_PROBE`,
`SOBER_GAME_LOADED_PATH`, `SOBER_RENDER_GUEST`, `SOBER_PROVENANCE_GATE`, or any
seed/GOT/ctor/lifecycle/evidence knob to "obtain" proof. With the frame modules
now landed, also never set their knobs: `SOBER_FRAME_EVIDENCE`,
`SOBER_FRAME_SYNTH_TAG`, `SOBER_FRAME_THREAD` (module kill/self-test switches),
and the withheld evidence-driver family from `F9` §0:
`SOBER_RENDER_FRAME_EVIDENCE`, `SOBER_EVIDENCE_SURFACE`,
`SOBER_EVIDENCE_DEVICE`, `SOBER_EVIDENCE_REDRAW`,
`SOBER_RENDER_SUPPRESS_SYNTHETIC`, `SOBER_FRAME_REDRAW_DRIVE`. All must join the
gate's `seeded_state` OR-chain (§7). The runtime assertion that hygiene held is
the positive `seeded_state=0` line in §3.3.

### 3.1 Positive callback-provenance leg

Normative strings (T4-4 §1.3) — all required:

```
[callback-provenance] notifier hit 0x27eaf6e placeId=<N> cached_class=base+0x72a2f48 cached_method=base+0x72a2fc0 slot=0x6dbd860
[callback-provenance] gameLoadedCallback: class=base+0x72a2f48 (match) method=base+0x72a2fc0 (match) thunk_ret=base+0x2584960(seen,+) notifier_ret=base+0x27eafb3(seen,+) -> GENUINE
[callback-provenance] dispatch arg placeId=<N> matches notifier placeId=<N> name=gameLoadedCallback sig=(J)V count_delta=<D>
[app-shell] gate provenance: notifier-driven gameLoadedCallback at 0x27eaf6e placeId=<N> (RSI>0), class/method cached non-NULL, return 0x27eafb3 over thunk 0x2584960 -> GENUINE
```

Exact assertions:

```bash
# notifier hit, placeId>0, cached class/method, registered slot
grep -Eq '^\[callback-provenance\] notifier hit 0x27eaf6e placeId=[1-9][0-9]* .*cached_class=base\+0x72a2f48 .*cached_method=base\+0x72a2fc0 .*slot=0x6dbd860' "$ERR" \
  || { echo "FAIL: no notifier hit with placeId>0 (DECISION 038)"; tail -40 "$ERR"; exit 1; }
if grep -Eq 'notifier hit 0x27eaf6e placeId=0([^0-9]|$)' "$ERR"; then
  echo "FAIL: notifier placeId=0 is not proof"; tail -40 "$ERR"; exit 1; fi

# notifier RA pair on one line, with cached-ID match and GENUINE verdict
grep -Eq '^\[callback-provenance\] gameLoadedCallback: .*class=base\+0x72a2f48 \(match\) .*method=base\+0x72a2fc0 \(match\) .*thunk_ret=base\+0x2584960\(seen[^)]*\) .*notifier_ret=base\+0x27eafb3\(seen[^)]*\) -> GENUINE' "$ERR" \
  || { echo "FAIL: no 0x2584960/0x27eafb3 RA pair (DECISION 038)"; tail -40 "$ERR"; exit 1; }

# dispatch argument matches notifier placeId, qualifying name/sig, count delta>=1
grep -Eq '^\[callback-provenance\] dispatch arg placeId=[1-9][0-9]* matches notifier placeId=[1-9][0-9]* name=gameLoadedCallback sig=\(J\)V count_delta=[1-9][0-9]*' "$ERR" \
  || { echo "FAIL: dispatch arg/placeId mismatch"; tail -40 "$ERR"; exit 1; }

# gate's own decisive verdict — this also satisfies the existing :129-133 leg
grep -Eq '^\[app-shell\] gate provenance: notifier-driven gameLoadedCallback at 0x27eaf6e placeId=[1-9][0-9]* \(RSI>0\).*return 0x27eafb3 over thunk 0x2584960 -> GENUINE' "$ERR" \
  || { echo "FAIL: no native notifier-driven callback provenance (DECISION 038)"; tail -40 "$ERR"; exit 1; }
```

Rules: `onAppShellReloadNeeded()V` has no native reader in this build and **must
not** satisfy this leg; the provenance lines must name `gameLoadedCallback`. The
name-only leg (`guest->Java callback: …`, §3.4) stays necessary but is never
sufficient. The pair order matters: `0x2584960` is nearer the dispatch than
`0x27eafb3`; `base+0x2eb4bb1` (`onAppBridgeNotification`) is disqualified.

### 3.2 Guest-tagged render leg

Normative strings (T4-4 §1.3 / T3-5 §4) — all required:

```
[render] guest-tagged swap at 0x649d422 ra=base+<RA_IN_GUEST_TEXT> tag=guest-loop | guest-tagged swaps: <N> | guest GLES draws between tagged swaps: <M>
[render] guest render verdict: guest-tagged frame confirmed (swaps=<N> draws=<M> fromSurface=installed window=bound) -> GUEST-TAGGED
[app-shell] render provenance: guest-tagged frame at 0x649d422 (swaps=<N>, draws=<M>), phase3_done=1 -> OK
```

`N>=2` and a **stable** RA are required: T4-4 §3.2's earlier `N>=1` is hardened
by T3-5 §4 (`g_swaps_guest_loop >= 2`, `g_loop_ra_consistent`) and R6 §4.2
(`guest>=2` with recurring RA). A single tagged swap fails closed.

Exact assertions:

```bash
# guest-tagged swap line (RA in guest, tag=guest-loop, N>=2, M>=1)
SWAP_LINE=$(grep -E '^\[render\] guest-tagged swap at 0x649d422 ra=base\+0x[0-9a-f]+ tag=guest-loop \| guest-tagged swaps: ' "$ERR" | head -1)
[ -n "$SWAP_LINE" ] || { echo "FAIL: no guest-tagged swap line (BLK-06)"; tail -40 "$ERR"; exit 1; }
printf '%s\n' "$SWAP_LINE" | grep -Eq 'guest-tagged swaps: [2-9][0-9]* \| guest GLES draws between tagged swaps: [1-9][0-9]*$' \
  || { echo "FAIL: need >=2 guest swaps and >=1 bracketed guest draw"; tail -40 "$ERR"; exit 1; }

# RA must lie inside the guest executable classifier window [base, base+0x6d33b90)
RVA=$(printf '%s\n' "$SWAP_LINE" | sed -n 's/.*ra=base+0x\([0-9a-f][0-9a-f]*\).*/\1/p')
[ -n "$RVA" ] || { echo "FAIL: missing guest swap RA field"; tail -40 "$ERR"; exit 1; }
rva_dec=$((16#$RVA))
[ "$rva_dec" -ge 1 ] && [ "$rva_dec" -le $((0x6d33b90)) ] \
  || { echo "FAIL: guest swap RA 0x$RVA outside guest .text span"; tail -40 "$ERR"; exit 1; }

# loop stability: the same guest RA recurs on >=2 tagged swaps
first_ra=$(printf '%s\n' "$SWAP_LINE" | sed -n 's/.*ra=base+\(0x[0-9a-f][0-9a-f]*\) tag=guest-loop.*/\1/p')
ra_reps=$(grep -Fc "ra=base+$first_ra tag=guest-loop" "$ERR")
[ "$ra_reps" -ge 2 ] || { echo "FAIL: guest swap RA not stable across >=2 swaps"; tail -40 "$ERR"; exit 1; }

# parse counts from the authoritative line
swaps_n=$(printf '%s\n' "$SWAP_LINE" | sed -n 's/.*guest-tagged swaps: \([0-9][0-9]*\).*/\1/p')
draws_m=$(printf '%s\n' "$SWAP_LINE" | sed -n 's/.*guest GLES draws between tagged swaps: \([0-9][0-9]*\).*/\1/p')
[ "${swaps_n:-0}" -ge 2 ] && [ "${draws_m:-0}" -ge 1 ] || { echo "FAIL: bad counts"; tail -40 "$ERR"; exit 1; }

# module verdict: window binding token is decisive; fromSurface may be the
# real NDK path (T3-5 §4 open question), so accept installed|native.
grep -Eq '^\[render\] guest render verdict: guest-tagged frame confirmed \(swaps=[1-9][0-9]*, draws=[1-9][0-9]*, fromSurface=(installed|native), window=bound\) -> GUEST-TAGGED' "$ERR" \
  || { echo "FAIL: no guest render verdict with window=bound"; tail -40 "$ERR"; exit 1; }

# gate accepts the render leg
grep -Eq '^\[app-shell\] render provenance: guest-tagged frame at 0x649d422 \(swaps=[1-9][0-9]*, draws=[1-9][0-9]*\), phase3_done=1 -> OK' "$ERR" \
  || { echo "FAIL: gate did not accept guest render leg"; tail -40 "$ERR"; exit 1; }

# field consistency: verdict and gate must quote the same counts as the swap line
grep -qF "(swaps=$swaps_n draws=$draws_m fromSurface=" "$ERR" || { echo "FAIL: verdict count mismatch"; exit 1; }
grep -qF "(swaps=$swaps_n, draws=$draws_m), phase3_done=1 -> OK" "$ERR" || { echo "FAIL: gate count mismatch"; exit 1; }
```

Window-binding rule: the test asserts the module's **`window=bound`** token. A
bare non-NULL `*(base+0x72a3308) != NULL` (today satisfied only by the host store
at `render_guest.c:326-329`/`:417-423`) is **not** an asserted line and can never
make this leg pass; the guest-bound RA at `0x26fea78`/`0x2a2087c`/`0x2a20907`
plus a generation delta at `0x72a3310` is the module's responsibility (T3-5
§3.3). Do not add a test shortcut that checks the global directly.

Offline (post-run, not a shell assertion): disassemble the 5 bytes at
`ra-5..ra` and confirm an indirect `call *0x18(%reg)` through vtable
`0x7191300` (`ff 50 18` / `ff 90 18 00 00 00`), not a direct `e8 rel32` (the
host Route-B call at `guest_render.c:354`) and not another indirect call (R6
§5.2).

**Emitter alignment checklist — landed modules (OBSERVED 2026-11-21).** The
`frame_evidence.c` module now in the tree does **not** yet emit the §3.2
normative strings, so these assertions keep the test RED as-is:

1. `frame_evidence_verdict_string()` (`frame_evidence.c:462-466`) returns only
   `GUEST-TAGGED (swaps=%llu draws=%llu window=bound)`. The gate/lead must emit
   the full `[render] guest render verdict: guest-tagged frame confirmed
   (swaps=… draws=… fromSurface=installed|native window=bound) -> GUEST-TAGGED`
   line and the `[app-shell] render provenance: … -> OK` line. Do **not** weaken
   the test pattern to accept the short string — a bare verdict is not the
   per-swap RA evidence.
2. `frame_evidence_verdict()` (`:437`) accepts `swaps_guest >= 1` and keeps no
   stable-RA state (`grep` finds no loop-RA/consistency counter). T3-5 §4 and R6
   §4.2 require `>=2` with a recurring RA; until the emitter satisfies that, a
   `GUEST-TAGGED` verdict still fails this test (correct fail-closed).
3. The per-swap `[render] guest-tagged swap at 0x649d422 ra=base+0x… tag=guest-loop
   | guest-tagged swaps: N | guest GLES draws between tagged swaps: M` line is
   not emitted by `frame_evidence.c`; it must be added by the frame emitter (or
   the gate) with every field intact. Likewise the positive callback-provenance
   lines in §3.1 are not emitted by `provenance_gate.c` (it only builds a
   `describe()` buffer) — the gate must emit them before the early return.
4. `SOBER_FRAME_EVIDENCE`, `SOBER_FRAME_SYNTH_TAG` and `SOBER_FRAME_THREAD` are
   new withhold entries not yet in the gate chain; §3.0 keeps them unset and §7
   requires them added in the same change.

### 3.3 Clean state, name leg, phase-2/3 ancestry, marker

```bash
# positive clean-state assertion — the runtime proof that §3.0 hygiene held
grep -qxF '[app-shell] clean state: seeded_state=0 (all opt-ins unset)' "$ERR" \
  || { echo "FAIL: seeded_state not proven 0"; tail -40 "$ERR"; exit 1; }

# preserved name leg (necessary, not sufficient) and legacy ancestry
grep -Eq 'guest->Java callback: (gameLoadedCallback|onAppShellReloadNeeded)' "$ERR" \
  || { echo "FAIL: no qualifying guest->Java app-shell callback"; tail -40 "$ERR"; exit 1; }
grep -q 'guest call 0x649d422' "$ERR" \
  || { echo "FAIL: phase-3 guest render call missing"; tail -40 "$ERR"; exit 1; }
{ grep -qx 'guest init complete' "$OUT" && [ -f "$INIT_MARKER" ]; } \
  || { echo "FAIL: phase-2 init completion regressed"; tail -40 "$OUT"; exit 1; }
{ grep -qx 'first frame rendered' "$OUT" && [ -f "$FRAME_MARKER" ]; } \
  || { echo "FAIL: phase-3 first-frame regressed"; tail -40 "$OUT"; exit 1; }
# existing GOT-hook install line remains ancestry
grep -q 'installed guest-swap counter hook at GOT 0x722a1a8' "$ERR" \
  || { echo "FAIL: no GOT 0x722a1a8 hook install line"; tail -40 "$ERR"; exit 1; }
```

### 3.4 Exactly one marker, clean exit, visible window

```bash
marker_count=$(grep -cx 'app shell rendered' "$OUT")
[ "$marker_count" -eq 1 ] || { echo "FAIL: marker count=$marker_count (want 1)"; exit 1; }
[ -f "$SHELL_MARKER" ] || { echo "FAIL: no shell marker file"; exit 1; }
[ "$rc" -eq 0 ] || { echo "FAIL: harness exit $rc"; exit 1; }
# §5 unchanged: xdotool window seen, else native window: handle= present
```

Count the marker in `$OUT` only. The script's own trailing banner
(`echo "app shell rendered"` at `:150`) is its success message, **not** a second
emission.

---

## 4. Forbidden lines — any hit ⇒ immediate FAIL

### 4.1 ALT / HONESTY (decision 038 §1/§3)

```bash
deny() { if grep -qF "$1" "$ERR"; then echo "FAIL: forbidden line: $1"; tail -40 "$ERR"; exit 1; fi; }
for pat in \
  'ALT PATH' \
  'ALT PATH guest call 0x25848e0' \
  'ALT PATH: gameLoadedCallback method=' \
  'ALT PATH: skipping 0x247dc8d' \
  'HONESTY NOTICE' \
  'ALT-CALLBACK EXERCISED (DIAGNOSTIC)'; do deny "$pat"; done
```

These remain forbidden even though the current gate makes them unreachable
(038 §5). Re-enabling `SOBER_ALT_CALLBACK_PATH` to turn the test green is the
explicitly forbidden false green.

### 4.2 Harness thunk / unclassified / appbridge

```bash
for pat in \
  'HARNESS-THUNK' \
  'HARNESS_THUNK' \
  'UNCLASSIFIED' \
  'unclassified' \
  'APPBRIDGE' \
  'onAppBridgeNotification' \
  'MIXED'; do deny "$pat"; done
```

Mixed GENUINE + HARNESS-THUNK, or guest-tag + counted synthetic, must withhold;
the gate must never emit `-> GUEST-TAGGED` in such a run. `onAppBridgeNotification`
is JNI plumbing and is rejected (013 note; 038 §2.3); its only permitted
occurrence is the withheld opt-in diagnostic under
`SOBER_LOG_APP_BRIDGE_NOTIFICATION`, which must be absent here.

### 4.3 Synthetic swap as evidence (R3 / R6 §5)

```bash
# tag=synthetic is allowed ONLY as a non-counting diagnostic line
if grep -F 'tag=synthetic' "$ERR" | grep -qv 'excluded from guest-tagged count'; then
  echo "FAIL: synthetic swap presented as evidence"; tail -40 "$ERR"; exit 1; fi

# the legacy host-RA triple must never coexist with a GUEST-TAGGED claim
if grep -Eq 'swaps\(total/guest/synth\)=[0-9]+/0/[0-9]+' "$ERR" \
   && grep -q 'GUEST-TAGGED' "$ERR"; then
  echo "FAIL: zero guest-tagged swaps with GUEST-TAGGED verdict"; tail -40 "$ERR"; exit 1; fi

# our native first-frame swap and Route B are ancestry only; neither may be
# cited as the render proof line (the §3.2 lines are the only proof accepted).
```

The legacy `guest-driven swaps observed: 1` line may remain present as
phase-3 ancestry, but it is never an asserted proof line: it is native counter
`0` + module `total` (R1 §0), not a guest-tagged count. Likewise the native
first-frame swap (`sober_render_swap()`'s own `dlsym`'d `eglSwapBuffers`) never
traverses guest GOT `0x722a1a8` and cannot enter `g_swaps_guest_loop`.

### 4.4 Withhold / absence lines

```bash
for pat in \
  'marker withheld: diagnostic guest-state seed active' \
  'marker withheld: callback=' \
  'marker withheld: guest-tagged render absent' \
  'marker withheld: callback provenance absent' \
  'DIAGNOSTIC ONLY (no marker)' \
  '[ev-suppress]' \
  '-> MIXED -> WITHHELD' \
  '-> ABSENT('; do deny "$pat"; done
```

`[ev-suppress]` is permitted only in an explicit evidence-mode diagnostic
(T3-5 §3.5) and is a withhold entry; a green proof run has none. All these lines
must remain **available** in withheld runs — they are forbidden only on the
green path.

---

## 5. Green exit criterion (normative comment to place above §3)

> **GREEN** iff, on the frozen engine (BuildID `204ad049…6c9`, SHA
> `3e498d6494…41335`, unchanged pre/post, §0/§2 checks green) with every
> `SOBER_*` opt-in unset: all §3.1–§3.4 lines are emitted by the named
> default-ON production paths in **one** run — notifier hit `0x27eaf6e` with
> `placeId>0`; RA pair `0x2584960` under `base+0x27eafb3` with cached IDs and
> `-> GENUINE`; the gate's own `gate provenance … -> GENUINE`; a guest-`.text`
> RA `tag=guest-loop` swap at `0x649d422` with `N>=2` stable RA, `M>=1`
> bracketed guest GLES draws, `window=bound`, `-> GUEST-TAGGED` and the gate's
> `render provenance … -> OK`; and the positive `clean state: seeded_state=0`
> line — and **all §4 lines are absent**, `rc==0`, `SOBER_SHELL_MARKER` exists,
> stdout contains exactly one `app shell rendered` (count in `$OUT` only), and
> the native window is visible. Absence of any one required line, or presence
> of any one forbidden line, is a correct **RED**.

The marker remains WITHHELD until every clause above holds. Provenance alone,
render alone, a name count alone, a diagnostic line alone, or any knob-enabled
run never qualifies.

---

## 6. RED today — explicit, evidence-anchored

Current captured logs (`R1 §0`, `R6 §4.2`, `P8 §0`, `T4-4 §5`) show:

```
[render] render_guest counters: source=module from_surface=2 proc_loads=48
         draws=0 swaps(total/guest/synth)=1/0/1 lifecycle=1/1/1/1
[app-shell] default guest render path -> rc=0 (already installed=1, guest-tagged swaps=0)
[app-shell] marker withheld: diagnostic guest-state seed active (no app-shell frame proven)
[app-shell] marker withheld: callback=0 render=0 (env calls=33 callbacks=0 last='isVrDevice')
```

- No `[callback-provenance] notifier hit 0x27eaf6e …` line (§3.1 fails).
- No `0x2584960`/`0x27eafb3` RA pair, no `-> GENUINE` (§3.1 fails).
- No `ra=base+… tag=guest-loop` line; `guest-tagged swaps: 0`; `draws=0`;
  no `window=bound` verdict (§3.2 fails).
- No `[app-shell] clean state: seeded_state=0 …` line (§3.3 fails).
- The one counted swap is the host Route-B call at `guest_render.c:354`; its RA
  is in the host PIE `0x5637a6…`, correctly tagged synthetic (`R1 §3`, `R6 §3`).
  The window global is host-written (`render_guest.c:326-329`), a false-green
  seed (`P8 §5`). No guest frame loop runs because `[nativeData+0x98]` is NULL
  (`P8 §5`, `R1 §4`).

Therefore **the test stays RED** after these assertions are added. That is the
correct, honest outcome; do not loosen any pattern, set any opt-in, or re-enable
the ALT thunk to make it green. Once the frame modules land and a clean run
emits all §3 strings, the same assertions flip to green without dilution.

---

## 7. Withhold-chain parity for the test

1. The gate's `seeded_state` OR-chain in `guest_app_shell.c` is authoritative;
   the test's unset/allowlist (§3.0) must be cross-checked against it. The
   positive `clean state: seeded_state=0` line is the runtime proof they agree.
2. New frame/provenance module knobs (e.g. `SOBER_PROVENANCE_GATE`, any
   evidence/suppression/surface knob) must join that chain in the same change
   that adds them, and the test's hygiene list must grow with it. The
   provenance gate itself is the fail-closed credential, deliberately not a seed.
3. Retired/diagnostic modules (`callback_provenance`, `notifier_probe`, ALT
   thunk) stay detectable via §4 even if they are not installed by default.
4. Diagnostic markers must keep their distinct token/file/function and must
   never call `emit_marker`; §4 asserts their absence on the green path.

---

## 8. Evidence index and limits

**Normative strings:** `analysis/T4_4_gate-tests.md` §1.3/§3;
`analysis/T3_5_render-evidence.md` §3–§4; `analysis/R6_swap_provenance.md` §4.2.
**Frozen-engine facts (OBSERVED):** `analysis/R1_zero_tagged_swaps.md`,
`analysis/P1_render_vtable.md`, `analysis/P8_anw_guest_consumption.md`,
`docs/decisions/013`, `038`. **Harness (read only):** `tests/app-shell.sh:1-151`,
`src/linux/guest_app_shell.c:1608-1800` (gate chain and sole emit site),
`src/jni/provenance_gate.h`, `src/jni/render_guest.c`, `src/linux/guest_render.c`.

**Limits:** no build, harness run, debugger capture or REA query was performed
for this document. §3 strings are contract requirements, not observed runtime
output; the emitter must align to them (or the consumer patterns adapted without
dropping a field). The landed `frame_*` sources and `F9_frame_integration_order.md`
were read only to identify the concrete emitter-alignment gaps in §3.2; `F9`'s
claim that no `frame_*` sources exist is already stale in this working tree. No
file other than this report was created or modified by G2. The marker is
**WITHHELD** and no success is claimed.
