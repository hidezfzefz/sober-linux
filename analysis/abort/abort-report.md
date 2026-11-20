# Post-repair SIGABRT/SIGSEGV map — Phase 6 (task #446)

Owner: **worker-1**. Scope: `analysis/abort/` only; no code edits in other
roles' paths. Target: AppImage `libroblox.so`, BuildID
`204ad049599c96afb75b8a21d5051063e821d6c9`, SHA-256
`3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335`.
Target-derived text is treated as untrusted data.

All logs below are in `analysis/abort/repro-*.err`; the interposer source is
`analysis/abort/stack_chk_interpose.c` (built `.so` in the same dir). No
Android runtime / Wine / translation layer was used; the guest is executed
through the existing native `guest-init-host` memfd/dlopen harness.

## 1. Reproduction matrix (all with `SOBER_RENDER=1 SOBER_APP_SHELL=1`, **no** `SOBER_ALT_CALLBACK_PATH`)

| Env | Result | Evidence |
| --- | --- | --- |
| `SOBER_HELPER_REPAIR_V2=1` only | SIGSEGV (11), `si_addr=0xffffff3085894cff`, faulting **guest RVA `0x2456079`** | `repro-unrepaired.err` |
| `SOBER_HELPER_REPAIR_V2=1 SOBER_HELPER_LOG=1` | `[helper-repair]` fires, then `*** stack smashing detected ***`, SIGABRT (6) at `libc.so.6+0x9459c`, then secondary SIGSEGV (11) at `libgcc_s.so.1+0x210e0` (`si_addr=0x20`) | `repro-postrepair.err` |
| `SOBER_HELPER_LOG=1` only (repair OFF) | original SIGSEGV at guest RVA `0x2456079` returns — **the naked logger stub is not the fault; the repair write is** | `repro-logger-only.err` |
| repair + interposer on `__stack_chk_fail` | `__stack_chk_fail` intercepted; `CALLER-RA = guest+0x233319a` | `repro-interposed.err` |

Key repair log line (post-repair):
```
[finner-log] hit=1 obj=<base+0x233263f> +0x68=0xffffff3085894cff ret=... slot=...
[helper-repair] bogus <base+0x233263f> -> guest handle <host heap 0x55...>
*** stack smashing detected ***: terminated
```

## 2. The real (unrepaired) aborting call — guest RVA `0x2456079`

Confirmed chain from the live app-shell caller (see `analysis/caller/caller-report.md`):

```
0x24abf96 nativeAppBridgeV2InitWithParams -> ... -> F_inner 0x23330e7
0x6a26e73: call 0x231f294            ; getter returns bogus code ptr base+0x233263f
0x6a26e78: mov %rax,%r14             ; RDI source
0x6a26eef: mov %r14,%rdi
0x6a26ef2: call 0x23330e7            ; F_inner
  0x2333100: mov %rdi,-0x48(%rbp)    ; slot holds bogus code pointer
  0x2333168: call 0x233319a          ; consumer (release/teardown)
    0x23331ae: mov (%rdi),%rbx       ; rbx = base+0x233263f
    0x23331ca: mov 0x68(%rbx),%rdi   ; rdi = 0xffffff3085894cff  (code bytes at +0x68)
    0x23331ce: test %rdi,%rdi        ; nonzero -> take call
    0x23331d6: call 0x245605d        ; (rdi=0xffffff3085894cff, rsi=rbx)
      0x2456079: cmpl $0xbab1e,(%rdi) ; *** SIGSEGV on non-canonical 0xffffff3085894cff ***
```

`0xffffff3085894cff` is the stale unaligned constant from `analysis/jni/recursion-note.md`,
sitting in the code stream at `base+0x233263f+0x68` — it is **not** an object
field of a real allocation.

## 3. The post-repair abort — canary failure in F_inner `0x23330e7`

