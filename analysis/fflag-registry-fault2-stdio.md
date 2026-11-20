# Fault #2 after ctor[0] replay — bogus Bionic `__sF` stdio stream (follow-up to #462)

Owner: **flagbridge**. Analysis only — no `src/`, CMake, gate or `tests/` edits.
Date 2026-11-20. Engine BuildID `204ad049…`, SHA `3e498d6494…`, same frozen object
as task #462. Evidence: `/tmp/opencode/ctor3.log` (full run), `/tmp/opencode/libroblox.dis`,
`reloc.py`, `objdump -d /usr/lib64/libc.so.6`, decoded smali.

## 1. Verdict

`flagCount = 0` and `Flag Provider ID from Java: 0` are **incidental**, not causal.
Replaying another skipped ctor (1/3/4) is **not** the fix either — none of them
touches stdio. The crash is a **host/guest ABI mismatch on the Bionic stdio global
`__sF`**:

> The guest's logger writes/flushers use `FILE*` values derived as
> `__sF + 0x98` (stdout) and `__sF + 0x130` (stderr). In this NDK Bionic `FILE`
> is 0x98 bytes and `__sF` is `FILE __sF[3]` (needs **0x198 = 408 bytes**). The
> harness exports `__sF` as `sober_libc_obj___sF[64]` (0x40 bytes, only 16 bytes
> seeded). The guest's derived `FILE*` therefore lands in zeroed out-of-bounds
> BSS. glibc `_IO_fflush` reads the `_lock` word at `FILE+0x88`, gets NULL, then
> dereferences `NULL+8` → **si_addr=0x8**, exactly as logged.

The fix is to supply a **real, Bionic-sized, lock-initialised `__sF`** (or interpose
`fflush`/`fwrite` for the two synthetic stream addresses). All guest code — the
deferred flag-provider task, the logger, the JNI calls — then continues unmodified.

## 2. The fault, instruction by instruction

Log (`ctor3.log`, `[harness] ===== GUEST FAULT =====`):
```
signal=11 si_addr=0x8 rip=/usr/lib64/libc.so.6+0x7c8ff
regs: rdi=(nil) rsi=0x1 rdx=0x7f9b3a61a6c0 rcx=(nil)
```
glibc `objdump` at `fflush` (`_IO_fflush` = `0x7c860`):
```
7c864: test %rdi,%rdi          ; FILE* itself is non-NULL (BSS address)
7c877: testb $0x80,0x1(%rdi)   ; flags byte readable, zero -> fall through
7c8e8: mov 0x88(%rdi),%rdi     ; _lock = *(FILE+0x88) == 0  (BSS zero)
7c8ff: mov 0x8(%rdi),%rax      ; FAULT: read NULL+8  <-- rip
```
So the passed `FILE*` is *readable but zero-filled* — a pointer into BSS past the
undersized `sober_libc_obj___sF`, not a real FILE. `rsi=1` and `rdx=...` are the
leftovers of the preceding `fwrite(buf, 1, len, FILE)` at guest `0x2e61cf1`.

## 3. Guest-side chain (every frame is real guest code)

Backtrace / FP chain from `ctor3.log` (frame → RVA → meaning):

| Frame | RVA | What it is |
| --- | --- | --- |
| #2 | libc `+0x7c8ff` | `_IO_fflush`, fault on `NULL->_lock` (see §2) |
| #3 | `0x2e61d18` | return after `call fflush@plt` (`0x2e61d13`) inside logger `0x2e61ca0` |
| #4 | `0x2e5c3c1` | return after `call *0x30(%rax)` (`0x2e5c3be`) in `0x2e5c380` |
| #5 | `0x2e5d454` | return after `call 0x2e5c380` (`0x2e5d44f`) in weak-ref helper `0x2e5d420` |
| #6 | `0x2ed3ad8` | return after `call 0x2e5d420` (`0x2ed3ad3`) in `0x2ed3aa4` = `nativeRegisterJavaFlagProvider+0x7db0` |
| #7 | `0x6caee44` | return after `call 0x2ed3aa4` (`0x6caee3f`), `rdi = 0x72a0520` (FFlag registry) |
| #8 | `0x6caed7c` | flag-provider registration worker (registers `baseURL`, `userAgent`, … from strings at `0x56ae00`, `0x450f7d`, `0x26bd84`, `0x5a29da` via formatter `0x20ab4b6`) |
| #9 | `0x6c47cd4` | caller of `0x6caed5d` → `0x6caed7c` |
| #10 | `0x2a13332` | scheduler dispatcher (`call *%rax` at `0x2a13330`) |
| #11 | `0x1e477ec` | scheduler task body (also calls JNI getter `0x227dde2`) |
| #12 | `0x2a0daf0` | guest thread/bootstrap trampoline |

