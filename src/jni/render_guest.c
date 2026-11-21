/*
 * render_guest.c - DEFAULT-ON guest render/surface binding (logical Team 3,
 *                  agent T3-4).  See render_guest.h for the full evidence
 *                  chain, frozen RVAs and the exact wiring signature.
 *
 * HARD RULES OBSERVED HERE
 * ------------------------
 *  - NEW FILES ONLY.  No CMake/engine/gate edits.  No decision-013 marker.
 *  - Default ON; kill switch SOBER_RENDER_GUEST=0 -> inert no-op.
 *  - No guest code bytes are patched.  The only guest writes are three GOT
 *    JUMP_SLOT pointer swaps and the ANativeWindow global publish at
 *    0x72a3308 (+ generation 0x72a3310), mirroring the guest's own 0x26fea5a.
 *  - No host function pointer is ever installed in a guest vtable.  The
 *    lifecycle thunks' callback slots are NULL (the guest NULL-checks every
 *    one of them), so no indirect call enters harness code.
 *  - GLES draw counting wraps the function pointers obtained through the
 *    guest's eglGetProcAddress loader (0x649c633...) with a counting
 *    tail-call trampoline that preserves all argument/return registers.
 */
#define _GNU_SOURCE
#include "render_guest.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

/* Compat shim's global EGL function pointer (src/compat/gl_egl_forward.c).
 * Weak so this translation unit also links into a target that omits compat. */
#if defined(__GNUC__) || defined(__clang__)
extern void (*sober_fn_eglCreateWindowSurface)(void) __attribute__((weak));
#else
extern void (*sober_fn_eglCreateWindowSurface)(void);
#endif

/* ---- frozen guest prototypes -------------------------------------------- */
typedef void   *(*rg_anw_from_surface_fn)(void *, void *);
typedef int     (*rg_swap_fn)(void *, void *);
typedef void   *(*rg_get_proc_fn)(const char *);
typedef uintptr_t (*rg_create_ctx_fn)(uintptr_t, uintptr_t);
typedef void    (*rg_surface_created_fn)(void *, void *, uintptr_t, void *);
typedef void    (*rg_surface_changed_fn)(void *, void *, uintptr_t, void *,
                                         int, int, int);
typedef void    (*rg_surface_redraw_fn)(void *, void *, uintptr_t, void *);
typedef void    (*rg_surface_destroyed_fn)(void *, void *, uintptr_t);

/* ---- module state -------------------------------------------------------- */
static uintptr_t s_base;
static int       s_installed;
static int       s_last_rc = SOBER_RENDER_GUEST_DISABLED;
static SoberGuestANativeWindow *s_anw;
/* G8: host stores to 0x72a3308 are ACTIVE until a genuine guest store is
 * proven by sober_render_evidence_bringup().  Never set to 0 by assumption. */
static int s_host_publish = 1;

int sober_render_guest_set_host_publish(int enabled)
{
    int prev = s_host_publish;
    s_host_publish = enabled ? 1 : 0;
    return prev;
}

int sober_render_guest_host_publish(void) { return s_host_publish; }
static uintptr_t s_ctx;
static uintptr_t s_ctx_vt;
static unsigned char *s_nd;                 /* nativeData (host, 0x200 bytes) */
static void     *s_surface_token[2];        /* opaque Surface *token*         */
static char      s_dummy_env[16];
static uintptr_t s_got_old[3];              /* fromSurface, swap, getProcAddr */

static _Atomic uint64_t s_from_surface_calls;
static _Atomic uint64_t s_gles_proc_loads;
static _Atomic uint64_t s_gles_draw_wrapped;
static _Atomic uint64_t s_swaps_total;
static _Atomic uint64_t s_swaps_guest;
static _Atomic uint64_t s_swaps_synthetic;
static _Atomic uint64_t s_life[4];

static void rg_bump(_Atomic uint64_t *c)
{
    atomic_fetch_add_explicit(c, 1u, memory_order_relaxed);
}

/* ---- counting tail-call trampolines ------------------------------------- *
 * x86-64 only (the frozen guest is x86-64).  Each 32-byte stub is:
 *   ff 05 0c 00 00 00      incq  0xc(%rip)     ; counter @ stub+0x12
 *   48 b8 <imm64 real>     movabs %rax, real
 *   ff e0                  jmp    *%rax
 *   <8-byte counter>                            ; stub+0x12
 *   <6 x 0x90 pad>
 * RSP is untouched and the real function is reached by a tail jmp, so the
 * original return address and every argument register arrive unmodified.
 * RAX and FLAGS are caller-saved across a call, so clobbering them is safe. */
