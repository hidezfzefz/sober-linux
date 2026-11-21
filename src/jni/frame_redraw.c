/*
 * frame_redraw.c - DEFAULT-ON guest redraw driver (group C / agent F2).
 *                  See frame_redraw.h for the full evidence chain, frozen
 *                  RVAs and the exact wiring signature.
 *
 * HARD RULES OBSERVED HERE
 * ------------------------
 *  - NEW FILES ONLY.  No CMake/engine/gate/marker edits.
 *  - Default ON; kill switch SOBER_FRAME_REDRAW=0 -> inert no-op.
 *  - No guest code byte is patched.  Only GOT JUMP_SLOT pointer swaps and
 *    host-owned nativeData fields are written.
 *  - HOST NEVER CALLS 0x649d422.  It appears only in vtable-slot comparison.
 *    The swap is reached only by genuine guest code (thunk 0x2a20a00 ->
 *    [+0x98] callback -> guest frame path -> [vtable+0x18] -> 0x649d422).
 *  - Route A (preferred): the caller passes the guest-built nativeData from
 *    initializeNativeCode 0x2a1fe30; its callbacks are already genuine.  Route
 *    B seeds NULL slots with the genuine AGDK callbacks (0x2a23a10 created,
 *    0x2a23cc0 resized, 0x2a23c40 REDRAW, 0x2a23b60 destroyed) installed by
 *    the guest's own table installer at 0x2a23001 -- never host pointers
 *    (F10 FP-7).  nativeData+0x00 -> callbacks table at nativeData+0x50, so
 *    thunk slot +0x88/+0x90/+0x98/+0xa0 = callbacks+0x38/0x40/0x48/0x50 (P5).
 *    +0x140 is the ANativeWindow data field, not a callback.  A real frame
 *    additionally needs the guest render thread running (P6 S5).
 */
#define _GNU_SOURCE
#include "frame_redraw.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

/* ---- frozen guest prototypes -------------------------------------------- */
/* JNI onSurfaceRedrawNeededNative: (JNIEnv*, jclass, jlong nd, jobject surf) */
typedef void (*fr_redraw_fn)(void *, void *, uintptr_t, void *);
typedef void (*fr_life_created_fn)(void *, void *, uintptr_t, void *);
typedef void (*fr_life_changed_fn)(void *, void *, uintptr_t, void *,
                                   int, int, int);
typedef void (*fr_life_destroy_fn)(void *, void *, uintptr_t);
typedef int  (*fr_swap_fn)(void *, void *);
typedef void (*fr_clear_fn)(unsigned int);
typedef void (*fr_draw_elements_fn)(unsigned int, int, unsigned int,
                                    const void *);
typedef void (*fr_draw_arrays_fn)(unsigned int, int, int);

/* ---- module state -------------------------------------------------------- */
static uintptr_t   s_base;
static int         s_installed;
static int         s_driven;
static int         s_last_rc = SOBER_FR_DISABLED;
static unsigned    s_flags;
static unsigned char *s_owned_nd;              /* when cfg.native_data==NULL */
static unsigned char *s_nd;                    /* effective nativeData        */
static void       *s_window;
static void       *s_env;
static void       *s_token;
static void       *s_surface_token[2];         /* opaque token storage        */
static char        s_dummy_env[16];

/* Saved GOT pointers: [0]=swap, [1]=glClear, [2]=glDrawElements,
 * [3]=glDrawArrays.  Each may chain to an already-installed hook. */
static uintptr_t   s_got_old[4];
static uintptr_t   s_got_slot[4];
static int         s_observe;

static _Atomic uint64_t s_drives;
static _Atomic uint64_t s_swaps_total;
static _Atomic uint64_t s_swaps_guest;
static _Atomic uint64_t s_swaps_synthetic;
static _Atomic uint64_t s_dispatch_draws;
static _Atomic uint64_t s_guest_draws;

static void fr_bump(_Atomic uint64_t *c)
{
    atomic_fetch_add_explicit(c, 1u, memory_order_relaxed);
}

/* ---- address classification --------------------------------------------- */
static int fr_in_guest(uintptr_t addr)
{
    return s_base != 0u && addr >= s_base &&
           addr < s_base + (uintptr_t)SOBER_FR_TEXT_SIZE;
}

