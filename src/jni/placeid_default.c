/*
 * placeid_default.c - DEFAULT-ON, correctly typed StartGameParams.placeId()J.
 *
 * Team 4 / agent T4-2.  NEW FILE ONLY; see placeid_default.h for the complete
 * evidence chain (guest RVAs, smali getter table) and the lead wiring contract.
 *
 * The module registers all 24 AutoValue_StartGameParams getters through the
 * bridge's sober_jni_set_callback_handler() (replace-by-name).  placeId()J is
 * served as a full machine jlong > 0; J/Z/I getters are never served as a
 * jstring handle.  The cached guest global 0x72a31c0 is only ever READ here.
 */
#define _GNU_SOURCE
#include "placeid_default.h"
#include "native_jni.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* ---- getter indices (MUST match the s_getters[] table order) ------------ */
enum {
    PID_SURFACE = 0, PID_PLATFORM, PID_DEVICE, PID_PLACE_ID, PID_USER_ID,
    PID_ACCESS_CODE, PID_LINK_CODE, PID_GAME_ID, PID_UNDER13, PID_USERNAME,
    PID_CONVERSATION_ID, PID_RESERVED_CODE, PID_CALL_ID, PID_JOIN_REQUEST_TYPE,
    PID_REFERRAL_PAGE, PID_LAUNCH_DATA, PID_JOIN_CONTEXT, PID_EVENT_ID,
    PID_JOIN_ATTEMPT_ID, PID_JOIN_ATTEMPT_ORIG, PID_ISO_CONTEXT, PID_REFERRED_BY,
    PID_GAME_ID_EXCLUDE, PID_VR_CONTEXT
};

typedef char pid_assert_count[(SOBER_PLACEID_GETTER_COUNT == 24 &&
                               PID_VR_CONTEXT + 1 == 24) ? 1 : -1];
typedef char pid_assert_intptr[(sizeof(intptr_t) >= 8) ? 1 : -1];

/* Exact smali names and signatures, verified against
 * /tmp/opencode/mini/out/smali/com/roblox/engine/jni/autovalue/
 * AutoValue_StartGameParams.smali (24 public getters). */
