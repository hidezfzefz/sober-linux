/*
 * frame_surface.c - DEFAULT-ON real-EGLSurface binding for the frozen guest
 *                   (agent F5, group C).  See frame_surface.h for the full
 *                   evidence chain, frozen RVAs and wiring signature.
 *
 * HARD RULES OBSERVED HERE
 * ------------------------
 *  - NEW FILES ONLY.  No CMake/engine/gate/marker edits.  Default ON;
 *    SOBER_FRAME_SURFACE=0 -> inert no-op.
 *  - No guest code byte is patched.  The only guest writes are two GOT
 *    JUMP_SLOT pointer swaps, the ctx+0x18/ctx+0x28 field stores that mirror
 *    guest instructions 0x649c2cf / 0x649c3f6, and the host-side nativeData
 *    +0x140 re-assert.  The guest WINDOW GLOBAL 0x72a3308 is NEVER written by
 *    this module -- guest 0x26fea5a writes it (0x26feaa0) after its own
 *    generation increment (0x26fea90) and fromSurface call (0x26fea78).
 *  - No host fake vtable, no host callback installed in nativeData, no JNI
 *    stub, no decision-013 marker.  Real guest code performs the bind, the
 *    config lookup, the surface creation and all four lifecycle thunks; the
 *    real host EGL driver creates the EGLSurface.
 *  - Every counter is an actual event (hook entry / real call / real restore).
 */
#define _GNU_SOURCE
#include "frame_surface.h"
#include "frame_evidence.h"   /* G8 C2 producer (read-only ledger feed) */
#include "fields_default.h"   /* #492 mNativeObject provider registration */

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

/* Compat shim's global EGL function-pointer slots (src/compat/gl_egl_forward.c).
 * Weak: this translation unit also links into targets that omit the compat
 * shim, where an unresolved weak pointer reads as NULL.  They are only a
 * fallback; the lead-supplied config pointers are always preferred. */
#if defined(__GNUC__) || defined(__clang__)
extern void (*sober_fn_eglCreateWindowSurface)(void) __attribute__((weak));
extern void (*sober_fn_eglMakeCurrent)(void) __attribute__((weak));
#else
extern void (*sober_fn_eglCreateWindowSurface)(void);
extern void (*sober_fn_eglMakeCurrent)(void);
#endif

/* ---- frozen guest prototypes -------------------------------------------- */
typedef void *(*fs_anw_from_surface_fn)(void *env, void *surface);
typedef void *(*fs_create_window_surface_fn)(void *dpy, void *cfg,
                                             void *win, const int *attribs);
typedef int   (*fs_make_current_fn)(void *dpy, void *draw, void *read,
                                    void *ctx);
/* Genuine signature (disassembly of callers 0x26fe8c5 / 0x2773220 / 0x2ebd5d0):
 *   void helper_0x26fea5a(JNIEnv *env, jobject surface, const char *logTag);
 * rdx is the FLog tag copied to r14; passing only 2 args leaves it garbage and
 * faults in the guest logger. */
typedef void  (*fs_bind_window_fn)(void *env, void *surface,
                                   const char *log_tag);

/* Forward declaration: registered as the mNativeObject provider at install. */
static int64_t fs_native_handle_cb(void *env, void *surface_obj, void *user);
typedef void *(*fs_config_helper_fn)(void *ctx);
typedef void *(*fs_surface_helper_fn)(void *ctx, void *cfg);
typedef void  (*fs_life4_fn)(void *env, void *surface, uintptr_t nd,
                             void *token);
typedef void  (*fs_life7_fn)(void *env, void *surface, uintptr_t nd,
                             void *token, int width, int height, int flags);
typedef void  (*fs_life3_fn)(void *env, void *surface, uintptr_t nd);

/* ---- module state -------------------------------------------------------- */
static uintptr_t s_base;
static int       s_installed;
static int       s_last_rc = SOBER_FRAME_SURFACE_DISABLED;
static SoberFrameSurfaceWindow *s_win;
static unsigned char *s_nd;                 /* host nativeData, 0x200 bytes */
static void     *s_ctx;                     /* genuine guest 0x48-byte ctx  */
static void     *s_surface;                 /* real EGLSurface from hook    */
static void     *s_cfg;                     /* EGLConfig from guest helper  */
static void     *s_create_fn;               /* real EGL create (config)     */
static void     *s_makecurrent_fn;          /* real EGL makeCurrent (config)*/
static char      s_env[16];
static char      s_token[2];                /* opaque Surface *token*       */
static uintptr_t s_got_old[2];              /* fromSurface, createSurface   */
static uintptr_t s_window_global_before;
static uintptr_t s_window_global_after;
static uint32_t  s_gen_before;
static uint32_t  s_gen_after;
static int       s_guest_store_seen;
static int       s_surface_ready;
static int       s_makecurrent_ok;

/* All real-event counters.  None of them is touched by install(), verify(),
 * describe() or selftest(). */
static _Atomic uint64_t s_from_surface_calls;
static _Atomic uint64_t s_from_surface_guest_ra;
static _Atomic uint64_t s_from_surface_bind_ra;
static _Atomic uint64_t s_create_calls;
static _Atomic uint64_t s_create_from_helper;
static _Atomic uint64_t s_create_ok;
static _Atomic uint64_t s_create_window_matches;
static _Atomic uint64_t s_bind_helper_calls;
static _Atomic uint64_t s_cfg_helper_calls;
static _Atomic uint64_t s_helper_calls;
static _Atomic uint64_t s_surface_bound;
static _Atomic uint64_t s_makecurrent_calls;
static _Atomic uint64_t s_makecurrent_success;
static _Atomic uint64_t s_life[4];
static _Atomic uint64_t s_reasserts;

static void fs_bump(_Atomic uint64_t *c)
{
    atomic_fetch_add_explicit(c, 1u, memory_order_relaxed);
}

/* ---- frozen engine identity (F10 bogus-base defence) --------------------- *
 * The module owns no guest-base state: the lead supplies the load bias.  We
 * read the mapped ELF's PT_NOTE / NT_GNU_BUILD_ID and require the AppImage
 * prefix 204ad049 before touching any GOT, so a wrong base can never make the
 * RVAs "work" against a different build.  Read-only; clamped against a bogus
 * pointer (wrong ELF magic short-circuits). */
static uint16_t fs_rd16(const unsigned char *p)
{
    uint16_t v;
    memcpy(&v, (const void *)p, sizeof(v));
    return v;
}

static uint32_t fs_rd32(const unsigned char *p)
{
    uint32_t v;
    memcpy(&v, (const void *)p, sizeof(v));
    return v;
}

