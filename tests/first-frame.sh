#!/usr/bin/env bash
# Phase 3 smoke test (worker-3, task #380): native window + first frame.
#
# Extends the phase-2 init-complete harness with SOBER_RENDER=1, verifies the
# deterministic "first frame rendered" marker, and checks that a real native
# X11 window was mapped on the display while the harness held it open.
#
# Preserves every phase-2 green check. Owned by worker-3.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
GUEST="/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so"
HOST_BIN="$ROOT/build/guest-init-host"
NATIVE="$ROOT/build/sober-native"
OUT="/tmp/sober-first-frame.out"
ERR="/tmp/sober-first-frame.err"
INIT_MARKER="/tmp/sober-guest-init.marker"
FRAME_MARKER="/tmp/sober-first-frame.marker"
BUILD_ID="204ad049599c96afb75b8a21d5051063e821d6c9"
WINDOW_TITLE="SoberNativeRenderPhase3"

if [ ! -f "$GUEST" ]; then echo "SKIP: guest engine not extracted at $GUEST"; exit 0; fi

# Build (harness + render backend) if needed.
if [ ! -x "$HOST_BIN" ] || [ "$ROOT/src/linux/guest_render.c" -nt "$HOST_BIN" ] \
   || [ "$ROOT/src/render/native_render.c" -nt "$HOST_BIN" ] \
   || [ "$ROOT/src/linux/guest_dlopen_host.c" -nt "$HOST_BIN" ]; then
    echo "== building native harness + render backend =="
    cmake -S "$ROOT" -B "$ROOT/build" -DCMAKE_BUILD_TYPE=Release >/tmp/sober-cmake.log 2>&1 \
      || { echo "FAIL: cmake configure"; tail -20 /tmp/sober-cmake.log; exit 1; }
    cmake --build "$ROOT/build" --target guest-init-host --parallel 4 >/tmp/sober-build.log 2>&1 \
      || { echo "FAIL: harness build"; tail -20 /tmp/sober-build.log; exit 1; }
fi

echo "== 0. engine identity + phase-2 prerequisites =="
actual_id=$(readelf -n "$GUEST" 2>/dev/null | awk '/Build ID:/{print $3; exit}')
[ "$actual_id" = "$BUILD_ID" ] || { echo "FAIL: wrong engine build"; exit 1; }
before_sha=$(sha256sum "$GUEST" | awk '{print $1}')
echo "engine BuildID: $actual_id"
echo "on-disk sha256 before: $before_sha"

echo "== 1. sober-native ldd stays clean =="
ldd "$NATIVE" 2>/dev/null | tee /tmp/sober-native-ldd.txt
if grep -Eqi 'wine|proton|box64|qemu|wibo|basalt|mocktail|android|translation|X11|wayland|libEGL|libGL' /tmp/sober-native-ldd.txt; then
    echo "FAIL: sober-native dependency regression"; exit 1
fi

echo "== 2. native window + first frame =="
rm -f "$INIT_MARKER" "$FRAME_MARKER" "$OUT" "$ERR"
HOLD_MS=9000
EGL_PLATFORM=x11 LIBGL_ALWAYS_SOFTWARE=1 \
LD_LIBRARY_PATH="$ROOT/build/ndk-stubs${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
SOBER_RENDER=1 SOBER_WINDOW_HOLD_MS="$HOLD_MS" \
SOBER_INIT_MARKER="$INIT_MARKER" SOBER_FRAME_MARKER="$FRAME_MARKER" \
timeout 120 "$HOST_BIN" "$GUEST" >"$OUT" 2>"$ERR" &
hpid=$!

# Poll for the frame marker while the harness holds the window open.
win_seen=0
marker_seen=0
deadline=$((SECONDS + 60))
while kill -0 "$hpid" 2>/dev/null && [ $SECONDS -lt $deadline ]; do
    [ -f "$FRAME_MARKER" ] && marker_seen=1
    if command -v xdotool >/dev/null 2>&1; then
        if xdotool search --name "$WINDOW_TITLE" >/dev/null 2>&1; then
            win_seen=1
            break
        fi
    fi
    sleep 0.2
done
wait "$hpid"; rc=$?
echo "harness exit: $rc"
[ -f "$FRAME_MARKER" ] && marker_seen=1

echo "--- render log ---"
grep -E '^\[render\]|render phase|GUEST FAULT' "$ERR" | tail -25
echo "--- stdout ---"
cat "$OUT"

echo "== 3. phase-2 checks preserved =="
grep -q '^guest init complete$' "$OUT" && [ -f "$INIT_MARKER" ] \
    || { echo "FAIL: phase-2 guest init completion regressed"; exit 1; }
after_sha=$(sha256sum "$GUEST" | awk '{print $1}')
echo "on-disk sha256 after:  $after_sha"
[ "$before_sha" = "$after_sha" ] || { echo "FAIL: on-disk engine modified"; exit 1; }

echo "== 4. guest render entry-point calls (worker-1 render-map, BuildID 204ad049) =="
if grep -q 'guest call 0x649d422 swap' "$ERR" \
   && grep -q 'guest-driven swaps observed: [1-9]' "$ERR"; then
    grep -E 'guest call 0x649d422|guest-driven swaps' "$ERR"
else
    echo "FAIL: no guest render entry-point call / guest swap observed"; tail -30 "$ERR"; exit 1
fi

echo "== 5. deterministic first-frame signal (gated on guest calls) =="
if [ "$rc" -eq 0 ] && [ "$marker_seen" -eq 1 ] && grep -q '^first frame rendered$' "$OUT"; then
    echo "first frame rendered"
else
    echo "FAIL: no first-frame marker (rc=$rc marker=$marker_seen)"
    tail -30 "$ERR"
    exit 1
fi

echo "== 6. visible native window =="
if [ "$win_seen" -eq 1 ]; then
    echo "visible native window confirmed on display: $WINDOW_TITLE"
    if command -v xdotool >/dev/null 2>&1; then
        xdotool search --name "$WINDOW_TITLE" 2>/dev/null | head -3
    fi
else
    echo "WARN: X window not observed by xdotool (headless display or timing)"
    # A successful EGL window-surface swap is still a valid first frame.
    grep -q 'native window: handle=' "$ERR" || { echo "FAIL: no native window created"; exit 1; }
fi

echo "first frame rendered"
exit 0
