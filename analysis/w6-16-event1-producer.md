# W6-16 — Genuine producer of MessageBus event id 1 (`GameLoaded`): DEX + native

Owner: **worker-16** (team TEAM-C2YA-ZTU9-RMAV). Task #551. **Analysis only** — no
`src/`, `tests/`, CMake, docs, gate or engine edit; no guest byte write, no fabricated
descriptor/node/callback, no host callback, no gate weakening, nothing committed.
Target strings/bytes treated as untrusted DATA. Date: 2026-11-21.

## 0. Evidence base (authoritative, re-derived this session)

| Artifact | SHA-256 | Verified |
| --- | --- | --- |
| Frozen engine `libroblox.so` | `3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335` | `sha256sum`, 120,041,712 B |
| `base.apk` (authoritative) | `114a6f8ed6f0f974cf632a9cbec2f5cd45e1d85d7ed251db4f3920140bc6ead4` | `sha256sum` |
| `base.apk:classes.dex` | `00309819b729f9c6f618b8a6f9befe6595caf9e316c3e5572cb10cf2bf7d84ac` | extracted this session |
| `base.apk:classes2.dex` | `5afac2bbd9f0e4f2e523f9f6f720bc74f96407aa551b0be44716334f44696f52` | matches CA01 note |
| `base.apk:classes3.dex` | `1b2406a01003e0ee1784824510d3ae88995ce914bc07cd7b0bfd4852f44e0909` | extracted this session |

I did **not** use `~/Downloads/basalt-arm64*` (modded). DEX decompiled with the installed
`apktool` from the SHA-matched `base.apk` into `/tmp/opencode/auth_smali` (smali:
12209 / 7059 / 7260 files across classes/classes2/classes3).

> **Correction to the corpus.** The pre-existing `/tmp/opencode/mini/out/smali` is from a
> different, 2.4 MB `mini.apk` whose `classes*.dex` sizes (6277920 B) do **not** match the
> authoritative APK (9057172 / 6145160 / 6983328 B). Concretely, the `nativeAppBridgeV2StartGameWithParam`
> call site is `uh/h0.smali` in the authoritative DEX but `uh/j0.smali` in the mini. Any
> prior DEX claim sourced only from `mini/out/smali` should be re-verified against the
> authoritative APK. All DEX facts below are from the authoritative smali.

Method: `objdump -d -M intel` + a full `objdump` listing (`/tmp/opencode/libroblox.dis`),
`nm -D`, `readelf`, the vetted APS2 decoder `analysis/bootstrap/decode_aps2.py`
(565,390 `R_X86_64_RELATIVE` relocs), and decoded authoritative smali. Frozen engine SHA
re-verified on disk before analysis. For the first `R E` PT_LOAD, RVA == file offset.

---

## 1. Verdict (up front)

1. **The authoritative DEX does not publish event id 1.** Java supplies only (a) the
   game-join `StartGameParams` including `placeId` (call `nativeAppBridgeV2StartGameWithParam`)
   and (b) the *consumer* `NativeGLJavaInterface.gameLoadedCallback(J)V`. There is no
   Java `MessageBus.publish` invocation that maps to event id 1. The Java
   `com/roblox/universalapp/messagebus/MessageBus` is a **separate, string-keyed** bus and is
   not this integer-event notifier system (§3).
2. **Event id 1 is a native internal engine notifier event.** Registration, descriptor,
   subscription node, dispatch and notifier are all guest-owned; the placeId payload is
   the sole datum and is carried in a plain `jlong` message (§2).
3. **There is no static producer function for event id 1.** `0x27eaf04` (the node dispatch
   whose execution *is* the publication) has **0 direct `call`/`jmp` references and 6 `lea`
   (node-creation) references** in the whole disassembly. It is reachable only **virtually,
   through the function pointer at `node+0x08`** (§4). This is a property of the guest's
   virtual event system, not a recoverable direct call edge.
4. **Clean trigger: DISPROVEN for the native harness.** The upstream trigger is the guest's
   game-load state machine (`nativeAppBridgeV2StartGameWithParam` → Lua DataModel load →
   `SessionReporterState_GameLoaded`), which never runs to that state in the harness. The
   harness's only route to `0x27eaf04` is the host `dispatch(node, &place_id)` re-drive in
   `src/jni/publish_default.c` — a host-entered seed that the provenance gate correctly
   rejects (missing guest RA pair / guest text above). No guest byte patch, fake vtable,
   host callback or gate weakening changes this (§5). Marker stays WITHHELD.

---

## 2. The native event-id-1 machine (byte-exact, SHA-bound)