static uint64_t fs_rd64(const unsigned char *p)
{
    uint64_t v;
    memcpy(&v, (const void *)p, sizeof(v));
    return v;
}

static int fs_engine_identity_ok(uintptr_t base)
{
    const unsigned char *e = (const unsigned char *)base;
    uint64_t phoff;
    uint16_t phentsize, phnum;
    unsigned i;

    if (!e) return 0;
    if (e[0] != 0x7f || e[1] != 'E' || e[2] != 'L' || e[3] != 'F') return 0;
    phoff = fs_rd64(e + 32);
    phentsize = fs_rd16(e + 54);
    phnum = fs_rd16(e + 56);
    /* Frozen build: 9 program headers of 56 bytes at file offset 0x40. */
    if (phoff == 0u || phoff > 0x100000u || phentsize != 56u ||
        phnum == 0u || phnum > 64u) return 0;

    for (i = 0; i < phnum; ++i) {
        const unsigned char *ph = e + (size_t)phoff + (size_t)i * phentsize;
        uint32_t type = fs_rd32(ph);
        uint64_t vaddr, filesz;
        size_t off, end;

        if (type != 4u) continue;              /* PT_NOTE */
        vaddr = fs_rd64(ph + 16);
        filesz = fs_rd64(ph + 32);
        if (vaddr == 0u || vaddr > (uint64_t)SOBER_FRAME_SURFACE_TEXT_SIZE ||
            filesz == 0u || filesz > 0x1000u) continue;
        off = (size_t)vaddr;
        end = off + (size_t)filesz;

        while (off + 12u <= end) {
            uint32_t namesz = fs_rd32(e + off);
            uint32_t descsz = fs_rd32(e + off + 4u);
            uint32_t ntype  = fs_rd32(e + off + 8u);
            size_t nameoff, descoff, next;

            if (namesz > 0x100u || descsz > 0x100u) break;
            nameoff = off + 12u;
            descoff = nameoff + (((size_t)namesz + 3u) & ~(size_t)3u);
            if (descoff + (size_t)descsz > end) break;
            /* NT_GNU_BUILD_ID desc bytes are "20 4a d0 49 ..." (the hex text
             * 204ad049... is NOT a little-endian uint32 -- compare bytes). */
            if (ntype == 3u && namesz >= 4u && descsz >= 4u &&
                memcmp((const void *)(e + nameoff), "GNU", 3) == 0 &&
                memcmp((const void *)(e + descoff), "\x20\x4a\xd0\x49", 4) == 0)
                return 1;
            next = descoff + (((size_t)descsz + 3u) & ~(size_t)3u);
            if (next <= off) break;
            off = next;
        }
    }
    return 0;
}

/* ---- guest-address classification --------------------------------------- */
static int fs_in_guest(uintptr_t ra)
{
    return s_base != 0u && ra >= s_base &&
           ra < s_base + (uintptr_t)SOBER_FRAME_SURFACE_TEXT_SIZE;
}

/* RA at our 0x72294a8 hook from guest 0x26fea5a lands at 0x26fea7d. */
static int fs_in_bind_helper(uintptr_t ra)
{
    return fs_in_guest(ra) &&
           ra >= s_base + (uintptr_t)SOBER_FRAME_SURFACE_RVA_FROM_SURFACE_CALL &&
           ra <  s_base + (uintptr_t)SOBER_FRAME_SURFACE_RVA_BIND_RA_END;
}

/* RA from the window-surface call site inside 0x649d1c2. */
static int fs_in_surface_helper(uintptr_t ra)
{
    return fs_in_guest(ra) &&
           ra >= s_base + (uintptr_t)SOBER_FRAME_SURFACE_RVA_SURFACE_HELPER &&
           ra <  s_base + (uintptr_t)SOBER_FRAME_SURFACE_RVA_SURFACE_HELPER + 0x40u;
}

/* ---- GOT helpers (pointer swaps only, never code bytes) ------------------ */
static int fs_page_prot(uintptr_t page)
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

static int fs_got_write(uintptr_t slot, uintptr_t val, uintptr_t *old_out)
{
    long ps = sysconf(_SC_PAGESIZE);
    uintptr_t page;
    int prot;
    uintptr_t old;

    if (ps <= 0) ps = 4096;
    page = slot & ~((uintptr_t)ps - 1u);
    prot = fs_page_prot(page);
    if (mprotect((void *)page, (size_t)ps, PROT_READ | PROT_WRITE) != 0)
        return -1;
    old = *(volatile uintptr_t *)slot;
    *(volatile uintptr_t *)slot = val;
    if (prot >= 0 && prot != (PROT_READ | PROT_WRITE))
        (void)mprotect((void *)page, (size_t)ps, prot);
    if (old_out) *old_out = old;
    return 0;
}

/* ---- guest-global readers (never writers) ------------------------------- */
static uintptr_t fs_window_global(void)
{
    if (!s_base) return 0u;
    return *(volatile uintptr_t *)(s_base
                    + (uintptr_t)SOBER_FRAME_SURFACE_RVA_G_WINDOW);
}

static uint32_t fs_window_generation(void)
{
    if (!s_base) return 0u;
    return *(volatile uint32_t *)(s_base
                    + (uintptr_t)SOBER_FRAME_SURFACE_RVA_G_WINDOW_GEN);
}

/* ---- nativeData +0x140 maintenance (P2 2 / follow-up) -------------------- *
 * The redraw thunk 0x2a20a00 tests [nd+0x140] at 0x2a20a0c BEFORE it tests
 * the +0x98 callback, and the destroy thunk 0x2a20a30 zeroes +0x140 at
 * 0x2a20a6a.  After every guest lifecycle call we therefore check the field
 * and restore it to the guest-bound window ONLY when it actually changed;
 * the re-assert counter is bumped only on a real restore (no fake counts). */
static void fs_assert_window_field(void)
{
    uintptr_t want = (uintptr_t)s_win;
    uintptr_t cur;

    if (!s_nd || !want) return;
    cur = *(volatile uintptr_t *)(s_nd
            + (uintptr_t)SOBER_FRAME_SURFACE_ND_OFF_WINDOW);
    if (cur != want) {
        *(volatile uintptr_t *)(s_nd
            + (uintptr_t)SOBER_FRAME_SURFACE_ND_OFF_WINDOW) = want;
        fs_bump(&s_reasserts);
    }
}

/* ---- the two GOT hooks --------------------------------------------------- */
/* void *ANativeWindow_fromSurface(JNIEnv *, jobject) -- GOT 0x72294a8.
 * A guest-RA caller receives our compat-prefix window; the call is counted
 * with its RA class.  Host callers fall through to the original resolver. */
