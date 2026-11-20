# REVIEW A10 — Documentation quality & evidence-traceability audit

Auditor: **A10** (full-project review). Scope: **this file only** — `analysis/REVIEW_A10_docs.md`.
Analysis-only: no `src/`, `CMakeLists.txt`, `tests/`, `docs/`, `README.md`, `PROGRESS.md`,
gate or engine file was edited. Strict marker **WITHHELD**: nothing here asserts or
promotes `app shell rendered`; decisions 013/038 stay strict.

Sources reviewed: `README.md`, `PROGRESS.md`, all `docs/decisions/*.md`,
`docs/interfaces/{abi-map.md,libroblox-*.txt}`, `analysis/rea/{report.md,findings.json,bionic-versions.json}`,
and **all** `analysis/**/*.md` reports (69 at audit time, incl. the other auditors' `REVIEW_A*`
files that landed concurrently). Cross-checked claims against on-disk artifacts and
`tests/verification-report-*.md`. Target-derived strings are untrusted DATA.

Frozen engine used for every RVA check: `/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so`,
BuildID `204ad049599c96afb75b8a21d5051063e821d6c9`, SHA-256
`3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335` (AppImage 2.740.931).
The Flatpak `791c3f9e…` / 2.745.1142 build is a different object; its RVAs must not be mixed.

---

## 0. Bottom line

- **Coverage: strong.** Every subsystem has at least one analysis report; evidence IDs,
  SHA/BuildID identity declarations, frozen-build caveats and explicit "no success claimed"
  statements are the norm across the 69 reports.
- **Freshness and internal consistency: weak.** Several concrete, checkable doc defects
  exist (stale SHA/counts, un-remediated audit findings, contradictory status paragraphs,
  a wrong-build evidence citation in the canonical ABI contract).
- **README status dashboard: mostly honest in substance, but stale in its numbers.** The
  decisive lines are correct (`Genuine app-shell callback observed 0/1 — 0%`,
  `app shell rendered marker WITHHELD`, subsystem ⬜ rows, unchecked "done" checklist,
  "Honest headline" paragraph). However three dashboard figures are factually stale or
  unverifiable (DOC-01, DOC-02) and two 100% lines need qualification (DOC-10, DOC-15).
- **Domain % (documentation completeness): ≈ 67 % (67–68 %).** Weighted breakdown in §6.

The gate integrity of the *documentation* is intact — no doc in scope claims the app-shell
marker was emitted. The defects are accuracy/traceability defects, not success claims.

---

## 1. Documentation defects (ranked)

