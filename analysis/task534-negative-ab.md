# Task #529/#534 — Fail-closed negative A/B expansion N10–N20 (wave5)

Author: worker-5 (Convio team `TEAM-HAF8-HCGC-DJZ5`). Scope: **this file only**.
Analysis-only: no `src/`, `tests/`, `CMakeLists.txt`, `README.md`, `docs/`, or
vault edits; no commit. Gate code and tests were reviewed **read-only**. The only
commands run besides reads/grep were pure-text, no-write, always-exit-0
diagnostics: `SOBER_D09_WITHHOLD_AUDIT=1 bash tests/D09-withhold-audit.sh`
(output in §6) and a read-only Python tree-wide knob/chain scan (§4 W2). The
full `tests/app-shell.sh` harness was deliberately **not** run: it rebuilds
`build/`, and wave5 workers have in-flight work, so it would produce
non-attributable evidence rather than an honest A/B.

Snapshot: clean working tree at `1cb6fc5` (matches vault `State.md`). Line
numbers are for that tree; future wave5 edits may shift them — the quoted
symbols/strings are the durable evidence.

Frozen engine: BuildID `204ad049599c96afb75b8a21d5051063e821d6c9`, SHA-256
`3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335`.
**Marker status: WITHHELD. No success claimed. Gate not weakened. Lead commits/pushes.**

---

## 0. Vault review (state / ownership / verification)

* `State.md` — score ≈66%; proven init/latch/G8-under-seeds/node-discovery; blockers: no *clean*
  notifier callback (only host-triggered `publish_default` re-drive, correctly rejected), no
  clean render (worker-3: impossible without real Java surface lifecycle). Consistent with code.
* `Ownership.md` — worker-5 owns this note and withhold/test review only; all `src/`, `tests/`,
  `docs/`, git owned by lead/other workers. Respected.
* `Verification.md` — D09 must print GAP LIST `(none)`; app-shell.sh must be RED with no marker
  until genuine; seeded runs must never emit the marker. D09's `(none)` is a **scope artifact**
  (§4 W2), not proof of closure — this note reports that honestly.
* `Decisions.md` 013/038 + `Goal.md` — marker WITHHELD until genuine notifier callback +
  guest frame + `seeded_state==0`; no fabrication, no gate weakening. Held throughout.

## 1. Gate contract re-validated (read-only)

Single emitter `emit_marker("app shell rendered\n", "SOBER_SHELL_MARKER")`
(`guest_app_shell.c:2010`), reachable only if `shell_cb && render_ok` (`:1985-2002`):

* **Leg C** `shell_cb = is_genuine("gameLoadedCallback") || is_genuine("onAppShellReloadNeeded")`
  (`:1748-1749`), forced 0 by harness taint (`:1750`). GENUINE requires, at one dispatch:
  qualifying name (`SOBER_PG_QUAL_COUNT=1`, `provenance_gate.c:45-46`), frozen BuildID
  (`:469-470`), cached class/method identity (`:439-443`), `place_id>0` (`:465-467`), thunk RA
  `0x2584960`, notifier RA `0x27eafb3`, invoke RA `0x27eaf32` and node dispatch `0x27eaf04`
  entered from guest `.text` (`:270-301,393-397,477-480`). Host-entered dispatch leaves a host
  `node_caller_ra` → not genuine; any harness thunk sets sticky `g_harness`
  (`:481,494-497`) → `is_genuine()==0` (`:512-519`).
* **Leg R** `render_ok = guest_render_ok()` (`frame_evidence.c:1803-1813`) =
  phase3_done && `swaps_guest>=2` && stable guest loop RA && `draws>=1` &&
  `draws_between>=1` && `window_bound` (G8 C0–C10, `fe_window_check` `:1593-1695+`).
  RA classification is `fe_ra_tag()` guest-.text-range vs SYNTHETIC (`:556-571`);
  mixed guest/synthetic swaps set `s_mixed` (`:647-652`) and zero every decisive
  accessor (`:1757-1789`).
* **Leg S** `seeded_state` OR-chain (`:1761-1960`) forces `shell_cb=0` and prints
  `marker withheld: diagnostic guest-state seed active` (`:1961-1964`). The only positive
  provenance line `...notifier-driven gameLoadedCallback...-> GENUINE` is emitted from the
  **clean branch only** (`:1965-1971` → `provenance_gate.c:598-617`, one-shot, requires
  is_genuine && placeId>0).

## 2. N1–N9 re-spot-check (clean tree)

