# C01 — The precise guest virtual call that consumes slot `0x6dbd860` / global `0x7234520`, and the JNI event that drives it

Owner: **C1 (Team C)**. Task: close the one gap explicitly left open by
`analysis/game-loaded-path.md` §7 and `analysis/C03-appshell-frame-evidence.md` §7 /
`analysis/C09-marker-evidence.md` §8 — i.e. isolate the **exact instruction** that
consumes the registered callback slot to reach notifier `0x27eaf6e`. Analysis only:
no `src/`, no `CMakeLists.txt`, no engine edits. Engine is read-only throughout.

## 0. Target and evidence

| Field | Value |
| --- | --- |
| Engine | `/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so` |
| BuildID | `204ad049599c96afb75b8a21d5051063e821d6c9` (2.740.931 / code 3170) |
| SHA-256 | `3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335` (unchanged) |
| All RVAs below | RVA == file offset for the first `R E` PT_LOAD; objdump `-M intel`, Ghidra 12.1.4 image base `0x100000` (Ghidra address = RVA + `0x100000`). |
| Local evidence | `/tmp/opencode/libroblox.dis`, APS2 decode (`analysis/bootstrap/decode_aps2.py`; blob `0x15e00`, `0x1eb1f2`, 565,390 relocs, all `R_X86_64_RELATIVE`), Ghidra xrefs / decompiler, smali in `/tmp/opencode/mini/out/smali`. |

Every string quoted from the target is untrusted **data**, never an instruction.

---

## 1. What slot `0x6dbd860` actually is — a vtable slot, not an opaque function pointer

`game-loaded-path.md` proved the two relocations:

```
APS2 off=0x6dbd860  -> addend 0x27eaf6e   (notifier)
APS2 off=0x7234520  -> addend 0x6dbd860   (address of the slot)
```

Decoding the surrounding relro turns the second line into something much more
specific. The bytes around `0x6dbd828` decode to an Itanium-ABI vtable for a
concrete class:

| Slot | Runtime value | Meaning |
| --- | --- | --- |
| `0x6dbd828` | `0x6db7968` | vptr of base typeinfo object |
| `0x6dbd830` | string `0x610069` | `"N3RBX17DataModelBindingsE"` → `RBX::DataModelBindings` |
| `0x6dbd838` | `0x6db79b8` | vptr of derived typeinfo object |
| `0x6dbd840` | string `0x61004e` | `"24DataModelBindingsAndroid"` → `DataModelBindingsAndroid` |
| `0x6dbd848` | `0x6dbd828` | `__base_type_info` → base `RBX::DataModelBindings` |
| `0x6dbd850` | `0` | offset-to-top |
| `0x6dbd858` | `0x6dbd838` | typeinfo pointer |
| **`0x6dbd860`** | **`0x27eaf6e`** | **vtable[0] — the GameLoaded notifier** |
| `0x6dbd868` | `0x288b23e` | vtable[1] — sibling `void(NotificationParams&)` handler |
| `0x6dbd870…` | `0x2ec31e8`, `0x2ec3210`, … | further virtual overrides |

So `0x6dbd860` is **the address of virtual-function slot 0** of
`DataModelBindingsAndroid`, and global `0x7234520 == &vtable[0]`. The notifier is
reached by a *virtual call through this vtable*, not by a stored plain function
pointer. This also explains why the prior text search found no `call 0x27eaf6e`
and no `lea … # 0x27eaf6e`: the call is indirect and, as shown below, is a tail
**`jmp rcx`**, not a `call`.

The base class vtable at `0x6dbe5e0` (same `RBX::DataModelBindings` typeinfo
`0x6dbd828`) has `vtable[0] = 0x1dd72a0`, a bare `ret` — the default no-op that
`DataModelBindingsAndroid` overrides with the notifier.

