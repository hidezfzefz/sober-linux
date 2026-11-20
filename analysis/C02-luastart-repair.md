# C02 — `SOBER_LUA_APP_START` / `0x25448f6`: exact pre-state and minimal real-guest call order

Worker: **C2 (logical Team C)**. Scope: `analysis/C02-luastart-repair.md` only.
No `src/`, CMake, gate, test or engine edits. Engine (read-only throughout):
`/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so`, BuildID
`204ad049…`, SHA-256 `3e498d6494…`. Decision 013 stays untouched; no byte
patch, no synthetic callback, no fake vtable, no guest instruction change.
All RVAs are AppImage-build only. Date 2026-11-20.

Evidence classes: **OBSERVED** = bytes/decoded smali/harness log; **DERIVED** =
APS2 relocation or call-graph inference; **UNKNOWN** = not established.

---

## 1. Verdict — three corrections to the task framing

1. **The trap is not raised by `0x25448f6`.** In `lua.log` the call to
   `nativeAppBridgeStartLuaAppDM` (`0x25448f6`) **returns normally** (log line
   3717). The two following calls also return: `0x2773194` is logged at 3718.
   The RBXCRASH line (3719) and the fault (3721-3758) occur inside the **third
   call of the `SOBER_LUA_APP_START` block**, `setTaskSchedulerBackgroundMode`
   (`0x2eba91c`, `src/linux/guest_app_shell.c:1258`) — its own success log line
   is never printed. Frame `#6 = guest+0x2eba9a8` is the instruction *immediately
   after* the `call 0x26fe6a4` at `0x2eba9a3` inside that export. OBSERVED.
2. **The panic is the "flags not loaded" guard, not the RSL scheduler panic of
   #464.** `lua.log:3719` reads
   `RBXCRASH: FatalRuntimeError (Can't initialize the TaskScheduler before flags
   have been loaded)`. That literal is at RVA **`0x36a389`** (OBSERVED, raw
   string read), passed as `rsi` to crash dispatcher `0x6caed5d` with
   `edi = 6` at `0x2386594`, inside `0x2385fa6`. The guard is
   `0x23862b0: cmp BYTE PTR [0x7cdd149],0 ; je 0x2386588`. **The single byte
   `0x7cdd149 == 0` is the whole cause.** The earlier `257f718-Context,20` RSL
   panic (`int3-6caef66-rbxcrash.md`) is a different, later fault.
3. **`0x7cef650` is not part of the fix.** It is only read by the reporter tail
   (`0x6caef58`) to choose `int3` vs `abort()`; writing it would merely turn
   the trap into `abort()`, which is pointless and is a forbidden byte patch.
   `0x7cdd149` must become 1 via **real guest code**, not by a `movb`.

### 1.1 Exact proven fault chain (OBSERVED, `lua.log` + disassembly)

```
harness: call base+0x2eba91c (env,class,false,"sober-native")
  0x2eba91c setTaskSchedulerBackgroundMode
    0x2eba9a3: call 0x26fe6a4            ; edi = (background != 0) = 0
      0x26fe7b3: call 0x2385fa6          ; lazy TaskScheduler/AppUI singleton build
        0x23862b0: cmp byte [0x7cdd149],0
        0x23862b7: je 0x2386588          ; <-- LATCH == 0
          0x2386588: lea rsi,[0x36a389]  ; "Can't initialize the TaskScheduler..."
          0x238658f: mov edi,6
          0x2386594: call 0x6caed5d      ; crash dispatcher, type 6
            0x6caed7c: RBXCRASH reporter
              0x6caef4f: cmp byte [0x7cdd149],0 ; je 0x6caef66
              0x6caef58: cmp byte [0x7cef650],0 ; je 0x6caef66
              0x6caef61: call abort
              0x6caef66: int3            ; <-- SIGTRAP, rip = guest+0x6caef67
```

Fault record: `signal=5`, `rip=…+0x6caef67`, `assert_flag1 @0x7cdd149 = NULL`,
`assert_flag2 @0x7cef650 = NULL` (`lua.log:3723,3735,3736,3739`). FP chain
`0x6caed7c → 0x2386599 → 0x26fe7b8 → 0x2eba9a8` (`lua.log:3753-3757`)
matches the call graph exactly.