The `[faithful-javavm] AttachCurrentThread name="FunctionMarshaller"` line confirms
this is the guest's own deferred JNI task, not a harness callback.

### The logger and where its `FILE*` comes from

Formatter objects built by **`0x2e61280`** (the ctor[0] initializer we now replay):

```
0x2e61750 (formatter A, vptr 0x6db49d8) and 0x2e617e0 (formatter B, vptr 0x6db4b08):
   +0x00 = vptr
   +0x40 = rsi  <-- the FILE* stream          (0x2e61772 / 0x2e61802)
   +0x48 = string object; +0x50 = rdx; +0x58 = bool
```
Four such formatters are constructed at `0x2e61376`, `0x2e613e8`, `0x2e61596`,
`0x2e61602`. Their `rsi` is computed at:
```
0x2e61359 / 0x2e6157e: mov $0x98,%esi ; add *(0x72281d8),%rsi  -> __sF+0x98
0x2e613cb / 0x2e615ea: mov $0x130,%esi; add *(0x72281d8),%rsi  -> __sF+0x130
```
Vtable slot +0x30 of both formatters is the logger:
```
reloc.py dump 0x6db49d8 -> 0x2e61ca0 @0x6db4a08
reloc.py dump 0x6db4b08 -> 0x2e61ca0 @0x6db4b38
```
Logger `0x2e61ca0` then does exactly the two calls seen in the backtrace:
```
0x2e61ce2: mov 0x40(%rbx),%rcx ; mov %r15,%rdi ; mov %r14,%rdx ; call fwrite@plt  (0x2e61cf1)
0x2e61d0f: mov 0x40(%rbx),%rdi ; call fflush@plt (0x2e61d13)   <-- faults
```
These match `rsi=1`, `rdx=len`, `rcx=0`?/FILE at fault — i.e. the `fwrite` just
returned and `fflush` on the same bogus stream killed the process.

## 4. The exact missing state: full Bionic `__sF`

`0x72281d8` is a **GLOB_DAT (R_X86_64_GLOB_DAT, type 6, addend 0) for symbol
`__sF`** (`reloc.py dump 0x72281d8` → `type 6 sym __sF addend 0x0`;
`fflush` JUMP_SLOT is `0x7229750`). Same area: `stderr` GLOB_DAT at `0x7228198`.

Harness side (`src/compat/bionic_libc.c`):
```
:442  unsigned char sober_libc_obj___sF[64];          // 0x40 bytes
:898  native_lookup("__sF");
:899  __builtin_memcpy(sober_libc_obj___sF, p, 16);    // only 16 bytes seeded
:1334 __asm__(".globl __sF\n\t__sF = sober_libc_obj___sF");
```
A second, conflicting definition exists in `src/compat/android_compat.c:41`
(`FILE *__sF[3]`, 24 bytes, host `stdin/stdout/stderr` pointers, exported via
`exports.map:84`). Whichever the loader picks, both are far shorter than the
**0x198 bytes** the guest expects. Guest address arithmetic:
```
__sF + 0x98  = __sF[1] (stdout)  -> past a 0x40 or 0x18 byte object -> zeroed BSS
__sF + 0x130 = __sF[2] (stderr)  -> likewise
```
`glibc FILE._lock` is at `+0x88`; the zeroed region gives `_lock = NULL` and
`fflush` faults at `NULL+8`. This also explains why the earlier DEBUG lines print
fine: they go through `__android_log_print` (works), not through this logger.

**Not the cause** (checked):
* `flagCount=0` / provider ID 0 — those DEBUG lines are logged *before* the fault
  and come from `nativeInitializeNativeFlags` (`0x2465210`, which returns rc=0).
  A populated String[] changes no stream pointer.
