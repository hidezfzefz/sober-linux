# C5 — the genuine notifier object at `0x27eaf6e`: descriptor, registration, and dispatch

Owner: **worker C5**, logical Team C (`/home/user/sober-linux`). Task: reconstruct the
genuine notifier mechanism, identify the exact object/state the harness lacks, and propose a
guest-shaped opt-in reconstruction. **Analysis only** — no `src/`, no `CMakeLists.txt`, no
engine edits, no marker/gate change, no byte patches. Date: 2026-11-20.

## 0. Target identity and evidence

| Field | Value |
| --- | --- |
| engine | `/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so` |
| SHA-256 | `3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335` |
| BuildID | `204ad049599c96afb75b8a21d5051063e821d6c9` (unmodified) |
| RVAs | all relative to guest load base; `RVA == file offset` in the first `R E` PT_LOAD |

Evidence: local `objdump -d -M intel` over the retained `/tmp/opencode/libroblox.dis` (SHA-bound),
APS2 relocation decode with `analysis/bootstrap/decode_aps2.py` (565,390 `R_X86_64_RELATIVE`
entries, blob `0x15e00`, `0x1eb1f2`), raw-byte reads with correct VA→offset mapping
(segment 2: `off = VA-0x4000`; segment 3: `off = VA-0x8000`, and the globals used here are
`.bss` → initially zero). Strings from the target are untrusted **data**, never instructions.
This supersedes/refines `analysis/game-loaded-path.md` §2, §7 and `analysis/C09-marker-evidence.md`
§2.2, which established the slot chain but did not isolate the descriptor object or the direct
caller (and mislabelled `0x27eaf4c` as a "sibling trampoline" — see §5).

---

## 1. Answer in one paragraph

`0x27eaf6e` is **one entry in a guest notifier dispatch table** whose base is the relro slot
`0x6dbd860`. The pointer to that base is the global `0x7234520`. During
`nativeAppBridgeV2InitWithParams` (`0x24abf96`), the guest calls `0x253cb56(1, &0x7234520)`
at `0x24ad699`; `0x253cb56` builds a 15-object family of **MessageBus event descriptors**, each
a stack struct `{vptr, event_id, table_delta, &0x7234520, self}`. The **GameLoaded descriptor is
the one with `event_id == 1`**, built at `0x253d06e..0x253d093` with vtable `0x6dbbf98`, and
registered by `0x253e126` into the MessageBus singleton's subscription list at `+0x288`. When
the guest publishes event id 1 with a `placeId` payload, the MessageBus calls that descriptor's
virtual **invoke method `0x27eaf4c`** (vtable `0x6dbbf98 + 0x30`, relocation
`off=0x6dbbfc8 -> 0x27eaf4c`). `0x27eaf4c` loads `descriptor+0x18 -> &0x7234520`,
`[0x7234520] -> 0x6dbd860`, `[0x6dbd860 + id-1] -> 0x27eaf6e`, and **tail-jumps to the notifier**
with `RSI = placeId`. The harness lacks the **event publication** (the guest side that publishes
id 1 with a real placeId), not the slot, the descriptor vtable, or the cached JIDs.

---

## 2. Three layers of guest-owned indirection (all present after relocation)

```
descriptor+0x18  ──▶  0x7234520        (global pointer, .data/.bss, APS2: addend 0x6dbd860)
                         │
                         ▼
                      0x6dbd860        (relro slot = notifier TABLE BASE, APS2: addend 0x27eaf6e)
                         │  + (event_id - 1) bytes
                         ▼
                      0x27eaf6e        (GameLoaded notifier; table entry 0)
                         │  call
                         ▼
                      0x25848e0        (thunk -> CallStaticVoidMethodV, JNIEnv+0x470)
                         │
                         ▼
                 gameLoadedCallback(J)V   (cached class 0x72a2f48 / method 0x72a2fc0)
```

Relocation proofs (unique — `who_points_to` over all 565,390 relocations):

```
off 0x6dbd860 holds ptr 0x27eaf6e      ; only reference to the notifier anywhere
off 0x7234520 holds ptr 0x6dbd860      ; only reference to the slot anywhere
off 0x6dbbfc8 holds ptr 0x27eaf4c      ; only reference to the invoke trampoline
```

The function-pointer table at `0x6dbd860` (APS2 addends, 20 entries `0x6dbd860..0x6dbd8f8`):

