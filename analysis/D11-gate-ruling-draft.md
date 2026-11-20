# D11 — Draft final lead ruling: ALT thunk, `tests/app-shell.sh`, and the strict 013 gate

Worker: **D11** (logical Team D, wave 2). Scope: **this file only**
(`analysis/D11-gate-ruling-draft.md`). This is a **draft for the lead**, not an
enactment. No `docs/`, `PROGRESS.md`, `tests/`, `src/`, gate, CMake or engine file
was edited by D11. The lead owns `docs/` + `PROGRESS.md`; the test edits below are
lead-approved and executed by the test owner.

Governing constraint (unchanged): strict **decision 013**
(`docs/decisions/013-no-weak-app-shell-marker.md:6-13,53-64`). The user ruled
**gate strict; `onAppBridgeNotification` rejected as app-shell proof**. Nothing in
this draft relaxes, bypasses or reclassifies the `app shell rendered` gate. No
success is claimed.

Snapshot note: the gate is being edited concurrently, so anchor on symbols
(`shell_cb`, `seeded_state`, `render_ok`, `emit_marker` call site), not the line
numbers below. At this snapshot `guest_app_shell.c` has `shell_cb` ≈ `:1394-1395`,
the `seeded_state` OR-chain ≈ `:1400-1480`, `render_ok` ≈ `:1488`, the
`!shell_cb || !render_ok` return ≈ `:1490-1506`, the (now unreachable) ALT
`HONESTY NOTICE` ≈ `:1508-1513`, and the sole
`emit_marker("app shell rendered\n", "SOBER_SHELL_MARKER")` ≈ `:1514`.
`SOBER_ALT_CALLBACK_PATH` is the **first** `seeded_state` term (≈ `:1400`), so the
ALT thunk already force-withholds the real marker; `tests/app-shell.sh:68` unsets
it and `:112-116` still demands its log line.

---

## 0. What this ruling resolves

| Conflict | Source | Resolution |
| --- | --- | --- |
| 037 treats a green `app-shell.sh` under the ALT thunk as proof of a "genuine guest→Java callback"; 013 calls the same thunk non-proof and defers tightening to a lead decision | `037:24-33`, `013:6-13,46-51`; D03-D1, D06-F5, D10-R2 | Ruling (a): thunk is diagnostic-only, never proof; 037 amended; 013's open item closed |
| `app-shell.sh` unsets `SOBER_ALT_CALLBACK_PATH` (`:68`) but requires `ALT PATH guest call 0x25848e0` (`:112-116`) — mutually exclusive | `013:46-51`, memo §1, D03-D2 | Ruling (c): remove stale assertion, assert absence of ALT/HONESTY lines, stay red until genuine proof; reject memo Option A |
| Need to retain phase-5 thunk coverage without weakening the gate | memo Option C, D3 step 3 | Ruling (d): separate default-OFF diagnostic marker that never calls `emit_marker("app shell rendered")` |

The lead's prior ruling is explicitly preserved: `onAppBridgeNotification` is
JNI plumbing and may only be counted/logged by
`SOBER_LOG_APP_BRIDGE_NOTIFICATION`; it can never emit the marker.

---

## 1. THE RULING (paste-ready) — proposed new file `docs/decisions/038-strict-gate-alt-thunk-ruling.md`

Everything between the markers below is the exact, complete content for the new
decision file. It is written so `013` and `037` need only the short appendices in
§2.

````text
# Decision 038 — Strict gate preserved: the ALT thunk is diagnostic-only, never app-shell proof

Status: accepted (lead). Date: 2026-11-20. Owner: lead (`docs/`, `PROGRESS.md`, `src/`, gate).
Authority: reaffirms decision 013 (`docs/decisions/013-no-weak-app-shell-marker.md`); amends decision 037 (`docs/decisions/037-callback-thunk-milestone.md`); closes the open inconsistency at 013:46-51 and the `tests/app-shell.sh` contradiction recorded in `analysis/decision-memo-alt-thunk-vs-013.md`, `analysis/D03-gate-decision-audit.md` (D3-2), `analysis/D06-decision-crosscheck.md` (F5) and `analysis/D10-assurance-synthesis.md` (R2). The user's ruling is unchanged: **gate strict; `onAppBridgeNotification` rejected as app-shell proof.**

