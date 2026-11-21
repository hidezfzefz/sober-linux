/*
 * frame_callbacks.c - genuine-guest-RVA nativeData callback slot population.
 *
 * F1 / group C.  NEW FILES ONLY: this file + frame_callbacks.h.  See the header
 * for scope, evidence, exact RVAs and the decision-013 honesty statement.
 *
 * HARD RULES HONOURED HERE
 *  - Every value written into nativeData+0x88/+0x90/+0x98/+0xa0/+0xb8 is
 *    guest_base + a frozen guest RVA (Route B) or comes from the guest's OWN
 *    0x278-byte nativeData (Route A).  No host function pointer, no trampoline,
 *    no "counting stub" ever enters a callback slot.
 *  - No guest byte is patched: no mprotect, no PLT/GOT hook, no code write.
 *    We only allocate OUR nativeData and read guest globals.
 *  - No JNI callback is fabricated and no decision-013 marker is emitted.
 *  - DEFAULT ON; getenv("SOBER_FRAME_CALLBACKS") == "0" => inert no-op.
 */
#define _GNU_SOURCE
#include "frame_callbacks.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- frozen guest prototypes -------------------------------------------- */
typedef uintptr_t (*fc_init_native_fn)(void *, void *, void *, void *, void *,
                                       void *, void *, void *);
typedef void (*fc_life_fn)(void *, void *, void *, void *); /* env,this,nd,surf */

/* ---- module state -------------------------------------------------------- */
static uintptr_t  s_base;
static unsigned char *s_nd;          /* managed nativeData (0x278 bytes)       */
static int        s_nd_guest_owned;  /* Route A: do not free                   */
static int        s_installed;
static int        s_route      = SOBER_FC_ROUTE_NONE;
static int        s_last_rc    = SOBER_FRAME_CALLBACKS_DISABLED;
static void      *s_env;
static void      *s_window;          /* preferred guest-bound window           */
static int        s_width      = 1280;
static int        s_height     = 720;
static uintptr_t  s_cb[5];           /* created/changed/redraw/destroyed/config */

static _Atomic uint64_t s_installs;
static _Atomic uint64_t s_verifies;
static _Atomic uint64_t s_verify_ok;
static _Atomic uint64_t s_route_guest;
static _Atomic uint64_t s_route_synth;
static _Atomic uint64_t s_created;
static _Atomic uint64_t s_changed;
static _Atomic uint64_t s_redraw;
static _Atomic uint64_t s_destroyed;
static _Atomic uint64_t s_window_sets;
static _Atomic uint64_t s_window_restores;

static void fc_bump(_Atomic uint64_t *c) {
    __atomic_fetch_add(c, 1u, __ATOMIC_SEQ_CST);
}

/* ---- small helpers ------------------------------------------------------- */
static int fc_env_int(const char *name, int fallback) {
    const char *e = getenv(name);
    if (e && *e) {
        char *end = NULL;
        long v = strtol(e, &end, 10);
        if (end != e && v > 0 && v < 0x100000) return (int)v;
    }
    return fallback;
}

/* A guest code address is inside [base, base+text_size). */
static int fc_in_guest_text(uintptr_t p) {
    if (!s_base || !p) return 0;
    return p >= s_base && (p - s_base) < (uintptr_t)SOBER_FC_TEXT_SIZE;
}

static uintptr_t fc_slot(uintptr_t off) {
    return s_nd ? *(volatile uintptr_t *)(s_nd + (size_t)off) : 0u;
}
static void fc_set_slot(uintptr_t off, uintptr_t val) {
    if (s_nd) *(volatile uintptr_t *)(s_nd + (size_t)off) = val;
}

/* Read the guest-bound window published by the genuine fromSurface helper. */
static uintptr_t fc_guest_window_global(void) {
    if (!s_base) return 0u;
    return *(volatile uintptr_t *)(s_base
                                   + (uintptr_t)SOBER_FC_G_ANIVE_WINDOW);
}

