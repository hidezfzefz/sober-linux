/*
 * javavm_semantics.c - faithful JavaVM / JNIEnv thread semantics (task #453).
 *
 * Pure native C.  New file only; see javavm_semantics.h for the rationale.
 * The only guest memory written is the JavaVM pointer slot at RVA 0x7cdec78,
 * and only when SOBER_FAITHFUL_JAVAVM is set.
 */
#define _GNU_SOURCE
#include "javavm_semantics.h"
#include "scheduler_guest.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>

/* ---- W25 read-only VM-call ring (last 64 ops, async-safe dump) ---------- */
struct fj_vm_ring {
    int64_t   tid;
    uint32_t  op;        /* 1=Attach 2=Detach 3=GetEnv 4=GetJavaVM */
    int32_t   rc;
    uintptr_t env;       /* returned JNIEnv* (0 for Detach/GetJavaVM) */
    char      name[20];  /* thread name from AttachCurrentThread args */
};
static struct fj_vm_ring fj_ring[64];
static volatile unsigned fj_ring_pos;
static void fj_ring_note(uint32_t op, int32_t rc, uintptr_t env,
                         const char *name)
{
    unsigned i = __atomic_fetch_add(&fj_ring_pos, 1, __ATOMIC_RELAXED) & 63u;
    struct fj_vm_ring *e = &fj_ring[i];
    e->tid = (int64_t)syscall(SYS_gettid);
    e->op = op; e->rc = rc; e->env = env;
    unsigned n = 0;
    if (name) while (n < sizeof(e->name) - 1u && name[n]) {
        e->name[n] = name[n]; ++n;
    }
    e->name[n] = 0;
}
long sober_javavm_ring_dump_async(int fd)
{
    char buf[8192], *p = buf, *end = buf + sizeof(buf) - 1;
    static const char hx[] = "0123456789abcdef";
    #define WP(s) do { const char *_s=(s); while (*_s && p<end) *p++=*_s++; } while(0)
    #define WH(v) do { uintptr_t _v=(uintptr_t)(v); \
        if (p+2<end){*p++='0';*p++='x';} \
        for(int _i=(int)(sizeof(uintptr_t)*8)-4;_i>=0&&p<end;_i-=4) \
            *p++=hx[(_v>>_i)&0xfu]; } while(0)
    WP("[W25_VM_RING] tid,name,op,rc,env:\n");
    unsigned pos = __atomic_load_n(&fj_ring_pos, __ATOMIC_RELAXED);
    unsigned start = pos > 64u ? pos - 64u : 0u;
    for (unsigned i = start; i < pos; ++i) {
        struct fj_vm_ring *e = &fj_ring[i & 63u];
        WP("["); WH((uintptr_t)(i - start)); WP("] tid=");
        /* tid as hex is fine; report op numerically */
        WH((uintptr_t)e->tid); WP(" op="); WH(e->op);
        WP(" rc="); WH((uintptr_t)(int64_t)e->rc);
        WP(" env="); WH(e->env); WP(" name="); WP(e->name); WP("\n");
    }
    #undef WP
    #undef WH
    ssize_t u = write(fd, buf, (size_t)(p - buf));
    return (long)(u > 0 ? u : 0);
}

/* ---- JNI constants (ABI values from jni.h) ----------------------------- */
#define FJ_JNI_OK        (0)
#define FJ_JNI_ERR       (-1)
#define FJ_JNI_EDETACHED (-2)
#define FJ_JNI_EVERSION  (-3)
#define FJ_JNI_EINVAL    (-6)

/* Local jint/jboolean are ABI-identical to jni.h. */
typedef int32_t  fj_jint;
typedef uint8_t  fj_jboolean;

/* Guest BSS slot that JNI_OnLoad (0x227af6e -> 0x227b8fb -> 0x227b92d) fills
 * with its JavaVM* argument.  Skipping JNI_OnLoad leaves it NULL. */
#define FJ_VM_GLOBAL_RVA 0x7cdec78u

/* JNINativeInterface_ has 4 reserved pointers then 231 function pointers. */
#define FJ_ENV_FUNCS   231u
#define FJ_ENV_SLOTS   (4u + FJ_ENV_FUNCS)
/* Verified guest/JNIEnv slots we override (index into fn[]). */
#define FJ_SLOT_THROW             9u
#define FJ_SLOT_THROWNEW         10u
#define FJ_SLOT_EXCEPTIONOCCURRED 11u
#define FJ_SLOT_EXCEPTIONCLEAR   13u
#define FJ_SLOT_GETJAVAVM       215u
#define FJ_SLOT_EXCEPTIONCHECK  224u   /* vtable byte offset 0x720 */

