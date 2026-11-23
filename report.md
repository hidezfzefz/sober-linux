# W32 #643 — harness (one page)

Worktree `/tmp/opencode/w32-harness`, branch w32-harness. Engine SHA `3e498d64`.
flock held; no device/guest writes/gate edits/seeder; seed OFF; JNI_OnLoad-first
ON; full JSON SHA verified. Every success claim quotes a raw line. The uploaded
doc under `/tmp/opencode/uploads` was NOT read (out of scope).
Evidence: `trace_{1,2,3}.err`, `nowatch_{1,2,3}.err`, `rund3.sh`.

## FAULTING INSTRUCTION FIRST — PROVEN
`guest+0x6ca7f95`, bytes `e8 b6 b3 d6 fb` = **`call 0x2a13350`**,
`si_addr = rsp-8` (stack overflow). Raw `nowatch_1.err`:
`[W30_FAULT] rip=0x00007fffef8e3f95 si_addr=0x00007fffff7fefc8 rsp=0x00007fffff7fefd0 rbp=0x00007fffff7ff0a0 insn=00000000000000e8 00000000000000b6 ...` (corrected to exact raw `rbp=0x00007fffff7ff0a0` by lead/audit — earlier `...090` was not verbatim)

## (a) HW exec breakpoints on the two +0x28 writers — PROVEN
`[W32_ARM] base=0x7fffe8c3c000 entry=0 obj=0 writer0=0 writer1=0` (all 3).
Run D ×3: `0x2a0ed45` **0 hits**, `0x2a1362f` **68 hits** every run
(`w0=0 w1=68 precede=0`). First hit, raw `trace_1.err`:
`[W32_WRITER 0x2a1362f] hit=1 tid=1606363 guest_RA=guest+0x0000000002a0ede0 entries_so_far=27 precedes_first_entry=0`
**No hit precedes the first `0x2a13350` entry** (`precedes_first_entry=1` count
= 0/0/0); all writer hits are on tid 1606363. Caller RAs observed:
`guest+0x2a0ede0`, `guest+0x2a0e598`, `guest+0x2a0e1d8`, plus `guest+0x2a0e840` and `guest+0x2a0f492` (two RAs omitted in the original list, restored by lead/audit — all 5 appear in the raw writer-hit lines).

## (b) Distinct obj values — PROVEN
`trace_1.err`: 37,547 `[W32_OBJ]` lines, first three:
`[W32_OBJ #1] tid=1606363 obj=0x7fffe85c2500 [obj+0x28]=0000000000000000`
`#2` and `#3` identical. Distinct obj count **1 per run**
(`grep -o obj=... | sort -u` → `obj=0x7fffe85c2500` only; runs 2-3 →
`obj=0x7fffe860c640`, also 1 each). **Same object across every entry within a
run**; address varies between runs (allocation layout), `[obj+0x28]=0` always.

## (c) Rung1 WITHOUT watchers — PROVEN + ledger
No watcher armed (`grep -c 'W32_ARM|W31_ARM|W21_WATCH|W21_LATCH_HIT|W28_PAIRS'
nowatch_1.err` = 0). Raw, all 3 runs:
`[W32_LATCH_RO] before=0 after=1`
`[checkpoint] rung1 PASS-BOUNDARY-REPLAY (DEBT=1)`
Supporting raw: `[avd-boundary] verified /tmp/opencode/w11-arg0.json bytes sha256=8215519f...`;
`[app-shell] AVD boundary pre: arg0_len=1533221 ... latch=0 ...`;
`[app-shell] AVD boundary nativeInitClientSettings 0x2359277 -> rc=0 ... latch=1 ...`;
`[flags-latch] route=none (inert; no guest call, latch=0, withhold=0)`. Host source has **no write** to `0x7cdd149` (only comparisons); the label-only
change accepts before=0/after=1 + SHA when no watcher is armed. Ledger entry
sent lead: "W32 watcher-free rung1: before=0 after=1, SHA 8215519f, no harness
write -> PASS-BOUNDARY-REPLAY (DEBT=1) label only; DEBT".

## ADDENDUM — 0x6ca800e SIG STOP hook (lead mid-wave add, read-only)
`[W32_CALL720_ARM] base=0x7fffe8c3c000 host=0x555555554000 fd_rc=0`. Raw hits:
```
[W32_CALL720] hit=1 tid=1612252 rdi=0x555555ae0bb8 rax=0x555555ae0420 [rax]=(nil) [rax+0x720]=0x555555582690 decode=host+0x000000000002e690
[W32_CALL720] hit=2 tid=1612252 rdi=0x555555ae0bb8 rax=0x555555ae0420 [rax]=(nil) [rax+0x720]=0x555555582690 decode=host+0x000000000002e690
[W32_CALL720] hit=3 tid=1612252 rdi=0x555555ae0bb8 rax=0x555555ae0420 [rax]=(nil) [rax+0x720]=0x555555582690 decode=host+0x000000000002e690
[W32_CALL720] hit=20 tid=1612252 rdi=0x555555ae0bb8 rax=0x7fffb8001f18 [rax]=(nil) [rax+0x720]=0x7fffef8e3f72 decode=guest+0x0000000006ca7f72
[W32_CALL720] hit=100 tid=1612252 rdi=0x555555ae0bb8 rax=0x7fffb8001f18 [rax]=(nil) [rax+0x720]=0x7fffef8e3f72 decode=guest+0x0000000006ca7f72
```
**PROVEN:** `[rax+0x720]` (JNI fn[224]) starts as host bridge `host+0x2e690`,
then at hit 20 becomes **`guest+0x6ca7f72`** — the *prologue of the same
function* containing `call [rax+0x720]` at `0x6ca800e` (static decode:
`6ca7f72: push rbp; mov rbp,rsp; sub rsp,0xc0`). Calling it re-enters that
function → infinite recursion → stack overflow. This is the recursion's
mechanism. No fix applied (read-only). Evidence `call720c.err`.

## NOT DONE
No ledger edit by me (entry sent); no guest bytes/gate edits/device; writer
`0x2a1362f` not proven to target this obj's +0x28 (UNPROVEN); why `0x2a0ed45`
never runs UNPROVEN (static); who writes fn[224] with the guest pointer
UNPROVEN (next step).

## First divergence / next step
`[obj+0x28]` stays 0 so the `0x6ca8014` recursion never terminates; the
37,547th entry faults at `call 0x2a13350`, `si_addr=rsp-8`. Mechanism is in the
addendum (fn[224] becomes a self-pointer). Next: static identify who writes
fn[224] with the guest pointer.
