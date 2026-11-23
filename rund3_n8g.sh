#!/bin/bash
# n8 WAVE3: read-only mimalloc globals at after-ctor3 and at fault.
exec 9>/tmp/opencode/harness.lock
flock -x 9
export EGL_PLATFORM=x11 LIBGL_ALWAYS_SOFTWARE=1
export LD_LIBRARY_PATH=/tmp/opencode/n8-harness/n8bin/ndk-stubs
export SOBER_RENDER=1 SOBER_WINDOW_HOLD_MS=0 SOBER_APP_SHELL=1
export SOBER_AVD_BOUNDARY_REPLAY=1
export SOBER_SEEDER_DISABLE=1 SOBER_JNI_ONLOAD_FIRST=1
export SOBER_BIONIC_SF_FULL=1
cd /tmp/opencode/n8-harness
G=/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so
for i in 1 2 3; do
  env -u SOBER_AVD_BOUNDARY_PATH -u SOBER_AVD_BOUNDARY_UNVERIFIED \
      -u SOBER_N2_JAVA_OBJECT -u SOBER_N1_TRACE -u SOBER_W32_TRACE \
      -u SOBER_W32_CALL720 -u SOBER_W31_OBJ_TRACE -u SOBER_W31_LATCH_WATCH \
      SOBER_N8_CTOR3=1 SOBER_N8_PROBE=1 timeout 120 taskset -c 0,1 \
      ./n8bin/guest-init-host "$G" > n8g_$i.out 2> n8g_$i.err
  echo $? > n8g_$i.rc
done
echo RUN_N8G_DONE
