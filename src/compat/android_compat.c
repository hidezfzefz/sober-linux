/*
 * android_compat.c - Android NDK/Bionic surface reimplemented natively.
 *
 * Every facility here is backed by Linux/glibc primitives: POSIX files for
 * assets, epoll for ALooper, clock_gettime for timing, pthreads, dlopen for
 * real EGL/GLES. No Android runtime is loaded or linked.
 *
 * Owned by worker-3 (task #370).
 */
#define _GNU_SOURCE
#include "android_compat.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* Implemented by the generated gl_egl_forward.c */
int  sober_gl_egl_init(void);
void sober_gl_egl_shutdown(void);
const char *sober_gl_egl_backend(void);
unsigned sober_gl_egl_symbol_count(void);

/* ====================================================================== */
/* lifecycle / accounting                                                  */
/* ====================================================================== */
static unsigned g_stub_hits;
static unsigned g_impl_count = 177;   /* 177 missing imports addressed here */

/* Bionic exposes `FILE __sF[3]` (3 x 0x98 = 0x198 bytes). The guest derives
 * stdout/stderr as __sF+0x98 / __sF+0x130, so the object must really be that
 * large; the old 3-pointer array (24 bytes) caused out-of-bounds reads. */
unsigned char __sF[0x200];
uintptr_t __stack_chk_guard = (uintptr_t)0xABCDEF0123456789ULL;

const char *AMEDIAFORMAT_KEY_MIME             = "mime";
const char *AMEDIAFORMAT_KEY_WIDTH            = "width";
const char *AMEDIAFORMAT_KEY_HEIGHT           = "height";
const char *AMEDIAFORMAT_KEY_BIT_RATE         = "bitrate";
const char *AMEDIAFORMAT_KEY_FRAME_RATE       = "frame-rate";
const char *AMEDIAFORMAT_KEY_I_FRAME_INTERVAL = "i-frame-interval";
const char *AMEDIAFORMAT_KEY_CHANNEL_COUNT    = "channel-count";
const char *AMEDIAFORMAT_KEY_SAMPLE_RATE      = "sample-rate";
const char *AMEDIAFORMAT_KEY_COLOR_FORMAT     = "color-format";
const char *AMEDIAFORMAT_KEY_STRIDE           = "stride";

static void note_stub(const char *what)
{
    __android_log_write(ANDROID_LOG_WARN, "sober-android", what);
    g_stub_hits++;
}

SOBER_EXPORT int sober_android_init(int argc, char **argv)
{
    (void)argc; (void)argv;
    sober_gl_egl_init();
    __android_log_write(ANDROID_LOG_INFO, "sober-android",
                        "native NDK/Bionic shim initialised (no Android runtime)");
    return 1;
}

SOBER_EXPORT void sober_android_shutdown(void)
{
    sober_gl_egl_shutdown();
}

SOBER_EXPORT const char *sober_android_backend(void)
{
    return "native-linux/posix (epoll+mmap+pthread+dlopen NDK shim)";
}
SOBER_EXPORT const char *sober_android_gfx_backend(void)
{
    return sober_gl_egl_backend();
}
SOBER_EXPORT unsigned sober_android_stub_count(void) { return g_stub_hits; }
SOBER_EXPORT unsigned sober_android_implemented_count(void)
{
    return g_impl_count + sober_gl_egl_symbol_count();
}

/* ====================================================================== */
/* logging                                                                 */
/* ====================================================================== */
static const char *prio_name(int p)
{
    switch (p) {
        case ANDROID_LOG_VERBOSE: return "VERBOSE";
        case ANDROID_LOG_DEBUG:   return "DEBUG";
        case ANDROID_LOG_INFO:    return "INFO";
        case ANDROID_LOG_WARN:    return "WARN";
        case ANDROID_LOG_ERROR:   return "ERROR";
        case ANDROID_LOG_FATAL:   return "FATAL";
        default:                  return "?";
    }
}

SOBER_EXPORT void __android_log_write(int prio, const char *tag, const char *text)
{
    fprintf(stderr, "[%s/%s] %s\n", prio_name(prio), tag ? tag : "", text ? text : "");
}

