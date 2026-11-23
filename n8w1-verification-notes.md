# n8 wave1 audit notes (worktree n8-audit, cores 6-7)

MANDATORY. READ-ONLY. No device/runs. No questions. Verify every claim vs QUOTED raw lines.

## Secrets scan
COMMAND:
grep -rniE '(Cookie:[[:space:]]*[^"]{8,}|Authorization:|Bearer +[A-Za-z0-9._-]{12,}|eyJ[A-Za-z0-9_-]{10,}\.|-R[0-9]{4,}-[A-Za-z0-9]{8,}|[A-Za-z0-9._%+-]+@(roblox|gmail|yahoo|hotmail|outlook)[A-Za-z0-9.-]*)' /tmp/opencode/n8-*/ --include='*.md' --include='*.txt' --include='*.err' --include='*.log' | head -20
OUTPUT: 66 self-matches (ledger recorded commands + verification-notes in 3 worktrees); no credential values.

## Git
git ls-files | grep -iE '(arg0|\.pcap$|\.dump$|frida|logcat|phone|base\.apk|flag.*\.json|token|core\.[0-9])' -> (none).
n8 worktrees: audit/harness at 569dcb4, static-a at e82e5c2.

## Wave1 goals
- Decision: host-only replay genuine ctor3 0x1db9a40 in init_array order (after ctor0, before ctor3602), default OFF for A/B, ledger DEBT=1
- static-A FIRST: disassemble ctor3 globals/calls/getenv/syscall, polarities of je 23a1422/23a1465 and cmpb 239eb14, replay safety
- harness: A/B ctor3 OFF/ON x3 each, quote [0x7cdede0] before/after, HardAssert, faulting instr, N14_REPLAY 3/3
- audit: quotes, ledger with owner+removal for ctor3 replay, secrets, SHA, no captures

Object OFF, SF_FULL=1 (DEBT=1), TSD seed OFF, JNI_OnLoad-first ON. SHA banner quoted every run.
No guest writes/hand-set bytes/fake returns/seeder/remaps. Rate limit wait 5min x3 then BLOCKED.

## Verification
- static-A #701: all ctor3 disasm quotes verified (2 globals, 9 allocator calls, no env/thread/JNI,
  probe predicate); polarities verified (flag ZERO -> assert; al==0 skips DFLog, both converge at
  23a1465 message call); statically SAFE, needs ctor[2] replayed.
- harness #703: SHA+banner 6/6; ctor3 replay 1/1/1 ON, 0/0/0 OFF; `[N8_CTOR3] before/after
  0x7cdede0=0x00` flag unchanged; HardAssert 1/1/1 both arms, N14_REPLAY 3/3 0/0/0 both arms,
  same fault RIP guest+0x6caef67; rc=139 6/6. /dev/shm rc=126 build NOT credited. No benefit.
- Ledger 47: ctor3 replay host-only owner=harness + removal condition, DEBT=1.
- Secrets scan 67 self-matches; git no captures/tokens.
- Git index writes fail -> commit/push via /dev/shm tmpfs plumbing.

## Wave2 verification (task #706)
- static-A #705: all quotes verified — bl=1 needs 4 conditions; decisive 1dbe4fc is unbridged
  mimalloc-private-arena predicate that host glibc malloc never populates; 1dbdfba bridged to
  malloc_usable_size. So flag stays 0 by construction (runtime UNPROVEN by static-A).
- harness #704: SHA+banner 3/3; arm 4/6 bps (CMP2/STORE failed perf limit); CMP1 zero hits 3/3
  with final [0x7cdede0]=0x00 => je at 1db9a8d taken => usable-size low byte zero => first check
  zeroes flag. Independently confirms no-benefit A/B of wave1 and static-A construction argument.
- Secrets scan 68 self-matches; git no captures/tokens.
- No new ledger entry needed (entry 47 already has owner+removal); wave2 recorded in ledger prose.

## Wave3 verification (task #709)
- static-A #708: all quotes verified (mimalloc globals writers/readers, arena-init explicit host call,
  1dbe4fc tiny blast radius but forging nonzero return = semantic fake -> gate/DEBT policy). REPORT ONLY.
- harness #707: SHA+banner 3/3; [N8G AFTER_CTOR3]/[AT_FAULT] identical; audit recomputed bit41=43,
  ptidx=0x2b05c, p<low-bound=True; ptword=0 => two independent rejections by 1dbe4fc => flag stays 0.
- Secrets scan 69 self-matches; git no captures/tokens. No new ledger entry (entry 47 covers replay).
