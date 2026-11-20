/*
 * jni_probe.c - standalone self-test / headless probe for native_jni.c.
 *
 * Worker-2, Phase 4 task #386.  Calls the JNIEnv/JavaVM tables through a
 * layout-identical local struct, exactly as the guest's own jni.h would.
 */
#include "native_jni.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/* Mirror the public JNI table layout without depending on a JDK header. */
typedef struct { void *slots[4 + 231]; } TestNativeInterface;
typedef struct { TestNativeInterface *functions; } TestEnv;
typedef struct { void *slots[3 + 5]; } TestInvokeInterface;
typedef struct { TestInvokeInterface *functions; } TestVM;

typedef struct {
    char *name;
    char *signature;
    void *fnPtr;
} TestJNINativeMethod;

static int callback_seen;
static intptr_t cb_narrow(void *env, void *obj, const char *name,
                          const char *sig, void *args, void *user)
{ (void)env; (void)obj; (void)name; (void)sig; (void)args; (void)user; return 4; }

static intptr_t cb(void *env, void *obj, const char *name, const char *sig,
                   void *args, void *user)
{
    (void)env; (void)obj; (void)args; (void)user;
    printf("callback dispatched: name=%s sig=%s\n", name, sig ? sig : "");
    callback_seen++;
    return 0;
}

