# Decision 038 — Strict gate preserved: the ALT thunk is diagnostic-only, never app-shell proof

Status: accepted (lead). Date: 2026-11-20.

Authority: reaffirms decision 013; amends decision 037; closes the open
inconsistency at 013:46-51 and the `tests/app-shell.sh` contradiction recorded
in D03, D06, D10 and D11. The user's ruling is unchanged: **gate strict;
`onAppBridgeNotification` rejected as app-shell proof.** Nothing here relaxes
the done condition; no success is claimed.

## 1. The ALT thunk is diagnostic-only

`SOBER_ALT_CALLBACK_PATH` and the guest thunk it calls at RVA `0x25848e0`
(`gameLoadedCallback(J)V` via `CallStaticVoidMethodV`, JNIEnv `+0x470` / slot 142)
are diagnostic-only. Host code calls the thunk directly, skipping the real
app-start path, so they prove only that our bridge can receive a guest thunk call
and that the phase-3 render RVA ran. They **never** prove a login/app-shell frame
and must never satisfy or contribute to the `app shell rendered` gate.

The knob stays default-OFF and remains the first term of the `seeded_state`
withhold chain, so any ALT run force-withholds the real marker. The
`HONESTY NOTICE` is documentation, not a pass banner; its current unreachability
is correct strengthened behaviour. Option A of the earlier decision memo (make
the thunk the proof) is rejected.

## 2. What the real marker requires

`emit_marker("app shell rendered\n", "SOBER_SHELL_MARKER")` may execute only when
all of the following hold in one run on the frozen engine (BuildID `204ad049…`,
SHA `3e498d6494…`):

1. **Callback leg (C), notifier-driven:** the guest's own state machine delivers
   `gameLoadedCallback(J)V` from notifier `0x27eaf6e` with `RSI = placeId != 0`,
   cached class `0x72a2f48` / method `0x72a2fc0` non-NULL, calling thunk
   `0x25848e0` and returning to `base+0x27eafb3`. A name-only counter is
   necessary but not sufficient; notifier-hit and thunk-return-address
   provenance must be enforced by the gate itself with all opt-ins unset.
   `onAppShellReloadNeeded()V` qualifies only if independently shown
   notifier-driven (it has no native reader in this build).
2. **Render leg (R):** phase-3 guest render `0x649d422` executed
   (`g_guest_swaps >= 1`), `sober_render_phase3_done() == 1`, with
   `guest init complete` and `first frame rendered` present. Render alone is
   never sufficient.
3. **Clean-state leg (S):** `seeded_state == 0` — every guest-mutating/opt-in
   `SOBER_*` knob unset, per the live OR-chain in `guest_app_shell.c`. No
   `marker withheld: diagnostic guest-state seed active`, no `ALT PATH` /
   `ALT PATH guest call 0x25848e0`, no `HONESTY NOTICE`. `onAppBridgeNotification`
   (`0x72a3028` → `0x3dea91`), accessor getters, synthetic seeds and every
   harness-triggered thunk are disqualified.

Until native provenance instrumentation exists to establish leg (C), the coded
gate cannot prove it: the marker stays **withheld** and no run qualifies.

## 3. `tests/app-shell.sh` contract

Options **B + C** of the earlier memo are adopted; Option A is rejected. The
stale ALT-thunk assertion is removed. The test asserts the *absence* of
ALT/HONESTY lines and synthetic-state withhold lines, requires a qualified
callback name plus phase-3 render, and requires notifier/return-address
provenance as the decisive condition. It stays **red** (no marker) until genuine
default-path proof exists; a green from re-enabling the thunk is a false green
and forbidden. It must not set `SOBER_CALLBACK_PROVENANCE` (a diagnostic opt-in
and withhold entry) as proof.

## 4. Separate diagnostic marker

Phase-5 coverage is retained, if at all, only in a separate default-OFF,
explicitly-labelled diagnostic with its own token and marker file (e.g.
`ALT-CALLBACK EXERCISED (DIAGNOSTIC) (NO APP-SHELL FRAME PROVEN)` and
`SOBER_APP_SHELL_ALT_MARKER`), modelled on `tests/fault-baseline.sh`, with a
mandatory honesty notice. It must be absent from ctest, CMakeLists.txt and
verify-native.sh, must never emit or check `app shell rendered`, and reports data
only (exit 0). Any C-level diagnostic emission must use a distinct function,
token and file and **must never call** `emit_marker("app shell rendered\n", …)`.

## 5. Verification and honesty limits

Verifier: ALT-alone and every opt-in emit zero markers; default stays
`callback=0 render=1` with zero markers; phase-2/3 markers, verify-native.sh,
ctest 2/2, libc-only ldd and engine SHA/BuildID unchanged. This ruling changes
no guest behaviour: ALT is already withheld, so the ALT `HONESTY NOTICE` is
unreachable — a strengthening, not a license to remove withhold entries.
`onAppBridgeNotification` remains rejected. No login/app-shell frame has been
proven.
