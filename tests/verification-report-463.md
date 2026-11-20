# Verification report 463 — phase-8 wiring, marker gate, and opt-in withhold A/B

Verifier, team `TEAM-LF55-5P2L-B4DQ`, task #463. Date 2026-11-20.
Repo: `/home/user/sober-linux`. Build: `cmake --build build -j4` (all targets up to date).
Guest: `/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so`, BuildID
`204ad049599c96afb75b8a21d5051063e821d6c9`, SHA256
`3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335` (unchanged before/after every run).

Scope discipline: `src/`, `CMakeLists.txt`, `tests/app-shell.sh`, and the marker gate were NOT
modified. `src/linux/guest_app_shell.c` was read only. Files written: this report and
`analysis/decision-memo-alt-thunk-vs-013.md` (finalized recommendation, not enacted).

## 1. Invariants

| Check | Result |
| --- | --- |
| `cmake --build build -j4` | clean, all targets built |
| `bash tests/verify-native.sh` | PASS, rc=0; native ELF + no translation-layer deps |
| `ctest --test-dir build --output-on-failure` | 2/2 Passed (`sober_native_check`, `sober_native_version`) |
| `ldd build/sober-native` | `linux-vdso.so.1`, `libc.so.6`, `/lib64/ld-linux-x86-64.so.2` only |
| engine BuildID / SHA256 | unchanged, matches baseline |
| `bash tests/first-frame.sh` | PASS, rc=0; `guest init complete` + `first frame rendered`; guest swap `0x649d422`; SHA unchanged |

## 2. Strict gate on default path (`bash tests/app-shell.sh`)

- rc=1 at §3: `FAIL: no genuine guest->Java JNI callback observed` — expected under decision 013.
- stdout: exactly one `guest init complete`, one `first frame rendered`.
- **Zero** `app shell rendered` emissions (no marker file, no stdout token).
- Gate: `marker withheld: callback=0 render=1 (env calls=33 callbacks=0 last='isVrDevice')`.
- No `diagnostic guest-state seed active` line → `seeded_state=0` on default path (all opt-ins OFF).
- Pre-existing guest fault `0x2eb4b65` unchanged.
- Phase-8 log lines present but no-op: native-object shapes `rc=-1`, JNI_OnLoad JavaVM seed
  `rc=0`, faithful JavaVM `rc=0`, JSON flag override bridge `rc=-2`.

## 3. Opt-in force-withhold A/B

Code (`src/linux/guest_app_shell.c`, read only): all five task opt-ins are in the `seeded_state`
OR-chain at `:1150-1157`; any set forces `shell_cb = 0` at `:1161`; the sole marker emission
(`:1191`) is reachable only through `shell_cb && render_ok` at `:1167`. Additional withhold
entries: `SOBER_SEED_USER_CONTROLLER`, `SOBER_SEED_APP_DELEGATE`, `SOBER_REAL_APPSTATE_PATH`
(total eight). Each module is independently default-OFF.

| Run | Marker | Notes |
| --- | --- | --- |
| `SOBER_ALT_CALLBACK_PATH=1` (control) | **emitted** | rc=0, `callbacks=1`, HONESTY NOTICE — the false-green the gate must prevent |
| ALT + `SOBER_FAITHFUL_JAVAVM` | none | guest fault `0x2e5d42a` before gate |
| ALT + `SOBER_NATIVE_OBJECTS` | none | reached gate, `callbacks=1`, `marker withheld: diagnostic guest-state seed active` |
| ALT + `SOBER_JSON_FLAG_BRIDGE` | none | guest fault `0x24ad562` before gate |
| ALT + `SOBER_JSON_APP_SINGLETON` | none | reached gate, `callbacks=1`, withhold line printed |
| ALT + `SOBER_SEED_JNI_VM` | none | guest fault `0x2e5d42a` before gate |
| each of the five opt-ins alone | none in all five | NATIVE_OBJECTS + JSON_APP_SINGLETON printed withhold line; others faulted before gate |

Conclusion: every opt-in env var force-withholds the marker, by construction and in every
observed run. No phase-8 opt-in produced a genuine callback; three fault the guest earlier.
Nothing is being wrongly discarded by the withhold list today (default path `callbacks=0`).

## 4. Fault baseline default-OFF

`bash tests/fault-baseline.sh`: rc=0, informational; no marker emitted or checked; absent from
ctest (`build/CTestTestfile.cmake` has only the two sober-native tests) and from
`tests/verify-native.sh`. Fault record unchanged: `signal=11`, `rdi=(nil)`,
`faulting guest RVA: 0x2eb4b65`, SHA/BuildID unchanged.

## 5. Outcome

Decision memo finalized with recommendation **B + C; reject A** — remove the stale ALT-thunk
assertion, keep the strict real-path callback gate, park the thunk in a default-OFF labelled
diagnostic, keep all eight withholds, and enact nothing without an explicit lead ruling.
