# Decision 018 — Phase 4 blocked on guest-internal object state

Status: accepted (lead). Task #385 is blocked; phase-4 done condition NOT met.

## What was delivered and verified

- `analysis/jni/jni-map.md` — JNI boundary map (REA evidence, exact AppImage
  RVAs, JNIEnv slots, cached-ID globals, 21-entry guest→Java callback table).
- `src/jni/` — native JNIEnv (231 slots) + JavaVM (5 slots), pure
  ExceptionCheck/Clear, re-entrancy guard, Bionic-compatible TSD in the
  versioned libc shim, seeded cached jmethodIDs/flags, typed non-NULL object
  callbacks, defensive canonical handle normalization, and the full
  A/B/C/D synthetic object graph from `analysis/jni/recursion-note.md`
  Addendum 8. `jni_probe` self-test passes.
- `tests/app-shell.sh` — strict smoke test that FAILS with clear diagnostics
  rather than emitting a false marker.

## Blocker

With guest `JNI_OnLoad` skipped (it recursed via the scheduler-gated FindClass
wrapper) and cached IDs/flags seeded, three startup RVAs run
(`0x24650a9`, `0x233762c`, `0x24abf96`), then the guest faults at
`0x2456079` (`cmpl $0xbab1e,(%rdi)`) with `rdi=0xffffff3085894cff` — a
sign-extended, non-canonical pointer stored in a **guest-internal** object,
not in any handle returned by our bridge. Reverting all guest patches and
using the exactly-shaped synthetic object graph does not change this.

Per DECISION 013 the reduced gate is rejected: `app shell rendered` must be
gated on a genuine guest→Java callback, and none fires. Worker-1 has been
asked to trace the writer of the bogus `[rbx+0x68]` field; reader-side patches
are stopped (decision 017) because they only propagate the pointer.

## Phase-2/3 regression status

All preserved and green: `guest init complete`, `first frame rendered` with a
real guest render RVA call, `verify-native.sh`, ctest 2/2, `ldd` libc-only,
on-disk engine SHA unchanged.

## Conclusion

This milestone is not achievable by extending the JNI bridge alone; it needs
guest-side state reconstruction for an internal object the app-shell path
consumes. No false marker is emitted.
