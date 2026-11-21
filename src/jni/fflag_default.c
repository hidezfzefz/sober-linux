/*
 * fflag_default.c - DEFAULT-ON replay of the genuine FFlag-registry static
 *                   constructor (init_array[0], RVA 0x2e62390).  See
 *                   fflag_default.h for the full evidence chain, the frozen
 *                   RVAs and the exact wiring signature for the lead.
 *
 * HARD RULES OBSERVED HERE
 * ------------------------
 *  - Default ON; kill switch SOBER_FFLAG_DEFAULT=0/off/no/false -> inert no-op.
 *  - The only state change is the guest's OWN ctor running its genuine
 *    initializer (0x2e62390 -> 0x2e618b0 -> 0x2e61280).  This module performs
 *    NO host byte writes, NO guest code patches, NO fake vtables, NO callbacks
 *    and NO decision-013 marker.  All module-side guest access is read-only.
 *  - Idempotent: the guest once-guard makes repeat calls harmless, and the
 *    module short-circuits as soon as the registry verifies valid.
 */
#define _GNU_SOURCE
#include "fflag_default.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

typedef void (*fflag_nullary_fn)(void);

static unsigned long g_replay_calls;   /* times we actually invoked the ctor */
static int            g_last_rc = SOBER_FFLAG_DEFAULT_DISABLED;

/* Default ON: disabled only when the kill switch is explicitly falsy. */
int sober_fflag_default_enabled(void)
{
    const char *e = getenv(SOBER_FFLAG_DEFAULT_ENV);
    if (!e)
        return 1;
    if (strcmp(e, "0") == 0 || strcasecmp(e, "off") == 0 ||
        strcasecmp(e, "no") == 0 || strcasecmp(e, "false") == 0)
        return 0;
    return 1;
}

const char *sober_fflag_default_env_vars(void)
{
    return SOBER_FFLAG_DEFAULT_ENV;
}

void sober_fflag_default_reset(void)
{
    g_replay_calls = 0;
    g_last_rc = SOBER_FFLAG_DEFAULT_DISABLED;
}

unsigned long sober_fflag_default_replay_count(void)
{
    return g_replay_calls;
}

int sober_fflag_default_last_rc(void)
{
    return g_last_rc;
}

uint32_t sober_fflag_default_guard(void *guest_base)
{
    if (!guest_base)
        return 0;
    return *(volatile uint32_t *)((unsigned char *)guest_base +
                                  SOBER_FFLAG_DEFAULT_RVA_GUARD);
}

void *sober_fflag_default_handle(void *guest_base)
{
    if (!guest_base)
        return NULL;
    return *(void *volatile *)((unsigned char *)guest_base +
                               SOBER_FFLAG_DEFAULT_RVA_HANDLE);
}

void *sober_fflag_default_control_block(void *guest_base)
{
    if (!guest_base)
        return NULL;
    /* The FFlag registry is a GLOBAL OBJECT at guest_base+0x72a0520, not a
     * pointer to a heap object.  ctor[0] initialises it in place: 0x2e613fd
     * stores the static vptr base+0x6db4008 at +0, and 0x2e5bc10 -> 0x2e7b850
     * installs the shared_ptr control block at the GLOBAL's +0x38.  Reading
     * *(handle_value + 0x38) is wrong: the handle value is a guest .rodata
     * address (base+0x6db4008) whose +0x38 is read-only, so it always read 0.
     * Read the field where the guest actually writes it. */
    return *(void *volatile *)((unsigned char *)guest_base +
                               SOBER_FFLAG_DEFAULT_RVA_HANDLE +
                               SOBER_FFLAG_DEFAULT_OFF_CTRL);
}

/* Read-only verification: guard==257, handle!=0, handle+0x38 != 0. */
int sober_fflag_default_verify(void *guest_base)
{
    if (!guest_base)
        return SOBER_FFLAG_DEFAULT_ERR;
    if (sober_fflag_default_guard(guest_base) != SOBER_FFLAG_DEFAULT_GUARD_EXPECTED)
        return SOBER_FFLAG_DEFAULT_VERIFY;
    if (!sober_fflag_default_handle(guest_base))
        return SOBER_FFLAG_DEFAULT_VERIFY;
    if (!sober_fflag_default_control_block(guest_base))
        return SOBER_FFLAG_DEFAULT_VERIFY;
    return SOBER_FFLAG_DEFAULT_OK;
}

