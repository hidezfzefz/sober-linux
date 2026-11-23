# n8w1 #701 — ctor3 safety + HardAssert polarities (BEFORE run)

Read-only; cores 2-3; no runs/device. Every claim quotes a raw line.
Evidence: `dis_ctor3_full.txt`, `raw_polarities.txt`.

Faulting instruction first: guest `0x6caef66: int3` (HardAssert), bytes `cc`.

## (1) ctor3 `0x1db9a40` — complete inventory — PROVEN

Globals written (exactly two, quoted):
> `1db9a4b: movq $0x20,0x7cdedd8`   (usable-size probe input = 0x20)
> `1db9acf: mov %bl,0x7cdede0`      (probe result byte)

Every call (nine, all internal guest allocator helpers, quoted):
> `1db9a5b: call 1dbad62` with `1db9a56: mov $0x20,%edi`  (allocate 0x20)
> `1db9a66: call 1dbe4fc` → `1db9a6b: mov %eax,%r15d`     (usable size #1)
> `1db9a71: call 1dbdfba` → `1db9a76: mov %rax,%r14`      (end ptr #1)
> `1db9a83: call 1dbe559`                                  (free #1)
> `1db9a9b: call 1dbab02` with `rdi=[0x7cdedd8]`          (allocate #2)
> `1db9aa6: call 1dbe4fc` → `1db9aab: mov %eax,%ebx`      (usable size #2)
> `1db9ab0: call 1dbdfba` → `1db9ab5: mov %rax,%r14`      (end ptr #2)
> `1db9ac2: call 1dbe5be`                                  (free #2)
then `1db9add: ret`.

**No `getenv`, no syscall, no JNIEnv, no pthread/thread creation** — the full
function body is quoted above and contains none of those. Probe logic quoted:
`1db9a88: xor %ebx,%ebx`; `1db9a8a: test %r15b,%r15b` /
`1db9a8d: je 1db9acf` (result 0 if first query returned 0);
`1db9a8f: cmp %r12,%r14` / `1db9a92: jb 1db9acf` (result 0 if end ptr < base);
second query `1db9aa6/1db9aab`, `1db9ac7: cmp %r12,%r14` /
`1db9aca: setae %al` / `1db9acd: and %al,%bl`, then the store. So the probe is
a pure allocator usable-size/geometry check, matching `fflag_default.c:111-113`
("mimalloc usable-size/allocator feature probe").

## (2) Polarities — PROVEN

- `0x239eb14: cmpb $0,0x7cdede0` (in printer `0x239ead3`): if the byte is
  **ZERO**, `0x239eb1b: je 0x23a1422` is taken → assertion branch. If NON-ZERO,
  fall through to `0x239eb21: test %al,%al` (version check result).
- `0x23a1422: test %al,%al` / `0x23a1424: je 0x23a1465`: if `%al == 0`
  → **jump to `0x23a1465`**, skipping the log block; if `%al != 0` → log
  `"[DFLog::Mimalloc] Mimalloc integration is NOT detected"` (`0x23a1445: lea
  #3a3078,%rcx`, `0x23a1460: call 232de10`) and then **fall through to the
  same `0x23a1465`**. Both polarities converge there.
- `0x23a1465` is **not a branch** — it is `lea … #0x4c0d2b,%rsi`, the message
  pointer for the HardAssert call; the branch that targets it is the `je` at
  `0x23a1424`. From `0x23a1465` → `0x23a146c: xor %edi,%edi` →
  `0x23a146e: call 0x6caed5d` (dispatcher) → reporter `0x6caed7c` →
  `0x6caef4f`/`0x6caef58` → `0x6caef66: int3`.

So: flag ZERO at `0x239eb14` selects the assert path; `al==0` at `0x23a1422`
skips only the DFLog line, and **either** polarity of that `je` ends at the
HardAssert dispatcher.

## (3) Replay safety — PROVEN / UNPROVEN

SAFE by static evidence: ctor3 uses only the guest allocator, allocates/frees
two 0x20-byte blocks, writes two guest BSS globals, needs **no env var** (no
getenv), **no thread** (no pthread), and makes **no JNI/fake-return calls**.
Prerequisite: the guest allocator must be ready — ctor3's first call is
`0x1dbad62` (guest malloc) at `0x1db9a5b`; the harness already replays ctor[2]
which initializes the arena (prior wave log `ctor[2] rva=0x1dbe2b1` with
`arena_table` set). Replaying ctor3 in init_array order after ctor0 and before
ctor3602 is therefore consistent with its allocator dependency, and it sets
`0x7cdede0` through genuine guest code (not a hand-set byte).

UNPROVEN: that the two allocations actually succeed in the harness (no run
performed in this wave); that `0x7cdede0` becomes non-zero (the probe result
depends on the real allocator's usable-size geometry). Neither is a static
safety blocker, but both need the harness A/B run.

## What NOT done
No apply/build, runs/gdb/perf, device/AVD/Frida/phone, new captures, guest
writes, gate edits, seeder, fake returns, remaps, or hand-setting `0x7cdede0`,
no credentials, no captures/logs/tokens in git.

## First divergence + next step
First divergence: harness leaves `0x7cdede0` zero because ctor3 is skipped;
real boot runs it. Next: harness A/B ctor3 OFF/ON x3, quoting `[0x7cdede0]`
before/after, the HardAssert, and the faulting instruction.
