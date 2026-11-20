# Decision 026 — Phase 4 closed BLOCKED after exhaustive attempts

Status: accepted (lead). DECISION 012's done condition is NOT met; the strict
gate correctly withholds `app shell rendered`.

## Every fix attempted (all empirically ruled out)

1. JNI synthetic A/B/C/D object graph with canonical pointers and magics.
2. Bionic-compatible pthread TSD in the versioned libc shim.
3. Seeded cached jmethodIDs/flags; pure ExceptionCheck/Clear; re-entrancy guard.
4. Corrected startup RVA JNI ABI (strings, AssetManager, Configuration,
   shared_ptr wrapper) — advanced startup from 3 to 5 RVAs.
5. Allocator zero-fill at all four bridged allocation paths.
6. Real native AAssetManager/AConfiguration handles.
7. Reader-side value normalisation at `0x23331ca` (+0x68) — removed the
   `0x2456079` fault, exposed +0x20.
8. Object-level substitution at `0x23331ae`: zeroed 0x1000 object -> heap
   corruption; 0x2000 object allocated via the guest's own patched bridge ->
   still `free(): invalid size`.
9. Full RefBase reconstruction considered and ruled out: `call *(%r15)` at
   `0x2333585` resolves via APS2 addend `0x1dbe559` to the guest free bridge,
   which phase-2 patches to native `host_free`. There is no
   RefBase/vtable/refcount metadata to reconstruct. The pointer freed in RBX
   is not our substitute (or the bridge pairing differs) and cannot be
   identified without a runtime capture.

## Root cause summary

The `0xc0dedbad` handle is created entirely by guest code through indirect
calls with no static caller or relocation (zero references to `0x234fff8` in
the whole 120 MB binary). Its `+0x68` field is left uninitialized with stale
machine-code bytes (`0xffffff3085894cff`, an unaligned `mov %r8,-0xd0(%rbp)`
slice — ASLR-invariant code *content*, not a pointer). GDB batch and
attach-by-PID attempts never produced a watchpoint hit (loader SIGTRAP flood
and ptrace stops), so the allocation site remains unknown.

## Final state

- Strict DECISION 013 gate intact; `tests/app-shell.sh` fails honestly.
- Phase-2/3 fully green: `guest init complete`, `first frame rendered` via
  real guest RVA, `verify-native.sh`, ctest 2/2, ldd libc-only, engine hash
  unchanged.
- No Android runtime, Wine or translation layer.
- Remaining gap: capturing the guest-internal allocation site that produces
  the handle, which needs a working runtime trace on this host.
