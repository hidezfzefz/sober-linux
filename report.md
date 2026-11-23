# n4 #675 static-B — #552 chain to fflush(NULL) (READ-ONLY)

Engine SHA 3e498d64, taskset -c 4,5, worktree /tmp/opencode/n4-static-b.
FAULTING INSTRUCTION FIRST (lead/harness): host libc `fflush+0x3f`, `mov rax,[rdi+8]`, `si_addr=0x8`, rdi=NULL — **not guest code**. RET64 absent.

## PROVEN — "first guest RVA" 0x72a0818 is DATA, not a call site

`readelf -S`: `.bss NOBITS vaddr 0x7282500 size 0xb1ad1c` (covers 0x72a0818). RETRACTED: treating it as an instruction address.

## PROVEN — List.size() call site and immediate post-`size()==0` action

`0x2383c18` (nativePostClientSettingsLoadedInitialization3) calls main body `0x2383c84` at `2383c6d`. Inside it (dis_552_body.txt):
```
2383d16: lea -0x1e3519f(%rip),%rdx   # 54eb7e   ; "size"
2383d1d: lea -0x1edef0c(%rip),%rcx   # 4a4e18   ; "()I"
2383cfc/2383d2a: call *0x108(%rax)   ; GetMethodID("size","()I")
2383d2a: (rax = methodID)
2383d30: mov %r13,%rdi
2383d33: mov %r14,%rsi              ; rsi = jobject (entry rdx)
2383d36: mov %rax,%rdx
2383d39: xor %eax,%eax
2383d3b: call 238463c               ; <-- CallIntMethodA wrapper (size())
2383d40: test %eax,%eax
2383d42: jle 23844e9                ; size() <= 0 -> EARLY NORMAL RETURN
23844e9: mov 0x4ea4f08(%rip),%rax   # 72293f8  (stack canary)
23844f3: cmp -0x30(%rbp),%rax
23844f7: jne 2384510
238450a: ret
```
Helper `0x238463c`: `238460c: lea # 4a4e18 ("()I")`; `238461c: call *0x108(%rax)` (GetMethodID); `23846b6: ff 90 90 01 00 00 call *0x190(%rax)` (CallIntMethodA -> eax = size). Strings verified from file: `0x54eb7e="size"`, `0x4a4e18="()I"`, `0x217ca9="java/util/List"`, `0x3305b9="get"`, `0x250e4f="(I)Ljava/lang/Object;"`.
So **right after size() returns 0: `test eax,eax; jle` -> normal early return**, not an immediate abort.

## PROVEN — how fflush(NULL) is reached (fatal-report/abort helper 0x2ea6a10)

`0x2383c84` contains C++ exception cleanup/catch blocks; its catch path calls the fatal helper:
```
23845d2: mov %rbx,%rdi
23845d5: e8 36 24 b2 00   call 2ea6a10
```
Helper `0x2ea6a10` (dis_fflush_helper.txt):
```
2ea6a66: mov $0x130,%ebx
2ea6a6b: add 0x4381766(%rip),%rbx  # 72281d8  ; __sF base + 0x130 = stderr
2ea6a8c: call 6d31aa0 <fprintf@plt>
2ea6a91: mov %rbx,%rdi
2ea6a94: e8 87 b5 e8 03  call 6d32020 <fflush@plt>
2ea6a99: call 6d320d0 <abort@plt>
```
Format string at `0x2345cf` = `"libunwind: %s - %s\n"`. This matches `/home/user/sober-linux/src/compat/bionic_sf_full.c:5-8`: *"glibc's `_IO_fflush` faults reading the NULL `_lock` at FILE+0x88"* and the guest's `__sF+0x98/+0x130` stderr derivation. The same fatal helper is called from catch blocks in the tail-jumped function `0x239d1c7` (e.g. `239d8ba`, `239da5f`, `239dcf8`). #552's full chain: `0x2383c18 -> 0x2383c84 (size/get loop; catch -> 2ea6a10)` then `2383c72: call 23847db` then `2383c7f: jmp 239d1c7` (more catch -> 2ea6a10).

## UNPROVEN / RETRACTED

- UNPROVEN: which exact exception producer fires after `size()==0` (the early return itself does not call 2ea6a10); needs harness landing-pad/RET trace.
- UNPROVEN: why rdi=0 at fflush vs `__sF+0x130` here (different call site or zeroed `__sF`); `__sF` is a host stub symbol, no guest write.
- RETRACTED: `0x72a0818` as guest call site; RETRACTED: guest-code fault in fflush.

## Evidence / NOT done

Evidence: dis_552.txt, dis_552_body.txt, dis_fflush_helper.txt, dis_23847db.txt, report.md. NOT DONE: no runs/device/emulator/captures/guest writes/gate edits/seeder/fake returns/path remaps/apply; object OFF, no extension. No captures/tokens in git.