## 1. Scope and effect

This ruling resolves two contradictions without changing the strict done condition:

- Decision 037's honesty section treats a green `tests/app-shell.sh` under `SOBER_ALT_CALLBACK_PATH` as evidence of a "genuine guest→Java callback", while decision 013 calls that same harness-triggered thunk non-proof and defers tightening to an explicit lead decision.
- `tests/app-shell.sh:68` unsets `SOBER_ALT_CALLBACK_PATH` while `:112-116` requires the ALT-thunk log line — the two assertions are mutually exclusive.

Nothing here relaxes, bypasses or reclassifies the `app shell rendered` gate. No success is claimed.

## 2. Ruling (a) — the ALT thunk is diagnostic-only and never proof

`SOBER_ALT_CALLBACK_PATH` and the guest thunk it calls at RVA `0x25848e0` (`gameLoadedCallback(J)V` via `CallStaticVoidMethodV`, JNIEnv offset `0x470` / global slot 142) are a **diagnostic-only** exercise of a real guest JNI thunk. They are **harness-triggered**: host code calls the thunk directly and skips `nativeAppBridgeAppStart 0x247dc8d`. They prove at most that our bridge can receive a guest thunk call and that the phase-3 render RVA ran. They **never** prove a login/app-shell frame, and they must never satisfy or contribute to the `app shell rendered` gate.

Consequently:

- the knob stays default-OFF and stays in the `seeded_state` withhold chain (any ALT run force-withholds the real marker);
- the `HONESTY NOTICE` is documentation of that fact, not a pass banner, and its current unreachability behind the withhold is the correct, strengthened behaviour;
- **Option A** of `analysis/decision-memo-alt-thunk-vs-013.md` (stop unsetting the knob and make the thunk the proof) is **rejected**.

## 3. Ruling (b) — what the real marker requires

`emit_marker("app shell rendered\n", "SOBER_SHELL_MARKER")` may execute only when **all** of the following hold in the same run, on the frozen engine (BuildID `204ad049…`, SHA-256 `3e498d6494…`, never the Flatpak `791c3f9e…` build):

1. **Callback leg (C) — notifier-driven.** The guest's own state machine delivers a qualifying app-shell callback. The only practically reachable one is `gameLoadedCallback(J)V`: the guest itself enters notifier `0x27eaf6e` with `RSI = placeId != 0`, cached class `0x72a2f48` and method `0x72a2fc0` non-NULL; that notifier calls thunk `0x25848e0` and returns to `base+0x27eafb3`; the bridge then logs `guest->Java callback: gameLoadedCallback` with a count delta after the introducer calls. `onAppShellReloadNeeded()V` qualifies only if independently shown to be genuinely notifier-driven (it currently has no native reader). A per-name counter is **necessary but not sufficient**: notifier-hit and thunk-return-address provenance must be captured **by the gate itself**, with all opt-ins unset. The opt-in `SOBER_CALLBACK_PROVENANCE` recorder (`src/jni/callback_provenance.c`) is itself a diagnostic toggle and must be added to the withhold chain; its `[callback-provenance] … -> GENUINE / HARNESS-THUNK` lines may validate the discriminator in the separate diagnostic run but can never make a run qualifying. `onAppBridgeNotification(String,String)` (`0x72a3028` → thunk `0x3dea91`), accessor getters (`surface`/`platformParams`/`vrContext`), synthetic seeds and every harness-triggered thunk are **disqualified**; they may only be counted or logged by their own opt-in diagnostics.
2. **Render leg (R).** Phase-3 guest render `0x649d422` executed (`g_guest_swaps >= 1`) and `sober_render_phase3_done() == 1`; `guest init complete` and `first frame rendered` present. Render alone is never sufficient.
3. **Clean-state leg (S).** `seeded_state == 0`: every guest-mutating / opt-in `SOBER_*` knob unset, i.e. every entry of the live `seeded_state` OR-chain in `guest_app_shell.c` (`SOBER_ALT_CALLBACK_PATH`, `SOBER_START_APP`, all `SOBER_SEED_*`, `SOBER_FAITHFUL_*`, the JSON/object/flag bridges, patch/repair/ctor/GOT knobs, `SOBER_LOG_APP_BRIDGE_NOTIFICATION`, `SOBER_GAME_LOADED_PATH`, `SOBER_LUA_APP_START`, etc.). The **source chain is authoritative**, not any hardcoded count in a document. Timing/marker-path knobs (`SOBER_APP_SHELL_WAIT_MS`, `SOBER_WINDOW_HOLD_MS`, `SOBER_RENDER`, `SOBER_APP_SHELL`, `SOBER_SHELL_MARKER`) are not disqualifying. No `marker withheld: diagnostic guest-state seed active`, no `ALT PATH` / `ALT PATH guest call 0x25848e0` line and no `HONESTY NOTICE` line may appear in a qualifying run. Any **future** guest-mutating knob must be added to the chain before its A/B result is trusted.

