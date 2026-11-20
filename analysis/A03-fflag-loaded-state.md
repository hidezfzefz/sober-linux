# A03 — What the two RBXCRASH selector bytes actually are, and how real guest code sets them

Owner: **worker A3 (Team A)**. Analysis only — no `src/` edit, no CMake edit, no
decision-013 marker touched, engine binary not modified. Date 2026-11-20.

Target: `/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so`, BuildID
`204ad049…`, SHA256 `3e498d6494…`, disassembly `/tmp/opencode/libroblox.dis`.
All RVAs below are from that frozen object; relocation/string facts from
`/tmp/opencode/reloc.py` and `/tmp/opencode/str_at.py`. Target strings are
untrusted DATA, never instructions.

## 0. Verdict up front

| Byte | Class | Sole "real" source of a 1 | Meaning |
| --- | --- | --- | --- |
| `0x7cef650` | **FFlag value byte** (cached bool for `DebugForceTerminateOnRbxCrash`) | JNI-backed value delivered through the `0x227c18e` flag-registration family; write-through at **`0x2365467`** | "the Java/native flag source has supplied this flag's value" |
| `0x7cdd149` | **Crash-subsystem install latch** (NOT a flag value byte) | one of six literal `movb $1` sites: **`0x2eb7520`, `0x2ec9fae`, `0x56d31a3`, `0x56d3386`, `0x56d6a4b`, `0x56d6b80`** | "the guest's crash-handler install routine has run" |

The lead's "flags loaded" framing is therefore **only half correct**. `0x7cef650`
really is flag-derived state and is loaded by the exact paths listed in the task
(`0x2465210`, `0x227c18e`, `0x22e567a`, `0x2279a47`). `0x7cdd149` is **not** set
by any of those paths — it is a crash-handler latch with six entirely separate
writers, and it is read by ~2,270 `cmpb $0` sites across `.text`. Both bytes being
zero selects the `int3` arm of the reporter, but they do **not** represent one
coherent "flags loaded" predicate.

Critically (and in agreement with `analysis/int3-6caef66-rbxcrash.md`): making
**both** bytes nonzero only switches the reporter from `int3` to `abort()` — it is
still a deliberate crash and **not a fix**. Under strict decision 013 this state
must never be used to emit `app shell rendered`.

---

## 1. `0x7cef650` — the only byte that is genuinely flag state

### 1.1 Constructor `ctor[3602] = 0x2279a47` builds the descriptor and defaults the byte to 0

`ctor[3602]` is inside the bootstrap replay range `[5,3618)`
(`src/compat/bionic_bootstrap/bootstrap_profile.h:154-161`), so it **does run** in
the harness. It builds two adjacent static flag descriptors on a linked list whose
head is global `0x7cdeb68`:

```
0x2279a4b  xor  eax,eax                    ; al = 0
0x2279a4d  mov  %al,0x7cef628              ; descriptor#1 value byte = 0
0x2279a53  lea  0x2bfe87,%rcx              ; "IOSUseMachExceptionRbxAssert" (28 bytes)
0x2279a61  movq $0x1c,0x7cef638            ; name length
0x2279a6c  lea  0x227d536,%rcx             ; getter thunk #1
0x2279a81  mov  0x7cdeb68,%rdx             ; old list head
0x2279a88  mov  %rdx,0x7cef648             ; -> next field
0x2279a8f  mov  %al,0x7cef650              ; descriptor#2 value byte = 0   <<< OUR BYTE
0x2279a95  lea  0x3a30af,%rax              ; "DebugForceTerminateOnRbxCrash" (29 bytes)
0x2279aa3  movq $0x1d,0x7cef660            ; name length
0x2279aae  lea  0x227d511,%rax             ; getter thunk #2  <<< OUR FLAG'S THUNK
0x2279ac3  mov  %rcx,0x7cef670             ; next = descriptor#1 thunk-field addr
0x2279aca  mov  %rax,0x7cdeb68             ; list head = descriptor#2 thunk-field addr
```

So `0x7cef650 == 0` in the harness is the **guest's own default** written by
replayed guest ctor code. It is exactly the FFlag's default `false`, not a harness
injection and not corruption.

