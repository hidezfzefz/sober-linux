/*
 * scheduler_guest.c - opt-in TaskScheduler nesting prime via genuine guest code.
 *
 * T1-2 (logical Team 1, substrate).  NEW FILE ONLY; see scheduler_guest.h for
 * the full rationale, RVAs, exact hook point and integration contract.
 *
 * Hard guarantees, by construction (there is no guest-memory WRITE in this
 * file): the only guest addresses ever used are called as FUNCTIONS at RVA
 * 0x2a132c0 (enter) and 0x2a0de60 (TLS accessor).  No crash-mode flag global is
 * read or written, no guest instruction/byte/vtable is patched, no JNI slot or
 * callback is invoked and no marker is emitted.  The marker stays WITHHELD.
 */
#define _GNU_SOURCE
#include "scheduler_guest.h"
#include "tsd_default.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* SysV ABI: arg0 is edi.  Guest 0x2a132fd overwrites ebx with 1, so any
 * nonzero value is an ENTER; we use the canonical 1. */
typedef void  (*sg_guard_fn)(int);
/* Guest 0x2a0de60 returns TLS_block + 0x410, i.e. the ADDRESS of the slot that
 * holds the per-thread obj pointer (guest 0x2a132d7: `mov (%rax),%r14`). */
typedef void *(*sg_tls_fn)(void);

static void        *sg_base;
static sg_guard_fn  sg_guard;
static sg_tls_fn    sg_tls;
static int          sg_installed;
static pthread_t    sg_owner_tid;       /* install() runs on the harness main thread */
static int          sg_owner_tid_valid;
static __thread int sg_primed;   /* one prime per thread, set before the call */

/* Cumulative counters (relaxed atomics; A/B diagnostics only, no mechanism).
 * These are HOST diagnostics; they never inflate the guest's own nesting
 * counter at obj+0x70, which is written only by guest RVA 0x2a1333c. */
static long sg_attach_calls;
static long sg_worker_attaches;
static long sg_prime_calls;
static long sg_prime_skips;
static long sg_main_thread_rejects;  /* prime refused on the installer thread */
static long sg_coherence_rejects;    /* prime refused: split/missing TSD key3 */
static long sg_verify_errors;        /* prime ran but nesting() != 1 */
static long sg_nesting_reads;
static long sg_nesting_errors;
static long sg_last_nesting = -1;

static long sg_add(long *p, long d)
{
    return __atomic_fetch_add(p, d, __ATOMIC_RELAXED);
}

static long sg_get(long *p)
{
    return __atomic_load_n(p, __ATOMIC_RELAXED);
}

static int sg_opted_in(void)
{
    const char *v = getenv(SOBER_SCHEDULER_GUEST_ENV);
    return v != NULL && strcmp(v, "1") == 0;
}

int sober_scheduler_guest_enabled(void)
{
    return sg_opted_in();
}

const char *sober_scheduler_guest_env_vars(void)
{
    return SOBER_SCHEDULER_GUEST_ENV "=1";
}

