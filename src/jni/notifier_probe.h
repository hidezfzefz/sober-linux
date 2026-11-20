/*
 * notifier_probe.h - opt-in, observation-only instrumentation of the genuine
 * GameLoaded notifier and its sibling descriptor invoke in the frozen engine.
 *
 * SCOPE (strict)
 * --------------
 * NEW files only: this file + notifier_probe.c.  No CMake edits, no engine
 * edits, no edits to any shared src/ file, no tests/ edits, no marker edits.
 * The engine object is the frozen AppImage object
 *   /tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so
 *   SHA-256 3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335
 *   BuildID 204ad049599c96afb75b8a21d5051063e821d6c9  (SOBER_NP_BUILDID32).
 * Every address below is an RVA against the caller-supplied mapped base of
 * THAT object (RVA == file offset in its first R E PT_LOAD).
 *
 * WHAT THIS IS / IS NOT
 * ---------------------
 * This module installs a verified near trampoline (mprotect RWX -> write ->
 * restore RX) at two genuine guest sites so a hit counter, the RSI argument,
 * and the thunk return-address RVA can be *observed*.  It is a DIAGNOSTIC.
 *
 *  - It NEVER emits a guest->Java callback.  The handler writes counters only;
 *    it never calls 0x25848e0 / any JNI slot / any bridge function.
 *  - It NEVER emits or suppresses `app shell rendered` and never touches the
 *    decision-013 gate state.
 *  - It patches guest bytes, therefore under decision 013 / C09 section 4 it
 *    MUST be FORCE-WITHHELD by the lead: enabling SOBER_NOTIFIER_PROBE=1 must
 *    force shell_cb = 0.  The lead owns adding the env var (or the
 *    sober_notifier_probe_should_withhold() query) to the seeded_state OR-chain
 *    in guest_app_shell.c.  This module does not make that gate change itself.
 *  - Enabling it is NOT evidence for the marker.  It only *collects* evidence
 *    (notifier hit + placeId + expected thunk return address) for the lead's
 *    independent, lead-owned corroboration at the JNI dispatch.
 *
 * DEFAULT OFF: nothing is mapped, patched, or recorded unless
 * getenv("SOBER_NOTIFIER_PROBE") is exactly "1".
 */
#ifndef SOBER_JNI_NOTIFIER_PROBE_H
#define SOBER_JNI_NOTIFIER_PROBE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Informational build guard (first 32 bits of the frozen BuildID). */
#define SOBER_NP_BUILDID32 0x204ad049u

/* ---- opt-in ------------------------------------------------------------ */
#define SOBER_NOTIFIER_PROBE_ENV "SOBER_NOTIFIER_PROBE"

/* Default OFF: true only when getenv(SOBER_NOTIFIER_PROBE_ENV) == "1". */
int         sober_notifier_probe_enabled(void);
/* Human-readable opt-in description (for harness/env dumps). */
const char *sober_notifier_probe_env_vars(void);

/* ---- genuine guest RVAs (frozen engine BuildID 204ad049 / SHA 3e498d6494) */
#define SOBER_NP_RVA_DESCRIPTOR_INVOKE 0x27eaf4cu /* sibling descriptor invoke */
#define SOBER_NP_RVA_NOTIFIER_ENTRY    0x27eaf6eu /* GameLoaded notifier: RSI=placeId */
#define SOBER_NP_RVA_THUNK_CALL        0x27eafaeu /* `call 0x25848e0` in notifier  */
#define SOBER_NP_RVA_THUNK_RET         0x27eafb3u /* return addr after that call   */
#define SOBER_NP_RVA_THUNK             0x25848e0u /* generic JNI variadic thunk    */

/* Expected prologue bytes, verified with memcmp BEFORE any write.
 *   0x27eaf6e: 55 48 89 e5 | 41 57 | 41 56 | 53           (push rbp / mov
 *              rbp,rsp / push r15 / push r14 / push rbx)   -> 9 bytes, all
 *              position-independent.
 *   0x27eaf4c: 48 89 f8 | 48 8b 4f 08                     (mov rax,rdi /
 *              mov rcx,[rdi+8])                           -> 7 bytes, all
 *              position-independent.
 * If these bytes do not match, no patch is installed (fail closed). */
