#!/bin/bash
# n3 READ-ONLY: #552 native entry regs + JNI ring; Run D x3, object OFF.
exec 9>/tmp/opencode/harness.lock
flock -x 9
export EGL_PLATFORM=x11 LIBGL_ALWAYS_SOFTWARE=1
export LD_LIBRARY_PATH=/tmp/opencode/n3-harness/build-n3/ndk-stubs
export SOBER_RENDER=1 SOBER_WINDOW_HOLD_MS=0 SOBER_APP_SHELL=1
export SOBER_AVD_BOUNDARY_REPLAY=1
export SOBER_SEEDER_DISABLE=1 SOBER_JNI_ONLOAD_FIRST=1
cd /tmp/opencode/n3-harness
G=/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so
for i in 1 2 3; do
  env -u SOBER_AVD_BOUNDARY_PATH -u SOBER_AVD_BOUNDARY_UNVERIFIED \
      -u SOBER_N2_JAVA_OBJECT -u SOBER_N1_TRACE -u SOBER_W32_TRACE \
      -u SOBER_W32_CALL720 -u SOBER_W31_OBJ_TRACE -u SOBER_W31_LATCH_WATCH \
      -u SOBER_W28_PAIRS -u SOBER_W21_WATCH -u SOBER_W26_WATCH \
      SOBER_N3_ENTRY=1 timeout 120 taskset -c 0,1 \
      ./build-n3/guest-init-host "$G" > n3_$i.out 2> n3_$i.err
  echo $? > n3_$i.rc
done
echo RUN_D3_N3_DONE
