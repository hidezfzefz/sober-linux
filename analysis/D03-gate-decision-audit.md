# D3 — Gate/decision audit: can `app shell rendered` be emitted without strict Decision 013 evidence?

Worker: D3 (Team D). Scope: **this file only**. `src/`, `tests/`, `docs/` and
`CMakeLists.txt` were **read only**; no edits were made to them.

Inputs audited:
- `src/linux/guest_app_shell.c` (full file, current worktree)
- `tests/app-shell.sh`
- `docs/decisions/013-no-weak-app-shell-marker.md` (incl. current uncommitted phase-7/8-9 lead notes)
- Supporting (read only, for corroboration): `src/jni/native_jni.c`,
  `docs/decisions/037-callback-thunk-milestone.md`, `docs/decisions/012-jni-app-shell-milestone.md`,
  `analysis/decision-memo-alt-thunk-vs-013.md`, `tests/verification-report-471.md`, `PROGRESS.md`.

## 0. Verdict

The strict Decision 013 gate is **correctly implemented for the default path** (all opt-ins
off → zero marker emissions), and the 13 withhold-list gaps escalated by verifier #471 have
since been closed in the current code. **However, one live hole remains that can emit the real
marker without a genuine app-shell callback**, plus one unresolved test-side contradiction:

| ID | Severity | Finding |
| --- | --- | --- |
| D3-1 | **Critical** | `SOBER_ALT_CALLBACK_PATH` is **not** in the `seeded_state` withhold list. A harness-triggered thunk (`0x25848e0`) satisfies `shell_cb`, so `app shell rendered` is emitted with **no genuine guest-driven app-shell callback**. Direct conflict with 013/037. |
| D3-2 | High | `tests/app-shell.sh` is self-contradictory: it unsets `SOBER_ALT_CALLBACK_PATH` (`:68`) but requires the ALT-thunk log line (`:112-116`). The test can never verify the strict gate; the only way to make it green is the false green 013 rejects. |
| D3-3 | Medium | Withhold-list near-miss: `SOBER_START_APP` (the actual start-app driver) is not listed; only the similarly-named `SOBER_STARTAPP_PARAMS` (a parameter-building knob) is. Defence-in-depth inconsistency. |
| D3-4 | Info (positive) | `onAppBridgeNotification` correctly **cannot** satisfy the gate — consistent with the user's ruling and 013's phase-8/9 note. |
| D3-5 | Info | `SOBER_APP_SHELL_DIAGNOSTIC` does not implement a separate diagnostic marker; it only adds a log inside the withheld branch. Semantic mismatch with 013's "opt-in diagnostic", not a marker hole. |

## 1. The only marker-emission path

There is exactly **one** emission site in the harness:

- `guest_app_shell.c:1409` — `emit_marker("app shell rendered\n", "SOBER_SHELL_MARKER")`
  (`emit_marker` at `:32-45` writes to stdout **and** to the marker file).

It is reachable only through the gate at `:1385`:

```
shell_cb  = callback_was_called("onAppShellReloadNeeded")            // :1331-1332
          || callback_was_called("gameLoadedCallback")
if (seeded_state) shell_cb = 0;                                      // :1337-1380
render_ok = sober_render_phase3_done();                               // :1383
if (!shell_cb || !render_ok) { ... return -4; }                       // :1385-1401
emit_marker(...)                                                      // :1409
```

`callback_was_called()` returns `handler->count > 0`
(`src/jni/native_jni.c:1432-1440`), and `handler->count` is incremented only by
`sr_dispatch_call()` when guest code calls into the bridge
(`native_jni.c:474-516`, increment at `:502-503`). Therefore the **only** ways to
set `shell_cb` are:

1. a genuine guest→Java call to `onAppShellReloadNeeded` / `gameLoadedCallback`
   through the bridge (qualified evidence under 013), or
2. the host-side **ALT thunk** at `guest_app_shell.c:1312-1324`, which directly calls
   guest RVA `0x25848e0` with the cached `gameLoadedCallback` method ID and thereby
   drives the same dispatch/count path (**not** qualified evidence).

`render_ok` is the persisted phase-3 flag (`:1383`), which is legitimate per 013 as long as
it is paired with a genuine callback.

## 2. Findings in detail

### D3-1 (Critical) — harness-triggered thunk can emit the real marker

- `alt_path` is read at `:1155`: `getenv("SOBER_ALT_CALLBACK_PATH") != NULL`.
- The ALT block at `:1312-1324` obtains the `gameLoadedCallback` method ID and calls
  `0x25848e0(env, class, method, 0, 0, 0)` — the guest's real JNI→Java thunk, but
  triggered by the host, not by the app-shell state machine.
- That call increments the `gameLoadedCallback` handler count
  (`native_jni.c:502-503`), so `shell_cb` at `:1331-1332` becomes **true**.
- `SOBER_ALT_CALLBACK_PATH` is **absent** from the withhold chain (`:1337-1375`). With
  that single variable set and no other opt-ins, `seeded_state == 0`, `render_ok == 1`,
  and execution reaches `:1409`.
