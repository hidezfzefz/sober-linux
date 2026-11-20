# A13 — Why the genuine controller installer never writes `app+0x440`

Status: **FINAL ANALYSIS — no code changes.** Scope: this file only. `src/`
shared files, `CMakeLists.txt`, the gate/test scripts and the engine binary were
READ ONLY. Worker A13, logical Team A (wave 2), strict decision 013.

Engine: `/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so`, BuildID
`204ad049599c96afb75b8a21d5051063e821d6c9`, SHA-256
`3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335` (re-verified
unchanged before and after this analysis). All RVAs below are AppImage-build
only.

Evidence classes: **OBSERVED** = bytes read from the frozen ELF via
`objdump -d --start-address/--stop-address` (`/tmp/opencode/dis.sh`) or APS2
relocation decode (`python3 /tmp/opencode/reloc.py`); **RUNTIME** = quoted from
lead logs with source named; **DERIVED** = call-graph/role inference. No runtime
execution was performed by this worker. Target strings/data are untrusted DATA.

---

## 0. Executive answer

1. **The installer is reached and runs.** `nativeGameGlobalInit` (JNI entry
   `0x233762c`) dispatches vtable slot `+0x30` through the real guest call
   **`call *0x30(%rax)` at RVA `0x23389f7`** (helper `0x233896c`), which lands on
   the controller installer **`0x24ad50a`** (vtable base `0x6dbc7c8`, slot at
   `0x6dbc7f8`, APS2 type-8 relocation — OBSERVED, §1). The `realstate2.log` FP
   chain frame `#3 = guest+0x23389fa` is the return address of exactly that
   `call`, which proves it (RUNTIME, §2).
2. **The exact missing state is the app singleton at `guest_base+0x72cb5e0`.**
   It is still NULL. `0x24ad50a` calls `0x30ef840`, which dereferences the
   singleton; with it NULL, `0x24ad562` (`mov (%rax),%rcx`, `rax==0`) faults.
   The installer therefore **never reaches** `0x24ae1fe`, and the genuine store
   to `app+0x440` at **`0x24aea9c`** (`mov %r14,0x440(%rbx)`) never executes
   (OBSERVED §3, RUNTIME §4).
