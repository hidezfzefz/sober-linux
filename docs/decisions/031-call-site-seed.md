# Decision 031 — Seed the missing +0x68 at the app-shell call site

Status: accepted (lead). Highest-confidence fix so far.

## Evidence

worker-4's dispatch map shows TWO call paths into consumer `0x233319a`:
- safe path: zeroes `[handle+0x68]` at `0x232004f`, then calls the consumer at
  `0x2320057`;
- failing app-shell path: `0x23330e7 -> 0x2333165 lea -0x48(%rbp),%edi ->
  0x2333168 call 0x233319a`, which skips the zeroing.

worker-2's zero-signal field log proved the three zeroed 0x1100 handles never
hold the bogus value; the consumed object comes via this skipping path.

## Patch (worker-3 owns src/linux/)

Exact bytes (objdump, AppImage build):
```
0x2333165: 8d 7d b8           lea -0x48(%rbp),%edi
0x2333168: e8 2d 00 00 00     call 0x233319a
```

Patch only the 5-byte call at `0x2333168` (verify original bytes) to
`e8 <rel32>` to a native stub in a near executable page:

```
push %rax
mov (%rdi),%rax          ; obj from the slot
test %rax,%rax
jz 1f
movq $0,0x68(%rax)
movq $0,0x20(%rax)
1: pop %rax
jmp 0x233319a           ; tail-jump; original return address 0x233316d is intact
```

Opt-in `SOBER_CALL_SITE_FIX=1`; other patches off; strict DECISION 013 gate.
