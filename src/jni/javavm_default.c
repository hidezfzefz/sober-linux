/*
 * javavm_default.c - DEFAULT-ON faithful JavaVM install (substrate T1-3).
 *
 * Pure native C.  New file only; see javavm_default.h for the rationale and the
 * lead wiring contract.  The only guest bytes this module writes are the
 * single JavaVM* slot at guest RVA 0x7cdec78 (the cached-ID seed is delegated
 * to the existing sober_jni_seed_guest_jni_state() bridge API).
 */
#define _GNU_SOURCE
#include "javavm_default.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "javavm_semantics.h"   /* sober_javavm_is_active / _vm (adopt opt-in VM) */

/* ---- JNI constants (ABI values from jni.h) ----------------------------- */
#define JD_JNI_OK        (0)
#define JD_JNI_ERR       (-1)
#define JD_JNI_EDETACHED (-2)
#define JD_JNI_EVERSION  (-3)
#define JD_JNI_EINVAL    (-6)

typedef int32_t  jd_jint;

/* JNINativeInterface_ = 4 reserved pointers + 231 function pointers.  We copy
 * the bridge's fully built table verbatim so every entry is a real function. */
#define JD_ENV_FUNCS  231u
#define JD_ENV_SLOTS  (4u + JD_ENV_FUNCS)
#define JD_SLOT_GETJAVAVM 215u              /* fn[215] -> table[219] (vtable +0x6d8) */

/* JNIEnv::NewStringUTF is fn[163] -> table[167] -> byte offset 0x538, the
 * slot the default-fault caller at 0x2eb4b65 dereferences. */
#define JD_SLOT_NEWSTRINGUTF 163u

/* ---- object shapes (layout-compatible with native_jni.c SREnvObj/SRVmObj) */
typedef struct {
    void *functions;   /* +0x00: JD native interface table */
    void *owner;       /* +0x08: real SoberJni* (bridge ABI) */
} JdEnvObj;

typedef struct {
    void *functions;   /* +0x00: JdInvokeTable */
    void *owner;       /* +0x08: SoberJni* (informational) */
} JdVmObj;

/* JNIInvokeInterface_ order: 3 reserved slots then 5 functions.  The guest
 * calls them at vtable offsets +0x18/+0x20/+0x28/+0x30/+0x38. */
typedef struct {
    void *reserved[3];
    void *fn[5];
} JdInvokeTable;

/* ---- module state ------------------------------------------------------ */
static int             g_installed;
static SoberJni       *g_owner;
static void           *g_guest_base;
static void           *g_vm_ptr;     /* VM actually in the global (ours/adopted) */
static void           *g_env_ptr;    /* env GetEnv hands out (ours/adopted) */
static JdVmObj         g_vm;
static JdEnvObj        g_env;
static JdInvokeTable   g_invoke;
static void           *g_env_table[JD_ENV_SLOTS];

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

static long g_getenv_calls;
static long g_attach_calls;
static long g_detach_calls;
static long g_verify_calls;
static long g_verify_ok;

/* ---- forward declarations ---------------------------------------------- */
static jd_jint jd_DestroyJavaVM(void *vm);
static jd_jint jd_AttachCurrentThread(void *vm, void **env_out, void *args);
static jd_jint jd_DetachCurrentThread(void *vm);
static jd_jint jd_GetEnv(void *vm, void **env_out, jd_jint version);
static jd_jint jd_AttachCurrentThreadAsDaemon(void *vm, void **env_out,
                                              void *args);
static jd_jint jd_GetJavaVM(void *env, void ***vm_out);

/* ---- kill switch ------------------------------------------------------- */
const char *sober_javavm_default_env_vars(void)
{
    return SOBER_JAVAVM_DEFAULT_ENV;
}

int sober_javavm_default_enabled(void)
{
    const char *v = getenv(SOBER_JAVAVM_DEFAULT_ENV);
    if (v && v[0] == '0' && (v[1] == '\0' || v[1] == ' ' || v[1] == '\t'))
        return 0;                       /* SOBER_JAVAVM_DEFAULT=0 -> killed */
    return 1;                           /* unset / any other value -> ON */
}

/* ---- JNIInvokeInterface_ implementations ------------------------------- */
static jd_jint jd_DestroyJavaVM(void *vm)
{
    (void)vm;
    /* A real JVM waits for non-daemon threads here; the harness never calls it
     * during a diagnostic run.  Stay harmless: no-op, no fabricated callback. */
    return JD_JNI_OK;
}

static jd_jint jd_AttachCurrentThread(void *vm, void **env_out, void *args)
{
    (void)vm;
    (void)args;
    if (!env_out) return JD_JNI_EINVAL;
    *env_out = g_installed ? (void *)&g_env : NULL;
    if (!g_installed) return JD_JNI_ERR;
    g_attach_calls++;
    return JD_JNI_OK;
}

