# n1-4 #658 static-B — 2a1e3a0 TRUE-path terminus + 2a132c0 nesting writer (READ-ONLY)

Engine SHA 3e498d64, taskset -c 4,5, worktree /tmp/opencode/n1-static-b.
Faulting instruction first (n1-3 harness, post-SA_ONSTACK, report_n13.md): `0x2ea6446: push %r12` (2/3) or `0x6ca7f95: call 0x2a13350` (1/3), all `si_addr=rsp-8` stack guard. The `0x6ca7f95` site is the **FALSE** path of wrapper `0x6ca7f72`; the TRUE path analyzed here is a different, NULL-terminated callback chain.

All addresses, bytes and mnemonics below are verbatim from saved disassembly; only long export symbol names are abbreviated `<...+offset>`.

## PROVEN — 2a1e3a0 TRUE path (dis_2a1e3a0.txt)

Reached from `2a12f1a: call 2a1e3a0` with rdi=slot holder, rsi=nesting word masked, rdx=scheduler, r8=1, r9=0.
```
 2a1e3dd:	48 8b 46 48       	mov    0x48(%rsi),%rax
 2a1e3e4:	74 24             	je     2a1e40a
 2a1e408:	ff d0             	call   *%rax
 2a1e40e:	48 8d 05 1b 02 00 00 	lea    0x21b(%rip),%rax  # 2a1e630
 2a1e415:	49 89 46 40       	mov    %rax,0x40(%r14)
 2a1e41d:	49 89 46 48       	mov    %rax,0x48(%r14)
 2a1e43d:	48 8d 15 9c d0 ff ff 	lea    -0x2f64(%rip),%rdx  # 2a1b4e0
 2a1e476:	ff e2             	jmp    *%rdx
 2a1e495:	4c 8b 63 40       	mov    0x40(%rbx),%r12
 2a1e499:	4c 8b 7b 48       	mov    0x48(%rbx),%r15
 2a1e4a7:	49 8b 45 48       	mov    0x48(%r13),%rax
 2a1e4d9:	ff d0             	call   *%rax
 2a1e4db:	49 c7 43 50 00 00 00 00 	movq   $0x0,0x50(%rbx)
 2a1e4e3:	4c 89 ff          	mov    %r15,%rdi
 2a1e4e6:	4c 89 f6          	mov    %r14,%rsi
 2a1e4e9:	41 ff d4          	call   *%r12
 2a1e4ec:	48 85 c0          	test   %rax,%rax
 2a1e4ef:	0f 84 2a 01 00 00 	je     2a1e61f
 2a1e62d:	c3                	ret
```
- Which fn pointer: the **closure call is `2a1e4e9: call *%r12`**, with `%r12 = 0x40(%rbx)` (slot holder +0x40, loaded at `2a1e495`) and context `%r15 = 0x48(%rbx)` (loaded at `2a1e499`). Before it there are two indirect calls through `0x48(...)` of the nesting object (`2a1e408` and `2a1e4d9`). The `2a1e476: jmp *%rdx` goes to helper `0x2a1b4e0`, which is only a context-copy (`mov (%rsi),%rax; mov 0x8(%rsi),%rcx; mov %rdi,0x38(%rax); mov 0x68(%rax),%rdx; mov %rdx,0x68(%rcx); ret`) — NOT a closure call.
- Termination: `2a1e4ec: test %rax,%rax; 2a1e4ef: je 2a1e61f; 2a1e62d: ret` — the chain terminates when a closure returns NULL; a non-NULL result loops through the same call sequence again. So the TRUE path is a NULL-terminated callback chain, not an unconditional recursion.
- Re-entry into `0x6ca7f72`: **`0x6ca7f72` has ZERO references in `0x2a1e3a0`** (verified grep; only 2 refs in whole binary: `6c8e4cc: lea ... # 6ca7f72` vtable builder and the function start itself). All calls on this path are indirect runtime pointers. Therefore no *static* path from TRUE branch to `0x6ca7f72` exists; whether the runtime closure pointer at `+0x40` ever equals the wrapper is **UNPROVEN** from static bytes. The harness fault sites are the FALSE path (`0x6ca7f95`) and the getter (`0x2ea6446`), not this TRUE path.

## PROVEN — 2a132c0 FunctionMarshaller writes +0x70 only (dis_2a132c0.txt)

```
 2a132cd:	e8 8e ab ff ff    	call   2a0de60
 2a132d2:	e8 89 ab ff ff    	call   2a0de60
 2a132d7:	4c 8b 30          	mov    (%rax),%r14
 2a132da:	4d 8b 7e 70       	mov    0x70(%r14),%r15
 2a132de:	45 89 fc          	mov    %r15d,%r12d
 2a132e1:	41 83 e4 3f       	and    $0x3f,%r12d
 2a132ec:	85 db             	test   %ebx,%ebx
 2a13330:	ff d0             	call   *%rax
 2a13332:	4c 01 e3          	add    %r12,%rbx
 2a13335:	49 83 e7 c0       	and    $0xffffffffffffffc0,%r15
 2a13339:	49 09 df          	or     %rbx,%r15
 2a1333c:	4d 89 7e 70       	mov    %r15,0x70(%r14)
 2a13348:	c3                	ret
```
The ONLY data write is `2a1333c: mov %r15,0x70(%r14)`. There is **no `0x28(%...)` reference at all** in this function (verified grep of dis_2a132c0.txt), so it cannot set `obj+0x28`. It adjusts the low 6-bit nesting counter by ±1 (`rbx` = 1 if arg nonzero, -1 if zero) and writes back to `+0x70`, with RSL handler call at `2a13330: call *%rax` on saturation/underflow (handler global `0x727e910`). The closure word `obj+0x28` is written only by `2a0ed45: mov %r15,0x28(%rbx)` (static-A #655), not by FunctionMarshaller.

## Evidence / NOT done

Evidence: dis_2a1e3a0.txt (117 lines), dis_2a132c0.txt, dis_2a12e80.txt, dis_2a0ed45.txt, /tmp/opencode/n1-harness/report_n13.md, /tmp/opencode/n1-static-a/report_chain.md. NOT DONE: no runs/device/captures/guest writes/gate/seeder/apply. UNPROVEN: concrete runtime value of the closure pointer at slot+0x40, whether it can equal 0x6ca7f72, real-boot correctness of the TRUE path, and which function the `0x48(...)` indirect calls resolve to. Next smallest step: read-only confirm at harness which pointer `0x40(%rbx)` holds when `2a1e4e9` is reached, and whether `2a1e4ef` (NULL test) is ever taken.
