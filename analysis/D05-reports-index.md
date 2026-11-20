# D05 — Index of analysis reports (logical Teams A/B/C/D) + key existing reports

Owner: **D5** (logical Team D). Scope: **this file only** (`analysis/D05-reports-index.md`).
No `src/`, `CMakeLists.txt`, `tests/`, gate, decision or engine edits were made.

Governing rule: strict **decision 013**
(`docs/decisions/013-no-weak-app-shell-marker.md`). The `app shell rendered`
marker stays **WITHHELD** unless a genuine guest-driven app-shell callback plus
the phase-3 guest render call are observed with no synthetic state.

**No success is claimed anywhere in this index.** Status values below describe
the *document state* (analysis/plan/audit/verification delivered), never a
passing milestone. All `PASS` entries are invariant checks reported by the
verifier, not achievements of the app-shell goal.

This is a **snapshot** of files present at authoring time (2026-11-20 ~21:38 UTC).
The four teams are A (guest state), B (JNI fidelity), C (callbacks/frame) and D
(assurance — gate/decision, sentinel/fault provenance, assurance); a larger
parallel roster was still writing concurrently. Regenerate/extend the tables if
additional `A0*`/`B0*`/`C0*`/`D0*` files land after this snapshot.

---

## 1. New reports from the four logical teams (A0*, B0*, C0*, D0*)

