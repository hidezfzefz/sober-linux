# Blocker #2 — event-1 publish path, re-analysed with the correct engine/APK

Frozen engine: SHA `3e498d64…`, BuildID `204ad049…`,
`/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so`.
Authoritative DEX: SHA-matched `base.apk` `114a6f8e…`.
No Flatpak/moded APK/mini-smali evidence was used.

## Question

Worker-4's AVD oracle established that main-process boot sets the flags-loaded latch
`0x7cdd149` through the client-settings path. With the latch no longer the decisive
issue, the remaining callback blocker is: **what genuinely publishes MessageBus event
id 1**, reaching dispatch `0x27eaf04` from guest `.text` so the strengthened
provenance gate can certify `gameLoadedCallback(J)V`?

## Static findings

1. **No Java/DEX publisher.** The authoritative DEX (verified with Craft Anything
   `--deep android-method-show` / `android-class-users-find`), plus worker-16's
   `analysis/w6-16-event1-producer.md`, finds no Java call that publishes event 1.
   Java supplies `placeId` via `nativeAppBridgeV2StartGameWithParam 0x2ebd51a` and
   consumes `gameLoadedCallback(J)V`; the universalapp event bus is string-keyed and
   separate.
2. **`0x27eaf04` has no direct static caller.** The frozen disassembly contains six
   `lea` references to it: `0x253e235` (registration of the id=1 descriptor), and
   five more in the same descriptor-family builder `0x253cb56`. There are **zero**
   `call 0x27eaf04` instructions. Dispatch is virtual through `node+0x08`.
3. **No code xrefs to the notifier's own log strings.**
   - `"onGameLoaded() SessionReporterState_GameLoaded placeId:%lld"` at `0x36c7c9`
     is referenced only by notifier `0x27eafbf`.
   - `"gameLoadedSignal"` at `0x2c3165` is referenced only at `0x27e961b`.
   This means the publisher is data-driven/virtual, not a direct call to the notifier.
4. **Bus singleton and node layout.** Bus getter `0x24a8e44(4)` returns
   `0x72cbdd0`. Registration `0x253cb56` writes the 0x18-byte intrusive-list
   container at `bus+0x288`; the 0x60-byte node is linked at `container+0x08`
   (`analysis/scale-a1-node-registration.md`). Our host finder bug (one missing
   indirection) is already fixed.
5. **Plausible but unproven publisher**: the guest game-load state machine
   (`SessionReporterState_GameLoaded` → virtual publish) reached from the full
   DataModel/join lifecycle. No direct static edge to event 1 was found.
6. Static sweeps for a function that walks `bus+0x288` and calls `node+0x08`
   produced only generic container/destructor loops; none could be tied to
   `0x72cbdd0` without runtime evidence.

## Runtime oracle attempt

An isolated AVD (`W3SurfaceOracle`, port 5556) was started with the authoritative
APK to capture logcat around a real game join / deep link
(`https://www.roblox.com/games/…`). The emulator went offline before boot completed,
so no new oracle evidence was obtained from this attempt. This is an environment
failure, not evidence for or against any hypothesis.

## Honest status

The clean event-1 publish trigger remains **unresolved**. No direct static producer
exists in the correct engine or authoritative DEX; the notifier can currently be
reached only by the host-entered `publish_default` re-drive, which the 3-frame
provenance gate correctly rejects. No guest bytes, fake callbacks, or synthetic
publish events were introduced. Marker stays WITHHELD.

## Next experiments

1. Boot the rootable `W6_04RootOracle` AVD with authoritative APKs and start a real
   game join via the manifest deep link; capture `logcat -v threadtime` for
   `gameLoadedCallback` / `SessionReporterState_GameLoaded`.
2. Use Frida or GDB on the emulator to break at `0x27eaf04` and record the guest
   caller chain (expected through `node+0x08`), then map it back to the frozen
   engine RVA.
3. If a genuine producer is identified, reproduce that guest call sequence natively
   without seeds; re-run the full marker contract and negative A/B suite.
