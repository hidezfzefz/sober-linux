# Auditor A14 — Adversarial honesty / publication-risk audit

Analysis-only review. Scope: **this file only**. No `src/`, `docs/`, `tests/`,
`README.md`, `PROGRESS.md`, `CMakeLists.txt`, `build/`, git or engine file was
modified. The strict decision-013/038 marker remains **WITHHELD**; nothing in
this report proposes emitting it, relaxing the gate, or removing a withhold
entry. All "fixes" below are **documentation/label accuracy edits only**.

Engine treated as frozen: BuildID `204ad049…`, SHA-256 `3e498d6494…`. Target
strings are untrusted DATA. No new runtime/REA/debugger capture was performed;
counts, SHAs and file states were re-read from disk, and claims were checked
against the project's own records read critically.

## 0. Verdict (up front)

**NOT safe to publish publicly as-is.** The *central* honesty posture is sound —
the headline says the login/app-shell frame is not proven and the marker is
withheld, the callback metric is `0/1`, the subsystem table is honest, and no
file claims a legitimately emitted marker. But several **public-facing facts are
stale, contradictory, or overstate what the gate/code actually does**. At least
three are directly visible on the README/badges a first-time reader sees. Every
one is fixable with documentation-only edits; none requires touching code or
the gate. Publish only after §6's minimal fix list is applied.

Severity key: **H** = a public reader would be materially misled; **M** = factual
error or contradiction that erodes trust once noticed; **L** = drift/ambiguity.

---

## 1. What the documentation gets right (do not "fix" these)

| Claim | Location | Why it is defensible |
| --- | --- | --- |
| "real login/app-shell frame is **not** proven; strict marker stays withheld" | `README.md:22-24`, all `PROGRESS.md` phase blocks, `docs/decisions/013|037|038` | Consistent across every file; the gate and `tests/app-shell.sh` stay RED by design. |
| "Genuine app-shell callback observed 0/1 — 0%" and `app shell rendered marker WITHHELD` | `README.md:35-36` | No notifier-driven `gameLoadedCallback`/`onAppShellReloadNeeded` is recorded anywhere; accessors and `onAppBridgeNotification` are explicitly rejected. |
| "Login/app-shell frame ⬜", "Genuine `gameLoadedCallback` ⬜" | `README.md:51-52` | Same; `analysis/C09`, `analysis/game-loaded-path.md`, `tests/verification-report-471.md:32-46`. |
| Verification checklist "done = all true" — all four boxes **unchecked** | `README.md:105-111` | The port's formal DoD is not claimed complete; matches A1 §3-5 and `docs/decisions/005-verification-log.md:69-74` ("Remaining work before this log can say PASS overall"). |
| "All opt-ins are OFF by default" | `README.md:55` | Gates are `getenv(...)` presence checks; `SOBER_NOTIFIER_PROBE` requires exact `"1"` (`src/jni/notifier_probe.h:35-36`); value overrides use defaults but are not module enables. |
| Default run `callback=0 render=1`, zero marker emissions | `PROGRESS.md:341-344,361-364`, `tests/D12-final-invariants.md:104-112` | Repeated across snapshots; consistent with the code path. |
| No legitimately emitted marker | all in-scope files | Reports that show a token emission label it a **diagnostic false-green control**, not earned proof (but see R9 — that labelling is stale). |

**No document in scope claims the marker.** Do not weaken any of the above while
fixing the risks below.

---

## 2. Percent / badge / count defensibility

