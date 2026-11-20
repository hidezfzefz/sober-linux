/*
 * global_string_seed.h - opt-in seed of the guest global "std::string" handle
 * at guest_base+0x72a0520 (Phase 8, task #461).
 *
 * Owner: objmodel. Scope: this header + global_string_seed.c only.
 *
 * WHY
 * ---
 * With SOBER_SEED_JNI_VM=1 + SOBER_FAITHFUL_JAVAVM=1 the genuine guest attach
 * path runs, but the worker thread still faults at guest RVA 0x2e5d42a:
 *
 *     2e5d420: movb  $0,(%rdi)
 *     2e5d423: mov   %rsi,0x8(%rdi)
 *     2e5d427: mov   (%rsi),%rax        <-- rsi = guest_base+0x72a0520
 *     2e5d42a: mov   -0x18(%rax),%rax   <-- rax == 0 -> si_addr = -0x18
 *
 * i.e. the backing word of the guest global string handle at +0 is NULL.  The
 * guest's OWN static initialiser for this global is at 0x2e613ed; it installs
 * a genuine type-descriptor pointer at +0 (descriptor 0x6db3ff0, whose word
 * [0] is the +8 field displacement, published as descriptor+0x18 = 0x6db4008)
 * and a genuine string header vtable 0x6ed8d00 on the data object at +0x30.
 *
 * This module replays that shape.  Every pointer published is a genuine guest
 * RVA (base + RVA) or a guest-malloc'd address.  NO host function pointer is
 * ever stored, no guest CODE byte is patched, and nothing emits a callback or
 * touches the decision-013 marker.  Default OFF (SOBER_SEED_GLOBAL_STRING=1).
 */
#ifndef SOBER_GLOBAL_STRING_SEED_H
#define SOBER_GLOBAL_STRING_SEED_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct SoberJni;   /* forward declaration (native_jni.h is the full type) */

/* Return codes for sober_seed_global_string(). */
#define SOBER_GSEED_OK          0    /* enabled and seeded (or already valid) */
#define SOBER_GSEED_DISABLED    1    /* env var unset: clean no-op */
#define SOBER_GSEED_ERR        (-1)  /* bad args or guest allocator failure */

/* Guest RVAs, frozen engine BuildID 204ad049599c96afb75b8a21d5051063e821d6c9.
 * All are genuine engine addresses; none is a harness/host symbol. */
#define SOBER_GSEED_RVA_GLOBAL      0x72a0520u  /* the global handle itself */
#define SOBER_GSEED_RVA_DESCRIPTOR  0x6db3ff0u  /* type descriptor: word[0] = 8 */
#define SOBER_GSEED_RVA_HANDLE_PTR  0x6db4008u  /* descriptor + 0x18 -> reader */
#define SOBER_GSEED_RVA_STRING_VT   0x6ed8d00u  /* genuine string header vtable */
#define SOBER_GSEED_RVA_MALLOC      0x1dbab02u  /* guest allocator (malloc) */
#define SOBER_GSEED_HANDLE_BYTES    0xa0u       /* handle window: +0 .. +0x98 */
#define SOBER_GSEED_STRING_BYTES    0x40u       /* data obj: hdr(0x20)+buffer(0x20) */
#define SOBER_GSEED_STRING_CAP      500u        /* 0x1f4, as in formatter 0x20ab4f5 */

/* Default OFF: true only when SOBER_SEED_GLOBAL_STRING=1 in the environment. */
int sober_global_string_seed_enabled(void);

/*
 * Seed the global string handle at guest_base+0x72a0520.
 *
 *   int rc = sober_seed_global_string(j, (void *)guest_base);
 *
 * Call EARLY — right after sober_jni_init(), BEFORE the JNI_OnLoad/JavaVM
 * seeds.  The fault is issued by a guest worker thread spawned by the faithful
 * JavaVM attach path; if this call sits after those seeds that thread can read
 * the still-NULL global before the seed lands (verified: seed_done=0 at the
 * fault).  Use sober_seed_global_string_early(guest_base) when the JNI bridge
 * is not yet available; it is equivalent to passing j=NULL.  Safe to call
 * unconditionally: it is a no-op returning SOBER_GSEED_DISABLED unless
 * SOBER_SEED_GLOBAL_STRING=1.
 * If the guest has already installed a non-NULL backing word at +0 the call is
 * a no-op returning SOBER_GSEED_OK, unless SOBER_GSEED_FORCE=1.  Returns
 * SOBER_GSEED_ERR on bad arguments or guest-malloc failure (the global is left
 * untouched in that case).  `j` is currently unused by the builder but kept in
 * the signature so the lead's wiring matches the other phase-8 modules.
 */
int sober_seed_global_string(struct SoberJni *j, void *guest_base);

/* Early-install form: identical seed, no JNI bridge needed (j=NULL).  Call
 * this right after sober_jni_init() / before the JNI_OnLoad/JavaVM seeds. */
int sober_seed_global_string_early(void *guest_base);

/* Diagnostics (opt-in-safe: no side effects). */
const char *sober_global_string_seed_describe(char *buf, size_t cap);
void   sober_global_string_seed_reset(void);
unsigned long sober_global_string_seed_writes(void);
int sober_global_string_seed_done(void);  /* diagnostic: seed completed? */

#ifdef __cplusplus
}
#endif
#endif /* SOBER_GLOBAL_STRING_SEED_H */