static const SoberPlaceidGetter s_getters[SOBER_PLACEID_GETTER_COUNT] = {
    { SOBER_PLACEID_GET_SURFACE,           SOBER_PLACEID_SIG_SURFACE,           SOBER_PLACEID_RET_OBJECT },
    { SOBER_PLACEID_GET_PLATFORM,          SOBER_PLACEID_SIG_PLATFORM,          SOBER_PLACEID_RET_OBJECT },
    { SOBER_PLACEID_GET_DEVICE,            SOBER_PLACEID_SIG_DEVICE,            SOBER_PLACEID_RET_DEVICE },
    { SOBER_PLACEID_GET_PLACE_ID,          SOBER_PLACEID_SIG_PLACE_ID,          SOBER_PLACEID_RET_J },
    { SOBER_PLACEID_GET_USER_ID,           SOBER_PLACEID_SIG_USER_ID,           SOBER_PLACEID_RET_J },
    { SOBER_PLACEID_GET_ACCESS_CODE,       SOBER_PLACEID_SIG_ACCESS_CODE,       SOBER_PLACEID_RET_STRING },
    { SOBER_PLACEID_GET_LINK_CODE,         SOBER_PLACEID_SIG_LINK_CODE,         SOBER_PLACEID_RET_STRING },
    { SOBER_PLACEID_GET_GAME_ID,           SOBER_PLACEID_SIG_GAME_ID,           SOBER_PLACEID_RET_STRING },
    { SOBER_PLACEID_GET_UNDER13,           SOBER_PLACEID_SIG_UNDER13,           SOBER_PLACEID_RET_Z },
    { SOBER_PLACEID_GET_USERNAME,          SOBER_PLACEID_SIG_USERNAME,          SOBER_PLACEID_RET_STRING },
    { SOBER_PLACEID_GET_CONVERSATION_ID,   SOBER_PLACEID_SIG_CONVERSATION_ID,   SOBER_PLACEID_RET_J },
    { SOBER_PLACEID_GET_RESERVED_CODE,     SOBER_PLACEID_SIG_RESERVED_CODE,     SOBER_PLACEID_RET_STRING },
    { SOBER_PLACEID_GET_CALL_ID,           SOBER_PLACEID_SIG_CALL_ID,           SOBER_PLACEID_RET_STRING },
    { SOBER_PLACEID_GET_JOIN_REQUEST_TYPE, SOBER_PLACEID_SIG_JOIN_REQUEST_TYPE, SOBER_PLACEID_RET_I },
    { SOBER_PLACEID_GET_REFERRAL_PAGE,     SOBER_PLACEID_SIG_REFERRAL_PAGE,     SOBER_PLACEID_RET_STRING },
    { SOBER_PLACEID_GET_LAUNCH_DATA,       SOBER_PLACEID_SIG_LAUNCH_DATA,       SOBER_PLACEID_RET_STRING },
    { SOBER_PLACEID_GET_JOIN_CONTEXT,      SOBER_PLACEID_SIG_JOIN_CONTEXT,      SOBER_PLACEID_RET_STRING },
    { SOBER_PLACEID_GET_EVENT_ID,          SOBER_PLACEID_SIG_EVENT_ID,          SOBER_PLACEID_RET_STRING },
    { SOBER_PLACEID_GET_JOIN_ATTEMPT_ID,   SOBER_PLACEID_SIG_JOIN_ATTEMPT_ID,   SOBER_PLACEID_RET_STRING },
    { SOBER_PLACEID_GET_JOIN_ATTEMPT_ORIG, SOBER_PLACEID_SIG_JOIN_ATTEMPT_ORIG, SOBER_PLACEID_RET_STRING },
    { SOBER_PLACEID_GET_ISO_CONTEXT,       SOBER_PLACEID_SIG_ISO_CONTEXT,       SOBER_PLACEID_RET_STRING },
    { SOBER_PLACEID_GET_REFERRED_BY,       SOBER_PLACEID_SIG_REFERRED_BY,       SOBER_PLACEID_RET_J },
    { SOBER_PLACEID_GET_GAME_ID_EXCLUDE,   SOBER_PLACEID_SIG_GAME_ID_EXCLUDE,   SOBER_PLACEID_RET_STRING },
    { SOBER_PLACEID_GET_VR_CONTEXT,        SOBER_PLACEID_SIG_VR_CONTEXT,        SOBER_PLACEID_RET_OBJECT }
};

/* ---- module state -------------------------------------------------------- */
static int             g_registered;
static int             g_prepared;
static SoberJni       *g_handle_owner;
static void           *g_h[SOBER_PLACEID_GETTER_COUNT];
static volatile uint64_t g_calls[SOBER_PLACEID_GETTER_COUNT];
static volatile uint64_t g_dispatch;      /* placeId()J dispatches            */
static volatile unsigned g_installs;
static volatile unsigned g_claims;
static volatile unsigned g_sig_mismatch;
static volatile int      g_shadow;
static int64_t           g_value = SOBER_PLACEID_DEFAULT_VALUE;
static int               g_last_rc = SOBER_PLACEID_DEFAULT_DISABLED;

/* ---- kill switch / value ------------------------------------------------- */
int sober_placeid_default_enabled(void)
{
    const char *e = getenv(SOBER_PLACEID_DEFAULT_ENV);
    if (e && (strcmp(e, "0") == 0 || strcasecmp(e, "off") == 0 ||
              strcasecmp(e, "no") == 0 || strcasecmp(e, "false") == 0))
        return 0;
    return 1;
}

const char *sober_placeid_default_env_vars(void)
{
    return SOBER_PLACEID_DEFAULT_ENV " (kill switch), "
           SOBER_PLACEID_VALUE_ENV " (optional positive override)";
}

