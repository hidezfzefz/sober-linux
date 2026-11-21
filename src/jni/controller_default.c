/*
 * controller_default.c - Team 2 / T2-3.  See controller_default.h for the full
 * evidence chain, the frozen-engine RVA set and the exact wiring signature.
 *
 * HARD RULES OBSERVED HERE
 * ------------------------
 *  - DEFAULT ON; disabled ONLY by SOBER_CONTROLLER_DEFAULT=0/off/no/false;
 *  - no guest CODE byte is patched, no guest->Java callback is emitted, the
 *    decision-013 marker is never touched;
 *  - the FFlag bit is set by the guest's own setter 0x23648be, never by a raw
 *    byte write from this module;
 *  - the controller is installed only through the genuine vtable dispatch at
 *    0x23389f7 -> 0x24ad50a -> 0x24ae1fe -> store 0x24aea9c.  We never call
 *    0x24ae1fe, 0x24aea9c or 0x24ad50a directly;
 *  - the only host-side allocation is a transient 0x28-byte argument container
 *    (guest-allocated, no vtable, never published as guest state) that mirrors
 *    the guest's own stack slot at 0x24ac620;
 *  - verification reads only genuine guest state.
 */
#define _GNU_SOURCE
#include "controller_default.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#define CD_ENV       SOBER_CONTROLLER_DEFAULT_ENV
#define CD_VERBOSE   SOBER_CONTROLLER_DEFAULT_VERBOSE_ENV
#define CD_PROBE     SOBER_CONTROLLER_DEFAULT_PROBE_ENV
#define CD_WAIT      SOBER_CONTROLLER_DEFAULT_WAIT_ENV
#define CD_STRICT    SOBER_CONTROLLER_DEFAULT_STRICT_ENV

/* Optional sibling default modules.  Weak so that this translation unit links
 * even when the lead has not wired them yet; when present they are the genuine
 * producers of the two other prerequisites. */
extern int sober_fflag_default_replay(void *guest_base)
    __attribute__((weak));
extern int sober_singleton_default_install_flags(void *guest_base, unsigned flags)
    __attribute__((weak));
extern int sober_singleton_default_install(void *guest_base)
    __attribute__((weak));

/* Guest short-string encoding used by the flag setter (0x23649a4..0x23649bf):
 * bit0=1, length at +0x08, char pointer at +0x10. */
struct cd_shortstr {
    uint8_t  flag;
    uint8_t  pad[7];
    uint64_t len;
    const char *ptr;
};

/* 0x28-byte transient argument container: +0x20 = app-bridge object.  This is
 * exactly the shape the guest builds at 0x24ac620 (rbp-0x110) before calling
 * the genuine registration helper 0x2eb6b2a. */
#define CD_WRAPPER_SIZE 0x28u

static int cd_env_off(const char *name)
{
    const char *v = getenv(name);
    return v && (v[0] == '0' || !strcasecmp(v, "off") ||
                 !strcasecmp(v, "no") || !strcasecmp(v, "false"));
}

static int cd_env_truthy(const char *name)
{
    const char *v = getenv(name);
    return v && v[0] != '0' && strcasecmp(v, "off") &&
           strcasecmp(v, "no") && strcasecmp(v, "false");
}

static int cd_verbose(void) { return getenv(CD_VERBOSE) != NULL; }
#define CDV(...) do { if (cd_verbose()) \
    fprintf(stderr, "[cd] " __VA_ARGS__); } while (0)

static unsigned long cd_dispatches;
static unsigned long cd_verifications;
static int            cd_last_rc = SOBER_CD_OK;
static unsigned       cd_wait_ms = 200u;

int sober_controller_default_enabled(void)
{
    return !cd_env_off(CD_ENV);
}

const char *sober_controller_default_env_vars(void)
{
    return CD_ENV "," CD_VERBOSE "," CD_PROBE "," CD_WAIT "," CD_STRICT;
}

void sober_controller_default_reset(void)
{
    cd_dispatches = 0;
    cd_verifications = 0;
    cd_last_rc = SOBER_CD_OK;
    cd_wait_ms = 200u;
}

uint64_t sober_controller_default_dispatches(void) { return cd_dispatches; }
uint64_t sober_controller_default_verifications(void) { return cd_verifications; }
int sober_controller_default_last_rc(void) { return cd_last_rc; }