- The only guard is the `HONESTY NOTICE` at `:1403-1408`, which is a **log line, not a
  gate**. The source itself concedes the problem at `:1313-1315`: the thunk
  "may satisfy the coded callback gate, but it is HARNESS-TRIGGERED — it does NOT prove
  a login/app-shell frame."
- Corroboration (empirical, from read-only reports):
  - `analysis/decision-memo-alt-thunk-vs-013.md:95-97`: "Control —
    `SOBER_ALT_CALLBACK_PATH=1` only: rc=0, marker file **and** stdout token emitted,
    `callbacks=1`, HONESTY NOTICE printed."
  - `tests/verification-report-471.md:60-65, 99`: ALT-alone emits the marker; it is
    described as "the labelled false-green the gate must catch".
- **Contradiction:** 013 `:6-13` requires a *genuine* guest→Java callback plus phase-3
  render; 037 `:24-33` says the ALT thunk "does not prove a login/app-shell frame" and
  "Strict gate from decision 013 stays intact — no marker without the real callback."
  The current coded gate accepts the thunk as the callback. Under the user's reaffirmed
  strict ruling this is a weakening, even though the default path remains clean.

### D3-2 (High) — `tests/app-shell.sh` ALT-thunk contradiction

- `:66-68`: "no harness-triggered thunk bypass; the callback must arise from the real
  `nativeAppBridgeAppStart 0x247dc8d` path" → `unset SOBER_ALT_CALLBACK_PATH`.
- `:112-116`: requires `ALT PATH guest call 0x25848e0` in stderr, "FAIL: callback did
  not come from guest thunk 0x25848e0 (DECISION 037)".
- With the env unset, `alt_path == false` at `guest_app_shell.c:1155`, the ALT block at
  `:1312-1324` is skipped, and the string checked at `:112` can **never** be logged.
  The two assertions are mutually exclusive; the test cannot pass on the genuine path.
- This is the exact "Open inconsistency, not resolved by this note" recorded in
  `013:46-51`. The consequence for the audit: `app-shell.sh` does not actually verify
  strict 013 today. Its §4 marker check (`:122-127`) sits downstream of the ALT-thunk
  assertion, so the only way to make the test green is to re-enable the thunk — the
  "false green" 013 rejected.
- Note also that `:108-110` only greps for the loose string `guest->Java callback:`,
  which is printed by *every* callback handler (including getters like `surface()`,
  `platformParams()`, `vrContext()` and the ALT thunk), not just by the two qualified
  gate callbacks. The test therefore cannot distinguish qualified from unqualified
  callbacks even before the ALT contradiction.

### D3-3 (Medium) — `SOBER_START_APP` vs `SOBER_STARTAPP_PARAMS`

- The real start-app driver is gated at `:1209` by `SOBER_START_APP` (calls
  `nativeAppBridgeV2StartAppWithParams` at `0x26fe80f`, `:1237-1241`).
- The withhold list contains `SOBER_STARTAPP_PARAMS` at `:1356` — a *different* knob,
  consumed by `sober_startapp_params_make()` to build faithful parameters (and noted as
  such in the log at `:1211-1213`). The driver variable itself is not withheld.
- Today this does not create a false green (a genuine guest call is not itself
  disqualifying, and accessor callbacks from it are not gate callbacks), but it is a
  defence-in-depth near-miss worth correcting so that the "harness-manufactured startup"
  policy is applied consistently.

### D3-4 (Info, positive) — `onAppBridgeNotification` is correctly non-qualifying

- Registration happens only under `SOBER_LOG_APP_BRIDGE_NOTIFICATION` (`:956-958`).
- `shell_cb` (`:1331-1332`) checks only `onAppShellReloadNeeded` / `gameLoadedCallback`,
  so a fired `onAppBridgeNotification` cannot set it; additionally that env var **is**
  in the withhold list (`:1353`), forcing `shell_cb = 0`.
- Conclusion: the user's ruling and `013:53-64` are honoured in code — the notification
  may only be counted/logged, and can never emit the marker. No change needed.

### D3-5 (Info) — diagnostic-mode semantics

- `SOBER_APP_SHELL_DIAGNOSTIC` is used only at `:1393-1396`, inside the *withheld*
  branch, to print "DIAGNOSTIC ONLY (no marker)". It does not create a distinct
  diagnostic token/marker as 013 `:9-10` contemplates, and it does not withhold the
  marker when a genuine callback exists. This is harmless for strictness (a genuine
  callback legitimately produces the marker), but it is the natural hook to reuse for
  the Option-C diagnostic described below.

### Positive observations (no hole)

- The withhold chain now contains all 13 variables escalated in
  `tests/verification-report-471.md:67-80` (`:1361-1375`) plus the phase-8/9 additions.
- Default path (all opt-ins off): `callback=0 render=1` → marker withheld, `return -4`
  — confirmed by `tests/verification-report-471.md:28, 77` and
  `analysis/decision-memo-alt-thunk-vs-013.md:64-80`.