Until provenance instrumentation exists to establish leg (C), the name-only coded gate cannot prove it: the marker stays **withheld** and no run qualifies. Adding notifier/return-address capture is a separate, additive, lead-approved change; this ruling does not make it.

## 4. Ruling (c) — `tests/app-shell.sh` contract

Adopt options **B + C** of `analysis/decision-memo-alt-thunk-vs-013.md`; reject A.

- Delete the stale `tests/app-shell.sh:112-116` assertion that requires `ALT PATH guest call 0x25848e0`.
- Assert the **absence** of `ALT PATH guest call 0x25848e0`, `ALT PATH:` and `HONESTY NOTICE` lines and of the synthetic-state withhold line; assert a **qualified-name** callback (`gameLoadedCallback` / `onAppShellReloadNeeded`), the phase-3 render RVA `0x649d422`, and notifier/return-address provenance as the decisive condition.
- Keep the strict marker check. `tests/app-shell.sh` must stay **red** (no marker, e.g. `callback=0 render=1`) until a genuine default-path callback with full provenance exists. A green produced by re-enabling the thunk is a **false green** and is forbidden.
- The test edit is **lead-approved** and executed by the test owner; no worker may make it unilaterally.

## 5. Ruling (d) — the separate diagnostic marker

Phase-5 thunk coverage is retained, if at all, only in a **separate, default-OFF, explicitly-labelled diagnostic** with its own token and marker file (e.g. `ALT-CALLBACK EXERCISED (DIAGNOSTIC) (NO APP-SHELL FRAME PROVEN)` and `SOBER_APP_SHELL_ALT_MARKER`), modelled on `tests/fault-baseline.sh`, with a mandatory honesty notice. It must be absent from `ctest`, `CMakeLists.txt` and `tests/verify-native.sh`, must never emit or check `app shell rendered`, and must report data only (exit 0).

If a C-level diagnostic emission is added, it must use a **distinct** function, token and file, and **must never call** `emit_marker("app shell rendered\n", ...)`. Reusing that call site would recreate exactly the weak marker decision 013 rejected. The existing `SOBER_APP_SHELL_DIAGNOSTIC` hook is the natural opt-in hook, but it too must only log inside the withheld branch and never emit the real marker.

## 6. Implementation, ownership and verification