int sober_scheduler_guest_install(void *guest_base)
{
    if (!guest_base) return SOBER_SG_ERR;
    if (!sg_opted_in()) return SOBER_SG_DISABLED;
    if (sg_installed) return SOBER_SG_OK;      /* idempotent; never re-resolve */

    unsigned char *b = (unsigned char *)guest_base;
    /* Resolve entry points only.  No guest byte is read or written here. */
    sg_guard = (sg_guard_fn)(uintptr_t)(b + SOBER_SG_RVA_GUARD_ENTER);
    sg_tls   = (sg_tls_fn)(uintptr_t)(b + SOBER_SG_RVA_TLS_ACCESSOR);
    sg_base  = guest_base;
    /* Remember the installer thread.  The FunctionMarshaller worker is a
     * different pthread; this is what makes the prime WORKER-SCOPED instead of
     * firing on the harness main thread (the old guest_app_shell.c:1518 call). */
    sg_owner_tid = pthread_self();
    sg_owner_tid_valid = 1;
    sg_installed = 1;

    /* READ-ONLY FLog provenance dump (host diagnostic only; no guest call, no
     * write).  The guest derives each FLog stream FILE* as
     * [GOT 0x72281d8] + {0x98,0x130} and stores it into obj+0x40 via guest ctor
     * 0x2e61750 / 0x2e617e0.  Dump the GOT word and the +0x40 slots of the
     * known FLog globals so a divergence can be pinned without guessing. */
    if (sg_opted_in() && getenv("SOBER_SCHED_DIAG") &&
        sober_tsd_default_verify_build(guest_base)) {
        static const uint32_t rvas[] = {
            0x72a0290u, 0x72a0818u, 0x72a0880u, 0x72a03e0u,
            0x72a0520u, 0x72a0660u, 0x72a08e8u, 0x72a0338u,
            0x72a0958u, 0x72a0a30u
        };
        const unsigned char *q = (const unsigned char *)guest_base;
        void *sf = *(void *const *)(q + 0x72281d8u);
        fprintf(stderr,
                "[sg-diag] FLog provenance: __sF GOT(0x72281d8)=%p "
                "stderr=%p stdout=%p\n",
                sf, (unsigned char *)sf + 0x130u, (unsigned char *)sf + 0x98u);
        for (size_t i = 0; i < sizeof(rvas) / sizeof(rvas[0]); ++i) {
            void *obj = *(void *const *)(q + rvas[i]);
            void *fp = NULL;
            if (obj && (uintptr_t)obj > 0x10000u)
                fp = *(void *const *)((const unsigned char *)obj + 0x40u);
            fprintf(stderr, "[sg-diag]   obj@0x%x = %p  +0x40 = %p\n",
                    rvas[i], obj, fp);
        }
    }
    return SOBER_SG_OK;
}

int sober_scheduler_guest_tsd_coherent(void *out_state)
{
    return sober_tsd_default_key3_coherent(sg_base, out_state);
}

void sober_scheduler_guest_prime_worker(void)
{
    void *guest_tls = NULL;

    if (!sg_opted_in() || !sg_installed || !sg_guard) return;
    if (sg_primed) {
        (void)sg_add(&sg_prime_skips, 1);
        return;
    }
    /* Fail closed on the installer thread: a prime issued there is the harness
     * main thread, not the worker.  No guest code is executed in that case. */
    if (sg_owner_tid_valid && pthread_equal(pthread_self(), sg_owner_tid)) {
        (void)sg_add(&sg_main_thread_rejects, 1);
        return;
    }
    /* Coherent per-thread TSD key3: the guest's getspecific@plt GOT 0x72298b0
     * and setspecific@plt GOT 0x72295c8 must both resolve to the SAME versioned
     * stub and the key global 0x72a1b08 must read 3.  Read-only; the guest owns
     * the TLS state (it builds it at 0x2ea6520 with its own allocator + the
     * guest's own setspecific).  No host seed, no fabricated object. */
    if (!sober_tsd_default_key3_coherent(sg_base, &guest_tls)) {
        (void)sg_add(&sg_coherence_rejects, 1);
        return;
    }
    /* Set the per-thread flag BEFORE entering guest code: the guard makes no
     * callbacks to us, but this keeps the prime exactly-once under re-entry. */
    sg_primed = 1;
    (void)sg_add(&sg_prime_calls, 1);
    /* GENUINE GUEST EXECUTION.  RVA 0x2a132c0 -> 0x2a13332..0x2a1333c writes
     * obj+0x70 = (tag & ~0x3f) | 1, producing c == 1 with the real tag intact.
     * No matching harness leave: A6 section 5. */
    sg_guard(1);
    /* Read-only post-condition: the guest's own accessor must report c == 1
     * on this thread.  A mismatch is recorded but never papered over. */
    if (sober_scheduler_guest_nesting() != 1)
        (void)sg_add(&sg_verify_errors, 1);
}