/* Parse the optional override; enforce placeId > 0 unconditionally. */
static int64_t pid_parse_value(void)
{
    const char *e = getenv(SOBER_PLACEID_VALUE_ENV);
    if (e && *e) {
        char *end = NULL;
        long long v;
        errno = 0;
        v = strtoll(e, &end, 10);
        if (!errno && end != e && (!end || *end == '\0') && v > 0)
            return (int64_t)v;
    }
    return (int64_t)SOBER_PLACEID_DEFAULT_VALUE;
}

static int64_t pid_value_now(void)
{
    return g_value > 0 ? g_value : (int64_t)SOBER_PLACEID_DEFAULT_VALUE;
}

int64_t sober_placeid_default_value(void)
{
    return g_registered ? pid_value_now() : pid_parse_value();
}

/* ---- getter table helpers ------------------------------------------------ */
static int pid_find(const char *name)
{
    if (!name) return -1;
    for (int i = 0; i < SOBER_PLACEID_GETTER_COUNT; ++i)
        if (strcmp(s_getters[i].name, name) == 0) return i;
    return -1;
}

const SoberPlaceidGetter *sober_placeid_default_getters(size_t *count)
{
    if (count) *count = (size_t)SOBER_PLACEID_GETTER_COUNT;
    return s_getters;
}

/* Non-placeId scalar values.  All are machine integers, never jobjects. */
static intptr_t pid_scalar(int idx)
{
    switch (idx) {
    case PID_PLACE_ID:      return (intptr_t)pid_value_now();   /* J, > 0   */
    case PID_USER_ID:       return (intptr_t)1;                 /* J         */
    case PID_CONVERSATION_ID:   return (intptr_t)0;              /* J         */
    case PID_REFERRED_BY:   return (intptr_t)0;                  /* J         */
    case PID_UNDER13:       return (intptr_t)0;                  /* Z (false) */
    case PID_JOIN_REQUEST_TYPE: return (intptr_t)0;               /* I         */
    default:                return (intptr_t)0;
    }
}

static const char *pid_string_default(int idx)
{
    switch (idx) {
    case PID_GAME_ID:  return "123456789";
    case PID_USERNAME: return "SoberUser";
    default:           return "";          /* builder defaults for optionals */
    }
}

static const char *pid_object_class(int idx)
{
    switch (idx) {
    case PID_SURFACE:    return "android/view/Surface";
    case PID_PLATFORM:   return "com/roblox/engine/jni/model/PlatformParams";
    case PID_VR_CONTEXT: return "android/app/Activity";
    default:             return "java/lang/Object";
    }
}

/* ---- the ONE getter handler used by all 24 names ------------------------- */
static intptr_t pid_get(void *env, void *obj, const char *name,
                        const char *sig, void *args, void *user)
{
    (void)env; (void)obj; (void)args; (void)user;
    int idx = pid_find(name);
    if (idx < 0) return 0;

    g_calls[idx]++;
    if (sig && sig[0] && strcmp(sig, s_getters[idx].sig) != 0)
        g_sig_mismatch++;

    if (idx == PID_PLACE_ID) {
        g_dispatch++;
        return pid_scalar(idx);              /* full jlong; > 0; no jstring */
    }
    switch (s_getters[idx].kind) {
    case SOBER_PLACEID_RET_J:
    case SOBER_PLACEID_RET_Z:
    case SOBER_PLACEID_RET_I:
        return pid_scalar(idx);              /* primitives: machine integers */
    case SOBER_PLACEID_RET_DEVICE:
        return (intptr_t)0;                  /* builder sets deviceParams null */
    case SOBER_PLACEID_RET_STRING:
    case SOBER_PLACEID_RET_OBJECT:
        return (intptr_t)g_h[idx];           /* cached, typed handle          */
    default:
        return (intptr_t)0;
    }
}

