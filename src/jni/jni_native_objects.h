/*
 * jni_native_objects.h - faithful guest-native C++ layouts for autovalue
 * jobjects (Phase 8, tasks #455/#459).
 *
 * Owner: objmodel. Scope: this file + jni_native_objects.c only.
 *
 * The Roblox guest treats jobjects as pointers to native C++ objects.  Our
 * generic synthetic jobjects carry valid JNI magics but NOT the C++ vtable /
 * field layout the guest's direct field reads expect (fault RVA 0x2e5d42a
 * reads `*(obj)-0x18`; 0x320581b needs a delegate vtable with slot +0x90).
 *
 * This module builds SrObject-compatible objects whose backing object has a
 * valid negative-offset header and whose field window covers every offset the
 * guest reads.  All side-effecting entry points are opt-in
 * (SOBER_NATIVE_OBJECTS=1); no guest bytes are patched, no callbacks emitted,
 * no decision-013 marker touched.  No vtable points at a host function: every
 * vptr we publish is either a genuine guest vtable address (base + RVA) or a
 * zeroed displacement table.
 */
#ifndef SOBER_JNI_NATIVE_OBJECTS_H
#define SOBER_JNI_NATIVE_OBJECTS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct SoberJni;   /* forward declaration (native_jni.h is the full type) */

/* Default OFF.  Enabled only with SOBER_NATIVE_OBJECTS=1 in the environment. */
int  sober_native_objects_enabled(void);

/* Opt-in install: stores guest_base and publishes the module description.
 * Returns SOBER_JNI_OK (0) when enabled, -1 when disabled or on bad args.
 * Must be called (with SOBER_NATIVE_OBJECTS set) before any builder if the
 * caller wants genuine guest vtable addresses in the shapes. */
int  sober_native_objects_install(struct SoberJni *j, void *guest_base);

/* Native C++ layout builders.  All return an SrObject-compatible jobject or
 * NULL.  Each carries a guest-shaped negative-offset header on its backing
 * object so direct C++ reads (helper 0x2e5d420) cannot fault. */
void *sober_native_string(struct SoberJni *j, const char *text);
void *sober_native_platform_params(struct SoberJni *j,
                                   float dpi_scale, int32_t width_mm,
                                   int32_t height_mm);
void *sober_native_init_params(struct SoberJni *j);
void *sober_native_start_app_params(struct SoberJni *j);
void *sober_native_java_list(struct SoberJni *j, void *const *items,
                             size_t count);

/* A shaped delegate for the app+0x408 slot: its vptr is the GENUINE guest
 * delegate vtable at guest_base + 0x6e047b0 (never a host stub).  Building it
 * does NOT install it -- app+0x408 is written by the guest itself at
 * 0x25d7e75 / 0x25f7e7e, and installing remains the harness's decision. */
void *sober_native_delegate(struct SoberJni *j);

/* Guest-visible field offsets / RVAs (see analysis/native-object-layouts.md). */
#define SOBER_NATIVE_PLATFORM_VTABLE     0x00u
#define SOBER_NATIVE_PLATFORM_DPI        0x08u   /* float */
#define SOBER_NATIVE_PLATFORM_WIDTH_MM   0x0cu   /* int32 */
#define SOBER_NATIVE_PLATFORM_HEIGHT_MM  0x10u   /* int32 */
#define SOBER_NATIVE_PLATFORM_SIZE       0x40u
#define SOBER_NATIVE_DELEGATE_DTOR_SLOT  0x90u   /* vtable +0x90 */
#define SOBER_NATIVE_VTABLE_RVA_STRING   0x6ed8d00u
#define SOBER_NATIVE_VTABLE_RVA_LIST     0x6db3ec0u
#define SOBER_NATIVE_VTABLE_RVA_PLATFORM 0x6dbcb20u
#define SOBER_NATIVE_VTABLE_RVA_DELEGATE 0x6e047b0u

/* Diagnostics. */
size_t sober_native_objects_created(void);
void   sober_native_objects_reset(void);
const char *sober_native_objects_describe(char *buf, size_t cap);

#ifdef __cplusplus
}
#endif
#endif /* SOBER_JNI_NATIVE_OBJECTS_H */
