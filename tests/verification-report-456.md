# Verifier report — task #456 (phase-8 invariant re-run + ALT-thunk decision draft)

Date: 2026-11-20. Owner: verifier (team `TEAM-MV6K-LSYJ-FGQQ`, member `mem_d82da9991b14a1e5e367`).
Scope: re-run all invariants after the first phase-8 edits landed; keep `tests/fault-baseline.sh`
default-OFF; draft (not enact) the lead decision memo on `tests/app-shell.sh` vs decision 013.
**No `src/`, gate, or `tests/app-shell.sh` changes were made by this worker.**

Snapshot point: `src/jni/native_jni.c`/`.h` (07:23) and `src/linux/guest_app_shell.c` (07:27)
had landed and `build/guest-init-host` was rebuilt (07:27). Phase-8 tasks #453/#454/#455 were
still in progress, adding new files that the lead will wire, so this is an "as-landed" pass;
a final re-run is requested once the lead integrates them.

## 1. Invariants — all PASS

| Check | Command / detail | Result |
| --- | --- | --- |
| Native verification script | `bash tests/verify-native.sh` | PASS (rc=0): native ELF, no translation-layer deps |
| ctest 2/2 | `ctest --test-dir build --output-on-failure` | 2/2 passed (`sober_native_check`, `sober_native_version`); no other tests registered |
| `ldd` libc-only | `ldd build/guest-init-host`, `ldd build/sober-native` | only `linux-vdso.so.1`, `libc.so.6`, `/lib64/ld-linux-x86-64.so.2` for both |
| Engine identity | `readelf -n` / `sha256sum` on `/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so` | BuildID `204ad049599c96afb75b8a21d5051063e821d6c9`; SHA-256 `3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335` — unchanged |
| Guest init complete | default-path harness run stdout | present |
| First frame rendered | default-path harness run stdout | present |
| Zero marker emissions | default-path stdout/stderr + marker files | no `app shell rendered`; no marker files |

Default-path run (exact fault-baseline env, `SOBER_RENDER=1 SOBER_APP_SHELL=1
SOBER_SCHEDULE_RUNNABLE=1 SOBER_APP_SHELL_WAIT_MS=3000`, EGL/software GL, `LD_LIBRARY_PATH=build/ndk-stubs`,
`timeout 90`): harness rc=139 at the pre-existing guest RVA `0x2eb4b65`. stdout was exactly:

```
guest init complete
first frame rendered
```

No `sober-app-shell.marker` (nor init/frame marker files, which that run did not request) and no
marker text. Decision 013 honesty upheld: no login/app-shell frame is proven and no weak marker
was emitted.

## 2. `tests/app-shell.sh` — currently FAILS (expected, and diagnostically important)

`bash tests/app-shell.sh` exits **1** at §3: `FAIL: no genuine guest->Java JNI callback observed`.
stdout again shows `guest init complete` + `first frame rendered`; stderr shows
`marker withheld: callback=0 render=1`, `guest call 0x247dc8d nativeAppBridgeAppStart`, and the
fault at `0x2eb4b65`. Two independent causes:

1. the real path has not yet produced a genuine guest→Java callback (phase-8 work still landing);
2. the test is internally contradictory: `:68` unsets `SOBER_ALT_CALLBACK_PATH` (Phase 6,
   `b8bdab0`) while `:112-116` requires the `ALT PATH guest call 0x25848e0` log line that only
   that env can produce (Phase 5, `129bd6f`).

This is the conflict documented in decision 013:46-51. The strict gate behaved correctly. The
draft lead decision memo is at `analysis/decision-memo-alt-thunk-vs-013.md`; no change to the
test or gate was enacted.

## 3. Fault baseline stays default-OFF

Confirmed `tests/fault-baseline.sh` is informational only: it is not registered in ctest
(`ctest -N` lists exactly 2 tests), is not referenced by `tests/verify-native.sh`, and the only
references to its name are its own file/comments. No default-ON wiring was introduced.

## 4. Coordination / next steps

- Asked phase-8 workers (javavm #453, flagbridge #454, objmodel #455) to ping for a final clean
  pass once their APIs are wired; they are adding new files only and are not touching the gate.
- Agreed with objmodel to write a new standalone memo file under `analysis/` rather than edit
  `analysis/appstate_fields.md` (objmodel's file).
- Flagged for the lead: `guest_app_shell.c:1135-1138` force-withholds the marker under
  `SOBER_REAL_APPSTATE_PATH` / `SOBER_SEED_*`, so a phase-8 genuine callback reached only via
  those opt-ins will still be withheld; this must be reconciled before `app-shell.sh` can go
  green under decision 013.

**Bottom line:** all five requested invariants PASS; `app-shell.sh` is red for an honest reason
(no genuine callback yet) plus a stale Phase-5 assertion; decision 013 is upheld; the lead
decision is drafted and awaiting a ruling.
