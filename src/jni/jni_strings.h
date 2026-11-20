/*
 * jni_strings.h - canonical jstring / java.lang.String payload builder
 * (Team B, worker B4).  NEW FILE - no existing src/ file is modified.
 *
 * ---------------------------------------------------------------------------
 * WHY THIS MODULE EXISTS
 * ---------------------------------------------------------------------------
 * The guest reads every JNI string through JNIEnv slot 165
 * (`GetStringUTFChars`, vtable byte offset 0x548 = (4 reserved + 165) * 8).
 * The canonical conversion helper at frozen-engine RVA 0x2315532 is:
 *
 *     231554b: mov  (%rsi),%rax        ; rax = env->functions
 *     231554e: mov  %rsi,%rdi          ; rdi = env
 *     2315551: mov  %rdx,%rsi          ; rsi = jstring
 *     2315554: xor  %edx,%edx          ; isCopy = NULL
 *     2315556: call *0x548(%rax)       ; GetStringUTFChars
 *     2315561: mov  %rax,%r12
 *     231556a: call 0x1dc1208          ; copy bytes -> native std::string
 *     231557b: call *0x550(%rax)       ; ReleaseStringUTFChars (+0x550)
 *
 * The existing bridge (src/jni/native_jni.c) stores string bytes in
 * `SrObject.payload` (a host heap pointer) and its A-backing at the base of
 * a bare calloc buffer, so `*(backing-0x18)` reads malloc metadata and the
 * first 0x90 bytes of the handle expose host pointers (name/sig/payload).
 * A guest C++ deref of such a handle (the 0x2e5d420 weak-ref helper reads
 * `rax = *(jobj); disp = *(rax-0x18)`) can consume that host metadata - and
 * historically the stack-guard sentinel 0xABCDEF0123456789 via the GOT slot
 * at guest RVA 0x72293f8 (see analysis/sentinel-abcdef-trace.md).
 *
 * This module builds a *canonical* string object:
 *   - the payload is exposed identically through GetStringUTFChars (+0x548),
 *     GetStringChars (+0x528 -> fn[161]) and String.toString() (same handle);
 *   - a guest-shaped std::string data object carries a NEGATIVE-OFFSET
 *     header: genuine guest vtable 0x6ed8d00 at `data-0x20`, displacement 0
 *     at `data-0x18`, self pointer, size and capacity 0x1f4 - all genuine
 *     guest RVAs or integers, never host function pointers;
 *   - no word of the guest-visible object equals 0xABCDEF0123456789 and no
 *     host code pointer is published (auditable with sober_jstr_audit()).
 *
 * Default OFF.  Enable with SOBER_CANONICAL_JSTRINGS=1.  Nothing here emits a
 * guest->Java callback, patches a guest byte, or touches the decision-013
 * `app shell rendered` marker.  The engine .so is never modified.
 */
#ifndef SOBER_JNI_STRINGS_H
#define SOBER_JNI_STRINGS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct SoberJni;   /* opaque; native_jni.h is the full definition */

#define SOBER_JSTR_OK          0
#define SOBER_JSTR_DISABLED    1
#define SOBER_JSTR_ERR        (-1)

/* Genuine engine RVAs (BuildID 204ad049599c96a..., SHA 3e498d6494...). */
#define SOBER_JSTR_RVA_STRING_VTABLE  0x6ed8d00u  /* std::string header vtable */
#define SOBER_JSTR_STRING_CAPACITY    0x1f4u      /* 500 (formatter 0x20ab4f5) */
#define SOBER_JSTR_SENTINEL           0xabcdef0123456789ULL

/* ---- gate -------------------------------------------------------------- */
/* True only when SOBER_CANONICAL_JSTRINGS=1 in the environment. */
int sober_jstr_enabled(void);

/* ---- builders ---------------------------------------------------------- */
/*
 * Build a standalone canonical jstring (NOT registered with the bridge).
 * `guest_base` is the mapped engine base; its RVAs are used for the
 * negative-offset vtable.  Passing NULL publishes a zero vtable (still
 * canonical: no host pointer, no sentinel).  Returns NULL when disabled or
 * on allocation failure.  The caller owns the handle; release it with
 * sober_jstr_destroy() (or leak it - the harness is short-lived).
 */
void *sober_jstr_create(void *guest_base, const char *text);

/*
 * Bridge-compatible form: obtain a registered SrObject handle via
 * sober_jni_new_string_utf8(), then shape its payload fields so the
 * bridge's own GetStringUTFChars/GetStringChars return the canonical bytes.
 * Use this when the lead wants the existing JNIEnv table to work unchanged.
 * Returns the same pointer as sober_jni_new_string_utf8() on success.
 */
void *sober_jstr_create_bridge(struct SoberJni *j, void *guest_base,
                               const char *text);

void sober_jstr_destroy(void *h);   /* free a standalone handle */

/* ---- payload accessors (JNI-shaped, usable as vtable entries) ----------- */
/* These ignore `env`.  They accept both standalone and bridge handles because
 * the two share the SrObject-compatible field offsets. */
const char     *sober_jstr_get_utf8(void *env, void *str, uint8_t *is_copy);
const uint16_t *sober_jstr_get_chars(void *env, void *str, uint8_t *is_copy);
void            sober_jstr_release_utf8(void *env, void *str, const char *chars);
void            sober_jstr_release_chars(void *env, void *str,
                                         const uint16_t *chars);

/* Plain helpers (no env argument) for the lead's C code. */
const char     *sober_jstr_bytes(void *str);      /* "" for an invalid handle */
size_t          sober_jstr_length(void *str);    /* byte length, no NUL */
/* java.lang.String.toString() returns `this`; refs++ and return the handle. */
void           *sober_jstr_to_string(void *str);

/* ---- opt-in JNIEnv installation ---------------------------------------- */
/*
 * Install the four string accessors into a JNINativeInterface_ `fn[]` array
 * (the 231-slot array that follows the four reserved words of JNIEnv):
 *   fn[161] GetStringChars      (JNIEnv +0x528)
 *   fn[162] ReleaseStringChars  (JNIEnv +0x530)
 *   fn[165] GetStringUTFChars   (JNIEnv +0x548)
 *   fn[166] ReleaseStringUTFChars (JNIEnv +0x550)
 * The lead owns native_jni.c and can call
 *   sober_jstr_install(j->env_table.fn, guest_base);
 * Returns SOBER_JSTR_DISABLED without touching the table when the env var is
 * unset.  No guest byte is patched.
 */
int sober_jstr_install(void **env_fn_array, void *guest_base);

/* ---- audit / diagnostics ----------------------------------------------- */
/*
 * Walk every guest-visible word of the handle and its backing/data objects.
 * Returns 0 when clean, otherwise the number of offending words (sentinel or
 * a host text/code pointer in a guest-visible slot).  A human-readable report
 * is written to buf (never NULL-guarded away by the caller's cap).
 */
int sober_jstr_audit(void *str, char *buf, size_t cap);

const char *sober_jstr_describe(char *buf, size_t cap);
void        sober_jstr_reset(void);       /* reset diagnostic counters */
unsigned long sober_jstr_created(void);   /* handles built this run */
void       *sober_jstr_guest_base(void);   /* last installed base, or NULL */

#ifdef __cplusplus
}
#endif
#endif /* SOBER_JNI_STRINGS_H */
