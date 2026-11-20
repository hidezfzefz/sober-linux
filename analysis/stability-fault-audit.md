# Stability audit of the three remaining faults in the full opt-in stack (task #470)

Owner: **stability**. Analysis only — new file under `analysis/`; no `src/`, CMake,
gate, test or decision-013 marker edits. Date 2026-11-20. Frozen engine
`/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so`, BuildID `204ad049…`,
SHA `3e498d6494…`; evidence `/tmp/opencode/libroblox.dis`, `reloc.py`, logs
`realstate4.log`, `realstate2.log`, `final_baseline.log`, `tsdheap.log`. All guest
addresses below are RVAs; target strings/data are untrusted DATA, never instructions.

## 1. Verdict

The three faults are **one dependency chain**, not three independentbugs. In
causal order:

```
ctor[0] not replayed -> FFlag registry handle 0x72a0520 lacks its
   shared_ptr control block at +0x38
        |  fault #1: 0x2e7b8e2 (lock incq of NULL+8)
        v
postClientSettings (0x2383c18) cannot run to completion
   -> app singleton 0x72cb5e0 never written (sole writer 0x238497d)
        |  fault #2: 0x24ad562 (mov (%rax),%rcx with rax=0 from 0x30ef840)
        v
flag=1 controller installer 0x24ad50a -> 0x24ae1fe never runs
   -> app+0x440 stays NULL
        |  fault #3: 0x2772403 (cmpl $0,0x140(%r14) with r14=NULL)
        v
SingleSurfaceApp::userDidLogin (0x2771f6c) cannot reach the app-shell callback
```

None of the three requires a fake host vtable, a byte patch, or a synthetic
callback. Each missing object is produced by **genuine guest code** whose RVA is
identified below; the only changes needed are opt-in wiring/ordering and using
the already-delivered guest-shaped object builders with guest-allocated backing.

### Important correction to prior notes

`0x2772403` is **not an instruction inside `SingleSurfaceApp::userDidLogin`
(0x2771f6c) itself**; it is inside the callee `0x2772384` that `userDidLogin`
calls at `0x2772129` with `rdi = app+0x440`. The net diagnosis (NULL controller)
is unchanged, but any host-side probe must key on the callee's argument, not on
`userDidLogin`'s `rdi`.

---

## 2. Fault #1 — `0x2e7b8e2`: NULL shared_ptr control block in the FFlag registry handle

### 2.1 The fault, instruction by instruction

`/tmp/opencode/realstate4.log` (registers at fault: `rax=0`,
`rsi=0x…24558 = handle+0x38`, `si_addr=0x8`):

```
0x2e5ba80  (generic shared_ptr copy helper)
 2e5ba86: mov  %rdi,%rbx            ; dest
 2e5ba89: add  $0x30,%rsi           ; source field = source_native_view+0x30
 2e5ba8d: call 0x2e7b8d0
   2e7b8d0: mov  (%rsi),%rax         ; rax = control-block pointer
   2e7b8d3: mov  %rax,(%rdi)         ; publish into dest
   2e7b8d6: lea  0x72a0b88,%rcx      ; guest "no control block" SENTINEL
   2e7b8dd: cmp  %rcx,%rax
   2e7b8e0: je   2e7b8e7             ; sentinel -> skip retain
   2e7b8e2: lock incq 0x8(%rax)      ; <-- FAULT: rax == 0 -> si_addr 0x8
```

Caller chain from `realstate4.log` (every frame is real guest code):

```
#2  0x2e7b8e2   shared_ptr retain (fault)
#3  0x2e5ba92   return from 0x2e5ba80
#4  0x2ed3b13   nativeRegisterJavaFlagProvider (0x2ed3aa4)
#5  0x6caee44   return after call 0x2ed3aa4 (rdi = 0x72a0520)
#6  0x6caed7c   flag-provider registration worker (FunctionMarshaller)
#7  0x2386599   nativePostClientSettingsLoadedInitialization3 (0x2383c18)
#11 0x2383c77   harness call site (SOBER_REAL_APPSTATE_POSTSETTINGS)
```

At `0x2ed3ae2..0x2ed3b0e` the guest computes the source view of the registry:

```
2ed3ae2: mov (%rbx),%rax          ; rbx = handle 0x72a0520
2ed3ae5: mov -0x18(%rax),%rax     ; D = *(descriptor) = 8  (verified: byte read
                                  ;  of file offset 0x6dafff0 gives word[0]=8)
2ed3ae9: lea (%rbx,%rax,1),%r12   ; native view = handle+8
2ed3b0b: mov %r12,%rsi            ; source for 0x2e5ba80
                                  ; helper adds 0x30 -> handle+0x38
```