/* ---- gate ---------------------------------------------------------------- */
int sober_frame_callbacks_enabled(void) {
    const char *e = getenv(SOBER_FRAME_CALLBACKS_ENV);
    return !(e && strcmp(e, "0") == 0);
}

const char *sober_frame_callbacks_env_vars(void) {
    return SOBER_FRAME_CALLBACKS_ENV "=0 (kill switch; default ON) ; "
           SOBER_FRAME_CALLBACKS_GUEST_ENV "=0 (skip Route A guest nativeData)";
}

/* ---- install ------------------------------------------------------------- */
/* Route A: drive the guest's own 0x278-byte nativeData constructor.  We call
 * the real body at 0x2a1fe80 with the full static-JNI signature used by Java
 * (env, clazz, String x3, AssetManager, byte[], Configuration) -> jlong.  The
 * returned object is adopted ONLY if its window callbacks are all non-NULL
 * guest code addresses.  Best-effort: any failure falls back to Route B. */
static uintptr_t fc_try_guest_native_data(
    const struct sober_frame_callbacks_config *cfg) {
    static const uintptr_t offs[4] = {
        SOBER_FC_ND_OFF_CREATED, SOBER_FC_ND_OFF_CHANGED,
        SOBER_FC_ND_OFF_REDRAW,  SOBER_FC_ND_OFF_DESTROYED
    };
    fc_init_native_fn fn;
    uintptr_t nd;
    unsigned i;

    if (!cfg->env || !cfg->prefer_guest_native_data) return 0u;
    if (getenv(SOBER_FRAME_CALLBACKS_GUEST_ENV)) {
        const char *e = getenv(SOBER_FRAME_CALLBACKS_GUEST_ENV);
        if (e && strcmp(e, "0") == 0) return 0u;
    }
    fn = (fc_init_native_fn)(uintptr_t)(s_base
         + (uintptr_t)SOBER_FC_RVA_INIT_BODY);
    nd = fn(cfg->env, cfg->clazz,
            cfg->str_arg[0], cfg->str_arg[1], cfg->str_arg[2],
            cfg->asset_manager, cfg->saved_state, cfg->configuration);
    if (!nd) return 0u;
    for (i = 0; i < 4u; ++i) {
        uintptr_t cb = *(volatile uintptr_t *)(nd + offs[i]);
        if (!fc_in_guest_text(cb)) return 0u;
    }
    return nd;
}

