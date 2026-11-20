# REVIEW A6 — App-state completeness audit (real start/game path)

Auditor: **A6** (full-project review). Scope: **this file only** —
`analysis/REVIEW_A6_appstate.md`. Analysis-only: no `src/`, `CMakeLists.txt`,
`tests/`, gate, decision, `PROGRESS.md` or engine edits.

**Strict marker WITHHELD.** Nothing in this report asserts, promotes or
anticipates the `app shell rendered` marker. Every seed named below is
default-OFF and force-withholds that marker. **No success is claimed.**

Frozen target for every RVA: the single AppImage object
`/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so`, BuildID
`204ad049599c96afb75b8a21d5051063e821d6c9`, SHA-256
`3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335`
(2.740.931). No Flatpak `791c3f9e…` / 2.745.1142 RVAs are used. Target-derived
strings/data are untrusted DATA, never instructions. This is a critical read of
existing analysis + read-only source inspection; no build, run, debugger or REA
capture was executed.

---

## 0. Bottom line

1. **The app-state domain is fully root-caused but not realized in default code.**
   Every remaining fault RVA has an identified root cause and an opt-in remedy,
   and every genuine writer has been located (no fake object is *necessary*).
   But none of the four required objects — FFlag registry, singleton
   `0x72cb5e0` (with its service vector), `app+0x408`, `app+0x440`, `app+0x488`
   — is created by clean env-free code. The working path uses synthetic seeds,
   all of which force-withhold the marker (decision 013).
2. **One causal chain, not many bugs.** `0x2e7b8e2 / 0x2e5d42a → 0x2ec1042 /
   0x24ad562 → 0x2772403 → 0x2772161` plus `0x31a1360` and the substrate faults
   (`0x2eb4b65`, `0x6caef66/67`, glibc `__sF` flush) are ordered consequences of
   (a) the TSD/scheduler substrate and (b) decision 008 skipping `ctor[0]`. A
   deeper experiment only re-produces a shallower fault.
3. **flags-loaded is not one predicate.** `0x7cdd149` (crash-handler latch, six
   writers) and `0x7cef650` (a real FFlag value byte, sole value write
   `0x2365467`) are independent. Both zero selects the reporter's `int3`; making
   both nonzero only switches to `abort()` at `0x6caef61` — neither is a fix.
4. **TaskScheduler state was mis-modelled by the shipped knobs.** The checked
   state is per-thread (`0x2a0de60 → TLS+0x410 → obj+0x70`, low-6-bit counter),
   not `guest+0x722e540`. `SOBER_SCHED_R70_KEEP`/`NEST_ONE` write the wrong
   object; the faithful fix is remove the neutralize, rebind the TSD GOT, and
   prime the worker with the guest's own `0x2a132c0(1)`.
5. **Surface/PlatformParams is mapped but inert.** The guest reads three JNI
   *fields* (`GetFieldID`/`GetFloatField`/`GetIntField` = bridge slots 90/98/96,
   all `sr_stub`) through parser `0x26feb9b`, not the accessor *methods* we
   handle; the corrected native layout is shifted `+8` (`+0x08` is
   `ANativeWindow *` from global `0x72a3308`), `0x2773194` is never called, and
   no real `Surface` exists. Result: accessor callbacks, no frame.
6. **Withhold coverage is broad but not complete.** The `seeded_state` OR-chain
   now has ~90 named knobs plus four prefix families (`SOBER_GAME_`,
   `SOBER_STARTAPP_`, `SOBER_SGV2_`, `SOBER_PLATFORM_PARAMS_`), covering all 47
   Tier-2 value modifiers found by D02; the dynamic `SOBER_PROP_<name>` family
   and the 10 Tier-3 guest-visible config seeds remain uncovered (needs lead
   ruling).

### Domain % (this audit's scope, weighted): **≈ 37 % complete** (band 30–40 %)

"Complete" = required state produced by clean env-free code, with partial credit
where a root cause is fully mapped and an opt-in *real-guest-code* remedy exists
and is observed to work. Root-cause coverage alone is ≈ 90 %; the realized,
env-free app state is ≈ 20 %, matching A13's `App state 0.20`. Composite
evidence for the marker is **0 %**.

