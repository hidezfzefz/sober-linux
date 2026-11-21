# Task #524 — id=1 MessageBus GameLoaded node registration

Owner: **worker-1** (task #524). Scope owned here: `src/jni/publish_default.{h,c}`,
`src/jni/app_singleton_bridge.{h,c}` (read-only use), this file.
Frozen engine: `/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so`,
BuildID `204ad049599c96afb75b8a21d5051063e821d6c9`,
SHA-256 `3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335`
(re-verified on disk). Target bytes/strings are untrusted DATA.
Marker stays **WITHHELD**; decisions 013/038 binding. No guard write, no
byte patch, no fabricated node/descriptor/vtable/callback.

## 1. Verdict

The reported `NO_NODE` was a **host-side node-discovery defect**, not a missing
guest prerequisite and not a failed guest registration. The guest genuinely
links the id=1 subscription node, but `bus+0x288` does **not** point at the node:
it points at a **0x18-byte intrusive-list container**, and the real 0x60-byte
node hangs one level below it at `container+0x08`. Our
`sober_publish_default_find_node()` treated `bus+0x288` as the node, compared
`container+0x08` (a heap pointer) with the dispatch function `base+0x27eaf04`,
and walked `container+0x20` out of bounds — so it returned NULL for every
genuinely registered node. With the container indirection added (a read-only
extra dereference), the very same guest node is found, `publish` returns OK and
the notifier-driven `gameLoadedCallback` fires.

## 2. Frozen-engine facts (exact RVAs, `objdump -d -M intel`, frozen SHA)

### 2.1 Call site and registration preconditions
```
24ad699: lea rsi,[rip+0x4d86e80]  # 7234520   (&slot_global)
24ad6a0: mov edi,0x1
24ad6a5: call 253cb56                          (id=1 registration helper)
```
`nativeAppBridgeV2InitWithParams 0x24abf96` reaches this unconditionally: the
FFlag `0x72cb648` test at `0x24ad531` branches to `0x24ad5f2` (flag clear) or
falls through (flag set), and both paths converge on `0x24ad699`
(`0x24ad5ed jmp 24ad699`, `0x24ad696` falls through). No early `ret` is
jump-reachable from the main flow.

`0x253cb56` entry has exactly two guards before straight-line work:
```
253cb7c: test edi,edi ; je 253d1b7     (event id must be nonzero; we pass 1)
253cb84: cmp BYTE [0x72a31ba],0 ; jne 253d1b7   (one-shot guard)
253cb91: mov BYTE [0x72a31ba],1        (spent here, 13 bytes in)
```
A whole-image scan finds **only** these two references to `0x72a31ba` (one
read, one write) — no other guest or host code resets it. Guard `0` therefore
means the helper was not entered; guard `1` means it was.

### 2.2 The id=1 descriptor (13th of ~15 registrations)
```
253d064: mov edi,0x4
253d069: call 24a8e44              ; bus getter -> rax = base+0x72cbdd0
253d06e: lea rcx,[rip+0x487ef23]   # 6dbbf98   ; descriptor vptr
253d07c: mov [r14],rcx             ; +0x00 = vptr
253d07f: mov QWORD [r14+8],1       ; +0x08 = id 1
253d08f: mov [r14+0x18],rbx        ; +0x18 = slot global (arg2 = base+0x7234520)
253d093: mov [r14+0x20],r14        ; +0x20 = descriptor self
253d09d: call 253e126              ; insert into bus+0x288
```
This is exactly what `sober_publish_default_descriptor()` validates, so the
descriptor itself was never the problem. `0x24a8e44(4)` returns `base+0x72cbdd0`
directly when guards `0x72cc270`/`0x72cc718` are armed (they are, `1/1`), so the
bus getter cannot fail.

### 2.3 THE ROOT CAUSE — the missed container indirection
```
253e126: lea rsi,[rdi+0x288]       ; rdi = bus; rsi = ADDRESS of the +0x288 slot
253e150: call 253e180              ; build(out=&stack, slot=rsi, desc)

253e180:
 253e191: r15 = slot address (base+0x72cbdd0+0x288)
 253e1a7: call 23172ce             ; get/create LIST CONTAINER at the slot
 253e1ac: call 1dbab02(0x60)       ; r13 = genuine subscription node
 253e1c2: call 253e21e             ; fill node from descriptor
 253e1c7: rdi = [r15]              ; = *(bus+0x288) = container
 253e1cd: call 231772c             ; intrusive insert(container, node)

23172ce (0x23172ec..0x2317357):
 23172ec: cmp QWORD [rdi],0        ; slot empty? -> create
 231731a: mov edi,0x18
 231731f: call 2317426             ; allocate 0x18-byte CONTAINER
 231732e: [hdr+0x00] = 0x100000000 ; refcount
 2317331: [hdr+0x08] = 0           ; FIRST NODE (initially empty)
 2317339: [hdr+0x10] = 0x7fffffff  ; max/sentinel
 2317354: mov [r14],rax            ; *(bus+0x288) = hdr   <-- INDIRECTION

253e21e (0x253e232..0x253e27c) builds the 0x60-byte node:
 253e23c: node+0x08 = 0x27eaf04    ; dispatch fn ptr
 253e24a: node+0x20 = [slot]       ; = hdr (OWNER back-pointer, NOT next)
 253e27c: call 226c048 -> node+0x50 = &embedded descriptor; copy desc into node+0x30

231772c (0x2317752..0x231778d):
 2317752: r12 = [hdr+0x08]         ; old first node
 2317766: [hdr+0x08] = node        ; REAL node linked ONE LEVEL DOWN
 231778d: [node+0x10] = r12        ; forward link lives at node+0x10
```
Ground truth from the dispatcher: `0x27eaf1f: mov rdi,[rdi+0x50]` — the object
handed to `0x27eaf04` is the 0x60-byte node (descriptor at `+0x50`), not the
0x18-byte container. Both buses (default `0x72cbdd0`, mode-3 `0x72cc278`) use
this same container chain.

The old `find_node()` (before this task) did:
```
cur = *(bus+0x288);                 // = container (0x18 bytes)
dispatch = *(cur+0x08);             // = heap node ptr, never base+0x27eaf04
descriptor(b, cur) reads cur+0x50;  // outside the 0x18-byte container
cur = *(cur+0x20);                  // out of bounds
```
so it could never accept a genuine node. Making that test pass without the
fix would require writing a synthetic node into guest memory — fabrication,
which is forbidden. Hence the old NO_NODE was uninformative about the guest.

## 3. The fix (shipped host code, read-only)

In `src/jni/publish_default.{h,c}`:

* Added `SOBER_PD_OFF_LIST_HDR_FIRST = 0x08` and corrected the comments:
  `SOBER_PD_OFF_BUS_LIST (0x288)` is a SLOT holding the 0x18-byte container,
  `SOBER_PD_NODE_OFF_NEXT` is `0x10` (the forward link set at `0x231778d`),
  and `0x20` is the owner back-pointer (`SOBER_PD_NODE_OFF_OWNER`).
* `sober_publish_default_find_node()` now does:
  `hdr = *(bus+0x288); node = *(hdr+0x08); next = *(node+0x10)` and applies
  the existing dispatch (`node+0x08 == base+0x27eaf04`) + descriptor
  (vptr `0x6dbbf98`, id 1, slot `&0x7234520`) checks to the real node.
* Read-only evidence line in `pd_ensure_node()` reports guard, container and
  `hdr+8` after the guest registration call.

No guest byte is written, no guest code is called by the finder, and no
descriptor/node/vtable/callback is fabricated. The only state change remains
the existing opt-in `0x253cb56(1,&base+0x7234520)` re-drive, which is genuine
guest code (guarded one-shot, idempotent) and is already force-withheld.

## 4. Runtime A/B (fresh `cmake --build build -j4`, engine SHA unchanged)

Seeded env (both runs): `SOBER_CRASHPAD_RETURN=1 SOBER_FAITHFUL_JNI_ARRAYS=1
SOBER_GAME_LOADED_PATH=1 SOBER_APP_SHELL=1 SOBER_RENDER=1`.

**A — default (re-drive enabled):**
```
[app-shell] crashpad-return module -> rc=0 latch=1
[app-shell] MessageBus prereqs -> ... node=0 ... guard@0x72a31ba=0 bus_guards=1/1 ... node=(nil) ...
[publish-default] guest node registration 0x253cb56(1,&0x7234520)
    -> node=0x55d5ba62f710 guard@0x72a31ba=1 hdr@0x72cbdd0+0x288=0x55d5b97712f0
       hdr+8=0x55d5ba62f710 (read-only discovery; no fabricated node)
[app-shell] guest->Java callback: gameLoadedCallback
[app-shell] default MessageBus publish -> rc=0
[app-shell] env_calls=166 callbacks=25 ... last='gameLoadedCallback'
[app-shell] marker withheld: diagnostic guest-state seed active
```
Before the fix the identical diagnostic (log `/tmp/opencode/w4-final.log`)
reported `-> node=(nil)` and `default MessageBus publish -> rc=-3`, with
`last='onAppBridgeNotification'`.

**B — kill switch `SOBER_PUBLISH_NODE_REDRIVE=0`:**
```
[publish-default] 0x253cb56 re-drive disabled by SOBER_PUBLISH_NODE_REDRIVE=0
[app-shell] default MessageBus publish -> rc=-3
[app-shell] env_calls=165 callbacks=24 ... last='vrContext'
```
No node and no callback without the genuine guest registration -> the finder
fix alone fabricates nothing.

`guard` goes `0 -> 1` in run A, proving the guest itself spent its one-shot
guard inside the re-drive. The pre-re-drive `guard=0` also shows the guest's
earlier `InitWithParams` call did not reach `0x24ad6a5` in this harness; why
that earlier call is skipped is outside this task's owned files (JNI-layer
behaviour in the long `0x24abf96` path) and is left to the lead. A genuine
node is nevertheless produced by the re-drive and is now discoverable.