### 2.1 Registration (proven)
```
24abf96  nativeAppBridgeV2InitWithParams   (JNI export)
  24ad50a   FFlag-gated lifecycle-listener setup (gate byte 0x72cb648 = EnableAppBridge*
            components; registers via vtable [obj+0x98]/[obj+0x10]) -- NOT a publisher
  24ad699:  lea rsi,[0x7234520]; mov edi,1; call 0x253cb56   <-- only call to registrar
  253cb56:  test edi,edi; cmp byte [0x72a31ba],0; jne exit; mov byte [0x72a31ba],1
            (one-shot guard spent 13 bytes in, BEFORE the node is built)
  253cb9d:  call 0x24a8e44 with edi=4  -> MessageBus singleton base+0x72cbdd0
  253d06e:  descriptor vptr 0x6dbbf98; 253d07f id=1; 253d08f slot=&0x7234520
  253d09d:  call 0x253e126 -> ... -> 0x23172ce (create 0x18-byte list CONTAINER at bus+0x288)
                                -> 0x1dbab02(0x60) (node)
                                -> 0x253e21e (node+0x08 = 0x27eaf04 dispatch at 0x253e235;
                                              node+0x50 = embedded descriptor)
                                -> 0x231772c (intrusive insert: container+0x08 = node,
                                              node+0x10 = next)
```
(`bus+0x288` is a SLOT holding the container pointer; the node is one level down at
`container+0x08` — the indirection documented in `scale-a1-node-registration.md`.)

### 2.2 Publication path (the chain that fires the callback)
```
node+0x08 = 0x27eaf04   subscription-node dispatch ("the publish trampoline")
  27eaf1f: rdi = [node+0x50]                       ; embedded id=1 descriptor
  27eaf2b: rsi = &saved_message                    ; &jlong placeId
  27eaf2f: call [desc.vptr+0x30] = 0x27eaf4c       ; descriptor virtual invoke
    27eaf4c: event_id=[desc+8]=1; slot=[desc+0x18]=&0x7234520; placeId=[rsi]
    27eaf63: rax=[0x7234520]=0x6dbd860             ; notifier TABLE
    27eaf66: rcx=[0x6dbd860 + 1 - 1] = 0x27eaf6e   ; entry for id=1
    27eaf6b: jmp rcx                                ; tail-call notifier, RSI=placeId
      27eaf6e: GetEnv; class=[0x72a2f48]; method=[0x72a2fc0]
      27eafae: call 0x25848e0        (JNI CallStaticVoidMethodV thunk)
      27eafb3: <-- provenance return address; logs
                "onGameLoaded() SessionReporterState_GameLoaded placeId:%lld" (0x36c7c9)
```

### 2.3 APS2 uniqueness (independently re-decoded this session)
```
who_points_to(0x27eaf6e) -> only 0x6dbd860      (notifier table entry 0, id=1)
who_points_to(0x6dbd860) -> only 0x7234520      (descriptor slot global)
who_points_to(0x27eaf4c) -> only 0x6dbbfc8      (vtable vptr 0x6dbbf98 + 0x30)
who_points_to(0x27eaf04) -> NONE                 (installed by six `lea` sites, not a reloc)
```
The full notifier table `0x6dbd860` was decoded; entries are indexed by `id-1` with ids
`8k+1`. Entry 0 (`id=1`) = `0x27eaf6e`. Neighbouring entries are JNI-callback thunks in the
AppBridge/StartGame region (`0x2ec31e8`, `0x2ec3210`, `0x2ec3d86`, `0x2ec4396`,
`0x2ec4656`, …) — consistent with an internal event family fired by the engine's
game-load state machine, **not** by any Java publish call.

---

## 3. Authoritative DEX findings

- `NativeGLJavaInterface.smali:103` — `gameLoadedCallback(J)V` is the *consumer endpoint*:
  `sget-object sImplementation, EngineJavaCallback2; invoke-virtual b(J)V`. It does not
  publish anything; it is the Java side that native `0x27eaf6e` calls into.
- The implementation chain is `com/roblox/client/game/a.smali:73 b(J)V` (logs
  `"gameLoadedCallback() "`, tag `rbx.game.callback`) → `a$a.n(J)` → …
  `ExperienceSession.smali:2092 n(J)` (logs `"onGameLoaded: placeId = "`).
- The placeId source is the game-join call `NativeGLInterface.smali:75`
  `nativeAppBridgeV2StartGameWithParam(StartGameParams)I`, invoked from
  **`uh/h0.smali:532`** (authoritative). It is the API that, natively, caches placeId to
  global `0x72a31c0` (store at `0x2ebd86a`; read at `0x2ec1370`).
