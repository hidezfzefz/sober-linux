# Attach all work to breakpoint commands so it runs even if `run` does not
# return cleanly in batch mode (worker-1, decision 022 follow-up).
set pagination off
set confirm off
set environment LD_LIBRARY_PATH /home/user/sober-linux/build/ndk-stubs
set environment EGL_PLATFORM x11
set environment LIBGL_ALWAYS_SOFTWARE 1
set environment SOBER_RENDER 1
set environment SOBER_APP_SHELL 1
set environment SOBER_WINDOW_HOLD_MS 100
set environment SOBER_NO_HANDLE_SANITISER 1
set args /tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so
file /home/user/sober-linux/build/guest-init-host
break guest_app_shell_run
commands
silent
printf "GDB_GUEST_BASE %p\n", $rdi
set $gb = (unsigned long long)$rdi
break *($gb + 0x23500c6)
break *($gb + 0x23331ae)
continue
end
run
printf "GDB_AFTER_RUN rip=%p\n", $rip
bt 6
quit
