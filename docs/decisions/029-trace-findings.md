# Decision 029 — Phase 4b trace findings (still blocked)

Status: accepted (lead). Strict DECISION 013 gate unchanged.

## Confirmed corrections

- `src/jni/native_jni.c` had JNI string functions at wrong slots; fixed to
  canonical indices (159/161/162/163/165/166). `jni_probe` green.
- APS2 static addend for allocator slot `0x726c0c0` is `0x253225e`, but the
  slot is REWRITTEN AT RUNTIME by `mov %r9,0x726c0c0(%rip)` at `0x231e915` to
  `0x231f0e2` — a zeroing calloc wrapper (`nmemb*size`, then `memset@plt`).
  This is why the 7th bridge at `0x253225e` was never hit.
- An 8th bridge at `0x231f0e2` IS hit: three 0x1100 blocks are allocated via
  our zeroing host_calloc (e.g. `ptr=0x7fe8f406f890`), verdict from
  `SOBER_TRACE_CALLOC=1`. Yet the fault at `0x2456079` persists with stale
  `0xffffff3085894cff` in `[handle+0x68]`.
- The only guest magic writer `0x231f34b` (in `JNIWebLoginProtocol_init`) has
  hit counter 0 in this path — not the source here.
- Our own synthetic `0xc0dedbad` objects were audited and given zero `+0x20`/`+0x68`;
  that also did not remove the fault.
- Page-protection write-watch armed on the three handles but captured no WRITE
  and hung (SIGSEGV handler conflict / duplicate DSO load). Inconclusive.

## Current best hypothesis

The consumed object pointer itself is bogus (points into guest `.text` or is
one-byte-misaligned), so `[obj+0x68]` reads static code bytes rather than a
heap field. Two read-only research subagents are now attacking: (a) a custom
ptrace hardware-watchpoint tracer, and (b) the static call chain from the five
startup RVAs to the slot at `0x23331ae`.

## Regression

Phase-2/3 green throughout: `guest init complete`, `first frame rendered`,
`verify-native.sh`, ctest 2/2, ldd libc-only, engine hash unchanged.