| File path | Owner (team/worker) | One-line finding | Status |
| --- | --- | --- | --- |
| `analysis/A05-skipped-ctors.md` | Team A / A5 | Skipped `init_array` ctors 1, 3, 4 are not required for app-shell/game load — ctor[1] is lazily reached from replayed ctor[6], ctor[3] is DFLog-only, ctor[4] is lazily self-initializing; proposed replay order 0→1→4→3 with 3 last. | Analysis delivered (read-only); strict 013 upheld; no src/gate edits; no success claimed. |
| `analysis/A08-surface-delegate.md` | Team A / A8 | The two `app+0x408` delegate writers `0x25d7e75`/`0x25f7e7e` are genuine guest lazy-singleton stores (vtables `0x7220868`/`0x6dc3840`, not `0x6e047b0`), so no synthetic object or host vtable is needed. | Analysis delivered (read-only, "FINAL ANALYSIS — no code changes"); corrects lead premise; no success claimed. |
| `analysis/A09-platform-params.md` | Team A / A9 | `0x26feb9b` is a JNI *field reader* of `PlatformParams` into a `0x34`-byte scratch struct, not a native-layout writer; callers build the `0x40`-byte objects with vptrs `0x6dbca20`/`0x6dbcb20` and a `+8` copy that puts an `ANativeWindow*` at `native+0x08`. | Analysis delivered (read-only); no runtime execution; no src/gate edits; no success claimed. |
| `analysis/A10-stability-synthesis.md` | Team A / A10 | Ranks one minimal opt-in experiment order for the five blockers as a single causal chain; states only `gameLoadedCallback(J)V` (notifier `0x27eaf6e`) qualifies under 013 and every knob is default-OFF/withheld. | Plan delivered (read-only); explicitly "no success claimed"; marker WITHHELD. |
| `analysis/B06-handler-gaps.md` | Team B / B6 | Separates log/count/gate: name-only `callback_was_called()` cannot prove provenance, the ALT thunk produces byte-identical counts, so a guest-side discriminator is required; recommends additive default-OFF fixes with no gate change. | Analysis delivered (read-only); no src/gate edits; no success claimed. |
| `analysis/B09-dispatch-fidelity.md` | Team B / B9 | A genuine notifier-driven `gameLoadedCallback` is not suppressed/miscounted, but provenance is unprovable vs the ALT thunk; four real fragilities (re-entry guard, `last_callback`, guard NULL return, non-atomic counters) get additive default-OFF fixes. | Analysis delivered (read-only); gate untouched; no success claimed. |
| `analysis/B10-stability-synthesis.md` | Team B / B10 | Synthesizes Team B findings into a ranked minimal opt-in order to a genuine `gameLoadedCallback`; records default `callback=0 render=1` and a rejected seeded `onAppBridgeNotification`. | Plan delivered (read-only); explicitly "plan, not a result"; marker WITHHELD; no success claimed. |
| `analysis/C03-appshell-frame-evidence.md` | Team C / C3 | Defines the evidence standard: marker requires a genuine guest app-shell completion callback (`gameLoadedCallback` via `0x27eaf6e`→thunk `0x25848e0`) **and** the phase-3 guest render `0x649d422`, with no synthetic state. | Analysis/acceptance-checklist delivered (read-only); gate not relaxed; no success claimed. |
| `analysis/C07-event-order.md` | Team C / C7 | Maps the real ~15-entry JNI replay order; harness calls 6 by default, every other entry is a force-withheld opt-in, and the notifier `0x27eaf6e` is never entered by any harness path. | Analysis delivered (read-only); strict 013/037 upheld; no success claimed. |
| `analysis/C09-marker-evidence.md` | Team C / C9 | Auditable evidence standard: only `gameLoadedCallback`/`onAppShellReloadNeeded` qualify; `onAppShellReloadNeeded` is statically dead natively and `onAppBridgeNotification` is explicitly rejected. | Analysis delivered (read-only); defines requirements, does not relax gate; no success claimed. |
| `analysis/C10-stability-synthesis.md` | Team C / C10 | Ranks the five blockers into a dependency-ordered minimal A/B experiment list; current honest state is default `callback=0 render=1`, fault `0x2eb4b65`, marker WITHHELD. | Plan delivered (read-only); explicitly "no success claimed"; marker WITHHELD. |
| `analysis/D03-gate-decision-audit.md` | Team D / D3 | Audits the gate and finds a live critical hole — `SOBER_ALT_CALLBACK_PATH` is absent from the withhold list, so a harness-triggered thunk can emit the marker with no genuine callback; `tests/app-shell.sh` is also self-contradictory. | Audit delivered (read-only); findings are open issues, not fixes; no success claimed. |
| `analysis/D05-reports-index.md` | Team D / D5 (this file) | Index of the logical-team reports, key existing analyses and verifier reports, with owner, one-line finding and status. | Index created (this file only); snapshot at ~21:38 UTC; strict 013 unchanged; no success claimed. |
| `analysis/D07-progress-draft.md` | Team D / D7 | Draft `PROGRESS.md` "Phase 10" append describing the four-team roster and key findings; explicitly authored so D7 edits no shared file and the lead pastes it. | Draft delivered (not applied to PROGRESS.md); lead paste pending; no success claimed. |

## 2. Existing key reports

