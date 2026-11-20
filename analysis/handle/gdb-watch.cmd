# gdb batch script (worker-1, task #387 follow-up) — catch the writer of the
# guest app-shell handle's +0x68 field.  Run with:
#
#   gdb -q -batch -x analysis/handle/gdb-watch.cmd
#
# or, once the harness is rebuilt:
#
#   gdb -q -batch -x /home/user/sober-linux/analysis/handle/gdb-watch.cmd \
#       2>&1 | tee /tmp/gdb-handle.out
#
# Target: AppImage libroblox.so BuildID 204ad049... (never Flatpak).
# Guest RVAs used: 0x23331ae (consumer: rbx = *(rdi)), 0x23500c6 (writer:
# mov backing_ptr -> [handle+0x68]), 0x23331b4 (resume after replicated
# instructions).

set pagination off
set confirm off
set print pretty on
set breakpoint pending on

# --- environment / argv for the phase-4 harness ---
set environment LD_LIBRARY_PATH /home/user/sober-linux/build/ndk-stubs
set environment EGL_PLATFORM x11
set environment LIBGL_ALWAYS_SOFTWARE 1
set environment SOBER_RENDER 1
set environment SOBER_APP_SHELL 1
set environment SOBER_WINDOW_HOLD_MS 1000
# Keep all experimental guest patches OFF (decision C).
set environment SOBER_NO_HANDLE_SANITISER 1
set args /tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so
file /home/user/sober-linux/build/guest-init-host

# --- stage 1: stop at host entry to app-shell; guest base is in RDI ---
break guest_app_shell_run
run
set $gb = (unsigned long long)$rdi
printf "\n[gdb] guest base = %p\n", (void *)$gb

# --- stage 2: break at the writer and at the first consumer ---
break *($gb + 0x23500c6)
break *($gb + 0x23331ae)
continue

# Determine which breakpoint hit by RIP.
if $rip == $gb + 0x23500c6
  printf "\n[gdb] WRITER HIT at 0x23500c6: rbx(handle)=%p r14(backing)=%p field=%p\n", $rbx, $r14, (void *)($rbx+0x68)
  bt
  info registers rdi rsi rbx r14 rip
  # Continue to the consumer so we can also arm the watchpoint on the result.
  continue
end

if $rip == $gb + 0x23331ae
  set $obj = (unsigned long long)$rbx
  printf "\n[gdb] CONSUMER at 0x23331ae: slot=%p handle(obj)=%p\n", $rdi, (void *)$obj
  printf "[gdb] obj[0]=%p  obj[0x68]=%p  (magic expect 0xc0dedbad)\n", *(void * *)$obj, *(void * *)($obj+0x68)
  # Hardware WRITE watchpoint on the exact qword the guest later dereferences.
  watch -l *(unsigned long long *)($obj+0x68)
  continue
  printf "\n[gdb] WATCH HIT: rip=%p guest_rva=%#lx obj[0x68]=%p\n", $rip, $rip-$gb, *(void * *)($obj+0x68)
  bt
  info registers
  frame
  x/8i $rip-16
end

# --- stage 3: if no watchpoint hit, catch the eventual SIGSEGV location ---
printf "\n[gdb] final stop rip=%p guest_rva=%#lx si-ish rdi=%p\n", $rip, $rip-$gb, $rdi
bt
quit