#define SOBER_NP_NOTIFIER_PROLOGUE_LEN   9u
#define SOBER_NP_DESCRIPTOR_PROLOGUE_LEN 7u

/* Return codes for install/uninstall. */
#define SOBER_NP_OK          0
#define SOBER_NP_DISABLED    1   /* opt-in env var not "1"                       */
#define SOBER_NP_ERR        (-1) /* bad arg / prologue mismatch / mmap/mprotect  */

/* Observation snapshot.  Values are cumulative for the process lifetime.
 * `thunk_ret_addr` is base + 0x27eafb3 once installed (0 otherwise); the
 * installer verifies at 0x27eafae that the `call rel32` really targets the
 * thunk 0x25848e0, so this is a checked RVA, not a fabricated constant. */
typedef struct {
    uint64_t   hits_notifier;      /* hits at 0x27eaf6e                         */
    uint64_t   hits_descriptor;    /* hits at 0x27eaf4c                         */
    uint64_t   last_place_id;      /* RSI captured at last notifier hit         */
    uint64_t   last_descriptor_rsi;/* RSI captured at last descriptor hit       */
    uintptr_t  guest_base;         /* base passed to install                    */
    uintptr_t  thunk_ret_addr;     /* base + 0x27eafb3 (0 until installed)      */
    int        installed;          /* 1 while trampolines are live              */
} SoberNotifierProbeSnapshot;

/* ---- lead-facing API --------------------------------------------------- *
 * ONE-LINE HOOKS FOR THE LEAD (lead owns these call sites):
 *
 *   After the guest base is known (e.g. in guest_app_shell.c, once the engine
 *   is mapped and before the introducer sequence), install the probe:
 *
 *       int rc = sober_notifier_probe_install(guest_base);
 *       // rc: SOBER_NP_OK / SOBER_NP_DISABLED / SOBER_NP_ERR
 *
 *   In the decision-013 seeded_state OR-chain, force the marker withheld:
 *
 *       seeded_state |= sober_notifier_probe_should_withhold();
 *       // or simply add getenv("SOBER_NOTIFIER_PROBE") to the knob list
 *
 *   At teardown, restore the exact original prologue bytes (idempotent):
 *
 *       sober_notifier_probe_uninstall();
 *
 *   For logs / evidence dumps:
 *
 *       SoberNotifierProbeSnapshot s;
 *       sober_notifier_probe_snapshot(&s);
 *       char buf[256]; sober_notifier_probe_describe(buf, sizeof(buf));
 * ------------------------------------------------------------------------ */

/* Install the verified trampolines.  No-op returning SOBER_NP_DISABLED unless
 * SOBER_NOTIFIER_PROBE=1.  Idempotent: a second call while installed returns
 * SOBER_NP_OK.  Verifies both prologues AND the 0x27eafae -> 0x25848e0 call
 * context before writing anything; any mismatch/mmap/mprotect failure rolls
 * back and returns SOBER_NP_ERR with no bytes left patched. */
int sober_notifier_probe_install(void *guest_base);

/* Restore the original prologue bytes at both sites and unmap the trampolines.
 * Idempotent and safe to call when never installed.  Counters and
 * thunk_ret_addr are intentionally preserved as evidence after uninstall. */
int sober_notifier_probe_uninstall(void);

/* 1 when trampolines are currently live. */
int sober_notifier_probe_active(void);

/* 1 when the probe is enabled, installed, or has recorded any hit.  The lead
 * ORs this into the decision-013 withhold chain (force-withhold). */
int sober_notifier_probe_should_withhold(void);

/* ---- diagnostics ------------------------------------------------------- */
uint64_t   sober_notifier_probe_hits_notifier(void);
uint64_t   sober_notifier_probe_hits_descriptor(void);
uint64_t   sober_notifier_probe_last_place_id(void);
uintptr_t  sober_notifier_probe_thunk_return_addr(void);
void       sober_notifier_probe_snapshot(SoberNotifierProbeSnapshot *out);
/* Fill buf with a one-line status dump; returns buf. */
const char *sober_notifier_probe_describe(char *buf, size_t cap);

#ifdef __cplusplus
}
#endif
#endif /* SOBER_JNI_NOTIFIER_PROBE_H */
