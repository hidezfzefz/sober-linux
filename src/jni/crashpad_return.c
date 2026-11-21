/*
 * crashpad_return.c - opt-in clean-return route for crashpad HandlerMain
 *                     (worker-2, team task #509).
 *
 * See crashpad_return.h for the full fault chain and the validated route.
 * Every state change is produced by genuine shipped guest code or by host-side
 * IPC/stdio plumbing.  NO guest byte write, NO fake vtable/callback, NO host
 * write to guest state, NO decision-013 marker.
 *
 * The stdio wrappers are deliberately compiled into this object with default
 * visibility: -rdynamic in the root CMakeLists exports them, so the compat
 * shim's native_lookup("fputs"/"fflush") resolves to them.  When the module is
 * disabled they are pure pass-through to glibc, so a default run is unchanged.
 */
#define _GNU_SOURCE
#include "crashpad_return.h"

#include <dlfcn.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "jni_arrays.h"
#include "native_jni.h"

#define DEFAULT_ARGV                                                       \
    "crashpad-handler,"                                                    \
    "--database=/tmp/sober-crashpad-db,"                                   \
    "--initial-client-fd=@SOCK@,"                                          \
    "--no-periodic-tasks,"                                                 \
    "--no-rate-limit"

/* ------------------------------------------------------------------------ */
/* state / env helpers                                                       */
/* ------------------------------------------------------------------------ */
static volatile void   *g_guest_base;
static volatile unsigned g_latch;
static volatile int     g_active;        /* route actually executed          */
static volatile int     g_bridge_on;     /* Bionic __sF bridge active         */

static int env_flag(const char *name)
{
    const char *v = getenv(name);
    if (!v || !*v) return 0;
    return !strcasecmp(v, "1") || !strcasecmp(v, "on") ||
           !strcasecmp(v, "yes") || !strcasecmp(v, "true");
}

/* Default-ON sub-knobs: absent variable means enabled. */
static int env_flag_default_on(const char *name)
{
    const char *v = getenv(name);
    if (!v || !*v) return 1;
    return !(!strcasecmp(v, "0") || !strcasecmp(v, "off") ||
             !strcasecmp(v, "no") || !strcasecmp(v, "false"));
}

int sober_crashpad_return_enabled(void)
{
    return env_flag(SOBER_CRASHPAD_RETURN_ENV);
}

int sober_crashpad_return_should_withhold(void)
{
    return g_active || g_bridge_on;
}

unsigned sober_crashpad_return_latch(void)
{
    return g_latch;
}

const char *sober_crashpad_return_env_vars(void)
{
    return SOBER_CRASHPAD_RETURN_ENV "," SOBER_CRASHPAD_RETURN_ARGV_ENV ","
           SOBER_CRASHPAD_RETURN_PASSCRED_ENV "," SOBER_CRASHPAD_RETURN_STDIO_ENV;
}

/* Task #525 — read-only verdict.  The shipped DEX has exactly one Java caller
 * of nativeRunCrashpadHandler: CrashpadHandler.main(String[]), the entry point
 * of the crash-handler SUBPROCESS.  Boot (rg.a1.h0 -> nativeInitCrashpad
 * 0x232d6b8) only registers that class; it never runs it and cannot write the
 * latch.  So there is no opt-in-free Java boot path to 0x2eca720 / 0x2ec9fae.
 * This function intentionally returns 0 and must never be used to weaken the
 * seeded_state withhold chain. */
int sober_crashpad_return_java_boot_reachable(void)
{
    return 0;
}

/* Task #525 — exact prerequisites for the (always WITHHELD) clean-return
 * route.  Static text; it reads no state and performs no guest call. */
const char *sober_crashpad_return_prereqs(void)
{
    return "SOBER_APP_SHELL=1,SOBER_RENDER=1,"
           SOBER_CRASHPAD_RETURN_ENV "=1," SOBER_FAITHFUL_JNI_ARRAYS_ENV "=1"
           " (optional " SOBER_CRASHPAD_RETURN_ARGV_ENV ","
           SOBER_CRASHPAD_RETURN_PASSCRED_ENV ","
           SOBER_CRASHPAD_RETURN_STDIO_ENV ")";
}

/* ------------------------------------------------------------------------ */
/* Host-side Bionic __sF stdio bridge (fputs + fflush)                       */
/* ------------------------------------------------------------------------ */
static int (*cr_real_fputs)(const char *, FILE *);
static int (*cr_real_fflush)(FILE *);
static unsigned char *cr_sf_base;

