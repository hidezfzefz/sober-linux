# n1-5 #662 static-B — installer arg contract + closure source (READ-ONLY)

Engine SHA 3e498d64, taskset -c 4,5, worktree /tmp/opencode/n1-static-b.
Faulting instruction first (n1-4 harness report_n14.md): `0x6ca7f95: call 0x2a13350` (si_addr=rsp-8 stack guard; wrapper FALSE path). No apply, no guest writes.
Addresses/bytes/mnemonics below verbatim; long export symbol names abbreviated `<...+offset>`.

## RETRACTION / CORRECTION (premise of the task)

`0x2a0ed0f` is **NOT inside `0x2a0e8c0`**. `0x2a0e8c0` ends at `2a0eb92: ret` (verified dis_2a0e8c0_full.txt lines 216-217), followed by int3 padding, then a new function starts at `2a0eba0: push %rbp`. The `+0x28` store at `2a0ed45` and the quoted `2a0ed0f` are in **`0x2a0eba0`**, reached by `2a0eb0f: call 2a0eba0` from `0x2a0e8c0`. My earlier reports described this loosely as "caller-supplied stack arg"; the precise contract is below.

## PROVEN — arg contract of installer `0x2a0eba0` (dis_2a0e8c0_full.txt, second function)

Entry saves (quoted):
```
 2a0ebb1:	4c 89 45 b8    	mov    %r8,-0x48(%rbp)
 2a0ebb5:	48 89 4d c0    	mov    %rcx,-0x40(%rbp)
 2a0ebb9:	48 89 55 d0    	mov    %rdx,-0x30(%rbp)
 2a0ebbd:	48 89 75 c8    	mov    %rsi,-0x38(%rbp)
 2a0ebc1:	49 89 fd       	mov    %rdi,%r13
 2a0ebce:	49 be ff ff ff ff ff 	movabs $0xffffffffffff,%r14
```
No stack args are read (only the five registers above). Caller setup at `2a0eaa0` (quoted from caller dump):
```
 2a0eaa0:	48 89 75 d0    	mov    %rsi,-0x30(%rbp)
 2a0eaa4:	48 89 55 d8    	mov    %rdx,-0x28(%rbp)
 2a0eaa8:	48 89 4d e0    	mov    %rcx,-0x20(%rbp)
 2a0eabc:	4c 8d bb 80 05 00 00  lea    0x580(%rbx),%r15
 2a0eacb:	48 8b 40 68    	mov    0x68(%rax),%rax
 2a0eacf:	48 8b 40 60    	mov    0x60(%rax),%rax
 2a0eafd:	48 8d 4d d0    	lea    -0x30(%rbp),%rcx
 2a0eb01:	4c 8d 45 d8    	lea    -0x28(%rbp),%r8
 2a0eb05:	4c 8d 4d e0    	lea    -0x20(%rbp),%r9
 2a0eb09:	4c 89 ff       	mov    %r15,%rdi
 2a0eb0c:	4c 89 f6       	mov    %r14,%rsi
 2a0eb0f:	e8 8c 00 00 00 	call   2a0eba0
```
Therefore the installer contract is: **rdi** = atomic tagged list-head word (nesting base +0x580, target of `lock cmpxchg ... ,0x0(%r13)`); **rsi** = out-pointer, `2a0ed01: mov %rcx,(%rax)` writes the tagged word into `*rsi`; **rdx** = optional refcount pointer (`2a0ecbf: lock xadd %rdx,0x8(%r13)`, `2a0ecd1: mov (%rsi),%rax`); **rcx** = pointer to caller arg2, dereferenced at `2a0ed08` and passed as rsi to `2a13620`; **r8** = pointer to caller arg3, dereferenced at `2a0ed0f`; r9 is overwritten as a local and is not an input.

## PROVEN — closure pointer source at 2a0ed0f

```
 2a0ed04:	48 8b 45 c0    	mov    -0x40(%rbp),%rax
 2a0ed08:	48 8b 30       	mov    (%rax),%rsi
 2a0ed0b:	48 8b 45 b8    	mov    -0x48(%rbp),%rax
 2a0ed0f:	4c 8b 38       	mov    (%rax),%r15
 2a0ed12:	4d 21 f7       	and    %r14,%r15
 2a0ed15:	48 89 df       	mov    %rbx,%rdi
 2a0ed18:	ba 03 00 00 00 	mov    $0x3,%edx
 2a0ed1d:	e8 fe 48 00 00 	call   2a13620
 2a0ed45:	4c 89 7b 28    	mov    %r15,0x28(%rbx)
```
So `r15 = *(r8_arg) & 0x0000ffffffffffff` (canonical 48-bit pointer mask, r14 set at `2a0ebce`). It is a **pointer-to-word** input (one dereference), not an inline value — consistent with harness #657 finding the captured 3rd native's opaque object unreconstructable. The object's own `+0x40/+0x48` are self-referential bookkeeping, not the closure:
```
 2a0ed22:	48 8d 43 30    	lea    0x30(%rbx),%rax
 2a0ed2e:	49 83 c6 b1    	add    $0xffffffffffffffb1,%r14
 2a0ed32:	49 21 c6       	and    %rax,%r14
 2a0ed35:	4c 89 73 38    	mov    %r14,0x38(%rbx)
 2a0ed39:	4c 89 73 40    	mov    %r14,0x40(%rbx)
 2a0ed3d:	48 c7 43 48 00 00 00 	movq   $0x0,0x48(%rbx)
```

## PROVEN / UNPROVEN — already-live closure pointer in guest fields

- **PROVEN** a genuine guest closure pointer is installed at the *scheduler* object's `+0x40` (from n1-4 dis_2a1e3a0.txt): `2a1e40e: lea ... # 2a1e630; 2a1e415: mov %rax,0x40(%r14); 2a1e41d: mov %rax,0x48(%r14)`, and consumed as a call target: `2a1e495: mov 0x40(%rbx),%r12; 2a1e499: mov 0x48(%rbx),%r15; 2a1e4e9: call *%r12`.
- **PROVEN** the installer's caller reads a live object chain from TLS slot `+0x68`: `2a0eacb: mov 0x68(%rax),%rax; 2a0eacf: mov 0x60(%rax),%rax`.
- **Structurally**, because the installer does `mov (%rax),%r15` on its r8 arg, passing `r8 = &scheduler+0x40` would load that live closure without fabricating a heap object. **UNPROVEN** that this pointer is the semantically correct arg3 for this installer: the `+0x40` closure is installed only on the TRUE path of `2a12e80` (`2a12eef: cmpq $0x0,0x28(%rbx); 2a12ef4: je 2a12f21`), which requires `+0x28` already nonzero — the very field this installer writes (circular). Harness observed `+0x28=0`, `al=0` at every hit, so the TRUE path is not reached. No arg substitution, object fabrication, guest write or apply was performed.

## Evidence / NOT done

Evidence: dis_2a0e8c0_full.txt (361 lines), dis_2a1e3a0.txt, dis_2a12e80.txt, /tmp/opencode/n1-static-a/report_n14.md, /tmp/opencode/n1-harness/report_n14.md. NOT DONE: no runs/device/captures/guest writes/gate/seeder/fake returns/path remaps/apply. UNPROVEN: arg2/arg3 provenance at the genuine native entry, exact type of the `+0x68` object, whether `0x2a1e630` is the intended closure, and any runtime value of `+0x40` in harness. Next smallest step: read-only confirm at harness which word r8 points to when `2a0ed0f` executes, and compare it with `0x40` of the TLS scheduler object.
