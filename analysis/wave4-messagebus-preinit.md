# Wave4 #516 — MessageBus pre-init wiring (lead takeover)

Frozen engine: BuildID `204ad049599c96afb75b8a21d5051063e821d6c9`,
SHA `3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335`.
Read-only analysis plus one opt-in harness call; no guest byte writes, no fake
objects/callbacks.

## Finding

`nativeAppBridgeStartLuaAppDM` `0x25448f6` is a guard/dispatch wrapper whose real
driver `0x25449fc` reaches the mode-4 app-state accessor `0x24a8e44(4)`, which
**constructs and returns the MessageBus singleton** `0x72cbdd0` (service id 4,
guards `0x72cc270` / `0x72cc718`). This is documented in
`analysis/P7_lua_render_trigger.md` and confirmed against the frozen object.

The id=1 GameLoaded registration `0x253cb56(1,&0x7234520)` tests and sets its
one-shot guard `0x72a31ba` at function entry (`0x253cb84`/`0x253cb91`) BEFORE it
builds the descriptor and inserts the node into the bus `+0x288` list via
`0x253e126`. If the bus does not yet exist when `nativeAppBridgeV2InitWithParams
0x24abf96` calls it at `0x24ad6a5`, the guard is spent and every later re-drive
is inert (worker-3 #500 finding F1).

## Wiring

`src/linux/guest_app_shell.c` now calls the genuine export `0x25448f6(env,class)`
immediately before `0x24abf96`, gated by opt-in `SOBER_MESSAGEBUS_PREINIT`
(default OFF, in `seeded_state`). The knob is a guest-state seed and can never
produce a clean marker.

## Current status

With the default latch still 0, the harness dies at the TaskScheduler guard
`0x6caef67` in the singleton path BEFORE reaching `0x24abf96`, so this pre-init
call has not yet been exercised end-to-end. It becomes reachable as soon as a
genuine non-terminating latch route lands (worker-2 #509). No claim of callback
success is made. Marker WITHHELD, decisions 013/038 intact.
