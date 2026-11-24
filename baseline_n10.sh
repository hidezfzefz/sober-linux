#!/bin/bash
# n10w1 baseline: fail-closed input gate (input missing).
exec 9>/tmp/opencode/harness.lock
flock -x 9
export EGL_PLATFORM=x11 LIBGL_ALWAYS_SOFTWARE=1
export LD_LIBRARY_PATH=/tmp/opencode/n10-harness/n10bin/ndk-stubs
export SOBER_RENDER=1 SOBER_WINDOW_HOLD_MS=0 SOBER_APP_SHELL=1
export SOBER_AVD_BOUNDARY_REPLAY=1
export SOBER_SEEDER_DISABLE=1 SOBER_JNI_ONLOAD_FIRST=1
export SOBER_BIONIC_SF_FULL=1
cd /tmp/opencode/n10-harness
G=/tmp/opencode/n9-harness/n9bin/libroblox.so
env -u SOBER_AVD_BOUNDARY_PATH -u SOBER_AVD_BOUNDARY_UNVERIFIED \
    timeout 120 taskset -c 0,1 \
    ./n10bin/guest-init-host "$G" > baseline.out 2> baseline.err
echo $? > baseline.rc
echo BASELINE_DONE