int sober_controller_default_should_withhold(void)
{
    return sober_controller_default_enabled() && cd_dispatches != 0;
}

static int cd_wait_budget_ms(void)
{
    const char *v = getenv(CD_WAIT);
    unsigned long n;
    if (!v || !*v) return 200;
    n = strtoul(v, NULL, 10);
    if (n == 0 || n > 5000) return 200;
    return (unsigned)n;
}

void *sober_controller_default_app(void *guest_base)
{
    if (!guest_base) return NULL;
    unsigned char *b = (unsigned char *)guest_base;
    /* 0x24ad6fc returns base+0x74d75a0; its +0x20 holds app.  A direct read
     * is equivalent and performs no guest call when the latch is unset. */
    return *(void **)(void *)(b + SOBER_CD_RVA_STATIC_WRAPPER +
                             SOBER_CD_OFF_APP);
}

void *sober_controller_default_controller(void *guest_base)
{
    void *app = sober_controller_default_app(guest_base);
    if (!app) return NULL;
    return *(void **)((unsigned char *)app + SOBER_CD_OFF_CONTROLLER);
}

void *sober_controller_default_control(void *guest_base)
{
    void *app = sober_controller_default_app(guest_base);
    if (!app) return NULL;
    return *(void **)((unsigned char *)app + SOBER_CD_OFF_CONTROL);
}

uintptr_t sober_controller_default_controller_vptr(void *guest_base)
{
    void *ctl = sober_controller_default_controller(guest_base);
    if (!ctl) return 0;
    return *(uintptr_t *)ctl;
}

/* ---- prerequisite 1: FFlag 0x72cb648 == 1 via the guest's own setter ----- */
int sober_controller_default_ensure_flag(void *guest_base)
{
    if (!sober_controller_default_enabled()) return SOBER_CD_DISABLED;
    if (!guest_base) return SOBER_CD_ERR;
    unsigned char *b = (unsigned char *)guest_base;
    volatile unsigned char *flag =
        (volatile unsigned char *)(b + SOBER_CD_RVA_FLAG_BYTE);
    if (*flag) return SOBER_CD_OK;

    /* The flag registry ctor (init_array[0] 0x2e62390) must have installed the
     * registry; replay it through the sibling default module when available. */
    if (sober_fflag_default_replay)
        (void)sober_fflag_default_replay(guest_base);

    void *reg = *(void **)(void *)(b + SOBER_CD_RVA_FLAG_REGISTRY);
    if (!reg) {
        CDV("flag registry @0x%llx is NULL\n",
            (unsigned long long)SOBER_CD_RVA_FLAG_REGISTRY);
        return SOBER_CD_NO_FLAG;
    }

    /* Exact setter invocation from the guest's JSON member loop 0x237df3f..
     * 0x237df9c: name "EnableAppBridgeComponents", value "True", len 1,
     * type 4 (bool).  The short strings are read-only data supplied by us;
     * the GUEST setter writes the FFlag byte. */
    struct cd_shortstr name, value;
    memset(&name, 0, sizeof(name));
    memset(&value, 0, sizeof(value));
    name.flag = 1;
    name.len = strlen("EnableAppBridgeComponents");
    name.ptr = "EnableAppBridgeComponents";
    value.flag = 1;
    value.len = strlen("True");
    value.ptr = "True";

    typedef int (*cd_set_flag_fn)(void *, void *, void *, unsigned,
                                  unsigned, unsigned);
    cd_set_flag_fn set_flag =
        (cd_set_flag_fn)(uintptr_t)(b + SOBER_CD_RVA_FLAG_SETTER);
    int rc = set_flag(reg, &name, &value, 1u, 4u, 0u);
    CDV("guest flag setter 0x%x -> rc=%d flag=%u\n",
        SOBER_CD_RVA_FLAG_SETTER, rc, (unsigned)*flag);
    return *flag ? SOBER_CD_OK : SOBER_CD_NO_FLAG;
}

