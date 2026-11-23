# n7w1 #687 — ctor3602 write condition + assert polarity

Read-only; cores 2-3; no runs/device. Every success claim quotes a raw line.
Evidence: `dis_ctor3602.txt`, `raw_assert.txt`.

Faulting instruction first: guest **`0x6caef66: int3`** (HardAssert trap),
bytes `cc`. Reached by `je` from the two byte tests below.

## (1) ctor 3602 `0x2279a47` — the write is UNCONDITIONAL — PROVEN

Quoted raw disassembly (full function `0x2279a47..0x2279b55`):
> `2279a47: push %rbp` / `2279a48: mov %rsp,%rbp`
> `2279a4b: xor %eax,%eax`            ⇒ `%al = 0`
> `2279a4d: mov %al,0x7cef628`
> `2279a53: lea … # 2bfe87,%rcx` ; `2279a5a: mov %rcx,0x7cef630`
> `2279a61: movq $0x1c,0x7cef638`
> `2279a6c: lea … # 227d536,%rcx` ; `2279a73: mov %rcx,0x7cef640`
> `2279a7a: lea … # 7cef640,%rcx`
> `2279a81: mov 0x7cdeb68(%rip),%rdx`
> `2279a88: mov %rdx,0x7cef648`
> `2279a8f: mov %al,0x7cef650`        <-- the write: stores 0, no guard

There is **no condition guarding `0x2279a8f`**: `%al` was zeroed once at
`0x2279a4b` and no test/branch/jump occurs between it and the store. No
`getenv`, no import/GOT check, no global flag test, no syscall, no CPU-feature
probe, and no detection call decides it. The only calls in the ctor come
*after* the write (`2279adf: call 1dc1208`, `2279b48: call 2e8afb0`) and build
objects/strings, not a mimalloc detection result.

**RETRACTED** the premise that a detection call or compared feature guards the
write to `0x7cef650` — the store is an unconditional zero assignment.

## (2) Assert polarity — PROVEN

Quoted raw disassembly (`0x6caef4f..0x6caef6e`):
> `6caef4f: cmpb $0x0,0x7cdd149`      (latch byte — tested FIRST)
> `6caef56: je 0x6caef66`
> `6caef58: cmpb $0x0,0x7cef650`      (mimalloc byte — tested SECOND)
> `6caef5f: je 0x6caef66`
> `6caef61: call 0x6d320d0 <abort@plt>`
> `6caef66: int3`                     <-- faulting instruction
> `6caef67: mov %eax,%eax`
> `6caef69: call 0x6d320d0 <abort@plt>`

Polarity: `je 0x6caef66` is taken when the tested byte equals **ZERO**, so the
`int3` HardAssert trap at `0x6caef66` fires when **either** byte is zero:
- latch `0x7cdd149 == 0` → trap (first test), or
- mimalloc flag `0x7cef650 == 0` → trap (second test).

The explicit `abort@plt` at `0x6caef61` runs only when **both** bytes are
non-zero. So the answer to "is the abort path taken on zero?" is: **the `int3`
trap path is taken on zero**; the direct `abort()` call is taken on non-zero.
Byte order: `0x7cdd149` first, `0x7cef650` second.

## What NOT done
No apply/build, no runs/gdb/perf, no device/AVD/Frida/phone, no new captures,
no guest writes, gate edits, seeder, fake JNI returns, remaps, or setting
`0x7cef650` by hand, no credentials, no captures/logs/tokens in git. I did not
locate a local raw `sha256=8215519f` banner line in my own evidence files; the
input/SHA banner is the harness's condition, not asserted here (UNPROVEN from
my side).

## First divergence + next step
First divergence: ctor 3602 unconditionally writes 0 to `0x7cef650`, so the
assert's second byte is zero and `je 0x6caef66` (int3) fires; on a genuine
boot the mimalloc probe would leave it non-zero. Next: harness exec-bp at
`0x2279a8f` (hit/tid/order/value) and read both assert bytes at the fault.
