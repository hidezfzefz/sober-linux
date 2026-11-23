#!/bin/bash
# n2 step3: DEX-typed harness Java object ON x3 + OFF baseline x3.
exec 9>/tmp/opencode/harness.lock
flock -x 9
export EGL_PLATFORM=x11 LIBGL_ALWAYS_SOFTWARE=1
export LD_LIBRARY_PATH=/tmp/opencode/n2-harness/build-n2/ndk-stubs
export SOBER_RENDER=1 SOBER_WINDOW_HOLD_MS=0 SOBER_APP_SHELL=1
export SOBER_AVD_BOUNDARY_REPLAY=1
export SOBER_SEEDER_DISABLE=1 SOBER_JNI_ONLOAD_FIRST=1
cd /tmp/opencode/n2-harness
G=/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so
COMMON="-u SOBER_AVD_BOUNDARY_PATH -u SOBER_AVD_BOUNDARY_UNVERIFIED -u SOBER_W32_TRACE -u SOBER_W32_CALL720 -u SOBER_W31_OBJ_TRACE -u SOBER_W31_LATCH_WATCH -u SOBER_W28_PAIRS -u SOBER_W21_WATCH -u SOBER_W26_WATCH -u SOBER_N2_TABLE -u SOBER_N1_SKIP_THIRD -u SOBER_N1_NO_PREBASE_REPLAY -u SOBER_N1_NO_MISSING_NATIVES"
for i in 1 2 3; do
  env $COMMON SOBER_N2_JAVA_OBJECT=1 SOBER_N1_TRACE=1 timeout 120 taskset -c 0,1 \
    ./build-n2/guest-init-host "$G" > obj_on_$i.out 2> obj_on_$i.err
  echo $? > obj_on_$i.rc
done
for i in 1 2 3; do
  env $COMMON SOBER_N1_TRACE=1 timeout 120 taskset -c 0,1 \
    ./build-n2/guest-init-host "$G" > obj_off_$i.out 2> obj_off_$i.err
  echo $? > obj_off_$i.rc
done
echo RUN_D6_OBJ_DONE
