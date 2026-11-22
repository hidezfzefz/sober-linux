/*
 * scheduler_guest.h - opt-in TaskScheduler nesting prime via GENUINE guest code.
 *
 * T1-2 (logical Team 1, substrate).  NEW FILES ONLY: this header plus
 * scheduler_guest.c.  No shared src/, CMakeLists.txt, tests/, git or engine
 * edits.  Frozen engine: libroblox.so BuildID 204ad049..., SHA 3e498d6494...
 * All RVAs below are against the caller-supplied mapped base of THAT object.
 *
 * WHAT THIS MODULE IS
 * -------------------
 * A6 established that the state checked by the "257f718-Context,20" panic is
 * NOT guest+0x722e540.  It is a per-thread object
 *
 *     obj = *(TLS_block + 0x410)          (TLS_block via thread key 3)
 *
 * whose +0x70 word is a tagged pointer (base 0x7282580, 64-byte aligned) with
 * its LOW SIX BITS used as a nesting counter c = word & 0x3f.  The guest's own
 * enter/leave guard at RVA 0x2a132c0 maintains it; a "leave" (edi=0) panics at
 * RVA 0x2a13329 when c == 0.  The worker's failing leave is
 * 0x1e477e7 -> 0x227dde2 -> 0x227de0a -> 0x2a132c0(0).
 *
 * This module does exactly one thing: on the FunctionMarshaller worker, at the
 * point where the worker task is about to run (A6: the task thunk at RVA
 * 0x2a0daa0, immediately before the vtable task call at RVA 0x2a0daf7, hence
 * strictly BEFORE the release path above), it executes
 *
 *     ((void (*)(int))(guest_base + 0x2a132c0u))(1);
 *
 * so the guest ITSELF computes and stores obj+0x70 = (tag & ~0x3f) | 1 at RVA
 * 0x2a13332..0x2a1333c.  Nothing is written by us into guest memory, no guest
 * instruction/vtable/byte is patched, no callback is fabricated and no
 * crash-mode flag global is touched.  No matching harness "leave" is issued
 * (A6 section 5: one unconditional enter is benign for both enter/leave
 * pairings).  The marker stays WITHHELD; use sober_scheduler_guest_should_withhold()
 * in the lead-owned gate chain.
 *
 * The host-side hook point available without patching guest code is the
 * guest's OWN JavaVM->AttachCurrentThread(name="FunctionMarshaller") call: the
 * harness already sees that genuine JNI call on that exact thread.  Call
 * sober_scheduler_guest_on_attach(name) right after that attach completes; it
 * primes only on that name.  If the lead has a different host hook on the
 * worker immediately before RVA 0x2a0daf7, call
 * sober_scheduler_guest_prime_worker() there instead.
 *
 * PRECONDITIONS (lead-owned, not performed here): the corrupting scheduler
 * "neutralize" writes must be off, the Bionic TSD GOT must be consistent
 * (SOBER_BIONIC_TSD_GOT=1) and the TSD backing must be stable, otherwise the
 * guest's own 0x2a0de60 calls inside 0x2a132c0 cannot materialise obj.
 *
 * OPT-IN: every function is a no-op (or returns SOBER_SG_DISABLED) unless
 * getenv("SOBER_SCHEDULER_GUEST") is exactly "1".  The env var name is exported
 * as SOBER_SCHEDULER_GUEST_ENV.
 *
 * INTEGRATION (lead-owned call sites, no CMake edits needed in this module):
 *
 *   #include "scheduler_guest.h"
 *   ... once, after the engine base is known:
 *       sober_scheduler_guest_install(guest_base);
 *   ... inside the genuine JavaVM AttachCurrentThread path, on the attaching
 *       thread, after the guest attach has completed:
 *       sober_scheduler_guest_on_attach(name);
 *   ... read-only confirmation on that worker:
 *       int c = sober_scheduler_guest_nesting();   // (obj+0x70)&0x3f, -1 on error
 *       char b[256]; sober_scheduler_guest_stats(b, sizeof(b));
 *   ... decision-013 gate chain (marker stays withheld):
 *       seeded_state |= sober_scheduler_guest_should_withhold();
 *
 * SELF-TEST (no guest, synthetic stand-ins; compiles with -Werror):
 *   gcc -std=gnu17 -Wall -Wextra -Werror -Isrc/jni \
 *       -DSOBER_SCHEDULER_GUEST_SELFTEST \
 *       src/jni/scheduler_guest.c -o /tmp/sg_selftest -pthread
 *   /tmp/sg_selftest
 */
#ifndef SOBER_JNI_SCHEDULER_GUEST_H
#define SOBER_JNI_SCHEDULER_GUEST_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef SOBER_SG_API
#define SOBER_SG_API __attribute__((visibility("default")))
#endif

/* ---- opt-in ------------------------------------------------------------ */
#define SOBER_SCHEDULER_GUEST_ENV "SOBER_SCHEDULER_GUEST"
#define SOBER_SG_BUILDID32        0x204ad049u   /* informational build guard */
#define SOBER_SG_WORKER_NAME      "FunctionMarshaller"