/* ---- prerequisite 2: app singleton + populated service vector ----------- */
int sober_controller_default_ensure_singleton(void *guest_base)
{
    if (!sober_controller_default_enabled()) return SOBER_CD_DISABLED;
    if (!guest_base) return SOBER_CD_ERR;
    unsigned char *b = (unsigned char *)guest_base;

    if (getenv(CD_PROBE) && cd_env_off(CD_PROBE)) {
        /* caller explicitly disabled the accessor probe: presence is enough */
        if (*(void **)(void *)(b + SOBER_CD_RVA_APP_SINGLETON))
            return SOBER_CD_OK;
    } else {
        typedef void *(*cd_accessor_fn)(void);
        cd_accessor_fn accessor =
            (cd_accessor_fn)(uintptr_t)(b + SOBER_CD_RVA_ACCESSOR);
        void *service = accessor ? accessor() : NULL;
        if (*(void **)(void *)(b + SOBER_CD_RVA_APP_SINGLETON) && service) {
            CDV("singleton live; accessor 0x%x -> %p\n",
                SOBER_CD_RVA_ACCESSOR, service);
            return SOBER_CD_OK;
        }
    }

    /* Delegate to the sibling default module: it creates the 0xa8 object with
     * the genuine guest allocator/ctor and installs a guest-shaped service
     * vector, then verifies with the same accessor. */
    int rc = SOBER_CD_NO_SINGLETON;
    if (sober_singleton_default_install_flags)
        rc = sober_singleton_default_install_flags(guest_base, 0u);
    else if (sober_singleton_default_install)
        rc = sober_singleton_default_install(guest_base);
    CDV("singleton_default -> rc=%d\n", rc);

    if (*(void **)(void *)(b + SOBER_CD_RVA_APP_SINGLETON))
        return SOBER_CD_OK;
    return SOBER_CD_NO_SINGLETON;
}

/* ---- prerequisite 3: find the genuine 0xe0 app-bridge object ------------ */
void *sober_controller_default_find_bridge(void *guest_base)
{
    if (!guest_base) return NULL;
    unsigned char *b = (unsigned char *)guest_base;

    /* nativeGameGlobalInit worker deque (0x23387b6/0x23380fe):
     *   map[i>>9][(i & 0x1ff)] == task, task+0x20 == app-bridge object.
     * Read-only scan; the object's vptr must be the genuine 0x6dbc7c8. */
    uintptr_t map = *(uintptr_t *)(void *)(b + SOBER_CD_RVA_TASK_MAP);
    uintptr_t first = *(uintptr_t *)(void *)(b + SOBER_CD_RVA_TASK_BASE);
    uintptr_t count = *(uintptr_t *)(void *)(b + SOBER_CD_RVA_TASK_COUNT);
    uintptr_t expect = (uintptr_t)(b + SOBER_CD_RVA_BRIDGE_VT);
    if (!map || !count || count > (1u << 20)) return NULL;

    for (uintptr_t i = 0; i < count; ++i) {
        uintptr_t idx = first + i;
        uintptr_t chunk = *(uintptr_t *)(uintptr_t)(map + ((idx >> 9) * 8u));
        if (!chunk) continue;
        uintptr_t task =
            *(uintptr_t *)(uintptr_t)(chunk + ((idx & 0x1ffu) * 8u));
        if (!task) continue;
        uintptr_t obj = *(uintptr_t *)(uintptr_t)(task + SOBER_CD_OFF_APP);
        if (obj && *(uintptr_t *)(uintptr_t)obj == expect) {
            CDV("bridge object %p (task %p, index %llu) vptr %p\n",
                (void *)obj, (void *)task, (unsigned long long)i,
                (void *)expect);
            return (void *)obj;
        }
    }

    /* Secondary registration vector used by the lazy bridge 0x23376d6:
     * array at 0x72cee68, element count at 0x72cee70; entry+0x20 is the
     * wrapper holding the app-bridge object. */
    uintptr_t arr = *(uintptr_t *)(void *)(b + 0x72cee68u);
    uintptr_t n = *(uintptr_t *)(void *)(b + 0x72cee70u);
    if (arr && n && n < (1u << 20)) {
        for (uintptr_t i = 0; i < n; ++i) {
            uintptr_t entry = *(uintptr_t *)(uintptr_t)(arr + i * 8u);
            if (!entry) continue;
            uintptr_t obj = *(uintptr_t *)(uintptr_t)(entry + SOBER_CD_OFF_APP);
            if (obj && *(uintptr_t *)(uintptr_t)obj == expect) {
                CDV("bridge object %p (registration vector %llu)\n",
                    (void *)obj, (unsigned long long)i);
                return (void *)obj;
            }
        }
    }
    return NULL;
}