| byte offset | address | addend (notifier) | family hint |
| --- | --- | --- | --- |
| `+0x00` (id 1) | `0x6dbd860` | `0x27eaf6e` | **GameLoaded** (log `SessionReporterState_GameLoaded`) |
| `+0x08` (id 9) | `0x6dbd868` | `0x288b23e` | generic JNI notify |
| `+0x50` (id 0x59) | `0x6dbd8b8` | `0x27e9d0c` | JNI notify (method global `0x72a2fa8`) |
| `+0x88` (id 0x91) | `0x6dbd8f8` | `0x2ec45a6` | GameLeave / `gameDidLeave()V` |

Every entry in this table that I inspected has the same shape as `0x27eaf6e`: `get JNIEnv`
(`0x227b9e2`) → load class `0x72a2f48` and a cached method global → `call 0x25848e0`. Event ids
are all `8*k + 1` (`0x1, 0x9, 0x31, 0x39, … 0x91`), so `event_id - 1` is the byte offset of the
8-byte entry; the low bit is the "resolve through table" flag tested by the invoke methods.

---

## 3. The event descriptor object that contains `&0x7234520`

### 3.1 Exact construction (GameLoaded descriptor), `0x253cb56`

`0x24ad699` passes `rdi=1` ("enable" flag; `test edi,edi; je` at `0x253cb7c` skips on 0) and
`rsi = &0x7234520`. `0x253cb6a` saves that pointer in `rbx`, and every descriptor built by the
function stores `rbx` at `+0x18`. The GameLoaded one is built at `0x253d06e..0x253d093`:

```
253d06e: lea 0x6dbbf98,%rcx        ; vtable for the GameLoaded descriptor
253d075: lea -0x2a0(%rbp),%r14     ; descriptor (stack object)
253d07c: mov %rcx,(%r14)           ; +0x00 = vptr 0x6dbbf98
253d07f: movq $0x1,0x8(%r14)       ; +0x08 = event_id 1  <-- GAMELOADED
253d087: movq $0x0,0x10(%r14)      ; +0x10 = table_delta 0
253d08f: mov %rbx,0x18(%r14)       ; +0x18 = &0x7234520  <-- THE POINTER IN QUESTION
253d093: mov %r14,0x20(%r14)       ; +0x20 = self
253d097: mov %rax,%rdi             ; rax = MessageBus singleton from 0x24a8e44(4)
253d09a: mov %r14,%rsi             ; descriptor
253d09d: call 0x253e126            ; register this subscription
```

So the object is a **0x28-byte C++ "event handler descriptor"**:

```
+0x00  vptr        = guest RVA 0x6dbbf98        (relocated guest vtable)
+0x08  event_id    = 1                           (GameLoaded; encoded as 8*k+1)
+0x10  table_delta = 0                           (added to &0x7234520 before deref)
+0x18  slot_ptr    = &0x7234520                  (address of the global slot pointer)
+0x20  self        = &descriptor                 (used by the virtual self-release path)
```

The 15 descriptors `0x253cb56` registers all share `+0x18 = &0x7234520` and `+0x10 = 0`; they
differ only in `event_id` (`0x79, 0x81, 0x69, 0x71, 0x31, 0x39, 0x41, 0x49, 0x51, 0x59, 0x61,
0x9, 0x1, 0x89, 0x91`) and vtable. **`event_id == 1` is the GameLoaded descriptor** and the only
one whose vtable/copy/invoke are the bespoke `0x6dbbf98` / `0x2ebc4d8` / `0x27eaf4c` set; the
other ids use the generic `0x6dbba28`-family vtable whose invoke is `0x2ebb616`.

### 3.2 The descriptor vtable `0x6dbbf98` (APS2 addends)

| vptr+ | addend | role |
| --- | --- | --- |
| `+0x00` | `0x1dd72a0` | scalar deleting destructor |
| `+0x08` | `0x2ebc490` | destroy (no-op tail) |
| `+0x10` | `0x2ebc4a4` | clone: `alloc(0x20)`, set vptr `0x6dbbf98`, copy `+8,+0x10,+0x18` |
| `+0x18` | `0x2ebc4d8` | copy-into-existing (same three fields) |
| `+0x20` | `0x1dd72a0` | scalar deleting destructor (second entry) |
| `+0x28` | `0x2ebc4f4` | destroy (no-op tail) |
| **`+0x30`** | **`0x27eaf4c`** | **invoke / dispatch — the direct caller of the notifier (§5)** |
| `+0x38` | `0x2ebc508` | type check (`cmp [rsi+8], 0x60dd48`) |
| `+0x40` | `0x2ebc51e` | returns vtable `0x6dbbff8` |