| ID | Sev | Location | Defect | Evidence / recommended fix |
| --- | --- | --- | --- | --- |
| **DOC-01** | High | `README.md:62` | Binary inventory SHA for `build/guest-init-host` is **`733c8f2314eb…`**, but the actual file hashes to **`1e7787f28f87…`**. | On-disk `sha256sum build/guest-init-host` = `1e7787f28f87…`. Binary mtime 23:04:44Z, README mtime 23:08:01Z — README was written *after* the rebuild yet kept the old hash. Regenerate the inventory (the other three SHAs — sober-native `7647f6d60c52…`, libc.so `d131185d781f…`, engine `3e498d649421…` — match). |
| **DOC-02** | High | `README.md:49,54-55` | Two dashboard counts are stale/unverifiable: **"62 analysis reports"** and **"22 opt-in modules in the withhold chain."** | On disk: 54 top-level `analysis/*.md`, 69 including subdirs/concurrent REVIEW files; no count equals 62 and no manifest defines the metric. "22" is badly obsolete: `analysis/D02-withhold-audit.md` (final) says **71 knob names** in the chain with **57 named withhold-worthy knobs still outside**; `PROGRESS.md:299-301` says 17/17 then +14 more; `D10` says 37→39 then wider gaps. Replace with a "see D02 for live count" pointer, not a hard number. The linked verification badge (`verification-report-471.md`) also carries the superseded "17/17" figure. |
| **DOC-03** | High | `docs/interfaces/abi-map.md:46-53` (canonical contract) | The "Priority Android NDK/Bionic import groups (worker-2, Ghidra)" section cites Ghidra evidence `ev_e87289a…` with **SHA `8afce3e695d0…`** — the **Flatpak** engine — and calls these "the evidence-backed priority subset," while the same file declares target BuildID `204ad049…` and warns "never mix RVAs" (`:89-90`). No caveat that this evidence is wrong-build. | `analysis/ghidra/interface-map.md:3-9` *does* carry the critical caveat ("symbol categories transfer; addresses do not; do not reuse these RVAs"). That caveat was dropped when the evidence was promoted into the canonical map. Add it verbatim to abi-map §46-53; keep only symbol *names/categories*, never the addresses. |
| **DOC-04** | High | `docs/decisions/013…:56,75`; `PROGRESS.md:384`; `decision-memo-alt-thunk-vs-013.md:25-31,95-97,184-185`; `C03…:171,191` | `analysis/D06-decision-crosscheck.md` findings **F1–F4 were never remediated** in the documents D6 said the lead owns. Still present: phantom knob `SOBER_SEED_JNI_STRING_BRIDGE` (013:56); "No gate change was made" (013:75, PROGRESS:384) though the chain gained ALT/START_APP; stale line numbers and "§2.3 shows the ALT path still emits that marker" (memo:184); "Prints `HONESTY NOTICE`" (C03:171) and "all 40+ env vars" (C03:191). | Decision 038 was appended as a *header amendment* to 013/037 but the stale body text was left in place, creating internal contradictions. Apply D6 §4 items 1–6 (documentation-only); prefer symbolic anchors over line numbers. |
| **DOC-05** | Medium | `docs/decisions/005-verification-log.md:62` | "sysconf returning **1000** instead of 4096" — a third value supported by nothing. | Decision 009:19-20 says buggy `sysconf()` returns **0**; `PROGRESS.md:62` says the fix returns **4096**. No doc/source supports 1000. Correct to "returns 4096 instead of 0". |
| **DOC-06** | Medium | `docs/decisions/005-verification-log.md:41-45,69-74` | "…because the Android/Bionic bootstrap … is not patched in yet. **That is the next blocker**" is left immediately above a "Phase 2 final verification — MILESTONE COMPLETE" section (`:47`) reporting all 3618 ctors and 565390 relocations. The §69 "Remaining work before this log can say PASS overall" list is never closed. | Mark lines 41-45 as a historical pre-Phase-2 snapshot (or delete), and close/replace §69 with the post-Phase-2 status. |
| **DOC-07** | Medium | `PROGRESS.md:1,282-308,346-364,366-408` | **Duplicate, out-of-order and self-contradictory Phase 9 sections.** One "Phase 9 … parallel fidelity work" (`:282`) ends with a completed final verification; a second "Phase 9 — current team … (parallel track)" (`:366`) appears *after* Phase 10 and Phase 10 wave 2 and still lists "Next steps (active tasks)" #465–#472 as pending. Title date `:1` "2026-11-18" is stale for Phase-10 content. | Merge the two Phase 9 records; remove the "active tasks" framing (they were completed and integrated); update the snapshot date. |
| **DOC-08** | Medium | `analysis/D10-assurance-synthesis.md:43` (§0 input table) | D10's summary of D02 is itself stale: "**163** `SOBER_*` literals", "**47** names in chain at its **21:41:16Z** snapshot", "**78 confirmed Tier-1/2 gaps**". The final D02 file (mtime 21:46:12Z) says **168** literals, **71** knob names at **21:44:21Z**, and **47 named withhold-worthy knobs outside** (Tier-2) + 10 Tier-3 + family. | D10 synthesizes an intermediate D02 snapshot, so its headline gap arithmetic disagrees with the document it cites. Re-point to D02 final numbers. Also `D10:23` cites "D6 §4.7", but D6 §4 has items 1–7 and no §4.7 subsection. |
| **DOC-09** | Medium | `analysis/A10-stability-synthesis.md:36,51-57` vs `analysis/A04-tsd-key-audit.md:16-17,238-239` | Contradiction on a central blocker: A10 (21:37) says guest GOT `0x72298b0` **binds glibc**, not the Bionic stub, and proposes a *new* knob `SOBER_BIONIC_TSD_GOT` (Rank 1/1a). A04 (21:50) shows the two-slot rebind **already exists**, is mechanically correct, and cleared the fault (`tsdgot.log`; slots listed "yes (already)"). | No supersession note. Add "superseded by A04 — rebind present and verified" to the A10 header/Rank-1 1a. Note: `README.md:44`'s ✅ TSD row agrees with A04, so the README is not at fault here. |
| **DOC-10** | Medium | `README.md:28` | "Guest constructors replayed **3618/3618 — 100%**" is unqualified. `abi-map.md:94` documents default replay ranges `[2,3)` + `[5,3618)`, i.e. `init_array[0,1,3,4]` skipped; `A05…:22` confirms indices 0,1,3,4 skipped and that **ctor[0]** (`0x2e62390`) is a genuine required initializer replayed only via opt-in `SOBER_REPLAY_FFLAG_CTOR`. | The line describes "entries processed by the loader", not "all guest constructors ran by default". Add a footnote: "default replay skips init_array[0,1,3,4]; ctor[0] replay is opt-in." |
| **DOC-11** | Low | `analysis/D05-reports-index.md:16-20` | The reports index is a ~21:38 UTC snapshot listing only 19 analysis reports. **50 of 69** current `analysis/**/*.md` files are absent, including all `REVIEW_A*` audits and A04/D02/D06/D10/D11. Its own instruction to "regenerate/extend … if additional files land" was never executed. | Regenerate the index or label it clearly as a frozen historical snapshot with a pointer to a live manifest. |
| **DOC-12** | Low | `analysis/D02-withhold-audit.md`, `analysis/D06-decision-crosscheck.md` | These two audits enumerate guest RVAs (`0x23331ae`, `0x2e62390`, `0x27eaf6e`, `0x25848e0`, …) with **no BuildID / frozen-target declaration**, unlike the other 67 reports. | Under the project's own rule ("all RVAs from the single frozen build") add a one-line provenance header to each. |
| **DOC-13** | Low | `docs/decisions/` | Decision-number gaps **015** (referenced only in `014…:33` "Follow-up patch (decision 015)"), **016**, **034**, **036** have no file. A1 already flagged these; no index explains them. | Add "superseded / no decision recorded" stub files or a numbering note in `docs/decisions/README`. |
| **DOC-14** | Low | `analysis/D11-gate-ruling-draft.md:1-6`, `analysis/D07-progress-draft.md:1-3` | D11 still reads "Draft final lead ruling … not an enactment" though decision **038 accepted its ruling**; D07 still says "in progress" while `PROGRESS.md` records final Phase-10 state. | No harm (both self-label as drafts), but add "superseded by decision 038 / applied to PROGRESS" pointers to prevent reuse. |
| **DOC-15** | Medium (honesty nuance) | `README.md:33,105-111`; `docs/assets/badge-verification.svg` | "Verification (verify-native + ctest) **2/2 — 100%**" sits directly above "Verification (done = all true)" with **0 of 4 boxes checked**. The label is narrowly scoped, but a skimmer reading eight 100% lines can infer overall verification complete. The "Honest headline" (`:22-24`) mitigates but does not reconcile the two. | Footnote the metric: "2/2 = the two diagnostic scripts only; the formal done checklist at §Verification remains 0/4." |
| **DOC-16** | Medium | `PROGRESS.md:21`; `docs/interfaces/abi-map.md:80` | Claim "**63 REA evidence IDs**" is not traceable. Across the two shipped REA deliverables (`findings.json`, `report.md`) the union of unique `ev_…` IDs is **38**; no manifest enumerating 63 exists in the repo. | Either ship the missing evidence-ID manifest or restate as "evidence IDs cited in `analysis/rea/report.md`" (≈35–38) so the number can be checked. |

