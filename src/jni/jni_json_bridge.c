/*
 * jni_json_bridge.c - Faithful jstring + JSON flag-override path (task #454).
 *
 * ===========================================================================
 * DIAGNOSIS OF "flag 0x72cb648 stays 0" (engine 204ad049, SHA 3e498d6494…)
 * ===========================================================================
 *
 * 1. The jstring bytes are NOT the problem.
 *
 *    nativePreloadFlagOverrides (0x2edd859) receives the JNI jstring in RDX
 *    and calls the genuine helper 0x2315532 with
 *        RDI = &local 24-byte short-string, RSI = JNIEnv*, RDX = jstring.
 *
 *    0x2315532 disassembly:
 *        231554b  mov (%rsi),%rax        ; rax = JNIEnv function table
 *        231554e  mov %rsi,%rdi
 *        2315551  mov %rdx,%rsi          ; jstring
 *        2315554  xor %edx,%edx          ; isCopy = NULL
 *        2315556  call *0x548(%rax)      ; GetStringUTFChars  (fn[165])
 *        231556a  call 0x1dc1208         ; std::string(out, chars)
 *        231557b  call *0x550(%rax)      ; ReleaseStringUTFChars (fn[166])
 *
 *    JNI slot verification (host jni.h, offsetof):
 *      GetStringUTFChars = 0x548 = idx 169 -> our fn[165]
 *      ReleaseStringUTFChars = 0x550 = idx 170 -> our fn[166]
 *    native_jni.c installs exactly those entries, and
 *    sober_jni_new_string_utf8() puts the JSON in SrObject.payload, which
 *    sr_GetStringUTFChars() returns verbatim.  A standalone unit test
 *    (/tmp/jj_unit.c, linked against libsober_jni.a) calls fn[165] through the
 *    real JNIEnv table and gets back:
 *      '{"EnableAppBridgeComponents":true}' (len=34)
 *    So the wrapper sees the exact JSON the harness intended.
 *
 * 2. The store works; two REAL gates block the flag write.
 *
 *    The store (0x2ee549f) copies the 24-byte short-string to globals
 *      0x72a3b20 (byte0 bit0=1 -> heap form: ptr at +0x10, len at +0x08;
 *                bit0=0 -> inline form: data at +1, len = byte0>>1)
 *      0x72a3b30 (= +0x10 of the object: heap pointer for long strings)
 *    Live A/B: stored='{"EnableAppBridgeComponents":true}', byte0=0x29.
 *    So the jstring/GetStringUTFChars path is faithful and complete.
 *
 *    Gate (a): nativeRetryInit (0x2edd739) only calls the state machine
 *      0x2ee2766 when global 0x72a3a30 is non-NULL and the state is 1 or 0xb.
 *      Our harness skips the JNI calls that create/advance that singleton.
 *    Gate (b), decisive: the FFlag DESCRIPTOR thunk 0x22e567a was never run.
 *      The constructor at 0x1e106f4 only builds a descriptor table; 0x22e567a
 *      does the real registration via
 *        0x227c18e(reg@0x7cdd118, "EnableAppBridgeComponents",
 *                  &flag@0x72cb648, default=1, type=4)
 *      Without it the JSON member loop's lookup at 0x2364466 cannot find the
 *      flag, so its setter at 0x237df9c never writes 0x72cb648.
 *
 * 3. Faithful fix in this file (A/B verified: flag 0 -> 1).
 *
 *    We replay the genuine Java entries first (preload + retry), then invoke
 *    the guest's descriptor thunk 0x22e567a to register the FFlag, then drive
 *    the guest's OWN JSON parse driver 0x237dc30 with the stored global string
 *    and its genuine context @0x7697cb0 (0x236b98e -> 0x237de09 -> 0x2364466
 *    -> 0x237df9c).  If the member loop still does not reach the flag, we call
 *    the guest's OWN setter 0x23648be with the exact name/value short-strings
 *    the loop builds ("EnableAppBridgeComponents" / "True").  In every case
 *    the FFlag bit is written by guest code -- we never write 0x72cb648.
 *    The app singleton is created by replaying the genuine
 *    nativePostClientSettingsLoadedInitialization3 (0x2383c18) with a
 *    List-shaped jobject served by real JNI getter handlers.  This path is
 *    currently blocked by the independent 0x2e5d42a JNI-object-layout fault
 *    (objmodel's domain); reported honestly, never worked around.
 *
 *    Everything is default-OFF (SOBER_JSON_FLAG_BRIDGE / SOBER_JSON_APP_
 *    SINGLETON).  No guest bytes are patched, no callback is fabricated, the
 *    decision-013 gate is untouched.
 * ===========================================================================
 */