#define FJ_ENV_MAGIC 0x464A45564E5631ULL  /* "FJENV1" */

/* ---- object shapes ----------------------------------------------------- */
/* LAYOUT COMPATIBLE with native_jni.c's SREnvObj for its first two fields:
 * native_jni's stubs read functions from +0 and SoberJni* owner from +8. */
typedef struct FaithfulThread FaithfulThread;

typedef struct {
    void          *functions;   /* +0x00: FJ env table (JNINativeInterface_) */
    void          *owner;       /* +0x08: real SoberJni* (bridge ABI) */
    uint64_t       magic;       /* +0x10: FJ_ENV_MAGIC */
    FaithfulThread *thread;     /* +0x18: per-thread state */
    void          *vm;          /* +0x20: our faithful JavaVM */
} FaithfulEnv;

typedef struct {
    void *functions;            /* +0x00: JNIInvokeInterface_ vtable */
    void *owner;                /* +0x08: SoberJni* (informational) */
} FaithfulVM;

/* JNIInvokeInterface_ order: 3 reserved slots, then 5 functions.  The guest
 * calls them at vtable offsets +0x18/+0x20/+0x28/+0x30/+0x38. */
typedef struct {
    void *reserved[3];
    void *fn[5];
} FJInvokeTable;

struct FaithfulThread {
    FaithfulEnv    env;
    pthread_t      self;
    char           name[64];    /* JavaVMAttachArgs.name, bounded copy */
    int            daemon;
    int            attached;
    int            pending_exception;
    void          *pending_throwable;
    FaithfulThread *next;         /* global list for teardown/inspection */
};

/* ---- module state ------------------------------------------------------ */
static int             g_installed;
static SoberJni       *g_owner;
static FaithfulVM      g_vm;
static FJInvokeTable   g_invoke;
static void           *g_env_table[FJ_ENV_SLOTS];
static FaithfulThread *g_threads;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_key_t   g_tls;
static pthread_once_t  g_tls_once = PTHREAD_ONCE_INIT;

static long g_attach_calls;
static long g_idempotent_attaches;
static long g_detach_calls;
static long g_detach_rejected;
static long g_getenv_calls;
static long g_getenv_edetached;
static long g_exception_checks;
static long g_daemon_attaches;

/* ---- forward declarations for the vtable ------------------------------- */
static fj_jint fj_DestroyJavaVM(void *vm);
static fj_jint fj_AttachCurrentThread(void *vm, void **env_out, void *args);
static fj_jint fj_DetachCurrentThread(void *vm);
static fj_jint fj_GetEnv(void *vm, void **env_out, fj_jint version);
static fj_jint fj_AttachCurrentThreadAsDaemon(void *vm, void **env_out,
                                              void *args);

static void    fj_ExceptionClear(void *env);
static void   *fj_ExceptionOccurred(void *env);
static fj_jboolean fj_ExceptionCheck(void *env);
static fj_jint fj_Throw(void *env, void *thr);
static fj_jint fj_ThrowNew(void *env, void *clazz, const char *msg);
static fj_jint fj_GetJavaVM(void *env, void ***vm_out);

/* ---- TLS --------------------------------------------------------------- */
static void fj_tls_destructor(void *p);   /* forward ref for key_create */

static void fj_make_tls(void)
{
    (void)pthread_key_create(&g_tls, fj_tls_destructor);
}

static void fj_tls_destructor(void *p)
{
    FaithfulThread *t = (FaithfulThread *)p;
    if (!t) return;
    /* Thread is exiting without an explicit DetachCurrentThread (the JVM
     * auto-detaches in this case).  Unlink and release; do not call
     * pthread_setspecific from a destructor. */
    pthread_mutex_lock(&g_lock);
    for (FaithfulThread **pp = &g_threads; *pp; pp = &(*pp)->next) {
        if (*pp == t) { *pp = t->next; break; }
    }
    t->attached = 0;
    g_detach_calls++;
    pthread_mutex_unlock(&g_lock);
    free(t);
}

static FaithfulThread *fj_current(void)
{
    pthread_once(&g_tls_once, fj_make_tls);
    return (FaithfulThread *)pthread_getspecific(g_tls);
}

/* ---- internal attach/detach -------------------------------------------- */
/* Copy an untrusted JavaVMAttachArgs.name into a CALLER-OWNED buffer.  A
 * per-call buffer (rather than a global scratch) keeps concurrent attaches
 * on different guest threads race-free. */
