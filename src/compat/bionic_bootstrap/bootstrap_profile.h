/*
 * bootstrap_profile.h - validated AppImage Bionic bootstrap profile.
 *
 * OWNER: worker-1 (task #375).  Do not edit outside src/compat/bionic_bootstrap/.
 *
 * EVERY value below is from the AppImage engine build ONLY:
 *   libroblox.so  BuildID 204ad049599c96afb75b8a21d5051063e821d6c9
 *   SHA256        3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335
 *   version       2.740.931 (version_code 3170)
 *
 * The Flatpak engine (BuildID 791c3f9e..., version 2.745.1142, SHA
 * 8afce3e6...) is a DIFFERENT binary.  Its RVAs MUST NOT be used here.
 * The runtime guard in bionic_bootstrap.c refuses to patch anything unless
 * the mapped object's NT_GNU_BUILD_ID matches SOBER_BOOTSTRAP_BUILD_ID_BYTES.
 *
 * Evidence:
 *   - docs/interfaces/abi-map.md (rows: six mimalloc bridges, four data seeds,
 *     Bionic runtime bootstrap; section "REA evidence for the bridge rows").
 *   - analysis/rea/findings.json  decision_003_004_extension.bridge_profile
 *     (source: native/mocktail/roblox_host_abi.json, BUILD-ID gated).
 *   - docs/decisions/006-init-milestone.md (blocker list).
 *   - Direct local-shell cross-check against the mapped engine on 2026-11-18:
 *       readelf -nW  -> GNU NT_GNU_BUILD_ID = 204ad049...
 *       readelf -dW  -> DT_INIT_ARRAY VMA 0x7220e58, INIT_ARRAYSZ 28944 (0x7110),
 *                       i.e. 3618 x 8-byte entries; .init_array section addr
 *                       0x7220e58 / file offset 0x721ce58.  The profile's
 *                       "init_array_offset" is a VMA, NOT a file offset.
 *       readelf -lW  -> LOAD#0 off==vaddr (so bridge RVAs are also file
 *                       offsets), LOAD#2 (RW/RELRO) holds .init_array,
 *                       LOAD#3 (RW/BSS-like) holds all four data slots.
 *       dd+od       -> the six bridge prologues below (see BRIDGE_FINGERPRINT).
 */
#ifndef SOBER_BOOTSTRAP_PROFILE_H
#define SOBER_BOOTSTRAP_PROFILE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 20 raw bytes of 204ad049599c96afb75b8a21d5051063e821d6c9. */
#define SOBER_BOOTSTRAP_BUILD_ID_BYTES                                        \
    { 0x20, 0x4a, 0xd0, 0x49, 0x59, 0x9c, 0x96, 0xaf, 0xb7, 0x5b,           \
      0x8a, 0x21, 0xd5, 0x05, 0x10, 0x63, 0xe8, 0x21, 0xd6, 0xc9 }
#define SOBER_BOOTSTRAP_BUILD_ID_LEN 20u
#define SOBER_BOOTSTRAP_BUILD_ID_STR "204ad049599c96afb75b8a21d5051063e821d6c9"
#define SOBER_BOOTSTRAP_PAYLOAD_SHA256 \
    "3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335"

/* ELF identity expected of the guest (x86-64 Android 26, ET_DYN). */
#define SOBER_BOOTSTRAP_ELF_CLASS  2u   /* ELFCLASS64 */
#define SOBER_BOOTSTRAP_ELF_DATA   1u   /* ELFDATA2LSB */
#define SOBER_BOOTSTRAP_ELF_MACHINE 62u /* EM_X86_64 */
#define SOBER_BOOTSTRAP_ELF_TYPE   3u   /* ET_DYN */

/* --- six mimalloc host bridges (RVA of the guest thunk to redirect) ----- */
/* Evidence: abi-map.md "host allocate/usable_size/reallocate/aligned/free";
 * findings.json bridge_profile.bridge_entries.  Each RVA lands in LOAD#0
 * (offset == RVA); each begins with a plausible x86-64 prologue (verified). */