- `SOBER_GAME_LOADED_PATH` (`:1267-1285`) and `SOBER_LUA_APP_START` (`:1246-1262`) are
  in the withhold list (`:1354-1355`), so those opt-in real-path drivers cannot produce a
  green either.

## 3. Contradictions to report to the lead

1. **Code vs 013/037:** the ALT thunk is accepted as the gate callback and can emit
   `app shell rendered` (D3-1), while 013 and 037 declare it non-proof.
2. **Test vs itself:** `app-shell.sh` unsets the ALT path yet requires its log line
   (D3-2); this is the unresolved inconsistency already recorded at 013:46-51.
3. **Withhold-list naming:** `SOBER_STARTAPP_PARAMS` is withheld but the actual driver
   `SOBER_START_APP` is not (D3-3).
4. **Policy asymmetry:** every guest-mutating opt-in is disqualifying *except* the one
   that directly triggers the qualifying callback (`SOBER_ALT_CALLBACK_PATH`) — the
   exception is precisely the one that matters most.

## 4. Minimal remediation plan (strengthens, does NOT weaken Decision 013)

All edits below are lead/owner-owned; D3 performs none of them.

1. **Close the gate hole (src, lead-owned).** Add
   `getenv("SOBER_ALT_CALLBACK_PATH")` — and, for consistency,
   `getenv("SOBER_START_APP")` — to the `seeded_state` OR-chain at
   `guest_app_shell.c:1337-1375`. This uses the existing, proven defence-in-depth
   mechanism (`shell_cb = 0` at `:1376-1380`) and guarantees that a harness-triggered
   thunk can never emit the marker, regardless of the render flag. Keep the
   `HONESTY NOTICE` at `:1403-1408` as documentation. This is a pure tightening: it
   removes a false green and changes no qualified path.

2. **Fix the test contradiction (tests, lead-approved owner).** Adopt **Option B** of
   `analysis/decision-memo-alt-thunk-vs-013.md`: delete the stale
   `tests/app-shell.sh:112-116` ALT-thunk assertion; keep `:108` (a genuine callback
   was observed) and `:117-120` (phase-3 render RVA `0x649d422` ran); additionally
   assert that no `HONESTY NOTICE` / `alt_path` line is present so the callback must
   have come from the real path. Optionally tighten `:108-111` to grep for the two
   qualified callback names rather than the generic `guest->Java callback:` prefix.
   The test correctly stays red until a real default-path callback lands.

3. **Park the thunk as a labelled diagnostic (Option C companion).** Give the ALT path
   its own default-OFF marker/token (e.g. `SOBER_APP_SHELL_ALT_MARKER` with a string
   such as `alt-callback exercised (diagnostic) (no app-shell frame proven)`), reuse
   the `SOBER_APP_SHELL_DIAGNOSTIC` hook at `:1393`, and place its assertions in a
   separate default-OFF script not wired into ctest or `verify-native.sh`, modelled on
   `tests/fault-baseline.sh`. The diagnostic must never emit or check
   `app shell rendered`. This preserves phase-5 coverage without promoting it to proof.

4. **Reject Option A.** Do **not** restore the ALT thunk as app-shell proof (i.e. do not
   simply stop unsetting the variable in `app-shell.sh:68`). A notice in test output does
   not change what the literal marker asserts and would reintroduce the weak marker 013
   rejected as the done condition.

5. **Docs (lead-owned).** Amend decision 037 to record the thunk as diagnostic-only;
   resolve the open inconsistency at `013:46-51` with an explicit ruling implementing
   steps 1-3; note the added withhold entries. Keep the reaffirmation at `013:53-64`
   unchanged: `onAppBridgeNotification` remains non-qualifying.

6. **Verify (verifier).** Re-run the #471-style A/B matrix after step 1: `SOBER_ALT_CALLBACK_PATH=1`
   alone must now print `marker withheld: diagnostic guest-state seed active` and emit
   **zero** markers; confirm zero marker emissions on the default path and with every
   opt-in; re-run phase-2/3 checks, `verify-native.sh`, ctest, ldd-clean and engine
   SHA/BuildID invariants. Do not claim success until a genuine default-path
   `onAppShellReloadNeeded`/`gameLoadedCallback` plus phase-3 render is observed.

## 5. Evidence references

- `src/linux/guest_app_shell.c:32-45, 949-958, 1155, 1209, 1242, 1267-1285, 1312-1324,
  1329-1409` (read only).
- `tests/app-shell.sh:66-68, 105-127, 137` (read only).
- `docs/decisions/013-no-weak-app-shell-marker.md:6-13, 38-51, 53-64`.
- `docs/decisions/037-callback-thunk-milestone.md:20-33`;
  `docs/decisions/012-jni-app-shell-milestone.md:12-21`.
- `src/jni/native_jni.c:474-516, 1432-1440`.
- `analysis/decision-memo-alt-thunk-vs-013.md:11-44, 60-111, 121-207` (recommendation B+C).
- `tests/verification-report-471.md:55-89` (withhold-chain gap history and ALT control run).
- `PROGRESS.md:104-107, 115-147, 313-352`.
