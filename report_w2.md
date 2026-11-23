# n7w2 #691 — exhaustive writers of 0x7cef650 / 0x7cdede0

Read-only; cores 2-3; no runs/device. Every claim quotes a raw line.
Evidence: `raw_all_7cef650.txt`, `raw_all_7cdede0.txt`, `raw_ctor3_tail.txt`,
`raw_skip_src.txt`.

Faulting instruction first: guest `6caef66: int3` (HardAssert), bytes `cc`.

## Exhaustive reference scan — PROVEN

Whole-engine grep of the full disassembly found **exactly three** references
to `0x7cef650` (quoted):
> `2279a8f: mov %al,0x7cef650`   — the ONLY write, in replayed ctor 3602 (`0x2279a47`)
> `227d51f: lea … # 7cef650`     — address taken as an argument
> `6caef58: cmpb $0,0x7cef650`   — assert read

and **exactly two** references to `0x7cdede0` (quoted):
> `1db9acf: mov %bl,0x7cdede0`   — the ONLY write, in ctor 3 (`0x1db9a40`)
> `239eb14: cmpb $0,0x7cdede0`   — DFLog printer read

## Which sets NONZERO / skipped vs replayed — PROVEN

- `0x7cef650`: the sole writer stores `%al`, and ctor 3602 executes
  `2279a4b: xor %eax,%eax` before it with no intervening test. So it **always
  writes ZERO**. Ctor 3602 is **REPLAYED** by decision 008 (index 3602 ∈
  `[5,3618)`; static-B quoted `n5_1.err:3652 ctor[3602] rva=0x2279a47`).
- `0x7cdede0`: ctor 3 computes the byte from a real feature probe — quoted
  `1db9a6b: mov %eax,%r15d`, `1db9aa6: call 1dbe4fc`, `1db9aab: mov %eax,%ebx`,
  `1db9ac7: cmp %r12,%r14`, `1db9aca: setae %al`, `1db9acd: and %al,%bl`,
  `1db9acf: mov %bl,0x7cdede0` — i.e. a boolean/pointer-range result that CAN
  be nonzero. But ctor 3 is **SKIPPED** by decision 008, quoted from
  `fflag_default.c:111-113`:
  `{ 3u, 0x1db9a40u, "mimalloc usable-size/allocator feature probe (globals 0x7cdedd8/0x7cdede0); only reader 0x239eb14 selects a DFLog string -> no functional effect" }`.
  On real boot the Android linker runs all init_array entries including 3, so
  it can be set nonzero there; in the harness it stays zero (.bss).

## BLOCKED — no nonzero writer for 0x7cef650 exists — PROVEN

Task instruction: "If none, write BLOCKED with evidence."
**BLOCKED**: there is no function or ctor anywhere in the engine that writes a
nonzero value to `0x7cef650`. The only writer is ctor 3602's unconditional
`mov %al,0x7cef650` with `%al == 0`. Therefore the premise "a genuine boot
leaves `0x7cef650` nonzero so the assert passes" is **RETRACTED/UNPROVEN** —
`6caef58: cmpb $0,0x7cef650` / `6caef5f: je 6caef66` takes the int3 path on
**every** boot, harness and real, unless some external actor writes the byte.

## Capture / DEX input changing ctor3602's zero write — none — PROVEN/UNPROVEN

Ctor 3602 has no input dependency: quoted disassembly of the whole function
`2279a47..2279b55` shows only `xor %eax,%eax`, stores/LEAs, and two calls
*after* the store (`2279adf: call 1dc1208`, `2279b48: call 2e8afb0`). There is
no `getenv`, no argument use, no JNI/DEX input, no file/network read before
`0x2279a8f`. So no captured input or DEX value can change that zero write —
**PROVEN** from the quoted function body. Whether some later ctor indirectly
changes `0x7cef650` through `0x227d51f`'s `lea` argument was not proven by a
raw line — **UNPROVEN**, and the exhaustive reference scan shows no other
writer.

## What NOT done
No apply/build, runs/gdb/perf, device/AVD/Frida/phone, new captures, guest
writes, gate edits, seeder, fake returns/remaps, hand-setting the byte, no
credentials, no captures/logs/tokens in git.

## First divergence + next step
First divergence: `0x7cdede0` is written by skipped ctor 3 (nonzero on real
boot, zero in harness), but `0x7cef650` has no nonzero writer at all. Next:
lead/harness must decide whether the `int3(0x6caef66)` assert is expected on
all boots (gate/scope decision — not mine).
