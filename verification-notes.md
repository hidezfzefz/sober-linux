# W29 audit verification notes (worktree w29-audit)

Engine SHA `3e498d64`. No device, no runs by audit. Faulting instruction named first.

## Secrets scan (command + output)
COMMAND: `grep -rniE '(Cookie:[[:space:]]*[^"]{8,}|Authorization:|Bearer +[A-Za-z0-9._-]{12,}|eyJ[A-Za-z0-9_-]{10,}\.|-R[0-9]{4,}-[A-Za-z0-9]{8,}|[A-Za-z0-9._%+-]+@(roblox|gmail|yahoo|hotmail|outlook)[A-Za-z0-9.-]*)' /tmp/opencode/w29-*/ --include='*.md' --include='*.txt' --include='*.err' --include='*.log' | head -20`
OUTPUT: 3 matches, all the SAME self-match — the ledger's own recorded w28 secrets-scan
command line (in each of the 3 worktree copies of FABRICATION-LEDGER.md:432). No credential values.

## Git / branches
`git ls-files | grep -iE '(w2[0-9]-|arg0|\.pcap$|\.dump$|frida|logcat|phone|base\.apk)'` →
(no capture/phone/logcat files tracked). Worktrees w29-audit/w29-static/w29-harness each on own branch.

## Agent claim verification

### static #633 — VERIFIED (PROVEN)
Evidence `/tmp/opencode/w29-static/{report.md,dis_*.txt,symbols.txt,capture_missing_natives.txt}`.
- FAULTING INSTR 0x6ca229c = call *0x548(%rax) (bytes ff9005480000, verified earlier); stack RA
  0x231555c NOT fault. (Prior-wave fact, restated; no new evidence needed.)
- 2 guest setspecific writers of key 0x72a1b08: 0x2ea656e `call pthread_setspecific@plt` in
  accessor 0x2ea6440 (fresh calloc state, +0=1 @0x2ea6555), 0x2ea6695 `jmp pthread_setspecific@plt`
  in dtor 0x2ea6670 — verified in dis_tls_key3.txt / dis_tls_destructor.txt. 0x2ea61b6 is a
  different key (excluded) — dis_key_other.txt. Key created @0x2ea664f by once cb 0x2ea6640.
  Threads UNPROVEN. JNI_OnLoad no setspecific (PROVEN).
- Missing natives: SetPreferencesFile 0x246515d (capture #613 "prefs"), InitFlags 0x2465210
  (capture #614 flag String[]) — symbols.txt confirms both RVAs; harness 0x2465210 opt-in,
  0x246515d never. PROVEN.

## Pending
harness — no formal report at time of writing.

## Seeded-state inventory (from ledger + source, to be finalized)
- Modified inputs: SOBER_AVD_BOUNDARY_REPLAY 1,533,221B JSON (ledger #1/#2/#3); SOBER_AVD_BOUNDARY_PATH
  78B w15-flags-1.json (ledger #9).
- TSD seed: SOBER_STUB_TSD_SEED / approved host TSD seed key3 non-NULL (ledger #16).
- Remaining seeder slots: 0x72a2f48..0x72a3050 still seeded; only 0x72a3058 excluded (ledger #13).
- Host value fixes: distinct base dirs (#19), empty channel (#20), captured-order replay (#21).
- Default-off diag knobs (no ledger line each): many SOBER_* in withhold chain.