| Sub-domain | Weight | Score | Basis |
| --- | --- | --- | --- |
| FFlag ctor/registry + flags-loaded latch | 15 % | 0.55 | `ctor[0]` root cause fully mapped; `SOBER_REPLAY_FFLAG_CTOR` verified (guard `0x72a0a30`=257, `handle+0x38` non-NULL). Latch fully characterized but has no faithful fix (and is not a fix target). Not env-free. |
| App singleton `0x72cb5e0` + service vector | 15 % | 0.20 | Sole writer `0x238497d` known; opt-in bridges exist but **no log ever shows singleton population**; replay still faults (`0x2e7b8e2`, `0x2e5d42a`). |
| `app+0x408` delegate | 12 % | 0.30 | Three writers, four guest vtables and both lazy helpers fully mapped; faithful construction specified; only the fake host-no-op seed is actually installed. |
| `app+0x440` controller | 12 % | 0.25 | Dispatch `0x23389f7`, installer `0x24ad50a`, store `0x24aea9c` fully mapped; blocked on singleton; fake seed bypass. |
| `app+0x488` inner object | 10 % | 0.35 | Writer `0x24af2e6 → 0x231c124`, vptr `0x6e02918` mapped; guest-shaped seeds exist; genuine path blocked. |
| TaskScheduler state | 12 % | 0.35 | Per-thread tagged-counter state fully decoded; faithful sequence specified; shipped knobs corrupt the wrong object and are withheld. |
| Surface / PlatformParams | 14 % | 0.25 | Field path, corrected `+8` layout, `0x72a3308` and dead `0x2773194` all mapped; field slots are `sr_stub`; no frame. |
| Opt-in seeds / withhold coverage | 10 % | 0.80 | OR-chain + prefix families cover Tier 1/2; residual Tier-3 + `SOBER_PROP_*` dynamic family (D02). |
| **Total** | **100 %** | — | **≈ 37 %**; defensible band 30–40 %. |

Reconciliation: A13 books this whole area at `0.20` with withhold coverage under
Gate/tests/docs. Credit for root-cause mapping and working opt-in remedies
raises the figure here to ~37 %; neither figure claims goal achievement.

---

## 1. Every remaining fault RVA — root cause and status

"Status" values: **DEFAULT** = reproduced on the clean env-free path;
**OPT-IN FIX** = root-caused, remedy exists as a default-OFF knob (withheld);
**LATENT** = only reachable under another seed / not currently hit;
**MAPPED-NOT-CALLED** = entry/state gap that is silent rather than a crash;
**HISTORICAL** = earlier manifestation, no longer current.

