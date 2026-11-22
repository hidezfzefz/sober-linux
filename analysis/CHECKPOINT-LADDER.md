# Checkpoint ladder (new rule)

Rule: **no gate, marker or milestone is defined until an AVD capture proves it
reachable.** The single `app shell rendered` marker is retired. Each rung below
has an AVD-captured reference. Nothing here is a success condition for the goal
until all defined rungs pass with `seeded_state == 0` and no ledger debt.

## Rung 1 — client-settings boundary returns + flags-loaded latch

- Condition: `nativeInitClientSettings 0x2359277` returns; guest latch
  `0x7cdd149 == 1`, written by the guest's own `0x2eb7520`.
- AVD reference: `/tmp/opencode/w12-gate-avd.log` (authoritative base.apk
  `114a6f8e`, engine `3e498d64`): `INIT_ENTER` CF0=0, D00=0, D08[4]=0,
  LATCH=0, GUARD.b0=0x80 → `LATCH_WR@0x2eb7520` (RA `base+0x2359342`,
  D08[4]=0x00010000) → `INIT_LEAVE rc=0, LATCH=1`.
- Reproducible now. No auth required.

## Rung 2 — app shell boot chain (unauthenticated)

- Condition: `nativeAppBridgeAppStart` → `nativeAppBridgeV2InitWithParams` →
  `nativeAppBridgeStartLuaAppDM` all entered in order.
- AVD reference: `/tmp/opencode/w12-j2n-300.log` (300 s window, 714 hooks,
  1423 calls): `#727 t=292010ms nativeAppBridgeAppStart`,
  `#734 t=292046ms nativeAppBridgeV2InitWithParams`,
  `#736 t=292074ms nativeAppBridgeStartLuaAppDM`. Reached without login.
- Reproducible now. No auth required.

## Rung 3 — authenticated rungs (UNDEFINED)

- PlaceId, `RBXSurfaceView`/ANativeWindow lifecycle, `StartGameWithParam`,
  notifier-driven `gameLoadedCallback(J)V`, guest-tagged G8 frame.
- **No AVD capture exists.** The 300 s oracle above contains zero surface,
  placeId, StartGame or gameLoadedCallback calls; the w13 deep-link stimulus
  attempt died with HTTP 401s on auth endpoints.
- Therefore rung 3 has no definition and cannot be asserted. It stays
  UNDEFINED until an authenticated AVD capture (valid session + actual game
  join) exists. This is the answer to "is the marker reachable": unverified.

## Debt

See `FABRICATION-LEDGER.md`. Any checkpoint reached with a ledger entry active
is reported as `CHECKPOINT ... DEBT=1` and does not count as clean.
