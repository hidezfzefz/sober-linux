# n2 Step 4 audit notes (worktree n2-audit, cores 6-7)

No device/runs. No questions. Verify every claim against QUOTED raw lines.

## Secrets scan
COMMAND:
grep -rniE '(Cookie:[[:space:]]*[^"]{8,}|Authorization:|Bearer +[A-Za-z0-9._-]{12,}|eyJ[A-Za-z0-9_-]{10,}\.|-R[0-9]{4,}-[A-Za-z0-9]{8,}|[A-Za-z0-9._%+-]+@(roblox|gmail|yahoo|hotmail|outlook)[A-Za-z0-9.-]*)' /tmp/opencode/n2-*/ --include='*.md' --include='*.txt' --include='*.err' --include='*.log' | head -20
OUTPUT: 36 self-matches (ledger recorded scan commands + verification-notes in 4 worktrees); no credential values.

## Git
git ls-files | grep -iE '(arg0|\.pcap$|\.dump$|frida|logcat|phone|base\.apk|flag.*\.json|token|core\.[0-9])' -> (none).
n2 worktrees at f8a0069, own branches.

## Ordered steps
1. static-B + harness read-only table test (r14@6c8e4d3 vs bridge table, [bridge+0x720] vs base+0x6ca7f72, rax@6ca800e; report in-place patch immediately)
2. static-A DEX type/fields/methods/call site of #552 arg + guest JNIEnv call list
3. harness builds DEX-typed object within limits, Run D x3 ON, baseline OFF
4. audit verify (this task)

## Verification
Pending — agents still in step 1.

Requirements to check:
- ledger owner+removal condition for any HARNESS-JAVA-OBJECT
- SHA quoted per run (w11-arg0.json 8215519f)
- retract unquoted success claims
- hard-fail/stop limits respected