/* ---- preparation / claiming --------------------------------------------- */
static int pid_prepare(SoberJni *j)
{
    if (!j) return SOBER_JNI_ERR;
    if (g_prepared && g_handle_owner == j) return SOBER_JNI_OK;

    for (int i = 0; i < SOBER_PLACEID_GETTER_COUNT; ++i) {
        g_h[i] = NULL;
        if (s_getters[i].kind == SOBER_PLACEID_RET_STRING)
            g_h[i] = sober_jni_new_string_utf8(j, pid_string_default(i));
        else if (s_getters[i].kind == SOBER_PLACEID_RET_OBJECT)
            g_h[i] = sober_jni_new_native_object(j, pid_object_class(i), 0x40, NULL);
    }
    g_prepared = 1;
    g_handle_owner = j;
    return SOBER_JNI_OK;
}

static int pid_claim(SoberJni *j)
{
    if (!j) return SOBER_JNI_ERR;
    int ok = 0;
    /* set_callback_handler() replaces by name in native_jni.c, so the last
     * claim wins: no legacy four-getter handler can shadow ours. */
    for (int i = 0; i < SOBER_PLACEID_GETTER_COUNT; ++i)
        if (sober_jni_set_callback_handler(j, s_getters[i].name, pid_get, NULL)
            == SOBER_JNI_OK)
            ok++;
    g_claims++;
    return ok == SOBER_PLACEID_GETTER_COUNT ? SOBER_JNI_OK : SOBER_JNI_ERR;
}

int sober_placeid_default_install(SoberJni *j, void *guest_base)
{
    if (!sober_placeid_default_enabled()) {
        g_last_rc = SOBER_PLACEID_DEFAULT_DISABLED;
        fprintf(stderr, "[placeid-default] disabled (%s=0); handlers untouched\n",
                SOBER_PLACEID_DEFAULT_ENV);
        return SOBER_PLACEID_DEFAULT_DISABLED;
    }
    if (!j) {
        g_last_rc = SOBER_PLACEID_DEFAULT_ERR;
        return SOBER_PLACEID_DEFAULT_ERR;
    }

    g_value = pid_parse_value();
    if (pid_prepare(j) != SOBER_JNI_OK) {
        g_last_rc = SOBER_PLACEID_DEFAULT_ERR;
        return SOBER_PLACEID_DEFAULT_ERR;
    }
    int rc = pid_claim(j);
    if (rc == SOBER_JNI_OK) {
        g_registered = 1;
        g_installs++;
    }
    g_last_rc = rc == SOBER_JNI_OK ? SOBER_PLACEID_DEFAULT_OK : SOBER_PLACEID_DEFAULT_ERR;
    (void)guest_base;               /* the cached global is read, never written */

    if (g_last_rc == SOBER_PLACEID_DEFAULT_OK)
        fprintf(stderr,
                "[placeid-default] DEFAULT-ON: claimed %d/%d getters with exact "
                "smali sigs; placeId()J -> jlong %lld (>0, no jstring); "
                "getter site 0x%x, store 0x%x, cached global 0x%x\n",
                SOBER_PLACEID_GETTER_COUNT, SOBER_PLACEID_GETTER_COUNT,
                (long long)pid_value_now(),
                SOBER_PLACEID_RVA_GETTER_SITE,
                SOBER_PLACEID_RVA_HELPER_STORE, SOBER_PLACEID_RVA_GLOBAL);
    return g_last_rc;
}

int sober_placeid_default_reclaim(SoberJni *j)
{
    if (!sober_placeid_default_enabled()) return SOBER_PLACEID_DEFAULT_DISABLED;
    if (!j) return SOBER_PLACEID_DEFAULT_ERR;
    if (!g_prepared || g_handle_owner != j)
        return sober_placeid_default_install(j, NULL);
    int rc = pid_claim(j);
    if (rc == SOBER_JNI_OK) g_registered = 1;
    return rc == SOBER_JNI_OK ? SOBER_PLACEID_DEFAULT_OK : SOBER_PLACEID_DEFAULT_ERR;
}

