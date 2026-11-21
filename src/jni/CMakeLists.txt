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
    crashpad_return.c
    frame_callbacks.c

    # ---- Phase 8 default modules -------------------------------------------
    # All 15 delivered modules are compiled into sober_jni.  They self-gate
    # (default-ON unless their SOBER_*_DEFAULT=0 kill switch is set); the
    # scheduler_guest module is the one exception and is opt-in
    # (SOBER_SCHEDULER_GUEST=1).  native_jni.c installs exactly one module per
    # bridge responsibility -- the _default modules supersede their older
    # opt-in counterparts (which stay here only because other harness objects
    # still reference their symbols; they are NOT installed by native_jni.c).
    jnislots_default.c
    fields_default.c
    placeid_default.c
    javavm_default.c
    tsd_default.c
    fflag_default.c
    flags_latch_default.c
    alloc_default.c
    singleton_default.c
    app488_default.c
    delegate_default.c
    controller_default.c
    publish_default.c
    provenance_gate.c
    scheduler_guest.c
    render_guest.c

    # ---- 40-agent frame push (all default-ON, fail-closed) ----------------
    frame_synthetic_tag.c
    frame_evidence.c
    frame_dispatch.c
    frame_draw.c
    frame_surface.c
    frame_thread.c
    frame_redraw.c
)
target_include_directories(sober_jni PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

# Standalone self-test / headless probe.
add_executable(jni_probe jni_probe.c)
target_link_libraries(jni_probe PRIVATE sober_jni sober_render)
