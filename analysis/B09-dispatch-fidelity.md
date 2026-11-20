# B9 — Dispatch fidelity audit: `sr_dispatch_call` (`src/jni/native_jni.c`)

Owner: **worker B9**, logical Team B. Date 2026-11-20.
Scope: **this file only**. `src/`, `CMakeLists.txt` and the engine
`/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so` (BuildID
`204ad049599c96afb75b8a21d5051063e821d6c9`) were **read only** and unchanged.
Strict **decision 013** is assumed binding (`docs/decisions/013-no-weak-app-shell-marker.md`).

Subject of audit: `sr_dispatch_call()` at `src/jni/native_jni.c:474-516`, its
wrappers (`:519-616`), its counter/log accessors (`:1426-1454`), and the
consumers in `src/linux/guest_app_shell.c` (read only).

---

## 0. Executive verdict

1. **A genuine notifier-driven `gameLoadedCallback(J)V` is NOT suppressed or
   miscounted on the default/genuine path.** The notifier `0x27eaf6e` → thunk
   `0x25848e0` → `CallStaticVoidMethodV` (JNI slot 142 / `JNIEnv+0x470`) path
   enters the dispatcher with a correctly named `(J)V` method object
   (`0x72a2fc0`), a pure handler, and no active same-thread dispatch. It will
   increment `callback_calls`, set `last_callback = "gameLoadedCallback"`, and
   make `sober_jni_callback_was_called("gameLoadedCallback")` true.
2. **But the bridge cannot prove provenance.** The harness-triggered ALT thunk
   (`SOBER_ALT_CALLBACK_PATH`) produces *byte-identical* counter state. Only a
   guest-side discriminator (breakpoint at `0x27eaf6e` / return `0x27eafb3`,
   all opt-ins unset, no ALT/HONESTY line) satisfies decision 013 — exactly as
   `analysis/game-loaded-path.md:245-266` requires.
3. **There are four real fragilities** that *could* make a genuine callback
   invisible or misleading, all outside the normal path: (a) the boolean
   re-entry guard drops every nested dispatch silently and is never reset if a
   handler unwinds; (b) `last_callback` records the last *dispatch attempt*, not
   the last *counted callback* (observed baseline `callbacks=0 last='isVrDevice'`);
   (c) the guard's early return bypasses object-return shaping, handing the guest
   NULL in exactly the suppressed case; (d) counters/`last_callback` are plain
   non-atomic writes even though the harness explicitly waits for worker-thread
   callbacks.
4. All proposed changes are **additive / diagnostic / default-OFF**, or strictly
   behaviour-preserving. **None touches the decision-013 gate or the withhold
   list.** I recommend the lead adopt P1–P4 (§5) and reject any relaxation of
   the gate.

---

## 1. Exact dispatch mechanics (line-cited)

```
sr_dispatch_call(j, obj, method, args, is_va, want_object)   native_jni.c:474
  :477  if (!j) return 0;
  :478  if (sr_in_dispatch) return 0;        <-- guard: silent drop
  :479  sr_in_dispatch = 1;                  <-- boolean, _Thread_local (:176)
  :481  j->env_calls++;
  :482  m = sr_check(method, SR_K_METHOD);
  :483  name = m->name ? : "?";  sig = m->sig ? : "";
  :485  sr_add_log(j, "Call %s%s (obj=%p)", name, sig, obj);
  :486  snprintf(j->last_callback, ..., "%s", name);
  :488  intptr_t result = 0;
  :490  for each registered native matching method_name:   <-- name only
           result = fn(&j->env_obj, obj, name, sig, args); break;
  :499  for each handler matching name:                    <-- name only
  :502      j->callback_calls++;
  :503      h->count++;
  :504      result = h->fn(...); break;
  :510  if (want_object)
  :511      result = (intptr_t)sr_shaped_or_new(j, (void*)result, "java/lang/String");
  :514  sr_in_dispatch = 0;
  :515  return result;
```

Relevant wrappers:

- `SR_DEFINE_CALL_PLAIN/V/A` (`:519-540`) and `:551-583` pass `want_object=1`
  only for the nine `Call*ObjectMethod*` forms; every other form, including
  `CallVoidMethod*`, passes `0`.