So the missing word is **`*(uint64_t *)(guest_base + 0x72a0520 + 0x38)`** — the
shared_ptr control block of the FFlag registry handle at its native-view `+0x30`.

### 2.2 Who is supposed to install it (real guest code)

The guest's own static initializer installs it, via a lazy-initialized global:

```
init_array[0] 0x2e62390 -> 0x2e618b0 (guard 0x72a0a30) -> 0x2e61280
  2e6140e: call 0x2e5bc10         ; rdi = handle+8
    2e5bc58: lea 0x30(%rdi),%rax  ; rax = handle+0x38
    2e5bc5b: jmp 0x2e7b850
      2e7b850: guard byte 0x72a0cd8; if unset -> 0x2a0d6c0/0x2a0d7d0
      2e79af0: lazy create, 0x2e77400(0x72a0b88, 1)
                 stores control block at global 0x72a0cc0
      2e7b864: mov 0x72a0cd0,%rax   ; rax = global control block
      2e7b86b: mov %rax,(%rbx)      ; <-- writes handle+0x38
      2e7b87a: lock incq 0x8(%rax)  ; retain
```

`0x2e61280` also zeroes `handle+0x38` at `0x2e613f4` before the call above, and
gseed (`global_string_seed.c`) writes `+0x00/+0x08/+0x10/+0x30/+0x90/+0x98`
but never `+0x38`. `realstate4.log` confirms the cause: it has the gseed line
(3679) but **no `replayed guest ctor 0x2e62390` line** — `SOBER_REPLAY_FFLAG_CTOR`
was off. `0x72a0520` held gseed's hand-shaped value, with `+0x38 = 0`.

### 2.3 Minimal opt-in fix (no byte writes, no fake vtable)

1. **Run the real ctor**: `SOBER_REPLAY_FFLAG_CTOR=1` → call `guest_base+0x2e62390`.
   This executes `0x2e7b850`, which calls `0x2e79af0` to lazily build the control
   block from genuine guest sentinel `0x72a0b88` via `0x2e77400`, then stores it
   at `handle+0x38`. No JNI dependency, no callback, no host pointer. The knob
   already exists (`guest_app_shell.c:875-882`).
2. **Fix the wiring order (the real bug)**: `guest_app_shell.c:858-866` calls
   `sober_jni_json_populate_app_singleton()` (postsettings replay,
   `0x2383c18`) **before** the ctor replay at `:875-882`. With both opt-ins on,
   postsettings reaches fault #1 first. Move the `SOBER_JSON_APP_SINGLETON`
   block to AFTER the `SOBER_REPLAY_FFLAG_CTOR` block (global-string seed stays
   earliest). This is pure ordering — no new mechanism.
3. Narrow fallback (only if the ctor cannot be replayed on some path): invoke
   the genuine helper `guest_base+0x2e7b850` once with
   `rdi = guest_base+0x72a0520+0x38`. It is the guest's own code and handles
   lazy init itself. Do **not** store the sentinel `0x72a0b88` directly — that
   would be a raw word write, not guest state creation.

### 2.4 Read-only verification

Before/after the ctor call log: `guard 0x72a0a30` (expect `0x101`/257),
`0x72a0cd8` (lazy guard), `0x72a0cd0`/`0x72a0cc0` (control block, must be
non-NULL), and `*(uint64_t *)(0x72a0520+0x38)` (must be non-NULL and != sentinel
only if a real retain is intended). Success: fault #1 disappears.

---

## 3. Fault #2 — `0x24ad562`: `0x30ef840` returns NULL without the app singleton

### 3.1 The fault

`/tmp/opencode/realstate2.log` (`rax=0`, `si_addr=0x0`, FP chain
`0x23389fa → 0x23388a2 → 0x233876e`, i.e. the `nativeGameGlobalInit` worker):

```
0x24ad50a  (controller installer; vtable slot +0x30 of class 0x6dbc7c8,
           APS2: 0x6dbc7f8 -> 0x24ad50a)
 24ad531: cmpb $0x0,0x72cb648       ; FFlag EnableAppBridgeComponents
 24ad538: je 24ad5f2                ; flag=0 -> skip all of this
 24ad542: call 0x24ae02b
 24ad547: call 0x30ef840            ; returns 0 here
 24ad558: test %rcx,%rcx
 24ad562: mov (%rax),%rcx           ; <-- FAULT: rax == 0
 24ad56c: call *0x98(%rcx)
 ...
 24ad59e: call 0x30ef840
 24ad5a3: add $0xb0,%rbx
 24ad5b6: call *0x10(%rcx)          ; -> 0x24ae1fe installs app+0x440
```