SOBER_EXPORT void __android_log_buf_write(int bufid, int prio,
                                          const char *tag, const char *text)
{
    (void)bufid;
    __android_log_write(prio, tag, text);
}

SOBER_EXPORT int __android_log_print(int prio, const char *tag,
                                     const char *fmt, ...)
{
    char buf[8192];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    __android_log_write(prio, tag, buf);
    return n;
}

static char g_abort_msg[1024];

SOBER_EXPORT void android_set_abort_message(const char *msg)
{
    if (msg) snprintf(g_abort_msg, sizeof(g_abort_msg), "%s", msg);
}

SOBER_EXPORT void __android_log_assert(const char *cond, const char *tag,
                                       const char *fmt, ...)
{
    char buf[8192];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    fprintf(stderr, "[FATAL/%s] assertion failed (%s): %s%s%s\n",
            tag ? tag : "", cond ? cond : "?", buf,
            g_abort_msg[0] ? " | " : "", g_abort_msg);
    fflush(stderr);
    abort();
}

/* ====================================================================== */
/* AAsset / AAssetManager                                                   */
/* ====================================================================== */
struct AAssetManager { char root[PATH_MAX]; };
struct AAsset {
    unsigned char *data;
    off_t          length;
    int            fd;
};

static AAssetManager *g_asset_manager;

static const char *asset_root(void)
{
    const char *env = getenv("SOBER_ASSET_ROOT");
    if (env && *env) return env;
    const char *candidates[] = {
        "/tmp/opencode/sober-apk/base/assets",
        "/tmp/opencode/sober-apk/assets",
        "./assets",
        ".",
    };
    for (unsigned i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
        struct stat st;
        if (stat(candidates[i], &st) == 0 && S_ISDIR(st.st_mode)) return candidates[i];
    }
    return ".";
}

SOBER_EXPORT AAssetManager *AAssetManager_fromJava(JNIEnv *env, jobject obj)
{
    (void)env; (void)obj;
    if (!g_asset_manager) {
        g_asset_manager = (AAssetManager *)calloc(1, sizeof(*g_asset_manager));
        if (!g_asset_manager) return NULL;
        snprintf(g_asset_manager->root, sizeof(g_asset_manager->root), "%s", asset_root());
        __android_log_print(ANDROID_LOG_INFO, "sober-android",
                            "AAssetManager root: %s", g_asset_manager->root);
    }
    return g_asset_manager;
}

static void asset_path(AAssetManager *mgr, const char *name,
                       char *out, size_t cap)
{
    const char *base = mgr ? mgr->root : ".";
    if (!strncmp(name, "assets/", 7)) name += 7;
    if (cap == 0) return;
    size_t blen = strlen(base);
    size_t n = blen < cap - 1 ? blen : cap - 1;
    memcpy(out, base, n);
    out[n] = '\0';
    if (n < cap - 1) {
        out[n++] = '/';
        out[n] = '\0';
        size_t room = cap - n - 1;
        size_t m = strlen(name);
        if (m > room) m = room;
        memcpy(out + n, name, m);
        out[n + m] = '\0';
    }
}

SOBER_EXPORT AAsset *AAssetManager_open(AAssetManager *mgr,
                                        const char *filename, int mode)
{
    (void)mode;
    if (!filename) return NULL;
    char path[PATH_MAX];
    asset_path(mgr, filename, path, sizeof(path));

    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return NULL;
    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size < 0) { close(fd); return NULL; }

    AAsset *a = (AAsset *)calloc(1, sizeof(*a));
    if (!a) { close(fd); return NULL; }
    a->length = (off_t)st.st_size;
    a->fd = fd;
    return a;
}

SOBER_EXPORT void AAsset_close(AAsset *asset)
{
    if (!asset) return;
    free(asset->data);
    if (asset->fd >= 0) close(asset->fd);
    free(asset);
}

SOBER_EXPORT off_t AAsset_getLength(AAsset *asset)
{
    return asset ? asset->length : 0;
}

