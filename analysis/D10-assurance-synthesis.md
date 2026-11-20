# D10 — Assurance synthesis: one ranked list of open gaps

Worker: **D10** (logical Team D). Scope: **this file only**
(`analysis/D10-assurance-synthesis.md`). No `src/`, `tests/`, `docs/`,
`CMakeLists.txt`, gate, engine, `PROGRESS.md` or any other shared file was edited.

Governing rule: strict **decision 013**
(`docs/decisions/013-no-weak-app-shell-marker.md:6-13,53-64`). The
`app shell rendered` marker stays **WITHHELD** unless a genuine guest-driven
`onAppShellReloadNeeded`/`gameLoadedCallback` plus the phase-3 guest render
(`0x649d422`) are observed with **no synthetic state, no ALT thunk, and no
`onAppBridgeNotification` as proof**. The user's ruling is gate-strict.

**No success is claimed anywhere in this synthesis.** Every item below is an
open gap, a repair proposal owned by someone else, or an acceptance criterion —
never a passing milestone. "PASS" appears only where a Team D report recorded an
invariant check at a stated snapshot.

Snapshot: 2026-11-20 ~21:41 UTC. Other workers were still editing
`src/linux/guest_app_shell.c` and writing `tests/D*` files concurrently. Line
numbers and knob counts are therefore snapshot-specific; prefer the symbolic
anchors (`shell_cb`, `seeded_state`, `render_ok`, `emit_marker` call site) given
in D6 §4.7. Frozen engine for every RVA here:
`/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so`, BuildID
`204ad049599c96afb75b8a21d5051063e821d6c9`, SHA-256
`3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335`. Never mix
with the Flatpak `791c3f9e…` / 2.745.1142 RVAs.

---

## 0. Inputs synthesized

| Input | What it contributes |
| --- | --- |
| `tests/D01-invariants.md` | Eight invariants PASS at 21:34–21:39; default gate `callback=0 render=1`, zero marker; one intermittent XIO teardown failure; advises final re-run. |
| `analysis/D03-gate-decision-audit.md` | Gate has one emission site; `SOBER_ALT_CALLBACK_PATH` hole (D3-1), `app-shell.sh` self-contradiction (D3-2), `SOBER_START_APP` near-miss (D3-3), `onAppBridgeNotification` correctly non-qualifying (D3-4), diagnostic-mode semantics (D3-5); B+C remediation, reject A. |
| `tests/D04-baseline-tracking.md` | Single regression table: 3 fixed faults (`0x2e5d42a`, `0x2e7b8e2`, `0x6caef67`) that must stay absent, 5 open faults (`0x2eb4b65`, `0x24ad562`, `0x2772403`, `0x2ec1042`, `0x31a1360`), comparison rules. |
| `analysis/D05-reports-index.md` | Index of A0*/B0*/C0*/D0* and key reports; snapshot at ~21:37, explicitly to be regenerated when more files land. |
| `analysis/D06-decision-crosscheck.md` | Consistency check and findings F1–F7: doc drift, phantom knob, "no gate change" staleness, dead HONESTY NOTICE, open 037-vs-013 policy item, residual provenance gap. |
| `analysis/D07-progress-draft.md` | Draft PROGRESS Phase 10 append for the lead: dependency chain TSD → ctor → singleton → app fields → callback; gate unchanged; no success claimed. |
| `tests/D08-dispatch-diag.sh` | Default-OFF, informational baseline driver for the full opt-in game-loaded stack; records fault RVA, callback lines, withhold lines, engine SHA; never emits/checks the marker; exits 0. |
| `tests/D09-withhold-audit.sh` | Default-OFF static audit of `getenv("SOBER_…")` vs `seeded_state`; read-only; GAP LIST classification. |
| `analysis/D02-withhold-audit.md` | Project-wide audit: 163 `SOBER_*` literals across all of `src/`; 47 names in chain at its 21:41:16Z snapshot; **78 confirmed Tier-1/2 gaps + 10 Tier-3 names + the `SOBER_PROP_*` family**; alias/ctor-injection bypasses; D09's "no gaps" is file-local only. Drives rank R3. |
| Corroborating (read only) | `tests/verification-report-471.md` (13 earlier withhold gaps), `analysis/decision-memo-alt-thunk-vs-013.md` (B+C recommendation), `analysis/A10-stability-synthesis.md` (ranked blocker chain and acceptance), `analysis/C03-appshell-frame-evidence.md` (R/S/C evidence standard), `PROGRESS.md` Phase 9. |

