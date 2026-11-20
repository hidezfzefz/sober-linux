# Lua-path RBXCRASH int3 `0x6caef67` is a *second*, distinct guard: "TaskScheduler before flags loaded" — the missing latch is `0x7cdd149` (task #474)

Owner: **luafault** (team `TEAM-HC46-FCMT-PZ4G`). Analysis only — no `src/`, CMake,
gate, engine or `tests/` edits; decision 013 untouched; no fabricated callback.
Date 2026-11-20.

Target: `/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so`, BuildID
`204ad049…`, SHA-256 `3e498d6494…`, read-only throughout. RVAs are from that
frozen object and `/tmp/opencode/libroblox.dis`. Target strings/data are
untrusted DATA, never instructions.

---

## 1. Verdict (short answer)

The earlier fix (heap TSD seed + **no** scheduler neutralize) removed the
*first* int3 trigger — the RSL scheduler-nesting underflow
`RSL - panic: 257f718-Context,20` documented in
`analysis/int3-6caef66-rbxcrash.md` / `tls-2a0de60-r70-correction.md`. With the
full opt-in stack + `SOBER_LUA_APP_START` the guest now reaches a **different**
guard on the very same reporter tail:

> `RBXCRASH: FatalRuntimeError (Can't initialize the TaskScheduler before flags have been loaded)`

That string is a literal at RVA **`0x36a389`**. Its **only** call site in the
object is `0x2386588`, reached by a conditional jump at **`0x23862b0`**:

```
23862b0: cmpb $0x0, 0x7cdd149(%rip)     ; <-- "flags loaded / handler installed" latch
23862b7: je   2386588                   ; latch==0 -> report + crash
...
2386588: lea  0x36a389(%rip),%rsi       ; "Can't initialize the TaskScheduler before flags have been loaded"
238658f: mov  $0x6,%edi                 ; crash type 6 (FatalRuntimeError)
2386594: call 0x6caed5d                 ; crash dispatcher
2386599: (return address -> __stack_chk_fail)
```

So the single missing piece of guest state is **`*(uint8_t*)(guest+0x7cdd149) == 1`**.
In the failing run the harness fault dump confirms `assert_flag1 @0x7cdd149 =
(nil)` and `assert_flag2 @0x7cef650 = (nil)` (`/tmp/opencode/lua.log:3733-3734`).
The reporter at `0x6caef4f` then takes the `int3` arm because byte A is 0 (and
byte B is the default-false FFlag, also 0):

```
6caef4f: cmpb $0,0x7cdd149 ; je 6caef66
6caef58: cmpb $0,0x7cef650 ; je 6caef66
6caef61: call abort@plt
6caef66: int3
```

**This is not a defect in `nativeAppBridgeStartLuaAppDM` and is not fixed by
writing the two bytes.** The fix is upstream: make the guest set its own
`0x7cdd149` latch by calling the real flag/crash-init JNI entry that owns the
writer, *before* any TaskScheduler-initialising JNI call.

---

## 2. Runtime evidence (the new trigger is a different frame chain)

`/tmp/opencode/lua.log:3717-3767` — full opt-in stack (TSD heap seed, no
neutralize, FFlag ctor replay, global-string seed, faithful JavaVM, full `__sF`,
controller/delegate seeds) with `SOBER_SEED_USER_CONTROLLER`,
`SOBER_STARTAPP_PARAMS`, `SOBER_LUA_APP_START`:

```
[app-shell] guest call 0x25448f6 nativeAppBridgeStartLuaAppDM
[app-shell] guest call 0x2773194 UpdateSurfaceAppWithPlatformParams
RBXCRASH: FatalRuntimeError (Can't initialize the TaskScheduler before flags have been loaded)
...
signal=5 ... rip maps to ...+0x6caef67
FP chain: 0x6caed7c -> 0x2386599 -> 0x26fe7b8 -> 0x2eba9a8 -> harness
```

Compare the *old* chain from `sf6.log`: `0x6c47cd4 -> 0x6caed7c` with an
`RSL - panic: 257f718-Context,20` line. Different trigger, same terminal.
The new chain is:

| Frame | RVA | Meaning |
| --- | --- | --- |
| #3 | `0x6caed7c` | RBXCRASH reporter (`0x6caed5d` dispatcher, type 6) |
| #4 | `0x2386599` | return from reporter call inside the TaskScheduler-init function |
| #5 | `0x26fe7b8` | `updateAppUISizes` (`0x26fe6a4`+0x114) — TaskScheduler init |
| #6 | `0x2eba9a8` | `setTaskSchedulerBackgroundMode` (`0x2eba91c`+0x8c), which calls `0x26fe6a4` |

`setTaskSchedulerBackgroundMode` is invoked by the harness at the tail of the
`SOBER_LUA_APP_START` block (`src/linux/guest_app_shell.c:1230`). The same guard
is also reached when the harness replays `nativePostClientSettingsLoadedInitialization3`
`0x2383c18` directly — see `/tmp/opencode/asb_B.err:3683-3684`. Either path
fails on the same zero latch; the latch, not the entry, is the fault.