static int fr_in_dispatch(uintptr_t ra)
{
    return fr_in_guest(ra) &&
           ra >= s_base + (uintptr_t)SOBER_FR_RVA_GLES_DISPATCH &&
           ra <  s_base + (uintptr_t)SOBER_FR_RVA_GLES_DISPATCH_END;
}

/* ---- GOT helpers --------------------------------------------------------- */
static int fr_page_prot(uintptr_t page)
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

static int fr_got_write(uintptr_t slot, uintptr_t val, uintptr_t *old_out)
{
    long ps = sysconf(_SC_PAGESIZE);
    uintptr_t page;
    int prot;
    uintptr_t old;

    if (ps <= 0) ps = 4096;
    page = slot & ~((uintptr_t)ps - 1u);
    prot = fr_page_prot(page);
    if (mprotect((void *)page, (size_t)ps, PROT_READ | PROT_WRITE) != 0)
        return -1;
    old = *(volatile uintptr_t *)slot;
    *(volatile uintptr_t *)slot = val;
    if (prot >= 0 && prot != (PROT_READ | PROT_WRITE))
        (void)mprotect((void *)page, (size_t)ps, prot);
    if (old_out) *old_out = old;
    return 0;
}

/* ---- chained observation hooks ------------------------------------------ */
/* Every hook is an exact-signature forwarding function, so all argument and
 * return registers arrive at the previous (possibly genuine) GOT target
 * unmodified.  __builtin_return_address(0) is the *guest* call site because
 * the guest calls these through its PLT/GOT. */
static void fr_note_draw(uintptr_t ra)
{
    if (fr_in_dispatch(ra)) fr_bump(&s_dispatch_draws);
    if (fr_in_guest(ra))    fr_bump(&s_guest_draws);
}

static void fr_hook_clear(unsigned int mask)
{
    uintptr_t ra = (uintptr_t)__builtin_return_address(0);
    fr_clear_fn orig;
    fr_note_draw(ra);
    orig = (fr_clear_fn)(uintptr_t)s_got_old[1];
    if (orig) orig(mask);
}

static void fr_hook_draw_elements(unsigned int mode, int count,
                                  unsigned int type, const void *indices)
{
    uintptr_t ra = (uintptr_t)__builtin_return_address(0);
    fr_draw_elements_fn orig;
    fr_note_draw(ra);
    orig = (fr_draw_elements_fn)(uintptr_t)s_got_old[2];
    if (orig) orig(mode, count, type, indices);
}

static void fr_hook_draw_arrays(unsigned int mode, int first, int count)
{
    uintptr_t ra = (uintptr_t)__builtin_return_address(0);
    fr_draw_arrays_fn orig;
    fr_note_draw(ra);
    orig = (fr_draw_arrays_fn)(uintptr_t)s_got_old[3];
    if (orig) orig(mode, first, count);
}

static int fr_hook_swap(void *dpy, void *surf)
{
    uintptr_t ra = (uintptr_t)__builtin_return_address(0);
    fr_swap_fn orig;
    fr_bump(&s_swaps_total);
    if (fr_in_guest(ra)) fr_bump(&s_swaps_guest);
    else                 fr_bump(&s_swaps_synthetic);
    orig = (fr_swap_fn)(uintptr_t)s_got_old[0];
    return orig ? orig(dpy, surf) : 0;
}

static int fr_install_observe(void)
{
    uintptr_t slot;

    slot = s_base + (uintptr_t)SOBER_FR_GOT_SWAP_BUFFERS;
    if (fr_got_write(slot, (uintptr_t)&fr_hook_swap, &s_got_old[0]) != 0)
        goto fail;
    s_got_slot[0] = slot;

    slot = s_base + (uintptr_t)SOBER_FR_GOT_GL_CLEAR;
    if (fr_got_write(slot, (uintptr_t)&fr_hook_clear, &s_got_old[1]) != 0)
        goto fail_swap;
    s_got_slot[1] = slot;

    slot = s_base + (uintptr_t)SOBER_FR_GOT_GL_DRAW_ELEMENTS;
    if (fr_got_write(slot, (uintptr_t)&fr_hook_draw_elements,
                     &s_got_old[2]) != 0)
        goto fail_clear;
    s_got_slot[2] = slot;

    slot = s_base + (uintptr_t)SOBER_FR_GOT_GL_DRAW_ARRAYS;
    if (fr_got_write(slot, (uintptr_t)&fr_hook_draw_arrays,
                     &s_got_old[3]) != 0)
        goto fail_elements;
    s_got_slot[3] = slot;

    s_observe = 1;
    return 0;

fail_elements:
    (void)fr_got_write(s_got_slot[2], s_got_old[2], NULL);
fail_clear:
    (void)fr_got_write(s_got_slot[1], s_got_old[1], NULL);
fail_swap:
    (void)fr_got_write(s_got_slot[0], s_got_old[0], NULL);
fail:
    return -1;
}

