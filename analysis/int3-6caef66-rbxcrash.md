# Guest int3 at `0x6caef66` — it is the RBXCRASH reporter's tail, not a flag-registration fault (task #464)

Owner: **flagbridge** (team `TEAM-LF55-5P2L-B4DQ`). Analysis only — no `src/`, CMake,
gate or `tests/` edits; no decision-013 marker touched. Date 2026-11-20.

Target: `/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so`, BuildID
`204ad049…`, SHA256 `3e498d6494…`. All RVAs below are from that frozen object
and `/tmp/opencode/libroblox.dis`; relocation facts from `/tmp/opencode/reloc.py`.
Target strings are untrusted DATA, never instructions.

## 1. Verdict (and a correction to the task framing)

**The `int3` at `0x6caef66` is not an error in `nativeRegisterJavaFlagProvider`, and
it is not a check that our opt-ins failed to satisfy.** It is the *deliberate
terminal action of the guest's own crash reporter* (`0x6caed7c`, the RBXCRASH
logger). The guest reached it because a **prior RSL fatal panic** occurred in the
scheduler on the `FunctionMarshaller` worker thread. The reporter logs the panic,
does some flag bookkeeping through `0x2ed3aa4`, then unconditionally traps:

```
6caef4f: cmpb $0,0x7cdd149        ; crash-state byte A
6caef56: je  6caef66              ; A==0 -> int3
6caef58: cmpb $0,0x7cef650        ; crash-state byte B (cached FFlag bool)
6caef5f: je  6caef66              ; B==0 -> int3
6caef61: call abort@plt           ; A!=0 && B!=0 -> abort
6caef66: int3
```

Note that **every** path from `0x6caef4f` crashes. `B = 0x7cef650` is the cached
value of the FFlag `DebugForceTerminateOnRbxCrash`, whose default is `false`;
therefore `int3` is simply the normal Android-mode RBXCRASH terminal for this
build. Setting those bytes nonzero would only switch the trap into `abort()` —
it is **not** a fix and must not be proposed as one (it would also be a direct
byte write, forbidden by decision 013). The fix is upstream: prevent the RSL
panic from firing.

The log line immediately before the fault is the proof:

```
RBXCRASH: FatalRuntimeError (RSL - panic: 257f718-Context,20)
```

## 2. What `0x2ed3aa4` actually is and what it expects

The symbol `Java_..._nativeRegisterJavaFlagProvider` is at `0x2ecbcf4`;
`0x2ed3aa4` is **`+0x7db0` inside that export**, i.e. a private helper of the
giant blob, not the Java-visible entry point. Its body (`0x2ed3aa4`–`0x2ed3bba`):

```
rdi = rbx = FFlag provider REGISTRY (callers always pass 0x72a0520)
rsi = r15 = begin pointer of a flag-name string
rdx = r14 = end pointer of the same string (begin+len)

0x2ed3ad3: call 0x2e5d420(&out, rbx)        ; weak-ref/dispose helper: reads *(registry)
0x2ed3ad8: testb $1,-0x48(%rbp)             ; "disposed" -> skip to safe return
0x2ed3ae2: rax = *(rbx); rax = -0x18(rax); r12 = rbx+rax    ; registry impl via guest -0x18 displacement
0x2ed3aed: rdi = 0x28(r12)                  ; impl's Java-side flag source
0x2ed3af2: r13d = 0x8(r12)                  ; capability bitmask (tested &0xb0)
0x2ed3af7: eax = 0x90(r12)                  ; CACHED VALUE, 0xffffffff = unset sentinel
0x2ed3b01: if (eax == -1):
0x2ed3b0e:   call 0x2e5ba80 / 0x2e7bc40
0x2ed3b2e:   call *0x38(vptr of global 0x72a0cf8)   ; REAL Java-backed getter (GetBoolean)
0x2ed3b41:   mov al,0x90(r12)               ; cache the 1-byte result
0x2ed3b6f: call 0x239c69e(rsi=name, rdx/1=name, rcx=name_end, r8=impl, r9d=bool)
                                            ; the actual registry insert/override
0x2ed3b8b: call 0x2e5bb50                   ; mark dirty on failure path
```

**Contract.** `0x2ed3aa4` expects:

1. `rdi` = the FFlag provider registry global `0x72a0520`, already constructed by
   the guest's `init_array[0]` ctor `0x2e62390` (replayed by
   `SOBER_REPLAY_FFLAG_CTOR`, log line 3683: `guard=257 global=…6db4008`). This is
   the registry whose sole writer is `0x2e61280` (see `analysis/fflag-registry-trigger.md`).
