#!/usr/bin/env bash
# tests/D08-dispatch-diag.sh -- worker D8 / logical Team D (default-OFF diagnostic).
#
# INFORMATIONAL BASELINE ONLY -- NOT A TEST.
# ---------------------------------------------------------------------------
# This script is never run by ctest, tests/verify-native.sh, or any build/CI
# target. The only way it runs is an explicit human invocation:
#
#   bash tests/D08-dispatch-diag.sh                 # default paths
#   D08_TIMEOUT=150 D08_WAIT_MS=5000 bash tests/D08-dispatch-diag.sh
#   D08_GUEST=/path/to/libroblox.so bash tests/D08-dispatch-diag.sh
#
# It launches the CURRENT full opt-in game-loaded stack (env inventory below,
# taken read-only from PROGRESS.md, docs/decisions/013 and
# src/linux/guest_app_shell.c) and records four things for regression comparison:
#   1. the guest fault RVA, if the harness faults;
#   2. every `guest->Java callback` line the JNI bridge logs;
#   3. the `seeded_state` / gate withhold lines (marker is force-withheld by
#      design whenever this stack is active);
#   4. the engine SHA-256 (and BuildID as context).
#
# STRICT DECISION 013 / NO MARKER EMISSION OR CHECK:
#   * all marker-file env vars (phase-2/3 as well as the decision-013 one) are
#     explicitly UNSET here; this script never asks the harness to write a
#     marker file and never emits the marker text;
#   * no SOBER_ALT_CALLBACK_PATH / harness-triggered thunk is used -- the real
#     game-loaded path is driven only;
#   * nothing here asserts, greps for, or treats as success any app-shell
#     completion marker. The withhold lines are recorded as data, not as a
#     pass condition;
#   * the script ALWAYS exits 0, whatever the harness exit status is.
#
# Faulting A/B byte-patch knobs (SOBER_VALIDATOR_PATCH, SOBER_FINAL_PATCH,
# SOBER_FIELD_NORMALISER, SOBER_OBJECT_FIX, SOBER_ALLOC_SITE_FIX, SOBER_LOG_HANDLE,
# SOBER_APP_SHELL_FIX, SOBER_CALL_SITE_FIX) and the two known-faulting appstate
# knobs (SOBER_REAL_APPSTATE_FLAG, SOBER_REAL_APPSTATE_POSTSETTINGS) are
# deliberately NOT part of this stack -- they are reverted A/B experiments, not
# the working opt-in game-loaded configuration.
set -uo pipefail   # intentionally NO -e: harness failure must not abort this script.

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
GUEST="${1:-${D08_GUEST:-/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so}}"
HOST_BIN="$ROOT/build/guest-init-host"
OUT="/tmp/sober-d08-dispatch.out"
ERR="/tmp/sober-d08-dispatch.err"
TIMEOUT="${D08_TIMEOUT:-120}"
WAIT_MS="${D08_WAIT_MS:-5000}"

if [ ! -f "$GUEST" ]; then
    echo "SKIP: guest engine not extracted at $GUEST"
    exit 0
fi
if [ ! -x "$HOST_BIN" ]; then
    echo "SKIP: harness binary not built at $HOST_BIN"
    exit 0
fi

