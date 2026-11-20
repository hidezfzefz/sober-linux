# REVIEW A9 — audit of `tests/*.sh` and `tests/*.md` verification assets

Auditor: A9 (full-project review). Date: 2026-11-20. Analysis-only; **no shared file was
edited**. My sole in-scope output is this file.

**Strict marker: WITHHELD.** Under DECISION 013 the app-shell completion marker
(`app shell rendered` / `SOBER_SHELL_MARKER`) is withheld on every default path.
Nothing in this audit may be read as asserting that the marker was emitted as proof
of app-shell completion. It was not, in any report or in any run I examined. The
strict gate is **correctly RED**.

Scope: all nine `tests/*.sh` scripts and all eight `tests/*.md` verification /
diagnostic reports, plus the two ctest registrations and the ldd / SHA identity
checks referenced throughout. Method: read every asset in scope; independently
re-ran the deterministic checks (`verify-native.sh`, `ctest -N` / `ctest`,
`ldd`, `file`, engine `sha256sum`/`readelf`, D09 bare and `--run`, chain-membership
of the report-471 gap knobs) against the current worktree to ground the reports'
claims.

---

## 1. Executive summary

- The verification apparatus is **large, disciplined and honest**, but it is
  **almost entirely manual / report-driven**. The only thing wired into CI is two
  trivial self-checks on `sober-native` (`ctest` 2/2). None of the nine shell
  scripts is registered in ctest, CMake, or `verify-native.sh`.
- **What passes:** native ELF identity, clean libc-only `ldd`, engine SHA/BuildID
  integrity, phase-2 guest init, phase-3 first frame + guest render call, and the
  default-OFF status of the fault/diagnostic scripts. Reports 451/456/460/463/471,
  D01 and D12 all agree here, and I reproduced the key checks directly.
- **What is expected to fail (strict gate):** `tests/app-shell.sh` is RED by
  design at §3. It demands a notifier-provenance line that the harness does not
  emit, and the real guest→Java completion callback has never occurred. All
  reports treat this as the correct, honest outcome. The marker stays WITHHELD.
- **Largest gaps:** (a) "runs without compat runtime" and "ABI coverage" are
  manual notices only — 0% automated; (b) ctest covers none of the functional
  behaviour; (c) the withhold-chain audit D09 is scoped to a single source file
  and its "GAP LIST: (none)" must not be over-read; (d) several reports are stale
  relative to the current scripts (quoted assertion text, withhold counts);
  (e) `first-frame.sh` is environmentally flaky (X11 teardown can force rc=1).

**Overall verification-intent coverage ≈ 57%** across the 13 domains scored in
§5 (this figure includes two manual-only domains at 0%). **Automated CI-gating
coverage ≈ 15%** — only the two structural ctest self-checks gate a build.

---

## 2. Inventory of `tests/*.sh` (9 scripts)

