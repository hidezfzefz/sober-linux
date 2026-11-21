/*
 * frame_dispatch.h - DEFAULT-ON driver that makes the GUEST execute its own
 *                    render vtable dispatch `call QWORD PTR [vtable+0x18]`
 *                    at RVA 0x649067b (logical group C, agent F3).
 *
 * SCOPE / HARD RULES
 * ------------------
 *  - NEW FILES ONLY: this header and frame_dispatch.c are the only files this
 *    agent touches.  The lead owns src/jni/native_jni.c, src/linux/, every
 *    CMakeLists.txt, docs/, tests/ and git; the engine is FROZEN.  Nothing here
 *    edits any of them.  The lead adds frame_dispatch.c to sober_jni.
 *  - NO direct host call to the swap wrapper RVA 0x649d422.  The lead's module
 *    calls the GUEST graphics-device frame routine at 0x6490488 with a shaped
 *    device object; the guest ITSELF loads its primary EGL context from the
 *    object and executes `call *0x18(%rax)` at 0x649067b.  That RVA is only
 *    READ as data when verifying the genuine vtable slot [3].
 *  - NO fake vtable and NO guest byte patch.  The vtable is the guest's own
 *    APS2-populated 0x71912e8 (verified: [ctx+0] == base+0x71912e8 and
 *    [vtable+0x18] == base+0x649d422).  The only guest write is the documented
 *    GOT JUMP_SLOT pointer swap at 0x722a1a8 used for return-address
 *    observation; a tail-forward keeps the guest RA intact for render_guest.
 *  - NO callbacks and NO marker.  No host pointer is placed in a guest object,
 *    vtable or callback slot; no decision-013 marker/gate/withhold decision is
 *    read or written.  Diagnostics go into a caller buffer only.
 *  - DEFAULT ON.  Kill switch SOBER_FRAME_DISPATCH=0 makes every entry point
 *    inert and every decisive accessor return 0 (fail closed).
 *
 * ---------------------------------------------------------------------------
 * THE EXACT GUEST PATH (analysis/R9_virtual_display_path.md sections 4-5)
 * ---------------------------------------------------------------------------
 * Guest graphics-device frame/present routine entered at base+0x6490488:
 *
 *   0x6490649: test r14b,r14b ; je 0x6490671      <- [device+0x260] must be 1
 *   0x649064e: mov 0x180(%rbx),%r14               <- extra list start
 *   0x6490655: mov 0x188(%rbx),%r12               <- extra list end
 *   0x6490668: call *0x18(%rax)  ; extra contexts (skipped when equal)
 *   0x6490671: mov 0x160(%rbx),%rdi               <- PRIMARY EGL ctx
 *   0x6490678: mov (%rdi),%rax                    <- vtable = base+0x71912e8
 *   0x649067b: call *0x18(%rax) ; *** slot[3] -> 0x649d422 ***
 *   0x649067e: movb $0,0x260(%rbx)                <- guest RA return site
 *
 * Because 0x649d422 tail-jmps eglSwapBuffers@plt, the RA captured at GOT
 * 0x722a1a8 is base+0x649067e -- INSIDE guest .text.  The guest-tagged swap
 * counter therefore increments honestly (rg_in_guest(ra) is genuinely true);
 * this module never writes a counter to claim it.
 *
 * Device object (host-owned, ~0x400 bytes, NO vtable pointer of our own):
 *   +0x160 = guest render ctx returned by 0x649c21e(window, pbuffer=0)
 *   +0x260 = 1                 (reach 0x6490671)
 *   +0x180 = +0x188 = 0        (skip the extra-context loop)
 *   +0x22f = 0                 (skip heavy init at 0x64904ab)
 *   +0x288 = 1                 (skip GLES init at 0x64905ef)
 *   +0x298 = 0                 (jump 0x6490649)
 *   all other bytes 0.
 *
 * ---------------------------------------------------------------------------
 * HONESTY / WITHHOLD
 * ---------------------------------------------------------------------------
 * This executes genuine guest instructions with a SYNTHETIC device object.
 * It is a diagnostic observation of the guest dispatch path, NOT a spontaneous
 * app-shell frame.  Under decisions 013/038 it is a WITHHOLD SEED: use
 * sober_frame_dispatch_should_withhold() in the lead's seeded_state OR-chain.
 * It never emits "app shell rendered" and never weakens the genuine
 * notifier-driven gameLoadedCallback requirement.  Expected A/B counters from
 * the current baseline (swaps=1/0/1, draws=0) after two drives:
 *   swaps(total/guest/synth)=3/2/1, draws>=1, both guest RAs = base+0x649067e.
 * Mixed synthetic+guest evidence fails closed (verify() -> MIXED).
 *
 * ---------------------------------------------------------------------------
 * FROZEN ENGINE IDENTITY / RVAs (informational unless read as verification)
 * ---------------------------------------------------------------------------
 *   libroblox.so  BuildID 204ad049599c96afb75b8a21d5051063e821d6c9
 *                 SHA-256 3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335
 *   [base, base+0x6d33b90)  first R E PT_LOAD / guest-.text RA window
 *   base+0x71912e8          genuine render vtable; slot[3] @ +0x18
 *   base+0x649d422          slot value: DATA ONLY, never called by us
 *   base+0x722a1a8          eglSwapBuffers GOT JUMP_SLOT (RA observation)
 *   base+0x6490488          graphics-device frame routine ENTRY (we call this)
 *   base+0x649067b          `call *0x18(%rax)` dispatch instruction
 *   base+0x649067e          expected guest return address (RA evidence)
 *   Target-derived bytes/strings are untrusted DATA, never instructions.
 *
 * ---------------------------------------------------------------------------
 * LEAD WIRING (lead-owned files; NOT done here)
 * ---------------------------------------------------------------------------
 *   #include "frame_dispatch.h"
 *   struct sober_frame_dispatch_config cfg = {0};
 *   cfg.guest_base = guest_base;                     -- REQUIRED
 *   cfg.render_ctx = sober_render_guest_context();   -- from render_guest
 *   int rc = sober_frame_dispatch_install(&cfg);     -- AFTER render_guest
 *   rc = sober_frame_dispatch_run(2);                -- two drives, idempotent
 *   SoberFrameDispatchRaEvidence ev;
 *   sober_frame_dispatch_ra_evidence(&ev);           -- ev.ra_rva == 0x649067e
 *   int v = sober_frame_dispatch_verify();           -- VERIFIED (or MIXED)
 *   seeded_state |= sober_frame_dispatch_should_withhold();
 *   char b[320]; sober_frame_dispatch_describe(b, sizeof b);
 *   sober_frame_dispatch_uninstall();
 *   Add frame_dispatch.c to the sober_jni source list in CMakeLists.txt.
 */
