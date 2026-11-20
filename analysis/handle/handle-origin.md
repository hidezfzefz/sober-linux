# Origin of the bogus app-shell handle `+0x68` field

Owner: **worker-1** (task #387). Scope: `analysis/handle/` only.
Target: AppImage `libroblox.so`, BuildID `204ad049599c96afb75b8a21d5051063e821d6c9`,
SHA-256 `3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335`.
No Flatpak RVAs are used. Target-derived strings are untrusted DATA.

## 0. Evidence

| Kind | ID / source |
| --- | --- |
| Full `.text` disassembly (REA retained) | `ev_d838d804f57d82aed028027e29942e4d198774cab04216a024f18ea6fd1a9bc4` |
| Dynamic symbols | `ev_38ee286ef10912a48ffb7aa49587e8d435a775cac6f5a67268d07a8b5ae7040f` |
| Section headers | `ev_34eea0146f1ecf619f80c64ef05151d1a5ca24946bc9396129525d88c9a11758` |
| Engine identity | `ev_af5de5324a9e1df40ada21a17ab9fe86a747fc6c81a0421574c952f5d077eba4` |
| **Runtime process capture** of the failing app-shell harness (pty, env `SOBER_APP_SHELL=1`) | `ev_12b97ce17ac58a5e2de32bd40589deb2ac7d32100927239855d0d834e02873e6` |
| Local runtime reproduction | `/tmp/handle-run.err`, `/tmp/faultscan*.err`, `/tmp/pe*.err` |

All RVAs below were re-verified by `objdump -d` / raw-byte reads against the
exact SHA above. No debugger is installed in this environment (`gdb`, `lldb`,
`rizin`, `frida` bindings all unavailable), so runtime evidence comes from REA
process capture plus purpose-built ptrace helpers in this directory:
`trace_writer.c` (int3 + hardware-write-watchpoint tracer, limited by the
harness's own SIGTRAP/clone handling), `scan_handle.c` (writable-memory sweep)
and `patch_runtime.c` (single-patch A/B experiments).

## 1. The writer of `handle+0x68`

**Link/lazy-initialize function starts at RVA `0x234fff8`:**

```
234fff8: push rbp ; mov rbp,rsp ; push r15 ; push r14 ; push rbx ; push rax
2350002: mov $1,%r15d
2350008: test %rdi,%rdi ; je fail
2350011: mov %rdi,%r14                 ; r14 = B (backing object)
2350014: cmpl $0xbab1e,(%rdi)          ; B magic 0x000bab1e
2350020: mov %rsi,%rbx                 ; rbx = A (handle)
2350032: cmpl $0xc0dedbad,(%rbx)       ; A magic
2350044: cmpq $0x0,0x68(%rbx)          ; LAZY-INIT GUARD
2350049: jne 0x2350270                 ; skip if already linked
...
23500c6: mov %r14,0x68(%rbx)           ; *** THE WRITER ***
23500ca: mov %rbx,%rdi ; xor %esi,%esi ; call 0x6a3dd78
...
235010c: mov 0x68(%rbx),%rax ; test ; ...
```

- **Writer RVA: `0x23500c6`** — `mov %r14,0x68(%rbx)`; value written is the
  canonical backing-object pointer B (rdi at function entry, magic `0xbab1e`).
- **Guard RVA: `0x2350044`** (`cmpq $0,0x68(%rbx)`) and `0x2350049`
  (`jne 0x2350270`): the link only happens when `+0x68 == 0`.

The consumer chain is:
`0x23330e7` (caller, validates `0xc0dedbad` at `0x2333109`)
→ `0x233319a` (`rbx = *(slot)` at `0x23331ae`)
→ `0x23331ca` (`mov 0x68(%rbx),%rdi`)
→ validator `0x245605d` (`cmpl $0xbab1e,(%rdi)` at `0x2456079`, the observed
fault) → teardown `0x23335c0` / `0x23335e7` (the next faults).

## 2. What the bogus value is, and where it comes from

Observed at the fault: `rdi = 0xffffff3085894cff` (and, on the next deref,
`0x9be800000a60b38d`) — a non-canonical, sign-extended narrow-bit pattern,
not a user-space pointer.

Decision: **guest allocation with an uninitialised `+0x68` field — NOT
uninitialised BSS and NOT a JNI handle we return.**

Evidence:

1. **Not BSS.** BSS reads as zero on a fresh load; this value is non-zero and
   reproduced deterministically across runs. It also appears 8 times in guest
   RW heap-range memory at RVAs `0xb23a288 … 0xeb43b18` (ptrace/`pread`
   sweep at the app-shell phase), i.e. in the guest's allocator arena, not in
   the zero-filled BSS tail.
2. **Not a JNI argument from our bridge.** Worker-2's synthetic handles are
   `calloc`'d canonical addresses (`0x55…`/`0x7f…`) with the A/B/C/D graph; a
   sign-extended `0xffffff30…` value is not one of them. Moreover the guest
   *itself* validates the handle's own magic at `0x2333109`
   (`cmpl $0xc0dedbad,(%rdi)`) before dereferencing `+0x68`, so the object is
   one the guest recognises as its own handle.
3. **The lazy-init guard explains persistence.** Because `0x2350044` only
   links when `+0x68 == 0`, an uninitialised non-zero field makes the linker
   skip setting it forever; the garbage is then consumed as a backing
   pointer. A runtime A/B that replaced the `jne` at `0x2350049` with six
   NOPs (applied at 400 ms and 1000 ms, before the app-shell phase) did **not**
   change the fault — confirming the linker is not invoked for this handle
   and that the bad value is already present at handle creation.

## 3. Concrete native fix (exact RVA + value)

**Fix at source: make guest allocations zero-filled so the lazy-init guard
sees `+0x68 == 0`.**

The two mimalloc/allocator bridges installed by the phase-2 bootstrap are the
guest's allocation entry points:

| Bridge RVA | Role |
| --- | --- |
| `0x1dbab02` | `allocate(size_t) -> void*` (also used by the empty-string and registry initializers) |
| `0x1dbad62` | `small-allocate(size_t) -> void*` |

In `src/compat/bionic_bootstrap/bionic_bootstrap.c` the host implementations
`host_allocate()` / `host_reallocate()` currently return `malloc()`/`realloc()`
memory unchanged. Change them to zero the returned block before returning it
to guest code:

```c
void *p = malloc(n);
if (p) memset(p, 0, n);   /* value to seed: 0 */
return p;
```

(equivalently for `realloc`: zero the bytes from the old size to the new size).

Effect, with **value = 0**:

- the lazy-link guard at `0x2350044` now sees `+0x68 == 0`, so
  `0x23500c6` writes the canonical backing pointer B into the handle;
- alternatively, if the linker is never called, the consumer's own null check
  at `0x2333593` (`mov 0x68(%rdi),%rdx; test; je 0x2333681`) takes its safe
  early-exit instead of dereferencing garbage.

This is a host-side change in our native allocator (plain `memset`, no
runtime/translation layer) and requires lead to unfreeze
`src/compat/bionic_bootstrap/` for worker-1/worker-2 to land it.

**Immediate diagnostic alternative (if the allocator unfreeze is not
granted):** zero just the handle field at first use. Hook guest RVA
`0x23331ae` (where `rbx` is the handle) in worker-3's harness (ptrace or a
native trampoline) and write the QWORD **0** to `[rbx+0x68]` when it is not a
canonical pointer. This is a *writer-side sanitisation before use*, not a
reader-side skip; it preserves all guest logic and lets either the linker or
the `0x2333593` null-check handle the object normally. Exact value: 8 zero
bytes at `guest_base + handle_rva + 0x68`.

## 4. What was ruled out

- Patching the validator `0x245605d` → `mov eax,4; ret` (decision 014) only
  moves the fault to `0x23335c0`/`0x23335f3`; it does not repair the handle.
- Patching the lazy-init guard `0x2350049` to NOPs (this task) does not change
  the fault — linker not called for this object.
- Patching consumer helpers `0x23335c0`, `0x23335e7`, `0x23335a0`,
  `0x233319a` produced only a chain of new faults / a SIGABRT via atexit;
  all were reverted (decision C).
- Worker-2's A/B/C/D synthetic object graph cannot affect this object because
  the handle is guest-created (verified by the guest's own magic check).

## 5. gdb follow-up (debugger authorised by user, 2026-11-19)

`analysis/handle/gdb-watch.cmd` is a ready-to-run gdb batch script for the
authorised `dev-debug/gdb` build. It:

1. sets the exact phase-4 env/argv (`SOBER_APP_SHELL=1`,
   `LD_LIBRARY_PATH=build/ndk-stubs`, guest path);
2. breaks at host `guest_app_shell_run` and reads the guest load base from RDI;
3. sets breakpoints at guest RVA `0x23500c6` (the writer) and `0x23331ae`
   (the first consumer);
4. at the consumer, reads `rbx` (the handle) and arms a hardware WRITE
   watchpoint on `[rbx+0x68]`;
5. prints the watchpoint RIP (as a guest RVA), full backtrace, registers and
   nearby instructions, so the allocating call stack is captured.

Run when gdb is available:
`gdb -q -batch -x analysis/handle/gdb-watch.cmd 2>&1 | tee /tmp/gdb-handle.out`.

## 6. Limits

- Without GDB/Frida the watchpoint tracer could not be completed; the writer
  RVA is from disassembly plus A/B runtime patching, not a captured watchpoint
  hit. The REA process capture supplies the runtime fault context.
- It remains to be verified at runtime that zero-filled allocations do not
  break any other guest subsystem; if they do, restrict zeroing to blocks
  whose first 4 bytes will be a `0xc0dedbad` handle (post-allocation check).
- Evidence for "guest heap allocation" is circumstantial but strong
  (non-BSS value, guest-validated magic, arena-range memory hits, guard
  semantics); a full allocation-site trace would require a working debugger.
