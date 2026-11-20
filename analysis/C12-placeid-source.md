# C12 — the genuine source of the `placeId` (jlong) in RSI of notifier `0x27eaf6e`

Owner: **worker C12**, logical Team C (`/home/user/sober-linux`). Analysis only —
`src/`, `CMakeLists.txt`, the marker gate and the engine are untouched. Strict
decision 013 applies: nothing here is a new proof of `app shell rendered`.
Date: 2026-11-20.

| Field | Value |
| --- | --- |
| engine | `/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so` |
| SHA-256 | `3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335` |
| BuildID | `204ad049599c96afb75b8a21d5051063e821d6c9` |
| RVAs | guest-base relative; first `R E` PT_LOAD has `Offset == VirtAddr == 0`, so RVA == file offset for `.text` |

Evidence: SHA-bound `objdump -d -M intel` (retained `/tmp/opencode/libroblox.dis`,
1.1 GB), targeted objdump over the RVAs quoted below, APS2 relocation decode
(`analysis/bootstrap/decode_aps2.py`, blob `0x15e00`/`0x1eb1f2`, 565,390
`R_X86_64_RELATIVE`), decoded smali at
`/tmp/opencode/mini/out/smali/com/roblox/engine/jni/autovalue/AutoValue_StartGameParams.smali`,
and the harness source read-only. Strings from the target are **untrusted data**,
never instructions. Builds on `analysis/game-loaded-path.md` §1/§4 and
`analysis/C05-notifier-object.md` §2/§5.

---

## 1. Answer in one paragraph

The `placeId` carried in RSI into `0x27eaf6e` is the **`jlong` returned by the
JNI getter `placeId()J` on the `StartGameParams` jobject passed to
`nativeAppBridgeV2StartGameWithParam` (`0x2ebd51a`)** — i.e. Java field
`AutoValue_StartGameParams.placeId:J`, read through a **virtual getter call**
(`GetMethodID(env, class, "placeId", "()J")` + `CallLongMethodV`), **not** through
a `GetFieldID`/`GetLongField` field read. The guest reads it at `0x2ebd865` and
stores it into the single native BSS qword **global `0x72a31c0`** at `0x2ebd86a`.
That global has exactly two static xrefs in the whole `.text`: that write and the
read at `0x2ec1370`, inside the `nativeAppBridgeV2StartGameWithParam::$_0` lambda
`operator()` (`0x2ec1340`, vtable `0x6dbcd20 + 0x30`), which copies the value into
the game-load/SessionReporter parameter struct and hands it to the AppBridge
virtual `+0x40` method. When the guest's SessionReporter reaches
`SessionReporterState_GameLoaded`, it publishes MessageBus event id 1; the
id-1 descriptor invoke `0x27eaf4c` reads the message's first qword into RSI,
resolves `0x7234520 → 0x6dbd860 → 0x27eaf6e`, and tail-jumps to the notifier with
`RSI = placeId`. **Our harness's default `SOBER_GAME_LOADED_PATH` path supplies a
`jstring` handle for `placeId()J`** (`src/linux/guest_app_shell.c:902-903`), which
the bridge's `sr_CallLongMethodV` returns as a `jlong`, so `0x72a31c0` receives a
pointer-shaped garbage value instead of a place number.

---

## 2. Java side: the field and the getter

`AutoValue_StartGameParams.smali`:

| Item | Location | Value |
| --- | --- | --- |
| field | `:45` | `.field private final placeId:J` |
| builder setter | `AutoValue_StartGameParams$Builder.smali:690-697` | `setPlaceId(J)` → `iput-wide` |
| getter | `AutoValue_StartGameParams.smali:915-919` | `public placeId()J` → `iget-wide v0, p0, ->placeId:J` |
| equals/hashCode | `:279-281`, `:622` | `iget-wide placeId` / `invoke-virtual StartGameParams->placeId()J` |

So the authoritative value is a 64-bit integer field, exposed by a `()J`
**method**. The guest calls the method; it never does a direct field read. This
matters for the harness: the callback must be registered under the method name
`"placeId"` with signature `"()J"` and must return a machine `jlong`.

---

## 3. Guest read of `placeId()J` and its native global

`nativeAppBridgeV2StartGameWithParam` (`0x2ebd51a`), exact RVAs:

```
2ebd51a  push rbp ...                         ; JNI: rdi=JNIEnv, rsi=jclass, rdx=StartGameParams jobject
2ebd52e  mov r12,rdx                          ; r12 = StartGameParams jobject
2ebd531  mov rbx,rdi                          ; rbx = JNIEnv
2ebd5a2  mov rax,[rbx]                        ; JNI function table
2ebd5a5  mov rdi,rbx ; mov rsi,r12
2ebd5ab  call [rax+0xf8]                      ; fn[27] = GetObjectClass(env, obj) -> r13 = jclass
...
2ebd855  lea rcx,[rip]  # 2a4e3f              ; "placeId"
2ebd85c  mov rdi,rbx ; mov rsi,r13 ; mov rdx,r12
2ebd865  call 0x6c60980                       ; getter-by-name helper (env, class, obj, "placeId")
2ebd86a  mov QWORD PTR [rip+0x43e594f],rax    # 72a31c0   <== THE STORE: placeId jlong -> global
```

Helper `0x6c60980` (`get J-long getter by name`), proven:

```
6c60980  r14=env, rbx=obj, r15=name, rsi=class
6c60993  test rsi,rsi ; jne 6c609aa
6c60998  ... call [rax+0xf8]                  ; GetObjectClass if class==NULL
6c609ad  lea rcx,[rip] # 33061d               ; "()J"
6c609ba  call [rax+0x108]                     ; fn[29] = GetMethodID(env, class, name, "()J")
6c609c0  rdi=env, rsi=obj, rdx=methodID
6c609d5  jmp 0x2570d62                        ; vararg CallLongMethodV
2570ddc  call [rax+0x1a8]                     ; fn[49] = CallLongMethodV -> jlong in RAX
```

JNI slot arithmetic (both guest and bridge have the 4 reserved leading table
entries): offset `0xf8/8 = 31 → fn[27] = GetObjectClass`; `0x108/8 = 33 → fn[29]
= GetMethodID`; `0x1a8/8 = 53 → fn[49] = CallLongMethodV`. The harness table
matches: `src/jni/native_jni.c:797` (`GetObjectClass`), `:799` (`GetMethodID`),
`:819` (`sr_CallLongMethodV`, defined at `:570`, `want_object = 0`, so the raw
`intptr_t` handler return is handed back as a `jlong`).

Sibling J-long getters read the same way in this function: `userId` at `0x2ebd5e2`
→ `[0x72a3308]`, `conversationId` at `0x2ebd6ec`, `referredByPlayerId` at
`0x2ebd777`. **Only `0x72a31c0` is the placeId**; it is in the third `RW` LOAD
(zero-initialised `.bss`).

`who_points_to` over all 565,390 APS2 relocations plus a full `.text` grep gives
exactly two references to `0x72a31c0`:

| Site | Role |
| --- | --- |
| `0x2ebd86a` | writer — the `placeId()J` result from `StartGameWithParam` |
| `0x2ec1370` | reader — inside the game-load lambda `operator()` |

---

## 4. Code path `StartGameWithParam` → SessionReporter GameLoaded