- `com/roblox/universalapp/messagebus/MessageBus.smali` (classes2) exposes only
  `publishRaw(String,String)`, `publishProtocolMethod(Request/Response)Raw(...)`,
  `subscribe`/`doSubscribeRaw`, etc. — a **string-keyed** bus with JNI exports
  `Java_com_roblox_universalapp_messagebus_MessageBus_publishRaw` (`0x2479b3c`) etc. This is
  a different subsystem from the integer-id notifier of §2; publishing to it does not
  reach `0x27eaf6e`/`gameLoadedCallback`. (Confirmed: `publishRaw` builds string-keyed
  message/linking objects and calls a vtable `+0x8`, no reference to `0x6dbd860`,
  `0x7234520`, `0x27eaf04` or the id-1 descriptor vptr `0x6dbbf98`.)
- `nativeAppBridgeV2SendAppEventOnGameLoaded` (`0x2ebca9e`, Java `uh/f.A`) — a tempting
  alternative — is **disqualified** as the producer: it builds an event object with a
  **different vptr `0x6dbcc20`** and routes through `nativePassKeyEvent` (`0x2eb6b2a`) and
  the AppBridge event path gated by JNIAppBridge flag `0x72a3268`. It never touches the
  id-1 notifier chain. It cannot fire event id 1.

**Conclusion (DEX):** there is no Java-side producer of MessageBus event 1. Java is the
payload source and the callback sink; the firing of event 1 is entirely native.

---

## 4. Who is the genuine producer? (strict scope)

A "producer" must cause `0x27eaf04` to execute with `RDI = node`, `RSI = &placeId`.
Evidence:

- **0 direct callers.** `grep -cE '(call|jmp)[[:space:]]+27eaf04'` over the full listing
  = **0**; `grep -cE 'lea[[:space:]].*27eaf04'` = **6** (the node-creation sites at
  `0x253e235`, `0x288c2d4`, `0x288ceb8`, `0x3100061`, `0x3df87fd`, `0x560e7c1`). This
  agrees with the earlier Ghidra `xrefs` result in `C11-messagebus-publish.md` §4.
- The only way in is the **indirect** call through the node's `+0x08` function pointer,
  i.e. the generic subscription-list dispatch of the guest's event system. That walker is
  reached from the guest's game-load state machine and is not a directly addressable
  `publish(1, placeId)` symbol in this stripped image. I searched for a dedicated list-walk
  function (indirect `call [node+8]` with a `[node+0x10]` next-pointer advance near the
  `+0x288` container) and found only generic intrusive-list/destructor patterns; I do
  **not** claim a specific walker RVA — that would be an over-claim.
- Upstream, the placeId that becomes the message is cached by the genuine game-join export
  `nativeAppBridgeV2StartGameWithParam 0x2ebd51a` to `0x72a31c0` (store `0x2ebd86a`), and
  the notifier's own log label is `SessionReporterState_GameLoaded`. So the semantic
  producer is: **guest game join → Lua DataModel finish → SessionReporter state
  `GameLoaded` → guest event system walks the `+0x288` subscription list →
  `0x27eaf04`.** The state→dispatch edge is virtual and is stated at its true confidence
  (strongly supported by the log label / RTTI / placeId plumbing, **not** a static
  direct-call proof).

---

## 5. Clean-trigger verdict for the native harness — DISPROVEN

All genuine *static* prerequisites exist and were re-confirmed: reloc chain
(`[0x6dbd860]=0x27eaf6e`, `[0x7234520]=0x6dbd860`, `[0x6dbbfc8]=0x27eaf4c`), cached class
`0x72a2f48` / method `0x72a2fc0`, MessageBus guards `0x72cc270`/`0x72cc718`, placeId global
`0x72a31c0`, canary `0x72293f8`, one-shot guard `0x72a31ba`. What is missing is the
**event firing itself**: the guest never reaches `SessionReporterState_GameLoaded`, so
`0x27eaf04` is never entered by guest code.

The only harness route is `sober_publish_default_publish()` in `src/jni/publish_default.c`,
which calls `dispatch(node, &place_id)` with `dispatch = base+0x27eaf04`. That is a
**host-entered seed**: the provenance gate needs RA `0x27eafb3` directly above
`0x2584960` with guest text above `0x27eaf04`'s epilogue (`scale-a2-notifier-chain.md`
G6–G8); a host call leaves a host return address and is classified `HARNESS_THUNK`, never
genuine. Enabling it therefore forces `seeded_state != 0` and the marker WITHHELD — by
design and correctly.

The real Java lifecycle that would fire the event is omitted/unstable in the harness:
`nativePreloadFlagOverrides 0x2edd859` + `nativeRetryInit 0x2edd739` (FFlag
`EnableAppBridgeComponents` → gate byte `0x72cb648`, the very gate on the listener setup at
`0x24ad50a`), `nativeUpdateAdapterInit 0x234df63`, `setTaskSchedulerBackgroundMode
0x2eba91c`, `nativeAppBridgeV2StartAppWithParams 0x26fe80f`, `nativeAppBridgeStartLuaAppDM
0x25448f6` (indirect bus getter, unstable), and the full DataModel join. Without these,
event 1 is never published by the guest.

