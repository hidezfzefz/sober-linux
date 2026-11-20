/*
 * callback_provenance.h - opt-in, observation-only provenance recorder for the
 * guest's `gameLoadedCallback(J)V` dispatch.
 *
 * Worker B11 (logical Team B, wave 2).  NEW FILE -- no existing file is edited.
 *
 * WHAT THIS IS
 * ------------
 * A passive observer that the bridge owner may call from `sr_dispatch_call`
 * (src/jni/native_jni.c).  For the single method name `gameLoadedCallback` it
 * records, at dispatch time:
 *
 *   (a) the caller return addresses present on the stack -- in particular the
 *       thunk's `call *0x470(%rax)` return site (guest RVA 0x2584960) and the
 *       notifier's `call 0x25848e0` return site (guest RVA 0x27eafb3);
 *   (b) the class / method pointer identity passed to the dispatcher, compared
 *       with the guest's cached globals at base+0x72a2f48 (class) and
 *       base+0x72a2fc0 (method, `gameLoadedCallback(J)V`).
 *
 * It then classifies the dispatch:
 *   - GENUINE: notifier 0x27eaf6e -> thunk 0x25848e0 -> JNI slot 142, i.e. both
 *     return sites are on the stack (0x2584960 nearer the dispatch than
 *     0x27eafb3) and the class/method pointers are the cached guest ones.
 *   - HARNESS THUNK: the thunk return site 0x2584960 is present but the
 *     notifier return 0x27eafb3 is not -- i.e. `SOBER_ALT_CALLBACK_PATH` called
 *     base+0x25848e0 directly from host code (guest_app_shell.c).
 *
 * STRICT DECISION 013 / NO GATE EFFECT
 * ------------------------------------
 * This module NEVER calls into the guest, NEVER registers a callback handler,
 * NEVER emits a marker and NEVER reads or writes the gate.  It is diagnostic
 * only.  Its results are corroborating evidence; they are not a substitute for
 * the strict gate.  While the opt-in is active the marker MUST remain
 * force-withheld by the gate owner -- `sober_callback_provenance_is_active()`
 * is provided so the owner can add `SOBER_CALLBACK_PROVENANCE` to the withhold
 * chain.  No withhold list, gate or marker is changed by this module.
 *
 * OPT-IN
 * ------
 *   SOBER_CALLBACK_PROVENANCE=1        enables it (default OFF).
 *   SOBER_CALLBACK_PROVENANCE_GUEST_BASE=<hex>  optional explicit base;
 *                                      otherwise the largest r-x mapping in
 *                                      /proc/self/maps is used (same method as
 *                                      src/trace/native_trace.c).
 *
 * INTEGRATION (lead-owned edits, not made here)
 * ---------------------------------------------
 *   #include "callback_provenance.h"
 *   ... in sr_dispatch_call() after `name` is resolved:
 *       sober_callback_provenance_on_dispatch(name, obj, method);
 *   ... optionally once after seeding (to pass the exact base):
 *       sober_callback_provenance_install(guest_base);
 *   The per-dispatch hook is a no-op when the opt-in is off, and lazily
 *   auto-installs (auto-discovering the base) if install() was not called.
 */
#ifndef SOBER_CALLBACK_PROVENANCE_H
#define SOBER_CALLBACK_PROVENANCE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef SOBER_PROV_API
#define SOBER_PROV_API __attribute__((visibility("default")))
#endif

/* One-time install.  `guest_base` may be NULL to auto-discover.  Idempotent;
 * returns 0 when installed+enabled, -1 when the opt-in is off or the base
 * could not be determined.  Safe to call on every dispatch (cheap when off). */
SOBER_PROV_API int sober_callback_provenance_install(void *guest_base);

/* Per-dispatch observation hook.  `clazz` is the class object (the `c`/`obj`
 * argument of the static call), `method` is the jmethodID.  Never calls into
 * guest code and never changes bridge counters, gate or markers. */
SOBER_PROV_API void sober_callback_provenance_on_dispatch(const char *name,
                                                          void *clazz,
                                                          void *method);

/* True iff SOBER_CALLBACK_PROVENANCE=1 was seen (diagnostic force-withhold
 * signal for the gate owner). */
SOBER_PROV_API int sober_callback_provenance_is_active(void);

/* True only after a genuine notifier-driven gameLoadedCallback dispatch:
 * return sites 0x2584960 (thunk) and 0x27eafb3 (notifier) both observed in the
 * correct order, with the cached class/method pointer identity matching. */
SOBER_PROV_API int sober_callback_provenance_is_genuine(void);

/* True after a gameLoadedCallback dispatch that carried the thunk return site
 * but not the notifier return site (harness-triggered direct thunk call). */
SOBER_PROV_API int sober_callback_provenance_is_harness_thunk(void);

/* Diagnostic accessors (all zero until an observation is made). */
SOBER_PROV_API uint64_t   sober_callback_provenance_dispatch_count(void);
SOBER_PROV_API uintptr_t  sober_callback_provenance_guest_base(void);
SOBER_PROV_API uintptr_t  sober_callback_provenance_thunk_ret(void);
SOBER_PROV_API uintptr_t  sober_callback_provenance_notifier_ret(void);
SOBER_PROV_API uintptr_t  sober_callback_provenance_observed_class(void);
SOBER_PROV_API uintptr_t  sober_callback_provenance_observed_method(void);

/* One-line diagnostic description; returns `buf`. */
SOBER_PROV_API const char *sober_callback_provenance_describe(char *buf,
                                                             size_t cap);

#ifdef __cplusplus
}
#endif
#endif /* SOBER_CALLBACK_PROVENANCE_H */
