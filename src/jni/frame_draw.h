/*
 * frame_draw.h - DEFAULT-ON guest GLES draw driver + plain-draw GOT counter
 *                (logical group C, agent F4).  NEW FILE: this header and
 *                frame_draw.c are the only files this agent touches.
 *
 * SCOPE / HARD RULES
 * ------------------
 *  - The lead owns shared src/, every CMakeLists.txt, docs/, tests/ and git.
 *    This module edits NONE of them; the lead wires the API below.
 *  - Engine FROZEN.  No guest code byte is patched.  The only guest-memory
 *    writes are eight-byte stores into three imported GLES JUMP_SLOTs
 *    (mprotect page -> store -> restore protection), exactly the interposition
 *    mechanism already used by render_guest.c on 0x72294a8/0x722a1a8/0x722a180.
 *  - No decision-013 marker, no JNI callback, no host GLES call is ever
 *    counted as a guest draw.  A draw is counted ONLY when the return address
 *    captured at one of the three GOT wrappers lies inside guest .text
 *    [base, base+0x6d33b90).  Host-RA calls are recorded separately as
 *    `synthetic` and never added to the decisive guest draw count.
 *  - No double-count with render_guest's extension-table trampolines: this
 *    module wraps ONLY the three imported (PLT) GOTs.  The five
 *    eglGetProcAddress-loaded extension pointers (0x77a4fa0/8/b0, 0x77a4fe0/8)
 *    remain owned by render_guest.  A single invocation carries a unique
 *    dedup key (RA, slot, tid, per-thread sequence); an invocation already
 *    accepted is never counted again.
 *  - DEFAULT ON.  Kill switch: SOBER_FRAME_DRAW=0 (exact string "0") makes
 *    every entry point inert: no GOT write, no guest call, no counted draw.
 *    Killing this module therefore can only turn decisive counts to zero
 *    (fail-closed); it can never manufacture a green.
 *  - Diagnostic, NOT proof.  Everything here is harness-supplied guest state
 *    (synthetic draw descriptor, host-origin dispatcher call).  Under
 *    decisions 013/037/038 this must stay in `seeded_state` and the marker
 *    must remain WITHHELD.  The counters are observability only.
 *
 * ---------------------------------------------------------------------------
 * FROZEN TARGET / PROVENANCE (all numbers OBSERVED from the frozen object)
 * ---------------------------------------------------------------------------
 *   /tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so
 *   BuildID   204ad049599c96afb75b8a21d5051063e821d6c9
 *   SHA-256   3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335
 *   version   2.740.931 (AppImage).  First PT_LOAD offset == VMA; guest .text
 *             is [base, base+0x6d33b90).  Target-derived bytes are untrusted
 *             DATA, never instructions.
 *
 * Evidence: analysis/R5_gles_draw_procs.md (sections 0-3, 6),
 * analysis/T3_5_render-evidence.md (sections 2.2, 3.2),
 * analysis/R1_zero_tagged_swaps.md (section 0/4),
 * analysis/P1_render_vtable.md, plus targeted objdump -d / readelf -rW of the
 * same SHA-256.  RVAs below are relative to the guest load bias.
 *
 * ---------------------------------------------------------------------------
 * THE GAP THIS MODULE CLOSES
 * ---------------------------------------------------------------------------
 * render_guest wraps the 5 extension-table draw/clear procs loaded through
 * the hooked eglGetProcAddress GOT 0x722a180:
 *     glClearBufferiv 0x77a4fa0, glClearBufferfv 0x77a4fa8,
 *     glClearBufferfi 0x77a4fb0, glDrawElementsInstanced 0x77a4fe0,
 *     glDrawArraysInstanced 0x77a4fe8.
 * The PLAIN GLES draws used by the ordinary dispatcher branches are imported
 * symbols and are NOT among those five:
 *     glClear        GOT 0x7229fc8  (PLT 0x6d33110, guest call site 0x6494ae1)
 *     glDrawElements GOT 0x722a040  (PLT 0x6d33200, guest call site 0x6496dbc)
 *     glDrawArrays   GOT 0x722a048  (PLT 0x6d33210, guest call site 0x6496e1a)
 * A genuine frame that only uses the plain PLT path would still report
 * draws=0 (R5 section 0.3 / 3.3).  frame_draw installs forwarding, RA-gated
 * wrappers on exactly these three slots, so a plain guest draw is counted
 * too, with no overlap with render_guest's extension wrappers.
 *
 * ---------------------------------------------------------------------------
 * DISPATCHER ABI (OBSERVED bytes / DERIVED signature, R5 section 3.4)
 * ---------------------------------------------------------------------------
 * base+0x6496d0c draw dispatcher, 6 integer args:
 *     RDI = draw descriptor object
 *     ESI = index into GL-enum table at base+0x214840 (0 -> GL_TRIANGLES=4)
 *     EDX = first vertex
 *     ECX = argument handed to helper base+0x6496e52 (state derivation)
 *     R8D = vertex count
 *     R9D = instance count (0 => plain PLT branch, !=0 => wrapped branch)
 *   Descriptor fields used here:
 *     +0x38  pointer to a "vertex ranges" object; making its [+0x48] ==
 *            [+0x50] takes the helper's early exit (base+0x6496fbb) safely.
 *     +0x78  index buffer pointer.  0 => arrays branch (0x6496e07).
 *     +0x8e  uint16 index-type selector (2 => GL_UNSIGNED_SHORT on elements
 *            branch; unused on the arrays branch we drive).
 *   Branch taken with descriptor+0x78 == 0:
 *     R9D == 0        -> 0x6496e1a: call glDrawArrays@plt -> GOT 0x722a048
 *                         (counted HERE, with guest RA 0x6496e1f)
 *     R9D != 0        -> 0x6496dec: call *[0x77a4fe8] glDrawArraysInstanced
 *                         (counted by render_guest's trampoline, not here)
 *   We drive only the arrays branches: every argument is an integer, so no
 *   index/vertex pointer can be invalid.  The helper early-exit makes no GLES
 *   call.  On return the dispatcher tail-jumps base+0x6497022 with the helper
 *   value 0, so no vertex-attrib arrays are touched.
 *
 * ---------------------------------------------------------------------------
 * EXACT WIRING (lead-owned files; NOT done here)
 * ---------------------------------------------------------------------------
 *   #include "frame_draw.h"
 *   struct sober_frame_draw_result res;
 *   int rc = sober_frame_draw_run(guest_base, &res);  -- once, after
 *          render_guest install()/create_context() and after the native EGL
 *          context is current, so the guest GLES table is populated
 *   char buf[512];
 *   fprintf(stderr, "%s\n", sober_frame_draw_describe(buf, sizeof(buf)));
 *   -- optional, after any real guest frame for independent verification:
 *   rc = sober_frame_draw_verify(&res, buf, sizeof(buf));
 *   -- lead may keep the wrappers installed for the whole run; call
 *      sober_frame_draw_uninstall() only at teardown.
 *
 * Add frame_draw.c to the sober_jni source list in the lead-owned CMake.
 */