static void fj_copy_name(char *dst, size_t cap, const void *name)
{
    if (!dst || !cap) return;
    dst[0] = '\0';
    if (!name) return;
    uintptr_t v = (uintptr_t)name;
    /* Guest strings live in the mapped memfd (~0x7f...).  Reject junk values
     * such as 0xABCDEF0123456789 before touching them. */
    if (v < 0x10000u || v >= 0x0000800000000000ULL) return;
    const char *s = (const char *)name;
    size_t i = 0;
    for (; i + 1 < cap && s[i]; ++i)
        dst[i] = s[i];
    dst[i] = '\0';
}

/* args == JavaVMAttachArgs* { fj_jint version; char *name; jobject group; }
 * The group handle is a Java object we do not emulate; ignore it. */
static fj_jint fj_do_attach(void *vm, void **env_out, void *args, int daemon)
{
    (void)vm;
    if (env_out) *env_out = NULL;
    if (!g_installed || !g_owner) return FJ_JNI_ERR;

    char name[64] = {0};
    if (args) {
        /* name is the 2nd field of JavaVMAttachArgs. */
        const void *raw = *(const void * const *)((unsigned char *)args + 8);
        fj_copy_name(name, sizeof(name), raw);
    }

    pthread_mutex_lock(&g_lock);
    FaithfulThread *t = fj_current();
    if (t && t->attached) {
        if (env_out) *env_out = &t->env;
        g_idempotent_attaches++;
        g_attach_calls++;
        pthread_mutex_unlock(&g_lock);
        return FJ_JNI_OK;
    }
    t = (FaithfulThread *)calloc(1, sizeof(*t));
    if (!t) { pthread_mutex_unlock(&g_lock); return FJ_JNI_ERR; }
    t->env.functions = g_env_table;
    t->env.owner     = g_owner;
    t->env.magic     = FJ_ENV_MAGIC;
    t->env.thread    = t;
    t->env.vm        = &g_vm;
    t->self          = pthread_self();
    t->daemon        = daemon;
    t->attached      = 1;
    if (name[0]) snprintf(t->name, sizeof(t->name), "%s", name);
    else         t->name[0] = '\0';
    t->next = g_threads;
    g_threads = t;
    pthread_setspecific(g_tls, t);
    if (env_out) *env_out = &t->env;
    g_attach_calls++;
    if (daemon) g_daemon_attaches++;
    fj_ring_note(1u, FJ_JNI_OK, (uintptr_t)&t->env, name);
    pthread_mutex_unlock(&g_lock);

    fprintf(stderr,
            "[faithful-javavm] AttachCurrentThread%s name=\"%s\" -> env=%p "
            "(attached=%ld idempotent=%ld)\n",
            daemon ? "AsDaemon" : "", t->name, (void *)&t->env,
            g_attach_calls, g_idempotent_attaches);

    /* This is the genuine attach site on the worker thread: the AVD chain is
     * thunk 0x2a0daa0 -> enter-hook (RA 0x2a0daf0) -> task call 0x2a0daf7 ->
     * callback 0x2a18270 (RA 0x2a0dafa) -> FunctionMarshaller attach ->
     * settings 0x235a58c -> latch 0x2eb7520.  The harness has no guest-side
     * enter hook, so issue the single honest guest nesting prime HERE, on the
     * attaching thread, immediately before its task call.  The prime module
     * self-gates on SOBER_SCHEDULER_GUEST (already in the decision-013 seeded
     * state chain), refuses the harness main thread, and executes only guest
     * RVA 0x2a132c0(1); it fabricates no object and writes no guest byte. */
    if (name[0] && strcmp(name, SOBER_SG_WORKER_NAME) == 0)
        sober_scheduler_guest_prime_worker();
    return FJ_JNI_OK;
}

static fj_jint fj_do_detach(void *vm)
{
    (void)vm;
    if (!g_installed) return FJ_JNI_ERR;
    pthread_mutex_lock(&g_lock);
    FaithfulThread *t = fj_current();
    if (!t || !t->attached) {
        g_detach_rejected++;
        pthread_mutex_unlock(&g_lock);
        return FJ_JNI_EDETACHED;
    }
    for (FaithfulThread **pp = &g_threads; *pp; pp = &(*pp)->next) {
        if (*pp == t) { *pp = t->next; break; }
    }
    t->attached = 0;
    (void)pthread_setspecific(g_tls, NULL);
    g_detach_calls++;
    fj_ring_note(2u, FJ_JNI_OK, 0u, t->name);
    pthread_mutex_unlock(&g_lock);
    fprintf(stderr,
            "[faithful-javavm] DetachCurrentThread name=\"%s\" (detach=%ld)\n",
            t->name, g_detach_calls);
    free(t);
    return FJ_JNI_OK;
}