static jd_jint jd_DetachCurrentThread(void *vm)
{
    (void)vm;
    if (!g_installed) return JD_JNI_ERR;
    g_detach_calls++;
    /* Single shared bridge env: always attached.  Honest no-op. */
    return JD_JNI_OK;
}

static jd_jint jd_GetEnv(void *vm, void **env_out, jd_jint version)
{
    (void)vm;
    if (!env_out) return JD_JNI_EINVAL;
    *env_out = NULL;
    /* Accept "unspecified" (0) and JNI 1.1..1.8; the guest passes 0x10006. */
    if (version != 0 && ((uint32_t)version >> 16) != 1u) return JD_JNI_EVERSION;
    g_getenv_calls++;
    if (!g_installed) return JD_JNI_EDETACHED;
    *env_out = (void *)&g_env;
    return JD_JNI_OK;
}

static jd_jint jd_AttachCurrentThreadAsDaemon(void *vm, void **env_out,
                                              void *args)
{
    return jd_AttachCurrentThread(vm, env_out, args);
}

/* Honest JNIEnv::GetJavaVM override: return THIS VM rather than the bridge's
 * simplistic one.  This is an accessor, not a callback. */
static jd_jint jd_GetJavaVM(void *env, void ***vm_out)
{
    (void)env;
    if (!vm_out) return JD_JNI_EINVAL;
    *vm_out = (void **)&g_vm;
    return JD_JNI_OK;
}

/* ---- table construction ------------------------------------------------ */
static void jd_build_tables(SoberJni *j)
{
    /* Verbatim copy of the bridge's fully built JNINativeInterface_ table.
     * sober_jni_env(j) -> SREnvObj; its +0 word is the SRNativeInterface*. */
    void *bridge_env = sober_jni_env(j);
    void **src = bridge_env ? *(void ***)bridge_env : NULL;
    if (src) memcpy(g_env_table, src, sizeof(g_env_table));

    /* One honest override so GetJavaVM returns the VM actually installed. */
    g_env_table[4u + JD_SLOT_GETJAVAVM] = (void *)(uintptr_t)&jd_GetJavaVM;

    memset(&g_invoke, 0, sizeof(g_invoke));
    g_invoke.fn[0] = (void *)(uintptr_t)&jd_DestroyJavaVM;
    g_invoke.fn[1] = (void *)(uintptr_t)&jd_AttachCurrentThread;
    g_invoke.fn[2] = (void *)(uintptr_t)&jd_DetachCurrentThread;
    g_invoke.fn[3] = (void *)(uintptr_t)&jd_GetEnv;
    g_invoke.fn[4] = (void *)(uintptr_t)&jd_AttachCurrentThreadAsDaemon;

    memset(&g_vm, 0, sizeof(g_vm));
    g_vm.functions = (void *)&g_invoke;
    g_vm.owner     = (void *)j;

    memset(&g_env, 0, sizeof(g_env));
    g_env.functions = (void *)g_env_table;
    g_env.owner     = (void *)j;
}

/* ---- public API -------------------------------------------------------- */
int sober_javavm_default_install(SoberJni *j, void *guest_base)
{
    if (!sober_javavm_default_enabled()) {
        fprintf(stderr,
                "[jvm-default] disabled (%s=0); JavaVM global untouched\n",
                SOBER_JAVAVM_DEFAULT_ENV);
        return SOBER_JNI_OK;
    }
    if (!j || !guest_base) return SOBER_JNI_ERR;

    pthread_mutex_lock(&g_lock);
    if (g_installed) { pthread_mutex_unlock(&g_lock); return SOBER_JNI_OK; }

    /* Ensure the cached class/method IDs (0x72a2f48 / 0x72a2fc0) are live via
     * the existing bridge API.  In the normal harness they are already seeded,
     * so this is skipped and the only guest byte we write is the VM global. */
    if (!sober_javavm_default_cached_ids_live(guest_base))
        (void)sober_jni_seed_guest_jni_state(j, guest_base);

    /* If a diagnostic run installed the opt-in faithful VM, adopt it rather
     * than clobber its per-thread semantics (uses the faithful-vm API). */
    if (sober_javavm_is_active()) {
        void *adopted = sober_javavm_vm(j);
        g_owner      = j;
        g_guest_base = guest_base;
        g_vm_ptr     = adopted;
        g_env_ptr    = sober_javavm_current_env(j);
        g_installed  = 1;
        pthread_mutex_unlock(&g_lock);
        fprintf(stderr,
                "[jvm-default] ADOPTED opt-in faithful VM %p (env %p) at "
                "guest 0x%lx (no clobber)\n", adopted, g_env_ptr,
                (unsigned long)SOBER_JAVAVM_DEFAULT_VM_GLOBAL_RVA);
        return SOBER_JNI_OK;
    }

    jd_build_tables(j);
    if (!g_env_table[4u + JD_SLOT_NEWSTRINGUTF]) {
        pthread_mutex_unlock(&g_lock);
        return SOBER_JNI_ERR;             /* bridge env table was not built */
    }

    /* The single guest write: install the genuine two-level VM pointer. */
    void **slot = (void **)((unsigned char *)guest_base +
                            SOBER_JAVAVM_DEFAULT_VM_GLOBAL_RVA);
    void *old = *slot;
    *slot = (void *)&g_vm;

    g_owner      = j;
    g_guest_base = guest_base;
    g_vm_ptr     = (void *)&g_vm;
    g_env_ptr    = (void *)&g_env;
    g_installed  = 1;
    pthread_mutex_unlock(&g_lock);

    fprintf(stderr,
            "[jvm-default] INSTALLED guest 0x%lx %p -> %p; invoke=%p "
            "GetEnv(+0x30)=%p env=%p env_table=%p NewStringUTF(+0x538)=%p "
            "(verbatim bridge table, no fabricated callbacks)\n",
            (unsigned long)SOBER_JAVAVM_DEFAULT_VM_GLOBAL_RVA, old,
            (void *)&g_vm, (void *)&g_invoke,
            (void *)(uintptr_t)&jd_GetEnv, (void *)&g_env,
            (void *)g_env_table,
            g_env_table[4u + JD_SLOT_NEWSTRINGUTF]);
    return SOBER_JNI_OK;
}

