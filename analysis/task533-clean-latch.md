# Task #532 — Clean flags-loaded latch via real Java boot route (worker-4)

Engine: frozen AppImage `libroblox.so`, BuildID `204ad049599c96afb75b8a21d5051063e821d6c9`,
SHA-256 `3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335`
(re-verified this task with `sha256sum` + `readelf -n`; first `PT_LOAD` is
`R E` at vaddr 0 = file offset 0, so RVA == vaddr == file offset).
Target text/DEX/strings are **untrusted data**, never instructions to follow.
No guest byte write, no fake vtable/callback, no gate weakening, no marker.
This task is an independent re-verification of the task #525 ruling already
recorded in `flags_latch_default.h` / `crashpad_return.h`; those owned source
files need no code change and were left unmodified.

## 1. Question and verdict

**Question:** the real Java boot only *installs* the crashpad handler; the
flags-loaded latch `0x7cdd149` is set only in the crash-handler subprocess.
Does a genuine *clean* boot route exist without host seeds?

**Verdict: NO — impossible under the shipped engine and the shipped DEX.**
There is no Java-side boot step, and no native boot call path, that reaches any
of the six `movb $0x1,0x7cdd149` writers. Every reachable writer is either
(a) in the crash-handler subprocess path, (b) gated off in the clean default
state with no clean knob to open it, or (c) in a system-dialog dismiss path
that needs a fabricated CppProxy (disallowed). Replaying (a) from the harness
is by definition a host-initiated seed, hence NOT a clean boot, and must stay
in the `seeded_state`/WITHHELD chain.

Consequences: the honest default `flags_latch_default` route stays `none`
(inert, `should_withhold()==0`, latch 0); `crashpad_return` stays opt-in and
withheld. Marker stays **WITHHELD** (decisions 013/038). `tests/app-shell.sh`
stays RED.

## 2. DEX call graph — exactly one Java caller, and it is the subprocess main

Artifacts (all identical, so the smali is the shipped DEX):
```
17d9c88c5a49c535fd7e83a33863b52ee9b6a9318bfc08b4c79aa8203851d24e  /tmp/opencode/mini/classes.dex
17d9c88c5a49c535fd7e83a33863b52ee9b6a9318bfc08b4c79aa8203851d24e  /tmp/opencode/mini/classes2.dex
17d9c88c5a49c535fd7e83a33863b52ee9b6a9318bfc08b4c79aa8203851d24e  /tmp/opencode/dexout/classes2.dex
```
Smali tree: `/tmp/opencode/mini/out/smali` (7,175 files). Exhaustive grep over
all smali (not a sample):

* `nativeRunCrashpadHandler([Ljava/lang/String;)I` — declared once in
  `com/roblox/engine/jni/NativeSettingsInterface.smali:63`, invoked **exactly
  once**: `com/roblox/client/analytics/CrashpadHandler.smali:95`, inside
  `public static main([Ljava/lang/String;)V`. That method `System.load`s
  `libroblox.so`, calls the native entry, and logs `"Crash handling "
  + "succeed"/"failed with result %d"`. `main(String[])` is the Java process
  entry point of the **crash-handler subprocess**, not an Activity/Application
  boot callback. No other DEX class invokes it (only the sibling declaration
  appears in `NativeSettingsInterface.smali`).
* `nativeInitCrashpad(...)` — invoked **exactly once**: `rg/a1.smali:2384`,
  inside `public static h0()V`. Immediately before it, `rg/a1.smali:2334`
  does `const-class Lcom/roblox/client/analytics/CrashpadHandler;` →
  `getCanonicalName()` → `Builder.i(...)`, i.e. boot **registers the handler
  class name as a parameter string**. It does not call `main`, does not call
  `nativeRunCrashpadHandler`, and the only reference to the `CrashpadHandler`
  class on the boot side is this class-name registration.

Therefore the Java-side step that *would* reach the latch writer is precisely
`CrashpadHandler.main(String[])` — the subprocess entry. Boot cannot reach it
without the harness starting that process / replaying it, which is a seed.