`0x27eaf4c` sits immediately before the notifier `0x27eaf6e` in `.text`; `game-loaded-path.md`
saw the reloc `0x6dbbfc8 -> 0x27eaf4c` and called it a "sibling trampoline" — correctly noting it is
*not* the notifier, but it is in fact the **invoke method of the GameLoaded descriptor vtable**,
i.e. the guest code that reads the slot and jumps to the notifier.

---

## 4. How `0x253cb56` registers it

Chain for the id=1 descriptor: `0x253e126` → `0x253e180` → `0x253e21e` → `0x226c048`.

```
253e126(rdi=singleton, rsi=descriptor):
  lea 0x288(%rdi),%rsi          ; 253e142: subscription LIST HEAD in singleton at +0x288
  call 0x253e180                ; insert node at head (locked)
253e180:
  lock thread gate 0x23172ce
  alloc 0x60 via guest allocator 0x1dbab02
  call 0x253e21e(new_node, list_head, descriptor)
  unlock 0x231772c ; *(out) = node ; lock incl 0x4(node)   ; refcount
253e21e(new_node):
  +0x00 = 0x100000000
  +0x08 = 0x27eaf04             ; node type/vtable (same cluster as 0x27eaf4c)
  +0x20 = old list head         ; intrusive singly-linked "next"
  +0x18 = flags | 4
  +0x28 = 0x246fb6a             ; dispose/delete helper
  +0x30 = embedded descriptor copy (memcpy 0x226c048 of the 0x28-byte descriptor)
```

The **MessageBus singleton** is returned by `0x24a8e44(4)` (`0x24a8e4d..0x24a8e76`): globals
`0x72cbdd0` (default) or `0x72cc278` (mode 3), gated by one-shot bytes `0x72cc270` /
`0x72cc718`. These are `.bss` (zero-initialised), so the first call constructs the singleton and
`0x253cb56`'s own guard byte `0x72a31ba` is also initially zero → registration runs. The
registration site `0x24ad699` is reached unconditionally by all branches of `0x24abf96`
(verified convergence at `0x24ad5c0/0x24ad5d6/0x24ad5ed → 0x24ad699`).

Other families use different list offsets on the same singleton (`+0x2e0` for id `0x79`,
`+0x378` for ids `0x91`, etc.), but the node layout and the `+0x18 -> &0x7234520` /
`event_id -> table entry` resolution are identical.

**Net effect:** after `InitWithParams`, the guest holds a heap subscription node whose embedded
descriptor says "event id 1 → resolve callback through `&0x7234520` → table `0x6dbd860` →
notifier `0x27eaf6e`". Nothing in this chain is a host structure and no guest byte is patched.

---

## 5. The guest code that reads the slot and calls the notifier — `0x27eaf4c`

Full disassembly (the answer to "what reads the slot to call the notifier"):

```
27eaf4c: mov  %rdi,%rax              ; rax = descriptor (this)
27eaf4f: mov  0x8(%rdi),%rcx         ; rcx = event_id = 1
27eaf53: mov  0x18(%rdi),%rdi        ; rdi = &0x7234520   <-- the stored pointer
27eaf57: mov  (%rsi),%rsi            ; rsi = *(event message) = placeId (jlong payload)
27eaf5a: add  0x10(%rax),%rdi        ; rdi = &0x7234520 + delta(0)
27eaf5e: test $0x1,%cl               ; id low bit => table-indirect
27eaf61: je   27eaf6b
27eaf63: mov  (%rdi),%rax            ; rax = [0x7234520] = 0x6dbd860 (table base)
27eaf66: mov  -0x1(%rax,%rcx,1),%rcx ; rcx = [0x6dbd860 + 1 - 1] = 0x27eaf6e
27eaf6b: jmp  *%rcx                  ; TAIL-CALL the notifier, RSI = placeId
```

Semantics, all observed:

* It is the virtual method at vtable `0x6dbbf98 + 0x30` — the method the MessageBus invokes on
  the id=1 descriptor when event 1 is published.
* `mov (%rsi),%rsi` is the payload convention: the message object's **first qword is the placeId
  (jlong)** and is forwarded verbatim as the notifier's `RSI` argument.
