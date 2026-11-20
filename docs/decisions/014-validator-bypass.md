# Decision 014 — Native bypass of JNI object validator 0x245605d

Status: accepted (lead). Evidence: worker-1, REA `ev_d838d804`
(disassembly of `0x245605d`, `0x247dc8d`, `0x23331a3`).

## Problem

`nativeAppBridgeAppStart` (`0x247dc8d`) faults at guest RVA `0x2456079`:
`cmpl $0x000bab1e,(%rdi)` with `rdi = 0xffffff3085894cff` (a sign-extended
garbage handle). The validator `0x245605d` checks magic `0xbab1e` in arg0 and
`0xc0dedbad` in arg1. Its caller `0x23331d6` passes `rdi = *(rbx+0x68)`,
a field inside a JNI-derived guest object — **not** a global. Globals
`0x72a3268/70`, `0x72a3300` are phase-2 constructor state (writer at
`0x1dd695b`) and must not be seeded.

## Fix

Patch the first 6 bytes at `0x245605d`, originally `55 48 89 e5 41 57`, to:

```
b8 04 00 00 00 c3    ; mov eax, 4 ; ret
```

The validator returns 0 on failure and 4 on full success (`r13=4` at
`0x24560ad`). It has four direct callers (`0x23331d6`, `0x6a28023`,
`0x6a28257`, `0x6a3e3a3`), all plain `call` instructions, so a leaf patch is
safe — no caller depends on its stack frame.

Implement in the harness (worker-3) with mprotect RWX -> write -> restore RX,
verifying the original prologue before writing, exactly like the phase-2 bridge
patches. This is a native guest patch; no translation layer or extra runtime.

## Follow-up patch (decision 015)

After the validator bypass, startup advances (the guest writes real
`appData/ClientSettings` and logs) and then faults at `0x23335c0`:
`mov 0x120(%rdx),%rdi`, where `rdx` is the same sign-extended garbage handle
family in a guest object field at offset `0x120`. It calls shared-pointer
helper `0x2358475`, which has only two callers (`0x23335d5`, `0x23513ed`); its
caller at `0x23335da` takes a defined branch when `eax==0`.

Trial patch: overwrite the first 3 bytes of `0x2358475` (originally
`48 85 ff`, `test rdi,rdi`) with `31 c0 c3` (`xor eax,eax; ret`), using the
same mprotect RWX -> write -> restore RX procedure. Low risk (two callers, no
stack-frame dependency), but it is an experiment — if startup advances, refine
to patch only the offending dereference. Evidence: REA `ev_d838d804`.

DECISION 013's strict marker gate is unchanged: no `app shell rendered` until a
real guest→Java callback fires.