int sober_frame_callbacks_install(const struct sober_frame_callbacks_config *cfg) {
    uintptr_t nd = 0u;
    uintptr_t win;
    static const uintptr_t slot_off[5] = {
        SOBER_FC_ND_OFF_CREATED, SOBER_FC_ND_OFF_CHANGED,
        SOBER_FC_ND_OFF_REDRAW,  SOBER_FC_ND_OFF_DESTROYED,
        SOBER_FC_ND_OFF_CONFIG
    };
    static const uintptr_t default_cb[5] = {
        SOBER_FC_RVA_CB_CREATED, SOBER_FC_RVA_CB_CHANGED,
        SOBER_FC_RVA_CB_REDRAW,  SOBER_FC_RVA_CB_DESTROYED,
        SOBER_FC_RVA_CB_CONFIG
    };
    unsigned i;

    fc_bump(&s_installs);
    if (!sober_frame_callbacks_enabled()) {
        s_last_rc = SOBER_FRAME_CALLBACKS_DISABLED;
        return SOBER_FRAME_CALLBACKS_DISABLED;
    }
    if (!cfg || !cfg->guest_base) {
        s_last_rc = SOBER_FRAME_CALLBACKS_ERR_ARG;
        return SOBER_FRAME_CALLBACKS_ERR_ARG;
    }
    if (s_installed) {
        s_last_rc = SOBER_FRAME_CALLBACKS_OK;
        return SOBER_FRAME_CALLBACKS_OK;
    }

    s_base  = (uintptr_t)cfg->guest_base;
    s_env   = cfg->env;
    s_width  = cfg->width  > 0 ? cfg->width
             : fc_env_int("SOBER_WINDOW_WIDTH", 1280);
    s_height = cfg->height > 0 ? cfg->height
             : fc_env_int("SOBER_WINDOW_HEIGHT", 720);

    win = cfg->native_window ? (uintptr_t)cfg->native_window
                             : fc_guest_window_global();
    s_window = (void *)win;

    /* Preferred: the guest's own nativeData. */
    nd = fc_try_guest_native_data(cfg);
    if (nd) {
        s_nd = (unsigned char *)nd;
        s_nd_guest_owned = 1;
        s_route = SOBER_FC_ROUTE_GUEST;
        fc_bump(&s_route_guest);
        /* Record (do not rewrite) the guest's installed callbacks. */
        for (i = 0; i < 5u; ++i) s_cb[i] = fc_slot(slot_off[i]);
    } else {
        unsigned char *o;
        uintptr_t cb[5];
        cb[0] = cfg->cb_created   ? cfg->cb_created   : default_cb[0];
        cb[1] = cfg->cb_changed   ? cfg->cb_changed   : default_cb[1];
        cb[2] = cfg->cb_redraw    ? cfg->cb_redraw    : default_cb[2];
        cb[3] = cfg->cb_destroyed ? cfg->cb_destroyed : default_cb[3];
        cb[4] = cfg->cb_config    ? cfg->cb_config    : default_cb[4];

        /* Every default/override must be a genuine guest code RVA. */
        for (i = 0; i < 5u; ++i) {
            if (!fc_in_guest_text(s_base + cb[i])) {
                s_last_rc = SOBER_FRAME_CALLBACKS_ERR_GUEST;
                return SOBER_FRAME_CALLBACKS_ERR_GUEST;
            }
            s_cb[i] = s_base + cb[i];
        }

        o = (unsigned char *)calloc(1u, (size_t)SOBER_FC_ND_SIZE);
        if (!o) {
            s_last_rc = SOBER_FRAME_CALLBACKS_ERR_NOMEM;
            return SOBER_FRAME_CALLBACKS_ERR_NOMEM;
        }
        s_nd = o;
        s_nd_guest_owned = 0;
        s_route = SOBER_FC_ROUTE_SYNTH;
        fc_bump(&s_route_synth);

        /* Guest-shaped header: render vtable + cached JNIEnv + EGL handles.
         * All addresses here are guest-owned (vtable) or caller-supplied. */
        /* +0x00 = guest render vtable 0x71912e8 (genuine guest data address) */
        *(volatile uintptr_t *)(o + 0u) = s_base + (uintptr_t)0x71912e8u;
        if (s_env)
            *(volatile void **)(o + (size_t)SOBER_FC_ND_OFF_ENV) = s_env;
        if (cfg->render_context) {
            const unsigned char *rc = (const unsigned char *)cfg->render_context;
            *(volatile uintptr_t *)(o + SOBER_FC_ND_OFF_EGLDISPLAY) =
                *(const uintptr_t *)(rc + SOBER_FC_ND_OFF_EGLDISPLAY);
            *(volatile uintptr_t *)(o + SOBER_FC_ND_OFF_EGLSURFACE) =
                *(const uintptr_t *)(rc + SOBER_FC_ND_OFF_EGLSURFACE);
            *(volatile uintptr_t *)(o + SOBER_FC_ND_OFF_EGLCONTEXT) =
                *(const uintptr_t *)(rc + SOBER_FC_ND_OFF_EGLCONTEXT);
        }
        /* The five callback slots: GENUINE GUEST CODE ADDRESSES ONLY. */
        for (i = 0; i < 5u; ++i)
            *(volatile uintptr_t *)(o + (size_t)slot_off[i]) = s_cb[i];
    }

    /* Window holder: guest-bound ANativeWindow* (or explicit override).  The
     * destroy thunk clears +0x140 at 0x2a20a6a, so set_window() re-publishes. */
    fc_set_slot(SOBER_FC_ND_OFF_WINDOW, win);
    fc_set_slot(SOBER_FC_ND_OFF_WIDTH,  (uintptr_t)(uint32_t)s_width);
    fc_set_slot(SOBER_FC_ND_OFF_HEIGHT, (uintptr_t)(uint32_t)s_height);
    fc_bump(&s_window_sets);

    s_installed = 1;
    s_last_rc = SOBER_FRAME_CALLBACKS_OK;
    return SOBER_FRAME_CALLBACKS_OK;
}