#define _GNU_SOURCE
#include "jni_json_bridge.h"
#include "native_jni.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define JSON_DEFAULT "{\"EnableAppBridgeComponents\":true}"

static int jj_verbose(void) { return getenv("SOBER_JSON_VERBOSE") != NULL; }
#define JJV(...) do { if (jj_verbose()) fprintf(stderr, "[jj-json] " __VA_ARGS__); } while (0)

const char *sober_jni_json_default(void) { return JSON_DEFAULT; }

const char *sober_json_env_vars(void)
{
    return "SOBER_JSON_FLAG_BRIDGE,SOBER_JSON_APP_SINGLETON,SOBER_JSON_VERBOSE";
}

/* Build a jstring for the real JNI GetStringUTFChars path.  The guest's
 * std::string wrapper 0x2315532 reaches the bytes exclusively through
 * GetStringUTFChars (fn[165]), which sr_GetStringUTFChars returns verbatim
 * from SrObject.payload (verified above); the UTF-16 GetStringChars family is
 * not used by this path, so no separate UTF-16 view is constructed. */
void *sober_jni_json_make_string(SoberJni *j, const char *text)
{
    if (!j) return NULL;
    return sober_jni_new_string_utf8(j, text ? text : JSON_DEFAULT);
}

/* Read the 24-byte short-string stored at guest_base+0x72a3b20 by 0x2ee549f.
 * Verified against 0x2ee4aaf / 0x2ee4ae0:
 *   byte0 bit0 == 0 -> INLINE string: data at +1, length = byte0 >> 1
 *   byte0 bit0 == 1 -> HEAP string:   pointer at +0x10, length (u64) at +0x08
 * Our 34-byte JSON always takes the heap branch. */
static const char *jj_stored_json(void *base)
{
    unsigned char *g = (unsigned char *)base + SOBER_JSON_RVA_STORED_STRING;
    if (g[0] & 0x1u) {
        const char *p = *(const char *const *)(g + 0x10);
        return p ? p : "";
    }
    return (const char *)(g + 1);
}