All nine original controls still hold; no weakening found:

| # | Case | Blocked at (current tree) | Verdict |
|---|---|---|---|
| N1 | host-only swaps | `fe_ra_tag` SYNTHETIC; `s_swaps_guest` stays 0; `guest_render_ok` needs ≥2 (`frame_evidence.c:556-571,655-710,1806`) | RED ✅ |
| N2 | synthetic draw | draw counters count guest-tagged draws only (`:712-742,1763-1772`) | RED ✅ |
| N3 | harness-thunk callback | `g_harness` taint (`provenance_gate.c:481,494-497,512-517`) + `shell_cb=0` (`guest_app_shell.c:1750`) + `SOBER_ALT_CALLBACK_PATH` chain (`:1786`) + test forbidden lines (`app-shell.sh:138-142`) | RED ✅ |
| N4 | `seeded_state>0` | chain → withhold (`:1961-1964`); `app-shell.sh:143-145` | RED ✅ (scope caveat W2) |
| N5 | missing placeId | `place_id>0` in genuine predicate (`provenance_gate.c:465-467,477-480`); `SOBER_PLACEID_*` chained (`guest_app_shell.c:1922-1923`) | RED ✅ |
| N6 | wrong RA pair / appbridge | `sc.caller==CALLER_NOTIFIER` + notifier RA required; appbridge disqualified (`provenance_gate.c:310,477-482`); test requires both RVAs (`app-shell.sh:158`) | RED ✅ |
| N7 | no G8 window proof | `fe_window_check` C0–C10 reject codes (`frame_evidence.c:1593-1695+`); `window_bound` fail-closed (`:1786-1790`) | RED ✅ |
| N8 | host-entered node dispatch re-drive with full RA pair | `node_caller_ra` host → walk fails → `guest_above_notifier=0` (`:270-301,393-397`) | RED ✅ |
| N9 | wrong invoke RA `0x27eaf34` / malformed frame | `invoke_ret != base+0x27eaf32` (`:288-292`); frame bounds/alignment (`:280-286,294-296`) | RED ✅ |

## 3. New negative controls N10–N20 (wave5 risks) — must stay RED

Wave5 aims for a *clean* (`seeded_state==0`) notifier callback and guest frame. Each case below
is a control that must still force RED/withhold after any wave5 edit. "Blocked at" is the first
mechanism that forces `shell_cb=0`, `render_ok=0`, or a test FAIL; a control that flips green is
a false-green bug.

