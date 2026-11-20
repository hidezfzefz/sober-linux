/*
 * bionic_bootstrap.h - public C interface for the native Bionic bootstrap.
 *
 * OWNER: worker-1 (task #375).  Header path to publish in handoffs:
 *   src/compat/bionic_bootstrap/bionic_bootstrap.h
 *
 * This library performs, fully natively on Linux, the runtime patching the
 * Android loader would have done for libroblox.so before its init_array runs:
 *   (a) redirect the six mimalloc host bridges to native allocators,
 *   (b) seed the allocator object, JNI singleton, empty string and pre-JNI
 *       registry (four BSS-like slots with guest initializer functions),
 *   (c) optionally replay the validated init_array constructor ranges.
 *
 * NO Android runtime (basalt/mocktail), NO translation layer.  Every patch is
 * gated on the mapped object's NT_GNU_BUILD_ID == 204ad049... (AppImage build).
 *
 * Integration contract (DECISION D): worker-3 calls one of these from a point
 * that runs AFTER the guest is mapped + relocated but BEFORE (or instead of)
 * its init_array.  Two supported modes:
 *
 *   MODE A - dependency constructor + ordinary dlopen (recommended):
 *     Add a constructor to one of the guest's NDK stub DSOs that calls
 *       sober_bionic_bootstrap_run(NULL);
 *     NULL means "discover the mapped guest via dl_iterate_phdr and verify its
 *     build ID".  Stub constructors run before the guest's own init_array, so
 *     the seeds/bridges are in place when the guest constructors execute.
 *     Do NOT also replay manually in this mode (the loader runs init_array).
 *
 *   MODE B - custom native loader / manual init replay:
 *     After mapping + relocating the guest yourself:
 *       int rc = sober_bionic_bootstrap_run(guest_base);
 *     This calls apply() + replay_constructors() (ranges [2,3)+[5,3618)),
 *     which is the mocktail-equivalent constructor schedule.  In this mode
 *     you must suppress glibc's automatic init_array (do not use dlopen()).
 *
 * All functions are thread-safe for one-time application (guarded by an
 * atomic once-flag); repeated calls are no-ops returning 0.
 */
#ifndef SOBER_BIONIC_BOOTSTRAP_H
#define SOBER_BIONIC_BOOTSTRAP_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The native build uses -fvisibility=hidden; every symbol worker-3 binds via
 * weak refs / dlsym(RTLD_DEFAULT) must be explicitly published. */
#if defined(__GNUC__) || defined(__clang__)
#  define SOBER_BOOTSTRAP_EXPORT __attribute__((visibility("default")))
#else
#  define SOBER_BOOTSTRAP_EXPORT
#endif

/* Progress markers for fault localisation.  replay_constructors() sets these
 * immediately BEFORE invoking each entry, so a SIGSEGV handler can report the
 * exact index and guest RVA that faulted. */
extern SOBER_BOOTSTRAP_EXPORT volatile unsigned sober_bootstrap_ctors_done;
extern SOBER_BOOTSTRAP_EXPORT volatile unsigned long long sober_bootstrap_ctors_current_rva;
/* apply() phase: 1=verify 2=bridges 3=seeds 100=done. */
extern SOBER_BOOTSTRAP_EXPORT volatile unsigned sober_bootstrap_phase;
extern SOBER_BOOTSTRAP_EXPORT volatile unsigned sober_bootstrap_bridge_index;
extern SOBER_BOOTSTRAP_EXPORT volatile unsigned sober_bootstrap_seed_index;

/* Return codes. */
#define SOBER_BIONIC_OK                   0
#define SOBER_BIONIC_ERR_NO_GUEST        (-1) /* guest not found in link map      */
#define SOBER_BIONIC_ERR_BUILD_MISMATCH  (-2) /* build ID != 204ad049...          */
#define SOBER_BIONIC_ERR_BAD_ELF         (-3) /* not the expected x86-64 ET_DYN   */
#define SOBER_BIONIC_ERR_BAD_BASE        (-4) /* NULL/invalid guest base          */
#define SOBER_BIONIC_ERR_PROTECT         (-5) /* mprotect failed                  */
#define SOBER_BIONIC_ERR_FINGERPRINT     (-6) /* bridge prologue mismatch         */
#define SOBER_BIONIC_ERR_INIT_ARRAY      (-7) /* init_array/ctor lookup failed    */
#define SOBER_BIONIC_ERR_ALREADY_DIRTY   (-8) /* guest already patched elsewhere  */