`0x30ef840` decoded:

```
30ef844: call 0x6096928             ; feature gate (same gate as postsettings)
30ef849: test %al,%al
30ef84b: je 30ef86a                 ; alternate path via 0x1e31fb1 -> 0x30f07aa
30ef84d: mov 0x72cb5e0,%rdi         ; APP SINGLETON
30ef854: test %rdi,%rdi
30ef857: je 30ef87c                 ; NULL -> ...
30ef87c: xor %eax,%eax              ; ... RETURNS 0  <-- fault cause
30ef859: mov (%rdi),%rax
30ef85c: mov 0x10(%rax),%rax
30ef860: lea 0x72cb640,%rsi
30ef868: jmp *%rax                  ; genuine virtual call on the singleton
```

So the missing guest state is the **app singleton at `0x72cb5e0`**.

### 3.2 The sole legitimate writer

`nativePostClientSettingsLoadedInitialization3` (`0x2383c18`), at `0x238491e`:

```
238491e: call 0x6096928             ; SAME gate as 0x30ef844
2384925: je 23849a0                 ; gate false -> alternate path, no singleton
2384927: call 0x30efe14             ; config object
238492f: mov $0xa8,%edi
2384934: call 0x1dbab02             ; guest malloc(0xa8) -> r14 = app object
238493c: call 0x1dd55f2
238495a: mov %r14,%rdi
238495d: mov %rbx,%rsi
2384960: mov %r15,%rdx
2384965: call 0x608f85a             ; guest ctor for the app object
2384971: call 0x30efe88
238497d: mov %r14,0x72cb5e0         ; <-- THE ONLY WRITE OF THE SINGLETON
```

`grep 'mov.*72cb5e0'` over the full disassembly yields exactly this one store
(the other three hits are reads at `0x2384976`, `0x238498f`, `0x30ef84d`).
`realstate2.log` has no postsettings line and did not set it.

### 3.3 Minimal opt-in fix

1. Ensure the FFlag registry is real (§2) first.
2. Enable `SOBER_JSON_APP_SINGLETON=1`, which already calls
   `guest_base+0x2383c18` through `sober_jni_json_populate_app_singleton()`
   (`jni_json_bridge.c:242-281`) with `SOBER_NATIVE_OBJECTS=1`, so the List is
   `sober_native_java_list()` (genuine vptr `0x6db3ec0+0x10`) and the
   `size`/`isEmpty`/`get`/`getPackageName`/… JNI getters are installed
   (`jj_list_get`, `jj_exitinfo_get`). Do **not** use
   `SOBER_REAL_APPSTATE_POSTSETTINGS` with the plain zeroed 0x40 `java/util/List`
   at `guest_app_shell.c:671` — that is the object shape that led to fault #1.
3. The delivered `sober_native_java_list()` should be tightened so all its
   backing words are guest-shaped: allocate `+0x30` and `+0x40` from the guest
   allocator `0x1dbab02` (not host `calloc`), with `+0x30` a real control block
   (refcount 1 at `+8`, destroy slot from a genuine guest RVA or the sentinel
   `0x72a0b88`) and `+0x40` the element array; `+0x48` count. Today it stores
   one host `calloc` pointer in both `+0x30` and `+0x40`, which the guest's
   destructor `0x2e5baa0` / retain `0x2e7b8d0` interpret as a control block and
   an array respectively. This is a guest-shaped-object fix, not a host vtable.
4. Verify the gate too: read `0x77979c0` (gate byte) and the result of
   `0x6096928`; if the gate is false, neither postsettings nor `0x30ef840`
   takes the singleton path and fault #2 needs a different diagnostic.

Success criterion: `*(uintptr_t *)(0x72cb5e0)` non-NULL and attributable to the
store at `0x238497d`; fault #2 disappears.

---

## 4. Fault #3 — `0x2772403`: NULL user controller at `app+0x440`

### 4.1 The fault

`final_baseline.log` / `tsdheap.log` (`r14=0`, `si_addr=0x140`, FP chain
`0x277212e → 0x26fed20 → 0x23389fa …`):