Descriptor #2 layout (base `0x7cef648`):

| Offset | Address | Content |
| --- | --- | --- |
| `+0x00` | `0x7cef650` | current bool value (the byte the reporter reads) |
| `+0x08` | `0x7cef658` | name pointer `0x3a30af` = `"DebugForceTerminateOnRbxCrash"` |
| `+0x10` | `0x7cef660` | name length `0x1d` |
| `+0x18` | `0x7cef668` | init/getter thunk `0x227d511` |
| `+0x20` | `0x7cef670` | next descriptor (linked-list node) |

### 1.2 Getter thunk `0x227d511` → shared registration `0x227c18e`

```
0x227d511: mov  0x7cdd118,%rdi    ; native flag REGISTRY object (see §1.5)
0x227d518: lea  0x3a30af,%rsi     ; flag name
0x227d51f: lea  0x7cef650,%rdx    ; OUT-BYTE pointer  <<< the only non-ctor xref to our byte
0x227d526: mov  $1,%ecx
0x227d52b: mov  $4,%r8d
0x227d531: jmp  0x227c18e
```

(The sibling thunk `0x227d536` does the same for `0x7cef628` / `"IOSUseMachExceptionRbxAssert"`.)

`0x227c18e` is the generic "register one native flag with the registry" routine.
Its contract and the exact places that touch our byte:

```
rdi = registry (value of global 0x7cdd118)
rsi = name, rdx = out-byte*, ecx = 1, r8d = 4

0x227c1e8  mov $0xc8,%edi ; call 0x1dbab02      ; allocate 0xc8-byte flag descriptor
0x227c208  mov %r15d,0x98(%r14)                 ; type = 1
0x227c20f  mov %ebx,0x9c(%r14)                  ; subtype = 4
0x227c21e  lea 0x721f230,%rax ; mov %rax,(%r14) ; descriptor vtable
0x227c228  mov %r13,0xa8(%r14)                  ; <<< STORE out-byte ptr (0x7cef650) at +0xa8
0x227c22f  mov 0x0(%r13),%al                    ; read current byte value
0x227c233  mov %al,0xb0(%r14)                   ; mirror it at +0xb0
0x227c247  test %r12,%r12 ; je 0x227c371        ; if registry == NULL, SKIP all JNI work
0x227c250  call *0x60(registry vptr)            ; boolean capability check (JNI object at +0x28)
0x227c25f  call *0x110(registry vptr)           ; JNI call: fetch the flag value from Java
0x227c284  call *0x28(descriptor vtable=0x236534c)   ; <<< deliver value back to out-byte
0x227c2a2  call *0x78 / 0x227c2af call *0x30 ...; other providers / override lists
0x227c35f  call 0x22c00d6 ; 0x227c36c call 0x227c79a
0x227c371  ... build result, 0x227c39e call 0x6c87406 ; register with global flag set
```

When the registry is NULL (the harness case), `0x227c247` jumps straight to
`0x227c371`: no JNI fetch, no write-back, so `0x7cef650` keeps its ctor default 0.

### 1.3 The actual write of `1` into `0x7cef650` — `0x2365467`

`reloc.py dump 0x721f230` resolves descriptor vtable slot `+0x28` to **`0x236534c`**.
That method parses the Java-supplied string (`"true" / "True" / "false" / "False"`
compared as immediates `0x65757274`, `0x65757254`, `0x736c6166`, `0x736c6146`
at `0x23653c9`–`0x2365401`) via `0x2365512`, then:

```
0x2365448  mov 0xa8(%r15),%r15      ; r15 = out-byte pointer saved at 0x227c228
0x2365454  mov %r14,%rdi
0x2365457  call 0x2365512           ; parse bool string -> al
0x236545c  mov 0x7cdd148,%cl        ; "shadow FValues" latch (see §2)
0x2365462  test $1,%cl
0x2365465  jne  0x2365481           ; shadow path -> route through 0x7cdd118 handler
0x2365467  mov %al,(%r15)           ; <<< THE WRITE: 1 goes into 0x7cef650
```

