/*
 * tsd_default.h - DEFAULT-ON host binding of the guest's Bionic TSD namespace.
 *
 * Owner: Team 1 (substrate) / agent T1-1.  NEW FILE — no existing file edited.
 *
 * WHY THIS EXISTS (decisions 013 / 038, root-cause analysis A04/A10/B10):
 *   The frozen engine libroblox.so (BuildID 204ad049..., SHA 3e498d6494...)
 *   is an x86-64 Android ELF.  Its pthread_getspecific@plt / setspecific@plt
 *   resolve, on glibc, to glibc's pthread TSD key space.  Bionic keys are
 *   small integer indices (key 2 @ 0x7282000, key 3 @ 0x72a1b08).  glibc's
 *   key 2 returned the phase-1 __stack_chk_guard sentinel 0xabcdef0123456789,
 *   which the guest dereferenced as a thread-state pointer, causing the
 *   scheduler-gated ExceptionCheck/FindClass recursion and JNI_OnLoad failure.
 *   The correct, fully-native fix is to point the two guest GOT slots at our
 *   OWN versioned libc stub (build/ndk-stubs/libc.so), whose
 *   pthread_getspecific@@LIBC / pthread_setspecific@@LIBC operate on a private
 *   512-slot _Thread_local array indexed directly by the Bionic key.
 *
 * SCOPE — what this module does and does not do:
 *   * It performs ONLY the already-approved GOT rebind: two 8-byte writes to
 *     guest GOT 0x72298b0 (getspecific) and 0x72295c8 (setspecific).  No guest
 *     code bytes are patched; no Android runtime / translation layer.
 *   * It verifies (read-only) the two key globals and snapshots the canary
 *     global 0x72293f8 before/after, proving the canary was left untouched.
 *   * key_create/delete GOT slots 0x72297a0 / 0x722a2c8 are NOT rebound: the
 *     versioned stub forwards those two symbols to glibc (different key
 *     space), so it cannot provide them natively.  See
 *     sober_tsd_default_key_management_status().
 *   * It NEVER emits or contributes to the strict `app shell rendered` marker
 *     (decisions 013/038).  This is host substrate only.
 *
 * WIRING (lead-owned guest_app_shell.c / native_jni.c / CMakeLists.txt):
 *   Call sober_tsd_default_install() once, after the guest is mapped and
 *   relocated and its libc.so stub is loaded, before any guest JNI/startup
 *   RVA.  That single call is safe and idempotent and auto-discovers the guest
 *   via dl_iterate_phdr + build-ID check.  Alternatively pass an explicit load
 *   bias to sober_tsd_default_apply(guest_base).
 *
 *   Default is ON when wired.  Kill switch: environment variable
 *   SOBER_TSD_DEFAULT=0 makes every apply/install a no-op (returns 0).
 */
#ifndef SOBER_TSD_DEFAULT_H
#define SOBER_TSD_DEFAULT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Root build uses -fvisibility=hidden; publish every entry point. */
#if defined(__GNUC__) || defined(__clang__)
#  define SOBER_TSD_DEFAULT_API __attribute__((visibility("default")))
#else
#  define SOBER_TSD_DEFAULT_API
#endif

/* ---- frozen-build constants (RVAs are relative to the guest load bias) -- */
#define SOBER_TSD_DEFAULT_ENV              "SOBER_TSD_DEFAULT"
#define SOBER_TSD_EXPECTED_BUILD_ID        "204ad049599c96afb75b8a21d5051063e821d6c9"
#define SOBER_TSD_EXPECTED_SHA256          "3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335"
#define SOBER_TSD_BIONIC_SLOTS             512u   /* stub's private TSD array  */

#define SOBER_TSD_GOT_GETSPECIFIC_RVA      0x72298b0u  /* pthread_getspecific */
#define SOBER_TSD_GOT_SETSPECIFIC_RVA      0x72295c8u  /* pthread_setspecific */
#define SOBER_TSD_GOT_KEYCREATE_RVA        0x72297a0u  /* NOT rebound (glibc) */
#define SOBER_TSD_GOT_KEYDELETE_RVA        0x722a2c8u  /* NOT rebound (glibc) */
#define SOBER_TSD_KEY2_GLOBAL_RVA          0x7282000u  /* expected value 2    */
#define SOBER_TSD_KEY3_GLOBAL_RVA          0x72a1b08u  /* expected value 3    */
#define SOBER_TSD_CANARY_GLOBAL_RVA        0x72293f8u  /* MUST stay untouched  */

