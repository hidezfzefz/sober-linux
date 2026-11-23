# n8 WAVE3 #707 — harness (one page)

Worktree `/tmp/opencode/n8-harness` branch n8-harness. SOLE flock, cores 0-1,
Run D x3 with `SOBER_N8_CTOR3=1` + `SOBER_N8_PROBE=1`; READ-ONLY
`process_vm_readv` (no guest writes/hand-set bytes). SHA banner every run:
`[avd-boundary] verified /tmp/opencode/w11-arg0.json bytes sha256=8215519ff31bb79e66baa65baf9b09fe46ea172f64df33d2399db315fd27634b`
Evidence: `n8g_{1,2,3}.err`, `rund3_n8g.sh`.

## FAULTING INSTRUCTION FIRST — PROVEN
`guest+0x6caef67` — reported RIP after guest int3 at `0x6caef66` (HardAssert),
`signal=5`, `si_addr=0`, rc=139 3/3.
`[W28_FAULT_INSN] rip=0x00007f37a70cef67 guest_rva=0x06caef67`
`RBXCRASH: HardAssert (Expect to have mimalloc working, but it was not detected!)`

## Mimalloc globals, after ctor3 AND at fault — PROVEN (3/3, unchanged)
`[N8G AFTER_CTOR3] [7cd8ff8]=0x0000800000000000 [7cd9048]=0xffffffffffffffff [7cd9040]=0x00007f37a01ab000 alloc=0x0000560b82a97df0 bit41=43 bitmap_bit=0000000000000001 ptidx=000000000002b05c ptword=0x0000000000000000`
`[N8G AT_FAULT]` — byte-for-byte identical in the same run (and analogous
values in runs 2/3 with different ASLR addresses).

| global / derived | value | zero? |
|---|---|---|
| `[0x7cd8ff8]` arena low bound | `0x0000800000000000` | NO |
| `[0x7cd9048]` commit bitmap | `0xffffffffffffffff` | NO (all ones) |
| `[0x7cd9040]` page-table base | `0x00007f37a01ab000` | NO |
| ctor3 alloc ptr (from first USABLE hit rdi) | `0x0000560b82a97df0` | NO |
| `bit41 = (p>>41)&63` | 43 | n/a |
| `bitmap_bit` | 1 | NO |
| `ptidx = p>>29` | `0x2b05c` | n/a |
| **`ptword = [page_table + ptidx*8]`** | **`0x0000000000000000`** | **YES** |

## Does `1dbe4fc` see a registered pointer? — NO (PROVEN)
Two independent rejections on the same data:
1. `[0x7cd8ff8] = 0x800000000000` while `alloc = 0x560b82a97df0` — the
   pointer is **below** the arena low bound ⇒ `1dbe514: cmp %rdi,0x7cd8ff8;
   jbe ... ret 0` returns 0.
2. The page-table word indexed by the alloc pointer (`p>>29 = 0x2b05c`) is
   **ZERO** ⇒ `1dbe531: mov [0x7cd9040],%rax; mov (%rax,%rcx,8),%rax;
   test; je ... ret 0` also returns 0. The commit bitmap bit is 1, so the
   registration is missing specifically in the page table.
Thus `1dbe4fc(alloc) = 0` ⇒ `test r15b,r15b` at `1db9a8a` sees zero ⇒
`je 1db9acf` ⇒ `bl` stays 0 ⇒ `0x7cdede0 = 0` ⇒ HardAssert int3. This
confirms wave-2's control-flow inference with runtime values and matches
static-A #705's prediction (host glibc `malloc(0x20)` never registers the
pointer in mimalloc's private page table; `1dbe4fc` is not bridged).

## NOT DONE
No ledger edit; no device/captures; no object build. UNPROVEN: who populates
`[0x7cd9040]` in a real boot (static #708 scope) and whether a genuine
mimalloc runtime would register this pointer. No questions asked;
DELETE NOTHING honored.

## Evidence
`/tmp/opencode/n8-harness/n8g_{1,2,3}.err`, `report_n8g.md`, `rund3_n8g.sh`.
