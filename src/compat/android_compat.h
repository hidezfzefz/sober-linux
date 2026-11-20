/*
 * android_compat.h - Android NDK/Bionic surface reimplemented natively.
 *
 * Canonical contract: docs/interfaces/abi-map.md and
 * docs/interfaces/libroblox-imports.txt (573 undefined symbols).
 *
 * 396 of those imports are ordinary glibc/libm/pthread symbols and resolve
 * natively in the host process. The remaining 177 are implemented here:
 *   - EGL/GLES  -> tail-call trampolines into dlopen'd libEGL/libGLESv2
 *   - AAsset/AAssetManager, AConfiguration, ALooper, ANativeWindow -> Linux
 *   - AMediaCodec/AMediaFormat -> native structs + documented stub behaviour
 *   - __android_log_*, bionic fortified helpers, AMEDIAFORMAT_KEY_* data
 *
 * Owned by worker-3 (task #370).
 */
#ifndef SOBER_ANDROID_COMPAT_H
#define SOBER_ANDROID_COMPAT_H

#include "android_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- shim lifecycle ---------------------------------------------------- */
SOBER_EXPORT int         sober_android_init(int argc, char **argv);
SOBER_EXPORT void        sober_android_shutdown(void);
SOBER_EXPORT const char *sober_android_backend(void);
SOBER_EXPORT const char *sober_android_gfx_backend(void);
SOBER_EXPORT unsigned    sober_android_stub_count(void);
SOBER_EXPORT unsigned    sober_android_implemented_count(void);
SOBER_EXPORT unsigned    sober_gl_egl_symbol_count(void);

/* ---- logging ----------------------------------------------------------- */
SOBER_EXPORT void __android_log_write(int prio, const char *tag, const char *text);
SOBER_EXPORT void __android_log_buf_write(int bufid, int prio, const char *tag,
                                          const char *text);
SOBER_EXPORT int  __android_log_print(int prio, const char *tag,
                                      const char *fmt, ...);
SOBER_EXPORT void __android_log_assert(const char *cond, const char *tag,
                                       const char *fmt, ...);
SOBER_EXPORT void android_set_abort_message(const char *msg);

/* ---- AAsset / AAssetManager -------------------------------------------- */
SOBER_EXPORT AAssetManager *AAssetManager_fromJava(JNIEnv *env, jobject obj);
SOBER_EXPORT AAsset *AAssetManager_open(AAssetManager *mgr, const char *filename,
                                        int mode);
SOBER_EXPORT void AAsset_close(AAsset *asset);
SOBER_EXPORT off_t AAsset_getLength(AAsset *asset);
SOBER_EXPORT const void *AAsset_getBuffer(AAsset *asset);
SOBER_EXPORT int AAsset_openFileDescriptor(AAsset *asset, off_t *outStart,
                                           off_t *outLength);

/* ---- AConfiguration ---------------------------------------------------- */
SOBER_EXPORT AConfiguration *AConfiguration_new(void);
SOBER_EXPORT void AConfiguration_delete(AConfiguration *config);
SOBER_EXPORT void AConfiguration_fromAssetManager(AConfiguration *out,
                                                  AAssetManager *mgr);
SOBER_EXPORT void AConfiguration_getCountry(AConfiguration *config, char *out);
SOBER_EXPORT void AConfiguration_getLanguage(AConfiguration *config, char *out);
SOBER_EXPORT int  AConfiguration_getNavHidden(AConfiguration *config);
SOBER_EXPORT int  AConfiguration_getScreenHeightDp(AConfiguration *config);
SOBER_EXPORT int  AConfiguration_getScreenSize(AConfiguration *config);
SOBER_EXPORT int  AConfiguration_getScreenWidthDp(AConfiguration *config);