int sober_placeid_default_is_registered(void)
{
    return g_registered && sober_placeid_default_enabled();
}

/* ---- verification accessors ---------------------------------------------- */
int64_t sober_placeid_default_cached_global(void *guest_base)
{
    int64_t v = 0;
    if (!guest_base) return 0;
    memcpy(&v, (const unsigned char *)guest_base + SOBER_PLACEID_RVA_GLOBAL,
           sizeof(v));
    return v;
}

int sober_placeid_default_cached_ok(void *guest_base)
{
    if (!guest_base) return 0;
    int64_t cur = sober_placeid_default_cached_global(guest_base);
    return cur > 0 && cur == pid_value_now();
}

uint64_t sober_placeid_default_dispatch_count(void) { return g_dispatch; }

uint64_t sober_placeid_default_getter_calls(const char *name)
{
    int idx = pid_find(name);
    return idx < 0 ? 0 : g_calls[idx];
}

unsigned sober_placeid_default_install_count(void) { return g_installs; }
unsigned sober_placeid_default_claim_count(void) { return g_claims; }
unsigned sober_placeid_default_sig_mismatches(void) { return g_sig_mismatch; }
int sober_placeid_default_last_rc(void) { return g_last_rc; }

/* ---- static verification ------------------------------------------------- */
static int pid_table_sane(char *buf, size_t cap)
{
    int ok = 1;
    for (int i = 0; i < SOBER_PLACEID_GETTER_COUNT; ++i) {
        if (!s_getters[i].name || !s_getters[i].sig || !s_getters[i].sig[0]) {
            ok = 0;
            break;
        }
        for (int k = i + 1; k < SOBER_PLACEID_GETTER_COUNT; ++k)
            if (strcmp(s_getters[i].name, s_getters[k].name) == 0) ok = 0;
    }
    if (strcmp(s_getters[PID_PLACE_ID].name, SOBER_PLACEID_GET_PLACE_ID) != 0 ||
        strcmp(s_getters[PID_PLACE_ID].sig, SOBER_PLACEID_SIG_PLACE_ID) != 0 ||
        s_getters[PID_PLACE_ID].kind != SOBER_PLACEID_RET_J)
        ok = 0;
    if (buf && cap)
        (void)snprintf(buf, cap, "table=%s (24 unique names, placeId sig=%s kind=J)",
                       ok ? "OK" : "BAD", s_getters[PID_PLACE_ID].sig);
    return ok;
}

int sober_placeid_default_verify(SoberJni *j, void *guest_base,
                                 char *buf, size_t cap)
{
    char scratch[512];
    char table[160];
    int rc = SOBER_PLACEID_DEFAULT_OK;
    unsigned shadows = 0;
    int table_ok = pid_table_sane(table, sizeof(table));

    if (!sober_placeid_default_enabled()) {
        rc = SOBER_PLACEID_DEFAULT_DISABLED;
    } else if (!g_registered || !table_ok) {
        rc = SOBER_PLACEID_DEFAULT_ERR;
    } else {
        if (j) {
            for (int i = 0; i < SOBER_PLACEID_GETTER_COUNT; ++i)
                if (sober_jni_is_native_registered(j, NULL, s_getters[i].name))
                    shadows++;
        }
        g_shadow = (int)shadows;
        if (shadows) rc = SOBER_PLACEID_DEFAULT_SHADOWED;
        else if (g_sig_mismatch) rc = SOBER_PLACEID_DEFAULT_VERIFY;
        else if (guest_base && !sober_placeid_default_cached_ok(guest_base))
            rc = SOBER_PLACEID_DEFAULT_VERIFY;
    }
    g_last_rc = rc;

    (void)snprintf(scratch, sizeof(scratch),
                   "placeid-default verify: %s; %s; registered=%d shadows=%u "
                   "sig_mismatch=%u dispatch=%llu value=%lld cached(0x%x)=%lld "
                   "cached_ok=%d -> rc=%d",
                   sober_placeid_default_enabled() ? "enabled" : "DISABLED",
                   table, g_registered, shadows, (unsigned)g_sig_mismatch,
                   (unsigned long long)g_dispatch,
                   (long long)pid_value_now(),
                   SOBER_PLACEID_RVA_GLOBAL,
                   (long long)sober_placeid_default_cached_global(guest_base),
                   sober_placeid_default_cached_ok(guest_base), rc);
    if (buf && cap) {
        size_t n = strlen(scratch);
        if (n >= cap) n = cap - 1;
        memcpy(buf, scratch, n);
        buf[n] = '\0';
    }
    return rc;
}