---

## 2. Global `0x7cdd149` — the "flags loaded" latch (the actual gate)

**Correction to `int3-6caef66-rbxcrash.md §4`:** that document called
`0x7cdd149` a "crash-handler installation state bit". That is only half the
story. It is written with `movb $1` at the **end of the native flag/client-
settings loaders**, immediately followed by the optional crash-handler
install (the same `0x7cdd0d8` / `0x6c8323c` / `0x7cdd148` triplet). Its real
role in this path is the **"flags have been loaded" latch** consumed at
`0x23862b0`.

### 2.1 All direct byte writers (OBSERVED, `movb $1, [0x7cdd149]`)

| Writer | Real guest owner | Reachable from a JNI export the harness can call? |
| - | - | - |
| **`0x2eb7520`** | tail of `0x2eb7155` = shared body of `nativeInitClientSettings` (`0x2359277`) / `…Signed` (`0x2eb8a83`) / `…Cached` (`0x2eb8d87`) / `…CachedCompressed` (`0x2eb8ff7`), called from `uh/f$f.smali:144,159` | **YES — best choice.** No `ret` exists between `0x2eb7155` and `0x2eb7520`, so every normal path sets the latch. |
| `0x2ec9fae` | `CrashpadHandlerMain+0x3a1` | only via crashpad main; not a normal app JNI entry |
| `0x56d31a3`, `0x56d3386` | NativeDM state machine `0x2ee2766` (the `areFlagsLoaded` logger at `0x332a03` / `0x56d35b`) | conditional (`cmp QWORD [rbp-0xe8],0` at `0x56d3199`); reached from `nativeRetryInit` `0x2edd739` → `call 0x2ee2766` at `0x2edd760`, but contingent on guest state |
| `0x56d6a4b`, `0x56d6b80` | sibling crash-handler setup | not normal JNI |

### 2.2 `0x2eb7155` is the clean, unconditional writer (OBSERVED)

```
2eb7155: push rbp; ... ; sub rsp,0x378
2eb7188: movzx eax, BYTE PTR [rdi]          ; arg0 = std::string (JNI jstring already converted)
2eb71a1 / 2eb7225:  two alternative parse paths (both converge at 0x26fe7b3 region)
2eb74f1: ... release shared_ptr ...
2eb7520: mov BYTE PTR [0x7cdd149],1         ; <-- FLAGS LOADED
2eb7527: cmp BYTE PTR [0x7cdd0d8],0
2eb752e: je 2eb7543
2eb7537: call 0x6c8323c                     ; optional crash-handler install
2eb753c: mov BYTE PTR [0x7cdd148],1
```

Literal evidence the function is flag loading: it logs
`set_flag_filters_begin` (`0x4c3243`), `set_flag_filters_end` (`0x3deaa9`),
`parse_ixp_cache_end` (`0x26df4d`) through `0x232327c`.
**No `ret` occurs before `0x2eb7520`** (checked over `0x2eb7155..0x2eb751f`),
so a normal call always sets the latch. `0x235a58c` (`nativeInitClientSettings+0x1315`)
does the actual parse before the latch write.

**Minimal real-guest call that sets it:** `nativeInitClientSettings`
(`0x2359277`, signature `(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)I`
from `NativeGLInterface.smali:120`) with three valid Java `String` handles, or
`nativeInitClientSettingsSigned` (`0x2eb8a83`, four strings, smali:123). Both
are genuine JNI exports called on the real app by `uh/f$f` before the app-shell
start. DERIVED: any string content reaches the latch; UNKNOWN whether the
harness's synthetic `jstring` survives the `0x2315532` JNI→std::string
conversion for *this* export (it did survive at `0x2eba91c`, so the bridge
conversion itself works). Verify `0x7cdd149 == 1` after the call.

### 2.3 `0x7cef650` — reporter-only, do not touch (OBSERVED)

Only three xrefs exist in the whole image:

| RVA | Role |
| - | - |
| `0x2279a8f` | ctor[3602] writes the default `0` (FFlag `DebugForceTerminateOnRbxCrash`, name `0x3a30af`) |
| `0x227d51f` | `JNI_OnLoad+0x25b7` passes it as the out-byte to getter tail `0x227c18e` (real Java-backed flag source at `0x72a0cf8`, vtable `+0x38`) |
| `0x6caef58` | RBXCRASH reporter read |