| Claim | Location | Re-check result | Verdict |
| --- | --- | --- | --- |
| Engine identity 100%; 3618/3618 ctors; 565390/565390 relocs | `README.md:27-29`, badges `badge-guest-init/relocs.svg` | Matches `docs/decisions/005-verification-log.md:47-59`, `PROGRESS.md:58-64`; not independently re-executed here. | **Defensible** (plumbing metrics, not product). |
| Native init+window+first frame 3/3 | `README.md:30` | Three distinct milestones, each with its own marker/test (`tests/guest-dlopen.sh`, `first-frame.sh`). | **Defensible** with A4's caveat that the counted swap is harness-invoked (R10). |
| JNIEnv table 231/231 | `README.md:31`, `badge-jni.svg` | Bridge logs `env_slots=231` in `tests/D01-invariants.md:73`, `D12:37`. | **Defensible.** |
| Cached jmethodIDs 49/49 | `README.md:32` | Two seeded ranges in `src/jni/native_jni.c:1376-1379`: `0x72a2f48..0x72a3058` = 35 slots, `0x72ca178..0x72ca1e0` = 14 slots → 49. | **Defensible** (read as "49 slots"). |
| "Verification (verify-native + ctest) 2/2 100%" / badge `verification` | `README.md:13,33` | `ctest -N` = exactly 2 tests (`sober_native_check`, `sober_native_version`); both pass. | **Count correct, label misleading** — `tests/verify-native.sh:24-28` prints steps 3-4 as "Manual"; 2/2 measures ctest, not the four-step DoD. Relabel "ctest 2/2". |
| Decision-013 gate enforced 1/1 | `README.md:34` | OR-chain and single gate site exist (`src/linux/guest_app_shell.c:1449-1546`). | **Defensible** only as "withhold chain present" — not as "provenance enforced" (R1). |
| "23 JNI modules" | `README.md:54` | `find src/jni -name '*.c'` = 23. | **Defensible** if "module" = `.c` file (two are probes). |
| "62 analysis reports" | `README.md:49,54` | 62 `.md` files in `analysis/` **excluding** `REVIEW_*`; the total including audit/review files is in the 70s and still growing as other auditors write. | **Conditionally defensible** — make the exclusion explicit or generate the number (R14). |
| "9 test scripts" | `README.md:54` | `find tests -name '*.sh'` = 9. | **Defensible.** |
| "**22** opt-in modules in the withhold chain" | `README.md:54-55` | Chain at `guest_app_shell.c:1449-1546` = **87 `getenv` terms (86 named knobs + one macro) + 5 prefix families + `sober_notifier_probe_should_withhold()`**. `analysis/D02-withhold-audit.md:8` already recorded 71 at 21:44Z. | **NOT DEFENSIBLE — stale by ~4x.** See R3. |
| Binary inventory SHA of `build/guest-init-host` = `733c8f2314eb…` | `README.md:62` | Actual `sha256sum` prefix = `1e7787f28f8763`. Other three SHAs match (`7647f6d6…`, `d131185d…`, `3e498d64…`). | **Factual error.** See R2. |
| Progress map: `notifier-hit probe` and `callback provenance` tiles GREEN | `tools/gen_progress.py:69-70`, `docs/assets/progress.svg` | Notifier probe is opt-in, default-OFF and **not** used by the gate (`notifier_probe.h:16-36`, `038:51-52`); callback provenance module is default-OFF diagnostic. | **Overstatement in a public visual.** See R4. |

---

## 3. Concrete risks (claim → file:line → why → fix)

### R1 — README implies the gate enforces notifier provenance (H)
- **Claim:** `README.md:48`: "Strict marker gate + decisions 013/038 · ✅ · name-only counting rejected; **notifier provenance required**".
- **Reality:** the coded `shell_cb` is still a name-only OR of
  `callback_was_called("onAppShellReloadNeeded") || callback_was_called("gameLoadedCallback")`
  (`guest_app_shell.c:1443-1444`). `docs/decisions/038…:51-52` says explicitly
  "Until native provenance instrumentation exists to establish leg (C), the
  coded gate cannot prove it". The notifier-hit discriminator (C3) is a
  *requirement*, not implemented.
- **Why risky:** a public reader concludes a green run would prove a real
  guest-driven app-shell callback. It would not; the gate relies on withhold
  state plus a name counter. This is the exact conflation decisions 013/038
  were written to prevent.
- **Fix:** change to "withhold chain + name counter enforced; **notifier
  provenance is required by policy but not yet coded**; gate therefore stays
  WITHHELD." Keep the ✅ only for "withhold chain present".

### R2 — Stale SHA in the public binary inventory (M)
- **Claim:** `README.md:62` lists `build/guest-init-host` SHA `733c8f2314eb…`.
- **Reality:** on-disk prefix is `1e7787f28f8763`.
- **Fix:** regenerate the inventory from `sha256sum build/*` (ideally extend
  `tools/gen_progress.py`, which currently generates only badges/map), or mark
  the table "snapshot at <date>" and refresh it. Do **not** interpret this as a
  guest-engine change: the engine SHA is correctly unchanged.

### R3 — Withhold-chain count is stale by ~4x (M)
- **Claim:** `README.md:54-55` "22 opt-in modules in the decision-013 withhold chain".
- **Reality:** 87 `getenv` terms (86 named knobs + one macro) plus five
  `env_prefix_active` families plus `sober_notifier_probe_should_withhold()` in
  `guest_app_shell.c:1449-1546`; D02 recorded 71 at its snapshot. Related stale
  counts: `analysis/C03-appshell-frame-evidence.md:191` "all 40+ env vars",
  `tests/verification-report-471.md:50-58` "17" in-chain / 13 gaps.