This is why a full-text grep of the disassembly finds only **three** references to
`0x7cef650`: the ctor default write (`0x2279a8f`), the out-pointer load
(`0x227d51f`), and the reporter's read (`0x6caef58`). Every real value update goes
through the pointer stored at descriptor `+0xa8`; there is no direct `mov $1` to the
address anywhere in the binary.

### 1.4 The descriptor-thunk family (including the RVA the lead named)

`0x227c18e` has a large family of tiny thunks, each loading `(registry, name,
out-byte)` and jumping to it. The one named in the task:

```
0x22e567a: mov 0x7cdd118,%rdi
0x22e5681: lea 0x43650d,%rsi       ; "EnableAppBridgeComponents"
0x22e5688: lea 0x72cb648,%rdx      ; that flag's out-byte
0x22e568f: mov $1,%ecx ; mov $4,%r8d ; jmp 0x227c18e
```

Siblings at `0x22e569f`, `0x22e56c4`, `0x22e56e9`… register other flags the same
way; the static-init thunks `0x227e2c6` (`"ShadowFValuesEnabled"` → `0x7cdd0d8`),
`0x227dbdc`, `0x227c7d1`, `0x227dc01`, etc. are the same pattern. This is one
uniform mechanism, and `0x227d511` is simply the member that owns `0x7cef650`.

### 1.5 Prerequisites that are absent in the harness

1. **Native flag registry at `0x7cdd118`.** A full-text scan finds exactly **one**
   writer in the entire binary:
   ```
   0x227c092: mov %rbx,0x7cdd118    ; inside helper 0x227be14 (= JNI_OnLoad+0xeba)
   ```
   `0x227be14` allocates and initialises a ~0x338-byte registry object and publishes it.
   It is referenced as a *function pointer* at `0x7cdd108`, inside the static-init
   descriptor built by `ctor[3594]` (`0x2279055`–`0x22790d7`), which also builds the
   `"ShadowFValuesEnabled"` descriptor at `0x7cdd0d8`. In the harness `JNI_OnLoad`
   is skipped, so `0x7cdd118` stays NULL and every `0x227c18e` call short-circuits
   at `0x227c247`.
2. **FFlag provider registry at `0x72a0520`** — the *other* registry, built by
   `ctor[0] = 0x2e62390` → `0x2e618b0` → `0x2e61280` (DECISION 008 skips it; see
   `analysis/fflag-registry-trigger.md`). This is the Java-backed flag source the
   JNI getter at `0x227c25f` ultimately consults.
3. **A real JNIEnv/JavaVM with valid method IDs** and a Java flag source that
   answers `true` for `"DebugForceTerminateOnRbxCrash"`. Without it the boolean
   delivered to `0x2365512`/`0x2365467` is the default `false`.

### 1.6 What `nativeInitializeNativeFlags` (`0x2465210`) actually contributes

`0x2465210` is a real JNI export
(`FlagJniInterface.nativeInitializeNativeFlags([Ljava/lang/String;)…`). Its body:

```
0x2465295  mov 0x7cdd118,%rdi       ; read the registry (NULL in harness)
0x246529c  call 0x246549c           ; helper: lock registry+0x28, bump counter at +0x248,
                                    ;    build a 0x40-byte local-flags object (0x246554a)
0x24652cf  call 0x231bc4c           ; readLocalFlags
0x2465308  loop over jstring[]:
0x2465358  call 0x2465818          ; per-name local-flag init
              0x2465818 -> 0x24658be -> 0x227c49e (registry impl at +0x130)
              then recursive 0x24658be; JNI calls via registry vtable +0x558/+0x568
0x246542e  call 0x227bdd8
```

So `nativeInitializeNativeFlags` feeds the registry its **local flag overrides**
from the Java `String[]` and bumps its counters. It does **not** itself write
`0x7cef650`: the existing opt-in `SOBER_NATIVE_FLAGS_INIT`
(`src/linux/guest_app_shell.c:1180-1187`) calls it with a synthetic empty array and
returns `rc=0`, which is why the log shows `flagCount = 0` and the byte stays 0.
It is a **prerequisite/feeder**, not the writer; the write is `0x227d511 →
0x227c18e → 0x236534c → 0x2365467`.