The harness skips `JNI_OnLoad`, so it stays `0` — expected, not a defect. With
`0x7cdd149 = 1` set by §2.2, the panic disappears and the reporter is never
entered, so the `0x7cef650` value is irrelevant. **Do not set either byte
directly.**

---

## 3. TaskScheduler state that must be real

Two independent scheduler issues exist; fix both in this order.

### 3.1 The "flags loaded" precondition (immediate blocker)

`setTaskSchedulerBackgroundMode` (`0x2eba91c`) → `0x26fe6a4` lazily builds the
0x208-byte AppUI/TaskScheduler object via `0x2385fa6` (alloc `0x1dbab02`,
`movsd` fields, `memset`). That builder hard-requires `0x7cdd149 != 0`. With
§2.2 satisfied, `0x26fe6a4` proceeds: it stores the object into global
`0x7cd3f68`, marks `0x7cd3f70`, and returns. Only then call `0x2eba91c`.
OBSERVED.

### 3.2 The RSL liveness word — TSD, not a byte at `0x722e540`

Correction from `tls-2a0de60-r70-correction.md`: `guest_base+0x722e540` is a
**Bionic TLS key-descriptor**, not the scheduler singleton. The real per-thread
nesting word is reached by:

```
0x2a0de60 (TLS accessor):
  ... call 0x2ea6440(key_desc 0x722e560)  ; lazy init once
  ... call 0x2ea6440(key_desc 0x722e540)
  2a0deba: add $0x410,%rax                ; TLS_BLOCK + 0x410
  2a0dec1: ret
0x2ea6440:
  2ea6455: mov 0x72a1b08,%edi             ; global pthread_key_t = 3
  2ea645b: call pthread_getspecific@plt   ; GOT 0x72298b0
  2ea6573: mov 0x8(%r14,%r13,8),%r15      ; per-thread slot
```

The RSL assert at `0x2a13315` (`test %r12,%r12`, packed word at `+0x70` of that
block) must see **nonzero**. In the harness the guest's `pthread_getspecific`
GOT (`0x72298b0`) resolves to glibc; key 3 returns the phase-1 stack guard
`0xabcdef0123456789` or NULL, so the assert handler at `0x727e910` (→ RSL
panic `0x6c47c52`) fires `257f718-Context,20` and reaches the same reporter
tail. **Real fix:** bind that GOT to the Bionic-compatible TSD shim (key 3 →
per-thread allocation) so the guest's own lazy-init `0x2a0ded0` populates
`+0x410/+0x70`. This is a runtime compatibility binding, not a guest byte patch
and not a callback. `SOBER_SCHED_R70_KEEP`/`NEST_ONE` on `0x722e540` are proven
ineffective (`r70.log`, `nest.log`) — do not rely on them.

---

## 4. App singleton / controller / delegate state (what `0x25448f6` is for)

`0x25448f6` (export, `()V`, size `0xf9`) is a guard/dispatch entry; its real
delegate installs happen deeper (`0x25d7e75`, `0x25f7e7e` write the delegate
into `app+0x408`, `lua-app-start.md §3.3`). For those writes to be *useful* and
for the flag-enabled branch not to fault, three pieces of state must exist:

| State | Real guest producer | Verify |
| - | - | - |
| FFlag `EnableAppBridgeComponents` = 1 at **`0x72cb648`** | `nativePreloadFlagOverrides` `0x2edd859` with JSON `{"EnableAppBridgeComponents":true}` (grammar `appstate_fields.md` Addendum A), then `nativeRetryInit` `0x2edd739` → `0x2ee2766` to apply | `*(uint8*)(base+0x72cb648) == 1` |
| App singleton at **`0x72cb5e0`** | `nativePostClientSettingsLoadedInitialization3` `0x2383c18` with a **real** `java/util/List` | `*(void**)(base+0x72cb5e0) != 0`; without it the flag=1 path faults at `0x2ec1042` |
| User controller at **`app+0x440`** | vtable slot `0x6dbc7f8 → 0x24ad50a`, installed only when `0x72cb648 != 0` (`0x24ad531`); needs app object from the singleton | `app+0x440 != NULL` (`SOBER_APP_SLOT_DIAG`) |
| Delegate at **`app+0x408`** | `0x25448f6` → `0x25d7e75` / `0x25f7e7e`; alternative flag=0 writer `0x24aed3e` in `InitWithParams` is skipped when the flag is 1 | `app+0x408 != NULL` |