- **Fix:** replace the fixed number with a generated one (script the OR-chain
  regex as D02 §8 does), or state "the authoritative list is the live OR-chain
  in `guest_app_shell.c`" and drop the number. Never imply the count is complete
  — D02 still lists Tier-2/Tier-3 families outside it (R12).

### R4 — Public progress map shows opt-in diagnostics as DONE/green (H)
- **Claim:** `tools/gen_progress.py:69-70` assigns status `2` (green =
  "verified", per `docs/assets/progress.svg` legend) to `callback provenance`
  and `notifier-hit probe`.
- **Reality:** both are default-OFF diagnostics; the notifier probe installs a
  trampoline only with `SOBER_NOTIFIER_PROBE=1` (`notifier_probe.h:35-36`), is
  force-withheld, and contributes to the gate only a *withhold* signal
  (`guest_app_shell.c:1546`), never proof; `038:51-52` says native provenance
  instrumentation does not yet exist. Green here reads as
  "provenance verified", which is false.
- **Fix:** set both tiles to status `1` (orange = diagnostic/opt-in only) and
  relabel "notifier-hit probe (opt-in, not gated)" / "callback provenance
  (opt-in)". Regenerate `progress.svg`.

### R5 — README "verification" badge links to a stale report that shows a false-green (H)
- **Claim:** `README.md:13` `[![verification]]` → `tests/verification-report-471.md`.
- **Reality:** that report is a 20:03 UTC snapshot. Its §3a counts 17 in-chain
  knobs; §3b/§3c declare 13+ withhold gaps that the lead later closed; §2 quotes
  a test failure string `FAIL: no genuine guest->Java JNI callback observed`
  that the current `tests/app-shell.sh:118` no longer uses; §3a line 64 reports
  `ALT alone → marker emitted … HONESTY NOTICE`, which is no longer possible
  (D06 F4, `038:78-82`). §2/§3a also cite stale `guest_app_shell.c` line
  numbers (`:1235-1236`, `:1292`).
- **Why risky:** the only verification document a README reader clicks shows an
  ALT-thunk marker emission and an open false-green hole as if current. That is
  precisely "treats harness-triggered callbacks as genuine" in the reader's
  mind, even though the report labels it a control.
- **Fix:** link a current report (`tests/D12-final-invariants.md`), and add a
  dated "superseded by …" header to 463/471 (or update §3 counts and mark §3a's
  ALT control historical). Do not delete the historical data.

### R6 — Decision 013 contradicts itself and names a phantom knob (M)
- **Claim A:** `docs/decisions/013…:55-56` names `SOBER_SEED_JNI_STRING_BRIDGE`
  as an active force-withhold seed.
- **Reality:** the stub was removed; the name occurs nowhere in `src/` (only
  `analysis/D06…:87-93`, `D10…:194`, `tests/verification-report-451.md:102`
  where it is called *removed*). D06's recommended fix was never applied.
- **Claim B:** `docs/decisions/013…:60-65` says "**Open inconsistency, not
  resolved**: `tests/app-shell.sh` still requires `ALT PATH guest call
  0x25848e0` … neither [lead decision nor test change] has been made here."
- **Reality:** `docs/decisions/038…:54-63` (same decision's own "Lead ruling"
  at `:38-50`) says B+C adopted, the stale ALT assertion removed, and the test
  stays RED; current `tests/app-shell.sh:109-133` explicitly *rejects* ALT and
  requires notifier provenance. So decision 013's own text says both "closed"
  and "not resolved".
- **Fix:** replace the phantom knob with the live seed names (or say the chain
  is authoritative); mark the phase-7 "open inconsistency" note **superseded by
  decision 038, 2026-11-20** rather than deleting it. This is documentation-only
  and strengthens clarity without changing the gate.

### R7 — Decision 037's body still says a passing `app-shell.sh` "proves" a real callback (M)
- **Claim:** `docs/decisions/037…:38-47`: "A passing `tests/app-shell.sh` with
  `app shell rendered` **proves**: a genuine guest JNI thunk called into our
  bridge (real guest→Java callback)… It does not prove a login/app-shell frame."
- **Reality:** a top amendment (`037:1-13`) re-reads this subject to 038, but the
  body remains a stand-alone declarative "proves" about a **harness-triggered**
  thunk (`SOBER_ALT_CALLBACK_PATH` calls `0x25848e0` directly, skipping
  `AppStart`). A reader who stops at §38 sees a harness-triggered callback
  described as proof of something.