static void fr_remove_observe(void)
{
    unsigned i;
    if (!s_observe) return;
    for (i = 4u; i-- > 0u;)
        (void)fr_got_write(s_got_slot[i], s_got_old[i], NULL);
    s_observe = 0;
}

/* ---- nativeData preparation --------------------------------------------- */
static void fr_seed_callbacks(unsigned char *nd)
{
    /* Only fill slots that are NULL.  A caller that already installed valid
     * callbacks (via its own JNI path) is never overwritten. */
    if (*(void **)(nd + (size_t)SOBER_FR_ND_OFF_CREATED) == NULL)
        *(uintptr_t *)(nd + (size_t)SOBER_FR_ND_OFF_CREATED) =
            s_base + (uintptr_t)SOBER_FR_CB_CREATED;
    if (*(void **)(nd + (size_t)SOBER_FR_ND_OFF_CHANGED) == NULL)
        *(uintptr_t *)(nd + (size_t)SOBER_FR_ND_OFF_CHANGED) =
            s_base + (uintptr_t)SOBER_FR_CB_CHANGED;
    if (*(void **)(nd + (size_t)SOBER_FR_ND_OFF_REDRAW) == NULL)
        *(uintptr_t *)(nd + (size_t)SOBER_FR_ND_OFF_REDRAW) =
            s_base + (uintptr_t)SOBER_FR_CB_REDRAW;
    if (*(void **)(nd + (size_t)SOBER_FR_ND_OFF_DESTROYED) == NULL)
        *(uintptr_t *)(nd + (size_t)SOBER_FR_ND_OFF_DESTROYED) =
            s_base + (uintptr_t)SOBER_FR_CB_DESTROYED;
}

static uintptr_t fr_swap_slot_value(void)
{
    uintptr_t vt;
    if (!s_base) return 0u;
    vt = *(volatile uintptr_t *)(s_base + (uintptr_t)SOBER_FR_RVA_VTABLE);
    if (!vt) return 0u;
    return *(volatile uintptr_t *)(vt + (uintptr_t)SOBER_FR_VTABLE_SWAP_OFF);
}

/* ---- gate --------------------------------------------------------------- */
int sober_frame_redraw_enabled(void)
{
    const char *e = getenv(SOBER_FRAME_REDRAW_ENV);
    /* DEFAULT ON: only the exact string "0" kills it. */
    return !(e != NULL && strcmp(e, "0") == 0);
}

const char *sober_frame_redraw_env_vars(void)
{
    return SOBER_FRAME_REDRAW_ENV
           "=0 (kill switch; default ON - drives guest redraw 0x2a20a00 via "
           "guest callback 0x2a23c40, observes GLES dispatcher 0x6496d0c and "
           "swap 0x649d422 through GOT 0x722a1a8/0x7229fc8/0x722a040/0x722a048)";
}

