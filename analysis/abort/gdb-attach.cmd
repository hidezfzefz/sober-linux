set pagination off
set confirm off
set print thread-events off
handle SIGABRT stop print nopass
handle SIGSEGV stop print nopass
continue
echo ===BT===
bt 60
echo ===REGS===
info registers
echo ===FRAME===
info frame
echo ===MAPPINGS===
info proc mappings
echo ===GUESTBASE===
print/x g_guest_base
quit
