set pagination off
set confirm off
set print thread-events off
handle SIGABRT stop print nopass
handle SIGSEGV stop print nopass
handle SIGTRAP nostop nopass
set environment EGL_PLATFORM x11
set environment LIBGL_ALWAYS_SOFTWARE 1
set environment LD_LIBRARY_PATH /home/user/sober-linux/build/ndk-stubs
set environment SOBER_RENDER 1
set environment SOBER_APP_SHELL 1
set environment SOBER_WINDOW_HOLD_MS 1500
set environment SOBER_HELPER_REPAIR_V2 1
set environment SOBER_HELPER_LOG 1
set environment SOBER_INIT_MARKER /tmp/abort-gdb-init.marker
set environment SOBER_FRAME_MARKER /tmp/abort-gdb-frame.marker
set environment SOBER_SHELL_MARKER /tmp/abort-gdb-shell.marker
run /tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so
echo ===BT===
bt 50
echo ===REGS===
info registers
echo ===MAPPINGS===
info proc mappings
echo ===GUESTBASE===
print/x g_guest_base
