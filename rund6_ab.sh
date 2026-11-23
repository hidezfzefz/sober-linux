#!/bin/bash
# n5 #682 A/B in ONE session: SF_FULL ON x3 vs UNSET x3; object OFF.
exec 9>/tmp/opencode/harness.lock
flock -x 9
export EGL_PLATFORM=x11 LIBGL_ALWAYS_SOFTWARE=1
export LD_LIBRARY_PATH=/tmp/opencode/n5-harness/build-n5/ndk-stubs
export SOBER_RENDER=1 SOBER_WINDOW_HOLD_MS=0 SOBER_APP_SHELL=1
export SOBER_AVD_BOUNDARY_REPLAY=1
export SOBER_SEEDER_DISABLE=1 SOBER_JNI_ONLOAD_FIRST=1
cd /tmp/opencode/n5-harness
G=/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so
COMMON="-u SOBER_AVD_BOUNDARY_PATH -u SOBER_AVD_BOUNDARY_UNVERIFIED -u SOBER_N2_JAVA_OBJECT -u SOBER_N3_ENTRY -u SOBER_N1_TRACE"
for i in 1 2 3; do
  env $COMMON SOBER_BIONIC_SF_FULL=1 timeout 120 taskset -c 0,1 \
    ./build-n5/guest-init-host "$G" > ab_on_$i.out 2> ab_on_$i.err
  echo $? > ab_on_$i.rc
done
for i in 1 2 3; do
  env $COMMON -u SOBER_BIONIC_SF_FULL timeout 120 taskset -c 0,1 \
    ./build-n5/guest-init-host "$G" > ab_off_$i.out 2> ab_off_$i.err
  echo $? > ab_off_$i.rc
done
echo AB_DONE
