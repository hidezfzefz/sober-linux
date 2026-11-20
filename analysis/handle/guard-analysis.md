# Guard analysis: RVA 0x2350044 / 0x2350049 and the constant 0xffffff3085894cff

Owner: **worker-1** (task #387 follow-up, lead/user directive). Scope:
`analysis/handle/` only. AppImage engine BuildID `204ad049…` only.

## 1. What the guard actually checks (confirmed, no Ghidra needed)

Full disassembly of the lazy-link function `0x234fff8` (REA
`ev_d838d804…`, local `objdump -d`):

```
234fff8: push rbp; mov rbp,rsp; push r15; push r14; push rbx; push rax
2350002: mov $1,%r15d
2350008: test %rdi,%rdi ; je 0x2350270            ; B pointer required
2350011: mov %rdi,%r14                              ; r14 = B (backing object)
2350014: cmpl $0xbab1e,(%rdi)                       ; B magic
235001a: jne 0x2350270
2350020: mov %rsi,%rbx                              ; rbx = A (handle)
2350023: mov $2,%r15d
2350029: test %rsi,%rsi ; je 0x2350270
2350032: cmpl $0xc0dedbad,(%rbx)                    ; A magic
2350038: jne 0x2350270
2350044: cmpq $0x0,0x68(%rbx)                       ; *** GUARD ***
2350049: jne 0x2350270                              ; bail if already linked
...
23500c6: mov %r14,0x68(%rbx)                        ; THE WRITER (link B)
```

**Semantics:** the guard is a pure null-check on a QWORD field —
`[A + 0x68] == 0 ? link : bail`. It does **not** read a Looper, Binder,
ALooper, JNIEnv, JNI object state, or any flag register. The user/lead's
decision tree (fake ALooper / Binder scope-out / JNI field seeding) is **not
triggered** by this guard. It is a one-word back-pointer initialized lazily
by the guest's own linker, exactly as previously reported.

The value our harness observes in that field (`0xffffff3085894cff`) is
non-zero, so the guard bails and `0x23500c6` never links B. That is the
entire reason the later validator (`0x245605d`, `cmpl $0xbab1e,(%rdi)`) sees
a garbage pointer.

## 2. What 0xffffff3085894cff is

Facts (verified locally, no target execution in this step):

- ASLR-invariant: identical across two runs with different guest bases
  (lead's two-run check: `0x7f1508546079` vs `0x7ff6698d2079`).
- Little-endian byte sequence: **`ff 4c 89 85 30 ff ff ff`**.
- That 8-byte sequence occurs **42 times** in the guest's `.text` (e.g.
  `0x1dbfb7f`, `0x1e47c7c`, `0x231f82f`, `0x2331664`, `0x241fd15`, …), and
  its 5-byte core `4c 89 85 30 ff ff ff` occurs 245 times — it is the
  x86-64 instruction `mov %r8,-0xd0(%rbp)` (stack-save sequence) plus a
  neighbouring `0xff` byte.
- There is **no** `movabs $0xffffff3085894cff` immediate in the binary
  (lead's byte-search hypothesis was a false positive, confirmed).

**Conclusion:** the value is not a pointer and not a compile-time constant —
it is an **unaligned slice of guest machine code** (`ff 4c 89 85 30 ff ff
ff`) that happened to occupy the memory of the uninitialised field. This
explains both its ASLR invariance (code bytes are position-independent
data) and why writing to that location faulted in my earlier native hook
(decode 019 fallback): the field effectively aliases a read-only code
region. It is also consistent with the 8 occurrences of the same value
found in guest arena-range RW memory during runtime scanning.

Android `sp<>`/`wp<>` / RefBase sentinels checked: `INITIAL_STRONG_VALUE`
(`1<<28`), `OBJECT_LIFETIME_WEAK` flags, and the Bionic allocator debug fill
`0xdeadbaad` / `0xdead0000`-style patterns do **not** match. This is not an
Android invalid-handle sentinel; it is stale code bytes.

## 3. Implication for the fix

The correct state for `[A+0x68]` is either **0** (unlinked — the guest's
`0x2333593` null-check then takes its safe early-exit) or a **canonical
backing pointer B** linked by `0x23500c6`. Every source-level attempt so far
failed to change the field because the object is guest-internal and the
field aliases read-only bytes; writing it in-place is not possible from our
side. Therefore:

- no ALooper/Binder/JNI-subsystem fix is indicated;
- extending the synthetic A/B/C/D graph is explicitly not useful (user
  directive — confirmed by worker-3's unchanged reruns);
- the decisive step remains the GDB watchpoint run to identify **which guest
  code populated the field with this code-byte slice**, i.e. the allocation
  or copy site that must be fixed/zeroed at source.

## 4. Backward trace of RBX at the guard (user directive)

Inside `0x234fff8`, RBX is set at `0x2350020` by `mov %rsi,%rbx` — i.e. RBX
is simply the function's **second argument** (RSI), which the function validates
as the `0xc0dedbad` handle A. The first argument (RDI) is the backing object B
(magic `0xbab1e`) and is saved into R14 at `0x2350011`.

I then searched the entire 120 MB guest for any way this function could be
called with our handle:

| Search | Result |
| --- | --- |
| direct `e8 rel32` calls to `0x234fff8` in `.text` | **0** |
| 8-byte pointer / 4-byte immediate equal to `0x234fff8` anywhere in the file | **0** |
| RIP-relative `lea`/`mov` referencing `0x234fff8` | **0** |
| addend equal to `0x234fff8` in ELF `readelf -rW` relocations | **0** |
| addend equal to `0x234fff8` in the 565,390 APS2 packed relocations | **0** |

**Conclusion of the backward trace:** the lazy-link function `0x234fff8` is
never invoked on this path — there is no static caller, pointer, or relocation
that supplies RSI. The consumer `0x233319a` reads `[rbx+0x68]` directly at
`0x23331ca` *without* the linker ever having run. So there is no JNI argument,
cached-ID global, or synthetic object that "produces" the bogus handle at the
guard: the object is created entirely by guest code through indirect calls
(the consumer helper itself calls through a function-pointer table at
`0x726c0b8` at `0x2333263` etc.), and its `+0x68` field is simply left
uninitialised with stale code bytes. This is why every source-level graph /
allocator / handle change has left the value unchanged.

The only way to identify the exact creator is the attach-mode GDB watchpoint
(lead is running it) — statically the call chain terminates at an indirect
function-pointer table with no recoverable target.

## 5. Evidence

- REA full `.text` disassembly: `ev_d838d804f57d82aed028027e29942e4d198774cab04216a024f18ea6fd1a9bc4`.
- REA runtime process capture of the failing app-shell harness:
  `ev_12b97ce17ac58a5e2de32bd40589deb2ac7d32100927239855d0d834e02873e6`.
- Local raw-byte scan of the exact SHA (`ff 4c 89 85 30 ff ff ff` x42,
  core x245) and `objdump -d 0x234fff8..0x2350120`.
- Lead's two-run ASLR-invariance check (rdi identical across different guest bases).