**No clean native-harness trigger exists for MessageBus event id 1.** Reaching the callback
cleanly requires the genuine Android/Java lifecycle and Lua DataModel load — a forbidden
Android runtime or a withhold-worthy seed. Consistent with decision 013/038 and the Wave 5
terminal finding (State.md). This report proposes no byte patch, fake vtable, host callback
or gate change.

---

## 6. Reproduction commands

```
sha256sum /tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so \
  /home/user/.local/share/basalt/linux/payloads/3170-204ad049599c96afb75b8a21d5051063e821d6c9/sober_apk/base.apk
unzip -p <base.apk> classes2.dex | sha256sum            # 5afac2bb...
apktool d -f <base.apk> -o /tmp/opencode/auth_smali      # authoritative smali
grep -cE '(call|jmp)[[:space:]]+27eaf04' /tmp/opencode/libroblox.dis   # 0
grep -cE 'lea[[:space:]].*27eaf04' /tmp/opencode/libroblox.dis          # 6
grep -n '27eaf04\|27eaf4c\|27eaf6e\|253cb56\|24ad699' /tmp/opencode/libroblox.dis
python3 analysis/bootstrap/decode_aps2.py    # APS2; extend who_points_to(0x27eaf6e|0x6dbd860|0x27eaf4c|0x27eaf04)
grep -n 'nativeAppBridgeV2StartGameWithParam\|gameLoadedCallback' -r /tmp/opencode/auth_smali
```

## 7. Honesty / limitations

- Static analysis only; no debugger/emulator was used as evidence (Android SDK/emulator are
  research-only oracles, never the final runtime). No live hit at `0x27eaf04`/`0x27eaf6e`
  was observed by this worker.
- The exact generic "walk +0x288 list and call `node+0x08`" publisher routine is not isolated
  to a single RVA here; the dispatch is virtual and the stripped image exposes no direct
  caller. The id-1 identification rests on descriptor `id==1`, vptr `0x6dbbf98`, invoke
  `0x27eaf4c` and the unique table entry `[0x6dbd860]==0x27eaf6e`, not on a unique xref.
- The `SessionReporterState_GameLoaded → subscription dispatch` arrow is inferred from the
  notifier's own log label, RTTI (`DataModel`/`gameLoadedSignal`) and the placeId plumbing;
  it is not a recovered direct call edge.
- No source, test, CMake, doc, gate or engine file was changed. Nothing committed.
  Marker remains WITHHELD; decisions 013/038 intact.

---

## 8. Addendum — checked and disproven: surface-update vtable lead (worker-3 handoff)

worker-3 (#538, authoritative AVD oracle) suggested the relocated vtable dispatch in
`nativeAppBridgeV2UpdateSurfaceAppWithPlatformParams 0x2773194` — `0x2773301: call [rcx+rax]`
over vptr `0x6dbcb20`, slots `+0x20/+0x28` — as a candidate for the missing
surface-update → MessageBus-event-1 → `0x24ad50a` → `0x27eaf04` leg. I decoded the APS2
relocations of that vtable against the frozen SHA:

```
vtable 0x6dbcb20:  +0x00 -> 0x1dd72a0 (bare `ret`)
                   +0x08 -> 0x2ec0584 (trivial destructor)
                   +0x10 -> 0x2773354 (constructor)
                   +0x18 -> 0x2ec0598 (copy ctor, sets vptr 0x6dbcb20)
                   +0x20 -> 0x1dd72a0 (bare `ret`)
                   +0x28 -> 0x2773728 (trivial destructor)
                   +0x30 -> 0x2773398 ; +0x38 -> 0x2ec05c4 ; +0x40 -> 0x2ec05da
```

The `0x27732db..0x2773301` sequence is a **cleanup loop**: it reads the object's own
`+0x20` embedded-list pointer, selects slot `0x20` or `0x28`, and calls the corresponding
vtable entry on each element. Slots `+0x20`/`+0x28` resolve to a bare `ret` and a trivial
destructor respectively. Nothing in this vtable resolves to `0x27eaf04`, `0x27eaf4c`,
`0x27eaf6e`, `0x6dbbf98` (the id-1 descriptor vptr) or `0x6dbd860` (the notifier table).
It is a value/copy object for surface-update platform params, **not** the MessageBus event
producer. The handoff lead is therefore **disproven as a producer** (a real pointer/RA trace
would only confirm these cleanup targets). No change to the §1/§5 verdict: the genuine
event-1 firing remains the guest game-load state machine, with no clean harness trigger.