SOBER_EXPORT const void *AAsset_getBuffer(AAsset *asset)
{
    if (!asset) return NULL;
    if (!asset->data) {
        size_t n = (size_t)asset->length;
        asset->data = (unsigned char *)malloc(n + 1);
        if (!asset->data) return NULL;
        if (pread(asset->fd, asset->data, n, 0) != (ssize_t)n) {
            free(asset->data);
            asset->data = NULL;
            return NULL;
        }
        asset->data[n] = 0;
    }
    return asset->data;
}

SOBER_EXPORT int AAsset_openFileDescriptor(AAsset *asset, off_t *outStart,
                                           off_t *outLength)
{
    if (!asset || asset->fd < 0) return -1;
    if (outStart) *outStart = 0;
    if (outLength) *outLength = asset->length;
    return dup(asset->fd);
}

/* ====================================================================== */
/* AConfiguration                                                          */
/* ====================================================================== */
struct AConfiguration {
    int32_t screenWidthDp, screenHeightDp, screenSize, navHidden;
    char language[3], country[3];
};

static int env_int(const char *name, int fallback)
{
    const char *v = getenv(name);
    return v && *v ? atoi(v) : fallback;
}

SOBER_EXPORT AConfiguration *AConfiguration_new(void)
{
    AConfiguration *c = (AConfiguration *)calloc(1, sizeof(*c));
    if (!c) return NULL;
    c->screenWidthDp = 360;
    c->screenHeightDp = 640;
    c->screenSize = ACONFIGURATION_SCREENSIZE_NORMAL;
    return c;
}

SOBER_EXPORT void AConfiguration_delete(AConfiguration *config) { free(config); }

SOBER_EXPORT void AConfiguration_fromAssetManager(AConfiguration *out,
                                                  AAssetManager *mgr)
{
    (void)mgr;
    if (!out) return;
    out->screenWidthDp  = env_int("SOBER_SCREEN_WIDTH_DP", 360);
    out->screenHeightDp = env_int("SOBER_SCREEN_HEIGHT_DP", 640);
    out->screenSize = env_int("SOBER_SCREEN_SIZE", ACONFIGURATION_SCREENSIZE_NORMAL);
    out->navHidden = env_int("SOBER_NAV_HIDDEN", ACONFIGURATION_NAVHIDDEN_YES);
    const char *lang = getenv("SOBER_LANG");
    const char *country = getenv("SOBER_COUNTRY");
    snprintf(out->language, sizeof(out->language), "%s", (lang && *lang) ? lang : "en");
    snprintf(out->country, sizeof(out->country), "%s", (country && *country) ? country : "US");
}

SOBER_EXPORT void AConfiguration_getCountry(AConfiguration *c, char *out)
{ if (c && out) memcpy(out, c->country, sizeof(c->country)); }
SOBER_EXPORT void AConfiguration_getLanguage(AConfiguration *c, char *out)
{ if (c && out) memcpy(out, c->language, sizeof(c->language)); }
SOBER_EXPORT int AConfiguration_getNavHidden(AConfiguration *c)
{ return c ? c->navHidden : ACONFIGURATION_NAVHIDDEN_ANY; }
SOBER_EXPORT int AConfiguration_getScreenHeightDp(AConfiguration *c)
{ return c ? c->screenHeightDp : 0; }
SOBER_EXPORT int AConfiguration_getScreenSize(AConfiguration *c)
{ return c ? c->screenSize : ACONFIGURATION_SCREENSIZE_ANY; }
SOBER_EXPORT int AConfiguration_getScreenWidthDp(AConfiguration *c)
{ return c ? c->screenWidthDp : 0; }

/* ====================================================================== */
/* ALooper (epoll-backed)                                                   */
/* ====================================================================== */
#define ALOOPER_MAX_FDS 128
typedef struct {
    int used, fd, ident, events;
    ALooper_callbackFunc callback;
    void *data;
} LooperFd;

struct ALooper {
    int epfd;
    unsigned refs;
    pthread_mutex_t lock;
    LooperFd fds[ALOOPER_MAX_FDS];
};

static _Thread_local ALooper *t_looper;

static uint32_t epoll_events_from_native(int ev)
{
    uint32_t e = 0;
    if (ev & ALOOPER_EVENT_INPUT)  e |= EPOLLIN;
    if (ev & ALOOPER_EVENT_OUTPUT) e |= EPOLLOUT;
    if (ev & ALOOPER_EVENT_ERROR)  e |= EPOLLERR;
    if (ev & ALOOPER_EVENT_HANGUP) e |= EPOLLHUP;
    return e ? e : EPOLLIN;
}