---

## 2. `0x7cdd149` — a crash-handler latch, not flag state

A disassembly-wide census gives 2,273 literal references. Classified by opcode:

* ~2,180 are `cmpb $0,0x7cdd149` (or `mov`/`and` of the byte) — readers/guards.
* Exactly **six** are writers, all the same two-instruction idiom:

```
<writer>: movb $1,0x7cdd149
          cmpb $0,0x7cdd0d8            ; "ShadowFValuesEnabled" flag byte
          je  <skip>
          mov  0x7cdd118,%rdi
          call 0x6c8323c               ; install shadow-F-values crash handler
          movb $1,0x7cdd148            ; companion latch used at 0x2365462
```

| Writer RVA | Enclosing real guest routine (export RVA / offset) |
| --- | --- |
| `0x2eb7520` | `nativePassKeyEvent` (`0x2eb67ec` + `0xd34`) |
| `0x2ec9fae` | `CrashpadHandlerMain` (`0x2ec9c30` + `0x37e`) |
| `0x56d31a3` | large `native_1dismissAll` body (`0x3a09e06` + `0x1cc939d`) |
| `0x56d3386` | same body, second site |
| `0x56d6a4b` | same body, third site |
| `0x56d6b80` | same body, fourth site |

No flag thunk, no `ctor[3594]`/`ctor[3602]`, and no JNI flag export writes it. It
is a "crash subsystem installed" latch: the second stage is deliberately dormant
in the harness because `ctor[3594]` clears `0x7cdd0d8` at `0x2279055` and the
shadow-F-values flag is never fetched (§1). This confirms and sharpens the prior
`int3-6caef66-rbxcrash.md` classification.

Representative reader semantics: `0x6bfa149` (inside the real
`nativeAppBridgeAppStart` export) checks `0x7cdd149` and `0x7ccc6f8` before
proceeding — i.e. app-bridge startup consults the crash-handler latch. That is
why a zero latch is visible far beyond the reporter.

---

## 3. The reporter, verbatim (for reference)

```
0x6caef4f  cmpb $0,0x7cdd149        ; A
0x6caef56  je   0x6caef66           ; A==0 -> int3
0x6caef58  cmpb $0,0x7cef650        ; B
0x6caef5f  je   0x6caef66           ; B==0 -> int3
0x6caef61  call abort@plt           ; A!=0 && B!=0 -> abort
0x6caef66  int3
```

Every arm crashes. Nonzero bytes are a *different trap type*, not recovery.

---

## 4. Minimal opt-in real-guest-code sequence to make both bytes nonzero

All steps are genuine guest instructions/data already present in the engine — no
byte patch, no fabricated vtable, no synthetic callback, no direct store to either
global. Suggested knobs are additions for the lead to make; this worker edits none.

| # | Action (real guest RVA) | Effect / verification |
| --- | --- | --- |
| 0 | Existing defaults ON: `SOBER_REPLAY_FFLAG_CTOR` (`0x2e62390`), `SOBER_BIONIC_SF_FULL`, `SOBER_SCHED_R70_KEEP`, `SOBER_NATIVE_FLAGS_INIT` (`0x2465210`) | Gets past the known FFlag-ctor / stdio / scheduler faults so guest startup can reach the flag and crash subsystems. |
| 1 | Ensure the native flag registry is published: run the real `JNI_OnLoad` path (or, narrowly, replay its helper `0x227be14`) so `0x227c092` executes | Read `*(guest+0x7cdd118)`; must be non-NULL. Without it `0x227c18e` short-circuits at `0x227c247`. |
| 2 | Feed the registry from Java: call `nativeInitializeNativeFlags` `0x2465210` with a **real** `jstringArray` and a Java flag provider that answers `true` for `"DebugForceTerminateOnRbxCrash"` | `0x246529c`/`0x2465818` populate `0x7cdd118`; the existing synthetic empty-array call is insufficient. |
| 3 | Invoke the flag's own thunk `0x227d511` (no arguments) — or the generic entry `0x227c18e` with `rdi=*(0x7cdd118)`, `rsi=0x3a30af`, `rdx=0x7cef650`, `ecx=1`, `r8d=4` | Chain: `0x227c228` stores out-ptr → `0x227c284` calls `0x236534c` → `0x2365467` writes the parsed bool. Verify `*(uint8_t*)(guest+0x7cef650) == 1`. Thunk `0x22e567a` is the same mechanism for a different flag and is NOT a substitute. |
| 4 | Let the guest's crash-handler install run: `CrashpadHandlerMain` `0x2ec9c30` (writer `0x2ec9fae`) or JNI `nativePassKeyEvent` `0x2eb67ec` (writer `0x2eb7520`) | Verify `*(uint8_t*)(guest+0x7cdd149) == 1`. Companion `0x7cdd148` becomes 1 only if `0x7cdd0d8` (`ShadowFValuesEnabled`) is also fetched true via thunk `0x227e2c6`. |
| 5 | Observe the reporter at `0x6caef4f` | With A=B=1 it calls `abort()` at `0x6caef61` instead of trapping at `0x6caef66`. |

