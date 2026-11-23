# OVERNIGHT n1 static-A #644 — 0x2a0de60, obj+0x28, writer chains

Read-only; no runs/device/phone; cores 2-3 used for heavy cmds via
`taskset -c 2,3`. Engine SHA `3e498d64…`. Every success claim quotes a raw
disassembly line. Evidence: `dis_2a0de60.txt`, `dis_accessor.txt`,
`dis_predicate.txt`, `raw_writer1_aligned.txt`, `raw_writer2.txt`,
`raw_callers.txt`.

Faulting instruction first (W32 harness): `guest+0x6ca7f95: call 0x2a13350`,
`si_addr=rsp-8` (stack overflow), repeated frame `0x6ca8014`.

## 1. What `0x2a0de60` returns (PROVEN)

Quoted from `dis_2a0de60.txt`:
> `2a0de64: lea … # 722e560` → `2a0de6b: call 2ea6440` (TLS accessor, guard byte)
> `2a0de70: mov (%rax),%al` / `2a0de72: and $0x1,%al` / `2a0de76: jne 2a0deae`
> `2a0de78: call 2a0ded0` (lazy initialiser when guard bit clear)
> `2a0de7d: lea … # 722e540` → `2a0de84: call 2ea6440`
> `2a0de89: lea … # 2a0dfe0` ; `2a0de90: lea … # 6d37bc0` ; `2a0de9a: call 2ea6130`
> `2a0deab: movb $0x1,(%rax)` ; `2a0deae: lea … # 722e540` → `2a0deb5: call 2ea6440`
> `2a0deba: add $0x410,%rax` ; `2a0dec1: ret`

It returns **`TLS[0x722e540] + 0x410`** — the ADDRESS of the slot inside the
per-thread descriptor state that holds the scheduler `obj` pointer. The guard
descriptor is `0x722e560`; the object template is `0x2a0dfe0` with type/vtable
descriptor `0x6d37bc0`, installed by `0x2ea6130`. Callers then dereference it.

## 2. Chain to `getspecific(0x72a1b08)` (PROVEN)

Quoted from `dis_accessor.txt`:
> `2ea6455: mov … # 72a1b08,%edi` → `2ea645b: call 6d322e0 <pthread_getspecific@plt>`
> `2ea650c: mov … # 72a1b08,%edi` → `2ea6512: call 6d322e0 <pthread_getspecific@plt>`
> `2ea655a: movq $0x1,(%r14)` ; `2ea6561: mov %r12,0x8(%r14)`
> `2ea6565: mov … # 72a1b08,%edi` → `2ea656e: call 6d31d10 <pthread_setspecific@plt>`

So `0x2a0de60` reaches `pthread_getspecific` only through the accessor
`0x2ea6440`, which reads the Bionic key from global `0x72a1b08` and calls
`getspecific` twice (initial probe + re-check after the once/mutex path); the
lazy branch allocates a zeroed state block, writes `+0=1` and `+8=slot count`,
and publishes it with `setspecific`.

## 3. What is at `[rax]+0x28` (PROVEN)

Quoted from `dis_predicate.txt`:
> `2a13354: call 2a0de60` ; `2a13359: call 2a0de60`
> `2a1335e: mov (%rax),%rax` ; `2a13361: cmpq $0x0,0x28(%rax)` ; `2a13366: setne %al`

Since `0x2a0de60` returns `&slot`, `mov (%rax),%rax` yields `obj`; `+0x28` is
the **scheduler installed-callback/task word** (nonzero ⇒ queued task). This is
the predicate used by the JNI wrappers. UNPROVEN: the exact type/vtable of
`obj` at runtime (the `0x6d37bc0` descriptor is passed to `0x2ea6130`).

## 4. Writers and their callers (links PROVEN, entries UNPROVEN)

- `0x2a0ed45` is in function `0x2a0e8c0` (quoted
  `2a0e8c0: push %rbp`, `2a0ed45: mov %r15,0x28(%rbx)`). Its only direct
  caller: `2a0e83b: call 2a0e8c0`, inside function `0x2a0e7c0`. No static
  caller of `0x2a0e7c0` was found ⇒ invoked via function pointer / queued
  scheduler task is likely but UNPROVEN.
- `0x2a1362f` is the leaf zero-init helper `0x2a13620` (quoted
  `2a1362f: movq $0x0,0x28(%rdi)`). Seven direct callers, all in the
  `0x2a0e..0x2a0f` scheduler region: `2a0e3d0`, `2a0e793`, `2a0ea3a`,
  `2a0ed1d` (inside the writer-1 function), `2a0efda`, `2a0f2ab`, `2a0f699`.

Real-boot path: not determinable from static data. Harness W32 (quoted from
its report) shows `0x2a0ed45` **0 hits**, `0x2a1362f` **68 hits** per run and
`[W32_OBJ #1] … [obj+0x28]=0000000000000000` — i.e. the clearing writer fires
but the installing writer never does on the harness path; a capture-order
native / JNI_OnLoad / ctor / thread-start entry for either writer is UNPROVEN.

## What NOT done
No diff applied, no harness edit, no runs/gdb/perf, no device/AVD/Frida/phone,
no new captures, no guest writes, no gate edits, no seeder, no ledger edits,
no credential handling. Exact obj type/size and writer entry paths UNPROVEN.

## First divergence + next step
First divergence: harness `*(obj+0x28)==0` (clearing writer fires, installing
writer never) plus the slot-0x720 self-pointer found in W32. Next: harness
logs the installing writer with tid and entry native.