static void *fs_hook_from_surface(void *env, void *surface)
{
    uintptr_t ra = (uintptr_t)__builtin_return_address(0);
    uintptr_t w_before = 0u;
    void *ret;
    uint32_t g_before = 0u;
    fs_anw_from_surface_fn orig;

    /* G8 C2/C5: sample the window/generation BEFORE the genuine call so the
     * guest's own inc/store can be attributed to this exact event. */
    if (s_base) {
        w_before = *(volatile uintptr_t *)(s_base +
                      (uintptr_t)SOBER_FRAME_SURFACE_RVA_G_WINDOW);
        g_before = *(volatile uint32_t *)(s_base +
                      (uintptr_t)SOBER_FRAME_SURFACE_RVA_G_WINDOW_GEN);
    }

    fs_bump(&s_from_surface_calls);
    if (fs_in_guest(ra)) {
        fs_bump(&s_from_surface_guest_ra);
        if (fs_in_bind_helper(ra)) fs_bump(&s_from_surface_bind_ra);
    }
    if (s_win && fs_in_guest(ra)) {
        /* The genuine NDK function returns a +1 reference; so do we.  The
         * compat ANativeWindow_release() decrements the plain-integer count. */
        ++s_win->refs;
        ret = (void *)s_win;
    } else {
        orig = (fs_anw_from_surface_fn)(uintptr_t)s_got_old[0];
        ret = orig ? orig(env, surface) : (void *)NULL;
    }
    /* Feed the sole render verdict owner with the TRUE guest RA; do it here
     * (not from a C observer chained on top) so the RA stays guest-classified. */
    if (fs_in_guest(ra))
        frame_evidence_note_from_surface(ra, env, surface, (uintptr_t)ret,
                                         w_before, g_before);
    return ret;
}

/* void *eglCreateWindowSurface(dpy, cfg, win, attribs) -- GOT 0x722a178.
 * This is the single substitution point reached by guest 0x649d1c2.  We call
 * the REAL host EGL create function (lead config, else compat weak slot);
 * the native_render hook ignores the guest window token and passes the real
 * X11/Wayland drawable to libEGL.  No surface object is fabricated here. */
static void *fs_hook_create_surface(void *dpy, void *cfg,
                                    void *win, const int *attribs)
{
    uintptr_t ra = (uintptr_t)__builtin_return_address(0);
    fs_create_window_surface_fn create;
    void *real = NULL;

    fs_bump(&s_create_calls);
    if (fs_in_surface_helper(ra)) fs_bump(&s_create_from_helper);
    if (s_base && (uintptr_t)win == fs_window_global())
        fs_bump(&s_create_window_matches);

    if (s_create_fn) {
        create = (fs_create_window_surface_fn)(uintptr_t)s_create_fn;
        real = create(dpy, cfg, win, attribs);
    }
    if (!real && &sober_fn_eglCreateWindowSurface != NULL &&
        sober_fn_eglCreateWindowSurface != NULL) {
        create = (fs_create_window_surface_fn)(uintptr_t)
                 sober_fn_eglCreateWindowSurface;
        real = create(dpy, cfg, win, attribs);
    }
    if (!real) {
        /* Last resort: the original guest resolver (real host EGL via the
         * shim).  Forward unchanged; a NULL here is an honest failure. */
        create = (fs_create_window_surface_fn)(uintptr_t)s_got_old[1];
        if (create) real = create(dpy, cfg, win, attribs);
    }
    if (real) fs_bump(&s_create_ok);
    return real;
}

/* ---- small helpers ------------------------------------------------------- */
static int fs_env_int(const char *name, int fallback)
{
    const char *v = getenv(name);
    if (!v || !*v) return fallback;
    return (int)strtol(v, NULL, 10);
}

/* ---- public: gate -------------------------------------------------------- */
int sober_frame_surface_enabled(void)
{
    const char *e = getenv(SOBER_FRAME_SURFACE_ENV);
    /* DEFAULT ON: only the exact string "0" kills it. */
    return !(e != NULL && strcmp(e, "0") == 0);
}

const char *sober_frame_surface_env_vars(void)
{
    return SOBER_FRAME_SURFACE_ENV
           "=0 (kill switch; default ON - GOT hooks 0x72294a8/0x722a178, "
           "guest bind 0x26fea5a, config 0x649cf72, surface 0x649d1c2, "
           "swap 0x649d422)";
}