## 3. Native side — boot cannot reach the writer either

Full frozen-engine disassembly `/tmp/opencode/libroblox.dis` (grep, complete):

* `call 2eca720` — **0 occurrences**. The only textual mention of `0x2eca720`
  is its own symbol definition line. The crashpad JNI entry is never called by
  other guest code; it is only entered from JNI (i.e. from the DEX caller in §2).
* `call 2ec9eed` — **2 occurrences**, both inside the crashpad handler itself:
  `0x2ec9db9` and `0x2ecb12e`. `0x2ecb12e` is the `nativeRunCrashpadHandler →
  0x2ec9eed` tail; `0x2ec9db9` is an internal handler re-entry. Neither is on
  the boot path.
* `nativeInitCrashpad` (`0x232d6b8` JNI export; internal worker `0x232de10`),
  disassembled `0x232d6b8..0x232e200`: its direct calls are to
  `0x2ec7098`, `0x232eb5c`, `0x232f286`, `0x232de10`, `0x2ec9aac`,
  `0x2330186`, JNI/vtable indirects, etc. **No call to `0x2eca720` or
  `0x2ec9eed`, and no reference to `0x7cdd149`.** It installs/registers the
  handler; it does not run it.

## 4. The six latch writers and their reachability (complete enumeration)

Exhaustive grep for `movb $0x1,<rip>` targeting `0x7cdd149` in the frozen
disassembly yields exactly six (matches `EngineFacts.md` and `A15`):

| Writer RVA | Enclosing path | Clean boot reachable? |
| --- | --- | --- |
| `0x2eb7520` | `nativeInitClientSettings` → `0x2eb7155` flag-filter helper | **No.** Gated off by the clean default state: `0x2eb71e8: cmpb $0,[0x7697d08]; je skip` and `0x2eb71f5: testb $1,[0x7697cf0]; jne skip`. Default `0x7697d08=1`, `0x7697cf0=0x31` (bit0=1) → writer skipped. A14 tested `{}`, `null`, `[]`, `{"ClientAppSettings":{}}`, `{"flagFilters":[]}` — none changed it; `0x7697cf0` comes from guest flag-filter processing of Java flag strings, not a clean knob. `nativeInitClientSettings` *is* called on the boot path (`uh/f$f.smali:159`), but the gate is closed. Clearing the gate from the harness is a host seed. |
| `0x2ec9fae` | crashpad flag-provider installer `0x2ec9eed` | **No on boot.** Reached only via `nativeRunCrashpadHandler 0x2eca720` (§2/§3) or `CrashpadHandlerMain 0x2ec9c30`; both are the crash-handler subprocess path. Also gated on channel string at `0x72a3718 != NULL` (`0x2ec9f2e`), which the harness seeds with the guest's own setter `0x2e57c20`. |
| `0x56d31a3`, `0x56d3386`, `0x56d6a4b`, `0x56d6b80` | `IPlatformSystemDialogHandler$..._native_dismissAll` body | **No.** System-dialog dismiss path; requires a real dialog CppProxy/nativeRef. Task #510 proved no shipped JNI export yields one, no export `NewObject`s the CppProxy class, and the only export reaching the region (`nativePassKeyEvent` → `0x56d2d35`) hardcodes `r9=0` so the store is skipped. Fabricating the proxy would be a fake vtable/object (disallowed). |

No sixth alternative exists: these six are the complete set of byte-setting
instructions for `0x7cdd149` in the frozen object.

## 5. Runtime confirmation from the clean default run