int sober_javavm_default_is_active(void)
{
    return g_installed;
}

void *sober_javavm_default_vm(void)
{
    return g_installed ? g_vm_ptr : NULL;
}

void *sober_javavm_default_env(void)
{
    return g_installed ? g_env_ptr : NULL;
}

void *sober_javavm_default_global(void *guest_base)
{
    if (!guest_base) return NULL;
    return *(void **)((unsigned char *)guest_base +
                      SOBER_JAVAVM_DEFAULT_VM_GLOBAL_RVA);
}

int sober_javavm_default_getenv(void *guest_base, void **env_out)
{
    if (env_out) *env_out = NULL;
    if (!guest_base) return JD_JNI_ERR;
    void *vm = sober_javavm_default_global(guest_base);
    if (!vm) return JD_JNI_ERR;

    /* Call GetEnv through the VM's REAL vtable slot +0x30 (fn[3] after the
     * 3 reserved JNIInvokeInterface_ pointers). */
    JdInvokeTable *it = *(JdInvokeTable **)vm;
    if (!it || !it->fn[3]) return JD_JNI_ERR;
    jd_jint (*getenv_fn)(void *, void **, jd_jint) =
        (jd_jint (*)(void *, void **, jd_jint))(uintptr_t)it->fn[3];
    return (int)getenv_fn(vm, env_out, 0x10006);
}

int sober_javavm_default_cached_ids_live(void *guest_base)
{
    if (!guest_base) return 0;
    void *cls = *(void **)((unsigned char *)guest_base +
                           SOBER_JAVAVM_DEFAULT_CACHED_CLASS_RVA);
    void *mid = *(void **)((unsigned char *)guest_base +
                           SOBER_JAVAVM_DEFAULT_CACHED_METHOD_RVA);
    return (cls != NULL && mid != NULL);
}

int sober_javavm_default_verify(void *guest_base, char *buf, size_t cap)
{
    g_verify_calls++;
    void *global = sober_javavm_default_global(guest_base);
    void *env = NULL;
    int grc = global ? sober_javavm_default_getenv(guest_base, &env) : JD_JNI_ERR;
    int ids = sober_javavm_default_cached_ids_live(guest_base);
    int ok = (global != NULL && grc == JD_JNI_OK && env != NULL && ids);
    if (ok) g_verify_ok++;

    if (buf && cap) {
        void *cls = guest_base ? *(void **)((unsigned char *)guest_base +
                              SOBER_JAVAVM_DEFAULT_CACHED_CLASS_RVA) : NULL;
        void *mid = guest_base ? *(void **)((unsigned char *)guest_base +
                              SOBER_JAVAVM_DEFAULT_CACHED_METHOD_RVA) : NULL;
        (void)snprintf(buf, cap,
                       "active=%d global=%p getenv_rc=%d env=%p "
                       "class(0x72a2f48)=%p method(0x72a2fc0)=%p -> %s",
                       g_installed, global, grc, env, cls, mid,
                       ok ? "PASS" : "FAIL");
    }
    return ok;
}

const char *sober_javavm_default_describe(void *guest_base, char *buf, size_t cap)
{
    (void)sober_javavm_default_verify(guest_base, buf, cap);
    return buf;
}

void sober_javavm_default_stats(char *buf, size_t cap)
{
    if (!buf || !cap) return;
    (void)snprintf(buf, cap,
                   "active=%d enabled=%d getenv=%ld attach=%ld detach=%ld "
                   "verify=%ld verify_ok=%ld",
                   g_installed, sober_javavm_default_enabled(),
                   g_getenv_calls, g_attach_calls, g_detach_calls,
                   g_verify_calls, g_verify_ok);
}
