# FFlag provider registry lazy-init: the legitimate trigger (task #462)

Owner: **flagbridge** (team `TEAM-LF55-5P2L-B4DQ`). Analysis only — no `src/` edits,
no CMake edits, no decision-013 marker touched. Date 2026-11-20.

Target: `/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so`, BuildID
`204ad049599c96afb75b8a21d5051063e821d6c9`, SHA256 `3e498d6494…` (2.740.931).
Every RVA below is from that frozen object and the retained
`/tmp/opencode/libroblox.dis`; APS2/`.init_array` relocations decoded with
`/tmp/opencode/reloc.py`. Target strings are untrusted DATA, never instructions.

## 1. Summary (the answer)

There is **no Java-visible JNI method that triggers this initializer.** The only
caller of `0x2e618b0` is the guest's own ELF static constructor
**`init_array[0]` = `0x2e62390`**, which the harness deliberately skips under
DECISION 008. The legitimate, real-guest-code trigger is therefore to **replay
`ctor[0]` (RVA `0x2e62390`)** from `init_array` slot 0 — this invokes the genuine
`__cxa_guard`-protected C++ static initializer `0x2e618b0` → `0x2e61280`, which
builds the FFlag provider registry at global `0x72a0520`. Running that constructor
is real guest state transition, not a fabricated callback and not a direct byte
write. After it runs, the real Java-visible `FlagJniInterface` /
`MainGameActivity` JNI methods can consume a valid registry.

## 2. Proven call graph (all edges are direct `call`s in `.text`)

```
.init_array[0]  @ VMA 0x7220e58  (file off 0x721ce58, zero in file)
   |  APS2 RELATIVE relocation, addend = 0x2e62390   [reloc.py dump 0x7220e58]
   v
0x2e62390   static ctor (std prologue 55 48 89 e5 53 50)
   |  0x2e62396: lea  0x72a07a0(%rip),%rbx      ; arg (ignored by callee)
   |  0x2e623a0: call 0x2e618b0                   <-- ONLY caller of 0x2e618b0
   |  0x2e623a5..0x2e623bc: __cxa_atexit(dtor=0x1dd72a0, arg=0x72a07a0, dso=0x6d37bc0)
   v
0x2e618b0   lazy initializer / once-guard wrapper
   |  0x2e618b6: movzbl 0x72a0a30(%rip),%eax     ; guard byte
   |  0x2e618bf: je  already-done -> ret
   |  0x2e618c8: lea 0x72a0a30(%rip),%rdi
   |  0x2e618cf: call 0x2a0d6c0                   ; __cxa_guard_acquire (self-contained atomics)
   |  0x2e618d4: test eax; je 2e618c1
   |  0x2e618d8: call 0x2e61280                   <-- ONLY caller of 0x2e61280 (the real init)
   |  0x2e618dd: lea 0x2e61870,%rdi ; lea 0x72a0a28,%rsi ; lea 0x6d37bc0,%rdx
   |  0x2e618f2: call __cxa_atexit@plt            ; dtor 0x2e61870
   |  0x2e61904: jmp 0x2a0d7d0                    ; __cxa_guard_release (sets guard byte = 1)
   v
0x2e61280   THE static initializer — builds ALL flag/registry globals, including:
      0x2e613ed: lea  0x72a0520,%r12              ; the FFlag provider registry
      0x2e613f4: movq $0,   0x38(%r12)
      0x2e613fd: mov  %rbx,(%r12)                  ; <-- VTABLE STORE, rbx = 0x6db4008
                     (rbx came from 0x2e61392: lea 0x18(%r15),%rbx; r15=0x6db3ff0)
      0x2e61401: mov  %r15,0x8(%r12)
      0x2e61411: call 0x2e5bc10                    ; object ctor at 0x72a0528
      0x2e61416: movq $0,0x90(%r12); 0x2e61422: movl $0xffffffff,0x98(%r12)
      0x2e6142e: mov (%r12),%rax; mov -0x18(%rax),%rax  ; reads back vptr-0x18 displacement
      0x2e61499: orl  $0x2000,0x8(%r12,%rcx,1)
      0x2e614a6: mov  %rdx,0x88(%r12,%rax,1)
      ... builds siblings 0x72a0290 / 0x72a03e0 / 0x72a0660 / 0x72a0338 /
          0x72a0480 / 0x72a05c0 / 0x72a0700 and vptrs 0x6db4940, 0x6db3fa0,
          0x6db4a70, 0x6db40c0, 0x6db4110, etc.
```