int sober_placeid_default_verify_live(SoberJni *j, void *guest_base,
                                      char *buf, size_t cap)
{
    int rc = sober_placeid_default_verify(j, guest_base, buf, cap);
    if (rc == SOBER_PLACEID_DEFAULT_OK &&
        (g_dispatch == 0 || !sober_placeid_default_cached_ok(guest_base)))
        rc = SOBER_PLACEID_DEFAULT_VERIFY;
    return rc;
}

/* ---- probe (no bridge JNIEnv, no bridge counters) ------------------------ */
int sober_placeid_default_probe(char *buf, size_t cap)
{
    uint64_t saved_calls[SOBER_PLACEID_GETTER_COUNT];
    uint64_t saved_dispatch = g_dispatch;
    int bad = 0;

    for (int i = 0; i < SOBER_PLACEID_GETTER_COUNT; ++i)
        saved_calls[i] = g_calls[i];

    for (int i = 0; i < SOBER_PLACEID_GETTER_COUNT; ++i) {
        intptr_t r = pid_get(NULL, NULL, s_getters[i].name, s_getters[i].sig,
                             NULL, NULL);
        switch (s_getters[i].kind) {
        case SOBER_PLACEID_RET_J:
            if (r <= 0 && i == PID_PLACE_ID) bad++;
            if (i != PID_PLACE_ID && r < 0) bad++;
            break;
        case SOBER_PLACEID_RET_Z:
        case SOBER_PLACEID_RET_I:
            if (r != 0) bad++;
            break;
        case SOBER_PLACEID_RET_DEVICE:
            if (r != 0) bad++;                   /* genuine NULL, no jstring */
            break;
        case SOBER_PLACEID_RET_STRING:
        case SOBER_PLACEID_RET_OBJECT:
            if (!r) bad++;                       /* never hand guest NULL     */
            break;
        default:
            bad++;
            break;
        }
    }

    for (int i = 0; i < SOBER_PLACEID_GETTER_COUNT; ++i)
        g_calls[i] = saved_calls[i];
    g_dispatch = saved_dispatch;

    if (buf && cap)
        (void)snprintf(buf, cap,
                       "placeid-default probe: %d/%d correct; "
                       "placeId()J=%lld (>0); no jstring on J/Z/I; "
                       "counters restored (no bridge callback)",
                       SOBER_PLACEID_GETTER_COUNT - bad,
                       SOBER_PLACEID_GETTER_COUNT,
                       (long long)pid_value_now());
    return bad ? SOBER_PLACEID_DEFAULT_VERIFY : SOBER_PLACEID_DEFAULT_OK;
}

/* ---- end-to-end selftest through the real CallLongMethodV slot ----------- */
typedef int64_t (*pid_call_long_v_fn)(void *, void *, void *, va_list);

/* Local variadic wrapper: builds the empty va_list that JNI CallLongMethodV
 * receives for a no-argument getter, then calls fn[49] of the bridge table. */
