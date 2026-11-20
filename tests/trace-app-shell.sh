#!/usr/bin/env bash
# Phase 4b diagnostic: run the app-shell harness with worker-2's LD_PRELOAD
# allocation tracer. Owned by worker-3 (#395). Does NOT change harness behavior
# and never enables any experimental guest patch.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
GUEST="/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so"
HOST_BIN="$ROOT/build/guest-init-host"
TRACER="${SOBER_TRACER:-$ROOT/build/libnative_trace.so}"
LOG="${SOBER_TRACE_LOG:-/tmp/sober-trace-app-shell.err}"
OUT="${SOBER_TRACE_OUT:-/tmp/sober-trace-app-shell.out}"

if [ ! -x "$HOST_BIN" ] || [ ! -f "$TRACER" ]; then
    echo "FAIL: harness ($HOST_BIN) or tracer ($TRACER) missing"; exit 1
fi

echo "== app-shell allocation trace =="
echo "tracer: $TRACER"
echo "log:    $LOG"
rm -f "$LOG" "$OUT" /tmp/sober-trace-arm.marker

# Arm after guest init via the init marker, so the 3618 constructors are not
# traced (worker-2's recommendation). SOBER_TRACE=1 is the tracer's own switch.
EGL_PLATFORM=x11 LIBGL_ALWAYS_SOFTWARE=1 \
LD_LIBRARY_PATH="$ROOT/build/ndk-stubs${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
LD_PRELOAD="$TRACER${LD_PRELOAD:+:$LD_PRELOAD}" \
SOBER_TRACE=1 \
SOBER_TRACE_START_FILE=/tmp/sober-trace-arm.marker \
SOBER_TRACE_SCANBYTES=1024 \
SOBER_TRACE_DUMP=1 \
SOBER_RENDER=1 SOBER_APP_SHELL=1 \
SOBER_INIT_MARKER=/tmp/sober-trace-arm.marker \
SOBER_WINDOW_HOLD_MS="${SOBER_WINDOW_HOLD_MS:-2000}" \
timeout "${SOBER_TRACE_TIMEOUT:-180}" "$HOST_BIN" "$GUEST" >"$OUT" 2>"$LOG"
rc=$?
echo "harness exit: $rc"
echo "--- trace markers ---"
grep -E '^\[trace\]' "$LOG" | head -60
echo "--- summary ---"
grep -E '^\[trace\] (SUMMARY|MAGIC|ARMED)' "$LOG" | tail -20
echo "--- phase markers in stdout ---"
grep -E 'guest init complete|first frame rendered|app shell rendered' "$OUT" || true
exit 0