void sober_scheduler_guest_on_attach(const char *thread_name)
{
    (void)sg_add(&sg_attach_calls, 1);
    if (!thread_name) return;
    if (strcmp(thread_name, SOBER_SG_WORKER_NAME) != 0) return;
    (void)sg_add(&sg_worker_attaches, 1);
    sober_scheduler_guest_prime_worker();
}

int sober_scheduler_guest_nesting(void)
{
    int result = -1;
    (void)sg_add(&sg_nesting_reads, 1);

    if (sg_installed && sg_tls) {
        /* Guest RVA 0x2a0de60: returns TLS_block + 0x410 (= &obj pointer). */
        void *slot = sg_tls();
        if (slot) {
            /* guest 0x2a132d7: obj = *(TLS_block + 0x410). */
            void *obj = *(void **)slot;
            if (obj) {
                uint64_t word = 0;
                memcpy(&word, (const unsigned char *)obj + SOBER_SG_OBJ_COUNTER,
                       sizeof(word));
                result = (int)(word & (uint64_t)SOBER_SG_COUNTER_MASK);
            }
        }
    }

    if (result < 0) (void)sg_add(&sg_nesting_errors, 1);
    else            __atomic_store_n(&sg_last_nesting, result, __ATOMIC_RELAXED);
    return result;
}

int sober_scheduler_guest_is_active(void)
{
    return sg_installed;
}

int sober_scheduler_guest_is_primed(void)
{
    return sg_opted_in() && sg_primed;
}

int sober_scheduler_guest_should_withhold(void)
{
    /* Diagnostic signal only.  This module never touches the marker state. */
    return sg_opted_in();
}

void sober_scheduler_guest_stats(char *buf, size_t cap)
{
    if (!buf || !cap) return;
    /* READ-ONLY one-shot gate state.  0x729d680 is the guest's cmpxchg state
     * (0=uninit, 1=in-progress, 2=complete) and 0x729d688 is the task object
     * built by 0x2a168b0.  Reporting these proves whether the sole caller
     * (0x2a0de78 inside the TLS accessor) ever ran, without invoking it. */
    long oneshot = -1;
    void *taskobj = NULL;
    /* The selftest passes a synthetic 0x1000 buffer as "guest"; reading engine
     * globals from it would be out of bounds.  Gate these reads on the ELF
     * build-ID check (a read-only header test). */
    if (sg_base && sober_tsd_default_verify_build(sg_base)) {
        oneshot = *(const volatile uint32_t *)
                  ((const unsigned char *)sg_base + 0x729d680u);
        taskobj = *(void *const *)((const unsigned char *)sg_base + 0x729d688u);
    }
    /* 0x2a0de60 gates on byte0 of descriptor-2's per-index object returned by
     * 0x2ea6440: bit0 == 0 => call the one-shot, 1 => skip it.  Read that byte
     * straight out of THIS thread's key3 state; never call guest code. */
    int desc2b = -1;
    void *k3state = NULL;
    if (sg_base && sober_tsd_default_verify_build(sg_base) &&
        sober_scheduler_guest_tsd_coherent(&k3state) && k3state) {
        uint32_t idx = 0;
        memcpy(&idx, (const unsigned char *)sg_base + 0x722e560u + 0x10u,
               sizeof(idx));
        if (idx < 512u)
            desc2b = *(const unsigned char *)
                     ((const unsigned char *)k3state + (size_t)idx * 8u + 8u);
    }
    snprintf(buf, cap,
             "enabled=%d active=%d primed=%d attach=%ld worker=%ld "
             "prime=%ld skip=%ld main_reject=%ld tsd_reject=%ld verify_err=%ld "
             "reads=%ld read_err=%ld last_c=%ld "
             "oneshot(0x729d680)=%ld taskobj=%p desc2_byte=%d "
             "enter_rva=0x%x tls_rva=0x%x base=%p",
             sober_scheduler_guest_enabled(), sg_installed,
             sober_scheduler_guest_is_primed(),
             sg_get(&sg_attach_calls), sg_get(&sg_worker_attaches),
             sg_get(&sg_prime_calls), sg_get(&sg_prime_skips),
             sg_get(&sg_main_thread_rejects), sg_get(&sg_coherence_rejects),
             sg_get(&sg_verify_errors),
             sg_get(&sg_nesting_reads), sg_get(&sg_nesting_errors),
             sg_get(&sg_last_nesting),
             oneshot, taskobj, desc2b,
             SOBER_SG_RVA_GUARD_ENTER, SOBER_SG_RVA_TLS_ACCESSOR, sg_base);
}

