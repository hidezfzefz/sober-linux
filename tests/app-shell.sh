#!/usr/bin/env bash
# Phase 4 smoke test (worker-3, task #385): native JNI bridge + app shell.
#
# Extends the phase-3 harness with SOBER_APP_SHELL=1, verifies the deterministic
# "app shell rendered" marker, asserts real guest JNI activity, and checks that
# a real native X11 window remains visible. Preserves all earlier phase checks.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
GUEST="/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so"
HOST_BIN="$ROOT/build/guest-init-host"
NATIVE="$ROOT/build/sober-native"
OUT="/tmp/sober-app-shell.out"
ERR="/tmp/sober-app-shell.err"
INIT_MARKER="/tmp/sober-guest-init.marker"
FRAME_MARKER="/tmp/sober-first-frame.marker"
SHELL_MARKER="/tmp/sober-app-shell.marker"
BUILD_ID="204ad049599c96afb75b8a21d5051063e821d6c9"
WINDOW_TITLE="SoberNativeRenderPhase3"

if [ ! -f "$GUEST" ]; then echo "SKIP: guest engine not extracted at $GUEST"; exit 0; fi

# Rebuild when ANY phase-3/4 source or header is newer than the harness.
# (The old guard only checked guest_app_shell.c, so changes in src/jni/,
# bionic_bootstrap, gl_egl_forward, bionic_libc or the harness itself were
# silently ignored by `tests/app-shell.sh`.)
STALE=0
if [ ! -x "$HOST_BIN" ]; then STALE=1; else
  for src in \
      src/linux/guest_dlopen_host.c src/linux/guest_render.c src/linux/guest_render.h \
      src/linux/guest_app_shell.c src/linux/guest_app_shell.h \
      src/linux/android_relocs.c src/linux/android_relocs.h \
      src/linux/platform.c src/linux/platform.h src/linux/gfx_loader.c src/linux/gfx_loader.h \
      src/compat/android_compat.c src/compat/android_compat.h \
      src/compat/gl_egl_forward.c src/compat/ndk_stub.c \
      src/compat/bionic_libc.c src/compat/bionic_libc.map src/compat/gen_bionic_libc.py \
      src/compat/bionic_bootstrap/bionic_bootstrap.c \
      src/compat/bionic_bootstrap/bionic_bootstrap.h \
      src/compat/bionic_bootstrap/bootstrap_profile.h \
      src/compat/exports.map \
      src/render/native_render.c src/render/native_render.h \
      src/jni/native_jni.c src/jni/native_jni.h \
      src/trace/native_trace.c src/trace/native_trace.h \
      CMakeLists.txt src/render/CMakeLists.txt src/jni/CMakeLists.txt \
      src/trace/CMakeLists.txt; do
    if [ -f "$ROOT/$src" ] && [ "$ROOT/$src" -nt "$HOST_BIN" ]; then STALE=1; break; fi
  done
fi
if [ "$STALE" -eq 1 ]; then
    echo "== building harness + JNI bridge =="
    cmake -S "$ROOT" -B "$ROOT/build" -DCMAKE_BUILD_TYPE=Release >/tmp/sober-cmake.log 2>&1 \
      || { echo "FAIL: cmake"; tail -20 /tmp/sober-cmake.log; exit 1; }
    cmake --build "$ROOT/build" --target guest-init-host --parallel 4 >/tmp/sober-build.log 2>&1 \
      || { echo "FAIL: build"; tail -20 /tmp/sober-build.log; exit 1; }
fi

echo "== 0. engine identity + clean ldd =="
actual_id=$(readelf -n "$GUEST" 2>/dev/null | awk '/Build ID:/{print $3; exit}')
[ "$actual_id" = "$BUILD_ID" ] || { echo "FAIL: wrong engine build"; exit 1; }
before_sha=$(sha256sum "$GUEST" | awk '{print $1}')
ldd "$NATIVE" 2>/dev/null | tee /tmp/sober-native-ldd.txt
if grep -Eqi 'wine|proton|box64|qemu|wibo|basalt|mocktail|android|translation' /tmp/sober-native-ldd.txt; then
    echo "FAIL: sober-native dependency regression"; exit 1
fi

echo "== 1. app-shell startup (native JNI bridge, REAL path) =="
# Phase 6: no harness-triggered thunk bypass; the callback must arise from the
# real nativeAppBridgeAppStart 0x247dc8d path.
unset SOBER_ALT_CALLBACK_PATH
rm -f "$INIT_MARKER" "$FRAME_MARKER" "$SHELL_MARKER" "$OUT" "$ERR"
HOLD_MS=9000
EGL_PLATFORM=x11 LIBGL_ALWAYS_SOFTWARE=1 \
LD_LIBRARY_PATH="$ROOT/build/ndk-stubs${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
SOBER_RENDER=1 SOBER_APP_SHELL=1 SOBER_WINDOW_HOLD_MS="$HOLD_MS" \
SOBER_INIT_MARKER="$INIT_MARKER" SOBER_FRAME_MARKER="$FRAME_MARKER" \
SOBER_SHELL_MARKER="$SHELL_MARKER" \
timeout 180 "$HOST_BIN" "$GUEST" >"$OUT" 2>"$ERR" &
hpid=$!