/* ---- ALooper ----------------------------------------------------------- */
SOBER_EXPORT ALooper *ALooper_prepare(int opts);
SOBER_EXPORT ALooper *ALooper_forThread(void);
SOBER_EXPORT void ALooper_acquire(ALooper *looper);
SOBER_EXPORT void ALooper_release(ALooper *looper);
SOBER_EXPORT int  ALooper_addFd(ALooper *looper, int fd, int ident, int events,
                                ALooper_callbackFunc callback, void *data);
SOBER_EXPORT int  ALooper_removeFd(ALooper *looper, int fd);
SOBER_EXPORT int  ALooper_pollOnce(int timeoutMillis, int *outFd,
                                   int *outEvents, void **outData);

/* ---- ANativeWindow ----------------------------------------------------- */
SOBER_EXPORT ANativeWindow *ANativeWindow_fromSurface(JNIEnv *env, jobject surface);
SOBER_EXPORT void ANativeWindow_acquire(ANativeWindow *window);
SOBER_EXPORT void ANativeWindow_release(ANativeWindow *window);
SOBER_EXPORT int32_t ANativeWindow_getWidth(ANativeWindow *window);
SOBER_EXPORT int32_t ANativeWindow_getHeight(ANativeWindow *window);

/* ---- AMediaFormat ------------------------------------------------------ */
SOBER_EXPORT AMediaFormat *AMediaFormat_new(void);
SOBER_EXPORT void AMediaFormat_delete(AMediaFormat *format);
SOBER_EXPORT bool AMediaFormat_setInt32(AMediaFormat *format, const char *name,
                                        int32_t value);
SOBER_EXPORT bool AMediaFormat_setFloat(AMediaFormat *format, const char *name,
                                        float value);
SOBER_EXPORT bool AMediaFormat_setString(AMediaFormat *format, const char *name,
                                         const char *value);
SOBER_EXPORT bool AMediaFormat_setBuffer(AMediaFormat *format, const char *name,
                                         const void *data, size_t size);
SOBER_EXPORT bool AMediaFormat_getInt32(AMediaFormat *format, const char *name,
                                        int32_t *out);
SOBER_EXPORT bool AMediaFormat_getBuffer(AMediaFormat *format, const char *name,
                                         void **data, size_t *size);
SOBER_EXPORT char *AMediaFormat_toString(AMediaFormat *format);

/* ---- AMediaCodec ------------------------------------------------------- */
SOBER_EXPORT AMediaCodec *AMediaCodec_createDecoderByType(const char *mime);
SOBER_EXPORT AMediaCodec *AMediaCodec_createEncoderByType(const char *mime);
SOBER_EXPORT void AMediaCodec_delete(AMediaCodec *codec);
SOBER_EXPORT int  AMediaCodec_configure(AMediaCodec *codec,
                        const AMediaFormat *format, ANativeWindow *surface,
                        void *crypto, uint32_t flags);
SOBER_EXPORT int  AMediaCodec_start(AMediaCodec *codec);
SOBER_EXPORT int  AMediaCodec_stop(AMediaCodec *codec);
SOBER_EXPORT int  AMediaCodec_flush(AMediaCodec *codec);
SOBER_EXPORT ssize_t AMediaCodec_dequeueInputBuffer(AMediaCodec *codec,
                                                    int64_t timeoutUs);
SOBER_EXPORT uint8_t *AMediaCodec_getInputBuffer(AMediaCodec *codec,
                        size_t idx, size_t *outSize);
SOBER_EXPORT int  AMediaCodec_queueInputBuffer(AMediaCodec *codec, size_t idx,
                        off_t offset, size_t size, uint64_t timeUs,
                        uint32_t flags);
SOBER_EXPORT ssize_t AMediaCodec_dequeueOutputBuffer(AMediaCodec *codec,
                        AMediaCodec_buffer_info *info, int64_t timeoutUs);
SOBER_EXPORT uint8_t *AMediaCodec_getOutputBuffer(AMediaCodec *codec,
                        size_t idx, size_t *outOffset, size_t *outSize);
