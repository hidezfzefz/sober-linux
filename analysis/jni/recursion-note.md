# Note: ctor[3613] recursion fault at guest RVA 0x6ca7f95 / 0x6ca8014

Owner: **worker-1** (task #383 follow-up, lead request). Scope: `analysis/jni/`.
All RVAs are AppImage build `204ad049` (same SHA as `jni-map.md`). Evidence:
REA `ev_d838d804…` full `.text` disassembly; local targeted `objdump -d`.

## What the code is

The recurring frame `0x6ca8014` is the **epilogue** of the function starting at:

- **`0x6ca7f72`** — a JNI exception-check / scheduler-task wrapper:
  - `0x6ca7f80`: loads a global singleton pointer from `0x72293f8` (the
    guest's JNI/ABI global slot, initialised by phase-2 `ctor[5]`), saves it
    as a stack canary at `-0x18(%rbp)`.
  - `0x6ca7f95` (the faulting call): `call 0x2a13350` — a predicate that reads
    the scheduler singleton returned by `0x2a0de60` and returns
    `*(obj+0x28) != 0`.
  - **predicate TRUE path** (`0x6ca7f9a..0x6ca7ff8`): builds a small closure at
    `0x6ca7fa3` whose function pointer is the lambda **`0x6ca805b`**, calls
    `0x2a13560` → tail-jumps **`0x2a12e80`** (scheduler dispatcher), then calls
    the closure's function pointer indirectly at `0x6ca7ff0` (`call *%rax`).
  - **predicate FALSE path** (`0x6ca7ffa..0x6ca8014`): calls `0x6c89e50`,
    `0x6c89e70` (JNIEnv getters) and then
    **`call *0x720(%rax)` at `0x6ca800e`**.
- **`0x6ca805b`** — the lambda. At `0x6ca807c` it calls the same JNIEnv
  getters and at **`0x6ca8090` executes `call *0x720(%rax)`**.
- **`0x2a12e80`** — scheduler dispatcher. It increments a 6-bit nesting
  counter at `obj+0x70` (`and $0x3f` / `inc`), and:
  - if `*(obj+0x28) != 0`: calls `0x2a1e3a0` (schedule/run task) which invokes
    the lambda `0x6ca805b`;
  - else (`0x2a12f21`): calls the closure function pointer `*(r14)` **twice**
    with `rdi = r14+8`, then clears it.
- **`0x2a0de60`** — singleton getter for the scheduler object (reads the flag
  at `0x722e560`, falls back through `0x722e540`). The predicate's
  `obj+0x28` field and the nesting counter at `obj+0x70` are the state that
  drives the wrapper.

**`0x720` is JNIEnv offset `0x388`-family: index 228 in
`JNINativeInterface_` = `ExceptionCheck`** (confirmed by two independent call
sites: `0x6ca800e` and `0x6ca8090`, both preceded by JNIEnv getters
`0x6c89e50`/`0x6c89e70`). So this entire subtree is the guest's
**"check for a pending Java exception after a scheduled task"** path.

## Root cause assessment

This is a **JNIEnv-hook re-entrancy recursion**, not a missing guest class:

1. The path is entered when `*(scheduler+0x28) != 0` — i.e. the guest has an
   installed JNI exception/task callback (a function pointer in the scheduler
   singleton). During phase-2 init the scheduler callback slot is normally
   NULL, so this branch was never taken; enabling the app-shell path
   (`SOBER_APP_SHELL=1`) / installing worker-2's JNIEnv hooks makes it
   non-NULL.
2. The guest then calls our JNIEnv `ExceptionCheck` (vtable offset `0x720`)
   from `0x6ca800e` and again from the lambda at `0x6ca8090`. If our
   `ExceptionCheck` hook either (a) calls back into the guest, or (b) returns
   a non-zero/"pending" result that makes the dispatcher retry the task, the
   wrapper at `0x6ca7f72` is re-entered on every iteration, incrementing the
   nesting counter at `obj+0x70` until the stack is exhausted
   (`si_addr≈rsp`, repeated `0x6ca8014` frames).
3. The immediate trigger is therefore **our JNIEnv hooks / the app-shell
   startup sequence**, not `JNI_OnLoad` alone: `JNI_OnLoad` only caches method
   IDs (see `jni-map.md` §1); it does not install this scheduler callback.

## Addendum — more precise root cause from worker-2 (verified here)

Worker-2 traced the same fault one level deeper, and I confirmed it by
disassembly of `0x2ea6440` (called from `0x2a0de60`):

```
2ea6455: mov 0x72a1b08(%rip),%edi     ; pthread_key_t from guest BSS global
2ea645b: call pthread_getspecific@plt ; 0x6d322e0
2ea6460: test %rax,%rax
2ea6463: je 2ea6520                  ; NULL => allocate fresh thread state
2ea646c: mov 0x8(%rax),%r15          ; dereference the returned TSD pointer
```

So the "scheduler singleton" `obj` from `0x2a0de60` is actually **guest TLS
thread state obtained through Bionic `pthread_getspecific`**. The
`0xabcdef0123456789` value seen at the fault is `__stack_chk_guard` from
`src/compat/android_compat.c:42` (worker-2), not a JNI bridge handle. Because
our glibc `pthread_getspecific` returns that stale/non-NULL value for the
guest key `0x72a1b08`, the guest takes its "thread state already exists"
branch, dereferences a garbage pointer, and the JNI exception-check wrapper
`0x6ca7f72` recursion/stack-exhaustion follows.

The primary fix is therefore worker-2's opt-in
`sober_jni_seed_guest_tls(guest_base, 0x72a1b08, state_bytes)`: call
`pthread_setspecific(key, zeroed_block)` so the guest's `getspecific` returns a
valid, zeroed state block. My earlier suggestion to zero `obj+0x28` is a
secondary/no-op once the TLS state is valid. Long-term the frozen versioned
libc shim needs Bionic-compatible `pthread_{get,set}specific` with an internal
TSD array.

## Second recursion site — `FindClass` wrapper `0x6c8f340` (lead follow-up)

After the TSD fix removed the `0x6ca7f95` recursion, the app shell starts
(`TelemetryProtocol::setProcessTimeOverride: 0`, `DeviceStaticParams is null.`)
but faults at guest RVA `0x6c8f368` with repeated frame `0x6c8f408`. This is
the **same scheduler-gated wrapper pattern, but for `FindClass`**:

- Function start **`0x6c8f340`** (canary from `0x72293f8` at `0x6c8f35d`).
- `0x6c8f368`: `call 0x2a13350` — the same TLS/scheduler predicate
  (`*(obj+0x28) != 0`, obj from `0x2a0de60`).
- TRUE path (`0x6c8f371..0x6c8f3e5`): builds closure with lambda
  **`0x6c8f451`**, calls `0x2a13560` → dispatcher `0x2a12e80`, then calls the
  closure function pointer indirectly at `0x6c8f3e3`.
- FALSE path (`0x6c8f3ee..0x6c8f405`): `0x6c89e50`, `0x6c89e70`, then
  **`call *0x30(%rax)` = JNIEnv `FindClass`** at `0x6c8f405`, with
  `rdi = class-loader/Env object`, `rsi = class-name argument`.
- Lambda `0x6c8f451` also calls `call *0x30(%rax)` (FindClass) at `0x6c8f493`.

So the recursion is the guest's **scheduler-gated JNI FindClass dispatch**,
not a distinct bug: it is driven by the very same `obj+0x28` predicate. Two
concrete follow-ups:

1. Confirm the TLS seed actually made the predicate FALSE: after
   `sober_jni_seed_guest_tls(guest_base, 0x72a1b08, …)`, the state block
   returned by `0x2a0de60` should have `*(obj+0x28) == 0`. If it is still
   non-NULL, the scheduler path stays active and every FindClass is queued as
   a task — a ready recipe for re-entrant dispatch. Zero that slot directly
   as a belt-and-braces seed.
2. **`getDeviceStaticParams()` currently returns NULL** (the
   `[WARN/JNIMain] DeviceStaticParams is null.` line). Worker-2's
   `sr_default_app_shell_handler` returns `0` for *every* callback, including
   object- and String-returning ones. The guest very likely retries class
   lookup / object construction for `com/roblox/engine/jni/model/DeviceStaticParams`
   when the callback returns NULL, looping through this FindClass wrapper.
   Give `getDeviceStaticParams`, `getWebViewUserAgent`,
   `getMobileAdvertisingId` and the locale/user getters **non-NULL typed return
   values** (a real `SrObject` of the right class / non-empty strings) in the
   default handler instead of returning 0.

## Addendum 2 — why glibc `pthread_setspecific` seeding failed (worker-3 A/B)

Worker-3 tried all combinations (TLS seed, scheduler neutralisation, both,
neither, larger state) with `JNI_OnLoad` enabled: all still hit a stack-overflow
recursion whose top frame is the `pthread_getspecific` helper `0x2ea6442` with
`rax = 0xABCDEF0123456789`. I verified the resolution chain:

```
guest pthread_getspecific@plt 0x6d322e0  (GOT 0x72298b0)
  -> build/ndk-stubs/libc.so:pthread_getspecific@@LIBC @0x52d0
  -> trampoline loads sober_libc_fn_pthread_getspecific
  -> glibc pthread_getspecific(key read from guest global 0x72a1b08)
```

The key value is a **Bionic TSD key**, but we feed it to glibc's key index
space (or an invalid key), so the returned pointer is the stale stack-guard
sentinel and `0x2ea6440` dereferences it. Calling glibc
`pthread_setspecific()` on the same key cannot fix this — the key namespaces
are different. The correct, still-fully-native fix is a **Bionic-compatible
TSD array in the versioned libc shim**: implement `pthread_key_create`,
`pthread_getspecific`, `pthread_setspecific` ourselves (a small `key -> void*`
map, or at least one slot for the single guest key) and point the three
trampolines at those implementations. This is a ~20-line targeted change and
does not introduce an Android runtime or translation layer. Once
`JNI_OnLoad` can return, the cached `jmethodID` globals are populated and a
genuine guest→Java callback can be observed for the truthful
`app shell rendered` marker (decision 013).

## Addendum 3 — `nativeAppBridgeAppStart` fault is a handle-magic check

At the point where the seeded-startup path reaches `nativeAppBridgeAppStart`
(`0x247dc8d`), the fault at `0x2456079` is a **JNI handle validation**, not a
global-seeding problem:

- `0x245605d` checks `cmpl $0x000bab1e,(%rdi)` at `0x2456079` (arg0 magic),
  `cmpl $0xc0dedbad,(%rbx)` at `0x245608f` (arg1 magic), then
  `[arg1+0x68] == arg0` and an index at `arg1+0x10` into a table at `arg0+0x10`.
  Returns 0 on failure, 4 on full success.
- Its caller `0x23331d6` passes `rdi = *(rbx+0x68)` where `rbx = *(jobj)` — a
  field inside a JNI object reference. A bad value (`0xffffff3085894cff`) means
  our JNI bridge returned a handle whose layout does not match the guest's
  expected object shape.
- Globals `0x72a3268/0x72a3270/0x72a3300` read by `nativeAppBridgeAppStart`
  are **not** written by `JNI_OnLoad` (writer of 0x72a3270 at `0x1dd695b`);
  they are phase-2 ctor state and should already be valid.

Fix belongs in `src/jni` handle semantics (worker-2): make jstring/jobject
handles guest-shaped, or neutralise the two magic checks. A **concrete native
bypass** was handed to worker-3: patch the validator entry at `0x245605d`
(first 6 bytes `55 48 89 e5 41 57`) to `b8 04 00 00 00 c3`
(`mov eax,4; ret`) — 4 is the validator's success return value and all four
callers are plain `call`s, so a leaf return is safe.

## Addendum 4 — next fault `0x23335c0` / `0x2358475` (same handle family)

After the decision-014 validator bypass, startup advances and faults at
`0x23335c0`: `mov 0x120(%rdx),%rdi; add $0x120,%rdx; lea 0xa60(%rbx),%rsi;
call 0x2358475` with `rdi = 0xffffff3085894cff` (same garbage family, fault
address `+0x20`). `0x2358475` is a small shared-pointer/refcount utility
(16-byte struct copy, back-pointer update at `+0x10`, returns 1/2/0) with only
two callers (`0x23335d5`, `0x23513ed`). Upstream chain:
`0x233316d <- 0x233319a <- 0x2333234 <- 0x23335c0`. Same root cause: a JNI
argument-derived guest object has an uninitialised/truncated pointer at offset
`0x120`; no seeded global is involved. Trial bypass handed to the team: patch
the first 3 bytes of `0x2358475` (`48 85 ff`) to `31 c0 c3`
(`xor eax,eax; ret`), low risk because there are only two callers and the
caller has a defined `eax==0` branch.

## Addendum 5 — exact narrow patch for 0x23335c0

Disassembly confirms worker-2's trace: `0x2333593 mov 0x68(%rdi),%rdx` loads the
garbage pointer; `0x23335c0 mov 0x120(%rdx),%rdi` dereferences it.
**Narrow patch (handed to worker-3):** at RVA `0x23335c0`, replace the 7 bytes
`48 8b ba 20 01 00 00` with `31 ff 90 90 90 90 90` (`xor edi,edi` + 5 NOPs).
The subsequent `add $0x120,%rdx` is pure arithmetic, and `0x2358475` returns 1
via its `test rdi,rdi; je` path when `rdi==0`; caller `0x23335da` has a defined
non-zero branch. Alternative (broader): `0x2333593` bytes `48 8b 57 68` ->
`31 d2 90 90` so the `je 0x2333681` early-exit is taken.

## Addendum 6 — next fault `0x23335f3`

After the `0x23335c0` patch, the next fault is `0x23335f3` `cmpl $0,0x8(%rcx)`
with `rcx = 0x9be800000a60b38d` (garbage) loaded at `0x23335e7`
`mov 0xf90(%rbx),%rcx` — same JNI-derived guest object, another uninitialised
field. Raw bytes at `0x23335e7` verified `48 8b 8b 90 0f 00 00`.
**Narrow patch:** replace those 7 bytes with `31 c9 90 90 90 90 90`
(`xor ecx,ecx` + 5 NOPs); `test rcx,rcx; je 0x23335f9` then takes the safe
branch. Note worker-2 confirmed the garbage originates in a **guest-internal
object** torn down by this helper (not a JNI handle), so guest-side narrow
patches are the correct vehicle; no further JNI-bridge changes are warranted.

## Addendum 7 — robust fix: skip the corrupted-object teardown helper

The `xor edi,edi` patch at `0x23335c0` is correct and safe: `0x2358475`
returns 1 on `rdi==0` and the caller has a defined non-zero branch. The
`0x23335f3` fault is a new garbage field. Helpers `0x23335a0..0x2333681` read
the same corrupted `[rbx+0xf90]` field TWICE (`0x23335e7` and `0x2333636`), so
per-field patches just move the fault. **Robust primary fix:** patch the entry
of `0x23335a0` (bytes `55 48 89 e5 53 50 48 89 fb`) to `31 c0 c3`
(`xor eax,eax; ret`) — it is a teardown/destructor reached via
`call *(%r15)` at `0x2333585`, so a leaf return is safe and removes the whole
corrupted-object teardown. Fallback per-field patches: `0x23335e7`
`48 8b 8b 90 0f 00 00` -> `31 c9 90 90 90 90 90` and `0x2333636`
`48 8b 83 90 0f 00 00` -> `31 c0 90 90 90 90 90`.

## Addendum 8 — expected layout of the synthetic JNI object (decision 017)

From the validator `0x245605d` and the teardown `0x233319a..0x2333681`:
the JNI handle is consumed as a **slot**: `rbx = *(rdi)` at `0x23331ae`, and
`0x23331ba` zeroes `*(rdi)` (releases the reference). So the bridge should
return a pointer to a slot containing the object pointer, or make the object's
first word self-consistent with that use.

Object **A** (the guest object behind the JNI reference, `rbx`):

| Offset | Type / value | Evidence |
| --- | --- | --- |
| `+0x00` | u32 magic `0xc0dedbad` | validator `0x245608f` |
| `+0x10` | u32 index into B's table (use 0) | validator `0x24560a5`, `0x24560c5` |
| `+0x20` | pointer, zeroed on teardown | `0x2333205` |
| `+0x68` | pointer to object B (canonical!) | `0x23331ca`, `0x2333593` |
| `+0x70` | pointer, zeroed on teardown | `0x23331f8`-region |
| `+0x735` | u8 flag, bit 3 tested | `0x23335de`, `0x233362d` |
| `+0xa50` | u64, zeroed on teardown | `0x2333666` |
| `+0xa58` | u32, zeroed on teardown | `0x2333671` |
| `+0xaa0`, `+0xab0` | pointers used by `0x233360e`/`0x233361c` | disassembly |
| `+0xf90` | pointer to object D (canonical!) — **A owns this**, not B | `0x23335e7`, `0x2333636` |
| `+0x1005`, `+0x1006` | u8 flags, bit 1 tested | `0x23331c1`, `0x233320d` |

Object **B** (from `A+0x68`):

| Offset | Type / value | Evidence |
| --- | --- | --- |
| `+0x00` | u32 magic `0x000bab1e` | validator `0x2456079` |
| `+0x10` | table of 8-byte pointers (base) | `0x24560b8`, `0x24560c2` |
| `+0x20` | u32 table capacity (use >= index+1, e.g. 16) | `0x24560bc` |
| `+0x120` | pointer to object C (canonical!) — **B owns this**, not A | `0x23335c0` |

Object **C** (from `B+0x120`): at least 16 writable bytes; passed as `rdi`
to shared-pointer helper `0x2358475` together with `rsi = &A+0xa60`.
Object **D** (from `A+0xf90`): at least 16 bytes with an i32 at `+0x08`
set to 0 (checked `cmpl $0,0x8(%rcx)` at `0x23335f3`/`0x2333642`).

Recommended synthetic seeding: canonical calloc'd A/B/C/D with the two magics,
`A+0x10 = 0`, `B+0x20 = 16`, B's table entries pointing at B, and `D+8 = 0`.
This replaces the per-deref patches (decision 017). Evidence: REA
`ev_d838d804…`; raw bytes and disassembly of `0x245605d`, `0x233319a`,
`0x23335a0`.

## Addendum 9 — origin of the bad object (decision 017 follow-up)

The object consumed at `0x233322c` is a **guest-created handle**, not a JNI
bridge handle: its caller function starts at `0x23330e7`, stores `rdi` in a
stack slot at `0x2333100`, and at `0x2333109` the guest itself checks
`cmpl $0xc0dedbad,(%rdi)` — so it recognises the object's magic yet its
`+0x68` field is uninitialised. `0x2333164` passes the slot address to helper
`0x233319a` (`48 85 ff` first bytes), which dereferences `[rbx+0x68]` and
starts the validator/teardown chain. There are **no direct callers** of
`0x23330e7` (indirect/vtable), so a writer-side patch is not statically
locatable. Recommended single native patch: overwrite the first 3 bytes of
`0x233319a` with `31 c0 c3` (`xor eax,eax; ret`) to skip this whole bad-object
consumer helper — it only validates/tears down the object (zeroes
`+0x20/+0x70/+0x300`, conditionally calls `sigaction(13)`), so a leaf return
is safe and the caller continues at `0x233316d`.

## Recommended mitigations (for worker-2 / worker-3)

- Make the JNIEnv `ExceptionCheck` hook **pure and non-reentrant**: return 0
  (no pending exception) immediately, with no guest callbacks, no logging that
  re-enters the JNI bridge, and no task scheduling. A re-entrancy guard
  (thread-local depth counter) around the whole JNI dispatch is strongly
  recommended.
- Clear/zero the scheduler callback slot `*(obj+0x28)` (obj from `0x2a0de60`,
  singleton base near `0x722e540`/`0x722e560`) before driving the app-shell
  startup. With that slot NULL the wrapper takes the short path at
  `0x6ca7ffa` and no task is scheduled.
- Also zero the 6-bit nesting counter at `obj+0x70` before startup so any
  early recursion cannot inherit a near-full depth.
- If the crash persists, set a hardware watchpoint on `obj+0x28` and on
  JNIEnv slot `0x720` to identify the installer; both are guest-side state,
  so a single deterministic seed before the first app-shell call should be
  sufficient.
- Verified independently: `JNI_OnLoad` is at `0x227af5a`, startup exports
  (`nativeGameGlobalInit 0x233762c`, `nativeAppBridgeV2InitWithParams
  0x24abf96`, `nativeScheduleOnFirstFrame 0x2ebf950`, etc.) match the live
  binary's dynsym; no RVA here comes from the Flatpak build.