## 1. Ranking rule

Ranks are ordered by: **(1)** whether the gap blocks the strict done condition;
**(2)** whether it can admit a false green (marker without genuine evidence);
**(3)** whether it erodes the audit trail (stale docs, unexercised diagnostics,
unverified snapshots); **(4)** hygiene. Items already mitigated in the current
worktree are listed in §4 and are *not* ranked. "Owner" follows the scope rule:
lead owns `src/`, `CMakeLists.txt`, `PROGRESS.md`, `docs/`; verifier/test owner
handles `tests/` reports/scripts; analysis workers own their own `analysis/`
files. No rank authorizes a gate relaxation.

---

## 2. Ranked list — summary

| Rank | Severity | Open assurance gap | Owner | Primary file(s) |
| --- | --- | --- | --- | --- |
| **R1** | Critical | No qualifying app-shell callback on the default path; marker cannot legitimately be emitted. | Lead (integration) + guest/JNI analysis workers | `src/linux/guest_app_shell.c`, `src/jni/*`, `tests/app-shell.sh`, `PROGRESS.md` |
| **R2** | High | `tests/app-shell.sh` is self-contradictory and cannot verify the strict gate; its only green path is the false green 013 rejects. | Lead (ruling) + test owner | `tests/app-shell.sh`, `docs/decisions/013`, `docs/decisions/037` |
| **R3** | High | Withhold-chain coverage is incomplete and drifting: D02 finds 78 confirmed Tier-1/2 gaps across `src/` (+10 Tier-3 and a dynamic `SOBER_PROP_*` family), plus alias/ctor-injection bypasses; D09's "no gaps" is file-local only. | Lead (`src` chain) + D2/D9 (audit scripts) + verifier (re-run) | `src/linux/guest_app_shell.c`, all `src/` modules, `tests/D09-withhold-audit.sh`, `analysis/D02-withhold-audit.md` |
| **R4** | High | Gate is name-only: no guest-return-address provenance; a future direct call to thunk `0x25848e0` would defeat it. | Lead + JNI-fidelity workers (B/C) | `src/jni/native_jni.c`, `analysis/C03`, `analysis/B06`, `analysis/B09` |
| **R5** | High | Five open guest-state faults keep the default path from ever reaching a genuine callback. | Lead (integration) | `src/linux/guest_app_shell.c`, `src/jni/*`, `tests/D04-baseline-tracking.md` |
| **R6** | Medium | Decision docs, reports index and progress draft are stale/phantom-laden; an old false-green run can be misreported as current. | Lead (`docs/`, `PROGRESS.md`) + D5/D6 (their files) | `docs/decisions/013`, `037`, `analysis/decision-memo-alt-thunk-vs-013.md`, `analysis/D05-reports-index.md`, `analysis/D07-progress-draft.md` |
| **R7** | Medium | New diagnostic scripts D08/D09 are default-OFF and have no recorded baseline run; their allowlist/stack is unreviewed. | Test owner + verifier | `tests/D08-dispatch-diag.sh`, `tests/D09-withhold-audit.sh` |
| **R8** | Medium | Invariant PASS results predate the lead's final integration; a final re-run is outstanding. | Verifier | `tests/D01-invariants.md`, `tests/verification-report-*.md` |
| **R9** | Low | Intermittent libX11 `XIO` teardown makes `first-frame.sh` spuriously rc=1. | Verifier / test owner | `tests/D01-invariants.md` §2, `tests/first-frame.sh` |

---

## 3. Detailed gaps and acceptance criteria