```
SingleSurfaceApp::userDidLogin (0x2771f6c):
 2771f90: mov 0x20(%rdi),%rbx       ; app
 ...
 277211f: mov 0x440(%rbx),%rdi      ; <-- user controller passed as rdi
 2772126: mov %r14,%rsi            ; app+0x118
 2772129: call 0x2772384            ; helper
    277239b: mov %rdi,%r14          ; r14 = controller
    ...
    2772403: cmpl $0x0,0x140(%r14)  ; <-- FAULT when controller == NULL
 277215a: mov 0x408(%rbx),%rdi      ; delegate (separate opt-in, 0x2772161)
```

So the missing object is the **genuine user controller at `app+0x440`**, with a
readable `+0x140` field. The fake `SOBER_SEED_USER_CONTROLLER` (a zeroed
`guest_malloc(0x2000)` blob at `guest_app_shell.c:753-789`) satisfies that one
read but is not guest state; removing it must be replaced by the real installer.

### 4.2 The legitimate installer

From `analysis/appstate_fields.md` §3 and this audit's disassembly:

```
0x24ad50a  (controller installer; vtable slot +0x30 of class 0x6dbc7c8,
           APS2: 0x6dbc7f8 -> 0x24ad50a)
  24ad531: flag 0x72cb648 must be NONZERO
  24ad59e: call 0x30ef840           ; needs app singleton (§3)
  24ad5a3: add $0xb0,%rbx           ; this+0xb0 = shared_ptr built inside
                                    ; InitWithParams at 0x24ac5a9/0x24ac5c8
  24ad5b6: call *0x10(%rcx)         ; -> 0x24ae1fe
     24ae226: mov 0x20(%rdi),%rbx   ; app
     ... stores rdx into app+0x440
```

Prerequisites, all of which are real guest state produced by the fixes above:

* FFlag `EnableAppBridgeComponents` at `0x72cb648` genuinely 1 — the JSON flag
  bridge already sets it through the guest setter `0x23648be` (name string
  `0x43650d`, value `"True"`), not by a byte write.
* App singleton `0x72cb5e0` non-NULL (§3), so `0x30ef840` returns a real object.
* `nativeAppBridgeV2InitWithParams` (`0x24abf96`) run with fully backed getters
  so the `this+0xb0` controller shared_ptr is actually constructed at
  `0x24ac5a9`/`0x24ac5c8` (guest allocator `0x1dbab02`).
* The installer must actually be dispatched through the genuine vtable slot
  `0x6dbc7f8 -> 0x24ad50a`; there is no direct call in `.text`.

### 4.3 Minimal opt-in fix

1. Keep the flag = 1 (JSON bridge `0x23648be`) and singleton (§3) live.
2. Replay the real Java order from `analysis/appstate_fields.md` §5:
   `0x2edd859` (override store) → `0x2edd739` (apply) →
   `0x2ebcf36` (AppReady) → `0x24abf96` (InitWithParams, fully shaped) →
   `0x25448f6` (StartLuaAppDM, installs `app+0x408`) →
   `0x26fe80f` (StartAppWithParams) → `0x2773194` (surface params).
3. Make `SOBER_SEED_USER_CONTROLLER` conditional/no-op when
   `*(app+0x440)` is already non-NULL and guest-owned; then remove it from the
   stack as soon as the installer is observed to write the slot.
4. After the controller is genuine, re-check the *next* fault: with the fake
   controller gone, `0x2772161` (`mov (%rdi),%rax; call *0x90(%rax)` on
   `app+0x408`) becomes the next gate — this is the delegate, installed by
   `0x25448f6` or by `0x24aed3e` with genuine vtable `0x6e047b0`. This is a
   follow-up, not part of fault #3's fix.

Success criterion: `app+0x440` non-NULL, pointer inside the guest heap, and
`0x2772403` no longer faults; no fake vtable and no decision-013 marker touched.

---

## 5. Recommended order of experiments (each A/B, all opt-in, default OFF)