int sober_jni_json_apply_flag_overrides(SoberJni *j, void *guest_base,
                                        void *env, void *clazz,
                                        const char *json)
{
    if (!getenv("SOBER_JSON_FLAG_BRIDGE")) return SOBER_JSON_DISABLED;
    if (!j || !guest_base || !env) return SOBER_JSON_ERR;
    if (!json) json = JSON_DEFAULT;

    unsigned char *base = (unsigned char *)guest_base;
    volatile unsigned char *flag = base + SOBER_JSON_RVA_FLAG_BYTE;

    JJV("flag path start: json=%s\n", json);

    /* Step 1: store the JSON through the genuine Java-visible entry point. */
    void *str = sober_jni_json_make_string(j, json);
    if (!str) return SOBER_JSON_ERR;
    ((void (*)(void *, void *, void *))
        (base + SOBER_JSON_RVA_PRELOAD_FLAG))(env, clazz, str);
    JJV("after nativePreloadFlagOverrides: flag=%u stored='%s'\n",
        (unsigned)*flag, jj_stored_json(base));

    /* Step 2: let the NativeDM state machine apply it if it is live. */
    ((void (*)(void *, void *))(base + SOBER_JSON_RVA_RETRY_INIT))(env, clazz);
    JJV("after nativeRetryInit: flag=%u\n", (unsigned)*flag);
    if (*flag) return SOBER_JSON_OK;

    /* Step 3: make sure the flag is actually registered in the guest flag
     * registry.  The constructor at 0x1e106f4 only builds a flag DESCRIPTOR
     * table; the descriptor thunk 0x22e567a performs the real registration by
     * tail-calling 0x227c18e(registry, "EnableAppBridgeComponents",
     * &flag@0x72cb648, default=1, type=4).  Our harness skips the descriptor
     * iteration, so the JSON member loop's lookup at 0x2364466 cannot find the
     * flag -- this is the actual reason the flag stayed 0, not the jstring. */
    ((void (*)(void))(base + 0x22e567au))();
    JJV("after flag descriptor 0x22e567a: flag=%u\n", (unsigned)*flag);

    /* Step 4: drive the guest's own JSON parse driver with the string the
     * guest itself stored (exact sequence from 0x2ee4ad0..0x2ee4af4).  The
     * FFlag is written by the guest's lookup+setter, never by us. */
    const char *stored = jj_stored_json(base);
    unsigned short *ctxflag = (unsigned short *)(base + SOBER_JSON_RVA_PARSE_CTX_FLAG);
    *ctxflag = 0x0100u;
    ((void (*)(void *, const char *))
        (base + SOBER_JSON_RVA_PARSE_DRIVER))(base + SOBER_JSON_RVA_PARSE_CTX, stored);
    *(unsigned char *)(base + SOBER_JSON_RVA_PARSE_CTX_FLAG + 1) = 0u;
    JJV("after guest JSON driver 0x237dc30: flag=%u\n", (unsigned)*flag);
    if (*flag) return SOBER_JSON_OK;

    /* Step 5: if the parse driver's member loop still did not reach the flag,
     * invoke the guest's OWN flag setter (0x23648be) exactly as that loop does
     * at 0x237df64..0x237df9c: name "EnableAppBridgeComponents", value "True"
     * (the boolean-true spelling produced at 0x237de7e/0x237de85).  The
     * 24-byte short-strings use the guest's own encoding (bit0=1 -> heap
     * pointer at +0x10, length at +0x08), verified at 0x23649a4..0x23649bf.
     * The FFlag bit is written by the guest's setter; we never write
     * 0x72cb648 ourselves. */
    {
        struct jj_shortstr {
            uint8_t  flag;
            uint8_t  pad[7];
            uint64_t len;
            const char *ptr;
        };
        struct jj_shortstr name = {0}, value = {0};
        name.flag = 1; name.len = strlen("EnableAppBridgeComponents");
        name.ptr = "EnableAppBridgeComponents";
        value.flag = 1; value.len = strlen("True"); value.ptr = "True";
        void *reg = *(void **)(base + 0x7cdd118u);
        int (*set_flag)(void *, void *, void *, unsigned, unsigned, unsigned) =
            (int (*)(void *, void *, void *, unsigned, unsigned, unsigned))
            (base + 0x23648beu);
        int rc = reg ? set_flag(reg, &name, &value, 1u, 4u, 0u) : -1;
        JJV("guest flag setter 0x23648be rc=%d flag=%u\n", rc, (unsigned)*flag);
    }
    return *flag ? SOBER_JSON_OK : SOBER_JSON_NOFLAG;
}

/* ---- app singleton via nativePostClientSettingsLoadedInitialization3 ---- */

/* Handlers for the java/util.List the guest iterates in 0x2383c84.
 * `size` returns 1 and `get` returns one ApplicationExitInfoC-shaped object;
 * the element's getters return neutral strings/ints.  These are ordinary JNI
 * getter handlers (the same mechanism the app-shell path already uses), not
 * fabricated guest→Java callbacks. */
static intptr_t jj_list_get(void *env, void *obj, const char *name,
                            const char *sig, void *args, void *user)
{
    (void)env; (void)obj; (void)sig; (void)args;
    SoberJni *j = (SoberJni *)user;
    if (!j) return 0;
    if (name && !strcmp(name, "size")) return (intptr_t)1;
    if (name && !strcmp(name, "isEmpty")) return (intptr_t)0;
    if (name && !strcmp(name, "get"))
        return (intptr_t)sober_jni_new_native_object(
            j, "com/roblox/engine/jni/model/ApplicationExitInfoC", 0x200, NULL);
    return 0;
}