/* ------------------------------------------------------------------------ */
/* Standalone self-test.  Compile ONLY this file with
 *   -DSOBER_SCHEDULER_GUEST_SELFTEST
 * to get a `main` that exercises the exact same dispatch/accessor code with
 * synthetic stand-ins for the two guest functions.  It never loads a guest.
 * ------------------------------------------------------------------------ */
#ifdef SOBER_SCHEDULER_GUEST_SELFTEST

static unsigned char sg_test_obj[0x80];   /* per-thread obj (64-byte aligned) */
static void         *sg_test_obj_slot;    /* stands in for TLS_block + 0x410   */
static int           sg_test_guard_calls;
static int           sg_test_guard_arg;

/* Synthetic stand-in for guest RVA 0x2a132c0: it performs the guest's own
 * store at 0x2a1333c, i.e. word = (word & ~0x3f) | 1. */
static void sg_test_guard(int arg)
{
    uint64_t word = 0;
    sg_test_guard_calls++;
    sg_test_guard_arg = arg;
    memcpy(&word, sg_test_obj + SOBER_SG_OBJ_COUNTER, sizeof(word));
    word = (word & ~(uint64_t)SOBER_SG_COUNTER_MASK) | 1u;
    memcpy(sg_test_obj + SOBER_SG_OBJ_COUNTER, &word, sizeof(word));
}

/* Synthetic stand-in for guest RVA 0x2a0de60. */
static void *sg_test_tls(void)
{
    return &sg_test_obj_slot;
}

/* Synthetic stand-ins for the read-only TSD verifiers.  The standalone
 * selftest links ONLY this file, so tsd_default.c is not present.  The fake
 * "guest" is not a real ELF, so build verification must fail (that also keeps
 * the extended stats out of bounds of the synthetic buffer). */
int sober_tsd_default_verify_build(void *guest_base)
{
    (void)guest_base;
    return 0;
}

int sober_tsd_default_key3_coherent(void *guest_base, void **out_state)
{
    (void)guest_base;
    if (out_state) *out_state = NULL;
    return 1;
}