| # | Fault RVA | Sub-domain | Root cause | Status / remedy |
| --- | --- | --- | --- | --- |
| 1 | `0x2eb4b65` | substrate / JNI | Worker dereferences the unseeded `JNI_OnLoad` JavaVM* global `0x7cdec78` (helper `0x227b9e2`, caller `0x2eb4b65`); `JNI_OnLoad` is skipped. | **DEFAULT** baseline. Opt-in `SOBER_SEED_JNI_VM`+`SOBER_FAITHFUL_JAVAVM` clear it but expose `0x2e5d42a`; withheld. |
| 2 | `0x6caef66` / reported `0x6caef67` (`int3`) | TaskScheduler / latch | `257f718-Context,20` scheduler nesting underflow: TSD key-3 namespace mismatch (`pthread_getspecific` GOT `0x72298b0` → glibc) + neutralize `memset(p+0x28,0,16)` at `0x722e570` clobbers the second TLS descriptor. Both reporter bytes `0x7cdd149`/`0x7cef650` are 0. | **OPT-IN FIX**: `SOBER_BIONIC_TSD_GOT`, `SOBER_NO_SCHED_NEUTRAL`, heap `SOBER_TLS_SEED`, plus guest `0x2a132c0(1)` on the worker. Making the two bytes nonzero only yields `abort()` at `0x6caef61` — not a fix. Withheld. |
| 3 | `realloc(): invalid pointer` SIGABRT (FP `0x2ea6495→0x2a0de70→0x2a13399`) | TaskScheduler / TSD | `SOBER_STUB_TSD_SEED` used a static BSS buffer that the guest's grow path `0x2ea6440 → 0x227aafa → host realloc()` reallocates. | **OPT-IN FIX**: `calloc`'d heap backing (`native_jni.c:1226-1249`). Withheld. |
| 4 | `libc+0x7c8ff` (`_IO_fflush`, `si_addr=0x8`) | FFlag/logger stdio | Guest logger `0x2e61ca0` flushes `FILE*` derived as `__sF+0x98/+0x130`; harness `__sF` is 0x40 (or 0x18) bytes, not the required 0x198, so `_lock` at `FILE+0x88` is NULL. | **OPT-IN FIX**: `SOBER_BIONIC_SF_FULL` (resize + redirect the two streams). Withheld. |
| 5 | `0x2e7b8e2` (`lock incq 0x8(%rax)`, `rax=0`, `si_addr=0x8`) | FFlag registry | `*(0x72a0520+0x38)` shared_ptr control block is NULL: decision 008 skips `ctor[0]=0x2e62390 → 0x2e618b0 → 0x2e61280 → 0x2e5bc10 → 0x2e7b850`, the sole installer. | **OPT-IN FIX**: `SOBER_REPLAY_FFLAG_CTOR` + wiring order (`SOBER_JSON_APP_SINGLETON` block moved *after* ctor replay). Withheld. |
| 6 | `0x2e5d42a` (`mov -0x18(%rax),%rax`, `rax=0`) | FFlag registry / autovalue | NULL backing of the FFlag registry global `0x72a0520` (`0x2e5d970 → 0x2e5d420`); same skipped `ctor[0]`. Also reproduced when `SOBER_REAL_APPSTATE_POSTSETTINGS` replays postsettings with a plain zeroed `java/util/List` (guest treats the jobject as a native C++ collection). | **OPT-IN FIX**: same as #5; for the List path use `SOBER_NATIVE_OBJECTS` guest-vptr `0x6db3ec0+0x10` shape with guest-allocated `+0x30`/`+0x40`. Withheld. |
| 7 | `0x2ec1042` (`mov rcx,[rax]`, `rax=0`, `si_addr=0x0`) | singleton + service vector | App/service singleton `0x72cb5e0` NULL; `0x30ef840` returns 0. The singleton carries a service vector at `+0x48/+0x50` (one app-bridge descriptor, key at `+0x68`, service size `0x58`); it is empty. | **OPT-IN FIX**: `SOBER_APP_SINGLETON_BRIDGE` / `SOBER_JSON_APP_SINGLETON` + `SOBER_NATIVE_OBJECTS` → real `0x2383c18 → 0x238497d`; or `SOBER_SINGLETON_SERVICES`. Never observed successful in any log. Withheld. |
| 8 | `0x24ad562` (`mov (%rax),%rcx`, `rax=0`) | `app+0x440` controller | Same singleton gap in the controller installer `0x24ad50a` (vtable slot `+0x30` of `0x6dbc7c8` at `0x6dbc7f8`, dispatched by `call *0x30` at `0x23389f7`): `0x30ef840` returns 0 and `0x24ae1fe`/store `0x24aea9c` never run. | **OPT-IN FIX**: #7 + flag `0x72cb648`=1 via guest setter `0x23648be` + `InitWithParams 0x24abf96` building `this+0xb0`. Withheld. |
| 9 | `0x2772403` (`cmpl $0,0x140(%r14)`, `r14=0`) | `app+0x440` controller | `SingleSurfaceApp::userDidLogin 0x2771f6c` passes NULL `app+0x440` to callee `0x2772384` (called at `0x2772129`); genuine controller absent. | **OPT-IN FIX**: genuine installer store `0x24aea9c`; currently bypassed by fake `SOBER_SEED_USER_CONTROLLER` (zeroed `guest_malloc(0x2000)`). Withheld. |
| 10 | `0x2772161` / `0x320581b` (`mov (%rdi),%rax; call *0x90(%rax)` on NULL) | `app+0x408` delegate | Delegate at `app+0x408` NULL; `nativeAppBridgeStartLuaAppDM 0x25448f6` (the Java entry that installs it) is never called by the harness. | **OPT-IN FIX**: fake `SOBER_SEED_APP_DELEGATE` (host no-op vtable — disqualified) or faithful `SOBER_FAITHFUL_APP_DELEGATE` via guest helpers `0x1dd55f2`/`0x25f7ff2` (vtables `0x7220868`/`0x6dc3840`). Withheld. |
| 11 | `0x31a1360` (`mov 0x28(%rbx),%rax`, `rbx=0`, `si_addr=0x28`; `rax=0xabcdef…` is the frame canary from GOT `0x72293f8`, **incidental**) | `app+0x488` inner | `app+0x488` NULL, loaded by caller `0x320b0ba: mov 0x488(%rbx),%rdi`; FP `0x320b0ba→0x320b0e7→0x31a1168`. Genuine object built in `InitWithParams`: `0x24af28a/0x24af28f` (vptr `0x6e02918`), `0x24af2cc/0x24af2e6 → 0x231c124`. | **OPT-IN FIX**: `SOBER_SEED_APP_488` / `SOBER_APP488_FIX` / `SOBER_APP488_INNER` guest-shaped object; canary must not be "fixed" and guard must not be widened. Withheld. |
| 12 | `0x649cf9a` (bogus pbuffer deref) | surface / render | Route A passes `extra=1` as an EGL pbuffer pointer to guest `0x649c21e → 0x649cf72`. | **OPT-IN / render domain (A4)**: call `0x649c21e(window, pbuffer=0)`; Route A disabled by default. Withheld. |
| 13 | `0x25d7f55` (write through delegate slot `+0x10` return) | `app+0x408` delegate | **LATENT**: only taken if crash latch `0x7cdd149` is set; the fake delegate's `sober_noop_virtual` returns 0 and the guest writes through that pointer. | **LATENT**; cleared by a genuine delegate (A8 addendum). Do not set the latch to "test" it. |
| 14 | `0x2456079` (clean-HEAD `b8bdab0`) | InitWithParams / JNI | Earlier manifestation of the same app-state gap inside `InitWithParams` before the six-arg real `AppStart` was reachable. | **HISTORICAL**; phase-7 edits let execution advance to `0x2eb4b65`. Not a current fault. |
| 15 | `0x23331ae` / `0x233263f` / `0x245605d` garbage-pointer faults | JNI handle fidelity (cross-cutting) | Bogus `+0x68` handle / stale return address in `F_inner`; guest validator `0x245605d`. | **OPT-IN / B10**: fix pointer provenance at `0x2333164` (canonical getter `0x231f294`), not by byte patching the validator (`0x23335c0` etc.). Withheld. |
| 16 | `0x2773194` never called; `0x26feb9b` field slots 90/96/98 `sr_stub`; `0x72a3308` NULL | surface / PlatformParams | **MAPPED-NOT-CALLED**. Guest reads `dpiScale:F`, `viewportWidthMm:I`, `viewportHeightMm:I` via JNI *fields*; our bridge handles only the accessor *methods*. No real `Surface` → no `ANativeWindow` → no frame. | **OPT-IN FIX**: `SOBER_PLATFORM_PARAMS_FIELDS` (+ `SOBER_STARTAPP_PARAMS`/`SOBER_NATIVE_OBJECTS`), replay `0x2773194` after `StartAppWithParams`. Withheld; even a frame from this path is not app-shell proof. |