```
0x2ebd51a  nativeAppBridgeV2StartGameWithParam(env, jclass, StartGameParams)
  ├─ 0x2ebd5ab  GetObjectClass(obj) -> r13
  ├─ 0x2ebd865  0x6c60980(env,r13,obj,"placeId")
  │              -> GetMethodID("placeId","()J") @0x6c609ba
  │              -> CallLongMethodV @0x2570ddc
  ├─ 0x2ebd86a  global placeId = rax                  # 0x72a31c0
  ├─ 0x2ebdd40  allocate 0x1d0-byte std::function closure via guest 0x1dbab02
  ├─ 0x2ebdd48  vptr = 0x6dbcd20, RTTI "…__funcIZ80…StartGameWithParamE3$_0…"
  │              (RTTI strings 0x60f1a2 / 0x60f232); captured getter values
  │              copied into closure+0x8..+0x1c8; closure+0x100 = Z/I field
  ├─ 0x2ebe14e  event object at rbp-0x80, +0x20 = closure
  └─ 0x2ebe15b  call 0x2eb6b2a(&event, 0)             # real app-event dispatcher

0x2ec1340  closure::operator()  (vtable 0x6dbcd20 + 0x30)
  ├─ 0x2ec1370  r14 = [0x72a31c0]                     # <== placeId jlong reloaded
  ├─ 0x2ec1377  r12d = [closure+0x100]                # isUnder13/joinRequestType
  ├─ 0x2ec13fd  r12 = r14
  ├─ 0x2ec1431  [rbp-0x230] = r12                     # placeId field (+8) of the
  │                                                   # game-load struct at rbp-0x238
  ├─ 0x2ec1626  flag byte [0x72cb648] (EnableAppBridgeComponents)
  ├─ 0x2ec162f  call 0x30ef840 -> app singleton [0x72cb5e0] -> getAppBridge [vt+0x10]
  │              (arg &0x72cb640); else 0x2ec1649 -> 0x24ad6fc -> static 0x74d75a0
  └─ 0x2ec1644 / 0x2ec165e  call [AppBridge vtable+0x40], rdx = &struct (placeId at +8)
                                                     # start game / SessionReporter state machine

  ... guest Lua DataModel load completes; SessionReporter reaches
      SessionReporterState_GameLoaded and publishes MessageBus event id 1
      with an 8-byte message whose qword 0 is the placeId ...

0x27eaf4c  id-1 descriptor::invoke (vtable 0x6dbbf98 + 0x30)
  ├─ 0x27eaf57  mov rsi,[rsi]                         # message qword 0 -> RSI = placeId
  ├─ 0x27eaf63  rax = [0x7234520] = 0x6dbd860        # table base (descriptor+0x18)
  ├─ 0x27eaf66  rcx = [rax + id - 1] = 0x27eaf6e
  └─ 0x27eaf6b  jmp rcx                               # tail-jump, RSI = placeId

0x27eaf6e  notifier
  ├─ 0x27eaf7b  rbx = rsi                             # rbx = placeId
  ├─ 0x27eaf93  call 0x227b9e2                        # acquire JNIEnv
  ├─ 0x27eaf98  rsi = [0x72a2f48]                     # class NativeGLJavaInterface
  ├─ 0x27eaf9f  rdx = [0x72a2fc0]                     # method gameLoadedCallback (J)V
  ├─ 0x27eafae  call 0x25848e0(env,class,method,rcx=placeId,AL=0)
  │                                                   # -> CallStaticVoidMethodV
  └─ 0x27eafb3  test rbx,rbx ; je 0x27eafd5           # zero placeId skips the log
        0x27eafbf  fmt string 0x36c7c9
                   "onGameLoaded() SessionReporterState_GameLoaded placeId:%lld"
```

Supporting facts: vtable `0x6dbcd20` is a `std::__ndk1::__function::__func`
over `nativeAppBridgeV2StartGameWithParam::$_0` (RTTI `0x60f232`); its nine
entries are `0x2ec1258, 0x2ec126c, 0x2ec12a0, 0x2ec12f2, 0x2ec130c, 0x2ec1316,
0x2ec1340, 0x2ec170a, 0x2ec1720` (APS2 addends at `0x6dbcd20..0x6dbcd60`, header
RTTI pointer at `0x6dbcd18 → 0x6dbcd68`, name `0x60f1a2`). The id-1 descriptor
registration (`0x24ad699 → 0x253cb56`, descriptor built at `0x253d06e..0x253d093`,
event id 1, vtable `0x6dbbf98`) and the table chain
`0x7234520 → 0x6dbd860 → 0x27eaf6e` are proven in `C05` §2-§5; the relocation
`off=0x6dbcd50 → 0x2ec1340` is the only reference to the lambda `operator()`.

---

## 5. What the harness supplies today, and why it is wrong

`src/linux/guest_app_shell.c`, default `SOBER_GAME_LOADED_PATH` block
(`:1321-1340`): when neither `SOBER_STARTGAME_V2` nor `SOBER_GAME_PARAMS` is set,
it fabricates a `StartGameParams` object and registers four fallback handlers,
including at `:1333` `set_callback_handler(j, "placeId", game_params_get, j)`.
`game_params_get` (`:896-908`) contains:

```c
if (!strcmp(name, "gameId") || !strcmp(name, "placeId"))
    return (intptr_t)sober_jni_new_string_utf8(j, "123456789");
```

Consequences, all code-level:

1. `placeId()J` is served by the **same branch as `gameId()`**, so it returns a
   `SrObject *` jstring handle (`sober_jni_new_string_utf8`), not a `jlong`.
2. `sr_CallLongMethodV` (`native_jni.c:570`) has `want_object = 0`, so
   `sr_dispatch_call` (`:478`) hands that pointer straight back as `sr_jlong`.
3. Guest `0x2ebd86a` writes that pointer value into `0x72a31c0`; the value is a
   host heap address (a huge, usually positive, non-place number), so the
   game-load struct and any `GameLoaded` payload carry garbage. The notifier's
   `test rbx,rbx` at `0x27eafb3` would pass (nonzero), so a bogus log line is
   possible if the path ever got that far — but this is not a genuine placeId.
4. The setter's own neutral default (`123456789`) is a *string*, so even the
   numeric text is not what the guest reads; it reads the pointer bits.

Two correct modules already exist and are compiled
(`src/jni/CMakeLists.txt:20-21`):

