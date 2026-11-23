# n5 audit notes (worktree n5-audit, cores 6-7)

READ-ONLY. No device/runs. No questions. Verify every claim vs QUOTED raw lines.

## Secrets scan
COMMAND:
grep -rniE '(Cookie:[[:space:]]*[^"]{8,}|Authorization:|Bearer +[A-Za-z0-9._-]{12,}|eyJ[A-Za-z0-9_-]{10,}\.|-R[0-9]{4,}-[A-Za-z0-9]{8,}|[A-Za-z0-9._%+-]+@(roblox|gmail|yahoo|hotmail|outlook)[A-Za-z0-9.-]*)' /tmp/opencode/n5-*/ --include='*.md' --include='*.txt' --include='*.err' --include='*.log' | head -20
OUTPUT: 60 self-matches (ledger recorded commands + verification-notes in 4 worktrees); no credential values.

## Git
git ls-files | grep -iE '(arg0|\.pcap$|\.dump$|frida|logcat|phone|base\.apk|flag.*\.json|token|core\.[0-9])' -> (none).
n5 worktrees at 12e2407, own branches.

## Ordered goals
- harness: shim ring (tid, FILE*, caller RVA, __sF range), RET64 every fault, last 15 stderr lines, objdump libc, SOBER_BIONIC_SF_FULL ON Run D x3 ledger
- static-A: fatal helper 2ea6a10 format+callers; __sF layout/_lock/streams
- static-B: after-#552 calls + guest threads + 3rd N14 replay
- audit: quotes/ledger/secrets/SHA

Object OFF, no extension, no guest writes/fake returns/seeder/remaps. SHA 8215519f quoted per run.
Prior established fault: host libc _IO_fflush@@GLIBC_2.2.5+0x9f, NULL rdi, si_addr=0x8.

## Verification
Pending — agents starting.