/* ---- JNIInvokeInterface_ implementations ------------------------------- */
static fj_jint fj_DestroyJavaVM(void *vm)
{
    (void)vm;
    /* Real DestroyJavaVM waits for all non-daemon threads.  The harness never
     * calls it during a diagnostic run; stay harmless and never fabricate a
     * callback here. */
    fprintf(stderr, "[faithful-javavm] DestroyJavaVM (no-op)\n");
    return FJ_JNI_OK;
}

static fj_jint fj_AttachCurrentThread(void *vm, void **env_out, void *args)
{
    return fj_do_attach(vm, env_out, args, 0);
}

static fj_jint fj_AttachCurrentThreadAsDaemon(void *vm, void **env_out,
                                              void *args)
{
    return fj_do_attach(vm, env_out, args, 1);
}

static fj_jint fj_DetachCurrentThread(void *vm)
{
    return fj_do_detach(vm);
}

static fj_jint fj_GetEnv(void *vm, void **env_out, fj_jint version)
{
    (void)vm;
    if (!env_out) return FJ_JNI_EINVAL;
    *env_out = NULL;
    /* Accept JNI 1.1..1.8 and "unspecified" (0); the guest passes 0x10006. */
    if (version != 0 && ((uint32_t)version >> 16) != 1u) return FJ_JNI_EVERSION;
    g_getenv_calls++;
    FaithfulThread *t = fj_current();
    if (!g_installed || !t || !t->attached) {
        g_getenv_edetached++;
        if (g_getenv_edetached <= 6)
            fprintf(stderr,
                    "[faithful-javavm] GetEnv -> JNI_EDETACHED (-2) "
                    "(calls=%ld edetached=%ld)\n",
                    g_getenv_calls, g_getenv_edetached);
        return FJ_JNI_EDETACHED;
    }
    *env_out = &t->env;
    fj_ring_note(3u, FJ_JNI_OK, (uintptr_t)&t->env, t->name);
    return FJ_JNI_OK;
}

/* ---- JNIEnv exception-state overrides ---------------------------------- */
static FaithfulThread *fj_env_thread(void *env)
{
    if (!env) return NULL;
    FaithfulEnv *e = (FaithfulEnv *)env;
    if (e->magic != FJ_ENV_MAGIC) return NULL;   /* not one of our envs */
    return e->thread;
}

static void *fj_ExceptionOccurred(void *env)
{
    FaithfulThread *t = fj_env_thread(env);
    return (t && t->pending_exception) ? t->pending_throwable : NULL;
}

static fj_jboolean fj_ExceptionCheck(void *env)
{
    FaithfulThread *t = fj_env_thread(env);
    __atomic_add_fetch(&g_exception_checks, 1, __ATOMIC_RELAXED);
    return (t && t->pending_exception) ? 1u : 0u;
}

static void fj_ExceptionClear(void *env)
{
    FaithfulThread *t = fj_env_thread(env);
    if (t) { t->pending_exception = 0; t->pending_throwable = NULL; }
}

static fj_jint fj_Throw(void *env, void *thr)
{
    FaithfulThread *t = fj_env_thread(env);
    if (t) { t->pending_exception = 1; t->pending_throwable = thr; }
    return FJ_JNI_OK;
}

static fj_jint fj_ThrowNew(void *env, void *clazz, const char *msg)
{
    (void)clazz;
    FaithfulThread *t = fj_env_thread(env);
    if (t) {
        t->pending_exception = 1;
        /* No synthetic Throwable is fabricated: keep the handle NULL.  The
         * guest here never calls ExceptionOccurred while pending is set. */
        t->pending_throwable = NULL;
    }
    (void)msg;
    return FJ_JNI_OK;
}

static fj_jint fj_GetJavaVM(void *env, void ***vm_out)
{
    (void)env;
    if (!vm_out) return FJ_JNI_EINVAL;
    *vm_out = (void **)&g_vm;
    fj_ring_note(4u, FJ_JNI_OK, (uintptr_t)env, "");
    return FJ_JNI_OK;
}