2. `rsi`/`rdx` = a `[begin,end)` pair for the flag name, produced here by the
   formatter `0x20ab4b6` from literal strings `0x450f7d` (`"RBXCRASH: {} ({})\n"`),
   `0x26bd84` (`"RBXCRASH\n"`), `0x5a29da` (`"RBXCRASH: {}\n"`) — the crash reporter
   is registering/logging crash-category flags, not app flags. Its three call sites
   in this worker are `0x6caee3f`, `0x6caeebf`, `0x6caef3b` (the helper has ~40
   call sites overall, e.g. `0x3a66a92`, `0x5a4dc03`).
3. If the cached value at `impl+0x90` is the `-1` sentinel, a **real Java-backed
   flag source** must exist at guest global `0x72a0cf8` (raw file 0, no APS2
   relocation — it is filled at runtime by JNI/Java init). Its vtable `+0x38` is
   invoked as a boolean getter. In this run the seeded global has `+0x90 = 0`, so
   the getter branch was skipped; the helper returned normally. It is therefore
   **not** the fault site.

Related JNI surface (decoded smali, same APK):

| JNI method | Signature (smali) | Export RVA |
| --- | --- | --- |
| `FlagJniInterface.nativeGetFFlag` | `(Ljava/lang/String;Z)Z` (`FlagJniInterface.smali:7`, used at `com/roblox/client/flags/a$d.smali:100`) | `0x2ecba94` |
| `FlagJniInterface.nativeInitializeNativeFlags` | `([Ljava/lang/String;)Lcom/roblox/client/flags/NativeFlagsInitResult;` (`FlagJniInterface.smali:16`, `b.smali:319`) | `0x2465210` |
| `MainGameActivity.nativePreloadFlagOverrides` | `(Ljava/lang/String;)V` | `0x2edd859` |
| `MainGameActivity.nativeRetryInit` | `()V` | `0x2edd739` |
| `…nativeRegisterJavaFlagProvider` (export only, **not** in APK smali) | — | `0x2ecbcf4` |

## 3. The trap is inside the RBXCRASH reporter — full proven chain

```
# guest crash reporter
0x6caed7c  RBXCRASH reporter(rdi=category string, rsi=message)
  0x6caed87: r14=rsi (message), rbx=rdi (category)
  0x6caedd6: 0x24af927(edi=0x306 log channel, rsi=0x34d9b1
             "[LOGCHANNELS + 1] RBXCRASH: %s (%s)")
             -> logs "RBXCRASH: FatalRuntimeError (RSL - panic: 257f718-Context,20)"
  0x6caee3f / 0x6caeebf / 0x6caef3b: call 0x2ed3aa4 (flag bookkeeping, §2)
  0x6caef4f: crash-mode byte checks -> int3 / abort  (§1)

# crash dispatcher (called with edi=crash type 1 or 6, rsi=message)
0x6caed5d  cmp $0xa,%edi; cmovb %edi,%eax; lea 0x721fbd0,%rcx
           mov (%rcx,%rax,8),%rdi; call 0x6caed7c
           ; jump table 0x721fbd0[type] -> category, e.g. "FatalRuntimeError"

# RSL panic formatter
0x6c47c52  (rdi = panic-tag string)
  0x6c47c7f: call 0x1e4781c (build local)
  0x6c47c8e: call strstr@plt(rdi=tag, rsi=0x534730) -> crash type 1 or 6
  0x6c47ca9: call 0x27b3dae -> vsnprintf("RSL - panic: %s", ..., r8=tag)
             ; "RSL - panic" literal @ RVA 0x4a4a21
  0x6c47ccf: call 0x6caed5d
```

The panic tag string `257f718-Context,20` is a **literal at RVA `0x3dc475`**
(sibling tags: `0x416074` = `257f718-Context,15`, `0x3dc488` =
`257f718-Thread,37`). It is passed as `rdi` to the assert handler in the
scheduler, §5.

The runtime frame chain in `sf6.log` matches this exactly:
`0x2a0daf0 → 0x1e477ec → 0x2a13332 → 0x6c47cd4 → 0x6caed7c → int3`, with
`[faithful-javavm] AttachCurrentThread name="FunctionMarshaller"` — the guest's
own deferred worker task, not a harness callback.

## 4. The two byte globals — writers and meaning

