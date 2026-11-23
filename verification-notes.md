# W32 audit verification notes (worktree w32-audit)

Engine SHA `3e498d64`. QUOTE rule: every success claim must quote a raw line.
Constraint: `/tmp/opencode/uploads` is OUT OF SCOPE — not read, not scanned.

## Secrets scan (command + output)
COMMAND:
grep -rniE '(Cookie:[[:space:]]*[^"]{8,}|Authorization:|Bearer +[A-Za-z0-9._-]{12,}|eyJ[A-Za-z0-9_-]{10,}\.|-R[0-9]{4,}-[A-Za-z0-9]{8,}|[A-Za-z0-9._%+-]+@(roblox|gmail|yahoo|hotmail|outlook)[A-Za-z0-9.-]*)' /tmp/opencode/w32-*/ --include='*.md' --include='*.txt' --include='*.err' --include='*.log' | head -20
OUTPUT: 6 self-matches (ledger's own recorded scan command, 2 per worktree × 3); no credential values.
(Glob /tmp/opencode/w32-*/ excludes uploads.)

## Git
git ls-files | grep -iE '(arg0|\.pcap$|\.dump$|frida|logcat|phone|base\.apk)' -> (no capture/phone files tracked).
w32 worktrees each on own branch: w32-audit, w32-static, w32-harness.

## Agent claim verification
Pending — both w32 report.md files are stale w31 copies at 04:19; agents still working.