/* ---- install ------------------------------------------------------------ */
int sober_frame_redraw_install(const struct sober_frame_redraw_config *cfg)
{
    uintptr_t vt;

    if (!sober_frame_redraw_enabled()) {
        s_last_rc = SOBER_FR_DISABLED;
        return SOBER_FR_DISABLED;
    }
    if (!cfg || !cfg->guest_base) {
        s_last_rc = SOBER_FR_ERR_ARG;
        return SOBER_FR_ERR_ARG;
    }
    if (s_installed) {
        s_last_rc = SOBER_FR_OK;
        return SOBER_FR_OK;
    }

    s_base = (uintptr_t)cfg->guest_base;
    s_flags = cfg->flags;
    s_env = cfg->jni_env ? cfg->jni_env : (void *)s_dummy_env;
    s_token = cfg->surface_token;
    s_surface_token[0] = cfg->native_window;
    if (!s_token) s_token = (void *)&s_surface_token;

    /* Take the caller's nativeData (>=0x200) or build one. */
    if (cfg->native_data) {
        s_nd = (unsigned char *)cfg->native_data;
        s_owned_nd = NULL;
    } else {
        s_owned_nd = (unsigned char *)calloc(1u, (size_t)SOBER_FR_ND_SIZE);
        if (!s_owned_nd) {
            s_last_rc = SOBER_FR_ERR_ARG;
            return SOBER_FR_ERR_ARG;
        }
        s_nd = s_owned_nd;
    }

    /* JNIEnv at +0x10, ANativeWindow at +0x140 -- exactly the fields the
     * guest thunks 0x2a20850/0x2a20a00 read. */
    *(void **)(s_nd + (size_t)SOBER_FR_ND_OFF_ENV) = s_env;
    s_window = cfg->native_window;
    if (s_window) {
        *(void **)(s_nd + (size_t)SOBER_FR_ND_OFF_WINDOW) = s_window;
    } else if (cfg->native_data) {
        /* Caller supplied its own nativeData: honour the window it already
         * published at +0x140 instead of reporting a phantom NULL window. */
        s_window = *(void **)(s_nd + (size_t)SOBER_FR_ND_OFF_WINDOW);
    }

    /* Make surface callbacks valid with the guest's own callback RVAs. */
    if (!(s_flags & SOBER_FR_FLAG_NO_SEED_CB))
        fr_seed_callbacks(s_nd);

    /* Publish the window where the guest's own 0x26fea5a helper stores it. */
    if (!(s_flags & SOBER_FR_FLAG_NO_GLOBAL) && s_window) {
        *(volatile uintptr_t *)(s_base
            + (uintptr_t)SOBER_FR_G_WINDOW) = (uintptr_t)s_window;
        __atomic_fetch_add((uintptr_t *)(s_base
            + (uintptr_t)SOBER_FR_G_WINDOW_GEN), 1u, __ATOMIC_SEQ_CST);
    }

    /* The render vtable must really point at 0x649d422 before we claim the
     * guest can reach the swap; otherwise fail closed. */
    vt = *(volatile uintptr_t *)(s_base + (uintptr_t)SOBER_FR_RVA_VTABLE);
    if (!vt ||
        vt != s_base + (uintptr_t)SOBER_FR_RVA_VTABLE ||
        fr_swap_slot_value() !=
            s_base + (uintptr_t)SOBER_FR_RVA_SWAP_WRAPPER) {
        s_last_rc = SOBER_FR_ERR_PRECOND;
        /* Only host-owned nativeData was touched; the two guest writes
         * (window global + generation) are intentionally left in place, as
         * render_guest does, because guest code may already reference them. */
        free(s_owned_nd);
        s_owned_nd = NULL;
        s_nd = NULL;
        s_base = 0u;
        return SOBER_FR_ERR_PRECOND;
    }

    if (!(s_flags & SOBER_FR_FLAG_NO_OBSERVE)) {
        if (fr_install_observe() != 0) {
            free(s_owned_nd);
            s_owned_nd = NULL;
            s_nd = NULL;
            s_base = 0u;
            s_last_rc = SOBER_FR_ERR_GOT;
            return SOBER_FR_ERR_GOT;
        }
    }

    s_installed = 1;
    s_driven = 0;
    s_last_rc = SOBER_FR_OK;
    return SOBER_FR_OK;
}