#define SOBER_TSD_DEFAULT_OK                0
#define SOBER_TSD_DEFAULT_ERR_DISABLED     (-1) /* kill switch: no-op, not a fault */
#define SOBER_TSD_DEFAULT_ERR_NO_GUEST     (-2)
#define SOBER_TSD_DEFAULT_ERR_BUILD        (-3) /* build-ID mismatch           */
#define SOBER_TSD_DEFAULT_ERR_STUB         (-4) /* libc.so stub / symbol absent */
#define SOBER_TSD_DEFAULT_ERR_PROTECT      (-5) /* mprotect failed             */
#define SOBER_TSD_DEFAULT_ERR_CANARY       (-6) /* canary changed (abort path) */

/* Return codes shared by diagnose()/selftest(). */
#define SOBER_TSD_DEFAULT_HEALTHY          0
#define SOBER_TSD_DEFAULT_UNHEALTHY        (-1)

/* ---- enable / state ---------------------------------------------------- */

/* 1 when the mechanism is enabled (default), 0 only when the environment
 * variable SOBER_TSD_DEFAULT is set to exactly "0". */
SOBER_TSD_DEFAULT_API int sober_tsd_default_enabled(void);

/* 1 once the rebind has been successfully applied in this process. */
SOBER_TSD_DEFAULT_API int sober_tsd_default_is_applied(void);

/* ---- guest discovery / verification (read-only) ------------------------ */

/* Verify a mapped guest load bias carries the expected NT_GNU_BUILD_ID.
 * Reads the ELF headers from `guest_base` directly; no writes, no link-map
 * dependency.  Returns 1 on match, 0 otherwise (NULL included). */
SOBER_TSD_DEFAULT_API int sober_tsd_default_verify_build(void *guest_base);

/* Locate libroblox.so in the current link map via dl_iterate_phdr() and
 * verify its build ID.  Returns the load bias, or NULL if not found. */
SOBER_TSD_DEFAULT_API void *sober_tsd_default_find_guest(void);

/* ---- apply ------------------------------------------------------------- */

/* Rebind the two approved GOT slots to the versioned LIBC stubs.  Honours the
 * Enforces the build-ID gate, is idempotent, and verifies the canary is
 * unchanged before returning.  Returns SOBER_TSD_DEFAULT_OK (0) on success and
 * on a repeat call, SOBER_TSD_DEFAULT_ERR_DISABLED when the SOBER_TSD_DEFAULT=0
 * kill switch is set (a clean no-op, not a fault), other negatives on failure. */
SOBER_TSD_DEFAULT_API int sober_tsd_default_apply(void *guest_base);

/* Convenience entry point for the lead: auto-discover the guest and apply.
 * Safe to call unconditionally; never touches a non-matching object. */
SOBER_TSD_DEFAULT_API int sober_tsd_default_install(void);

/* ---- self-test / diagnostics (read-only: never write guest memory) ------ */

/* Full read-only report for a guest (auto-discovered when guest_base is NULL):
 * build-ID status, stub resolution, GOT words (bound or not), key2/key3 values
 * vs expected 2/3, and the canary value.  Returns HEALTHY (0) when the guest,
 * stub and both keys are valid.  Always fills buf when buf_len > 0. */
SOBER_TSD_DEFAULT_API int sober_tsd_default_diagnose(void *guest_base,
                                                      char *buf, size_t buf_len);

/* Read-only coherence check for the guest's Bionic TSD key 3 on the CALLING
 * thread.  The guest's only key3 accessors are pthread_getspecific@plt
 * (0x6d322e0 -> GOT 0x72298b0) and pthread_setspecific@plt (0x6d31d10 ->
 * GOT 0x72295c8); both must resolve to the SAME versioned stub, and the guest
 * key global 0x72a1b08 must still read 3.  A split store would let the guest
 * build TLS state through one array and read it back through the other.
 *
 * Returns 1 when coherent, 0 on any mismatch (including the SOBER_TSD_DEFAULT=0
 * kill switch).  *out_state, when non-NULL, receives THIS thread's current key3
 * slot read through the stub; NULL means the guest has not built it yet (the
 * guest builds it lazily at 0x2ea6520 via its own allocator + setspecific).
 * This function performs NO guest writes, NO host TSD seed and NO patching. */
SOBER_TSD_DEFAULT_API int sober_tsd_default_key3_coherent(void *guest_base,
                                                          void **out_state);

/* Resolve the versioned LIBC TSD symbols with no guest access.  Useful as a
 * constructor-time / test-time self-test of the stub itself. */
SOBER_TSD_DEFAULT_API int sober_tsd_default_selftest(char *buf, size_t buf_len);

/* Explain why key_create/key_delete GOT slots are deliberately not rebound:
 * the versioned stub forwards them to glibc, so it cannot provide native
 * Bionic key semantics.  No writes.  Always returns SOBER_TSD_DEFAULT_OK. */
SOBER_TSD_DEFAULT_API int sober_tsd_default_key_management_status(char *buf,
                                                                  size_t buf_len);

#ifdef __cplusplus
}
#endif
#endif /* SOBER_TSD_DEFAULT_H */
