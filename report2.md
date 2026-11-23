# n7w2 #692 static-B — real boot: library load → AppStart (READ-ONLY)

FAULTING INSTRUCTION FIRST (n7w1): guest `int3 0x6caef66` (RIP `0x6caef67`) HardAssert mimalloc; fault bytes `[0x7cdd149]=1, [0x7cef650]=0` 3/3. No runs/writes.

## Backlog 5 — real-boot order (all raw quotes)

| # | Real-boot step | Evidence | Harness divergence |
|---|---|---|---|
| 1 | Android linker applies `DT_ANDROID_RELA` (APS2) and runs **all 3618** init_array ctors | `android_relocs.h:8-12` "plain dlopen() leaves the 3618-entry DT_INIT_ARRAY at RVA 0x7220e58 zero-filled"; `guest_dlopen_host.c:824` std ranges `{2,3},{5,3618}` | replays only 2 and 5..3617; skips 0,1,3,4 |
| 2 | ctor[0] FFlag registry `0x2e62390` runs as a genuine ctor | `guest_app_shell.c:1411-1419` decision 008 deliberate skip; log `n5_1.err:3687 [fflag-default] replayed guest ctor 0x2e62390 (FFlag registry): guard=257 ... ctors 1/3/4 not required ...` | skipped genuine call; host-side replay **after** ctor3602 (3687 > 3652) |
| 3 | ctor[1] protobuf once-guard `0x1dba8f3`, ctor[3] allocator thread init `0x1e47927`, ctor[4] time baseline run | `guest_dlopen_host.c:824` skip; `:850-857` `SOBER_CTOR1`; `:851-854` "allocator thread init after ctor[2]" only native/all ranges; log line 3687 names them | never run in std profile |
| 4 | ctor3602 `0x2279a47` writes `0x7cef650` (mimalloc/FFlag byte) unconditionally to 0 | static-A #687: `2279a4b xor %eax,%eax; 2279a8f mov %al,0x7cef650`, no detection call; log `n5_1.err:3652 [harness] ctor[3602] rva=0x2279a47` | same write; harness then faults because byte stays 0 |
| 5 | ART VM calls `JNI_OnLoad` `0x227af5a` | `n5_1.err:3702 [app-shell] W24 JNI_OnLoad FIRST 0x227af5a -> ver=0x10006` | harness calls it explicitly first (JNI_OnLoad-first ON) |
| 6 | Lifecycle natives in captured order: #67 PreCreated (2406ms), #100 PreResumed (18665ms), #105 OnResumed (20511ms) | `w12-j2n-300.log:82,115,120` quoted in report.md | N14 replays only #67 `0x23239c5` and #105 `0x2328066` (`guest_app_shell.c:1738-1748`), pre-BaseDirs |
| 7 | Scheduler guard latch `0x7cdd149` 0→1 (real AVD: write at `0x2eb7520`) | `w18-avd/peract2.log:19 HOOK LATCH_WR 0x2eb7520 OK`; harness `n5_1.err:3706 [W32_LATCH_RO] before=0 after=1` | harness flips it immediately before nativeInitClientSettings, after ctor replay (3652→3706) |
| 8 | #551 `nativeInitClientSettings` then #552 `nativePostClientSettingsLoadedInitialization3` | `w12-j2n-300.log:566` (`t=290317ms`), `:567` (`t=290696ms ...([object Object])`) | N14 3rd replay `0x2383c18` with opaque param_obj (`guest_app_shell.c:1759-1766`) |
| 9 | Mimalloc integration detected true (real Bionic + engine allocator loaded) | static-A #683 "mimalloc probe=DFLog-only" in harness; n6 static-B: kernel glibc bridges | real boot has genuine mimalloc; harness DFLog-only → HardAssert |
| 10 | Java calls `nativeAppBridgeAppStart` `0x247dc8d`, then `nativeAppBridgeV2StartAppWithParams` | `guest_app_shell.c:1919-1931` call + log line; `:1961-1964` "Java follows nativeAppBridgeAppStart with nativeAppBridgeV2StartAppWithParams(StartAppParams)" | harness call is opt-in and never reached while HardAssert fires at step 4/9 |

## UNPROVEN / NOT done

- UNPROVEN: why real boot does not trap on the same zero `0x7cef650` — possibly a later FFlag write via `227d51f: lea ...#7cef650` with name `DebugForceTerminateOnRbxCrash`, not located statically.
- UNPROVEN: exact real-boot timestamp of nativeAppBridgeAppStart (not in w12 300-line capture).
- RETRACTED: none. NOT DONE: no runs/device/guest writes/gate edits/seeder/fake returns/remaps; object OFF, no captures/tokens in git.
