# n7 wave1 audit notes (worktree n7-audit, cores 6-7)

MANDATORY. READ-ONLY. No device/runs. No questions. Verify every claim vs QUOTED raw lines.

## Secrets scan
COMMAND:
grep -rniE '(Cookie:[[:space:]]*[^"]{8,}|Authorization:|Bearer +[A-Za-z0-9._-]{12,}|eyJ[A-Za-z0-9_-]{10,}\.|-R[0-9]{4,}-[A-Za-z0-9]{8,}|[A-Za-z0-9._%+-]+@(roblox|gmail|yahoo|hotmail|outlook)[A-Za-z0-9.-]*)' /tmp/opencode/n7-*/ --include='*.md' --include='*.txt' --include='*.err' --include='*.log' | head -20
OUTPUT: 68 self-matches (ledger recorded commands + verification-notes in 4 worktrees); no credential values.

## Git
git ls-files | grep -iE '(arg0|\.pcap$|\.dump$|frida|logcat|phone|base\.apk|flag.*\.json|token|core\.[0-9])' -> (none).
n7 worktrees at fb68f0f, own branches.

## Wave1 goals
- fault = guest int3 0x6caef66 HardAssert mimalloc
- harness read-only: read [0x7cdd149]/[0x7cef650] at fault; exec bp ctor3602 entry + 0x2279a8f (hit/tid/order/value); Run D x3 SHA banner
- static-A: ctor3602 write condition + detection call + assert polarity
- static-B: 3618 ctor replay file:line, ctor3602 membership/order, real-boot-only steps, dl_iterate_phdr shim
- audit: quotes, ledger, secrets, SHA

SF_FULL=1 (DEBT=1, withhold unchanged), TSD seed OFF, JNI_OnLoad-first ON, DEX object OFF.
SHA banner + w11-arg0.json 8215519f quoted every run. Rate limit wait 5min x3 then BLOCKED.

## Wave2 verification (task #690)
- harness #689: SHA+banner 3/3; early arm `[N7B_ARM] … fds=4/5/6 rc=0/0/0` at log line 8 BEFORE
  ctor[3602] at 3653; hits entry=1/store=1/after=1 per run; `al=0x00` stored, readback `[0x7cef650]=0x00`.
- static-A #691: exhaustive scan 3 refs to 0x7cef650 (only write al=0), 2 to 0x7cdede0
  (ctor3 nonzero-capable but SKIPPED). BLOCKED: no nonzero writer for 0x7cef650 -> premise
  "genuine boot leaves it nonzero" RETRACTED/UNPROVEN. Ledger 46.
- static-B #692: real-boot load-to-AppStart table (10 rows) all quoted lines verified.
- Ledger entries 45 (early-arming closes entry 44 UNPROVEN) + 46 (BLOCKED).

## Wave3 verification (task #696)
- harness #695: SHA+banner 3/3; 3rd native ENTERED 1/1/1 `[N7C_THIRD 0x2383c18] hit=1 … rdx=rcx=shared_param_obj`
  (audit verified rdx==shared_param_obj=0x55ec63806e80); N14_REPLAY 3/3 count 0/0/0; order 3735 bp hit
  -> 3736 HardAssert -> 3737 W30_FAULT => assert fires INSIDE 3rd native. Refinement over w1/w2.
- static-A #694: call site source 1760-1761, opaque=param_obj 1736, log 1762 immediately after call;
  n14_1.err 3724/3726/3728/3730 verified; exact inner NULL-rdi fflush caller UNPROVEN.
- static-B #693: two distinct reporters (unwind 0x2ea6a10 vs HardAssert 0x6caed7c) — CONFLATION RETRACTED;
  size()==0 early normal return, no throw located; UNPROVEN items preserved.
- Secrets scan 71 self-matches; git no captures/tokens.
- Git index writes still fail -> commit/push via plumbing (fresh index built by update-index --index-info
  from git ls-tree -r HEAD with the two changed paths replaced; commit-tree; direct ref file write; push).
  NOTE: /dev/shm GIT_INDEX_FILE works; other paths intermittently fail.

## Wave4 verification (task #699)
- harness #698: SHA+banner 3/3; arm 4/5 inner bps (trap bp failed, perf slot limit) with
  fds=-1/... inner_rc=0/0/0/0/-1; hits FLOG_FFLUSH=2/UNWIND=0/ASSERT_DISPATCH=1/ASSERT_REPORT=1
  per run; order dispatch->reporter->FLog#1->HardAssert msg->FLog#2->W30_FAULT => HardAssert reporter
  (FLog 0x2e61d13) fires, unwind reporter 0x2ea6a94 NOT taken. Trap instruction UNPROVEN by bp.
- static-A #697: no direct size()==0->HardAssert edge (2383d42 jle -> canary -> 238450a ret);
  message call 23a146e (rsi=0x4c0d2b, edi=0), dispatcher 6caed61..77, table 0x721fbd0; reach via
  printer 239d215 -> 239eb14 flag 0x7cdede0 -> 23a1424 -> call; entry edge to 239d215 UNPROVEN.
- static-B #700: index clamped 0..10 selects tag in rdi, message is rsi from call site — RETRACTED
  'message table' framing; table contents UNPROVEN (relocation-filled). Verified by direct objdump.
- Secrets scan 72 self-matches; git no captures/tokens.