/* ---- table construction ------------------------------------------------ */
static void fj_build_tables(SoberJni *j)
{
    void *bridge_env = sober_jni_env(j);
    if (!bridge_env) return;
    /* SREnvObj.functions == SRNativeInterface*; copy its 4 reserved + 231
     * function pointers verbatim, then override the honest JNI semantics. */
    void **src = *(void ***)bridge_env;
    if (!src) return;
    memcpy(g_env_table, src, sizeof(g_env_table));

    g_env_table[4 + FJ_SLOT_THROW]             = (void *)(uintptr_t)&fj_Throw;
    g_env_table[4 + FJ_SLOT_THROWNEW]          = (void *)(uintptr_t)&fj_ThrowNew;
    g_env_table[4 + FJ_SLOT_EXCEPTIONOCCURRED] = (void *)(uintptr_t)&fj_ExceptionOccurred;
    g_env_table[4 + FJ_SLOT_EXCEPTIONCLEAR]    = (void *)(uintptr_t)&fj_ExceptionClear;
    g_env_table[4 + FJ_SLOT_GETJAVAVM]         = (void *)(uintptr_t)&fj_GetJavaVM;
    g_env_table[4 + FJ_SLOT_EXCEPTIONCHECK]    = (void *)(uintptr_t)&fj_ExceptionCheck;

    memset(&g_invoke, 0, sizeof(g_invoke));
    g_invoke.fn[0] = (void *)(uintptr_t)&fj_DestroyJavaVM;
    g_invoke.fn[1] = (void *)(uintptr_t)&fj_AttachCurrentThread;
    g_invoke.fn[2] = (void *)(uintptr_t)&fj_DetachCurrentThread;
    g_invoke.fn[3] = (void *)(uintptr_t)&fj_GetEnv;
    g_invoke.fn[4] = (void *)(uintptr_t)&fj_AttachCurrentThreadAsDaemon;

    memset(&g_vm, 0, sizeof(g_vm));
    g_vm.functions = &g_invoke;
    g_vm.owner = j;
}

/* ---- public API -------------------------------------------------------- */
int sober_javavm_install(SoberJni *j, void *guest_base)
{
    if (!getenv(SOBER_FAITHFUL_JAVAVM_ENV)) {
        fprintf(stderr,
                "[faithful-javavm] disabled (%s unset); JavaVM semantics "
                "unchanged\n", SOBER_FAITHFUL_JAVAVM_ENV);
        return SOBER_JNI_OK;              /* default OFF, no-op */
    }
    if (!j || !guest_base) return SOBER_JNI_ERR;

    pthread_mutex_lock(&g_lock);
    if (g_installed) { pthread_mutex_unlock(&g_lock); return SOBER_JNI_OK; }
    fj_build_tables(j);
    if (!g_env_table[4]) { pthread_mutex_unlock(&g_lock); return SOBER_JNI_ERR; }

    void **slot = (void **)((unsigned char *)guest_base + FJ_VM_GLOBAL_RVA);
    void *old = *slot;
    *slot = &g_vm;                       /* the only guest bytes we write */
    g_owner = j;
    g_installed = 1;
    pthread_mutex_unlock(&g_lock);

    fprintf(stderr,
            "[faithful-javavm] INSTALLED: guest 0x%lx %p -> %p; "
            "invoke=%p Attach=%p Detach=%p GetEnv=%p ExceptionCheck=%p "
            "(env tables copied from bridge)\n",
            (unsigned long)FJ_VM_GLOBAL_RVA, old, (void *)&g_vm,
            (void *)&g_invoke, (void *)(uintptr_t)&fj_AttachCurrentThread,
            (void *)(uintptr_t)&fj_DetachCurrentThread,
            (void *)(uintptr_t)&fj_GetEnv,
            (void *)(uintptr_t)&fj_ExceptionCheck);

    /* The thread that called JNI_OnLoad-equivalent setup is attached in a
     * real JVM.  Attach this (harness/main) thread under that model; guest
     * worker threads stay detached and take the genuine attach path. */
    void *main_env = NULL;
    (void)fj_AttachCurrentThread(&g_vm, &main_env, NULL);
    return SOBER_JNI_OK;
}

void *sober_javavm_vm(SoberJni *j)
{
    (void)j;
    return g_installed ? (void *)&g_vm : NULL;
}

void *sober_javavm_current_env(SoberJni *j)
{
    (void)j;
    if (!g_installed) return NULL;
    FaithfulThread *t = fj_current();
    return (t && t->attached) ? (void *)&t->env : NULL;
}

int sober_javavm_is_active(void)
{
    return g_installed;
}

void sober_javavm_stats(char *buf, size_t cap)
{
    if (!buf || !cap) return;
    snprintf(buf, cap,
             "active=%d attach=%ld idempotent=%ld detach=%ld "
             "detach_rejected=%ld getenv=%ld edetached=%ld "
             "exc_check=%ld daemon=%ld",
             g_installed, g_attach_calls, g_idempotent_attaches,
             g_detach_calls, g_detach_rejected, g_getenv_calls,
             g_getenv_edetached, (long)__atomic_load_n(&g_exception_checks,
                                                        __ATOMIC_RELAXED),
             g_daemon_attaches);
}