/* Return a human-readable description of the last error on this thread. */
SOBER_BOOTSTRAP_EXPORT const char *sober_bionic_bootstrap_last_error(void);

/*
 * Verify that `guest_base` is the AppImage engine build we support, by reading
 * its PT_NOTE (NT_GNU_BUILD_ID) and ELF header.  Returns SOBER_BIONIC_OK if it
 * matches.  Does not mutate the guest.  `guest_base` may be NULL to discover
 * the guest via dl_iterate_phdr().
 */
SOBER_BOOTSTRAP_EXPORT int sober_bionic_bootstrap_verify_build(void *guest_base);

/*
 * Locate the mapped guest (libroblox.so) in the current process link map via
 * dl_iterate_phdr(); verifies its build ID.  Returns its load base (the value
 * to add RVAs to) or NULL.  Useful from a dependency constructor.
 */
SOBER_BOOTSTRAP_EXPORT void *sober_bionic_bootstrap_find_guest(void);

/*
 * Install bridges + seed data slots.  Does NOT run any constructors.
 * Returns 0 on success.  Idempotent.
 */
SOBER_BOOTSTRAP_EXPORT int sober_bionic_bootstrap_apply(void *guest_base);
SOBER_BOOTSTRAP_EXPORT int sober_bionic_bootstrap_seed_jni_singleton(void *guest_base);
SOBER_BOOTSTRAP_EXPORT int sober_bionic_bootstrap_acquire_valid_handle(void *guest_base);
SOBER_BOOTSTRAP_EXPORT int sober_bionic_bootstrap_trace_free_slot(void *guest_base);
/*
 * Manually invoke init_array entries in the validated ranges [2,3) and
 * [5,3618) at VMA 0x7220e58 (file offset 0x721ce58).  Only valid in MODE B
 * (custom loader) where glibc will not run init_array itself.  Idempotent.
 */
SOBER_BOOTSTRAP_EXPORT int sober_bionic_bootstrap_replay_constructors(void *guest_base);

/*
 * Convenience: apply() + replay_constructors().  Use in MODE B only.
 * In MODE A (dependency constructor + dlopen) call apply() only, because the
 * dynamic loader runs init_array itself; a separate helper
 * sober_bionic_bootstrap_apply_only() is provided for that mode.
 */
SOBER_BOOTSTRAP_EXPORT int sober_bionic_bootstrap_run(void *guest_base);
SOBER_BOOTSTRAP_EXPORT int sober_bionic_bootstrap_apply_only(void *guest_base);

/* Read DT_INIT_ARRAY / DT_INIT_ARRAYSZ from the guest dynamic section (build-
 * ID gated, no code executed).  Useful for a harness that drives ctors itself. */
SOBER_BOOTSTRAP_EXPORT int sober_bionic_bootstrap_init_array_info(
    void *guest_base, unsigned long long *out_rva, unsigned int *out_count);

/* Return 1 if the bootstrap has been successfully applied in this process. */
SOBER_BOOTSTRAP_EXPORT int sober_bionic_bootstrap_is_applied(void);

/*
 * MODE A helper: call this from a constructor in one of the guest's NDK stub
 * DSOs (liblog.so / libandroid.so / ...).  It discovers the mapped guest via
 * dl_iterate_phdr() and applies seeds+bridges before the guest's init_array.
 * Exported with default visibility so a stub DSO can dlsym/call it.
 */
SOBER_BOOTSTRAP_EXPORT void sober_bionic_bootstrap_from_stub_constructor(void);

/* Profile metadata (for diagnostics / tests / evidence). */
SOBER_BOOTSTRAP_EXPORT const char *sober_bionic_bootstrap_build_id_string(void);
SOBER_BOOTSTRAP_EXPORT const char *sober_bionic_bootstrap_header_path(void);

#ifdef __cplusplus
}
#endif
#endif /* SOBER_BIONIC_BOOTSTRAP_H */
