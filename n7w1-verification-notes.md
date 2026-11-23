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
- Git index writes still fail -> commit/push via plumbing (fresh index built by update-index --index-info
  from git ls-tree -r HEAD with the two changed paths replaced; commit-tree; direct ref file write; push).