- `sr_CallStaticObjectMethod` (`:587-595`) passes `want_object=1` and returns the
  shaped object (the `DeviceStaticParams is null` fix).
- `sr_CallStaticVoidMethodV` / `A` (`:607-616`) — **the gameLoadedCallback entry
  point** — pass `want_object=0`, so object shaping never touches this callback.
  Wired at `SETFN(*t,138,...)` = global slot 142 / `0x470` (`:837`), matching
  the thunk at `call *0x470(%rax)` (`analysis/callback/callback-map.md:31,46-53`).

Consumers (read only):

- `sober_jni_callback_was_called()` (`:1432-1441`) → `h->count > 0` by name — the
  **sticky** per-name signal the gate uses.
- `sober_jni_callback_count()` (`:1427`) → aggregate `callback_calls`.
- `sober_jni_last_callback()` (`:1429-1430`) → most recent dispatch name.
- `sober_jni_describe_activity()` (`:1443-1454`) → the
  `env_calls/callbacks/register_natives/last=` line seen in gate logs.
- Gate: `guest_app_shell.c:1331-1332` (`shell_cb` from the two named callbacks),
  `1376-1380` (force-withhold on any seeded-state knob), `1385-1400` (withhold
  and `return -4`), `1409` (sole marker emission).

---

## 2. Finding F1 — `callback_calls` counting

**Semantics.** The counter is incremented **once per dispatch, only if a named
handler matches the method name** (`:501-505`). It is *not* incremented for:
dispatches with no matching handler; dispatches suppressed by the guard;
`env_calls`-only JNI entry points (FindClass, GetMethodID, GetEnv, etc.); or the
native-method loop (`:490-497`). `h->count` is incremented in lockstep, so
`callback_was_called(name)` and the aggregate cannot disagree about a named
callback that actually fired.

**Evidence it works for the genuine path.** The harness installs the default
handler set (`guest_app_shell.c:951`), which includes `"gameLoadedCallback"`
(`native_jni.c:1059`), then explicitly rebinds that name to the pure
`shell_callback` log function (`guest_app_shell.c:952-953`, definition
`:130-136`: `fprintf` only, **no guest re-entry**). The method object the
notifier loads from `0x72a2fc0` is seeded with the exact name/sig
`"gameLoadedCallback"` / `"(J)V"` (`native_jni.c:1304`, used by
`:1337-1345`); the generic range-fill at `:1362-1376` only fills **NULL** slots
(`:1369`) and therefore cannot overwrite it. Handler matching is name-only, so
even a re-`GetStaticMethodID`'d object with the same name still counts.

**Miscount scenarios (all non-default or diagnostic):**

1. *Handler not installed.* If a configuration skipped
   `sober_jni_install_default_app_shell_handlers()` and the explicit rebinds, a
   genuine callback would still be dispatched (`env_calls++`, `last_callback`
   updated, log line emitted) but `callback_calls` and `h->count` would stay 0 →
   the gate would withhold. The current harness always installs them.
2. *Native + handler name collision.* Both loops run (`:490`, `:499`); a native
   with the same name would be invoked **and** the handler would fire, with the
   handler's result overwriting the native's. `callback_calls` still increments
   once. Default baseline has `register_natives=0`, so not currently reachable.
3. *Count is taken before the handler runs* (`:502` before `:504`). A handler
   that faults after being entered leaves a counted-but-incomplete callback.
   `shell_callback` cannot do this.
4. *Provenance blindness (critical for decision 013).* ALT path
   (`guest_app_shell.c:1312-1324`) drives the same thunk with the same method
   name; counter state is indistinguishable from the genuine notifier. The
   verification reports confirm this: ALT-alone control yields `callbacks=1`,
   marker emitted plus HONESTY NOTICE (`tests/verification-report-463.md:45`,
   `decision-memo-alt-thunk-vs-013.md:95-97`). **The counter is necessary but
   never sufficient evidence.**
5. *Non-atomic increment.* `callback_calls++`/`h->count++` (`:502-503`) are plain
   RMWs. The harness explicitly anticipates engine worker-thread delivery
   (`guest_app_shell.c:1287-1309`). Concurrent dispatches can lose updates →
   undercount. `callback_was_called()` is robust to this (one lost update still
   leaves `count >= 1`), but the "callbacks increased by ≥1" acceptance
   evidence (`game-loaded-path.md:252-255`) could be affected.