/* ---- frozen-engine RVAs (RVA == file offset in the first R E PT_LOAD) --- */
#define SOBER_SG_RVA_GUARD_ENTER     0x2a132c0u /* nesting enter/leave routine */
#define SOBER_SG_RVA_TLS_ACCESSOR    0x2a0de60u /* returns TLS_block + 0x410   */
#define SOBER_SG_RVA_WORKER_THUNK    0x2a0daa0u /* " RBX Worker" task thunk    */
#define SOBER_SG_RVA_TASK_CALL       0x2a0daf7u /* `call *0x10(%rbx)` <= HOOK  */
#define SOBER_SG_RVA_GUARD_UPDATE    0x2a13332u /* guest writes the counter    */
#define SOBER_SG_RVA_GUARD_UNDERFLOW 0x2a13329u /* "257f718-Context,20" arm     */
#define SOBER_SG_RVA_ACQUIRE         0x1e4781cu /* scoped-ref acquire          */
#define SOBER_SG_RVA_ACQUIRE_ENTER   0x1e4786bu /* guest's own enter call       */
#define SOBER_SG_RVA_RELEASE         0x227dde2u /* scoped-ref release           */
#define SOBER_SG_RVA_RELEASE_LEAVE   0x227de0au /* guest's own leave (panicked) */
#define SOBER_SG_RVA_TLS_KEY         0x72a1b08u /* Bionic key global (value 3)  */
#define SOBER_SG_RVA_TLS_DESC_1      0x722e540u /* descriptor #1 (NOT scheduler)*/
#define SOBER_SG_RVA_TLS_DESC_2      0x722e560u /* descriptor #2               */

/* Guest data layout used by 0x2a132c0 and by the verification accessor. */
#define SOBER_SG_TLS_OBJ_SLOT        0x410u     /* slot holding obj*            */
#define SOBER_SG_OBJ_COUNTER         0x70u      /* tagged word inside obj        */
#define SOBER_SG_COUNTER_MASK        0x3fu      /* low 6 bits = counter          */
#define SOBER_SG_TAG_BASE            0x7282580u /* tag base; INFORMATIONAL ONLY  */

/* ---- return codes ------------------------------------------------------- */
#define SOBER_SG_OK          0
#define SOBER_SG_DISABLED    1    /* opt-in env var is not "1"                */
#define SOBER_SG_ERR       (-1)   /* bad argument                             */

/* True only when getenv(SOBER_SCHEDULER_GUEST_ENV) == "1". */
SOBER_SG_API int sober_scheduler_guest_enabled(void);

/* Human-readable opt-in description (for harness/env dumps). */
SOBER_SG_API const char *sober_scheduler_guest_env_vars(void);

/* One-time opt-in install; resolves (does not modify) the two guest entry
 * points RVA 0x2a132c0 and 0x2a0de60.  Returns SOBER_SG_OK, SOBER_SG_DISABLED
 * or SOBER_SG_ERR.  Idempotent; writes ZERO bytes to guest memory. */
SOBER_SG_API int sober_scheduler_guest_install(void *guest_base);

/* Hook called from the genuine JavaVM AttachCurrentThread path, on the thread
 * that is attaching, after the guest attach completes.  Primes the worker only
 * when thread_name == "FunctionMarshaller".  No-op when the opt-in is off. */
SOBER_SG_API void sober_scheduler_guest_on_attach(const char *thread_name);

/* Prime the CALLING thread with the guest's own enter: exactly one execution
 * of guest_base+0x2a132c0 with edi=1, at most once per thread.  This is the
 * primitive to call from any host hook that runs on the worker immediately
 * before guest RVA 0x2a0daf7.  No matching harness leave is issued. */
SOBER_SG_API void sober_scheduler_guest_prime_worker(void);

/* Read-only verification accessor.  Calls the genuine guest TLS accessor at
 * RVA 0x2a0de60, computes obj = *(slot) where slot = TLS_block+0x410, and
 * returns (obj+0x70) & 0x3f.  Returns -1 on any null/error; -1 is also the
 * value before install. */
SOBER_SG_API int sober_scheduler_guest_nesting(void);

/* Non-zero once install() actually resolved the guest entry points. */
SOBER_SG_API int sober_scheduler_guest_is_active(void);

/* Read-only live coherence check of the guest's Bionic key 3 on the calling
 * thread (delegates to sober_tsd_default_key3_coherent).  *out_state receives
 * this thread's guest-built TLS state, or NULL if the guest has not built it
 * yet.  Returns 0 when SOBER_TSD_DEFAULT=0 is set, the build does not match,
 * key3 is not 3, or the get/setspecific GOT slots are split. */
SOBER_SG_API int sober_scheduler_guest_tsd_coherent(void *out_state);

/* Non-zero when this thread has already been primed (and opt-in is on). */
SOBER_SG_API int sober_scheduler_guest_is_primed(void);

/* Force-withhold signal for the lead's decision-013 gate chain.  This module
 * NEVER writes the marker itself. */
SOBER_SG_API int sober_scheduler_guest_should_withhold(void);

/* Cumulative, thread-safe one-line diagnostics for the A/B report. */
SOBER_SG_API void sober_scheduler_guest_stats(char *buf, size_t cap);

#ifdef __cplusplus
}
#endif
#endif /* SOBER_JNI_SCHEDULER_GUEST_H */
