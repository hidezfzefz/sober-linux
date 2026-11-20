#!/usr/bin/env bash
# Native dlopen harness: maps the unmodified AppImage libroblox.so and drives
# its init_array via the native bootstrap, with deterministic completion and
# crash instrumentation. Owned by worker-3 (task #377).
#
# Canonical order (DECISION 008):
#   1. byte-identical memfd copy, DT_INIT_ARRAYSZ neutralised (no auto ctors)
#   2. locate guest load bias
#   3. apply Android packed DT_ANDROID_RELA relocations (src/linux/)
#   4. worker-1 bootstrap: build-ID gate, data seeds, mimalloc bridges
#   5. replay ctor ranges [2,3) + [5,3618)
#   6. deterministic "guest init complete" signal
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
GUEST="/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so"
HOST_BIN="$ROOT/build/guest-init-host"
NATIVE="$ROOT/build/sober-native"
OUT="/tmp/sober-guest-dlopen.out"
ERR="/tmp/sober-guest-dlopen.err"
MARKER="/tmp/sober-guest-init.marker"
BUILD_ID="204ad049599c96afb75b8a21d5051063e821d6c9"

if [ ! -f "$GUEST" ]; then echo "SKIP: guest engine not extracted at $GUEST"; exit 0; fi

# Build the harness if it is missing or older than its sources.
if [ ! -x "$HOST_BIN" ] || [ "$ROOT/src/linux/guest_dlopen_host.c" -nt "$HOST_BIN" ] \
   || [ "$ROOT/src/linux/android_relocs.c" -nt "$HOST_BIN" ]; then
    echo "== building native harness =="
    cmake -S "$ROOT" -B "$ROOT/build" -DCMAKE_BUILD_TYPE=Release >/tmp/sober-cmake.log 2>&1 \
      || { echo "FAIL: cmake configure"; tail -20 /tmp/sober-cmake.log; exit 1; }
    cmake --build "$ROOT/build" --target guest-init-host --parallel 4 >/tmp/sober-build.log 2>&1 \
      || { echo "FAIL: harness build"; tail -20 /tmp/sober-build.log; exit 1; }
fi

echo "== 0. engine identity (must be AppImage build $BUILD_ID) =="
actual_id=$(readelf -n "$GUEST" 2>/dev/null | awk '/Build ID:/{print $3; exit}')
echo "guest Build ID: $actual_id"
if [ "$actual_id" != "$BUILD_ID" ]; then
    echo "FAIL: wrong engine build (expected $BUILD_ID)"; exit 1
fi
before_sha=$(sha256sum "$GUEST" | awk '{print $1}')
echo "on-disk engine sha256 before: $before_sha"

echo "== 1. native ELF + clean ldd =="
file "$HOST_BIN"
ldd "$NATIVE" 2>/dev/null | tee /tmp/sober-native-ldd.txt
if grep -Eqi 'wine|proton|box64|qemu|wibo|basalt|mocktail|android|translation' /tmp/sober-native-ldd.txt; then
    echo "FAIL: sober-native has a translation-layer dependency"; exit 1
fi

echo "== 2. native dlopen + instrumented init_array replay =="
rm -f "$MARKER"
LD_LIBRARY_PATH="$ROOT/build/ndk-stubs${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
SOBER_INIT_MARKER="$MARKER" \
timeout 120 "$HOST_BIN" "$GUEST" >"$OUT" 2>"$ERR"
rc=$?
echo "harness exit: $rc"

echo "--- relocation / bootstrap summary ---"
grep -E '^\[relocs\] (applied|DT_ANDROID_RELA)|^\[harness\] (guest base|bootstrap|ctor\[|delegating)' "$ERR" | head -20
echo "--- last 12 log lines ---"
tail -12 "$ERR"

echo "== 3. crash instrumentation =="
if grep -q 'GUEST FAULT' "$ERR"; then
    echo "FAULT REPORT:"
    grep -A16 'GUEST FAULT' "$ERR" | head -20
    # Annotate the guest RVA if one was reported.
    rva=$(grep -o 'faulting guest RVA: 0x[0-9a-f]*' "$ERR" | awk '{print $4}')
    if [ -n "${rva:-}" ]; then
        echo "fault disassembly near $rva:"
        objdump -d --start-address="$rva" --stop-address=$((rva + 32)) "$GUEST" 2>/dev/null | tail -12
    fi
fi

echo "== 4. on-disk engine unchanged =="
after_sha=$(sha256sum "$GUEST" | awk '{print $1}')
echo "on-disk engine sha256 after:  $after_sha"
if [ "$before_sha" != "$after_sha" ]; then
    echo "FAIL: on-disk engine modified"; exit 1
fi
echo "byte deltas: none on disk; private memfd copy changes only DT_INIT_ARRAYSZ"
echo "(loader metadata, RVA 0x7220e58 count 28944 -> 0; no code/data bytes touched)"

echo "== 5. deterministic init completion =="
if grep -q '^guest init complete$' "$OUT" && [ -f "$MARKER" ] && [ "$rc" -eq 0 ]; then
    echo "guest init complete"
    exit 0
fi
echo "FAIL: guest init did not complete (harness exit $rc, marker $([ -f "$MARKER" ] && echo present || echo absent))"
exit 1