* For id=1, `id-1 == 0`, so the loaded function pointer is exactly `[0x6dbd860] == 0x27eaf6e`.
  The notifier then does `call 0x25848e0` at `0x27eafae` and returns to `0x27eafb3` — that
  return address remains the hard provenance discriminator from `C09`/`C03` (a harness ALT-path
  call to `0x25848e0` returns into harness code and never enters `0x27eaf4c`/`0x27eaf6e`).
* The generic sibling for the rest of the family is `0x2ebb616` (vtable `0x6dbba28 + 0x30`),
  which performs the same `mov 0x18(%rdi),%rdi` / `mov (%rdi),%rax` /
  `mov -0x1(%rax,%rdx,1),%rdx` resolution but with refcounted message arguments and a full
  prologue/epilogue. The GameLoaded variant is the minimal tail-jump because its payload is a
  raw `jlong`.

The notifier itself (`0x27eaf6e`) and the JID setup are already documented in
`analysis/game-loaded-path.md` §1 and `analysis/C09-marker-evidence.md` §2.1/§2.3: class global
`0x72a2f48`, method global `0x72a2fc0`, thunk `0x25848e0`, log string
`"onGameLoaded() SessionReporterState_GameLoaded placeId:%lld"` (`0x36c7c9`).

---

## 6. The exact object/state our harness lacks

**Present already (no reconstruction needed):**

1. The relro globals `0x6dbd860 -> 0x27eaf6e` and `0x7234520 -> 0x6dbd860` (APS2 at load).
2. The guest vtable `0x6dbbf98` and invoke `0x27eaf4c` (guest `.text`/relro).
3. The cached class/method globals `0x72a2f48` / `0x72a2fc0` (JNI bridge seeding).
4. The subscription descriptor itself — `guest_app_shell.c:1186` calls `0x24abf96`, and
   `0x24ad699 → 0x253cb56 → 0x253e126` runs on the full seed stack (guard bytes are zero), so an
   id=1 descriptor node is installed in singleton `+0x288`.

**Missing (the actual blocker):**

5. **The event publication.** Nothing in the harness publishes MessageBus **event id 1** with a
   `placeId` payload. `SOBER_GAME_LOADED_PATH` (`guest_app_shell.c:1302+`) calls
   `nativeAppBridgeV2StartGameWithParam` (`0x2ebd51a`) and
   `nativeAppBridgeV2SendAppEventOnGameLoaded` (`0x2ebca9e`), but per `PROGRESS.md` Phase 9 it
   faults at guest `0x31a1360` (NULL `app+0x488`, bogus TSD-sentinel handle) before a Lua
   DataModel finishes loading and the SessionReporter reaches `GameLoaded`. Consequently neither
   `0x27eaf4c` nor `0x27eaf6e` is ever entered, and no genuine `gameLoadedCallback(J)V` is counted.
6. Upstream state required for (5), catalogued by other workers: `app+0x488`
   (`src/jni/app488_fix.h`, guest writer `0x24af26a..0x24af2e6`, genuine vtable `0x6e02918`),
   app singleton `0x72cb5e0` (fault chain `0x2e7b8e2 → 0x24ad562 → 0x2772403`), and a faithful
   `StartGameParams` placeId (`SOBER_STARTGAME_V2` / `SOBER_GAME_PARAMS`).
7. A **nonzero placeId** for full fidelity: `0x27eafb3` skips the log string when `placeId == 0`
   (the thunk call at `0x27eafae` still happens), so a real placeId is needed for the log-based
   discriminator too.

---

## 7. Proposed guest-shaped opt-in reconstruction (no host vtables, no byte patches)

Two shapes, explicitly ranked. Both use only guest RVAs, guest code, and guest-shaped data;
neither writes a guest byte nor points a vtable at a host function.

### Shape A — faithful (provenance-correct; the only decision-013-qualifying route)

Close the upstream object gaps (5–7) and let the **guest** publish event id 1:

1. Enable the already-authored guest-shaped seeds for the blocking objects:
   `SOBER_APP488_FIX=1` (builds `app+0x488` with vptr `0x6e02918` via guest allocator `0x1dbab02`),
   app-singleton construction, and a faithful `StartGameParams` with `placeId > 0`.
