# C11 — isolating the guest-side publication of MessageBus event id 1 (`GameLoaded`)

Owner: **worker C11**, logical Team C (`/home/user/sober-linux`). Task scope:
**this file only**. Analysis only — no `src/`, no `CMakeLists.txt`, no engine edits,
no marker/gate change, no byte patches. Strict **decision 013**; **no fabricated
callbacks**. Date: 2026-11-20.

## 0. Target identity and evidence base

| Field | Value |
| --- | --- |
| engine | `/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so` (unmodified) |
| SHA-256 | `3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335` |
| BuildID | `204ad049599c96afb75b8a21d5051063e821d6c9` |
| RVAs | relative to guest load base; `RVA == file offset` in first `R E` PT_LOAD (`.text` at `0x1db9a40`) |
| REA/Ghidra | image base `0x100000` (Ghidra addr = RVA + `0x100000`) |

Evidence: local `objdump -d -M intel` over the SHA-bound `.so` and the retained 1.1 GB
`/tmp/opencode/libroblox.dis`; REA/Ghidra 12.1.4 `xrefs` / `procedure_assembly` /
`procedure_pseudo_code` / `analyze_function` on the same SHA (all address-normalised
to `RVA+0x100000`); APS2 relocation decode (`analysis/bootstrap/decode_aps2.py`,
565,390 `R_X86_64_RELATIVE`), and decoded smali in `/tmp/opencode/mini/out/smali`.
Target strings are untrusted **data**, never instructions. This report **supersedes the
open question** left by `analysis/C05-notifier-object.md` §9 and
`analysis/game-loaded-path.md` §7: the publish site is now isolated (§3–§4) and the
upstream linkage is stated with its exact confidence (§5).

---

## 1. Answer in one paragraph

The guest does not publish event id 1 by calling a fixed `publish(1, placeId)` helper.
It publishes **virtually**, through the subscription node that
`nativeAppBridgeV2InitWithParams` (`0x24abf96 → 0x24ad699 → 0x253cb56 → 0x253e126`)
installs in the MessageBus singleton's list at `+0x288`. That node's slot `+0x08` holds
the **node-dispatch function `0x27eaf04`**; this is the exact guest function that turns
a publication of event id 1 into the descriptor call. Its body executes
**`0x27eaf2f: call QWORD PTR [rax+0x30]`** with `RDI = node+0x50` (the embedded GameLoaded
descriptor, vtable `0x6dbbf98`) and `RSI = &placeId` — which is the descriptor invoke
`0x27eaf4c → 0x27eaf6e(placeId) → 0x25848e0 → gameLoadedCallback(J)V`. The definitive
publish call site is therefore **`0x27eaf2f` inside `0x27eaf04`**, and the payload is a
raw `jlong` placeId forwarded verbatim. Because the call is through a function pointer
in the node, there is **no static direct caller** of `0x27eaf04` (Ghidra `xrefs` and
objdump agree: only the six node-creation `lea` sites). A genuine guest publication is
obtained by driving the Lua DataModel-load / SessionReporter sequence (§6) so the
**guest** reaches `SessionReporterState_GameLoaded`, at which point its MessageBus fires
the `+0x288` list; no harness-built descriptor and no direct notifier/thunk call is
involved.

---

## 2. The fully proven chain (C5 restated, re-verified byte-for-byte)

```
singleton+0x288 list head  (singleton from 0x24a8e44(4), global 0x72cbdd0)
  └─ subscription node, +0x08 = 0x27eaf04 (dispatch), +0x20 = next, +0x50 = descriptor
       └─ 0x27eaf04  (node dispatch)  <== THE PUBLISH FUNCTION (§3)
            └─ 0x27eaf2f: call [desc.vptr + 0x30] = 0x27eaf4c
                 └─ 0x27eaf4c (descriptor invoke)  [C05 §5]
                      └─ 0x7234520 → 0x6dbd860 → [0x6dbd860] = 0x27eaf6e
                           └─ 0x27eaf6e (notifier), RSI = placeId
                                └─ 0x27eafae: call 0x25848e0
                                     └─ JNI CallStaticVoidMethodV → gameLoadedCallback(J)V
                                        (class 0x72a2f48, method 0x72a2fc0)
                                └─ 0x27eafb3: log "onGameLoaded() SessionReporterState_GameLoaded placeId:%lld" (0x36c7c9)
```

