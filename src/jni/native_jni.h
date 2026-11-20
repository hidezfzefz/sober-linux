/*
 * native_jni.h - Native JNI bridge for the Sober/Roblox guest.
 *
 * Worker-2, Phase 4 task #386.  Owned exclusively by src/jni/.
 *
 * This is a NATIVE reimplementation of the JNIEnv / JavaVM ABI: synthetic
 * jclass/jobject/jmethodID handles, a 231-slot JNINativeInterface_-shaped
 * function table, global/local refs, RegisterNatives bookkeeping and native
 * callbacks.  No Android runtime, no Wine/box64, no instruction translation.
 * A real native JVM is optional; the bridge itself needs no JVM at all.
 *
 * Integration (worker-3 owns the harness):
 *   1. sober_jni_create() / sober_jni_init()
 *   2. pass sober_jni_java_vm() to the guest's JNI_OnLoad()
 *   3. register native handlers with sober_jni_register_native() and/or
 *      callback handlers with sober_jni_set_callback_handler()
 *   4. drive the app-shell path; gate `app shell rendered` on
 *      sober_jni_callback_count() / sober_jni_callback_was_called()
 *   5. sober_jni_shutdown()
 */
#ifndef SOBER_NATIVE_JNI_H
#define SOBER_NATIVE_JNI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SOBER_JNI_OK     0
#define SOBER_JNI_ERR  (-1)

/* ---- headless capability probe --------------------------------------- */
typedef struct {
    int         bridge_ready;   /* we can synthesise JNIEnv + JavaVM natively */
    int         jvm_available;  /* optional native libjvm.so found (dlopen probe) */
    const char *jvm_path;       /* static path string, or "" */
    unsigned    env_slots;      /* number of JNIEnv function-table slots (231) */
    unsigned    vm_slots;       /* number of JavaVM function-table slots (5) */
} SoberJniProbe;

/* Never starts a JVM and never touches guest memory.  Returns SOBER_JNI_OK. */
int         sober_jni_probe(SoberJniProbe *out);
const char *sober_jni_probe_describe(char *buf, size_t cap);

/* ---- lifecycle -------------------------------------------------------- */
typedef struct {
    const char *app_name;   /* optional label for logging */
    int         verbose;    /* log each JNI call to stderr when non-zero */
} SoberJniOptions;

typedef struct SoberJni SoberJni;   /* opaque */

int  sober_jni_create(const SoberJniOptions *opts, SoberJni **out);
int  sober_jni_init(SoberJni *j);   /* idempotent */
void sober_jni_shutdown(SoberJni *j);

/* Native handles to hand to the guest. */
void *sober_jni_java_vm(SoberJni *j);   /* JavaVM* for JNI_OnLoad(vm, reserved) */
void *sober_jni_env(SoberJni *j);       /* primary JNIEnv* */
int  sober_jni_attach(SoberJni *j, void **env_out);  /* AttachCurrentThread simulation */

/* ---- synthetic object / method cache --------------------------------- */
void *sober_jni_find_class(SoberJni *j, const char *name);
void *sober_jni_get_method_id(SoberJni *j, void *clazz,
                              const char *name, const char *sig);
void *sober_jni_new_global_ref(SoberJni *j, void *obj);
void  sober_jni_delete_global_ref(SoberJni *j, void *ref);

/* Native builders for correct JNI call ABIs (decision 020): create shaped
 * jstring handles and opaque native objects (e.g. the app param struct) that
 * the harness can pass to startup RVAs.  All returned handles have the full
 * A/B/C/D guest-shaped graph and canonical pointers. */
void *sober_jni_new_string_utf8(SoberJni *j, const char *text);
const char *sober_jni_string_utf8(SoberJni *j, void *str);
void *sober_jni_new_native_object(SoberJni *j, const char *class_name,
                                  size_t bytes, const void *initial);
unsigned char *sober_jni_native_object_data(SoberJni *j, void *obj,
                                            size_t *cap);

/* Shared-pointer teardown support (worker-1's vtable mapping for the
 * unmapped-RIP fault in nativeAppBridgeAppStart): a native no-op destructor
 * thunk plus a vtable whose +0x10/+0x20/+0x28 slots point to it, and a
 * control block with a valid vtable pointer at +0 and refcount 1 at +8. */
void sober_jni_noop_shared_dtor(void *control_block);
void *sober_jni_shared_vtable(void);
void *sober_jni_new_shared_control(SoberJni *j);

/* A shared_ptr-style parameter wrapper: +0 = guest-style vtable, +8 = refcount
 * 1, +0x10 = optional back-pointer, payload from +0x40.  Pass this instead of
 * a bare A-handle where the guest destroys a JNI param via shared_ptr. */
void *sober_jni_new_shared_param(SoberJni *j, const char *back_pointer,
                                 size_t bytes, const void *initial);