/* ---- public: install ----------------------------------------------------- */
int sober_frame_surface_install(const struct sober_frame_surface_config *cfg)
{
    uintptr_t slot;
    int w, h;

    if (!sober_frame_surface_enabled()) {
        s_last_rc = SOBER_FRAME_SURFACE_DISABLED;
        return SOBER_FRAME_SURFACE_DISABLED;
    }
    if (!cfg || !cfg->guest_base || !cfg->context) {
        s_last_rc = SOBER_FRAME_SURFACE_ERR_ARG;
        return SOBER_FRAME_SURFACE_ERR_ARG;
    }
    if (s_installed) {
        s_last_rc = SOBER_FRAME_SURFACE_OK;
        return SOBER_FRAME_SURFACE_OK;
    }
    if (!fs_engine_identity_ok((uintptr_t)cfg->guest_base)) {
        s_last_rc = SOBER_FRAME_SURFACE_ERR_IDENTITY;
        return SOBER_FRAME_SURFACE_ERR_IDENTITY;
    }

    s_base = (uintptr_t)cfg->guest_base;
    s_ctx = cfg->context;

    /* No host fake vtable: only the compat ANativeWindow prefix is present. */
    s_win = (SoberFrameSurfaceWindow *)calloc(1u, sizeof(*s_win));
    if (!s_win) {
        s_last_rc = SOBER_FRAME_SURFACE_ERR_ARG;
        return SOBER_FRAME_SURFACE_ERR_ARG;
    }
    w = cfg->width > 0 ? cfg->width : fs_env_int("SOBER_WINDOW_WIDTH", 1280);
    h = cfg->height > 0 ? cfg->height : fs_env_int("SOBER_WINDOW_HEIGHT", 720);
    s_win->refs = 0x10000u;       /* survive every guest release/rebind */
    s_win->width = (int32_t)w;
    s_win->height = (int32_t)h;
    s_win->magic = 0x41574e57u;   /* 'AWNW' */
    s_win->native_window = cfg->native_window;

    /* Host nativeData: callbacks (+0x88/+0x90/+0x98/+0xa0/+0xb8) stay NULL.
     * +0x10 is the JNIEnv token handed to fromSurface by the guest thunks. */
    s_nd = (unsigned char *)calloc(1u, (size_t)SOBER_FRAME_SURFACE_ND_SIZE);
    if (!s_nd) {
        free(s_win);
        s_win = NULL;
        s_last_rc = SOBER_FRAME_SURFACE_ERR_ARG;
        return SOBER_FRAME_SURFACE_ERR_ARG;
    }
    *(void **)(s_nd + (size_t)SOBER_FRAME_SURFACE_ND_OFF_ENV) =
        (void *)s_env;
    /* Start with the guest-bound window field unset; the guest created/changed
     * thunks and fs_assert_window_field() populate it (see bind_native_data). */

    s_create_fn = cfg->egl_create_window_surface;
    s_makecurrent_fn = cfg->egl_make_current;
    s_token[0] = (char)(uintptr_t)s_win;

    /* G8 C3: register the exact Surface token with the real mNativeObject
     * provider.  The callback returns 0 until the GUEST has published the
     * window, so an early/unbound read fails closed. */
    (void)sober_fields_native_set_provider((void *)&s_token,
                                           fs_native_handle_cb, NULL);

    /* Install GOT hook 1: ANativeWindow_fromSurface. */
    slot = s_base + (uintptr_t)SOBER_FRAME_SURFACE_GOT_FROM_SURFACE;
    if (fs_got_write(slot, (uintptr_t)&fs_hook_from_surface,
                     &s_got_old[0]) != 0)
        goto got_fail;
    /* Install GOT hook 2: eglCreateWindowSurface. */
    slot = s_base + (uintptr_t)SOBER_FRAME_SURFACE_GOT_CREATE_SURFACE;
    if (fs_got_write(slot, (uintptr_t)&fs_hook_create_surface,
                     &s_got_old[1]) != 0) {
        (void)fs_got_write(s_base
            + (uintptr_t)SOBER_FRAME_SURFACE_GOT_FROM_SURFACE,
            s_got_old[0], NULL);
        goto got_fail;
    }

    s_installed = 1;
    s_last_rc = SOBER_FRAME_SURFACE_OK;
    return SOBER_FRAME_SURFACE_OK;

got_fail:
    free(s_nd);
    s_nd = NULL;
    free(s_win);
    s_win = NULL;
    s_last_rc = SOBER_FRAME_SURFACE_ERR_GOT;
    return SOBER_FRAME_SURFACE_ERR_GOT;
}

/* ---- G8 C3/C4 handle contract for the mNativeObject provider -------------- */
/* Registered with worker-1's #492 provider so GetLongField can return the
 * genuine GUEST-published window for the exact Surface token we hand to
 * 0x26fea5a.  Returns 0 (fail closed) before the guest bind or for any other
 * object.  No host pointer is fabricated. */
static int64_t fs_native_handle_cb(void *env, void *surface_obj, void *user)
{
    (void)env; (void)user;
    if (!sober_frame_surface_is_surface_token(surface_obj)) return 0;
    return (int64_t)(uintptr_t)sober_frame_surface_native_handle();
}

int sober_frame_surface_is_surface_token(void *obj)
{
    return sober_frame_surface_enabled() && obj == (void *)&s_token;
}

void *sober_frame_surface_native_handle(void)
{
    /* The real ANativeWindow our genuine fromSurface hook returns to the guest
     * and that the guest then publishes at 0x72a3308.  It must be available
     * DURING the fromSurface call (C3 reads mNativeObject at that moment),
     * i.e. before s_guest_store_seen is set afterwards.  C4/C5 still require
     * the guest's own ret/gen evidence, so this never sets window_bound alone. */
    if (!sober_frame_surface_enabled() || !s_installed || !s_win)
        return NULL;
    return (void *)s_win;
}

/* ---- public: guest-written window provenance ----------------------------- */
int sober_frame_surface_bind_guest_window(void)
{
    fs_bind_window_fn bind;

    if (!sober_frame_surface_enabled()) {
        s_last_rc = SOBER_FRAME_SURFACE_DISABLED;
        return SOBER_FRAME_SURFACE_DISABLED;
    }
    if (!s_installed || !s_win || !s_base) {
        s_last_rc = SOBER_FRAME_SURFACE_ERR_STATE;
        return SOBER_FRAME_SURFACE_ERR_STATE;
    }

    s_window_global_before = fs_window_global();
    s_gen_before = fs_window_generation();

    /* Genuine guest code performs the fromSurface call, generation increment
     * and global store.  This module never writes 0x72a3308 itself. */
    bind = (fs_bind_window_fn)(uintptr_t)(s_base
             + (uintptr_t)SOBER_FRAME_SURFACE_RVA_BIND_WINDOW);
    /* Third argument is the genuine FLog tag used by the guest's
     * "Created ANativeWindow ... (ID:n)" record at 0x26feaf7. */
    bind((void *)s_env, (void *)&s_token,
         "nativeAppBridgeV2UpdateSurfaceAppWithPlatformParams");
    fs_bump(&s_bind_helper_calls);

    s_window_global_after = fs_window_global();
    s_gen_after = fs_window_generation();

    if (s_window_global_after != (uintptr_t)s_win) {
        s_last_rc = SOBER_FRAME_SURFACE_ERR_WINDOW;
        return SOBER_FRAME_SURFACE_ERR_WINDOW;
    }
    if (!s_guest_store_seen) {
        if (s_gen_after <= s_gen_before ||
            atomic_load_explicit(&s_from_surface_bind_ra,
                                 memory_order_relaxed) == 0u) {
            s_last_rc = SOBER_FRAME_SURFACE_ERR_PROVENANCE;
            return SOBER_FRAME_SURFACE_ERR_PROVENANCE;
        }
        s_guest_store_seen = 1;
    } else if (atomic_load_explicit(&s_from_surface_bind_ra,
                                    memory_order_relaxed) == 0u) {
        s_last_rc = SOBER_FRAME_SURFACE_ERR_PROVENANCE;
        return SOBER_FRAME_SURFACE_ERR_PROVENANCE;
    }

    /* The nativeData window holder must follow the guest-bound window. */
    fs_assert_window_field();
    s_last_rc = SOBER_FRAME_SURFACE_OK;
    return SOBER_FRAME_SURFACE_OK;
}