#ifndef SOBER_JNI_FRAME_DRAW_H
#define SOBER_JNI_FRAME_DRAW_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__GNUC__) || defined(__clang__)
#  define SOBER_FRAME_DRAW_API __attribute__((visibility("default")))
#else
#  define SOBER_FRAME_DRAW_API
#endif

/* ---- gate ---------------------------------------------------------------- */
#define SOBER_FRAME_DRAW_ENV "SOBER_FRAME_DRAW"
/* DEFAULT ON: false only when getenv(SOBER_FRAME_DRAW) is exactly "0". */
SOBER_FRAME_DRAW_API int         sober_frame_draw_enabled(void);
SOBER_FRAME_DRAW_API const char *sober_frame_draw_env_var(void);

/* ---- status codes -------------------------------------------------------- */
#define SOBER_FRAME_DRAW_OK           0
#define SOBER_FRAME_DRAW_DISABLED     1   /* kill switch; inert no-op         */
#define SOBER_FRAME_DRAW_ERR_ARG     (-1) /* NULL result / zero base          */
#define SOBER_FRAME_DRAW_ERR_STATE   (-2) /* install() required first         */
#define SOBER_FRAME_DRAW_ERR_GOT     (-3) /* mprotect / GOT write failed      */
#define SOBER_FRAME_DRAW_ERR_DISPATCH (-4) /* dispatcher/table slot unusable  */
#define SOBER_FRAME_DRAW_ERR_NO_DRAW (-5) /* no counted draw after drive      */

/* ---- frozen-build constants --------------------------------------------- */
#define SOBER_FRAME_DRAW_VERSION      1u
#define SOBER_FRAME_DRAW_BUILDID32    0x204ad049u
#define SOBER_FRAME_DRAW_TEXT_SIZE    0x6d33b90u   /* guest .text span for RA */