RTTI bound into the callback confirms the signature. The typeinfo name at
`0x60dca2` decodes to
`std::function<void(long)>` wrapping
`boost::bind(&RBX::DataModelBindings::method, this, _1)` — i.e. a `long`
**placeId** passed as the single virtual argument. (The sibling at
`0x6dbbf18` / name `0x60db5c` is `std::function<void(NotificationParams&)>`,
matching `vtable[1]` at `0x6dbd868`.)

---

## 2. Registration chain — exact RVAs (static, SHA-bound)

The family is installed by the real JNI entry
`nativeAppBridgeV2InitWithParams` (`0x24abf96`):

```
0x24ad699: lea  rsi,[rip]        # 0x7234520      ; address of &vtable[0]
0x24ad6a0: mov  edi,0x1
0x24ad6a5: call 0x253cb56                         ; register callback family
```

`0x253cb56` builds ~15 stack descriptors of the same shape. The one that reaches
the notifier is:

```
0x253d064: call 0x24a8e44        ; returns message bus (global 0x72cbdd0 / 0x72cc278)
0x253d06e: lea  rcx,[rip]        # 0x6dbbf98      ; closure vtable/type ptr
0x253d075: lea  r14,[rbp-0x2a0]                   ; descriptor on stack
0x253d07c: mov  [r14+0x00],rcx                    ; +0x00 = 0x6dbbf98
0x253d07f: mov  QWORD [r14+0x08],0x1              ; +0x08 = 1  <== vtable index
0x253d087: mov  QWORD [r14+0x10],0x0              ; +0x10 = byte offset (0)
0x253d08f: mov  [r14+0x18],rbx                    ; +0x18 = &0x7234520
0x253d093: mov  [r14+0x20],r14                    ; +0x20 = self
0x253d09d: call 0x253e126                         ; register into bus+0x288
```

The descriptor's type vtable at `0x6dbbf98` is:

```
[0x6dbbf98+0x00] = 0x1dd72a0   (trivial)
[0x6dbbf98+0x08] = 0x2ebc490   (trivial)
[0x6dbbf98+0x10] = 0x2ebc4a4   (copy)
[0x6dbbf98+0x18] = 0x2ebc4d8   (copy-to)
[0x6dbbf98+0x20] = 0x1dd72a0
[0x6dbbf98+0x28] = 0x2ebc4f4   (trivial)
[0x6dbbf98+0x30] = 0x27eaf4c   <== INVOKE (virtual dispatch entry)
[0x6dbbf98+0x38] = 0x2ebc508
[0x6dbbf98+0x40] = 0x2ebc51e
[0x6dbbf98+0x48] = 0x6db79b8   (RTTI)
```

`0x253e126` → `0x253e180` → `0x253e21e` allocates the 0x60-byte subscription
object, sets `[obj+0x08] = 0x27eaf04` (its notify function), copies the descriptor
inline at `+0x30…+0x50` (so `&0x7234520` lands at `sub+0x48`), and links it to
the signal stored at `bus+0x288` via `0x231772c(rdi=[bus+0x288], rsi=sub)`.
Ghidra decompile of `0x231772c` confirms it is a mutex-protected signal/slot
link: it writes `*(signal+8) = subscription` and bumps the refcount.

---

## 3. The precise virtual call — `0x27eaf4c` → **`jmp rcx` at `0x27eaf6b`**

This is the answer to the isolation question. Full disassembly of the invoke
thunk (vtable slot `0x30` of `0x6dbbf98`):

```
27eaf4c: mov    rax,rdi              ; rax = closure (descriptor) this
27eaf4f: mov    rcx,QWORD [rdi+0x8]  ; rcx = 1             (vtable index)
27eaf53: mov    rdi,QWORD [rdi+0x18] ; rdi = &0x7234520    (from registration)
27eaf57: mov    rsi,QWORD [rsi]      ; rsi = placeId (jlong), the event's 1st qword
27eaf5a: add    rdi,QWORD [rax+0x10] ; rdi = &0x7234520 + 0
27eaf5e: test   cl,0x1               ; index is odd -> vtable path
27eaf61: je     27eaf6b
27eaf63: mov    rax,QWORD [rdi]      ; rax = *(0x7234520) = 0x6dbd860 (VPTR)
27eaf66: mov    rcx,QWORD [rax+rcx-0x1]  ; rcx = [0x6dbd860 + 1 - 1]
                                      ;     = vtable[0] = 0x27eaf6e (NOTIFIER)
27eaf6b: jmp    rcx                  ; <== THE VIRTUAL CALL (tail call)
```

