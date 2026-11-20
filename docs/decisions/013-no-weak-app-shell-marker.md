# Decision 013 — No weak `app shell rendered` marker; fix TSD root cause

Status: accepted (lead).

## Ruling on worker-3's proposed fallback

Rejected as the **done condition**. A marker gated only on "bridge inited +
JNIEnv calls + some startup RVAs invoked + phase-3 render" proves JNI plumbing
was exercised, not that a login/app-shell frame was presented. It may remain
as an **opt-in diagnostic** (e.g. `SOBER_APP_SHELL_DIAGNOSTIC=1`) but the real
`app shell rendered` marker must be gated on a real guest→Java callback (e.g.
`onAppShellReloadNeeded` / `gameLoadedCallback`) plus the phase-3 guest render
call, as in decision 012.

## Root cause to fix

- Guest `pthread_getspecific` resolves to our versioned libc stub. Bionic keys
  are small integer indices; glibc's key space is unrelated, so key `2` returns
  the phase-1 `__stack_chk_guard` sentinel `0xabcdef0123456789` instead of
  NULL/valid thread state. The guest then treats that as an existing thread
  state or jobject and recurses/faults.
- Replacing the stack guard at runtime is not acceptable (breaks glibc smoke
  canaries for already-compiled code).

## Fix ownership

- **Unfreeze** `src/compat/bionic_libc.c` and
  `src/compat/gen_bionic_libc.py` for **worker-2 only** (phase-2 author of that
  shim). Implement Bionic-compatible pthread TSD: a thread-local array for
  guest keys, with our `pthread_setspecific`/`pthread_getspecific` trampolines
  operating on that array instead of glibc's key space. Keep all 411 version
  nodes, ldd-clean build, and phase-2/3 tests green.
- worker-2 also harden JNI object getters: treat sentinel/invalid handles as
  NULL rather than dereferencing them.
- worker-3: keep the strict marker gate; use the fallback only as a diagnostic
  while the TSD fix lands.

## Lead ruling (D11 / decision 038) — ALT inconsistency closed

The open ALT-thunk / `app-shell.sh` inconsistency is closed by
`docs/decisions/038-strict-gate-alt-thunk-ruling.md`, accepted as part of this
decision. `SOBER_ALT_CALLBACK_PATH` and thunk `0x25848e0` are diagnostic-only and
never proof; the ALT knob stays in `seeded_state`. The real marker requires a
**notifier-driven** `gameLoadedCallback(J)V` (notifier `0x27eaf6e`, `placeId != 0`,
return `0x27eafb3`) or a genuinely notifier-driven `onAppShellReloadNeeded`,
plus phase-3 render `0x649d422`, plus `seeded_state == 0` and no ALT/HONESTY
lines. A name counter is necessary but not sufficient. The stale ALT assertion
in `tests/app-shell.sh` was removed and replaced with strict absence checks; the
test stays red until genuine proof. `onAppBridgeNotification` remains rejected.
No weakening is authorized; no success is claimed.

## Lead note (phase 7, task #452)

`src/linux/guest_app_shell.c` now force-withholds the marker whenever any
phase-7 diagnostic seed (`SOBER_SEED_USER_CONTROLLER`,
`SOBER_SEED_APP_DELEGATE`, `SOBER_SEED_GLOBAL_STRING`) is active. This is
defence in depth only: those seeds manufacture guest state and therefore cannot
prove a login/app-shell frame.

**Open inconsistency, not resolved by this note:** `tests/app-shell.sh` still
requires `ALT PATH guest call 0x25848e0` before it accepts the marker. That is
a harness-triggered thunk and, per this decision, is not app-shell proof.
Tightening it requires an explicit lead decision and a corresponding test
change; neither has been made here. The team was asked not to modify `tests/`
unilaterally.

## Lead note (phase 8/9, current team `TEAM-XT29-DCJS-2UVE`)

Reaffirmed, not relaxed: the full opt-in stack reaches a stable start-app path
and fires a genuine, counted `onAppBridgeNotification(String,String)`
(`jni-map.md:102`, cached ID `0x72a3028` → thunk `0x3dea91`). That callback stays
**explicitly rejected** as app-shell proof — it is JNI plumbing, and seeded guest
state cannot qualify. The strict condition is unchanged: a real
`onAppShellReloadNeeded` or `gameLoadedCallback` plus the phase-3 guest render
call. No gate weakening was made (the withhold chain was strengthened); the marker remains withheld. The callback may only
be counted/logged by the opt-in diagnostic `SOBER_LOG_APP_BRIDGE_NOTIFICATION`.
Remaining blockers and the task-linked next steps are recorded in PROGRESS.md
(Phase 9); no success is claimed.
