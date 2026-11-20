# Decision 007 — Loader-side init-ordering control is permitted

Status: accepted (lead). Clarifies DECISION A ("unmodified guest").

## Ruling

worker-3's proposal is **approved**: dlopen a private in-memory (memfd) copy of
the AppImage `libroblox.so` with `DT_INIT_ARRAYSZ` set to 0 so the native host
controls constructor ordering, apply worker-1's bootstrap, then drive the
constructor array at `base + 0x7220e58` with the validated skip ranges.

This is **not** a translation layer and does not violate "fully native":
- the on-disk engine is never modified;
- guest code and data are byte-identical — only a loader metadata tag in a
  private in-memory copy is neutralised;
- no emulation, no binary translation, no Android runtime is involved.

## Conditions (must hold for the milestone to count)

1. Hash the on-disk engine before and after the run; they must be identical.
2. Document the exact byte deltas between the original and the memfd copy —
   expected to be `DT_INIT_ARRAYSZ` only. Any code/data delta invalidates the
   result.
3. Replay all constructors in the ranges `[2,3)` and `[5,3618)` exactly as
   specified by the validated host-ABI profile; skip only those ranges. Report
   the index of any constructor that faults.
4. Emit a deterministic `guest init complete` signal only after all required
   constructors have been driven successfully and `dlopen` has returned.
5. Keep `build/sober-native` a native ELF: `ldd` shows only libc/ld-linux, no
   Wine/Proton/box64/Android runtime dependency.
6. Record in the handoff that "unmodified" means code/data/on-disk artifact;
   loader-init metadata is adjusted in memory for ordering control. Do not
   claim the raw ELF metadata is unchanged.

## Rejected alternative context

`glibc la_preinit` does not fire for dlopen'd objects (worker-3 verified with
an LD_AUDIT probe), and stock `dlopen` runs all 3618 constructors before
returning — so it cannot give the bootstrap a chance to run first. The memfd
init-array control is the chosen mechanism.