## 5. Knobs / seeded_state

**No new knob is introduced by this fix** — the finder is always-correct and
read-only. The existing `SOBER_PUBLISH_NODE_REDRIVE` (default enabled, exact
falsy value = kill switch) remains the only gate on the guest-state-seeding
re-drive and is already in the forced-withhold chain via
`sober_publish_default_should_withhold()`. `SOBER_PREINIT_BUS` (task #507,
default OFF, opt-in, falsy = kill switch) is unchanged and must stay in
`seeded_state`.

Per worker-2's provenance-gate finding, a host-entered `0x27eaf04` re-drive
leaves a host return address and is scored UNCLASSIFIED, never GENUINE, so this
seeded diagnostic still cannot make the marker green. The marker stays
WITHHELD under decisions 013/038.

## 6. Corroboration
* Lead handoff + parallel analysis `analysis/scale-a1-node-registration.md`
  (same root cause, independently derived).
* Frozen `objdump -d -M intel` of `0x24ad699/0x24ad6a5`, `0x253cb56`,
  `0x253d064..0x253d09d`, `0x253e126`, `0x253e180`, `0x23172ce`,
  `0x231772c`, `0x253e21e`, `0x226c048`, `0x27eaf04`.
* Prior context: `analysis/C11-messagebus-publish.md`,
  `analysis/callback/task500-latch-ready-prereqs.md` (F1/F2/F3),
  `analysis/callback/task507-preinit-ordering.md`, `docs/REVIEW-100.md`.
