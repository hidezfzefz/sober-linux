# Decision 009 — Root cause: versioned libc stub is self-hosting

Status: accepted (lead). Supersedes the "frozen" clause of decision 006 for
two specific files.

## Finding (worker-1)

`src/compat/bionic_libc.c` / `gen_bionic_libc.py` (lead-authored in phase 1)
have a self-hosting bug:

- the stub's constructor calls `sober_bionic_libc_install()`, which calls
  `dlsym(g_glibc, name)`;
- but the stub itself **exports `dlsym`** as one of the 411 versioned
  trampolines, so the call binds to the stub's own trampoline;
- that trampoline's `sober_libc_fn_dlsym` slot is still NULL, so **every**
  `sober_libc_fn_*` slot stays NULL (verified: `fn_sysconf`, `fn_memmove`,
  `fn_strlen` all NULL after dlopen of the stub).

Consequence: guest `sysconf()` returns 0, so arena init `0x1dbb178` bails at
its `sysconf(_SC_PAGESIZE) <= 0` guard, `arena_table@0x7cd9040` stays NULL, and
ctor[6] dereferences NULL. This explains every symptom from the last phase:
the "arena allocation failure", the protobuf red herring, and the ctor[6]
fault/hang. It is in the native shim, not the guest semantics.

## Ruling

- **Unfreeze** `src/compat/bionic_libc.c` and `src/compat/gen_bionic_libc.py`
  for **worker-3 only**. All other phase-1 files stay frozen.
- worker-3 must make resolution use glibc's real `dlsym` through a
  non-interposable path (e.g. `dlvsym` via an RTLD_NEXT handle obtained before
  the trampolines take effect, or generation-time static binding), regenerate
  the shim, and verify with a standalone probe that all 411 slots resolve
  non-NULL — especially `sysconf`, `memmove`, `memcmp`, `strlen`,
  `pthread_getspecific`, `pthread_once`, `getpid`, `sched_getcpu`.
- worker-1's `repair_stub_libc_slots()` workaround in
  `src/compat/bionic_bootstrap/` stays for now but must be removed once
  worker-3's fix is verified; worker-1 owns that removal.
- Keep `build/sober-native` ldd-clean and all 411 version nodes intact.
