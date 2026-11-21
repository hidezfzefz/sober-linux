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
static __thread int sg_primed;   /* one prime per thread, set before the call */

/* Cumulative counters (relaxed atomics; A/B diagnostics only, no mechanism). */
static long sg_attach_calls;
static long sg_worker_attaches;
static long sg_prime_calls;
static long sg_prime_skips;
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
    sg_installed = 1;
    return SOBER_SG_OK;
}

void sober_scheduler_guest_prime_worker(void)
{
    if (!sg_opted_in() || !sg_installed || !sg_guard) return;
    if (sg_primed) {
        (void)sg_add(&sg_prime_skips, 1);
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
    snprintf(buf, cap,
             "enabled=%d active=%d primed=%d attach=%ld worker=%ld "
             "prime=%ld skip=%ld reads=%ld read_err=%ld last_c=%ld "
             "enter_rva=0x%x tls_rva=0x%x base=%p",
             sober_scheduler_guest_enabled(), sg_installed,
             sober_scheduler_guest_is_primed(),
             sg_get(&sg_attach_calls), sg_get(&sg_worker_attaches),
             sg_get(&sg_prime_calls), sg_get(&sg_prime_skips),
             sg_get(&sg_nesting_reads), sg_get(&sg_nesting_errors),
             sg_get(&sg_last_nesting),
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

    char stats[256];
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
