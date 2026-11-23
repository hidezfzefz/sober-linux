# n4 audit notes (worktree n4-audit, cores 6-7)

READ-ONLY. No device/runs. No questions. Verify every claim vs QUOTED raw lines.

## Secrets scan
COMMAND:
grep -rniE '(Cookie:[[:space:]]*[^"]{8,}|Authorization:|Bearer +[A-Za-z0-9._-]{12,}|eyJ[A-Za-z0-9_-]{10,}\.|-R[0-9]{4,}-[A-Za-z0-9]{8,}|[A-Za-z0-9._%+-]+@(roblox|gmail|yahoo|hotmail|outlook)[A-Za-z0-9.-]*)' /tmp/opencode/n4-*/ --include='*.md' --include='*.txt' --include='*.err' --include='*.log' | head -20
OUTPUT: 52 self-matches (ledger recorded commands + verification-notes in 4 worktrees); no credential values.

## Git
git ls-files | grep -iE '(arg0|\.pcap$|\.dump$|frida|logcat|phone|base\.apk|flag.*\.json|token|core\.[0-9])' -> (none).
n4 worktrees at 59690e1, own branches.

## Ordered goals
- harness first: host-only maps-aware crash handler (open/read/write only), print maps lines for RIP + first 3 RET64 return addresses; Run D x3 object OFF; fault instr + first guest RVA; resolve symbol via nm/objdump
- static-A: guest call site slot/symbol/rdi source/shim
- static-B: containing fn + caller chain to #552 + after size()=0
- audit: quotes, ledger, secrets, no captures, SHA

Object OFF, no extension, no guest writes/fake returns/seeder/remaps. SHA 8215519f quoted per run.

## Verification
Pending — agents starting.