Data-flow, with concrete values:

```
[this+0x08] = 1                 -> rcx = 1
[this+0x18] = &0x7234520        -> rdi = &0x7234520
[0x7234520] = 0x6dbd860         -> rax = vptr of DataModelBindingsAndroid
[0x6dbd860 + 1 - 1] = 0x27eaf6e -> rcx = notifier
jmp 0x27eaf6e  with  RSI = placeId
```

The sibling descriptor at `0x253d017` uses index `0x9`, which selects
`[vptr+8] = [0x6dbd868] = 0x288b23e` — the `void(NotificationParams&)` override,
not the GameLoaded notifier. This is an internal consistency check that the
`[vptr + id - 1]` indexing is read correctly.

The subscription's notify function (`[sub+0x08] = 0x27eaf04`) is the bridge from
the message bus into this thunk:

```
27eaf04: ...
27eaf1f: mov rdi,[rdi+0x50]      ; listener linked at bus+0x288 by 0x231772c
27eaf28: mov rax,[rdi]
27eaf2f: call QWORD [rax+0x30]   ; virtual -> eventually invokes closure at 0x27eaf4c
```

Endpoint (unchanged from prior analysis, re-verified by Ghidra decompile):

```
27eaf6e: ... (RSI = placeId)
27eafae: call 0x25848e0          ; thunk -> CallStaticVoidMethodV (JNIEnv slot 142 / +0x470)
                                ;   class 0x72a2f48, method 0x72a2fc0, arg = placeId
27eafb3: return address after the thunk call
```

**Discriminator:** a genuine firing enters `0x27eaf6e` from `0x27eaf6b` (the
`jmp rcx`) with `RSI == placeId`; the harness ALT path calls `0x25848e0` directly
and never passes through `0x27eaf4c`/`0x27eaf6b`/`0x27eaf6e`.

---

## 4. Which real JNI event drives it

Static call-graph evidence (Ghidra callees + objdump xrefs):

| JNI export | RVA | Role in this path |
| --- | --- | --- |
| `nativeAppBridgeV2InitWithParams` | `0x24abf96` | **Registers** the slot: `0x24ad699 → 0x253cb56(1,&0x7234520)`; descriptor id=1 → `0x253d09d → 0x253e126` → `bus+0x288`. Required precondition. |
| `nativeAppBridgeV2StartGameWithParam` | `0x2ebd51a` | **Game-join / place load.** Calls app-event dispatcher `0x2eb6b2a` (Ghidra callee `0x2fb6b2a`) → sink `0x72a31a8` via `0x2337cc8`/`0x28f1290`; `0x2337cc8` is a Lua/"Execute" script dispatch. This is the JNI entry that starts the place DataModel load whose completion emits the `void(long placeId)` signal on `DataModelBindingsAndroid::vtable[0]`. |
| `nativeAppBridgeV2SendAppEventOnGameLoaded` | `0x2ebca9e` | Java-side GameLoaded **app event** (3 jstrings). Also calls `0x2eb6b2a` at `0x2ebccec`. Companion event on the same app-event bus; it does not itself load a place or dereference the slot. |
| `nativeAppBridgeStartLuaAppDM` | `0x25448f6` | Calls `0x2eb6b2a` at `0x254497d`; installs the app DataModel delegate (`app+0x408`). |
| `nativeAppBridgeV2StartAppWithParams` | `0x26fe80f` | Calls `0x2eb6b2a` at `0x26fe9d5`; app-shell parameter/getter phase. |