3. **FFlag ctor replay + JSON flag bridge are necessary but insufficient.** They
   fix the FFlag *registry* control block (fault #1, `0x2e7b8e2`) and set the
   *flag byte* `0x72cb648` = 1 via the guest setter `0x23648be`. Neither writes
   `0x72cb5e0`. The singleton has a single writer in the whole engine —
   `mov %r14,0x72cb5e0` at **`0x238497d`** inside
   `nativePostClientSettingsLoadedInitialization3` (`0x2383c18`) — and that
   replay path is gated by both flag `0x72cb648` (checked at `0x2384870`) and
   the GL-capability predicate `0x6096928` (checked at `0x238491e`). **No log in
   the tree ever enabled the singleton-population knob**, so this third state
   was never created (RUNTIME, §4).
4. **Minimal opt-in fix (all real guest code, no fake object, no byte patch):**
   keep ctor replay + JSON flag bridge, then additionally populate the app
   singleton through the existing replay/slice bridge (`0x2383c18` → guest
   ctor `0x608f85a` → guest store `0x238497d`), then let the already-replayed
   `nativeGameGlobalInit`/`InitWithParams` pair dispatch `0x23389f7`; disable
   `SOBER_SEED_USER_CONTROLLER` so the genuine write at `0x24aea9c` is the only
   writer (§5). Correct prerequisite order is mandatory.

---

## 1. The dispatch — the real guest call that invokes slot `+0x30` (OBSERVED)

### 1.1 The vtable slot

`python3 /tmp/opencode/reloc.py dump 0x6dbc7c8 8`:

```
0x6dbc7c8 -> 0x2ec004e (type 8)    0x6dbc7d0 -> 0x2ec007c (type 8)
0x6dbc7d8 -> 0x24ad3ee (type 8)    0x6dbc7e0 -> 0x2ec00be (type 8)
0x6dbc7e8 -> 0x2ec00d8 (type 8)    0x6dbc7f0 -> 0x253e42a (type 8)
0x6dbc7f8 -> 0x24ad50a (type 8)    <-- slot +0x30 = controller installer
```

`python3 /tmp/opencode/reloc.py slots 0x24ad50a` → `['0x6dbc7f8']` (unique).
So vtable base is `0x6dbc7c8` and **slot `+0x30` lives at `0x6dbc7f8`**. There
is no direct `call 0x24ad50a` in `.text`; it is reachable only through this
slot.

### 1.2 The instance that owns the vtable is built by `InitWithParams`

`0x24abf96` (`nativeAppBridgeV2InitWithParams`) constructs the `0xe0`-byte
object that carries `0x6dbc7c8`:

```
24ac57c: mov $0xe0,%edi
24ac581: call 1dbab02          ; guest allocator, size 0xe0
24ac586: mov %rax,%rbx
24ac589: mov %rax,%r15
24ac58c: lea 0x6dbc7c8(%rip),%rax   ; GENUINE vptr
24ac593: mov %rax,(%rbx)
24ac5a4: call 24ad210          ; build shared_ptr at obj+0x08
24ac5a9: lea 0xb0(%rbx),%r14   ; controller shared_ptr slot
24ac5c8: movups %xmm0,(%r14)   ; +0xb0 published here (from -0x2f0(%rbp))
24ac5e7: add $0xc8,%rbx
24ac620: lea -0x110(%rbp),%r14
24ac627: mov %r15,0x20(%r14)   ; object stored in wrapper at +0x20
24ac630: call 2eb6b2a          ; register wrapper into nativeGameGlobalInit
```

`0x2eb6b7d` uses global `0x72a31a8`; `0x2ebb57f` writes `0x72a31a8` with the
result of `0x23376d6` (`nativeGameGlobalInit+0xaa`, reached via the lazy bridge
at `0x2ebb576`). This is how the object reaches the dispatch helper. The same
factory pattern is inlined at `0x24ad3ee-0x24ad40d` (`mov $0xe0,%edi; call
0x1dbab02; lea 0x6dbc7c8; mov %rax,(%rbx)`) — a second genuine producer of the
same class.

### 1.3 The dispatch site

`0x233896c` (helper in the `nativeGameGlobalInit` worker, `0x233889d` calls it):

```
23389eb: mov 0x20(%rbx),%rdi    ; the app-bridge object registered above
23389ef: test %rdi,%rdi
23389f2: je 23389fa            ; NULL -> no dispatch
23389f4: mov (%rdi),%rax        ; rax = vptr = 0x6dbc7c8
23389f7: call *0x30(%rax)       ; <== DISPATCH -> 0x24ad50a
23389fa: mov 0x30(%rbx),%rdi    ; (next instruction; return address in FP)
```

**This `call *0x30(%rax)` at RVA `0x23389f7` is the real guest call that
dispatches the controller installer.** `0x23389fa` is exactly 3 bytes after it
(`ff 50 30`), so the FP frame at `0x23389fa` is its return address.

---

## 2. Runtime proof that this dispatch is the one that faults (RUNTIME)

`/tmp/opencode/realstate2.log` (ctor replay ON, JSON flag bridge `rc=0`,
nativeGameGlobalInit called):

```
3683: [app-shell] replayed guest ctor 0x2e62390 (FFlag registry): guard=257 ...
3696: [app-shell] JSON flag override bridge (SOBER_JSON_FLAG_BRIDGE) -> rc=0
3707: [app-shell] guest call 0x233762c nativeGameGlobalInit() ...
3713: [harness] rip maps to /proc/self/fd/3+0x24ad562
      [harness] rax=(nil)
3727: [harness] faulting guest RVA: 0x24ad562
3730: [harness]   #2 /proc/self/fd/3+0x24ad562
3731: [harness]   #3 /proc/self/fd/3+0x23389fa   <-- return of 0x23389f7
3732: [harness]   #4 /proc/self/fd/3+0x23388a2
3733: [harness]   #5 /proc/self/fd/3+0x233876e
```

The frame sequence `0x233876e → 0x23388a2 → 0x23389fa` matches the guest chain
`nativeGameGlobalInit worker (0x233876e/0x23388a2) → helper 0x233896c →
call *0x30 → 0x24ad50a`. The `0xe0` object existed (the `je 0x23389fa` at
`0x23389f2` was NOT taken), slot `+0x30` was dispatched, and the installer
faulted. No log line for any app-singleton population appears anywhere.

---

## 3. Installer internals — where the genuine write would happen (OBSERVED)

### 3.1 Stage 1: `0x24ad50a` (slot `+0x30`)

```
24ad51b: mov %rdi,%rbx              ; this = app-bridge object
24ad531: cmpb $0x0,0x72cb648        ; FFlag EnableAppBridgeComponents
24ad538: je 24ad5f2                 ; flag=0 -> alternate branch, no controller
24ad542: call 24ae02b               ; helper; checks latch 0x7cdd149, not a blocker
24ad547: call 30ef840               ; needs app singleton 0x72cb5e0
24ad558: test %rcx,%rcx
24ad562: mov (%rax),%rcx            ; <-- FAULT when 0x30ef840 returned NULL
24ad56c: call *0x98(%rcx)
24ad59e: call 30ef840
24ad5a3: add $0xb0,%rbx             ; this+0xb0 controller shared_ptr (§1.2)
24ad5aa: mov (%rax),%rcx
24ad5b6: call *0x10(%rcx)           ; -> 0x24ae1fe
```

`reloc.py slots 0x24ae1fe` → `['0x6e01ce0']`, i.e. `0x24ae1fe` is slot `+0x10`
of the object returned by `0x30ef840` (vtable base `0x6e01cd0`).

### 3.2 `0x30ef840` — the singleton gate

```
30ef844: call 6096928              ; GL-capability predicate
30ef849: test %al,%al
30ef84b: je 30ef86a                ; alternate path (0x1e31fb1 -> 0x30f07aa)
30ef84d: mov 0x72cb5e0,%rdi        ; APP SINGLETON
30ef854: test %rdi,%rdi
30ef857: je 30ef87c                ; NULL -> ...
30ef87c: xor %eax,%eax             ; ... RETURNS 0   <-- fault cause at 0x24ad562
30ef859: mov (%rdi),%rax
30ef85c: mov 0x10(%rax),%rax
30ef860: lea 0x72cb640,%rsi
30ef868: jmp *%rax                 ; genuine virtual call on the singleton
```

### 3.3 Stage 2: `0x24ae1fe` — the genuine store

```
24ae212: mov %rdx,%r14             ; rdx = this+0xb0 (controller shared_ptr)
24ae226: mov 0x20(%rdi),%rbx       ; rbx = app (from object built at 0x2384965)
24ae936: cmpb $0x0,0x72cb648       ; flag recheck
24ae93d: je 24aeb85
24ae943: mov $0x280,%edi
24ae948: call 1dbab02              ; guest malloc: 0x280-byte user controller
24ae95d: call 6d31d00              ; memset 0x208 bytes; fields initialised ...
24aea7d: mov $0x20,%edi
24aea82: call 1dbab02
24aea8e: lea 0x6e02568(%rip),%rcx  ; genuine vptr for the intrusive wrapper
24aea95: mov %rcx,(%rax)
24aea98: mov %r14,0x18(%rax)
24aea9c: mov %r14,0x440(%rbx)      ; <== THE GENUINE STORE TO app+0x440
24aeaaa: mov %rax,0x448(%rbx)      ; intrusive control ptr
```

`grep '0x440('` confirms the only guest writes of a pointer at app+`0x440` are
`0x24aea9c` (this installer) and the `flag==0` sibling at `0x24aebc0` (the
`0x460` field). The consumer that faults on the NULL is `0x2772403`
(`cmpl $0,0x140(%r14)` in helper `0x2772384`, called from
`SingleSurfaceApp::userDidLogin` `0x2771f6c` at `0x2772129`).

---

## 4. Why ctor replay + JSON flag bridge are not enough (OBSERVED + RUNTIME)

The two knobs fix two of three prerequisites; the third — `0x72cb5e0` — is
never created:

| Gate | Required state | Fixed by | Evidence |
| - | - | - | - |
| FFlag registry control block at `0x72a0520+0x38` | real guest control block | `SOBER_REPLAY_FFLAG_CTOR` → `0x2e62390` | `clean-tsd.log:3684` guard=257 |
| Flag `0x72cb648` | 1 | `SOBER_JSON_FLAG_BRIDGE` → guest setter `0x23648be` | `phase8-all.log:3710` `rc=1 flag=1` |
| **App singleton `0x72cb5e0`** | **non-NULL 0xa8-byte guest object** | **`SOBER_JSON_APP_SINGLETON` / `SOBER_APP_SINGLETON_BRIDGE` → `0x2383c18` → `0x238497d`** | **NO log ever shows this** |

Sole writer of the singleton (OBSERVED, `grep '72cb5e0'` over the full
disassembly): `238497d: mov %r14,0x72cb5e0`. Its enclosing path in
`0x2383c18`:

```
2384870: cmpb $0x0,0x72cb648        ; flag must be 1
2384877: je 2384a13                ; flag=0 -> skip singleton creation entirely
238491e: call 6096928              ; GL predicate must be true
2384925: je 23849a0                ; predicate false -> alternate path, no singleton
2384927: call 30efe14              ; settings
238492f: mov $0xa8,%edi
2384934: call 1dbab02              ; guest malloc(0xa8)
238493c: call 1dd55f2
2384965: call 608f85a              ; guest ctor for the app object
2384971: call 30efe88
238497d: mov %r14,0x72cb5e0        ; THE ONLY STORE
```

Predicate `0x6096928` (OBSERVED): reads lazy byte `0x77979c0` (init via
`0x2a0d6c0`), then returns
`(0x77979b8 < 0x7797990) && (0x7797968 != 0)`. If the flags-loaded latch
`0x7cdd149` is still NULL — as every fault dump shows
(`assert_flag1 @0x7cdd149 = nil`) — the predicate may be false and the *replay*
strategy cannot create the singleton even with flag=1.

Runtime cross-check (RUNTIME):

* `realstate2.log`: ctor replay + flag bridge both succeeded, yet fault at
  `0x24ad562` with `rax=0` — exactly the NULL-singleton signature. There is no
  `[app-shell] JSON app-singleton path` / `[app-shell] app-singleton bridge`
  line; `getenv("SOBER_JSON_APP_SINGLETON")` was unset.
* `phase8-all.log:3710`: flag=1 via `0x23648be`, but ctor replay was OFF →
  fault #1 at `0x2e5d42a` before postsettings; singleton never reached.
* `clean-tsd.log:3684/3694/3708`: ctor replay ON but JSON flag bridge `rc=-2`
  → flag=0, so `0x24ad50a` takes the `je 0x24ad5f2` branch and the fake
  `SOBER_SEED_USER_CONTROLLER` writes `app+0x440` instead
  (`user-controller seed: installed … at app+0x440`).
* Global grep: no `app-singleton bridge`/`JSON app-singleton path`/non-NULL
  `singleton=` line exists in any log. The missing state has never been
  exercised.

**Conclusion:** with the two knobs on, the guest reaches and dispatches
`0x24ad50a`, but `0x30ef840` returns NULL because `0x72cb5e0` is NULL, so the
installer faults at `0x24ad562` and `0x24ae1fe` (and its store at `0x24aea9c`)
never run. That is the whole reason `app+0x440` is never written by genuine
code.

---

## 5. Minimal opt-in sequence using real guest code (no fake object, no byte patch)

All steps are existing opt-ins/entries; none fabricates a callback or writes a
host vtable. Order is load-bearing.

1. **Early global-string seed** (`SOBER_SEED_GLOBAL_STRING=1`) — as today.
2. **FFlag registry ctor** — `SOBER_REPLAY_FFLAG_CTOR=1` → call
   `guest_base+0x2e62390`. Installs the shared_ptr control block at
   `0x72a0520+0x38` (guard `0x72a0a30` = 257). Must precede step 3 (the
   existing code at `guest_app_shell.c:973-980` already does).
3. **Flag** — `SOBER_JSON_FLAG_BRIDGE=1` → guest setter `0x23648be` sets
   `0x72cb648` = 1 (verified `phase8-all.log:3710`). Must follow step 2.
4. **App singleton (the missing state)** — enable one of the existing bridges
   at the existing call site (`guest_app_shell.c:984-1001`), *after* step 3:
   * Preferred, pure guest code: `SOBER_APP_SINGLETON_BRIDGE=1`,
     `SOBER_APP_SINGLETON_STRATEGY=replay`. It calls the genuine JNI entry
     `0x2383c18`; the guest allocates with `0x1dbab02`, constructs with
     `0x608f85a` and stores `0x72cb5e0` itself at `0x238497d`.
     * If predicate `0x6096928` reads false (latch `0x7cdd149` NULL), replay
       returns EMPTY; switch to `SOBER_APP_SINGLETON_STRATEGY=slice`, which
       invokes the same guest ctor helpers directly and performs only the
       single publication that mirrors `0x238497d` (no host vtable, no code
       patch).
     * Alternative equivalent knob: `SOBER_JSON_APP_SINGLETON=1` together with
       `SOBER_NATIVE_OBJECTS=1` (so the List at the `0x2383c18` call is the
       guest-vptr `0x6db3ec0+0x10` shape, not the zeroed `java/util/List` that
       faults at `0x2e5d42a`). This also ends in the guest's `0x238497d` store.
   * Verify `*(uintptr_t *)(base+0x72cb5e0) != NULL` before continuing.
5. **`nativeGameGlobalInit`** — replay JNI entry `guest_base+0x233762c`
   (already in the harness, `guest_app_shell.c:3701`-equivalent). This is what
   walks `0x233876e → 0x23388a2 → 0x233896c` and executes the slot `+0x30`
   dispatch at `0x23389f7`.
6. **`nativeAppBridgeV2InitWithParams`** — replay JNI entry
   `guest_base+0x24abf96` (already in the harness) with the
   `SOBER_STARTAPP_PARAMS`/getter bridge fully backed, so the `0xe0` object with
   vptr `0x6dbc7c8` and its `+0xb0` controller shared_ptr are actually built
   (§1.2). Order vs step 5 is immaterial as long as both precede the dispatch.
7. **Remove the fake** — make `SOBER_SEED_USER_CONTROLLER` a no-op (currently
   `guest_app_shell.c:753-844`, a zeroed `guest_malloc(0x2000)` blob installed
   at `app+0x440`). Keep it only until step 4 is proven; then unset it so the
   genuine store at `0x24aea9c` is the sole writer and is observable.
8. **Diagnostics before/after** at every step: `0x72cb648`, `0x72cb5e0`, gate
   bytes `0x77979c0/0x77979b8/0x7797990/0x7797968`, and `app+0x440`. Success
   is `app+0x440` non-NULL with `*(void**)controller` a genuine guest RVA
   (the wrapper is `0x6e02568` from `0x24aea8e`), no fault at `0x24ad562`, and
   no fault at `0x2772403`. The next expected fault (if any) is a separate
   blocker and must be reported honestly.
9. **Marker** — all of the above stay in the diagnostic forced-withhold set:
   add the new `SOBER_APP_SINGLETON_*` / `SOBER_JSON_APP_SINGLETON` knobs to the
   `seeded_state` OR-chain (`guest_app_shell.c:1336-1340`). Zero
   `app shell rendered` emissions under any of them (decision 013).

### Prohibitions (unchanged)

* No host function in a vtable slot; no `sober_noop_virtual`-style no-op object.
* No byte patch / mprotect / trampoline at any installer or vtable address.
* No fabricated callback; the genuine `onAppBridgeNotification` remains
  diagnostic only. No decision-013 marker while any knob is set.

---

## 6. Falsifiers

* If, with flag=1 and `0x72cb5e0` non-NULL, `0x24ad562` still faults, then a
  second gate inside `0x30ef840` (the predicate at `0x30ef844`, or the virtual
  target on the singleton's vtable) is the real blocker — re-read that path,
  not the delegate.
* If the slot `+0x30` dispatch is not observed (no FP frame `0x23389fa`), then
  the `0xe0`/`0x6dbc7c8` object was not registered by `0x24abf96` — fix the
  `InitWithParams` JNI getters, not the singleton.
* If `0x24aea9c` executes but `app+0x440` is immediately overwritten, audit the
  `flag==0` sibling writer at `0x24aebc0` and the fake seed.

---

## 7. Evidence index

* Frozen-engine disassembly (`/tmp/opencode/dis.sh`): `0x24ac57c-0x24ac66a`,
  `0x24ad3ee-0x24ad440`, `0x24ad50a-0x24ad6c8`, `0x24ae02b-0x24ae11e`,
  `0x24ae1fe-0x24aec18` (store `0x24aea9c`, `0x24aebc0`), `0x30ef840-0x30ef87c`,
  `0x2384870-0x23849ad` (store `0x238497d`), `0x6096928-0x609699d`,
  `0x23389eb-0x2338a15`, `0x2eb6b2a-0x2eb6bb0`, `0x2ebb576-0x2ebb587`.
* APS2 relocation decode: `python3 /tmp/opencode/reloc.py dump 0x6dbc7c8 12`;
  `slots 0x24ad50a` → `0x6dbc7f8`; `slots 0x24ae1fe` → `0x6e01ce0`.
* RUNTIME logs: `realstate2.log:3683,3696,3707,3713,3727,3730-3733` (dispatch
  FP chain + NULL-singleton fault), `phase8-all.log:3690-3712` (flag=1 without
  ctor replay → fault `0x2e5d42a`), `clean-tsd.log:3684,3694,3701,3708-3709`
  (ctor replay, flag bridge failed, fake controller), global grep showing no
  singleton population in any log.
* Harness source (READ ONLY): `src/linux/guest_app_shell.c:645-706, 753-844,
  955-1007, 1336-1340`; `src/jni/app_singleton_bridge.h:30-58, 78-100, 104-129`;
  `src/jni/jni_json_bridge.c:242-281`; `src/jni/jni_native_objects.c:77-87`.
* Prior work: `analysis/stability-fault-audit.md` §2-§4 (fault chain #1/#2/#3),
  `analysis/A08-surface-delegate.md` (delegate at `app+0x408`, separate gate),
  `analysis/appstate_fields.md`, `docs/decisions/013-no-weak-app-shell-marker.md`.