#define RG_TRAMP_COUNT       16u
#define RG_TRAMP_SIZE        32u
#define RG_TRAMP_COUNTER_OFF 18u   /* 0x12 */

struct rg_tramp {
    unsigned char *code;
    char           name[32];
};
static struct rg_tramp s_tramps[RG_TRAMP_COUNT];
static unsigned        s_tramp_used;
static unsigned char  *s_tramp_map;

static int rg_ensure_trampoline_page(void)
{
    if (s_tramp_map) return 0;
    s_tramp_map = (unsigned char *)mmap(
        NULL, (size_t)RG_TRAMP_COUNT * RG_TRAMP_SIZE,
        PROT_READ | PROT_WRITE | PROT_EXEC,
        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (s_tramp_map == MAP_FAILED) {
        s_tramp_map = NULL;
        return -1;
    }
    return 0;
}

static void *rg_wrap_draw(const char *name, void *real)
{
    unsigned char *code;
    unsigned idx;
    size_t n;

    if (!real) return real;
    if (rg_ensure_trampoline_page() != 0) return real;
    if (s_tramp_used >= RG_TRAMP_COUNT) return real;

    idx = s_tramp_used;
    code = s_tramp_map + (size_t)idx * RG_TRAMP_SIZE;
    code[0] = 0xff; code[1] = 0x05; code[2] = 0x0c;
    code[3] = 0u;   code[4] = 0u;   code[5] = 0u;      /* incq 0xc(%rip) */
    code[6] = 0x48; code[7] = 0xb8;                    /* movabs rax, imm64 */
    memcpy(code + 8, &real, sizeof(real));
    code[16] = 0xff; code[17] = 0xe0;                  /* jmp rax          */
    memset(code + RG_TRAMP_COUNTER_OFF, 0, 8u);        /* zero counter     */
    memset(code + RG_TRAMP_COUNTER_OFF + 8u, 0x90,
           RG_TRAMP_SIZE - RG_TRAMP_COUNTER_OFF - 8u);
    __builtin___clear_cache((char *)code,
                            (char *)(code + RG_TRAMP_SIZE));

    s_tramps[idx].code = code;
    n = strnlen(name, sizeof(s_tramps[idx].name) - 1u);
    memcpy(s_tramps[idx].name, name, n);
    s_tramps[idx].name[n] = '\0';
    ++s_tramp_used;
    rg_bump(&s_gles_draw_wrapped);
    return (void *)code;
}

static uint64_t rg_draw_calls(void)
{
    uint64_t total = 0u;
    unsigned i;
    for (i = 0; i < s_tramp_used; ++i)
        total += *(volatile uint64_t *)(s_tramps[i].code
                                        + RG_TRAMP_COUNTER_OFF);
    return total;
}

/* Only entry points that actually emit draw/clear work are wrapped. */
static const char *const k_draw_names[] = {
    "glDrawArrays", "glDrawElements", "glDrawRangeElements",
    "glDrawArraysInstanced", "glDrawElementsInstanced",
    "glDrawElementsBaseVertex", "glDrawRangeElementsBaseVertex",
    "glDrawArraysInstancedBaseInstance",
    "glDrawElementsInstancedBaseVertex",
    "glDrawElementsInstancedBaseInstance",
    "glDrawElementsInstancedBaseVertexBaseInstance",
    "glMultiDrawArrays", "glMultiDrawElements",
    "glMultiDrawElementsBaseVertex",
    "glClear", "glClearBufferfv", "glClearBufferiv",
    "glClearBufferuiv", "glClearBufferfi"
};
#define RG_NDRAW (sizeof(k_draw_names) / sizeof(k_draw_names[0]))

static int rg_is_draw_name(const char *name)
{
    size_t n = strnlen(name, 64u);
    size_t i;
    if (n == 0u || n >= 64u) return 0;
    for (i = 0; i < RG_NDRAW; ++i)
        if (strlen(k_draw_names[i]) == n &&
            strncmp(name, k_draw_names[i], n) == 0)
            return 1;
    return 0;
}

/* ---- guest-address classification --------------------------------------- */
static int rg_in_guest(uintptr_t ra)
{
    return s_base != 0u && ra >= s_base &&
           ra < s_base + (uintptr_t)SOBER_RENDER_GUEST_TEXT_SIZE;
}

static int rg_in_gles_loader(uintptr_t ra)
{
    return rg_in_guest(ra) &&
           ra >= s_base + (uintptr_t)SOBER_RENDER_GUEST_RVA_GLES_LOADER &&
           ra <  s_base + (uintptr_t)SOBER_RENDER_GUEST_RVA_GLES_LOADER_END;
}

/* ---- GOT helpers --------------------------------------------------------- */
static int rg_page_prot(uintptr_t page)
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

static int rg_got_write(uintptr_t slot, uintptr_t val, uintptr_t *old_out)
{
    long ps = sysconf(_SC_PAGESIZE);
    uintptr_t page;
    int prot;
    uintptr_t old;

    if (ps <= 0) ps = 4096;
    page = slot & ~((uintptr_t)ps - 1u);
    prot = rg_page_prot(page);
    if (mprotect((void *)page, (size_t)ps, PROT_READ | PROT_WRITE) != 0)
        return -1;
    old = *(volatile uintptr_t *)slot;
    *(volatile uintptr_t *)slot = val;
    if (prot >= 0 && prot != (PROT_READ | PROT_WRITE))
        (void)mprotect((void *)page, (size_t)ps, prot);
    if (old_out) *old_out = old;
    return 0;
}

/* ---- the three GOT hooks ------------------------------------------------- */
/* void *ANativeWindow_fromSurface(JNIEnv *, jobject) */
static void *rg_hook_from_surface(void *env, void *surface)
{
    rg_anw_from_surface_fn orig;
    rg_bump(&s_from_surface_calls);
    if (s_anw) {
        /* The genuine NDK function returns a +1 reference; so do we.  The
         * compat ANativeWindow_release() correspondingly decrements it.  The
         * counter follows the compat shim's plain-integer convention. */
        ++s_anw->refs;
        return (void *)s_anw;
    }
    orig = (rg_anw_from_surface_fn)(uintptr_t)s_got_old[0];
    return orig ? orig(env, surface) : NULL;
}

/* EGLBoolean eglSwapBuffers(EGLDisplay, EGLSurface) -- tails the guest PLT. */
static int rg_hook_swap(void *dpy, void *surf)
{
    uintptr_t ra = (uintptr_t)__builtin_return_address(0);
    rg_swap_fn orig;
    rg_bump(&s_swaps_total);
    if (rg_in_guest(ra)) rg_bump(&s_swaps_guest);
    else                 rg_bump(&s_swaps_synthetic);
    orig = (rg_swap_fn)(uintptr_t)s_got_old[1];
    return orig ? orig(dpy, surf) : 0;
}

/* void *eglGetProcAddress(const char *) -- count GLES loads, wrap draws. */
static void *rg_hook_get_proc(const char *name)
{
    uintptr_t ra = (uintptr_t)__builtin_return_address(0);
    rg_get_proc_fn orig = (rg_get_proc_fn)(uintptr_t)s_got_old[2];
    void *real;

    if (rg_in_gles_loader(ra)) rg_bump(&s_gles_proc_loads);
    real = orig ? orig(name) : NULL;
    if (real && rg_in_gles_loader(ra) && rg_is_draw_name(name))
        return rg_wrap_draw(name, real);
    return real;
}

/* ---- small helpers ------------------------------------------------------- */
static int rg_env_int(const char *name, int fallback)
{
    const char *v = getenv(name);
    if (!v || !*v) return fallback;
    return (int)strtol(v, NULL, 10);
}

/* ---- lifecycle thunk driver --------------------------------------------- */
static int rg_bind_surface(void)
{
    void *env = (void *)s_dummy_env;
    void *tok = (void *)&s_surface_token;
    uintptr_t nd = (uintptr_t)s_nd;
    int w = s_anw ? (int)s_anw->width : 0;
    int h = s_anw ? (int)s_anw->height : 0;

    /* created: the guest first releases [nativeData+0x140]; NULL is safe. */
    ((rg_surface_created_fn)(uintptr_t)(s_base
        + (uintptr_t)SOBER_RENDER_GUEST_RVA_SURFACE_CREATED))(
            env, NULL, nd, tok);
    rg_bump(&s_life[0]);

    /* changed: acquire/release old window, fromSurface again, then
     * ANativeWindow_getWidth/getHeight through the compat GOT functions. */
    ((rg_surface_changed_fn)(uintptr_t)(s_base
        + (uintptr_t)SOBER_RENDER_GUEST_RVA_SURFACE_CHANGED))(
            env, NULL, nd, tok, w, h, 0);
    rg_bump(&s_life[1]);

    /* redraw: reads [nativeData+0x140] then NULL-checks [+0x98] (0x2a20a18),
     * so it is a genuine guest thunk call with no host callback. */
    ((rg_surface_redraw_fn)(uintptr_t)(s_base
        + (uintptr_t)SOBER_RENDER_GUEST_RVA_SURFACE_REDRAW))(
            env, NULL, nd, tok);
    rg_bump(&s_life[2]);

    /* destroyed: NULL-checks [+0xa0] (0x2a20a4f), releases the window and
     * zeroes [+0x140].  Our high initial refcount keeps the window alive. */
    ((rg_surface_destroyed_fn)(uintptr_t)(s_base
        + (uintptr_t)SOBER_RENDER_GUEST_RVA_SURFACE_DESTROYED))(
            env, NULL, nd);
    rg_bump(&s_life[3]);

    /* Republish: the guest's own fromSurface helper writes this global; do
     * the same explicitly so it stays our window after the lifecycle replay.
     * G8: suppressed once a genuine guest store has been proven. */
    if (s_base && s_host_publish) {
        *(volatile uintptr_t *)(s_base
            + (uintptr_t)SOBER_RENDER_GUEST_G_WINDOW) = (uintptr_t)s_anw;
    }
    return SOBER_RENDER_GUEST_OK;
}

/* ---- public: gate -------------------------------------------------------- */
int sober_render_guest_enabled(void)
{
    const char *e = getenv(SOBER_RENDER_GUEST_ENV);
    /* DEFAULT ON: only the exact string "0" kills it. */
    return !(e != NULL && strcmp(e, "0") == 0);
}

const char *sober_render_guest_env_vars(void)
{
    return SOBER_RENDER_GUEST_ENV
           "=0 (kill switch; default ON - GOT hooks 0x72294a8/0x722a1a8/"
           "0x722a180, surface thunks 0x2a20850/0x2a208c0/0x2a20a00/0x2a20a30, "
           "guest EGL context 0x649c21e with pbuffer=0)";
}

/* ---- public: install ----------------------------------------------------- */
int sober_render_guest_install(const struct sober_render_guest_config *cfg)
{
    uintptr_t slot, old_window;
    int w, h;

    if (!sober_render_guest_enabled()) {
        s_last_rc = SOBER_RENDER_GUEST_DISABLED;
        return SOBER_RENDER_GUEST_DISABLED;
    }
    if (!cfg || !cfg->guest_base) {
        s_last_rc = SOBER_RENDER_GUEST_ERR_ARG;
        return SOBER_RENDER_GUEST_ERR_ARG;
    }
    if (s_installed) {
        s_last_rc = SOBER_RENDER_GUEST_OK;
        return SOBER_RENDER_GUEST_OK;
    }
    if (rg_ensure_trampoline_page() != 0) {
        s_last_rc = SOBER_RENDER_GUEST_ERR_TRAMP;
        return SOBER_RENDER_GUEST_ERR_TRAMP;
    }

    s_base = (uintptr_t)cfg->guest_base;

    /* Build the guest-shaped ANativeWindow. */
    s_anw = (SoberGuestANativeWindow *)calloc(1u, sizeof(*s_anw));
    if (!s_anw) {
        s_last_rc = SOBER_RENDER_GUEST_ERR_ARG;
        return SOBER_RENDER_GUEST_ERR_ARG;
    }
    w = cfg->width > 0 ? cfg->width
        : rg_env_int("SOBER_WINDOW_WIDTH", 1280);
    h = cfg->height > 0 ? cfg->height
        : rg_env_int("SOBER_WINDOW_HEIGHT", 720);
    /* The guest can release this pointer from pause/destroy paths we do not
     * control; a large initial count keeps the calloc'd window live for the
     * process lifetime (the compat shim frees at zero). */
    s_anw->refs = 0x10000u;
    s_anw->width = (int32_t)w;
    s_anw->height = (int32_t)h;
    s_anw->magic = 0x41574e57u;     /* 'AWNW' */
    s_anw->version = 1;
    s_anw->vtable = (void *)(s_base
                            + (uintptr_t)SOBER_RENDER_GUEST_RVA_VTABLE);
    s_anw->native_window = cfg->native_window;
    s_anw->native_handle = cfg->native_handle;
    s_anw->egl_display = cfg->egl_display;
    s_anw->egl_surface = cfg->egl_surface;
    s_anw->egl_context = cfg->egl_context;
    s_anw->min_swap = 0;
    s_anw->max_swap = 1;

    /* Build the synthetic nativeData the four GameActivity thunks expect.
     * All three callback slots (+0x88/+0x98/+0xa0) stay NULL on purpose:
     * every guest thunk NULL-checks them before an indirect call. */
    s_nd = (unsigned char *)calloc(1u,
                                   (size_t)SOBER_RENDER_GUEST_ND_SIZE);
    if (!s_nd) {
        free(s_anw);
        s_anw = NULL;
        s_last_rc = SOBER_RENDER_GUEST_ERR_ARG;
        return SOBER_RENDER_GUEST_ERR_ARG;
    }
    *(void **)(s_nd + (size_t)SOBER_RENDER_GUEST_ND_OFF_ENV) =
        (void *)s_dummy_env;
    s_surface_token[0] = (void *)s_anw;    /* opaque token, not a real Surface */

    /* Publish the window exactly where the guest's 0x26fea5a publishes it.
     * G8: this host store is skipped once the real guest bind is proven. */
    old_window = *(volatile uintptr_t *)(s_base
        + (uintptr_t)SOBER_RENDER_GUEST_G_WINDOW);
    if (s_host_publish) {
        *(volatile uintptr_t *)(s_base
            + (uintptr_t)SOBER_RENDER_GUEST_G_WINDOW) = (uintptr_t)s_anw;
        __atomic_fetch_add((uintptr_t *)(s_base
            + (uintptr_t)SOBER_RENDER_GUEST_G_WINDOW_GEN), 1u,
            __ATOMIC_SEQ_CST);
    }

    /* Let guest EGL init reach the real native drawable. */
    if (cfg->egl_create_window_surface_hook &&
        &sober_fn_eglCreateWindowSurface != NULL) {
        sober_fn_eglCreateWindowSurface =
            (void (*)(void))(uintptr_t)cfg->egl_create_window_surface_hook;
    }

    /* Install the GOT hooks (fromSurface, swap, getProcAddr). */
    slot = s_base + (uintptr_t)SOBER_RENDER_GUEST_GOT_FROM_SURFACE;
    if (rg_got_write(slot, (uintptr_t)&rg_hook_from_surface,
                     &s_got_old[0]) != 0)
        goto got_fail;
    slot = s_base + (uintptr_t)SOBER_RENDER_GUEST_GOT_SWAP_BUFFERS;
    if (rg_got_write(slot, (uintptr_t)&rg_hook_swap, &s_got_old[1]) != 0) {
        (void)rg_got_write(s_base
            + (uintptr_t)SOBER_RENDER_GUEST_GOT_FROM_SURFACE,
            s_got_old[0], NULL);
        goto got_fail;
    }
    slot = s_base + (uintptr_t)SOBER_RENDER_GUEST_GOT_GET_PROCADDR;
    if (rg_got_write(slot, (uintptr_t)&rg_hook_get_proc, &s_got_old[2]) != 0) {
        (void)rg_got_write(s_base
            + (uintptr_t)SOBER_RENDER_GUEST_GOT_FROM_SURFACE,
            s_got_old[0], NULL);
        (void)rg_got_write(s_base
            + (uintptr_t)SOBER_RENDER_GUEST_GOT_SWAP_BUFFERS,
            s_got_old[1], NULL);
        goto got_fail;
    }

    s_installed = 1;
    s_last_rc = SOBER_RENDER_GUEST_OK;
    return SOBER_RENDER_GUEST_OK;

got_fail:
    /* Do not leave a dangling window pointer in the guest global. */
    if (s_host_publish)
        *(volatile uintptr_t *)(s_base
            + (uintptr_t)SOBER_RENDER_GUEST_G_WINDOW) = old_window;
    free(s_nd);
    s_nd = NULL;
    free(s_anw);
    s_anw = NULL;
    s_last_rc = SOBER_RENDER_GUEST_ERR_GOT;
    return SOBER_RENDER_GUEST_ERR_GOT;
}

/* ---- public: bind / create / run ----------------------------------------- */
int sober_render_guest_bind_surface(void)
{
    if (!sober_render_guest_enabled()) {
        s_last_rc = SOBER_RENDER_GUEST_DISABLED;
        return SOBER_RENDER_GUEST_DISABLED;
    }
    if (!s_installed) {
        s_last_rc = SOBER_RENDER_GUEST_ERR_STATE;
        return SOBER_RENDER_GUEST_ERR_STATE;
    }
    s_last_rc = rg_bind_surface();
    return s_last_rc;
}

int sober_render_guest_create_context(void)
{
    rg_create_ctx_fn create;
    uintptr_t ctx, vt;

    if (!sober_render_guest_enabled()) {
        s_last_rc = SOBER_RENDER_GUEST_DISABLED;
        return SOBER_RENDER_GUEST_DISABLED;
    }
    if (!s_installed) {
        s_last_rc = SOBER_RENDER_GUEST_ERR_STATE;
        return SOBER_RENDER_GUEST_ERR_STATE;
    }

    /* pbuffer=0 is essential: a non-zero second argument is stored at ctx+0x10
     * and dereferenced by the config helper at 0x649cf9a (C04 section 1.4). */
    create = (rg_create_ctx_fn)(uintptr_t)(s_base
             + (uintptr_t)SOBER_RENDER_GUEST_RVA_CREATE_CONTEXT);
    ctx = create((uintptr_t)s_anw, (uintptr_t)SOBER_RENDER_GUEST_PBUFFER);
    s_ctx = ctx;
    if (!ctx) {
        s_last_rc = SOBER_RENDER_GUEST_ERR_CONTEXT;
        return SOBER_RENDER_GUEST_ERR_CONTEXT;
    }
    vt = *(volatile uintptr_t *)ctx;
    s_ctx_vt = vt;
    if (vt != s_base + (uintptr_t)SOBER_RENDER_GUEST_RVA_VTABLE) {
        s_last_rc = SOBER_RENDER_GUEST_ERR_VTABLE;
        return SOBER_RENDER_GUEST_ERR_VTABLE;
    }
    s_last_rc = SOBER_RENDER_GUEST_OK;
    return SOBER_RENDER_GUEST_OK;
}

int sober_render_guest_swap_via_vtable(void)
{
    uintptr_t vt, slot;
    int (*swap)(uintptr_t);

    if (!sober_render_guest_enabled()) {
        s_last_rc = SOBER_RENDER_GUEST_DISABLED;
        return SOBER_RENDER_GUEST_DISABLED;
    }
    if (!s_installed || !s_ctx || !s_ctx_vt) {
        s_last_rc = SOBER_RENDER_GUEST_ERR_STATE;
        return SOBER_RENDER_GUEST_ERR_STATE;
    }
    vt = s_ctx_vt;
    if (vt != s_base + (uintptr_t)SOBER_RENDER_GUEST_RVA_VTABLE) {
        s_last_rc = SOBER_RENDER_GUEST_ERR_VTABLE;
        return SOBER_RENDER_GUEST_ERR_VTABLE;
    }
    slot = *(volatile uintptr_t *)(vt
           + (uintptr_t)SOBER_RENDER_GUEST_VTABLE_SWAP_OFF);
    if (slot != s_base + (uintptr_t)SOBER_RENDER_GUEST_RVA_SWAP_WRAPPER) {
        s_last_rc = SOBER_RENDER_GUEST_ERR_VTABLE;
        return SOBER_RENDER_GUEST_ERR_VTABLE;
    }
    swap = (int (*)(uintptr_t))(uintptr_t)slot;
    (void)swap(s_ctx);    /* RA is host -> counted as a synthetic swap */
    s_last_rc = SOBER_RENDER_GUEST_OK;
    return SOBER_RENDER_GUEST_OK;
}

int sober_render_guest_run(const struct sober_render_guest_config *cfg)
{
    int rc = sober_render_guest_install(cfg);
    if (rc != SOBER_RENDER_GUEST_OK) return rc;
    rc = sober_render_guest_bind_surface();
    if (rc != SOBER_RENDER_GUEST_OK) return rc;
    return sober_render_guest_create_context();
}

void sober_render_guest_uninstall(void)
{
    if (!s_installed) return;
    (void)rg_got_write(s_base
        + (uintptr_t)SOBER_RENDER_GUEST_GOT_FROM_SURFACE, s_got_old[0], NULL);
    (void)rg_got_write(s_base
        + (uintptr_t)SOBER_RENDER_GUEST_GOT_SWAP_BUFFERS, s_got_old[1], NULL);
    (void)rg_got_write(s_base
        + (uintptr_t)SOBER_RENDER_GUEST_GOT_GET_PROCADDR, s_got_old[2], NULL);
    s_installed = 0;
    /* s_anw / s_nd are deliberately retained: guest code may still hold the
     * pointers.  Counters and accessors stay valid for post-run diagnostics. */
}

int sober_render_guest_is_installed(void)
{
    return s_installed;
}

/* ---- public: state accessors --------------------------------------------- */
SoberGuestANativeWindow *sober_render_guest_window(void) { return s_anw; }
void *sober_render_guest_context(void) { return (void *)s_ctx; }
void *sober_render_guest_context_vtable(void) { return (void *)s_ctx_vt; }
uintptr_t sober_render_guest_base(void) { return s_base; }
int sober_render_guest_last_rc(void) { return s_last_rc; }

uint64_t sober_render_guest_from_surface_calls(void)
{
    return atomic_load_explicit(&s_from_surface_calls, memory_order_relaxed);
}
uint64_t sober_render_guest_gles_proc_loads(void)
{
    return atomic_load_explicit(&s_gles_proc_loads, memory_order_relaxed);
}
uint64_t sober_render_guest_gles_draw_procs_wrapped(void)
{
    return atomic_load_explicit(&s_gles_draw_wrapped, memory_order_relaxed);
}
uint64_t sober_render_guest_gles_draw_calls(void)
{
    return rg_draw_calls();
}
uint64_t sober_render_guest_swaps_total(void)
{
    return atomic_load_explicit(&s_swaps_total, memory_order_relaxed);
}
uint64_t sober_render_guest_swaps_guest_tagged(void)
{
    return atomic_load_explicit(&s_swaps_guest, memory_order_relaxed);
}
uint64_t sober_render_guest_swaps_synthetic(void)
{
    return atomic_load_explicit(&s_swaps_synthetic, memory_order_relaxed);
}
uint64_t sober_render_guest_lifecycle_calls(unsigned idx)
{
    if (idx >= 4u) return 0u;
    return atomic_load_explicit(&s_life[idx], memory_order_relaxed);
}

/* ---- public: diagnostics ------------------------------------------------- */
const char *sober_render_guest_describe(char *buf, size_t cap)
{
    if (!buf || cap == 0u) return "";
    snprintf(buf, cap,
             "enabled=%d installed=%d rc=%d base=%#lx anw=%p anw_refs=%u "
             "w=%d h=%d ctx=%#lx vt=%#lx(vt_expect=%#lx) "
             "got_from=%#x swap=%#x getproc=%#x "
             "life=%llu/%llu/%llu/%llu from_surface=%llu "
             "gles_loads=%llu draw_wrapped=%llu draw_calls=%llu "
             "swaps=%llu guest=%llu synthetic=%llu env=%s=0",
             sober_render_guest_enabled(), s_installed, s_last_rc,
             (unsigned long)s_base, (void *)s_anw,
             s_anw ? (unsigned)s_anw->refs : 0u,
             s_anw ? (int)s_anw->width : 0,
             s_anw ? (int)s_anw->height : 0,
             (unsigned long)s_ctx, (unsigned long)s_ctx_vt,
             (unsigned long)(s_base
                 + (uintptr_t)SOBER_RENDER_GUEST_RVA_VTABLE),
             (unsigned)SOBER_RENDER_GUEST_GOT_FROM_SURFACE,
             (unsigned)SOBER_RENDER_GUEST_GOT_SWAP_BUFFERS,
             (unsigned)SOBER_RENDER_GUEST_GOT_GET_PROCADDR,
             (unsigned long long)sober_render_guest_lifecycle_calls(0),
             (unsigned long long)sober_render_guest_lifecycle_calls(1),
             (unsigned long long)sober_render_guest_lifecycle_calls(2),
             (unsigned long long)sober_render_guest_lifecycle_calls(3),
             (unsigned long long)sober_render_guest_from_surface_calls(),
             (unsigned long long)sober_render_guest_gles_proc_loads(),
             (unsigned long long)sober_render_guest_gles_draw_procs_wrapped(),
             (unsigned long long)sober_render_guest_gles_draw_calls(),
             (unsigned long long)sober_render_guest_swaps_total(),
             (unsigned long long)sober_render_guest_swaps_guest_tagged(),
             (unsigned long long)sober_render_guest_swaps_synthetic(),
             SOBER_RENDER_GUEST_ENV);
    return buf;
}

const char *sober_render_guest_env_dump(void)
{
    return SOBER_RENDER_GUEST_ENV "=0|1 (default ON; 0 kills all hooks and "
           "thunk calls); companion SOBER_WINDOW_WIDTH/SOBER_WINDOW_HEIGHT "
           "seed the guest-shaped ANativeWindow dimensions";
}

/* ---- public: self-test --------------------------------------------------- */
static int rg_selftest_target(int x) { return x * 3 + 1; }

int sober_render_guest_selftest(char *buf, size_t cap)
{
    int fails = 0;
    uint64_t before, after;
    void *stub;
    int (*fn)(int);

    /* RVA invariants. */
    if (SOBER_RENDER_GUEST_GOT_FROM_SURFACE == 0u ||
        SOBER_RENDER_GUEST_GOT_SWAP_BUFFERS == 0u ||
        SOBER_RENDER_GUEST_GOT_GET_PROCADDR == 0u ||
        SOBER_RENDER_GUEST_GOT_FROM_SURFACE
            == SOBER_RENDER_GUEST_GOT_SWAP_BUFFERS)
        ++fails;
    if (SOBER_RENDER_GUEST_RVA_VTABLE
        + SOBER_RENDER_GUEST_VTABLE_SWAP_OFF != 0x7191300u)
        ++fails;
    if (SOBER_RENDER_GUEST_CTX_SIZE != 0x48u) ++fails;
    if (SOBER_RENDER_GUEST_RVA_GLES_LOADER_END
        <= SOBER_RENDER_GUEST_RVA_GLES_LOADER) ++fails;
    if (!(SOBER_RENDER_GUEST_RVA_SURFACE_CREATED
          < SOBER_RENDER_GUEST_RVA_SURFACE_CHANGED &&
          SOBER_RENDER_GUEST_RVA_SURFACE_CHANGED
          < SOBER_RENDER_GUEST_RVA_SURFACE_REDRAW &&
          SOBER_RENDER_GUEST_RVA_SURFACE_REDRAW
          < SOBER_RENDER_GUEST_RVA_SURFACE_DESTROYED))
        ++fails;
    if (!rg_is_draw_name("glDrawElements")) ++fails;
    if (rg_is_draw_name("glClearColor")) ++fails;   /* state, not a draw */

    /* Trampoline mechanism: wrap a local function, call it, check pass-through
     * and that exactly one draw call was counted. */
    before = rg_draw_calls();
    stub = rg_wrap_draw("glDrawArrays(selftest)",
                        (void *)(uintptr_t)&rg_selftest_target);
    if (stub == (void *)(uintptr_t)&rg_selftest_target) {
        ++fails;                 /* RWX page unavailable or pool full */
    } else {
        fn = (int (*)(int))(uintptr_t)stub;
        if (fn(13) != 40) ++fails;
        after = rg_draw_calls();
        if (after != before + 1u) ++fails;
    }

    if (buf && cap) {
        snprintf(buf, cap,
                 "selftest=%s fails=%d enabled=%d draws=%llu wrapped=%llu",
                 fails == 0 ? "PASS" : "FAIL", fails,
                 sober_render_guest_enabled(),
                 (unsigned long long)rg_draw_calls(),
                 (unsigned long long)sober_render_guest_gles_draw_procs_wrapped());
    }
    return fails;
}