- **Lead:** paste these doc edits; keep the gate and withhold chain strict; integrate any provenance instrumentation only by explicit ruling.
- **Test owner (lead-approved):** apply the `tests/` edits and add the default-OFF diagnostic.
- **Verifier:** re-run the A/B matrix. `SOBER_ALT_CALLBACK_PATH=1` alone and every opt-in must emit **zero** `app shell rendered` markers; the default path stays `callback=0 render=1` with zero marker; `guest init complete` / `first frame rendered`, `verify-native.sh`, ctest 2/2, ldd libc/ld-linux-only and engine SHA/BuildID must be unchanged. No success claim until the full conjunction in §3 is observed.

## 7. Honesty limits

This ruling changes no code, test, gate or engine. It records a policy decision only. The current gate already withholds on `SOBER_ALT_CALLBACK_PATH` (it is the first `seeded_state` term), so the ALT-path `HONESTY NOTICE` is unreachable; that is a strengthening and is not a license to remove any withhold entry. `onAppBridgeNotification` remains rejected. No login/app-shell frame has been proven.
````

---

## 2. Exact proposed doc edits for the lead to paste

### 2.1 New file

Create `docs/decisions/038-strict-gate-alt-thunk-ruling.md` with the verbatim
content of §1 above. It is self-contained and needs no renumbering of existing
decisions.

### 2.2 Append to `docs/decisions/013-no-weak-app-shell-marker.md`

Paste the following **after the existing final line** (the phase-8/9 note), i.e.
after line 64. Do not alter any earlier line, in particular the
`onAppBridgeNotification` rejection at `:53-64`.

````text

## Lead ruling (D11, 2026-11-20) — open ALT-thunk / `app-shell.sh` inconsistency closed

The open inconsistency recorded above (lines 46-51) is closed by decision 038
(`docs/decisions/038-strict-gate-alt-thunk-ruling.md`), which is accepted as part
of this decision. In summary:

- `SOBER_ALT_CALLBACK_PATH` / guest thunk `0x25848e0` is **diagnostic-only and never proof**. The knob is and remains in the `seeded_state` withhold chain; the ALT path is default-OFF, and its `HONESTY NOTICE` is documentation, not a pass banner. Memo Option A (restoring the thunk as proof) is rejected.
- The real marker requires the conjunction in decision 038 §3: a **guest-driven/notifier-driven** `gameLoadedCallback(J)V` (notifier `0x27eaf6e`, `placeId != 0`, cached class `0x72a2f48` / method `0x72a2fc0` non-NULL, thunk return address `base+0x27eafb3`) — or a genuinely notifier-driven `onAppShellReloadNeeded` — **plus** phase-3 render `0x649d422` / `sober_render_phase3_done() == 1`, **plus** `seeded_state == 0` (every `SOBER_*` withhold knob unset) and no ALT/HONESTY lines. A per-name counter is necessary but not sufficient. `onAppBridgeNotification` remains rejected as proof (user ruling, §53-64).
- The stale `tests/app-shell.sh:112-116` ALT-thunk assertion is to be removed (Option B), with thunk coverage moved to a separate, default-OFF diagnostic that never emits or checks `app shell rendered` (Option C). `app-shell.sh` correctly stays red until genuine proof. The test edit is lead-approved and executed by the test owner.
- Any future guest-mutating `SOBER_*` knob must be added to the `seeded_state` chain before its A/B result is trusted. The source chain, not a hardcoded count, is authoritative.

No weakening of this gate is authorized by this ruling. No success is claimed.
````

### 2.3 Append to `docs/decisions/037-callback-thunk-milestone.md`

Paste the following **after the existing final line** (line 36, the
"All earlier experimental guest patches stay OFF…" line). Keep the existing
honesty requirement at `:24-33` verbatim; this appendix makes it normative and
reconciles it with 013/038.

````text

## Amendment (lead ruling D11, 2026-11-20) — the ALT thunk is diagnostic-only