/* ---- public: nativeData lifecycle + +0x140 maintenance ------------------- */
int sober_frame_surface_bind_native_data(void)
{
    fs_life4_fn created;
    fs_life7_fn changed;
    fs_life4_fn redraw;
    int w = s_win ? (int)s_win->width : 0;
    int h = s_win ? (int)s_win->height : 0;

    if (!sober_frame_surface_enabled()) {
        s_last_rc = SOBER_FRAME_SURFACE_DISABLED;
        return SOBER_FRAME_SURFACE_DISABLED;
    }
    if (!s_installed || !s_nd || !s_win || !s_base) {
        s_last_rc = SOBER_FRAME_SURFACE_ERR_STATE;
        return SOBER_FRAME_SURFACE_ERR_STATE;
    }
    if (!s_guest_store_seen) {
        s_last_rc = SOBER_FRAME_SURFACE_ERR_PROVENANCE;
        return SOBER_FRAME_SURFACE_ERR_PROVENANCE;
    }

    fs_assert_window_field();
    created = (fs_life4_fn)(uintptr_t)(s_base
                + (uintptr_t)SOBER_FRAME_SURFACE_RVA_LIFE_CREATED);
    created((void *)s_env, (void *)&s_token, (uintptr_t)s_nd,
            (void *)&s_token);
    fs_bump(&s_life[0]);
    fs_assert_window_field();

    changed = (fs_life7_fn)(uintptr_t)(s_base
                + (uintptr_t)SOBER_FRAME_SURFACE_RVA_LIFE_CHANGED);
    changed((void *)s_env, (void *)&s_token, (uintptr_t)s_nd,
            (void *)&s_token, w, h, 0);
    fs_bump(&s_life[1]);
    fs_assert_window_field();

    /* Redraw gates on +0x140 at 0x2a20a0c before it even reads +0x98;
     * re-assert after the call as well (P2 4.2 / follow-up). */
    redraw = (fs_life4_fn)(uintptr_t)(s_base
                + (uintptr_t)SOBER_FRAME_SURFACE_RVA_LIFE_REDRAW);
    redraw((void *)s_env, (void *)&s_token, (uintptr_t)s_nd,
           (void *)&s_token);
    fs_bump(&s_life[2]);
    fs_assert_window_field();

    if (*(volatile uintptr_t *)(s_nd
            + (uintptr_t)SOBER_FRAME_SURFACE_ND_OFF_WINDOW) !=
        (uintptr_t)s_win) {
        s_last_rc = SOBER_FRAME_SURFACE_ERR_WINDOW;
        return SOBER_FRAME_SURFACE_ERR_WINDOW;
    }
    s_last_rc = SOBER_FRAME_SURFACE_OK;
    return SOBER_FRAME_SURFACE_OK;
}

int sober_frame_surface_on_destroyed(void)
{
    fs_life3_fn destroyed;

    if (!sober_frame_surface_enabled()) {
        s_last_rc = SOBER_FRAME_SURFACE_DISABLED;
        return SOBER_FRAME_SURFACE_DISABLED;
    }
    if (!s_installed || !s_nd || !s_base) {
        s_last_rc = SOBER_FRAME_SURFACE_ERR_STATE;
        return SOBER_FRAME_SURFACE_ERR_STATE;
    }
    destroyed = (fs_life3_fn)(uintptr_t)(s_base
                + (uintptr_t)SOBER_FRAME_SURFACE_RVA_LIFE_DESTROYED);
    destroyed((void *)s_env, (void *)&s_token, (uintptr_t)s_nd);
    fs_bump(&s_life[3]);
    /* The destroy thunk zeroes +0x140 at 0x2a20a6a; restore it. */
    fs_assert_window_field();
    s_last_rc = SOBER_FRAME_SURFACE_OK;
    return SOBER_FRAME_SURFACE_OK;
}

/* ---- public: real EGLSurface binding via 0x649d1c2 / GOT 0x722a178 ------- */
int sober_frame_surface_bind_surface(void)
{
    uintptr_t ctx = (uintptr_t)s_ctx;
    uintptr_t display, eglctx;
    uintptr_t gw;
    fs_config_helper_fn cfg_helper;
    fs_surface_helper_fn surf_helper;
    fs_make_current_fn mc;
    void *cfg, *surf;
    int ok;

    if (!sober_frame_surface_enabled()) {
        s_last_rc = SOBER_FRAME_SURFACE_DISABLED;
        return SOBER_FRAME_SURFACE_DISABLED;
    }
    if (!s_installed || !s_ctx || !s_win) {
        s_last_rc = SOBER_FRAME_SURFACE_ERR_STATE;
        return SOBER_FRAME_SURFACE_ERR_STATE;
    }
    if (!s_guest_store_seen) {
        s_last_rc = SOBER_FRAME_SURFACE_ERR_PROVENANCE;
        return SOBER_FRAME_SURFACE_ERR_PROVENANCE;
    }

    /* The surface must come from the window the GUEST published. */
    gw = fs_window_global();
    if (gw != (uintptr_t)s_win) {
        s_last_rc = SOBER_FRAME_SURFACE_ERR_WINDOW;
        return SOBER_FRAME_SURFACE_ERR_WINDOW;
    }
    /* Genuine guest context only -- we never install a host vtable. */
    if (*(volatile uintptr_t *)(ctx
            + (uintptr_t)SOBER_FRAME_SURFACE_CTX_OFF_VPTR) !=
        s_base + (uintptr_t)SOBER_FRAME_SURFACE_RVA_VTABLE) {
        s_last_rc = SOBER_FRAME_SURFACE_ERR_CONTEXT;
        return SOBER_FRAME_SURFACE_ERR_CONTEXT;
    }
    /* Non-zero +0x10 is a shared parent -> guest would take the pbuffer path. */
    if (*(volatile uintptr_t *)(ctx
            + (uintptr_t)SOBER_FRAME_SURFACE_CTX_OFF_PARENT) != 0u) {
        s_last_rc = SOBER_FRAME_SURFACE_ERR_CONTEXT;
        return SOBER_FRAME_SURFACE_ERR_CONTEXT;
    }

    /* Mirror guest insn 0x649c2cf: ctx+0x18 = guest-bound ANativeWindow. */
    *(volatile uintptr_t *)(ctx
        + (uintptr_t)SOBER_FRAME_SURFACE_CTX_OFF_WINDOW) = gw;
    fs_assert_window_field();

    /* Genuine guest config helper 0x649cf72(ctx) -> EGLConfig. */
    cfg_helper = (fs_config_helper_fn)(uintptr_t)(s_base
                    + (uintptr_t)SOBER_FRAME_SURFACE_RVA_CONFIG_HELPER);
    cfg = cfg_helper((void *)ctx);
    fs_bump(&s_cfg_helper_calls);
    if (!cfg) {
        s_last_rc = SOBER_FRAME_SURFACE_ERR_CONFIG;
        return SOBER_FRAME_SURFACE_ERR_CONFIG;
    }
    s_cfg = cfg;

    /* Genuine guest surface helper 0x649d1c2(ctx, cfg) -> its eglCreate-
     * WindowSurface@plt reaches GOT 0x722a178, where our hook returns a real
     * EGLSurface from the host EGL driver. */
    surf_helper = (fs_surface_helper_fn)(uintptr_t)(s_base
                    + (uintptr_t)SOBER_FRAME_SURFACE_RVA_SURFACE_HELPER);
    surf = surf_helper((void *)ctx, cfg);
    fs_bump(&s_helper_calls);
    if (!surf) {
        s_last_rc = SOBER_FRAME_SURFACE_ERR_SURFACE;
        return SOBER_FRAME_SURFACE_ERR_SURFACE;
    }
    s_surface = surf;

    /* Mirror guest insn 0x649c3f6: ctx+0x28 = returned EGLSurface.  This is
     * exactly what the flat swap wrapper 0x649d422 reads. */
    *(volatile uintptr_t *)(ctx
        + (uintptr_t)SOBER_FRAME_SURFACE_CTX_OFF_SURFACE) = (uintptr_t)surf;
    fs_bump(&s_surface_bound);
    s_surface_ready = 1;

    /* Mirror guest insn 0x649c408: make the new surface current. */
    mc = (fs_make_current_fn)(uintptr_t)s_makecurrent_fn;
    if (!mc && &sober_fn_eglMakeCurrent != NULL &&
        sober_fn_eglMakeCurrent != NULL)
        mc = (fs_make_current_fn)(uintptr_t)sober_fn_eglMakeCurrent;
    if (mc) {
        display = *(volatile uintptr_t *)(ctx
                    + (uintptr_t)SOBER_FRAME_SURFACE_CTX_OFF_DISPLAY);
        eglctx = *(volatile uintptr_t *)(ctx
                    + (uintptr_t)SOBER_FRAME_SURFACE_CTX_OFF_CONTEXT);
        ok = mc((void *)display, surf, surf, (void *)eglctx);
        fs_bump(&s_makecurrent_calls);
        if (ok) {
            s_makecurrent_ok = 1;
            fs_bump(&s_makecurrent_success);
        } else {
            s_makecurrent_ok = 0;
            s_last_rc = SOBER_FRAME_SURFACE_ERR_MAKECURRENT;
            return SOBER_FRAME_SURFACE_ERR_MAKECURRENT;
        }
    }

    s_last_rc = SOBER_FRAME_SURFACE_OK;
    return SOBER_FRAME_SURFACE_OK;
}