static int64_t pid_invoke_long_v(void *env, void *obj, void *mid, ...)
{
    va_list ap;
    va_start(ap, mid);
    void *functions = *(void **)env;                 /* JNINativeInterface_ */
    void **slots = (void **)functions;               /* 4 reserved + fn[231] */
    pid_call_long_v_fn fn =
        (pid_call_long_v_fn)(uintptr_t)
            slots[4u + SOBER_PLACEID_JNI_FN_CALL_LONG_V];
    int64_t r = fn ? fn(env, obj, mid, ap) : 0;
    va_end(ap);
    return r;
}

int sober_placeid_default_selftest(SoberJni *j, char *buf, size_t cap)
{
    if (!j) {
        if (buf && cap) (void)snprintf(buf, cap, "placeid-default selftest: bad arg");
        return SOBER_PLACEID_DEFAULT_ERR;
    }
    if (!g_registered) {
        if (buf && cap)
            (void)snprintf(buf, cap,
                           "placeid-default selftest: not installed (rc=%d)",
                           g_last_rc);
        return SOBER_PLACEID_DEFAULT_ERR;
    }
    void *cls = sober_jni_find_class(j, "com/roblox/engine/jni/autovalue/StartGameParams");
    void *mid = sober_jni_get_method_id(j, cls, SOBER_PLACEID_GET_PLACE_ID,
                                         SOBER_PLACEID_SIG_PLACE_ID);
    void *env = sober_jni_env(j);
    int64_t got = 0;
    if (cls && mid && env)
        got = pid_invoke_long_v(env, NULL, mid);

    int ok = (got == pid_value_now() && got > 0);
    if (buf && cap)
        (void)snprintf(buf, cap,
                       "placeid-default selftest: CallLongMethodV(+0x%lx/fn[%u]) "
                       "placeId()J -> %lld (expect %lld, >0) %s "
                       "[DIAGNOSTIC: dispatches one bridge callback]",
                       (unsigned long)SOBER_PLACEID_JNI_OFF_CALL_LONG_V,
                       SOBER_PLACEID_JNI_FN_CALL_LONG_V,
                       (long long)got, (long long)pid_value_now(),
                       ok ? "PASS" : "FAIL");
    return ok ? SOBER_PLACEID_DEFAULT_OK : SOBER_PLACEID_DEFAULT_VERIFY;
}

/* ---- diagnostics --------------------------------------------------------- */
const char *sober_placeid_default_describe(void *guest_base, char *buf, size_t cap)
{
    if (!buf || cap == 0) return "";
    (void)snprintf(buf, cap,
                   "enabled=%d registered=%d installs=%u claims=%u "
                   "dispatch=%llu sig_mismatch=%u shadow=%d value=%lld "
                   "cached(0x%x)=%lld cached_ok=%d "
                   "site=0x%x store=0x%x global=0x%x callLongV=0x%x/fn[%u]",
                   sober_placeid_default_enabled(), g_registered,
                   g_installs, g_claims, (unsigned long long)g_dispatch,
                   (unsigned)g_sig_mismatch, g_shadow,
                   (long long)pid_value_now(),
                   SOBER_PLACEID_RVA_GLOBAL,
                   (long long)sober_placeid_default_cached_global(guest_base),
                   sober_placeid_default_cached_ok(guest_base),
                   SOBER_PLACEID_RVA_GETTER_SITE,
                   SOBER_PLACEID_RVA_HELPER_STORE, SOBER_PLACEID_RVA_GLOBAL,
                   SOBER_PLACEID_RVA_CALL_LONG_V,
                   SOBER_PLACEID_JNI_FN_CALL_LONG_V);
    return buf;
}

void sober_placeid_default_reset(void)
{
    for (int i = 0; i < SOBER_PLACEID_GETTER_COUNT; ++i) g_calls[i] = 0;
    g_dispatch = 0;
    g_installs = 0;
    g_claims = 0;
    g_sig_mismatch = 0;
    g_shadow = 0;
    g_last_rc = SOBER_PLACEID_DEFAULT_DISABLED;
    /* g_registered / g_prepared / g_value are intentionally retained. */
}