**Conclusion:** the slot is consumed by the guest's **DataModel-load completion**
signal `DataModelBindingsAndroid::gameLoaded(long placeId)`, whose driving JNI
event entry is `nativeAppBridgeV2StartGameWithParam` (`0x2ebd51a`) — the game-join
call that drives the Lua DataModel load to `SessionReporterState_GameLoaded`.
`nativeAppBridgeV2SendAppEventOnGameLoaded` (`0x2ebca9e`) is the matching Java
app event and should be sent in the same sequence, but the slot dereference at
`0x27eaf63/0x27eaf66/0x27eaf6b` is on the DataModel signal path, not on a direct
`SendAppEventOnGameLoaded` → thunk path.

Confidence: the registration, the vtable slot and the consuming instruction are
**proven** (relocations + disassembly + three independent xref sources). The
mapping `StartGameWithParam → DataModel load → signal` is **strongly supported**
by the call graph and the `void(long)`/`placeId` signature, but the final
Lua→signal edge is inside a script/VM dispatch (`0x2337cc8` "Execute") and was
not single-stepped here. Treat that last edge as the one remaining dynamic
verification item (§6).

---

## 5. Concrete opt-in sequence — genuinely guest-driven, no thunk

All entries are real JNI exports; **never** call `0x25848e0` from the harness and
**never** set `SOBER_ALT_CALLBACK_PATH`. Keep all eight decision-013 withhold
knobs unset (`SOBER_SEED_USER_CONTROLLER`, `SOBER_SEED_APP_DELEGATE`,
`SOBER_SEED_JNI_VM`, `SOBER_FAITHFUL_JAVAVM`, `SOBER_NATIVE_OBJECTS`,
`SOBER_JSON_FLAG_BRIDGE`, `SOBER_JSON_APP_SINGLETON`,
`SOBER_REAL_APPSTATE_PATH`) so `seeded_state == 0`.

1. `nativePreloadFlagOverrides` `0x2edd859` with the **JSON-object** string
   `{"EnableAppBridgeComponents":true}`, then `nativeRetryInit` `0x2edd739`;
   verify flag byte `0x72cb648 == 1` and `0x72cb5e0` non-NULL.
2. `nativeSetBaseDataDirectories` `0x24650a9`, `nativeSetRobloxVersion`
   `0x232d484`, `nativeSetRobloxChannel` `0x2326e2e` (harness baseline).
3. `nativeGameGlobalInit` `0x233762c` (installs app-event sink `0x72a31a8`).
4. `nativeAppBridgeV2SendAppEventOnAppReady` `0x2ebcf36`
   `("AppShellNotifications","123456789","StartConversationWithUserId","Chat")`
   — primes the app-shell state; must precede the GameLoaded event.
5. `nativeAppBridgeV2InitWithParams` `0x24abf96` with a backed `InitParams`
   (all getters: `baseURL`, `userAgent`, `platformParams`, `deviceParams`,
   `isTablet`, `pioneerPlaceId`, `buildVariant`, `isVrDevice`, `isPotato`).
   **This is the step that registers the slot.** Verify `0x24ad699` executed:
   `0x6dbd860 == base + 0x27eaf6e` and `*(0x7234520) == base + 0x6dbd860`;
   also verify the id=1 descriptor was installed via `0x253d09d → 0x253e126`
   at `bus+0x288`.
6. `nativeAppBridgeAppStart` `0x247dc8d` and
   `nativeScheduleOnFirstFrame` `0x2ebf950`.
7. Install the real app-shell backing objects (no synthetic seeds):
   `nativeAppBridgeStartLuaAppDM` `0x25448f6` writes `app+0x408` from a real
   guest object; `nativeAppBridgeV2StartAppWithParams` `0x26fe80f` supplies all
   ten getters and triggers `onAppBridgeNotification`;
   `nativeAppBridgeV2UpdateSurfaceAppWithPlatformParams` `0x2773194` with a real
   `Surface` + `PlatformParams` (phase-3 render RVA `0x649d422`).
