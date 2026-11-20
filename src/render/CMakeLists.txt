# Native render backend (worker-2, Phase 3 task #379).
#
# The root CMakeLists.txt is owned by worker-3.  To integrate, add:
#     add_subdirectory(src/render)
# and link `sober_render` PRIVATE into guest-init-host (do NOT link it into
# sober-native unless the version script in src/compat/exports.map is extended).
#
# IMPORTANT: there are deliberately NO link-time dependencies on EGL, GLES2,
# X11 or Wayland.  All of those libraries are dlopen()'d at runtime by
# native_render.c, so `ldd build/sober-native` stays libc/ld-linux only.

add_library(sober_render STATIC
    native_render.c
)
target_include_directories(sober_render PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
# dlopen/dlsym + the small mutex used for the active-context pointer.
target_link_libraries(sober_render PUBLIC ${CMAKE_DL_LIBS} pthread)

# Standalone self-test / headless capability probe.  Safe to build and run on
# a headless machine with `--probe` (it never opens a display).
add_executable(render_probe render_probe.c)
target_link_libraries(render_probe PRIVATE sober_render)