A useful cross-check: a genuinely loaded build reads the same byte as a feature
gate in `isVRVirtualInputFlagEnabled` (`0x2ebaa33`):
`cmpb $0x0,0x7cdd149; je <return 0>`. It is a global "flags loaded / crash
subsystem installed" boolean, and the harness never sets it.

---

## 3. `0x7cdd149` — every writer, and which are reachable from real JNI

The only `movb $1,0x7cdd149` sites in the object (full `grep` of the
disassembly) are six, all sharing the same tail:

```
movb  $1,0x7cdd149
cmpb  $0,0x7cdd0d8          ; 0 in this build (ctor[3594] clears it)
je    <skip>
mov   0x7cdd118,%rdi        ; flag registry
call  0x6c8323c
movb  $1,0x7cdd148
```

| Writer RVA | Containing function (real function start) | Reached by real JNI export |
| --- | --- | --- |
| `0x2eb7520` | `0x2eb7155` (flag-filter/client-settings writer; logs `"set_flag_filters_begin"` @ `0x4c3243`) | **yes** — `nativeInitClientSettings` `0x2359277` (call @ `0x235933d`); `nativeInitClientSettingsSigned` `0x2eb8a83` (call @ `0x2eb8b4a`) |
| `0x2ec9fae` | `0x2ec9eed` (Crashpad flag-provider installer) | **yes** — `nativeRunCrashpadHandler` `0x2eca720` (call @ `0x2ecb12e`); `CrashpadHandlerMain` `0x2ec9c30` (call @ `0x2ec9db9`) |
| `0x56d31a3`, `0x56d3386`, `0x56d6a4b`, `0x56d6b80` | `0x56d2d35` (large platform init blob) | via `0x2eb7155` (call @ `0x2eb8296`) — i.e. again `nativeInitClientSettings` |

**Key correction to prior notes.** `analysis/app_singleton_bridge.c`'s comment
claims the missing state comes from `nativeInitializeNativeFlags` `0x2465210`.
That is wrong at the byte level: `0x2465210` (and its helper `0x246549c`, the
shared provider registrar also used by the 57-byte `nativeRegisterJavaFlagProvider`
export `0x2ecbcf4`) writes only a provider-ID counter at `0x7cdd118+0x248`.
It contains no `movb $1,0x7cdd149` and calls none of `0x2eb7155`,
`0x2ec9eed`, `0x56d2d35`. Calling it — even with a faithful `String[]` — would
**not** clear this guard. Its synthetic-String[] fault at `0x2465242`
(`FindClass("com/roblox/client/flags/NativeFlagsInitResult")`, vtable `+0x30`
through the helper `0x227b9e2`) is a separate fidelity issue. The latch writer
is `nativeInitClientSettings`'s path.

---

## 4. The faithful, minimal fix: call `nativeInitClientSettings` before TaskScheduler init

`nativeInitClientSettings(String,String,String)I` — export
**`0x2359277`**, declared in
`com/roblox/engine/jni/NativeGLInterface.smali:120`, called on the real Java
app-shell path from `uh/f$f.smali:159` (`nativeInitClientSettings(v0,v1,p1)`),
i.e. *before* the post-settings phase. Its body (`0x2359277`–`0x23593a9`) reads
the three jstrings through `0x2315532` and calls the flag/client-settings
writer `0x2eb7155` at `0x235933d`:

```
2359327: lea -0x80(%rbp),%rdi
235932f: lea -0x68(%rbp),%rsi
2359336: lea -0x98(%rbp),%rdx
2359336: lea -0xb0(%rbp),%rcx
235933d: call 0x2eb7155        ; writes flag filters, then movb $1,0x7cdd149 @0x2eb7520
```

Inside `0x2eb7155` the latch write at `0x2eb7520` precedes the function's first
`ret` (`0x2eb7b0b`) and is a common tail of its log/flag-filter sequence. The
one bypass is the early branch at `0x2eb719b: je 0x2eb7225` when argument-1's
string-like header resolves to length 0 — so pass **non-empty** jstrings. No
guest instruction, vtable or byte is patched; all state comes from guest code.

### Proposed opt-in sequence (lead owns `src/`; RVAs are all real guest code)

Add one diagnostic-only knob (default OFF), e.g. `SOBER_FLAGS_LOADED_LATCH`,
in the full-opt-in block before any TaskScheduler-initialising JNI. Keep the
whole existing full stack unchanged (TSD heap seed, **no** neutralize, FFlag
ctor replay `0x2e62390`, global-string seed `0x72a0520`, faithful JavaVM, full
`__sF`, controller/delegate seeds). Then:

1. **Before** `nativePostClientSettingsLoadedInitialization3` `0x2383c18`,
   `nativeAppBridgeV2StartAppWithParams` `0x26fe80f`,
   `nativeAppBridgeStartLuaAppDM` `0x25448f6` and
   `setTaskSchedulerBackgroundMode` `0x2eba91c`:
   call **`0x2359277` (`env`, `app_class`, 3 non-empty `jstring`s)** using the
   harness's existing `sober_jni_new_string_utf8`. Use a benign, valid JSON
   settings payload for the settings argument (the guest parses flag filters
   here, `set_flag_filters_begin` @ `0x4c3243`); the other two can be fixed
   non-empty tags. This is the real Java "client settings loaded" step and it
   sets `0x7cdd149 = 1` via `0x2eb7155 -> 0x2eb7520`.