/* The three constructors DECISION 008 skips that are NOT required. */
static const SoberFflagSkippedCtor k_skipped[] = {
    { 1u, 0x1dba8f3u,
      "protobuf defaults/descriptor pool; body 0x1dba8f8 already called lazily "
      "by replayed ctor[6] (0x1dbef4a: call 0x1dba8f8) -> no replay needed" },
    { 3u, 0x1db9a40u,
      "mimalloc usable-size/allocator feature probe (globals 0x7cdedd8/0x7cdede0); "
      "only reader 0x239eb14 selects a DFLog string -> no functional effect" },
    { 4u, 0x1dba6c4u,
      "monotonic-time baseline 0x7d9ce38 / done 0x7d9ce30 via clock_gettime; "
      "lazily self-initializing from guest time helper 0x1ea4370" }
};

const SoberFflagSkippedCtor *sober_fflag_default_skipped_ctors(size_t *count)
{
    if (count)
        *count = sizeof(k_skipped) / sizeof(k_skipped[0]);
    return k_skipped;
}

const char *sober_fflag_default_describe(void *guest_base, char *buf, size_t cap)
{
    if (!buf || cap == 0)
        return "";
    snprintf(buf, cap,
             "guard=%u handle=%p ctrl=%p replayed=%lu rc=%d "
             "ctor[0]=0x2e62390-required ctor1/3/4=not-required",
             (unsigned)sober_fflag_default_guard(guest_base),
             sober_fflag_default_handle(guest_base),
             sober_fflag_default_control_block(guest_base),
             g_replay_calls, g_last_rc);
    return buf;
}

int sober_fflag_default_replay(void *guest_base)
{
    int rc;

    if (!sober_fflag_default_enabled()) {
        g_last_rc = SOBER_FFLAG_DEFAULT_DISABLED;
        return SOBER_FFLAG_DEFAULT_DISABLED;
    }
    if (!guest_base) {
        g_last_rc = SOBER_FFLAG_DEFAULT_ERR;
        return SOBER_FFLAG_DEFAULT_ERR;
    }

    /* Already valid (e.g. a prior call or the legacy opt-in block ran)?  The
     * guest once-guard makes a repeat harmless, but avoid the extra call. */
    if (sober_fflag_default_verify(guest_base) == SOBER_FFLAG_DEFAULT_OK) {
        g_last_rc = SOBER_FFLAG_DEFAULT_OK;
        return SOBER_FFLAG_DEFAULT_OK;
    }

    /* Replay the genuine init_array[0] ctor: real guest code, no byte writes,
     * no callbacks, no JNIEnv.  It internally builds the registry and its
     * shared_ptr control block via 0x2e618b0 -> 0x2e61280 -> 0x2e7b850. */
    fflag_nullary_fn ctor = (fflag_nullary_fn)(uintptr_t)(
        (unsigned char *)guest_base + SOBER_FFLAG_DEFAULT_RVA_CTOR0);
    ++g_replay_calls;
    ctor();

    rc = sober_fflag_default_verify(guest_base);
    g_last_rc = rc;

    fprintf(stderr,
            "[fflag-default] replayed guest ctor 0x2e62390 (FFlag registry): "
            "guard=%u (expect %u) handle=%p ctrl=0x%38p -> %s; "
            "ctors 1/3/4 not required (protobuf defaults=lazy, "
            "mimalloc probe=DFLog-only, time baseline=lazy)\n",
            (unsigned)sober_fflag_default_guard(guest_base),
            (unsigned)SOBER_FFLAG_DEFAULT_GUARD_EXPECTED,
            sober_fflag_default_handle(guest_base),
            sober_fflag_default_control_block(guest_base),
            rc == SOBER_FFLAG_DEFAULT_OK ? "OK" : "VERIFY-FAIL");
    return rc;
}
