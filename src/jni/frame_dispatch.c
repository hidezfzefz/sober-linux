/*
 * frame_dispatch.c - DEFAULT-ON genuine guest frame-dispatch driver.
 * See frame_dispatch.h for the exact R9 site, device-object shape and wiring.
 *
 * HARD RULES OBSERVED HERE
 * ------------------------
 *  - NEW FILES ONLY.  No CMake/engine/gate/test edits, no marker.
 *  - Never calls guest_base+0x649d422.  That RVA is only READ as data when
 *    verifying the genuine render vtable slot [3].  We call the GUEST routine
 *    at 0x6490488; the guest executes `call *0x18(%rax)` at 0x649067b itself.
 *  - Never patches a guest instruction byte.  The only guest write is the
 *    documented GOT JUMP_SLOT swap at 0x722a1a8 (observation hook that
 *    tail-forwards to the previous hook/driver with the guest RA intact).
 *  - Never installs a host pointer in a guest vtable/callback slot.  The
 *    shaped device object is host RAM with a guest ctx pointer at +0x160 and
 *    no vtable of our own.
 *  - Evidence lives in MAP_SHARED memory so a child fault in the guest routine
 *    tail (R9 section 6) cannot lose an already-captured guest-RA swap.
 */
#define _GNU_SOURCE
#include "frame_dispatch.h"

#include <errno.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

/* Forward REAL observed swap events to frame_evidence, the sole render-leg
 * verdict owner.  Weak so this TU still links without frame_evidence.c.
 * Only events actually captured by the GOT observer are forwarded; the
 * forwarding loop below never manufactures a count. */
#if defined(__GNUC__) || defined(__clang__)
extern void frame_evidence_record_swap(uintptr_t ra) __attribute__((weak));
#else
extern void frame_evidence_record_swap(uintptr_t ra);
#endif

/* ---- shared observation state (visible across the fork guard) ----------- */
struct fd_shared {
    _Atomic uint64_t swaps_total;
    _Atomic uint64_t swaps_guest;
    _Atomic uint64_t swaps_host;
    _Atomic uint64_t drives;
    _Atomic uint64_t verified;
    _Atomic uint64_t forward_guest; /* real guest-RA swap events observed     */
    _Atomic uint64_t forward_host;  /* real host-RA swap events observed      */
    _Atomic uintptr_t last_guest_ra;
    _Atomic uintptr_t last_host_ra;
    _Atomic uintptr_t last_ra;      /* written directly by the GOT stub       */
    _Atomic uint64_t seq;
    uintptr_t first_ra;
    int       stable_ra;
    uintptr_t site_rva;
    SoberFrameDispatchRaEvidence evidence;
};

/* ---- module state -------------------------------------------------------- */
static struct fd_shared *s_shared;
static uint64_t s_forwarded_guest;
static uint64_t s_forwarded_host;
static uintptr_t s_base;
static uintptr_t s_ctx;
static uintptr_t s_vt;
static uintptr_t s_slot;                /* value at [vtable+0x18]            */
static uintptr_t s_got_slot;            /* base+0x722a1a8                    */
static uintptr_t s_saved_target;        /* previous GOT value                */
static unsigned char *s_tramp;          /* RWX observer page                 */
static unsigned char *s_dev;            /* shaped ~0x400-byte device object  */
static int       s_installed;
static int       s_ctx_verified;
static int       s_observer;
static int       s_run_done;
static int       s_withhold;            /* sticky run-level withhold signal  */
static unsigned  s_flags;
static int       s_last_rc = SOBER_FD_DISABLED;

/* ---- small helpers ------------------------------------------------------- */
static int fd_env_disabled(void)
{
    const char *v = getenv(SOBER_FRAME_DISPATCH_ENV);
    if (!v || !*v) return 0;
    if (strcmp(v, "0") == 0 || strcasecmp(v, "off") == 0 ||
        strcasecmp(v, "no") == 0 || strcasecmp(v, "false") == 0) return 1;
    return 0;
}

int sober_frame_dispatch_enabled(void) { return fd_env_disabled() ? 0 : 1; }

const char *sober_frame_dispatch_env_vars(void)
{
    return SOBER_FRAME_DISPATCH_ENV "=0 (kill); default ON";
}

static int fd_in_guest(uintptr_t ra)
{
    return s_base != 0u && ra >= s_base &&
           ra < s_base + (uintptr_t)SOBER_FD_TEXT_SIZE;
}

