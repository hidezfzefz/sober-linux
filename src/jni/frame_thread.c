/*
 * frame_thread.c - DEFAULT-ON guest render-loop thread/task driver.
 *
 * NEW FILE ONLY; see frame_thread.h for the full rationale, RVAs, provenance,
 * preconditions and lead-wiring contract.  Hard guarantees, by construction:
 *
 *   - The ONLY guest address executed as code is the genuine
 *     onSurfaceRedrawNeededNative thunk at RVA 0x2a20a00 (overridable via
 *     cfg.loop_rva), plus the optional one-shot nesting prime at 0x2a132c0.
 *     The flat swap wrapper 0x649d422 is NEVER called by this module.
 *   - ZERO guest bytes are read as instructions or written; no GOT/vtable/
 *     nativeData slot is patched and no host function pointer is placed in a
 *     guest object.  nativeData +0x98/+0x140 are only READ, for verification.
 *   - No JNI callback is installed and no decision-013 marker is emitted or
 *     relaxed.  This module only *calls* guest code and counts.
 */
#define _GNU_SOURCE
#include "frame_thread.h"

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if defined(__linux__)
#include <sys/prctl.h>
#endif

/* The genuine guest frame trigger: void fn(jni_env, jni_class, native_data).
 * RVA 0x2a20a00 dispatches `jmp *[nativeData+0x98]` with rdi = nativeData. */
typedef void (*ft_loop_fn)(void *, void *, void *);
/* Guest nesting enter/leave guard: edi != 0 => ENTER (benign one-shot prime). */
typedef void (*ft_prime_fn)(int);

static struct sober_frame_thread_config s_cfg;
static pthread_t    s_thread;
static ft_loop_fn   s_loop_fn;
static ft_prime_fn  s_prime_fn;

/* Atomic state: 0/1 flags and relaxed counters. */
static int      s_installed;
static int      s_started;
static int      s_running;
static int      s_stop;
static int      s_thread_valid;
static uint64_t s_frames;
static uint64_t s_skips;
static uint64_t s_errors;

static int load_i(int *p)            { return __atomic_load_n(p, __ATOMIC_SEQ_CST); }
static void store_i(int *p, int v)   { __atomic_store_n(p, v, __ATOMIC_SEQ_CST); }

static uint64_t load_u(uint64_t *p)  { return __atomic_load_n(p, __ATOMIC_RELAXED); }
static uint64_t add_u(uint64_t *p, uint64_t d)
{
    return __atomic_fetch_add(p, d, __ATOMIC_RELAXED);
}

static int ft_opted_in(void)
{
    const char *v = getenv(SOBER_FRAME_THREAD_ENV);
    return !(v != NULL && strcmp(v, "0") == 0);   /* DEFAULT ON */
}

static void ft_nap(long usec)
{
    struct timespec ts;
    ts.tv_sec  = usec / 1000000L;
    ts.tv_nsec = (usec % 1000000L) * 1000L;
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR) { /* retry */ }
}

int sober_frame_thread_enabled(void)
{
    return ft_opted_in();
}

const char *sober_frame_thread_env_vars(void)
{
    return SOBER_FRAME_THREAD_ENV
           "=0 (kill switch; default ON - genuine guest frame trigger "
           "0x2a20a00 -> [nativeData+0x98], never 0x649d422)";
}

int sober_frame_thread_install(const struct sober_frame_thread_config *cfg)
{
    if (!ft_opted_in()) return SOBER_FT_DISABLED;
    if (!cfg || !cfg->guest_base) return SOBER_FT_ERR_ARG;
    if (load_i(&s_installed)) return SOBER_FT_OK;      /* idempotent */

    s_cfg = *cfg;
    if (s_cfg.interval_us <= 0)
        s_cfg.interval_us = SOBER_FT_DEFAULT_INTERVAL_US;

    uintptr_t base = (uintptr_t)cfg->guest_base;
    uintptr_t loop_rva = s_cfg.loop_rva ? s_cfg.loop_rva
                                        : (uintptr_t)SOBER_FT_RVA_SURFACE_REDRAW;
    /* Resolve entry points only.  No guest byte is read or written here. */
    s_loop_fn  = (ft_loop_fn)(base + loop_rva);
    s_prime_fn = s_cfg.prime_on_start
                     ? (ft_prime_fn)(base + (uintptr_t)SOBER_FT_RVA_GUARD_ENTER)
                     : NULL;

    store_i(&s_installed, 1);
    return SOBER_FT_OK;
}

