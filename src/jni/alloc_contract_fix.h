/*
 * alloc_contract_fix.h - opt-in allocator-contract wrappers (worker A14).
 *
 * SCOPE: this header + alloc_contract_fix.c ONLY.  No CMake edits, no engine
 * edits, no guest byte patches, no callbacks, no decision-013 marker changes.
 * The lead owns bionic_bootstrap.c and wires the returned pointers into the
 * bridge table there.
 *
 * WHAT THIS FIXES (all from analysis/A07-allocator-audit.md, confirmed against
 * the frozen engine BuildID 204ad049..., SHA 3e498d6494...):
 *
 *   F1 (M1) - the 8th bridge (rebound calloc at 0x231f0e2) must write ZERO into
 *     the 16-byte header, not the total size.  The guest's own core
 *     0x231f13a does `xor r14d,r14d` at 0x231f1ac and `mov %r14,(%r12)` at
 *     0x231f243.  The header is a 0-vs-custom-handle discriminator consumed
 *     by the matched free thunk 0x231f53a, not a size field.  Our old bridge
 *     wrote `total` (0x1110 for the 0x1100 handle), which the special-free
 *     path 0x6a28bf4 dereferences as an address when both feature flags are
 *     set -> latent SIGSEGV.
 *
 *   F2 (M2) - the 7th bridge at 0x253225e is a PLAIN-POINTER calloc (guest
 *     0x25322ea tail-jumps to memset and returns the arena block directly).
 *     It must NOT be given the base+0x10 header form.  A separate plain
 *     zeroing wrapper is provided for that slot.
 *
 *   F3 (M3) - free / realloc / usable_size must subtract 0x10 when handed a
 *     header-form pointer (base+0x10 returned by the header calloc).  A tiny
 *     atomic registry of header-form `base` values distinguishes those from
 *     plain host_allocate pointers, so the correction is applied only when
 *     appropriate.  The guest's own free thunk 0x231f53a already subtracts
 *     0x10 and passes `base` to our free bridge; because we register `base`,
 *     that path also lands on the correct glibc pointer (see "WIRING" below).
 *
 * DEFAULT OFF: nothing is installed by this module and no allocation path
 * changes unless getenv("SOBER_ALLOC_CONTRACT_FIX") is exactly "1".  The gate
 * is consulted once by sober_alloc_contract_fix_ops(); the wrappers themselves
 * are pure implementations and are safe to call if wired explicitly.
 *
 * WIRING (lead, in bionic_bootstrap.c install_bridges()/patch_calloc_slot()):
 *
 *   const sober_alloc_contract_ops *o = sober_alloc_contract_fix_ops();
 *   if (o) {
 *       bridge[0x253225e].host  = (void (*)(void))o->calloc_plain;  // F2
 *       bridge[0x231f0e2].host  = (void (*)(void))o->calloc_header; // F1
 *       bridge[0x1dbe559].host  = (void (*)(void))o->free;          // F3
 *       bridge[0x227aafa].host  = (void (*)(void))o->realloc;       // F3
 *       bridge[0x1dbdfba].host  = (void (*)(void))o->usable_size;   // F3
 *       // patch_calloc_slot(0x726c0c0) should store o->calloc_header
 *       // instead of &host_calloc when the gate is on.
 *   }
 *
 * This module never writes guest memory; all patching remains the lead's
 * existing, build-ID-gated bridge machinery.
 *
 * LIMITS
 * -----
 *  - M4 (A07) is out of scope: a plain host_allocate pointer passed through
 *    the guest thunk 0x231f53a is decremented by the guest before it reaches
 *    our free bridge.  That pointer is not in our registry (we only register
 *    header-form bases), so it is freed as given.  No start/game-path pair for
 *    that case was confirmed in A07; it needs a runtime free-pointer trace.
 *  - If the 8192-entry registry ever fills with live header-form blocks, the
 *    header calloc fails closed (returns NULL) rather than handing out a
 *    pointer whose later free/realloc would be misclassified.
 *  - The gate is checked by sober_alloc_contract_fix_ops() at install time.
 *    Do not install the pointers when it returns NULL (default OFF).
 */
#ifndef SOBER_JNI_ALLOC_CONTRACT_FIX_H
#define SOBER_JNI_ALLOC_CONTRACT_FIX_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -fvisibility=hidden is used by the native build; publish explicitly so the
 * pointers can also be picked up by dlsym if the lead prefers that route. */
#if defined(__GNUC__) || defined(__clang__)
#  define SOBER_ALLOC_CONTRACT_EXPORT __attribute__((visibility("default")))
#else
#  define SOBER_ALLOC_CONTRACT_EXPORT
#endif

/* Build identity these RVAs belong to (informational guard only). */
#define SOBER_ALLOC_CONTRACT_BUILDID32 0x204ad049u

/* ---- opt-in ------------------------------------------------------------ */
#define SOBER_ALLOC_CONTRACT_FIX_ENV "SOBER_ALLOC_CONTRACT_FIX"

