# CMake generated Testfile for 
# Source directory: /home/user/sober-linux
# Build directory: /home/user/sober-linux/build
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(sober_native_check "/home/user/sober-linux/build/sober-native" "--check")
set_tests_properties(sober_native_check PROPERTIES  _BACKTRACE_TRIPLES "/home/user/sober-linux/CMakeLists.txt;190;add_test;/home/user/sober-linux/CMakeLists.txt;0;")
add_test(sober_native_version "/home/user/sober-linux/build/sober-native" "--version")
set_tests_properties(sober_native_version PROPERTIES  _BACKTRACE_TRIPLES "/home/user/sober-linux/CMakeLists.txt;191;add_test;/home/user/sober-linux/CMakeLists.txt;0;")
subdirs("src/render")
subdirs("src/jni")
subdirs("src/trace")