Registration (all observed):

```
24ad699: lea  rsi,[0x7234520]; mov edi,1; call 0x253cb56     ; enable lifecycle family
253d06e: lea  rcx,[0x6dbbf98]                                 ; vptr (GameLoaded desc)
253d07f: mov  QWORD [r14+0x8],1                               ; event_id = 1
253d087: mov  QWORD [r14+0x10],0                              ; table_delta = 0
253d08f: mov  [r14+0x18],rbx                                  ; rbx = &0x7234520
253d093: mov  [r14+0x20],r14                                  ; self
253d09d: call 0x253e126                                       ; register into singleton+0x288
253e142: lea  rsi,[rdi+0x288]                                 ; subscription LIST HEAD
253e180 → 253e21e → 226c048                                  ; alloc node, memcpy desc to node+0x30
253e235: lea  rax,[0x27eaf04]; mov [rdi+0x8],rax              ; node +0x08 = dispatch fn ptr
```

Invoke / notifier (verbatim, SHA-bound):

```
27eaf4c: mov  rax,rdi                 ; descriptor (this)
27eaf4f: mov  rcx,[rdi+0x8]           ; event_id = 1
27eaf53: mov  rdi,[rdi+0x18]          ; &0x7234520
27eaf57: mov  rsi,[rsi]               ; placeId (jlong) from message
27eaf5a: add  rdi,[rax+0x10]          ; + delta(0)
27eaf5e: test cl,0x1                  ; table-indirect flag (id low bit)
27eaf63: mov  rax,[rdi]               ; [0x7234520] = 0x6dbd860
27eaf66: mov  rcx,[rax+rcx-0x1]       ; [0x6dbd860 + 1 - 1] = 0x27eaf6e
27eaf6b: jmp  rcx                     ; tail-call notifier, RSI = placeId
```

APS2 uniqueness (unchanged from C05 §2): `who_points_to(0x27eaf6e)` → only
`off=0x6dbd860`; `who_points_to(0x6dbd860)` → only `off=0x7234520`;
`who_points_to(0x27eaf4c)` → only `off=0x6dbbfc8`.

---

## 3. Isolating the publish function: `0x27eaf04` and the call at `0x27eaf2f`

`0x27eaf04` is the **subscription-node dispatch** written into every node's `+0x08`
slot (registration `0x253e235`; four more node builders at `0x288c2d4`,
`0x288ceb8`, `0x3100061`, `0x3df87fd`, `0x560e7c1` — same family). Full body:

```
27eaf04: push rbp; mov rbp,rsp; push rbx; sub rsp,0x18
27eaf0d: mov  rbx,[0x72293f8]        ; canary
27eaf14: mov  rax,[rbx]; mov [rbp-0x10],rax
27eaf1b: mov  [rbp-0x18],rsi         ; SAVE placeId (arg 2, jlong)
27eaf1f: mov  rdi,[rdi+0x50]         ; RDI = node+0x50 = embedded descriptor (this)
27eaf23: test rdi,rdi
27eaf26: je   27eaf42                ; null → error 0x2eb2992
27eaf28: mov  rax,[rdi]              ; rax = descriptor vptr = 0x6dbbf98
27eaf2b: lea  rsi,[rbp-0x18]         ; RSI = &placeId slot
27eaf2f: call QWORD PTR [rax+0x30]   ; *** THE PUBLISH CALL ***
                                     ;   = descriptor invoke 0x27eaf4c
                                     ;   RDI = descriptor, RSI = &placeId
27eaf32: ... canary check; ret
```

**Arguments of the publish call (`0x27eaf2f`), all observed:**

| Reg | Value | Meaning |
| --- | --- | --- |
| `RDI` | `[node+0x50]` | the embedded 0x28-byte GameLoaded descriptor; `+0x00 = 0x6dbbf98`, `+0x08 = 1`, `+0x18 = &0x7234520` |
| `RSI` | `&[rbp-0x18]` | address of the saved `jlong` placeId (the invoke does `mov (%rsi),%rsi`) |
| call target | `[0x6dbbf98+0x30]` | `0x27eaf4c` (APS2 `off=0x6dbbfc8 → 0x27eaf4c`) |

