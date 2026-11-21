# Wave7 lead note — client-settings latch gate state

Frozen engine BuildID `204ad049599c96afb75b8a21d5051063e821d6c9`,
SHA `3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335`.
Authoritative DEX: SHA-matched `base.apk` `114a6f8ed6f0f974cf632a9cbec2f5cd45e1d85d7ed251db4f3920140bc6ead4`.

## Starting point

Worker-3's AVD oracle (`analysis/w6-04-latch-oracle.md`) observed that main-process
boot reaches TaskScheduler/RenderJob/FLog with zero crashpad lines, so the flags-loaded
latch `0x7cdd149` is set by an in-process writer. The candidate is the client-settings /
flag-filter path `0x2eb7520`, which in our harness is gated off by
`0x7697d08 == 1` and `0x7697cf0 bit0 == 1` (observed `0x7697cf0 = 0x21`).

## Authoritative DEX payload fidelity

Craft Anything `--deep android-method-show` on the SHA-matched APK shows:

- `uj/a.d()` returns `/data/local/tmp/ClientAppSettings.json`.
- `uj/a.h()` reads and re-serializes that file as a JSON object.
- `uj/a.a()` returns that JSON string as the `overridePayload`.
- The real Java call is `nativeInitClientSettings(settingsPath, overridePayload, "GoogleAndroidApp")`.

`guest_app_shell.c` was corrected in wave6 to use this argument order and the
`NativeGLInterface` class (previously taken from a modded mini APK).

## Lead A/B experiment

With `SOBER_CLIENT_SETTINGS_INIT=1` and candidate external-JSON payloads, measured
`7697cf0` after the guest call:

```
{}                                              0x21
{"ClientAppSettings":{}}                         0x21
{"flagFilters":[]}                               0x21
{"ClientAppSettings":{"flagFilters":[]}}         0x21
{"DebugForceTerminateOnRbxCrash":false}          0x21
{"MigrateComponentManagerToRuntimeInjector":true} 0x21
{"EnableGameActivity10":true}                    0x21
{"FFlagEnableGameActivity10":true}               0x21
{"FStringUseNewClientSettings":false}            0x21
{"ClientAppSettings":{...}}                      0x21
{"DisableFlagFilters":true}                      0x21
{"SetFlagFilters":false}                         0x21
```

None cleared bit0. Static xrefs to `0x7697cf0` in the frozen engine show only a
zeroing `movups` in ctor `0x2096448` and reads/tests; any other write must be through
a computed pointer. A GDB hardware watchpoint on the byte was started but had not
completed at the time of this note.

## Honest status

The exact Java/FFlag state that clears `0x7697cf0 bit0` in real boot is **not yet
identified**. No guest byte was patched and no synthetic latch state was introduced.
The marker remains WITHHELD and the default path unchanged. This note supersedes no
gate condition and adds no knob.