### R1 (Critical) — No qualifying callback on the default path
- **Evidence:** D01 §4: `tests/app-shell.sh` rc=1 at §3, `callback=0 render=1`,
  zero marker; D07 draft "Remaining blockers": only the rejected
  `onAppBridgeNotification` has ever been observed; A10 §2 Rank 5/6; C03 §0–§1.
- **Why open:** every working route today uses synthetic seeds or a harness
  thunk, all of which force-withhold. No default-path evidence satisfies the
  three-leg standard (R render `0x649d422`, S guest-owned `app+0x408`/`+0x440`/
  singleton `0x72cb5e0`/`userDidLogin 0x2771f6c`, C guest-entered notifier
  `0x27eaf6e` → thunk `0x25848e0` → `gameLoadedCallback(J)V`).
- **Acceptance (all must hold simultaneously, with every `SOBER_*` opt-in unset):**
  1. Guest itself enters notifier `0x27eaf6e` with `RSI = placeId`; cached class
     `0x72a2f48` / method `0x72a2fc0` non-NULL; thunk `0x25848e0` fires; bridge
     log `guest->Java callback: gameLoadedCallback` with count ≥ 1; call returns
     into guest at `0x27eafb3`.
  2. Phase-3 guest render `0x649d422` executes with `render_ok = 1`.
  3. `[app-shell] marker withheld: diagnostic guest-state seed active` is
     **absent** (`seeded_state == 0`); no `ALT PATH guest call 0x25848e0`, no
     `HONESTY NOTICE`; no `onAppBridgeNotification` used as proof.
  4. Exactly one `app shell rendered` emission (stdout **and** marker file);
     D01's eight invariants re-run PASS; engine SHA/BuildID unchanged.
  Until then the marker stays withheld and **no success is claimed**.

### R2 (High) — `app-shell.sh` cannot verify the strict gate
- **Evidence:** D3-2 (`:66-68` unsets `SOBER_ALT_CALLBACK_PATH`; `:112-116`
  requires the ALT log line; `:108-110` greps the generic callback prefix),
  D6-F5 (open policy item), D6 §2.4, decision 013 `:46-51`.
- **Why open:** the two assertions are mutually exclusive; downstream marker
  checks sit after the unsatisfiable ALT assertion, so the only way to make the
  test green is re-enabling the thunk — the false green 013 rejected (Option A).
- **Acceptance:**
  1. Lead records an explicit ruling resolving decision 013 `:46-51` and
     amends decision 037 to mark the ALT thunk diagnostic-only (D3 step 5,
     D6 §4.4).
  2. The stale `ALT PATH guest call 0x25848e0` assertion is removed; if thunk
     coverage is retained it moves to a **separate default-OFF** script that
     never emits/checks `app shell rendered` (D3 Option C / verifier memo B+C).
  3. The qualified-callback grep targets `gameLoadedCallback` /
     `onAppShellReloadNeeded` by name and additionally asserts **no**
     `HONESTY NOTICE` / `alt_path` line.
  4. The test stays **red** until R1 holds; Option A (restoring the thunk as
     proof) is explicitly rejected.

### R3 (High) — Withhold-chain coverage incomplete and drifting
- **Evidence:** D02 (21:41:16Z) finds 163 distinct `SOBER_*` literals across
  `src/`, 116 outside the chain, classified as **22 Tier-1** (direct guest
  memory/code mutation, synthetic guest state, guest-path injection, module
  enable), **46 Tier-2** (value/strategy modifiers of withheld modules),
  **10 Tier-3** (guest-visible config seeding) plus the dynamic
  `SOBER_PROP_<name>` family — i.e. **78 confirmed named gaps** before Tier 3.
  Concrete bypasses: `SOBER_STARTAPP_V2` aliases the in-chain
  `SOBER_STARTAPP_PARAMS` (`startapp_v2.c:127-128`); `SOBER_HELPER_REPAIR` (v1)
  does the same guest-slot write as the in-chain `_V2`
  (`bionic_bootstrap.c:984-997`); `SOBER_CTOR0` / `SOBER_CTOR_RANGES=all`
  inject ctor[0] bypassing in-chain `SOBER_REPLAY_FFLAG_CTOR`
  (`guest_dlopen_host.c:428-460`); `SOBER_BIONIC_TSD_GOT` writes guest GOT slots
  (`android_relocs.c:296-304`). D02 also shows D09's "GAP LIST: (none)" is
  **file-local**: D09 parses only `guest_app_shell.c` and misses all 78 Tier-1/2
  gaps in the other 13 modules. D6 recorded 37→39 knobs in one minute; D02 saw
  40→46→47; my own D09 `--run` saw 40 with three gaps
  (`SOBER_SCHEDULE_RUNNABLE`, `SOBER_SCHED_RVA`, `SOBER_TLS_BYTES`) that the lead
  had added by D02's snapshot. D6-F7: residual provenance risk.