/* ---- trigger: genuine registration + genuine worker dispatch ------------ */
int sober_controller_default_trigger(void *guest_base, void *bridge_object)
{
    if (!sober_controller_default_enabled()) return SOBER_CD_DISABLED;
    if (!guest_base) return SOBER_CD_ERR;
    unsigned char *b = (unsigned char *)guest_base;

    void *bridge = bridge_object ? bridge_object :
                   sober_controller_default_find_bridge(guest_base);
    if (!bridge) return SOBER_CD_NO_BRIDGE;

    /* The genuine +0xb0 shared_ptr is authored by InitWithParams at
     * 0x24ac5a9/0x24ac5c8.  Require a live pointer/control word; a zeroed slot
     * would mean InitWithParams never built the controller state. */
    uintptr_t sp = *(uintptr_t *)((unsigned char *)bridge +
                                 SOBER_CD_OFF_BRIDGE_SHARED);
    uintptr_t ctrl = *(uintptr_t *)((unsigned char *)bridge +
                                   SOBER_CD_OFF_BRIDGE_SHARED + 0x10u);
    CDV("bridge %p +0xb0=%p +0xc0=%p\n", bridge, (void *)sp, (void *)ctrl);
    if (!sp && !ctrl) return SOBER_CD_NO_BRIDGE;

    typedef void *(*cd_alloc_fn)(size_t);
    cd_alloc_fn alloc = (cd_alloc_fn)(uintptr_t)(b + 0x1dbab02u);
    void *wrapper = alloc ? alloc(CD_WRAPPER_SIZE) : NULL;
    if (!wrapper) return SOBER_CD_ERR;
    memset(wrapper, 0, CD_WRAPPER_SIZE);
    *(void **)((unsigned char *)wrapper + SOBER_CD_OFF_APP) = bridge;

    /* Genuine registration (0x2eb6b2a) → lazy bridge 0x2337cc8 → worker task.
     * The worker then executes `call *0x30(%rax)` at 0x23389f7, i.e. slot
     * 0x6dbc7f8 → 0x24ad50a → 0x24ae1fe → store 0x24aea9c. */
    typedef void (*cd_register_fn)(void *, unsigned);
    cd_register_fn reg =
        (cd_register_fn)(uintptr_t)(b + SOBER_CD_RVA_REGISTER_BRIDGE);
    if (!reg) return SOBER_CD_NO_DISPATCH;
    reg(wrapper, 0u);

    /* Wake the real nativeGameGlobalInit worker (pthread_cond_signal wrapper
     * 0x2e5a5c0 on CV 0x72cef78) so it processes the queued task. */
    typedef void (*cd_wake_fn)(void *);
    cd_wake_fn wake = (cd_wake_fn)(uintptr_t)(b + SOBER_CD_RVA_WORKER_WAKE);
    if (wake) wake(b + SOBER_CD_RVA_WORKER_CV);

    ++cd_dispatches;
    CDV("dispatched via 0x%x (worker wake 0x%x); total=%lu\n",
        SOBER_CD_RVA_DISPATCH_SITE, SOBER_CD_RVA_WORKER_WAKE, cd_dispatches);
    return SOBER_CD_OK;
}

