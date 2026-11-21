# Task #530 — clean id=1 node registration via a guest-entered path

Owner: **worker-1** (task #530). Owned files touched:
`src/jni/publish_default.{h,c}` (one additive read-only probe),
`analysis/callback/task530-clean-node.md` (this note).
No edit to `guest_app_shell.c`, CMake, tests, docs, gate or git. No guest byte
write, no guard write, no fabricated node/descriptor/vtable/callback, no direct
`0x27eaf6e`/`0x25848e0` call, no marker. Marker stays **WITHHELD** (013/038).
Target bytes/strings are untrusted DATA.

Engine (re-verified on disk at the start and end of this task):
`/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so`,
SHA-256 `3e498d649421115bdbea69f78e2b818d4351d8990db60e232d2aef5b741335`,
BuildID `204ad049599c96afb75b8a21d5051063e821d6c9`. Unmodified.

---

## 1. Verdict

**Proven impossible in this harness with the task's proposed mechanism.**
Reordering `nativeAppBridgeAppStart 0x247dc8d` to run *before*
`InitWithParams 0x24abf96` — even combined with the direct `0x24a8e44(4)` bus
prime and the `0x25448f6` pre-init — does **not** make the guest enter
`0x24ad6a5 -> 0x253cb56`. In every A/B run the one-shot guard `0x72a31ba`
stays **0** and no genuine id=1 node is discovered after `InitWithParams`.

The task's premise is therefore only half right: AppStart does reach the bus
getter `0x24a8e44(4)` (`0x24a8daf` inside helper `0x24a8d8c`), but **bus
readiness is not the missing prerequisite**. The bus is already fully armed
(`bus_guards=1/1`) at the checkpoint where the guard is still 0. The real
missing prerequisite is the guest's own DataModel lifecycle listener firing —
the only path that calls the registration function — and that requires ART /
real Java / Lua DataModel state this native-only harness does not provide.

Separately and independently, even a genuinely registered node would still be
rejected by the provenance gate unless the node dispatch `0x27eaf04` is entered
from guest `.text`; the harness has no guest-side publisher for event 1. Both
halves of a clean notifier-driven callback are missing (§5, §6).

No knob was added. The one code change is a read-only probe (§7).

---

## 2. Static proof of the exact registration path (frozen image)

### 2.1 The single guest call site
`objdump -d -M intel` of the frozen SHA:
```
24ad699: lea rsi,[rip+0x4d86e80]  # 7234520     (&slot_global)
24ad6a0: mov edi,0x1
24ad6a5: call 253cb56                          (id=1 registration helper)
```
A whole-`.text` scan (`grep 'call 253cb56'`) finds **this one call only**.
`0x253cb56` spends its one-shot guard at `0x253cb91` (13 bytes in, before the
descriptor is built at `0x253d06e`) and exits silently at `0x253d1b7` if it is
already set. A whole-image byte scan finds only the read at `0x253cb84` and the
write at `0x253cb91` for `0x72a31ba` — no reset anywhere. So `guard==0` means
`0x253cb56` was never entered.

### 2.2 `0x24ad6a5` lives in an internal function reached only via a vtable slot
`0x24ad6a5` is inside internal function `0x24ad50a` (its own prologue), not in
the body of `0x24abf96`. There is **no** direct `call`/`jmp 24ad50a` anywhere in
`.text`. APS2 decode (blob `0x15e00`, all `R_X86_64_RELATIVE`) gives:
```
off=0x6dbc7f8  info=0x8  addend=0x24ad50a
```
i.e. at load the guest writes `base+0x24ad50a` into slot `0x6dbc7f8`.
The surrounding slots form a vtable at `0x6dbc7c8`:
```
0x6dbc7c8 +0x00 -> 0x2ec004e   +0x20 -> 0x2ec00d8
0x6dbc7d0 +0x08 -> 0x2ec007c   +0x28 -> 0x253e42a
0x6dbc7d8 +0x10 -> 0x24ad3ee   +0x30 -> 0x24ad50a   <-- REGISTRATION
0x6dbc7e0 +0x18 -> 0x2ec00be   +0x38 -> 0x2ec00fc
```
This vtable is installed on a 0xe0-byte object allocated with the guest
allocator at `0x24ac57c..0x24ac593` inside `0x24abf96`
(`mov edi,0xe0; call 1dbab02; mov [rbx], vtable 0x6dbc7c8`). It is the
DataModel lifecycle-listener family (the `0x2ec00xx` neighbours are
`AndroidSingleDataModelLifeCycleListener` methods; `0x2ec0381` fires the sibling
`gameDidLeave`). **The guest reaches `0x253cb56` only when that listener's
slot +0x30 is invoked by the guest's own lifecycle**, not when InitWithParams
returns.

### 2.3 Why AppStart-first cannot substitute for the lifecycle
`nativeAppBridgeAppStart 0x247dc8d` (`objdump` of the frozen SHA):
- reaches `0x24a8e44(4)` via helper `0x24a8d8c` at `0x24a8daf` — confirmed;
- its own object construction uses vtables `0x6dbc4f8` / `0x6dbc5b8`, **not**
  `0x6dbc7c8`, and it dispatches through the `0x2eb6b2a` global sink;
- prior analysis (decision C, `analysis/dispatch/dispatch-map.md §9`,
  `analysis/callback/callback-map.md §115`) records that it re-enters the
  broken `0x233319a` teardown path on a foreign handle.
- `analysis/scale-a3-java-call-order.md §2.4` states the smali does **not**
  fix the relative order of `q3()/M()` (AppStart) vs `Q2()`
  (InitWithParams) — "AppStart primes the bus first" is an assumption, not a
  decoded fact on this build.

So AppStart can prime the bus but cannot fire the lifecycle listener slot that
calls `0x24ad50a`.

---

## 3. Runtime A/B (fresh builds; engine SHA/BuildID unchanged)

Scratch copy of the repo at `/tmp/opencode/w1-scratch` (the real tree was never
edited by this experiment). The harness was extended with one opt-in diagnostic
`SOBER_W1_APPSTART_FIRST` that calls `0x247dc8d(env,clazz,url,ua,0,aid,plat,"")`
immediately **before** the `0x24abf96` call and skips the later AppStart call.
Baseline env for all runs (matches `w4-final.log`):
`SOBER_CRASHPAD_RETURN=1 SOBER_FAITHFUL_JNI_ARRAYS=1 SOBER_GAME_LOADED_PATH=1
SOBER_APP_SHELL=1 SOBER_RENDER=1`, with `SOBER_PUBLISH_NODE_REDRIVE=0` so a host
re-drive cannot manufacture a node.

| Run | Knobs (beyond baseline) | Result at the post-InitWithParams checkpoint |
| --- | --- | --- |
| A — baseline | none | `InitWithParams rc=-6066930334832433271` (0xabcdef… sentinel, same as `w4-final.log`); prereqs `node=0 guard@0x72a31ba=0 bus_guards=1/1`; `publish -> rc=-3` |
| B — **AppStart first** | `SOBER_W1_APPSTART_FIRST=1` | `AppStart before InitWithParams -> rc=-6066930334832433271 bus=(nil) guard=0`; then `prereqs node=0 guard=0 bus_guards=1/1`; `publish -> rc=-3` |
| D — AppStart first + prime | `SOBER_W1_APPSTART_FIRST=1 SOBER_MESSAGEBUS_PRIME=1` | prime succeeded (`bus` valid), but `prereqs node=0 guard=0 bus_guards=1/1`; `publish -> rc=-3` |
| C — full stack | B + `SOBER_MESSAGEBUS_PREINIT=1` (`0x25448f6`) + `SOBER_REAL_APPSTATE_PATH=1 SOBER_REAL_APPSTATE_FLAG=1` | **SIGSEGV (139)** before AppStart — the known `0x25448f6` pre-init instability (`task520-node-registration.md §6`) |
| prime-run.log (prior) | `SOBER_MESSAGEBUS_PRIME=1`, direct `0x24a8e44(4)` | `prime -> 0x7f7ad0cefdd0 guard=0` succeeded; `prereqs node=0 guard=0`; re-drive reported `node=(nil)` |

**Conclusion:** priming the bus (by AppStart, by the internal getter, or by
`0x25448f6`) is necessary but never sufficient. The one-shot guard is never
spent by the guest, so the node is never genuinely registered.

Read-only probe added in this task (§7) at the pre-publish checkpoint, re-drive
off, run B/D baseline:
```
[w1-probe] guard@0x72a31ba=0 bus_guards=1/1 node=(nil)
           listener_slot@0x6dbc7f8=0x7f1d1d0fd50a
           (expect 0x7f1d1d0fd50a=base+0x24ad50a)
           host_redrive=0 node_present=0 guest_self_registered=0
```
The listener slot **is** correctly relocated and live (it equals
`base+0x24ad50a`), yet the guest never invokes it. This isolates the blocker
precisely: it is the lifecycle *event*, not the listener object, the vtable, or
the bus.

---

## 4. Why the watchdog/provenance gate would still reject even a genuine node

The strengthened gate (`analysis/callback/task521-notifier-readiness.md §2`)
requires, in one 3-frame walk from the notifier RA:
```
[rbp_not+8] == base+0x27eaf32          (descriptor invoke leg ran)
[rbp_node+8] in guest .text            (node 0x27eaf04 entered by guest)
```
The current `sober_publish_default_publish()` calls `0x27eaf04` **directly**
from host code, so `[rbp_node+8]` is a harness return address and the gate
scores UNCLASSIFIED — correctly. A genuinely registered node does not change
this: the dispatcher that calls `[node+0x08]` is the guest's MessageBus
publisher for event id 1, which is reached only from the DataModel lifecycle
(`SessionReporterState_GameLoaded`, notifier log string `0x36c7c9` at
`0x27eafb8`). The harness has no guest-side publisher for event 1; the host
re-drive is the only publication source and is, by construction, host-entered.
Routing through a host-entered guest publish helper would still be a host seed
and could not make `seeded_state == 0`, so it is not a clean solution and was
not added.

---

## 5. Exact missing prerequisites (why impossible here)

For a clean, guest-entered id=1 registration **and** dispatch, all of the
following must exist simultaneously; none is present in this harness:

1. **Guest-driven lifecycle listener.** Something must invoke vtable slot
   `0x6dbc7f8` (`0x24ad50a`) from guest `.text`. That is the
   `AndroidSingleDataModelLifeCycleListener` event chain, which needs a real
   Lua DataModel reaching `SessionReporterState_GameLoaded`. The harness has no
   ART/Looper/Activity/DataModel and no guest-side publisher for event 1.
2. **Real JNI object fidelity.** `0x24abf96` and `0x253cb56` consume the
   `InitParams` jobject and the FFlag-control-block / listener objects through
   JNI getters. The harness returns a `0xabcdef…` sentinel from these exports
   and uses synthetic objects (`native-object shapes rc=-1`,
   `JSON flag override bridge rc=-2` in the logs); the object graph that would
   call slot +0x30 is never completed. This is the decision-C / `0x233319a`
   foreign-handle gap (needs the writer-side fix, not a byte patch).
3. **Notifier prerequisites** (already tracked by worker-2): JavaVM global
   `0x7cdec78` live, cached class/method `0x72a2f48`/`0x72a2fc0` genuine,
   placeId `0x72a31c0 > 0` from the real `0x2ebd51a` export. In the harness
   these are seeded by withhold-listed knobs, so `seeded_state != 0`.
4. **Guest frame prerequisites** (worker-3/4): real surface lifecycle
   (`0x2773194`, never called), `0x649d422` guest draw, flags-loaded latch.

None of these is a bus-readiness problem, which is why the task's
AppStart-primes-the-bus hypothesis cannot close the gap.

---

## 6. Decision 013/038 posture and knobs

- **No new knob.** The scratch-only `SOBER_W1_APPSTART_FIRST` was used for the
  A/B and exists **only** in `/tmp/opencode/w1-scratch`; it is not in the repo,
  not compiled into the delivered build, and needs no `seeded_state` entry.
- The additive read-only probe needs no knob, changes no guest state, and has
  no `seeded_state` impact.
- Existing worker-1 knobs unchanged: `SOBER_PREINIT_BUS` (default OFF, opt-in,
  kill switch, force-withheld), `SOBER_PUBLISH_NODE_REDRIVE` (default ON,
  falsy kill switch, force-withheld), `SOBER_PUBLISH_DEFAULT`. The re-drive
  remains a diagnostic seed and must stay in `seeded_state`; a host-entered
  `0x253cb56`/`0x27eaf04` never becomes a provenance GENUINE.
- Marker **WITHHELD**. `tests/app-shell.sh` stays RED. No gate weakening.

---

## 7. Code change delivered (owned files only)

`src/jni/publish_default.h`: added `SOBER_PD_RVA_LIFECYCLE_VTABLE (0x6dbc7c8)`,
`SOBER_PD_OFF_LIFECYCLE_REGISTER (0x30)`, `SOBER_PD_RVA_REGISTER_FN
(0x24ad50a)`, `typedef struct SoberPublishSelfReg` and the declaration of
`sober_publish_default_selfreg_probe()`.

`src/jni/publish_default.c`: added `sober_publish_default_selfreg_probe()` —
100% read-only. It samples guard `0x72a31ba`, bus guards `0x72cc270/0x72cc718`,
a genuinely discovered id=1 node (`find_node()`), the relocated listener slot
`*(base+0x6dbc7f8)`, and the module's own `g_register_calls` counter. It sets
`guest_self_registered` only when a node is present, the guard is spent, **and**
this module never re-drove `0x253cb56` — so a host seed can never be relabelled
as a clean guest registration. No guest call, no guest write, no callback.

Lead-facing use (lead-owned wiring, not performed here), at the existing
pre-publish diagnostic next to `sober_publish_default_prereqs()`:
```c
SoberPublishSelfReg sr; char b[320];
sober_publish_default_selfreg_probe((void *)base, &sr, b, sizeof(b));
fprintf(stderr, "[app-shell] selfreg probe: %s\n", b);
if (!sr.guest_self_registered)
    fprintf(stderr, "[app-shell] id=1 node NOT guest-registered -> WITHHELD\n");
```

---

## 8. Build / verification evidence

- Real repo: `cmake --build build -j4` -> **rc=0** (sober_jni +
  guest-init-host + jni_probe; only pre-existing warnings).
- Scratch A/B harness: fresh `cmake` configure + `cmake --build build -j4`
  -> rc=0; runs A/B/C/D as tabulated in §3.
- Engine identity re-checked after all runs: SHA
  `3e498d649421115bdbea69f78e2b818d4351d8990db60e232d2aef5b741335`,
  BuildID `204ad049599c96afb75b8a21d5051063e821d6c9` — unchanged.
- Frozen-image evidence: `objdump -d -M intel` of `0x24ad699/0x24ad6a5`,
  `0x24ad50a..0x24ad6c2`, `0x24abf96` (`0x24ac57c..0x24ac5a4`),
  `0x247dc8d..0x247e104`; whole-`.text` `call 253cb56` scan; APS2 decode
  (`0x6dbc7f8 -> 0x24ad50a`, vtable `0x6dbc7c8` slots).
- No guest byte was written, no guard patched, no node/vtable/callback
  fabricated, no marker emitted, in the real tree or in the scratch tree.

---

## 9. Handoff to lead

1. The AppStart-before-InitWithParams reorder is **not** a clean fix: A/B shows
   guard stays 0 and no node. Do not wire it as a clean path (it would only add
   a seed and a decision-C fault). `SOBER_MESSAGEBUS_PREINIT=1` (`0x25448f6`)
   still SIGSEGVs in the full stack — keep it opt-in/withheld.
2. Optionally wire the new read-only `sober_publish_default_selfreg_probe()`
   next to the existing prereqs diagnostic (signature in §7); no knob, no
   seeded_state impact.
3. The decisive missing prerequisite is a **guest lifecycle event** invoking
   vtable slot `0x6dbc7f8 -> 0x24ad50a`, plus a guest-side publisher for event
   1 so `0x27eaf04` is entered from guest `.text`. This is outside worker-1's
   owned files and depends on worker-2/3/4 prerequisites (ART/JNI fidelity,
   decision-C object fix, real surface/render, JavaVM/placeId). Until those are
   genuinely satisfied, callback stays 0/1 clean and the marker WITHHELD.