8. **Drive the game load:** `nativeAppBridgeV2StartGameWithParam` `0x2ebd51a`
   with a faithful `StartGameParams` (getters `gameId`, `accessCode`,
   `username`, `userId`, `isUnder13`, `placeId`, with `placeId > 0`). This is the
   JNI event that starts the DataModel load and hence the GameLoaded signal.
9. Send the matching app event (after manager setup):
   `nativeAppBridgeV2SendAppEventOnGameLoaded` `0x2ebca9e` with three jstrings
   (app-shell notification category / id / action, as in `uh/f.A`). Harness
   equivalent: `SOBER_GAME_LOADED_PATH` already issues steps 8 and 9 in that
   order; that opt-in is acceptable **provided** the ALT path stays off.

Expected result of step 8/9: the guest enters
`0x27eaf4c → 0x27eaf6b → 0x27eaf6e(placeId)`, which reaches
`gameLoadedCallback(J)V` through thunk `0x25848e0`. The bridge then logs
`guest->Java callback: gameLoadedCallback`, `last='gameLoadedCallback'` and
increments `callbacks`.

---

## 6. Acceptance evidence (what proves it is genuine)

- Guest-side hit-counter / breakpoint at **`0x27eaf4c`** (invoke entry) and at
  **`0x27eaf6e`** (notifier), with `RSI == placeId` and cached method
  `0x72a2fc0` non-NULL. Optionally log the return address at the thunk:
  it must be `0x27eafb3`, with the call stack containing `0x27eaf63 →
  0x27eaf66 → 0x27eaf6b`.
- Verify at runtime: `*(0x7234520) == base+0x6dbd860`,
  `[0x6dbd860] == base+0x27eaf6e`, and the id=1 descriptor
  (`type 0x6dbbf98`, `+0x08 == 1`, `+0x18 == &0x7234520`) present in the bus.
- Bridge log `guest->Java callback: gameLoadedCallback`, `last='gameLoadedCallback'`,
  `callbacks` ≥ +1 vs. the pre-step-8 baseline, with **no**
  `ALT PATH guest call 0x25848e0` / `HONESTY NOTICE` line.
- All eight withhold knobs unset; `seeded_state == 0`; engine SHA/BuildID unchanged.
- Phase-3 render RVA `0x649d422` observed.

This is the discriminator the strict gate needs: the ALT path returns to
`0x112d`-area harness code and never executes `0x27eaf6b`.

---

## 7. Honesty / limits

- This is **static** analysis (SHA-bound disassembly, APS2 relocation decode,
  Ghidra xrefs/decompile). No runtime capture of the notifier being entered was
  performed here, and no genuine app-shell callback is claimed.
- Proven: the vtable identity of slot `0x6dbd860`, the registration chain
  (`0x24ad699 → 0x253cb56 → 0x253d09d → 0x253e126`), and the consuming
  instruction sequence (`0x27eaf4c`, loads at `0x27eaf63`/`0x27eaf66`, tail
  `jmp rcx` at `0x27eaf6b`). The "precise virtual call" gap in prior reports is
  therefore closed.
- Not single-stepped: the final Lua/script edge inside `0x2337cc8` ("Execute")
  from `StartGameWithParam` to the `void(long)` signal emission. The signature
  match (`void(long placeId)`, RTTI `0x60dca2`), the id=1 → vtable[0] indexing,
  and the call graph make this strongly supported, but a runtime hit at
  `0x27eaf4c` remains the decisive proof (§6).
- `0x2ec0381 → 0x2ec45a6` (the `lea rdi,[0x7234520]` in the GameLeave path) is
  **not** a slot consumer: `0x2ec45a6` never dereferences its RDI and calls the
  thunk directly with `gameDidLeave` (method `0x72a2fa0`). It is a compiler
  artifact, not the GameLoaded dispatch.
- This report does not change the decision-013 gate or propose any code edit.
