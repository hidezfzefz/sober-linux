# n2 step1 #664 static-B — TLS holder writer + table provenance (READ-ONLY)

Engine SHA 3e498d64, taskset -c 4,5, worktree /tmp/opencode/n2-static-b.
FAULTING INSTRUCTION FIRST: `6ca800e: ff 90 20 07 00 00 call *0x720(%rax)` (fn[224]); RA `0x6ca8014` (n1 harness). No apply.

## RESULT: NOT an in-place patch. NO immediate alert triggered.

At `6c8e4d3` r14 is a **guest-private copy**, not the table the guest was given. The builder copies vtable entries from the given table into a per-key TLS buffer belonging to a *different* key, then replaces the holder's table pointer with that copy.

## PROVEN — [TLS holder+0] writer(s) (quoted raw)

Getter keys (dis_getters.txt):
```
6c89e74: lea 0x5f81ed(%rip),%rdi  # 7282068
6c89e7b: call 2ea6440
6c89e94: lea 0x5f81ed(%rip),%rdi  # 7282088
```
Shared helper 0x2ea6440 (dis_tls_helper.txt) — the pthread_getspecific holder is a per-thread array; the getter returns a per-key buffer at `+8+index*8`:
```
2ea6455: mov 0x43fb6ad(%rip),%edi  # 72a1b08
2ea645b: call 6d322e0 <pthread_getspecific@plt>
2ea64fc: mov %r13,0x10(%rbx)      ; key index assigned lazily under mutex
2ea655a: movq $0x1,(%r14)         ; <-- writes [TLS holder ARRAY +0] = 1 on first alloc
2ea6561: mov %r12,0x8(%r14)       ; capacity
2ea656e: call 6d31d10 <pthread_setspecific@plt>
2ea6573: mov 0x8(%r14,%r13,8),%r15 ; per-key buffer for this getter
2ea65de: mov %r15,0x8(%r14,%r13,8) ; installs that buffer
2ea65e3: mov %r15,%rax            ; returned to caller 6c89e70
```
The per-key buffer **returned by getter 6c89e70** has its `[0]` written by the vtable builder itself (dis_builder_fn.txt):
```
6c8d84a: mov %rdi,%rbx            ; rbx = holder holding given table at [0]
6c8d84d: call 6c89e90             ; buffer C (key 7282088) -> rax
6c8d85b: mov %rax,%r14            ; r14 = COPY DESTINATION
6c8d85e: mov (%rbx),%r15          ; r15 = *(holder) = given table pointer
6c8d86a: call 6c89e70             ; buffer B (key 7282068)
6c8d86f: mov %r15,(%rax)          ; <-- writes [getter-6c89e70 buffer +0] = given table ptr
6c8d872: call 6c89e70
6c8d877: mov (%rax),%rax
6c8d87a: mov (%rax),%rax          ; first vtable entry of given table
6c8d87d: mov %rax,(%r14)          ; copy into buffer C
```

## PROVEN — r14 provenance at 6c8e4d3 (dis_builder_fn.txt / dis_builder_full.txt)

```
6c8e4cc: lea 0x19a9f(%rip),%rax  # 6ca7f72
6c8e4d3: mov %rax,0x720(%r14)    ; self-pointer written into the COPY
6c8e50b: mov %rax,0x740(%r14)
6c8e512: mov %r14,(%rbx)         ; <-- guest saves the COPY as the real table
6c8e51f: ret
```
r14 is set ONLY at `6c8d85b: mov %rax,%r14` from `call 6c89e90` (key `0x7282088`), distinct from getter `6c89e70` (key `0x7282068`). The neighbour writes `mov %rax,0x8(%r14)`, `0x10(%r14)` … `0x740(%r14)` (dis_builder_full.txt) are a full ~234-entry vtable copy. Builders are called with `rdi = (%rbx)` (given table in holder): `6c8e9ac`, `6c8eb83`, `6c8ef35: call 6c8d840`; at `6c8e9a0/6c8e9a8` the guest also stores the original pointer at `0x18(%rax)` of getter-6c89e50's buffer. `227bde8: call 6c8e520` is a separate function (its own prologue at `6c8e520`), not this builder; exact caller chain from JNI_OnLoad to `6c8d840` is UNPROVEN here.

## UNPROVEN / RETRACTED

- UNPROVEN: exact identity of `*(rbx)` on the harness path (whether it is literally the bridge table given to JNI_OnLoad); requires harness read-only comparison of `rbx@6c8e4d3`, `[bridge_table+0x720]`, and `rax@6ca800e`.
- UNPROVEN: whether any *other* code path patches the given table in place — this instruction does not.
- RETRACTED: any claim that `6c8e4d3` modifies the harness-provided table; it modifies a copy. n1 conclusion "guest self-pointer at fn224" stands, but its storage is the guest copy, not the bridge table.

## Evidence / NOT done

Evidence: dis_tls_helper.txt (159 lines), dis_builder_fn.txt, dis_builder_full.txt, dis_getters.txt, callers_builder.txt, dis_onload_caller.txt, report.md. NOT DONE: no runs/device/emulator/Wine/box64/QEMU/captures; no guest writes/gate edits/seeder/fake returns/path remaps/apply. Input SHA 8215519f per task statement (not re-verified here; no new captures).