static FILE *cr_map_stream(FILE *stream)
{
    if (!g_bridge_on) return stream;          /* disabled: exact pass-through */
    if (!cr_sf_base)
        cr_sf_base = (unsigned char *)dlsym(RTLD_DEFAULT, "__sF");
    unsigned char *p = (unsigned char *)stream;
    if (cr_sf_base && p >= cr_sf_base && p < cr_sf_base + 0x200u) {
        size_t off = (size_t)(p - cr_sf_base);
        /* Bionic FILE __sF[3]: stdin +0x000, stdout +0x098, stderr +0x130. */
        if (off < 0x098u) return stdin;
        if (off < 0x130u) return stdout;
        return stderr;
    }
    return stream;
}

/* Exported on purpose: see file header.  RTLD_NEXT resolves the real glibc
 * entry (never this wrapper), so there is no recursion. */
__attribute__((visibility("default")))
int fputs(const char *s, FILE *stream)
{
    if (!cr_real_fputs)
        cr_real_fputs = (int (*)(const char *, FILE *))
            dlvsym(RTLD_NEXT, "fputs", "GLIBC_2.2.5");
    if (!cr_real_fputs) return -1;
    return cr_real_fputs(s, cr_map_stream(stream));
}

__attribute__((visibility("default")))
int fflush(FILE *stream)
{
    if (!cr_real_fflush)
        cr_real_fflush = (int (*)(FILE *))
            dlvsym(RTLD_NEXT, "fflush", "GLIBC_2.2.5");
    if (!cr_real_fflush) return 0;
    return cr_real_fflush(cr_map_stream(stream));
}

/* ------------------------------------------------------------------------ */
/* Real crashpad server peer                                                 */
/* ------------------------------------------------------------------------ */
static int cr_peer_fd;                       /* single opt-in route per run   */

static void *cr_server_peer(void *arg)
{
    int fd = *(int *)arg;
    struct timespec ts = {0, 50000000};      /* 50 ms: let the guest arrive    */

    nanosleep(&ts, NULL);

    /* ExceptionHandlerProtocol::ClientToServerMessage, 32 bytes in this engine.
     * +0 int32  version = 1
     * +4 uint32 type    = 0 (kTypeCheckCredentials)
     * +8 uint64 requesting_thread_stack_address = 0
     * +16/+24 ClientInformation addresses = 0
     * This is a genuine protocol message, not a fabricated callback/vtable. */
    unsigned char msg[32];
    memset(msg, 0, sizeof(msg));
    msg[0] = 1;                              /* version, little-endian        */

    ssize_t w = send(fd, msg, sizeof(msg), MSG_NOSIGNAL);
    fprintf(stderr, "[crashpad-return] server peer sent %zd-byte "
                    "CheckCredentials message (fd=%d)\n", w, fd);

    /* Keep the peer alive briefly, then close it.  The handler's epoll loop
     * observes EPOLLERR, drops the client and returns 0 (validated). */
    ts.tv_nsec = 200000000;                  /* 200 ms */
    nanosleep(&ts, NULL);
    close(fd);
    return NULL;
}

