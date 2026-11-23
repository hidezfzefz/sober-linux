#!/bin/bash
# n5: SOBER_BIONIC_SF_FULL ON, shim ring + RET64; Run D x3 object OFF.
exec 9>/tmp/opencode/harness.lock
flock -x 9
export EGL_PLATFORM=x11 LIBGL_ALWAYS_SOFTWARE=1
export LD_LIBRARY_PATH=/tmp/opencode/n5-harness/build-n5/ndk-stubs
export SOBER_RENDER=1 SOBER_WINDOW_HOLD_MS=0 SOBER_APP_SHELL=1
export SOBER_AVD_BOUNDARY_REPLAY=1
export SOBER_SEEDER_DISABLE=1 SOBER_JNI_ONLOAD_FIRST=1
export SOBER_BIONIC_SF_FULL=1
cd /tmp/opencode/n5-harness
G=/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so
for i in 1 2 3; do
  env -u SOBER_AVD_BOUNDARY_PATH -u SOBER_AVD_BOUNDARY_UNVERIFIED \
      -u SOBER_N2_JAVA_OBJECT -u SOBER_N3_ENTRY -u SOBER_N1_TRACE \
      -u SOBER_W32_TRACE -u SOBER_W32_CALL720 -u SOBER_W31_OBJ_TRACE \
      -u SOBER_W31_LATCH_WATCH -u SOBER_W28_PAIRS -u SOBER_W21_WATCH \
      -u SOBER_W26_WATCH timeout 120 taskset -c 0,1 \
      ./build-n5/guest-init-host "$G" > n5_$i.out 2> n5_$i.err
  echo $? > n5_$i.rc
done
echo RUN_D3_N5_DONE
