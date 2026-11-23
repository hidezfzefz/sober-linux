# OVERNIGHT n1-2 static-A #650 — writers of TLS[0x722e540]+0x410

Read-only; cores 2-3; no runs/device. Every success claim quotes a raw line.
Evidence: `dis_lazy_destructor.txt`, `dis_destructor.txt`, `dis_slot.txt`,
`dis_2a0de60.txt`, `dis_predicate.txt`, `raw_slot_all.txt`.

Faulting instruction first: `0x6ca7f95: call 0x2a13350` (`si_addr=rsp-8`).

## 1. The slot and its only proven writer — PROVEN

`0x2a0de60` returns `&slot = TLS[0x722e540] + 0x410` (quoted
`2a0de7d: lea … # 722e540` → `2a0de84: call 2ea6440`,
`2a0deba: add $0x410,%rax`, `2a0dec1: ret`). The caller dereferences it
(`2a1335e: mov (%rax),%rax`) to obtain `obj`.

The **only** instruction found writing that exact slot is in the lazy
initialiser `0x2a0ded0`, quoted:
> `2a0df1e: lea … # 722e540` → `2a0df25: call 2ea6440` ; `2a0df2a: mov %rax,%r14`
> `2a0df2d: mov %rbx,0x400(%rax)`
> `2a0df41: mov %rax,0x408(%r14)`
> `2a0df48: mov %rax,0x410(%r14)`      <-- writes obj into the slot
> `2a0df4f: test %rax,%rax` / `2a0df52: je 2a0dfc1` (NULL -> assert handler at `0x727e910`)

The value written is the return of `2a0df3c: call 2a16390` with
`2a0df34: mov $0x80,%esi` and `rdi = [0x729d688]` — a valid, non-NULL
per-thread object (0x80-byte allocation path via `sched_getcpu`). So the slot
is initialised **once, to a valid obj**, never to NULL by this function.

A binary-wide scan found ~30 other `mov …,0x410(%rXX)` instructions, but all
are in unrelated functions with different base registers; none was proven to
target `TLS[0x722e540]+0x410` (UNPROVEN).

## 2. Callers back to an entry — PROVEN links, entry UNPROVEN

Quoted:
> `2a0de78: call 2a0ded0` — the ONLY direct caller of the writer function,
> inside getter `0x2a0de60`.
> `2a13354: call 2a0de60` / `2a13359: call 2a0de60` — predicate `0x2a13350`
> `2a1335e: mov (%rax),%rax` / `2a13361: cmpq $0x0,0x28(%rax)` — reads `obj+0x28`

`0x2a13350` is the guard used by the JNI wrappers (`0x6ca7f95`, `0x6ca21f4`,
`0x6c8f368`), which are reached from JNI calls made by capture-order natives.
A specific capture-order native / JNI_OnLoad / ctor / thread-start as the
first caller is **UNPROVEN** statically. Harness W32 recorded tid `1606363`
on the predicate/writer path (`[W32_OBJ #1] tid=1606363 …`), but no tid is
attached to the writer instruction itself.

## 3. Destructor `0x2ea6670` — does NOT write the slot — PROVEN

Quoted from `dis_destructor.txt`:
> `2ea6677: mov (%rdi),%rax` ; `2ea667f: dec %rax` ; `2ea6682: mov %rax,(%rbx)`
> `2ea6685: mov … # 72a1b08,%edi` ; `2ea6695: jmp pthread_setspecific@plt`
> other branch: `2ea66b8: mov 0x10(%rbx,%r14,8),%rcx` ; `2ea66c6: call 1dbe559` (free)

It decrements the state block's `+0` refcount and re-arms `setspecific` with
the key from `0x72a1b08`; the alternate branch walks the slot array at
`+0x10 + 8*i` and frees entries. It never executes `mov …,0x410(%rXX)`, so it
does **not** directly NULL the slot. Whether it frees the object stored at
slot index 128 (`+0x410`) is UNPROVEN.

## What NOT done
No diff applied, no patch, no runs/gdb/perf, no device/AVD/Frida/phone, no new
captures, no guest writes, gate edits, seeder, ledger edits, no credentials.
Other `+0x410` instructions not proven to target this slot; writer entry and
destructor's indirect free remain UNPROVEN.

## First divergence + next step
First divergence: harness slot holds a valid obj (written once by `0x2a0df48`)
with `[obj+0x28]==0`, while the wrapper recursion persists via the slot-0x720
self-pointer. Next: harness sets an entry breakpoint at `0x2a0ded0` with tid.
