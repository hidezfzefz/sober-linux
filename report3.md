# n7w3 #693 static-B — exception path into fatal reporters (READ-ONLY)

FAULTING INSTRUCTION FIRST (n7w1/2): guest `int3 0x6caef66` (reported RIP `0x6caef67`: `89 c0 mov %eax,%eax; e8 62... call abort@plt`), SIGTRAP, `[0x7cdd149]=1, [0x7cef650]=0` 3/3. No runs/writes.

## PROVEN — two distinct fatal reporters, both reachable from #552 code

**A. Unwind reporter `0x2ea6a10`** (dis_n5 dis_fflush_helper.txt / n5 static-A):
```
2ea6a66: mov $0x130,%ebx
2ea6a6b: add 0x...(%rip),%rbx   # 72281d8  (__sF+0x130 stderr)
2ea6a72: lea ... # 2345cf       ; "libunwind: %s - %s\n"
2ea6a79: lea ... # 517ec0       ; "_Unwind_Resume"
2ea6a80: lea ... # 2a4d76       ; "_Unwind_Resume() can't return"
2ea6a8c: call 6d31aa0 <fprintf@plt>
2ea6a91: mov %rbx,%rdi
2ea6a94: call 6d32020 <fflush@plt>
2ea6a99: call 6d320d0 <abort@plt>
```
Strings verified from engine bytes: `0x2345cf="libunwind: %s - %s\n"`, `0x517ec0="_Unwind_Resume"`, `0x2a4d76="_Unwind_Resume() can't return"`. This is the `_Unwind_Resume can't return` path. It is called from a C++ exception catch block inside the #552 body `0x2383c84`:
```
23845cd: call 1dbe5be      ; destructor cleanup
23845d2: mov %rbx,%rdi
23845d5: call 2ea6a10      ; <-- unwind fatal reporter
```
(also hundreds of analogous catch blocks in `0x1dba..0x6ca1` regions).

**B. HardAssert reporter `0x6caed7c`** — the actual n7 fault. Message producer and dispatcher (n5 dis_assert_msg.txt / n7w1):
```
23a1465: lea -0x1ee0741(%rip),%rsi  # 4c0d2b
                                   ; "Expect to have mimalloc working, but it was not detected!"
23a146c: xor %edi,%edi
23a146e: call 6caed5d               ; dispatcher
6caed61: cmp $0xa,%edi
6caed69: cmovb %edi,%eax            ; index = min(edi,10)
6caed6c: lea 0x570e5d(%rip),%rcx    # 721fbd0  (11-entry table, zero in file, relocated at load)
6caed73: mov (%rcx,%rax,8),%rdi
6caed77: call 6caed7c               ; reporter
6caef4f: cmpb $0x0,0x102e1f3(%rip)  # 7cdd149  (latch, =1)
6caef56: je 6caef66
6caef58: cmpb $0x0,0x10406f1(%rip)  # 7cef650  (flag, =0)
6caef5f: je 6caef66
6caef61: call 6d320d0 <abort@plt>
6caef66: cc int3
```
Both reporters are generic: dispatcher `0x6caed5d` has many call sites across the engine; the mimalloc message selects this one.

## PROVEN — unwind callback requirement and the shim

`_Unwind_Resume` needs frame-descriptor lookup via `dl_iterate_phdr` (libgcc/libunwind walks loaded objects' `.eh_frame`). The guest-facing shim is `TRAMP(dl_iterate_phdr)` at `bionic_libc.c:1356`, macro `:459-470`:
```
464: "movq sober_libc_fn_" #name "(%%rip), %%rax"
465: "testq %%rax, %%rax"
466: "jz 1f"
467: "jmp *%%rax"
468: "1: xorl %%eax, %%eax"
469: "ret"
```
with pointer filled at `:919 sober_libc_fn_dl_iterate_phdr = native_lookup("dl_iterate_phdr")`. When NULL, the shim returns **0 and invokes no callback** (n7w1); when set, it forwards to glibc's iterator over the **host** link map. Guest C++ exception paths (e.g. #552's catch at `0x23845cd-23845d5`) therefore depend on unwind callbacks that the shim may not provide. Which branch occurs at fault time is **UNPROVEN** (no runs this task).

## PROVEN / UNPROVEN — exception after #552 size()=0

`#552` body `0x2383c84` calls `size()` via helper `0x238463c`:
```
2383d3b: call 238463c       ; CallIntMethodA("size","()I")
2383d40: test %eax,%eax
2383d42: jle 23844e9        ; size<=0 -> early NORMAL return (canary check; 238450a ret)
```
So `size()==0` itself does **not** throw — it returns normally; no `__cxa_throw` was located on that path. The exception that would drive reporter A after that point is **UNPROVEN** from static bytes. Equally UNPROVEN: whether the HardAssert path B is reached via an unwind failure (the message table `0x721fbd0` is relocation-filled, so index 0's target cannot be read from the file), and whether `dl_iterate_phdr` returns 0 at fault time.

## RETRACTED / NOT done

- RETRACTED: any claim that `size()==0` directly throws or that the unwind path is proven to cause the mimalloc HardAssert — both are separate reporters.
- NOT DONE: no runs/device/guest writes/gate edits/seeder/fake returns/remaps; object OFF; no captures/tokens in git.
