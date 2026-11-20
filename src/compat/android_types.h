/*
 * android_types.h - Native Linux definitions for the Android NDK API surface
 * imported by libroblox.so (x86-64 Android 26 guest).
 *
 * This is a SOURCE-LEVEL reimplementation: no Android/bionic runtime, no
 * mocktail/basalt, no Wine/box64/qemu. Everything here is backed by native
 * Linux/glibc/epoll/EGL primitives.
 *
 * Owned by worker-3 (task #370). Canonical contract:
 * docs/interfaces/abi-map.md + docs/interfaces/libroblox-imports.txt.
 */
#ifndef SOBER_ANDROID_TYPES_H
#define SOBER_ANDROID_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>

/* ---- export rules (lead decision: deterministic dynamic symbol table) -- */
#if defined(__GNUC__) || defined(__clang__)
#  define SOBER_EXPORT  __attribute__((visibility("default")))
#else
#  define SOBER_EXPORT
#endif
#define SOBER_NATIVE_API SOBER_EXPORT   /* back-compat alias for early tooling */

#ifdef __cplusplus
extern "C" {
#endif

/* ---- opaque NDK object types ------------------------------------------ */
typedef struct AAssetManager  AAssetManager;
typedef struct AAsset         AAsset;
typedef struct AConfiguration AConfiguration;
typedef struct ALooper        ALooper;
typedef struct ANativeWindow  ANativeWindow;

typedef struct AMediaCodecBufferInfo {
    int32_t  offset;
    int32_t  size;
    int64_t  presentationTimeUs;
    uint32_t flags;
} AMediaCodec_buffer_info;
typedef struct AMediaCodec    AMediaCodec;
typedef struct AMediaFormat   AMediaFormat;
typedef struct AImageReader   AImageReader;
typedef struct AImage         AImage;
typedef struct AHardwareBuffer AHardwareBuffer;

/* JNI types are opaque handles to us. */
typedef struct _JNIEnv JNIEnv;
typedef void *jobject;
typedef void *jclass;
typedef void *jmethodID;

/* ---- Android log priorities ------------------------------------------- */
#define ANDROID_LOG_VERBOSE 2
#define ANDROID_LOG_DEBUG   3
#define ANDROID_LOG_INFO    4
#define ANDROID_LOG_WARN    5
#define ANDROID_LOG_ERROR   6
#define ANDROID_LOG_FATAL   7
#define ANDROID_LOG_SILENT  8

/* ---- AAssetManager / AAsset modes ------------------------------------- */
#define AASSET_MODE_UNKNOWN      0
#define AASSET_MODE_RANDOM       1
#define AASSET_MODE_STREAMING    2
#define AASSET_MODE_BUFFER       3

/* ---- AConfiguration ---------------------------------------------------- */
#define ACONFIGURATION_SCREENSIZE_ANY     0x00
#define ACONFIGURATION_SCREENSIZE_SMALL   0x01
#define ACONFIGURATION_SCREENSIZE_NORMAL  0x02
#define ACONFIGURATION_SCREENSIZE_LARGE   0x03
#define ACONFIGURATION_SCREENSIZE_XLARGE  0x04
#define ACONFIGURATION_NAVHIDDEN_ANY      0x00
#define ACONFIGURATION_NAVHIDDEN_NO       0x01
#define ACONFIGURATION_NAVHIDDEN_YES      0x02

/* ---- ALooper ----------------------------------------------------------- */
#define ALOOPER_PREPARE_ALLOW_NON_CALLBACKS 0x01
#define ALOOPER_EVENT_INPUT   0x01
#define ALOOPER_EVENT_OUTPUT  0x02
#define ALOOPER_EVENT_ERROR   0x04
#define ALOOPER_EVENT_HANGUP  0x08
#define ALOOPER_EVENT_INVALID 0x10
#define ALOOPER_POLL_WAKE     (-1)
#define ALOOPER_POLL_CALLBACK (-2)
#define ALOOPER_POLL_TIMEOUT  (-3)
#define ALOOPER_POLL_ERROR    (-4)

typedef int (*ALooper_callbackFunc)(int fd, int events, void *data);

/* ---- AMediaCodec info codes ------------------------------------------- */
#define AMEDIACODEC_INFO_TRY_AGAIN_LATER      (-1)
#define AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED (-3)
#define AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED (-2)
#define AMEDIACODEC_INFO_END_OF_STREAM        (-1)

/* ---- ANativeWindow ----------------------------------------------------- */
#define WINDOW_FORMAT_RGBA_8888 1
#define WINDOW_FORMAT_RGBX_8888 2
#define WINDOW_FORMAT_RGB_565   4

#ifdef __cplusplus
}
#endif

#endif /* SOBER_ANDROID_TYPES_H */