| # | Experiment | Knobs | Expected observation |
| - | - | - | - |
| 0 | Baseline diagnostics only; seeds still present | full stack as today | confirm genuine `onAppBridgeNotification` count; log `0x72a0cd0/cd8`, `handle+0x38`, `0x72cb648`, `0x72cb5e0`, `app+0x408/+0x440`, gate `0x6096928` |
| 1 | Fault #1 isolation | gseed ON, ctor replay OFF vs ON | OFF reproduces `0x2e7b8e2` (`rax=0`, `si_addr=0x8`); ON makes `handle+0x38` non-NULL and the fault disappears (realstate4 had no ctor line) |
| 2 | Wiring order | move `SOBER_JSON_APP_SINGLETON` block after ctor replay | postsettings replay no longer reaches fault #1; check `SOBER_REPLAY_FFLAG_CTOR` log line precedes the JSON line |
| 3 | Fault #2 | previous + `SOBER_JSON_APP_SINGLETON=1`, `SOBER_NATIVE_OBJECTS=1`, flag=1 | `0x72cb5e0` becomes non-NULL via `0x238497d`; `0x24ad562` disappears; if it still faults, log `0x6096928` and the List shape |
| 4 | Fault #2 hardening | make `sober_native_java_list()` backing guest-allocated (`0x1dbab02`) with a real control block at `+0x30`, array at `+0x40` | removes host-calloc-as-control-block hazard in destructor `0x2e5baa0` / retain `0x2e7b8d0` |
| 5 | Fault #3 | previous + remove `SOBER_SEED_USER_CONTROLLER`, replay `0x24abf96` then dispatch vtable `0x6dbc7f8 -> 0x24ad50a -> 0x24ae1fe` | `app+0x440` set by guest; `0x2772403` gone; capture next genuine fault (likely `0x2772161` on `app+0x408`) |
| 6 | Gate check | any of the above | decision-013 marker still WITHHELD; these are diagnostics, never app-shell proof |

Do not enable more than one new variable per run; all three faults are
ordering/state dependencies, so an A/B that changes two at once is ambiguous.
Keep the default (all opt-ins off) path unchanged: baseline `0x2eb4b65`,
phase-2/3 markers present, no marker emission.

---

## 6. Non-goals / honesty constraints

* No `src/` edits by this task; the fixes above are recommendations to the lead
  and the owners of `guest_app_shell.c`, `jni_json_bridge.c` and
  `jni_native_objects.c`.
* No fake host vtables: every vptr/control-block value recommended is either a
  genuine guest RVA (`0x6db3ec0+0x10`, `0x72a0b88`, `0x6e047b0`) or lazily
  created by guest code (`0x2e79af0` → `0x2e77400`).
* No byte writes to bypass code; the "sentinel" is mentioned only as a value
  the guest itself uses and should be installed by guest code (`0x2e7b850`).
* No callbacks fabricated; the genuine `onAppBridgeNotification` remains
  diagnostic only, and the decision-013 gate stays strict.

## 7. Evidence index

* `realstate4.log:3679,3682-3730` — gseed only, no ctor replay; fault
  `0x2e7b8e2`, `rax=0`, `si_addr=0x8`, FP `0x2e5ba92 → 0x2ed3b13 → 0x6caee44
  → 0x6caed7c → 0x2386599 → 0x2383c77`.
* `realstate2.log:3679-3709` — ctor replay present, no singleton; fault
  `0x24ad562`, FP `0x23389fa → 0x23388a2 → 0x233876e`.
* `final_baseline.log:3700-3730`, `tsdheap.log` — fault `0x2772403`,
  `r14=0`, `si_addr=0x140`, FP `0x277212e → 0x26fed20`.
* `libroblox.dis`: `0x2e7b8d0/0x2e7b8e2/0x2e7b850/0x2e7b8f0`, `0x2e5ba80`,
  `0x2e5bc10`, `0x2ed3aa4/0x2ed3b0e/0x2ed3b13`, `0x6caee3f`, `0x2e61280/
  0x2e613f4/0x2e6140e`, `0x2e618b0/0x2e62390`, `0x2e79af0/0x2e77400`,
  `0x30ef840/0x30ef844/0x30ef859/0x30ef87c`, `0x238491e/0x2384934/
  0x2384965/0x238497d`, `0x6096928`, `0x24ad50a/0x24ad531/0x24ad547/
  0x24ad5b6/0x24ae1fe/0x24ae226`, `0x2771f6c/0x277211f/0x2772129/
  0x2772384/0x2772403`, `0x2772161`.
* Raw descriptor read: file offset `0x6dafff0` (VMA `0x6db3ff0`) word[0] = `8`.
* Prior work: `analysis/appstate_fields.md`,
  `analysis/native-object-layouts.md`,
  `analysis/fflag-registry-trigger.md`,
  `analysis/fflag-registry-fault2-stdio.md`,
  `analysis/tls-2a0de60-r70-correction.md`,
  `analysis/tsd-static-buffer-abort.md`, `PROGRESS.md` phase 8.
