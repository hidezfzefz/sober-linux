# D06 — Decision cross-check: decision 013 vs decision memo vs C03 vs current gate

Owner: **worker D6** (logical Team D). Date: 2026-11-20. Scope: **this analysis file only**.
No `docs/`, `src/`, `tests/`, `CMakeLists.txt` or gate edits were made. All four inputs were
read only. Strict **decision 013** throughout; the user's explicit ruling stands: **gate
strict, `onAppBridgeNotification` rejected as app-shell proof**. Nothing here relaxes,
bypasses or reclassifies the `app shell rendered` gate.

## 0. Snapshot, method, and an important volatility warning

Inputs cross-checked:

| Input | mtime (UTC) | Role |
| --- | --- | --- |
| `docs/decisions/013-no-weak-app-shell-marker.md` | 19:51:45 | ruling under test |
| `analysis/decision-memo-alt-thunk-vs-013.md` | 17:16:12 | verifier's B+C recommendation |
| `analysis/C03-appshell-frame-evidence.md` | 21:34:30 | evidence standard / acceptance checklist |
| `src/linux/guest_app_shell.c` | 21:37:42 (still changing) | current gate, read only |
| `docs/decisions/037-callback-thunk-milestone.md`, `PROGRESS.md`, `tests/app-shell.sh` | older | corroborating context |

**Volatility warning (material to several findings):** the gate was edited by another
worker *during* this cross-check. At 21:36 the withhold chain had 37 knobs and did **not**
contain `SOBER_ALT_CALLBACK_PATH`; at 21:37:42 it had **39**, including
`SOBER_ALT_CALLBACK_PATH` (`:1337`) and `SOBER_START_APP` (`:1338`). Line numbers after
`:1337` shifted accordingly. This is a **strengthening** (consistent with strict 013),
but it means every hardcoded count and line number in the three decision documents is
already drifting. Cross-check below is against the 21:37:42 snapshot.

## 1. Authoritative state of the current gate (21:37:42 snapshot)

- `src/linux/guest_app_shell.c:1331-1332` — `shell_cb` is *only*
  `callback_was_called("onAppShellReloadNeeded") || callback_was_called("gameLoadedCallback")`.
  Name-based; no provenance check.
- `:1337-1377` — `seeded_state` is a 39-term OR-chain; `SOBER_ALT_CALLBACK_PATH`,
  `SOBER_LOG_APP_BRIDGE_NOTIFICATION`, `SOBER_GAME_LOADED_PATH`, `SOBER_LUA_APP_START`,
  `SOBER_STARTAPP_PARAMS`, `SOBER_REAL_APPSTATE_PATH`, the `SOBER_SEED_*` / phase-8
  opt-ins and the patch/object-fix knobs are all disqualifying.
- `:1378-1382` — any active withhold knob forces `shell_cb = 0` and logs
  `marker withheld: diagnostic guest-state seed active`.
- `:1385` — `render_ok = sober_render_phase3_done()` (persisted phase-3 flag).
- `:1387-1403` — gate is `shell_cb && render_ok`; failure returns `-4`, logs
  `callback=%d render=%d (env calls=… callbacks=… last='…')`, and runs the opt-in
  `SOBER_APP_SHELL_DIAGNOSTIC` notice.
- `:1405-1410` — HONESTY NOTICE, then the **sole** `emit_marker("app shell rendered\n",
  "SOBER_SHELL_MARKER")` call site.
- `:952-958` — `shell_callback` installed for the two qualifying callback names;
  `onAppBridgeNotification` gets a handler **only** under
  `SOBER_LOG_APP_BRIDGE_NOTIFICATION`, which is itself in the withhold chain.
- `:1155`, `:1312-1324` — `SOBER_ALT_CALLBACK_PATH` skips AppStart and directly calls
  thunk `0x25848e0`, logging `ALT PATH guest call 0x25848e0`.

## 2. What is consistent (no action needed)

1. **Core ruling intact in all four inputs.** Real marker requires a genuine
   `onAppShellReloadNeeded`/`gameLoadedCallback` **plus** the phase-3 guest render;
   `onAppBridgeNotification`, accessor getters (`surface`/`platformParams`/`vrContext`)
   and harness-triggered thunks are not proof. Decision 013 (`:6-13`, `:53-64`), memo
   (`:171-192`), C03 (`:25-44`, `:160-179`, `:242-244`) and the gate all agree. ✓
2. **Default path.** All documents claim `callback=0 render=1`, marker withheld, no
   success claimed; the code's `shell_cb && render_ok` gate reproduces exactly that. ✓
3. **Lead's phase-8/9 ruling is mirrored.** `onAppBridgeNotification` may only be
   counted/logged by `SOBER_LOG_APP_BRIDGE_NOTIFICATION`; that knob is in the withhold
   chain (`:1355`) and its handler is opt-in (`:956-958`). ✓