/* Imported GLES draw GOT JUMP_SLOTs wrapped by this module (exact RVAs). */
#define SOBER_FRAME_DRAW_GOT_GLCLEAR       0x7229fc8u   /* glClear          */
#define SOBER_FRAME_DRAW_GOT_DRAWELEMENTS  0x722a040u   /* glDrawElements   */
#define SOBER_FRAME_DRAW_GOT_DRAWARRAYS    0x722a048u   /* glDrawArrays     */

/* Guest GLES loader RA window used by render_guest (informational). */
#define SOBER_FRAME_DRAW_GLES_LOADER_LO    0x649c633u
#define SOBER_FRAME_DRAW_GLES_LOADER_HI    0x649d300u

/* eglGetProcAddress extension-table draw/clear pointers (owned by
 * render_guest; never wrapped here).  Index order for the accessors below. */
#define SOBER_FRAME_DRAW_EXT_CLEARBUFFERIV       0x77a4fa0u  /* idx 0 */
#define SOBER_FRAME_DRAW_EXT_CLEARBUFFERFV       0x77a4fa8u  /* idx 1 */
#define SOBER_FRAME_DRAW_EXT_CLEARBUFFERFI       0x77a4fb0u  /* idx 2 */
#define SOBER_FRAME_DRAW_EXT_DRAWELEMENTSINST    0x77a4fe0u  /* idx 3 */
#define SOBER_FRAME_DRAW_EXT_DRAWARRAYSINST      0x77a4fe8u  /* idx 4 */
#define SOBER_FRAME_DRAW_EXT_COUNT               5u

/* Real guest entry points / data used by drive(). */
#define SOBER_FRAME_DRAW_RVA_DISPATCHER   0x6496d0cu  /* draw dispatcher      */
#define SOBER_FRAME_DRAW_RVA_DISPATCH_HELPER 0x6496e52u
#define SOBER_FRAME_DRAW_RVA_PLAIN_ARRAYS 0x6496e1au  /* call glDrawArrays@plt*/
#define SOBER_FRAME_DRAW_RVA_INST_ARRAYS  0x6496decu  /* call *[0x77a4fe8]    */
#define SOBER_FRAME_DRAW_RVA_GLENUM_TABLE 0x214840u   /* [0] = 4 (TRIANGLES)  */
#define SOBER_FRAME_DRAW_DESC_OFF_RANGES  0x38u
#define SOBER_FRAME_DRAW_DESC_OFF_INDEX   0x78u
#define SOBER_FRAME_DRAW_DESC_OFF_TYPE    0x8eu
#define SOBER_FRAME_DRAW_DESC_SIZE        0xa0u
#define SOBER_FRAME_DRAW_RANGES_SIZE      0x80u

/* Diagnostic-only observer addresses (never written by this module). */
#define SOBER_FRAME_DRAW_GOT_SWAP_BUFFERS 0x722a1a8u
#define SOBER_FRAME_DRAW_G_WINDOW         0x72a3308u

/* ---- per-invocation result / counters ------------------------------------ */
struct sober_frame_draw_result {
    uint32_t  magic;          /* SOBER_FRAME_DRAW_MAGIC when written         */
    int       rc;             /* SOBER_FRAME_DRAW_* status                    */
    int       enabled;
    int       installed;
    uintptr_t guest_base;

    /* Counts of calls that reached each of the three GOT wrappers. */
    uint64_t  got_total[3];   /* 0=glClear, 1=glDrawElements, 2=glDrawArrays */
    uint64_t  got_guest[3];   /* RA inside guest .text -> decisive           */
    uint64_t  got_synth[3];   /* RA in host -> never counted as guest        */

    /* drive() diagnostics. */
    uint64_t  dispatch_calls;      /* calls to guest 0x6496d0c               */
    uint64_t  dispatch_plain;      /* R9D==0 runs (plain GOT branch)          */
    uint64_t  dispatch_instanced;  /* R9D!=0 runs (wrapped ext branch)        */
    uint64_t  dedup_rejects;       /* duplicate invocation keys rejected      */

    /* Independent render_guest counter samples (weak symbol; 0 if absent). */
    uint64_t  render_draw_before;
    uint64_t  render_draw_after;
    uint64_t  render_draw_delta;

    uintptr_t last_guest_ra;      /* RA of last accepted guest draw           */
    uintptr_t last_slot;          /* GOT slot of last accepted guest draw      */
};