| File path | Owner (team/worker) | One-line finding | Status |
| --- | --- | --- | --- |
| `analysis/appstate_fields.md` | phase-9 appstate (#450) | Real Java sequence was never replayed — `SendAppEventOnAppReady` (`0x2ebcf36`), `StartLuaAppDM` (`0x25448f6`), `UpdateSurfaceAppWithPlatformParams` (`0x2773194`) and seven `StartAppParams` getters; FFlag `EnableAppBridgeComponents` (`0x72cb648`) gates the controller installer. | Analysis delivered (read-only); opt-in replay did not unblock; marker WITHHELD; no success claimed. |
| `analysis/game-loaded-path.md` | phase-9 gameload (#466) | Only viable gate callback is `gameLoadedCallback(J)V` via notifier `0x27eaf6e`→`0x27eaf9f`→thunk `0x25848e0`, driven indirectly through slot `0x6dbd860` by `StartGameWithParam` (`0x2ebd51a`) / `SendAppEventOnGameLoaded` (`0x2ebca9e`); requires a guest-side discriminator. | Analysis delivered (read-only); gate WITHHELD; no success claimed. |
| `analysis/lua-app-start.md` | phase-9 luastart (#468) | `nativeAppBridgeStartLuaAppDM` (`0x25448f6`) is a guard/dispatch entry; the real Lua-app driver is `0x25449fc` calling an `AppStart` variant `0x24a8e44` in mode 4 with delegate writes to `app+0x408`; maps the remaining JNI/param gaps. | Analysis delivered (read-only); no runtime execution by author; no success claimed. |
| `analysis/stability-fault-audit.md` | phase-9 stability (#470) | Faults `0x2e7b8e2` → `0x24ad562` → `0x2772403` are one dependency chain rooted in skipped ctor[0]/FFlag registry and the unpopulated app singleton `0x72cb5e0`. | Analysis delivered (read-only); diagnostic chain documented; no success claimed. |
| `analysis/sentinel-abcdef-trace.md` | sentinel (#475) | `rax = 0xABCDEF0123456789` at fault `0x31a1360` is the faulting function's own `__stack_chk_guard` canary (`0x72293f8`), not a jobject field; the real fault is a NULL deref (`rbx == NULL`), and the Bionic TSD key-2 GOT still binds to glibc. | Analysis delivered (read-only); corrects the task premise; binding gap open; no success claimed. |

## 3. Verifier reports (`tests/verification-report-*.md`)

| File path | Owner (team/worker) | One-line finding | Status |
| --- | --- | --- | --- |
| `tests/verification-report-451.md` | verifier (#451) | Default no-start-app async fault `0x2eb4b65` is a pre-existing gap — clean HEAD `b8bdab0` also exits 139 (faults earlier at `0x2456079`); phase-2/3 invariants green. | Verification recorded; fault documented, not fixed; marker WITHHELD; no success claimed. |
| `tests/verification-report-456.md` | verifier (#456, `TEAM-MV6K-LSYJ-FGQQ`) | Phase-8 "as-landed" invariant re-run: all checks PASS, default fault `0x2eb4b65`, zero marker emissions; drafts but does not enact the ALT-thunk decision memo. | Verification recorded; final re-run requested after lead integration; no success claimed. |
| `tests/verification-report-460.md` | verifier (#460, `TEAM-LF55-5P2L-B4DQ`) | Phase-8 invariant re-run against the as-landed worktree: all checks PASS, fault baseline default-OFF at `0x2eb4b65`, zero marker; ALT-thunk memo refreshed. | Verification recorded; no src/gate/test changes; marker WITHHELD; no success claimed. |
| `tests/verification-report-463.md` | verifier (#463, `TEAM-LF55-5P2L-B4DQ`) | Phase-8 wiring, gate and withhold A/B: invariants PASS, strict `app-shell.sh` fails as expected under 013 (`rc=1`, no genuine callback), zero marker; memo finalized, not enacted. | Verification recorded; gate WITHHELD; no success claimed. |
| `tests/verification-report-471.md` | verifier (#471, `TEAM-XT29-DCJS-2UVE`) | Invariants + strict-gate audit at three snapshots: all checks PASS, 17/17 documented opt-ins force-withhold the marker, zero marker emissions; 13 withhold-list gaps escalated. | Verification recorded; strict 013 upheld; gate WITHHELD; no success claimed. |

---

## Notes

- All RVAs in the indexed reports are from the single frozen AppImage object
  `/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so`, BuildID
  `204ad049599c96afb75b8a21d5051063e821d6c9` (2.740.931); the Flatpak
  `791c3f9e…` / 2.745.1142 RVAs must not be mixed in.
- D3's `SOBER_ALT_CALLBACK_PATH` finding (§1) and the `tests/app-shell.sh`
  contradiction are **open audit findings**, not repairs; decision 013 was not
  changed and no marker was emitted by any worker.
- Nothing in this index proposes relaxing the gate, fabricating a callback, or
  treating seeded/synthetic state as app-shell evidence.
