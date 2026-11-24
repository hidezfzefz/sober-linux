# n9w2 #716 — TaskScheduler fatal reporter vs HardAssert dispatcher

Read-only; cores 2-3; no runs/device. Every claim quotes a raw line.
Engine SHA `3e498d649421115b` (basalt copy). Evidence: `raw_fatal_reporter.txt`,
`raw_pred_2386588.txt`, `raw_latch_test.txt`, `raw_dispatcher_reporter.txt`,
`raw_harness_report.txt`, `raw_flags_latch_src.txt`.

Faulting instruction first: guest `0x6caef66: int3` (HardAssert), bytes `cc`;
reported RIP after it `0x6caef67`, `signal=5`, rc=139 (harness `report_n9.md`).

## (1) The TaskScheduler fatal reporter — PROVEN

Single reference to the string (file offset == vaddr, LOAD#0 `offset==vaddr`):
> string `strings -t x`: `36a389 Can't initialize the TaskScheduler before flags have been loaded`
> `2386588: lea -0x201c206(%rip),%rsi # 36a389`
> `238658f: mov $0x6,%edi`          (dispatcher table index **6**)
> `2386594: call 6caed5d`           (HardAssert dispatcher)

Only ONE predecessor reaches it (`raw_pred_2386588.txt`):
> `23862b0: cmpb $0x0,0x7cdd149`    (flags-loaded LATCH)
> `23862b7: je 2386588`             (byte == 0 → fatal)

So the tested global is **`0x7cdd149`**, the same latch the HardAssert reporter
tests at `0x6caef4f`. The fatal site is inside helper `0x2385fa6`
(`2385fa6: push %rbp ... sub $0x128,%rsp`), called from `23a1ae6`, `23a4a2f`,
`23a4b61`, `2519454`, `2519529` (all within the native symbol).

## (2) Relation to HardAssert dispatcher `0x6caed5d` — PROVEN

Both fatals go through the **same dispatcher**, differing only in the index:

| fatal | call site | message (RSI) | index (EDI) |
|---|---|---|---|
| TaskScheduler | `0x2386588` | rodata `0x36a389` "Can't initialize…" | **6** |
| mimalloc | `0x23a146e` | rodata `0x4c0d2b` "Expect to have mimalloc working…" | **0** (`23a146c: xor %edi,%edi`) |

Dispatcher quoted:
> `6caed61: cmp $0xa,%edi` / `6caed64: mov $0xa,%eax` / `6caed69: cmovb %edi,%eax`
> `6caed6c: lea #0x721fbd0,%rcx` / `6caed73: mov (%rcx,%rax,8),%rdi`
> `6caed77: call 6caed7c`          (reporter)

Reporter quoted:
> `6caef4f: cmpb $0,0x7cdd149` / `6caef56: je 6caef66`
> `6caef58: cmpb $0,0x7cef650` / `6caef5f: je 6caef66`
> `6caef61: call abort@plt` / `6caef66: int3`

So index 6 vs 0 selects a tag at table `0x721fbd0`, but **both** converge on
reporter `0x6caed7c` and the `int3`. The message text is passed in RSI by the
call site. The mimalloc path additionally tests `0x7cdede0`
(`239eb14: cmpb $0,0x7cdede0` → `239eb1b: je 23a1422`).

## (3) Allocator patch ON/OFF fatal message — CLAIM RETRACTED

**RETRACTION (after static-B #717 / audit #718):** I verified directly that
BOTH arms emit the SAME fatal at line 3717 — `n9_1.err:3717` and
`n9off_1.err:3717` both read `RBXCRASH: FatalRuntimeError (Can't initialize the
TaskScheduler before flags have been loaded)`. The harness `report_n9.md` phrase
"instead of the earlier `HardAssert (Expect to have mimalloc working...)`"
compared against EARLIER n7/n8 runs in different configurations, NOT a controlled
n9 A/B. So there is **no proven ON/OFF message switch**; the static explanation
below is RETRACTED as a causal claim. What remains PROVEN is the reporter mapping
in sections (1)-(2); the observed TaskScheduler fatal (both arms) is consistent
with latch `0x7cdd149` being zero in both, but the byte value is UNPROVEN.

### Original (retracted) claim, kept for the record — PROVEN (harness)

Harness `report_n9.md` (allocator patch OFF, default; `1dbe4fc` NOT bridged):
> `[N9_ALLOC_PATCH] OFF (default); guest allocator unpatched; 1dbe4fc NOT bridged` (3/3)
> `[N8G AT_FAULT] [7cd8ff8]=0x0 [7cd9048]=0x0 [7cd9040]=0x0 alloc=0x0` (3/3) — all three mimalloc globals ZERO (arena init was via the host bridge path, now disabled).
> `RBXCRASH: FatalRuntimeError (Can't initialize the TaskScheduler before flags have been loaded)` (3/3)
> — "instead of the earlier `HardAssert (Expect to have mimalloc working...)`"
> The `1dbe4fc` bp had **0 hits** in 3/3 with ctor3 OFF — no pointer tested.

**Static explanation (PROVEN chain, runtime latch value UNPROVEN without a run):**
- Patch **OFF** → no host arena init → mimalloc globals zero → boot faults early,
  before flags are loaded → latch `0x7cdd149` remains 0 → `23862b7: je 2386588`
  taken → TaskScheduler fatal (index 6).
- Patch **ON** (earlier runs) → arena init via bridge → boot reaches flags-loaded
  writer (`movb $0x1,0x7cdd149` at e.g. `56d31a3`) → latch = 1 → TaskScheduler
  `je` NOT taken → execution continues to the mimalloc assert, where
  `0x7cdede0`/`0x7cef650` are zero → `23a146e: call 6caed5d` (index 0) →
  "Expect to have mimalloc working…" HardAssert.

Source corroboration (`guest_app_shell.c:1210-1212`): "T2-5 flags-loaded latch
0x7cdd149. The module uses genuine shipped guest code only (no byte writes)…"

UNPROVEN: the actual latch byte value at the fault in each configuration (harness
did not dump `0x7cdd149` in this wave); the causal explanation is the unique
static path consistent with the two observed fatal messages.

## What NOT done
No apply/build/patch/bridge, runs/gdb/perf, device, new captures, guest writes,
gate edits, seeder, fake JNI returns, remaps, or hand-setting bytes. No
`1dbe4fc` bridge. No credentials, no captures/logs/tokens in git.

## First divergence + next
First divergence: allocator patch OFF faults before flags load (latch 0), so the
TaskScheduler fatal (index 6) preempts the mimalloc HardAssert (index 0); both
share dispatcher `0x6caed5d`/reporter `0x6caed7c`/`int3 0x6caef66`. Next:
harness dumps `[0x7cdd149]` at fault for both arms to confirm the latch polarity.
