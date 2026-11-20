# Attach-mode GDB script (worker-1, decision 022 follow-up).
# Usage (worker-3 adds opt-in SOBER_GDB_PAUSE_MS before guest_app_shell_run):
#
#   # start harness in background with SOBER_GDB_PAUSE_MS=8000
#   PID=<harness pid>
#   GB=$(awk '/sober-guest/{print "0x" strtonum("0x" $1); exit}' /proc/$PID/maps)
#   gdb -q -p $PID -ex "set \$gb=$GB" -x analysis/handle/gdb-attach.cmd 2>&1 \
#       | tee /tmp/gdb-attach.out
#
# Avoids constructor replay entirely: we attach AFTER init, during the pause.
set pagination off
set confirm off
printf "GDB_ATTACH guest_base=%p\n", (void*)$gb
set $consumer = (unsigned long long)$gb + 0x23331aeULL
set $writer   = (unsigned long long)$gb + 0x23500c6ULL
printf "GDB_ATTACH consumer=%p writer=%p\n", (void*)$consumer, (void*)$writer
break *($consumer)
break *($writer)
continue
printf "GDB_STOP rip=%p rva=%#lx\n", $rip, $rip-$gb
if $rip == $writer
  printf "GDB_WRITER rbx(handle)=%p r14(backing)=%p field=%p\n", $rbx, $r14, (void*)($rbx+0x68)
  bt 10
  info registers
  continue
end
if $rip == $consumer
  set $obj = (unsigned long long)$rbx
  printf "GDB_HANDLE obj=%p magic=%p field=%p\n", (void*)$obj, *(void**)$obj, *(void**)($obj+0x68)
  # Software watchpoint (hardware watchpoints unsupported here). Slow but
  # the remaining path is short.
  watch *(unsigned long long *)($obj+0x68)
  continue
  printf "GDB_WATCH rip=%p rva=%#lx field=%p\n", $rip, $rip-$gb, *(void**)($obj+0x68)
  bt 12
  info registers
end
printf "GDB_FINAL rip=%p rva=%#lx\n", $rip, $rip-$gb
bt 8
detach
quit