#define SOBER_BRIDGE_ALLOC_SMALL_RVA   0x1dbad62u
#define SOBER_BRIDGE_USABLE_SIZE_RVA   0x1dbdfbau
#define SOBER_BRIDGE_REALLOCATE_RVA    0x227aafau
#define SOBER_BRIDGE_ALLOCATE_RVA      0x1dbab02u
#define SOBER_BRIDGE_ALIGNED_ALLOC_RVA 0x1dd60e5u
#define SOBER_BRIDGE_FREE_RVA          0x1dbe559u
/* Seventh bridge (Phase 4b decision 028): unpatched guest calloc at
 * 0x253225e, reached through allocator slot 0x726c0c0.  It creates the
 * 0xc0dedbad app-shell handle with stale, non-zeroed memory. */
#define SOBER_BRIDGE_CALLOC_RVA        0x253225eu
/* Runtime-rebound calloc used by the app-shell path (worker-3's slot dump):
 * the slot 0x726c0c0 holds guest+0x231f0e2 at fault time, not 0x253225e. */
#define SOBER_BRIDGE_CALLOC_RUNTIME_RVA 0x231f0e2u

/* Length in bytes of the absolute-jump trampoline we write at each bridge:
 *   48 b8 imm64   movabs rax, <host function>
 *   ff e0         jmp rax
 * 12 bytes, well clear of every prologue fingerprint (8 bytes). */
#define SOBER_BRIDGE_TRAMPOLINE_LEN 12u

/* First 8 bytes at each bridge RVA, read directly from the AppImage engine
 * (dd if=libroblox.so bs=1 skip=<rva> count=8 | od -An -tx1) on 2026-11-18.
 * NOTE: findings.json bridge_entries gives small-allocate's fingerprint as
 * 554889e5890d202a, but that is actually the bytes at the *allocator
 * initializer* RVA 0x2251b2a.  The real bytes at 0x1dbad62 are 554889e55350
 * 4889.  We verify against the bytes we measured from the binary, not the
 * transcribed profile string; the RVA itself is unchanged and correct. */
#define SOBER_BRIDGE_FP_ALLOC_SMALL   { 0x55,0x48,0x89,0xe5,0x53,0x50,0x48,0x89 }
#define SOBER_BRIDGE_FP_USABLE_SIZE   { 0x48,0x89,0xfe,0x48,0x8b,0x05,0x7c,0xb0 }
#define SOBER_BRIDGE_FP_REALLOCATE    { 0x55,0x48,0x89,0xe5,0x41,0x56,0x53,0x48 }
#define SOBER_BRIDGE_FP_ALLOCATE      { 0x55,0x48,0x89,0xe5,0x53,0x50,0xe8,0x37 }
#define SOBER_BRIDGE_FP_ALIGNED_ALLOC { 0x55,0x48,0x89,0xe5,0x41,0x56,0x53,0x48 }
#define SOBER_BRIDGE_FP_FREE          { 0x48,0x89,0xfe,0x48,0x8b,0x05,0xdd,0xaa }
#define SOBER_BRIDGE_FP_CALLOC        { 0x55,0x48,0x89,0xe5,0x41,0x56,0x53,0x48 }
#define SOBER_BRIDGE_FP_CALLOC_RUNTIME { 0x55,0x48,0x89,0xe5,0x53,0x50,0x48,0x85 }
#define SOBER_BRIDGE_FP_LEN 8u

/* --- native data slots + their guest initializers ----------------------- */
/* The initializer at init_rva is a guest function that populates slot_rva.
 * "Seeding" = (1) make sure the slot region is writable and zeroed (it is a
 * BSS-like tail of LOAD#3 in the profile, already zero at load), then
 * (2) call the initializer with SysV AMD64 convention after the bridges are
 * installed (the initializers may allocate).  Sizes come from worker-2's
 * bootstrap map when available; the defaults below are conservative bounds
 * for the small header objects and are overridable via the apply-ex routine. */
/* Sizes confirmed by worker-2's analysis/bootstrap/bootstrap-map.md (guest
 * disassembly).  `slot_size` is the span we make writable; `zero_size` is how
 * much we zero before calling the initializer (0 = leave untouched).  All
 * four slots are BSS-like and worker-2 verified ZERO packed relocations target
 * them (nor +/- 0x20), so the zeroing below cannot clobber relocated data. */
#define SOBER_SEED_ALLOCATOR_SLOT_RVA      0x7834570u
#define SOBER_SEED_ALLOCATOR_INIT_RVA      0x2251b2au
/* Named slot is the allocator pointer inside a ~0x9cB state block starting at
 * 0x7834554.  Zero only from the named slot to the end of that block. */
