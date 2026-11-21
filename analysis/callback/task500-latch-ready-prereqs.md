# Task #500 — latch-ready callback prerequisites (id=1 node + app singleton / app+0x488)

Owner: **worker-3** (task #500). Scope: `src/jni/publish_default.*`,
`provenance_gate.*`, `startgame_v2.*`, `app_singleton_bridge.*` and this file.
Engine frozen: `/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so`,
BuildID `204ad049599c96afb75b8a21d5051063e821d6c9`,
SHA256 `3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335`
(verified on disk before analysis). Target bytes are untrusted DATA.
Marker stays **WITHHELD**; decision 013/038 binding. No fabricated callback,
publish, placeId, node, descriptor or vtable; no guest byte patches.

## 1. Verification of the id=1 MessageBus registration `0x253cb56(1,&0x7234520)`

All addresses below are RVAs of the frozen object (first `R E` PT_LOAD has
RVA == file offset; confirmed with `readelf -lW`).

* The guest itself makes this exact call at the end of
  `nativeAppBridgeV2InitWithParams` (entry `0x24abf96`):

  ```
  24ad699: lea  0x4d86e80(%rip),%rsi   # 0x7234520   (&slot_global)
  24ad6a0: mov  $0x1,%edi                            (event_id = 1)
  24ad6a5: call 0x253cb56                            (registration fn)
  ```

  So our host-side signature `reg(1, b + 0x7234520)` in `pd_ensure_node()` is
  **correct** (arg1 = event id, arg2 = address of the cached slot global).

* Inside `0x253cb56` the id=1 GameLoaded descriptor is built at **`0x253d06e`**:

  ```
  253d064: mov  $0x4,%edi ; call 0x24a8e44   ; MessageBus service (bus id 4)
  253d06e: lea  0x6dbbf98(%rip),%rcx         ; descriptor vptr 0x6dbbf98
  253d075: lea  -0x2a0(%rbp),%r14            ; stack descriptor
  253d07c: mov  %rcx,(%r14)                  ; desc+0x00 = 0x6dbbf98
  253d07f: movq $0x1,0x8(%r14)               ; desc+0x08 = 1  (GameLoaded id)
  253d087: movq $0x0,0x10(%r14)              ; desc+0x10 = 0
  253d08f: mov  %rbx,0x18(%r14)              ; desc+0x18 = &0x7234520 (RSI)
  253d093: mov  %r14,0x20(%r14)              ; desc+0x20 = self
  253d09d: call 0x253e126                    ; register node into bus +0x288
  ```

  The other registrations in the same function use different ids
  (`0x79`, `0x81`, `0x69`, `0x71`, `0x31`, `0x89`, ...) and the same slot;
  only the `0x253d06e` block is id=1 / vptr `0x6dbbf98`. The constants used by
  `sober_publish_default_descriptor()` (vptr `0x6dbbf98`, id 1,
  slot `0x7234520`) are therefore **correct**.

* Node layout built by `0x253e21e` (called from `0x253e180` with a `0x60`
  guest allocation from `0x1dbab02`), matches `publish_default.h`:

  ```
  253e232: node+0x00 = 0x100000000
  253e23c: node+0x08 = 0x27eaf04     ; dispatch fn (publish_default NODE_DISPATCH)
  253e24a: node+0x20 = *(bus+0x288)  ; intrusive next
  253e266: node+0x18 |= 4
  253e271: node+0x28 = 0x246fb6a
  253e27c: call 0x226c048(node+0x30, descriptor)
  ```

  `0x226c048` reads `descriptor+0x20` (self) and stores it at `node+0x30+0x20`
  == **`node+0x50`** — exactly the `SOBER_PD_NODE_OFF_DESC_SELF` used by
  `find_node()`/`descriptor()`. The `+0x288` list head used by `find_node()`
  is confirmed by `0x253e142: lea 0x288(%rdi),%rsi` in `0x253e126`.

* The bus getter `0x24a8e44(4)` returns the default MessageBus object at
  `0x72cbdd0` after arming one-shot guards `0x72cc270` (A) and `0x72cc718`
  (B); for arg 3 it returns `0x72cc278`. This is why `find_node()` walks both
  buses and gates the mode-3 bus on those two guards — also **correct**.

**Conclusion:** the registration call, descriptor identity, node layout and
bus list offsets in `publish_default` are all genuine and correctly encoded.
There is no fault in the RVA set itself.

## 2. Exact faults found (and fixed)

### F1 — one-shot guard consumed before registration prerequisites (UPSTREAM)
`0x253cb56` tests the guard at `0x253cb84` and **sets it at `0x253cb91`, before
any descriptor is built at `0x253d06e`**. If the guest's one call to
`0x253cb56` (from `0x24ad6a5`) is entered while the MessageBus service or app
state is not ready, the guard becomes 1, registration is skipped/faulted, and
no later host re-drive can ever register the id=1 node (a second call exits at
`0x253d1b7`). Observable signature: `guard@0x72a31ba == 1` and
`find_node() == NULL`. **This is the root prerequisite fault** and can only be
fixed upstream (lead-owned `guest_app_shell.c`): the MessageBus/app
prerequisites must exist *before* the guest reaches `0x24ad6a5`. Our module
cannot repair it without patching guest bytes or writing the guard, which is
forbidden. We now detect and report it precisely (see F2/F3).

### F2 — blind re-drive ignored the spent guard and had no kill switch (FIXED)
`pd_ensure_node()` previously called `reg(1, &slot)` whenever the node was
absent, even when the guard was already 1 (inert) — a misleading loop, and a
diagnostic guest call with no on/off control. Fixed:
* read the guard at `0x72a31ba` first; if spent and node absent, emit an exact
  `WITHHELD` diagnostic and do **not** re-drive;
* new kill switch `SOBER_PUBLISH_NODE_REDRIVE` (default enabled; exact
  `0`/off/no/false disables) so the diagnostic guest call can be turned off.

### F3 — verify() collapsed all upstream faults into `-3 NO_NODE` (FIXED)
`sober_publish_default_verify()` checked only canary/APS2 relocs/JNI globals
and the node, so a missing guard/app/MessageBus/JNI state could not be told
apart from a broken relocation. Added read-only
`sober_publish_default_prereqs()` returning a six-bit flag mask
(`CHAIN,JNI,BUS,NODE,PLACE_ID,APP`), the raw guard and bus-guard bytes, the
bus/app/node pointers and the sampled placeId, plus the first failing result
code. No guest call, write, callback or publish is performed by this query.

### F4 — no read-only app singleton / app+0x488 readiness query (FIXED)
The closure `0x2ec1340` reads the app singleton through `0x30ef840`, which
requires `0x72cb5e0 != NULL`; the game-load consumer at `0x320b0ba` reads the
inner object from `app+0x488` and faults at `0x31a1360` when it is NULL.
`app_singleton_bridge` had a pointer accessor only. Added read-only
`sober_app_singleton_app488_ready()` / `_app488_inner()` (no knob, no state
change) and included them in `sober_app_singleton_describe()`.

## 3. Ordered prerequisites for the latch-ready chain (once latch=1)

1. **Flags/scheduler latch** `0x7cdd149 == 0` — worker-2/lead (still blocked;
   not in this task's files). Until repaired, step 1/3 never run.
2. **MessageBus service** (`0x24a8e44(4)` → `0x72cbdd0`, guards `0x72cc270` /
   `0x72cc718`) available *before* the guest call at `0x24ad6a5`, so the
   one-shot guard at `0x72a31ba` is spent only after registration succeeds.
3. **id=1 node registered**: `0x253cb56(1,&0x7234520)` builds vptr
   `0x6dbbf98` / id 1 / slot `0x7234520`, node `+0x08=0x27eaf04`,
   `+0x20=next`, `+0x50=desc`, inserted into bus `+0x288`.
4. **App singleton** `0x72cb5e0 != NULL` (guest ctor via
   `app_singleton_bridge` replay/slice) and its vptr transport intact for
   `0x30ef840` (which also gates on GL predicate `0x6096928`).
5. **app+0x488 inner object non-NULL**, built by genuine guest code
   (`app488_default`, default-ON; guest CREATE branch `0x24af26a` /
   `0x3190869` / outer init `0x253c93c` / inner ctor `0x31a0a52`).
6. **placeId** cached at `0x72a31c0` from `StartGameParams.placeId()J`
   (`0x2ebd51a` → `0x2ebd86a`) must be `> 0` — already hardened in the
   provenance gate; `publish_default` refuses `place_id <= 0`.
7. **APS2 relocs** live: `0x6dbbfc8→0x27eaf4c`, `0x7234520→0x6dbd860`,
   `0x6dbd860→0x27eaf6e`; JNI class/method globals `0x72a2f48`/`0x72a2fc0`.
8. **Credential RA pair** `0x2584960` (thunk return) → `0x27eafb3`
   (notifier return) observed by `provenance_gate`, with `seeded_state==0`.

When all hold, the guest chain `0x2ebd51a → 0x2ec1340 → (SessionReporter →
MessageBus event 1) → node dispatch 0x27eaf04 → 0x27eaf2f → 0x27eaf4c →
notifier 0x27eaf6e → 0x25848e0` runs with return address `0x27eafb3`.

## 4. Code changes in this task

* `publish_default.h/.c`: new kill switch `SOBER_PUBLISH_NODE_REDRIVE`; new
  read-only `sober_publish_default_prereqs()` + `SoberPublishPrereqs`; RVAs
  `0x72a31ba` (guard), `0x72cb5e0` (app), `0x6096928` (GL predicate);
  `pd_ensure_node()` now detects the spent guard and honours the kill switch;
  `describe()` reports the redrive state.
* `app_singleton_bridge.h/.c`: read-only `app488_ready()` / `app488_inner()`
  and `+0x488` / accessor / predicate RVAs; `describe()` extended.
* No change to `provenance_gate.*` or `startgame_v2.*` — their credential and
  24-getter wiring were verified intact for this chain.
* No CMake, engine, marker, guest_app_shell or test file was edited.

## 5. New knob — report to lead for `seeded_state`

`SOBER_PUBLISH_NODE_REDRIVE` (in `sober_publish_default_env_vars()`):
* default **ENABLED** (unset or any non-falsy value) — preserves behaviour;
* exact `0`/`off`/`no`/`false` = **kill switch**, makes the diagnostic
  `0x253cb56` re-drive an inert no-op;
* the re-drive it controls is a **guest-state seed**, so it stays in the
  forced-withhold chain through `sober_publish_default_should_withhold()`
  (which counts actual `g_register_calls`/`g_publish_calls`/hits). The knob can
  only *remove* a seed; it can never make a seeded run look clean. Recommend
  lead add the variable name to the diagnostic env dump, but NOT treat setting
  it as clearing `seeded_state` — `seeded_state` is already computed from the
  actual re-drive counters.
* `sober_publish_default_prereqs()` and the new `app_singleton_bridge` query
  are read-only: no knob, no guest state, no `seeded_state` impact.

## 6. Hand-off to lead (files not owned here)

1. Ensure the MessageBus/app prerequisites of §3 item 2 exist *before* the
   `0x24abf96` call in `guest_app_shell.c` (~line 1442), so the guard at
   `0x72a31ba` is not consumed prematurely (fault F1).
2. Optionally call `sober_publish_default_prereqs(base, &p, buf, sizeof buf)`
   near the existing `publish_seeded` diagnostic (~line 1613) and log the
   one-line report; call `sober_app_singleton_app488_ready(base)` for the
   app+0x488 prerequisite. Both are read-only and safe on the default path.
3. Keep every callback-leg knob in `seeded_state`, including the new
   `SOBER_PUBLISH_NODE_REDRIVE` as described in §5.

## 7. Honest status

The callback credential remains hardened and the default path stays
**0/1** (no run reaches `0x27eaf6e`) while the latch is blocked by worker-2.
No fabricated callback, publish, placeId, node or provenance line was added;
the marker stays **WITHHELD**. This task delivered verification, precise fault
detection, a kill-switched diagnostic re-drive and read-only prerequisite
queries — not a green gate.