- **Why open:** any guest-mutating knob reaching thunk `0x25848e0` without a
  withhold entry can defeat the name-only gate; the chain is growing faster
  than the audits, and the current diagnostic cannot certify project scope.
- **Acceptance:**
  1. Lead adds all D02 Tier-1 and Tier-2 names to `seeded_state`, explicitly
     adds `SOBER_STARTAPP_V2` next to the legacy alias, and resolves the
     `SOBER_HELPER_REPAIR` v1/v2 and `SOBER_CTOR0`/`CTOR_RANGES` bypasses;
     `SOBER_PROP_*` is prefix-matched if the lead adopts Tier 3.
  2. A **source-tree-wide** check (D02 §1 method: `getenv` + indirect names +
     `env_or`/`env_int` wrappers, not just `guest_app_shell.c`) reports zero
     unclassified guest-mutating knobs; every future knob is added to the chain
     or an explicitly justified benign allowlist **before** its A/B run is
     trusted (D6 §4.1, D3 §3.4).
  3. D09 stays default-OFF, read-only, exits 0 and never emits/checks the
     marker; its output is labelled **file-local**, not a project-level gate.
  4. `SOBER_RENDER`/`SOBER_APP_SHELL` and marker-path knobs stay **out** of the
     chain (D02 §5.5-6); re-run D02 and D09 after each lead integration and
     record the reconciled counts in a verifier report.

### R4 (High) — Name-only provenance; no guest-return discriminator
- **Evidence:** D3 §1 (`shell_cb` is `callback_was_called(name)` only), D6-F7,
  B06 (`callback_was_called()` cannot prove provenance; ALT thunk produces
  byte-identical counts), B09 (re-entry guard, `last_callback`, NULL return,
  non-atomic counters), C03 §1 leg C and P1.
- **Why open:** with ALT and `onAppBridgeNotification` now withheld, the known
  false-green sources are closed, but a future opt-in calling thunk `0x25848e0`
  directly would still satisfy the gate; the decisive notifier entry
  `0x27eaf6e` / return `0x27eafb3` check is not implemented.
- **Acceptance:**
  1. An **additive, default-OFF** provenance recorder captures the caller/return
     site of `gameLoadedCallback`; A/B shows it distinguishes a thunk-driven
     call from a notifier-driven one (`0x27eaf6e` → `0x25848e0` → `0x27eafb3`).
  2. No gate change is made without an explicit lead ruling; if one is later
     approved it must strengthen only, never accept a harness-triggered call.
  3. The four additive B09 handler fragilities are fixed without changing the
     completion contract; no fabricated counts; strict 013 unchanged.

### R5 (High) — Open guest-state faults block the real path
- **Evidence:** D04 table open rows `0x2eb4b65`, `0x24ad562`, `0x2772403`,
  `0x2ec1042`, `0x31a1360`; D07 draft blockers (TSD GOT `0x72298b0` bound to
  glibc, RBXCRASH `int3 0x6caef66`, singleton `0x72cb5e0`, app fields
  `+0x408`/`+0x440`/`+0x488`); A10 §1 dependency chain B1→B5.
- **Why open:** these are the causal reason R1 has no genuine callback; synthetic
  seeds merely mask `0x2772403`/`+0x440` and cannot be used as evidence.
