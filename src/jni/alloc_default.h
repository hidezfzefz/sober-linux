/*
 * alloc_default.h - DEFAULT-ON allocator-contract wrappers (substrate T1-5).
 *
 * SCOPE: this header + alloc_default.c ONLY.  No CMake edits, no engine edits,
 * no guest byte patches, no callbacks, no decision-013 marker changes.  The
 * lead owns bionic_bootstrap.c and wires the returned pointers into the eight
 * bridges and the calloc slot there.
 *
 * WHY THIS MODULE EXISTS
 * ----------------------
 * A3/A7 identified three concrete deviations in the frozen engine's host
 * allocator bridges (BuildID 204ad049..., SHA 3e498d6494...):
 *
 *   F1 (M1) - the 8th bridge / calloc slot writes `total` (n+0x10) into the
 *     16-byte header, but the guest's own core 0x231f13a stores ZERO at
 *     0x231f1ac (`xor r14d,r14d`) and 0x231f243 (`mov %r14,(%r12)`).  The
 *     header is a 0-vs-custom-handle discriminator consumed by the matched
 *     free thunk 0x231f53a, NOT a size field.  A nonzero header makes the
 *     special-free path 0x6a28bf4 dereference a near-zero "handle".
 *
 *   F2 (M2) - the 7th bridge at 0x253225e is a PLAIN-POINTER calloc (guest
 *     0x25322ea tail-jumps to memset and returns the arena block directly).
 *     It must NOT get the base+0x10 header form.
 *
 *   F3 (M3) - free / realloc / usable_size must subtract 0x10 when handed a
 *     header-form pointer.  A tiny atomic registry of header-form `base`
 *     values distinguishes those from plain host pointers, so the correction
 *     is applied only when appropriate.  (The guest's own 0x231f53a thunk
 *     already subtracts 0x10 and hands us `base`; because we register `base`,
 *     that path also lands on the correct glibc pointer.)
 *
 * Unlike the opt-in alloc_contract_fix module (default OFF), THIS module is
 * DEFAULT-ON: the correct pointers are supplied unless the operator explicitly
 * sets SOBER_ALLOC_DEFAULT=0 (kill switch).  The gate is consulted once by
 * sober_alloc_default_ops(); the wrappers themselves are pure implementations
 * and are safe to call if wired explicitly.
 *
 * WIRING (lead, in bionic_bootstrap.c install_bridges()/patch_calloc_slot()):
 *
 *   const struct sober_alloc_default_ops *o = sober_alloc_default_ops();
 *   if (o) {
 *       bridge[0x1dbab02].host  = (void (*)(void))o->allocate;       // plain
 *       bridge[0x1dbad62].host  = (void (*)(void))o->alloc_small;    // plain
 *       bridge[0x1dbe559].host  = (void (*)(void))o->free;           // F3
 *       bridge[0x1dbdfba].host  = (void (*)(void))o->usable_size;    // F3
 *       bridge[0x227aafa].host  = (void (*)(void))o->realloc;        // F3
 *       bridge[0x1dd60e5].host  = (void (*)(void))o->aligned_alloc;  // plain
 *       bridge[0x253225e].host  = (void (*)(void))o->calloc_plain;   // F2
 *       bridge[0x231f0e2].host  = (void (*)(void))o->calloc_header;  // F1
 *       // patch_calloc_slot(0x726c0c0) stores sober_alloc_default_slot_target()
 *       // (== o->calloc_header) instead of &host_calloc when the gate is on.
 *   }
 *
 * This module never writes guest memory; all patching remains the lead's
 * build-ID-gated bridge machinery.  It uses glibc malloc/calloc/realloc/free
 * consistently (it does NOT mix in mimalloc), because the -0x10 registry and
 * malloc_usable_size() are only correct against the same allocator family.
 *
 * LIMITS
 * -----
 *  - A plain host_allocate pointer passed through the guest thunk 0x231f53a
 *    is decremented by the guest before reaching our free bridge.  That base
 *    is not in our registry (we only register header-form bases), so it is
 *    freed as given (A07 M4; needs a runtime free-pointer trace to close).
 *  - If the 8192-entry registry fills with live header-form blocks, the header
 *    calloc fails closed (returns NULL) rather than handing out a pointer
 *    whose later free/realloc would be misclassified.
 */