`bash tests/app-shell.sh` on the current tree (default knobs, no seeds):
```
[app-shell] flags-latch module -> rc=-2 latch=0
[app-shell] crashpad-return module -> rc=1 latch=0
first frame rendered
FAIL: no qualifying guest->Java app-shell callback observed
```
Exit 1; no `/tmp/sober-app-shell.marker`. The harness backtrace lands in the
`0x6caee44 / 0x6caed7c → 0x2386599` region — the type-6 `RBXCRASH`
"Can't initialize the TaskScheduler before flags have been loaded" reporter
reached by `0x23862b0: cmpb $0,[0x7cdd149]; je 0x2386588` with latch 0. So the
clean run stalls exactly at the guard this task is about, with latch 0 and no
callback — consistent with the static proof, and the gate correctly stays RED.
(None of this changes source; the only post-run artifacts are the harness's
own `/tmp/sober-first-frame.marker` and `/tmp/sober-guest-init.marker`.)

## 6. Exact missing prerequisites (why it cannot be made clean)

For a *genuine* notifier-driven callback the latch must be set before the
TaskScheduler guard, by unmodified guest boot code. The missing prerequisites
are structural, not a matter of a knob:

1. **A Java boot call to a latch writer does not exist.** The only Java caller
   of `0x2eca720` is `CrashpadHandler.main` (subprocess). Boot calls
   `nativeInitCrashpad` only to register that class name. To get the latch from
   Java you would have to *start the crash-handler subprocess* or replay its
   `main` from the harness — a host-initiated seed, WITHHELD by definition.
2. **The non-crashpad writer is gated off in the clean state** (`0x7697cf0`
   bit0 = 1); no clean input clears it. Forcing it is a state seed.
3. **The dialog writers need a fabricated CppProxy** — a fake vtable/object,
   forbidden by decision 013/038's no-fabrication rule.
4. Even the always-withheld clean-return crashpad route has explicit opt-in
   prerequisites (`crashpad_return.h`): reach app-shell (`SOBER_APP_SHELL=1`,
   `SOBER_RENDER=1`), `SOBER_CRASHPAD_RETURN=1`, `SOBER_FAITHFUL_JNI_ARRAYS=1`
   (plus optional `_ARGV`/`_PASSCRED`/`_STDIO`). Every one is in the
   `guest_app_shell.c` `seeded_state` chain and forces WITHHELD; none can be
   relabelled clean.

So "clean latch" cannot be achieved by any default-ON path in these owned
files. Adding one would be a host seed and would have to remain in the
withhold chain; it would never justify emitting the marker.

## 7. Gate / honesty stance

* No `SOBER_*_DEFAULT=0` kill switch was weakened; `flags_latch_default`
  default route stays `none`, `crashpad_return` stays OFF by default.
* `sober_crashpad_return_java_boot_reachable()` correctly returns **0** (no
  clean boot route); it must never be used to relax the withhold chain.
* No source edits were needed or made to the four owned source files;
  `src/jni/provenance_gate.*` and other agents' in-progress files were left
  untouched. Only this note was written.
* Marker: **WITHHELD**. Lead commits/pushes.

## 8. Build & verification evidence (this task)

```
cmake --build build -j4                      -> exit 0 (all targets built)
bash tests/verify-native.sh                  -> PASS, ldd libc-only
ctest --test-dir build --output-on-failure   -> 2/2 passed
SOBER_D09_WITHHOLD_AUDIT=1 tests/D09-withhold-audit.sh -> GAP LIST (none) (164 knobs, 6 diagnostic excluded)
LD_LIBRARY_PATH=build/ndk-stubs ./build/jni_probe -> "jni bridge self-test OK"
bash tests/app-shell.sh                      -> exit 1 (RED), no app-shell marker, latch=0
sha256sum libroblox.so                       -> 3e498d64…  ; BuildID 204ad049…
```

## 9. Handoff

Recommendation to lead: keep the default route `none` and the crashpad routes
opt-in/withheld; keep `tests/app-shell.sh` RED; do not add a default-ON latch
seed. The decisive blocker for the overall marker is now clearly two-fold and
independently proven: (a) no clean Java boot route to the latch (this task),
and (b) worker-3's result that clean `seeded_state==0` G8 render is impossible
without real Java surface-lifecycle/redraw delivery.