/* ---- verify -------------------------------------------------------------- */
int sober_frame_callbacks_verify(char *buf, size_t cap) {
    static const uintptr_t offs[4] = {
        SOBER_FC_ND_OFF_CREATED, SOBER_FC_ND_OFF_CHANGED,
        SOBER_FC_ND_OFF_REDRAW,  SOBER_FC_ND_OFF_DESTROYED
    };
    const char *names[4] = {"created", "changed", "redraw", "destroyed"};
    unsigned i;
    int ok = 1;

    fc_bump(&s_verifies);
    if (!s_installed) {
        s_last_rc = SOBER_FRAME_CALLBACKS_ERR_STATE;
        if (buf && cap) snprintf(buf, cap, "not-installed");
        return SOBER_FRAME_CALLBACKS_ERR_STATE;
    }
    for (i = 0; i < 4u; ++i) {
        uintptr_t v = fc_slot(offs[i]);
        if (!fc_in_guest_text(v)) {
            ok = 0;
            if (buf && cap)
                snprintf(buf, cap, "%s slot %#lx not guest code",
                         names[i], (unsigned long)v);
            break;
        }
    }
    if (ok && !fc_slot(SOBER_FC_ND_OFF_WINDOW)) {
        ok = 0;
        if (buf && cap) snprintf(buf, cap, "window holder +0x140 is NULL");
    }
    if (ok) fc_bump(&s_verify_ok);
    s_last_rc = ok ? SOBER_FRAME_CALLBACKS_OK : SOBER_FRAME_CALLBACKS_ERR_GUEST;
    if (ok && buf && cap)
        snprintf(buf, cap,
                 "route=%s nd=%p +0x88=%#lx +0x90=%#lx +0x98=%#lx "
                 "+0xa0=%#lx +0x140=%#lx",
                 s_route == SOBER_FC_ROUTE_GUEST ? "guest" : "synthetic",
                 (void *)s_nd,
                 (unsigned long)fc_slot(SOBER_FC_ND_OFF_CREATED),
                 (unsigned long)fc_slot(SOBER_FC_ND_OFF_CHANGED),
                 (unsigned long)fc_slot(SOBER_FC_ND_OFF_REDRAW),
                 (unsigned long)fc_slot(SOBER_FC_ND_OFF_DESTROYED),
                 (unsigned long)fc_slot(SOBER_FC_ND_OFF_WINDOW));
    return ok ? SOBER_FRAME_CALLBACKS_OK : SOBER_FRAME_CALLBACKS_ERR_GUEST;
}

/* ---- window holder maintenance ------------------------------------------ */
int sober_frame_callbacks_set_window(void *window) {
    uintptr_t w = window ? (uintptr_t)window
                : (s_window ? (uintptr_t)s_window : fc_guest_window_global());
    if (!s_installed || !w) {
        s_last_rc = SOBER_FRAME_CALLBACKS_ERR_STATE;
        return SOBER_FRAME_CALLBACKS_ERR_STATE;
    }
    fc_set_slot(SOBER_FC_ND_OFF_WINDOW, w);
    fc_bump(&s_window_sets);
    return SOBER_FRAME_CALLBACKS_OK;
}