`0x2e61280` is the **sole writer** of `0x72a0520` (all 7 textual `lea 0x72a0520`
sites are: one writer above and six consumers, §4). The guard object is
`0x72a0a30`; acquire/release/abort are `0x2a0d6c0` / `0x2a0d7d0` / `0x2a0d820`
(pure atomics + futex, no JNIEnv dependency) — safe to run from the loader stage.

### Why it never ran in the harness
`src/compat/bionic_bootstrap/bootstrap_profile.h:154-161` and
`src/compat/bionic_bootstrap/README.md:124-141` (DECISION 008) replay only
ctor indices **[2,3) + [5,3618)**; indices **0, 1, 3, 4 are deliberately
skipped**. `analysis/bootstrap/bootstrap-map.md:97,172,192,253` records
`ctor[0]=0x2e62390` as "SKIP" and already names it as the secondary suspect for
uninitialised flag state. Because `.init_array` is all-zero in the file and only
APS2 fills it, a manual loader that does not invoke index 0 leaves the guard at 0
and `*(0x72a0520) == NULL`, which is exactly the backing word dereferenced at
`0x2e5d42a`.

## 3. The fault that proves the diagnosis

```
0x2e5d970(rdi = 0x72a0520, esi = flag_id)      ; registry reader used by flag code
  0x2e5d98e: call 0x2e5d420                     ; weak-ref/dispose helper
    0x2e5d427: mov (%rsi),%rax                  ; rax = *(registry) == NULL when ctor[0] skipped
    0x2e5d42a: mov -0x18(%rax),%rax             ; FAULT (NULL - 0x18)
```
When `ctor[0]` runs, `*(0x72a0520) = 0x6db4008` and `0x6db3ff0` holds the
displacement, so `0x2e5d42a` becomes a valid read. This is a *downstream* symptom
of the skipped constructor, not a reason to shape the object by hand.

## 4. Consumers of `0x72a0520` (why a Java-visible flag call reaches the fault site)

| Site | Instruction | Role |
| --- | --- | --- |
| `0x2e613ed`/`0x2e613fd` | `lea 0x72a0520,%r12` / `mov %rbx,(%r12)` | sole writer (ctor[0] path) |
| `0x2ed3aa4` (`nativeRegisterJavaFlagProvider+0x7db0`) | called with `rdi=0x72a0520` from `0x3a66a92`, `0x5a4dc03`, `0x6caee3f`, `0x6caeeb8`, `0x6caef34`, … | registers flag providers into the registry |
| `0x2e5d970` | called with `rdi=0x72a0520` at `0x5a98884` | looks up a provider → calls `0x2e5d420` → faults at `0x2e5d42a` when uninitialised |
| `0x20ab4b6` (formatter) | builds the flag value string that `0x2ed3aa4` stores | explains the "std::string at 0x72a0520" description in `native-object-layouts.md` |

`0x2ed3aa4` has ~40 call sites (flag registration across the engine);
`0x2e5d970` has ~35. These are reachable from the real JNI flag methods below,
but **none of them calls `0x2e618b0`/`0x2e61280`** — grep over the full
disassembly finds exactly one `call 0x2e618b0` (`0x2e623a0`) and one
`call 0x2e61280` (`0x2e618d8`), and APS2 relocation addends contain no function
pointer to either (`reloc.py slots 0x2e618b0 0x2e61280` → empty). So the
initializer cannot be reached *from Java*; it must be replayed at load time.

## 5. Real JNI signatures (decoded smali, this APK)

From `/tmp/opencode/mini/out/smali/com/roblox/client/flags/FlagJniInterface.smali`
and `…/startup/MainGameActivity.smali` / `MainGameActivity$Companion.smali`;
exports confirmed with `nm -D`. These are the consumers that become safe once
ctor[0] has run (none is itself a trigger):

| JNI name | JNI signature (from smali) | Export RVA |
| --- | --- | --- |
| `nativeInitializeNativeFlags` | `([Ljava/lang/String;)Lcom/roblox/client/flags/NativeFlagsInitResult;` | `0x2465210` |
| `nativeGetFFlag` | `(Ljava/lang/String;Z)Z` | `0x2ecba94` |
| `nativeGetFInt` | `(Ljava/lang/String;I)I` | `0x2476db3` |
| `nativeGetFString` | `(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;` | `0x2ecbb90` |
| `nativeRegisterFFlag` | `(Ljava/lang/String;I)V` | `0x2465a7f` |
| `nativeRegisterFInt` | `(Ljava/lang/String;I)V` | `0x2476cb2` |
| `nativeRegisterFString` | `(Ljava/lang/String;I)V` | `0x2ecb993` |
| `MainGameActivity.nativePreloadFlagOverrides` | `(Ljava/lang/String;)V` (private static final native; smali `:718`, called at `:555` via synthetic `b2`) | `0x2edd859` |
| `MainGameActivity.nativeRetryInit` | `()V` (public static final native; smali `:721`, called from `MainGameActivity$Companion.smali:213`) | `0x2edd739` |