This is the **exact** guest function/call that produces a genuine MessageBus event-id-1
publication: it is the only site in the image that calls the id=1 descriptor's virtual
invoke with the placeId message. Reaching `0x27eaf2f` ⇒ reaching `0x27eaf4c` ⇒
`0x27eaf6e`. Honest boundary: I could not attribute a *single static caller* to
`0x27eaf04`, because the MessageBus invokes it **indirectly** through the node's `+0x08`
function pointer (see §4). That is a property of the guest's virtual dispatch, not a
gap in the chain.

---

## 4. Why the publication has no direct caller — the `+0x288` subscription list

* The MessageBus singleton is a **plain struct**, not a polymorphic object: its
  constructor `0x30f9aae` only zeroes fields and `memset`s the region at `+0x288`
  (`0x211` bytes) — no vptr is written. There is therefore no `messageBus->publish()`
  virtual method to name.
* Subscription is **list-based**: `0x253e126` passes `&singleton+0x288` as the list head
  to `0x253e180`, which allocates a 0x60-byte node (`0x1dbab02`), fills it
  (`0x253e21e`, descriptor memcpy at `+0x30`), and links it with the locked intrusive
  insert `0x231772c`. Node `+0x20` is `next`; node `+0x08` is the dispatch function
  pointer (`0x27eaf04` for this family).
* Publication is the mirror operation: the MessageBus walks the `+0x288` list and calls
  each node's `+0x08` dispatch — i.e. `0x27eaf04`. Exhaustive checks:

  - `xrefs(0x27eaf04)` (Ghidra) → only the six **node-creation** `lea` sites
    (`0x253e235`, `0x288c2d4`, `0x288ceb8`, `0x3100061`, `0x3df87fd`, `0x560e7c1`); no call.
  - `xrefs(0x27eaf6e)` / `xrefs(0x27eaf4c)` → empty (code) / only the reloc slot.
  - APS2 `who_points_to` for the whole `0x7234520 → 0x6dbd860 → 0x27eaf6e` chain is unique.

  This is a **negative result with exact scope**: the notifier and invoke are reachable
  *only* by virtual dispatch from a subscription node — consistent with C05 §2 and with
  the decision-013 rule that a direct `call 0x25848e0` is disqualified.

* Event-id encoding (C05 §2, re-verified): ids are `8k+1`; the low bit is the
  "table-indirect" flag tested at `0x27eaf5e`, and `id-1` is the byte offset into the
  notifier table `0x6dbd860`. `id==1` ⇒ entry `[0x6dbd860] == 0x27eaf6e`. The
  `+0x288` list is the id=1 family (siblings: id `0x79 → +0x2e0`, `0x81 → +0x2e8`,
  `0x91 → +0x378`, all registered by the same `0x253cb56` block).

---

## 5. Upstream: SessionReporter / Lua DataModel load and the placeId source

The log string `onGameLoaded() SessionReporterState_GameLoaded placeId:%lld` has exactly
**one** textual xref in the whole image — the notifier `0x27eafbf` (C5/C9). So the
state whose transition fires event id 1 is, by the engine's own label,
`SessionReporterState_GameLoaded`. The guest-side prerequisites and the placeId source
are (all observed RVAs):

| Link | Evidence | Confidence |
| --- | --- | --- |
| descriptor family registered at init | `0x24ad699 → 0x253cb56(1,&0x7234520)`; guard `0x72a31ba` initially 0 | proven |
| placeId carried from StartGameParams into the game-loaded report | `nativeAppBridgeV2StartGameWithParam 0x2ebd51a` reads the 6 getters (`gameId`, `accessCode`, `username`, `userId`, `isUnder13`, `placeId`); `0x2ee4838`/`0x2ee0a04` log `dataModelBindings_onGameLoaded: placeId = {}.` (`0x4357a3`) / `onGameLoaded: placeId:{}.` (`0x56d327`) with the placeId in `RSI` | proven (payload plumbing) |
| Lua DataModel finish → `SessionReporterState_GameLoaded` → MessageBus fires `+0x288` list → `0x27eaf04` | RTTI `onGameLoaded…DataModel` (`0x6881cc`) + `gameLoadedSignal` (`0x2c3165`) show a DataModel-loaded lambda; the state→list edge itself is only reachable through virtual dispatch | strongly supported, **not** a static direct-call proof (same limit as C05 §9) |