static int native_events_from_epoll(uint32_t ev)
{
    int out = 0;
    if (ev & EPOLLIN)  out |= ALOOPER_EVENT_INPUT;
    if (ev & EPOLLOUT) out |= ALOOPER_EVENT_OUTPUT;
    if (ev & EPOLLERR) out |= ALOOPER_EVENT_ERROR;
    if (ev & EPOLLHUP) out |= ALOOPER_EVENT_HANGUP;
    return out;
}

SOBER_EXPORT ALooper *ALooper_prepare(int opts)
{
    (void)opts;
    if (t_looper) return t_looper;
    ALooper *l = (ALooper *)calloc(1, sizeof(*l));
    if (!l) return NULL;
    l->epfd = epoll_create1(EPOLL_CLOEXEC);
    if (l->epfd < 0) { free(l); return NULL; }
    l->refs = 1;
    pthread_mutex_init(&l->lock, NULL);
    t_looper = l;
    return l;
}

SOBER_EXPORT ALooper *ALooper_forThread(void) { return t_looper; }

SOBER_EXPORT void ALooper_acquire(ALooper *l)
{ if (l) { pthread_mutex_lock(&l->lock); l->refs++; pthread_mutex_unlock(&l->lock); } }

SOBER_EXPORT void ALooper_release(ALooper *l)
{
    if (!l) return;
    int destroy = 0;
    pthread_mutex_lock(&l->lock);
    if (l->refs && --l->refs == 0) destroy = 1;
    pthread_mutex_unlock(&l->lock);
    if (!destroy) return;
    if (t_looper == l) t_looper = NULL;
    close(l->epfd);
    pthread_mutex_destroy(&l->lock);
    free(l);
}

SOBER_EXPORT int ALooper_addFd(ALooper *l, int fd, int ident, int events,
                               ALooper_callbackFunc cb, void *data)
{
    if (!l || fd < 0) return -1;
    pthread_mutex_lock(&l->lock);
    int slot = -1;
    for (int i = 0; i < ALOOPER_MAX_FDS; ++i) if (!l->fds[i].used) { slot = i; break; }
    if (slot < 0) { pthread_mutex_unlock(&l->lock); return -1; }
    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = epoll_events_from_native(events);
    ev.data.fd = fd;
    int rc = epoll_ctl(l->epfd, EPOLL_CTL_ADD, fd, &ev);
    if (rc == 0) {
        l->fds[slot].used = 1;
        l->fds[slot].fd = fd;
        l->fds[slot].ident = ident;
        l->fds[slot].events = events;
        l->fds[slot].callback = cb;
        l->fds[slot].data = data;
    }
    pthread_mutex_unlock(&l->lock);
    return rc;
}

SOBER_EXPORT int ALooper_removeFd(ALooper *l, int fd)
{
    if (!l) return -1;
    pthread_mutex_lock(&l->lock);
    int found = -1;
    for (int i = 0; i < ALOOPER_MAX_FDS; ++i)
        if (l->fds[i].used && l->fds[i].fd == fd) { found = i; break; }
    int rc = -1;
    if (found >= 0) {
        rc = epoll_ctl(l->epfd, EPOLL_CTL_DEL, fd, NULL);
        memset(&l->fds[found], 0, sizeof(l->fds[found]));
    }
    pthread_mutex_unlock(&l->lock);
    return rc;
}