/* Default OFF: true only when getenv(SOBER_ALLOC_CONTRACT_FIX) == "1". */
SOBER_ALLOC_CONTRACT_EXPORT int sober_alloc_contract_fix_enabled(void);

/* Return the table of installable wrappers when enabled, NULL otherwise.
 * The returned pointer is static and valid for the process lifetime. */
SOBER_ALLOC_CONTRACT_EXPORT const struct sober_alloc_contract_ops *
sober_alloc_contract_fix_ops(void);

/* Human-readable gate description (for harness/env dumps). */
SOBER_ALLOC_CONTRACT_EXPORT const char *sober_alloc_contract_fix_env_vars(void);

/* ---- genuine guest RVAs (BuildID 204ad049 / SHA 3e498d6494...) ---------- */
#define SOBER_ALLOC_FIX_RVA_RAW_CALLOC       0x253225eu /* 7th bridge, plain  */
#define SOBER_ALLOC_FIX_RVA_HEADER_CALLOC    0x231f0e2u /* 8th bridge, header */
#define SOBER_ALLOC_FIX_RVA_HEADER_CALLCORE  0x231f13au /* core: n+0x10 alloc */
#define SOBER_ALLOC_FIX_RVA_HEADER_ZERO_A    0x231f1acu /* xor r14d,r14d      */
#define SOBER_ALLOC_FIX_RVA_HEADER_ZERO_B    0x231f243u /* mov %r14,(%r12)    */
#define SOBER_ALLOC_FIX_RVA_FREE             0x1dbe559u /* free bridge        */
#define SOBER_ALLOC_FIX_RVA_FREE_THUNK       0x231f53au /* guest -0x10 thunk  */
#define SOBER_ALLOC_FIX_RVA_REALLOC          0x227aafau /* realloc bridge     */
#define SOBER_ALLOC_FIX_RVA_USABLE_SIZE      0x1dbdfbau /* usable_size bridge */
#define SOBER_ALLOC_FIX_RVA_CALLOC_SLOT      0x726c0c0u /* rebind slot        */

/* Size of the header prefixed to every header-form allocation. */
#define SOBER_ALLOC_CONTRACT_HEADER_SIZE 0x10u

/* ---- wrapper signatures ------------------------------------------------ */
typedef void *(*sober_alloc_calloc_fn)(size_t nmemb, size_t size);
typedef void  (*sober_alloc_free_fn)(void *p);
typedef void *(*sober_alloc_realloc_fn)(void *p, size_t n);
typedef size_t (*sober_alloc_usable_size_fn)(void *p);

struct sober_alloc_contract_ops {
    sober_alloc_calloc_fn      calloc_header;  /* F1 -> 0x231f0e2 slot/bridge */
    sober_alloc_calloc_fn      calloc_plain;   /* F2 -> 0x253225e bridge      */
    sober_alloc_free_fn        free;           /* F3 -> 0x1dbe559 bridge      */
    sober_alloc_realloc_fn     realloc;        /* F3 -> 0x227aafa bridge      */
    sober_alloc_usable_size_fn usable_size;    /* F3 -> 0x1dbdfba bridge      */
};

/* Individual wrappers, exported so the lead can wire them one at a time
 * (A/B F1+F2 before F3).  Equivalent to the fields of the ops table. */
SOBER_ALLOC_CONTRACT_EXPORT void *sober_alloc_contract_calloc_header(
    size_t nmemb, size_t size);
SOBER_ALLOC_CONTRACT_EXPORT void *sober_alloc_contract_calloc_plain(
    size_t nmemb, size_t size);
SOBER_ALLOC_CONTRACT_EXPORT void sober_alloc_contract_free(void *p);
SOBER_ALLOC_CONTRACT_EXPORT void *sober_alloc_contract_realloc(
    void *p, size_t n);
SOBER_ALLOC_CONTRACT_EXPORT size_t sober_alloc_contract_usable_size(void *p);

/* ---- diagnostics ------------------------------------------------------- */
/* One-line status dump; returns buf. */
SOBER_ALLOC_CONTRACT_EXPORT const char *sober_alloc_contract_fix_describe(
    char *buf, size_t cap);
/* Counters (all zero when the module was never used). */
SOBER_ALLOC_CONTRACT_EXPORT uint64_t sober_alloc_contract_header_allocs(void);
SOBER_ALLOC_CONTRACT_EXPORT uint64_t sober_alloc_contract_plain_allocs(void);
SOBER_ALLOC_CONTRACT_EXPORT uint64_t sober_alloc_contract_registry_hits(void);
SOBER_ALLOC_CONTRACT_EXPORT uint64_t sober_alloc_contract_registry_misses(void);
SOBER_ALLOC_CONTRACT_EXPORT uint64_t sober_alloc_contract_realloc_moves(void);

#ifdef __cplusplus
}
#endif
#endif /* SOBER_JNI_ALLOC_CONTRACT_FIX_H */