static void *ft_thread_main(void *arg)
{
    (void)arg;
#if defined(__linux__)
    (void)prctl(PR_SET_NAME, "sober-frame", 0, 0, 0);
#endif

    /* Optional one-shot, BENIGN prime of this thread's guest TLS/scheduler
     * state via the guest's own nesting-enter routine (A6: an unconditional
     * enter leaves c>=1 and cannot underflow the later leave). */
    if (s_prime_fn) s_prime_fn(1);

    store_i(&s_running, 1);
    while (!load_i(&s_stop)) {
        uint64_t done = load_u(&s_frames);
        if (s_cfg.max_frames > 0 && done >= (uint64_t)s_cfg.max_frames) break;

        if (s_cfg.native_data) {
            /* GENUINE GUEST EXECUTION.  0x2a20a00 re-dispatches the guest's
             * own frame callback at [nativeData+0x98]; this is what makes
             * frames repeat through guest code, not a host swap loop. */
            s_loop_fn(s_cfg.jni_env, s_cfg.jni_class, s_cfg.native_data);
            (void)add_u(&s_frames, 1);
        } else {
            (void)add_u(&s_skips, 1);
        }

        if (s_cfg.interval_us > 0) ft_nap(s_cfg.interval_us);
    }
    store_i(&s_running, 0);
    return NULL;
}

int sober_frame_thread_start(void)
{
    if (!ft_opted_in()) return SOBER_FT_DISABLED;
    if (!load_i(&s_installed)) {
        int rc = sober_frame_thread_install(&s_cfg);
        if (rc != SOBER_FT_OK) return rc;
        if (!load_i(&s_installed)) return SOBER_FT_ERR_STATE;
    }
    /* Claim the single start atomically so concurrent callers are idempotent. */
    if (__atomic_exchange_n(&s_started, 1, __ATOMIC_SEQ_CST)) return SOBER_FT_OK;
    if (load_i(&s_running)) return SOBER_FT_OK;

    store_i(&s_stop, 0);
    int rc = pthread_create(&s_thread, NULL, ft_thread_main, NULL);
    if (rc != 0) {
        (void)add_u(&s_errors, 1);
        store_i(&s_started, 0);
        return SOBER_FT_ERR_THREAD;
    }
    s_thread_valid = 1;
    return SOBER_FT_OK;
}

void sober_frame_thread_stop(void)
{
    if (!load_i(&s_installed)) return;
    store_i(&s_stop, 1);

    /* Never join ourselves (a callback on the render thread may call stop). */
    if (s_thread_valid && !pthread_equal(pthread_self(), s_thread)) {
        pthread_join(s_thread, NULL);
        s_thread_valid = 0;
        store_i(&s_started, 0);
        store_i(&s_running, 0);
    }
}

int sober_frame_thread_run(const struct sober_frame_thread_config *cfg)
{
    int rc = sober_frame_thread_install(cfg);
    if (rc != SOBER_FT_OK) return rc;
    return sober_frame_thread_start();
}

int sober_frame_thread_is_active(void)
{
    return load_i(&s_installed);
}

int sober_frame_thread_is_running(void)
{
    return load_i(&s_running);
}

uint64_t sober_frame_thread_frames(void)  { return load_u(&s_frames); }
uint64_t sober_frame_thread_skips(void)   { return load_u(&s_skips); }
uint64_t sober_frame_thread_errors(void)  { return load_u(&s_errors); }