| # | Wave5 risk / negative case | Blocked at | Verdict |
|---|---|---|---|
| **N10** | **"False unset" — knob present but empty/`0`.** A wave5 run sets a guest-mutating knob to `""` or `0` (e.g. `SOBER_FRAME_FLOG_SINK=0`, `SOBER_PROVENANCE_GATE_GUEST_BASE=`) and claims clean state. | Chain tests *presence* (`getenv(...) != NULL`), not truthiness (`guest_app_shell.c:1761-1960`) → `seeded_state=1` → withhold (`:1961-1964`). For default-ON kill switches this is conservatively false-RED, acceptable. | RED ✅ |
| **N11** | **Credential-base redirection.** Wave5 points the provenance gate at a shadow/mock image via either base-override env (`SOBER_PROVENANCE_GATE_GUEST_BASE` at `provenance_gate.h:115` or `SOBER_CALLBACK_PROVENANCE_GUEST_BASE`) to forge class/method/placeId. | Both names are in the chain (`guest_app_shell.c:1770,1895`) → withhold; plus `g_build_verified` BuildID check (`provenance_gate.c:120-144,216-218,469-470`) fails on a non-frozen image. F3 regression guard. | RED ✅ |
| **N12** | **Dead second-credential bypass.** Wave5 tries to reach green through the OR'd `onAppShellReloadNeeded` or via `onAppBridgeNotification`, instead of `gameLoadedCallback`. | `SOBER_PG_QUAL_COUNT=1` / `k_qual_names` (`provenance_gate.c:45-46`): `qual_index()` returns −1 for any other name (`:401-407`) → `is_genuine()==0` (`:512-519`); appbridge is explicitly disqualified (`:310,482`). | RED ✅ |
| **N13** | **Host-invoked JNI with cached identity.** Wave5 calls the callback from host JNI (`CallStaticVoidMethod`) using the exact cached class/method handles and a name-only callback count. | No guest stack chain → `sc.caller==CALLER_NONE` → HARNESS_THUNK verdict + sticky `g_harness` (`provenance_gate.c:481,494-497`) → `shell_cb=0` (`guest_app_shell.c:1750`); name-only counters are diagnostic only (`:1751-1756`); `app-shell.sh:158-159` forbids HARNESS-THUNK. | RED ✅ |
| **N14** | **"Clean" `publish_default` re-drive.** Wave5 re-drives `0x253cb56`/notifier `0x27eaf04` from the default-ON publish module with no env set, hoping the RA pair alone passes. | 3-frame walk needs `node_caller_ra` in guest `.text` (`:270-301,393-397`); host-entered dispatch fails even with full thunk/notifier pair. Independent of `seeded_state` (N8 regression in wave5 form). | RED ✅ |
| **N15** | **Default-ON publish module self-seed.** `publish_default` drives guest state/callback but `should_withhold()` is 0, so the env-chain alone would report clean. | `sober_publish_default_should_withhold()` returns 1 once any register/publish/hit/preinit counter is nonzero (`publish_default.c:171-190`), and it is in the chain (`guest_app_shell.c:1943`). Correct: an enabled-but-inert module does not withhold (`:178`), so a genuinely clean path stays reachable. | RED ✅ |
| **N16** | **Relabel a synthetic swap as guest.** Wave5 disables `SOBER_FRAME_SYNTH_TAG` (`=0`) and/or sets `SOBER_RENDER_SUPPRESS_SYNTHETIC`, then presents the host Route-B swap as a guest frame. | Mixed-taint authority is frame_evidence's **own** `s_swaps_synthetic`, incremented by `fe_ra_tag` in `frame_evidence_record_swap` (`:556-571,647-652,704-708`); `fe_tainted()` zeroes all decisive accessors (`:1757-1789`). Both knobs are chained (`guest_app_shell.c:1950`); synthetic-tag is observation-only, so its kill switch cannot create green (see W3). | RED ✅ |
| **N17** | **Clean H6/G8 C10.** Wave5 tries to satisfy G8 C10 (exactly one guest publish log, `frame_evidence.c:1642,1694-1695+`) without the seeded FLOG sink, then claims a clean render. | C10 observer is installed only under `SOBER_FRAME_FLOG_SINK=1` (`:361-369,1153-1155`), which is in the chain (`guest_app_shell.c:1776`); any accepting run is seeded → withhold. Structurally RED (F4); do **not** relax C10. | RED ✅ |
| **N18** | **Gate/test integrity evasion.** Wave5 emits two markers, a marker without the file, or a forbidden line (ALT PATH / HONESTY NOTICE / HARNESS-THUNK / `marker withheld:`), or relies on the missing-engine SKIP. | `app-shell.sh:138-145,158-164,186-193` fail each; `:20` SKIP exits 0 — CI hazard (W1). Marker count must be exactly 1 (`:186-188`). | RED ✅ (fix SKIP status) |
| **N19** | **New cross-TU knob not in chain.** Wave5 adds a default-ON guest-mutating module (or macro-aliased env) whose enable knob is absent from the one-TU D09 scan. | Env-presence chain + `should_withhold()` verdicts (`guest_app_shell.c:1761-1960`) must cover each new knob; D09 is single-TU/literal-only (W2) and reports `(none)` by scope, so verify with the tree-wide scan in §4. Current sweep found no live gap. | RED ✅ (audit must extend) |
| **N20** | **Positive-line leak from seeded branch.** Wave5 emits `[app-shell] gate provenance: ... -> GENUINE` (or renders a "render provenance -> OK" line) while `seeded_state>0`, so `app-shell.sh` passes though the run is withheld. | `emit_provenance()` is called only in the clean `else` branch (`guest_app_shell.c:1965-1971`), is one-shot and re-checks is_genuine + `pid>0` (`provenance_gate.c:598-617`); the `render provenance ... -> OK` string has **no emitter** in src, so `app-shell.sh:182-183` stays RED (W1). | RED ✅ |

## 4. False-green / withhold-gap findings

* **F1 CLOSED** — `SOBER_FRAME_CALLBACKS`, `_GUEST_ND` and `sober_frame_callbacks_should_withhold()`
  are chained (`guest_app_shell.c:1771-1773`).
* **F2 CLOSED** — `SOBER_PREINIT_BUS`, `SOBER_MESSAGEBUS_PRIME` chained (`:1768-1769`);
  `publish_default` also runtime-withholds (N15).