The phase-5 honesty requirement above is retained verbatim and is now made
normative. The opt-in `SOBER_ALT_CALLBACK_PATH` thunk at `0x25848e0` is a
**diagnostic-only** exercise of a real guest JNI thunk: it is harness-triggered,
proves only that our bridge can receive a guest thunk call and that the phase-3
render RVA ran, and **never** proves a login/app-shell frame. It must never
satisfy or contribute to the `app shell rendered` gate, and no passing
`tests/app-shell.sh` may rest on it.

Accordingly, the "A passing `tests/app-shell.sh` with `app shell rendered`
proves…" language in the honesty requirement above is now read subject to
decision 038 (`docs/decisions/038-strict-gate-alt-thunk-ruling.md`): with the
current gate the ALT knob is in the withhold chain, so a thunk-driven run
withholds the real marker; the only admissible pass is the real-path conjunction
defined in decision 038 §3 — a notifier-driven `gameLoadedCallback(J)V` plus the
phase-3 render with all opt-ins unset. The HONESTY NOTICE is documentation of
that limitation, not a success banner. Thunk coverage belongs in the separate
default-OFF diagnostic with its own token/marker, which must **never** call
`emit_marker("app shell rendered\n", ...)`. This amendment records no success and
makes no gate relaxation; decision 013 stays strict.
````

### 2.4 Optional one-line pointer for `PROGRESS.md` (lead pastes)

````text
- Decision 038 (lead ruling D11): the phase-5 ALT thunk (`0x25848e0`) is diagnostic-only and never app-shell proof; `tests/app-shell.sh` drops its stale ALT assertion (B+C), asserts no ALT/HONESTY lines and stays red until a notifier-driven `gameLoadedCallback` + phase-3 render with all opt-ins unset. Strict 013 unchanged; no success claimed.
````

---

## 3. Exact proposed test edits (lead approves; test owner applies)

### 3.1 `tests/app-shell.sh` — replace the whole of §3 (lines 105-120) with:

````bash
echo "== 3. strict gate evidence (DECISION 013 / 038) =="
# A pass requires a genuinely guest-driven app-shell callback AND the phase-3
# guest render. The harness-triggered ALT thunk is diagnostic-only, never proof
# (038 (a)): no ALT/HONESTY line and no synthetic-state withhold may appear.
if grep -q 'ALT PATH guest call 0x25848e0' "$ERR" \
   || grep -q 'HONESTY NOTICE' "$ERR" \
   || grep -q 'ALT PATH:' "$ERR"; then
    echo "FAIL: harness-triggered ALT thunk present (DECISION 038)"; tail -40 "$ERR"; exit 1
fi
if grep -q 'marker withheld: diagnostic guest-state seed active' "$ERR"; then
    echo "FAIL: synthetic guest state present (DECISION 013)"; tail -40 "$ERR"; exit 1
fi
if ! grep -Eq 'guest->Java callback: (gameLoadedCallback|onAppShellReloadNeeded)' "$ERR"; then
    echo "FAIL: no qualifying guest->Java app-shell callback observed"; tail -40 "$ERR"; exit 1
fi
grep -E 'guest->Java callback: (gameLoadedCallback|onAppShellReloadNeeded)' "$ERR" | tail -12
if ! grep -q 'guest call 0x649d422' "$ERR"; then
    echo "FAIL: phase-3 guest render call missing"; tail -40 "$ERR"; exit 1
fi
grep 'guest call 0x649d422' "$ERR" | tail -2
# 038 (b) decisive leg: the callback must be notifier-driven, not thunk-driven.
# The gate must emit its OWN provenance verdict with all opt-ins unset. This
# line does NOT exist yet, so this test is correctly RED today. Do NOT set
# SOBER_CALLBACK_PROVENANCE here: that knob is a diagnostic opt-in (and a
# force-withhold entry), so enabling it would violate the clean-state leg.
# It may validate the discriminator only in the separate diagnostic script.
if ! grep -Eq '\[app-shell\].*notifier-driven gameLoadedCallback.*0x27eaf6e.*0x27eafb3' "$ERR" \
   || grep -q 'HARNESS-THUNK' "$ERR"; then
    echo "FAIL: no native notifier-driven callback provenance (DECISION 038)"
    tail -40 "$ERR"; exit 1
