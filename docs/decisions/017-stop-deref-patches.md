# Decision 017 — Stop per-dereference guest patches

Status: accepted (lead).

## Why

worker-3 correctly reported that the narrow patches applied so far do not
converge:

- `0x245605d` validator -> `mov eax,4; ret` (decision 014)
- `0x23335c0` `mov rdi,[rdx+0x120]` -> `xor edi,edi; nops`
- `0x23335e7` `mov rcx,[rbx+0xf90]` -> `xor ecx,ecx; nops`

Each patch merely propagates the same bogus guest-internal pointer to the next
dereference; the latest fault is `0x6a30c62` with
`rax=0x9be800000a60b38d`, and no genuine guest→Java callback has fired.

## Ruling

- Revert the two narrow dereference patches at `0x23335c0` and `0x23335e7` to
  their original bytes. Do not add more one-off dereference patches.
- Keep phase-2/3 checks green and the strict DECISION 013 marker gate.
- Fix the source instead: the guest consumes JNI objects with an internal
  layout our synthetic handles do not fully match. Known offsets from
  worker-1/worker-2 traces:
  - `jobj + 0x68` -> object B (guest expects magic `0xbab1e`)
  - `B + 0x120` and `B + 0xf90` -> read as pointers
  - object B first dword `0xc0dedbad` on the arg1 side
- worker-1 to deliver the expected struct sketch with evidence; worker-2 to
  extend the synthetic `SrObject` layout with canonical, self-consistent
  pointers; worker-3 to re-run only after that lands.

## Update — single robust patch (worker-1)

worker-1 found that the same garbage field `[rbx+0xf90]` is dereferenced twice
inside helper `0x23335a0` (`0x23335e7` and `0x2333636`), so patching one field
just moves the fault. Preferred single patch: overwrite the ENTRY of
`0x23335a0` (original `55 48 89 e5 53 50 48 89 fb`) with `31 c0 c3`
(`xor eax,eax; ret`). The helper only zeroes `[rbx+0xa50/0xa58]` and optionally
logs; it is called indirectly via `call *(%r15)` at `0x2333585`, so a leaf
return is safe for a short harness run. Keep the `0x245605d` and `0x23335c0`
patches; revert `0x23335e7`. If this still leads to new garbage dereferences,
stop patching and report the remaining gap honestly. Evidence: REA
`ev_d838d804`, disassembly `0x23335a0..0x2333681`.