static void fc_ensure_window(void) {
    if (!fc_slot(SOBER_FC_ND_OFF_WINDOW)) {
        uintptr_t w = s_window ? (uintptr_t)s_window : fc_guest_window_global();
        if (w) {
            fc_set_slot(SOBER_FC_ND_OFF_WINDOW, w);
            fc_bump(&s_window_restores);
        }
    }
}

/* ---- lifecycle driving --------------------------------------------------- */
int sober_frame_callbacks_drive_lifecycle(int with_destroyed) {
    fc_life_fn fn;
    void *surf = NULL;

    if (!s_installed) {
        s_last_rc = SOBER_FRAME_CALLBACKS_ERR_STATE;
        return SOBER_FRAME_CALLBACKS_ERR_STATE;
    }
    /* created: 0x2a20850(env, surf, nd, surf) -> [nd+0x88] */
    fn = (fc_life_fn)(uintptr_t)(s_base
         + (uintptr_t)SOBER_FC_RVA_SURFACE_CREATED);
    fn(s_env, surf, s_nd, surf);
    fc_bump(&s_created);
    /* changed: 0x2a208c0 -> [nd+0xa0]/[+0x88]/[+0x90] */
    fn = (fc_life_fn)(uintptr_t)(s_base
         + (uintptr_t)SOBER_FC_RVA_SURFACE_CHANGED);
    fn(s_env, surf, s_nd, surf);
    fc_bump(&s_changed);
    /* redraw: re-publish +0x140 first (a prior destroy zeroes it), then
     * 0x2a20a00 -> window gate 0x2a20a0c -> [+0x98] -> jmp 0x2a20a20. */
    fc_ensure_window();
    {
        int rc = sober_frame_callbacks_drive_redraw();
        if (rc != SOBER_FRAME_CALLBACKS_OK) return rc;
        if (with_destroyed) return sober_frame_callbacks_drive_destroyed();
        return SOBER_FRAME_CALLBACKS_OK;
    }
}

int sober_frame_callbacks_drive_redraw(void) {
    fc_life_fn fn;
    if (!s_installed) {
        s_last_rc = SOBER_FRAME_CALLBACKS_ERR_STATE;
        return SOBER_FRAME_CALLBACKS_ERR_STATE;
    }
    fc_ensure_window();
    if (!fc_slot(SOBER_FC_ND_OFF_WINDOW) ||
        !fc_slot(SOBER_FC_ND_OFF_REDRAW)) {
        s_last_rc = SOBER_FRAME_CALLBACKS_ERR_GUEST;
        return SOBER_FRAME_CALLBACKS_ERR_GUEST;
    }
    fn = (fc_life_fn)(uintptr_t)(s_base
         + (uintptr_t)SOBER_FC_RVA_SURFACE_REDRAW);
    fn(s_env, NULL, s_nd, NULL);
    fc_bump(&s_redraw);
    s_last_rc = SOBER_FRAME_CALLBACKS_OK;
    return SOBER_FRAME_CALLBACKS_OK;
}

int sober_frame_callbacks_drive_destroyed(void) {
    fc_life_fn fn;
    if (!s_installed) {
        s_last_rc = SOBER_FRAME_CALLBACKS_ERR_STATE;
        return SOBER_FRAME_CALLBACKS_ERR_STATE;
    }
    fn = (fc_life_fn)(uintptr_t)(s_base
         + (uintptr_t)SOBER_FC_RVA_SURFACE_DESTROYED);
    fn(s_env, NULL, s_nd, NULL);
    fc_bump(&s_destroyed);
    s_last_rc = SOBER_FRAME_CALLBACKS_OK;
    return SOBER_FRAME_CALLBACKS_OK;
}