fi
````

Notes for the lead/test owner:

- `unset SOBER_ALT_CALLBACK_PATH` at `:68` **stays**; the contradiction is removed
  by deleting the stale `:112-116` requirement, not by re-enabling the thunk.
- `:108-110` is deliberately replaced by the qualified-name grep above: the
  generic prefix `guest->Java callback:` also matches getters and was never a
  discriminator.
- §4 marker check (`:122-127`) and §5 window check stay unchanged. The test
  remains red at the new provenance guard until that evidence exists — this is
  the required state, not a regression.
- Do **not** wire any diagnostic into `ctest` or `tests/verify-native.sh`.

### 3.2 New default-OFF diagnostic (Option C) — `tests/app-shell-alt-diagnostic.sh` (skeleton)

````bash
#!/usr/bin/env bash
# D11 / decision 038 (d): DIAGNOSTIC ONLY. Default-OFF, informational, NOT in
# ctest/CMakeLists.txt/verify-native.sh. Never emits or checks "app shell rendered".
# Reports phase-5 thunk coverage as data and always exits 0.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
GUEST="/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so"
HOST_BIN="$ROOT/build/guest-init-host"
DIAG_ERR="/tmp/sober-app-shell-alt-diag.err"
ALT_MARKER="/tmp/sober-app-shell-alt-diagnostic.marker"   # NOT SOBER_SHELL_MARKER

[ -x "$HOST_BIN" ] && [ -f "$GUEST" ] || { echo "SKIP: harness/guest missing"; exit 0; }

EGL_PLATFORM=x11 LIBGL_ALWAYS_SOFTWARE=1 \
LD_LIBRARY_PATH="$ROOT/build/ndk-stubs${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
SOBER_RENDER=1 SOBER_APP_SHELL=1 SOBER_ALT_CALLBACK_PATH=1 \
SOBER_APP_SHELL_ALT_MARKER="$ALT_MARKER" SOBER_WINDOW_HOLD_MS=1000 \
timeout 120 "$HOST_BIN" "$GUEST" >/dev/null 2>"$DIAG_ERR" || true

# Own token + own marker file only. NO "app shell rendered" token/file.
DIAG_TOKEN="ALT-CALLBACK EXERCISED (DIAGNOSTIC) (NO APP-SHELL FRAME PROVEN)"
echo "$DIAG_TOKEN"
printf '%s\n' "$DIAG_TOKEN" > "$ALT_MARKER"

grep -E 'ALT PATH guest call 0x25848e0|HONESTY NOTICE|marker withheld' "$DIAG_ERR" || true
echo "diagnostic only: no app-shell frame proven; exit 0"
exit 0
````

Corresponding C-level rule for any future `guest_app_shell.c` diagnostic
emission (lead-owned `src/`, shown as a rule, not an enacted patch):

````c
/* decision 038 (d): a separate diagnostic marker must have its own token and
 * file and MUST NEVER call emit_marker("app shell rendered\n", ...).
 * The real emit_marker() call site stays behind shell_cb && render_ok. */
static void emit_alt_diag_marker(const char *text, const char *env_name)
{
    /* write(text) to stdout and to getenv(env_name) only; never SOBER_SHELL_MARKER */
}
````

### 3.3 Gate-code note (no D11 edit; for lead awareness)

No gate relaxation is needed or proposed. In the current source
`SOBER_ALT_CALLBACK_PATH` is already the first `seeded_state` term, so
`alt_path ⇒ seeded_state ⇒ shell_cb = 0 ⇒ return -4` before the sole
`emit_marker` call site; the ALT `HONESTY NOTICE` is unreachable and should stay
as documentation (or move to the §3.2 diagnostic), never as a pass banner. Keep
every withhold entry, including `SOBER_START_APP`, `SOBER_LOG_APP_BRIDGE_NOTIFICATION`
and the D02 Tier-1/2 names. Do not remove a withhold entry on the theory that a
"genuine" callback was discarded — with all opt-ins unset the default path has
`callbacks=0`, so nothing genuine is being discarded today.