# The full opt-in game-loaded stack. Every entry is a default-OFF opt-in from
# PROGRESS.md and/or a getenv() in src/linux/guest_app_shell.c; each one also
# lands in that file's decision-013 'seeded_state' force-withhold chain (or is a
# plain harness/logging knob, grouped separately below).
STACK_ENV=(
    # --- base harness substrate (same phase-2/3 base as fault-baseline.sh) ---
    SOBER_RENDER=1
    SOBER_APP_SHELL=1
    SOBER_SCHEDULE_RUNNABLE=1
    SOBER_APP_SHELL_WAIT_MS="$WAIT_MS"
    SOBER_WINDOW_HOLD_MS=0
    EGL_PLATFORM=x11
    LIBGL_ALWAYS_SOFTWARE=1
    "LD_LIBRARY_PATH=$ROOT/build/ndk-stubs${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

    # --- phase-8 real guest repairs (registry / global string / streams) ---
    SOBER_SEED_GLOBAL_STRING=1
    SOBER_GSEED_FORCE=1
    SOBER_GSEED_DIAG=1
    SOBER_REPLAY_FFLAG_CTOR=1
    SOBER_NATIVE_FLAGS_INIT=1
    SOBER_BIONIC_SF_FULL=1

    # --- Bionic TSD + faithful JavaVM/JNI object state ---
    SOBER_TLS_SEED=1
    SOBER_STUB_TSD_SEED=1
    SOBER_NO_SCHED_NEUTRAL=1
    SOBER_SCHED_R70_KEEP=1
    SOBER_SCHED_NEST_ONE=1
    SOBER_SEED_JNI_VM=1
    SOBER_FAITHFUL_JAVAVM=1
    SOBER_NATIVE_OBJECTS=1
    SOBER_JSON_FLAG_BRIDGE=1
    SOBER_JSON_APP_SINGLETON=1
    SOBER_SEED_JNI_SINGLETON=1
    SOBER_APP_SINGLETON_BRIDGE=1
    SOBER_HELPER_REPAIR_V2=1

    # --- synthetic app state (withheld by decision 013) + real appstate replay ---
    SOBER_SEED_USER_CONTROLLER=1
    SOBER_SEED_APP_DELEGATE=1
    SOBER_SEED_APP_488=1
    SOBER_REAL_APPSTATE_PATH=1

    # --- real app-shell + game-loaded path (no ALT thunk, no marker file) ---
    SOBER_START_APP=1
    SOBER_STARTAPP_PARAMS=1
    SOBER_LUA_APP_START=1
    SOBER_GAME_LOADED_PATH=1
    SOBER_LOG_APP_BRIDGE_NOTIFICATION=1
    SOBER_JNI_VERBOSE=1
)

# Defence in depth: never inherit any marker-file path or the thunk bypass.
unset SOBER_SHELL_MARKER SOBER_INIT_MARKER SOBER_FRAME_MARKER \
      SOBER_ALT_CALLBACK_PATH SOBER_APP_SHELL_DIAGNOSTIC

sha_before="$(sha256sum "$GUEST" | awk '{print $1}')"
build_id="$(readelf -n "$GUEST" 2>/dev/null | awk '/Build ID:/{print $3; exit}')"

rm -f "$OUT" "$ERR"

env "${STACK_ENV[@]}" timeout "$TIMEOUT" "$HOST_BIN" "$GUEST" >"$OUT" 2>"$ERR"
harness_rc=$?

sha_after="$(sha256sum "$GUEST" | awk '{print $1}')"

FAULT_RE='^\[harness\] (===== GUEST FAULT =====|signal=|rip maps to |fault addr maps to |faulting guest RVA:|faulting guest data RVA:)'

echo "== D08 dispatch diagnostic -- INFORMATIONAL BASELINE (decision 013) =="
echo "harness:      $HOST_BIN"
echo "guest:        $GUEST"
echo "engine SHA-256 (pre):  $sha_before"
echo "engine SHA-256 (post): $sha_after"
echo "BuildID:      ${build_id:-<unavailable>}"
echo "harness exit: $harness_rc (recorded, never asserted; script exits 0)"
if [ "$sha_before" != "$sha_after" ]; then
    echo "NOTE: engine SHA changed during this run (diagnostic only; not a pass/fail)."
fi

echo "-- opt-in stack (informational; all default-OFF in normal builds) --"
printf '  %s\n' "${STACK_ENV[@]}"

echo "-- guest fault RVA (if any) --"
if grep -Eq "$FAULT_RE" "$ERR"; then
    grep -E "$FAULT_RE" "$ERR" | tail -12 || true
else
    echo "none"
fi

echo "-- guest->Java callback lines --"
if grep -Fq 'guest->Java callback:' "$ERR"; then
    grep -F 'guest->Java callback:' "$ERR" | tail -40 || true
else
    echo "none"
fi
# Counter context comes from the same bridge, not from any marker.
grep -E '^\[app-shell\] (callback count|wait complete)' "$ERR" | tail -4 || true

echo "-- seeded_state / gate withhold lines --"
if grep -Eq '^\[app-shell\] marker withheld:' "$ERR"; then
    grep -E '^\[app-shell\] marker withheld:' "$ERR" | tail -10 || true
else
    echo "none"
fi

echo "== end informational baseline (harness exit $harness_rc; script exit 0) =="
exit 0