#ifndef SOBER_JNI_ALLOC_DEFAULT_H
#define SOBER_JNI_ALLOC_DEFAULT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -fvisibility=hidden is used by the native build; publish explicitly so the
 * pointers can also be picked up by dlsym if the lead prefers that route. */
#if defined(__GNUC__) || defined(__clang__)
#  define SOBER_ALLOC_DEFAULT_EXPORT __attribute__((visibility("default")))
#else
#  define SOBER_ALLOC_DEFAULT_EXPORT
#endif

/* Build identity these RVAs belong to (informational guard only). */
#define SOBER_ALLOC_DEFAULT_BUILDID32 0x204ad049u

/* ---- default-on gate / kill switch -------------------------------------- */
#define SOBER_ALLOC_DEFAULT_ENV "SOBER_ALLOC_DEFAULT"

/* DEFAULT ON: false only when getenv(SOBER_ALLOC_DEFAULT) == "0". */
SOBER_ALLOC_DEFAULT_EXPORT int sober_alloc_default_enabled(void);

/* Return the table of installable wrappers when enabled, NULL when killed.
 * The returned pointer is static and valid for the process lifetime. */
SOBER_ALLOC_DEFAULT_EXPORT const struct sober_alloc_default_ops *
sober_alloc_default_ops(void);

/* Human-readable gate description (for harness/env dumps). */
SOBER_ALLOC_DEFAULT_EXPORT const char *sober_alloc_default_env_vars(void);

/* ---- genuine guest RVAs (BuildID 204ad049 / SHA 3e498d6494...) ---------- */
#define SOBER_ALLOC_DEFAULT_RVA_ALLOCATE        0x1dbab02u /* plain alloc    */
#define SOBER_ALLOC_DEFAULT_RVA_ALLOC_SMALL     0x1dbad62u /* plain alloc    */
#define SOBER_ALLOC_DEFAULT_RVA_FREE            0x1dbe559u /* F3 free bridge */
#define SOBER_ALLOC_DEFAULT_RVA_USABLE_SIZE     0x1dbdfbau /* F3 size bridge */
#define SOBER_ALLOC_DEFAULT_RVA_REALLOCATE      0x227aafau /* F3 realloc     */
#define SOBER_ALLOC_DEFAULT_RVA_ALIGNED         0x1dd60e5u /* plain aligned  */
#define SOBER_ALLOC_DEFAULT_RVA_CALLOC_PLAIN    0x253225eu /* 7th, plain     */
#define SOBER_ALLOC_DEFAULT_RVA_CALLOC_HEADER   0x231f0e2u /* 8th, header    */
#define SOBER_ALLOC_DEFAULT_RVA_HEADER_ZERO_A   0x231f1acu /* xor r14d,r14d  */
#define SOBER_ALLOC_DEFAULT_RVA_HEADER_ZERO_B   0x231f243u /* mov %r14,(%r12)*/
#define SOBER_ALLOC_DEFAULT_RVA_FREE_THUNK      0x231f53au /* guest -0x10    */
#define SOBER_ALLOC_DEFAULT_RVA_CALLOC_SLOT     0x726c0c0u /* rebind slot    */

/* Size of the header prefixed to every header-form allocation. */
#define SOBER_ALLOC_DEFAULT_HEADER_SIZE 0x10u

/* ---- wrapper signatures ------------------------------------------------- */
typedef void  *(*sober_alloc_default_alloc_fn)(size_t n);
typedef void  *(*sober_alloc_default_aligned_fn)(size_t alignment, size_t n);
typedef void  *(*sober_alloc_default_calloc_fn)(size_t nmemb, size_t size);
typedef void   (*sober_alloc_default_free_fn)(void *p);
typedef void  *(*sober_alloc_default_realloc_fn)(void *p, size_t n);
typedef size_t (*sober_alloc_default_usable_size_fn)(void *p);