Non-fault mode-byte note: reporter `0x6caef4f–0x6caef66` — with
`0x7cdd149 != 0 && 0x7cef650 != 0` it calls `abort()` at `0x6caef61`; every arm
crashes. Neither byte is a recovery state.

---

## 2. Findings by scope area

### 2.1 FFlag ctor / registry
`ctor[0]=0x2e62390` is the **sole** initializer of the FFlag provider registry
`0x72a0520` (`0x2e623a0 → 0x2e618b0 → 0x2e61280`; guard `0x72a0a30`), and
decision 008 skips it (canonical replay ranges `[2,3)+[5,3618)`). The missing
word is `*(0x72a0520+0x38)` (control block), whose absence faults at `0x2e7b8e2`;
its NULL backing also faults at `0x2e5d42a`. `SOBER_REPLAY_FFLAG_CTOR` is wired
(`guest_app_shell.c:995`) and verified (guard 257, registry vptr `0x6db4008`).
A narrow guest-code fallback is `0x2e7b850` with `rdi=0x72a0520+0x38` — never a
raw sentinel (`0x72a0b88`) write. Skipped ctors 1/3/4 are **irrelevant**: ctor[1]
is lazily reached from ctor[6] (`0x1dbef4a`), ctor[3] is a DFLog-only mimalloc
probe, ctor[4] is lazily self-initializing from `0x1ea4370` (A05).

