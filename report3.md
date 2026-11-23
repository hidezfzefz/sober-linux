# n1-3 #653 static-B — 2a12e80 dispatcher terminus (READ-ONLY)

Engine SHA 3e498d64, taskset -c 4,5, worktree /tmp/opencode/n1-static-b.
Faulting instruction first: `6ca800e: call *0x720(%rax)` (fn224 ExceptionCheck) re-entered by this dispatcher's scheduled closure.

## PROVEN — 2a12e80 full (dis_2a12e80.txt)

All addresses, bytes and mnemonics below are verbatim from the saved disassembly;
only the long export symbol names are abbreviated as `<...+offset>`.

```
 2a12e98:	e8 c3 af ff ff	call   2a0de60 <...+0x29a6c5>
 2a12e9d:	48 8b 18       	mov    (%rax),%rbx
 2a12ea0:	4c 8b 63 68    	mov    0x68(%rbx),%r12
 2a12ea4:	4d 8b 7c 24 70 	mov    0x70(%r12),%r15
 2a12ea9:	45 89 fd       	mov    %r15d,%r13d
 2a12eac:	41 83 e5 3f    	and    $0x3f,%r13d
 2a12eb0:	48 8b 05 59 ba 86 04 	mov    0x486ba59(%rip),%rax  # 727e910
 2a12ed2:	ff d0          	call   *%rax
 2a12ed4:	49 ff c5       	inc    %r13
 2a12ed7:	49 83 e7 c0    	and    $0xffffffffffffffc0,%r15
 2a12edb:	4d 09 ef       	or     %r13,%r15
 2a12ede:	4d 89 7c 24 70 	mov    %r15,0x70(%r12)
 2a12eef:	48 83 7b 28 00 	cmpq   $0x0,0x28(%rbx)
 2a12ef4:	74 2b          	je     2a12f21
 2a12ef6:	48 8b 53 68    	mov    0x68(%rbx),%rdx
 2a12efa:	48 8b 73 70    	mov    0x70(%rbx),%rsi
 2a12efe:	48 83 e6 c0    	and    $0xffffffffffffffc0,%rsi
 2a12f0e:	48 89 df       	mov    %rbx,%rdi
 2a12f11:	41 b8 01 00 00 00 	mov    $0x1,%r8d
 2a12f1a:	e8 81 b4 00 00 	call   2a1e3a0 <...+0x2aac05>
 2a12f21:	49 8b 7e 08    	mov    0x8(%r14),%rdi
 2a12f27:	41 ff 16       	call   *(%r14)
 2a12f2a:	49 8b 7e 08    	mov    0x8(%r14),%rdi
 2a12f31:	41 ff 16       	call   *(%r14)
 2a12f34:	49 c7 06 00 00 00 00 	movq   $0x0,(%r14)
 2a12f6a:	49 ff cf       	dec    %r15
 2a12f6d:	49 83 e6 c0    	and    $0xffffffffffffffc0,%r14
 2a12f71:	4d 09 fe       	or     %r15,%r14
 2a12f74:	4c 89 73 70    	mov    %r14,0x70(%rbx)
 2a12f86:	c3             	ret
```

Semantics (my reading of the quoted lines):
- obj+0x70 = tagged word whose **low 6 bits** are a nesting counter. ENTER: `2a12ed4 inc %r13` / `2a12edb or` / `2a12ede mov %r15,0x70(%r12)`. LEAVE: `2a12f6a dec %r15` / `2a12f71 or` / `2a12f74 mov %r14,0x70(%rbx)`.
- Two RSL assert paths: ENTER calls handler at `0x727e910` via `call *%rax` (`2a12ed2`) when counter==0x3f and handler installed (guard `2a12eb0..2a12ec9`, `cmp $0x3f,%r13`); LEAVE calls same handler (`2a12f68: call *%rax`) when counter==0 and handler installed (`2a12f50..2a12f5f`, `and $0x3f,%r15`, `sete %dl`).
- Closure word test: `2a12eef cmpq $0x0,0x28(%rbx)` / `2a12ef4 je 2a12f21`.
  - TRUE path (closure != 0) TERMINAL CALL is guest **`2a1e3a0: call`** (`2a12f1a`) — a scheduler task-dispatch function, **not** a JNI table call. No `call *0x7xx(%rax)` in this dispatcher.
  - FALSE path (closure == 0) calls the closure's function pointer (ctx at +8) **twice** — `2a12f27 call *(%r14)` and `2a12f31 call *(%r14)` — then **clears it** with `2a12f34 movq $0x0,(%r14)`. That clear is the re-entry stop: a later dispatch sees NULL and cannot call the closure again.

## obj+0x28 = closure/task function pointer (not jmethodID)

Quoted from dis_2a0ed45.txt and verified via disassembly:
```
 2a0ed45:	4c 89 7b 28    	mov    %r15,0x28(%rbx)
 2a0ed0f:	4c 8b 38       	mov    (%rax),%r15
 2a0e83b:	e8 80 00 00 00 	call   2a0e8c0 <...+0x29b125>
```
`r15` is loaded from a caller-supplied stack arg pointer's first word (`mov (%rax),%r15`), i.e. `obj+0x28` holds a pointer to a closure object whose fn ptr is at offset 0 and ctx at +8 (consumed at `2a12f21/2a12f27/2a12f31`). The predicate `2a13361: cmpq $0x0,0x28(%rax)` tests this word; harness proved it 0 at the fault.

## Evidence / NOT done

Evidence: dis_2a12e80.txt (144 lines), dis_2a0ed45.txt, dis_2a13560.txt, /tmp/opencode/n1-harness/report.md. NOT DONE: no runs/device/captures/guest writes/gate/seeder/apply. UNPROVEN: exact closure object type/size, which concrete closure the TRUE path dispatches in real boot, and whether `2a1e3a0` itself re-enters the wrapper. Next smallest step: read-only trace `2a1e3a0` to its closure call and confirm `2a12f34` is reached on the harness recursion path.