/* ---- drive -------------------------------------------------------------- */
int sober_frame_redraw_drive_flags(unsigned flags)
{
    uintptr_t nd;

    if (!sober_frame_redraw_enabled()) {
        s_last_rc = SOBER_FR_DISABLED;
        return SOBER_FR_DISABLED;
    }
    if (!s_installed || !s_nd || !s_base) {
        s_last_rc = SOBER_FR_ERR_STATE;
        return SOBER_FR_ERR_STATE;
    }
    if (s_driven && !(flags & SOBER_FR_FLAG_FORCE)) {
        s_last_rc = SOBER_FR_OK;
        return SOBER_FR_OK;
    }

    /* Preconditions: window bound at +0x140 and redraw callback valid in
     * guest .text.  These are the "surface callbacks are valid" checkpoint. */
    if (*(void **)(s_nd + (size_t)SOBER_FR_ND_OFF_WINDOW) == NULL ||
        !fr_in_guest(*(uintptr_t *)(s_nd
            + (size_t)SOBER_FR_ND_OFF_REDRAW)) ||
        fr_swap_slot_value() !=
            s_base + (uintptr_t)SOBER_FR_RVA_SWAP_WRAPPER) {
        s_last_rc = SOBER_FR_ERR_PRECOND;
        return SOBER_FR_ERR_PRECOND;
    }

    nd = (uintptr_t)s_nd;

    /* Optional: replay the created/changed lifecycle thunks first so the
     * guest binds its window through ANativeWindow_fromSurface (GOT hook).
     * The destroyed thunk 0x2a20a30 is deliberately NOT called here: it zeroes
     * nativeData+0x140 at 0x2a20a6a and would defeat the redraw guard B
     * (P6 S5 step 8).  It belongs at teardown, not in the frame drive. */
    if (flags & SOBER_FR_FLAG_LIFECYCLE) {
        ((fr_life_created_fn)(uintptr_t)(s_base
            + (uintptr_t)SOBER_FR_RVA_SURFACE_CREATED))(
                s_env, NULL, nd, s_token);
        ((fr_life_changed_fn)(uintptr_t)(s_base
            + (uintptr_t)SOBER_FR_RVA_SURFACE_CHANGED))(
                s_env, NULL, nd, s_token, 0, 0, 0);
    }

    /* THE PRIMARY ACTION: call the genuine guest redraw thunk.  From here on
     * real guest code runs: 0x2a20a05 loads the window, 0x2a20a11 loads the
     * [+0x98] guest callback, 0x2a20a20 tail-jmps into it.  The host never
     * calls 0x649d422 or 0x6496d0c. */
    ((fr_redraw_fn)(uintptr_t)(s_base
        + (uintptr_t)SOBER_FR_RVA_REDRAW_THUNK))(
            s_env, NULL, nd, s_token);
    fr_bump(&s_drives);

    s_driven = 1;
    s_last_rc = SOBER_FR_OK;
    return SOBER_FR_OK;
}

int sober_frame_redraw_drive(void)
{
    return sober_frame_redraw_drive_flags(0u);
}

int sober_frame_redraw_run(const struct sober_frame_redraw_config *cfg)
{
    int rc = sober_frame_redraw_install(cfg);
    if (rc != SOBER_FR_OK) return rc;
    return sober_frame_redraw_drive_flags(cfg ? cfg->flags : 0u);
}

/* ---- uninstall / reset -------------------------------------------------- */
void sober_frame_redraw_uninstall(void)
{
    if (!s_installed) return;
    fr_remove_observe();
    s_installed = 0;
    /* s_nd / s_owned_nd are retained: the guest may still hold the pointer.
     * Counters and accessors remain valid for post-run diagnostics. */
}

void sober_frame_redraw_reset(void)
{
    atomic_store_explicit(&s_drives, 0u, memory_order_relaxed);
    atomic_store_explicit(&s_swaps_total, 0u, memory_order_relaxed);
    atomic_store_explicit(&s_swaps_guest, 0u, memory_order_relaxed);
    atomic_store_explicit(&s_swaps_synthetic, 0u, memory_order_relaxed);
    atomic_store_explicit(&s_dispatch_draws, 0u, memory_order_relaxed);
    atomic_store_explicit(&s_guest_draws, 0u, memory_order_relaxed);
    s_driven = 0;
}

/* ---- accessors ---------------------------------------------------------- */
int sober_frame_redraw_is_installed(void) { return s_installed; }
int sober_frame_redraw_is_driven(void)     { return s_driven; }
int sober_frame_redraw_last_rc(void)       { return s_last_rc; }
uintptr_t sober_frame_redraw_base(void)    { return s_base; }
void *sober_frame_redraw_native_data(void) { return (void *)s_nd; }
void *sober_frame_redraw_window(void)      { return s_window; }
uintptr_t sober_frame_redraw_redraw_cb(void)
{
    return s_nd ? *(uintptr_t *)(s_nd + (size_t)SOBER_FR_ND_OFF_REDRAW) : 0u;
}

