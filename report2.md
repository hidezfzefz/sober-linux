# n5 follow-up — new fault at guest+0x6caef67 (READ-ONLY)

FAULTING INSTRUCTION FIRST (harness n5_1/2/3.err:3730-3735, 3/3):
```
RBXCRASH: HardAssert (Expect to have mimalloc working, but it was not detected!)
[W30_FAULT] rip=0x00007f12da5d6f67 si_addr=0x0000000000000000 ... insn=0000000000000089 00000000000000c0 00000000000000e8 ...
[W28_FAULT_INSN] rip=... guest_rva=0x06caef67
signal=5 tid=1952583 rip=... si_addr=0x0000000000000000
```
The reported RIP `0x6caef67` is the instruction **after** the trap `6caef66: cc int3`. Bytes `89 c0 e8 62` = `89 c0 mov %eax,%eax; e8 62 31 08 00 call abort@plt` (dis_6caef67.txt).

## PROVEN — containing function + message

Function containing the trap: **`0x6caed7c`** (`push rbp` prologue), a reporter taking `(rdi = tag from table, rsi = message)`. It is reached via dispatcher `0x6caed5d`:
```
6caed5d: push %rbp
6caed61: cmp $0xa,%edi
6caed69: cmovb %edi,%eax
6caed6c: lea 0x570e5d(%rip),%rcx   # 721fbd0
6caed73: mov (%rcx,%rax,8),%rdi
6caed77: call 6caed7c
6caef4f: cmpb $0x0,0x102e1f3(%rip) # 7cdd149
6caef56: je 6caef66
6caef58: cmpb $0x0,0x10406f1(%rip) # 7cef650
6caef5f: je 6caef66
6caef61: call 6d320d0 <abort@plt>
6caef66: cc int3
```
Message producer (dis_assert_msg.txt): `23a1465: lea -0x1ee0741(%rip),%rsi # 4c0d2b`, `23a146c: xor %edi,%edi`, `23a146e: call 6caed5d`. Strings verified from engine bytes: `0x4c0d2b="Expect to have mimalloc working, but it was not detected!"`, `0x3a30df="RBXCRASH"`, `0x3a30e8="HardAssert"`, `0x3a3078="[DFLog::Mimalloc] Mimalloc integration is NOT detected"`. Harness RET64 first guest RVA `guest+0x3a30e8` and `guest+0x4c0d2b` are exactly these strings — confirming this call chain.

## PROVEN — predicate bytes and their writers

The assert requires **both** globals nonzero (either zero -> int3):
- `0x7cdd149` — written to 1 by `2eb7520: c6 05 ... 01 movb $0x1,... # 7cdd149` (the W18 per-activation latch write; other writers `2ec9fae`, `56d31a3`, `56d3386`, `56d6a4b`, `56d6b80`).
- `0x7cef650` — initialized **zero** by `2279a8f: 88 05 ... mov %al,... # 7cef650` (in the JNI_OnLoad init path where `xor %eax,%eax` at `2279a4b`; dis_mimalloc_flag.txt), and referenced at `227d51f: lea ... # 7cef650` alongside FFlag name `0x3a30af="DebugForceTerminateOnRbxCrash"`.

Within the reporter function, `6caed9b: mov 0x...(%rip),%rax # 7cef6c0; 6caedaa: call *%rax` invokes a global reporter before the byte checks.

## Startup step that normally makes mimalloc detection true

`src/compat/bionic_bootstrap/README.md:42-55` (verbatim):
```
**Six mimalloc bridges** (guest RVA → native host function):
| allocate (small) | `0x1dbad62` | `malloc`/`mi_malloc` |
mimalloc is used if the process already exports it;
otherwise native glibc malloc is used.
```
and `README.md:242-246`:
```
   allocator is glibc malloc when mimalloc is absent — still fully native.
```
`bionic_bootstrap.c:175`: `extern void *mi_malloc(size_t) __attribute__((weak));`
The harness redirects the six guest allocator thunks to host `malloc`/`free` and binds mimalloc with **weak** symbols; when the host process does not export mimalloc, the bridges use glibc malloc, so the guest's own mimalloc-integration detection returns false — logging `[DFLog::Mimalloc] Mimalloc integration is NOT detected` and firing this HardAssert. The genuine app has mimalloc loaded, so the detection is true and the trap is not reached.

## UNPROVEN / NOT done

- UNPROVEN: exact detection call that produces the boolean tested at `23a1422: test %al,%al; je 23a1465`; which of the two predicate bytes is specifically "mimalloc detected" vs the latch/FFlag gate; whether writing either byte alone clears the assert.
- NOT DONE: no apply/runs/device/captures/guest writes/gate edits/seeder/fake returns/path remaps; object OFF. Evidence: report2.md, dis_6caef67.txt, dis_assert_msg.txt, dis_mimalloc_flag.txt, dis_mimalloc_set.txt, n5-harness/report_n5.md.
