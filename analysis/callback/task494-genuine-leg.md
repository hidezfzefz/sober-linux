# Task #494 — genuine callback leg: chain, prerequisites, honest status

Owner: **worker-3** (task #494). Scope: this analysis file plus
`src/jni/publish_default.*`, `provenance_gate.*`, `startgame_v2.*`.
Engine frozen: AppImage `libroblox.so`, BuildID `204ad049…`, SHA `3e498d64…`.
No guest byte patches, no fake vtable/host callback, no fabricated publish or
placeId, no marker emission. Marker stays **WITHHELD** until the full 013/038
credential holds on a clean (`seeded_state==0`) path.

## 1. The genuine chain (all shipped guest code; RVAs relative to load bias B)

| Step | Guest code | What it does | What the host may do |
| --- | --- | --- | --- |
| 1 | `0x24abf96` InitParams → `0x24ad699` → `0x253cb56(1, B+0x7234520)` | Builds id=1 GameLoaded descriptor (vptr `0x6dbbf98`, id `+0x08=1`, slot `+0x18=&0x7234520`) and a `0x60` node with `+0x08 = 0x27eaf04`, inserts into MessageBus singleton (`0x72cbdd0`) `+0x288` list. One-shot guard `0x72a31ba`. | Call the genuine registration fn; never build a node/descriptor yourself. |
| 2 | `0x2ebd51a` StartGameWithParam → `0x2ebd865` `placeId()J` → store `0x2ebd86a` → `0x72a31c0` | Caches the real jlong placeId. | Supply a real `StartGameParams` jobject whose `placeId()J` returns J (not a jstring). |
| 3 | closure `0x2ec1340` (reads `0x72a31c0` at `0x2ec1370`, flag `0x72cb648`, app singleton `0x72cb5e0` via `0x30ef840`, AppBridge `[vt+0x40]`) | SessionReporter `GameLoaded` → MessageBus publish event id 1. | None — entered by the guest dispatcher only. |
| 4 | node dispatch `0x27eaf04` → `0x27eaf2f call [vptr+0x30]` → descriptor invoke `0x27eaf4c` → `[0x7234520]=0x6dbd860` → `[0x6dbd860]=0x27eaf6e` (tail jmp, RSI=placeId) | Real guest virtual dispatch of event id 1. | Enter `0x27eaf04` with `RDI=genuine node`, `RSI=&placeId` (`publish_default` does this; it stays diagnostic/force-withheld). |
| 5 | notifier `0x27eaf6e`: cached class `0x72a2f48`, method `0x72a2fc0`, `0x27eafae call 0x25848e0`, return site `0x27eafb3` | Calls `gameLoadedCallback(J)V` through JNI slot 142 (`+0x470`). | Never call `0x27eaf6e`/`0x25848e0` yourself. |
| 6 | `provenance_gate` (`native_jni.c:517` → `on_dispatch`) | Certifies GENUINE only on: thunk return `B+0x2584960` + notifier return `B+0x27eafb3` on the live stack, class/method identity vs cached globals, frozen BuildID, name `gameLoadedCallback`, **placeId>0**, no harness taint. | Query `is_genuine()`; call `emit_provenance()` only after `seeded_state==0`. |

## 2. Prerequisite status on the honest default path

- **Satisfied**: APS2 relocs (`0x6dbd860→0x27eaf6e`, `0x7234520→0x6dbd860`, `0x6dbbfc8→0x27eaf4c`); cached class/method seeded by JNI_OnLoad; JNIEnv function table available; placeid_default can serve a real `jlong>0`; provenance credential wired and default-ON.
- **Blocked (not in this task's files)**: flags-loaded latch `0x7cdd149==0` ⇒ TaskScheduler abort (`RBXCRASH: FatalRuntimeError`) before any step-1/3 work (worker-2/lead); id=1 node never registered (`publish_seeded → -3 NO_NODE`); `app+0x488` / app singleton gaps fault `0x2ebd51a`/`0x2ec1340` at `0x32056eb`; the guest's own SessionReporter→event-1 publish is not statically isolated in disassembly (C05 §9, C11 §8).
- **Withheld by design**: `publish_default` re-drive of `0x253cb56` and host-entered `0x27eaf04` are force-withheld (`sober_publish_default_should_withhold()` is in `seeded_state`); the ALT thunk path is HARNESS-THUNK and permanently taints the credential. Neither can make the marker green.

## 3. Code changes in this task (fail-closed, no fabrication)

- `provenance_gate.c/.h`: the positive credential now **also requires placeId>0** from the genuine guest global `B+0x72a31c0` (read-only sample at dispatch time; new RVA `SOBER_PROV_GATE_RVA_PLACE_ID`, accessor `sober_provenance_gate_place_id()`, `SoberProvenanceGateSnapshot.place_id`). A dispatch without a positive placeId can never certify GENUINE.
- `provenance_gate.c/.h`: added `sober_provenance_gate_emit_provenance()` — a one-shot, gate-owned diagnostic line emitted **only** when `is_genuine("gameLoadedCallback")` holds with pid>0 and no harness taint. It quotes only observed values (`0x27eaf6e`, `placeId`, `0x27eafb3`, `0x2584960`) and is **not** a marker. This fills the structural gap where `tests/app-shell.sh:158-162` required a line no source emitted.
- `publish_default.c`: `should_withhold()` now reflects ACTUAL diagnostic action (a `0x253cb56` re-drive, a `0x27eaf04` publish, or a recorded hit) instead of merely being compiled/enabled. A module that manufactured no state no longer makes `seeded_state==0` structurally unreachable; once it re-drives it is still withheld for the run.
- `provenance_gate.c`: hardened the stack discriminator — the frame directly above the notifier return `0x27eafb3` must ALSO be guest `.text` (`0x27eaf34` on the genuine node-dispatch path). A host-entered `0x27eaf04` (publish_default re-drive) leaves a harness return address there and can no longer be scored GENUINE, independently of `seeded_state`. The frame-chain fallback uses `[saved-rbp+0x10]` for the same check.
- Standalone check: with no genuine dispatch, `is_genuine()==0`, `place_id()==0`, `emit_provenance()==0` (fail closed).

## 4. Handoff required (lead-owned files, not edited here)

1. In `guest_app_shell.c`, after the `seeded_state` check establishes `seeded_state==0` and before the final withhold, call `sober_provenance_gate_emit_provenance()` so the genuine line appears in `$ERR`. Do **not** call it when any seed is active.
2. Emit `[app-shell] clean state: seeded_state=0 (all opt-ins unset)` only when `seeded_state==0` (the test requires it).
3. Keep every callback-leg knob (`SOBER_GAME_LOADED_PATH`, `SOBER_STARTGAME_V2`, `SOBER_GAME_PARAMS`, `SOBER_PUBLISH_DEFAULT`, `SOBER_PLACEID_DEFAULT`, `SOBER_CALLBACK_PROVENANCE`, `SOBER_NOTIFIER_PROBE`) in `seeded_state`.

## 5. Honest status

Callback leg remains **0/1** on the default path: no run reaches `0x27eaf6e`. No fabricated callback, publish, placeId, or provenance line was introduced. `tests/app-shell.sh` stays RED. The marker must remain WITHHELD until the genuine upstream guest state (latch/FFlag/singleton/app+0x488) is repaired by the owning roles and the guest itself publishes event id 1.
