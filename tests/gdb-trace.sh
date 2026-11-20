#!/usr/bin/env bash
# Phase 4b: gdb-as-parent trace of the guest app-shell allocation site.
# Owned by worker-3 (#395). Diagnostic only -- does NOT change harness behavior.
#
# ptrace_scope=1 on this host means gdb may only attach to a process it started.
# This script therefore launches the harness as gdb's child, with breakpoints at
# the three RVAs identified by the lead:
#   guest+0x1dbab02  allocator entry   -- ($rsp) is the allocation call site
#   guest+0x23500c6  writer of handle+0x68
#   guest+0x23331ae  consumer of the handle
# Use SOBER_GDB_PAUSE_MS as well if you prefer to set breakpoints after init.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
HOST_BIN="$ROOT/build/guest-init-host"
GUEST="/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so"
OUT="${SOBER_TRACE_OUT:-/tmp/sober-gdb-trace.txt}"
CMD="${SOBER_TRACE_GDB_CMD:-/tmp/sober-gdb.cmd}"

if ! command -v gdb >/dev/null 2>&1; then echo "SKIP: gdb not installed"; exit 0; fi
if [ ! -x "$HOST_BIN" ]; then echo "FAIL: build/guest-init-host missing"; exit 1; fi

cat > "$CMD" <<EOF
set pagination off
set confirm off
set print pretty on
# Keep default SIGTRAP handling so gdb's own breakpoints still stop.

set logging file $OUT
set logging overwrite on
set logging enabled on
set environment LD_LIBRARY_PATH $ROOT/build/ndk-stubs
set environment EGL_PLATFORM x11
set environment LIBGL_ALWAYS_SOFTWARE 1
set environment SOBER_RENDER 1
set environment SOBER_APP_SHELL 1
set environment SOBER_WINDOW_HOLD_MS 20000
starti
# Guest base is discovered after loader mapping; use a convenience breakpoint
# at the harness marker so we can compute it, then set the guest breakpoints.
break guest_app_shell_run
continue
printf "\\n[gdb-trace] guest base from 'info proc mappings' below\\n"
info proc mappings
# The guest load bias is also in \$_siginfo-free helper: read it from the
# harness global via the symbol if available.
printf "\\n[gdb-trace] g_guest_base = "
print/x g_guest_base
set \$gb = g_guest_base
break *(\$gb + 0x1dbab02)
break *(\$gb + 0x23500c6)
break *(\$gb + 0x23331ae)
continue
printf "\\n[gdb-trace] allocator hit: return address at rsp = "
print/x *(void**)\$rsp
bt 12
continue
bt 12
continue
bt 12
printf "\\n[gdb-trace] done\\n"
set logging enabled off
quit
EOF

echo "== gdb-as-parent app-shell trace =="
echo "gdb script: $CMD"
echo "trace out:  $OUT"
EGL_PLATFORM=x11 LIBGL_ALWAYS_SOFTWARE=1 \
  timeout "${SOBER_TRACE_TIMEOUT:-180}" gdb -q -batch -x "$CMD" --args "$HOST_BIN" "$GUEST" 2>&1 \
  | tee /tmp/sober-gdb-stdout.txt
rc=$?
echo "gdb exit: $rc"
echo "trace log: $OUT"
exit "$rc"
