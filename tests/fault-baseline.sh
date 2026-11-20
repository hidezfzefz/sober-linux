#!/usr/bin/env bash
# Default-OFF diagnostic baseline (verifier, task #451 follow-up).
#
# INFORMATIONAL ONLY. Records the current, known PRE-EXISTING async fault at
# guest RVA 0x2eb4b65 for regression comparison. It is NOT a test: it never
# asserts success, never fails CI, is not wired into ctest, is not invoked by
# tests/verify-native.sh, and does not emit or check any app-shell marker.
#
# Recorded trigger: the phase-7 default no-start-app path calls
# nativeAppBridgeAppStart (0x247dc8d) with the six real JNI String arguments
# (url, ua, false, androidId, platform, empty). Its worker thread reaches the
# unseeded JNI_OnLoad string-helper global at guest 0x7cdec78 and faults at
# 0x2eb4b65 (rdi=NULL). The global gap itself is pre-existing - this harness
# skips JNI_OnLoad; clean HEAD b8bdab0 faults earlier at 0x2456079 instead.
# Compare against that RVA on future runs; do not treat this as a pass/fail.
#
# Explicit invocation only:
#   bash tests/fault-baseline.sh
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
GUEST="/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so"
HOST_BIN="$ROOT/build/guest-init-host"
OUT="/tmp/sober-fault-baseline.out"
ERR="/tmp/sober-fault-baseline.err"

if [ ! -f "$GUEST" ]; then
    echo "SKIP: guest engine not extracted at $GUEST"
    exit 0
fi
if [ ! -x "$HOST_BIN" ]; then
    echo "SKIP: harness binary not built at $HOST_BIN"
    exit 0
fi

rm -f "$OUT" "$ERR"

# Exact default no-start-app environment from task #451. No SOBER_START_APP,
# no phase-7 diagnostic seeds, no ALT callback path, no marker env vars.
SOBER_RENDER=1 SOBER_APP_SHELL=1 SOBER_SCHEDULE_RUNNABLE=1 \
SOBER_APP_SHELL_WAIT_MS=3000 EGL_PLATFORM=x11 LIBGL_ALWAYS_SOFTWARE=1 \
LD_LIBRARY_PATH="$ROOT/build/ndk-stubs${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
timeout 90 "$HOST_BIN" "$GUEST" >"$OUT" 2>"$ERR"
harness_rc=$?

echo "== fault baseline record (informational; harness exit $harness_rc) =="
echo "harness: $HOST_BIN"
echo "guest:   $GUEST"
echo "sha256:  $(sha256sum "$GUEST" | awk '{print $1}')"
echo "BuildID: $(readelf -n "$GUEST" 2>/dev/null | awk '/Build ID:/{print $3; exit}')"

echo "-- fault record --"
grep -E '^\[harness\] (regs:|signal=|rip maps to|faulting guest RVA:)' "$ERR" \
    | tail -8 || true

echo "-- bridge / app-shell summary --"
grep -E '^\[app-shell\] (bridge=|seeded guest JNI state|JNI args|guest call)|^\[sober-jni\] seeded' "$ERR" \
    | tail -15 || true

echo "-- preserved phase-2/3 markers --"
grep -E '^(guest init complete|first frame rendered)$' "$OUT" || true

echo "== end baseline record (harness exit $harness_rc; script exit 0) =="
exit 0
