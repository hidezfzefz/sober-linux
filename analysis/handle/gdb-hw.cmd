# GDB batch script using HARDWARE breakpoints so SIGTRAP can be suppressed
# during the guest's 3618-constructor replay.  Run:
#   gdb -q -batch -x analysis/handle/gdb-hw.cmd </dev/null > /tmp/gdbhw.out 2>&1
set pagination off
set confirm off
handle SIGTRAP nostop noprint pass
handle SIGSEGV nostop noprint pass
set environment LD_LIBRARY_PATH /home/user/sober-linux/build/ndk-stubs
set environment EGL_PLATFORM x11
set environment LIBGL_ALWAYS_SOFTWARE 1
set environment SOBER_RENDER 1
set environment SOBER_APP_SHELL 1
set environment SOBER_WINDOW_HOLD_MS 100
set environment SOBER_NO_HANDLE_SANITISER 1
set args /tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so
file /home/user/sober-linux/build/guest-init-host
hbreak guest_app_shell_run
run
set $gb = (unsigned long long)$rdi
printf "GDB_GUEST_BASE %p\n", (void*)$gb
hbreak *($gb + 0x23500c6)
hbreak *($gb + 0x23331ae)
continue
printf "GDB_STOP1 rip=%p rva=%#lx\n", $rip, $rip-$gb
bt 6
continue
printf "GDB_STOP2 rip=%p rva=%#lx rbx=%p\n", $rip, $rip-$gb, $rbx
set $obj = (unsigned long long)$rbx
printf "GDB_OBJ %p magic=%p field=%p\n", (void*)$obj, *(void**)$obj, *(void**)($obj+0x68)
watch -l *(unsigned long long *)($obj+0x68)
continue
printf "GDB_WATCH rip=%p rva=%#lx field=%p\n", $rip, $rip-$gb, *(void**)($obj+0x68)
bt 10
kill
quit