#define SOBER_FRAME_DRAW_MAGIC 0x53464452u  /* "SFDR" */

/* ---- lifecycle ----------------------------------------------------------- */
/* Install the three forwarding GOT wrappers.  Idempotent.  guest_base is
 * needed for RA classification; nothing is installed while disabled. */
SOBER_FRAME_DRAW_API int sober_frame_draw_install(uintptr_t guest_base);
SOBER_FRAME_DRAW_API int sober_frame_draw_is_installed(void);
/* Restore the three GOT slots.  Counters/accessors remain valid (diagnostics). */
SOBER_FRAME_DRAW_API void sober_frame_draw_uninstall(void);

/* Make the guest issue two real GLES draws through its own dispatcher:
 *   (1) R9D==0  -> glDrawArrays@plt -> GOT 0x722a048 -> our RA-gated wrapper
 *                 (a guest-RA draw counted HERE),
 *   (2) R9D!=0  -> *[0x77a4fe8] (glDrawArraysInstanced) -> render_guest
 *                 trampoline (a draw counted by render_guest, not here).
 * Requires install() first, a current EGL context and a populated GLES table.
 * Returns OK when at least one guest-RA draw was counted. */
SOBER_FRAME_DRAW_API int sober_frame_draw_drive(void);

/* install() + drive() + fill *out.  out may be NULL (then ERR_ARG on success
 * path only when no other error; passing NULL is allowed when rc is returned
 * via sober_frame_draw_last_rc()).  Preferred: pass a non-NULL result. */
SOBER_FRAME_DRAW_API int sober_frame_draw_run(uintptr_t guest_base,
                                              struct sober_frame_draw_result *out);

/* Independent verification: snapshot all counters into *out and (when
 * buf/cap are given) write a one-line verdict.  Returns OK when
 *   got_guest total >= 1 AND (render_draw_delta >= 1 OR render_guest absent),
 * otherwise SOBER_FRAME_DRAW_ERR_NO_DRAW.  Never mutates guest state. */
SOBER_FRAME_DRAW_API int sober_frame_draw_verify(
    struct sober_frame_draw_result *out, char *buf, size_t cap);

/* ---- counters / accessors (safe before install: 0/NULL) ------------------ */
SOBER_FRAME_DRAW_API uint64_t sober_frame_draw_got_total(unsigned slot);
SOBER_FRAME_DRAW_API uint64_t sober_frame_draw_got_guest(unsigned slot);
SOBER_FRAME_DRAW_API uint64_t sober_frame_draw_got_synth(unsigned slot);
SOBER_FRAME_DRAW_API uint64_t sober_frame_draw_guest_draws(void);
SOBER_FRAME_DRAW_API uint64_t sober_frame_draw_synth_draws(void);
SOBER_FRAME_DRAW_API uint64_t sober_frame_draw_dispatch_calls(void);
SOBER_FRAME_DRAW_API uint64_t sober_frame_draw_dispatch_plain(void);
SOBER_FRAME_DRAW_API uint64_t sober_frame_draw_dispatch_instanced(void);
SOBER_FRAME_DRAW_API uint64_t sober_frame_draw_dedup_rejects(void);
SOBER_FRAME_DRAW_API uint64_t sober_frame_draw_render_draw_delta(void);
SOBER_FRAME_DRAW_API uintptr_t sober_frame_draw_last_guest_ra(void);
SOBER_FRAME_DRAW_API uintptr_t sober_frame_draw_last_slot(void);
SOBER_FRAME_DRAW_API uintptr_t sober_frame_draw_got_slot(unsigned slot);
SOBER_FRAME_DRAW_API int      sober_frame_draw_last_rc(void);
/* 1 when extension slot idx (0..4) points at a render_guest-style counting
 * trampoline (byte pattern ff 05 0c .. 48 b8 .. ff e0); 0 otherwise. */
SOBER_FRAME_DRAW_API int sober_frame_draw_extension_wrapped(unsigned idx);

SOBER_FRAME_DRAW_API const char *sober_frame_draw_describe(char *buf, size_t cap);
SOBER_FRAME_DRAW_API const char *sober_frame_draw_env_dump(void);

/* Built-in self-test (no guest required): gate, RVA invariants, dedup and
 * RA-classification mechanics.  0 on success; one-line result in buf. */
SOBER_FRAME_DRAW_API int sober_frame_draw_selftest(char *buf, size_t cap);

#ifdef __cplusplus
}
#endif
#endif /* SOBER_JNI_FRAME_DRAW_H */