int sober_frame_thread_verify(char *buf, size_t cap)
{
    int enabled = sober_frame_thread_enabled();
    int active  = load_i(&s_installed);
    int running = load_i(&s_running);

    uintptr_t base = (uintptr_t)s_cfg.guest_base;
    uintptr_t loop = (uintptr_t)s_loop_fn;
    int loop_in_text = (base != 0) &&
                       (loop >= base) &&
                       (loop - base < (uintptr_t)SOBER_FT_TEXT_SPAN);

    /* Read-only observation of the two guest slots the thunk consumes. */
    int nd_present = 0, cb_ok = 0, win_ok = 0;
    if (s_cfg.native_data) {
        void *cb = NULL, *win = NULL;
        memcpy(&cb, (const unsigned char *)s_cfg.native_data
                    + SOBER_FT_ND_OFF_REDRAW, sizeof(cb));
        memcpy(&win, (const unsigned char *)s_cfg.native_data
                    + SOBER_FT_ND_OFF_WINDOW, sizeof(win));
        nd_present = 1;
        cb_ok = (cb != NULL);
        win_ok = (win != NULL);
    }

    int ok = enabled && active && loop_in_text;
    if (buf && cap) {
        snprintf(buf, cap,
                 "enabled=%d active=%d running=%d base=%p loop=base+0x%llx "
                 "loop_in_guest_text=%d native_data=%d redraw_cb=%d window=%d "
                 "loop_rva=0x%llx frames=%llu skips=%llu errors=%llu -> %s",
                 enabled, active, running, (void *)base,
                 (unsigned long long)(loop ? loop - base : 0), loop_in_text,
                 nd_present, cb_ok, win_ok,
                 (unsigned long long)(s_cfg.loop_rva
                     ? s_cfg.loop_rva : (uintptr_t)SOBER_FT_RVA_SURFACE_REDRAW),
                 (unsigned long long)load_u(&s_frames),
                 (unsigned long long)load_u(&s_skips),
                 (unsigned long long)load_u(&s_errors),
                 ok ? "OK" : "NOT-READY");
    }
    return ok ? 0 : 1;
}

void sober_frame_thread_stats(char *buf, size_t cap)
{
    if (!buf || !cap) return;
    char vb[256];
    sober_frame_thread_verify(vb, sizeof(vb));
    snprintf(buf, cap,
             "enabled=%d active=%d running=%d frames=%llu skips=%llu "
             "errors=%llu env=%s=0 | %s",
             sober_frame_thread_enabled(), load_i(&s_installed),
             load_i(&s_running),
             (unsigned long long)load_u(&s_frames),
             (unsigned long long)load_u(&s_skips),
             (unsigned long long)load_u(&s_errors),
             SOBER_FRAME_THREAD_ENV, vb);
}

int sober_frame_thread_should_withhold(void)
{
    /* Diagnostic signal only; this module never touches marker state. */
    return ft_opted_in() && load_i(&s_installed);
}

/* ------------------------------------------------------------------------ */
/* Standalone self-test.  Compile ONLY this file with
 *   -DSOBER_FRAME_THREAD_SELFTEST
 * to get a `main` exercising the exact thread/stop/counter/verify code with
 * synthetic stand-ins.  It never loads or calls a real guest.
 * ------------------------------------------------------------------------ */
#ifdef SOBER_FRAME_THREAD_SELFTEST

static int  ft_test_loops;
static int  ft_test_primes;

static void ft_test_loop(void *env, void *cls, void *nd)
{
    (void)env; (void)cls; (void)nd;
    (void)__atomic_fetch_add(&ft_test_loops, 1, __ATOMIC_RELAXED);
}

static void ft_test_prime(int arg)
{
    (void)arg;
    (void)__atomic_fetch_add(&ft_test_primes, 1, __ATOMIC_RELAXED);
}