### 3.4 Provenance-recorder caveat (lead integration in progress)

An untracked `src/jni/callback_provenance.c/.h` (worker B11) already implements
exactly the discriminator of ruling (b): it checks the thunk-call return site
`0x2584960` and the notifier return site `0x27eafb3` plus cached class
`0x72a2f48` / method `0x72a2fc0` identity, and logs
`[callback-provenance] … -> GENUINE | HARNESS-THUNK | UNCLASSIFIED`. It is
**default-OFF** (`SOBER_CALLBACK_PROVENANCE=1`), observation-only, and its own
header calls `sober_callback_provenance_is_active()` a "diagnostic
force-withhold signal for the gate owner". Therefore:

- the lead must add `SOBER_CALLBACK_PROVENANCE` to the `seeded_state` chain while
  it remains a diagnostic opt-in; never set it in `app-shell.sh` as proof, since
  that would break the clean-state leg;
- its log lines may be asserted only in the **separate default-OFF diagnostic**
  (§3.2) to validate that the discriminator works (GENUINE vs HARNESS-THUNK);
- for a qualifying run the gate must enforce notifier provenance **natively**,
  with all opt-ins unset, and emit the gate-side verdict that §3.1 asserts.
  Until that integration lands, `app-shell.sh` stays red.

---

## 4. Acceptance checklist for the verifier after the ruling is applied

1. `docs/decisions/038-...md` exists; 013 and 037 carry the appendices of §2;
   no earlier 013/037 text was weakened; `onAppBridgeNotification` rejection intact.
2. `tests/app-shell.sh` no longer contains `ALT PATH guest call 0x25848e0` as a
   success requirement; it asserts absence of ALT/HONESTY/withhold lines and the
   notifier/return-address provenance lines, and stays red on the default path.
3. `SOBER_ALT_CALLBACK_PATH=1` alone and each opt-in: **zero** `app shell rendered`
   emissions (stdout token and marker file both checked); ALT run reaches the
   withhold branch.
4. Default path: `callback=0 render=1`, zero markers, no ALT/HONESTY lines;
   `guest init complete` + `first frame rendered` still present.
5. `SOBER_CALLBACK_PROVENANCE` is in the withhold chain while it is a diagnostic
   opt-in; no qualifying run sets it. The gate enforces notifier provenance
   natively and emits the §3.1 verdict; the opt-in recorder is used only in the
   separate diagnostic to validate GENUINE vs HARNESS-THUNK.
6. Diagnostic script is default-OFF, absent from `ctest -N` (still exactly
   `sober_native_check`, `sober_native_version`) and from `verify-native.sh`,
   emits only its own token/file and exits 0.
7. Engine SHA-256 `3e498d6494…` / BuildID `204ad049…` unchanged; ldd libc/ld-linux
   only; all phase-2/3 invariants PASS. No success claim until §1/§3 of the
   ruling (notifier-driven callback + render + clean state) is observed.

---

## 5. Honesty limits of this draft

- This is a **draft** produced by D11; the lead must enact it. D11 edited only
  this analysis file.
- Line numbers are snapshot-specific and were drifting during the audits; the
  durable facts are the gate expression, the withhold behaviour, the two
  qualifying callback names, the notifier `0x27eaf6e` / return `0x27eafb3`
  discriminator, and the single `emit_marker` call site.
- The memo's §2.3 ALT "control run" (rc=0 + marker) predates the current source,
  where ALT is withheld; do not re-run it and report the old false green as
  current.
- `onAppBridgeNotification` remains rejected; no guest-driven app-shell callback
  has been observed; the marker remains withheld; no success is claimed.