The separate **native flag registry** `0x7cdd118` has a single writer,
`0x227c092` inside JNI_OnLoad helper `0x227be14`; with JNI_OnLoad skipped it
stays NULL, so `0x227c18e` short-circuits at `0x227c247` and no flag value is
delivered. `SOBER_NATIVE_FLAGS_INIT` (`0x2465210`) with a synthetic empty
array only feeds local overrides (`flagCount = 0`) and does not write a flag
byte (A03).

### 2.2 Flags-loaded latch
Two unrelated bytes, both zero in the harness:
- `0x7cef650` — FFlag `DebugForceTerminateOnRbxCrash` value byte, defaulted to 0
  by replayed `ctor[3602]=0x2279a47`; sole *value* write is `0x2365467` via
  thunk `0x227d511 → 0x227c18e → descriptor vtable 0x236534c (+0x28)`, which
  needs a non-NULL registry `0x7cdd118` and a real Java flag source answering
  true.
- `0x7cdd149` — **crash-handler install latch, not flag state**, with exactly six
  `movb $1` writers (`0x2eb7520`, `0x2ec9fae`, `0x56d31a3`, `0x56d3386`,
  `0x56d6a4b`, `0x56d6b80`) and ~2,270 readers. Companion `0x7cdd148` needs
  `0x7cdd0d8` (`ShadowFValuesEnabled`) fetched true.

Both-zero selects `int3`; both-nonzero selects `abort()`. A faithful sequence
(registry → Java flag source → thunk → crash-handler install) exists (A03 §4)
but is diagnostic only, and supplying it without a real Java source would be
synthetic state that force-withholds. **Status: not fixed; not a legitimate fix
target.**

### 2.3 App singleton `0x72cb5e0` + service vector
Sole writer: `mov %r14,0x72cb5e0` at `0x238497d` inside
`nativePostClientSettingsLoadedInitialization3 0x2383c18`, gated by flag
`0x72cb648==1` (`0x2384870`) and GL predicate `0x6096928` (`0x238491e`, gate
byte `0x77979c0`). The 0xa8-byte object carries a service vector at `+0x48`
(begin) / `+0x50` (end) with one app-bridge descriptor (key `+0x68`, size 0x58);
`0x30ef840` reads it, and its NULL return is the cause of both `0x2ec1042` and
`0x24ad562`. Genuine path: `SOBER_REPLAY_FFLAG_CTOR` → flag via
`SOBER_JSON_FLAG_BRIDGE`/`0x23648be` → `SOBER_APP_SINGLETON_BRIDGE` (replay
`0x2383c18`, ctor `0x608f85a`, guest alloc `0x1dbab02`) or
`SOBER_JSON_APP_SINGLETON`+`SOBER_NATIVE_OBJECTS`, or `SOBER_SINGLETON_SERVICES`
(singleton publish mirroring `0x238497d`). Hardening: `sober_native_java_list()`
must guest-allocate `+0x30` control block and `+0x40` array (one host `calloc`
pointer serving both is misread by destructor `0x2e5baa0`/retain `0x2e7b8d0`).
**No log in the tree ever shows this state created** (A13 §4; A10 Rank 3).

### 2.4 `app+0x408` delegate
Pointer field, not an inline object. Three genuine writers:
`0x24aed3e` (InitWithParams factory `0x2520892`/ctor `0x252095e`, object vptr
**`0x6e04ec0`**; `0x6e047b0` is only a type/factory tag), `0x25d7e75` (helper
`0x1dd55f2` → `0x7d9d100`, vptr `0x7220868`) and `0x25f7e7e` (helper
`0x25f7ff2` → `0x72a5a88`, vptr `0x6dc3840`, intrusive-refcounted). Consumers
call slots `+0x10`, `+0x38`, `+0x90`. The installed `SOBER_SEED_APP_DELEGATE` is
a 0x100-byte host no-op-vtable fake — exactly what decision 013 forbids; it
must be **removed**, not extended. Faithful path: call the guest helpers
(preferred site A, simplest ABI), publish with a single 8-byte store, add
`SOBER_FAITHFUL_APP_DELEGATE` to the withhold chain (A08).