/* ------------------------------------------------------------------------ */
/* install                                                                   */
/* ------------------------------------------------------------------------ */
int sober_crashpad_return_install(void *guest_base, void *jni_env,
                                  void *jclass, void *jni)
{
    unsigned char *b = (unsigned char *)guest_base;
    static char channel[] = "LIVE";
    char buf[1024];
    const char *args[16];
    const char *spec;
    int n = 0, sp[2] = { -1, -1 };
    int passcred;

    if (!sober_crashpad_return_enabled()) {
        fprintf(stderr, "[crashpad-return] disabled "
                        "(SOBER_CRASHPAD_RETURN unset/0)\n");
        return SOBER_CRASHPAD_RETURN_DISABLED;
    }
    if (!guest_base) return SOBER_CRASHPAD_RETURN_ERR;

    /* Task #525: fail closed unless the faithful JNI array bridge is active.
     * The guest walks the argv String[] through JNIEnv fn[167]/fn[169]; with
     * the default stubs (GetArrayLength -> 0, GetObjectArrayElement -> NULL)
     * it sees an empty argv and crashpad takes its fatal "Usage" path instead
     * of a clean return.  Requiring the bridge makes SOBER_FAITHFUL_JNI_ARRAYS
     * an EXPLICIT prerequisite and stops a missing opt-in from producing a
     * misleading crash.  This does not touch guest state and never weakens a
     * gate: the route stays a host-initiated seed and forces WITHHELD. */
    if (!sober_jni_arrays_is_active()) {
        fprintf(stderr, "[crashpad-return] refusing: faithful JNI arrays "
                        "inactive (set %s=1); clean return needs a real "
                        "String[] walk\n", SOBER_FAITHFUL_JNI_ARRAYS_ENV);
        return SOBER_CRASHPAD_RETURN_ERR;
    }

    g_guest_base = guest_base;
    /* Opt-in stdio bridge: must be live before guest crashpad logging runs. */
    if (env_flag_default_on(SOBER_CRASHPAD_RETURN_STDIO_ENV))
        g_bridge_on = 1;

    /* Gate at 0x2ec9f2e: channel std::string at 0x72a3710 must be non-empty.
     * Seed it with the guest's OWN setter (same as flags-latch-default). */
    ((void (*)(void *, const char *))((unsigned char *)guest_base +
                                      SOBER_CRASHPAD_RETURN_RVA_SETTER))
        (b + SOBER_CRASHPAD_RETURN_RVA_CHANNEL, channel);

    spec = getenv(SOBER_CRASHPAD_RETURN_ARGV_ENV);
    if (!spec || !*spec) spec = DEFAULT_ARGV;
    if (!strstr(spec, "@SOCK@")) {
        /* Without a real IPC fd the handler always dies; fall back to the
         * validated default rather than replaying a known-crashing argv. */
        fprintf(stderr, "[crashpad-return] argv has no @SOCK@; using default\n");
        spec = DEFAULT_ARGV;
    }
    strncpy(buf, spec, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    char *p = buf;
    while (n < (int)(sizeof(args) / sizeof(args[0])) - 1) {
        args[n++] = p;
        char *comma = strchr(p, ',');
        if (!comma) break;
        *comma = '\0';
        p = comma + 1;
    }

    passcred = env_flag_default_on(SOBER_CRASHPAD_RETURN_PASSCRED_ENV);
    if (socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sp) == 0) {
        int one = 1;
        if (passcred) {
            /* crashpad::UnixCredentialSocket::RecvMsg requires SCM_CREDENTIALS;
             * SO_PASSCRED on the receiving socket makes the kernel attach it. */
            setsockopt(sp[0], SOL_SOCKET, SO_PASSCRED, &one, sizeof(one));
            setsockopt(sp[1], SOL_SOCKET, SO_PASSCRED, &one, sizeof(one));
        }
        for (int i = 0; i < n; ++i) {
            const char *at = strstr(args[i], "@SOCK@");
            if (at) {
                static char sockarg[64];
                snprintf(sockarg, sizeof(sockarg), "%.*s%d%s",
                         (int)(at - args[i]), args[i], sp[0],
                         at + strlen("@SOCK@"));
                args[i] = sockarg;
            }
        }
        cr_peer_fd = sp[1];
        pthread_t th;
        if (pthread_create(&th, NULL, cr_server_peer, &cr_peer_fd) == 0)
            pthread_detach(th);
        else
            fprintf(stderr, "[crashpad-return] server peer thread failed\n");
    } else {
        fprintf(stderr, "[crashpad-return] socketpair failed; guest IPC will "
                        "use the literal fd from argv\n");
    }

    /* Genuine JNI String[] via the faithful bridge (no fabricated callbacks). */
    void *arr = sober_jni_new_string_array((SoberJni *)jni, args, n);
    if (arr && jni_env && jclass)
        ((void (*)(void *, void *, void *))
            ((unsigned char *)guest_base + SOBER_CRASHPAD_RETURN_RVA_JNI))
            (jni_env, jclass, arr);
    else
        fprintf(stderr, "[crashpad-return] no JNI array/env/class; guest "
                        "not invoked\n");

    g_latch = (unsigned)*(volatile unsigned char *)(b +
                              SOBER_CRASHPAD_RETURN_RVA_LATCH);
    g_active = 1;
    fprintf(stderr, "[crashpad-return] 0x2ec9eed returned, argv=%d "
                    "latch@0x7cdd149=%u withhold=%d\n",
            n, g_latch, sober_crashpad_return_should_withhold());
    return g_latch ? SOBER_CRASHPAD_RETURN_OK : SOBER_CRASHPAD_RETURN_ZERO;
}