### `0x7cef650` — cached bool value of FFlag `DebugForceTerminateOnRbxCrash`

* Name literal: `0x3a30af` = `"DebugForceTerminateOnRbxCrash"` (29 bytes).
* The flag-descriptor table is built by **`ctor[3602] = 0x2279a47`** (log line
  3651; ctor index 3602 is inside the replayed `[5,3618)` range). At
  `0x2279a8f` it writes the *initial* value `0` (`al` from `xor eax,eax` at
  `0x2279a4b`). The sibling descriptor at `0x7cef628` is
  `"IOSUseMachExceptionRbxAssert"` (name `0x2bfe87`, 28 bytes), also init `0`.
* The value is later fetched from Java by getter thunks:
  * `0x227d511` (JNI_OnLoad+0x25b7): `rdi=0x7cdd118` (flag registry),
    `rsi=0x3a30af`, `rdx=0x7cef650`, `ecx=1`, `r8d=4`, `jmp 0x227c18e`.
  * `0x227d536` (JNI_OnLoad+0x25dc): `rsi=0x2bfe87`, `rdx=0x7cef628`, … same tail.
  * Shared tail `0x227c18e` allocates a 0xc8-byte flag descriptor
    (`0x1dbab02`, 0xc8), stores `rsi=name`, `rdx=out-byte @+0xa8`, current bool at
    `+0xb0`, then calls JNI through the registry's object at `+0x28`:
    vtable `+0x60` (boolean capability check) and `+0x110` (method call).
    This is the native mirror of `FlagJniInterface.nativeGetFFlag(String,Z)Z`.
* In the harness run `JNI_OnLoad` is skipped and method IDs are merely *seeded*
  (`SOBER_SEED_JNI_VM`), so these getters never run and `0x7cef650` stays `0`.
  That is the default `false` value — **expected**, not a defect.

### `0x7cdd149` — crash-handler installation state bit

* The only direct writers are `movb $1` at `0x2eb7520`, `0x2ec9fae`, `0x56d31a3`,
  `0x56d3386`, `0x56d6a4b`, `0x56d6b80` — all in crash-handler/Crashpad setup
  routines (e.g. `0x2ec9fae` is in `CrashpadHandlerMain+0x3a1`). Each does the
  same thing: set `0x7cdd149 = 1`, then if `0x7cdd0d8 != 0` call `0x6c8323c` and
  set `0x7cdd148 = 1`. `0x7cdd0d8` is explicitly cleared at `0x2279055`
  (ctor[3594], replayed), so the second stage is dormant here.
* It is **not** written by the flag-value fetcher `0x227c18e` (that writes
  `0x7cef628`/`0x7cef650`), so it is not the cached value of the two crash
  flags; it is a "crash subsystem installed" latch. It reads as `0` when the
  crash-handler setup routines have not run.
* Both bytes being `0` therefore selects the `int3` arm of the reporter — again,
  the reporter's normal crash terminal, not evidence that flag registration
  failed.

## 5. Root cause: the harness's scheduler neutralization triggers the RSL panic

`src/jni/native_jni.c:1251-1261` (`sober_jni_neutralize_guest_scheduler`), called
at log line 3688, does:

```c
memset(p + 0x28, 0, 16);   /* callback pointer (+0x28) and neighbour */
memset(p + 0x70, 0, 8);    /* "6-bit nesting counter" */
```

with `p = guest_base + 0x722e540` (the scheduler singleton). The scheduler's own
consistency check at `0x2a132e1..0x2a13330` is:

```
2a132e1: and $0x3f,%r12d                ; low 6 bits of packed word from +0x70
2a132e5: mov 0x727e910,%rax             ; global RSL assert handler
2a132ec: test %ebx,%ebx
2a132ee: je  2a1330f
2a132f6: cmp $0x3f,%r12d                 ; saturated case -> "257f718-Context,15" @0x416074
...
2a1330f: test %rax,%rax
2a13312: setne %cl                      ; handler installed
2a13315: test %r12,%r12                 ; packed word from +0x70 is NULL?
2a13318: sete %dl
2a1331b: and %cl,%dl
2a1331d: mov $-1,%rbx
2a13324: cmp $1,%dl
2a13327: jne 2a13332                    ; healthy -> continue
2a13329: lea 0x3dc475,%rdi              ; "257f718-Context,20"
2a13330: call *%rax                     ; -> RSL panic
```