- **Fix:** inline the qualifier — "would prove only that a harness-triggered
  guest thunk entered the bridge; under 038 this run force-withholds the marker
  and is never proof." Keep the amendment; do not remove the honesty clause.

### R8 — "Not enacted" is now false: B+C *was* enacted (M)
- **Claim:** `analysis/decision-memo-alt-thunk-vs-013.md:1-9` "FINAL
  recommendation — **NOT enacted**"; `PROGRESS.md:226-228` "(recommendation:
  B+C, **not enacted**)".
- **Reality:** `docs/decisions/038…:56` "Options **B + C** of the earlier memo
  are **adopted**"; `tests/app-shell.sh:105-133` implements them. The memo also
  has no supersession header, so §2.3's "ALT still emits that marker" is stale.
- **Fix:** add a dated supersession header to the memo; change `PROGRESS.md:228`
  to "recommendation B+C adopted by decision 038". This *corrects an
  understatement*; it must not be read as a green light for the ALT knob.

### R9 — Stale "HONESTY NOTICE printed / marker emitted" control descriptions (M)
- **Claims:** `analysis/C03-appshell-frame-evidence.md:171` (ALT thunk "Prints
  `HONESTY NOTICE`"); `tests/verification-report-463.md:45` and
  `tests/verification-report-471.md:64` ("ALT alone → emitted …
  HONESTY NOTICE — the false-green the gate must catch").
- **Reality:** `SOBER_ALT_CALLBACK_PATH` is itself the first `seeded_state`
  term (`guest_app_shell.c:1449`) and `HONESTY NOTICE` is unreachable dead code
  (D06 F4; `038:78-82` calls this a strengthening). The historical A/B remains
  valid for its timestamp but is not current behaviour.
- **Fix:** add "pre-038 snapshot" to those rows/tables; keep the data as the
  record of *why* the knob was withheld.

### R10 — README uses a harness-triggered guest call as evidence for the first-frame ✅ (M)
- **Claim:** `README.md:46` "Native X11/EGL window + first frame ✅ · guest swap
  `0x649d422` observed".
- **Reality:** the swap wrapper is called directly by the host with a synthetic
  `0x48`-byte context; the GOT counter has no return-address tagging, so it
  cannot distinguish a harness call from a guest frame loop (`REVIEW_A4 §1.3`,
  §2, §3 item 1). The native frame itself *is* proven — the evidence string is
  what overstates.
- **Fix:** "native `glClear` frame + X11 window verified; guest swap wrapper
  `0x649d422` invoked once by the harness (no guest frame loop proven)". Keep ✅
  for the native frame only.

### R11 — "JNI bridge, callbacks … ✅" unqualified (L)
- `README.md:47` uses the bare word "callbacks" in a ✅ row. A reader may map it
  to the app-shell callbacks that are explicitly ⬜ two rows later.
- **Fix:** "JNI bridge, field/array slots and diagnostics ✅; app-shell
  callbacks ⬜ (see rows below)".

### R12 — Residual withhold gaps are not disclosed on the public surface (M)
- D02 (`analysis/D02-withhold-audit.md:21-25,215-233`) closes all Tier-1 gaps but
  leaves **47 Tier-2 modifiers + 10 Tier-3 names + the `SOBER_PROP_*` family**
  outside the chain at its snapshot. Spot-check of the current chain confirms
  `SOBER_SCREEN_WIDTH_DP/HEIGHT_DP`, `SOBER_SCREEN_SIZE`, `SOBER_NAV_HIDDEN`,
  `SOBER_WINDOW_WIDTH/HEIGHT` and `SOBER_PROP_*` are still absent. Today this is
  harmless (every default run emits zero markers), but combined with R1 a
  reader is told the gate is complete.
- **Fix:** add one README sentence: "The chain covers all guest-mutating
  modules; D02 lists residual guest-visible config knobs not yet withhold-listed.
  Marker is withheld on every current run." Do **not** claim these gaps prove
  anything today.

### R13 — Git history subject line can be over-read (L)
- `git log`: `afed56d Phase 1-10: native Roblox Sober port, JNI fidelity, strict
  gate, status dashboard`. "native … port" without a "partial/blocked"
  qualifier could be read as goal completion; commit bodies are empty. Other
  subjects are appropriately hedged ("milestone blocked", "trial bypass",
  "honest blocked result").
- **Fix:** historic commits cannot be rewritten safely; add one line to
  `README.md:22` already exists — keep it prominent ("goal not achieved"). No
  action needed beyond keeping the honest headline at the top.

### R14 — Hand-maintained counts/line numbers drift continuously (L)
- Examples: `analysis/D06…:76-85,126-129` (F1/F6), `C03:191` "40+" knobs,
  `C09:192,281` `:1409` / "exactly one marker emitted", `471:41-46` stale line
  numbers, `D10 §0` quoting an older D02 snapshot (78+10 gaps vs D02's later
  0 Tier-1 / 47 Tier-2).
- **Fix:** prefer symbolic anchors (`shell_cb`, `seeded_state`, `render_ok`,
  `emit_marker` site) as D06 §4 recommends; generate counts in
  `tools/gen_progress.py`. Label every snapshot with a UTC timestamp.

---

## 4. Marker-claim check (explicit)

- No file in `README.md`, `PROGRESS.md`, `docs/`, `tests/` or `analysis/`
  claims `app shell rendered` was legitimately emitted.
- The only token emissions described are ALT-thunk diagnostic controls
  (`verification-report-463.md:45`, `471.md:64`) and they are labelled
  false-green — but that labelling describes **pre-038** code (R9).
- `tests/app-shell.sh:129-133` requires a notifier-driven provenance line that
  does not exist yet, so the test is correctly RED; `:150`'s
  `echo "app shell rendered"` is unreachable in the current build.
- `emit_marker("app shell rendered\n", …)` has a single, withhold-gated call
  site (`guest_app_shell.c:1581`, definition at `:42`); every opt-in term forces
  `shell_cb=0` before it.
- **Conclusion:** the marker is correctly WITHHELD. This audit proposes no
  change to that state and never suggests emitting the marker.

---

## 5. Publication safety, itemised

| Public surface | Safe? | Blocking items |
| --- | --- | --- |
| `README.md` headline / legend / ⬜ rows | **Yes** | none |
| `README.md` status block & subsystem table | **No** | R1, R2, R4, R10, R11, R3 |
| README badges + `docs/assets/progress.svg` | **No** | R1 (gate badge), R4 (green tiles), R5 (verification link) |
| `docs/decisions/013`, `037` | **No** | R6, R7 |
| `docs/decisions/038` | **Yes** | none found; keep as the authority |
| `PROGRESS.md` | **Conditionally** | R8, R12, and its phase-snapshot counts (R14) |
| `analysis/` reports | **Conditionally** | historical snapshots are honest if labelled; R6/R8/R9/R14 need supersession notes |
| git history | **Conditionally** | R13 (low) |

---

## 6. Minimal fix list (documentation-only; gate untouched)

Do these before publishing:

1. `README.md:48` — replace "notifier provenance required" with
   "required by policy, not yet coded; gate stays WITHHELD" (**R1**).
2. `README.md:62` — correct/regenerate `guest-init-host` SHA (**R2**).
3. `README.md:54-55` — drop or generate the "22 opt-in modules" count; point to
   the live OR-chain (**R3**).
4. `tools/gen_progress.py:69-70` — set `callback provenance` and
   `notifier-hit probe` to orange/diagnostic and relabel; regenerate
   `progress.svg` (**R4**).
5. `README.md:13` — link `tests/D12-final-invariants.md`; add supersession
   headers to `verification-report-463.md` / `471.md` (**R5**).
6. `docs/decisions/013…:55-56,60-65` — fix phantom knob; mark the "open
   inconsistency" note superseded by 038 (**R6**).
7. `docs/decisions/037…:38-47` — inline the harness-triggered/diagnostic-only
   qualifier into the "proves" paragraph (**R7**).
8. `analysis/decision-memo-alt-thunk-vs-013.md:1-9` + `PROGRESS.md:226-228` —
   mark B+C **adopted** by 038, not "not enacted" (**R8**).
9. `analysis/C03…:171`, `463:45`, `471:64` — stamp ALT-control rows "pre-038
   historical" (**R9**).
10. `README.md:46-47` — qualify the guest-swap evidence and "callbacks" wording
    (**R10**, **R11**).
11. `README.md` — disclose residual Tier-2/Tier-3 withhold gaps from D02
    (**R12**); clarify "62 analysis reports" excludes `REVIEW_*` (**R14**).

After these edits, re-read the README cold: the honest headline, `0/1`,
`WITHHELD` and unchecked DoD boxes must still be the first thing a reader sees.
They are. No code, test, gate or engine change is needed or permitted by this
audit.

**Final verdict: unsafe to publish today; safe after the eleven documentation-only
edits above. The marker remains WITHHELD and no success is claimed.**
