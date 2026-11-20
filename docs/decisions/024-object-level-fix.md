# Decision 024 — Object-level fix instead of per-field normalisation

Status: accepted (lead).

## Result of decision 023

worker-3's opt-in field normaliser at `0x23331ca` worked exactly as designed:
the `0x2456079` validator fault disappeared, `rdi` became NULL, and the guest
took the safe `je 0x23331dd` path. But the next instruction faulted at
`0x23331e6` (`mov 0x324(%rax),%ecx`) with `rax` garbage loaded from
`[rbx+0x20]`. The same guest object has multiple garbage fields.

## Ruling

Per-field normalisation is whack-a-mole. Fix the object pointer once at its
source:

```
0x23331ae: 48 8b 1f    mov (%rdi),%rbx     ; slot -> A
0x23331b1: 48 85 db    test %rbx,%rbx
0x23331b4: 74 ...      je 0x2333588
```

Patch 6 bytes at `0x23331ae..0x23331b3` with `call rel32` + `nop` to a native
stub: load `(%rdi)` into `rbx`; if it is non-canonical or `*(u32*)rbx !=
0xc0dedbad`, substitute a zeroed native buffer (>=0x1000 bytes) with
`*(u32*) = 0xc0dedbad`; then `test %rbx,%rbx; ret` to `0x23331b4` so the
guest's own `je` works. First test opt-in (`SOBER_OBJECT_FIX=1`), all other
patches off, strict DECISION 013 gate unchanged. worker-1 to confirm the
minimal required non-zero fields.