### 2.5 `app+0x440` controller
`0x24ad50a` is vtable slot `+0x30` of class `0x6dbc7c8` (slot at `0x6dbc7f8`,
APS2 type-8), dispatched only by `call *0x30(%rax)` at `0x23389f7` inside
`nativeGameGlobalInit`. Flag-nonzero branch: `0x24ad547 → 0x30ef840 →
0x24ad5a3 (this+0xb0) → 0x24ad5b6 → 0x24ae1fe`, whose genuine store is
`0x24aea9c` (`mov %r14,0x440(%rbx)`), wrapper vptr `0x6e02568` at
`0x24aea8e`. `this+0xb0` is a guest shared_ptr built at
`0x24ac5a9/0x24ac5c8` in `InitWithParams`. All prerequisites (flag, singleton,
InitWithParams getters) must be real; the current `SOBER_SEED_USER_CONTROLLER`
must be unset once the genuine write is observed (A13; A10 Rank 4b).

### 2.6 `app+0x488` inner object
Built in `InitWithParams` (`0x24abf96`): `0x24af26a/0x24af271` (NULL check),
`0x24af2cc/0x24af2d7 → 0x3190869` (guest `alloc(0xa8)`) and `0x24af2e0 →
0x231c124` (move/store), or the reuse path `0x24af288/0x24af28f` (vptr
`0x6e02918`), `+0x08 = app`, `+0x20 = self`. A NULL `+0x488` produces
`0x31a1360` on the game path; the `0xabcdef0123456789` in `rax` is the
faulting function's own `__stack_chk_guard` canary from GOT `0x72293f8`, **not**
an object field, and must not be neutralized. Opt-in guest-shaped builders:
`SOBER_APP488_FIX`, `SOBER_APP488_INNER`, `SOBER_SEED_APP_488`; genuine path is
the `0x24af2e6` write. Withheld (A10 B4; sentinel trace; `app488_fix.c`).

