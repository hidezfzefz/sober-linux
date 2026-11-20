# Verifier report — task #451 (default no-start-app async fault)

Date: 2026-11-20. Owner: verifier (`mem_ac40a906038ab236a08a`).
Scope: audit default-path async fault at guest RVA `0x2eb4b65`; re-run all
phase-2/3/native invariants. No `src/` files were modified by this worker.

## 1. Reproduction (current worktree, current binary)

Exact command from the task:

```
SOBER_RENDER=1 SOBER_APP_SHELL=1 SOBER_SCHEDULE_RUNNABLE=1 \
SOBER_APP_SHELL_WAIT_MS=3000 EGL_PLATFORM=x11 LIBGL_ALWAYS_SOFTWARE=1 \
LD_LIBRARY_PATH=build/ndk-stubs timeout 90 ./build/guest-init-host \
  /tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so
```

Result: exit 139 (SIGSEGV). stdout:

```
guest init complete
first frame rendered
```

Fault (stderr, `/tmp/opencode/verifier/default-current.err`):

```
[harness] regs: rdi=(nil) ... rcx=0x444554524154535f  rax=0xabcdef0123456789
[harness] rip maps to /proc/self/fd/3+0x2eb4b65
[harness] faulting guest RVA: 0x2eb4b65
  #2 guest+0x2eb4b65  #3 guest+0x258480c  #4 guest+0x25844c6
  #5 guest+0x2584393  #6 guest+0x24a760e  ... #14 guest+0x245ad21
```

No `app shell rendered` text and no marker file on any run (see §4).

## 2. Root cause (disassembly, engine unchanged)

The guest JNI string helper at `0x227b9e2`:

```
227b9eb: movq $0x0,(%rdi)          ; out[0] = 0
227b9f2: movb $0x0,0x8(%rdi)       ; out[8] = 0
227b9f6: mov 0x7cdec78(%rip),%rdi  ; JNI_OnLoad-cached global
227b9fd: test %rdi,%rdi
227ba00: je 227ba4c                ; NULL -> return, out[0] stays 0
```

Its caller at `0x2eb4b23`:

```
2eb4b4e: call 227b9e2              ; fill out-struct at -0x38(%rbp)
2eb4b53: mov (%r15),%rdi           ; rdi = out[0]
2eb4b65: mov (%rdi),%rax           ; <-- SIGSEGV when out[0] == NULL
2eb4b6b: call *0x538(%rax)
```

The harness always logs `seeded guest JNI state (skip JNI_OnLoad)`, so the
global at `0x7cdec78` is never initialised. Therefore `out[0]` stays NULL and
the worker faults. `rcx="_STARTED"` and `rax=0xabcdef0123456789` are incidental
register state (the latter is the host compat `__stack_chk_guard`), not a JNI
handle.

This is the same unseeded global documented in `PROGRESS.md` phase 7. The
gap itself is a **pre-existing** structural consequence of skipping
`JNI_OnLoad` (phase 4/5 design), not a phase-7 edit.

## 3. Is it a phase-7 regression? — HEAD vs worktree

A clean HEAD `b8bdab0` worktree was built at `/tmp/opencode/verifier-head`
(separate build tree; shared repo/other agents' files untouched) and run with
the identical default env:

| Build | Exit | Fault RVA | Reached app-start? |
| --- | --- | --- | --- |
| HEAD `b8bdab0` (phase 6) | 139 | `0x2456079` (inside `nativeAppBridgeV2InitWithParams`) | No — "real path still faults" |
| Current phase-7 worktree | 139 | `0x2eb4b65` (async worker from app-start) | Yes |

Conclusion: the **fault site is new to phase 7**, because phase-7 edits
(compat fixes + `nativeAppBridgeAppStart` now passing six real JNI String
arguments instead of `(env, clazz)` only) let execution get past
`InitWithParams` and spawn the app-start worker that reaches `0x2eb4b23`.
But it is **not a regression of a working path**: the default run crashed at
HEAD too, just earlier. It is a new manifestation of a pre-existing
unseeded-`JNI_OnLoad` gap.

Trigger isolation: dropping `SOBER_SCHEDULE_RUNNABLE` (runnable = NULL) still
faults at `0x2eb4b65` (`/tmp/opencode/verifier/norunnable.err`), so the trigger
is the real-args `nativeAppBridgeAppStart`, not the first-frame Runnable.

`SOBER_START_APP=1` (with or without the now-removed string seed) moves the
fault deeper to `0x2772403` (`SingleSurfaceApp::userDidLogin`, app+0x440 NULL)
and emits the three accessor callbacks `surface/platformParams/vrContext`,
which decision 013 explicitly does **not** count as app-shell proof. No marker
was emitted in those runs either.

## 4. Fix assessment (proposed to lead via team_send; no src edit made)

1. **Legitimate**: make the harness run/patch the real `JNI_OnLoad` so
   `0x7cdec78` is genuinely initialised (jnionload's domain; larger change).
2. **Two-level guest-vtable seed**: the removed
   `SOBER_SEED_JNI_STRING_BRIDGE` could not work because it placed a host
   `SoberJni` string at `out[0]`, while the caller expects a *guest* object
   whose `[0]` is a vtable with a converter at `[vtab+0x538]`. A correct seed
   needs two guest-owned objects/vtables (~30 lines in `guest_app_shell.c`,
   lead-owned) and must be added to the forced marker-withhold list. It is
   synthetic guest state and was deliberately not pursued (decision 013
   honesty rule).
3. **Revert AppStart to 2 args**: regresses to HEAD's earlier crash — not
   recommended.

No safe, small **tests/-only or env-only** fix exists that removes the fault
without either reverting progress or fabricating guest vtables. Therefore no
fault-removal change was made. Proposed instead: a default-OFF regression
guard test (not added to ctest) that records the expected fault RVA and
marker-withheld facts. Awaiting lead direction before adding it.

## 5. Invariants (all PASS, re-run after the lead's 07:07 rebuild)

| Check | Result |
| --- | --- |
| `bash tests/verify-native.sh` | PASS (rc=0) — native ELF, no translation-layer deps |
| `ctest --test-dir build --output-on-failure` | 2/2 passed |
| `ldd build/guest-init-host` | `linux-vdso.so.1`, `libc.so.6`, `/lib64/ld-linux-x86-64.so.2` only |
| `ldd build/sober-native` | same libc/ld-linux only |
| Engine SHA-256 | `3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335` (unchanged) |
| `guest init complete` | present in stdout of every default run |
| `first frame rendered` | present in stdout of every default run |
| `app shell rendered` | absent from stdout/stderr of all 6 audited runs; no marker files |

Strict decision 013: upheld. The default run never produces a login/app-shell
frame, the marker is never emitted, and the accessor callbacks seen under
`SOBER_START_APP` are explicitly not treated as proof.