Severity key: High = wrong/stale fact in a canonical or dashboard document, or a rule violation
in the interface contract; Medium = contradictory or un-remediated status statement; Low =
completeness/hygiene issue.

---

## 2. RVA / frozen-BuildID traceability audit

| Check | Result |
| --- | --- |
| Reports declaring the frozen engine identity (full SHA + BuildID, or accepted short `204ad049…`) | **67 / 69** analysis reports. RVA-heavy reports (A05, A08, C05, dispatch-map, handle-origin, trace-report, etc.) generally give full SHA+BuildID plus REA `ev_…` IDs. |
| Reports using guest RVAs with **no** build provenance | 2: D02, D06 (DOC-12). |
| Explicit "never mix Flatpak `791c3f9e…`" caveat present | ~15 reports (REVIEW_A4, D10, D11, C03/C04/C09, A07/A10, bootstrap-map, dispatch-map, handle-origin, trace-report, render-map, jni-map, rea/report, D05). Good discipline. |
| Wrong-build evidence promoted into a canonical doc | **1**: `docs/interfaces/abi-map.md` Ghidra section (DOC-03). `analysis/ghidra/interface-map.md` is itself correctly caveated as Flatpak-only — the defect is the *reuse*, not that file. |
| Evidence-count claims traceable to a shipped manifest | "411 versioned Bionic symbols" ✅ (`bionic-versions.json` → `versioned_undefined_count: 411`, `symbols` length 411). "559 exports / 573 imports" ✅ (`wc -l` = 559 / 573). "63 REA evidence IDs" ❌ (DOC-16). |
| "Do not mix RVAs" rule actually honoured inside report bodies | Yes — no AppImage report was found using a `791c3f9e`-build address; every Flatpak mention is a warning or an explicitly-flagged source (ghidra map, rea/report §1.1). |