2. Replay the real sequence in `guest_app_shell.c`: `0x24abf96` (registers the id=1 descriptor),
   `0x247dc8d`/`0x24a8e44`, `0x26fe80f`, `0x25448f6`, `0x2ebd51a`, `0x2ebca9e`.
3. The guest's SessionReporter → `GameLoaded` transition publishes MessageBus event id 1;
   the MessageBus calls the id=1 descriptor's `+0x30` invoke `0x27eaf4c`, which resolves
   `0x7234520 → 0x6dbd860 → 0x27eaf6e` and tail-jumps with `RSI = placeId`.
4. **Proof:** breakpoint/hit-counter at `0x27eaf4c` and `0x27eaf6e` with `RSI = placeId` and
   `[0x6dbd860] == base+0x27eaf6e`; thunk return address `0x27eafb3`; bridge log
   `last='gameLoadedCallback'`, `callbacks` increased. Only then may the strict gate emit the
   marker (with all 17 withhold knobs unset and phase-3 render `0x649d422` observed).

### Shape B — descriptor-level reconstruction (opt-in diagnostic; NOT decision-013 proof)

If the aim is to exercise the exact slot-resolution chain without the heavyweight game-load
state, reconstruct the descriptor/event objects guest-shaped and call the **genuine guest invoke**:

```
descriptor (guest heap via 0x1dbab02, 0x28 bytes):
  +0x00 = base + 0x6dbbf98     ; REAL guest vtable (never a host table)
  +0x08 = 1                    ; event_id
  +0x10 = 0                    ; delta
  +0x18 = base + 0x7234520     ; slot pointer
  +0x20 = descriptor           ; self
message (guest memory, 8 bytes):
  +0x00 = placeId (jlong, > 0)
call base + 0x27eaf4c with RDI = descriptor, RSI = &message
```

This traverses the genuine `0x27eaf4c → [0x7234520] → [0x6dbd860] → 0x27eaf6e → 0x25848e0`
chain and fires the real notifier using only guest code and guest-owned structures. It is a
strict improvement over `SOBER_ALT_CALLBACK_PATH`'s direct `call 0x25848e0`: the callback now
returns through `0x27eafb3` and is entered from a guest descriptor dispatch. **Honest limit:** the
call is still *initiated* by the harness, so under decisions 013/037 it can be an opt-in
diagnostic only and **must not** emit `app shell rendered`; Shape A is the sole qualifying route.
Gate/withhold behaviour is unchanged by this proposal.

---

## 8. Acceptance evidence / how to verify

* `grep -n "27eaf6e" /tmp/opencode/libroblox.dis` → label only (no static caller), and APS2
  `who_points_to(0x27eaf6e)` → only `off=0x6dbd860`.
* APS2 `who_points_to(0x6dbd860)` → only `off=0x7234520`; `who_points_to(0x27eaf4c)` → only
  `off=0x6dbbfc8`.
* Disassembly of `0x253d06e..0x253d09d` (descriptor id=1 with `&0x7234520`) and
  `0x27eaf4c..0x27eaf6b` (slot read + tail-jump) as quoted above.
* Live (lead/verifier): hit-counter at `0x27eaf4c`/`0x27eaf6e`, `RSI = placeId`, thunk return
  `0x27eafb3`, `[0x6dbd860] == base+0x27eaf6e`, `gameLoadedCallback` counted, no
  `HONESTY NOTICE`, no ALT path, engine SHA unchanged.

## 9. Limits / honesty

* Static analysis only; no runtime capture of the notifier being entered was performed by this
  worker. The event id ↔ notifier mapping is proven by the descriptor's `event_id=1`, its vtable's
  `+0x30 = 0x27eaf4c`, and the table entry `[0x6dbd860] = 0x27eaf6e`; the exact guest function
  that *publishes* MessageBus event id 1 (SessionReporter state machine) was not isolated, so
  Shape A's final publish step is strongly supported but not a direct-call proof.
* The 15-descriptor family and the id→table-entry encoding (`8*k+1`, byte offset `id-1`) are
  derived from APS2 addends plus the invoke disassembly; all inspected table entries are JNI
  notifiers, but I did not enumerate every entry's Java method.
* No gate change, no marker emission, no source/`CMakeLists`/engine modification is proposed or
  made. All 17 decision-013 withhold knobs remain authoritative. `onAppBridgeNotification`,
  the ALT thunk, and all phase-8/9 opt-ins remain disqualified as app-shell proof.