int sober_frame_surface_run(const struct sober_frame_surface_config *cfg)
{
    int rc = sober_frame_surface_install(cfg);
    if (rc != SOBER_FRAME_SURFACE_OK) return rc;
    rc = sober_frame_surface_bind_guest_window();
    if (rc != SOBER_FRAME_SURFACE_OK) return rc;
    rc = sober_frame_surface_bind_native_data();
    if (rc != SOBER_FRAME_SURFACE_OK) return rc;
    return sober_frame_surface_bind_surface();
}

void sober_frame_surface_uninstall(void)
{
    if (!s_installed) return;
    (void)fs_got_write(s_base
        + (uintptr_t)SOBER_FRAME_SURFACE_GOT_FROM_SURFACE,
        s_got_old[0], NULL);
    (void)fs_got_write(s_base
        + (uintptr_t)SOBER_FRAME_SURFACE_GOT_CREATE_SURFACE,
        s_got_old[1], NULL);
    sober_fields_native_clear_provider();
    s_installed = 0;
    /* s_win / s_nd / s_ctx / s_surface are deliberately retained: guest code
     * may still hold them.  All counters and accessors remain valid. */
}

int sober_frame_surface_is_installed(void) { return s_installed; }

/* ---- public: verify ------------------------------------------------------ */
int sober_frame_surface_verify(char *buf, size_t cap)
{
    int rc = SOBER_FRAME_SURFACE_OK;

    if (!sober_frame_surface_enabled()) {
        rc = SOBER_FRAME_SURFACE_DISABLED;
    } else if (!s_installed) {
        rc = SOBER_FRAME_SURFACE_ERR_STATE;
    } else if (!fs_engine_identity_ok(s_base)) {
        rc = SOBER_FRAME_SURFACE_ERR_IDENTITY;
    } else if (!s_win || !s_ctx || !s_nd) {
        rc = SOBER_FRAME_SURFACE_ERR_ARG;
    } else if (!s_guest_store_seen ||
               fs_window_global() != (uintptr_t)s_win ||
               atomic_load_explicit(&s_from_surface_bind_ra,
                                    memory_order_relaxed) == 0u) {
        rc = SOBER_FRAME_SURFACE_ERR_PROVENANCE;
    } else if (*(volatile uintptr_t *)(s_nd
                    + (uintptr_t)SOBER_FRAME_SURFACE_ND_OFF_WINDOW) !=
               (uintptr_t)s_win) {
        rc = SOBER_FRAME_SURFACE_ERR_WINDOW;
    } else if (*(volatile uintptr_t *)((uintptr_t)s_ctx
                    + (uintptr_t)SOBER_FRAME_SURFACE_CTX_OFF_VPTR) !=
               s_base + (uintptr_t)SOBER_FRAME_SURFACE_RVA_VTABLE) {
        rc = SOBER_FRAME_SURFACE_ERR_CONTEXT;
    } else if (!s_surface_ready ||
               *(volatile uintptr_t *)((uintptr_t)s_ctx
                    + (uintptr_t)SOBER_FRAME_SURFACE_CTX_OFF_SURFACE) !=
               (uintptr_t)s_surface ||
               !s_surface) {
        rc = SOBER_FRAME_SURFACE_ERR_SURFACE;
    }

    if (buf && cap) {
        snprintf(buf, cap,
                 "verify=%s rc=%d enabled=%d installed=%d "
                 "guest_win=%#lx our_win=%p gen=%u->%u guest_ra=%llu "
                 "bind_ra=%llu nd140=%#lx ctx_vt=%#lx surface=%p "
                 "create=%llu/%llu helper=%llu reasserts=%llu",
                 rc == 0 ? "PASS" : "FAIL", rc,
                 sober_frame_surface_enabled(), s_installed,
                 (unsigned long)fs_window_global(), (void *)s_win,
                 s_gen_before, s_gen_after,
                 (unsigned long long)sober_frame_surface_from_surface_guest_ra(),
                 (unsigned long long)sober_frame_surface_from_surface_bind_ra(),
                 (unsigned long)(s_nd ? *(volatile uintptr_t *)(s_nd
                     + (uintptr_t)SOBER_FRAME_SURFACE_ND_OFF_WINDOW) : 0u),
                 (unsigned long)(s_ctx ? *(volatile uintptr_t *)s_ctx : 0u),
                 s_surface,
                 (unsigned long long)sober_frame_surface_create_surface_calls(),
                 (unsigned long long)sober_frame_surface_create_surface_ok(),
                 (unsigned long long)sober_frame_surface_surface_helper_calls(),
                 (unsigned long long)sober_frame_surface_window_field_reasserts());
    }
    s_last_rc = rc;
    return rc;
}