Conclusion: BuildID/RVA discipline is **good but not uniform** — the one canonical-contract
lapse (DOC-03) and the two provenance-less audits (DOC-12) are the exceptions that matter.

---

## 3. Claims vs. evidence — spot checks

| Claim | Verified? |
| --- | --- |
| README `guest-init-host` SHA `733c8f2314eb…` | **No** — on-disk `1e7787f28f87…` (DOC-01). |
| README "62 analysis reports" / "22 withhold knobs" | **No / stale** (DOC-02). |
| README sober-native SHA `7647f6d60c52…`, libc.so `d131185d781f…`, engine `3e498d649421…` | **Yes** — all match on-disk artifacts. |
| README "23 JNI modules", "9 test scripts" | **Yes** — `src/jni/*.c` = 23; `tests/*.sh|py` = 9. |
| README badge links resolve (9 SVGs + generator `tools/gen_progress.py`) | **Yes** — all present; badge text matches the README figures. |
| PROGRESS "stale ALT assertion in `tests/app-shell.sh` replaced with strict absence checks + notifier provenance" (`:354-355`) | **Yes** — `app-shell.sh:109-131` now checks absence of ALT/HONESTY lines and requires notifier-driven provenance; mtime 21:53 > decision 038. |
| PROGRESS "all 3618 constructors driven" | **Qualified** — see DOC-10 (default skip set [0,1,3,4]; ctor[0] opt-in). |
| 005 "sysconf 1000 instead of 4096" | **No** — contradicts 009 and PROGRESS (DOC-05). |
| 013 "No gate change was made" | **No** — withhold chain was strengthened (DOC-04 / D06-F3). |
| 038 "decision 013/037 amended" | **Yes in form** (header amendments exist) but stale body text remains (DOC-04). |

---

## 4. Stale / contradictory statement clusters

1. **Decision-013/037 / memo / C03 cluster** (DOC-04) — the largest un-remediated cluster;
   D6 diagnosed it, 038 appended a ruling, but the empirical body text was not updated.
2. **005 verification-log time capsule** (DOC-05, DOC-06) — a pre-Phase-2 "next blocker"
   and a page-size typo coexist with the Phase-2-complete section in the same file.
3. **D02/D10/A04/A10 snapshot drift** (DOC-08, DOC-09) — three analyses summarize or
   predate a moving target; none carries a "superseded by …" pointer.
