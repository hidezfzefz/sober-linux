# WAVE28 #629 — full wrapper disasm, rax provenance, byte/slot checks

Read-only; no runs/device. Engine SHA `3e498d64…`. Faulting instruction named
first per the new rule; no stack return address is treated as the fault site.
Evidence: `dis_6ca21c6_6ca22b0.txt`, `dis_2315556.txt`, `bytes.txt`,
`dis_accessor_2ea6440.txt`, `dis_2a0de60.txt`, `dis_2a0ed45.txt`,
`dis_2a1362f.txt`, `capture_lines.txt`.

## (a) Full wrapper `0x6ca21c6..0x6ca22b0` — PROVEN

**Faulting instruction: `0x6ca229c: ff 90 48 05 00 00 = call *0x548(%rax)`.**
It dereferences **RAX** as a base, loads the 8-byte function pointer at
`[rax+0x548]`, and calls it. It is the FALSE (non-scheduler) inner JNI call of
the wrapper; the TRUE path uses the lambda at `0x6ca22ee`, which calls the same
slot at `0x6ca2340`.

Provenance of that rax (no global, no direct obj field):
```
6ca228b  call 0x6c89e70        ; -> 0x2ea6440(0x7282068) TLS descriptor accessor
6ca2290  mov (%rax),%rax       ; rax = *(field +0 of the TLS-resident object)
6ca2293  mov %r15,%rdi         ; r15 = [0x6ca2287] = *(0x6c89e50()+0x18) (env obj)
6ca2296  mov %r14,%rsi         ; r14 = arg2 saved at 0x6ca21db
6ca2299  mov %rbx,%rdx         ; rbx = arg3 saved at 0x6ca21d8
6ca229c  call *0x548(%rax)
```
`0x6c89e50/0x6c89e70` both `lea` a TLS descriptor (`0x7282048`/`0x7282068`)
and call `0x2ea6440`; that accessor reads key `0x72a1b08`, calls
`pthread_getspecific`, and returns `state[desc+0x10 index + 1]`. So rax is a
**field (+0x00) of a TLS-resident object**, not the scheduler `obj`.

Predicate at `0x6ca21f4 call 0x2a13350` then `test %al,%al; je 0x6ca2282`
tests `*(obj+0x28) != 0`, where `obj = *(TLS[0x722e540] + 0x410)` via
`0x2a0de60` (`0x2a0de64` descriptor `0x722e560`, `0x2a0de7d` descriptor
`0x722e540`, `0x2a0deba add $0x410,%rax`; predicate `0x2a1335e mov (%rax),%rax;
0x2a13361 cmpq $0,0x28(%rax)`).

**Guest writers found (PROVEN as instructions; exhaustiveness UNPROVEN):**
- `0x2a0ed45: mov %r15,0x28(%rbx)` — scheduler-object initializer (writes the
  callback word during construction).
- `0x2a1362f: movq $0x0,0x28(%rdi)` — struct zero-init (clears +0x28).
- The host-side `sober_jni_neutralize_guest_scheduler` memset at `p+0x28` is
  **not** a guest writer and is excluded by scope.
- `0x2ea6555: movq $0x1,(%r14)` / `0x2ea6561: mov %r12,0x8(%r14)` and
  `0x2ea656e: pthread_setspecific` populate the TLS state that ultimately
  supplies rax; no other guest writer of that field was located statically.

Capture-order natives before BaseDirs: `nativeSetRobloxChannel("")` (#70/#542),
`nativeSetRobloxVersion("2.740.931")` (#534), `nativeGameGlobalInit()` (#541).
Whether a `+0x28` writer sits on any of their paths is **UNPROVEN** statically
(no dynamic trace in this wave).

## (b) Byte verification — PROVEN
Raw file bytes at both sites are `ff 90 48 05 00 00`:
`0x6ca229c` and `0x2315556` are both `call *0x548(%rax)`. Hence `0x6ca229c`
is this wrapper's inner JNI call and **its return point is `0x6ca22a2`**
(`mov (%r12),%rcx` canary check). The rax supplying register/field at
`0x6ca229c` is `*(TLS[0x7282068]-resident object + 0x00)`; at `0x2315556` rax
is `*(env+0x00)` from `0x231554b mov (%rsi),%rax`. Stack RA `0x231555c` is
**not** the faulting instruction.

## (c) Slot convention (stated once) — PROVEN
`SRNativeInterface` = 4 reserved pointers then `fn[231]` at `+0x20`, so
`offset = 0x20 + 8*idx`, `fn[0] = GetVersion`, and the standard JNI index
(including the 4 reserved slots) is `idx + 4`. Thus `0x548 -> idx 165 ->
standard 169 -> GetStringUTFChars`; `0x550 -> idx 166`; `fn[169]` proper is at
`0x568` (GetObjectArrayElement).

## RA
`0x6ca21c6 -> 0x2a13350 (obj+0x28 predicate) -> FALSE 0x6ca2282/0x6ca228b ->
0x6ca2290 -> 0x6ca229c call *0x548(%rax) -> 0x6ca22a2 return/chk`.
Parallel helper: `0x2315532 -> 0x231554b mov (%rsi),%rax -> 0x2315556 call
*0x548(%rax) -> 0x231555c`.

## What NOT done
No diff applied/proposed for application, no harness edit, no runs/gdb/perf,
no device/AVD/Frida/phone, no new captures, no guest writes, no gate edits,
no ledger edits, no credentials. Writer exhaustiveness and dynamic predicate
value are UNPROVEN.

## First divergence + next step
First divergence: the `*(obj+0x28)!=0` scheduler gate on these GetStringUTFChars
wrappers and the capture-order/value deviations. Next: harness (faulting
instruction `0x6ca229c`, then registers) logs rax at `0x6ca2290` and the
predicate at `0x6ca21f4`.
