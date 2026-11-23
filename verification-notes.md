# Overnight n1-1 audit notes (worktree n1-audit)

Cores 6-7 only (taskset -c 6,7). No device/runs. No questions.

## Secrets scan
COMMAND:
grep -rniE '(Cookie:[[:space:]]*[^"]{8,}|Authorization:|Bearer +[A-Za-z0-9._-]{12,}|eyJ[A-Za-z0-9_-]{10,}\.|-R[0-9]{4,}-[A-Za-z0-9]{8,}|[A-Za-z0-9._%+-]+@(roblox|gmail|yahoo|hotmail|outlook)[A-Za-z0-9.-]*)' /tmp/opencode/n1-*/ --include='*.md' --include='*.txt' --include='*.err' --include='*.log' | head -20
OUTPUT: 16 self-matches (ledger recorded commands ×3 + verification-notes ×1, per 4 worktrees); no credential values.

## Git state
- flag*.json / token / .pcap / .dump / w11-arg0 / core.* tracked: NONE.
- Tracked .log files: 18 `appData/logs/*_last.log` (780B-1.5K each, small FLog snippets) + `build/Testing/Temporary/LastTest.log`.
  All pre-existing from commit afed56d ("Phase 1-10..."); NOT introduced by n1-1. Credential scan inside them: none.
  Report accurately: no NEW captures/flag-json/tokens; pre-existing benign FLog fixtures present.
- n1 worktrees at 702fae9, each own branch; my worktree clean so far.

## SHA-per-run requirement
Every agent run must quote `[avd-boundary] verified ... sha256=8215519f...` (or be MODIFIED-INPUT).

## Agent verification
Pending — reports are stale copies at 04:31/04:32; agents working.