**Order matters:** step 1 before 2 (registry must exist before `0x2465818`/`0x227c18e` can use it); step 2 before 3 (Java override must be in the registry before the thunk reads it); steps 1–4 before the reporter runs. ctor[3594]/ctor[3602] are already replayed and need no extra action.

---

## 5. Honesty / decision-013 implications (must be stated with any use of the above)

1. This sequence supplies **missing real initialization**, not a crash fix. With
   both bytes nonzero the process still dies — via `abort()` at `0x6caef61`.
   Claiming otherwise would be false.
2. Calling a thunk such as `0x227d511` in isolation, with no genuine Java flag
   source, would only *simulate* the loaded state; that is synthetic guest state
   and belongs on the decision-013 withhold list, not in a green result.
3. None of these paths emits a guest→Java callback from the real app-start path.
   Per decision 013 the `app shell rendered` marker must stay withheld; no
   `src/`, gate, or test change is implied by this analysis.
4. The upstream cause of the `int3` remains the scheduler neutralization
   (`native_jni.c` zeroing `+0x70` of `0x722e540` → RSL panic
   `257f718-Context,20`); see `analysis/int3-6caef66-rbxcrash.md` §5.

## 6. Evidence index

* `libroblox.dis`: `0x2279a47`–`0x2279aca` (ctor[3602] descriptor/default),
  `0x227d511`/`0x227d536`/`0x22e567a`/`0x227e2c6` (thunk family),
  `0x227c18e`–`0x227c3ff` (registration; out-ptr at `0x227c228`),
  `0x236534c`–`0x2365467` (vtable `+0x28` write-through),
  `0x2465210`–`0x2465455` (`nativeInitializeNativeFlags`),
  `0x227be14`/`0x227c092` (sole `0x7cdd118` writer),
  `0x2279055`–`0x22790d7` (ctor[3594] static-init descriptor),
  `0x2eb7520`/`0x2ec9fae`/`0x56d31a3`/`0x56d3386`/`0x56d6a4b`/`0x56d6b80`
  (`0x7cdd149` writers), `0x6caef4f`–`0x6caef66` (reporter).
* `str_at.py`: `0x3a30af` = "DebugForceTerminateOnRbxCrash", `0x2bfe87` =
  "IOSUseMachExceptionRbxAssert", `0x43650d` = "EnableAppBridgeComponents",
  `0x23178f` = "ShadowFValuesEnabled".
* `reloc.py dump 0x721f230` → slot `+0x28` = `0x236534c`; `nm -D` export RVAs
  `0x2465210`, `0x2eb67ec`, `0x2ec9c30`, `0x3a09e06`.
* `grep` census: `0x7cef650` = 3 textual xrefs; `0x7cdd149` = 2,273 xrefs of
  which 6 are `movb $1` writers; `0x7cdd118` = 1 textual writer.
* `analysis/fflag-registry-trigger.md`, `analysis/int3-6caef66-rbxcrash.md`,
  `src/linux/guest_app_shell.c:967,1180,1421-1424` (existing opt-ins),
  `src/compat/bionic_bootstrap/bootstrap_profile.h:154-161` (ctor ranges).