int main(void)
{
    int ok = 1;
    const char *old = getenv(SOBER_FRAME_THREAD_ENV);
    char saved[64] = {0};
    if (old) snprintf(saved, sizeof(saved), "%s", old);

    setenv(SOBER_FRAME_THREAD_ENV, "1", 1);
    printf("[ft-self-test] enabled=%d env=%s\n",
           sober_frame_thread_enabled(), sober_frame_thread_env_vars());
    ok &= sober_frame_thread_enabled() == 1;

    static unsigned char fake_guest[0x1000];
    static unsigned char nd[0x200];
    memset(fake_guest, 0xa5, sizeof(fake_guest));
    memset(nd, 0, sizeof(nd));
    unsigned char snapshot[0x1000];
    memcpy(snapshot, fake_guest, sizeof(snapshot));

    struct sober_frame_thread_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.guest_base    = fake_guest;
    cfg.native_data   = nd;                       /* +0x98/+0x140 all zero */
    cfg.interval_us   = 200;                      /* short test interval */
    cfg.max_frames    = 0;
    cfg.prime_on_start = 1;

    int rc = sober_frame_thread_install(&cfg);
    int rc2 = sober_frame_thread_install(&cfg);   /* idempotent */
    int nowrite = memcmp(fake_guest, snapshot, sizeof(snapshot)) == 0;
    printf("[ft-self-test] install=%d install2=%d active=%d no_guest_writes=%d\n",
           rc, rc2, sober_frame_thread_is_active(), nowrite);
    ok &= rc == SOBER_FT_OK && rc2 == SOBER_FT_OK
          && sober_frame_thread_is_active() && nowrite;

    /* Swap in synthetic stand-ins for the two resolved guest functions. */
    s_loop_fn  = ft_test_loop;
    s_prime_fn = ft_test_prime;

    char vb[256];
    (void)sober_frame_thread_verify(vb, sizeof(vb));
    printf("[ft-self-test] verify(no_cb)=%s\n", vb);
    ok &= (strstr(vb, "redraw_cb=0") != NULL);

    rc = sober_frame_thread_start();
    printf("[ft-self-test] start=%d running=%d\n",
           rc, sober_frame_thread_is_running());
    ok &= rc == SOBER_FT_OK;

    /* Let it produce a handful of frames, then stop. */
    struct timespec nap = {0, 30L * 1000L * 1000L};
    (void)nanosleep(&nap, NULL);
    sober_frame_thread_stop();
    printf("[ft-self-test] stopped running=%d frames=%llu standin_loops=%d "
           "primes=%d skips=%llu\n",
           sober_frame_thread_is_running(),
           (unsigned long long)sober_frame_thread_frames(),
           ft_test_loops, ft_test_primes,
           (unsigned long long)sober_frame_thread_skips());
    ok &= !sober_frame_thread_is_running()
          && sober_frame_thread_frames() > 0
          && ft_test_loops > 0 && ft_test_primes == 1
          && sober_frame_thread_skips() == 0;

    /* Idempotent stop. */
    sober_frame_thread_stop();

    /* max_frames cap. */
    s_started = 0;
    s_frames = 0;
    ft_test_loops = 0;
    cfg.max_frames = 3;
    /* re-install is idempotent (keeps old cfg); reset it directly for test. */
    s_cfg = cfg;
    rc = sober_frame_thread_start();
    nap.tv_sec = 0; nap.tv_nsec = 30L * 1000L * 1000L;
    (void)nanosleep(&nap, NULL);
    sober_frame_thread_stop();
    printf("[ft-self-test] capped frames=%llu cap=3\n",
           (unsigned long long)sober_frame_thread_frames());
    ok &= rc == SOBER_FT_OK && sober_frame_thread_frames() == 3;

    /* Stats + withhold. */
    char sb[512];
    sober_frame_thread_stats(sb, sizeof(sb));
    printf("[ft-self-test] stats=%s\n", sb);
    printf("[ft-self-test] withhold=%d\n",
           sober_frame_thread_should_withhold());
    ok &= sober_frame_thread_should_withhold() == 1;

    /* Kill switch disables everything. */
    setenv(SOBER_FRAME_THREAD_ENV, "0", 1);
    printf("[ft-self-test] optout enabled=%d withhold=%d\n",
           sober_frame_thread_enabled(),
           sober_frame_thread_should_withhold());
    ok &= !sober_frame_thread_enabled()
          && !sober_frame_thread_should_withhold();

    if (old) setenv(SOBER_FRAME_THREAD_ENV, saved, 1);
    else     unsetenv(SOBER_FRAME_THREAD_ENV);

    if (ok) printf("frame thread self-test OK\n");
    else    printf("frame thread self-test FAILED\n");
    return ok ? 0 : 1;
}

#endif /* SOBER_FRAME_THREAD_SELFTEST */