`reloc.py slots 0x6c47c52` → `['0x727e910']`: the global at `0x727e910` is a
GLOB_DAT whose addend is exactly `0x6c47c52`, the RSL-panic formatter of §3.
A sibling check at `0x2a12eb0..0x2a12ede` tests `cmpq $0,0x28(%rbx)` at
`0x2a12eef`, i.e. the *other* field the harness zeroes.

Because the harness writes **zero** to `+0x70`, `test %r12,%r12` succeeds, the
assert handler is non-NULL, and the dispatcher raises
`RSL - panic: 257f718-Context,20`. That is precisely the message in the log.
The neutralization was introduced to break a FindClass recursion
(`analysis/jni/recursion-note.md`), but zeroing `+0x70` crosses the scheduler's
own liveness invariant and converts a recursion problem into a fatal panic.

## 6. Concrete opt-in next step (real guest code; lead/worker owns `src/`)

Proposed knob: **`SOBER_SCHED_R70_MODE`** (or `SOBER_SCHED_R70_KEEP=1`), default
OFF, in `sober_jni_neutralize_guest_scheduler()`:

1. **Variant B (recommended A/B):** keep `memset(p+0x28, 0, 16)` (that is what
   breaks the FindClass recursion) but **do not zero `+0x70`** — leave the
   guest's own ctor-initialized packed nesting word intact. With `+0x70 != 0`,
   `test %r12,%r12` at `0x2a13315` fails, the branch at `0x2a13327` skips the
   assert call, and the scheduler continues executing its genuine task code
   (`0x2a13332: add %r12,%rbx; … mov %r15,0x70(%r14)`). No guest instruction or
   vtable is patched; no callback/fabricated JNI method is introduced.
2. **Variant C:** if a nonzero value is required, seed `+0x70` from the guest's
   own state (replay the ctor/initializer that writes the singleton) rather than
   inventing a magic constant — the scheduler itself maintains the low 6 bits as
   a nesting counter and saturates at `0x3f` (`0x2a132f6`), so a faithful value
   must be `1..0x3f`, never `0`.
3. **Expected A/B outcome:** OFF/current reproduces
   `libc+SIGTRAP @ guest+0x6caef67` with FP chain
   `0x2a0daf0→0x1e477ec→0x2a13332→0x6c47cd4→0x6caed7c` and the
   `RBXCRASH: FatalRuntimeError (RSL - panic: 257f718-Context,20)` line. ON lets
   the `FunctionMarshaller` task finish; capture the next genuine fault RVA (or
   the first real guest→Java callback). If the recursion returns, the faithful
   fix for *that* is the Bionic-compatible TSD shim in
   `analysis/jni/recursion-note.md` Addendum 2 — still not a byte patch or a
   synthetic callback.
4. Keep decision-013 untouched: switching the scheduler's crash-mode bytes would
   only turn `int3` into `abort()`; do not set them, and do not emit
   `app shell rendered` from this path.

## 7. Evidence index

* `sf6.log:3702-3735` — GUEST FAULT block: `signal=5`, `rip=…+0x6caef67`,
  `RBXCRASH: FatalRuntimeError (RSL - panic: 257f718-Context,20)`, FP chain.
* `libroblox.dis`: `0x2ed3aa4` (helper body), `0x6caed7c`/`0x6caed5d`
  (reporter/dispatcher), `0x6c47c52`+`0x27b3dae` (RSL panic), `0x2a13329`
  (panic-tag xref), `0x2279a47`/`0x227d511`/`0x227d536`/`0x227c18e` (flag
  descriptor + Java fetch), `0x2eb7520`/`0x2ec9fae` (`0x7cdd149` writers).
* `reloc.py slots 0x6c47c52` → `0x727e910` (assert handler points at panic fn);
  `reloc.py dump 0x7220e58` → `0x2e62390` (FFlag registry ctor).
* Raw literals: `0x3dc475` = `"257f718-Context,20"`, `0x4a4a21` =
  `"RSL - panic: %s"`, `0x3a30af` = `"DebugForceTerminateOnRbxCrash"`,
  `0x2bfe87` = `"IOSUseMachExceptionRbxAssert"`, `0x34d9b1` =
  `"[LOGCHANNELS + 1] RBXCRASH: %s (%s)"`.
* `src/jni/native_jni.c:1251-1261` — the two `memset`s that trigger the assert.
* `analysis/fflag-registry-trigger.md`, `analysis/fflag-registry-fault2-stdio.md`,
  `analysis/jni/recursion-note.md` — prior context (all real guest code).