4. **The test contradiction decision 013 flagged is still real and still open.**
   `tests/app-shell.sh:68` unsets `SOBER_ALT_CALLBACK_PATH`; `:112-116` requires the
   `ALT PATH guest call 0x25848e0` line. Decision 013 (`:46-51`) describes this
   accurately; the memo (`:39-44`) and the verification reports agree. ✓
5. **C03's gate anchors** (`:1319-1323`, `:1331-1332`, `:1337-1375`, `:1403-1408`,
   `:1409`) matched the 21:34 snapshot; its evidence standard is stricter than, and
   compatible with, the coded name-only gate. ✓
6. **No weakening proposed by any input.** The memo's B+C recommendation, C03's
   checklist and B06's origin-check proposal all keep or strengthen the gate. ✓

## 3. Contradictions and factual drift (findings F1–F7)

**F1 — Stale line numbers and counts in the decision memo (and now C03).**
The memo's §1 says "**current line numbers**, phase-8 wiring applied" and cites
`:1022`, `:1125-1137`, `:1144-1145`, `:1150-1157`, `:1158-1162`, `:1165`, `:1167-1183`,
`:1185-1190`, `:1191`, `:826-878`. In the 21:37:42 gate these anchors are `:1155`,
`:1312-1324`, `:1331-1332`, `:1337-1377`, `:1378-1382`, `:1385`, `:1387-1403`,
`:1405-1410`, `:1411`, `:943-1000`. C03's anchors were correct at 21:34 but shifted
+1..+2 after the 21:37 edit (`:1409` → `:1411`). The memo also states the withhold list
"has **eight** entries" (`:35`, `:144`, `:186`); it now has 39. C09 already flags this
supersession (`analysis/C09-marker-evidence.md:144-145`). **Severity: documentation
accuracy only; no gate impact.**

