# Layout spec for the handle consumed at 0x23331ae (decision 025)

Owner: **worker-1**. AppImage BuildID `204ad049…`. Evidence: REA
`ev_d838d804…` (full `.text`), APS2 decode via
`analysis/bootstrap/decode_aps2.py`, runtime capture `ev_12b97ce1…`.

## 1. What `call *(%r15)` at 0x2333585 actually is

R15 in helper `0x233319a` is loaded at `0x2333255`:

```
2333255: lea 0x726c0b8(%rip),%r15      # global function-pointer slot
...
2333576: xorps %xmm0,%xmm0
2333579: movups %xmm0,0x10(%r14)       # zero a 16-byte slot
233357e: movups %xmm0,(%r14)           # zero another 16-byte slot
2333582: mov %rbx,%rdi                 # rdi = the handle object
2333585: call *(%r15)                  # <-- indirect call, ~30 such sites
2333588: pop rbx ... ret
```

The packed-relocation decode of the table around that slot gives:

| Slot VMA | Addend (function) | Meaning |
| --- | --- | --- |
| `0x726c0b0` | `0x227aafa` | reallocate bridge |
| **`0x726c0b8`** | **`0x1dbe559`** | **free bridge — this is `*(r15)`** |
| `0x726c0c0` | `0x253225e` | guest internal |
| `0x726c0c8` | `0x1dbad62` | small-allocate bridge |
| `0x726c080..0x726c0a8` | `0x6a33e2c`, `0x6a3419b`, `0x6a3458a`, `0x6a346db`, `0x6a2db1d`, `0x6a3480e` | guest allocator helpers |

**Conclusion:** the "destructor" at `0x2333585` is simply **`free(rbx)`** — the
guest's own free bridge (`0x1dbe559` → `0x1dbe000` mimalloc free). It is NOT a
virtual `RefBase` destructor and there is no RefBase vtable at `0x726c0b8` —
that address is one slot in the guest's **allocator function table**. This
explains `free(): invalid size`: the pointer passed to `call *(%r15)` is not a
valid allocation from the same allocator (or is an interior/wrong pointer),
not that a missing vtable entry was called.

## 2. Consequences for the "full RefBase construction" plan

- No RefBase/weakref vtable or refcount fields are consumed by this teardown
  path. The `call *(%r15)` sites in `0x233319a` are allocator calls (free /
  reallocate / small-allocate), not virtual dispatch.
- Therefore **there is no vtable layout to construct**. The requirement is
  simpler and stricter: the pointer in RBX at `0x2333585` must be exactly a
  pointer returned by the same native allocator (correct chunk header/size),
  and the guest must not have written past its allocation.
- A synthetic object must be allocated with worker-1's/host's **native
  allocator** (the same `host_allocate`/`host_free` pair that backs the
  `0x1dbab02`/`0x1dbe559` bridges), with a size at least the largest field
  offset the guest writes: the helper touches `+0xf90` (D pointer), `+0x1005/
  +0x1006`, `+0xe08`, `+0xab0`, `+0xa58`, `+0xa50`, `+0x735`, `+0x300`,
  `+0x20`, `+0x10`, `+0x8`, `+0` => allocate/calloc **>= 0x1010 bytes** and
  zero it. `worker-3` currently uses 0x1000, which is **too small** — the
  guest writes at `+0x1005/+0x1006`, i.e. beyond a 0x1000 object, corrupting
  the adjacent heap chunk header. That alone can produce `free(): invalid
  size`.
- Minimal field values: `+0` = `0xc0dedbad` (magic validated at `0x2333109`);
  `+0x1006` bit 1 = skip validator; everything else 0 (all guest paths have
  null checks for +0x68, +0x20, +0x70, +0x735, +0xab0 as documented in the
  earlier handoff).

## 3. Concrete recommendation to worker-2/worker-3

1. Allocate the substitute object with the **host allocator paired to the
   guest free bridge** (worker-1's `host_allocate`), size **0x1010 or 0x2000**,
   zero-filled; magic `0xc0dedbad` at `+0`, flag byte `0x02` at `+0x1006`.
2. Do NOT fabricate a RefBase vtable/refcount — none is consumed here.
3. If `free(): invalid size` persists with a correctly-sized allocation, the
   pointer passed to `0x2333585` is not our substitute (it is reloaded from a
   guest field), and only the attach-mode GDB writer trace can find where.
4. Keep every experimental guest patch off by default (decision C) and the
   strict DECISION 013 gate intact.

## 4. Disassembly of the destructor target `0x1dbe559` (lead's explicit request)

The APS2 addend for slot `0x726c0b8` is exactly `0x1dbe559`. Its original
guest code is an **arena allocator free**, not a RefBase destructor:

```
1dbe559: mov %rdi,%rsi                       ; save block pointer
1dbe55c: mov 0x7cd9040(%rip),%rax            ; arena table (guest global)
1dbe563: mov %rdi,%rcx
1dbe566: shr $0x1d,%rcx                      ; arena index = ptr >> 29
1dbe56a: mov (%rax,%rcx,8),%rax              ; arena = table[arena index]
1dbe56e: mov %esi,%ecx
1dbe570: shr $0xd,%ecx
1dbe573: and $0xfff8,%ecx                    ; page slot = (ptr >> 13) & 0xfff8
1dbe579: mov (%rax,%rcx,1),%rdi              ; page = arena[page slot]
1dbe57d: test %rdi,%rdi ; je 1dbe5a1          ; no page -> return
1dbe582: mov (%rdi),%rax
1dbe585: xor %fs:0x0,%rax                    ; page-header canary vs TLS FS[0]
1dbe58e: jne 1dbe5a2                         ; mismatch -> slow path
1dbe590: mov 0x18(%rdi),%rax                 ; intrusive free list head
1dbe594: mov %rax,(%rsi) ; mov %rsi,0x18(%rdi)
1dbe59b: decw 0x10(%rdi)                     ; page live count
```

A faithful guest-arena block would therefore require the arena table at
`0x7cd9040`, a page header whose first word equals `FS[0]`, a free-list head
at page `+0x18` and a live count at page `+0x10`. **However**, `0x1dbe559` is
also the `free` bridge that phase-2's bootstrap patches with a 12-byte trampoline
to our native `host_free()` (glibc `free`). In every harness run that patch is
installed, so the arena-free code above **never executes**; the call becomes
plain glibc `free(rbx)`.

Consequently:

- there is **no guest RefBase/arena metadata to reconstruct** in this harness;
- `free(): invalid size` is glibc rejecting a pointer that is not a valid
  host-heap allocation;
- therefore the pointer in RBX at `0x2333585` is **not** our calloc'd substitute
  (it must be reloaded from a guest field after our stub runs), and this cannot
  be fixed by any static object layout. Only the attach-mode GDB watchpoint /
  a dump of RBX at `0x2333585` can identify where that pointer comes from.

Definitive answer to decision 025: full RefBase reconstruction is
**not applicable** — the teardown calls the patched free bridge, and the
residual problem is a wrong pointer value, not a missing object layout.

## 5. Evidence

- `ev_d838d804…` — full guest `.text` disassembly (helper `0x233319a`,
  `0x2333255`, `0x2333585`, free bridge `0x1dbe559`).
- APS2 packed-relocation decode with `analysis/bootstrap/decode_aps2.py`:
  slot `0x726c0b8` addend `0x1dbe559`, `0x726c0b0` addend `0x227aafa`,
  `0x726c0c8` addend `0x1dbad62`.
- Runtime capture of the abort: `free(): invalid size` via guest
  `0x1dbe665` <- `0x2333588` (lead/worker-3 reports).