* skipped ctor[1] `0x1dba8f3` (once-guard for flag `0x783dab0`), ctor[3]
  `0x1db9a40` (allocator/thread sizing), ctor[4] `0x1dba6c4` (jmp thunk to
  `0x1dbe828`) — none references `0x72281d8`, `__sF`, `fflush`, or the formatter
  vtables. The formatter objects were already fully built by ctor[0].
* the FFlag registry itself — `guard=257`, `*(0x72a0520)=base+0x6db4008` and fault
  `0x2e5d42a` are gone, exactly as lead reported.

## 5. Concrete opt-in next step (lead/worker-3 own `src/`; I edit nothing)

Suggested knob: **`SOBER_BIONIC_SF_FULL=1`** (default OFF), applied in the compat
layer before the first guest JNI call. Make the exported `__sF` match the guest's
ABI, then let the *same real guest path* run:

1. Allocate/export `__sF` as **>= 0x198 bytes** (e.g.
   `unsigned char sober_libc_obj___sF[0x200] __attribute__((aligned(8)))`), zeroed
   so `sober_libc_obj___sF+0x98` and `+0x130` are readable.
2. Initialise the two FILE slots the guest actually uses:
   * slot 1 at `__sF+0x98` (stdout), slot 2 at `__sF+0x130` (stderr);
   * each needs a **valid `_lock` at `+0x88`** pointing at a static
     `pthread_mutex_t` (glibc `fflush` reads/cmp-xchgs it), and benign flags at
     `+0x0/+0x1`, buffer pointers `+0x20..+0x38` if fwrite is to be passthrough.
3. Simplest robust variant (recommended, still no guest bytes touched): in the
   existing `TRAMP(fwrite)` / `TRAMP(fflush)` paths of `bionic_libc.c`, **detect
   `FILE* == __sF+0x98` or `__sF+0x130` and redirect to host `stdout` / `stderr`**
   (or return 0 for `fflush`). Pass every other FILE* through unchanged. This
   leaves every guest instruction — including the logger `0x2e61ca0`, the
   registry `0x2ed3aa4`, and the scheduler task — running as genuine guest code.
4. A/B with the existing opt-ins:
   ```
   SOBER_REPLAY_FFLAG_CTOR=1 SOBER_NATIVE_FLAGS_INIT=1 \
   SOBER_SEED_JNI_VM=1 SOBER_FAITHFUL_JAVAVM=1 \
   [SOBER_BIONIC_SF_FULL=1]   <-- compare OFF vs ON
   ```
   Expected: OFF reproduces `libc+0x7c8ff / si_addr=0x8` with FP chain
   `0x2e61ca0 → 0x2e5c380 → 0x2e5d420 → 0x2ed3aa4`; ON lets the
   FunctionMarshaller task finish registering providers and advances to the next
   genuine guest state (capture the new fault RVA if any).
5. Keep decision-013 untouched: this is stdio ABI plumbing, emits no callback,
   and must not set `app shell rendered`.

## 6. Evidence index

* `ctor3.log`: GUEST FAULT block, `rip=libc+0x7c8ff`, `si_addr=0x8`, `rdi=nil`,
  FP chain `2e5c3c1 → 2e5d454 → 2ed3ad8 → 6caee44 → 6caed7c → 6c47cd4 →
  2a13332 → 1e477ec → 2a0daf0`; DEBUG `flagCount = 0`.
* `objdump -d /usr/lib64/libc.so.6` at `0x7c860`: `0x7c8e8 mov 0x88(%rdi),%rdi`,
  `0x7c8ff mov 0x8(%rdi),%rax`.
* `libroblox.dis`: `0x2e61ca0` logger (`0x2e61d13 call fflush@plt`), `0x2e61750`
  / `0x2e617e0` formatter ctors (`+0x40` stream field), `0x2e5c380`
  (`call *0x30(%rax)`), `0x2e5d420`, `0x2ed3aa4`, `0x6caed7c`.
* `reloc.py dump 0x72281d8` → GLOB_DAT sym `__sF` addend 0;
  `reloc.py dump 0x6db49d8/0x6db4b08` → slot +0x30 = `0x2e61ca0`.
* `src/compat/bionic_libc.c:442,898-899,1027,1334`;
  `src/compat/android_compat.c:41,64-66`; `src/compat/bionic_libc.map:114`.