Ordering constraint from `stability-fault-audit.md`: the FFlag-ctor replay
`0x2e62390→0x2e618b0→0x2e61280→0x2e5bc10→0x2e7b850` must run before any
postsettings replay, or registry `0x72a0520` has a NULL control block and faults
at `0x2e7b8e2`.

---

## 5. Minimal opt-in call order — only real guest code

Everything below is a real guest export or a guest-owned helper. No guest byte
is patched; no JNI method body is fabricated; no callback is triggered by the
harness. Steps 0-2 are already on the default path (kept for ordering clarity).

| # | Call (RVA) | Purpose / verification |
| - | - | - |
| 0 | `nativeSetBaseDataDirectories` `0x24650a9`, `nativeSetRobloxVersion` `0x232d484`, `nativeSetRobloxChannel` `0x2326e2e`, `nativeGameGlobalInit` `0x233762c` | already present (`guest_app_shell.c:1098-1082`) |
| 1 | `nativePreloadFlagOverrides` **`0x2edd859`** with `{"EnableAppBridgeComponents":true}`, then `nativeRetryInit` **`0x2edd739`** | sets `0x72cb648 = 1`; verify. Must precede app-singleton and controller paths |
| 2 | `nativeInitClientSettings` **`0x2359277`** (3 jstrings) — or `…Signed` `0x2eb8a83` (4) | **the decisive fix**: `0x2eb7155 → 0x2eb7520` sets `0x7cdd149 = 1`. Verify byte != 0 before step 6. Alternative real writers: `nativeRetryInit`'s state machine (`0x56d31a3`/`0x56d3386`) if its preconditions hold |
| 3 | `nativePostClientSettingsLoadedInitialization3` **`0x2383c18`** with a genuine `java/util/List` | populates singleton `0x72cb5e0`; avoids `0x2ec1042`. Needs real List (synthetic one faults at `0x2e5d42a`) — UNKNOWN if bridge can supply it |
| 4 | Bionic TSD binding: guest `pthread_getspecific` GOT **`0x72298b0`** → shim, key from **`0x72a1b08`** (=3) | makes `+0x410/+0x70` of the per-thread block real; prevents RSL panic `0x2a13329` |
| 5 | `nativeAppBridgeV2InitWithParams` **`0x24abf96`** (faithful params), `nativeAppBridgeAppStart` **`0x247dc8d`**, `nativeAppBridgeV2StartAppWithParams` **`0x26fe80f`** (all 10 typed getters, `SOBER_STARTAPP_PARAMS`), `nativeScheduleOnFirstFrame` **`0x2ebf950`** | establishes the app object and its fields |
| 6 | `setTaskSchedulerBackgroundMode` **`0x2eba91c`** `(false, tag)` | **now safe** — `0x7cdd149 == 1`; completes `0x26fe6a4 → 0x2385fa6` |
| 7 | `nativeAppBridgeStartLuaAppDM` **`0x25448f6`** `(env, class)` | reaches `0x2eb6b2a → 0x2337cc8` and installs `app+0x408` at `0x25d7e75`/`0x25f7e7e` |
| 8 | `nativeAppBridgeV2UpdateSurfaceAppWithPlatformParams` **`0x2773194`** with real `PlatformParams` (`dpiScale`, viewport mm) | real shell surface |
| 9 | Observe `app+0x408`, `app+0x440` via `SOBER_APP_SLOT_DIAG`; wait for guest `gameLoadedCallback(J)V` notifier `0x27eaf6e` + render `0x649d422` | the only decision-013-qualifying evidence |