/* ---- public: state accessors --------------------------------------------- */
SoberFrameSurfaceWindow *sober_frame_surface_window(void) { return s_win; }
void *sober_frame_surface_context(void) { return s_ctx; }
void *sober_frame_surface_surface(void) { return s_surface; }
void *sober_frame_surface_egl_config(void) { return s_cfg; }
uintptr_t sober_frame_surface_base(void) { return s_base; }
void *sober_frame_surface_native_data(void) { return s_nd; }
uintptr_t sober_frame_surface_bound_window(void) { return fs_window_global(); }
uint32_t sober_frame_surface_generation_before(void) { return s_gen_before; }
uint32_t sober_frame_surface_generation_after(void) { return s_gen_after; }
int sober_frame_surface_guest_store_seen(void) { return s_guest_store_seen; }
int sober_frame_surface_surface_ready(void) { return s_surface_ready; }
int sober_frame_surface_last_rc(void) { return s_last_rc; }

/* ---- public: real-event counters ---------------------------------------- */
uint64_t sober_frame_surface_from_surface_calls(void)
{
    return atomic_load_explicit(&s_from_surface_calls, memory_order_relaxed);
}
uint64_t sober_frame_surface_from_surface_guest_ra(void)
{
    return atomic_load_explicit(&s_from_surface_guest_ra, memory_order_relaxed);
}
uint64_t sober_frame_surface_from_surface_bind_ra(void)
{
    return atomic_load_explicit(&s_from_surface_bind_ra, memory_order_relaxed);
}
uint64_t sober_frame_surface_create_surface_calls(void)
{
    return atomic_load_explicit(&s_create_calls, memory_order_relaxed);
}
uint64_t sober_frame_surface_create_surface_ok(void)
{
    return atomic_load_explicit(&s_create_ok, memory_order_relaxed);
}
uint64_t sober_frame_surface_create_from_helper(void)
{
    return atomic_load_explicit(&s_create_from_helper, memory_order_relaxed);
}
uint64_t sober_frame_surface_create_window_matches(void)
{
    return atomic_load_explicit(&s_create_window_matches, memory_order_relaxed);
}
uint64_t sober_frame_surface_bind_helper_calls(void)
{
    return atomic_load_explicit(&s_bind_helper_calls, memory_order_relaxed);
}
uint64_t sober_frame_surface_config_helper_calls(void)
{
    return atomic_load_explicit(&s_cfg_helper_calls, memory_order_relaxed);
}
uint64_t sober_frame_surface_surface_helper_calls(void)
{
    return atomic_load_explicit(&s_helper_calls, memory_order_relaxed);
}
uint64_t sober_frame_surface_surface_bound_count(void)
{
    return atomic_load_explicit(&s_surface_bound, memory_order_relaxed);
}
uint64_t sober_frame_surface_make_current_calls(void)
{
    return atomic_load_explicit(&s_makecurrent_calls, memory_order_relaxed);
}
uint64_t sober_frame_surface_make_current_ok(void)
{
    return atomic_load_explicit(&s_makecurrent_success, memory_order_relaxed);
}
uint64_t sober_frame_surface_lifecycle_calls(unsigned idx)
{
    if (idx >= 4u) return 0u;
    return atomic_load_explicit(&s_life[idx], memory_order_relaxed);
}
uint64_t sober_frame_surface_window_field_reasserts(void)
{
    return atomic_load_explicit(&s_reasserts, memory_order_relaxed);
}

/* ---- public: diagnostics ------------------------------------------------- */
const char *sober_frame_surface_describe(char *buf, size_t cap)
{
    if (!buf || cap == 0u) return "";
    snprintf(buf, cap,
             "enabled=%d installed=%d rc=%d base=%#lx "
             "win=%p refs=%u w=%d h=%d ctx=%p surface=%p cfg=%p "
             "guest_win=%#lx gen=%u->%u guest_store=%d ready=%d "
             "got_from=%#x got_create=%#x "
             "from=%llu guest_ra=%llu bind_ra=%llu "
             "create=%llu from_helper=%llu ok=%llu winmatch=%llu "
             "bind=%llu cfg=%llu helper=%llu bound=%llu "
             "mc=%llu/%llu life=%llu/%llu/%llu/%llu reasserts=%llu",
             sober_frame_surface_enabled(), s_installed, s_last_rc,
             (unsigned long)s_base, (void *)s_win,
             s_win ? (unsigned)s_win->refs : 0u,
             s_win ? (int)s_win->width : 0,
             s_win ? (int)s_win->height : 0,
             s_ctx, s_surface, s_cfg,
             (unsigned long)fs_window_global(), s_gen_before, s_gen_after,
             s_guest_store_seen, s_surface_ready,
             (unsigned)SOBER_FRAME_SURFACE_GOT_FROM_SURFACE,
             (unsigned)SOBER_FRAME_SURFACE_GOT_CREATE_SURFACE,
             (unsigned long long)sober_frame_surface_from_surface_calls(),
             (unsigned long long)sober_frame_surface_from_surface_guest_ra(),
             (unsigned long long)sober_frame_surface_from_surface_bind_ra(),
             (unsigned long long)sober_frame_surface_create_surface_calls(),
             (unsigned long long)sober_frame_surface_create_from_helper(),
             (unsigned long long)sober_frame_surface_create_surface_ok(),
             (unsigned long long)sober_frame_surface_create_window_matches(),
             (unsigned long long)sober_frame_surface_bind_helper_calls(),
             (unsigned long long)sober_frame_surface_config_helper_calls(),
             (unsigned long long)sober_frame_surface_surface_helper_calls(),
             (unsigned long long)sober_frame_surface_surface_bound_count(),
             (unsigned long long)sober_frame_surface_make_current_calls(),
             (unsigned long long)sober_frame_surface_make_current_ok(),
             (unsigned long long)sober_frame_surface_lifecycle_calls(0),
             (unsigned long long)sober_frame_surface_lifecycle_calls(1),
             (unsigned long long)sober_frame_surface_lifecycle_calls(2),
             (unsigned long long)sober_frame_surface_lifecycle_calls(3),
             (unsigned long long)sober_frame_surface_window_field_reasserts());
    return buf;
}

