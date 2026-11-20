# Decision 032 — Phase 4b finished BLOCKED

Status: accepted (lead). DECISION 012's done condition is NOT met; strict gate
withholds `app shell rendered`.

## What this team established

- Corrected our JNI string-table slots (159/161/162/163/165/166) and hardened
  the UTF-8/UTF-16 accessors against foreign handles.
- Discovered the allocator slot `0x726c0c0` is REBOUND at runtime
  (`0x231e915`) from APS2 addend `0x253225e` to zeroing calloc wrapper
  `0x231f0e2`; added 7th/8th native bridges and unified all bridge allocation
  to one allocator family (glibc).
- Mapped the indirect dispatch exhaustively (`analysis/dispatch/dispatch-map.md`):
  the free bridge `0x726c0b8 -> 0x1dbe559` serves 37 call sites; the guest
  zeroes `[handle+0x68]` at `0x232004f` on the SAFE path but the app-shell
  path skips it.
- Proved via zero-signal field polling that the three zeroed 0x1100 handles
  never hold the bogus value; the consumed object is a DIFFERENT object.
- Proved via worker-3's fault dump that the slot at `[rbp-0x48]` holds guest
  RVA `0x233263f` — a RETRY/BAIL-OUT code address, i.e. stale stack residue
  being used as an object pointer, not a heap handle.
- Confirmed by full call-chain trace that `0x24abf96` really reaches consumer
  `0x23330e7` and the patched call site `0x2333168`.
- Tried the targeted writer-side fix: zero fields at call site (faulted on the
  bogus pointer), substitute a shaped host object (malloc_usable_size
  mismatch), substitute the guest getter's object (still faults in
  malloc_usable_size). All opt-in and now off by default.

## Residual gap

The app-shell path supplies a non-object pointer into the consumer. Fixing it
requires identifying and repairing the indirect dispatch caller that stores
that code pointer (`0x6a26ef2` / `0x2457721` families), or a runtime trace
that can capture the write. Neither the in-process watchers nor gdb/ptrace
tracer produced a reliable writer RIP on this host (SIGTRAP handler and
multi-thread attach conflicts, no hardware debug-register support).

## Final state

- Strict gate intact; `tests/app-shell.sh` fails honestly with diagnostics.
- Phase-2/3 verified green: `guest init complete`, `first frame rendered` via
  real guest RVA, `verify-native.sh`, ctest 2/2, ldd libc-only, engine hash
  unchanged. No Android runtime, Wine or translation layer.