static int fd_is_expected_ra(uintptr_t ra)
{
    return s_base != 0u &&
           ra == s_base + (uintptr_t)SOBER_FD_RVA_EXPECTED_RA;
}

static void fd_bump(_Atomic uint64_t *c)
{
    atomic_fetch_add_explicit(c, 1u, memory_order_relaxed);
}

/* ---- decode `call QWORD PTR [reg+0x18]`: ff /2 with 0x18 displacement ---- */
static int fd_decode_call18(const unsigned char *p, unsigned *len,
                            unsigned *reg)
{
    const unsigned char *q = p;
    unsigned rex = 0u, modrm, mod, rm, n = 0u;
    uint32_t disp;

    if (*q >= 0x40u && *q <= 0x4fu) { rex = *q++; n = 1u; }
    if (q[0] != 0xffu) return 0;
    modrm = q[1];
    if (((modrm >> 3) & 7u) != 2u) return 0;   /* /2 == CALL indirect */
    mod = modrm >> 6;
    rm  = modrm & 7u;
    if (rm == 4u) return 0;                    /* SIB form: unsupported      */
    if (mod == 1u) {
        if (q[2] != 0x18u) return 0;
        n += 3u;
    } else if (mod == 2u) {
        memcpy(&disp, q + 2, sizeof(disp));
        if (disp != 0x18u) return 0;
        n += 6u;
    } else {
        return 0;                               /* mod=0 has no +0x18 disp    */
    }
    *len = n;
    *reg = rm + ((rex & 1u) ? 8u : 0u);
    return 1;
}

int sober_frame_dispatch_verify_site(uintptr_t rva, unsigned *out_len,
                                     unsigned *out_reg)
{
    unsigned len = 0u, reg = 0u;
    if (sober_frame_dispatch_enabled() && s_base != 0u &&
        rva < (uintptr_t)SOBER_FD_TEXT_SIZE &&
        fd_decode_call18((const unsigned char *)(s_base + rva), &len, &reg)) {
        if (out_len) *out_len = len;
        if (out_reg) *out_reg = reg;
        return 1;
    }
    if (out_len) *out_len = 0u;
    if (out_reg) *out_reg = 0u;
    return 0;
}

/* ---- GOT observer ------------------------------------------------------- *
 * JUMP_SLOT 0x722a1a8 points to this RWX stub.  It saves the TRUE return
 * address (the instruction after the guest's call), calls the C recorder,
 * restores every integer argument register, then tail-jumps to the previous
 * slot value.  The original guest RA stays on the stack, so render_guest's
 * downstream hook classifies the same swap with the same guest RA.
 *
 *   48 8b 04 24           mov  rax,[rsp]           ; true RA
 *   48 a3 <shared.last_ra> mov  [abs],rax
 *   51 52 56 57           push rcx/rdx/rsi/rdi
 *   41 50 41 51 41 52 41 53                        push r8..r11
 *   48 89 c7             mov  rdi,rax
 *   48 83 ec 08          sub  rsp,8
 *   48 b8 <fd_on_swap>   movabs rax, recorder
 *   ff d0                call rax
 *   48 83 c4 08          add  rsp,8
 *   41 5b 41 5a 41 59 41 58 5f 5e 5a 59           pop in reverse
 *   49 bb <s_saved_target> movabs r11, &slot
 *   41 ff 23             jmp  [r11]
 */
#define FD_STUB_SIZE 128u

static void fd_on_swap(uintptr_t ra);