/* One pointer per guest bridge, in the same order as install_bridges(). */
struct sober_alloc_default_ops {
    sober_alloc_default_alloc_fn       allocate;      /* -> 0x1dbab02 */
    sober_alloc_default_alloc_fn       alloc_small;   /* -> 0x1dbad62 */
    sober_alloc_default_free_fn        free;          /* -> 0x1dbe559 (F3) */
    sober_alloc_default_usable_size_fn usable_size;   /* -> 0x1dbdfba (F3) */
    sober_alloc_default_realloc_fn     realloc;       /* -> 0x227aafa (F3) */
    sober_alloc_default_aligned_fn     aligned_alloc; /* -> 0x1dd60e5 */
    sober_alloc_default_calloc_fn      calloc_plain;  /* -> 0x253225e (F2) */
    sober_alloc_default_calloc_fn      calloc_header; /* -> 0x231f0e2 (F1) */
};

/* Individual wrappers (identical to the ops table fields), exported so the
 * lead can wire them one slot at a time. */
SOBER_ALLOC_DEFAULT_EXPORT void *sober_alloc_default_allocate(size_t n);
SOBER_ALLOC_DEFAULT_EXPORT void *sober_alloc_default_alloc_small(size_t n);
SOBER_ALLOC_DEFAULT_EXPORT void sober_alloc_default_free(void *p);
SOBER_ALLOC_DEFAULT_EXPORT void *sober_alloc_default_realloc(void *p, size_t n);
SOBER_ALLOC_DEFAULT_EXPORT size_t sober_alloc_default_usable_size(void *p);
SOBER_ALLOC_DEFAULT_EXPORT void *sober_alloc_default_aligned_alloc(
    size_t alignment, size_t n);
SOBER_ALLOC_DEFAULT_EXPORT void *sober_alloc_default_calloc_plain(
    size_t nmemb, size_t size);
SOBER_ALLOC_DEFAULT_EXPORT void *sober_alloc_default_calloc_header(
    size_t nmemb, size_t size);

/* The value to store into guest slot 0x726c0c0: same function as the 8th
 * (header-form) calloc bridge.  Typed so it is -Wpedantic clean; cast to void*
 * at the patch site. */
SOBER_ALLOC_DEFAULT_EXPORT sober_alloc_default_calloc_fn
sober_alloc_default_slot_target(void);

/* ---- diagnostics -------------------------------------------------------- */
/* One-line status dump; returns buf. */
SOBER_ALLOC_DEFAULT_EXPORT const char *sober_alloc_default_describe(
    char *buf, size_t cap);
SOBER_ALLOC_DEFAULT_EXPORT uint64_t sober_alloc_default_header_allocs(void);
SOBER_ALLOC_DEFAULT_EXPORT uint64_t sober_alloc_default_plain_allocs(void);
SOBER_ALLOC_DEFAULT_EXPORT uint64_t sober_alloc_default_registry_hits(void);
SOBER_ALLOC_DEFAULT_EXPORT uint64_t sober_alloc_default_registry_misses(void);
SOBER_ALLOC_DEFAULT_EXPORT uint64_t sober_alloc_default_realloc_moves(void);
SOBER_ALLOC_DEFAULT_EXPORT uint64_t sober_alloc_default_registry_full(void);

/* Built-in contract check: 0 on success, number of failed checks otherwise;
 * writes a one-line result into buf when non-NULL.  Intended for the harness
 * self-test; does not touch guest memory and is safe in any env gating state. */
SOBER_ALLOC_DEFAULT_EXPORT int sober_alloc_default_selftest(
    char *buf, size_t cap);

#ifdef __cplusplus
}
#endif
#endif /* SOBER_JNI_ALLOC_DEFAULT_H */
