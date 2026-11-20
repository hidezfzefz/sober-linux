# Decision 025 — Full guest-shaped RefBase reconstruction

Status: user-authorized after decision 024's zeroed-object substitution aborted
with `free(): invalid size` in guest teardown.

## Why

- decision 023 field normalisation removed the `0x2456079` fault but exposed
  more garbage fields (+0x20, then an indirect call into `.text`).
- decision 024 substituted a zeroed 0x1000 object with magic `0xc0dedbad` at +0
  and byte `0x02` at +0x1006. It is installed correctly but the guest aborts:

```
free(): invalid size
#2 libc abort
#8 guest 0x1dbe665 (free wrapper)
#9 guest 0x2333588  (return from `call *(%r15)` at 0x2333585)
```

A zeroed object does not satisfy the teardown path; a real guest-shaped
RefBase/strong-pointer object is required.

## Plan

- worker-1: publish `analysis/handle/refbase-layout.md` — trace `r15` at
  `0x2333585` to its function-pointer table (suspected `0x726c0b8`), identify
  the destructor slot, and specify the COMPLETE object layout (magic, vtable,
  weakref/refcount fields, offsets, initial values) with REA evidence.
- worker-2: implement the layout in `src/jni/` as a native object factory with
  guest-style vtable; release/destroy thunks are safe native no-ops and the
  refcount is initialized as the guest expects.
- worker-3: substitute this object for the zeroed buffer in the `0x23331ae`
  stub and re-run `tests/app-shell.sh` with the strict DECISION 013 gate.

No other patches; all phase-2/3 checks must remain green.