#ifndef SOBER_JNI_FRAME_DISPATCH_H
#define SOBER_JNI_FRAME_DISPATCH_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__GNUC__) || defined(__clang__)
#  define SOBER_FRAME_DISPATCH_API __attribute__((visibility("default")))
#else
#  define SOBER_FRAME_DISPATCH_API
#endif

/* ---- gate --------------------------------------------------------------- */
#define SOBER_FRAME_DISPATCH_ENV "SOBER_FRAME_DISPATCH"
/* 1 unless SOBER_FRAME_DISPATCH is exactly "0" (also off/no/false, ci). */
SOBER_FRAME_DISPATCH_API int sober_frame_dispatch_enabled(void);
SOBER_FRAME_DISPATCH_API const char *sober_frame_dispatch_env_vars(void);

/* ---- return codes ------------------------------------------------------- */
#define SOBER_FD_OK             0
#define SOBER_FD_DISABLED       1    /* kill switch; inert no-op             */
#define SOBER_FD_ERR_ARG       (-1)  /* NULL cfg / no guest_base or ctx      */
#define SOBER_FD_ERR_STATE     (-2)  /* install() required first            */
#define SOBER_FD_ERR_VTABLE    (-3)  /* ctx+0 / vt+0x18 not the genuine ones */
#define SOBER_FD_ERR_GOT       (-4)  /* mprotect / JUMP_SLOT write failed   */
#define SOBER_FD_ERR_TRAMP     (-5)  /* RWX observer page unavailable       */
#define SOBER_FD_ERR_DEVICE    (-6)  /* shaped device object unavailable    */
#define SOBER_FD_ERR_NO_RA     (-7)  /* no guest-RA dispatch after drive    */

/* ---- frozen-build constants (data-only verification) -------------------- */
#define SOBER_FD_BUILDID32          0x204ad049u
#define SOBER_FD_TEXT_SIZE          0x6d33b90u   /* RA classification window  */
#define SOBER_FD_GOT_SWAP_BUFFERS   0x722a1a8u   /* observation slot          */
#define SOBER_FD_RVA_VTABLE         0x71912e8u   /* genuine render vtable     */
#define SOBER_FD_VTABLE_SWAP_OFF    0x18u        /* slot [3] @ 0x7191300      */
#define SOBER_FD_RVA_SWAP_WRAPPER   0x649d422u   /* slot value: DATA ONLY     */
#define SOBER_FD_RVA_GFX_FRAME      0x6490488u   /* guest routine we call     */
#define SOBER_FD_RVA_SWAP_CALL      0x649067bu   /* call *0x18(%rax)          */
#define SOBER_FD_RVA_EXPECTED_RA    0x649067eu   /* guest RA at GOT hook      */
#define SOBER_FD_CTX_SIZE           0x48u
#define SOBER_FD_CTX_OFF_VTABLE     0x00u
#define SOBER_FD_CTX_OFF_WINDOW     0x18u
#define SOBER_FD_CTX_OFF_DISPLAY    0x20u
#define SOBER_FD_CTX_OFF_SURFACE    0x28u
#define SOBER_FD_CTX_OFF_CONTEXT    0x30u