void sober_frame_callbacks_uninstall(void) {
    if (s_nd && !s_nd_guest_owned) free(s_nd);
    s_nd = NULL;
    s_nd_guest_owned = 0;
    s_installed = 0;
    s_route = SOBER_FC_ROUTE_NONE;
    s_base = 0u;
    s_env = NULL;
    s_window = NULL;
    memset(s_cb, 0, sizeof(s_cb));
    s_last_rc = SOBER_FRAME_CALLBACKS_DISABLED;
}

/* ---- accessors ----------------------------------------------------------- */
int sober_frame_callbacks_is_installed(void) { return s_installed; }
void *sober_frame_callbacks_native_data(void) { return s_nd; }
uintptr_t sober_frame_callbacks_base(void) { return s_base; }
int sober_frame_callbacks_route(void) { return s_route; }
void *sober_frame_callbacks_window(void) {
    return s_nd ? (void *)fc_slot(SOBER_FC_ND_OFF_WINDOW) : NULL;
}
int sober_frame_callbacks_last_rc(void) { return s_last_rc; }
uintptr_t sober_frame_callbacks_cb(unsigned idx) {
    return idx < 5u ? s_cb[idx] : 0u;
}
uint64_t sober_frame_callbacks_install_calls(void) {
    return atomic_load(&s_installs);
}
uint64_t sober_frame_callbacks_verify_calls(void) {
    return atomic_load(&s_verifies);
}
uint64_t sober_frame_callbacks_verify_ok(void) {
    return atomic_load(&s_verify_ok);
}
uint64_t sober_frame_callbacks_route_guest_ok(void) {
    return atomic_load(&s_route_guest);
}
uint64_t sober_frame_callbacks_route_synth_ok(void) {
    return atomic_load(&s_route_synth);
}
uint64_t sober_frame_callbacks_created_calls(void) {
    return atomic_load(&s_created);
}
uint64_t sober_frame_callbacks_changed_calls(void) {
    return atomic_load(&s_changed);
}
uint64_t sober_frame_callbacks_redraw_calls(void) {
    return atomic_load(&s_redraw);
}
uint64_t sober_frame_callbacks_destroyed_calls(void) {
    return atomic_load(&s_destroyed);
}
uint64_t sober_frame_callbacks_window_sets(void) {
    return atomic_load(&s_window_sets);
}
uint64_t sober_frame_callbacks_window_restores(void) {
    return atomic_load(&s_window_restores);
}
int sober_frame_callbacks_should_withhold(void) { return s_installed ? 1 : 0; }

const char *sober_frame_callbacks_describe(char *buf, size_t cap) {
    if (!buf || !cap) return "";
    snprintf(buf, cap,
             "frame_callbacks %s installed=%d route=%s nd=%p "
             "cb[created=%#lx changed=%#lx redraw=%#lx destroyed=%#lx] "
             "window=%#lx sets=%llu restores=%llu redraw_calls=%llu "
             "verify_ok=%llu withhold=%d",
             sober_frame_callbacks_enabled() ? "ON" : "OFF", s_installed,
             s_route == SOBER_FC_ROUTE_GUEST ? "guest"
               : s_route == SOBER_FC_ROUTE_SYNTH ? "synthetic" : "none",
             (void *)s_nd,
             (unsigned long)s_cb[0], (unsigned long)s_cb[1],
             (unsigned long)s_cb[2], (unsigned long)s_cb[3],
             (unsigned long)fc_slot(SOBER_FC_ND_OFF_WINDOW),
             (unsigned long long)atomic_load(&s_window_sets),
             (unsigned long long)atomic_load(&s_window_restores),
             (unsigned long long)atomic_load(&s_redraw),
             (unsigned long long)atomic_load(&s_verify_ok),
             s_installed ? 1 : 0);
    return buf;
}