uint64_t sober_frame_redraw_drives(void)
{
    return atomic_load_explicit(&s_drives, memory_order_relaxed);
}
uint64_t sober_frame_redraw_swaps_total(void)
{
    return atomic_load_explicit(&s_swaps_total, memory_order_relaxed);
}
uint64_t sober_frame_redraw_swaps_guest_tagged(void)
{
    return atomic_load_explicit(&s_swaps_guest, memory_order_relaxed);
}
uint64_t sober_frame_redraw_swaps_synthetic(void)
{
    return atomic_load_explicit(&s_swaps_synthetic, memory_order_relaxed);
}
uint64_t sober_frame_redraw_gles_dispatch_draws(void)
{
    return atomic_load_explicit(&s_dispatch_draws, memory_order_relaxed);
}
uint64_t sober_frame_redraw_gles_guest_draws(void)
{
    return atomic_load_explicit(&s_guest_draws, memory_order_relaxed);
}

/* ---- verify API --------------------------------------------------------- */
static int fr_rva_invariants_ok(void)
{
    if (SOBER_FR_RVA_VTABLE + SOBER_FR_VTABLE_SWAP_OFF !=
        SOBER_FR_RVA_SWAP_SLOT) return 0;
    if (SOBER_FR_RVA_SWAP_SLOT != 0x7191300u) return 0;
    if (SOBER_FR_RVA_REDRAW_DISP_LOAD >= SOBER_FR_RVA_REDRAW_DISP_JMP)
        return 0;
    if (SOBER_FR_RVA_REDRAW_DISP_JMP != SOBER_FR_RVA_REDRAW_THUNK + 0x20u)
        return 0;
    if (SOBER_FR_RVA_GLES_DISPATCH_END <= SOBER_FR_RVA_GLES_DISPATCH)
        return 0;
    if (SOBER_FR_RVA_SWAP_WRAPPER != 0x649d422u) return 0;
    return 1;
}

int sober_frame_redraw_verify_shape(char *buf, size_t cap)
{
    uintptr_t cb;
    int ok = 1;

    if (!sober_frame_redraw_enabled()) ok = 0;
    if (!s_installed || !s_nd || !s_base) ok = 0;
    if (!fr_rva_invariants_ok()) ok = 0;
    cb = s_nd ? *(uintptr_t *)(s_nd + (size_t)SOBER_FR_ND_OFF_REDRAW) : 0u;
    if (!fr_in_guest(cb)) ok = 0;
    if (!s_nd ||
        *(void **)(s_nd + (size_t)SOBER_FR_ND_OFF_WINDOW) == NULL) ok = 0;
    if (fr_swap_slot_value() !=
        s_base + (uintptr_t)SOBER_FR_RVA_SWAP_WRAPPER) ok = 0;

    if (buf && cap) {
        snprintf(buf, cap,
                 "shape=%s enabled=%d installed=%d base=%#lx cb=%#lx "
                 "window=%p slot=%#lx(expect=%#lx)",
                 ok ? "PASS" : "FAIL", sober_frame_redraw_enabled(),
                 s_installed, (unsigned long)s_base, (unsigned long)cb,
                 s_window, (unsigned long)fr_swap_slot_value(),
                 (unsigned long)(s_base
                     + (uintptr_t)SOBER_FR_RVA_SWAP_WRAPPER));
    }
    return ok ? SOBER_FR_OK : SOBER_FR_ERR_PRECOND;
}

int sober_frame_redraw_verify(char *buf, size_t cap)
{
    char shape[256];
    int rc = sober_frame_redraw_verify_shape(shape, sizeof(shape));
    uint64_t gs = sober_frame_redraw_swaps_guest_tagged();
    uint64_t dd = sober_frame_redraw_gles_dispatch_draws();

    if (rc == SOBER_FR_OK && s_driven && gs >= 1u && dd >= 1u)
        rc = SOBER_FR_OK;
    else
        rc = SOBER_FR_ERR_VERIFY;

    if (buf && cap) {
        snprintf(buf, cap,
                 "verify=%s rc=%d driven=%d guest_swaps=%llu "
                 "dispatch_draws=%llu guest_draws=%llu | %s",
                 rc == SOBER_FR_OK ? "PASS" : "FAIL", rc, s_driven,
                 (unsigned long long)gs, (unsigned long long)dd,
                 (unsigned long long)sober_frame_redraw_gles_guest_draws(),
                 shape);
    }
    return rc;
}

