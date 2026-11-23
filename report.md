# n3 #673 static-B — rbx@6c8e512 origin + wrapper table pointer (READ-ONLY)

Engine SHA 3e498d64, taskset -c 4,5, worktree /tmp/opencode/n3-static-b.
FAULTING INSTRUCTION FIRST: `6ca800e: ff 90 20 07 00 00 call *0x720(%rax)` (RA `0x6ca8014`).

## RESULT

1. `rbx` at `6c8e512` = the builder's entry `rdi` = **`*(caller's rsi argument)`** — a slot in a **host/bridge-provided callback argument struct**, not a guest heap allocation. The guest pre-seeds that slot with the original JNIEnv table pointer.
2. At wrapper `6ca8008: mov (%rax),%rax`, `rax` is the getter-`6c89e70` buffer and `[rax]` is the **ORIGINAL table pointer**, captured before the builder swaps the holder to the copy. The wrapper dereferences the **original**, not the patched copy.

## PROVEN — rbx chain (quoted raw)

Builder `0x6c8d840` (dis_builder_fn.txt):
```
6c8d84a: 48 89 fb        mov    %rdi,%rbx     ; rbx = entry rdi
6c8d85e: 4c 8b 3b        mov    (%rbx),%r15   ; r15 = *(rbx)
6c8d86f: 4c 89 38        mov    %r15,(%rax)   ; buffer[6c89e70][0] = *(rbx)
6c8e512: 4c 89 33        mov    %r14,(%rbx)   ; *(rbx) = copy
```
Caller `0x6c8e8a8` (dis_callers_fn.txt): `6c8e8bd: mov %rsi,%rbx`; `6c8e9a5: mov (%rbx),%rdi`; `6c8e9a8: mov %rdi,0x18(%rax)`; `6c8e9ac: call 6c8d840`. So builder rdi = `*(caller rsi)`.
Caller `0x6c8ea52` seeds that same slot explicitly from the saved original:
```
6c8eb79: 49 8b 47 18     mov    0x18(%r15),%rax   ; buffer[6c89e50][0x18] = original table
6c8eb7d: 48 89 03        mov    %rax,(%rbx)       ; *(rbx) = original table
6c8eb80: 48 8b 3b        mov    (%rbx),%rdi
6c8eb83: e8 b8 ec ff ff  call   6c8d840
```
(identical seeding at `6c8ef2b/6c8ef2f/6c8ef32/6c8ef35`). Sender of `0x18(%r15)`: `6c8e9a8: mov %rdi,0x18(%rax)`.
Host ownership: JNI_OnLoad registers the caller function pointer into a global, and the argument struct is tagged (dis_jnionload_reg.txt):
```
227b979: 48 8d 05 28 2f a1 04  lea ... # 6c8e8a8
227b980: 48 89 05 c9 32 a6 05  mov %rax,0x5a632c9(%rip)  # 7cdec50
227b9b1: 4c 89 33              mov %r14,(%rbx)           ; tag [arg]=0x7cdec20
```
No guest-side reader of global `0x7cdec50` exists in the engine (only the JNI_OnLoad writer), so the callback is invoked by the host/bridge; its `rsi` is host-supplied. The guest-side host table is global `0x7cdec60` (`6c8e987: mov ... # 7cdec60; call *0x30(%rax)`).

## PROVEN — wrapper table pointer (dis_wrapper_6ca8008.txt)

```
6ca7ffa: e8 51 1e fe ff  call   6c89e50
6ca7fff: 48 8b 58 18     mov    0x18(%rax),%rbx  ; rbx = original table (env arg)
6ca8003: e8 68 1e fe ff  call   6c89e70
6ca8008: 48 8b 00        mov    (%rax),%rax      ; rax = buffer[6c89e70][0]
6ca800b: 48 89 df        mov    %rbx,%rdi
6ca800e: ff 90 20 07 00 00  call   *0x720(%rax)
```
`buffer[6c89e70][0]` is written ONLY by `6c8d86f: mov %r15,(%rax)` (sole `mov ...,(%rax)` after `call 6c89e70` in the whole engine). Guard `6c8d861: cmp %rax,%r15; 6c8d864: je 6c8e515` skips the builder once holder[0] == copy, so the buffer keeps the ORIGINAL pointer captured at the first build. The copy (buffer from `6c89e90`) is stored only into `*(rbx)` at `6c8e512` and is NOT what the wrapper reads.

## UNPROVEN / RETRACTED

- UNPROVEN: how the harness observed `[bridge_table+0x720]==base+0x6ca7f72` given that this path writes fn224 into the COPY while the wrapper reads the ORIGINAL. Could be pointer identity (harness sampling holder[0] after `6c8e512`) or a separate in-place writer not located here. Needs harness pointer comparison, not a static claim.
- UNPROVEN: exact host struct type at `caller rsi`; only that it is host-supplied via the global callback registration and seeded by the guest.
- RETRACTED: any claim that `6c8e512` makes the wrapper use the copy — it does not.

## Evidence / NOT done

Evidence: dis_wrapper_6ca8008.txt, dis_builder_fn.txt, dis_callers_fn.txt, dis_jnionload_reg.txt, report.md. NOT DONE: no runs/device/emulator/captures/guest writes/gate edits/seeder/fake returns/path remaps/apply; object OFF, no extension. Input SHA 8215519f per task statement (not re-verified; no new captures).
