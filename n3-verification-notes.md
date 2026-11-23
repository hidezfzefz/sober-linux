# n3 audit notes (worktree n3-audit, cores 6-7)

READ-ONLY. No device/runs. No questions. Verify every claim vs QUOTED raw lines.

## Secrets scan
COMMAND:
grep -rniE '(Cookie:[[:space:]]*[^"]{8,}|Authorization:|Bearer +[A-Za-z0-9._-]{12,}|eyJ[A-Za-z0-9_-]{10,}\.|-R[0-9]{4,}-[A-Za-z0-9]{8,}|[A-Za-z0-9._%+-]+@(roblox|gmail|yahoo|hotmail|outlook)[A-Za-z0-9.-]*)' /tmp/opencode/n3-*/ --include='*.md' --include='*.txt' --include='*.err' --include='*.log' | head -20
OUTPUT: 44 self-matches (ledger recorded commands + verification-notes in 4 worktrees); no credential values.

## Git
git ls-files | grep -iE '(arg0|\.pcap$|\.dump$|frida|logcat|phone|base\.apk|flag.*\.json|token|core\.[0-9])' -> (none).
n3 worktrees at 225147e, own branches.

## Ordered goals
- static-A: faulting `mov rax,[rdi+8]` function RVA/bytes, path from #552, rdi source, startup native, call order vs w12
- harness: fault instr first (RIP/bytes/si_addr), #552 entry regs + inner rdi source, JNI ring calls on object (size/get/GetObjectClass/GetMethodID); zero calls = never read list
- static-B: rbx at 6c8e512 origin, table at 6ca8008 (copy vs original)
- audit: quotes, ledger, secrets, no captures

Object OFF, no extension, no guest writes/fake returns/seeder/gate edits. SHA 8215519f quoted per run.

## Verification
Pending — agents starting.
