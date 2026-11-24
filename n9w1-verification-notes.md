# n9 wave1 audit notes (worktree n9-audit, cores 6-7)

MANDATORY. READ-ONLY. No device/runs. No questions. Verify every claim vs QUOTED raw lines.

## Secrets scan
COMMAND:
grep -rniE '(Cookie:[[:space:]]*[^"]{8,}|Authorization:|Bearer +[A-Za-z0-9._-]{12,}|eyJ[A-Za-z0-9_-]{10,}\.|-R[0-9]{4,}-[A-Za-z0-9]{8,}|[A-Za-z0-9._%+-]+@(roblox|gmail|yahoo|hotmail|outlook)[A-Za-z0-9.-]*)' /tmp/opencode/n9-*/ --include='*.md' --include='*.txt' --include='*.err' --include='*.log' | head -20
OUTPUT: 81 self-matches (ledger recorded commands + verification-notes in 3 worktrees); no credential values.

## Git
git ls-files | grep -iE '(arg0|\.pcap$|\.dump$|frida|logcat|phone|base\.apk|flag.*\.json|token|core\.[0-9])' -> (none).
n9 worktrees at 0e6539a.

## CRITICAL: ledger-cited file existence check (task requirement)
Extraction: grep -oE '/tmp/opencode/[A-Za-z0-9._/-]+' analysis/FABRICATION-LEDGER.md | sort -u
=> 46 unique absolute paths cited; **0 exist** (all 46 missing after n8 disk emergency cleanup).
Named files (8): /tmp/opencode/w11-arg0.json, w15-flags-1.json, auth-base.apk,
w25-phone/base.apk, w18-audit/audit-notes.md, w19-harness/report.md,
w29-harness/report.md, w30-static/report.md.
Directories/globs (38): all n1-n8 and w18-w32 worktree paths (removed), /tmp/opencode/uploads.
No surviving copies of w11-arg0.json / auth-base.apk / w15-flags-1.json anywhere under /tmp or /home/user.
Relative source citations (src/..., analysis/...) DO exist in n9-audit worktree.
Quoted raw lines survive inline in ledger text, but original files cannot be re-opened.
VERDICT: FAIL — the check cannot be confirmed; files were deleted. Must report to lead.

## Wave1 goals
- static-A: callers of 1dbe4fc + pointer origin; list 8 patched bridge RVAs
- harness: read-only pointer+caller RVA, arena bounds, guest mmap page source, Run D x3 fault first
- audit: quotes, ledger, secrets, SHA, no ledger-cited file deleted

Object OFF, SF_FULL=1 DEBT=1, ctor3 OFF, seed OFF, JNI_OnLoad-first ON. SHA banner quoted every run.
No guest writes/hand-set bytes/fake returns/probe bridge. Push own (plumbing if index fails).

## Verification (task #715)
- Ledger-cited file check: FAIL — all 46 unique absolute paths cited are missing; 8 named files
  incl. w11-arg0.json, auth-base.apk, w15-flags-1.json. No copies survive. Entry 49.
- SHA per run: NOT MET — n9off_1/2/3 have 0 'verified' lines, all show 'open failed:
  /tmp/opencode/w11-arg0.json'. Harness correctly RETRACTED expectation; only provenance
  constant + N6 banner present. Guest engine SHA copy survives at basalt payload path (not arg0 SHA).
- Earlier n9_*.err rc=3 (177B, fopen guest No such file) NOT credited; engine copied to n9bin and rerun.
- static-A #713 all quotes verified (2 ctor3 callers of 1dbe4fc; 8 allocator bridge RVAs, none page-source).
- harness #714 all quotes verified: int3 0x6caef66 (RIP 0x6caef67), all mimalloc globals 0 with
  allocator patch OFF, fatal message changed to FatalRuntimeError, 1dbe4fc 0 hits, STOP honored.
- Secrets scan 81 self-matches; git no captures/tokens.
- Git index writes fail -> commit/push via /dev/shm tmpfs plumbing.

## Wave2 verification (task #718)
- Re-extracted ledger paths: 49 unique, 27 restored/exist, 22 missing (18 glob/dir, 4 named files:
  w11-arg0.json, w18-audit/audit-notes.md, w19-harness/report.md, w25-phone/base.apk).
- Restored verified: auth-base.apk 97621690B SHA 114a6f8e, w15-flags-1.json 78B SHA 4d50a0ec,
  guest engine restored. No 1533221B arg0 file anywhere.
- Marked UNPROVEN (NOT deleted): entry 22 leaked-credit (w22-w28 harness logs gone), line 419 arg0
  size/SHA, w18 line 74 notes, w19 line 149 report/scripts; w25 phone apk already recorded cleaned.
- Wave1 quotes re-verified; SHA 0 verified lines in n9off runs (verified=0/provenance=1/openfail=1),
  diagnostic-only per lead. Secrets 112 self-matches; git no captures/tokens.
- static-B #717 verified: 8 allocator bridge RVAs, install_bridge trampoline, default OFF,
  1dbe4fc NEVER bridged; message-switch RETRACTED (both arms 3717 FatalRuntimeError); flags-latch
  23862b0/23862b7 je 2386588/2386594 call 6caed5d verified.

## Wave3 verification (task #720)
- harness #721 all quotes verified from surviving logs (no new runs): fault guest+0x6caef67 int3
  6caef66 6/6; [N7_FAULT_BYTES] latch 0x7cdd149=0 AND 0x7cef650=0 at line 3720 in all 6 logs
  (tids 415380/415922/416825 ON, 422575/423081/423844 OFF). LATCH=0 BOTH configs.
- n8-harness dir + all n8*_*.err GONE -> earlier latch=1 / mimalloc-HardAssert claims RETRACTED
  as unverifiable from surviving evidence (inline quotes remain, nothing deleted).
- Fatal msg 6/6 TaskScheduler FatalRuntimeError at 3717; no mimalloc HardAssert in these logs.
- SHA 0 verified lines, openfailed 6/6 -> DIAGNOSTIC ONLY, NO rung credit.
- Secrets scan 113 self-matches; git no captures/tokens; no file deletions by audit.