4. **PROGRESS Phase 9 duplication** (DOC-07) — two "current/active" status records for the
   same team, one completed and one still listing next steps, separated by Phase 10 sections.

Common root cause: **hardcoded numbers, line numbers and snapshots in documents that describe
a concurrently-edited gate and a growing opt-in/withhold set.** D6 §4 item 7 ("prefer symbolic
anchors") is the right fix and was not adopted.

---

## 5. README status-dashboard honesty verdict

**Substantively honest, numerically stale.**

Honest elements (keep): the "Honest headline" (`:22-24`) explicitly says the real login/app-shell
frame is not proven and the marker withheld; the dashboard's two goal lines show
`0/1 — 0%` and `WITHHELD` (`:35-36`); the subsystem table marks `gameLoadedCallback` and
login/app-shell frame ⬜ and app state ⚠️ (`:50-52`); the legend, the decision-013/038 links,
and the entirely unchecked "done = all true" checklist (`:105-111`) reinforce this. The
progress SVG is correctly colour-coded (gameLoadedCallback / login frame / marker emitted =
red; app-state modules = orange; plumbing = green).

Misleading/stale elements (fix): DOC-01 (binary SHA), DOC-02 (report/withhold counts), DOC-10
(unqualified 3618/3618), DOC-15 (2/2 "Verification 100%" next to 0/4 done boxes), and the
badge chain to `verification-report-471.md` whose "17/17" figure is superseded by D02/D10.

**Verdict:** the dashboard does not claim success and does not weaken decision 013 — it is
honest about the *decisive* claim. Its credibility is reduced by specific stale numbers, not
by a false success narrative. Fixing DOC-01/02/10/15 would make it fully honest.

---

## 6. Domain % — documentation completeness (weighted)

| Sub-domain | Weight | Score | Basis |
| --- | --- | --- | --- |
| Scope coverage (every subsystem has a report; interfaces + decisions present) | 20 % | ~90 % | 69 analysis reports, 35 decision files, full export/import lists; minor decision-number gaps (DOC-13). |
| RVA / BuildID evidence traceability | 20 % | ~75 % | 67/69 declare frozen build; good Flatpak caveats; one canonical wrong-build citation (DOC-03) + 2 provenance-less audits (DOC-12) + unverifiable "63 IDs" (DOC-16). |
| Claim ↔ evidence accuracy (counts, SHAs, numeric facts) | 25 % | ~55 % | DOC-01, DOC-02, DOC-05, DOC-08, DOC-16 are concrete checkable mismatches. |
| Internal consistency / no stale contradictions | 20 % | ~50 % | D06 F1–F4 un-remediated, duplicate Phase 9, A10-vs-A04, 005 time capsule, draft/superseded markers. |
| README dashboard honesty | 15 % | ~70 % | Decisive lines correct and marker WITHHELD; but four stale/misleading figures (DOC-01/02/10/15). |

Weighted total = 0.20·90 + 0.20·75 + 0.25·55 + 0.20·50 + 0.15·70 = 18 + 15 + 13.75 + 10 + 10.5 = **≈ 67 %**.

**Domain % (documentation completeness): ≈ 67–68 %.** Read as: *the corpus is present and
largely evidence-backed, but a material minority of its status figures and cross-document
references are stale or contradictory.* Gate honesty itself remains at 100 % (no success
claim anywhere in scope).

---

## 7. Honesty limits

- Static document read + on-disk artifact checks (`sha256sum`, `wc -l`, `stat`, `grep`) only.
  No build, test, REA session, debugger run or engine modification was performed for this
  audit. "Verified" above means a claim matched an on-disk artifact or another cited document.
- The repository was being written concurrently by other review subagents; report counts
  (54 top-level / 69 total at audit time) and gate line numbers will drift. Findings are
  anchored to file paths and quoted text, not to a frozen snapshot beyond the stated times.
- Severity reflects documentation risk only. Every defect listed is fixable without touching
  the gate; none of them, by itself, changes the fact that the marker stays **WITHHELD**
  under decisions 013/038. No success is claimed in this report.