* **F3 CLOSED** — both guest-base override envs chained (`:1770,1895`) (N11).
* **F4 OPEN (no false-green; test unusable as positive gate).** No `.c` emits
  `[app-shell] render provenance: ... -> OK`; `app-shell.sh:182-183` can never pass. G8 C10 needs
  seeded `FLOG_SINK`, which forces the withhold line that `:143` rejects — the render assertions
  and `seeded_state=0` cannot co-occur. `app-shell.sh:20` SKIP still exits 0. Fail-closed, but a
  future genuine fix would still fail on a missing emitter.
* **F5 OPEN (audit-scope false-negative).** D09 hardcodes one source file
  (`D09-withhold-audit.sh:50`) and its regex matches only literal `getenv("SOBER_...")` (`:58`);
  macro lookups (`getenv(SOBER_FRAME_THREAD_ENV)`, `_SYNTH_TAG_ENV`, `_PROVENANCE_GATE_ENV`,
  `_FAITHFUL_JNI_ARRAYS_ENV`) and prefix families are invisible. "(none)" is not cross-TU closure.
  My read-only tree-wide scan at `1cb6fc5`: chain = **164 literal knobs + 1 macro-resolved
  (`SOBER_FAITHFUL_JNI_ARRAYS`, `guest_app_shell.c:1874`) + 6 prefixes**
  (`SOBER_GAME_`, `SOBER_STARTAPP_`, `SOBER_SGV2_`, `SOBER_PLATFORM_PARAMS_`, `SOBER_PROP_`,
  `SOBER_EVIDENCE_`). Of 263 src literals, 45 are uncovered and **all** are benign/gate-self/
  marker/observation-only: `SOBER_RENDER`, `SOBER_APP_SHELL`, `SOBER_PROVENANCE_GATE`,
  `SOBER_FRAME_EVIDENCE`, `SOBER_FRAME_SYNTH_TAG`, `SOBER_FRAME_THREAD`, `SOBER_FRAME_MARKER`,
  `SOBER_INIT_MARKER`, `SOBER_SHELL_MARKER`, `SOBER_*_VERBOSE`, `SOBER_TRACE_*`, host-config and
  diagnostic knobs. `SOBER_FRAME_THREAD` is covered via `sober_frame_thread_should_withhold()`
  (`:1959`); the gate self-kill-switches (`SOBER_PROVENANCE_GATE=0`, `SOBER_FRAME_EVIDENCE=0`)
  make the gate **RED**, not green, so they are correctly outside the withhold chain.
* **W3 (new, not a live gap).** `SOBER_FRAME_SYNTH_TAG` (default ON, kill switch `=0`,
  `frame_synthetic_tag.c:49-57`) is not in the chain, but its API is explicitly observation-only
  (`frame_synthetic_tag.h:77-81`); authoritative mixed-taint uses frame_evidence's independent
  `s_swaps_synthetic` (§3 N16). Disabling it cannot manufacture guest swaps. Recommend a default-run
  diagnostic line proving `enabled=1`, not a withhold entry.

**No live false-green found in the shipped tree.** Marker WITHHELD; gate fail-closed.

## 5. Recommended assertions to lead (do not weaken the gate)

1. Re-run N1–N20 after every wave5 knob/wiring change; require zero `app shell rendered`
   emissions in every RED case. Treat N10–N20 as release controls for the clean-callback/clean-render work.
2. Wire any new default-ON guest-mutating module into the `seeded_state` chain (env presence **and**
   its `should_withhold()` verdict) **before** wiring it into the callback/render path. Resolve macro
   env aliases (`*_ENV`) and model prefix families.
3. Keep both guest-base override envs (N11), `SOBER_PUBLISH_DEFAULT` runtime withhold (N15), and the
   3-frame guest walk (N8/N14) mandatory. Do not relax C10 (N17) or the placeId/BuildID checks.
4. Close F4 deliberately: implement a gate-owned, one-shot `[app-shell] render provenance: ... -> OK`
   emitter inside the genuine G2/G8 path, **or** remove that assertion from `app-shell.sh` with an
   explicit ruling. Decide either way; do not leave an accidental permanent RED. Change the
   missing-engine SKIP (`app-shell.sh:20`) to a distinct non-zero/skip status so CI cannot read it as pass.
5. Extend D09 to all `src/**/*.c|h`, resolve `SOBER_*_ENV` macros to their string values, and model
   `env_prefix_active("SOBER_*_")` as chain coverage; keep it default-OFF, always-exit-0, and out
   of ctest. Until then, use the §4 tree-wide scan as the authoritative cross-TU check.