SOBER_EXPORT AMediaFormat *AMediaCodec_getOutputFormat(AMediaCodec *codec);
SOBER_EXPORT void AMediaCodec_releaseOutputBuffer(AMediaCodec *codec,
                        size_t idx, bool render);

/* ---- bionic-only libc helpers ------------------------------------------ */
SOBER_EXPORT int  *__errno(void);
SOBER_EXPORT int   __system_property_get(const char *name, char *value);
SOBER_EXPORT const char *__gnu_strerror_r(int err, char *buf, size_t n);
SOBER_EXPORT void  __assert2(const char *file, int line,
                             const char *func, const char *expr);
SOBER_EXPORT void  __android_log_assert_shim(const char *expr);
SOBER_EXPORT size_t __strlen_chk(const char *s, size_t n);
SOBER_EXPORT char *__strchr_chk(const char *s, int c, size_t n);
SOBER_EXPORT char *__strncpy_chk2(char *dst, const char *src, size_t n,
                                  size_t dstlen, size_t srclen);
SOBER_EXPORT void *__memcpy_chk(void *dst, const void *src, size_t n, size_t dstlen);
SOBER_EXPORT size_t __fwrite_chk(const void *buf, size_t size, size_t n,
                                 FILE *f, size_t buflen);
SOBER_EXPORT ssize_t __write_chk(int fd, const void *buf, size_t n, size_t buflen);
SOBER_EXPORT ssize_t __sendto_chk(int fd, const void *buf, size_t n, int flags,
                                  const void *addr, socklen_t addrlen, size_t buflen);
SOBER_EXPORT void __FD_SET_chk(int fd, void *set, unsigned n);
SOBER_EXPORT void __FD_CLR_chk(int fd, void *set, unsigned n);
SOBER_EXPORT int  __FD_ISSET_chk(int fd, const void *set, unsigned n);
SOBER_EXPORT void __gcov_dump(void);
SOBER_EXPORT void __gcov_flush(void);

/* ---- ZSTD trace hooks (weak in the guest) ------------------------------ */
SOBER_EXPORT void *ZSTD_trace_compress_begin(void *ctx);
SOBER_EXPORT void  ZSTD_trace_compress_end(void *ctx, void *t);
SOBER_EXPORT void *ZSTD_trace_decompress_begin(void *ctx);
SOBER_EXPORT void  ZSTD_trace_decompress_end(void *ctx, void *t);

/* ---- AMediaFormat_KEY_* are DATA symbols, not functions ---------------- */
SOBER_EXPORT extern const char *AMEDIAFORMAT_KEY_MIME;
SOBER_EXPORT extern const char *AMEDIAFORMAT_KEY_WIDTH;
SOBER_EXPORT extern const char *AMEDIAFORMAT_KEY_HEIGHT;
SOBER_EXPORT extern const char *AMEDIAFORMAT_KEY_BIT_RATE;
SOBER_EXPORT extern const char *AMEDIAFORMAT_KEY_FRAME_RATE;
SOBER_EXPORT extern const char *AMEDIAFORMAT_KEY_I_FRAME_INTERVAL;
SOBER_EXPORT extern const char *AMEDIAFORMAT_KEY_CHANNEL_COUNT;
SOBER_EXPORT extern const char *AMEDIAFORMAT_KEY_SAMPLE_RATE;
SOBER_EXPORT extern const char *AMEDIAFORMAT_KEY_COLOR_FORMAT;
SOBER_EXPORT extern const char *AMEDIAFORMAT_KEY_STRIDE;

/* ---- data symbols used by bionic-compiled code ------------------------- */
SOBER_EXPORT extern unsigned char __sF[0x200];
SOBER_EXPORT extern uintptr_t __stack_chk_guard;

#ifdef __cplusplus
}
#endif

#endif /* SOBER_ANDROID_COMPAT_H */
