# Native allocation tracer (worker-2, Phase 4b task #394).
#
# This is an LD_PRELOAD DSO, deliberately NOT linked into sober-native or
# guest-init-host, so the normal build stays ldd-clean.  Integrate with:
#     add_subdirectory(src/trace)
# then use build/native_trace.so via LD_PRELOAD when tracing is needed.

add_library(native_trace SHARED
    native_trace.c
)
target_include_directories(native_trace PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(native_trace PRIVATE ${CMAKE_DL_LIBS})
set_target_properties(native_trace PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