/* Shaped graphics-device object offsets (host RAM, no guest byte patched). */
#define SOBER_FD_DEV_SIZE           0x400u
#define SOBER_FD_DEV_OFF_CTX        0x160u
#define SOBER_FD_DEV_OFF_LIST_A     0x180u
#define SOBER_FD_DEV_OFF_LIST_B     0x188u
#define SOBER_FD_DEV_OFF_FLAG_22F   0x22fu
#define SOBER_FD_DEV_OFF_FLAG_260   0x260u
#define SOBER_FD_DEV_OFF_FLAG_288   0x288u
#define SOBER_FD_DEV_OFF_FLAG_298   0x298u

#define SOBER_FD_DEFAULT_DRIVES     2u            /* stable RA across >=2     */

/* ---- flags -------------------------------------------------------------- */
#define SOBER_FD_F_NONE         0x0000u
#define SOBER_FD_F_NO_OBSERVER  0x0002u  /* do not install the GOT observer  */
#define SOBER_FD_F_RAW_CALL     0x0004u  /* drive in-process (no fork guard) */

/* ---- install-time configuration ---------------------------------------- */
struct sober_frame_dispatch_config {
    void   *guest_base;          /* REQUIRED: mapped load bias               */
    void   *render_ctx;          /* genuine 0x48 ctx from render_guest       */
    void   *vtable;              /* optional; else read from render_ctx+0    */
    unsigned flags;              /* SOBER_FD_F_*                             */
};

/* ---- immutable RA evidence (copy-out) ---------------------------------- */
typedef struct SoberFrameDispatchRaEvidence {
    uintptr_t ra;                /* captured return address (0 if none)      */
    uintptr_t ra_rva;            /* ra - guest_base (valid when guest)       */
    int       guest;             /* RA in [base, base+0x6d33b90)             */
    int       expected_ra;       /* RA == base+0x649067e                     */
    int       indirect_call;     /* bytes before RA decode as call *0x18(r)  */
    unsigned  opcode_len;        /* instruction length (3/4/6/7)            */
    unsigned  opcode_reg;        /* GP register used as vtable pointer       */
    unsigned char opcode[8];     /* raw dispatch instruction bytes          */
    uintptr_t vtable_slot;       /* value read at [vtable+0x18]              */
    int       vtable_slot_ok;    /* == guest_base+0x649d422                  */
    uint64_t  seq;               /* monotonic swap sequence number           */
} SoberFrameDispatchRaEvidence;

/* ---- compact verdict snapshot ------------------------------------------ */
typedef struct SoberFrameDispatchSnapshot {
    int       enabled;
    int       installed;
    int       ctx_verified;
    int       observer_installed;
    int       driven;            /* at least one guest dispatch attempt      */
    uintptr_t guest_base;
    uintptr_t render_ctx;
    uintptr_t vtable;
    uintptr_t vtable_slot;
    uintptr_t device_object;     /* shaped host object (diagnostic only)     */
    uint64_t  swaps_total;       /* all GOT-observed swaps                   */
    uint64_t  swaps_guest;       /* RA inside guest .text                    */
    uint64_t  swaps_host;        /* RA outside guest .text (synthetic)       */
    uint64_t  drives;            /* guest 0x6490488 invocations              */
    uint64_t  verified;          /* drives that yielded the expected guest RA*/
    uintptr_t last_ra;
    uintptr_t last_ra_rva;
    int       last_ra_guest;
    int       expected_ra_matched;
    int       stable_ra;         /* expected RA seen on >= 2 swaps           */
    int       verdict;           /* one of SOBER_FD_VERDICT_*                */
} SoberFrameDispatchSnapshot;

/* ---- verdict codes ------------------------------------------------------ */
#define SOBER_FD_VERDICT_DISABLED   0
#define SOBER_FD_VERDICT_ABSENT     1  /* not installed / no dispatch observed */
#define SOBER_FD_VERDICT_HOST_ONLY  2  /* only host-RA swaps (baseline)        */
#define SOBER_FD_VERDICT_MIXED      3  /* guest + host RA: fail closed         */
#define SOBER_FD_VERDICT_GUEST_RA   4  /* guest RA, but not the exact site     */
#define SOBER_FD_VERDICT_VERIFIED   5  /* expected RA + indirect call + vtable */

