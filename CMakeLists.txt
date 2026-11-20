# Native Linux build for the Sober port.
# Owned by worker-3 (task #370).
cmake_minimum_required(VERSION 3.16)
project(sober-native VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)          # _GNU_SOURCE primitives (epoll, signalfd)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Out-of-source: when configured with -B build, the binary is build/sober-native.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_compile_options(-Wall -Wextra -fvisibility=hidden)
add_link_options(-Wl,--as-needed)

# --- source-level Android NDK/Bionic compatibility shim -------------------
# The guest is an x86-64 Android ELF; there is no Win32 surface (decision 004).
add_library(sober_compat STATIC
    src/compat/android_compat.c
    src/compat/gl_egl_forward.c
)
target_include_directories(sober_compat PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/compat"
)
target_link_libraries(sober_compat PUBLIC m pthread ${CMAKE_DL_LIBS})

# --- Linux platform / event loop / runtime backend discovery -------------
# android_relocs.c is the native loader side of DECISION 008: it decodes the
# guest's Android packed DT_ANDROID_RELA stream (ignored by glibc) so the
# init_array pointers are real before any constructor runs.
add_library(sober_platform STATIC
    src/linux/platform.c
    src/linux/gfx_loader.c
    src/linux/android_relocs.c
)
target_include_directories(sober_platform PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/linux"
)
target_link_libraries(sober_platform PUBLIC pthread ${CMAKE_DL_LIBS})

# --- native Bionic bootstrap patcher (worker-1, task #375) ---------------
# Consumed via the published header src/compat/bionic_bootstrap/bionic_bootstrap.h
# and linked into both sober-native and the instrumented dlopen harness.
add_library(sober_bootstrap STATIC
    src/compat/bionic_bootstrap/bionic_bootstrap.c
)
target_include_directories(sober_bootstrap PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/compat/bionic_bootstrap"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/compat"
)
target_link_libraries(sober_bootstrap PUBLIC ${CMAKE_DL_LIBS})

# --- native Linux window + EGL/GLES backend (worker-2, task #379) --------
# Runtime-dlopen driven: no X11/EGL/Wayland LINK dependency, so sober-native's
# ldd stays libc-only. Consumed by the harness, not by sober-native.
add_subdirectory(src/render)

# --- native JNI bridge (worker-2, task #386) -----------------------------
# Pure native JNIEnv/JavaVM reimplementation. Linked only into the harness so
# sober-native's ldd stays libc-only.
add_subdirectory(src/jni)

# --- native allocation tracer (worker-2, task #394) -----------------------
# LD_PRELOAD DSO, built but NEVER linked into the harness or sober-native.
add_subdirectory(src/trace)

# --- native SONAME stubs for the guest's Android DT_NEEDED entries -------
# These are our own tiny Linux DSOs, NOT a translation runtime.
set(SOBER_STUB_DIR "${CMAKE_BINARY_DIR}/ndk-stubs")
file(MAKE_DIRECTORY "${SOBER_STUB_DIR}")

function(sober_ndk_stub target outname define)
    add_library(${target} SHARED src/compat/ndk_stub.c)
    set_target_properties(${target} PROPERTIES
        OUTPUT_NAME "${outname}"
        PREFIX "lib"
        SUFFIX ".so"
        LIBRARY_OUTPUT_DIRECTORY "${SOBER_STUB_DIR}"
    )
    target_compile_definitions(${target} PRIVATE ${define})
endfunction()

sober_ndk_stub(ndk_stub_log        log        STUB_LIBLOG)
sober_ndk_stub(ndk_stub_android    android    STUB_EMPTY)
sober_ndk_stub(ndk_stub_mediandk   mediandk   STUB_EMPTY)
sober_ndk_stub(ndk_stub_openmaxal  OpenMAXAL  STUB_EMPTY)
sober_ndk_stub(ndk_stub_opensles   OpenSLES   STUB_EMPTY)
# glibc installs libm.so/libc.so/libdl.so as GNU ld *scripts*, which an
# Android ELF cannot load. These native empty DSOs carry the SONAME; all
# symbol definitions still come from the real libc in the host's scope.
sober_ndk_stub(ndk_stub_m          m          STUB_EMPTY)
sober_ndk_stub(ndk_stub_dl         dl         STUB_EMPTY)

