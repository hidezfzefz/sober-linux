/*
 * ndk_stub.c - Minimal NATIVE shared objects carrying the Android SONAMEs
 * that libroblox.so lists in DT_NEEDED.
 *
 * These are not a translation layer: they contain our own Linux code (or are
 * intentionally empty). Their only job is to satisfy the ELF loader so the
 * guest can be mapped into the process; the actual API implementations live
 * in the host executable (src/compat/android_compat.c) and are resolved via
 * the global symbol scope.
 *
 * Built as liblog.so / libandroid.so / libmediandk.so / libOpenMAXAL.so /
 * libOpenSLES.so into build/ndk-stubs/. Owned by worker-3 (task #370).
 */
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

#ifdef STUB_LIBLOG

void __android_log_write(int prio, const char *tag, const char *text)
{
    fprintf(stderr, "[log/%d/%s] %s\n", prio, tag ? tag : "", text ? text : "");
}
int __android_log_print(int prio, const char *tag, const char *fmt, ...)
{
    char buf[4096];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    __android_log_write(prio, tag, buf);
    return n;
}
void __android_log_buf_write(int bufid, int prio, const char *tag, const char *text)
{ (void)bufid; __android_log_write(prio, tag, text); }
void __android_log_assert(const char *cond, const char *tag, const char *fmt, ...)
{
    char buf[4096];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    fprintf(stderr, "[FATAL/%s] %s: %s\n", tag ? tag : "", cond ? cond : "?", buf);
    fflush(stderr);
    abort();
}

#else

/* Deliberately empty: the guest resolves these APIs from the host's exported
 * shim in the global symbol scope. */
int sober_ndk_stub_marker;

#endif
