# n7w4 #697 — size()==0 path vs HardAssert call

Read-only; cores 2-3; no runs/device. Every claim quotes a raw line.
Evidence: `raw_size0_path.txt`, `raw_mimalloc_assert_path.txt`,
`raw_hardassert_dispatch.txt`, `raw_printer_path.txt`.

Faulting instruction first: guest `0x6caef66: int3` (HardAssert), bytes `cc`.

## (1) Direct path from size()==0 does NOT reach HardAssert — PROVEN

Quoted from `0x2383c18`:
> `2383d3b: call 238463c`      (CallIntMethodV → `List.size()`)
> `2383d40: test %eax,%eax`
> `2383d42: jle 23844e9`      (size() <= 0)
> `23844e9: mov 0x72293f8(%rip),%rax` / `mov (%rax),%rax` / `cmp -0x30(%rbp),%rax`
> `23844f9: add $0x128,%rsp` ; ... ; `238450a: ret`

The `jle` target runs a stack-canary check and **returns normally at
`0x238450a`**. There is **no** call to `0x6caed5d` or `0x6caed7c` on this path.
So a direct "size()==0 → HardAssert" edge does not exist in the static graph.

## (2) Which call passes the "Expect to have mimalloc working" message — PROVEN

Quoted raw disassembly (`0x23a1445..0x23a146e`):
> `23a1445: lea … # 3a3078,%rcx`   ("[DFLog::Mimalloc] Mimalloc integration is NOT detected")
> `23a1460: call 232de10`          (DFLog-style emitter)
> `23a1465: lea … # 4c0d2b,%rsi`   ("Expect to have mimalloc working, but it was not detected!")
> `23a146c: xor %edi,%edi`          <-- message index = 0
> `23a146e: call 6caed5d`           <-- HardAssert dispatcher

So the call is **at `0x23a146e`**, passing:
- `rsi` = pointer to rodata string `0x4c0d2b` = `Expect to have mimalloc working, but it was not detected!` (quoted earlier by `strings`),
- `edi = 0` — message **index 0**, not a length/flag.

Dispatcher quoted:
> `6caed61: cmp $0xa,%edi` / `6caed64: mov $0xa,%eax` / `6caed69: cmovb %edi,%eax`
> `6caed6c: lea … # 721fbd0,%rcx` / `6caed73: mov (%rcx,%rax,8),%rdi`
> `6caed77: call 6caed7c`          (reporter)

Index 0 selects table entry `[0]` at `0x721fbd0` and calls reporter
`0x6caed7c`, which contains the two byte tests at `0x6caef4f` (`0x7cdd149`)
and `0x6caef58` (`0x7cef650`) ending in `0x6caef66: int3`.

## (3) How 0x23a146e is reached — PROVEN links, entry UNPROVEN

Quoted chain:
> `239d215: call 239ead3`                (mi_option printer, inside `0x2383c18`)
> `239eb14: cmpb $0,0x7cdede0`           (ctor-3 mimalloc flag)
> `239eb1b: je 23a1422`                  (flag==0 → assertion branch)
> `23a1422: test %al,%al` / `23a1424: je 23a1465`
> `23a1465: lea #4c0d2b,%rsi` → `23a146e: call 6caed5d`

So the HardAssert is reached from the **mi_option printer path**, not from the
`size()<=0` early-return path. The `0x7cdede0` flag is written only by skipped
ctor 3 (task #691), so on the harness path it is zero ⇒ `je 0x23a1422` fires.

UNPROVEN: what makes execution take the `239d215` printer call in the faulting
run. Static-B #693 found "NO throw located" after `size()==0` and reported the
exception path as UNPROVEN; the `23845d5` site it cites is the C++ rethrow
helper `0x2ea6a10`, not a call to the printer or the HardAssert dispatcher.
There is no raw line proving a control-flow edge from `0x238450a` (the
size()==0 return) to `0x239d215`.

## What NOT done
No apply/build, runs/gdb/perf, device/AVD/Frida/phone, new captures, guest
writes, gate edits, seeder, fake returns, remaps, hand-setting bytes, no
credentials, no captures/logs/tokens in git.

## First divergence + next step
First divergence: harness reaches the HardAssert via the printer/flag path
(`0x7cdede0 == 0`) rather than via `List.size()==0`; the direct edge is absent.
Next: harness logs the branch at `0x239eb1b` and the value of `%al` at
`0x23a1422` for the faulting call (faulting instruction first).