/* ---- verification: genuine controller + genuine intrusive vptr ---------- */
int sober_controller_default_verify(void *guest_base, char *buf, size_t cap)
{
    ++cd_verifications;
    void *app = sober_controller_default_app(guest_base);
    if (!app) {
        if (buf && cap) snprintf(buf, cap, "app=NULL (wrapper+0x20)");
        return SOBER_CD_NO_APP;
    }
    if (!guest_base) return SOBER_CD_ERR;

    unsigned char *b = (unsigned char *)guest_base;
    cd_wait_ms = cd_wait_budget_ms();

    void *ctl = NULL;
    unsigned long steps = (cd_wait_ms + 4u) / 5u;
    for (unsigned long i = 0; i <= steps; ++i) {
        ctl = *(void **)((unsigned char *)app + SOBER_CD_OFF_CONTROLLER);
        if (ctl) break;
        struct timespec ts = {0, 5000000L}; /* 5 ms */
        nanosleep(&ts, NULL);
    }
    if (!ctl) {
        if (buf && cap) snprintf(buf, cap,
            "app=%p +0x440=NULL after %u ms (no genuine store 0x%x)",
            app, cd_wait_ms, SOBER_CD_RVA_GENUINE_STORE);
        return SOBER_CD_VERIFY;
    }

    uintptr_t expect = (uintptr_t)(b + SOBER_CD_RVA_CTRL_VPTR);
    uintptr_t cvt = *(uintptr_t *)ctl;
    void *control = *(void **)((unsigned char *)app + SOBER_CD_OFF_CONTROL);
    uintptr_t ivt = control ? *(uintptr_t *)control : 0;
    int direct = (cvt == expect);
    int intrusive = (ivt == expect);

    if (buf && cap)
        snprintf(buf, cap,
                 "app=%p +0x440=%p vptr=%p (expect %p) "
                 "+0x448=%p vptr=%p direct=%d intrusive=%d "
                 "dispatches=%lu",
                 app, ctl, (void *)cvt, (void *)expect,
                 control, (void *)ivt, direct, intrusive, cd_dispatches);

    /* Default: the genuine controller slot is live and the intrusive control
     * word written at 0x24aea8e..0x24aeaaa carries the genuine 0x6e02568 vptr.
     * Strict mode additionally requires that vptr on the slot itself. */
    if (direct) return SOBER_CD_OK;
    if (intrusive && !cd_env_truthy(CD_STRICT))
        return SOBER_CD_OK;
    return SOBER_CD_VERIFY;
}

/* ---- one-shot entry points ---------------------------------------------- */
static int cd_install_impl(void *guest_base, unsigned flags,
                           void *jni_env, void *jclass, void *init_params)
{
    if (!sober_controller_default_enabled()) {
        cd_last_rc = SOBER_CD_DISABLED;
        return cd_last_rc;
    }
    if (!guest_base) {
        cd_last_rc = SOBER_CD_ERR;
        return cd_last_rc;
    }
    unsigned char *b = (unsigned char *)guest_base;

    /* Idempotency: an already-populated slot short-circuits the trigger, but
     * verification below still requires the genuine 0x6e02568 vptr, so a fake
     * SOBER_SEED_USER_CONTROLLER object fails with SOBER_CD_VERIFY. */
    if (!(flags & SOBER_CD_F_FORCE) &&
        sober_controller_default_controller(guest_base)) {
        cd_last_rc = sober_controller_default_verify(guest_base, NULL, 0);
        CDV("controller already present; verify rc=%d\n", cd_last_rc);
        return cd_last_rc;
    }

    if (!(flags & SOBER_CD_F_SKIP_FLAG)) {
        int rc = sober_controller_default_ensure_flag(guest_base);
        if (rc != SOBER_CD_OK) {
            cd_last_rc = (rc == SOBER_CD_DISABLED) ? rc : SOBER_CD_NO_FLAG;
            return cd_last_rc;
        }
    }
    if (!(flags & SOBER_CD_F_SKIP_SINGLETON)) {
        unsigned sf = (flags & SOBER_CD_F_SKIP_PROBE) ? 0x2u : 0u;
        int rc = SOBER_CD_NO_SINGLETON;
        if (sober_singleton_default_install_flags)
            rc = sober_singleton_default_install_flags(guest_base, sf);
        else
            rc = sober_controller_default_ensure_singleton(guest_base);
        if (rc != SOBER_CD_OK) {
            cd_last_rc = SOBER_CD_NO_SINGLETON;
            return cd_last_rc;
        }
    }

    void *bridge = sober_controller_default_find_bridge(guest_base);
    if (bridge) {
        /* The controller installer needs this+0xb0 (a genuine shared_ptr
         * authored by InitWithParams at 0x24ac5a9/0x24ac5c8).  A bridge whose
         * shared_ptr is empty cannot produce a controller. */
        uintptr_t sp = *(uintptr_t *)((unsigned char *)bridge +
                                     SOBER_CD_OFF_BRIDGE_SHARED);
        uintptr_t sc = *(uintptr_t *)((unsigned char *)bridge +
                                     SOBER_CD_OFF_BRIDGE_SHARED + 0x10u);
        if (!sp && !sc) {
            CDV("bridge %p has an empty +0xb0 shared_ptr\n", bridge);
            bridge = NULL;
        }
    }
    if (!bridge && jni_env && jclass && init_params) {
        /* Re-enter the genuine JNI producer of the 0xe0 object and its +0xb0
         * shared_ptr.  InitWithParams itself registers the object via
         * 0x2eb6b2a; the explicit trigger below re-arms the worker. */
        typedef void (*cd_init_params_fn)(void *, void *, void *);
        cd_init_params_fn init =
            (cd_init_params_fn)(uintptr_t)
                (b + SOBER_CD_RVA_INIT_WITH_PARAMS);
        CDV("no bridge in deque; re-entering 0x%x with params %p\n",
            SOBER_CD_RVA_INIT_WITH_PARAMS, init_params);
        init(jni_env, jclass, init_params);
        bridge = sober_controller_default_find_bridge(guest_base);
    }
    if (!bridge) {
        cd_last_rc = SOBER_CD_NO_BRIDGE;
        return cd_last_rc;
    }

    int rc = sober_controller_default_trigger(guest_base, bridge);
    if (rc != SOBER_CD_OK) {
        cd_last_rc = rc;
        return rc;
    }
    cd_last_rc = sober_controller_default_verify(guest_base, NULL, 0);
    if (cd_last_rc == SOBER_CD_OK && (flags & SOBER_CD_F_STRICT_VPTR)) {
        uintptr_t expect = (uintptr_t)(b + SOBER_CD_RVA_CTRL_VPTR);
        if (sober_controller_default_controller_vptr(guest_base) != expect)
            cd_last_rc = SOBER_CD_VERIFY;
    }
    return cd_last_rc;
}