**Absolute minimum to stop this specific int3:** step 2 (set `0x7cdd149` via
`0x2359277`) *before* the current `0x2eba91c` call at
`guest_app_shell.c:1258`. Steps 1, 3, 4, 5 are required to make the rest of the
`SOBER_LUA_APP_START` block produce genuine app state rather than just avoid
the trap.

### 5.1 What is explicitly NOT a fix

* `movb $1, [0x7cdd149]` / `movb $1, [0x7cef650]` — direct byte patches
  (forbidden; `0x7cef650` only switches `int3`→`abort`).
* Zeroing or "keeping" `+0x70` at `0x722e540` — wrong object (TLS key desc).
* `SOBER_ALT_CALLBACK_PATH` thunk `0x25848e0` — harness-triggered, decision 013.
* Emitting `app shell rendered` from any of the above — `SOBER_LUA_APP_START`
  is in the `seeded_state` withhold list (`guest_app_shell.c:1355`), so the
  marker stays force-withheld regardless.
* Treating `onAppBridgeNotification(String,String)` (`0x72a3028 → 0x3dea91`)
  as app-shell proof — explicitly rejected in decision 013.

---

## 6. Residual unknowns (honesty)

1. UNKNOWN whether the harness's synthetic `jstring` is accepted by the
   `0x2315532` conversion and `0x235a58c` flag parser for `0x2359277`; if it
   faults, the fallback is `nativeRetryInit` `0x2edd739` (writers
   `0x56d31a3`/`0x56d3386`) with real flag state. Both remain real guest code.
2. UNKNOWN whether the real `java/util/List` for step 3 can be supplied by the
   current JNI object model; the synthetic one faults at `0x2e5d42a`.
3. UNKNOWN whether the virtual dispatch from `0x2eb6b2a → 0x2337cc8` actually
   invokes driver `0x25449fc` (slot `+0x30` of vtable `0x6dbc9a0`) on the JNI
   path; confirm before claiming the delegate writes execute.
4. No guest-driven frame has been observed. This document describes the
   *required state and call order*, not a successful app-shell render.

---

## 7. Evidence index

* `lua.log:3677-3758` — full opt-in run; lines 3717-3719 (last three calls and
  panic), 3723/3735/3736 (fault bytes), 3742-3757 (frame chain).
* `libroblox.dis` / direct `objdump`:
  `0x25448f6`, `0x25449fc`, `0x25d7e75`, `0x25f7e7e`, `0x24ad50a`,
  `0x24aed3e`, `0x2383c18`, `0x2edd859`, `0x2edd739`, `0x2359277`,
  `0x2eb8a83`, `0x2eb7155`, `0x2eb7520`, `0x2eba91c`, `0x26fe6a4`,
  `0x2385fa6`, `0x23862b0`, `0x2386588`, `0x6caed5d`, `0x6caed7c`,
  `0x6caef4f-0x6caef66`, `0x2279a8f`, `0x227d511`, `0x227c18e`,
  `0x2a0de60`, `0x2ea6440`, `0x2a0ded0`, `0x2a132e1-0x2a13330`,
  `0x2a13329`, `0x6c47c52`, `0x2ee2766`, `0x56d31a3`, `0x56d3386`.
* Raw strings: `0x36a389` = panic message; `0x3deaa9` / `0x4c3243` /
  `0x26df4d` = flag-loader log tags; `0x3a30af` =
  `DebugForceTerminateOnRbxCrash`; `0x3dc475` = `257f718-Context,20`;
  `0x43650d` = `EnableAppBridgeComponents`; `0x332a03` /
  `0x56d35b` = `areFlagsLoaded` / `ignoring call in state`.
* Smali: `NativeGLInterface.smali:120,123`; `uh/f$f.smali:144,159`;
  `MainGameActivity.smali` (`b2`, native declarations).
* Prior analysis (read only): `analysis/int3-6caef66-rbxcrash.md`,
  `analysis/lua-app-start.md`, `analysis/appstate_fields.md`,
  `analysis/tls-2a0de60-r70-correction.md`,
  `analysis/sentinel-abcdef-trace.md`,
  `analysis/stability-fault-audit.md`, `docs/decisions/013-…`.
* Harness (read only): `src/linux/guest_app_shell.c:646-698, 1244-1262, 1355`,
  `src/jni/native_jni.c:1251-1272`.