### 2.7 TaskScheduler state
Correct state: `obj = *(TLS_block+0x410)` from `0x2a0de60` / `0x2ea6440` / Bionic
key `0x72a1b08`=3; `obj+0x70` is a tagged pointer (base `0x7282580`, low 6 bits
a nesting counter). The guest enter/leave guard `0x2a132c0` requires `c>=1` at
leave and `c<=0x3e` at enter; the worker's leave is `0x1e477e7 → 0x227dde2 →
0x227de0a → 0x2a132c0(0)` and panics at `0x2a13329` when `c==0`. `guest+0x722e540`
is a TLS-key descriptor, not the scheduler; `SOBER_SCHED_R70_KEEP`/`NEST_ONE`
and the neutralize `memset` corrupt descriptor #2 at `0x722e570`. Faithful
sequence: remove neutralize (`SOBER_NO_SCHED_NEUTRAL`), rebind GOT
`0x72298b0`/`0x72295c8` via `dlvsym(...,"LIBC")` (`SOBER_BIONIC_TSD_GOT`), keep
the heap TSD seed, and call `0x2a132c0(1)` on the `FunctionMarshaller` worker
just before `0x2a0daf7` with **no** matching harness leave (A06; TLS correction).

### 2.8 Surface / PlatformParams
`0x26feb9b` is a **JNI field reader**, not a native-layout writer: it fills a
0x34-byte scratch struct from `GetObjectClass` + `GetFieldID` + `GetFloatField`
(slot 98)/`GetIntField` (slot 96) for `dpiScale:F`, `viewportWidthMm:I`,
`viewportHeightMm:I`. Our bridge maps fn[90]/fn[96]/fn[98] to `sr_stub`, so the
scratch carries zeros. Callers `0x26fe98f/0x26fec51` (StartApp, vptr `0x6dbca20`)
and `0x277328f/0x2773354` (UpdateSurface, vptr `0x6dbcb20`) copy scratch
`+0x00..+0x30` into a 0x40-byte native object at `+0x08..+0x38`; hence native
`+0x08` is `ANativeWindow *` (global `0x72a3308`, written by `0x26fea5a`),
`+0x10` float, `+0x14/+0x18` ints, `+0x20`=1. `SOBER_STARTAPP_PLATFORM_DPI=0x08`
in `startapp_params.h` is the *scratch* offset — dormant while the guest uses
JNI fields, wrong if the backing is ever read as native. `0x2773194` is never
called by any harness path; Java `uh/f.I()` gates it on "app started". No real
`Surface`/`IGraphicBufferProducer` exists, so `ANativeWindow_fromSurface` yields
NULL. Opt-in `SOBER_PLATFORM_PARAMS_FIELDS` (A09) + `SOBER_STARTAPP_PARAMS` are
withheld and cannot by themselves open the gate.

### 2.9 All opt-in seeds — inventory and withhold status
Every knob below is default-OFF and appears in the `seeded_state` OR-chain
(`guest_app_shell.c:1449-1546`) unless noted; activating any forces
`marker withheld: diagnostic guest-state seed active` (`:1547-1550`).

| Domain | Seeds (representative) | Withheld? |
| --- | --- | --- |
| FFlag ctor/registry | `SOBER_REPLAY_FFLAG_CTOR`, `SOBER_NATIVE_FLAGS_INIT`, `SOBER_CTOR0/CTOR1/CTOR_RANGES` | Yes |
| Flags/JSON/flag bridge | `SOBER_JSON_FLAG_BRIDGE`, `SOBER_JSON_APP_SINGLETON`, `SOBER_JSON_VERBOSE`, `SOBER_REAL_APPSTATE_FLAG/POSTSETTINGS` | Enable names yes; `SOBER_JSON_VERBOSE` benign |
| Singleton + service vector | `SOBER_APP_SINGLETON_BRIDGE`, `SOBER_APP_SINGLETON_STRATEGY/RELEASE_OLD`, `SOBER_JSON_APP_SINGLETON`, `SOBER_SINGLETON_SERVICES*`, `SOBER_SINGLETON_FIX*` | Yes |
| `app+0x408` delegate | `SOBER_SEED_APP_DELEGATE` (fake — remove), `SOBER_FAITHFUL_APP_DELEGATE`, `SOBER_NATIVE_OBJECTS` | Yes |
| `app+0x440` controller | `SOBER_SEED_USER_CONTROLLER` (fake — remove) | Yes |
| `app+0x488` inner | `SOBER_SEED_APP_488`, `SOBER_APP488_FIX`, `SOBER_APP488_INNER` (+ `SOBER_A48I_*` modifiers) | Enable names yes; `SOBER_A48I_*` modifiers **not enumerated** (verify) |
| TaskScheduler/TSD | `SOBER_STUB_TSD_SEED`, `SOBER_TLS_SEED/TLS_BYTES`, `SOBER_BIONIC_TSD_GOT`, `SOBER_NO_SCHED_NEUTRAL`, `SOBER_SCHED_RVA`, `SOBER_SCHED_R70_KEEP`, `SOBER_SCHED_NEST_ONE` | Yes (legacy R70/NEST_ONE are hacks — should be removed) |
| JavaVM / strings | `SOBER_SEED_JNI_VM`, `SOBER_FAITHFUL_JAVAVM`, `SOBER_SEED_GLOBAL_STRING`, `SOBER_GSEED_FORCE/DIAG`, `SOBER_CANONICAL_JSTRINGS`, `SOBER_SEED_JNI_SINGLETON` | Yes |
| Stdio | `SOBER_BIONIC_SF_FULL` (+ `SOBER_SF_*` modifiers) | Enable yes; `SOBER_SF_*` modifiers not enumerated (verify) |
| Surface/params | `SOBER_STARTAPP_PARAMS/V2`, `SOBER_PLATFORM_PARAMS_FIELDS`, `SOBER_NATIVE_OBJECTS`, `SOBER_GAME_PARAMS`, `SOBER_STARTGAME_V2`, `SOBER_GAME_LOADED_PATH`, `SOBER_LUA_APP_START` | Yes; all value modifiers covered by four prefix families |
| Render | `SOBER_GUEST_ROUTE_A` (faults at `0x649cf9a`), `SOBER_RENDER_*` | Guest-mutating Route A yes; render backend selection benign |
| Gate diagnostics | `SOBER_ALT_CALLBACK_PATH` (diagnostic-only, decision 038), `SOBER_LOG_APP_BRIDGE_NOTIFICATION`, `SOBER_CALLBACK_PROVENANCE`, `SOBER_NOTIFIER_PROBE*` | Yes |

Residual withhold gaps (D02, snapshot 2026-11-20T21:44Z): 10 Tier-3
guest-visible config seeds (`SOBER_ASSET_ROOT`, `SOBER_SCREEN_WIDTH_DP`,
`SOBER_SCREEN_HEIGHT_DP`, `SOBER_SCREEN_SIZE`, `SOBER_NAV_HIDDEN`,
`SOBER_LANG`, `SOBER_COUNTRY`, `SOBER_WINDOW_WIDTH`, `SOBER_WINDOW_HEIGHT`,
`SOBER_GUEST_PATH`) and the dynamic `SOBER_PROP_<name>` family are outside the
chain. The 47 Tier-2 value modifiers are now covered by the four
`env_prefix_active` families. D09 checks only `guest_app_shell.c`, so its
"no gaps" result is file-local. These are gate-integrity gaps, not proof.

---

## 3. Dependency order (load-bearing)

A deeper step is uninterpretable while a shallower one still faults. Converged
order from A10 Ranks 1–6, B10, C10 and A13 BLK-01…09:

```
1  TSD / scheduler / JNI_OnLoad substrate    (0x2eb4b65, 0x6caef66/67, realloc abort, libc+0x7c8ff)
   └─ 2  FFlag ctor[0] replay + wiring order   (0x2e7b8e2, 0x2e5d42a)
        └─ 3  app singleton 0x72cb5e0 + service vector (0x2ec1042, 0x24ad562)
             ├─ 3' JNI typed fidelity + flag JSON grammar  [cross-cutting: 0x2edd859/0x2edd739, 0x23648be]
             └─ 4  app+0x488 / +0x440 / +0x408, then remove fake seeds
                    (0x31a1360 → 0x2772403 → 0x2772161/0x320581b)
                 └─ 5  surface + guest-driven render binding (0x2773194, 0x649d422, 0x649cf9a fix)
                      └─ 6  genuine gameLoadedCallback notifier (0x27eaf6e → 0x25848e0 → return 0x27eafb3)
                           └─ 7  gate provenance (notifier hit + RA 0x27eafb3, seeded_state==0, no ALT)