#define SOBER_SEED_ALLOCATOR_SLOT_SIZE     0x80u   /* 0x7834570..0x78345ef */
#define SOBER_SEED_ALLOCATOR_ZERO_SIZE     0x80u

#define SOBER_SEED_JNI_SINGLETON_SLOT_RVA  0x7d9cf40u
#define SOBER_SEED_JNI_SINGLETON_INIT_RVA  0x1dba834u
#define SOBER_SEED_JNI_SINGLETON_SLOT_SIZE 1024u /* jni_singleton_bytes */
#define SOBER_SEED_JNI_SINGLETON_ZERO_SIZE 1024u /* init does atomic xchg */

#define SOBER_SEED_EMPTY_STRING_SLOT_RVA   0x79413b8u
#define SOBER_SEED_EMPTY_STRING_INIT_RVA   0x225dd2au
#define SOBER_SEED_EMPTY_STRING_SLOT_SIZE  0x40u
#define SOBER_SEED_EMPTY_STRING_ZERO_SIZE  0x40u

/* 0x7cdd118 is an 8-BYTE POINTER only.  The 0x338-byte registry object is a
 * separate guest allocation made by the initializer via bridge 0x1dbab02;
 * writing/zeroing 0x100 here would clobber adjacent BSS globals. */
#define SOBER_SEED_REGISTRY_SLOT_RVA       0x7cdd118u
#define SOBER_SEED_REGISTRY_INIT_RVA       0x227be14u
#define SOBER_SEED_REGISTRY_SLOT_SIZE      8u
#define SOBER_SEED_REGISTRY_ZERO_SIZE      8u

/* Related allocator slots also present in data_seeds (kept for completeness;
 * they are inside the same BSS-like LOAD#3 and are zero at load). */
#define SOBER_ARENA_GUARD_SLOT_RVA   0x7cd8fb0u
#define SOBER_ARENA_TABLE_SLOT_RVA   0x7cd9040u
#define SOBER_ARENA_TABLE_SLOT_COUNT 4194304u

/* --- constructors / init_array ------------------------------------------- */
/* DT_INIT_ARRAY VMA = 0x7220e58 (confirmed by readelf -dW: tag 0x19 =
 * 0x7220e58, INIT_ARRAYSZ = 28944 = 3618 x 8).  The profile calls this the
 * "offset" but it is a VIRTUAL ADDRESS, so it is added directly to the
 * mapped load base.  Its actual FILE offset is 0x721ce58 (section .init_array
 * addr 0x7220e58 / offset 0x721ce58), which we do not use for dispatch. */
#define SOBER_INIT_ARRAY_RVA         0x7220e58u
#define SOBER_INIT_ARRAY_FILE_OFFSET 0x721ce58u /* informational only */
#define SOBER_INIT_ARRAY_COUNT       3618u
#define SOBER_INIT_ARRAY_ENTRY_SIZE  8u

/* Validated replay ranges (half-open, over init_array indices):
 *   [2,3)     -> index 2 only (constructor RVA 0x1dbe2b1)
 *   [5,3618)  -> indices 5..3617 (index 5 == JNI singleton init 0x1dba834)
 * Indices 0,1,3,4 are deliberately NOT replayed by the mocktail profile. */
#define SOBER_CTOR_RANGE0_BEGIN 2u
#define SOBER_CTOR_RANGE0_END   3u
#define SOBER_CTOR_RANGE1_BEGIN 5u
#define SOBER_CTOR_RANGE1_END   3618u

/* Extra seed entry point from data_seeds: the profile and worker-2's map say
 * this allocator-thread initializer must be replayed AFTER ctor[2].  It is
 * not itself an init_array entry.  It reads TLS key 0x7282000 and calls
 * pthread_getspecific, so the guest's libc shims must be loaded. */
#define SOBER_ALLOCATOR_THREAD_INIT_RVA 0x1e47927u
#define SOBER_ARENA_INIT_RVA            0x1dbb178u /* self-guarded, via ctor[2] */

/* Cross-check anchors: constructor RVA at selected init_array indices. */
#define SOBER_CTOR_RVA_2 0x1dbe2b1u
#define SOBER_CTOR_RVA_3 0x1db9a40u
#define SOBER_CTOR_RVA_4 0x1dba6c4u
#define SOBER_CTOR_RVA_5 0x1dba834u

#ifdef __cplusplus
}
#endif
#endif /* SOBER_BOOTSTRAP_PROFILE_H */