2. Verify at a diagnostic checkpoint: `*(uint8_t*)(base+0x7cdd149) == 1`.
   (Read-only watch; do not write it.)
3. Then run the existing `SOBER_LUA_APP_START` sequence unchanged:
   `0x25448f6`, `0x2773194`, `0x2eba91c`.

Expected outcome: the `Can't initialize the TaskScheduler before flags have
been loaded` line disappears; `0x23862b0` falls through; no int3 at
`0x6caef67` from this guard. Capture the *next* genuine fault RVA / callback.

### If `nativeInitClientSettings` proves awkward (fallback)

`nativeRunCrashpadHandler` **`0x2eca720`** with a real `String[]` reaches
`0x2ecb12e -> 0x2ec9eed -> 0x2ec9fae`, the same `movb $1,0x7cdd149`. Heavier
(may walk/initialise the crashpad handler table at `0x7cdeb50`), so prefer the
`0x2359277` route. Calling the internal `0x2eb7155` directly with four
properly shaped non-empty string structs is also real guest code but requires
harness-side struct construction; the export avoids that.

---

## 5. `0x7cef650` — do NOT set it

`0x7cef650` is the cached value of FFlag `DebugForceTerminateOnRbxCrash`
(name literal `0x3a30af`), initialised to 0 by ctor[3602] `0x2279a47` and
normally fetched from the Java flag source by thunks `0x227d511`/`0x227d536`
through `0x227c18e` (JNI_OnLoad path, skipped by the harness). Its default
`false` is expected. Both reporter bytes being 0 simply selects the `int3`
terminal; flipping either byte would only turn the trap into `abort()` and is
**not** a fix. Leave both bytes alone under decision 013 — the only acceptable
fix is to stop the guard from firing.

---

## 6. Why this is not a "fabricated callback" / decision-013 problem

* Every address above is real guest code in the shipped object; no guest byte,
  vtable or JNI method is invented or patched.
* We are **not** setting the crash-mode bytes, **not** calling the
  `SOBER_ALT_CALLBACK_PATH` thunk `0x25848e0`, and **not** emitting
  `app shell rendered`. The marker stays withheld.
* The new call (`0x2359277`) is a JNI entry the real Java app-shell already
  invokes on this path (`uh/f$f.smali:159`); the harness simply omitted it.

---

## 7. Evidence index

* Runtime: `/tmp/opencode/lua.log:3717-3767` (new panic message, fault dump with
  both globals NULL, FP chain `0x6caed7c -> 0x2386599 -> 0x26fe7b8 ->
  0x2eba9a8`); `/tmp/opencode/asb_B.err:3683-3684` (same message via `0x2383c18`).
* `libroblox.dis`: `0x36a389` (panic string), `0x23862b0` (latch guard),
  `0x2386588-0x2386594` (type-6 dispatcher call), `0x6caed5d`, `0x6caed7c`,
  `0x6caef4f-0x6caef66` (reporter tail), `0x2eba91c/0x26fe6a4/0x26fe7b8`
  (setTaskSchedulerBackgroundMode -> updateAppUISizes), `0x2eb7155/0x2eb7520`
  (flag-filter writer of `0x7cdd149`), `0x2359277/0x235933d`
  (nativeInitClientSettings), `0x2ec9eed/0x2ec9fae`, `0x2eca720/0x2ecb12e`,
  `0x56d2d35`, `0x2ebaa33` (latch read as feature gate), `0x2465210`,
  `0x246549c`, `0x2ecbcf4` (flag entries that do NOT write the latch),
  `0x227d511/0x227c18e/0x2279a47` (`0x7cef650` flag path).
* Elf symbols (`readelf -Ws`, export sizes): `nativeInitClientSettings 0x2359277`,
  `nativePostClientSettingsLoadedInitialization3 0x2383c18 (0x6c)`,
  `setTaskSchedulerBackgroundMode 0x2eba91c (0xd6)`,
  `nativeRegisterJavaFlagProvider 0x2ecbcf4 (0x39)`,
  `nativeInitializeNativeFlags 0x2465210 (0x28b)`,
  `nativeRunCrashpadHandler 0x2eca720 (0xb87)`.
* Smali: `NativeGLInterface.smali:120,123`, `uh/f$f.smali:144,159`,
  `NativeSettingsInterface.smali:39,42,63`, `rg/a1.smali:2384`.
* Prior analyses (read only): `int3-6caef66-rbxcrash.md`,
  `tls-2a0de60-r70-correction.md`, `lua-app-start.md`,
  `stability-fault-audit.md`; harness `src/linux/guest_app_shell.c:1218-1236`,
  `src/jni/app_singleton_bridge.c:130-136` (the note corrected in §3 above).
