#!/bin/bash
# n9 WAVE1: allocator patch OFF (default), Run D x3.
exec 9>/tmp/opencode/harness.lock
flock -x 9
export EGL_PLATFORM=x11 LIBGL_ALWAYS_SOFTWARE=1
export LD_LIBRARY_PATH=/tmp/opencode/n9-harness/n9bin/ndk-stubs
export SOBER_RENDER=1 SOBER_WINDOW_HOLD_MS=0 SOBER_APP_SHELL=1
export SOBER_AVD_BOUNDARY_REPLAY=1
export SOBER_SEEDER_DISABLE=1 SOBER_JNI_ONLOAD_FIRST=1
export SOBER_BIONIC_SF_FULL=1
cd /tmp/opencode/n9-harness
G=/tmp/opencode/n9-harness/n9bin/libroblox.so
for i in 1 2 3; do
  env -u SOBER_AVD_BOUNDARY_PATH -u SOBER_AVD_BOUNDARY_UNVERIFIED \
      -u SOBER_N2_JAVA_OBJECT -u SOBER_N1_TRACE -u SOBER_W32_TRACE \
      -u SOBER_W32_CALL720 -u SOBER_W31_OBJ_TRACE -u SOBER_W31_LATCH_WATCH \
      -u SOBER_N8_CTOR3 -u SOBER_N8_PROBE -u SOBER_N8_EDGE -u SOBER_N9_PTR \
      -u SOBER_ALLOCATOR_PATCH -u SOBER_BOOTSTRAP_NO_BRIDGES \
      timeout 120 taskset -c 0,1 \
      ./n9bin/guest-init-host "$G" > n9off_$i.out 2> n9off_$i.err
  echo $? > n9off_$i.rc
done
echo RUN_N9OFF_DONE
