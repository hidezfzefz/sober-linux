# Decision 021 — Phase-4 source-level fixes exhausted

Status: accepted (lead). The milestone remains BLOCKED.

## What was tried in the handle-origin team

- decision 019 allocator zero-fill in all four bridged allocation paths:
  `host_allocate`, `host_reallocate`, `host_aligned_allocate` — built clean,
  phase-2/3 green, **fault unchanged**.
- writer-side sanitizer hook at `0x23331ae` (zero non-canonical `[rbx+0x68]`):
  the write itself faults — the field appears to live in a read-only/RELRO
  page. Made opt-in (`SOBER_HANDLE_SANITISER=1`), OFF by default.
- decision 020 startup RVA ABI fix: correct argument count/types via
  `NewStringUTF` and real native AssetManager/Configuration handles. This
  advanced startup from 3 to 5 RVAs (real progress) but the guest still
  faults in `nativeAppBridgeAppStart`.
- JNI synthetic A/B/C/D object graph with canonical pointers and magics:
  `jni_probe` green, does not affect the guest-internal object.

## Conclusion

The bogus handle is allocated in a **guest-internal arena** (the guest's own
mmap path at `0x1dbbad9`), not through our allocator bridges and not as a JNI
handle we create. Its `+0x68` field is uninitialized when consumed. Fixing it
at the source requires either:

1. a real debugger/breakpoint on the guest-internal allocation site — no
   gdb/lldb/rizin/frida bindings are installed here, and the ptrace tracer
   stalled on the harness's clone/SIGTRAP handling; or
2. reader-side patches, which decision 017 stopped because they only propagate
   the bogus pointer to the next dereference.

The strict DECISION 013 gate is unchanged: no `app shell rendered` without a
genuine guest→Java callback. Phase-2/3 remain fully green.