unsigned char *sober_jni_shared_param_data(void *wrapper, size_t *cap);

/* ---- RegisterNatives handling ----------------------------------------- */
/* fnPtr is stored verbatim and invoked (when possible) with the JNI call
 * prototype `void (*)(JNIEnv*, jobject, ...)`. */
int  sober_jni_register_native(SoberJni *j, const char *class_name,
                               const char *method_name, const char *sig,
                               void *fn);
int  sober_jni_is_native_registered(SoberJni *j, const char *class_name,
                                    const char *method_name);

/* ---- callback dispatch ------------------------------------------------- *
 * The guest caches jmethodIDs in JNI_OnLoad and later invokes them with
 * CallVoidMethod / CallIntMethod / ... .  Register a handler keyed by method
 * name; when the guest calls that method the bridge logs the call and invokes
 * the handler.  `args` is a `va_list *` for the Call*Method form and may be
 * NULL for the A/V forms; handlers should copy it with va_copy if needed. */
typedef intptr_t (*SoberJniCallbackFn)(void *env, void *obj,
                                       const char *name, const char *sig,
                                       void *args, void *user);
int  sober_jni_set_callback_handler(SoberJni *j, const char *method_name,
                                    SoberJniCallbackFn fn, void *user);

/* Register no-op native handlers for the full app-shell/activity callback
 * surface mapped by worker-1 (analysis/jni/jni-map.md), so a single call
 * satisfies every guest→Java callback with logging and sensible defaults. */
int  sober_jni_install_default_app_shell_handlers(SoberJni *j);

/* Opt-in Bionic TLS bridge for the guest's thread-state lookups: read the
 * pthread_key_t stored at guest_base + key_rva and install a zeroed state
 * block with pthread_setspecific() so the guest's pthread_getspecific() gets
 * a valid dereferenceable pointer instead of an unrelated TSD value.
 * Call BEFORE invoking guest app-shell/startup RVAs. */
int  sober_jni_seed_guest_tls(void *guest_base, uintptr_t key_rva,
                              size_t state_bytes);

/* Neutralise the guest scheduler singleton that worker-1 identified as the
 * source of the JNIEnv-hook recursion: zero the callback pointer at +0x28 and
 * the nesting counter at +0x70 of the object at guest_base + singleton_rva
 * (try 0x722e540 / 0x722e560).  Call before the app-shell startup RVAs. */
int  sober_jni_neutralize_guest_scheduler(void *guest_base,
                                          uintptr_t singleton_rva);

/* Seed the guest's cached jmethodID globals (worker-1 jni-map §1.2) with
 * synthetic SrObject handles and set the JNI_OnLoad completion flag, so the
 * harness can skip the guest's re-entrant JNI_OnLoad and drive the app-shell
 * startup RVAs directly.  Safe on BSS (no APS2 relocations touch these slots). */
int  sober_jni_seed_guest_jni_state(SoberJni *j, void *guest_base);

/* Opt-in (SOBER_SEED_JNI_VM) seed of the JNI_OnLoad-cached JavaVM global at
 * guest RVA 0x7cdec78.  The guest captures JNI_OnLoad's first argument (the
 * JavaVM*) there via 0x227af6e -> 0x227b8fb -> 0x227b92d.  The string helper
 * 0x227b9e2 retrieves a JNIEnv from it through the JNIInvokeInterface_ slots
 * +0x30 (GetEnv) / +0x20 (AttachCurrentThread) and then calls JNIEnv
 * NewStringUTF (vtable +0x538); skipping JNI_OnLoad leaves the global NULL and
 * faults at 0x2eb4b65.  Seeding with sober_jni_java_vm() reproduces the genuine
 * JNI object layout -- no synthetic guest vtable and no fabricated callbacks.
 * Default OFF; never emits or triggers a callback. */
int  sober_jni_seed_guest_onload_globals(SoberJni *j, void *guest_base);

/* Valid shaped object handle for the Phase 4c caller-side pointer fix.
 * NULL if no bridge was created. */
void *sober_jni_safe_app_shell_handle(void);
void *sober_jni_fresh_handle(void);

/* ---- activity inventory (for the harness marker) ---------------------- */
uint64_t sober_jni_env_call_count(SoberJni *j);      /* all JNIEnv calls */
uint64_t sober_jni_callback_count(SoberJni *j);      /* guest callback dispatches */
uint64_t sober_jni_register_natives_count(SoberJni *j);
int      sober_jni_callback_was_called(SoberJni *j, const char *method_name);
const char *sober_jni_last_callback(SoberJni *j);    /* "" if none */
/* Human-readable recent-activity summary; returns buf. */
const char *sober_jni_describe_activity(SoberJni *j, char *buf, size_t cap);

#ifdef __cplusplus
}
#endif
#endif /* SOBER_NATIVE_JNI_H */
