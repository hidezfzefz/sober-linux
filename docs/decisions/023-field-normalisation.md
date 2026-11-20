# Decision 023 — Authorized targeted field normalisation at 0x23331ca

Status: user-authorized, overrides decision 017's blanket reader-patch ban for
this ONE patch only.

## Why

worker-1's backward trace proves the lazy-link function `0x234fff8` is never
called on this path (zero direct callers, zero pointers, zero relocations), so
no writer ever initializes `[A+0x68]`. GDB attach stalled in a ptrace stop with
no usable trace. The consumer at `0x23331ca` reads the stale field directly.
A single value-normalisation at that load is therefore root-cause-appropriate,
not whack-a-mole.

## Exact bytes (AppImage build 204ad049)

```
0x23331ca: 48 8b 7b 68    mov 0x68(%rbx),%rdi
0x23331ce: 48 85 ff       test %rdi,%rdi
0x23331d1: 74 0a          je 0x23331dd
0x23331d3: 48 89 de       mov %rbx,%rsi
0x23331d6: e8 ...         call 0x245605d (validator)
```

## Patch

Overwrite 6 bytes at `0x23331ca..0x23331cf` with `E8 <rel32>` + `nop`, calling
a native stub that:

1. `mov (%rdi),%rax` — recover object A from the slot (RDI still holds the slot).
2. `mov 0x68(%rax),%rdi` — load the field.
3. if the pointer is non-canonical (high 16 bits non-zero), substitute 0.
4. `test %rdi,%rdi` to set flags, then `ret` to `0x23331d1`, so the guest's own
   `je 0x23331dd` takes the safe null path and the validator call is skipped.

Keep it opt-in for the first test (`SOBER_FIELD_NORMALISER=1`), keep all other
experimental patches off, verify original bytes before writing, and preserve
phase-2/3 checks. The strict DECISION 013 gate is unchanged: no
`app shell rendered` without a genuine guest→Java callback.