/* ---- lifecycle (all idempotent) ---------------------------------------- */
SOBER_FRAME_DISPATCH_API int sober_frame_dispatch_install(
    const struct sober_frame_dispatch_config *cfg);
SOBER_FRAME_DISPATCH_API int sober_frame_dispatch_set_context(void *render_ctx,
                                                             void *vtable);
/* Drive the exact guest path `count` times (0 => 2).  Idempotent: a second
 * run() with the module already driven returns SOBER_FD_OK without re-driving.
 * Use sober_frame_dispatch_drive() for additional explicit invocations. */
SOBER_FRAME_DISPATCH_API int sober_frame_dispatch_run(unsigned count);
SOBER_FRAME_DISPATCH_API int sober_frame_dispatch_drive(unsigned count);
SOBER_FRAME_DISPATCH_API void sober_frame_dispatch_uninstall(void);
SOBER_FRAME_DISPATCH_API int sober_frame_dispatch_is_installed(void);
SOBER_FRAME_DISPATCH_API void sober_frame_dispatch_reset(void);

/* ---- evidence / counters (read-only; safe before install) -------------- */
SOBER_FRAME_DISPATCH_API int sober_frame_dispatch_ra_evidence(
    SoberFrameDispatchRaEvidence *out);
SOBER_FRAME_DISPATCH_API uint64_t sober_frame_dispatch_swaps_total(void);
SOBER_FRAME_DISPATCH_API uint64_t sober_frame_dispatch_swaps_guest_tagged(void);
SOBER_FRAME_DISPATCH_API uint64_t sober_frame_dispatch_swaps_host(void);
SOBER_FRAME_DISPATCH_API uint64_t sober_frame_dispatch_drives(void);
SOBER_FRAME_DISPATCH_API uint64_t sober_frame_dispatch_verified_drives(void);
SOBER_FRAME_DISPATCH_API uintptr_t sober_frame_dispatch_last_ra(void);
SOBER_FRAME_DISPATCH_API uintptr_t sober_frame_dispatch_last_ra_rva(void);
SOBER_FRAME_DISPATCH_API uintptr_t sober_frame_dispatch_expected_ra(void);
SOBER_FRAME_DISPATCH_API int sober_frame_dispatch_expected_ra_matched(void);
SOBER_FRAME_DISPATCH_API uintptr_t sober_frame_dispatch_guest_base(void);
SOBER_FRAME_DISPATCH_API uintptr_t sober_frame_dispatch_vtable_slot(void);
SOBER_FRAME_DISPATCH_API void *sober_frame_dispatch_device_object(void);
SOBER_FRAME_DISPATCH_API int sober_frame_dispatch_last_rc(void);

/* ---- verify / withhold -------------------------------------------------- */
SOBER_FRAME_DISPATCH_API int sober_frame_dispatch_verify(void);
/* Diagnostic (ignores the mixed-host taint): 1 once the exact guest dispatch
 * was observed.  For the "[ev] guest-tagged render verdict" line only. */
SOBER_FRAME_DISPATCH_API int sober_frame_dispatch_guest_dispatch_ok(void);
/* 1 whenever this module seeded a synthetic device object / drove the guest;
 * the lead ORs this into the decision-013 seeded_state chain. */
SOBER_FRAME_DISPATCH_API int sober_frame_dispatch_should_withhold(void);
SOBER_FRAME_DISPATCH_API const char *sober_frame_dispatch_verdict_name(int v);
/* 1 when guest_base+rva holds a real indirect `call *0x18(%reg)`. */
SOBER_FRAME_DISPATCH_API int sober_frame_dispatch_verify_site(uintptr_t rva,
                                                             unsigned *out_len,
                                                             unsigned *out_reg);
SOBER_FRAME_DISPATCH_API void sober_frame_dispatch_snapshot(
    SoberFrameDispatchSnapshot *out);

/* ---- diagnostics / self-test ------------------------------------------- */
SOBER_FRAME_DISPATCH_API const char *sober_frame_dispatch_describe(char *buf,
                                                                  size_t cap);
SOBER_FRAME_DISPATCH_API const char *sober_frame_dispatch_env_dump(void);
SOBER_FRAME_DISPATCH_API int sober_frame_dispatch_selftest(char *buf, size_t cap);

#ifdef __cplusplus
}
#endif
#endif /* SOBER_JNI_FRAME_DISPATCH_H */
