#!/usr/bin/env bash
# Whole-port definition of done. Run from repo root.
set -euo pipefail
BIN="build/sober-native"
fail=0

echo "== 1. native ELF =="
if [ -f "$BIN" ] && file "$BIN" | grep -q 'ELF'; then
  file "$BIN"
else
  echo "FAIL: $BIN missing or not an ELF"; fail=1
fi

echo "== 2. no translation-layer dependencies =="
if [ -f "$BIN" ]; then
  ldd "$BIN" 2>/dev/null | tee /tmp/sober-ldd.txt
  if grep -Eqi 'wine|proton|box64|qemu|wibo' /tmp/sober-ldd.txt; then
    echo "FAIL: translation-layer dependency detected"; fail=1
  fi
else
  echo "SKIP: binary not built yet"
fi

echo "== 3. runs without compat runtime =="
echo "Manual: run ./$BIN in an environment with no Wine/Proton installed."

echo "== 4. ABI coverage =="
echo "Manual: every 'required' row in docs/interfaces/abi-map.md is implemented or marked unsupported with reason."

exit "$fail"