- **Acceptance (per D04 comparison rules):**
  1. Each open row is fixed in **env-free default guest code** or reclassified
     with its new RVA and exact knob delta (D04 rule 3); no byte-patching of
     checks, no fake vtables, no `_abcdef` canary change, no RBXCRASH mode bytes.
  2. Fixed rows `0x2e5d42a`/`0x2e7b8e2`/`0x6caef67` stay absent under full
     opt-in F; default row `0x2eb4b65` still recorded by `fault-baseline.sh`
     with `signal=11, rdi=(nil)`, engine SHA/BuildID unchanged, zero marker.
  3. One new knob per A/B run; every new knob lands in the R3 withhold chain.
  4. Then R1's acceptance is evaluated with all opt-ins unset.

### R6 (Medium) — Stale decision docs, index and progress draft
- **Evidence:** D6 F1 (memo/C03 anchors and "eight entries" stale vs 40), F2
  (phantom `SOBER_SEED_JNI_STRING_BRIDGE` in 013 `:40-44`), F3 ("No gate change
  was made" now false), F4 (ALT HONESTY NOTICE control run is dead code),
  F6 (count drift); D05 §0 snapshot warning; D07 draft not pasted.
- **Why open:** a reader may repeat the pre-21:37 ALT control and report its old
  false green as current, or test a knob that no longer exists.
- **Acceptance:**
  1. Replace phantom knob with live names; correct "no gate change" to
     "no weakening change; ALT and other guest-mutating knobs were added as a
     strengthening dated 2026-11-20"; keep `onAppBridgeNotification` rejection
     verbatim; amend 037 per R2.
  2. Mark the memo §2.3 control run as a pre-21:37 observation and preserve its
     B+C recommendation verbatim; correct C03's HONESTY NOTICE and "40+" claims.
  3. Prefer symbolic anchors over line numbers/counts in all four docs.
  4. D05 index regenerated after all `D0*` files land; D07 draft either pasted
     into `PROGRESS.md` or explicitly rejected by the lead.

### R7 (Medium) — D08/D09 diagnostics unreviewed and unexercised
- **Evidence:** D08 header (default-OFF, never in ctest/verify-native, always
  exit 0, no marker emission/check); D09 default-OFF gate; D01 §5 advises a
  final re-run; D05 notes D3's findings are open audit findings.
- **Why open:** scripts exist but no recorded baseline/audit output confirms the
  full opt-in stack's fault/callback/withhold state, and D09's benign allowlist
  has not been reviewed against the live source (R3).
- **Acceptance:**
  1. At least one recorded D08 run (pasted into a verifier report) showing the
     engine SHA unchanged, zero marker emission/check, and the observed fault
     RVA / callback lines / withhold lines as data.
  2. At least one recorded D09 audit run with the GAP LIST disposition of R3.
  3. Both remain absent from `ctest`, `CMakeLists.txt` and `verify-native.sh`;
     neither ever emits, greps for success on, or asserts `app shell rendered`.

### R8 (Medium) — Final invariant re-run after lead integration
- **Evidence:** D01 §5 "A subsequent re-run is advised after the lead's final
  integration, since other Team-D workers are still editing `tests/` in
  parallel"; D04 rule 1 (default row is the anchor); D05 snapshot caveat.
- **Why open:** all PASS results predate ongoing `src/` edits (the chain grew
  during the audits), so no single final snapshot certifies the integrated tree.
- **Acceptance:**
  1. After the lead's last integration and after all `D0*` files land, all eight
     D01 invariants PASS at one recorded snapshot: build, `verify-native.sh`,
     ctest 2/2, ldd libc/ld-linux only, engine SHA/BuildID unchanged,
     `first-frame.sh` functional markers, `fault-baseline.sh` default-OFF,
     zero `app shell rendered`.
  2. `ctest -N` still lists exactly 2; diagnostic scripts still default-OFF;
     any transient failure documented, not hidden.

### R9 (Low) — Intermittent first-frame XIO teardown
- **Evidence:** D01 §2 run 1 rc=1 from `XIO: fatal IO error 2 … on X server
  ":0"` at window teardown; runs 2–3 clean; same message on the app-shell run.
- **Why open:** a spurious red can be misread as a regression (or a clean re-run
  as a fix); the functional markers were present in all runs.
- **Acceptance:** root-cause or document the Xwayland teardown as environmental
  with a stable re-run criterion; `first-frame.sh` keeps asserting
  `guest init complete`, `first frame rendered`, swap `0x649d422`, 1 guest
  swap, visible window and unchanged engine SHA; no regression/success claim is
  based on the transient rc.

---

## 4. Not ranked — already mitigated at this snapshot (re-verify, do not claim)

- **D3-1 (was Critical):** D3 found `SOBER_ALT_CALLBACK_PATH` absent from the
  withhold list. The current source now has it as the first `seeded_state` term
  (`guest_app_shell.c:1351`), so D3-1's hole is closed **in code**; D6-F4 notes
  the HONESTY NOTICE is therefore unreachable dead code. Must be confirmed at
  R8's final run; do not repeat the old ALT control and report it as current.
- **D3-3 (was Medium):** `SOBER_START_APP` is now in the chain (`:1352`).
- **Verifier #471's 13 gaps:** now present in the chain (`:1376-1392`); R3
  covers the residual three and future churn.
- **D3-4 (positive):** `onAppBridgeNotification` cannot satisfy the gate; its
  handler is opt-in and its knob is withheld. Keep unchanged.
- **D01 invariants 1–8:** PASS at the 21:34–21:39 snapshot (see R8 for the
  outstanding final re-run).
- **D08/D09:** correctly default-OFF, read-only/no-side-effect, no marker
  emission or check (see R7 for the missing recorded runs). D09's three gaps
  (`SOBER_SCHEDULE_RUNNABLE`, `SOBER_SCHED_RVA`, `SOBER_TLS_BYTES`) were added
  to the chain by the lead between my D09 run and D02's snapshot; the broader
  project-wide gaps remain open in R3.
- **Gate semantics:** sole `emit_marker` call site remains behind
  `shell_cb && render_ok`; `seeded_state` forces `shell_cb = 0`; no worker
  relaxed the gate.

---

## 5. Cross-cutting invariants (bind every rank)

1. Strict decision 013 and the user's ruling are invariant: no weak marker,
   no ALT thunk as proof, no `onAppBridgeNotification` as proof, no synthetic
   state as evidence, no gate weakening.
2. The frozen engine identity (BuildID `204ad049…`, SHA `3e498d6494…`) and its
   RVA set are unchanged before and after every run; never mix Flatpak RVAs.
3. Diagnostic scripts stay default-OFF, out of ctest/`verify-native.sh`, and
   never emit or check `app shell rendered`; `fault-baseline.sh` likewise.
4. Any newly added `SOBER_*` knob is added to the withhold chain (R3) before its
   A/B result is trusted; no knob may silently discard or manufacture a
   qualifying callback.
5. No byte-patching of guest checks, no fake vtables/callbacks, no
   `__stack_chk_guard` change, no RBXCRASH mode-byte change.
6. One new variable per A/B run; fixed faults must stay absent; open faults may
   move but not vanish quietly (D04 rule 3).
7. No success claim until R1's full acceptance holds with all opt-ins unset.

---

## 6. Honesty limits

- This is a synthesis of other workers' reports plus read-only observation; I
  ran only the read-only D09 audit (`--run`) and did not build, run the harness,
  or execute any test. No marker was emitted by me.
- `analysis/D05-reports-index.md` is a ~21:37 snapshot (it predates D02/D10)
  and `analysis/D07-progress-draft.md` is a draft the lead has not pasted; both
  are ranked in R6.
- The source is being actively edited: D6 recorded 37→39 chain knobs in one
  minute, my D09 run saw 40 with three gaps, and D02's snapshot saw 47 with the
  three gaps already closed. All counts/line numbers here are snapshot-specific;
  re-verify anchors and re-run both audits before acting.
- D02's own volatility caveat applies: names it lists may already be in the
  chain when this is read; subtract them rather than reporting them as open.
- D3-1/D3-3 are described as **mitigated in code**, not as closed by a final
  run; R8 must confirm them. Nothing above asserts the app-shell goal has been
  reached.
