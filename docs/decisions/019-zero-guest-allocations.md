# Decision 019 — Zero guest allocations at the allocator bridge

Status: accepted (lead). Evidence: worker-1,
`analysis/handle/handle-origin.md`, REA
`ev_12b97ce17ac58a5e2de32bd40589deb2ac7d32100927239855d0d834e02873e6`
plus `ev_d838d804`.

## Root cause

The bogus handle `0xffffff3085894cff` in `[rbx+0x68]` is **uninitialized guest
allocation**, not a JNI handle and not BSS. The lazy link function at
`0x234fff8` writes its backing object B at `0x23500c6`
(`mov %r14,0x68(%rbx)`) but only when the guard at `0x2350044`
(`cmpq $0,0x68(%rbx)`) sees zero. A/B NOPing the guard at two timings did not
change the fault, so the linker is never reached for this handle — the bad
value is present at allocation time. The value is non-canonical; the guest
itself validates magic `0xc0dedbad` at `0x2333109`.

## Fix

- **Unfreeze `src/compat/bionic_bootstrap/bionic_bootstrap.c` for worker-2
  only** (ownership update after both workers requested it; worker-1's analysis
  task #387 is complete and must not edit code).
- In the native host allocator backing the six bridges — specifically
  `host_allocate()` (`allocate` bridge `0x1dbab02`, `small-allocate`
  `0x1dbad62`) and `host_reallocate()` (`0x227aafa`) — zero the returned
  block (`memset(p, 0, n)`) before returning. Consider the aligned path
  (`0x1dd60e5`) for consistency. VALUE = 0.
- Keep all build-ID gating, bridge fingerprints, seed ordering, evidence IDs
  and phase-2/3 behaviour otherwise unchanged.
- Fallback if zeroing is insufficient: writer-side hook at guest RVA
  `0x23331ae` (rbx = handle) to zero `[rbx+0x68]` when non-canonical.

No reader-side guest patches (decision 017) and no false marker (decision 013).