win_seen=0
deadline=$((SECONDS + 90))
while kill -0 "$hpid" 2>/dev/null && [ $SECONDS -lt $deadline ]; do
    if command -v xdotool >/dev/null 2>&1 \
       && xdotool search --name "$WINDOW_TITLE" >/dev/null 2>&1; then
        win_seen=1
    fi
    [ -f "$SHELL_MARKER" ] && break
    sleep 0.2
done
wait "$hpid"; rc=$?
echo "harness exit: $rc"

echo "--- bridge / app-shell log ---"
grep -E '^\[jni\]|^\[app-shell\]|^\[render\]|GUEST FAULT|render phase' "$ERR" | tail -35
echo "--- stdout ---"
cat "$OUT"

echo "== 2. preserved phase-2/3 checks =="
grep -q '^guest init complete$' "$OUT" && [ -f "$INIT_MARKER" ] \
    || { echo "FAIL: phase-2 init completion regressed"; exit 1; }
grep -q '^first frame rendered$' "$OUT" && [ -f "$FRAME_MARKER" ] \
    || { echo "FAIL: phase-3 first-frame regressed"; exit 1; }
after_sha=$(sha256sum "$GUEST" | awk '{print $1}')
[ "$before_sha" = "$after_sha" ] || { echo "FAIL: on-disk engine modified"; exit 1; }

echo "== 3. strict gate evidence (DECISION 013 / 038) =="
# A pass requires a genuinely guest-driven app-shell callback AND the phase-3
# guest render. The harness-triggered ALT thunk is diagnostic-only, never proof
# (038 (a)): no ALT/HONESTY line and no synthetic-state withhold may appear.
if grep -q 'ALT PATH guest call 0x25848e0' "$ERR" \
   || grep -q 'HONESTY NOTICE' "$ERR" \
   || grep -q 'ALT PATH:' "$ERR"; then
    echo "FAIL: harness-triggered ALT thunk present (DECISION 038)"; tail -40 "$ERR"; exit 1
fi
if grep -q 'marker withheld: diagnostic guest-state seed active' "$ERR"; then
    echo "FAIL: synthetic guest state present (DECISION 013)"; tail -40 "$ERR"; exit 1
fi
if ! grep -Eq 'guest->Java callback: (gameLoadedCallback|onAppShellReloadNeeded)' "$ERR"; then
    echo "FAIL: no qualifying guest->Java app-shell callback observed"; tail -40 "$ERR"; exit 1
fi
grep -E 'guest->Java callback: (gameLoadedCallback|onAppShellReloadNeeded)' "$ERR" | tail -12
if ! grep -q 'guest call 0x649d422' "$ERR"; then
    echo "FAIL: phase-3 guest render call missing"; tail -40 "$ERR"; exit 1
fi
grep 'guest call 0x649d422' "$ERR" | tail -2
# 038 (b) decisive leg: the callback must be notifier-driven, not thunk-driven.
# The gate must emit its OWN provenance verdict with all opt-ins unset; this
# line does not exist yet, so this test is correctly RED today. Do NOT set
# SOBER_CALLBACK_PROVENANCE here: it is a diagnostic opt-in and withhold entry.
if ! grep -Eq '\[app-shell\].*notifier-driven gameLoadedCallback.*0x27eaf6e.*0x27eafb3' "$ERR" \
   || grep -q 'HARNESS-THUNK' "$ERR"; then
    echo "FAIL: no native notifier-driven callback provenance (DECISION 038)"
    tail -40 "$ERR"; exit 1
fi

echo "== 4. deterministic app-shell marker =="
if [ "$rc" -eq 0 ] && [ -f "$SHELL_MARKER" ] && grep -q '^app shell rendered$' "$OUT"; then
    echo "app shell rendered"
else
    echo "FAIL: no app-shell marker (rc=$rc)"; tail -40 "$ERR"; exit 1
fi

echo "== 5. visible native window =="
if [ "$win_seen" -eq 1 ]; then
    echo "visible native window confirmed: $WINDOW_TITLE"
else
    echo "WARN: X window not observed by xdotool"
    grep -q 'native window: handle=' "$ERR" || { echo "FAIL: no native window"; exit 1; }
fi

echo "app shell rendered"
exit 0
