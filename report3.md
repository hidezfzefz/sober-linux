# n10w3 #733 static-B — HardAssert fault chain vs n9 TaskScheduler (READ-ONLY)

FAULTING INSTRUCTION FIRST: guest `int3 0x6caef66` (reported RIP `0x6caef67`, bytes `89 c0 e8 62`), SIGTRAP signal=5, rc=139 6/6. All runs MODIFIED INPUT (`w15-flags-1.json`), NO rung credit. DELETE NOTHING; no network/runs/writes.

## PROVEN — n10 chain (raw log + raw disasm)

1. **Latch hit in boundary native** (`n10-harness/report_n10w1b.md:32-34`):
```
[N10_LATCH_ARM] base=... fd=4 rc=0
[N10_LATCH_HIT 0x2eb7520] hit=1 tid=511649 guest_RA=guest+0x0000000002359342
```
`0x2359342` is the AVD-reference caller RA: return address of the call from `nativeInitClientSettings 0x2359277` → helper `0x2eb7155` → `movb $1,0x7cdd149` at `0x2eb7520` (static-A #719; surviving `analysis/CHECKPOINT-LADDER.md:14`: `LATCH_WR@0x2eb7520` (RA `base+0x2359342`)). One hit per run, 6/6.
2. **Fault bytes** (`report_n10w1b.md:23-24`): `[0x7cdd149]=0x0000000000000001 [0x7cef650]=0x0000000000000000` — latch now 1 (modified input makes the boundary replay run), flag still 0. Log: `RBXCRASH: HardAssert (Expect to have mimalloc working, but it was not detected!)`.
3. **HardAssert message + index** (n7 dis, surviving report3.md of n7):
```
23a1465: lea -0x1ee0741(%rip),%rsi  # 4c0d2b  ; "Expect to have mimalloc working, but it was not detected!"
23a146c: xor %edi,%edi                        ; INDEX 0
23a146e: call 6caed5d                         ; dispatcher
```
4. **Dispatcher**: `6caed61 cmp $0xa,%edi; 6caed64 mov $0xa,%eax; 6caed69 cmovb %edi,%eax; 6caed6c lea #721fbd0,%rcx; 6caed73 mov (%rcx,%rax,8),%rdi; 6caed77 call 6caed7c` — clamped index 0..10, table entry → `%rdi` (tag), message stays `%rsi`.
5. **Reporter → int3**: `6caef4f cmpb $0x0,byte[7cdd149]; 6caef56 je 6caef66` — **passes** (latch=1); `6caef58 cmpb $0x0,byte[7cef650]; 6caef5f je 6caef66` — **fails** (flag=0) → `6caef66: cc int3`. So with latch=1 the first guard is satisfied and the mimalloc flag guard fires.

## PROVEN — role of `[0x7cdede0]`

Surviving `analysis/A05-skipped-ctors.md:116-131,193` (raw):
```
1db9acf: mov  %bl,0x7cdede0         ; bool: "usable_size >= requested"
239eb14: cmpb $0,0x7cdede0
239eb1b: je 0x23a1422
  ; bool=0 -> DFLog 0x3a3078 len 0x36: "[DFLog::Mimalloc] Mimalloc integration is NOT detected"
```
`0x7cdede0` is the **mimalloc usable-size self-test probe bool**, written by skipped ctor[3] `0x1db9a40` (`A05:46`: writes `0x7cdedd8`=`0x20`, `0x7cdede0`=probe bool; calls bridges `0x1dbad62`,`0x1dbe4fc`,`0x1dbdfba`,`0x1dbe559`). Its **sole consumer** is `0x239eb14`, which selects a DFLog string. `A05:193`: "only reader `0x239eb14` selects a DFLog message; zero functional effect". With ctor3 skipped by decision 008 (std ranges `{2,3},{5,3618}`), the byte stays 0 → "Mimalloc integration is NOT detected" → path through `0x23a1424 je 0x23a1465` to the HardAssert above. Static-A #683 likewise recorded "mimalloc probe=DFLog-only".

## PROVEN — comparison with n9 TaskScheduler path

n9 (input missing, latch=0, `report_n9-harness/report_n9w3.md`): fault bytes `[0x7cdd149]=0` → guard fails at the *first* byte:
```
23862b0: cmpb $0x0,0x7cdd149
23862b7: je   2386588
2386588: lea  0x36a389(%rip),%rsi   ; "Can't initialize the TaskScheduler before flags have been loaded"
238658f: mov  $0x6,%edi             ; INDEX 6
2386594: call 0x6caed5d             ; SAME dispatcher
```
Both paths converge on dispatcher `0x6caed5d` → reporter `0x6caed7c` → `int3 0x6caef66`; only the message (`0x4c0d2b` vs `0x36a389`) and index (0 vs 6) differ. Which one fires is decided by the latch state at fault: latch=1 + flag=0 → mimalloc (n10); latch=0 → TaskScheduler (n9).

## RETRACTED / UNPROVEN

- RETRACTED: treating the two messages as different crash sites — one reporter/int3, two guard entry points.
- UNPROVEN: table `0x721fbd0` entry values (relocation-filled; `engine_full.dis` gone); exact runtime value of `0x7cdede0` at fault (no new reads); whether ctor3's probe has any effect beyond the DFLog string.
- NOT DONE: no runs/device/captures/guest writes/gate/seeder/fake returns/probe bridge/path remaps; no captures/tokens in git.
