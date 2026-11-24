# n10w1 #726 static-B — load→AppStart native/event table (READ-ONLY)

FAULTING INSTRUCTION FIRST: guest `int3 0x6caef66` (RIP `0x6caef67`), TaskScheduler FatalRuntimeError, latch `[0x7cdd149]=0` 6/6. DELETE NOTHING; no network/runs/writes; object OFF; w11-arg0.json LOST ⇒ all runs MODIFIED INPUT, NO rung credit.

## Source of quotes — important

`/tmp/opencode/w12-j2n-300.log` itself is **GONE** (audit #718/#720: missing, not deleted by us). Verbatim lines survive in `/tmp/opencode/n9-harness/raw_capture_entries.txt`, `raw_capture_order.txt`, `raw_552_capture.txt` and in `analysis/CHECKPOINT-LADDER.md`. All lines below are quoted from those surviving files, with their original w12 line numbers.

## PROVEN — order, args, harness replay vs skip (load → AppStart)

| w12 line / # | raw line (verbatim, truncated only where marked) | harness |
|---|---|---|
| 82 / #67 t=2406ms | `J2N #67 t=2406ms ...JNIActivityLifecycleCallbacks.nativeOnPreCreated("ActivitySplash")` | **REPLAY** `0x23239c5` (`guest_app_shell.c:1738`) |
| 85 / #70 t=2422ms | `J2N #70 t=2422ms ...NativeSettingsInterface.nativeSetRobloxChannel("")` | replay `0x2326e2e` (`:1784`) |
| 93 / #78 t=12320ms | `J2N #78 t=12320ms ...nativeOnCreated("ActivitySplash")` | SKIP |
| 101 / #86 t=15108ms | `J2N #86 t=15108ms ...nativeOnPostCreated("ActivitySplash")` | SKIP |
| 106 / #91 t=16027ms | `J2N #91 ...nativeOnPreStarted("ActivitySplash")` | SKIP |
| 108 / #93 t=16891ms | `J2N #93 ...nativeOnStarted("ActivitySplash")` | SKIP |
| 111 / #96 t=17795ms | `J2N #96 ...nativeOnPostStarted("ActivitySplash")` | SKIP |
| 115 / #100 t=18665ms | `J2N #100 ...nativeOnPreResumed("ActivitySplash")` | SKIP (note: N14 label "#105 nativeOnResumed" maps to w12 #105) |
| 120 / #105 t=20511ms | `J2N #105 t=20511ms ...nativeOnResumed("ActivitySplash")` | **REPLAY** `0x2328066` (`:1744`) |
| 131 / #116 t=24008ms | `J2N #116 ...nativeOnPostResumed("ActivitySplash")` | SKIP |
| 542 / #527 t=290171ms | `J2N #527 ...nativeSetRobloxChannel("")` | (W28_ORDER may skip duplicate) |
| 549 / #534 t=290179ms | `J2N #534 ...nativeSetRobloxVersion("2.740.931")` | replay `0x232d484` (`:1779`) unless w28_order_done |
| 556 / #541 t=290202ms | `J2N #541 ...NativeGLInterface.nativeGameGlobalInit()` | replay `0x233762c` (`:1832`) unless w28_order_done |
| 566 / #551 t=290317ms | `J2N #551 ...nativeInitClientSettings("{...}", null, "GoogleAndroidApp")` | **REPLAY** `0x2359277` (AVD-boundary) |
| 567 / #552 t=290696ms | `J2N #552 t=290696ms ...nativePostClientSettingsLoadedInitialization3([object Object])` | **REPLAY** `0x2383c18` (`:1760`) with opaque `param_obj` |
| 612 / #612 t=290866ms | `J2N #612 ...nativeSetBaseDataDirectories("/data/user/0/com.roblox.client", "/storage/emulated/0/Android/data/com.roblox.client")` | replay `0x24650a9` (`:1773`) |
| — | #613 PrefsFile `0x246515d`, #614 NativeFlags `0x2465210` | replayed after BaseDirs (`:1800`, `:1819`) |
| 742 / #727 t=292010ms | `#727 t=292010ms nativeAppBridgeAppStart` | conditional/opt-in `0x247dc8d` (`:1928`); ALT path skips (`:1913`) |
| 749 / #734 t=292046ms | `#734 t=292046ms nativeAppBridgeV2InitWithParams` | replay `0x24abf96` (`:1868`) |
| 751 / #736 t=292074ms | `#736 t=292074ms nativeAppBridgeStartLuaAppDM` | opt-in `0x25448f6` (`:915`, `:1850`) |

## PROVEN — rung2 refs #727/#734/#736 and what each needs

From `analysis/CHECKPOINT-LADDER.md:20-26` (verbatim):
```
- Condition: `nativeAppBridgeAppStart` → `nativeAppBridgeV2InitWithParams` →
  `nativeAppBridgeStartLuaAppDM` all entered in order.
- AVD reference: `/tmp/opencode/w12-j2n-300.log` (300 s window, 714 hooks,
  1423 calls): `#727 t=292010ms nativeAppBridgeAppStart`,
  `#734 t=292046ms nativeAppBridgeV2InitWithParams`,
  `#736 t=292074ms nativeAppBridgeStartLuaAppDM`. Reached without login.
```
- **#727 AppStart**: all three must be entered *in order*; host-side call re-enters the broken `0x233319a` path, so `SOBER_ALT_CALLBACK_PATH` skips it (`guest_app_shell.c:1911-1914`); also needs the MessageBus prime `0x24a8e44(4)` before the one-shot guard `0x72a31ba` is spent (`analysis/callback/task507-preinit-ordering.md:32,59-70`).
- **#734 V2InitWithParams**: needs genuine `InitParams` and the MessageBus prime before `0x24abf96` (once `0x24ad699: call 0x253cb56` spends the guard, a fault leaves no node — task507). Currently never returns (n5/n9 harness: `N14_REPLAY 3/3` never printed; fault inside the native).
- **#736 StartLuaAppDM**: `docs/WORKFLOW.md:62-64` requires "Rung 2 ... attempted only after the recursion is gone. 'Reached' requires a quoted `StartLuaAppDM` line from the harness log on the authoritative input" — impossible while `w11-arg0.json` is LOST (all runs MODIFIED INPUT, no rung credit, never print `PASS-BOUNDARY-REPLAY`).

## UNPROVEN / RETRACTED

- UNPROVEN: exact args of #727/#734/#736 (surviving CHECKPOINT-LADDER lines give only names/timestamps; `w12-j2n-300.log:742/749/751` with full args is gone); full w12 line 742/749/751 text.
- RETRACTED: any rung-2 credit claim from current runs (input LOST, diagnostic only); recursion-removal replay is NOT a rung-2 attempt (ledger 35).
- NOT DONE: no runs/device/network/writes/gate/seeder/fake returns/path remaps; DELETE NOTHING honored. No captures/tokens in git.