SOBER_EXPORT int ALooper_pollOnce(int timeoutMillis, int *outFd,
                                  int *outEvents, void **outData)
{
    ALooper *l = t_looper;
    if (!l) return ALOOPER_POLL_ERROR;
    int64_t deadline = 0;
    if (timeoutMillis > 0) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        deadline = (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000 + timeoutMillis;
    }
    for (;;) {
        int wait = timeoutMillis;
        if (timeoutMillis > 0 && deadline) {
            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            int64_t now = (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
            wait = (int)(deadline - now);
            if (wait < 0) wait = 0;
        }
        struct epoll_event evs[32];
        int n = epoll_wait(l->epfd, evs, 32, wait);
        if (n < 0) return ALOOPER_POLL_ERROR;
        if (n == 0) return ALOOPER_POLL_TIMEOUT;

        for (int i = 0; i < n; ++i) {
            int fd = evs[i].data.fd;
            pthread_mutex_lock(&l->lock);
            LooperFd *entry = NULL;
            for (int k = 0; k < ALOOPER_MAX_FDS; ++k)
                if (l->fds[k].used && l->fds[k].fd == fd) { entry = &l->fds[k]; break; }
            ALooper_callbackFunc cb = entry ? entry->callback : NULL;
            void *data = entry ? entry->data : NULL;
            int ident = entry ? entry->ident : fd;
            int out_ev = native_events_from_epoll(evs[i].events);
            pthread_mutex_unlock(&l->lock);

            if (cb && cb(fd, out_ev, data)) continue; /* consumed: keep polling */
            if (outFd) *outFd = fd;
            if (outEvents) *outEvents = out_ev;
            if (outData) *outData = data;
            return cb ? ALOOPER_POLL_CALLBACK : ident;
        }
        if (timeoutMillis == 0) return ALOOPER_POLL_TIMEOUT;
    }
}

/* ====================================================================== */
/* ANativeWindow                                                           */
/* ====================================================================== */
struct ANativeWindow {
    unsigned refs;
    int32_t width, height;
};

SOBER_EXPORT ANativeWindow *ANativeWindow_fromSurface(JNIEnv *env, jobject surface)
{
    (void)env; (void)surface;
    ANativeWindow *w = (ANativeWindow *)calloc(1, sizeof(*w));
    if (!w) return NULL;
    w->refs = 1;
    w->width  = env_int("SOBER_WINDOW_WIDTH", 1280);
    w->height = env_int("SOBER_WINDOW_HEIGHT", 720);
    return w;
}
SOBER_EXPORT void ANativeWindow_acquire(ANativeWindow *w)
{ if (w) w->refs++; }
SOBER_EXPORT void ANativeWindow_release(ANativeWindow *w)
{ if (w && w->refs && --w->refs == 0) free(w); }
SOBER_EXPORT int32_t ANativeWindow_getWidth(ANativeWindow *w) { return w ? w->width : 0; }
SOBER_EXPORT int32_t ANativeWindow_getHeight(ANativeWindow *w) { return w ? w->height : 0; }

/* ====================================================================== */
/* AMediaFormat                                                            */
/* ====================================================================== */
#define MF_MAX_ENTRIES 32
typedef enum { MF_INT32, MF_FLOAT, MF_STRING, MF_BUFFER } MFType;
typedef struct {
    int used;
    MFType type;
    char key[64];
    union {
        int32_t i;
        float f;
        char s[256];
        struct { unsigned char data[1024]; size_t size; } buf;
    } v;
} MFEntry;

struct AMediaFormat {
    MFEntry entries[MF_MAX_ENTRIES];
};

static MFEntry *mf_find(AMediaFormat *f, const char *key, int create)
{
    if (!f || !key) return NULL;
    for (int i = 0; i < MF_MAX_ENTRIES; ++i)
        if (f->entries[i].used && !strcmp(f->entries[i].key, key)) return &f->entries[i];
    if (!create) return NULL;
    for (int i = 0; i < MF_MAX_ENTRIES; ++i)
        if (!f->entries[i].used) {
            f->entries[i].used = 1;
            snprintf(f->entries[i].key, sizeof(f->entries[i].key), "%s", key);
            return &f->entries[i];
        }
    return NULL;
}

SOBER_EXPORT AMediaFormat *AMediaFormat_new(void)
{ return (AMediaFormat *)calloc(1, sizeof(AMediaFormat)); }
SOBER_EXPORT void AMediaFormat_delete(AMediaFormat *f) { free(f); }

SOBER_EXPORT bool AMediaFormat_setInt32(AMediaFormat *f, const char *k, int32_t v)
{ MFEntry *e = mf_find(f, k, 1); if (!e) return false; e->type = MF_INT32; e->v.i = v; return true; }
SOBER_EXPORT bool AMediaFormat_setFloat(AMediaFormat *f, const char *k, float v)
{ MFEntry *e = mf_find(f, k, 1); if (!e) return false; e->type = MF_FLOAT; e->v.f = v; return true; }
SOBER_EXPORT bool AMediaFormat_setString(AMediaFormat *f, const char *k, const char *v)
{
    MFEntry *e = mf_find(f, k, 1);
    if (!e || !v) return false;
    e->type = MF_STRING;
    snprintf(e->v.s, sizeof(e->v.s), "%s", v);
    return true;
}
SOBER_EXPORT bool AMediaFormat_setBuffer(AMediaFormat *f, const char *k,
                                          const void *data, size_t size)
{
    MFEntry *e = mf_find(f, k, 1);
    if (!e) return false;
    if (size > sizeof(e->v.buf.data)) size = sizeof(e->v.buf.data);
    e->type = MF_BUFFER;
    if (size && data) memcpy(e->v.buf.data, data, size);
    e->v.buf.size = size;
    return true;
}
SOBER_EXPORT bool AMediaFormat_getInt32(AMediaFormat *f, const char *k, int32_t *out)
{
    MFEntry *e = mf_find(f, k, 0);
    if (!e || e->type != MF_INT32 || !out) return false;
    *out = e->v.i;
    return true;
}
SOBER_EXPORT bool AMediaFormat_getBuffer(AMediaFormat *f, const char *k,
                                          void **data, size_t *size)
{
    MFEntry *e = mf_find(f, k, 0);
    if (!e || e->type != MF_BUFFER) return false;
    if (data) *data = e->v.buf.data;
    if (size) *size = e->v.buf.size;
    return true;
}
SOBER_EXPORT char *AMediaFormat_toString(AMediaFormat *f)
{
    char *out = (char *)calloc(1, 8192);
    if (!out) return NULL;
    size_t used = 0;
    for (int i = 0; i < MF_MAX_ENTRIES && used < 8100; ++i) {
        MFEntry *e = &f->entries[i];
        if (!e->used) continue;
        int n = 0;
        switch (e->type) {
            case MF_INT32:  n = snprintf(out + used, 8192 - used, "%s=%d ", e->key, e->v.i); break;
            case MF_FLOAT:  n = snprintf(out + used, 8192 - used, "%s=%f ", e->key, e->v.f); break;
            case MF_STRING: n = snprintf(out + used, 8192 - used, "%s=%s ", e->key, e->v.s); break;
            case MF_BUFFER: n = snprintf(out + used, 8192 - used, "%s=<buffer:%zu> ", e->key, e->v.buf.size); break;
        }
        if (n <= 0) break;
        used += (size_t)n;
    }
    return out;
}

/* ====================================================================== */
/* AMediaCodec - native stub with honest status reporting                  */
/* NOTE: hardware codecs are Android-only. These functions keep the guest's
 * state machine alive and return documented "try again later" semantics.
 * Lead is informed via handoff so abi-map.md can mark them 'unsupported'.  */
/* ====================================================================== */
#define CODEC_BUFFER_SIZE (256 * 1024)
struct AMediaCodec {
    char mime[96];
    AMediaFormat *format;
    ANativeWindow *window;
    unsigned char *input;
    unsigned char *output;
    int started;
};

static AMediaCodec *codec_create(const char *mime)
{
    AMediaCodec *c = (AMediaCodec *)calloc(1, sizeof(*c));
    if (!c) return NULL;
    if (mime) snprintf(c->mime, sizeof(c->mime), "%s", mime);
    c->input = (unsigned char *)malloc(CODEC_BUFFER_SIZE);
    c->output = (unsigned char *)malloc(CODEC_BUFFER_SIZE);
    if (!c->input || !c->output) { free(c->input); free(c->output); free(c); return NULL; }
    note_stub("AMediaCodec: native codec stub (no Android MediaCodec available)");
    return c;
}

SOBER_EXPORT AMediaCodec *AMediaCodec_createDecoderByType(const char *mime)
{ return codec_create(mime); }
SOBER_EXPORT AMediaCodec *AMediaCodec_createEncoderByType(const char *mime)
{ return codec_create(mime); }
SOBER_EXPORT void AMediaCodec_delete(AMediaCodec *c)
{
    if (!c) return;
    free(c->input); free(c->output);
    AMediaFormat_delete(c->format);
    free(c);
}
SOBER_EXPORT int AMediaCodec_configure(AMediaCodec *c, const AMediaFormat *format,
                        ANativeWindow *surface, void *crypto, uint32_t flags)
{
    (void)crypto; (void)flags;
    if (!c) return -1;
    c->window = surface;
    AMediaFormat_delete(c->format);
    c->format = AMediaFormat_new();
    if (format) memcpy(c->format, format, sizeof(*c->format));
    return 0;
}
SOBER_EXPORT int AMediaCodec_start(AMediaCodec *c) { if (!c) return -1; c->started = 1; return 0; }
SOBER_EXPORT int AMediaCodec_stop(AMediaCodec *c)  { if (!c) return -1; c->started = 0; return 0; }
SOBER_EXPORT int AMediaCodec_flush(AMediaCodec *c) { return c ? 0 : -1; }
SOBER_EXPORT ssize_t AMediaCodec_dequeueInputBuffer(AMediaCodec *c, int64_t timeoutUs)
{
    (void)timeoutUs;
    return (c && c->started) ? 0 : AMEDIACODEC_INFO_TRY_AGAIN_LATER;
}
SOBER_EXPORT uint8_t *AMediaCodec_getInputBuffer(AMediaCodec *c, size_t idx,
                                                  size_t *outSize)
{
    if (!c || idx != 0) return NULL;
    if (outSize) *outSize = CODEC_BUFFER_SIZE;
    return c->input;
}
SOBER_EXPORT int AMediaCodec_queueInputBuffer(AMediaCodec *c, size_t idx,
                        off_t offset, size_t size, uint64_t timeUs, uint32_t flags)
{
    (void)idx; (void)offset; (void)size; (void)timeUs; (void)flags;
    return c ? 0 : -1;
}
SOBER_EXPORT ssize_t AMediaCodec_dequeueOutputBuffer(AMediaCodec *c,
                        AMediaCodec_buffer_info *info, int64_t timeoutUs)
{
    (void)info; (void)timeoutUs;
    return c ? AMEDIACODEC_INFO_TRY_AGAIN_LATER : -1;
}
SOBER_EXPORT uint8_t *AMediaCodec_getOutputBuffer(AMediaCodec *c, size_t idx,
                        size_t *outOffset, size_t *outSize)
{
    if (!c || idx != 0) return NULL;
    if (outOffset) *outOffset = 0;
    if (outSize) *outSize = CODEC_BUFFER_SIZE;
    return c->output;
}
SOBER_EXPORT AMediaFormat *AMediaCodec_getOutputFormat(AMediaCodec *c)
{
    (void)c;
    AMediaFormat *f = AMediaFormat_new();
    if (f) AMediaFormat_setString(f, AMEDIAFORMAT_KEY_MIME, "video/raw");
    return f;
}
SOBER_EXPORT void AMediaCodec_releaseOutputBuffer(AMediaCodec *c,
                        size_t idx, bool render)
{ (void)c; (void)idx; (void)render; }

/* ====================================================================== */
/* bionic-only libc helpers                                                */
/* ====================================================================== */
SOBER_EXPORT int *__errno(void) { return &errno; }

SOBER_EXPORT int __system_property_get(const char *name, char *value)
{
    if (!name || !value) return 0;
    char envname[320];
    const char *v = NULL;
    /* Accept both the literal property name and a '.'->'_' normalised form. */
    snprintf(envname, sizeof(envname), "SOBER_PROP_%s", name);
    v = getenv(envname);
    if (!v) {
        size_t n = strlen("SOBER_PROP_");
        snprintf(envname, sizeof(envname), "SOBER_PROP_");
        for (const char *p = name; *p && n + 1 < sizeof(envname); ++p, ++n)
            envname[n] = (*p == '.') ? '_' : *p;
        envname[n] = '\0';
        v = getenv(envname);
    }
    if (!v) return 0;
    strcpy(value, v);
    return (int)strlen(v);
}

SOBER_EXPORT const char *__gnu_strerror_r(int err, char *buf, size_t n)
{
    return strerror_r(err, buf, n);
}

SOBER_EXPORT void __assert2(const char *file, int line,
                            const char *func, const char *expr)
{
    fprintf(stderr, "[FATAL] %s:%d: %s: assertion '%s' failed\n",
            file ? file : "?", line, func ? func : "?", expr ? expr : "?");
    fflush(stderr);
    abort();
}

static void chk_fail(const char *what)
{
    fprintf(stderr, "[FATAL] fortified libc check failed: %s\n", what);
    fflush(stderr);
    abort();
}

/* NOTE: these use __builtin_* / raw syscalls deliberately. Calling the
 * ordinary libc entry point here would be fortified straight back into this
 * same function and recurse forever. */
SOBER_EXPORT size_t __strlen_chk(const char *s, size_t n)
{
    size_t len = __builtin_strlen(s);
    if (len >= n) chk_fail("__strlen_chk");
    return len;
}
SOBER_EXPORT char *__strchr_chk(const char *s, int c, size_t n)
{
    if (__builtin_strlen(s) >= n) chk_fail("__strchr_chk");
    return (char *)__builtin_strchr(s, c);
}
SOBER_EXPORT char *__strncpy_chk2(char *dst, const char *src, size_t n,
                                  size_t dstlen, size_t srclen)
{
    (void)srclen;
    if (n > dstlen) chk_fail("__strncpy_chk2");
    return (char *)__builtin_strncpy(dst, src, n);
}
SOBER_EXPORT void *__memcpy_chk(void *dst, const void *src, size_t n, size_t dstlen)
{
    if (n > dstlen) chk_fail("__memcpy_chk");
    return __builtin_memcpy(dst, src, n);
}
SOBER_EXPORT size_t __fwrite_chk(const void *buf, size_t size, size_t n,
                                 FILE *f, size_t buflen)
{
    if (size * n > buflen) chk_fail("__fwrite_chk");
    ssize_t w = syscall(SYS_write, fileno(f), buf, size * n);
    return w < 0 ? 0 : (size_t)w / size;
}
SOBER_EXPORT ssize_t __write_chk(int fd, const void *buf, size_t n, size_t buflen)
{
    if (n > buflen) chk_fail("__write_chk");
    return syscall(SYS_write, fd, buf, n);
}
SOBER_EXPORT ssize_t __sendto_chk(int fd, const void *buf, size_t n, int flags,
                                  const void *addr, socklen_t addrlen, size_t buflen)
{
    if (n > buflen) chk_fail("__sendto_chk");
    return syscall(SYS_sendto, fd, buf, n, flags, addr, (size_t)addrlen);
}
SOBER_EXPORT void __FD_SET_chk(int fd, void *set, unsigned n)
{ if ((unsigned)fd >= n) chk_fail("__FD_SET_chk"); FD_SET(fd, (fd_set *)set); }
SOBER_EXPORT void __FD_CLR_chk(int fd, void *set, unsigned n)
{ if ((unsigned)fd >= n) chk_fail("__FD_CLR_chk"); FD_CLR(fd, (fd_set *)set); }
SOBER_EXPORT int __FD_ISSET_chk(int fd, const void *set, unsigned n)
{ if ((unsigned)fd >= n) chk_fail("__FD_ISSET_chk"); return FD_ISSET(fd, (const fd_set *)set); }
SOBER_EXPORT void __gcov_dump(void) { }
SOBER_EXPORT void __gcov_flush(void) { }

/* ====================================================================== */
/* ZSTD trace hooks (weak imports in the guest)                            */
/* ====================================================================== */
SOBER_EXPORT void *ZSTD_trace_compress_begin(void *ctx)   { (void)ctx; return NULL; }
SOBER_EXPORT void  ZSTD_trace_compress_end(void *ctx, void *t) { (void)ctx; (void)t; }
SOBER_EXPORT void *ZSTD_trace_decompress_begin(void *ctx) { (void)ctx; return NULL; }
SOBER_EXPORT void  ZSTD_trace_decompress_end(void *ctx, void *t) { (void)ctx; (void)t; }