Two related-but-distinct Java callbacks exist and must not be conflated under 013:

1. **MessageBus path (this task):** `0x27eaf6e → 0x25848e0 →` `NativeGLJavaInterface.gameLoadedCallback(J)V`,
   class global `0x72a2f48`, method global `0x72a2fc0`. This is the only callback that
   proves event-id-1 publication.
2. **SessionReporterJavaInterface path:** `0x2eeb32e → 0x2eeb1d0 →`
   `com/roblox/engine/jni/reporter/SessionReporterJavaInterface.gameActivity_onGameLoaded(J)V`,
   class global `0x72a3b88`, name `0x453577`, sig `(J)V` (`0x433046`). This is a separate
   native→Java notification (log `0x36cabb`, `gameActivity_onGameLoaded`), **not** the
   MessageBus notifier and **not** accepted as the decision-013 `gameLoadedCallback`.

---

## 6. Minimal opt-in sequence for a genuine guest-side publication

All calls are real JNI exports (RVAs/signatures from `analysis/game-loaded-path.md` §3,
`C07-event-order.md` §1, decoded smali). Do **not** call `0x25848e0`, do **not** set
`SOBER_ALT_CALLBACK_PATH`, do **not** build a descriptor. Run the existing host bridge so
that the guest itself drives the `+0x288` list:

| # | RVA | JNI export / signature | Role |
| - | - | - | - |
| 1 | `0x2edd859` + `0x2edd739` | `nativePreloadFlagOverrides(String)V` (JSON object `{"EnableAppBridgeComponents":true}`) + `nativeRetryInit()V` | set flag byte `0x72cb648 = 1` (JSON-object grammar required) |
| 2 | `0x233762c` | `nativeGameGlobalInit()V` | engine globals |
| 3 | `0x2383c18` | `nativePostClientSettingsLoadedInitialization3(List)V` | construct app singleton `0x72cb5e0` |
| 4 | `0x24abf96` | `nativeAppBridgeV2InitWithParams(InitParams)V` | **registers** the id=1 descriptor at `0x24ad699` → `+0x288` list |
| 5 | `0x247dc8d` / `0x2ebf950` | `nativeAppBridgeAppStart(String,String,Z,String,String,String)V` / `nativeScheduleOnFirstFrame(Runnable)V` | app start / first-frame |
| 6 | `0x25448f6` | `nativeAppBridgeStartLuaAppDM()V` | guest writes app delegate `app+0x408`; Lua-app start |
| 7 | `0x26fe80f` | `nativeAppBridgeV2StartAppWithParams(StartAppParams)V` | app-shell start (ten getters) |
| 8 | `0x2ebd51a` | `nativeAppBridgeV2StartGameWithParam(StartGameParams)I` | join place; drives Lua DataModel load with a faithful `placeId > 0` → `SessionReporterState_GameLoaded` → MessageBus fires `+0x288` → `0x27eaf04` → `0x27eaf2f` → `0x27eaf4c` → `0x27eaf6e` |
| 9 | `0x2ebca9e` | `nativeAppBridgeV2SendAppEventOnGameLoaded(String,String,String)V` | Java-side app event (after manager setup; context only) |
| 10 | `0x2773194` | `nativeAppBridgeV2UpdateSurfaceAppWithPlatformParams(Surface,PlatformParams)V` | phase-3 render evidence `0x649d422` |

Guest-shaped **opt-in data** (already authored elsewhere, still required as upstream
fixes): `SOBER_APP488_FIX=1` (builds `app+0x488`, vptr `0x6e02918`) and app-singleton
construction — because `PROGRESS.md` Phase 9 records that step 8 currently faults at
guest `0x31a1360` (NULL `app+0x488`) before the DataModel reaches `GameLoaded`. A
nonzero `placeId` is required for the log discriminator too (`0x27eafb3` skips the
`0x36c7c9` log when `placeId == 0`). Keep all 17 decision-013 withhold knobs unset.