int sober_controller_default_install(void *guest_base)
{
    return cd_install_impl(guest_base, 0u, NULL, NULL, NULL);
}

int sober_controller_default_install_flags(void *guest_base, unsigned flags)
{
    return cd_install_impl(guest_base, flags, NULL, NULL, NULL);
}

int sober_controller_default_install_with_jni(void *guest_base,
                                              void *jni_env, void *jclass,
                                              void *init_params,
                                              unsigned flags)
{
    return cd_install_impl(guest_base, flags, jni_env, jclass, init_params);
}

const char *sober_controller_default_describe(void *guest_base, char *buf,
                                              size_t cap)
{
    if (!buf || cap == 0) return "";
    if (!guest_base) { buf[0] = '\0'; return buf; }
    unsigned char *b = (unsigned char *)guest_base;
    void *bridge = sober_controller_default_find_bridge(guest_base);
    uintptr_t shared = 0, shared_ctrl = 0;
    if (bridge) {
        shared = *(uintptr_t *)((unsigned char *)bridge +
                                SOBER_CD_OFF_BRIDGE_SHARED);
        shared_ctrl = *(uintptr_t *)((unsigned char *)bridge +
                                     SOBER_CD_OFF_BRIDGE_SHARED + 0x10u);
    }
    void *ctl = sober_controller_default_controller(guest_base);
    void *control = sober_controller_default_control(guest_base);
    snprintf(buf, cap,
             "enabled=%d flag@0x%x=%u singleton=%p bridge=%p shared=%p/%p "
             "app=%p +0x440=%p vptr=%p +0x448=%p vptr=%p "
             "last_rc=%d dispatches=%lu",
             sober_controller_default_enabled(),
             SOBER_CD_RVA_FLAG_BYTE,
             (unsigned)*(volatile unsigned char *)(b + SOBER_CD_RVA_FLAG_BYTE),
             *(void **)(void *)(b + SOBER_CD_RVA_APP_SINGLETON),
             bridge, (void *)shared, (void *)shared_ctrl,
             sober_controller_default_app(guest_base),
             ctl, (void *)(ctl ? *(uintptr_t *)ctl : 0),
             control,
             (void *)(control ? *(uintptr_t *)control : 0),
             cd_last_rc, cd_dispatches);
    return buf;
}