6. Add a pre-flight assertion that all wave4/wave5 guest-mutating knobs are unset in the default run,
   and require the gate-owned `[app-shell] gate provenance: ... -> GENUINE` line plus
   `clean state: seeded_state=0` in any green run (`app-shell.sh:158-164`).

## 6. Evidence index (all read-only)

* Vault: `Home.md`, `Goal.md`, `EngineFacts.md`, `Decisions.md`, `State.md`, `Ownership.md`, `Verification.md`.
* `src/linux/guest_app_shell.c:1748-1756,1761-1960,1961-1972,1983-2010`
* `src/jni/provenance_gate.c:45-46,120-144,216-218,255-301,393-407,439-519,598-617`; `provenance_gate.h:58,114-115`
* `src/jni/frame_evidence.c:361-369,556-571,647-710,1153-1155,1391-1396,1593-1695+,1757-1824`
* `src/jni/publish_default.c:171-190`; `src/jni/frame_synthetic_tag.c:49-93`; `frame_synthetic_tag.h:77-81,115`
* `src/jni/frame_thread.c:63`; `frame_thread.h:107`; `jni_arrays.h:37`
* `tests/app-shell.sh:20,105-132,138-145,158-164,170-193`; `tests/D09-withhold-audit.sh:50,58,81-89,135-143`
* Prior audits: `analysis/task522-negative-ab-audit.md`, `analysis/D02-withhold-audit.md`, `docs/REVIEW-100.md`

## 7. D09 diagnostic output (re-run this review, default-OFF, pure text)

```
=== D09 withhold-chain audit (DECISION 013, strict) ===
getenv SOBER knobs referenced : 170
withhold-chain knobs found     : 164
-- referenced knobs NOT in the withhold chain (raw difference) --
SOBER_APP_SHELL_DIAGNOSTIC / SOBER_APP_SHELL_WAIT_MS / SOBER_APP_SLOT_DIAG /
SOBER_JNI_VERBOSE / SOBER_TRACE_FREE_SLOT / SOBER_WINDOW_HOLD_MS
-- GAP LIST: guest-mutating knobs NOT in the withhold chain --
(none)
```

As in F5, this `(none)` is scoped to one TU and literal `getenv` calls only; it does not
establish cross-TU closure. Marker WITHHELD. No gate condition weakened; no evidence fabricated.

---

## 8. Addendum — post-snapshot wave5 edits observed (in-flight, uncommitted)

Read-only re-check of the working tree after other workers' edits (note: the main matrix above
is intentionally frozen at `1cb6fc5`; re-run it after integration):

* **worker-2 / #531** added a live `JavaVM*` sample at `base+0x7cdec78` as a *conjunctive*
  requirement of the GENUINE predicate (`provenance_gate.c:471-490`: `... && java_vm != NULL`).
  This is strictly stronger than the snapshot predicate; N11/N12/N13/N14/N20 are unaffected and
  the new readiness mask is read-only. **No weakening.**
* **lead / #530** added the gate-owned render-provenance line
  (`guest_app_shell.c`, after `render_ok`):
  `if (!seeded_state && shell_cb && render_ok)` →
  `[app-shell] render provenance: guest-tagged frame at 0x649d422 (swaps=%llu, draws=%llu), phase3_done=1 -> OK`.
  This closes the missing-emitter half of **F4** in the recommended way: it cannot appear on a
  withheld run. **Verify before green:** the two values come from `sober_frame_dispatch_swaps_guest_tagged()`
  and `sober_frame_draw_guest_draws()` — confirm these are guest-RA-tagged decisive counters (not
  host/wrap-time totals), and confirm ≥2 swaps with one recurring RA and ≥1 bracketed draw as in
  `app-shell.sh:170-174`. The G8-C10-vs-clean tension (N17) is NOT resolved by this emitter.
* **lead / #530** changed the missing-engine SKIP from `exit 0` to `exit 1`
  (`app-shell.sh:20`) — closes the CI false-green hazard in F4, and added a pre-flight loop that
  unsets all inherited `SOBER_*` vars except a benign allowlist (`:68-76`), which hardens N10 at
  the harness level. Keep this; do not reintroduce a zero-exit SKIP.
* Legacy F4 residue remains: no run can pass `app-shell.sh` until both a clean notifier callback
  and clean G8 render exist; the gate must stay RED and the marker WITHHELD meanwhile.
* D09 was not changed; F5 (single-TU/literal scope) still stands. Re-run the §4 tree-wide scan
  after integration to confirm no new guest-mutating knob escaped the chain.
