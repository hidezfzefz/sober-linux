# W6-13 — Marker gate contract audit after waves 5-6

Author: **worker-13** (Convio team `TEAM-C2YA-ZTU9-RMAV`; role assigned to task #547).
Scope: **analysis only**. This file is the only artifact written. No `src/`, CMake,
`tests/`, docs, vault note, gate line or byte of the engine was changed. Lead commits/pushes.
Date: 2026-11-21 (UTC).

> Team-membership caveat: `team_join` for role `worker-13` is rejected
> "The team is full (12 agents)"; the same happened to worker-7 and worker-8. I have no
> member_id and therefore cannot claim #547 or mark it done via `team_task`. I notified the
> lead with `team_send` (sender member id `mem_31838fd9eeb435e0577a`, as workers 7/8 did)
> and performed the assigned analysis anyway. Lead should mark #547 done with this report
> as evidence if no slot opens.

## 1. Executive conclusion

**The decision-013/038 marker contract is intact after waves 5-6.** The *decisive*
positive assertions of `tests/app-shell.sh` — the gate-owned notifier-provenance line,
`clean state: seeded_state=0`, the gate-owned render-provenance line, and the single
`app shell rendered` marker/marker-file — are **each emitted only from code paths that are
unreachable when any guest-state seed is active**. A seeded run cannot satisfy their
conjunction. The strict test remains RED (empirically: `bash tests/app-shell.sh` → exit 1,
fails at "no qualifying guest->Java app-shell callback observed"), the marker stays
**WITHHELD**, and no gate weakening is needed or proposed.

Two classes of *non-decisive* positive lines are seed-satisfiable (name-only callback line,
`[render] ... GUEST-TAGGED` evidence lines); they are necessary-only diagnostics and are
**not sufficient** for the marker because the gate-owned lines above remain absent under
seed. This distinction is the precise contract and is spelled out in §3.

Two wave-5 caveats were checked and **closed** in code (F1/F2/F3/F4), one **remains open as
a scope artifact** (F5, D09 single-TU). No new false-green was found in the wave-6 modules.

## 2. Evidence read / commands run

Read (all read-only):
- `tests/app-shell.sh` (211 lines), `tests/D09-withhold-audit.sh`.
- `src/linux/guest_app_shell.c` (gate at `:1743-2033`; `seeded_state` chain `:1761-1960`;
  marker emitter `:87-100`; `env_prefix_active` `:242-248`).
- `src/jni/provenance_gate.c` (hook `:427-518`, genuine predicate `:480-490`,
  `is_genuine` `:522-530`, emitter `:663-683`), `provenance_gate.h`.
- `src/jni/callback_provenance.c` (`:218-312`), `src/jni/frame_evidence.c`
  (`:544-546`, `:655-710`, `:1391-1396`, `:1786-1813`, `:1816-1871`).
- `docs/decisions/013-no-weak-app-shell-marker.md` (`:38-50` ruling 038).
- Vault `Home/Goal/EngineFacts/Decisions/State/Ownership/CA01` and `ActiveTasks.md`.
- Prior negative audit `analysis/task522-negative-ab-audit.md` (F1-F5).

Commands (no source changes):
1. `SOBER_D09_WITHHOLD_AUDIT=1 bash tests/D09-withhold-audit.sh` →
   `getenv SOBER knobs referenced : 170`, `withhold-chain knobs found : 164`,
   `GAP LIST: (none)`.
2. Cross-TU extraction (Python, read-only): all `SOBER_*_ENV` macro values and literal
   `getenv` names under `src/**/*.{c,h}` vs the D09 chain + prefix coverage. Result in §6.
3. `bash tests/app-shell.sh > /tmp/w613-appshell.out 2>&1` → `exit=1`; end of output:
   `== 3. strict gate evidence (DECISION 013 / 038) ==` /
   `FAIL: no qualifying guest->Java app-shell callback observed`. stdout contains only the
   preserved `guest init complete` / `first frame rendered`; no marker line. Stale-source
   check (`find ... -newer build/guest-init-host`) was empty, so no rebuild was triggered.
4. Bounded seeded diagnostic with `SOBER_FRAME_EGL_INIT_DRIVE=1 SOBER_FRAME_FLOG_SINK=1
   SOBER_FRAME_FLOG_ENABLE=1 SOBER_PUBLISH_DEFAULT=1 SOBER_PREINIT_BUS=1` was attempted:
   the guest faulted before the gate (harness backtrace, exit 139), so it is **inconclusive
   as a seeded A/B**; it emitted zero marker lines and left no marker file. This is recorded
   honestly, not claimed as proof of the seeded-withhold path (that proof is the wave-5 A/B
   record plus the static guards in §4).

## 3. Positive-assertion map in `tests/app-shell.sh`

"Seed-satisfiable?" means: can a run *with diagnostic guest seeds active* produce that
line? "Gate-owned" means the emitter itself is inside the `seeded_state == 0` branch.

| # | Assertion (test line) | Emitter | Gate-owned / seed-guarded? | Seed-satisfiable? | Decisive? |
|---|---|---|---|---|---|
| 1 | BuildID `204ad049…` + `ldd` clean (`:56-63`), engine SHA unchanged (`:109-110`) | readelf/ldd/sha256sum | n/a (identity, no mutation allowed) | No (no byte patches permitted) | Necessary only |
| 2 | `guest init complete` + init marker (`:105-106`) | `guest_dlopen_host.c:653+` | No | Yes | Necessary only |
| 3 | `first frame rendered` + frame marker (`:107-108`) | `guest_render.c:477` | No | Yes | Necessary only |
| 4 | No `ALT PATH` / `HONESTY NOTICE` / `marker withheld: diagnostic guest-state seed active` (`:145-151`) | negative checks | n/a | n/a (these *detect* seeds) | Gate condition |
| 5 | `guest->Java callback: (gameLoadedCallback\|onAppShellReloadNeeded)` (`:153-156`) | JNI bridge diagnostic | **No** — name-only (`guest_app_shell.c:1751-1756` explicitly "never as proof") | **Yes** (host re-drive / ALT can fire the name) | Necessary only |
| 6 | `guest call 0x649d422` (`:157-160`) | phase-3 render path | No | Yes | Necessary only |
| 7 | `[app-shell] gate provenance: … notifier-driven gameLoadedCallback … 0x27eaf6e … 0x27eafb3 … -> GENUINE`, and no `HARNESS-THUNK` (`:165-169`) | `sober_provenance_gate_emit_provenance()` (`provenance_gate.c:663-683`), called **only** at `guest_app_shell.c:1969` inside `else` of `if (seeded_state)` | **Yes** | **No** | **Decisive** |
| 8 | `[app-shell] clean state: seeded_state=0` (`:170-171`) | `guest_app_shell.c:1970-1971`, same `else` branch | **Yes** | **No** | **Decisive** |
| 9 | `[render] guest-tagged swap … N>=2, M>=1` + exactly one recurring RA (`:177-186`) and `[render] guest render verdict … -> GUEST-TAGGED` (`:187-188`) | `fe_maybe_emit_lines()` (`frame_evidence.c:1816-1871`) | No — evidence recorder, not gate-owned | **Yes** (seeded G8 A/B emits these; see State.md wave5) | Necessary only |
| 10 | `[app-shell] render provenance: guest-tagged frame … -> OK` (`:189-190`) | `guest_app_shell.c:1988-1994`, guarded by `!seeded_state && shell_cb && render_ok` | **Yes** | **No** | **Decisive** |
| 11 | exactly one stdout `app shell rendered`, rc=0, marker file (`:193-200`) | `emit_marker()` at `guest_app_shell.c:2021`, reachable only after `:1996-2013` (seeded → `shell_cb=0` → `return -4`) | **Yes** | **No** | **Decisive** |
| 12 | visible native window (`:202-208`) | harness/window log | No | Yes | Necessary only |

**Conjunction result:** rows 7, 8, 10, 11 cannot hold in a seeded run; rows 5, 9 can, but
without 7/8/10/11 (and with the withhold/forbidden-line checks at rows 4) the test exits 1.
Therefore **no decisive positive assertion is seed-satisfiable**. This is the contract
decision 013/038 requires, and it is now structurally enforced rather than incidental.

## 4. Why the decisive guards hold

1. **Environment scrub at the test boundary.** `app-shell.sh:70-75` unsets *every*
   `SOBER_*` variable except five benign ones (`SOBER_SHELL_MARKER`,
   `SOBER_WINDOW_HOLD_MS`, `SOBER_APP_SHELL_WAIT_MS`, `SOBER_JNI_VERBOSE`,
   `SOBER_APP_SHELL_DIAGNOSTIC`); then `:78-83` re-adds only phase enables
   (`SOBER_RENDER`, `SOBER_APP_SHELL`), marker paths and timing. Because the scrub is a
   blanket prefix sweep rather than a fixed denylist, *newly added* guest-mutating knobs
   cannot leak into the strict run. Non-`SOBER` env reads were enumerated: `JAVA_HOME`
   (`native_jni.c:947`, diagnostic JVM file-probe only), `DISPLAY`/`WAYLAND_DISPLAY`
   (render backend choice), `_` (log). None seeds guest state.
2. **Withhold chain in the harness itself** (`guest_app_shell.c:1761-1960`): 164 literal
   knobs plus six `env_prefix_active("SOBER_GAME_|STARTAPP_|SGV2_|PLATFORM_PARAMS_|EVIDENCE_|PROP_")`
   sweeps plus module `*_should_withhold()` verdicts. Any hit forces
   `shell_cb=0`, prints the withhold line, and prevents both provenance emitters and the
   marker (`:1961-1972`, `:1988-1994`, `:1996-2013`, `:2021`). The provenance gate is
   deliberately *not* in the chain because it is the fail-closed positive credential, not a
   seed.
3. **Callback credential** (`provenance_gate.c:427-518`): requires qualifying name
   (`gameLoadedCallback` only), frozen BuildID, exact cached class/method handles
   (`0x72a2f48`/`0x72a2fc0`), `placeId>0`, live JavaVM `0x7cdec78`, thunk RA `0x2584960`
   with notifier RA `0x27eafb3` above it, descriptor-invoke RA `0x27eaf32`, and node
   dispatch `0x27eaf04` entered from guest `.text`. `is_genuine()` (`:522-530`) also fails
   closed on any sticky harness-thunk taint; `onAppBridgeNotification` is disqualified.
   Thus the wave-5 blocker (host `publish_default` re-drive producing the RA pair) is
   rejected by the `guest_above_notifier` leg independently of `seeded_state`.
4. **Render verdict owner** (`frame_evidence.c:1803-1813`): `guest_render_ok()` needs
   phase3 ancestry, ≥2 guest-tagged swaps with one stable guest RA, ≥1 guest draw and
   ≥1 bracketed draw, and G8 `window=bound`; `fe_tainted()` (mixed synthetic/guest,
   `:544-546,650-651`) fails closed. The code comment at `:1347-1382` documents why the
   clean path cannot reach G8 ACCEPT: real ACQUIRE/DIMS RAs, the true H6 sink RA
   `0x232e080`, and store-tied `fromSurface` all require the three seed opt-ins
   (`EGL_INIT_DRIVE`, `FLOG_SINK`, `FLOG_ENABLE`), which are in the withhold chain. The
   current RED run shows `guest-tagged swaps=0` on the default path.
5. **Build/engine identity**: `provenance_gate.c:122-146` verifies the frozen BuildID in
   `PT_NOTE` before any credential; `app-shell.sh:57-59,109-110` re-checks BuildID and
   on-disk SHA. Authoritative DEX/engine provenance is unchanged (CA01, `EngineFacts.md`).

## 5. Wave-5 findings F1-F5 re-checked after waves 5-6

- **F1 closed:** `sober_frame_callbacks_should_withhold()` is now ORed at
  `guest_app_shell.c:1773`, together with `SOBER_FRAME_CALLBACKS` and
  `SOBER_FRAME_CALLBACKS_GUEST_ND` (`:1771-1772`).
- **F2 closed:** `SOBER_PREINIT_BUS` is literal in the chain (`:1769`), and
  `sober_publish_default_should_withhold()` is ORed (`:1943`).
- **F3 closed:** `SOBER_PROVENANCE_GATE_GUEST_BASE` is literal in the chain (`:1770`).
- **F4 closed:** the gate-owned `[app-shell] render provenance … -> OK` line now has a real
  emitter (`guest_app_shell.c:1988-1994`); the missing-engine SKIP now exits 1
  (`app-shell.sh:20`), so a CI dashboard cannot read SKIP as green.
- **F5 still open (scope artifact, not a false-green):** D09 audits one TU and literal
  `getenv("SOBER_…")` only (`D09-withhold-audit.sh:50,58`). My cross-TU sweep found knob
  names invisible to D09: `SOBER_PROVENANCE_GATE` and `SOBER_FRAME_EVIDENCE`
  (kill switches; setting them to 0 makes observers/credential inert → fail closed),
  `SOBER_FRAME_SYNTH_TAG` (kill switch; module is unwired — only an `#include` at
  `guest_app_shell.c:60`, no call sites), `SOBER_FRAME_THREAD` (covered by
  `sober_frame_thread_should_withhold()` in chain at `:1959`), and
  `SOBER_FAITHFUL_JNI_ARRAYS` (macro `getenv(SOBER_FAITHFUL_JNI_ARRAYS_ENV)` in chain at
  `:1874`). None is an unchained guest-mutating seed. D09's `GAP LIST: (none)` is therefore
  accurate for its scope but should still be described as scoped, not global.

## 6. Residual risks / recommendations (no edits made)

1. Keep D09 default-OFF and out of ctest; if the lead extends it to all TUs, resolve
   `SOBER_*_ENV` macro values and model the six `env_prefix_active` prefixes at the same
   time, otherwise it will report false positives/negatives (F5).
2. Keep `tests/app-shell.sh` RED and the marker WITHHELD. Do not relax C0-C10, the
   `guest_above_notifier` walk, or the render-provenance emitter. The decisive RED is honest:
   clean notifier-driven callback and clean G8 render are both structurally unreachable in
   this native harness (State.md wave-5 conclusion; `frame_evidence.c:1347-1382`).
3. If future waves wire `frame_synthetic_tag` or add a new default-ON guest-mutating module,
   add its activation knob to the `seeded_state` chain *before* wiring — the blanket test
   scrub protects the strict run, but the in-harness chain is what protects ad-hoc runs.
4. Treat the name-only callback line (row 5) and `[render] ... GUEST-TAGGED` lines (row 9)
   strictly as diagnostics; any future test refactor must keep the gate-owned lines
   (rows 7/8/10/11) as mandatory AND-conditions.

## 7. Bottom line

No byte patch, fake callback, host callback, synthetic observation or gate weakening was
used or proposed. Marker: **WITHHELD**. `tests/app-shell.sh`: **RED** (exit 1). Engine SHA
`3e498d64…` / BuildID `204ad049…` untouched. Gate contract after waves 5-6: **verified
fail-closed**; decisive positives are unreachable under seed. Task #547 analysis complete.
