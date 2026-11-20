# Decision memo (FINAL recommendation — NOT enacted) — `tests/app-shell.sh` ALT-thunk contract vs Decision 013

Owner: verifier (team `TEAM-LF55-5P2L-B4DQ`, task #463; carried from task #460 / team
`TEAM-MV6K-LSYJ-FGQQ`, tasks #456/#451). Date: 2026-11-20.
Status: **FINAL recommendation for the lead.** Nothing in this memo has been enacted:
`tests/app-shell.sh`, the marker gate in `src/linux/guest_app_shell.c`, `src/`, and
`CMakeLists.txt` are unchanged by this worker. This memo only records evidence and a
recommendation. The verifier owns only `tests/` verification reports and this analysis file;
`src/linux/guest_app_shell.c` was READ ONLY throughout.

## 1. The conflict, precisely

- **Decision 013** (`docs/decisions/013-no-weak-app-shell-marker.md:6-13`) rules that the real
  `app shell rendered` marker must be gated on a **genuine guest→Java callback** plus the
  phase-3 guest render call. Lines 46-51 leave an explicit open inconsistency:
  `tests/app-shell.sh` still requires `ALT PATH guest call 0x25848e0` even though that is a
  harness-triggered thunk and therefore not app-shell proof; tightening it needs an explicit
  lead decision and a test change, neither made.
- **Decision 037** (`docs/decisions/037-callback-thunk-milestone.md:20-33`) introduced the opt-in
  `SOBER_ALT_CALLBACK_PATH` that calls guest thunk `0x25848e0`
  (`gameLoadedCallback` via `CallStaticVoidMethodV`, global slot 142 / `JNIEnv+0x470`).
  Its own honesty requirement says a green `app-shell.sh` proves a real guest thunk entered
  our bridge and the phase-3 render RVA ran, but **does not prove a login/app-shell frame**,
  because the callback is harness-triggered. Every report must state that.
- **Harness code** (`src/linux/guest_app_shell.c`, current line numbers, phase-8 wiring applied):
  `:1022` sets `alt_path` from `SOBER_ALT_CALLBACK_PATH`; `:1125-1137` calls `0x25848e0` and logs
  `ALT PATH guest call 0x25848e0`; `:1144-1145` computes `shell_cb` from the two named JNI
  callbacks; `:1150-1157` computes `seeded_state`; `:1158-1162` forces `shell_cb = 0` and logs
  `marker withheld: diagnostic guest-state seed active`; `:1165` reads persisted phase-3
  `render_ok`; `:1167-1183` gates on `shell_cb && render_ok` and returns `-4` on failure;
  `:1185-1190` prints the HONESTY NOTICE on an ALT-path pass; `:1191` is the sole
  `emit_marker("app shell rendered\n", ...)` call site. Notes: `:826-878` install the four
  phase-8 opt-in modules and log unconditionally, but each module is a no-op unless its own env
  knob is set, so those log lines do **not** mean `seeded_state` is active. The withhold list at
  `:1150-1157` now has **eight** entries (four new phase-8 knobs added since task #456):
  `SOBER_SEED_USER_CONTROLLER`, `SOBER_SEED_APP_DELEGATE`, `SOBER_SEED_JNI_VM`,
  `SOBER_FAITHFUL_JAVAVM`, `SOBER_NATIVE_OBJECTS`, `SOBER_JSON_FLAG_BRIDGE`,
  `SOBER_JSON_APP_SINGLETON`, `SOBER_REAL_APPSTATE_PATH`.
- **Test contract** (`tests/app-shell.sh`): `:68` (Phase 6, commit `b8bdab0`) runs
  `unset SOBER_ALT_CALLBACK_PATH` with the comment "no harness-triggered thunk bypass; the
  callback must arise from the real `nativeAppBridgeAppStart` `0x247dc8d` path". But `:112-116`
  (Phase 5, commit `129bd6f`) **requires** the string `ALT PATH guest call 0x25848e0` in stderr.
  Those two assertions are mutually exclusive: with the env unset the harness cannot emit the
  line the test demands. The test is internally contradictory independent of phase 8.

## 2. Verification evidence (task #463 re-run after lead's phase-8 wiring)

All commands run from `/home/user/sober-linux`; build `cmake --build build -j4` completed clean
(all targets up to date). Guest engine: `/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so`,
BuildID `204ad049599c96afb75b8a21d5051063e821d6c9`, SHA256
`3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335` (unchanged before and after
every run).

### 2.1 Invariants — all PASS

| Invariant | Result |
| --- | --- |
| `bash tests/verify-native.sh` | PASS (rc=0): native ELF present, no translation-layer deps |
| `ctest --test-dir build` | **2/2 Passed** (`sober_native_check`, `sober_native_version`) |
| `ldd build/sober-native` | only `linux-vdso.so.1`, `libc.so.6`, `/lib64/ld-linux-x86-64.so.2` — no Wine/Proton/box64/QEMU/Android/X11/GL deps |
| engine SHA256 / BuildID | unchanged; matches the task baseline exactly |
| `bash tests/first-frame.sh` | PASS (rc=0): `guest init complete` + `first frame rendered`, guest swap `0x649d422` observed, engine SHA unchanged |

### 2.2 Default path — strict gate correctly withholds; zero marker emissions

`bash tests/app-shell.sh` exits **1** at §3 with `FAIL: no genuine guest->Java JNI callback
observed`. This is the expected, correct outcome under decision 013, not a regression:

- stdout contains exactly one `guest init complete` and one `first frame rendered`;
- **zero** `app shell rendered` emissions — no marker file, no stdout token;
- gate log: `marker withheld: callback=0 render=1 (env calls=33 callbacks=0 last='isVrDevice')`;
- no `marker withheld: diagnostic guest-state seed active` line, i.e. `seeded_state=0` on the
  default path — the withhold is due to no genuine callback, not to a diagnostic seed;
- guest faults at the pre-existing RVA `0x2eb4b65` (unchanged baseline);
- phase-8 log lines are present but report no-op/default-OFF: native-object shapes `rc=-1`,
  JNI_OnLoad JavaVM seed `rc=0`, faithful JavaVM `rc=0`, JSON flag override `rc=-2`.

The red test has two independent causes: (1) the real path still produces no genuine guest→Java
callback, and (2) even if it did, the stale `:112` assertion could never be satisfied while `:68`
unsets the ALT path. Making it green by re-enabling the ALT thunk would be a **false green**.

### 2.3 Opt-in env vars force-withhold the marker — CONFIRMED

Code-level confirmation is authoritative and complete: all five task-listed opt-ins appear in
the `seeded_state` OR-chain at `guest_app_shell.c:1150-1157`; any one set forces `shell_cb = 0`
at `:1161`; the only marker emission (`:1191`) is reachable only via `shell_cb && render_ok`
(`:1167`). They therefore force-withhold independently of callback source or render state.
Each module is independently default-OFF (`native_jni.c:1401`, `javavm_semantics.c:384`,
`jni_native_objects.c:77`, `jni_json_bridge.c:125/245`). `jni_native_objects.h:15` explicitly
states these shapes emit no callbacks.

Empirical A/B (all runs with `SOBER_RENDER=1 SOBER_APP_SHELL=1`, marker token checked in both
stdout and marker file):

- **Control** — `SOBER_ALT_CALLBACK_PATH=1` only: rc=0, marker file **and** stdout token emitted,
  `callbacks=1`, HONESTY NOTICE printed. This is the known false-green the gate must prevent
  once any synthetic state is present.
- **ALT path + each opt-in** (`SOBER_FAITHFUL_JAVAVM`, `SOBER_NATIVE_OBJECTS`,
  `SOBER_JSON_FLAG_BRIDGE`, `SOBER_JSON_APP_SINGLETON`, `SOBER_SEED_JNI_VM`): **zero** marker
  emissions in all five runs. `SOBER_NATIVE_OBJECTS` and `SOBER_JSON_APP_SINGLETON` reached the
  gate with `callbacks=1` and printed `marker withheld: diagnostic guest-state seed active`
  (direct proof the forced withhold overrides an otherwise-satisfied callback). The other three
  faulted earlier in this worktree (RVAs `0x2e5d42a`, `0x24ad562`, `0x2e5d42a`) and never reached
  the gate, but still emitted zero markers.
- **Each opt-in alone** (no ALT path): zero marker emissions in all five runs;
  `SOBER_NATIVE_OBJECTS` and `SOBER_JSON_APP_SINGLETON` again printed the withhold line.

Conclusion: every opt-in env var force-withholds the app-shell marker, both by construction and
in every observed run. No phase-8 opt-in produced a genuine callback; three of them actually
fault the guest earlier. There is currently no callback being wrongly discarded by the withhold
list — the default path has `callbacks=0`.

### 2.4 Fault baseline remains default-OFF — CONFIRMED

`bash tests/fault-baseline.sh`: rc=0, informational only; no app-shell marker emitted or checked;
not referenced by `tests/verify-native.sh` and not registered in ctest (`build/CTestTestfile.cmake`
contains only `sober_native_check` and `sober_native_version`). Recorded fault unchanged:
`signal=11`, `faulting guest RVA: 0x2eb4b65`, `di=(nil)`, guest SHA/BuildID unchanged. Its
default-OFF status is intact.

## 3. Options for the lead

### Option A — Keep the ALT-thunk requirement, add an explicit honesty notice

- **Mechanism**: stop unsetting `SOBER_ALT_CALLBACK_PATH` in `app-shell.sh:68` (or set it for
  the test), keep `:112-116`, and make §3 print the decision 037 honesty text verbatim in every
  passing run; reports relabel the result as "alt-callback exercised; no app-shell frame proven".
- **Test impact**: `app-shell.sh` goes green quickly; phase-5 thunk coverage retained.
- **Phase-5 history impact**: decision 037 stays the basis for the test, but its honesty caveat
  becomes load-bearing rather than incidental; Phase 6's deliberate removal (`b8bdab0`) is
  reverted.
- **Cost / risk**: reintroduces exactly the weak marker decision 013 rejected as the done
  condition. A notice in test output does not change what the marker string asserts, and any
  automation or reader of `app shell rendered` is still misled.

### Option B — Require a genuine guest→Java callback from the real app-start path

- **Mechanism**: delete the stale `app-shell.sh:112-116` assertion; keep `:108` (a genuine
  callback was observed) and `:117` (phase-3 render RVA `0x649d422` ran); additionally require
  that no `HONESTY NOTICE` / `alt_path` line is present, i.e. the callback came from the real
  path rather than the thunk. Phase-8 work must actually deliver such a callback on the default
  path.
- **Reconciliation needed**: `guest_app_shell.c:1150-1157` forces `shell_cb = 0` whenever any of
  the eight opt-ins is active. If a future phase-8 callback appears only under one of those
  opt-ins, the marker will still be withheld. The lead must decide which real-path enabler, if
  any, is allowed to count — or confirm the callback appears with every opt-in unset. As of this
  re-run the point is moot in practice: no opt-in produced a callback and the default path has
  `callbacks=0`, so nothing is being wrongly discarded today.
- **Test impact**: `app-shell.sh` stays red until a real callback exists; no broadening of the
  gate. Phase-5 ALT-thunk result is reclassified as diagnostic, not app-shell proof; decision
  037 needs an amendment saying so.
- **Cost / risk**: red test for an indefinite period; depends on phase 8 actually landing.

### Option C — Separate diagnostic marker (recommended companion to B)

- **Mechanism**: keep the harness-triggered thunk but give it its own, clearly-named diagnostic
  token and marker file (e.g. `alt-callback exercised (diagnostic) (no app-shell frame proven)`
  + `SOBER_APP_SHELL_ALT_MARKER`, opt-in only; the existing `SOBER_APP_SHELL_DIAGNOSTIC` hook at
  `guest_app_shell.c:1175` is a partial precedent). Add a second, **default-OFF** diagnostic
  script — modelled on `tests/fault-baseline.sh`, not wired into ctest or `verify-native.sh` —
  that sets `SOBER_ALT_CALLBACK_PATH`, asserts thunk `0x25848e0` fires and the HONESTY NOTICE is
  printed, and never emits or checks `app shell rendered`. Meanwhile `tests/app-shell.sh`
  becomes strict per Option B.
- **Test impact**: `app-shell.sh` stays strict/red until genuine proof; the new diagnostic is
  green now (control run in §2.3) and cannot be mistaken for app-shell proof.
- **Phase-5 history impact**: preserves and documents the phase-5 thunk achievement and
  decision 037's honesty requirement without promoting it to the done condition.
- **Cost / risk**: one more test script, token and marker to maintain; needs lead approval for
  the test edit and the diagnostic marker name.

## 4. Recommendation (FINAL)

**Adopt B + C; reject A as the done condition. Do NOT enact any change in this memo without an
explicit lead ruling.**

- Remove the stale `ALT PATH guest call 0x25848e0` assertion from `tests/app-shell.sh` (`:112-116`)
  and keep the strict gate: a genuine guest→Java callback from the **real** path plus the
  phase-3 render RVA. That is the only state consistent with decision 013.
- Park the harness-triggered thunk in a default-OFF, explicitly-labelled diagnostic with its own
  marker and mandatory honesty notice (Option C). This retains the phase-5 coverage and settles
  the contradiction decision 013 flagged without weakening the gate.
- **Do not** restore the ALT path as app-shell proof (Option A). The honesty notice in
  `guest_app_shell.c:1187-1189` is correct and should stay — but a notice cannot repair a marker
  whose literal meaning is "app shell rendered". §2.3 shows the ALT path still emits that marker
  today; that is precisely why it must remain opt-in and never count.
- Keep all eight withhold entries in place. Every phase-8 opt-in is semantic scaffolding that
  fabricates no callbacks and now demonstrably force-withholds the marker; none should be
  allowed to satisfy decision 013. A real callback on the fully default path remains the only
  acceptable green.
- Until a genuine default-path callback lands, `app-shell.sh` remaining red (no marker,
  `callback=0 render=1`) is the correct outcome, not a regression to hide. **No edit to
  `tests/app-shell.sh`, the gate, or `src/` should be made by this worker.**

## 5. Decisions requested from lead

1. Choose A / B / C (recommendation: **B + C**).
2. If B or C: approve the `tests/app-shell.sh:112-116` removal and the new diagnostic token/marker,
   and name the owner of that follow-up test edit.
3. Decide the withhold-list policy for `SOBER_REAL_APPSTATE_PATH`, `SOBER_SEED_*`, and the four
   phase-8 opt-ins (`SOBER_FAITHFUL_JAVAVM`, `SOBER_NATIVE_OBJECTS`, `SOBER_JSON_FLAG_BRIDGE`,
   `SOBER_JSON_APP_SINGLETON`) at `guest_app_shell.c:1150-1157`. Verifier's recommendation:
   keep all of them disqualifying. javavm confirmed their module is opt-in by design and
   fabricates no callbacks; objmodel's shapes emit no callbacks; flagbridge's JSON paths are
   opt-in. No current opt-in produces a callback, so the policy has not yet discarded a genuine
   one.
4. Decide whether decision 037 needs a short amendment recording that the ALT thunk is
   diagnostic-only.

## 6. Evidence references

- `docs/decisions/013-no-weak-app-shell-marker.md` (esp. 6-13, 46-51)
- `docs/decisions/037-callback-thunk-milestone.md` (esp. 20-33)
- `tests/app-shell.sh:68`, `:108-120` (git blame: `:68`=`b8bdab0`, `:112`=`129bd6f`) — unchanged
- `src/linux/guest_app_shell.c:1022`, `:1125-1137`, `:1144-1145`, `:1150-1162`, `:1167-1191`,
  `:826-878` (read only; current line numbers)
- `tests/verification-report-451.md`, `tests/verification-report-456.md`,
  `tests/verification-report-460.md` (prior baselines)
- Task #463 re-run (this memo §2): verify-native.sh PASS; ctest 2/2; ldd libc/ld-linux only;
  engine SHA unchanged; first-frame.sh PASS; app-shell.sh rc=1 with zero marker emissions and
  `callback=0 render=1`; opt-in A/B matrix (§2.3); fault-baseline.sh default-OFF (§2.4).
