#!/bin/bash
# n10 WAVE1 (cont): allocator patch OFF x3 vs ON x3, modified input opt-in.
exec 9>/tmp/opencode/harness.lock
flock -x 9
export EGL_PLATFORM=x11 LIBGL_ALWAYS_SOFTWARE=1
export LD_LIBRARY_PATH=/tmp/opencode/n10-harness/n10bin/ndk-stubs
export SOBER_RENDER=1 SOBER_WINDOW_HOLD_MS=0 SOBER_APP_SHELL=1
export SOBER_AVD_BOUNDARY_REPLAY=1
export SOBER_AVD_BOUNDARY_UNVERIFIED=1
export SOBER_AVD_BOUNDARY_PATH=/tmp/opencode/w15-flags-1.json
export SOBER_N10_MODIFIED_INPUT=1
export SOBER_SEEDER_DISABLE=1 SOBER_JNI_ONLOAD_FIRST=1
export SOBER_BIONIC_SF_FULL=1
export SOBER_N10_LATCH=1
cd /tmp/opencode/n10-harness
G=/tmp/opencode/n9-harness/n9bin/libroblox.so
COMMON="-u SOBER_ALLOCATOR_PATCH -u SOBER_N2_JAVA_OBJECT -u SOBER_N1_TRACE -u SOBER_W32_TRACE -u SOBER_W32_CALL720 -u SOBER_W31_OBJ_TRACE -u SOBER_W31_LATCH_WATCH -u SOBER_N8_CTOR3"
for i in 1 2 3; do
  env $COMMON timeout 120 taskset -c 0,1 \
    ./n10bin/guest-init-host "$G" > ab_off_$i.out 2> ab_off_$i.err
  echo $? > ab_off_$i.rc
done
for i in 1 2 3; do
  env $COMMON SOBER_ALLOCATOR_PATCH=1 timeout 120 taskset -c 0,1 \
    ./n10bin/guest-init-host "$G" > ab_on_$i.out 2> ab_on_$i.err
  echo $? > ab_on_$i.rc
done
echo AB_DONE
