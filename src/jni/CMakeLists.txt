# Native JNI bridge (worker-2, Phase 4 task #386).
#
# Root CMakeLists.txt is owned by worker-3.  Integrate with:
#     add_subdirectory(src/jni)
# and link `sober_jni` PRIVATE into guest-init-host only.  Do NOT link it into
# sober-native unless src/compat/exports.map is extended (it has `local: *`).
#
# No JVM or JNI library is linked at build time: the bridge synthesises the
# JNIEnv/JavaVM tables itself.  Native JVM discovery in the probe uses
# access(2) only.  ldd stays libc-only.

add_library(sober_jni STATIC
    native_jni.c
    javavm_semantics.c
    jni_native_objects.c
    jni_json_bridge.c
    global_string_seed.c
    startapp_params.c
    app_singleton_bridge.c
    game_params.c
    startgame_v2.c
    jni_strings.c
    startapp_v2.c
    platform_fields.c
    app488_fix.c
    alloc_contract_fix.c
    callback_provenance.c
    static_object_calls.c
    scalar_returns.c
    notifier_probe.c
    app488_inner.c
    jni_arrays.c
    singleton_services.c
    singleton_fix.c
)
target_include_directories(sober_jni PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

# Standalone self-test / headless probe.
add_executable(jni_probe jni_probe.c)
target_link_libraries(jni_probe PRIVATE sober_jni)