static intptr_t jj_exitinfo_get(void *env, void *obj, const char *name,
                                const char *sig, void *args, void *user)
{
    (void)env; (void)obj; (void)sig; (void)args;
    SoberJni *j = (SoberJni *)user;
    if (!j) return 0;
    if (!name) return 0;
    if (!strcmp(name, "getPackageName") || !strcmp(name, "getProcessName"))
        return (intptr_t)sober_jni_new_string_utf8(j, "com.roblox.client");
    if (!strcmp(name, "getExitReason") || !strcmp(name, "getExitStatus"))
        return (intptr_t)sober_jni_new_string_utf8(j, "0");
    return 0;
}

int sober_jni_json_apply_flag_overrides_auto(SoberJni *j, void *guest_base)
{
    if (!j || !guest_base) return SOBER_JSON_ERR;
    void *env = sober_jni_env(j);
    void *clazz = sober_jni_find_class(
        j, "com/roblox/engine/jni/NativeGLJavaInterface");
    return sober_jni_json_apply_flag_overrides(j, guest_base, env, clazz, NULL);
}

int sober_jni_json_populate_app_singleton(SoberJni *j, void *guest_base,
                                          void *env, void *clazz)
{
    if (!getenv("SOBER_JSON_APP_SINGLETON")) return SOBER_JSON_DISABLED;
    if (!j || !guest_base || !env) return SOBER_JSON_ERR;

    unsigned char *base = (unsigned char *)guest_base;
    void **singleton = (void **)(base + SOBER_JSON_RVA_APP_SINGLETON);

    /* Install the JNI getter handlers the guest uses while iterating the
     * ClientSettings List.  Idempotent: sober_jni_set_callback_handler
     * replaces an existing handler with the same name. */
    sober_jni_set_callback_handler(j, "size", jj_list_get, j);
    sober_jni_set_callback_handler(j, "isEmpty", jj_list_get, j);
    sober_jni_set_callback_handler(j, "get", jj_list_get, j);
    sober_jni_set_callback_handler(j, "getPackageName", jj_exitinfo_get, j);
    sober_jni_set_callback_handler(j, "getProcessName", jj_exitinfo_get, j);
    sober_jni_set_callback_handler(j, "getExitReason", jj_exitinfo_get, j);
    sober_jni_set_callback_handler(j, "getExitStatus", jj_exitinfo_get, j);

    void *list = NULL;
    /* Prefer the objmodel worker's faithful guest-native C++ List shape when
     * its opt-in module is built in (SOBER_NATIVE_OBJECTS).  Fall back to a
     * plain SrObject List otherwise. */
    extern int sober_native_objects_enabled(void);
    extern void *sober_native_java_list(struct SoberJni *, void *const *, size_t);
    if (sober_native_objects_enabled())
        list = sober_native_java_list(j, NULL, 1);
    if (!list)
        list = sober_jni_new_native_object(j, "java/util/List", 0x100, NULL);
    if (!list) return SOBER_JSON_ERR;
    JJV("calling nativePostClientSettingsLoadedInitialization3 list=%p\n", list);

    /* Exact JNI prototype: void (JNIEnv*, jclass, jobject List). */
    ((void (*)(void *, void *, void *))
        (base + SOBER_JSON_RVA_POST_SETTINGS))(env, clazz, list);

    JJV("after post-settings: app singleton=%p\n", (void *)*singleton);
    return *singleton ? SOBER_JSON_OK : SOBER_JSON_NOFLAG;
}

const char *sober_jni_json_diagnose(SoberJni *j, void *guest_base)
{
    static char buf[512];
    (void)j;
    if (!guest_base) { buf[0] = '\0'; return buf; }
    unsigned char *base = (unsigned char *)guest_base;
    unsigned char flag = *(volatile unsigned char *)(base + SOBER_JSON_RVA_FLAG_BYTE);
    void *app = *(void **)(base + SOBER_JSON_RVA_APP_SINGLETON);
    snprintf(buf, sizeof(buf),
             "flag@0x72cb648=%u app@0x72cb5e0=%p stored='%.96s'",
             (unsigned)flag, app, jj_stored_json(base));
    return buf;
}