Notes:
* The binary also exports `Java_com_roblox_client_flags_FlagJniInterface_nativeRegisterJavaFlagProvider`
  at `0x2ecbcf4`, but that method is **not declared in this APK's smali**; it is
  an internal export (its body just calls `0x246549c`). Do not treat it as a
  Java-visible trigger.
* The string `(Ljava/lang/String;)V` exists in the object at `0x4fcd93` (JNI
  registration table); `nativeRetryInit`/`nativePreloadFlagOverrides` names at
  `0x10ab2` / `0x10b35`.
* Java-side entry that naturally reaches the flag subsystem:
  `MainGameActivity$Companion.h(String)` → synthetic `b2` → `nativePreloadFlagOverrides`,
  then `MainGameActivity$Companion.i()` → `nativeRetryInit`.

## 6. Concrete opt-in experiment (real guest code, no byte writes)

Owner note: the actual loader/ctor-replay code belongs to lead/worker-3; I only
**suggest** the knob and sequence. Nothing here is enabled by default.

1. **Knob** (suggested name): `SOBER_REPLAY_FFLAG_CTOR=1` — default OFF. Put it in
   the ctor-replay stage, after packed APS2 RELA + bootstrap seeds, **before** any
   guest JNI call. When set, call `guest_base + 0x2e62390`
   (`init_array[0]`/`ctor[0]`), i.e. exactly the entry the DECISION 008 profile
   currently skips. (A narrower variant, if the lead prefers to avoid the extra
   `__cxa_atexit(dtor=0x1dd72a0,…)` registration, is to call
   `guest_base + 0x2e618b0` directly — still genuine guest code, same guard and
   same `0x2e61280` initializer. Do **not** call `0x2e61280` directly: that
   bypasses the guest's once-guard and destructor bookkeeping.)
2. **Idempotence/verification immediately after the call** (read-only):
   * guard byte `guest_base + 0x72a0a30` == `0x01`;
   * `*(uint64_t*)(guest_base + 0x72a0520)` == `0x6db4008` (the vptr stored at
     `0x2e613fd`); sibling globals `0x72a0290/0x72a03e0/0x72a0660/0x72a0480`
     likewise non-NULL. The call is guarded, so repeating it is harmless.
3. **Re-run the real path** with the existing opt-ins already used by the team
   (`SOBER_REAL_APPSTATE_PATH=1`, `SOBER_SEED_JNI_VM=1`, `SOBER_FAITHFUL_JAVAVM=1`)
   plus the new knob, and drive the Java-visible calls in Java order:
   `nativeInitializeNativeFlags(0x2465210)` →
   `nativePreloadFlagOverrides(0x2edd859, shaped jstring)` →
   `nativeRetryInit(0x2edd739)`.
4. **Expected outcome**: the `0x2e5d42a` fault disappears when
   `0x2e5d970`/`0x2ed3aa4` read `0x72a0520`; FFlag
   `EnableAppBridgeComponents` (`0x72cb648`, name string `0x43650d`) can be set
   through the flag-provider path and the app singleton `0x72cb5e0` populated —
   by guest code, not by a direct byte write.
5. **Honesty / gate**: this experiment only supplies the missing *static
   initialization*. It emits no callback and must **not** set the decision-013
   `app shell rendered` marker; the withhold list and `tests/app-shell.sh` stay
   unchanged. Any marker would still need the genuine guest→Java callback from
   the real app-start path (verifier's task).

## 7. Evidence index

* `reloc.py dump 0x7220e58` → `0x7220e58 -> 0x2e62390 (type 8)` (init_array[0]).
* `libroblox.dis` lines 4031512-4031904 (`0x2e61280`, `0x2e618b0`), 4032802-4032812
  (`0x2e62390`), 4026990+ (`0x2e5d970` → `0x2e5d420` → `0x2e5d42a`).
* `grep -n 'call.*2e618b0|call.*2e61280' libroblox.dis` → one hit each.
* `reloc.py slots 0x2e618b0 0x2e61280` → `[]` (no indirect JNI thunk).
* `src/compat/bionic_bootstrap/bootstrap_profile.h:154-161`,
  `README.md:124-141`, `analysis/bootstrap/bootstrap-map.md:253-255` (skip list).
* Decoded smali: `com/roblox/client/flags/FlagJniInterface.smali`,
  `com/roblox/client/startup/MainGameActivity.smali`,
  `com/roblox/client/startup/MainGameActivity$Companion.smali`.