# Versioned Bionic libc bridge (task #373): our own native DSO that declares
# the LIBC / LIBC_N / LIBC_O version nodes the guest requires and forwards to
# glibc via dlsym. Not a translation layer — pure native code + loader glue.
add_library(ndk_bionic_libc SHARED
    src/compat/bionic_libc.c
    src/compat/bionic_signal.c
    src/compat/bionic_sf_full.c
)
set_target_properties(ndk_bionic_libc PROPERTIES
    OUTPUT_NAME "c"
    PREFIX "lib"
    SUFFIX ".so"
    LIBRARY_OUTPUT_DIRECTORY "${SOBER_STUB_DIR}"
)
target_link_libraries(ndk_bionic_libc PRIVATE ${CMAKE_DL_LIBS})
target_link_options(ndk_bionic_libc PRIVATE
    "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/src/compat/bionic_libc.map"
)

# --- final native ELF ----------------------------------------------------
add_executable(sober-native
    src/linux/main.c
)
set_target_properties(sober-native PROPERTIES
    BUILD_RPATH "$ORIGIN/ndk-stubs"
    INSTALL_RPATH "$ORIGIN/ndk-stubs"
)
target_include_directories(sober-native PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/compat"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/linux"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/compat/bionic_bootstrap"
)
# Keep every symbol in the compat shim (--whole-archive) and publish it in
# the dynamic symbol table (-rdynamic) so a target module loaded with dlopen()
# can resolve Win32 entry points with dlsym(RTLD_DEFAULT, "...").
# ldd stays clean: still only libc, no translation-layer dependency.
target_link_libraries(sober-native PRIVATE
    -Wl,--whole-archive
    sober_compat
    -Wl,--no-whole-archive
    -rdynamic
    "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/src/compat/exports.map"
    sober_platform
    sober_bootstrap
)
add_dependencies(sober-native
    ndk_stub_log ndk_stub_android ndk_stub_mediandk
    ndk_stub_openmaxal ndk_stub_opensles
    ndk_stub_m ndk_stub_dl ndk_bionic_libc
)

# --- instrumented native dlopen harness (worker-3, task #377) ------------
# Drives the canonical DECISION 008 ordering and emits "guest init complete"
# only after all required constructors return. -rdynamic keeps the NDK/Android
# compat symbols available to the guest through the global dynamic scope.
add_executable(guest-init-host
    src/linux/guest_dlopen_host.c
    src/linux/guest_render.c
    src/linux/guest_app_shell.c
)
set_target_properties(guest-init-host PROPERTIES
    BUILD_RPATH "$ORIGIN/ndk-stubs"
    INSTALL_RPATH "$ORIGIN/ndk-stubs"
)
target_include_directories(guest-init-host PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/compat"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/linux"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/compat/bionic_bootstrap"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/render"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/jni"
)
target_link_libraries(guest-init-host PRIVATE
    -Wl,--whole-archive
    sober_compat
    sober_bootstrap
    -Wl,--no-whole-archive
    -rdynamic
    sober_platform
    sober_render
    sober_jni
    ${CMAKE_DL_LIBS}
)
add_dependencies(guest-init-host
    ndk_stub_log ndk_stub_android ndk_stub_mediandk
    ndk_stub_openmaxal ndk_stub_opensles
    ndk_stub_m ndk_stub_dl ndk_bionic_libc
)

# Verification hook used by the lead's test plan (tests/ is lead-owned).
enable_testing()
add_test(NAME sober_native_check COMMAND sober-native --check)
add_test(NAME sober_native_version COMMAND sober-native --version)
