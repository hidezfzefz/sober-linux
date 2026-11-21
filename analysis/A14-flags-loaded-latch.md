# A14 — Flags-loaded latch `0x7cdd149` after the #485 singleton unblock

Engine BuildID `204ad049599c96afb75b8a21d5051063e821d6c9`,
SHA `3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335`.
All RVAs are read-only facts from the frozen x86_64 `libroblox.so`; no guest byte
is written and no marker is emitted by this work.

## 1. State advanced by #485

* FFlag registry repair (`0x227be14`, `0x22e567a`) now also replays the two
  descriptor thunks `0x2290e17` / `0x2290df2` for
  `MigrateComponentManagerToRuntimeInjector` and its
  `...HundredthPercent` rollout.
* Both are set through the guest setter `0x23648be` (bool `True` type 4, int
  `10000` type 3). Runtime: `migrate=1`, `rollout=10000`, `bucket=0`,
  predicate `0x6096928` returns `513` with `AL=1`, so accessor `0x30ef840`
  takes the real service path. The old fault `0x24ad562` is gone.
* Controller `ensure_flag` rc=0/flag=1; singleton construction now reaches
  the guest logger and the TaskScheduler guard.

## 2. The next genuine guard

`RBXCRASH: FatalRuntimeError (Can't initialize the TaskScheduler before flags
have been loaded)` — trap at `0x6caef67`. The check is

```
23862b0: cmpb $0,0x7cdd149    ; "flags loaded" latch
23862b7: je  2386588          ; type-6 FatalRuntimeError reporter
```

This is the second, distinct guard described in
`analysis/lua-rbxcrash-flags-loaded.md`. The latch must be set by **guest code**;
writing the byte from the harness is forbidden.

## 3. Tested guest routes (all still WITHHELD)

| Route | Result |
| --- | --- |
| `nativeInitClientSettings` `0x2359277` with canonical jstrings | rc=1, latch=0. `0x7697d08=1`, `0x7697cf0=0x31`; the parse branch at `0x2eb71f5` skips writer `0x2eb7520`. No JSON payload among `{}`, `null`, `[]`, `{"ClientAppSettings":{}}`, `{"flagFilters":[]}` changed this. |
| `nativeRunCrashpadHandler` `0x2eca720` with a real `String[]` (faithful array getters, `--crashCounter` etc.) | reaches `0x2ec9eed`, writes the latch, calls `0x2338e54`, then aborts in the guest's crashpad fatal path (`Fatal error: glibc detected an invalid stdio handle`). |
| Direct replay of the same writer `0x2ec9eed` with native argv | identical abort; launching the real crashpad handler is intrinsic to that writer's tail. |

Because the only reachable writers are crash-handler paths that terminate the
headless process, the latch remains 0 on the honest default path. Both
`SOBER_FLAGS_LATCH_CRASHPAD=1` (direct) and `SOBER_FLAGS_LATCH_JNI=1` (real JNI)
are opt-in diagnostics, are in the `seeded_state` withhold chain, and are
**not** enabled by default. Default run ends at the genuine guest int3
`0x6caef67` with no marker file.

## 4. Stdio plumbing added on the way

The generated Bionic libc shim now translates the three synthetic `__sF`
streams for `fflush`, `fclose` and `fwrite` instead of handing Bionic `FILE*`
layouts to glibc. This is host-side ABI plumbing only; it advances the logger
past `libc+0x7c8ff` but does not set the latch and emits no callback.

## 5. Lead follow-up (#491) — crashpad route stays opt-in

A/B with `SOBER_FLAGS_LATCH_ROUTE=crashpad-jni` / `narrow` confirms the writer
sets the latch and immediately enters crashpad's fatal path: the guest logs
`crashpad handler: real/simulated crash` and glibc validator aborts with
`Fatal error: glibc detected an invalid stdio handle`. Host-side plumbing in
`src/compat` (`fflush`/`fclose`/`fwrite` translation, double-close suppression,
an attempted `fprintf` mapping) did not make the writer return; under ptrace
the 120 MB memfd guest init does not complete within a practical trace window.

Ruling: this is a genuine but **terminating** guest crash path. The default
route is now `none` (inert, no guest call, no withhold), so the honest fault
baseline is preserved. All crashpad routes remain explicitly opt-in via
`SOBER_FLAGS_LATCH_ROUTE` and force the marker withhold. No byte writes and no
gate weakening.

## 6. Standing verdict

Callback leg remains **0/1**. No notifier hit at `0x27eaf6e`, no RA pair
`0x2584960 → 0x27eafb3`, no `gameLoadedCallback(J)V`. Score stays ≈61% and the
marker stays WITHHELD under decisions 013/038.
