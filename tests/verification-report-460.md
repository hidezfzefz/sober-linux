# Verifier report — task #460 (Phase 8 invariant re-run + ALT-thunk memo refresh)

Date: 2026-11-20. Owner: verifier, team `TEAM-LF55-5P2L-B4DQ`, member `mem_986e35991f995d105aab`.
Scope: `tests/` and `analysis/decision-memo-alt-thunk-vs-013.md` only. **No source, gate, or
`tests/app-shell.sh` changes were made.** This is a re-run of task #456's checks against the
as-landed Phase-8 worktree (HEAD `b8bdab0` plus uncommitted phase-8 edits from javavm/objmodel).

## 1. Invariants — all PASS

| Check | Command / detail | Result |
| --- | --- | --- |
| Native verification script | `bash tests/verify-native.sh` | PASS (rc=0): native ELF, no translation-layer deps |
| ctest 2/2 | `ctest --test-dir build --output-on-failure` | 2/2 passed (`sober_native_check`, `sober_native_version`); `ctest -N` lists exactly 2 |
| `ldd` libc-only | `ldd build/guest-init-host`, `ldd build/sober-native` | both: `linux-vdso.so.1`, `libc.so.6`, `/lib64/ld-linux-x86-64.so.2` only |
| Engine identity | `readelf -n`, `sha256sum` on `/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so` | BuildID `204ad049599c96afb75b8a21d5051063e821d6c9`; SHA-256 `3e498d64…41335` — unchanged |
| Guest init complete | default-path harness stdout + `/tmp/sober-guest-init.marker` | present |
| First frame rendered | default-path harness stdout + `/tmp/sober-first-frame.marker` | present |
| Zero marker emissions | `/tmp/sober-*.marker`, stdout/stderr of both runs | no `sober-app-shell.marker`; no `app shell rendered` text anywhere |
| Staleness | mtime of all phase-3/8 sources vs `build/guest-init-host` | none newer — harness is current |

## 2. Fault baseline stays default-OFF and records the same fault

`bash tests/fault-baseline.sh` (explicit invocation only) → harness rc=139, script rc=0:
`[harness] faulting guest RVA: 0x2eb4b65`, `rdi=(nil)`, stdout exactly
`guest init complete` / `first frame rendered`, plus the six real JNI
`nativeAppBridgeAppStart` arguments. This matches task #456's baseline exactly — no regression.

Default-OFF wiring re-confirmed: `ctest -N` = 2 tests (no baseline), `verify-native.sh` never
references it, `add_test` appears only twice in `CMakeLists.txt:189-190`, and the only textual
references to `fault-baseline` are its own file, `PROGRESS.md:157`, and the two verification
reports. No default-ON wiring introduced.

## 3. Strict gate stays RED for an honest reason

`bash tests/app-shell.sh` → exit **1** at §3: `FAIL: no genuine guest->Java JNI callback observed`.
stdout is exactly `guest init complete` / `first frame rendered`; no shell marker. Log highlights:

```
[app-shell] JNI_OnLoad JavaVM seed (SOBER_SEED_JNI_VM) -> rc=0
[app-shell] faithful JavaVM install (SOBER_FAITHFUL_JAVAVM) -> rc=0
[app-shell] env_calls=33 callbacks=0 register_natives=0 last=isVrDevice
[app-shell] marker withheld: callback=0 render=1 (env calls=33 callbacks=0 last='isVrDevice')
[app-shell] guest call 0x247dc8d nativeAppBridgeAppStart(...) -> rc=...
[harness] faulting guest RVA: 0x2eb4b65
```

Phase-8 progress is visible (`env_calls=33`, faithful-JavaVM A/B wiring present) but **callbacks=0**.
`guest_app_shell.c:1129-1161` withheld the marker because `shell_cb=0`, not because of the
diagnostic-seed list (`seeded_state` was 0 on this run: no `marker withheld: diagnostic guest-state
seed active` line). Decision 013 honesty upheld.

The test remains internally contradictory, as documented: `app-shell.sh:68` unsets
`SOBER_ALT_CALLBACK_PATH` (Phase 6, `b8bdab0`) while `:112-116` (Phase 5, `129bd6f`) requires
`ALT PATH guest call 0x25848e0`, which only that env can produce. The strict gate correctly fails
before reaching that assertion; re-enabling the thunk would produce a **false green**.

## 4. Memo refreshed (not enacted)

`analysis/decision-memo-alt-thunk-vs-013.md` updated with: current code line references, the new
`SOBER_FAITHFUL_JAVAVM` entry in the withhold list (`guest_app_shell.c:1140-1144`), this run's
`env_calls=33 / callbacks=0` evidence, and an explicit lead question on withhold-list
reconciliation. Options A/B/C and the B+C recommendation are unchanged; **nothing was enacted**.

## 5. Coordination

- Confirmed to objmodel the ownership split (their `analysis/native-object-layouts.md` +
  `src/jni/jni_native_objects.c/.h` vs my `tests/` + memo).
- Asked lead to rule whether `SOBER_FAITHFUL_JAVAVM` may count as a real-path enabler or must stay
  disqualifying — otherwise a phase-8 callback reached only under that opt-in is silently withheld.

**Snapshot caveat:** this pass ran at ~07:42 against the harness built before 07:44. At 07:44 a
concurrent edit removed the `sober_javavm_install` call from `guest_app_shell.c` (javavm reported
this to lead) and the harness was rebuilt at 07:45. Therefore the `faithful JavaVM install` log
line and `env_calls=33` above describe the pre-07:44 snapshot, not the current binary. The
structural invariants (build/ctest/ldd/engine SHA/identity) are unaffected; the runtime observations
should be re-run once lead re-wires the APIs and objmodel/#459 and flagbridge/#458 land.

**Post-landing re-check (after objmodel #459 landed):** re-ran the fast structural invariants —
`verify-native.sh` rc=0, ctest 2/2, `ldd build/guest-init-host` libc-only, engine SHA
`3e498d64…41335` unchanged, no source newer than `build/guest-init-host`. All still PASS. The
runtime/app-shell observations remain the ~07:42 snapshot described above.

**Bottom line:** all seven invariants PASS; the fault baseline is unchanged and default-OFF;
`app-shell.sh` is red for an honest reason (no genuine callback) plus the stale Phase-5 assertion;
decision 013 is upheld; the lead decision memo is refreshed and awaiting a ruling.