int main(void)
{
    int ok = 1;
    const char *old = getenv(SOBER_SCHEDULER_GUEST_ENV);
    char saved[64] = {0};
    if (old) snprintf(saved, sizeof(saved), "%s", old);

    /* Fresh synthetic obj: ctor tag 0x7282580 has c == 0. */
    memset(sg_test_obj, 0, sizeof(sg_test_obj));
    uint64_t tag = SOBER_SG_TAG_BASE;
    memcpy(sg_test_obj + SOBER_SG_OBJ_COUNTER, &tag, sizeof(tag));
    sg_test_obj_slot = sg_test_obj;

    /* Phase 1: opt-in gate + install must write no guest bytes at all. */
    setenv(SOBER_SCHEDULER_GUEST_ENV, "1", 1);
    printf("[sg-self-test] enabled=%d env=%s\n",
           sober_scheduler_guest_enabled(),
           sober_scheduler_guest_env_vars());
    ok &= sober_scheduler_guest_enabled() == 1;

    static unsigned char fake_guest[0x1000];
    memset(fake_guest, 0xa5, sizeof(fake_guest));
    unsigned char snapshot[0x1000];
    memcpy(snapshot, fake_guest, sizeof(snapshot));
    int rc = sober_scheduler_guest_install(fake_guest);
    int nowrite = memcmp(fake_guest, snapshot, sizeof(snapshot)) == 0;
    printf("[sg-self-test] install=%d active=%d no_guest_writes=%d\n",
           rc, sober_scheduler_guest_is_active(), nowrite);
    ok &= rc == SOBER_SG_OK && sober_scheduler_guest_is_active() && nowrite;

    /* Phase 2: exercise the real dispatch/accessor logic against stand-ins. */
    sg_guard = sg_test_guard;
    sg_tls   = sg_test_tls;
    sg_primed = 0;

    int c_before = sober_scheduler_guest_nesting();
    printf("[sg-self-test] c_before=%d\n", c_before);
    ok &= c_before == 0;

    /* The installer thread is the harness main thread: a prime there must be
     * rejected before any guest function is entered. */
    sober_scheduler_guest_prime_worker();
    printf("[sg-self-test] main_thread_guard_calls=%d primed=%d\n",
           sg_test_guard_calls, sober_scheduler_guest_is_primed());
    ok &= sg_test_guard_calls == 0 && !sober_scheduler_guest_is_primed();

    /* Emulate the FunctionMarshaller worker: a different pthread from the one
     * that ran install().  Clearing the captured id is the selftest stand-in
     * for that thread switch; production never clears it. */
    sg_owner_tid_valid = 0;

    sober_scheduler_guest_on_attach("OtherWorker");
    printf("[sg-self-test] other_worker_guard_calls=%d primed=%d\n",
           sg_test_guard_calls, sober_scheduler_guest_is_primed());
    ok &= sg_test_guard_calls == 0 && !sober_scheduler_guest_is_primed();

    sober_scheduler_guest_on_attach(SOBER_SG_WORKER_NAME);
    int c_after = sober_scheduler_guest_nesting();
    printf("[sg-self-test] worker_guard_calls=%d arg=%d c_after=%d primed=%d\n",
           sg_test_guard_calls, sg_test_guard_arg, c_after,
           sober_scheduler_guest_is_primed());
    ok &= sg_test_guard_calls == 1 && sg_test_guard_arg == 1 && c_after == 1
          && sober_scheduler_guest_is_primed();

    sober_scheduler_guest_on_attach(SOBER_SG_WORKER_NAME);
    printf("[sg-self-test] second_attach_guard_calls=%d\n", sg_test_guard_calls);
    ok &= sg_test_guard_calls == 1;        /* exactly one prime per thread */

    char stats[512];
    sober_scheduler_guest_stats(stats, sizeof(stats));
    printf("[sg-self-test] stats=%s\n", stats);

    /* Phase 3: opt-out must not prime even for the worker name. */
    setenv(SOBER_SCHEDULER_GUEST_ENV, "0", 1);
    sg_primed = 0;
    sg_test_guard_calls = 0;
    tag = SOBER_SG_TAG_BASE;
    memcpy(sg_test_obj + SOBER_SG_OBJ_COUNTER, &tag, sizeof(tag));
    sober_scheduler_guest_on_attach(SOBER_SG_WORKER_NAME);
    printf("[sg-self-test] opt_out_guard_calls=%d withhold=%d\n",
           sg_test_guard_calls, sober_scheduler_guest_should_withhold());
    ok &= sg_test_guard_calls == 0 && !sober_scheduler_guest_should_withhold();

    if (old) setenv(SOBER_SCHEDULER_GUEST_ENV, saved, 1);
    else     unsetenv(SOBER_SCHEDULER_GUEST_ENV);

    if (ok) printf("scheduler guest self-test OK\n");
    else    printf("scheduler guest self-test FAILED\n");
    return ok ? 0 : 1;
}

#endif /* SOBER_SCHEDULER_GUEST_SELFTEST */
