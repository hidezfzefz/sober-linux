#!/usr/bin/env bash
# D09 -- withhold-chain coverage audit (DECISION 013, strict gate).
#
# DEFAULT-OFF diagnostic. This script is deliberately NOT wired into ctest,
# CMakeLists.txt, or tests/verify-native.sh. It never builds or runs the port
# and never writes to any file: it only reads guest_app_shell.c as text and
# prints a report. It always exits 0.
#
# Enable explicitly, via either:
#   SOBER_D09_WITHHOLD_AUDIT=1 bash tests/D09-withhold-audit.sh
#   bash tests/D09-withhold-audit.sh --run
#
# What it audits:
#   1. every getenv("SOBER_...") knob referenced in src/linux/guest_app_shell.c
#   2. every knob listed in that file's `seeded_state` withhold chain
#   3. any guest-mutating knob that is referenced but NOT in the chain.
#      Such a knob can manufacture guest state while the strict gate still
#      reports a genuine success, i.e. a false-green risk.
#
# The classification below is a static allowlist of knobs that are purely
# diagnostics, logging or post-gate timing. Every other referenced knob that
# falls outside the chain is reported as a gap. A newly added knob is flagged
# by default; move it to BENIGN only after confirming it cannot mutate guest
# state and cannot justify the real marker on its own.

set -uo pipefail

# Keep sort/comm/grep byte-ordering identical across every pipeline stage.
export LC_ALL=C

# ---- default-OFF gate -------------------------------------------------------
enabled=0
case " ${SOBER_D09_WITHHOLD_AUDIT:-0} " in
    *" 1 "*) enabled=1 ;;
esac
for _arg in "$@"; do
    case "$_arg" in
        --run|--audit|--on) enabled=1 ;;
    esac
done
if [ "$enabled" -ne 1 ]; then
    echo "D09 withhold audit: default-OFF (no source read, no side effects)."
    echo "Enable with SOBER_D09_WITHHOLD_AUDIT=1 or --run."
    exit 0
fi

# ---- locate the source (read-only) -----------------------------------------
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_root=$(cd -- "$script_dir/.." && pwd)
src="$repo_root/src/linux/guest_app_shell.c"

if [ ! -r "$src" ]; then
    echo "D09 withhold audit: source not readable: $src"
    exit 0
fi

# ---- extraction (pure text, no file writes) --------------------------------
knob_re='getenv\("SOBER_[A-Z0-9_]+"'

all_knobs() {
    grep -oE "$knob_re" "$src" \
        | sed -E 's/getenv\("(.*)"/\1/' \
        | LC_ALL=C sort -u
}

# The chain is the multi-line `int seeded_state = getenv(...) || ...;` block.
# Start at its declaration, stop at the first line whose semicolon ends it.
chain_knobs() {
    awk '
        /int[[:space:]]+seeded_state[[:space:]]*=/ { in_chain = 1 }
        in_chain { print }
        in_chain && /;[[:space:]]*$/ { in_chain = 0 }
    ' "$src" \
        | grep -oE "$knob_re" \
        | sed -E 's/getenv\("(.*)"/\1/' \
        | LC_ALL=C sort -u
}

# Diagnostics / logging / purely-timing knobs -- intentionally outside the
# withhold chain because they do not manufacture guest app-shell state.
benign=$(cat <<'EOF'
SOBER_APP_SHELL_DIAGNOSTIC
SOBER_APP_SHELL_WAIT_MS
SOBER_APP_SLOT_DIAG
SOBER_JNI_VERBOSE
SOBER_TRACE_FREE_SLOT
SOBER_WINDOW_HOLD_MS
EOF
)

all=$(all_knobs)
chain=$(chain_knobs)
not_in_chain=$(comm -23 <(all_knobs) <(chain_knobs))
excluded=""
gaps=""
if [ -n "$not_in_chain" ]; then
    excluded=$(printf '%s\n' "$not_in_chain" | grep -xF -f <(printf '%s\n' "$benign") || true)
    gaps=$(printf '%s\n' "$not_in_chain" | grep -vxF -f <(printf '%s\n' "$benign") || true)
fi

count_lines() {
    if [ -z "$1" ]; then
        printf '0'
    else
        printf '%s\n' "$1" | grep -c .
    fi
}

# ---- report ----------------------------------------------------------------
echo "=== D09 withhold-chain audit (DECISION 013, strict) ==="
echo "source: $src"
echo "getenv SOBER knobs referenced : $(count_lines "$all")"
echo "withhold-chain knobs found     : $(count_lines "$chain")"
if [ -z "$chain" ]; then
    echo "WARNING: no seeded_state chain parsed -- treat every knob as a gap."
fi
echo

echo "-- referenced knobs NOT in the withhold chain (raw difference) --"
if [ -n "$not_in_chain" ]; then
    printf '%s\n' "$not_in_chain"
else
    echo "(none)"
fi
echo

echo "-- excluded: diagnostic / logging / timing knobs --"
if [ -n "$excluded" ]; then
    printf '%s\n' "$excluded"
else
    echo "(none)"
fi
echo

echo "-- GAP LIST: guest-mutating knobs NOT in the withhold chain --"
if [ -n "$gaps" ]; then
    printf '%s\n' "$gaps"
else
    echo "(none)"
fi

# Diagnostic only: reporting gaps is not a test failure.
exit 0