int main(int argc, char **argv)
{
    char desc[256];
    printf("jni probe: %s\n", sober_jni_probe_describe(desc, sizeof(desc)));
    if (argc > 1 && !strcmp(argv[1], "--probe")) return 0;

    SoberJni *j = NULL;
    if (sober_jni_create(NULL, &j) != SOBER_JNI_OK || !j) return 1;
    sober_jni_init(j);

    TestVM *vm = (TestVM *)sober_jni_java_vm(j);
    TestEnv *env = NULL;
    int (*GetEnv)(void *, void **, int) = (int (*)(void *, void **, int))vm->functions->slots[3 + 3];
    if (GetEnv(vm, (void **)&env, 0x00010006) != 0 || !env) {
        fprintf(stderr, "JavaVM.GetEnv failed\n");
        sober_jni_shutdown(j);
        return 1;
    }
    printf("JNIEnv acquired via JavaVM.GetEnv\n");

    /* Call through raw slots, as generated guest code does. */
    int32_t (*GetVersion)(void *) = (int32_t (*)(void *))env->functions->slots[4 + 0];
    void *(*FindClass)(void *, const char *) =
        (void *(*)(void *, const char *))env->functions->slots[4 + 2];
    void *(*GetMethodID)(void *, void *, const char *, const char *) =
        (void *(*)(void *, void *, const char *, const char *))env->functions->slots[4 + 29];
    void *(*NewGlobalRef)(void *, void *) =
        (void *(*)(void *, void *))env->functions->slots[4 + 17];
    void (*CallVoidMethod)(void *, void *, void *, ...) =
        (void (*)(void *, void *, void *, ...))env->functions->slots[4 + 57];
    int (*RegisterNatives)(void *, void *, const TestJNINativeMethod *, int) =
        (int (*)(void *, void *, const TestJNINativeMethod *, int))env->functions->slots[4 + 211];

    printf("GetVersion -> %#x\n", (unsigned)GetVersion(env));
    void *cls = FindClass(env, "com/roblox/engine/jni/NativeGLJavaInterface");
    printf("FindClass -> %p\n", cls);
    /* Verify the engine's handle validator (0x245605d) would accept it:
     * *(jobj) -> B (magic 0xc0dedbad), *(B+0x68) -> C (magic 0x000bab1e). */
    /* Synthetic handles carrying 0xc0dedbad must leave +0x20 and +0x68 ZERO
     * so the guest's lazy-link guard and teardown null-checks take their safe
     * branches (decision 028 follow-up / task #398). */
    void *A = *(void **)cls;
    uint32_t am = *(uint32_t *)A;
    uint64_t a20 = *(uint64_t *)((char *)A + 0x20);
    uint64_t a68 = *(uint64_t *)((char *)A + 0x68);
    printf("handle seed: A=%p magic=%#x +0x20=%llu +0x68=%llu\n",
           A, am, (unsigned long long)a20, (unsigned long long)a68);
    if (am != 0xc0dedbadu || a20 != 0 || a68 != 0) {
        fprintf(stderr, "synthetic handle seed fields are not zeroed\n");
        sober_jni_shutdown(j);
        return 1;
    }

    void *mid = GetMethodID(env, cls, "onSurfaceCreatedNative", "(JLandroid/view/Surface;)V");
    printf("GetMethodID -> %p\n", mid);
    NewGlobalRef(env, mid);

    sober_jni_set_callback_handler(j, "onSurfaceCreatedNative", cb, NULL);
    CallVoidMethod(env, cls, mid, (long)1234, (void *)0xdeadbeef);
    if (!callback_seen) {
        fprintf(stderr, "callback was not dispatched\n");
        sober_jni_shutdown(j);
        return 1;
    }

    /* Object-returning default handlers must produce non-NULL typed values. */
    sober_jni_install_default_app_shell_handlers(j);
    void *dsp_mid = sober_jni_get_method_id(j, cls, "getDeviceStaticParams",
        "()Lcom/roblox/engine/jni/model/DeviceStaticParams;");
    void *(*CallStaticObjectMethod)(void *, void *, void *, ...) =
        (void *(*)(void *, void *, void *, ...))env->functions->slots[4 + 110];
    void *dsp = CallStaticObjectMethod(env, cls, dsp_mid);
    printf("getDeviceStaticParams -> %p %s\n", dsp, dsp ? "(non-NULL)" : "(NULL!)");
    if (!dsp) {
        fprintf(stderr, "object callback returned NULL\n");
        sober_jni_shutdown(j);
        return 1;
    }

    /* A handler returning a narrow int must be normalised to a shaped handle. */
    sober_jni_set_callback_handler(j, "getNarrowHandle", cb_narrow, NULL);
    void *nm = sober_jni_get_method_id(j, cls, "getNarrowHandle", "()Ljava/lang/Object;");
    void *narrow = CallStaticObjectMethod(env, cls, nm);
    void *nb = narrow ? *(void **)narrow : NULL;
    uint32_t nmagic = nb ? *(uint32_t *)nb : 0;
    printf("narrow return normalised: ptr=%p Bmagic=%#x\n", narrow, nmagic);
    if (!nb || nmagic != 0xc0dedbadu) {
        fprintf(stderr, "narrow return was not normalised\n");
        sober_jni_shutdown(j);
        return 1;
    }

    void *js = sober_jni_new_string_utf8(j, "/tmp/sober");
    /* JNIEnv slot 165 must tolerate a foreign/invalid jstring without fault. */
    const char *(*GetStringUTFChars)(void *, void *, void *) =
        (const char *(*)(void *, void *, void *))env->functions->slots[4 + 165];
    const char *safe = GetStringUTFChars(env, (void *)0x55f800000000ULL, NULL);
    printf("GetStringUTFChars(foreign) -> \"%s\"\n", safe ? safe : "(null)");
    if (!safe || safe[0] != '\0') {
        fprintf(stderr, "foreign jstring was not handled safely\n");
        sober_jni_shutdown(j);
        return 1;
    }

    printf("jstring helper: %p -> \"%s\"\n", js, sober_jni_string_utf8(j, js));
    if (!js || strcmp(sober_jni_string_utf8(j, js), "/tmp/sober")) {
        fprintf(stderr, "jstring helper failed\n");
        sober_jni_shutdown(j);
        return 1;
    }
    unsigned char init[16] = { 1, 2, 3 };
    size_t objcap = 0;
    void *native = sober_jni_new_native_object(j, "AppInitParams", sizeof(init), init);
    unsigned char *p = sober_jni_native_object_data(j, native, &objcap);
    printf("native object helper: %p cap=%zu first=%u\n", native, objcap, p ? p[0] : 0);
    if (!native || objcap != sizeof(init) || !p || p[0] != 1) {
        fprintf(stderr, "native object helper failed\n");
        sober_jni_shutdown(j);
        return 1;
    }

    void **vt = (void **)sober_jni_shared_vtable();
    void *ctrl = sober_jni_new_shared_control(j);
    printf("shared vtable: +0x10=%p +0x20=%p +0x28=%p control=%p refs=%lld\n",
           vt[2], vt[4], vt[5], ctrl,
           ctrl ? (long long)*(int64_t *)((char *)ctrl + 8) : -1);
    if (!vt[2] || !vt[4] || !vt[5] || !ctrl ||
        *(int64_t *)((char *)ctrl + 8) != 1) {
        fprintf(stderr, "shared vtable/control helper failed\n");
        sober_jni_shutdown(j);
        return 1;
    }

    size_t wcap = 0;
    void *shared = sober_jni_new_shared_param(j, "back", 32, init);
    unsigned char *wdata = sober_jni_shared_param_data(shared, &wcap);
    printf("shared param: wrapper=%p vt=%p refs=%lld payload=%p cap=%zu\n",
           shared, *(void **)shared,
           (long long)*(int64_t *)((char *)shared + 8), wdata, wcap);
    if (!shared || *(void **)shared != sober_jni_shared_vtable() ||
        *(int64_t *)((char *)shared + 8) != 1 || wcap != 32 || !wdata) {
        fprintf(stderr, "shared param helper failed\n");
        sober_jni_shutdown(j);
        return 1;
    }

    /* Phase 5: CallStaticVoidMethodV/A must be installed at fn[138]/fn[139]
     * (global slot 142/143, offset 0x470/0x478) so the guest's cached
     * gameLoadedCallback thunk can be observed by our dispatcher. */
    if (!env->functions->slots[138] || !env->functions->slots[139]) {
        fprintf(stderr, "CallStaticVoidMethodV/A slots are NULL\n");
        sober_jni_shutdown(j);
        return 1;
    }
    printf("CallStaticVoidMethodV/A installed at fn[138]/fn[139]\n");

    TestJNINativeMethod mn = { "nativeGetFFlag", "()Z", (void *)(uintptr_t)cb };
    int rc = RegisterNatives(env, cls, &mn, 1);
    printf("RegisterNatives -> %d (native registered=%d)\n", rc,
           sober_jni_is_native_registered(j, NULL, "nativeGetFFlag"));

    char act[256];
    printf("activity: %s\n", sober_jni_describe_activity(j, act, sizeof(act)));
    printf("callbacks=%llu was_called=%d last=%s\n",
           (unsigned long long)sober_jni_callback_count(j),
           sober_jni_callback_was_called(j, "onSurfaceCreatedNative"),
           sober_jni_last_callback(j));
    sober_jni_shutdown(j);
    printf("jni bridge self-test OK\n");
    return 0;
}