**Verdict:** no default-path miscount. Harden atomics (P4) and never treat the
counter as proof of genuineness (P6).

---

## 3. Finding F2 — `last_callback`

**Semantics.** `last_callback` is assigned from the method object's name at
`:486`, i.e. on **every non-suppressed dispatch attempt**, *before* handler
matching. It is therefore "last JNI method called", **not** "last counted
callback" and not even "last callback".

**Evidence.** Baseline gate log: `callback=0 render=1 (env calls=33 callbacks=0
last='isVrDevice')` (`tests/verification-report-471.md:36`,
`tests/verification-report-460.md:41-42`). A dispatch named `isVrDevice`
advanced `last_callback` while `callback_calls` remained 0 — proof that the two
fields measure different things.

Consequences for a genuine gameLoadedCallback:

- On successful dispatch, `last_callback` becomes `"gameLoadedCallback"` — but
  only until the **next** dispatch overwrites it. The
  `game-loaded-path.md:252` acceptance line `last='gameLoadedCallback'` is
  therefore timing-sensitive and weaker than the sticky
  `callback_was_called("gameLoadedCallback")` (which the gate correctly uses).
- A genuine callback **suppressed by the guard** (§4) leaves `last_callback`
  completely unchanged, because the early return at `:478` precedes `:486`.
  There is no "last suppressed" trace at all today.

**Verdict:** keep the existing field/API for backward compatibility (the gate
only prints it), but add a separate `last_counted_callback` diagnostic (P2) if
the lead wants `last=` to be usable as corroboration.

---

## 4. Finding F3 — object-return shaping

**Normal path is correct.** `:510-513` funnels every `want_object` result through
`sr_shaped_or_new()` (`:393-400`), which returns the raw handle unchanged only
if `sr_check()` validates our `SR_OBJ_MAGIC`; otherwise it allocates a fresh
canonical `SrObject`. So a valid handler result (e.g. the typed objects built by
`sr_default_app_shell_handler`, `:1036-1051`) passes through, and an unhandled
or zero result becomes a non-NULL shaped object. This is the decision 013/014
invariant and is sound. `gameLoadedCallback` is `(J)V`/void, `want_object=0`
(`:607-611`), so shaping does not apply to it.

**Gap on the suppressed path.** The guard returns **before** the shaping block
(`:478` vs `:510`). A nested object-returning call (e.g.
`getDeviceStaticParams`, `getWebViewUserAgent`) therefore returns raw
`0`/NULL to the guest from its wrapper (`(ret)r`, `:525`). That directly
contradicts the comment at `:508-509` ("must NEVER hand the guest a NULL") and
is the same NULL-return family that produced the `DeviceStaticParams is null`
loop described in `analysis/jni/recursion-note.md:123-132`. A genuine void
gameLoadedCallback is unaffected, but if the harness ever observes that loop it
could be the guard, not the handler.

**Verdict:** shape on the suppressed branch too (P1/P5). Strictly improves the
guest-facing invariant; does not change counting or gating.

---

## 5. Finding F4 — recursive re-entry guard

**Shape.** `_Thread_local int sr_in_dispatch` (`:176`); set at `:479`, cleared
at `:514`. Boolean, not a depth counter. The recursion note explicitly
recommended "a re-entrancy guard (thread-local depth counter) around the whole
JNI dispatch" (`analysis/jni/recursion-note.md:301-305`); the boolean is the
simplest realization and intentionally drops all nesting.

**Why the genuine path is safe.** The notifier obtains JNIEnv through the JavaVM
global (`0x227b9e2` → our `sr_GetEnv`/`sr_AttachCurrentThread`, VM table
`:754-767`), which does **not** touch `sr_in_dispatch`. The thunk then calls
`CallStaticVoidMethodV` while no dispatch is on the stack: our handlers
(`shell_callback`, `sr_default_app_shell_handler`) are pure and never call back
into guest RVAs, and the default config has no registered native (`:490` loop
empty). Cross-thread delivery is also safe because the flag is per-thread.

**Scenarios that WOULD suppress a genuine callback:**