**F2 — `SOBER_SEED_JNI_STRING_BRIDGE` is a phantom withhold knob in decision 013.**
Decision 013 `:40-44` names it as one of the phase-7 force-withhold seeds. It appears
nowhere in `src/`; the only other reference is `tests/verification-report-451.md:102`,
where it is described as **removed** ("The removed `SOBER_SEED_JNI_STRING_BRIDGE`
could not work…"). The live equivalents are `SOBER_SEED_GLOBAL_STRING`,
`SOBER_SEED_JNI_VM`, `SOBER_SEED_JNI_SINGLETON`. **Severity: documentation accuracy;
a reader could test the wrong knob and wrongly conclude the withhold is absent.**

**F3 — "No gate change was made" is now literally false.**
Decision 013's phase-8/9 note (`:53-64`, esp. `:61`) and `PROGRESS.md:276-280`,
`:324-330` say "No gate change was made". The 21:37 edit added at least
`SOBER_ALT_CALLBACK_PATH` and `SOBER_START_APP` to the withhold chain. This is a
**strengthening fully consistent with strict 013**, but the absolute statement is stale.
**Severity: low; must not be read as permission to remove any withhold entry.**

**F4 — The ALT-path HONESTY NOTICE is now unreachable dead code, and three documents
still describe it as a live pass banner.**
`alt_path = getenv("SOBER_ALT_CALLBACK_PATH") != NULL` (`:1155`) and the same env var is
now the first `seeded_state` term (`:1337`). Therefore `alt_path ⇒ seeded_state ⇒
shell_cb = 0 ⇒ return -4` at `:1402`, **before** `:1405-1410`. Consequences:
- memo §2.3 control run (`:95-97`: "rc=0, marker file and stdout token emitted,
  HONESTY NOTICE… the known false-green") and §4 (`:184-185`: "§2.3 shows the ALT path
  still emits that marker today") are no longer true of the current code;
- C03 §4.3 table (`:171`: ALT thunk "Prints `HONESTY NOTICE`") is no longer true;
- decision 037's honesty clause (`:24-33`) describes a passing `app-shell.sh` that the
  current gate can no longer produce.
The *policy* in those documents is still correct; only their empirical claims are stale.
**Severity: medium — a reader may repeat the pre-21:37 control run and report a false
green that can no longer occur.**

**F5 — Decision 037 vs decision 013 tension remains unresolved (already partly
acknowledged).** 037 (`:26-33`) treats a green `app-shell.sh` as proof of "a genuine
guest→Java callback" via the harness-triggered thunk with a notice; 013 (`:46-51`)
calls that same thunk "not app-shell proof" and defers tightening to an explicit lead
decision. The memo's Option B/C and its §5 decision #4 (amend 037 to mark the ALT thunk
diagnostic-only) are the minimal resolution; no such ruling/test edit has been made.
The user's ruling ("gate strict, `onAppBridgeNotification` rejected") does **not**
settle the ALT-thunk question. **Severity: open policy item; no code change.**

**F6 — Count drift in C03/C09 vs code.** C03 P1 (`:191-198`) says "all 40+ env vars";
C09 says "All 37 … plus `SOBER_ALT_CALLBACK_PATH` unset". The 21:37:42 chain has 39,
**including** ALT. Both are approximate snapshots of a list that is actively growing.
**Severity: cosmetic, but it will keep going stale.**

**F7 — Residual provenance gap is real but already disclosed.** C03's decisive
discriminator C3 (notifier `0x27eaf6e` entry / return address `0x27eafb3`, `:234-241`)
is not implemented; the gate remains name-only (`shell_cb` at `:1331`). C03 (`:176-179`)
and B06 (`:278-284`) say so explicitly. With ALT and `onAppBridgeNotification` now
withheld, the *known* false-green sources are closed, but a future opt-in that calls
thunk `0x25848e0` directly without a withhold entry would defeat the name-only gate —
the risk C03 P1 and verifier #471 already flag. **Severity: residual risk, not a
contradiction; keep the "any future guest-mutating opt-in must be added" rule.**

## 4. Recommended minimal, non-weakening updates to decision docs

All are **documentation-only** and belong to the lead (`docs/`, `PROGRESS.md`); the
analysis-file owners handle their own files. None changes the gate, the test or `src/`.

1. **Decision 013, phase-7 note (`:40-44`):** replace the phantom
   `SOBER_SEED_JNI_STRING_BRIDGE` with the live seed names, or state that the stub was
   removed and the withhold chain is the authoritative list. Add one sentence that any
   *future* guest-mutating knob must be added before its run can be trusted (aligns the
   note with C03 P1 / verifier #471).
2. **Decision 013, phase-8/9 note (`:53-64`):** change "No gate change was made" to
   "No weakening change was made; the ALT-path knob (and later guest-mutating knobs)
   were subsequently added to the withhold chain as a strengthening, dated 2026-11-20."
   Keep every other sentence, especially the rejection of `onAppBridgeNotification`.
3. **Decision 013, open-inconsistency note (`:46-51`):** keep it open, but add that
   `SOBER_ALT_CALLBACK_PATH` is now itself disqualifying, so the test's `:112-116`
   assertion is unsatisfiable for a second reason. Reaffirm that only an explicit lead
   ruling plus a `tests/` edit may close it; D6 makes neither.
4. **Decision 037 (`:24-33`):** accept the memo's §5 decision #4 — a one-paragraph
   amendment recording that the ALT thunk is **diagnostic-only**, that its honesty
   clause remains the rationale, and that a passing `app-shell.sh` under the current
   gate can only come from the real path. Do not delete the honesty requirement.
5. **Decision memo (status header and §2.3/§4):** add a short "superseded line numbers
   and counts" note (point to C09 and the live source), and mark the §2.3 control run
   as a pre-21:37 observation. Preserve the **B+C recommendation verbatim**; if Option C
   is enacted, the separate diagnostic token must use its own emission site and must
   never call `emit_marker("app shell rendered\n", …)` — otherwise it would recreate
   the exact weakness 013 rejected.
6. **C03 (`:171`, `:191-198`, P1/G1 line refs):** correct "Prints HONESTY NOTICE" to
   "now withheld before the notice (21:37 gate)", change "40+" to "the full, maintained
   withhold chain", and re-base the shifted line refs to symbolic anchors
   (`shell_cb`, `seeded_state`, `render_ok`, `emit_marker` call site). Leave the
   R/S/C checklist and the "no success is claimed" honesty section untouched.
7. **General (all four docs):** prefer symbolic anchors over line numbers and counts.
   The gate changed twice in the two minutes of this cross-check; any hardcode will be
   wrong again by the next integration.

## 5. Bottom line

- **No contradiction in the ruling itself.** Decision 013, the memo, C03 and the code
  all hold the same strict line: genuine `onAppShellReloadNeeded`/`gameLoadedCallback`
  plus the phase-3 render, with `onAppBridgeNotification` and synthetic state rejected.
- **All material drift is factual/staleness** (line numbers, knob counts, a removed seed
  name, a now-dead HONESTY NOTICE, and a "no gate change" statement overtaken by a
  strengthening). The most consequential is F4: do not re-run the memo's ALT control and
  report its old false green as current.
- **The one genuinely open policy item is F5** (test's ALT-thunk assertion vs 013),
  exactly where decision 013 already left it. Its minimal resolution is the memo's B+C
  plus a short 037 amendment, with the gate untouched.
- **No success is claimed.** Default path remains `callback=0 render=1`, marker
  withheld; only the non-qualifying `onAppBridgeNotification` has been observed.

## 6. Honesty limits

- This is a static cross-check of documents plus a read-only gate snapshot at
  21:37:42 UTC; no run, build, or test was executed by D6.
- The gate is being actively edited by other workers, so its exact line numbers and
  withhold count may differ from §1 by the time this is read. The *semantics* checked
  here (gate expression, withhold behaviour, callback names, marker policy) are the
  durable facts; re-verify anchors before using them.
- Findings F1/F4/F6 are about documentation accuracy, not about engine behaviour;
  F5 is an unresolved lead decision, not something D6 may enact.