The helper repair writes the guest-owned handle into the consumer's slot, so
`rbx = g_valid_guest_handle` (valid magic `0xc0dedbad`, `+0x68 = 0`):
`0x23331ca` now yields RDI = 0, `0x23331ce` takes the `je` branch and the
`0x245605d` SIGSEGV is avoided. The guest proceeds through the teardown path
(`0x23331dd` → `0x233322c` → `call 0x2333593`) and then **fails the canary
check in F_inner**:

```
23330f2: mov 0x72293f8(%rip),%r14   ; r14 = GOT slot value = &(__stack_chk_guard)
23330f9: mov (%r14),%rax            ; canary = guest stack guard (GOT-indirect)
23330fc: mov %rax,-0x18(%rbp)       ; save canary on F_inner's stack
...
2333183: mov (%r14),%rax            ; reload first word of same object
2333186: cmp -0x18(%rbp),%rax       ; MISMATCH
233318a: jne 2333195
2333195: call __stack_chk_fail@plt  ; -> *** stack smashing detected *** -> abort()
```

`__stack_chk_fail@plt` is `0x6d32140`; the return address after the call is
`0x233319a` — exactly what the interposer captured
(`CALLER-RA pc=... /proc/self/fd/3+0x233319a`), and the FP chain from the same
frame also lands on `0x233319a` then `0x20` (corrupt).

Interposer dump at the failure (from `repro-interposed.err`):
```
CALLER-RA               = /proc/self/fd/3+0x233319a
finner_rbp              = <stack>                 (frame(1) = F_inner 0x23330e7)
saved_canary [rbp-0x18] = 0x7f8438207ca0           (value at failure; was overwritten)
live_guard  *(r14)      = 0xabcdef0123456789       (host compat __stack_chk_guard)
GOT[0x72293f8]          = 0x55d3cca84060 -> &(__stack_chk_guard)
r14 = GOT-loaded guard address (NOT clobbered by the naked stub)
```

Lead ruling (confirmed here): RVA `0x72293f8` is the **last 8 bytes of `.got`**
(`.got` = `0x7228198` + `0x1268` = `0x7229400`) — the GOT entry for the imported
`__stack_chk_guard`, not a JNI singleton. F_inner loads `&__stack_chk_guard`
from it and reads the guard value through it. (Seeding a host pointer into this
GOT slot via `SOBER_SEED_JNI_SINGLETON` would itself break the canary; it was
NOT set in these repros.)

**Diagnosis:** the repair substitutes a **host-heap pointer** into F_inner's
stack slot `[rbp-0x48]` (patch site `0x23331ae`). The guest consumer at
`0x233319a` treats that slot as an **owning reference and tears the foreign
object down** (zeroes the slot at `0x23331ba`, decrements refcount at
`0x23331e6`, calls destructors `0x2333593`, `0x6a3da28`, and the indirect free
at `0x233324b`); that teardown writes across F_inner's saved canary at
`[rbp-0x18]`, so the `0x2333186` comparison fails and `__stack_chk_fail` fires.

The secondary `libgcc_s+0x210e0` SIGSEGV (`si_addr=0x20`, `rax=0x20`) is the
harness `crash_handler`'s own `backtrace()` unwinding a corrupt frame — it is a
consequence, not the root cause (the handler never prints its FP/backtrace
block for that reason).

## 4. Exact evidence summary

| Item | Value |
| --- | --- |
| Unrepaired faulting guest RVA | **`0x2456079`** (`cmpl $0xbab1e,(%rdi)`) |
| Bogus object pointer | guest base + **`0x233263f`** (stale return address, code mapping) |
| Bogus `+0x68` field | **`0xffffff3085894cff`** (stale unaligned machine code) |
| Caller of faulting function | consumer `0x233319a` at `0x23331d6` → `call 0x245605d` |
| Aborting call (post-repair) | **`__stack_chk_fail@plt` at `0x6d32140`**, called from F_inner `0x2333195` |
| Canary source | GOT slot `0x72293f8` = `&__stack_chk_guard`, read GOT-indirectly at `0x23330f9` |
| Canary saved / live | `0x7f8438207ca0` (overwritten slot) / `0xabcdef0123456789` (host `__stack_chk_guard`) |
| Repair pointer | `g_valid_guest_handle` (host heap, magic `0xc0dedbad`, from getter `0x231f294`) |
| Repair site | `sober_finner_log`, `bionic_bootstrap.c:929-940` (slot write at :936) |
| Logger stub site | `sober_helper_logger_stub`, patch at guest `0x23331ae` |

