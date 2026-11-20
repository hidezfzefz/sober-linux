# src/jni — native JNI bridge (Phase 4, worker-2)

## Contract

`native_jni.h` is the public API. All `int` functions return
`SOBER_JNI_OK` (0) / `SOBER_JNI_ERR` (-1).

Lifecycle: `sober_jni_create(opts,&j)`, `sober_jni_init(j)`,
`sober_jni_shutdown(j)`.

Handles for the guest: `sober_jni_java_vm(j)` returns a `JavaVM*` to pass as
the first argument of the guest's `JNI_OnLoad` (`0x227af5a`);
`sober_jni_env(j)` returns the primary `JNIEnv*`;
`sober_jni_attach(j,&env)` simulates `AttachCurrentThread`.

Classes/methods/refs: `sober_jni_find_class`, `sober_jni_get_method_id`,
`sober_jni_new_global_ref`, `sober_jni_delete_global_ref`.

Registration: `sober_jni_register_native(class,name,sig,fn)` and the
`RegisterNatives` JNIEnv slot both populate the same table;
`sober_jni_is_native_registered()` queries it.

Callback dispatch: `sober_jni_set_callback_handler(name, fn, user)` installs a
handler invoked when the guest calls a cached jmethodID with that name through
`CallVoidMethod`/`Call*Method`/`CallStatic*Method`. `args` is a `va_list*` for
plain and `V` forms, a `jvalue*` for `A` forms. Convenience:
`sober_jni_install_default_app_shell_handlers(j)` installs no-op, logging
handlers for every callback in worker-1's `analysis/jni/jni-map.md` §4.

`sober_jni_seed_guest_jni_state(j, guest_base)` fills the complete cached
jmethodID ranges `0x72a2f48..0x72a3058` and `0x72ca178..0x72ca1e0` with
synthetic method objects (named entries from worker-1's map where known) and
sets both JNI_OnLoad flags (`0x72a3000`, `0x7d8fb60`) to 1. Use it with
`SOBER_SKIP_JNI_ONLOAD=1` instead of calling the re-entrant guest JNI_OnLoad.

Opt-in guest mitigations (both strictly opt-in, no effect on phase-2/3):
`sober_jni_seed_guest_tls(guest_base, 0x72a1b08, 4096)` installs a zeroed
thread-state block in the **NDK libc stub's** private TSD array (resolved via
`dlvsym(stub, "pthread_setspecific", "LIBC")`; the stub DSO must already be
loaded, or set `SOBER_NDK_STUB_LIBC` to its path) — it no longer uses glibc's
unrelated key space;
`sober_jni_neutralize_guest_scheduler(guest_base, 0x722e540)` zeroes the
scheduler singleton's +0x28 callback pointer and +0x70 nesting counter that
worker-1 identified as the JEnv-hook recursion source. `ExceptionCheck` /
`ExceptionClear` are pure (no callbacks, no logging, no counters) and the whole
dispatch path has a thread-local re-entrancy guard.

Gating counters: `sober_jni_env_call_count`, `sober_jni_callback_count`,
`sober_jni_register_natives_count`, `sober_jni_callback_was_called(name)`,
`sober_jni_last_callback()`, `sober_jni_describe_activity()`.

## ABI fidelity

The synthetic `JNIEnv` is a pointer to a 231-slot `JNINativeInterface_`-shaped
table (4 reserved slots first); `JavaVM` is a pointer to a 5-slot
`JNIInvokeInterface_`-shaped table (3 reserved slots first). Slot indices were
verified against the JDK `jni.h` and against worker-1's byte offsets
(`FindClass` 0x30 → index 2, `GetStaticMethodID` 0x388 → 109,
`NewGlobalRef` 0xa8 → 17, `ExceptionClear` 0x88 → 13,
`ExceptionCheck` 0x720 → 224). Unimplemented slots are harmless stubs
returning 0/NULL; object-returning `Call*Method` forms return a non-NULL
synthetic String object so string getters cannot crash the guest.

## Build / self-test

No JVM needed. Link deps: none beyond libc (the optional JVM check uses
`access()`). Integrate with `add_subdirectory(src/jni)` and link `sober_jni`
only into `guest-init-host`.

```sh
gcc -std=gnu17 -Wall -Wextra -Isrc/jni \
    src/jni/native_jni.c src/jni/jni_probe.c -o /tmp/jni_probe -ldl
/tmp/jni_probe --probe    # headless: prints bridge/JVM capability, no JVM started
/tmp/jni_probe            # self-test: JavaVM.GetEnv -> FindClass -> GetMethodID
                          # -> CallVoidMethod -> callback -> RegisterNatives
```

Verified output: `jni bridge self-test OK`, `callbacks=1 was_called=1`.