**Why this qualifies under 013:** the descriptor, the node, the list, the vtable and the
notifier are all guest-owned; the harness only supplies faithful opt-in object state and
calls the same JNI exports the real app calls. The callback is *initiated by the guest*
when its own state machine publishes event id 1 — it is not a host-built descriptor
(C05 Shape B) and not a direct notifier/thunk call (the disqualified ALT path).

---

## 7. Proof / acceptance criteria (decision 013)

A pass requires **all** of the following, observed live:

1. **Guest hit at the publish site:** breakpoint/hit-counter at `0x27eaf04` and
   `0x27eaf2f`, with `RDI = base + node+0x50` (descriptor, vptr `0x6dbbf98`, `+8 == 1`)
   and `[rbp-0x18] == placeId > 0`; step into `0x27eaf4c` with `RSI = placeId`.
2. **Notifier entered from the guest:** hit at `0x27eaf6e` with `RSI == placeId`,
   `[0x7234520] == base+0x6dbd860`, `[0x6dbd860] == base+0x27eaf6e`, and cached
   class/method globals `0x72a2f48`/`0x72a2fc0` non-NULL.
3. **Return-address discriminator:** thunk call at `0x27eafae` returns to
   **`0x27eafb3`** (guest), never into harness code; bridge log
   `guest→Java callback: gameLoadedCallback`, `last='gameLoadedCallback'`, `callbacks`
   incremented ≥1 over the pre-step-8 baseline.
4. **Log evidence:** `__android_log_print(3,"rbx.jni",0x36c7c9,placeId)` emits
   `onGameLoaded() SessionReporterState_GameLoaded placeId:<placeId>` (requires placeId>0).
5. **No disqualifiers:** no `ALT PATH guest call 0x25848e0`, no `HONESTY NOTICE`,
   `SOBER_ALT_CALLBACK_PATH` unset, no harness-built descriptor; engine SHA-256/BuildID
   unchanged. (The `SessionReporterJavaInterface.gameActivity_onGameLoaded` path in §5
   is a separate notification and is not a substitute for criteria 2–4.)
6. **Gate:** all 17 withhold knobs unset (`seeded_state == 0`) and phase-3 render
   `0x649d422` observed before `shell_cb && render_ok` may emit `app shell rendered`.

Reproduction commands for the static claims:

```
grep -n "27eaf04\|27eaf2f\|27eaf4c\|27eaf6e" /tmp/opencode/libroblox.dis
REA xrefs 0x28eaf04 / 0x28eaf4c / 0x28eaf6e   (Ghidra base 0x100000)
decode_aps2.py who_points_to(0x27eaf6e|0x6dbd860|0x27eaf4c)
```

---

## 8. Limits / honesty

* Static analysis only; no runtime capture of `0x27eaf04`/`0x27eaf6e` being entered was
  performed by this worker. The **publish call site** (`0x27eaf2f`) and its arguments are
  a direct disassembly fact; the claim that the MessageBus reaches it via the `+0x288`
  list (§4) is proven for the *registration* side and strongly supported but **not a
  static direct-call proof** for the *publication* side, because the call is virtual.
* `0x27eaf04` is shared by several subscription families (six node builders reference
  it); the id=1 identification rests on the descriptor's `event_id==1`, its vtable
  `0x6dbbf98`/invoke `0x27eaf4c`, and the table entry `[0x6dbd860]==0x27eaf6e`, not on a
  unique `0x27eaf04` xref.
* The `SessionReporterState_GameLoaded → +0x288 list` arrow (§5) is inferred from the
  log-string semantics, RTTI (`DataModel`/`gameLoadedSignal`), and the JNI call order;
  it is not a recovered direct call edge. Step 8's upstream object fixes are catalogued
  by other workers, not re-derived here.
* No gate change, no marker emission, no source/CMakeLists/engine modification is
  proposed. All 17 decision-013 withhold knobs remain authoritative. `onAppBridgeNotification`,
  the ALT thunk, and the `SessionReporterJavaInterface.gameActivity_onGameLoaded` path
  remain disqualified as decision-013 proof.
