import gdb

envs = {
    "EGL_PLATFORM": "x11",
    "LIBGL_ALWAYS_SOFTWARE": "1",
    "LD_LIBRARY_PATH": "/home/user/sober-linux/build/ndk-stubs",
    "SOBER_RENDER": "1",
    "SOBER_APP_SHELL": "1",
    "SOBER_WINDOW_HOLD_MS": "1000",
    "SOBER_HELPER_REPAIR_V2": "1",
    "SOBER_HELPER_LOG": "1",
}
for k, v in envs.items():
    gdb.execute("set environment %s %s" % (k, v))
for k in ("SOBER_INIT_MARKER", "SOBER_FRAME_MARKER", "SOBER_SHELL_MARKER"):
    gdb.execute("unset environment %s" % k)

gdb.execute("set pagination off")
gdb.execute("set confirm off")
gdb.execute("set print thread-events off")
gdb.execute("handle SIGABRT stop print nopass")
gdb.execute("handle SIGSEGV stop print nopass")
gdb.execute("handle SIGTRAP stop nopass")
gdb.execute("run /tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so")

target = None
for i in range(300):
    try:
        out = gdb.execute("continue", to_string=True)
    except gdb.error as e:
        print("GDBERR", e)
        break
    if "SIGABRT" in out or "Aborted" in out:
        target = "SIGABRT"
        print("STOPPED_AT", target, "after", i, "continues")
        break
    if "SIGSEGV" in out or "Segmentation" in out:
        target = "SIGSEGV"
        print("STOPPED_AT", target, "after", i, "continues")
        break
    if "exited normally" in out or "exited with" in out:
        print("EXITED", out.strip())
        break

print("===BT===")
gdb.execute("bt 60")
print("===REGS===")
gdb.execute("info registers")
print("===FRAME===")
try:
    gdb.execute("info frame")
except gdb.error as e:
    print("no frame", e)
print("===MAPPINGS===")
gdb.execute("info proc mappings")
print("===GUESTBASE===")
try:
    gdb.execute("print/x g_guest_base")
except gdb.error as e:
    print("no g_guest_base", e)