1. **Unwind/longjmp during a dispatch.** There is no cleanup between `:479` and
   `:514`; a single early return exists only *before* setting the flag. If a
   handler (or a guest exception unwinding through a custom handler) leaves the
   function without reaching `:514`, `sr_in_dispatch` stays 1 on that thread and
   **every later dispatch on that thread is silently dropped** — no `env_calls`,
   no `callback_calls`, no `last_callback`, no log. A subsequent genuine
   notifier-driven gameLoadedCallback would be completely invisible. Low
   probability with today's pure handlers, but it is the strongest "wrongly
   suppressed" mechanism.
2. **Same-thread re-entrant delivery.** If the guest reaches the notifier from
   inside a JNIEnv callback handler (e.g. a scheduler-gated wrapper), the guard
   drops it. This is an inherent tension: the guard exists precisely to stop
   the `0x6ca7f72`/`0x6c8f340` scheduler recursion. If the genuine callback ever
   arrives via that path, suppressing it is the intended safety behaviour — but
   the lead must know it can happen rather than assume `callbacks=0` means "no
   genuine callback occurred".
3. **Diagnostic visibility.** Today none of the above leaves any trace. All
   suppressed calls are indistinguishable from "JNI was never called".

**Verdict:** do not weaken the guard by default. Convert to a depth counter with
a diagnostic log of suppressed calls (P1/P3) so the lead can *prove* whether
suppression is occurring, and only then consider a narrowly scoped opt-in
experiment (P3b), always paired with the withhold list.

---

## 6. Finding F5 — provenance and the withhold list

The dispatcher keys on `name` only (`:492`, `:501`). It has no notion of caller
RVA, return address, or thread origin, so:

- genuine notifier thunk, harness ALT thunk, and a bare host-side
  `sober_jni_*` dispatch of a method named `gameLoadedCallback` all produce the
  same `callback_calls`, `h->count`, and `last_callback`;
- the only decision-013 discriminator remains guest-side evidence
  (`game-loaded-path.md:245-264`).

Therefore **no change in this file may be used to justify emitting the marker**.
In particular, do not fold "suppressed" or "unwind-recovered" events into
`callback_calls`. The withhold list (`guest_app_shell.c:1337-1375`) must keep
disqualifying every guest-mutating opt-in; the ALT path must stay opt-in with its
HONESTY NOTICE (`:1403-1408`).

---

## 7. Minimal opt-in-safe changes proposed for the lead

All are confined to `src/jni/native_jni.c` / `native_jni.h` (lead-owned) and are
**additive, default-OFF or behaviour-preserving**. I have not applied any.

### P1 — Suppressed-dispatch diagnostic counter + log (default OFF)
Add to `struct SoberJni` (`:150-171`):
```c
uint64_t suppressed_calls;
char     last_suppressed[SR_LOG_WIDTH];
```
Replace the guard at `:478` with:
```c
if (sr_in_dispatch) {
    if (j) {
        SrObject *sm = sr_check(method, SR_K_METHOD);
        __atomic_fetch_add(&j->suppressed_calls, 1, __ATOMIC_RELAXED);
        snprintf(j->last_suppressed, sizeof(j->last_suppressed), "%s",
                 sm && sm->name ? sm->name : "?");
        if (getenv("SOBER_JNI_DISPATCH_DIAG"))
            fprintf(stderr, "[sober-jni] SUPPRESS nested Call %s (thread-local guard)\n",
                    j->last_suppressed);
    }
    /* preserve current behaviour: nothing counted, no re-entry */
    if (want_object && j)                       /* see P5 */
        return (intptr_t)sr_shaped_or_new(j, NULL, "java/lang/String");
    return 0;
}
```
Add accessors `sober_jni_suppressed_call_count()` / `sober_jni_last_suppressed()`
next to `:1427-1430`, and print them in `sober_jni_describe_activity()` only
when `SOBER_JNI_DISPATCH_DIAG` is set. **Why:** turns "suppressed" from
invisible into observable; if the genuine path ever reports a suppressed
`gameLoadedCallback`, the lead has a concrete explanation for `callbacks=0`.