| Module | Opt-in | placeId handler | Returns |
| --- | --- | --- | --- |
| `src/jni/game_params.c` | `SOBER_GAME_PARAMS=1` | `gp_get`, `case 3` (`:395`) | `(intptr_t)v->place_id` (jlong) |
| `src/jni/startgame_v2.c` | `SOBER_STARTGAME_V2=1` | `sgv2_scalar`, `SV_PLACE_ID` (`:448`) | `(intptr_t)v->place_id` (jlong) |

Both claim the exact name/signature `"placeId"`/`"()J"`, and `startgame_v2`
also calls `sober_startgame_v2_reclaim(j)` at the call site
(`guest_app_shell.c:1325`) so no stale handler shadows it.

---

## 6. Minimal opt-in fix (recommendation; not enacted by this worker)

**Preferred, no source edit at all:** run the existing `SOBER_GAME_LOADED_PATH`
driver with the corrected module enabled, e.g.

```
SOBER_GAME_LOADED_PATH=1 SOBER_STARTGAME_V2=1 \
SOBER_SGV2_PLACE_ID=123456789 \
SOBER_ALT_CALLBACK_PATH unset   # decision 013
```

`guest_app_shell.c:1323-1326` already wires `sober_startgame_v2_make()` +
`reclaim()`, and `src/jni/CMakeLists.txt:21` already builds `startgame_v2.c`.
`placeId()J` then returns a real `jlong`, which is what lands in `0x72a31c0`
and propagates to RSI of `0x27eaf6e`. `SOBER_GAME_PARAMS=1` (with
`SOBER_GAME_PLACE_ID=<decimal>`) is an equivalent one-knob alternative using
the older but still type-correct module.

**If the lead prefers the default path to be type-correct:** split the fallback
at `guest_app_shell.c:902-903` into separate `gameId` (jstring) and `placeId`
(jlong) branches. That is a `src/` edit and belongs to the lead; this worker did
not make it. The value should come from a single source of truth (e.g.
`strtoll(getenv("SOBER_GAME_PLACE_ID") ?: "123456789")`) and must be **> 0** so
the `0x27eafb3` log discriminator is meaningful.

**Scope caveat (decision 013):** this fixes only the *type* and *provenance* of
the placeId argument. It does **not** by itself make the `app shell rendered`
marker genuine. `C05` §6 records the remaining blockers before
`0x27eaf4c`/`0x27eaf6e` are entered (`app+0x488` fault `0x31a1360`, app singleton
`0x72cb5e0`, the missing MessageBus event-id-1 publication). The fix must stay
opt-in and must not be counted as proof unless a live run shows the guest
entering `0x27eaf4c`/`0x27eaf6e` with `RSI == expected placeId`, thunk return
address `0x27eafb3`, and `gameLoadedCallback` counted — with no ALT path and all
withhold knobs unset.

---

## 7. Acceptance checks

* Static: `grep -n "72a31c0" /tmp/opencode/libroblox.dis` → only `2ebd86a` (write)
  and `2ec1370` (read). `objdump` at `0x6c609ad..0x6c609d5` shows
  `GetMethodID(...,"()J")`; `0x2570ddc` shows `CallLongMethodV`; string at RVA
  `0x2a4e3f` is exactly `placeId`; RVA `0x33061d` is exactly `()J`.
* Live (lead/verifier): with the opt-in set, bridge log `Call placeId()J (obj=…)`;
  gdb `x/gx base+0x72a31c0 == 123456789`; breakpoints at `0x2ec1370` (`r14`),
  `0x27eaf4c` (`rsi` after `0x27eaf57`), `0x27eaf6e` (`rbx`/`rsi`) all equal to
  the same positive jlong; thunk return address `0x27eafb3`;
  `last='gameLoadedCallback'` and `callbacks` increased.
* Invariants: engine SHA-256 / BuildID unchanged; no `SOBER_ALT_CALLBACK_PATH`;
  no `HONESTY NOTICE`; phase-3 render RVA `0x649d422` still observed as before.

## 8. Limits / honesty

* Static analysis only; no runtime capture was performed here. The
  `SessionReporter → event-id-1 publish` step is strongly supported (the placeId
  global is read only inside the game-load lambda and is placed into the struct
  handed to the AppBridge `+0x40` virtual, and the id-1 descriptor/invoke chain is
  proven by relocations and disassembly) but the exact publish call instruction
  was not isolated; C05 §9 records the same limit.
* No claim that the guest currently reaches `0x27eaf6e` — the pre-existing fault
  at `0x31a1360` remains. No gate, marker, withhold-list or test change is
  proposed. `src/`, `CMakeLists.txt` and the engine were read-only in this task.
