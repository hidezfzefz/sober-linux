#!/bin/bash
# n8 WAVE1 A/B: ctor3 0x1db9a40 replay OFF x3 vs ON x3.
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
COMMON="-u SOBER_AVD_BOUNDARY_PATH -u SOBER_AVD_BOUNDARY_UNVERIFIED -u SOBER_N2_JAVA_OBJECT -u SOBER_N1_TRACE -u SOBER_W32_TRACE -u SOBER_W32_CALL720 -u SOBER_W31_OBJ_TRACE -u SOBER_W31_LATCH_WATCH"
for i in 1 2 3; do
  env $COMMON -u SOBER_N8_CTOR3 timeout 120 taskset -c 0,1 \
    ./n8bin/guest-init-host "$G" > ab_off_$i.out 2> ab_off_$i.err
  echo $? > ab_off_$i.rc
done
for i in 1 2 3; do
  env $COMMON SOBER_N8_CTOR3=1 timeout 120 taskset -c 0,1 \
    ./n8bin/guest-init-host "$G" > ab_on_$i.out 2> ab_on_$i.err
  echo $? > ab_on_$i.rc
done
echo AB_DONE