| Script | Role | Asserts / emits | Wired into ctest/CMake/verify-native? | Status today |
| --- | --- | --- | --- | --- |
| `verify-native.sh` | Whole-port definition of done | §1 native ELF, §2 ldd no translation layer, §3 + §4 **manual notices only** | standalone (invoked by reports) | **PASS** rc=0 (reproduced) |
| `guest-dlopen.sh` | Phase-2 init harness (worker-3 #377) | BuildID gate, clean ldd, `guest init complete` + marker, on-disk SHA unchanged | no | **PASS** (per reports) |
| `first-frame.sh` | Phase-3 smoke (worker-3 #380) | frame marker, guest swap `0x649d422`, guest-driven swap ≥1, X11 window, SHA unchanged, phase-2 preserved | no | **PASS** rc=0 (2/3 clean in D01; 3/3 in D12) |
| `app-shell.sh` | Phase-4 strict decision-013 gate (worker-3 #385) | no ALT thunk/HONESTY, no synthetic-seed withhold, real guest→Java callback, guest render call, **notifier-provenance verdict**, shell marker | no | **EXPECTED RED** rc=1 at §3 |
| `fault-baseline.sh` | Default no-start-app fault recorder (#451 follow-up) | records RVA `0x2eb4b65`, `signal=11`, `rdi=NULL`; **never asserts, always exit 0** | no (explicit invocation only) | **PASS** (informational) |
| `D08-dispatch-diag.sh` | Full opt-in dispatch diagnostic (worker D8) | records fault RVA, guest→Java lines, withhold lines, SHA; unsets all marker env; always exit 0 | no (explicit only) | **PASS** (informational) |
| `D09-withhold-audit.sh` | Static withhold-chain coverage audit (worker D9) | text-only parse of `guest_app_shell.c`: 92 knobs, 86 in chain, 6 benign, 0 gaps; always exit 0 | no (default-OFF, `--run`/env gate) | **PASS** (informational, scoped) |
| `gdb-trace.sh` | Phase-4b gdb-as-parent allocator trace (#395) | sets guest breakpoints, prints backtraces; skips if gdb absent | no | diagnostic only |
| `trace-app-shell.sh` | Phase-4b LD_PRELOAD allocation trace (#395) | prints `[trace]` markers and phase tokens; exit 0 | no | diagnostic only |

Note: every script that needs the guest engine **SKIPs with rc=0 when
`/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so` is absent** (first-frame,
app-shell, guest-dlopen, fault-baseline, D08). A "green" from these scripts in an
environment without the engine proves nothing. In this worktree the engine is
present (SHA `3e498d64…41335`, BuildID `204ad049…6c9`), so they actually run.

---

## 3. Inventory of `tests/*.md` verification reports (8)

| Report | Date | Snapshot / purpose | Headline outcome |
| --- | --- | --- | --- |
| `verification-report-451.md` | 20 Nov | default-path async fault RVA `0x2eb4b65` root-cause + HEAD vs worktree | all invariants PASS; fault is new site but not a regression of a working path; no tests/-only fix exists |
| `verification-report-456.md` | 20 Nov | phase-8 as-landed re-run + ALT-thunk memo draft | 5 invariants PASS; app-shell RED (no genuine callback + stale ALT assertion) |
| `verification-report-460.md` | 20 Nov | phase-8 re-run, memo refresh | 7 invariants PASS; app-shell RED; withhold reconciliation flagged |
| `verification-report-463.md` | 20 Nov | phase-8 wiring, marker gate, opt-in A/B | invariants PASS; 8 withholds; ALT control is the labelled false-green |
| `verification-report-471.md` | 20 Nov 20:00 | invariants + decision-013 gate audit | invariants PASS; **17 in-chain, 13+2 coverage gaps escalated to lead** |
| `D01-invariants.md` | 20 Nov 21:34 | Team-D invariant re-run | 8/8 PASS; first-frame 2/3 clean + 1 X11 XIO flake; chain "now expanded" |
| `D04-baseline-tracking.md` | 20 Nov | 8-row fault baseline table (3 fixed, 5 open) | decision 013 invariant; marker WITHHELD in every row |
| `D12-final-invariants.md` | 20 Nov 21:50 | Team-D final re-run | 9/9 PASS; first-frame 3/3 clean; D08/D09/fault-baseline all default-OFF |

The reports are internally consistent on the invariant set: `verify-native.sh`
rc=0, ctest 2/2, libc-only ldd, engine SHA/BuildID unchanged, `guest init
complete` + `first frame rendered` present, and **zero `app shell rendered`
emissions and no marker file on every default path**.

---

## 4. Passes, expected failures, and direct evidence

### 4.1 What passes (verified this audit)
- `bash tests/verify-native.sh` → rc=0; §1 ELF, §2 ldd = `linux-vdso.so.1`,
  `libc.so.6`, `/lib64/ld-linux-x86-64.so.2` only. (reproduced)
- `ctest --test-dir build -N` → exactly 2 tests; `ctest` → 2/2 Passed
  (`sober_native_check`, `sober_native_version`). (reproduced)
- `CMakeLists.txt:189-191` → `enable_testing()` + exactly two `add_test`;
  `build/CTestTestfile.cmake` confirms no third registration. (reproduced)
- `file build/sober-native` → native x86-64 PIE ELF. (reproduced)
- Engine `sha256sum` = `3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335`,
  BuildID `204ad049599c96afb75b8a21d5051063e821d6c9`. (reproduced)
- `first-frame.sh`: phase-3 markers, guest swap `0x649d422`, guest-driven swap,
  visible window — PASS across reports (D01 2/3 clean, D12 3/3 clean).
- Default-OFF discipline: ctest / CMake / verify-native reference none of
  fault-baseline, D08, D09, first-frame, app-shell. (reproduced for D09 wiring)

### 4.2 What is expected to fail (strict gate) — WITHHELD
- `app-shell.sh` §3 is **permanently RED**. Its decisive leg (lines 129-133)
  requires a line matching
  `\[app-shell\].*notifier-driven gameLoadedCallback.*0x27eaf6e.*0x27eafb3`
  and forbids `HARNESS-THUNK`. I grepped the current
  `src/linux/guest_app_shell.c`: **no** `notifier-driven`, `0x27eaf6e`,
  `0x27eafb3` or `HARNESS-THUNK` verdict is emitted by the harness. The script's
  own comment (lines 126-128) states this line "does not exist yet" and that the
  test is "correctly RED today"; it deliberately does **not** set
  `SOBER_CALLBACK_PROVENANCE` (a diagnostic opt-in and withhold entry, now in the
  chain at `guest_app_shell.c:1530`).
- The real guest→Java completion callback (`gameLoadedCallback` /
  `onAppShellReloadNeeded`) has never occurred on the default path: reports show
  `callbacks=0`, `last=isVrDevice`, `marker withheld: callback=0 render=1`.
- Therefore the gate has **no positive proof to give**, and the marker is
  **WITHHELD** on every run. This is the intended, honest state; RED is the
  expected result, not a defect to "fix" by re-enabling the thunk (that would be
  a false green, as every report warns).

### 4.3 Withhold-chain closure (important cross-report finding)
Report 471 §3b/3c escalated **15 knobs** (13 gaps + `SOBER_APP_SINGLETON_BRIDGE`
+ `SOBER_STARTAPP_PARAMS`) that mutated guest state while outside the
`seeded_state` chain — a false-green risk. D01/D12 report the chain was
subsequently expanded. I verified directly: **all 15 are now IN-CHAIN**, and D09
`--run` reports **92 referenced, 86 in chain, 6 benign, GAP LIST: (none)**. The
gap is closed in source. Caveat: D09 is a static text audit of
`guest_app_shell.c` only (see §6).

---

## 5. Coverage table (domain %)

Scoring rubric: % = how completely the domain's verification intent is
**mechanically asserted** by an executable check, Credit is given for report
evidence where no executable gate exists but the property was explicitly
observed. "In CI" = wired to fail a build/ctest run.

| # | Domain | Primary check(s) | Status | Automated assert? | In CI? | Coverage |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | Native ELF + linkage (ldd / no translation layer) | verify-native §1-2; guest-dlopen §1; first-frame §1; app-shell §0 | PASS | yes | partial (ctest ties to same binary) | **100%** |
| 2 | Engine identity / integrity (SHA, BuildID) | pre/post checks in first-frame, app-shell, guest-dlopen, fault-baseline, D08; all reports | PASS | yes (where present) | no | **95%** |
| 3 | Phase-2 guest init / ctor replay | guest-dlopen §5; preserved preconditions in first-frame §3, app-shell §2 | PASS | yes | no | **90%** |
| 4 | Phase-3 render / first frame | first-frame §2-6; app-shell §2; guest swap `0x649d422` | PASS | yes (window check softened to WARN) | no | **85%** |
| 5 | Strict decision-013 gate / marker WITHHELD | app-shell §3; A/B in reports 463/471; D01/D12 §5 | EXPECTED RED | yes (as negative proof) | no | **90%** of withhold intent; **0%** of green-proof intent (by design) |
| 6 | Known-fault tracking (baseline + D04 8 rows) | fault-baseline.sh; D04 table | INFO / default-OFF | records, never fails | no | **70%** |
| 7 | D08 dispatch diagnostic | D08-dispatch-diag.sh | INFO / default-OFF | no gate | no | **60%** |
| 8 | D09 withhold-chain audit | D09-withhold-audit.sh | INFO / default-OFF | static, exit 0 | no | **65%** |
| 9 | ctest automated regression | 2 trivial `--check` / `--version` self-checks | PASS | yes | **yes** | **30%** |
| 10 | JNI callback provenance | app-shell §3 decisive leg (line absent) | RED / negative evidence only | no positive check | no | **25%** |
| 11 | gdb / allocation-trace diagnostics | gdb-trace.sh, trace-app-shell.sh | diagnostic | no | no | **25%** |
| 12 | "Runs without compat runtime" (verify-native §3) | manual notice text | MANUAL ONLY | no | no | **0%** |
| 13 | ABI map coverage (verify-native §4) | manual notice text | MANUAL ONLY | no | no | **0%** |

Aggregate (13 domains): **≈ 57%** verification-intent coverage.
CI-gating coverage (domains 9 + the linkage slice of 1): **≈ 15%**.

---

## 6. Gaps in verification

1. **CI covers almost nothing functional.** ctest registers only two shallow
   self-checks on `sober-native`. None of the nine shell scripts is wired into
   ctest/CMake/verify-native. A green CI run says nothing about guest init,
   render, JNI, or the withhold gate. (High severity for assurance.)
2. **Two "definition of done" items are unchecked.** `verify-native.sh` §3 and
   §4 print manual notices; "no Wine/Proton present" and "every required ABI row
   implemented" are never mechanically verified. (0% automated.)
3. **Marker-env SKIP false-greens.** Five scripts exit 0 without testing when the
   guest engine is absent (§2 note). Green ≠ executed.
4. **D09 "0 gaps" is narrowly scoped.** It parses only `getenv("SOBER_…")`
   calls in `guest_app_shell.c`, hard-codes a 6-entry benign allowlist, and
   always exits 0. Guest-mutating knobs defined/read in *other* translation units
   (e.g. `app_singleton_bridge.c`, `startapp_params.c`, `bionic_bootstrap.c`)
   are invisible to it. Its clean result is good evidence but not a whole-project
   guarantee. (Moderate.)
5. **Reports are stale relative to current scripts.** Reports 456/460/463 quote
   the old §3 failure text (`no genuine guest->Java JNI callback observed`); the
   current `app-shell.sh:118` says `no qualifying guest->Java app-shell callback
   observed` and adds the 038(b) provenance leg. Report 463 counts "eight"
   withholds while 471 counts 17 in-chain. Withhold counts and quoted assertions
   in older reports should not be taken as current state. (Low-moderate.)
6. **`first-frame.sh` environmental flake.** D01 run 1 produced rc=1 from an X11
   `XIO: fatal IO error 2` at teardown even though every functional marker was
   present. The script ties success to harness `rc==0`, so a transient headless
   Xwayland error can fail an otherwise green test. D12 saw 3/3 clean. (Low;
   honestly disclosed in both reports.)
7. **The gate can never go green under the current script.** app-shell §3
   requires a harness verdict that does not exist; this is *correct* today, but
   there is no documented "definition of done" for flipping it green (what exact
   provenance line, emitted by which code path, with all opt-ins unset). Until
   that is specified, the gate is a permanent RED with no exit criterion. (Moderate.)
8. **No automated regression guard for the fault catalogue.** D04's 8-row table
   (3 fixed, 5 open) is a manual reference; nothing in ctest checks that "fixed"
   RVAs stay absent or that "open" RVAs move rather than vanish. (Moderate.)

---

## 7. Bottom line

- The verification corpus is **honest and thorough for what it checks**: native
  identity, linkage, engine integrity, phase-2/3 functionality and the strict
  WITHHOLD discipline are repeatedly and consistently demonstrated across eight
  reports and two independent Team-D re-runs.
- **Everything functional is manual; the automated surface is two trivial
  ctest self-checks.** Domain coverage averages ≈ 57% of verification intent,
  with only ≈ 15% actually gating CI.
- **`app-shell.sh` is expected RED** and the marker is **WITHHELD** — this is the
  correct strict-gate outcome, not a failure to be patched by re-enabling the
  ALT thunk. No report, and nothing I reproduced, claims app-shell completion.
- The one substantive gap reported in-scope (report 471's 15 withhold-chain
  holes) **has been closed in source** (all 15 now in-chain; D09 reports 0 gaps),
  with the caveat that D09 only audits a single file.
- Remaining work to raise assurance: wire at least the structural/smoke checks
  into ctest, automate the two manual "definition of done" notices, document the
  gate's green exit criterion, and broaden withhold auditing beyond
  `guest_app_shell.c`.