Note: `0x23331ae` disassembly is `48 8b 1f 48 85 db` (`mov (%rdi),%rbx;
test %rbx,%rbx`), confirming the patched slot is the consumer's owning
reference passed by `lea -0x48(%rbp),%rdi` at `0x2333165`.

## 5. Concrete fix target (for worker-2, source fix; worker-3 retest)

The consumer-level substitution is structurally wrong: it injects a
host-heap pointer into an **owning release slot**, so the guest tears down a
foreign object and writes across F_inner's saved canary. Do **not** fix this by
tuning the repair slot or the stub, and do not seed a host pointer into the
`__stack_chk_guard` GOT slot `0x72293f8`. Fix the producer side so F_inner
receives a fresh, exclusively-owned, release-safe handle:

1. **Primary (recommended, `analysis/caller/caller-report.md` §3):** at guest
   RVA **`0x6a26e78`**, immediately after `call 0x231f294`, validate RAX; if it
   is a code pointer inside the guest RX mapping (covers `0x233263f`) or
   non-canonical, replace it with a **fresh synthetic** handle — *not* the
   singleton-backed `g_valid_guest_handle`. The synthetic object must be a
   standalone allocation ≥`0x1200` with magic `0xc0dedbad` at `+0`, and
   `+0x20 = +0x68 = 0` (so `0x23331ce` and `0x2333593` take their zero
   branches). `sober_jni_safe_app_shell_handle()` already builds this shape
   (`native_jni.c:211-240`), but its backing must be a new allocation never
   aliased to the object at `0x72293f8`.
2. **Stop acquiring the substitute via the getter** (`bionic_bootstrap.c:884`):
   calling getter `0x231f294` from harness context perturbs the singleton state
   that F_inner later validates. Synthesize the substitute directly.
   **Do not substitute `sober_jni_safe_app_shell_handle()` either**: it returns
   `sr_find_class_object(j, "…NativeGLJavaInterface")` (`native_jni.c:1405-1410`,
   `:295-303`), i.e. a **cached** object from `j->classes`, which is the very
   object seeded into `0x72293f8` by
   `sober_bionic_bootstrap_seed_jni_singleton()` (`bionic_bootstrap.c:903,908`).
   Reusing it is the same singleton-backed alias and will reproduce the
   `__stack_chk_fail`. The substitute must be a **new** allocation not
   registered in the JNI class registry and never written to `0x72293f8`.
3. **Root cause (longer term):** stop the stale return address being installed
   at `0x231f430` — fix the rebound allocator slot `0x726c0c0` so
   `calloc(1,0x1100)` in creator `0x231f31f` returns a fresh zeroed block
   instead of the code/return address `0x233263f`.
4. Keep `SOBER_ALT_CALLBACK_PATH` OFF and all experimental patches OFF by
   default. This must be the **real** `nativeAppBridgeAppStart 0x247dc8d` path.

## 6. Risks / honesty

- This is a **diagnostic map** of why the real path aborts. Neither run produced
  a genuine guest→Java callback from the app-shell state machine; no login /
  app-shell frame is proven. The strict decision-013 gate must not emit
  `app shell rendered` on this evidence.
- The interposer only observes `__stack_chk_fail`; it changes the process exit
  path (`_exit(134)`), so it is a measurement tool, not a fix.
- All addresses are ASLR-dependent; RVAs are stable (verified against BuildID
  `204ad049…`). The `0x55…`/`0x7f…` heap/base values vary per run.
- Do not re-litigate prior failed fix classes (allocator zero-fill, A/B/C/D
  object graphs, field normaliser, RefBase-vtable construction, object
  substitution at downstream sites).