/* ---- diagnostics -------------------------------------------------------- */
const char *sober_frame_redraw_describe(char *buf, size_t cap)
{
    if (!buf || cap == 0u) return "";
    snprintf(buf, cap,
             "enabled=%d installed=%d driven=%d rc=%d base=%#lx nd=%p "
             "cb=%#lx window=%p observe=%d "
             "drives=%llu swaps=%llu guest=%llu synth=%llu "
             "dispatch_draws=%llu guest_draws=%llu "
             "rva_redraw=%#x rva_dispatch=%#x rva_swap=%#x",
             sober_frame_redraw_enabled(), s_installed, s_driven, s_last_rc,
             (unsigned long)s_base, (void *)s_nd,
             (unsigned long)sober_frame_redraw_redraw_cb(), s_window,
             s_observe,
             (unsigned long long)sober_frame_redraw_drives(),
             (unsigned long long)sober_frame_redraw_swaps_total(),
             (unsigned long long)sober_frame_redraw_swaps_guest_tagged(),
             (unsigned long long)sober_frame_redraw_swaps_synthetic(),
             (unsigned long long)sober_frame_redraw_gles_dispatch_draws(),
             (unsigned long long)sober_frame_redraw_gles_guest_draws(),
             (unsigned)SOBER_FR_RVA_REDRAW_THUNK,
             (unsigned)SOBER_FR_RVA_GLES_DISPATCH,
             (unsigned)SOBER_FR_RVA_SWAP_WRAPPER);
    return buf;
}

const char *sober_frame_redraw_env_dump(void)
{
    return SOBER_FRAME_REDRAW_ENV
           "=0|1 (default ON; 0 kills every hook and thunk call).  The module "
           "seeds genuine AGDK callbacks 0x2a23a10/0x2a23cc0/0x2a23c40/"
           "0x2a23b60 and observes GLES dispatcher 0x6496d0c and swap "
           "0x649d422 without ever calling the swap wrapper from the host.";
}

/* ---- self-test (no guest required) -------------------------------------- */
int sober_frame_redraw_selftest(char *buf, size_t cap)
{
    int fails = 0;
    uintptr_t save_base = s_base;

    if (!fr_rva_invariants_ok()) ++fails;
    if (SOBER_FR_ND_OFF_REDRAW != 0x98u ||
        SOBER_FR_ND_OFF_WINDOW != 0x140u ||
        SOBER_FR_ND_SIZE != 0x200u) ++fails;
    /* Exact AGDK installer values (P5), not a monotonic-order guess. */
    if (SOBER_FR_CB_CREATED   != 0x2a23a10u) ++fails;
    if (SOBER_FR_CB_CHANGED   != 0x2a23cc0u) ++fails;
    if (SOBER_FR_CB_REDRAW    != 0x2a23c40u) ++fails;
    if (SOBER_FR_CB_DESTROYED != 0x2a23b60u) ++fails;
    if (!(SOBER_FR_RVA_SURFACE_CREATED < SOBER_FR_RVA_SURFACE_CHANGED &&
          SOBER_FR_RVA_SURFACE_CHANGED < SOBER_FR_RVA_REDRAW_THUNK &&
          SOBER_FR_RVA_REDRAW_THUNK < SOBER_FR_RVA_SURFACE_DESTROYED))
        ++fails;

    /* Classification with a synthetic base: dispatcher window and guest span. */
    s_base = 0x10000000u;
    if (!fr_in_dispatch(s_base + SOBER_FR_RVA_GLES_DISPATCH + 0x10u)) ++fails;
    if (fr_in_dispatch(s_base + SOBER_FR_RVA_GLES_DISPATCH_END)) ++fails;
    if (!fr_in_guest(s_base + SOBER_FR_RVA_SWAP_WRAPPER)) ++fails;
    if (fr_in_guest(s_base + SOBER_FR_TEXT_SIZE)) ++fails;
    s_base = save_base;

    if (buf && cap) {
        snprintf(buf, cap,
                 "selftest=%s fails=%d enabled=%d rva_swap=%#x "
                 "rva_dispatch=%#x rva_redraw=%#x",
                 fails == 0 ? "PASS" : "FAIL", fails,
                 sober_frame_redraw_enabled(),
                 (unsigned)SOBER_FR_RVA_SWAP_WRAPPER,
                 (unsigned)SOBER_FR_RVA_GLES_DISPATCH,
                 (unsigned)SOBER_FR_RVA_REDRAW_THUNK);
    }
    return fails;
}