const char *sober_frame_surface_env_dump(void)
{
    return SOBER_FRAME_SURFACE_ENV
           "=0|1 (default ON; 0 kills both GOT hooks and all guest calls). "
           "Window provenance is guest-written by 0x26fea5a (RA 0x26fea7d, "
           "gen 0x72a3310, global 0x72a3308); this module never host-stores "
           "that global. nativeData+0x140 is re-asserted after bind/redraw "
           "and after 0x2a20a30 clears it at 0x2a20a6a.";
}

/* ---- public: self-test (no guest required, no real counters touched) ----- */
static int fs_selftest_identity(void)
{
    unsigned char img[0x400];
    unsigned char *ph, *note;
    uint32_t u32;
    uint16_t u16;
    uint64_t u64;

    memset(img, 0, sizeof(img));
    img[0] = 0x7f; img[1] = 'E'; img[2] = 'L'; img[3] = 'F';
    ph = img + 64;
    note = img + 0x200;
    u64 = 64u;   memcpy(img + 32, &u64, sizeof(u64));  /* e_phoff */
    u16 = 56u;   memcpy(img + 54, &u16, sizeof(u16));  /* e_phentsize */
    u16 = 1u;    memcpy(img + 56, &u16, sizeof(u16));  /* e_phnum */
    u32 = 4u;    memcpy(ph, &u32, sizeof(u32));        /* PT_NOTE */
    u64 = 0x200u; memcpy(ph + 16, &u64, sizeof(u64));  /* p_vaddr */
    u64 = 0x40u;  memcpy(ph + 32, &u64, sizeof(u64));  /* p_filesz */
    u32 = 4u; memcpy(note,      &u32, sizeof(u32));    /* namesz */
    u32 = 4u; memcpy(note + 4,  &u32, sizeof(u32));    /* descsz */
    u32 = 3u; memcpy(note + 8,  &u32, sizeof(u32));    /* NT_GNU_BUILD_ID */
    memcpy(note + 12, "GNU", 4u);
    memcpy(note + 16, "\x20\x4a\xd0\x49", 4);
    if (!fs_engine_identity_ok((uintptr_t)img)) return 0;
    memcpy(note + 16, "\xde\xad\xbe\xef", 4);
    if (fs_engine_identity_ok((uintptr_t)img)) return 0;
    return 1;
}

static void fs_selftest_reassert(unsigned char *nd, uintptr_t win,
                                 uint64_t *counter)
{
    uintptr_t cur = *(volatile uintptr_t *)(nd
                        + (uintptr_t)SOBER_FRAME_SURFACE_ND_OFF_WINDOW);
    if (cur != win) {
        *(volatile uintptr_t *)(nd
            + (uintptr_t)SOBER_FRAME_SURFACE_ND_OFF_WINDOW) = win;
        ++*counter;
    }
}

int sober_frame_surface_selftest(char *buf, size_t cap)
{
    int fails = 0;
    unsigned char scratch[SOBER_FRAME_SURFACE_ND_SIZE];
    uint64_t n = 0u;
    uintptr_t win = (uintptr_t)&scratch;

    /* RVA invariants. */
    if (SOBER_FRAME_SURFACE_GOT_FROM_SURFACE == 0u ||
        SOBER_FRAME_SURFACE_GOT_CREATE_SURFACE == 0u ||
        SOBER_FRAME_SURFACE_GOT_FROM_SURFACE
            == SOBER_FRAME_SURFACE_GOT_CREATE_SURFACE) ++fails;
    if (SOBER_FRAME_SURFACE_RVA_SURFACE_HELPER != 0x649d1c2u) ++fails;
    if (SOBER_FRAME_SURFACE_GOT_CREATE_SURFACE != 0x722a178u) ++fails;
    if (SOBER_FRAME_SURFACE_RVA_G_WINDOW != 0x72a3308u) ++fails;
    if (SOBER_FRAME_SURFACE_RVA_G_WINDOW_GEN != 0x72a3310u) ++fails;
    if (SOBER_FRAME_SURFACE_DESTROY_FIELD_CLEAR != 0x2a20a6au) ++fails;
    if (SOBER_FRAME_SURFACE_REDRAW_FIELD_CHECK != 0x2a20a0cu) ++fails;
    if (SOBER_FRAME_SURFACE_ND_OFF_WINDOW != 0x140u) ++fails;
    if (SOBER_FRAME_SURFACE_CTX_OFF_SURFACE != 0x28u) ++fails;
    /* .text must cover every code RVA (GOT slots live past .text, so compare
     * against the highest code anchor, not the GOT). */
    if (SOBER_FRAME_SURFACE_TEXT_SIZE <=
        SOBER_FRAME_SURFACE_RVA_BIND_WINDOW) ++fails;
    if (SOBER_FRAME_SURFACE_TEXT_SIZE <=
        SOBER_FRAME_SURFACE_RVA_SURFACE_HELPER) ++fails;
    if (!fs_selftest_identity()) ++fails;

    /* The +0x140 re-assert mechanism: only a real clear bumps the counter. */
    memset(scratch, 0, sizeof(scratch));
    fs_selftest_reassert(scratch, win, &n);
    if (n != 1u) ++fails;
    if (*(uintptr_t *)(scratch + (uintptr_t)SOBER_FRAME_SURFACE_ND_OFF_WINDOW)
        != win) ++fails;
    fs_selftest_reassert(scratch, win, &n);
    if (n != 1u) ++fails;                     /* idempotent, no fake bump */
    *(uintptr_t *)(scratch
        + (uintptr_t)SOBER_FRAME_SURFACE_ND_OFF_WINDOW) = 0u; /* 0x2a20a6a */
    fs_selftest_reassert(scratch, win, &n);
    if (n != 2u) ++fails;

    if (buf && cap) {
        snprintf(buf, cap,
                 "selftest=%s fails=%d enabled=%d gate=%s reasserts=%llu "
                 "got_create=%#x surface_helper=%#x window_global=%#x",
                 fails == 0 ? "PASS" : "FAIL", fails,
                 sober_frame_surface_enabled(), SOBER_FRAME_SURFACE_ENV,
                 (unsigned long long)n,
                 (unsigned)SOBER_FRAME_SURFACE_GOT_CREATE_SURFACE,
                 (unsigned)SOBER_FRAME_SURFACE_RVA_SURFACE_HELPER,
                 (unsigned)SOBER_FRAME_SURFACE_RVA_G_WINDOW);
    }
    return fails;
}
