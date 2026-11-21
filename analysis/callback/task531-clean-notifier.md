# Task #531 — clean (seeded_state==0) notifier / provenance readiness

Owner: **worker-2**. Scope (owned files only): `src/jni/provenance_gate.{c,h}`,
`src/jni/startgame_v2.{c,h}`, `src/jni/callback_provenance.{c,h}` and this note.
Worker-1's `publish_default.*` / `app_singleton_bridge.*`, worker-3's render
files, worker-4's latch files, the lead's gate/marker/git and the engine are
**not** edited. Marker stays **WITHHELD** (decisions 013/038).

Frozen target re-verified this task:
`/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so`,
BuildID `204ad049599c96afb75b8a21d5051063e821d6c9`,
SHA-256 `3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335`
(`sha256sum` + `readelf -n`). Target bytes/strings are untrusted DATA. No guest
byte patch, fabricated node/descriptor/vtable/callback, placeId invention, host
callback or gate weakening was performed.

## 1. Verdict

The provenance credential is now **complete for all four task #531 legs** and
stays fail-closed. The missing leg is JavaVM-global liveness, which I added as
a read-only observation and as a required condition of `is_genuine()`; the
other three (cached IDs, placeId>0, guest-entered node dispatch) were already
present from #522 and remain unchanged. A new read-only `readiness()` report
lets the lead log the static legs separately from the dispatch-only legs.
**A clean `seeded_state==0` run still cannot produce a genuine callback**, because
the upstream guest-state prerequisites are absent: no clean guest-entered id=1
MessageBus node, and the flags latch stays 0 on the default (non-seeding) route.
This is an honest blocker list, not a reason to relax the gate.

## 2. Code changes (additive, fail-closed)

`src/jni/provenance_gate.h`
* `SOBER_PROV_GATE_RVA_JAVA_VM 0x7cdec78u` (line 158).
* `SoberProvenanceGateSnapshot.java_vm_global` (line 188).
* `sober_provenance_gate_java_vm_global()` (line 259).
* `SOBER_PROV_GATE_READY_{BASE,BUILD,JAVAVM,CACHED_CLASS,CACHED_METHOD,PLACE_ID}`
  bitmask + `SOBER_PROV_GATE_READY_ALL`, and
  `sober_provenance_gate_readiness(char *buf, size_t cap)` (lines 268–285).

`src/jni/provenance_gate.c`
* `g_java_vm` atomic + reset (lines 73, 197).
* `on_dispatch()` samples `base+0x7cdec78` read-only and requires
  `java_vm != NULL` in the GENUINE expression (lines 475–488). This is a
  *strengthening*: a NULL global can only remove a credential. Reaching the
  thunk already implies a live global (0x227b9e2 GetEnv would fault otherwise),
  so no genuine chain can be newly rejected.
* `readiness()` (line 610): lazily installs/auto-discovers the base, samples the
  six static legs, fills a neutral one-line report and returns the bitmask.
  It never calls into the guest, never emits a provenance/marker line, never
  stamps the dispatch-time observations, and a kill switch/NO_BASE clears the
  bits rather than faking readiness.
* Snapshot / `describe()` extended with `jvm=… placeId=…` (lines 728, 740–764).

`src/jni/startgame_v2.{c,h}` and `src/jni/callback_provenance.{c,h}`: **no
change needed**. startgame_v2's `placeId` default is a real positive `J`, but it
is an opt-in (`SOBER_STARTGAME_V2`, withhold-listed); the *clean* positive value
comes from worker-4/lead's default-ON `placeid_default`. The opt-in recorder
already requires cached-ID identity and the guest-entered node walk, and it is
force-withheld via `SOBER_CALLBACK_PROVENANCE`, so it cannot fabricate a clean
green.

## 3. Exact readiness predicate (fail-closed, at one `gameLoadedCallback` dispatch)

`GENUINE` is set iff **all** hold simultaneously in
`sober_provenance_gate_on_dispatch(name, clazz, method)`:

```
S1  SOBER_PROVENANCE_GATE enabled (default ON; kill switch -> inert)        [static]
S2  guest base resolved + frozen BuildID 204ad049… verified                 [static]
S3  name == "gameLoadedCallback"                                            [dispatch]
S4  clazz  == *(void**)(base+0x72a2f48), non-NULL cached jclass             [both]
S5  method == *(void**)(base+0x72a2fc0), non-NULL cached jmethodID          [both]
S6  *(int64_t*)(base+0x72a31c0) > 0        (real placeId jlong)            [both]
S7  *(void**)(base+0x7cdec78) != NULL       (live JavaVM global)  <- NEW    [both]
S8  thunk RA base+0x2584960 live on the stack (nearest such word)           [dispatch]
S9  notifier RA base+0x27eafb3 in the +16..+48 word window above S8          [dispatch]
S10 frame walk: [rbp_not+8] == base+0x27eaf32 (call [rax+0x30] @0x27eaf2f)   [dispatch]
S11 frame walk: [rbp_node+8] in guest .text -> node 0x27eaf04 guest-entered  [dispatch]
S12 !is_harness_thunk() (sticky global taint) and verdict != APPBRIDGE       [run]
```

Classification is disjoint: `HARNESS_THUNK` (thunk RA with no notifier RA — the
host/ALT path) sets a sticky taint and is never genuine; `APPBRIDGE`
(`0x2eb4bb1`) is disqualified; anything else is `UNCLASSIFIED`.
`sober_provenance_gate_emit_provenance()` is a one-shot *diagnostic* line that
fires only on S1–S12 with placeId>0; it is not a marker and quotes only sampled
values. The lead still ANDs this with `render_ok` and `seeded_state==0`.

### Static readiness bitmask (no dispatch required)

`readiness()` returns the OR of: BASE, BUILD, JAVAVM, CACHED_CLASS,
CACHED_METHOD, PLACE_ID. The dispatch-only legs S3, S8–S11 are **deliberately
not asserted** there; the report prints them as `dispatch-only`, so STATIC-READY
can never be mistaken for a callback credential. `READY_ALL` is the six-bit
static mask only.

## 4. Clean-state leg-by-leg posture (`seeded_state==0`)

| Leg | Clean-path provider | Clean verdict |
| --- | --- | --- |
| S2 build/frozen image | verified from the r-x mapping / NT_GNU_BUILD_ID | YES |
| S4 cached class 0x72a2f48 | bridge default `sober_jni_seed_guest_jni_state()`; `javavm_default` re-asserts via `cached_ids_live()` (guest_app_shell.c:1009) | non-NULL YES; *identity* only tested at dispatch |
| S5 cached method 0x72a2fc0 | same as S4 | non-NULL YES; identity only at dispatch |
| S6 placeId 0x72a31c0>0 | default-ON `placeid_default` (guest_app_shell.c:1641) serves `placeId()J` = 123456789; guest 0x2ebd51a stores the jlong at 0x2ebd86a | YES once the StartGameParams export runs |
| S7 JavaVM 0x7cdec78 | default-ON `javavm_default` (guest_app_shell.c:1009) installs a genuine two-level JavaVM; kill switch `SOBER_JAVAVM_DEFAULT=0`; the old `SOBER_SEED_JNI_VM` remains opt-in/withheld | YES (install) |
| S11 node guest-entered | requires the *guest* to register id=1 at 0x24ad6a5 and MessageBus to publish invoking node 0x27eaf04 | **NO — blocker B1** |

The `*_DEFAULT` env vars are withhold-listed **when set**; unset (the clean
run) they merely mean "default module active". `javavm_default` is NOT in the
chain by module status, only by knob. `publish_default` was refined so
`should_withhold()` returns 0 until it actually drives 0x253cb56 / 0x27eaf04,
so a clean `seeded_state==0` is not structurally impossible. `flags_latch_default`
is default-ON but `route=none` (inert, withhold=0), so it does not force a
seeded state either — but it leaves the latch at 0 (blocker B2).

## 5. Blockers (exact, with owner)