### P2 — Counted-only last callback (diagnostic, default OFF)
Add `char last_counted_callback[SR_LOG_WIDTH];` and, inside the handler branch
immediately after `:503`, do
`snprintf(j->last_counted_callback, sizeof(j->last_counted_callback), "%s", name);`
Expose `sober_jni_last_counted_callback()`. **Why:** removes the
`last='isVrDevice'` vs `callbacks=0` ambiguity without changing the existing
`last_callback` contract.

### P3 — Depth counter instead of boolean (robustness, default behaviour unchanged)
`static _Thread_local unsigned sr_dispatch_depth;` (`:176`); replace `:478-479`
with `if (sr_dispatch_depth) { ...P1... }  sr_dispatch_depth++;` and `:514` with
`sr_dispatch_depth--;`. Under `SOBER_JNI_DISPATCH_DIAG`, log entry/exit depth so
a stuck non-zero depth (the unwind case of §5) is detectable.
*P3b (optional experiment, NOT recommended as default):* an env
`SOBER_JNI_ALLOW_NESTED_VOID` that permits a nested `want_object==0` dispatch to
proceed, used only to test whether suppression hides a genuine callback. This
env **must** be added to the decision-013 withhold list so it can never produce
a marker, and must be documented as re-introducing the scheduler recursion risk.

### P4 — Atomic counters (behaviour-preserving)
Use `__atomic_fetch_add(&j->env_calls, 1, __ATOMIC_RELAXED)` at `:481`,
`&j->callback_calls` at `:502`, `&h->count` at `:503`, and the suppressed
counter in P1. Treat `last_callback`/`last_counted_callback` as best-effort
(no string atomicity). **Why:** the harness waits for worker-thread callbacks
(`guest_app_shell.c:1287-1309`); non-atomic RMWs can undercount.

### P5 — Shape object on suppressed path (fidelity)
Included inline in P1: return a shaped object for suppressed `want_object`
calls. If the lead wants literally zero default change, gate that one line on
`getenv("SOBER_JNI_SHAPE_SUPPRESSED")`; my recommendation is to make it
unconditional because it strictly restores the invariant stated at `:508-509`
and only affects calls that today return NULL.

### P6 — No gate/provenance changes (explicit non-change)
Do **not** change decision 013, the withhold list, the ALT-path handling, or
fold any new counter into `callback_calls`. Keep guest-side proof
(`0x27eaf6e` entry / `0x27eafb3` return, all opt-ins unset, no ALT/HONESTY line,
phase-3 render RVA `0x649d422`) as the only acceptable green.

---

## 8. Evidence index

- `src/jni/native_jni.c:474-516` (dispatcher), `:519-616` (wrappers),
  `:393-400` (`sr_shaped_or_new`), `:1006-1022` (handler registry),
  `:1024-1052` (default handler), `:1054-1081` (handler list incl.
  `gameLoadedCallback`), `:1282-1345` (seed of `0x72a2fc0`), `:1426-1454`
  (accessors). All read only.
- `src/linux/guest_app_shell.c:130-136` (pure `shell_callback`), `:951-958`
  (handler install/rebind), `:1287-1309` (worker-thread wait),
  `:1312-1324` (ALT thunk), `:1331-1332`, `:1376-1409` (decision-013 gate).
  Read only.
- `docs/decisions/013-no-weak-app-shell-marker.md:6-13,46-51,53-64`.
- `analysis/game-loaded-path.md:25-70,200-266` (notifier, opt-in call order,
  acceptance evidence).
- `analysis/callback/callback-map.md:10-61,63-100,102-124` (thunk ABI, slot
  142 / `0x470`, risks).
- `analysis/jni/recursion-note.md:95-132,299-315` (FindClass recursion, NULL
  getter loop, guard recommendation).
- `analysis/decision-memo-alt-thunk-vs-013.md:82-111,136-192` (baseline
  `callbacks=0 last='isVrDevice'`, ALT false-green, keep all opt-ins disqualifying).
- `tests/verification-report-471.md:36,64-76`,
  `tests/verification-report-463.md:29,45-55`,
  `tests/verification-report-460.md:41-49`.
- Engine identity reaffirmed from existing records: SHA-256
  `3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335`,
  BuildID `204ad049599c96afb75b8a21d5051063e821d6c9`; **not modified**.