static int fd_build_observer(void)
{
    unsigned char *c;
    uintptr_t imm;

    if (!s_shared) return -1;
    s_tramp = (unsigned char *)mmap(NULL, FD_STUB_SIZE,
                                    PROT_READ | PROT_WRITE | PROT_EXEC,
                                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (s_tramp == MAP_FAILED) { s_tramp = NULL; return -1; }
    c = s_tramp;
    c[0] = 0x48; c[1] = 0x8b; c[2] = 0x04; c[3] = 0x24;
    c[4] = 0x48; c[5] = 0xa3;
    imm = (uintptr_t)&s_shared->last_ra;
    memcpy(c + 6, &imm, sizeof(imm));
    c[14] = 0x51; c[15] = 0x52; c[16] = 0x56; c[17] = 0x57;
    c[18] = 0x41; c[19] = 0x50; c[20] = 0x41; c[21] = 0x51;
    c[22] = 0x41; c[23] = 0x52; c[24] = 0x41; c[25] = 0x53;
    c[26] = 0x48; c[27] = 0x89; c[28] = 0xc7;
    c[29] = 0x48; c[30] = 0x83; c[31] = 0xec; c[32] = 0x08;
    c[33] = 0x48; c[34] = 0xb8;
    imm = (uintptr_t)&fd_on_swap;
    memcpy(c + 35, &imm, sizeof(imm));
    c[43] = 0xff; c[44] = 0xd0;
    c[45] = 0x48; c[46] = 0x83; c[47] = 0xc4; c[48] = 0x08;
    c[49] = 0x41; c[50] = 0x5b; c[51] = 0x41; c[52] = 0x5a;
    c[53] = 0x41; c[54] = 0x59; c[55] = 0x41; c[56] = 0x58;
    c[57] = 0x5f; c[58] = 0x5e; c[59] = 0x5a; c[60] = 0x59;
    c[61] = 0x49; c[62] = 0xbb;
    imm = (uintptr_t)&s_saved_target;
    memcpy(c + 63, &imm, sizeof(imm));
    c[71] = 0x41; c[72] = 0xff; c[73] = 0x23;
    __builtin___clear_cache((char *)c, (char *)(c + FD_STUB_SIZE));
    return 0;
}

static int fd_page_prot(uintptr_t page)
{
    FILE *f = fopen("/proc/self/maps", "r");
    char line[512];
    int prot = -1;
    if (!f) return -1;
    while (fgets(line, sizeof(line), f)) {
        unsigned long lo = 0u, hi = 0u;
        char perms[8] = { 0 };
        if (sscanf(line, "%lx-%lx %4s", &lo, &hi, perms) != 3) continue;
        if ((uintptr_t)lo <= page && page < (uintptr_t)hi) {
            prot = 0;
            if (perms[0] == 'r') prot |= PROT_READ;
            if (perms[1] == 'w') prot |= PROT_WRITE;
            if (perms[2] == 'x') prot |= PROT_EXEC;
            break;
        }
    }
    fclose(f);
    return prot;
}

static int fd_got_write(uintptr_t slot, uintptr_t val, uintptr_t *old_out)
{
    long ps = sysconf(_SC_PAGESIZE);
    uintptr_t page;
    int prot;
    uintptr_t old;

    if (ps <= 0) ps = 4096;
    page = slot & ~((uintptr_t)ps - 1u);
    prot = fd_page_prot(page);
    if (mprotect((void *)page, (size_t)ps, PROT_READ | PROT_WRITE) != 0)
        return -1;
    old = *(volatile uintptr_t *)slot;
    *(volatile uintptr_t *)slot = val;
    if (prot >= 0 && prot != (PROT_READ | PROT_WRITE))
        (void)mprotect((void *)page, (size_t)ps, prot);
    if (old_out) *old_out = old;
    return 0;
}

/* ---- evidence recording ------------------------------------------------- */
static void fd_record_evidence(uintptr_t ra)
{
    SoberFrameDispatchRaEvidence *e;
    unsigned len = 0u, reg = 0u, k;
    uint64_t seq;

    if (!s_shared) return;
    fd_bump(&s_shared->swaps_total);
    if (fd_in_guest(ra)) {
        fd_bump(&s_shared->swaps_guest);
        fd_bump(&s_shared->forward_guest);   /* one real event -> one forward */
        atomic_store_explicit(&s_shared->last_guest_ra, ra,
                              memory_order_relaxed);
        if (s_shared->first_ra == 0u) s_shared->first_ra = ra;
        else if (s_shared->first_ra == ra) s_shared->stable_ra = 1;
    } else {
        fd_bump(&s_shared->swaps_host);
        fd_bump(&s_shared->forward_host);    /* keep mixed evidence honest    */
        atomic_store_explicit(&s_shared->last_host_ra, ra,
                              memory_order_relaxed);
        /* A later host/Route-B swap must not erase the guest-RA evidence
         * already captured: counters and last_ra still record it faithfully. */
        if (s_shared->evidence.guest) return;
    }

    e = &s_shared->evidence;
    memset(e, 0, sizeof(*e));
    e->ra = ra;
    e->ra_rva = fd_in_guest(ra) ? ra - s_base : 0u;
    e->guest = fd_in_guest(ra);
    e->expected_ra = fd_is_expected_ra(ra);
    e->opcode_len = 0u;
    if (e->guest && s_base != 0u) {
        static const unsigned lens[4] = { 3u, 4u, 6u, 7u };
        for (k = 0u; k < 4u; ++k) {
            unsigned l = lens[k];
            if (ra < s_base || (uintptr_t)(ra - s_base) < l) continue;
            if (fd_decode_call18((const unsigned char *)(ra - l), &len, &reg) &&
                len == l) {
                e->indirect_call = 1;
                e->opcode_len = len;
                e->opcode_reg = reg;
                memcpy(e->opcode, (const void *)(ra - l), len);
                break;
            }
        }
    }
    e->vtable_slot = s_slot;
    e->vtable_slot_ok = (s_vt != 0u && s_base != 0u &&
                         s_slot == s_base + (uintptr_t)SOBER_FD_RVA_SWAP_WRAPPER);
    seq = atomic_fetch_add_explicit(&s_shared->seq, 1u,
                                    memory_order_relaxed) + 1u;
    e->seq = seq;
}

static void fd_on_swap(uintptr_t ra)
{
    fd_record_evidence(ra);             /* shared: survives a later child fault */
}

/* Forward exactly the events the GOT observer really captured.  Called in the
 * PARENT after fd_drive_once(), so a child fault cannot lose/corrupt the
 * frame_evidence process-local immutable log.  The child may observe a host
 * swap too; forwarding it keeps frame_evidence's sticky mixed taint honest. */
static void fd_forward_evidence(void)
{
    void (*record)(uintptr_t) = frame_evidence_record_swap;
    uint64_t g, h;
    if (!record || !s_shared) return;
    g = atomic_load_explicit(&s_shared->forward_guest, memory_order_relaxed);
    h = atomic_load_explicit(&s_shared->forward_host, memory_order_relaxed);
    while (s_forwarded_guest < g) {
        uintptr_t ra = atomic_load_explicit(&s_shared->last_guest_ra,
                                           memory_order_relaxed);
        record(ra);
        ++s_forwarded_guest;
    }
    while (s_forwarded_host < h) {
        uintptr_t ra = atomic_load_explicit(&s_shared->last_host_ra,
                                           memory_order_relaxed);
        record(ra);                    /* 0 is classified as synthetic */
        ++s_forwarded_host;
    }
}

/* ---- shaped device object ------------------------------------------------ */
static int fd_build_device(void)
{
    uintptr_t w;
    if (s_dev) return 0;
    s_dev = (unsigned char *)calloc(1u, (size_t)SOBER_FD_DEV_SIZE);
    if (!s_dev) return -1;

    w = s_ctx;
    memcpy(s_dev + (size_t)SOBER_FD_DEV_OFF_CTX, &w, sizeof(w));
    /* +0x180 == +0x188 == 0 (calloc): skip the extra-context loop. */
    s_dev[SOBER_FD_DEV_OFF_FLAG_22F] = 0u;
    s_dev[SOBER_FD_DEV_OFF_FLAG_260] = 1u;
    s_dev[SOBER_FD_DEV_OFF_FLAG_288] = 1u;
    s_dev[SOBER_FD_DEV_OFF_FLAG_298] = 0u;
    return 0;
}

/* ---- drive the exact guest path ----------------------------------------- */
typedef void (*fd_gfx_frame_fn)(void *dev);

/* One guest invocation.  Returns the RA captured at GOT 0x722a1a8 (0 if no
 * swap was observed).  The fork guard keeps a fault in the guest tail from
 * taking down the harness; MAP_SHARED evidence survives either outcome. */
static uintptr_t fd_drive_once(void)
{
    fd_gfx_frame_fn frame = (fd_gfx_frame_fn)(s_base +
                                    (uintptr_t)SOBER_FD_RVA_GFX_FRAME);
    pid_t pid;

    if (!s_installed || !s_dev || !frame) return 0u;
    atomic_store_explicit(&s_shared->last_ra, 0u, memory_order_relaxed);
    fd_bump(&s_shared->drives);

    if (s_flags & SOBER_FD_F_RAW_CALL) {
        frame(s_dev);
        return atomic_load_explicit(&s_shared->last_ra, memory_order_relaxed);
    }

    pid = fork();
    if (pid < 0) {
        /* Fork unavailable: fall back to a direct guest call. */
        frame(s_dev);
        return atomic_load_explicit(&s_shared->last_ra, memory_order_relaxed);
    }
    if (pid == 0) {
        (void)alarm(3u);
        frame(s_dev);
        _exit(0);
    }
    for (;;) {
        int status = 0;
        pid_t r = waitpid(pid, &status, 0);
        if (r >= 0 || errno != EINTR) break;
    }
    return atomic_load_explicit(&s_shared->last_ra, memory_order_relaxed);
}

/* ---- lifecycle ---------------------------------------------------------- */
int sober_frame_dispatch_install(const struct sober_frame_dispatch_config *cfg)
{
    uintptr_t vt = 0u;

    s_last_rc = SOBER_FD_DISABLED;
    if (fd_env_disabled()) return SOBER_FD_DISABLED;
    if (s_installed) { s_last_rc = SOBER_FD_OK; return SOBER_FD_OK; }
    if (!cfg || !cfg->guest_base || !cfg->render_ctx ||
        (uintptr_t)cfg->guest_base >
        UINTPTR_MAX - (uintptr_t)SOBER_FD_TEXT_SIZE) {
        s_last_rc = SOBER_FD_ERR_ARG;
        return s_last_rc;
    }

    s_shared = (struct fd_shared *)mmap(NULL, sizeof(struct fd_shared),
                                        PROT_READ | PROT_WRITE,
                                        MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (s_shared == MAP_FAILED) {
        s_shared = NULL;
        s_last_rc = SOBER_FD_ERR_DEVICE;
        return s_last_rc;
    }
    memset(s_shared, 0, sizeof(*s_shared));
    s_forwarded_guest = 0u;
    s_forwarded_host = 0u;

    s_base = (uintptr_t)cfg->guest_base;
    s_ctx = (uintptr_t)cfg->render_ctx;
    s_flags = cfg->flags;
    s_saved_target = 0u;

    /* Verify the GENUINE vtable; never substitute or fabricate one. */
    vt = cfg->vtable ? (uintptr_t)cfg->vtable :
         *(volatile uintptr_t *)(s_ctx + (uintptr_t)SOBER_FD_CTX_OFF_VTABLE);
    if (vt != s_base + (uintptr_t)SOBER_FD_RVA_VTABLE) {
        s_last_rc = SOBER_FD_ERR_VTABLE;
        goto fail;
    }
    s_vt = vt;
    s_slot = *(volatile uintptr_t *)(vt + (uintptr_t)SOBER_FD_VTABLE_SWAP_OFF);
    if (s_slot != s_base + (uintptr_t)SOBER_FD_RVA_SWAP_WRAPPER) {
        s_last_rc = SOBER_FD_ERR_VTABLE;
        goto fail;
    }
    s_ctx_verified = 1;

    if (fd_build_device() != 0) {
        s_last_rc = SOBER_FD_ERR_DEVICE;
        goto fail;
    }

    s_got_slot = s_base + (uintptr_t)SOBER_FD_GOT_SWAP_BUFFERS;
    if ((s_flags & SOBER_FD_F_NO_OBSERVER) == 0u) {
        if (fd_build_observer() != 0) {
            s_last_rc = SOBER_FD_ERR_TRAMP;
            goto fail;
        }
        if (fd_got_write(s_got_slot, (uintptr_t)s_tramp,
                         &s_saved_target) != 0) {
            s_last_rc = SOBER_FD_ERR_GOT;
            goto fail;
        }
        s_observer = 1;
    }

    s_installed = 1;
    s_run_done = 0;
    s_withhold = 1;                     /* seeded device object: withhold      */
    s_last_rc = SOBER_FD_OK;
    return SOBER_FD_OK;

fail:
    if (s_tramp) { munmap(s_tramp, FD_STUB_SIZE); s_tramp = NULL; }
    free(s_dev); s_dev = NULL;
    if (s_shared) { munmap(s_shared, sizeof(*s_shared)); s_shared = NULL; }
    s_observer = 0;
    return s_last_rc;
}

int sober_frame_dispatch_set_context(void *render_ctx, void *vtable)
{
    uintptr_t vt;
    if (fd_env_disabled()) { s_last_rc = SOBER_FD_DISABLED; return s_last_rc; }
    if (!s_installed || !render_ctx) {
        s_last_rc = SOBER_FD_ERR_STATE;
        return s_last_rc;
    }
    vt = vtable ? (uintptr_t)vtable :
         *(volatile uintptr_t *)((uintptr_t)render_ctx +
                                 (uintptr_t)SOBER_FD_CTX_OFF_VTABLE);
    if (vt != s_base + (uintptr_t)SOBER_FD_RVA_VTABLE ||
        *(volatile uintptr_t *)(vt + (uintptr_t)SOBER_FD_VTABLE_SWAP_OFF) !=
            s_base + (uintptr_t)SOBER_FD_RVA_SWAP_WRAPPER) {
        s_last_rc = SOBER_FD_ERR_VTABLE;
        return s_last_rc;
    }
    s_ctx = (uintptr_t)render_ctx;
    s_vt = vt;
    s_slot = *(volatile uintptr_t *)(vt + (uintptr_t)SOBER_FD_VTABLE_SWAP_OFF);
    s_ctx_verified = 1;
    free(s_dev);
    s_dev = NULL;
    if (fd_build_device() != 0) {
        s_last_rc = SOBER_FD_ERR_DEVICE;
        return s_last_rc;
    }
    s_last_rc = SOBER_FD_OK;
    return SOBER_FD_OK;
}

int sober_frame_dispatch_drive(unsigned count)
{
    unsigned i, n = count ? count : SOBER_FD_DEFAULT_DRIVES;
    uintptr_t ra = 0u;

    if (fd_env_disabled()) { s_last_rc = SOBER_FD_DISABLED; return s_last_rc; }
    if (!s_installed || !s_shared) {
        s_last_rc = SOBER_FD_ERR_STATE;
        return s_last_rc;
    }
    for (i = 0u; i < n; ++i) {
        uintptr_t seen = fd_drive_once();
        if (seen != 0u) {
            unsigned olen = 0u, oreg = 0u;
            ra = seen;
            if (fd_is_expected_ra(seen) && seen > s_base + 3u &&
                fd_decode_call18((const unsigned char *)
                    (seen - 3u), &olen, &oreg) && olen == 3u)
                fd_bump(&s_shared->verified);
            /* Stamp the site on the shared evidence record. */
            if (s_shared->evidence.guest && s_shared->evidence.ra == seen)
                s_shared->site_rva = (uintptr_t)SOBER_FD_RVA_SWAP_CALL;
        }
    }
    fd_forward_evidence();              /* real RA events -> sole verdict owner */
    s_withhold = 1;
    s_last_rc = (atomic_load_explicit(&s_shared->verified,
                                     memory_order_relaxed) > 0u)
                    ? SOBER_FD_OK : SOBER_FD_ERR_NO_RA;
    (void)ra;
    return s_last_rc;
}

int sober_frame_dispatch_run(unsigned count)
{
    if (fd_env_disabled()) { s_last_rc = SOBER_FD_DISABLED; return s_last_rc; }
    if (!s_installed) { s_last_rc = SOBER_FD_ERR_STATE; return s_last_rc; }
    if (s_run_done) { s_last_rc = SOBER_FD_OK; return SOBER_FD_OK; }
    if (sober_frame_dispatch_drive(count) != SOBER_FD_OK) return s_last_rc;
    s_run_done = 1;                     /* idempotent: no further auto-drives */
    return SOBER_FD_OK;
}

void sober_frame_dispatch_uninstall(void)
{
    if (!s_installed) return;
    if (s_observer && s_tramp) {
        if (*(volatile uintptr_t *)s_got_slot == (uintptr_t)s_tramp)
            (void)fd_got_write(s_got_slot, s_saved_target, NULL);
        munmap(s_tramp, FD_STUB_SIZE);
        s_tramp = NULL;
    }
    s_observer = 0;
    free(s_dev);
    s_dev = NULL;
    if (s_shared) {
        munmap(s_shared, sizeof(*s_shared));
        s_shared = NULL;
    }
    s_installed = 0;
    s_ctx_verified = 0;
    s_run_done = 0;
    /* s_withhold stays set: a seeded run remains withhold-worthy. */
    s_last_rc = fd_env_disabled() ? SOBER_FD_DISABLED : SOBER_FD_OK;
}

int sober_frame_dispatch_is_installed(void)
{
    return s_installed && !fd_env_disabled();
}

void sober_frame_dispatch_reset(void)
{
    if (s_shared) memset(s_shared, 0, sizeof(*s_shared));
    s_forwarded_guest = 0u;
    s_forwarded_host = 0u;
    s_run_done = 0;
    s_withhold = 0;
    s_last_rc = fd_env_disabled() ? SOBER_FD_DISABLED : SOBER_FD_OK;
}

/* ---- evidence / counters ------------------------------------------------ */
int sober_frame_dispatch_ra_evidence(SoberFrameDispatchRaEvidence *out)
{
    if (!out) return -1;
    if (fd_env_disabled() || !s_shared) {
        memset(out, 0, sizeof(*out));
        return -1;
    }
    *out = s_shared->evidence;
    return s_shared->evidence.ra != 0u ? 0 : -1;
}

uint64_t sober_frame_dispatch_swaps_total(void)
{
    return (fd_env_disabled() || !s_shared) ? 0u :
           atomic_load_explicit(&s_shared->swaps_total, memory_order_relaxed);
}
uint64_t sober_frame_dispatch_swaps_guest_tagged(void)
{
    return (fd_env_disabled() || !s_shared) ? 0u :
           atomic_load_explicit(&s_shared->swaps_guest, memory_order_relaxed);
}
uint64_t sober_frame_dispatch_swaps_host(void)
{
    return (fd_env_disabled() || !s_shared) ? 0u :
           atomic_load_explicit(&s_shared->swaps_host, memory_order_relaxed);
}
uint64_t sober_frame_dispatch_drives(void)
{
    return (fd_env_disabled() || !s_shared) ? 0u :
           atomic_load_explicit(&s_shared->drives, memory_order_relaxed);
}
uint64_t sober_frame_dispatch_verified_drives(void)
{
    return (fd_env_disabled() || !s_shared) ? 0u :
           atomic_load_explicit(&s_shared->verified, memory_order_relaxed);
}
uintptr_t sober_frame_dispatch_last_ra(void)
{
    return (fd_env_disabled() || !s_shared) ? 0u :
           atomic_load_explicit(&s_shared->last_ra, memory_order_relaxed);
}
uintptr_t sober_frame_dispatch_last_ra_rva(void)
{
    uintptr_t ra = sober_frame_dispatch_last_ra();
    return (!fd_env_disabled() && fd_in_guest(ra)) ? ra - s_base : 0u;
}
uintptr_t sober_frame_dispatch_expected_ra(void)
{
    return s_base ? s_base + (uintptr_t)SOBER_FD_RVA_EXPECTED_RA : 0u;
}
int sober_frame_dispatch_expected_ra_matched(void)
{
    return !fd_env_disabled() && s_shared &&
           fd_is_expected_ra(atomic_load_explicit(&s_shared->last_ra,
                                                  memory_order_relaxed));
}
uintptr_t sober_frame_dispatch_guest_base(void) { return s_base; }
uintptr_t sober_frame_dispatch_vtable_slot(void) { return s_slot; }
void *sober_frame_dispatch_device_object(void) { return (void *)s_dev; }
int sober_frame_dispatch_last_rc(void) { return s_last_rc; }

/* ---- verify / withhold -------------------------------------------------- */
int sober_frame_dispatch_verify(void)
{
    uint64_t guest, host;
    if (fd_env_disabled()) return SOBER_FD_VERDICT_DISABLED;
    if (!s_installed || !s_shared) return SOBER_FD_VERDICT_ABSENT;
    guest = atomic_load_explicit(&s_shared->swaps_guest, memory_order_relaxed);
    host = atomic_load_explicit(&s_shared->swaps_host, memory_order_relaxed);
    if (guest == 0u)
        return host > 0u ? SOBER_FD_VERDICT_HOST_ONLY : SOBER_FD_VERDICT_ABSENT;
    if (host > 0u) return SOBER_FD_VERDICT_MIXED;      /* fail closed (R9 §4) */
    if (!s_shared->evidence.expected_ra ||
        !s_shared->evidence.indirect_call ||
        !s_shared->evidence.vtable_slot_ok)
        return SOBER_FD_VERDICT_GUEST_RA;
    return s_shared->stable_ra ? SOBER_FD_VERDICT_VERIFIED
                               : SOBER_FD_VERDICT_GUEST_RA;
}

int sober_frame_dispatch_guest_dispatch_ok(void)
{
    return !fd_env_disabled() && s_shared &&
           s_shared->evidence.expected_ra &&
           s_shared->evidence.indirect_call &&
           s_shared->evidence.vtable_slot_ok;
}

int sober_frame_dispatch_should_withhold(void)
{
    return fd_env_disabled() ? 0 : s_withhold;
}

const char *sober_frame_dispatch_verdict_name(int v)
{
    switch (v) {
    case SOBER_FD_VERDICT_DISABLED:  return "disabled";
    case SOBER_FD_VERDICT_ABSENT:    return "absent";
    case SOBER_FD_VERDICT_HOST_ONLY: return "host-only";
    case SOBER_FD_VERDICT_MIXED:     return "mixed-withheld";
    case SOBER_FD_VERDICT_GUEST_RA:  return "guest-ra";
    case SOBER_FD_VERDICT_VERIFIED:  return "verified-guest-dispatch";
    default:                         return "unknown";
    }
}

void sober_frame_dispatch_snapshot(SoberFrameDispatchSnapshot *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->enabled = sober_frame_dispatch_enabled();
    out->installed = s_installed && out->enabled;
    out->ctx_verified = s_ctx_verified;
    out->observer_installed = s_observer;
    out->driven = (sober_frame_dispatch_drives() > 0u);
    out->guest_base = s_base;
    out->render_ctx = s_ctx;
    out->vtable = s_vt;
    out->vtable_slot = s_slot;
    out->device_object = (uintptr_t)s_dev;
    out->swaps_total = sober_frame_dispatch_swaps_total();
    out->swaps_guest = sober_frame_dispatch_swaps_guest_tagged();
    out->swaps_host = sober_frame_dispatch_swaps_host();
    out->drives = sober_frame_dispatch_drives();
    out->verified = sober_frame_dispatch_verified_drives();
    out->last_ra = sober_frame_dispatch_last_ra();
    out->last_ra_rva = sober_frame_dispatch_last_ra_rva();
    out->last_ra_guest = fd_in_guest(out->last_ra);
    out->expected_ra_matched = sober_frame_dispatch_expected_ra_matched();
    out->stable_ra = s_shared ? s_shared->stable_ra : 0;
    out->verdict = sober_frame_dispatch_verify();
}

/* ---- diagnostics / self-test ------------------------------------------- */
const char *sober_frame_dispatch_describe(char *buf, size_t cap)
{
    if (!buf || cap == 0u) return "";
    snprintf(buf, cap,
             "frame_dispatch enabled=%d installed=%d obs=%d ctx=%d drv=%llu "
             "swaps(total/guest/host)=%llu/%llu/%llu verified=%llu "
             "exp_ra=base+0x%llx last_ra=base+0x%llx site=0x%llx verdict=%s "
             "withhold=%d",
             sober_frame_dispatch_enabled(), s_installed, s_observer,
             s_ctx_verified,
             (unsigned long long)sober_frame_dispatch_drives(),
             (unsigned long long)sober_frame_dispatch_swaps_total(),
             (unsigned long long)sober_frame_dispatch_swaps_guest_tagged(),
             (unsigned long long)sober_frame_dispatch_swaps_host(),
             (unsigned long long)sober_frame_dispatch_verified_drives(),
             (unsigned long long)(uintptr_t)SOBER_FD_RVA_EXPECTED_RA,
             (unsigned long long)sober_frame_dispatch_last_ra_rva(),
             (unsigned long long)(uintptr_t)SOBER_FD_RVA_SWAP_CALL,
             sober_frame_dispatch_verdict_name(sober_frame_dispatch_verify()),
             sober_frame_dispatch_should_withhold());
    return buf;
}

const char *sober_frame_dispatch_env_dump(void)
{
    return SOBER_FRAME_DISPATCH_ENV " (default ON; =0 disables; withhold seed)";
}

int sober_frame_dispatch_selftest(char *buf, size_t cap)
{
    static const unsigned char ok1[3] = { 0xff, 0x50, 0x18 };
    static const unsigned char ok2[4] = { 0x41, 0xff, 0x50, 0x18 };
    static const unsigned char ok3[6] = { 0xff, 0x90, 0x18, 0, 0, 0 };
    static const unsigned char bad[5] = { 0xe8, 0, 0, 0, 0 };
    unsigned len = 0u, reg = 0u;
    int rc = 0;

    if (!fd_decode_call18(ok1, &len, &reg) || len != 3u || reg != 0u) rc = -1;
    if (!fd_decode_call18(ok2, &len, &reg) || len != 4u || reg != 8u) rc = -1;
    if (!fd_decode_call18(ok3, &len, &reg) || len != 6u || reg != 0u) rc = -1;
    if (fd_decode_call18(bad, &len, &reg)) rc = -1;
    if (SOBER_FD_RVA_EXPECTED_RA - SOBER_FD_RVA_SWAP_CALL != 3u) rc = -1;
    if (strcmp(sober_frame_dispatch_verdict_name(SOBER_FD_VERDICT_VERIFIED),
               "verified-guest-dispatch") != 0) rc = -1;

    if (buf && cap) {
        snprintf(buf, cap, "frame_dispatch selftest %s (decoder/R9 RA)",
                 rc == 0 ? "PASS" : "FAIL");
    }
    return rc;
}