* **B1 — primary (worker-1, task #528, PROVEN IMPOSSIBLE clean): no clean
  guest-entered id=1 node/publish.** The only guest call to `0x253cb56` is
  `0x24ad6a5` inside guest fn `0x24ad50a`, reached only through vtable slot
  `+0x30` of the DataModel lifecycle listener `0x6dbc7c8` (relocated slot
  `0x6dbc7f8`); there is no direct caller. Worker-1's read-only
  `sober_publish_default_selfreg_probe()` and runtime A/B (re-drive OFF) showed
  guard@0x72a31ba=0 / node=0 in the baseline, with AppStart-first, and even with
  a direct `0x24a8e44(4)` bus prime; the `0x25448f6` preinit variant faults.
  The listener slot is live (=base+0x24ad50a) but never invoked, and there is no
  guest publisher for event 1 to enter `0x27eaf04` from guest .text. Bus
  readiness is not the blocker — the guest lifecycle event is. Therefore
  S8–S11 cannot be satisfied at `seeded_state==0`. No node fabrication, guard
  write, or re-drive relabeling is permitted. Evidence:
  `analysis/callback/task530-clean-node.md`.
* **B2 — lead/worker-4: flags latch 0x7cdd149 stays 0 in the clean route.**
  `flags_latch_default` documents that the only `movb $1` writers are the
  crashpad handlers, reachable only from the crash subprocess, not boot. With
  `route=none` the latch is 0, so the app-shell phase that reaches
  `0x27eaf6e` does not complete cleanly. `SOBER_CRASHPAD_RETURN` is withheld.
  Worker-4's task #532 (note `analysis/task533-clean-latch.md`) proves this
  independently: only one `nativeRunCrashpadHandler` call exists, in
  `CrashpadHandler.main` (subprocess); `nativeInitCrashpad` has no such call and
  no `0x7cdd149` reference; every clean `movb $1` route is gated off or needs a
  fabricated CppProxy. Runtime app-shell.sh: latch=0, fault at the TaskScheduler
  guard. So `java_boot_reachable()==0` on the clean path.
* **B3 — worker-3 (render leg, independent of this task):** clean guest GLES /
  G8 window proof was proved impossible without real Java surface lifecycle;
  the lead/013 gate also requires `render_ok`, so even a fixed callback leg
  would not make the marker green. Marker stays WITHHELD.
* **B4 — sandbox runtime limit (not a code defect):** this environment has no
  usable X display (`XOpenDisplay failed`, `guest_render_run` rc=-2 → harness
  rc=11) and no Xvfb, so the app-shell/JNI phase cannot be exercised here.
  Runtime confirmation of a clean callback is unavailable in this sandbox;
  verification below is build + read-only unit evidence.

## 6. Verification performed

* Frozen identity: `sha256sum` / `readelf -n` unchanged (values above).
* `cmake --build build --parallel 4` — **PASS** (`sober_jni`, `guest-init-host`,
  `jni_probe`, all targets; only pre-existing warnings in the lead's
  `guest_app_shell.c`). No CMake edit was needed: `provenance_gate.c` is
  already in the `sober_jni` target.
* Standalone unit check `/tmp/opencode/task531_readiness_test.c` compiled with
  `-O2 -Wall -Wextra -Werror` against the real `provenance_gate.c`, using
  read-only globals in anonymous memory (no guest bytes, no callbacks):
  `ALL TASK-531 READINESS CHECKS PASSED`. It confirmed: S7 bit set when the
  global is live and cleared when NULL; placeId<=0 clears its bit; kill switch
  clears every bit; `readiness()` stamps no dispatch observation and
  `emit_provenance()` still returns 0; report names all four task #531 globals
  and flags the dispatch-only legs.
* Clean `env -i` harness attempt (`SOBER_RENDER=1 SOBER_APP_SHELL=1`, no opt-ins)
  reached `guest init complete` (phase 2) then stopped at render init (B4);
  no callback, marker, or guest-state seed was produced by my code.

## 7. Handoff (lead-owned, not edited here)

Optional one-line diagnostic next to the existing gate log:
`char b[512]; sober_provenance_gate_readiness(b, sizeof b);` and log `b`, plus
`sober_provenance_gate_describe()` for the per-dispatch verdict. Keep the
binding unchanged: marker requires S1–S12 **AND** `render_ok` **AND**
`seeded_state==0`. Never call `emit_provenance()` on a seeded run. No commit or
push by this worker (lead owns git).