```

Hard ordering rules: global-string seed earliest (worker race with JNI_OnLoad
seeds); FFlag ctor replay before any postsettings/JNI flag traffic; JSON flag
bridge after ctor replay; singleton after flag=1; controller/delegate/inner
after singleton; all fake seeds removed and every opt-in unset before a gate
callback can qualify. ctor[3] (allocator probe) must run last if replayed at
all; ctors 1/3/4 are otherwise not required. Making the RBXCRASH mode bytes
nonzero, byte-patching a validator, or widening `sr_is_safe_ptr()` for the
canary are prohibited (decisions 013/014/038).

---

## 4. Honesty limits

- Static analysis of the frozen AppImage plus critical reading of existing
  analysis and read-only source inspection; no new runtime, REA or debugger
  evidence was produced. RVAs are AppImage-build only.
- "Root cause identified" means the cited reports locate a genuine guest writer
  or state invariant; it does **not** mean the state is produced by clean code.
  Several opt-in remedies were never observed to succeed (notably app-singleton
  population), and some are explicitly disqualified (host-no-op delegate,
  scheduler neutralize, mode-byte writes).
- Withhold-chain coverage is a source snapshot; D02/D09 line numbers and knob
  counts drift as the tree is edited — re-verify anchors before acting.
- Domain % is a planning judgement, not a measurement; the band 30–40 % reflects
  uncertainty in how much partial credit to give root-cause mapping vs realized
  env-free state.
- Under decisions 013/038 no qualifying `gameLoadedCallback(J)V` with
  `placeId!=0` / return `0x27eafb3`, guest-driven render and `seeded_state==0`
  has ever been observed. The marker remains **WITHHELD**; `tests/app-shell.sh`
  staying RED is the correct state. **No success is claimed.**