/* ---- self-test (no guest required) -------------------------------------- */
int sober_frame_callbacks_selftest(char *buf, size_t cap) {
    int ok = 1;
    const uintptr_t va = (uintptr_t)0x10000000u; /* synthetic base */
    static const uintptr_t cb[5] = {
        SOBER_FC_RVA_CB_CREATED, SOBER_FC_RVA_CB_CHANGED,
        SOBER_FC_RVA_CB_REDRAW,  SOBER_FC_RVA_CB_DESTROYED,
        SOBER_FC_RVA_CB_CONFIG
    };
    unsigned i;

    for (i = 0; i < 5u; ++i)
        if (cb[i] == 0u || cb[i] >= (uintptr_t)SOBER_FC_TEXT_SIZE) ok = 0;
    if (SOBER_FC_ND_SIZE < SOBER_FC_ND_OFF_HEIGHT + 4u) ok = 0;
    if (!(SOBER_FC_ND_OFF_CREATED < SOBER_FC_ND_OFF_CHANGED &&
          SOBER_FC_ND_OFF_CHANGED < SOBER_FC_ND_OFF_REDRAW &&
          SOBER_FC_ND_OFF_REDRAW  < SOBER_FC_ND_OFF_DESTROYED &&
          SOBER_FC_ND_OFF_DESTROYED < SOBER_FC_ND_OFF_WINDOW)) ok = 0;
    if (SOBER_FC_RVA_SURFACE_REDRAW != 0x2a20a00u) ok = 0;
    if (SOBER_FC_RVA_INIT_BODY != 0x2a1fe80u) ok = 0;
    if (fc_in_guest_text(va + SOBER_FC_RVA_CB_REDRAW)) ok = 0; /* no base set */

    if (buf && cap)
        snprintf(buf, cap,
                 "frame_callbacks selftest %s: 5 guest-RVA callbacks, "
                 "ND=0x%x, redraw rva=0x%x, init=0x%x",
                 ok ? "PASS" : "FAIL", SOBER_FC_ND_SIZE,
                 SOBER_FC_RVA_SURFACE_REDRAW, SOBER_FC_RVA_INIT_BODY);
    return ok ? 0 : -1;
}

#ifdef SOBER_FRAME_CALLBACKS_SELFTEST
int main(void) {
    char b[256];
    int ok = (sober_frame_callbacks_selftest(b, sizeof(b)) == 0);
    printf("[fc-self-test] %s\n", b);

    /* Gate: default ON, "0" is the only kill switch. */
    unsetenv(SOBER_FRAME_CALLBACKS_ENV);
    ok &= sober_frame_callbacks_enabled() == 1;
    setenv(SOBER_FRAME_CALLBACKS_ENV, "0", 1);
    ok &= sober_frame_callbacks_enabled() == 0;
    unsetenv(SOBER_FRAME_CALLBACKS_ENV);
    {
        int default_on = sober_frame_callbacks_enabled();
        int killed;
        setenv(SOBER_FRAME_CALLBACKS_ENV, "0", 1);
        killed = sober_frame_callbacks_enabled();
        printf("[fc-self-test] gate default/kill = %d/%d\n",
               default_on, killed);
        ok &= (default_on == 1 && killed == 0);
        unsetenv(SOBER_FRAME_CALLBACKS_ENV);
    }

    /* All installed callbacks must be guest RVAs, never host addresses. */
    {
        const uintptr_t rv[5] = {
            SOBER_FC_RVA_CB_CREATED, SOBER_FC_RVA_CB_CHANGED,
            SOBER_FC_RVA_CB_REDRAW, SOBER_FC_RVA_CB_DESTROYED,
            SOBER_FC_RVA_CB_CONFIG
        };
        unsigned i;
        for (i = 0; i < 5u; ++i)
            printf("[fc-self-test] cb[%u] rva=%#lx\n", i,
                   (unsigned long)rv[i]);
    }
    printf("frame callbacks self-test %s\n", ok ? "OK" : "FAILED");
    return ok ? 0 : 1;
}
#endif /* SOBER_FRAME_CALLBACKS_SELFTEST */
