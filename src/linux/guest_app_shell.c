/*
 * guest_app_shell.c - Phase 4 JNI bridge + app-shell startup (worker-3, #385).
 *
 * All RVAs are from analysis/jni/jni-map.md, AppImage BuildID 204ad049599...
 * Per lead's DECISION 013: the "app shell rendered" marker is emitted ONLY after
 * a genuine guest->Java JNI callback has been observed AND the phase-3 guest
 * render path is known to have run. A weak diagnostic mode is opt-in and never
 * emits the real marker.
 */
#define _GNU_SOURCE
#include <signal.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <stdlib.h>
#include <unistd.h>
#include "guest_app_shell.h"
#include "bionic_bootstrap.h"
#include "native_jni.h"
#include "javavm_semantics.h"
#include "jni_json_bridge.h"
#include "jni_native_objects.h"
#include "global_string_seed.h"
#include "startapp_params.h"
#include "app_singleton_bridge.h"
#include "game_params.h"
#include "startgame_v2.h"
#include "jni_strings.h"
#include "startapp_v2.h"
#include "app488_fix.h"
#include "notifier_probe.h"
#include "app488_inner.h"
#include "jni_arrays.h"
#include "singleton_services.h"
#include "singleton_fix.h"
#include "native_render.h"
#include "android_compat.h"

/* ---- DEFAULT-ON module APIs (task #477) --------------------------------
 * Implementations live in libsober_jni.a (src/jni/CMakeLists.txt, worker-1).
 * Do NOT #include the .c files here: that would create a second copy of
 * each module's state (notably provenance_gate), so the dispatch hook in
 * native_jni.c and this gate would consult different instances.
 */
#include "tsd_default.h"
#include "avd_boundary.h"
#include "hw_breakpoint.h"
#include "fields_default.h"
#include "javavm_default.h"
#include "fflag_default.h"
#include "scheduler_guest.h"
#include "singleton_default.h"
#include "app488_default.h"
#include "delegate_default.h"
#include "controller_default.h"
#include "placeid_default.h"
#include "publish_default.h"
#include "render_guest.h"
#include "provenance_gate.h"
#include "frame_evidence.h"
#include "frame_dispatch.h"
#include "frame_draw.h"
#include "frame_surface.h"
#include "frame_thread.h"
#include "frame_synthetic_tag.h"
#include "frame_callbacks.h"
#include "flags_latch_default.h"
#include "crashpad_return.h"

/* Checkpoint ladder (AVD-captured references only; see analysis/CHECKPOINT-LADDER.md).
 * Rung 3 is intentionally undefined: no authenticated AVD capture exists. */
static int g_ck_rung1_returned;
/* W32: read-only latch samples around nativeInitClientSettings (no writes). */
int g_w32_latch_before = -1, g_w32_latch_after = -1;
extern int g_w29_arg0_sha_ok;
static int g_ck_rung2_app_start;
static int g_ck_rung2_v2_init;
static int g_ck_rung2_lua_dm;

/* Gated crashpoint diagnostic: report the guest latch when a guest path aborts
 * inside the crashpad writer, without changing any guest state. */
static uintptr_t g_abrt_base;
static void sober_abrt_diag(int sig)
{
    (void)sig;
    if (g_abrt_base)
        fprintf(stderr, "[abrt-diag] latch=%u counter=%#x\n",
                (unsigned)*(volatile unsigned char *)(g_abrt_base + 0x7cdd149u),
                (unsigned)*(volatile unsigned char *)(g_abrt_base + 0x72a3710u));
    _exit(134);
}

#include <dlfcn.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

static void emit_marker(const char *text, const char *env_name)
{
    ssize_t n = write(STDOUT_FILENO, text, strlen(text));
    (void)n;
    const char *path = getenv(env_name);
    if (path) {
        int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) {
            ssize_t m = write(fd, text, strlen(text));
            (void)m;
            close(fd);
        }
    }
}

/* The guest's pthread_getspecific@@LIBC is backed by the versioned stub's own
 * 512-slot TSD array (worker-2's Bionic fix), whereas worker-2's seed helper
 * calls glibc's pthread_setspecific -- a different store. Seed the STUB's TSD
 * explicitly using its versioned pthread_setspecific@@LIBC. */
/* n1-4: read-only obj+0x28 sampler.  Calls the genuine guest slot getter
 * 0x2a0de60 (same one the predicate uses) via the host bridge, then reads
 * slot -> obj -> obj+0x28 with process_vm_readv.  No guest writes. */
static long n14_readmem(uintptr_t addr, void *dst, size_t len)
{
    struct iovec li = { dst, len }, ri = { (void *)addr, len };
    return process_vm_readv((pid_t)getpid(), &li, 1UL, &ri, 1UL, 0UL);
}
/* n1-5 READ-ONLY closure scan.  Contract (static-A #656 / static-B #653):
 * a genuine closure/task pointer is canonical after masking 0xffffffffffff;
 * the object it points to has fn ptr at +0 and ctx ptr at +8.  Search qwords
 * already present in the live obj (esp. +0x40/+0x68 holders).  No writes. */
static void n15_scan_closures(uintptr_t base)
{
    uintptr_t (*slot_getter)(void) = (uintptr_t (*)(void))(base + 0x2a0de60u);
    uintptr_t slot = slot_getter(), obj = 0;
    if (n14_readmem(slot, &obj, 8u) != 8 || !obj) {
        fprintf(stderr, "[N15_SCAN] slot=%p obj UNREADABLE\n", (void *)slot);
        return;
    }
    fprintf(stderr, "[N15_SCAN] obj=%p scanning qwords obj-0x2000..obj+0x2000 tid=%ld\n",
            (void *)obj, (long)getpid());
    uintptr_t fn_cand = 0, ctx_cand = 0;   /* filled by candidate below */
    /* Read-only mapping provenance for any candidate region. */
    {
        FILE *m = fopen("/proc/self/maps", "r");
        if (m) {
            char ln[512];
            while (fgets(ln, sizeof(ln), m)) {
                unsigned long lo, hi;
                if (sscanf(ln, "%lx-%lx", &lo, &hi) != 2) continue;
                if ((unsigned long)obj >= lo && (unsigned long)obj < hi)
                    fprintf(stderr, "[N15_MAP obj] %s", ln);
                if ((unsigned long)fn_cand >= lo && (unsigned long)fn_cand < hi)
                    fprintf(stderr, "[N15_MAP fn] %s", ln);
                if ((unsigned long)ctx_cand >= lo && (unsigned long)ctx_cand < hi)
                    fprintf(stderr, "[N15_MAP ctx] %s", ln);
            }
            fclose(m);
        }
    }
    for (int64_t off = -0x2000; off < 0x2000; off += 8) {
        uintptr_t v = 0;
        if (n14_readmem((uintptr_t)((int64_t)obj + off), &v, 8u) != 8) continue;
        uintptr_t masked = v & 0xffffffffffffULL;
        if (masked < 0x10000u) continue;
        uintptr_t fn = 0, ctx = 0;
        long g1 = n14_readmem(masked, &fn, 8u);
        long g2 = n14_readmem(masked + 8u, &ctx, 8u);
        uintptr_t fnm = fn & 0xffffffffffffULL, ctxm = ctx & 0xffffffffffffULL;
        int fn_ok = (g1 == 8 && fnm >= 0x10000u);
        int ctx_ok = (g2 == 8 && ctxm >= 0x10000u);
        if (fn_ok && ctx_ok) {
            fn_cand = fn; ctx_cand = ctx;
            fprintf(stderr,
                    "[N15_CLOSURE] obj%+lld raw=%p masked=%p fn=%p ctx=%p\n",
                    (long long)off, (void *)v, (void *)masked,
                    (void *)fn, (void *)ctx);
        }
    }
    {
        FILE *m = fopen("/proc/self/maps", "r");
        if (m) {
            char ln[512];
            while (fgets(ln, sizeof(ln), m)) {
                unsigned long lo, hi;
                if (sscanf(ln, "%lx-%lx", &lo, &hi) != 2) continue;
                if (fn_cand && (unsigned long)fn_cand >= lo &&
                    (unsigned long)fn_cand < hi)
                    fprintf(stderr, "[N15_MAP fn] %s", ln);
                if (ctx_cand && (unsigned long)ctx_cand >= lo &&
                    (unsigned long)ctx_cand < hi)
                    fprintf(stderr, "[N15_MAP ctx] %s", ln);
            }
            fclose(m);
        }
    }
}

static void n14_log_guard(uintptr_t base, const char *tag)
{
    uintptr_t (*slot_getter)(void) =
        (uintptr_t (*)(void))(base + 0x2a0de60u);
    uintptr_t slot = slot_getter();
    uintptr_t obj = 0; uint64_t guard = 0;
    long g1 = n14_readmem(slot, &obj, 8u);
    long g2 = g1 == 8 ? n14_readmem(obj + 0x28u, &guard, 8u) : -1;
    fprintf(stderr, "[N14_GUARD %s] slot=%p obj=%p guard=%s%016lx tid=%ld\n",
            tag, (void *)slot, (void *)obj,
            g2 == 8 ? "" : "UNREADABLE-", (unsigned long)guard,
            (long)getpid());
}

uintptr_t g_w29_tsd_state;   /* read-only by the W29 perf trace */
static void seed_stub_tsd(uintptr_t base)
{
    /* W30: REMOVED by default.  This TSD seed is harness-fabricated state and
     * caused the 0x6ca229c fault (see ledger).  Only an explicit diagnostic
     * opt-in can re-enable it; Run D never does. */
    (void)base;
    if (!getenv("SOBER_W30_TSD_SEED_OPTIN")) {
        fprintf(stderr, "[app-shell] W30 TSD seed REMOVED (default OFF; "
                        "harness-fabricated state)\n");
        return;
    }
    void *stub = dlopen("libc.so", RTLD_NOLOAD | RTLD_LAZY);
    if (!stub) {
        fprintf(stderr, "[app-shell] stub libc.so handle not found: %s\n", dlerror());
        return;
    }
    int (*stub_setspecific)(unsigned long, void *) =
        (int (*)(unsigned long, void *))
        dlvsym(stub, "pthread_setspecific", "LIBC");
    if (!stub_setspecific) {
        fprintf(stderr, "[app-shell] stub pthread_setspecific@@LIBC not found\n");
        return;
    }
    uint32_t key = 0;
    memcpy(&key, (const void *)(base + 0x72a1b08u), sizeof(key));
    /* Must be a real heap allocation: guest TLS accessor 0x2ea6440 reallocs
     * this buffer. A static/BSS address makes glibc abort with
     * "realloc(): invalid pointer". */
    unsigned char *state = (unsigned char *)calloc(1, 65536);
    if (!state) {
        fprintf(stderr, "[app-shell] stub-TSD seed: calloc failed\n");
        return;
    }
    g_w29_tsd_state = (uintptr_t)state;
    int rc = stub_setspecific((unsigned long)key, state);
    fprintf(stderr,
            "[app-shell] stub-TSD seed: key=%u state=%p [state+0]=%#lx rc=%d\n",
            (unsigned)key, (void *)state,
            (unsigned long)*(unsigned long *)state, rc);
}

/* Native guest patches (DECISIONS 014/015). Verify the original bytes before
 * writing, mprotect RWX -> write -> restore RX, like the phase-2 bridges. */
static int patch_guest_bytes(uintptr_t base, uintptr_t rva,
                             const unsigned char *expect,
                             const unsigned char *patch, size_t len,
                             const char *label)
{
    uintptr_t site = base + rva;
    if (memcmp((const void *)site, expect, len) != 0) {
        fprintf(stderr, "[app-shell] %s prologue mismatch at 0x%lx; refusing to patch\n",
                label, (unsigned long)rva);
        return -1;
    }
    long ps = sysconf(_SC_PAGESIZE);
    if (ps <= 0) ps = 4096;
    uintptr_t page = site & ~((uintptr_t)ps - 1);
    if (mprotect((void *)page, (size_t)ps,
                 PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        fprintf(stderr, "[app-shell] %s mprotect failed\n", label);
        return -1;
    }
    memcpy((void *)site, patch, len);
    __builtin___clear_cache((char *)site, (char *)site + len);
    mprotect((void *)page, (size_t)ps, PROT_READ | PROT_EXEC);
    fprintf(stderr, "[app-shell] patched guest 0x%lx (%s)\n",
            (unsigned long)rva, label);
    return 0;
}

static int patch_guest_validator(uintptr_t base)
{
    static const unsigned char expect[6] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57};
    static const unsigned char patch[6]  = {0xb8, 0x04, 0x00, 0x00, 0x00, 0xc3};
    return patch_guest_bytes(base, 0x245605du, expect, patch, sizeof(patch),
                             "validator -> mov eax,4; ret");
}

/* Final authorized experiment (worker-1): leaf-return the validator/teardown
 * consumer at 0x233319a so it never dereferences the guest-internal object's
 * bogus +0x68 field. Opt-in via SOBER_FINAL_PATCH=1. */
static int patch_final_consumer(uintptr_t base)
{
    static const unsigned char expect[3] = {0x48, 0x85, 0xff};
    static const unsigned char patch[3] = {0x31, 0xc0, 0xc3};
    return patch_guest_bytes(base, 0x233319au, expect, patch, sizeof(patch),
                             "consumer -> xor eax,eax; ret");
}

/* Called by worker-2's dispatcher when the guest invokes a cached jmethodID. */
static intptr_t shell_callback(void *env, void *obj, const char *name,
                               const char *sig, void *args, void *user)
{
    (void)env; (void)obj; (void)sig; (void)args; (void)user;
    fprintf(stderr, "[app-shell] guest->Java callback: %s\n", name);
    return 0;
}

/* The guest invokes the Java Runnable handed to nativeScheduleOnFirstFrame
 * when the first frame is dispatched.  Counting it proves the frame task ran
 * (it is not one of the app-shell completion callbacks). */
static intptr_t run_callback(void *env, void *obj, const char *name,
                             const char *sig, void *args, void *user)
{
    (void)env; (void)obj; (void)sig; (void)args; (void)user;
    fprintf(stderr, "[app-shell] guest->Java callback: %s%s (first-frame task)\n",
            name, sig ? sig : "");
    return 0;
}

/* StartAppParams accessors called by nativeAppBridgeV2StartAppWithParams.
 * The guest is the caller; the bridge supplies shaped Java objects. */
static intptr_t start_app_get(void *env, void *obj, const char *name,
                              const char *sig, void *args, void *user)
{
    (void)env; (void)obj; (void)sig; (void)args;
    SoberJni *j = (SoberJni *)user;
    if (!name) return 0;
    if (!strcmp(name, "surface")) {
        void *o = sober_jni_new_native_object(j, "android/view/Surface", 16, NULL);
        fprintf(stderr, "[app-shell] guest->Java callback: surface() -> %p\n", o);
        return (intptr_t)o;
    }
    if (!strcmp(name, "platformParams")) {
        void *o = sober_jni_new_native_object(j,
            "com/roblox/engine/jni/model/PlatformParams", 0x100, NULL);
        fprintf(stderr, "[app-shell] guest->Java callback: platformParams() -> %p\n", o);
        return (intptr_t)o;
    }
    if (!strcmp(name, "vrContext")) {
        void *o = sober_jni_new_native_object(j, "android/app/Activity", 16, NULL);
        fprintf(stderr, "[app-shell] guest->Java callback: vrContext() -> %p\n", o);
        return (intptr_t)o;
    }
    return 0;
}

/* JNI-style call: JNICALL(JNIEnv *env, jclass clazz) -> integer/pointer. */
static long call_jni_static(uintptr_t fn, void *env, void *clazz)
{
    return ((long (*)(void *, void *))fn)(env, clazz);
}

/* Any env var with one of these prefixes selects fake values for a
 * withheld guest-object module. Checked by prefix so future value overrides
 * cannot silently escape the decision-013 withhold chain (D02 Tier 2). */
extern char **environ;
static int env_prefix_active(const char *prefix)
{
    size_t n = strlen(prefix);
    for (char **e = environ; e && *e; ++e)
        if (!strncmp(*e, prefix, n) && (*e)[n] == '=') return 1;
    return 0;
}

/* ------------------------------------------------------------------ */
/* DECISION 023: targeted field-normaliser stub (OPT-IN)               */
/* ------------------------------------------------------------------ */
/* Native leaf stub replacing guest 0x23331ca..0x23331d0 (7 bytes):
 *   original: mov 0x68(%rbx),%rdi ; test %rdi,%rdi
 *   we load the same field, then zero it if it is non-canonical, and set
 *   flags with a final `test %rdi,%rdi` so the guest's `je 0x23331dd`
 *   at 0x23331d1 takes the safe null path. RBX is preserved; RAX is
 *   caller-saved and reloaded by the guest at 0x23331dd. */
__asm__(
    ".globl sober_field_normaliser\n\t"
    ".type sober_field_normaliser, @function\n\t"
    "sober_field_normaliser:\n\t"
    /* normalise stale code bytes in [rbx+0x20] (guest null-checks it next) */
    "mov 0x20(%rbx), %rax\n\t"
    "test %rax, %rax\n\t"
    "jz 2f\n\t"
    "shr $48, %rax\n\t"
    "test %rax, %rax\n\t"
    "jz 2f\n\t"
    "movq $0, 0x20(%rbx)\n\t"
    "2:\n\t"
    /* normalise [rbx+0x68] into RDI; set flags for guest's `je`. */
    "mov 0x68(%rbx), %rdi\n\t"
    "mov %rdi, %rax\n\t"
    "shr $48, %rax\n\t"
    "test %rax, %rax\n\t"
    "jz 3f\n\t"
    "xor %edi, %edi\n\t"
    "3:\n\t"
    "test %rdi, %rdi\n\t"
    "ret\n\t"
    ".size sober_field_normaliser, .-sober_field_normaliser\n\t"
    ".globl sober_field_normaliser_end\n\t"
    "sober_field_normaliser_end:\n\t"
);
extern void sober_field_normaliser(void);
extern char sober_field_normaliser_end;

static int patch_field_normaliser(uintptr_t base)
{
    static const unsigned char expect[7] = {
        0x48, 0x8b, 0x7b, 0x68, 0x48, 0x85, 0xff};
    uintptr_t site = base + 0x23331cau;
    if (memcmp((const void *)site, expect, sizeof(expect)) != 0) {
        fprintf(stderr, "[app-shell] field-normaliser prologue mismatch at "
                        "0x23331ca; refusing to patch\n");
        return -1;
    }
    long ps = sysconf(_SC_PAGESIZE);
    if (ps <= 0) ps = 4096;
    uintptr_t page = site & ~((uintptr_t)ps - 1);
    if (mprotect((void *)page, (size_t)ps,
                 PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        fprintf(stderr, "[app-shell] field-normaliser mprotect failed\n");
        return -1;
    }
    /* The harness PIE and the guest mapping are typically >2 GiB apart, so a
     * direct call rel32 cannot reach our stub. Place a COPY of the tiny leaf
     * stub in an executable page near the guest, then call that. */
    uintptr_t hint = base - 0x08000000u;   /* 128 MiB below the guest */
    void *near = mmap((void *)hint, 4096, PROT_READ | PROT_WRITE | PROT_EXEC,
                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (near == MAP_FAILED) {
        /* hint unavailable: let the kernel choose and verify reachability. */
        near = mmap(NULL, 4096, PROT_READ | PROT_WRITE | PROT_EXEC,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (near == MAP_FAILED) {
            mprotect((void *)page, (size_t)ps, PROT_READ | PROT_EXEC);
            fprintf(stderr, "[app-shell] near-stub mmap FAILED\n");
            return -1;
        }
    }
    size_t stub_len = (size_t)(&sober_field_normaliser_end -
                               (char *)&sober_field_normaliser);
    memcpy(near, (void *)&sober_field_normaliser, stub_len);
    __builtin___clear_cache((char *)near, (char *)near + stub_len);

    int64_t wide = (int64_t)((uintptr_t)near - (site + 5));
    if (wide < INT32_MIN || wide > INT32_MAX) {
        fprintf(stderr, "[app-shell] near stub %p unreachable from 0x23331ca "
                        "(disp=%lld)\n", near, (long long)wide);
        mprotect((void *)page, (size_t)ps, PROT_READ | PROT_EXEC);
        return -1;
    }
    int32_t disp = (int32_t)wide;
    unsigned char patch[7];
    patch[0] = 0xe8;  /* call rel32 */
    memcpy(patch + 1, &disp, sizeof(disp));
    patch[5] = 0x90; patch[6] = 0x90;
    memcpy((void *)site, patch, sizeof(patch));
    __builtin___clear_cache((char *)site, (char *)site + sizeof(patch));
    mprotect((void *)page, (size_t)ps, PROT_READ | PROT_EXEC);
    fprintf(stderr, "[app-shell] DECISION 023 field-normaliser installed at "
                    "0x23331ca -> near stub %p (disp=%d, %zu bytes)\n",
            near, (int)disp, stub_len);
    return 0;
}

/* ------------------------------------------------------------------ */
/* DECISION 024: object-level fix at the load site 0x23331ae          */
/* ------------------------------------------------------------------ */
/* Stub built at runtime so the substituted-object address is embedded as
 * an immediate (RIP-relative won't survive copying into the near page). */
static unsigned char *build_object_fix_stub(uintptr_t substitute)
{
    static unsigned char stub[64] = {
        0x48, 0x8b, 0x1f,                   /* 0: mov (%rdi),%rbx          */
        0x48, 0x89, 0xd8,                   /* 3: mov %rbx,%rax            */
        0x48, 0xc1, 0xe8, 0x30,             /* 6: shr $48,%rax             */
        0x48, 0x85, 0xc0,                   /*10: test %rax,%rax           */
        0x75, 0x0c,                         /*13: jnz 27 (bad)             */
        0x81, 0x3b, 0xad, 0xde, 0xed, 0xc0, /*15: cmpl $0xc0dedbad,(%rbx)  */
        0x75, 0x04,                         /*21: jne 27 (bad)             */
        0x48, 0x85, 0xdb,                   /*23: test %rbx,%rbx           */
        0xc3,                               /*26: ret                      */
        0x48, 0xbb, 0,0,0,0,0,0,0,0,        /*27: movabs $substitute,%rbx  */
        0x48, 0x85, 0xdb,                   /*37: test %rbx,%rbx           */
        0xc3,                               /*40: ret                      */
    };
    memcpy(stub + 29, &substitute, sizeof(substitute));
    return stub;
}

static int patch_object_fix(uintptr_t base)
{
    /* Allocate through the guest's OWN patched allocator bridge so the guest's
     * free bridge (0x1dbe559) can release it. 0x2000 leaves room for the
     * guest's writes at +0x1005/+0x1006 without corrupting the chunk header. */
    static const unsigned char expect[6] = {
        0x48, 0x8b, 0x1f, 0x48, 0x85, 0xdb};  /* mov (%rdi),%rbx; test */
    uintptr_t site = base + 0x23331aeu;
    if (memcmp((const void *)site, expect, sizeof(expect)) != 0) {
        fprintf(stderr, "[app-shell] object-fix prologue mismatch at 0x23331ae\n");
        return -1;
    }
    /* Build the minimal valid substitute object (worker-1's layout). */
    void *(*guest_alloc)(size_t) = (void *(*)(size_t))(base + 0x1dbab02u);
    unsigned char *obj = (unsigned char *)guest_alloc(0x2000);
    if (!obj) return -1;
    memset(obj, 0, 0x2000);
    uint32_t magic = 0xc0dedbadu;
    memcpy(obj, &magic, sizeof(magic));
    obj[0x1006] = 0x02;   /* skips the validator at 0x23331c1 */

    unsigned char *stub = build_object_fix_stub((uintptr_t)obj);
    long ps = sysconf(_SC_PAGESIZE);
    if (ps <= 0) ps = 4096;
    uintptr_t hint = base - 0x08000000u;
    void *near = mmap((void *)hint, 4096, PROT_READ | PROT_WRITE | PROT_EXEC,
                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (near == MAP_FAILED)
        near = mmap(NULL, 4096, PROT_READ | PROT_WRITE | PROT_EXEC,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (near == MAP_FAILED) { free(obj); return -1; }
    memcpy(near, stub, 41);
    __builtin___clear_cache((char *)near, (char *)near + 41);

    int64_t wide = (int64_t)((uintptr_t)near - (site + 5));
    if (wide < INT32_MIN || wide > INT32_MAX) { free(obj); return -1; }
    int32_t disp = (int32_t)wide;
    uintptr_t page = site & ~((uintptr_t)ps - 1);
    if (mprotect((void *)page, (size_t)ps,
                 PROT_READ | PROT_WRITE | PROT_EXEC) != 0) { free(obj); return -1; }
    unsigned char patch[6] = {0xe8, 0,0,0,0, 0x90};
    memcpy(patch + 1, &disp, sizeof(disp));
    memcpy((void *)site, patch, sizeof(patch));
    __builtin___clear_cache((char *)site, (char *)site + sizeof(patch));
    mprotect((void *)page, (size_t)ps, PROT_READ | PROT_EXEC);
    fprintf(stderr, "[app-shell] DECISION 024 object fix installed at 0x23331ae "
                    "-> near stub %p, substitute obj %p (disp=%d)\n",
            near, (void *)obj, (int)disp);
    return 0;
}

/* ------------------------------------------------------------------ */
/* PHASE 4b: ONE targeted fix at the real allocation site (worker-1)   */
/* ------------------------------------------------------------------ */
/* Guest RVA 0x231f33d `call *(%rax)` allocates the 0x1100 handle via the
 * unpatched guest calloc (slot 0x726c0c0 -> 0x253225e). The guest writes
 * magic at 0x231f34b but NEVER initialises +0x20/+0x68, leaving stale code
 * bytes that later fault at 0x23331e6 / 0x2456079. Replace the 6-byte magic
 * write with a call to a near stub that writes the same magic AND zeroes the
 * two fields; RAX (the handle) is preserved for the following instructions. */
static int patch_alloc_site_fix(uintptr_t base)
{
    static const unsigned char expect[6] = {
        0xc7, 0x00, 0xad, 0xdb, 0xde, 0xc0};  /* movl $0xc0dedbad,(%rax) */
    uintptr_t site = base + 0x231f34bu;
    if (memcmp((const void *)site, expect, sizeof(expect)) != 0) {
        fprintf(stderr, "[app-shell] alloc-site prologue mismatch at 0x231f34b\n");
        return -1;
    }
    extern unsigned long g_alloc_site_hits;
    unsigned char stub_code[38];
    stub_code[0] = 0x49; stub_code[1] = 0xbb;           /* movabs $counter,%r11 */
    memcpy(stub_code + 2, &g_alloc_site_hits, sizeof(void *));
    stub_code[10] = 0x49; stub_code[11] = 0xff;
    stub_code[12] = 0x03;                               /* incq (%r11)         */
    static const unsigned char tail[25] = {
        0xc7, 0x00, 0xad, 0xdb, 0xde, 0xc0,             /* magic              */
        0x48, 0xc7, 0x40, 0x20, 0,0,0,0,                /* movq $0,0x20(%rax) */
        0x48, 0xc7, 0x40, 0x68, 0,0,0,0,                /* movq $0,0x68(%rax) */
        0xc3,                                            /* ret                */
    };
    memcpy(stub_code + 13, tail, sizeof(tail));
    long ps = sysconf(_SC_PAGESIZE);
    if (ps <= 0) ps = 4096;
    uintptr_t hint = base - 0x08000000u;
    void *near = mmap((void *)hint, 4096, PROT_READ | PROT_WRITE | PROT_EXEC,
                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (near == MAP_FAILED)
        near = mmap(NULL, 4096, PROT_READ | PROT_WRITE | PROT_EXEC,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (near == MAP_FAILED) return -1;
    memcpy(near, stub_code, sizeof(stub_code));
    __builtin___clear_cache((char *)near, (char *)near + sizeof(stub_code));
    (void)stub_code;

    int64_t wide = (int64_t)((uintptr_t)near - (site + 5));
    if (wide < INT32_MIN || wide > INT32_MAX) return -1;
    int32_t disp = (int32_t)wide;
    uintptr_t page = site & ~((uintptr_t)ps - 1);
    if (mprotect((void *)page, (size_t)ps,
                 PROT_READ | PROT_WRITE | PROT_EXEC) != 0) return -1;
    unsigned char patch[6] = {0xe8, 0,0,0,0, 0x90};
    memcpy(patch + 1, &disp, sizeof(disp));
    memcpy((void *)site, patch, sizeof(patch));
    __builtin___clear_cache((char *)site, (char *)site + sizeof(patch));
    mprotect((void *)page, (size_t)ps, PROT_READ | PROT_EXEC);
    fprintf(stderr, "[app-shell] ALLOC-SITE FIX installed at 0x231f34b -> "
                    "near stub %p (disp=%d): magic + zero +0x20/+0x68\n",
            near, (int)disp);
    return 0;
}

/* PHASE 4b diagnostics: logging-only consumer stub at 0x23331ca. Records the
 * guest object pointer and its first dword / +0x20 / +0x68 into globals for
 * the fault handler, then executes the original mov/test/ret unchanged. */
static int patch_handle_log(uintptr_t base)
{
    static const unsigned char expect[7] = {
        0x48, 0x8b, 0x7b, 0x68, 0x48, 0x85, 0xff};
    uintptr_t site = base + 0x23331cau;
    if (memcmp((const void *)site, expect, sizeof(expect)) != 0) {
        fprintf(stderr, "[app-shell] handle-log prologue mismatch at 0x23331ca\n");
        return -1;
    }
    extern volatile unsigned long g_handle_log[4];
    unsigned char stub[48] = {
        0x41, 0x53,                               /* push %r11 (preserve)      */
        0x49, 0xbb, 0,0,0,0,0,0,0,0,             /* movabs $log,%r11          */
        0x49, 0x89, 0x1b,                         /* mov %rbx,0(%r11)          */
        0x48, 0x8b, 0x03,                         /* mov (`rbx),%rax           */
        0x49, 0x89, 0x43, 0x08,                   /* mov %rax,8(%r11)          */
        0x48, 0x8b, 0x43, 0x20,                   /* mov 0x20(%rbx),%rax       */
        0x49, 0x89, 0x43, 0x10,                   /* mov %rax,16(%r11)         */
        0x48, 0x8b, 0x43, 0x68,                   /* mov 0x68(%rbx),%rax       */
        0x49, 0x89, 0x43, 0x18,                   /* mov %rax,24(%r11)         */
        0x41, 0x5b,                               /* pop %r11                  */
        0x48, 0x8b, 0x7b, 0x68,                   /* mov 0x68(%rbx),%rdi (orig)*/
        0x48, 0x85, 0xff,                         /* test %rdi,%rdi (orig)     */
        0xc3,                                     /* ret                       */
    };
    memcpy(stub + 4, &g_handle_log, sizeof(void *));
    long ps = sysconf(_SC_PAGESIZE);
    if (ps <= 0) ps = 4096;
    uintptr_t hint = base - 0x08000000u;
    void *near = mmap((void *)hint, 4096, PROT_READ | PROT_WRITE | PROT_EXEC,
                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (near == MAP_FAILED)
        near = mmap(NULL, 4096, PROT_READ | PROT_WRITE | PROT_EXEC,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (near == MAP_FAILED) return -1;
    memcpy(near, stub, sizeof(stub));
    __builtin___clear_cache((char *)near, (char *)near + sizeof(stub));
    int64_t wide = (int64_t)((uintptr_t)near - (site + 5));
    if (wide < INT32_MIN || wide > INT32_MAX) return -1;
    int32_t disp = (int32_t)wide;
    uintptr_t page = site & ~((uintptr_t)ps - 1);
    if (mprotect((void *)page, (size_t)ps,
                 PROT_READ | PROT_WRITE | PROT_EXEC) != 0) return -1;
    unsigned char patch[7] = {0xe8, 0,0,0,0, 0x90, 0x90};
    memcpy(patch + 1, &disp, sizeof(disp));
    memcpy((void *)site, patch, sizeof(patch));
    __builtin___clear_cache((char *)site, (char *)site + sizeof(patch));
    mprotect((void *)page, (size_t)ps, PROT_READ | PROT_EXEC);
    fprintf(stderr, "[app-shell] handle-log stub installed at 0x23331ca\n");
    return 0;
}

/* ------------------------------------------------------------------ */
/* PHASE 4b: ONE targeted writer-side fix (worker-1/worker-4)          */
/* ------------------------------------------------------------------ */
/* The safe guest path zeroes [handle+0x68] at RVA 0x232004f before calling
 * consumer 0x233319a; the app-shell path calls it at 0x2333168 via the lea at
 * 0x2333165 and SKIPS that zeroing, leaving stale code bytes. Replace those
 * 9 bytes with a call to a near stub that performs the lea, zeroes the two
 * fields, then calls the original consumer and returns to 0x233316d. */
static int patch_app_shell_call_site(uintptr_t base)
{
    static const unsigned char expect[8] = {
        0x8d, 0x7d, 0xb8,                   /* lea -0x48(%rbp),%edi */
        0xe8, 0x2d, 0x00, 0x00, 0x00};      /* call 0x233319a      */
    uintptr_t site = base + 0x2333165u;
    if (memcmp((const void *)site, expect, sizeof(expect)) != 0) {
        const unsigned char *got = (const unsigned char *)site;
        fprintf(stderr, "[app-shell] app-shell call-site prologue mismatch at "
                        "0x2333165: got %02x %02x %02x %02x %02x %02x %02x %02x\n",
                got[0],got[1],got[2],got[3],got[4],got[5],got[6],got[7]);
        return -1;
    }
    unsigned char stub[31] = {
        0x48, 0x8d, 0x7d, 0xb8,                   /* lea -0x48(%rbp),%rdi     */
        0x48, 0x8b, 0x07,                         /* mov (%rdi),%rax          */
        0x48, 0xc7, 0x40, 0x68, 0,0,0,0,          /* movq $0,0x68(%rax)       */
        0x48, 0xc7, 0x40, 0x20, 0,0,0,0,          /* movq $0,0x20(%rax)       */
        0xe8, 0,0,0,0,                            /* call rel32 consumer      */
        0xc3,                                     /* ret -> 0x233316d         */
    };
    /* rel32 from the call inside the near stub to the guest consumer. */
    uintptr_t consumer = base + 0x233319au;
    int64_t cdisp = (int64_t)(consumer - 0u); /* filled once near is known */
    long ps = sysconf(_SC_PAGESIZE);
    if (ps <= 0) ps = 4096;
    uintptr_t hint = base - 0x08000000u;
    void *near = mmap((void *)hint, 4096, PROT_READ | PROT_WRITE | PROT_EXEC,
                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (near == MAP_FAILED)
        near = mmap(NULL, 4096, PROT_READ | PROT_WRITE | PROT_EXEC,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (near == MAP_FAILED) return -1;
    /* call instruction in stub starts at near+20, returns to near+25. */
    cdisp = (int64_t)(consumer - ((uintptr_t)near + 25));
    if (cdisp < INT32_MIN || cdisp > INT32_MAX) return -1;
    int32_t cd = (int32_t)cdisp;
    memcpy(stub + 21, &cd, sizeof(cd));
    memcpy(near, stub, sizeof(stub));
    __builtin___clear_cache((char *)near, (char *)near + sizeof(stub));

    int64_t wide = (int64_t)((uintptr_t)near - (site + 5));
    if (wide < INT32_MIN || wide > INT32_MAX) return -1;
    int32_t disp = (int32_t)wide;
    uintptr_t page = site & ~((uintptr_t)ps - 1);
    if (mprotect((void *)page, (size_t)ps,
                 PROT_READ | PROT_WRITE | PROT_EXEC) != 0) return -1;
    unsigned char patch[8] = {0xe8, 0,0,0,0, 0x90,0x90,0x90};
    memcpy(patch + 1, &disp, sizeof(disp));
    memcpy((void *)site, patch, sizeof(patch));
    __builtin___clear_cache((char *)site, (char *)site + sizeof(patch));
    mprotect((void *)page, (size_t)ps, PROT_READ | PROT_EXEC);
    fprintf(stderr, "[app-shell] APP-SHELL FIX installed at 0x2333165 -> "
                    "near stub %p (disp=%d, consumer disp=%d)\n",
            near, (int)disp, (int)cd);
    return 0;
}

/* Native helper: build a valid zeroed 0xc0dedbad object to replace the bogus
 * code pointer the app-shell path stores in the slot at [rbp-0x48]. Allocate
 * through the guest's OWN patched calloc bridge (0x231f0e2 -> host_calloc) so
 * the guest's free bridge can release it without `free(): invalid size`. */
static uintptr_t g_safe_handle_base;

static void *sober_safe_app_shell_handle(void)
{
    void *(*guest_calloc)(size_t, size_t) =
        (void *(*)(size_t, size_t))(g_safe_handle_base + 0x231f0e2u);
    unsigned char *obj = (unsigned char *)guest_calloc(1, 0x2000);
    if (!obj) return NULL;
    uint32_t magic = 0xc0dedbadu;
    memcpy(obj, &magic, sizeof(magic));   /* +0x20 and +0x68 stay ZERO */
    return obj;
}

/* DECISION 031 (revised): the slot at [rbp-0x48] holds a CODE POINTER, so we
 * must replace the pointer, not zero its fields. Patch the 5-byte call at
 * guest 0x2333168; the near stub calls the helper, stores the safe object
 * into the slot *(void**)rdi, then tail-jumps to the original consumer
 * 0x233319a (returning to 0x233316d via the patched call's return address). */
static int patch_call_site_fix(uintptr_t base)
{
    static const unsigned char expect[5] = {0xe8, 0x2d, 0x00, 0x00, 0x00};
    g_safe_handle_base = base;
    uintptr_t site = base + 0x2333168u;
    if (memcmp((const void *)site, expect, sizeof(expect)) != 0) {
        const unsigned char *g = (const unsigned char *)site;
        fprintf(stderr, "[app-shell] call-site prologue mismatch at 0x2333168: "
                        "got %02x %02x %02x %02x %02x\n",
                g[0],g[1],g[2],g[3],g[4]);
        return -1;
    }
    unsigned char stub[36] = {
        0x41, 0x54,                               /* 0: push %r12              */
        0x4c, 0x89, 0xfc,                         /* 2: mov %rdi,%r12 slot addr*/
        0x48, 0xb8, 0,0,0,0,0,0,0,0,              /* 5: movabs $helper,%rax    */
        0xff, 0xd0,                               /*15: call %rax              */
        0x49, 0x89, 0x04, 0x24,                   /*17: mov %rax,(%r12) SLOT   */
        0x4c, 0x89, 0xe7,                         /*21: mov %r12,%rdi          */
        0xe8, 0,0,0,0,                            /*24: call rel32 consumer    */
        0x41, 0x5c,                               /*29: pop %r12               */
        0xe9, 0,0,0,0,                            /*31: jmp 0x233316d          */
    };
    /* Use the guest's OWN canonical handle getter (safe path calls it at
     * 0x23200cd) so the returned object belongs to the guest allocator. */
    uintptr_t helper = base + 0x231f294u;
    memcpy(stub + 7, &helper, sizeof(helper));
    long ps = sysconf(_SC_PAGESIZE);
    if (ps <= 0) ps = 4096;
    uintptr_t hint = base - 0x08000000u;
    void *near = mmap((void *)hint, 4096, PROT_READ | PROT_WRITE | PROT_EXEC,
                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (near == MAP_FAILED)
        near = mmap(NULL, 4096, PROT_READ | PROT_WRITE | PROT_EXEC,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (near == MAP_FAILED) return -1;
    int64_t cwide = (int64_t)((base + 0x233319au) - ((uintptr_t)near + 29));
    int64_t rwide = (int64_t)((base + 0x233316du) - ((uintptr_t)near + 36));
    if (cwide < INT32_MIN || cwide > INT32_MAX ||
        rwide < INT32_MIN || rwide > INT32_MAX) return -1;
    int32_t cd = (int32_t)cwide, rd = (int32_t)rwide;
    memcpy(stub + 25, &cd, sizeof(cd));
    memcpy(stub + 32, &rd, sizeof(rd));
    memcpy(near, stub, sizeof(stub));
    __builtin___clear_cache((char *)near, (char *)near + sizeof(stub));

    int64_t wide = (int64_t)((uintptr_t)near - (site + 5));
    if (wide < INT32_MIN || wide > INT32_MAX) return -1;
    int32_t disp = (int32_t)wide;
    uintptr_t page = site & ~((uintptr_t)ps - 1);
    if (mprotect((void *)page, (size_t)ps,
                 PROT_READ | PROT_WRITE | PROT_EXEC) != 0) return -1;
    unsigned char patch[5] = {0xe8, 0,0,0,0};
    memcpy(patch + 1, &disp, sizeof(disp));
    memcpy((void *)site, patch, sizeof(patch));
    __builtin___clear_cache((char *)site, (char *)site + sizeof(patch));
    mprotect((void *)page, (size_t)ps, PROT_READ | PROT_EXEC);
    fprintf(stderr, "[app-shell] DECISION 031 call-site fix installed at "
                    "0x2333168 -> near stub %p (disp=%d, consumer disp=%d)\n",
            near, (int)disp, (int)cd);
    return 0;
}

/* OPT-IN repair experiment: the start-app worker calls
 * SingleSurfaceApp::userDidLogin (guest 0x2771f6c), which dereferences the
 * user-controller pointer at app+0x440. In this harness that slot is still
 * NULL when the worker runs, so the genuine guest path faults at 0x2772403.
 * Allocate a zeroed, guest-owned object and install it in that slot. This does
 * NOT fabricate any Java callback: it only lets the real guest startup path
 * continue so we can observe whether an app-shell callback arrives. */
/* The JNI_OnLoad-cached string-helper global 0x7cdec78 is deliberately NOT
 * stubbed here. Worker jnionload owns its faithful reconstruction in
 * src/jni/native_jni.c; a synthetic vtable at this site faults (rip=nil), so
 * the earlier inline experiment was removed rather than kept as a trap. */

/* ------------------------------------------------------------------ */
/* OPT-IN real app-state path (task #450). The guest's controller installer
 * 0x24ad50a is gated by FFlag EnableAppBridgeComponents (0x72cb648), and the
 * delegate at app+0x408 is written by nativeAppBridgeStartLuaAppDM (0x25448f6),
 * neither of which our harness previously invoked. Replay those real Java-visible
 * entries; no guest bytes are patched and no callback is fabricated. */
static void real_appstate_prepare(uintptr_t base, void *env, void *clazz,
                                  SoberJni *j)
{
    /* Exact grammar from analysis/appstate_fields.md addendum: ClientAppSettings
     * is a JSON object; nativePreloadFlagOverrides only STORES it, and
     * nativeRetryInit (0x2edd739) applies it to the FFlag registry. */
    void *over = sober_jni_new_string_utf8(j,
        "{\"EnableAppBridgeComponents\":true}");
    ((void (*)(void *, void *, void *))(base + 0x2edd859u))(env, clazz, over);
    call_jni_static(base + 0x2edd739u, env, clazz); /* nativeRetryInit */
    unsigned char *ff = (unsigned char *)(base + 0x72cb648u);
    fprintf(stderr, "[app-shell] real-appstate: flag 0x72cb648 after "
                    "nativePreloadFlagOverrides = %u\n", (unsigned)*ff);
    if (!*ff && getenv("SOBER_REAL_APPSTATE_FLAG")) {
        *ff = 1;
        fprintf(stderr, "[app-shell] real-appstate: DIAGNOSTIC flag seed "
                        "(grammar not yet confirmed) -> 1\n");
    }
    if (!*ff)
        fprintf(stderr, "[app-shell] real-appstate: flag still 0 (set "
                        "SOBER_REAL_APPSTATE_FLAG=1 to force the diagnostic "
                        "branch; it then faults at 0x2ec1042 because singleton "
                        "0x72cb5e0 is unpopulated)\n");
    /* The flag=1 path resolves the app through singleton 0x72cb5e0, which is
     * created by nativePostClientSettingsLoadedInitialization3. Replay that
     * real Java call before anything touches the flag-enabled branch. */
    /* Doing this with our synthetic List faults at 0x2e5d42a: the guest treats
     * the jobject as a native C++ collection, which our JNI object model does
     * not reproduce. Kept as a separate opt-in for study, not on by default. */
    if (getenv("SOBER_REAL_APPSTATE_POSTSETTINGS")) {
        void *list = sober_jni_new_native_object(j, "java/util/List", 0x40, NULL);
        ((void (*)(void *, void *, void *))(base + 0x2383c18u))(env, clazz, list);
        fprintf(stderr, "[app-shell] real-appstate: called "
                        "nativePostClientSettingsLoadedInitialization3 0x2383c18; "
                        "app singleton=0x%lx\n",
                (unsigned long)*(uintptr_t *)(base + 0x72cb5e0u));
    }
    void *a = sober_jni_new_string_utf8(j, "AppShellNotifications");
    void *b = sober_jni_new_string_utf8(j, "123456789");
    void *c = sober_jni_new_string_utf8(j, "StartConversationWithUserId");
    void *d = sober_jni_new_string_utf8(j, "Chat");
    ((void (*)(void *, void *, void *, void *, void *, void *))
        (base + 0x2ebcf36u))(env, clazz, a, b, c, d);
    fprintf(stderr, "[app-shell] real-appstate: called SendAppEventOnAppReady "
                    "0x2ebcf36\n");
}

static void real_appstate_start_lua(uintptr_t base, void *env, void *clazz)
{
    ((void (*)(void *, void *))(base + 0x25448f6u))(env, clazz);
    g_ck_rung2_lua_dm = 1;
    fprintf(stderr, "[app-shell] real-appstate: called "
                    "nativeAppBridgeStartLuaAppDM 0x25448f6\n");
}

/* Getter handlers for the StartAppParams/InitParams fields the real Java builds
 * but our synthetic object did not previously back. */
static intptr_t real_path_get(void *env, void *obj, const char *name,
                              const char *sig, void *args, void *user)
{
    intptr_t v = start_app_get(env, obj, name, sig, args, user);
    if (v) return v;
    if (!name) return 0;
    SoberJni *j = (SoberJni *)user;
    if (!strcmp(name, "appStarterPlace") || !strcmp(name, "pioneerPlaceId"))
        return (intptr_t)sober_jni_new_string_utf8(j, "0");
    if (!strcmp(name, "appStarterScript"))
        return (intptr_t)sober_jni_new_string_utf8(j, "-- app starter --\n");
    if (!strcmp(name, "appUserId"))
        return (intptr_t)sober_jni_new_string_utf8(j, "1");
    if (!strcmp(name, "username"))
        return (intptr_t)sober_jni_new_string_utf8(j, "SoberUser");
    if (!strcmp(name, "selectedTheme") || !strcmp(name, "buildVariant"))
        return (intptr_t)sober_jni_new_string_utf8(j, "default");
    if (!strcmp(name, "baseURL") || !strcmp(name, "userAgent"))
        return (intptr_t)sober_jni_new_string_utf8(j, "https://www.roblox.com");
    if (!strcmp(name, "deviceParams"))
        return (intptr_t)sober_jni_new_native_object(j,
            "com/roblox/engine/jni/model/DeviceParams", 0x100, NULL);
    /* isUnder13 / membershipType / isTablet / isVrDevice / isPotato: boolean or
     * integer getters; a zero return is the honest neutral value. */
    return 0;
}

/* No-op vtable target for the synthetic delegate below. Accepts up to four
 * pointer-size args and returns zero, so it is safe for the guest's void and
 * int virtual calls alike. */
static long sober_noop_virtual(long a, long b, long c, long d)
{
    (void)a; (void)b; (void)c; (void)d;
    return 0;
}

static int seed_app_delegate(void *app, uintptr_t base)
{
    void *(*guest_malloc)(size_t) =
        (void *(*)(size_t))(base + 0x1dbab02u);
    unsigned char **slot = (unsigned char **)((char *)app + 0x408);
    if (*slot) return 0;
    /* Task #A8: prefer the guest's own lazy singleton helper. It returns a
     * guest object with a genuine guest vtable (no host no-op vtable). */
    if (getenv("SOBER_FAITHFUL_APP_DELEGATE")) {
        void *(*guest_helper)(void) = (void *(*)(void))(base + 0x1dd55f2u);
        void *real = guest_helper();
        if (real) {
            *slot = (unsigned char *)real;
            fprintf(stderr, "[app-shell] faithful app delegate from guest "
                            "helper 0x1dd55f2: %p -> app+0x408\n", real);
            return 0;
        }
        fprintf(stderr, "[app-shell] faithful app delegate helper returned "
                        "NULL; falling back\n");
    }
    void *vt = guest_malloc(0x100);
    void *obj = guest_malloc(0x100);
    if (!vt || !obj) return -1;
    memset(vt, 0, 0x100);
    memset(obj, 0, 0x100);
    for (int i = 0; i < 32; i++)
        ((void **)vt)[i] = (void *)sober_noop_virtual;
    *(void **)obj = vt;
    *slot = (unsigned char *)obj;
    fprintf(stderr, "[app-shell] app-delegate seed: installed obj=%p vt=%p "
                    "at app+0x408\n", obj, vt);
    return 0;
}

/* Task #473/#475: the genuine app+0x488 object is built in InitWithParams at
 * 0x24af2cc/0x24af2e6 with vptr 0x6e02918, +8 = app and +0x20 = self. The
 * game-load path reads [app+0x488] at 0x320b0ba and faults when NULL. */
static uintptr_t g_seed_app_base;
static void *g_seed_app_488_wrapper;
static int seed_app_488(void)
{
    void *(*guest_malloc)(size_t) =
        (void *(*)(size_t))(g_seed_app_base + 0x1dbab02u);
    void *app = *(void **)((char *)g_seed_app_488_wrapper + 0x20);
    unsigned char **slot = (unsigned char **)((char *)app + 0x488);
    if (*slot) return 0;
    void *obj = guest_malloc(0x100);
    if (!obj) return -1;
    memset(obj, 0, 0x100);
    *(uintptr_t *)obj = g_seed_app_base + 0x6e02918u;
    *(void **)((char *)obj + 8) = app;
    *(void **)((char *)obj + 0x20) = obj;
    *slot = (unsigned char *)obj;
    fprintf(stderr, "[app-shell] app+0x488 seed: obj=%p vptr=0x%lx app=%p\n",
            obj, (unsigned long)(g_seed_app_base + 0x6e02918u), app);
    return 0;
}

static int seed_user_controller(uintptr_t base)
{
    g_seed_app_base = base;
    void *(*get_static_wrapper)(void) =
        (void *(*)(void))(base + 0x24ad6fcu);
    void *(*guest_malloc)(size_t) =
        (void *(*)(size_t))(base + 0x1dbab02u);

    unsigned char flag = *(const unsigned char *)(base + 0x72cb648u);
    void *wrapper = get_static_wrapper();
    g_seed_app_488_wrapper = wrapper;
    if (!wrapper) {
        fprintf(stderr, "[app-shell] user-controller seed: wrapper is NULL\n");
        return -1;
    }
    void *app = *(void **)((char *)wrapper + 0x20);
    if (!app) {
        fprintf(stderr, "[app-shell] user-controller seed: app is NULL "
                        "(wrapper=%p)\n", wrapper);
        return -1;
    }
    void **slot = (void **)((char *)app + 0x440);
    fprintf(stderr, "[app-shell] user-controller seed: flag72cb648=%u "
                    "wrapper=%p app=%p slot=%p current=%p\n",
            (unsigned)flag, wrapper, app, (void *)slot, (void *)*slot);
    if (*slot) return 0;
    void *ctl = guest_malloc(0x2000);
    if (!ctl) {
        fprintf(stderr, "[app-shell] user-controller seed: guest malloc failed\n");
        return -1;
    }
    memset(ctl, 0, 0x2000);
    *slot = ctl;
    fprintf(stderr, "[app-shell] user-controller seed: installed %p at app+0x440\n",
            ctl);
    if (getenv("SOBER_SEED_APP_DELEGATE"))
        (void)seed_app_delegate(app, base);
    if (getenv("SOBER_APP488_INNER")) {
        void *o488 = sober_app488_inner_install_from_wrapper((void *)base);
        fprintf(stderr, "[app-shell] app488_inner module -> %p\n", o488);
    } else if (getenv("SOBER_APP488_FIX")) {
        void *o488 = sober_app488_fix_install_from_wrapper((void *)base);
        fprintf(stderr, "[app-shell] app488_fix module -> %p\n", o488);
    } else if (getenv("SOBER_SEED_APP_488")) {
        (void)seed_app_488();
    }
    return 0;
}

static void log_app_slots(uintptr_t base, const char *where)
{
    void *(*get_static_wrapper)(void) =
        (void *(*)(void))(base + 0x24ad6fcu);
    void *wrapper = get_static_wrapper();
    if (!wrapper) {
        fprintf(stderr, "[app-shell] %s: wrapper NULL\n", where);
        return;
    }
    void *app = *(void **)((char *)wrapper + 0x20);
    if (!app) {
        fprintf(stderr, "[app-shell] %s: app NULL\n", where);
        return;
    }
    fprintf(stderr, "[app-shell] %s: app=%p +0x408=%p +0x440=%p\n",
            where, app, *(void **)((char *)app + 0x408),
            *(void **)((char *)app + 0x440));
}

/* src/compat/bionic_sf_full.c lives in the dlopened libc.so stub, not in this
 * executable; resolve its opt-in installer through the stub handle. */
static uintptr_t g_sf_base;

static void install_sf_full(uintptr_t base)
{
    g_sf_base = base;
    void *stub = dlopen("libc.so", RTLD_NOLOAD | RTLD_LAZY);
    if (!stub) return;
    void (*fn)(unsigned char *) =
        (void (*)(unsigned char *))dlsym(stub, "sober_bionic_sf_full_install");
    if (fn) fn(__sF);
    fprintf(stderr, "[bionic-sf] guest GOT __sF=%p | android_compat __sF=%p "
                    "| libc-stub __sF=%p\n",
            (void *)*(uintptr_t *)(g_sf_base + 0x72281d8u),
            (void *)__sF, dlsym(stub, "__sF"));
}

/* Task #466: getter handlers for the Java StartGameParams builder. Values are
 * neutral stand-ins; the point is to let the genuine guest game-load path run
 * and reach notifier 0x27eaf6e -> gameLoadedCallback(J)V. */
static intptr_t game_params_get(void *env, void *obj, const char *name,
                                const char *sig, void *args, void *user)
{
    (void)env; (void)obj; (void)sig; (void)args;
    SoberJni *j = (SoberJni *)user;
    if (!j || !name) return 0;
    if (!strcmp(name, "gameId") || !strcmp(name, "placeId"))
        return (intptr_t)sober_jni_new_string_utf8(j, "123456789");
    if (!strcmp(name, "accessCode"))
        return (intptr_t)sober_jni_new_string_utf8(j, "");
    if (!strcmp(name, "username"))
        return (intptr_t)sober_jni_new_string_utf8(j, "SoberUser");
    return 0;  /* userId / isUnder13: neutral integers */
}

int guest_app_shell_run(uintptr_t base)
{
    if (getenv("SOBER_ABRT_DIAG")) {
        g_abrt_base = base;
        signal(SIGABRT, sober_abrt_diag);
    }
    SoberJniOptions opts;
    memset(&opts, 0, sizeof(opts));
    opts.app_name = "sober-native-app-shell";
    opts.verbose = getenv("SOBER_JNI_VERBOSE") ? 1 : 0;

    SoberJniProbe probe;
    memset(&probe, 0, sizeof(probe));
    if (sober_jni_probe(&probe) != SOBER_JNI_OK) {
        fprintf(stderr, "[app-shell] JNI bridge probe FAILED\n");
        return -1;
    }
    char pbuf[256];
    fprintf(stderr, "[app-shell] %s\n",
            sober_jni_probe_describe(pbuf, sizeof(pbuf)));

    SoberJni *j = NULL;
    if (sober_jni_create(&opts, &j) != SOBER_JNI_OK || !j) {
        fprintf(stderr, "[app-shell] JNI bridge create FAILED\n");
        return -2;
    }
    sober_jni_init(j);

    /* F7: bind the immutable render-evidence recorder to the frozen base. */
    frame_evidence_set_guest_base((uintptr_t)base);

    /* ================= DEFAULT-ON module wiring (task #477) ================
     * Dependency order (each installer self-gates on its SOBER_*_DEFAULT=0 kill
     * switch; no getenv() needed here):
     *   1 substrate   : TSD GOT rebind, faithful JavaVM, provenance credential
     *   2 flags       : FFlag ctor[0] replay, then genuine flag setter
     *   3 scheduler   : guest nesting prime (opt-in, on FunctionMarshaller attach)
     *   4 singleton   : populated app singleton + services
     *   5 render      : guest-tagged surface/swap path (native context already up)
     * Later, once the app pointer exists: app+0x488, delegate, controller;
     * then placeId before StartGame and publish after it.  Every knob/verdict
     * of these modules forces shell_cb=0 in the seeded_state chain below, so
     * the strict 013/038 marker stays WITHHELD unless genuine proof appears. */
    int tsd_rc = sober_tsd_default_install();
    fprintf(stderr, "[app-shell] default TSD GOT rebind -> rc=%d\n", tsd_rc);
    int jvm_rc = sober_javavm_default_install(j, (void *)base);
    fprintf(stderr, "[app-shell] default JavaVM install -> rc=%d\n", jvm_rc);
    int pg_rc = sober_provenance_gate_install((void *)base);
    fprintf(stderr, "[app-shell] provenance gate install -> rc=%d\n", pg_rc);

    /* FFlag registry must exist before the singleton gate (0x72cb648 plus the
     * 0x6096928 predicate). Ensure the flag through the guest's own setter. */
    int ff_rc = sober_fflag_default_replay((void *)base);
    fprintf(stderr, "[app-shell] default FFlag ctor[0] replay -> rc=%d\n", ff_rc);
    /* Scheduler guest nesting prime: install now; the actual prime is fired
     * from on_attach("FunctionMarshaller") at the JavaVM attach site. */
    int sg_rc = sober_scheduler_guest_install((void *)base);
    fprintf(stderr, "[app-shell] scheduler guest install -> rc=%d\n", sg_rc);

    /* Guest-tagged render path. Phase 3 already created a native context, so
     * derive the config from the active render and let the module install its
     * RA-tagged swap/draw hooks. */
    struct sober_render_guest_config rg_cfg;
    memset(&rg_cfg, 0, sizeof(rg_cfg));
    SoberRender *active_r = sober_render_get_active();
    rg_cfg.guest_base    = (void *)base;
    rg_cfg.native_window = sober_render_native_window(active_r);
    rg_cfg.native_handle = sober_render_native_handle(active_r);
    rg_cfg.egl_display   = sober_render_egl_display(active_r);
    rg_cfg.egl_surface   = sober_render_egl_surface(active_r);
    rg_cfg.egl_context   = sober_render_egl_context(active_r);
    sober_render_size(active_r, &rg_cfg.width, &rg_cfg.height);
    /* Phase 3 (guest_render.c) already ran render_guest_run() with its own
     * config; do not create a second guest context here (it re-enters 0x649c21e
     * and faults). Just ensure the module is installed and reuse its state. */
    int rg_rc = sober_render_guest_is_installed()
              ? 0 : sober_render_guest_run(&rg_cfg);
    fprintf(stderr, "[app-shell] default guest render path -> rc=%d "
                    "(already installed=%d, guest-tagged swaps=%llu)\n",
            rg_rc, sober_render_guest_is_installed(),
            (unsigned long long)sober_render_guest_swaps_guest_tagged());

    /* Worker-3 #490: fail-closed frame-evidence bringup.  It may only promote
     * the render verdict after observing a genuine guest store event. */
    int ev_rc = sober_render_evidence_bringup((uintptr_t)base,
        sober_render_native_window(active_r),
        (unsigned)rg_cfg.width, (unsigned)rg_cfg.height);
    fprintf(stderr, "[app-shell] render evidence bringup rc=%d\n", ev_rc);

    /* T2-5 flags-loaded latch 0x7cdd149. The module uses genuine shipped guest
     * code only (no byte writes); its default route is nativeRunCrashpadHandler
     * 0x2eca720 with a real String[]. Default-ON with kill switch
     * SOBER_FLAGS_LATCH_DEFAULT=0; every attempted route forces withhold. */
    (void)sober_jni_arrays_install(j);  /* idempotent; needed by crashpad-jni */
    int latch_rc = sober_flags_latch_default_install(
        (void *)base, sober_jni_env(j),
        sober_jni_find_class(j, "com/roblox/engine/jni/NativeSettingsInterface"),
        j);
    fprintf(stderr, "[app-shell] flags-latch module -> rc=%d latch=%u\n",
            latch_rc, sober_flags_latch_default_latch());

    /* B3: opt-in faithful JNI array objects (GetArrayLength/GetObjectArrayElement). */
    int arrays_rc = sober_jni_arrays_install(j);
    fprintf(stderr, "[app-shell] faithful JNI arrays (SOBER_FAITHFUL_JNI_ARRAYS) "
                    "-> rc=%d\n", arrays_rc);

    /* Worker-2 #509: opt-in clean-return crashpad route.  Real AF_UNIX IPC and
     * host-side Bionic __sF fputs/fflush bridging let the genuine latch writer
     * 0x2ec9eed return with latch=1.  Every active run is seeded/WITHHELD. */
    int cr_rc = sober_crashpad_return_install(
        (void *)base, sober_jni_env(j),
        sober_jni_find_class(j, "com/roblox/engine/jni/NativeSettingsInterface"),
        j);
    fprintf(stderr, "[app-shell] crashpad-return module -> rc=%d latch=%u\n",
            cr_rc, sober_crashpad_return_latch());

    /* C13: opt-in, fail-closed notifier-hit instrumentation. Patches guest
     * bytes only after exact prologue checks; force-withheld below. */
    int np_rc = sober_notifier_probe_install((void *)base);
    fprintf(stderr, "[app-shell] notifier probe install -> rc=%d\n", np_rc);

    /* Wave11: replay the AUTHORITATIVE AVD-captured JNI boundary. The old
     * synthetic (path, "{}", "GoogleAndroidApp") call is REMOVED entirely.
     * arg0 is the exact verified byte stream captured in Frida session
     * w11-hook-settings; arg1 is Java null; arg2 is "GoogleAndroidApp".
     * This is a separate provenance class, not a seeded_state knob. */
    /* W24 Run D (HOST CODE ONLY, ledger debt): AVD boots with JNI_OnLoad
     * BEFORE nativeInitClientSettings.  Our harness had the reverse.  When
     * SOBER_JNI_ONLOAD_FIRST is set, do the genuine JNI_OnLoad here first (with
     * the approved TSD seed), then let the settings replay run, and skip the
     * later duplicate.  Seeder stays OFF; no guest byte is written. */
    static int w24_onload_done;
    if (getenv("SOBER_JNI_ONLOAD_FIRST") && !w24_onload_done) {
        seed_stub_tsd((uintptr_t)base);
        int (*early_onload)(void *, void *) =
            (int (*)(void *, void *))(base + 0x227af5au);
        int ever = early_onload(sober_jni_java_vm(j), NULL);
        w24_onload_done = 1;
        fprintf(stderr, "[app-shell] W24 JNI_OnLoad FIRST 0x227af5a -> "
                        "ver=%#x\n", (unsigned)ever);
    }

    /* W28 exact captured order (host-only, opt-in): after OnLoad, call
     * Channel(""), Version and GameGlobalInit BEFORE InitClientSettings.
     * The three later duplicates are skipped when this runs.  No guest bytes. */
    static int w28_order_done;
    if (getenv("SOBER_W28_REORDER") && !w28_order_done) {
        w28_order_done = 1;
        void *w28_env = sober_jni_env(j);
        void *w28_cls = sober_jni_find_class(j,
            "com/roblox/engine/jni/NativeGLJavaInterface");
        void *w28_chan = sober_jni_new_string_utf8(j, "");
        void *w28_ver = sober_jni_new_string_utf8(j, "2.740.931");
        long w28_rc;
        w28_rc = ((long (*)(void *, void *, void *))(base + 0x2326e2eu))
                     (w28_env, w28_cls, w28_chan);
        fprintf(stderr, "[app-shell] W28_ORDER 1 Channel(\"\") 0x2326e2e rc=%ld\n",
                w28_rc);
        w28_rc = ((long (*)(void *, void *, void *))(base + 0x232d484u))
                     (w28_env, w28_cls, w28_ver);
        fprintf(stderr, "[app-shell] W28_ORDER 2 Version 0x232d484 rc=%ld\n", w28_rc);
        w28_rc = call_jni_static(base + 0x233762cu, w28_env, w28_cls);
        fprintf(stderr, "[app-shell] W28_ORDER 3 GameGlobalInit 0x233762c rc=%ld\n",
                w28_rc);
    }

    int avd_boundary_active = 0;
    if (sober_avd_boundary_enabled()) {
        char prov[256];
        sober_avd_boundary_provenance(prov, sizeof(prov));
        fprintf(stderr, "[app-shell] AVD boundary provenance: %s\n", prov);

        char *arg0_bytes = NULL;
        size_t arg0_len = 0;
        int rc = sober_avd_boundary_load(&arg0_bytes, &arg0_len);
        if (rc != 0) {
            fprintf(stderr, "[app-shell] AVD boundary REPLAY FAILED rc=%d\n", rc);
        } else {
            void *cs_env = sober_jni_env(j);
            void *cs_cls = sober_jni_find_class(j,
                "com/roblox/engine/jni/NativeGLInterface");
            void *a = NULL, *c = NULL;
            if (sober_jstr_enabled()) {
                /* Install canonical accessors so GetStringUTFChars @0x2315532
                 * sees the EXACT captured bytes rather than a NULL handle. */
                void **cs_fn = cs_env
                    ? (void **)((char *)*(void **)cs_env + 0x20u) : NULL;
                int ji_rc = sober_jstr_install(cs_fn, (void *)base);
                fprintf(stderr, "[app-shell] AVD boundary canonical jstrings "
                                "install -> rc=%d\n", ji_rc);
                a = sober_jstr_create((void *)base, arg0_bytes);
                c = sober_jstr_create((void *)base, SOBER_AVD_BOUNDARY_ARG2);
            } else {
                a = sober_jni_new_string_utf8(j, arg0_bytes);
                c = sober_jni_new_string_utf8(j, SOBER_AVD_BOUNDARY_ARG2);
            }
            void *b = NULL;                       /* Java null, as captured */
            /* Read-only: FFlag context global 0x72a3058 -> +0x18 object
             * pointer. Wave12 says this holds the 0x30 token on the harness
             * and a real heap object on AVD. */
            void *fflag_ctx = *(void *const *)(base + 0x72a3058u);
            void *fflag_obj = fflag_ctx
                ? *(void *const *)((unsigned char *)fflag_ctx + 0x18u) : NULL;
            g_w32_latch_before =
                (int)*(volatile unsigned char *)(base + 0x7cdd149u);
            fprintf(stderr, "[app-shell] AVD boundary pre: arg0_len=%zu "
                            "jstr0=%p D08=%#x latch=%u "
                            "fflag_ctx=%p fflag_obj=%p\n",
                    arg0_len, a,
                    (unsigned)*(volatile unsigned char *)(base + 0x7697d08u),
                    (unsigned)*(volatile unsigned char *)(base + 0x7cdd149u),
                    fflag_ctx, fflag_obj);
            /* W15 diagnostic: in-process perf hardware write-watchpoint on
             * the guest stack slot read at 0x23121ae. Address is supplied by
             * the caller (ASLR-off runs give a deterministic slot). */
            if (getenv("SOBER_HW_WATCH")) {
                const char *a = getenv("SOBER_HW_WATCH_ADDR");
                uintptr_t hf = (uintptr_t)__builtin_frame_address(0);
                uintptr_t wa = a ? strtoull(a, NULL, 0) : hf - 0x1dd8u;
                fprintf(stderr, "[HWBP] host_frame=%p watch=%p\n",
                        __builtin_frame_address(0), (void *)wa);
                (void)sober_hw_watch_install((void *)wa);
            }
            /* W15 diagnostic: stop here so gdb can attach to an ALREADY
             * loaded process and watch the guest stack slot without paying
             * the 120 MB dlopen cost under ptrace. No guest state is changed. */
            if (getenv("SOBER_STOP_BEFORE_SETTINGS")) {
                fprintf(stderr, "[app-shell] SIGSTOP before 0x2359277 "
                                "(gdb attach point)\n");
                fflush(stderr);
                /* ptrace_scope=1: allow this diagnostic attach without
                 * changing any system setting or guest state. */
                (void)prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY, 0, 0, 0);
                raise(SIGSTOP);
            }
            long cs_rc = ((long (*)(void *, void *, void *, void *, void *))
                (base + 0x2359277u))(cs_env, cs_cls, a, b, c);
            g_ck_rung1_returned = 1;
            g_w32_latch_after =
                (int)*(volatile unsigned char *)(base + 0x7cdd149u);
            fprintf(stderr, "[W32_LATCH_RO] before=%d after=%d\n",
                    g_w32_latch_before, g_w32_latch_after);
            /* W23: emit the rung1 line IMMEDIATELY on return with write(2),
             * before any later phase can fault.  Label only: PASS-BOUNDARY-
             * REPLAY(DEBT=n) when the read-only credential holds; never CLEAN.
             */
            {
                /* W32: watcher-free read-only credential: before=0, after=1, SHA verified.
             * All harness latch accesses are READS (no write anywhere). */
            const int boundary = sober_w21_boundary_ok() ||
                (g_w32_latch_before == 0 && g_w32_latch_after == 1 &&
                 g_w29_arg0_sha_ok);
                const int debt = sober_avd_boundary_enabled() ? 1 : 0;
                char rb[160]; int rn = 0;
                static const char pre[] = "[checkpoint] rung1 ";
                for (unsigned k = 0; k < sizeof(pre) - 1; ++k) rb[rn++] = pre[k];
                static const char pass[] = "PASS-BOUNDARY-REPLAY (DEBT=";
                static const char fail[] = "FAIL (DEBT=";
                const char *lab = boundary ? pass : fail;
                for (unsigned k = 0; lab[k]; ++k) rb[rn++] = lab[k];
                rb[rn++] = (char)('0' + (debt & 7));
                rb[rn++] = ')'; rb[rn++] = '\n';
                ssize_t u = write(STDERR_FILENO, rb, (size_t)rn);
                (void)u;
            }
            fprintf(stderr, "[app-shell] AVD boundary nativeInitClientSettings "
                            "0x2359277 -> rc=%ld D08_len=%#x latch=%u "
                            "CF0=%#x 72a31b8=%#x\n",
                    cs_rc,
                    (unsigned)*(volatile unsigned char *)(base + 0x7697d08u),
                    (unsigned)*(volatile unsigned char *)(base + 0x7cdd149u),
                    (unsigned)*(volatile unsigned char *)(base + 0x7697cf0u),
                    (unsigned)*(volatile unsigned char *)(base + 0x72a31b8u));
            free(arg0_bytes);
            avd_boundary_active = 1;
        }
    } else if (getenv("SOBER_CLIENT_SETTINGS_INIT")) {
        fprintf(stderr, "[app-shell] SOBER_CLIENT_SETTINGS_INIT is RETIRED; "
                        "use %s=1 for the AVD-captured boundary\n",
                SOBER_AVD_BOUNDARY_ENV);
    }

    /* Task #461: must land BEFORE the JNI_OnLoad/JavaVM seeds — the first
     * guest worker can read global 0x72a0520 as soon as the faithful VM is
     * installed. The module is a no-op unless SOBER_SEED_GLOBAL_STRING=1. */
    int gseed_rc = sober_seed_global_string(j, (void *)base);
    fprintf(stderr, "[app-shell] global-string seed at 0x72a0520 "
                    "(SOBER_SEED_GLOBAL_STRING, early) -> rc=%d\n", gseed_rc);
    install_sf_full(base);

    /* Task #462: decision 008 deliberately skips init_array[0] (RVA 0x2e62390),
     * the ONLY caller of the once-guarded FFlag registry initializer
     * 0x2e618b0 -> 0x2e61280. Without it the global at 0x72a0520 stays NULL and
     * the worker faults at 0x2e5d42a. Opt-in replay of the genuine guest ctor —
     * no byte patches, no fake vtables, no callbacks. */
    if (getenv("SOBER_REPLAY_FFLAG_CTOR")) {
        ((void (*)(void))(base + 0x2e62390u))();
        fprintf(stderr,
                "[app-shell] replayed guest ctor 0x2e62390 (FFlag registry): "
                "guard=%u global=%p\n",
                (unsigned)*(uint32_t *)(base + 0x72a0a30u),
                (void *)*(uintptr_t *)(base + 0x72a0520u));
    }

    /* Task #465: genuine app-singleton construction must follow the FFlag ctor
     * (the registry control block at handle+0x38 is installed there). */
    if (getenv("SOBER_GSEED_DIAG"))
        fprintf(stderr, "[app-shell] pre-singleton global@0x72a0520=%p\n",
                (void *)*(uintptr_t *)(base + 0x72a0520u));
    if (sober_app_singleton_bridge_enabled()) {
        void *e = sober_jni_env(j);
        void *c = sober_jni_find_class(j,
            "com/roblox/engine/jni/NativeGLJavaInterface");
        int asb = sober_app_singleton_populate(j, (void *)base, e, c);
        fprintf(stderr, "[app-shell] app-singleton bridge -> rc=%d, "
                        "singleton=%p\n", asb,
                (void *)*(uintptr_t *)(base + 0x72cb5e0u));
    }
    if (getenv("SOBER_JSON_APP_SINGLETON")) {
        void *e = sober_jni_env(j);
        void *c = sober_jni_find_class(j,
            "com/roblox/engine/jni/NativeGLJavaInterface");
        int rc_app = sober_jni_json_populate_app_singleton(j, (void *)base, e, c);
        fprintf(stderr, "[app-shell] JSON app-singleton path -> rc=%d, "
                        "singleton=%p\n", rc_app,
                (void *)*(uintptr_t *)(base + 0x72cb5e0u));
    }

    /* The default-ON singleton_default already populated the service vector.
     * The old singleton_fix / singleton_services modules are diagnostic
     * alternatives and are now OFF unless explicitly opted in. */
    if (sober_singleton_fix_enabled()) {
        int ss_rc = sober_singleton_fix_populate((void *)base);
        fprintf(stderr, "[app-shell] singleton_fix (opt-in alternative) -> rc=%d\n",
                ss_rc);
    } else if (getenv("SOBER_SINGLETON_SERVICES")) {
        int ss_rc = sober_singleton_services_install((void *)base);
        fprintf(stderr, "[app-shell] singleton services (opt-in) -> rc=%d\n", ss_rc);
    }

    /* Phase-8 opt-in modules (all default OFF, each no-ops when its env var is
     * unset). Native-object shapes must be installed before any guest RVA. */
    int obj_install = sober_native_objects_install(j, (void *)base);
    fprintf(stderr, "[app-shell] native-object shapes (SOBER_NATIVE_OBJECTS) "
                    "-> rc=%d\n", obj_install);

    /* Handle every callback in worker-1's app-shell list, with named handlers
     * for the two we use as the completion signal. */
    sober_jni_install_default_app_shell_handlers(j);
    sober_jni_set_callback_handler(j, "onAppShellReloadNeeded", shell_callback, NULL);
    sober_jni_set_callback_handler(j, "gameLoadedCallback", shell_callback, NULL);
    /* Diagnostic only: make the genuine guest→Java app-bridge notification
     * visible/countable. Does NOT satisfy decision 013 by itself. */
    if (getenv("SOBER_LOG_APP_BRIDGE_NOTIFICATION"))
        sober_jni_set_callback_handler(j, "onAppBridgeNotification",
                                       shell_callback, NULL);

    /* A/B diagnostics from the lead (default ON, opt-out available). */
    if (getenv("SOBER_TLS_SEED")) {
        size_t bytes = getenv("SOBER_TLS_BYTES")
                       ? (size_t)atol(getenv("SOBER_TLS_BYTES")) : 4096;
        int trc = sober_jni_seed_guest_tls((void *)base, 0x72a1b08u, bytes);
        fprintf(stderr, "[app-shell] TLS seed (key rva 0x72a1b08, %zu bytes) -> rc=%d\n",
                bytes, trc);
        if (getenv("SOBER_STUB_TSD_SEED")) seed_stub_tsd(base);  /* experimental */
    }
    /* Old fake seed: the scheduler "neutralize" memset corrupts the second TLS
     * descriptor (0x722e560) and is incompatible with the genuine nesting prime.
     * It is now OFF by default; enable only for A/B with SOBER_SCHED_NEUTRAL=1. */
    if (getenv("SOBER_SCHED_NEUTRAL") && !getenv("SOBER_NO_SCHED_NEUTRAL")) {
        uintptr_t srva = getenv("SOBER_SCHED_RVA")
                         ? (uintptr_t)strtoull(getenv("SOBER_SCHED_RVA"), NULL, 0)
                         : 0x722e540u;
        int nrc = sober_jni_neutralize_guest_scheduler((void *)base, srva);
        fprintf(stderr, "[app-shell] scheduler neutralize (opt-in, rva 0x%lx) -> rc=%d\n",
                (unsigned long)srva, nrc);
    }

    /* W22 Run C (APPROVED host-only change, ledger debt until clean):
     * call the guest's GENUINE JNI_OnLoad at RVA 0x227af5a through our
     * JavaVM instead of seeding the cached JNI globals by hand.  The
     * FindClass-wrapper recursion cause is already fixed in this tree
     * (Bionic TSD rebind + typed getters).  SOBER_LEGACY_JNI_SEED keeps the
     * old seeder as a documented fallback; default is the genuine path.
     * JNI_OnLoad returns a JNI version (0x10006), not SOBER_JNI_OK. */
    int seed_rc = SOBER_JNI_OK;
    if (getenv("SOBER_LEGACY_JNI_SEED")) {
        seed_rc = sober_jni_seed_guest_jni_state(j, (void *)base);
        fprintf(stderr, "[app-shell] legacy JNI seed (skip JNI_OnLoad) -> rc=%d\n",
                seed_rc);
    } else {
        /* W23 approved host-only TSD seed + read-only diagnostic (agent2).
         * Writes ONLY the host _Thread_local stub array; no guest bytes, no
         * cached-jmethodID seeder. */
        seed_stub_tsd((uintptr_t)base);
        {
            void *h = dlopen("libc.so", RTLD_NOLOAD | RTLD_LAZY);
            void *(*getspecific_fn)(unsigned int) = h
                ? (void *(*)(unsigned int))dlvsym(h, "pthread_getspecific",
                                                  "LIBC") : NULL;
            unsigned key = *(volatile unsigned int *)
                ((unsigned char *)base + 0x72a1b08u);
            void *got = getspecific_fn ? getspecific_fn(key) : NULL;
            fprintf(stderr,
                    "[app-shell] pre-OnLoad TSD key=%u get=%p\n", key, got);
        }
        if (w24_onload_done) {
            fprintf(stderr, "[app-shell] JNI_OnLoad already done first "
                            "(W24 order fix); skipping duplicate\n");
        } else {
            int (*jni_onload_fn)(void *, void *) =
                (int (*)(void *, void *))(base + 0x227af5au);
            int ver = jni_onload_fn(sober_jni_java_vm(j), NULL);
            fprintf(stderr,
                    "[app-shell] genuine JNI_OnLoad 0x227af5a -> ver=%#x\n",
                    (unsigned)ver);
            if (ver <= 0) seed_rc = SOBER_JNI_ERR;
        }
    }
    if (seed_rc != SOBER_JNI_OK) {
        sober_jni_shutdown(j);
        return -3;
    }

    /* Flag setter + singleton MUST run AFTER the cached JNI IDs are seeded:
     * the FFlag descriptor thunk 0x22e567a -> 0x227c18e performs a JNI fetch
     * through the registry, which needs those IDs.  Drive the flag through the
     * guest's own setter (registry created by 0x227be14 when JNI_OnLoad was
     * skipped), then install the populated app singleton. */
    int ff_set_rc = SOBER_CD_DISABLED;
    if (sober_fflag_default_verify((void *)base) == SOBER_FFLAG_DEFAULT_OK)
        ff_set_rc = sober_controller_default_ensure_flag((void *)base);
    fprintf(stderr, "[app-shell] controller ensure_flag -> rc=%d flag=%u\n",
            ff_set_rc, (unsigned)*(volatile unsigned char *)(base + 0x72cb648u));
    int sd_rc = sober_singleton_default_install((void *)base);
    fprintf(stderr, "[app-shell] default app singleton -> rc=%d singleton=%p\n",
            sd_rc, *(void **)(base + 0x72cb5e0u));
    if (getenv("SOBER_SD_DEBUG")) {
        unsigned char *app = *(unsigned char **)(base + 0x72cb5e0u);
        fprintf(stderr, "[sd-debug] app=%p", (void *)app);
        if (app) {
            uintptr_t vt = *(uintptr_t *)app;
            uintptr_t slot10 = *(uintptr_t *)(vt + 0x10);
            uintptr_t begin = *(uintptr_t *)(app + 0x48);
            uintptr_t end = *(uintptr_t *)(app + 0x50);
            uintptr_t desc = begin ? *(uintptr_t *)begin : 0;
            fprintf(stderr, " vt=%p slot10=%p (expect %p) vec=%p..%p desc=%p",
                    (void *)vt, (void *)slot10,
                    (void *)(base + 0x6091654u),
                    (void *)begin, (void *)end, (void *)desc);
            if (desc)
                fprintf(stderr, " key=%p (expect %p)",
                        (void *)*(uintptr_t *)(desc + 0x68),
                        (void *)(base + 0x72cb640u));
        }
        unsigned pred = ((unsigned (*)(void))(base + 0x6096928u))();
        void *probe = ((void *(*)(void))(base + 0x30ef840u))();
        fprintf(stderr,
                " pred=%u(al=%u) gate_byte=%u migrate_flag=%u "
                "rollout=%u bucket=%u rollout_name=%p probe=%p\n",
                pred, pred & 0xff,
                (unsigned)*(volatile unsigned char *)(base + 0x77979c0u),
                (unsigned)*(volatile unsigned char *)(base + 0x7797968u),
                *(volatile unsigned *)(base + 0x7797990u),
                *(volatile unsigned *)(base + 0x77979b8u),
                *(void **)(base + 0x7797998u), probe);
    }

    /* Task #449: opt-in seed of the JNI_OnLoad-cached JavaVM* global
     * 0x7cdec78 (the true identity of the previously assumed "string helper").
     * The API itself checks SOBER_SEED_JNI_VM and is a no-op by default. */
    int vm_seed_rc = sober_jni_seed_guest_onload_globals(j, (void *)base);
    fprintf(stderr, "[app-shell] JNI_OnLoad JavaVM seed (SOBER_SEED_JNI_VM) "
                    "-> rc=%d\n", vm_seed_rc);
    int vm_rc = sober_javavm_install(j, (void *)base);
    fprintf(stderr, "[app-shell] faithful JavaVM semantics "
                    "(SOBER_FAITHFUL_JAVAVM) -> rc=%d\n", vm_rc);
    int json_rc = sober_jni_json_apply_flag_overrides_auto(j, (void *)base);
    fprintf(stderr, "[app-shell] JSON flag override bridge "
                    "(SOBER_JSON_FLAG_BRIDGE) -> rc=%d\n", json_rc);
    /* DECISION C (this team): ALL experimental guest patches are reverted to
     * original bytes by default. Kept behind opt-in env knobs for A/B only;
     * the proper fix is worker-1's handle-origin analysis + worker-2's
     * source-level creation-time fix. */
    if (getenv("SOBER_VALIDATOR_PATCH")) {
        if (patch_guest_validator(base) != 0) {
            sober_jni_shutdown(j);
            return -5;
        }
    }
    if (getenv("SOBER_FINAL_PATCH")) {
        if (patch_final_consumer(base) != 0) {
            sober_jni_shutdown(j);
            return -6;
        }
    }
    /* DECISION 023 targeted field normaliser (opt-in for A/B). */
    if (getenv("SOBER_FIELD_NORMALISER")) {
        if (patch_field_normaliser(base) != 0) {
            sober_jni_shutdown(j);
            return -9;
        }
    }
    /* DECISION 024 object-level fix (opt-in for A/B). */
    if (getenv("SOBER_OBJECT_FIX")) {
        if (patch_object_fix(base) != 0) {
            sober_jni_shutdown(j);
            return -10;
        }
    }
    /* PHASE 4b: single targeted fix at the real allocation site. */
    if (getenv("SOBER_ALLOC_SITE_FIX")) {
        if (patch_alloc_site_fix(base) != 0) {
            sober_jni_shutdown(j);
            return -11;
        }
    }
    /* PHASE 4b: opt-in logging-only consumer stub (diagnostic, no behavior change). */
    if (getenv("SOBER_LOG_HANDLE")) {
        if (patch_handle_log(base) != 0) {
            sober_jni_shutdown(j);
            return -12;
        }
    }
    /* PHASE 4b: opt-in A/B for the earlier 8-byte variant. */
    if (getenv("SOBER_APP_SHELL_FIX")) {
        if (patch_app_shell_call_site(base) != 0) {
            sober_jni_shutdown(j);
            return -13;
        }
    }
    /* DECISION 031: the ONE approved targeted fix (opt-in). */
    if (getenv("SOBER_CALL_SITE_FIX")) {
        if (patch_call_site_fix(base) != 0) {
            sober_jni_shutdown(j);
            return -14;
        }
    }

    /* Decision 036: acquire a guest-owned handle from normal harness context
     * BEFORE any host object is written into the singleton global. */
    if (getenv("SOBER_HELPER_REPAIR_V2")) {
        if (sober_bionic_bootstrap_acquire_valid_handle((void *)base) != 0) {
            sober_jni_shutdown(j);
            return -16;
        }
    }
    /* Decision 034: JNI_OnLoad is skipped, so the guest's JNI singleton
     * global 0x72293f8 can hold stale code bytes. Seed it now, after all
     * constructors have run (seeding during bootstrap regressed init). */
    if (getenv("SOBER_SEED_JNI_SINGLETON")) {
        if (sober_bionic_bootstrap_seed_jni_singleton((void *)base) != 0) {
            sober_jni_shutdown(j);
            return -15;
        }
    }

    void *env = sober_jni_env(j);
    void *app_class = sober_jni_find_class(j,
        "com/roblox/engine/jni/NativeGLJavaInterface");

    /* DECISION 020: pass real JNI args using worker-2's helpers (full
     * guest-shaped A/B/C/D graph on every returned handle).
     * W27 host-only value fixes (LEDGER DEBT):
     *  - the authoritative Android capture passes TWO DISTINCT base dirs;
     *    create two real, existing Linux dirs under a per-run temp root.
     *  - channel is "" in the capture, not "LIVE".
     * No guest bytes are written and no shim path remapping is used. */
    char w27_root[] = "/tmp/sober-w27-XXXXXX";
    const char *w27_tmp = mkdtemp(w27_root);
    char w27_base1[128], w27_base2[128];
    if (w27_tmp) {
        snprintf(w27_base1, sizeof(w27_base1), "%s/data", w27_tmp);
        snprintf(w27_base2, sizeof(w27_base2), "%s/sdcard", w27_tmp);
        if (mkdir(w27_base1, 0700) != 0 || mkdir(w27_base2, 0700) != 0) {
            fprintf(stderr, "[app-shell] W27 base-dir mkdir FAILED\n");
        }
    } else {
        snprintf(w27_base1, sizeof(w27_base1), "/tmp");
        snprintf(w27_base2, sizeof(w27_base2), "/var/tmp");
    }
    fprintf(stderr, "[app-shell] W27 base dirs: '%s' '%s' (distinct=%d)\n",
            w27_base1, w27_base2, strcmp(w27_base1, w27_base2) != 0);
    void *s_base1 = sober_jni_new_string_utf8(j, w27_base1);
    void *s_base2 = sober_jni_new_string_utf8(j, w27_base2);
    void *s_ver   = sober_jni_new_string_utf8(j, "2.740.931");
    void *s_chan  = sober_jni_new_string_utf8(j, "");
    /* Lead's ruling: use REAL native handles from the frozen compat shim, in a
     * generous zeroed param blob, instead of a small synthetic blob. */
    void *am = (void *)AAssetManager_fromJava(NULL, NULL);
    void *cfg = (void *)AConfiguration_new();
    unsigned char params[0x1000];
    memset(params, 0, sizeof(params));
    memcpy(params + 0x00, &am, sizeof(am));
    memcpy(params + 0x08, &cfg, sizeof(cfg));
    memcpy(params + 0x10, &am, sizeof(am));
    memcpy(params + 0x18, &cfg, sizeof(cfg));
    /* worker-2's shared_ptr wrapper: guest-friendly vtable + refcount, with
     * the native handles in its payload (no bare A-handle passed here). */
    void *param_obj = sober_jni_new_shared_param(j,
        "com/roblox/engine/jni/NativeAppBridgeV2InitParams",
        sizeof(params), params);
    fprintf(stderr, "[app-shell] JNI args: base=%p,%p ver=%p chan=%p "
                    "am=%p cfg=%p shared_param_obj=%p (blob=%#zx)\n",
            s_base1, s_base2, s_ver, s_chan, am, cfg, param_obj, sizeof(params));

    /* n1-4 host-only: replay the three AVD-captured pre-BaseDirs natives in
     * captured order (#67, #105, #552).  Genuine guest natives via host bridge;
     * no guest bytes/gate/fake returns/path remaps.  Sample obj+0x28 read-only
     * after each one. */
    long rc;
    if (!getenv("SOBER_N1_NO_PREBASE_REPLAY")) {
        void *s_splash = sober_jni_new_string_utf8(j, "ActivitySplash");
        /* #552's argument is logged only as opaque [object Object]; use the
         * harness's real native-backed shared_param_obj (not a fabricated
         * empty object) as the closest genuine host-bridge value. */
        void *opaque = param_obj;
        rc = ((long (*)(void *, void *, void *))(base + 0x23239c5u))
                 (env, app_class, s_splash);
        fprintf(stderr, "[N14_REPLAY 1/3] nativeOnPreCreated 0x23239c5"
                        "(\"ActivitySplash\") -> rc=%ld\n", rc);
        n14_log_guard(base, "after nativeOnPreCreated");

        rc = ((long (*)(void *, void *, void *))(base + 0x2328066u))
                 (env, app_class, s_splash);
        fprintf(stderr, "[N14_REPLAY 2/3] nativeOnResumed 0x2328066"
                        "(\"ActivitySplash\") -> rc=%ld\n", rc);
        n14_log_guard(base, "after nativeOnResumed");

        if (!getenv("SOBER_N1_SKIP_THIRD")) {
          rc = ((long (*)(void *, void *, void *))(base + 0x2383c18u))
                   (env, app_class, opaque);
          fprintf(stderr, "[N14_REPLAY 3/3] nativePostClientSettingsLoaded"
                          "Initialization3 0x2383c18([object Object])"
                          " -> rc=%ld\n", rc);
          n14_log_guard(base, "after nativePostClientSettingsLoadedInitialization3");
        } else fprintf(stderr, "[N14_REPLAY 3/3] SKIPPED (diagnostic)\n");
    }

    if (getenv("SOBER_N15_SCAN")) n15_scan_closures(base);

    /* Generic SysV calls: first 6 pointer args in registers, remainder on the
     * stack. Each JNI static native method takes (JNIEnv*, jclass, ...). */
    rc = ((long (*)(void *, void *, void *, void *))(base + 0x24650a9u))
             (env, app_class, s_base1, s_base2);
    fprintf(stderr, "[app-shell] guest call 0x24650a9 nativeSetBaseDataDirectories"
                    "(base1,base2) -> rc=%ld\n", rc);

    if (!w28_order_done) {
      rc = ((long (*)(void *, void *, void *))(base + 0x232d484u))
               (env, app_class, s_ver);
      fprintf(stderr, "[app-shell] guest call 0x232d484 nativeSetRobloxVersion"
                      "(ver) -> rc=%ld\n", rc);

      rc = ((long (*)(void *, void *, void *))(base + 0x2326e2eu))
               (env, app_class, s_chan);
      fprintf(stderr, "[app-shell] guest call 0x2326e2e nativeSetRobloxChannel"
                      "(chan) -> rc=%ld\n", rc);
    } else {
      fprintf(stderr, "[app-shell] W28_ORDER skipped duplicate Version/Channel\n");
    }

    int real_path = getenv("SOBER_REAL_APPSTATE_PATH") != NULL;
    /* n1-3 rule 6 (host-only): replay the two natives missing from the harness
     * in AVD-captured order, after BaseDirs (#612):
     *   #613 nativeSetPreferencesFile("prefs")
     *   #614 nativeInitializeNativeFlags(String[] flag names from capture)
     * No guest bytes/gate writes, no fake returns, no path remapping. */
    if (!getenv("SOBER_N1_NO_MISSING_NATIVES")) {
        void *s_prefs = sober_jni_new_string_utf8(j, "prefs");
        rc = ((long (*)(void *, void *, void *))(base + 0x246515du))
                 (env, app_class, s_prefs);
        fprintf(stderr, "[app-shell] N1_RULE6 1 nativeSetPreferencesFile "
                        "0x246515d(\"prefs\") -> rc=%ld\n", rc);

        void *flags = NULL;
        if (sober_jni_arrays_is_active()) {
            /* verbatim tokens from w12-j2n-300.log:629 (last is truncated there) */
            const char *flag_names[] = {
                "EnableAndroidBinaryChannelDownloadTiming",
                "PgsTreatmentActive",
                "EnableLazyWorkManagerInit",
                "EnableAppPlatformQoSEmergencyOnAnd..."
            };
            flags = sober_jni_new_string_array(j, flag_names, 4);
        }
        if (!flags)
            flags = sober_jni_new_native_object(j, "[Ljava/lang/String;",
                                                0x80, NULL);
        rc = ((long (*)(void *, void *, void *))(base + 0x2465210u))
                 (env, app_class, flags);
        fprintf(stderr, "[app-shell] N1_RULE6 2 nativeInitializeNativeFlags "
                        "0x2465210(String[4]) -> rc=%ld\n", rc);
    }

    if (real_path) {
        /* The flag override must be stored and APPLIED before game-global init,
         * otherwise the FFlag registry has already been built without it. */
        real_appstate_prepare(base, env, app_class, j);
    }

    if (!w28_order_done) {
      rc = call_jni_static(base + 0x233762cu, env, app_class);
      fprintf(stderr, "[app-shell] guest call 0x233762c nativeGameGlobalInit() "
                      "-> rc=%ld\n", rc);
    } else {
      fprintf(stderr, "[app-shell] W28_ORDER skipped duplicate GameGlobalInit\n");
    }
    if (getenv("SOBER_GSEED_DIAG"))
        fprintf(stderr, "[app-shell] gseed diag: global@0x72a0520 = %p\n",
                (void *)*(uintptr_t *)(base + 0x72a0520u));

    /* Lead #516: MessageBus pre-init.  The real driver inside
     * nativeAppBridgeStartLuaAppDM 0x25448f6 reaches the mode-4 accessor
     * 0x24a8e44(4), which CONSTRUCTS the MessageBus singleton 0x72cbdd0 before
     * the id=1 registration at 0x24ad699 -> 0x253cb56 consumes its one-shot
     * guard 0x72a31ba.  Calling it earlier through genuine guest code avoids
     * spending that guard while the bus is uninitialised.  Opt-in/diagnostic:
     * it seeds guest state and forces the marker withhold. */
    if (getenv("SOBER_MESSAGEBUS_PREINIT")) {
        ((void (*)(void *, void *))(base + 0x25448f6u))(env, app_class);
        fprintf(stderr, "[app-shell] MessageBus pre-init via 0x25448f6 "
                        "-> bus=%p guard=%u\n",
                (void *)*(uintptr_t *)(base + 0x72cbdd0u),
                (unsigned)*(volatile unsigned char *)(base + 0x72a31bau));
    }

    /* Scale-wave: prime the MessageBus service directly through the guest's
     * OWN internal accessor 0x24a8e44(4) before InitWithParams spends the id=1
     * registration guard.  The real Java path primes it via nativeAppBridgeAppStart
     * 0x247dc8d, which our harness previously called too late.  Opt-in/withheld. */
    if (getenv("SOBER_MESSAGEBUS_PRIME")) {
        void *bus = ((void *(*)(int))(base + 0x24a8e44u))(4);
        fprintf(stderr, "[app-shell] MessageBus prime 0x24a8e44(4) -> %p "
                        "guard=%u\n", bus,
                (unsigned)*(volatile unsigned char *)(base + 0x72a31bau));
    }

    rc = ((long (*)(void *, void *, void *))(base + 0x24abf96u))
             (env, app_class, param_obj);
    g_ck_rung2_v2_init = 1;
    fprintf(stderr, "[app-shell] guest call 0x24abf96 nativeAppBridgeV2InitWithParams"
                    "(param_obj) -> rc=%ld\n", rc);
    if (getenv("SOBER_APP_SLOT_DIAG"))
        log_app_slots(base, "after InitWithParams");

    /* ---- app-pointer-dependent default modules (task #477) ----------------
     * InitWithParams has now produced the static app wrapper, so the
     * *_from_wrapper() installers can discover app = wrapper+0x20 themselves.
     * Order: genuine app+0x488 object, then the +0x408 delegate, then the
     * +0x440 controller (which verifies both and drives the real installer). */
    void *o488 = NULL;
    int dd_rc = SOBER_CD_DISABLED, cd_rc = SOBER_CD_DISABLED;
    if (sober_singleton_default_ptr((void *)base)) {
        o488 = sober_app488_default_install_from_wrapper((void *)base);
        fprintf(stderr, "[app-shell] default app+0x488 -> %p\n", o488);
        dd_rc = sober_delegate_default_install_from_wrapper((void *)base);
        fprintf(stderr, "[app-shell] default app+0x408 delegate -> rc=%d\n", dd_rc);
        cd_rc = sober_controller_default_install((void *)base);
        fprintf(stderr, "[app-shell] default app+0x440 controller -> rc=%d\n", cd_rc);
    } else {
        /* Don't re-enter guest construction (0x253c93c / 0x24ad50a) with a
         * NULL FFlag control block: that faults at 0x2e5d42a. */
        fprintf(stderr, "[app-shell] default app488/delegate/controller SKIPPED: "
                        "singleton prerequisite rc=%d\n", sd_rc);
    }

    /* Genuine scheduler nesting prime on the FunctionMarshaller attach. The
     * module's on_attach() self-gates on SOBER_SCHEDULER_GUEST=1; the true
     * attach-site call belongs in the JavaVM attach path (lead-owned), so this
     * harness-side call is the earliest point available to us. */
    sober_scheduler_guest_on_attach("FunctionMarshaller");
    if (getenv("SOBER_SCHED_DIAG")) {
        char sg_stats_early[512];
        sober_scheduler_guest_stats(sg_stats_early, sizeof(sg_stats_early));
        fprintf(stderr, "[app-shell] scheduler diag (post-attach): %s\n",
                sg_stats_early);
    }

    int alt_path = getenv("SOBER_ALT_CALLBACK_PATH") != NULL;
    if (alt_path) {
        /* DECISION C: nativeAppBridgeAppStart reaches the broken 0x233319a
         * object path, so skip it in this diagnostic mode. */
        fprintf(stderr, "[app-shell] ALT PATH: skipping 0x247dc8d "
                        "nativeAppBridgeAppStart (broken object path)\n");
    } else {
        if (getenv("SOBER_TRACE_FREE_SLOT"))
            (void)sober_bionic_bootstrap_trace_free_slot((void *)base);
        /* Java declaration (classes2.dex, verified):
         * nativeAppBridgeAppStart(String baseUrl, String userAgent,
         *                         boolean arc, String androidId,
         *                         String platform, String unused) */
        void *s_url  = sober_jni_new_string_utf8(j, "https://www.roblox.com");
        void *s_ua   = sober_jni_new_string_utf8(j, "Roblox/Android");
        void *s_aid  = sober_jni_new_string_utf8(j, "0000000000000000");
        void *s_plat = sober_jni_new_string_utf8(j, "AppAndroidV");
        void *s_ext  = sober_jni_new_string_utf8(j, "");
        rc = ((long (*)(void *, void *, void *, void *, int,
                        void *, void *, void *))(base + 0x247dc8du))
                 (env, app_class, s_url, s_ua, 0, s_aid, s_plat, s_ext);
        g_ck_rung2_app_start = 1;
        fprintf(stderr, "[app-shell] guest call 0x247dc8d nativeAppBridgeAppStart"
                        "(url,ua,false,androidid,platform,empty) -> rc=%ld\n", rc);
        if (getenv("SOBER_APP_SLOT_DIAG"))
            log_app_slots(base, "after AppStart");
    }

    /* The guest's own init can reset the global string after the early seed;
     * SOBER_GSEED_FORCE re-applies it at the late boundary (diagnostic only). */
    if (getenv("SOBER_GSEED_FORCE")) {
        int late = sober_seed_global_string(j, (void *)base);
        fprintf(stderr, "[app-shell] global-string late re-seed -> rc=%d\n", late);
    }
    if (real_path) {
        real_appstate_start_lua(base, env, app_class);
        if (getenv("SOBER_APP_SLOT_DIAG"))
            log_app_slots(base, "after StartLuaAppDM");
    }

    /* Java signature: nativeScheduleOnFirstFrame(Runnable).  The guest stores
     * a weak ref and dispatches it when the first frame task runs. */
    void *runnable = NULL;
    if (getenv("SOBER_SCHEDULE_RUNNABLE")) {
        runnable = sober_jni_new_native_object(j, "java/lang/Runnable", 8, NULL);
        (void)sober_jni_set_callback_handler(j, "run", run_callback, NULL);
    }
    rc = ((long (*)(void *, void *, void *))(base + 0x2ebf950u))
             (env, app_class, runnable);
    fprintf(stderr, "[app-shell] guest call 0x2ebf950 nativeScheduleOnFirstFrame"
                    "(runnable=%p) -> rc=%ld\n", runnable, rc);

    /* Java follows nativeAppBridgeAppStart with
     * nativeAppBridgeV2StartAppWithParams(StartAppParams) (uh/f;->E()).
     * Opt-in until it is proven not to disturb the strict gate. */
    if (getenv("SOBER_START_APP") || real_path) {
        void *params = sober_startapp_params_make(j, (void *)base);
        if (params)
            fprintf(stderr, "[app-shell] faithful StartAppParams built by "
                            "SOBER_STARTAPP_PARAMS\n");
        else
            params = sober_jni_new_native_object(j,
                "com/roblox/engine/jni/autovalue/StartAppParams", 0x100, NULL);
        if (!params && real_path) {
            (void)sober_jni_set_callback_handler(j, "surface", real_path_get, j);
            (void)sober_jni_set_callback_handler(j, "platformParams", real_path_get, j);
            (void)sober_jni_set_callback_handler(j, "vrContext", real_path_get, j);
            (void)sober_jni_set_callback_handler(j, "appStarterPlace", real_path_get, j);
            (void)sober_jni_set_callback_handler(j, "appStarterScript", real_path_get, j);
            (void)sober_jni_set_callback_handler(j, "appUserId", real_path_get, j);
            (void)sober_jni_set_callback_handler(j, "isUnder13", real_path_get, j);
            (void)sober_jni_set_callback_handler(j, "username", real_path_get, j);
            (void)sober_jni_set_callback_handler(j, "membershipType", real_path_get, j);
            (void)sober_jni_set_callback_handler(j, "selectedTheme", real_path_get, j);
            fprintf(stderr, "[app-shell] real-appstate: registered all ten "
                            "StartAppParams getters\n");
        } else if (!params) {
            (void)sober_jni_set_callback_handler(j, "surface", start_app_get, j);
            (void)sober_jni_set_callback_handler(j, "platformParams", start_app_get, j);
            (void)sober_jni_set_callback_handler(j, "vrContext", start_app_get, j);
        }
        if (getenv("SOBER_SEED_USER_CONTROLLER"))
            (void)seed_user_controller(base);
        rc = ((long (*)(void *, void *, void *))(base + 0x26fe80fu))
                 (env, app_class, params);
        fprintf(stderr, "[app-shell] guest call 0x26fe80f "
                        "nativeAppBridgeV2StartAppWithParams(%p) -> rc=%ld\n",
                params, rc);
    }

    /* Task #468: replay the Lua app start driver plus the two never-called JNI
     * entries it depends on. All real guest exports; no thunk, no marker. */
    if (getenv("SOBER_LUA_APP_START")) {
        void *surf = sober_jni_new_native_object(j, "android/view/Surface", 16, NULL);
        void *plat = sober_jni_new_native_object(j,
            "com/roblox/engine/jni/model/PlatformParams", 0x40, NULL);
        call_jni_static(base + 0x25448f6u, env, app_class);
        fprintf(stderr, "[app-shell] guest call 0x25448f6 "
                        "nativeAppBridgeStartLuaAppDM\n");
        ((void (*)(void *, void *, void *, void *))(base + 0x2773194u))
            (env, app_class, surf, plat);
        fprintf(stderr, "[app-shell] guest call 0x2773194 "
                        "UpdateSurfaceAppWithPlatformParams\n");
        void *tag = sober_jni_new_string_utf8(j, "sober-native");
        ((void (*)(void *, void *, int, void *))(base + 0x2eba91cu))
            (env, app_class, 0, tag);
        fprintf(stderr, "[app-shell] guest call 0x2eba91c "
                        "setTaskSchedulerBackgroundMode(false,...)\n");
    }

    /* Task #466: drive the genuine game-loaded path (no ALT thunk). These are
     * real JNI exports; the notifier 0x27eaf6e fires gameLoadedCallback(J)V
     * through its registered slot only when the game actually loads. */
    if (getenv("SOBER_GAME_LOADED_PATH")) {
        void *sg = NULL;
        if (getenv("SOBER_STARTGAME_V2")) {
            sg = sober_startgame_v2_make(j, (void *)base);
            (void)sober_startgame_v2_reclaim(j);  /* last-write-wins guard */
        }
        else if (getenv("SOBER_GAME_PARAMS"))
            sg = sober_game_params_make(j, (void *)base);
        if (!sg) {
            sg = sober_jni_new_native_object(j,
                "com/roblox/engine/jni/autovalue/StartGameParams", 0x100, NULL);
        }
        /* Default-ON placeId bridge: register all 24 genuine StartGameParams
         * getters (placeId()J returns a real jlong, never a jstring) as the last
         * claim before the StartGameWithParam RVA reads them. */
        int pid_rc = sober_placeid_default_install(j, (void *)base);
        (void)sober_placeid_default_reclaim(j);
        fprintf(stderr, "[app-shell] default placeId getters -> rc=%d\n", pid_rc);
        rc = ((long (*)(void *, void *, void *))(base + 0x2ebd51au))
                 (env, app_class, sg);
        fprintf(stderr, "[app-shell] guest call 0x2ebd51a "
                        "nativeAppBridgeV2StartGameWithParam -> rc=%ld\n", rc);
        /* Default-ON MessageBus publication: genuine dispatch at 0x27eaf04
         * drives the notifier 0x27eaf6e -> gameLoadedCallback path. The placeId
         * comes from the guest's own cached global (0x72a31c0). */
        /* Worker-3 #500: read-only latch-ready prerequisite report and
         * app+0x488 readiness query; neither seeds state nor emits a marker. */
        char prereq_buf[256];
        int prereq_rc = sober_publish_default_prereqs(
            (void *)base, NULL, prereq_buf, sizeof(prereq_buf));
        fprintf(stderr, "[app-shell] MessageBus prereqs -> rc=%d %s\n",
                prereq_rc, prereq_buf);
        fprintf(stderr, "[app-shell] app+0x488 ready -> %d\n",
                sober_app_singleton_app488_ready((void *)base));
        int pub_rc = sober_publish_default_publish_seeded((void *)base);
        fprintf(stderr, "[app-shell] default MessageBus publish -> rc=%d\n", pub_rc);
        void *g1 = sober_jni_new_string_utf8(j, "AppShellNotifications");
        void *g2 = sober_jni_new_string_utf8(j, "123456789");
        void *g3 = sober_jni_new_string_utf8(j, "StartConversationWithUserId");
        ((void (*)(void *, void *, void *, void *, void *))
            (base + 0x2ebca9eu))(env, app_class, g1, g2, g3);
        fprintf(stderr, "[app-shell] guest call 0x2ebca9e "
                        "nativeAppBridgeV2SendAppEventOnGameLoaded\n");
    }

    /* WITHHELD DIAGNOSTIC: drive the guest's own frame-dispatch helper
     * (0x6493242 / 0x6490488) so guest-tagged swaps and GLES draws can be
     * observed. This executes real guest instructions against a synthetic
     * device object; it is NOT app-shell proof. */
    if (getenv("SOBER_FRAME_DRIVE")) {
        /* F5 guest window publish is not wired here: in this configuration it
         * faults at 0x2e5c3c1 before the frame-drive diagnostic. Keep it as an
         * opt-in module for a separate A/B; the render gate stays fail-closed. */
        struct sober_frame_dispatch_config fd;
        memset(&fd, 0, sizeof(fd));
        fd.guest_base = (void *)base;
        fd.render_ctx = sober_render_guest_context();
        int frc = sober_frame_dispatch_install(&fd);
        if (!frc) frc = sober_frame_dispatch_run(2);
        fprintf(stderr, "[app-shell] frame dispatch -> rc=%d\n", frc);
        int drc = sober_frame_draw_install((uintptr_t)base);
        if (!drc) drc = sober_frame_draw_run((uintptr_t)base, NULL);
        struct SoberFrameDispatchRaEvidence fe;
        memset(&fe, 0, sizeof(fe));
        sober_frame_dispatch_ra_evidence(&fe);
        fprintf(stderr, "[app-shell] frame draw -> rc=%d\n", drc);
        fprintf(stderr,
                "[app-shell] frame counters: dispatch swaps total=%llu guest=%llu "
                "ra_rva=%#lx expected=%d | draw guest=%llu\n",
                (unsigned long long)sober_frame_dispatch_swaps_total(),
                (unsigned long long)sober_frame_dispatch_swaps_guest_tagged(),
                (unsigned long)fe.ra_rva, fe.expected_ra,
                (unsigned long long)sober_frame_draw_guest_draws());
    }

    /* Engine worker threads may deliver the real app-shell callback after the
     * startup entry points return.  Keep the bridge alive and report arrivals. */
    const char *wait_s = getenv("SOBER_APP_SHELL_WAIT_MS");
    if (wait_s) {
        long wait_ms = atol(wait_s);
        if (wait_ms > 0) {
            uint64_t prev = sober_jni_callback_count(j);
            for (long elapsed = 0; elapsed < wait_ms; elapsed += 100) {
                struct timespec ts = { .tv_sec = 0, .tv_nsec = 100000000L };
                nanosleep(&ts, NULL);
                uint64_t now = sober_jni_callback_count(j);
                if (now != prev) {
                    fprintf(stderr, "[app-shell] callback count %llu -> %llu "
                                    "last='%s'\n",
                            (unsigned long long)prev, (unsigned long long)now,
                            sober_jni_last_callback(j));
                    prev = now;
                }
            }
            fprintf(stderr, "[app-shell] wait complete: callbacks=%llu last='%s'\n",
                    (unsigned long long)sober_jni_callback_count(j),
                    sober_jni_last_callback(j));
        }
    }

    if (alt_path) {
        /* Exercise the guest's genuine gameLoadedCallback thunk directly with
         * seeded JNI handles. This may satisfy the coded callback gate, but it
         * is HARNESS-TRIGGERED -- it does NOT prove a login/app-shell frame. */
        void *method = sober_jni_get_method_id(j, app_class,
                                               "gameLoadedCallback", "(J)V");
        fprintf(stderr, "[app-shell] ALT PATH: gameLoadedCallback method=%p\n", method);
        long arc = ((long (*)(void *, void *, void *, void *, void *, void *))
                        (base + 0x25848e0u))
                       (env, app_class, method, 0, 0, 0);
        fprintf(stderr, "[app-shell] ALT PATH guest call 0x25848e0 "
                        "gameLoadedCallback(env,class,method,0,0,0) -> rc=%ld\n", arc);
    }

    fprintf(stderr, "[app-shell] %s\n",
            sober_jni_describe_activity(j, pbuf, sizeof(pbuf)));

    /* DECISION 013/038 gate: a NOTIFIER-DRIVEN guest->Java callback plus a
     * GUEST-TAGGED render swap must both be present. A name-only counter is no
     * longer sufficient (decision 038 §2): the positive credential comes from
     * the fail-closed provenance gate. The harness-thunk verdict and every
     * manufactured-state knob disqualify the run below. */
    int shell_cb = sober_provenance_gate_is_genuine("gameLoadedCallback")
                || sober_provenance_gate_is_genuine("onAppShellReloadNeeded");
    if (sober_provenance_gate_is_harness_thunk()) shell_cb = 0;
    /* Name-only observation is kept as a diagnostic, never as proof. */
    if (getenv("SOBER_APP_SHELL_DIAGNOSTIC"))
        fprintf(stderr, "[app-shell] names: onAppShellReloadNeeded=%d "
                        "gameLoadedCallback=%d\n",
                sober_jni_callback_was_called(j, "onAppShellReloadNeeded"),
                sober_jni_callback_was_called(j, "gameLoadedCallback"));
    /* DECISION 013 hardening (task #452): any synthetic guest-state seed from
     * the phase-7 diagnostics makes the startup state harness-manufactured, so
     * it can never justify the real marker, regardless of which callbacks the
     * bridge happened to observe. These knobs are all off by default. */
    int seeded_state = getenv("SOBER_ABRT_DIAG") ||
                       getenv("SOBER_CRASHPAD_RETURN") ||
                       getenv("SOBER_CRASHPAD_RETURN_ARGV") ||
                       getenv("SOBER_CRASHPAD_RETURN_PASSCRED") ||
                       getenv("SOBER_CRASHPAD_RETURN_STDIO") ||
                       sober_crashpad_return_should_withhold() ||
                       getenv("SOBER_MESSAGEBUS_PREINIT") ||
                       getenv("SOBER_MESSAGEBUS_PRIME") ||
                       getenv("SOBER_PREINIT_BUS") ||
                       getenv("SOBER_PROVENANCE_GATE_GUEST_BASE") ||
                       getenv("SOBER_FRAME_CALLBACKS") ||
                       getenv("SOBER_FRAME_CALLBACKS_GUEST_ND") ||
                       sober_frame_callbacks_should_withhold() ||
                       getenv("SOBER_FRAME_EGL_INIT_DRIVE") ||
                       getenv("SOBER_FRAME_FLOG_ENABLE") ||
                       getenv("SOBER_FRAME_FLOG_SINK") ||
                       getenv("SOBER_FLAGS_LATCH_DEFAULT") ||
                       getenv("SOBER_FLAGS_LATCH_ROUTE") ||
                       getenv("SOBER_FLAGS_LATCH_CS_JSON") ||
                       getenv("SOBER_FLAGS_LATCH_ARGV") ||
                       sober_flags_latch_default_should_withhold() ||
                       sober_frame_evidence_should_withhold() ||
                       getenv("SOBER_FIELDS_NATIVE_DEFAULT") ||
                       sober_fields_native_should_withhold() ||
                       getenv("SOBER_SD_DEBUG") ||
                       getenv("SOBER_ALT_CALLBACK_PATH") ||
                       getenv("SOBER_START_APP") ||
                       getenv("SOBER_SCHEDULE_RUNNABLE") ||
                       getenv("SOBER_SCHED_RVA") ||
                       getenv("SOBER_TLS_BYTES") ||
                       getenv("SOBER_STARTGAME_V2") ||
                       getenv("SOBER_CLIENT_SETTINGS_INIT") ||
                       getenv("SOBER_SETTINGS_JSON") ||
                       getenv("SOBER_GAME_PARAMS") ||
                       getenv("SOBER_CANONICAL_JSTRINGS") ||
                       getenv("SOBER_SEED_USER_CONTROLLER") ||
                       getenv("SOBER_SEED_APP_DELEGATE") ||
                       getenv("SOBER_FAITHFUL_APP_DELEGATE") ||
                       getenv("SOBER_SEED_APP_488") ||
                       getenv("SOBER_SEED_JNI_VM") ||
                       getenv("SOBER_FAITHFUL_JAVAVM") ||
                       getenv("SOBER_NATIVE_OBJECTS") ||
                       getenv("SOBER_JSON_FLAG_BRIDGE") ||
                       getenv("SOBER_JSON_APP_SINGLETON") ||
                       getenv("SOBER_SEED_GLOBAL_STRING") ||
                       getenv("SOBER_GSEED_FORCE") ||
                       getenv("SOBER_GSEED_DIAG") ||
                       getenv("SOBER_REPLAY_FFLAG_CTOR") ||
                       getenv("SOBER_NATIVE_FLAGS_INIT") ||
                       getenv("SOBER_BIONIC_SF_FULL") ||
                       getenv("SOBER_SCHED_R70_KEEP") ||
                       getenv("SOBER_SCHED_NEST_ONE") ||
                       getenv("SOBER_LOG_APP_BRIDGE_NOTIFICATION") ||
                       getenv("SOBER_GAME_LOADED_PATH") ||
                       getenv("SOBER_LUA_APP_START") ||
                       getenv("SOBER_STARTAPP_PARAMS") ||
                       getenv("SOBER_STARTAPP_V2") ||
                       getenv("SOBER_APP_SINGLETON_BRIDGE") ||
                       getenv("SOBER_REAL_APPSTATE_PATH") ||
                       /* verifier #471: remaining guest-mutating opt-ins that
                        * could otherwise produce a false green. */
                       getenv("SOBER_SEED_JNI_SINGLETON") ||
                       getenv("SOBER_REAL_APPSTATE_FLAG") ||
                       getenv("SOBER_REAL_APPSTATE_POSTSETTINGS") ||
                       getenv("SOBER_TLS_SEED") ||
                       getenv("SOBER_STUB_TSD_SEED") ||
                       getenv("SOBER_HELPER_REPAIR_V2") ||
                       getenv("SOBER_NO_SCHED_NEUTRAL") ||
                       getenv("SOBER_VALIDATOR_PATCH") ||
                       getenv("SOBER_FINAL_PATCH") ||
                       getenv("SOBER_FIELD_NORMALISER") ||
                       getenv("SOBER_OBJECT_FIX") ||
                       getenv("SOBER_ALLOC_SITE_FIX") ||
                       getenv("SOBER_LOG_HANDLE") ||
                       getenv("SOBER_APP_SHELL_FIX") ||
                       getenv("SOBER_CALL_SITE_FIX") ||
                       /* D02 Tier-1: every remaining guest-memory/ctor/bridge
                        * mutator across all modules must withhold the marker. */
                       getenv("SOBER_BIONIC_TSD_GOT") ||
                       getenv("SOBER_APP488_FIX") ||
                       getenv("SOBER_PLATFORM_PARAMS_FIELDS") ||
                       getenv("SOBER_HANDLE_SANITISER") ||
                       getenv("SOBER_NO_HANDLE_SANITISER") ||
                       getenv("SOBER_CALLER_PTR_FORCE") ||
                       getenv("SOBER_HELPER_REPAIR") ||
                       getenv("SOBER_HELPER_LOG") ||
                       getenv("SOBER_FINNER_LOG") ||
                       getenv("SOBER_FINNER_BP") ||
                       getenv("SOBER_CURL_HOLDER_FIX") ||
                       getenv("SOBER_CALLER_PTR_FIX") ||
                       getenv("SOBER_BOOTSTRAP_INTERPOSE") ||
                       getenv("SOBER_BOOTSTRAP_NO_BRIDGES") ||
                       getenv("SOBER_BOOTSTRAP_NO_SEEDS") ||
                       getenv("SOBER_NO_DIRECT_SEED_EMPTY") ||
                       getenv("SOBER_SEED_MASK") ||
                       getenv("SOBER_CTOR0") ||
                       getenv("SOBER_CTOR1") ||
                       getenv("SOBER_CTOR_RANGES") ||
                       getenv("SOBER_GUEST_ROUTE_A") ||
                       getenv("SOBER_APP_SINGLETON_STRATEGY") ||
                       getenv("SOBER_APP_SINGLETON_RELEASE_OLD") ||
                       /* D02 Tier 2: any fake value override for a withheld
                        * guest-object module withholds the marker too. */
                       env_prefix_active("SOBER_GAME_") ||
                       env_prefix_active("SOBER_STARTAPP_") ||
                       env_prefix_active("SOBER_SGV2_") ||
                       env_prefix_active("SOBER_PLATFORM_PARAMS_") ||
                       getenv("SOBER_CALLBACK_PROVENANCE") ||
                       getenv("SOBER_STATIC_OBJECT_CALLS") ||
                       getenv("SOBER_SCALAR_RETURNS") ||
                       getenv("SOBER_ALLOC_CONTRACT_FIX") ||
                       getenv("SOBER_NOTIFIER_PROBE") ||
                       getenv("SOBER_APP488_INNER") ||
                       getenv(SOBER_FAITHFUL_JNI_ARRAYS_ENV) ||
                       getenv("SOBER_SINGLETON_SERVICES") ||
                       getenv("SOBER_SINGLETON_SERVICES_GATE") ||
                       getenv("SOBER_SINGLETON_SERVICES_FRESH") ||
                       getenv("SOBER_SINGLETON_SERVICES_RELEASE_OLD") ||
                       getenv("SOBER_SINGLETON_FIX") ||
                       getenv("SOBER_SINGLETON_FIX_GATE") ||
                       getenv("SOBER_SINGLETON_FIX_FRESH") ||
                       getenv("SOBER_SINGLETON_FIX_PROBE") ||
                       getenv("SOBER_SINGLETON_FIX_VERBOSE") ||
                       /* A8 residual: Tier-3 guest-visible config seeds. */
                       getenv("SOBER_ASSET_ROOT") ||
                       getenv("SOBER_SCREEN_WIDTH_DP") ||
                       getenv("SOBER_SCREEN_HEIGHT_DP") ||
                       getenv("SOBER_SCREEN_SIZE") ||
                       getenv("SOBER_NAV_HIDDEN") ||
                       getenv("SOBER_LANG") ||
                       getenv("SOBER_COUNTRY") ||
                       getenv("SOBER_WINDOW_WIDTH") ||
                       getenv("SOBER_WINDOW_HEIGHT") ||
                       getenv("SOBER_GUEST_PATH") ||
                       getenv("SOBER_CALLBACK_PROVENANCE_GUEST_BASE") ||
                       env_prefix_active("SOBER_PROP_") ||
                       /* Task #477: every knob/modifier of the newly wired
                        * default-ON modules forces a withhold. Setting any of
                        * them means the guest state was harness-tuned, so it
                        * can never justify the real marker. */
                       getenv("SOBER_TSD_DEFAULT") ||
                       getenv("SOBER_JAVAVM_DEFAULT") ||
                       getenv("SOBER_FFLAG_DEFAULT") ||
                       getenv("SOBER_SCHEDULER_GUEST") ||
                       getenv("SOBER_AVD_BOUNDARY_REPLAY") ||
                       getenv("SOBER_AVD_BOUNDARY_UNVERIFIED") ||
                       getenv("SOBER_AVD_BOUNDARY_PATH") ||
                       getenv("SOBER_STOP_BEFORE_SETTINGS") ||
                       getenv("SOBER_HW_WATCH") ||
                       getenv("SOBER_HW_WATCH_ADDR") ||
                       getenv("SOBER_SCHED_DIAG") ||
                       getenv("SOBER_SCHED_NEUTRAL") ||
                       getenv("SOBER_SINGLETON_DEFAULT") ||
                       getenv("SOBER_SINGLETON_DEFAULT_GATE") ||
                       getenv("SOBER_SINGLETON_DEFAULT_PROBE") ||
                       getenv("SOBER_SINGLETON_DEFAULT_FRESH") ||
                       getenv("SOBER_SINGLETON_DEFAULT_VERBOSE") ||
                       getenv("SOBER_APP488_DEFAULT") ||
                       getenv("SOBER_DELEGATE_DEFAULT") ||
                       getenv("SOBER_DELEGATE_SOURCE") ||
                       getenv("SOBER_DELEGATE_VERIFY") ||
                       getenv("SOBER_DELEGATE_CALL10") ||
                       getenv("SOBER_DELEGATE_VERBOSE") ||
                       getenv("SOBER_CONTROLLER_DEFAULT") ||
                       getenv("SOBER_CONTROLLER_DEFAULT_VERBOSE") ||
                       getenv("SOBER_CONTROLLER_DEFAULT_PROBE") ||
                       getenv("SOBER_CONTROLLER_DEFAULT_WAIT_MS") ||
                       getenv("SOBER_CONTROLLER_DEFAULT_STRICT_VPTR") ||
                       getenv("SOBER_PLACEID_DEFAULT") ||
                       getenv("SOBER_PLACEID_VALUE") ||
                       getenv("SOBER_PUBLISH_DEFAULT") ||
                       getenv("SOBER_PUBLISH_NODE_REDRIVE") ||
                       getenv("SOBER_RENDER_GUEST") ||
                       /* Authoritative modules wired by other roles; their
                        * knobs still force a withhold in this gate. */
                       getenv("SOBER_ALLOC_DEFAULT") ||
                       getenv("SOBER_FIELDS_DEFAULT") ||
                       getenv("SOBER_FIELDS_DEFAULT_DPI") ||
                       getenv("SOBER_FIELDS_DEFAULT_WIDTH_MM") ||
                       getenv("SOBER_FIELDS_DEFAULT_HEIGHT_MM") ||
                       getenv("SOBER_JNISLOTS_DEFAULT") ||
                       getenv("SOBER_FLAGS_LATCH_CRASHPAD") ||
                       getenv("SOBER_FLAG_REGISTRY_REPAIR") ||
                       /* Module verdicts: active manufactured state. The
                        * provenance gate is deliberately NOT here: it is the
                        * fail-closed positive credential, not a seed. */
                       sober_scheduler_guest_should_withhold() ||
                       sober_delegate_default_should_withhold() ||
                       sober_controller_default_should_withhold() ||
                       sober_publish_default_should_withhold() ||
                       sober_notifier_probe_should_withhold() ||
                       /* G3: frame injector knobs and verdict drivers. */
                       getenv("SOBER_RENDER_FRAME_EVIDENCE") ||
                       getenv("SOBER_EVIDENCE_SURFACE") ||
                       getenv("SOBER_EVIDENCE_DEVICE") ||
                       getenv("SOBER_EVIDENCE_REDRAW") ||
                       getenv("SOBER_RENDER_SUPPRESS_SYNTHETIC") ||
                       getenv("SOBER_FRAME_DRIVE") ||
                       getenv("SOBER_FRAME_DISPATCH") ||
                       getenv("SOBER_FRAME_DRAW") ||
                       getenv("SOBER_FRAME_REDRAW") ||
                       getenv("SOBER_FRAME_SURFACE") ||
                       getenv("SOBER_EVIDENCE_FRAME") ||
                       getenv("SOBER_FRAME_REDRAW_DRIVE") ||
                       env_prefix_active("SOBER_EVIDENCE_") ||
                       sober_frame_thread_should_withhold() ||
                       sober_frame_dispatch_should_withhold();
    /* Lead diagnostic (read-only): dump the scheduler-prime counters so a run
     * can prove whether the worker-scoped guest prime actually fired.  This
     * knob is in seeded_state above and prints no marker. */
    if (getenv("SOBER_SCHED_DIAG")) {
        char sg_stats[512];
        sober_scheduler_guest_stats(sg_stats, sizeof(sg_stats));
        fprintf(stderr, "[app-shell] scheduler diag: %s\n", sg_stats);
    }
    /* Wave11: distinct provenance class, NOT folded into seeded_state.
     * AVD-captured boundary input is host-supplied oracle data and therefore
     * fails closed on its own, while remaining separately identifiable. */
    int avd_boundary_provenance = sober_avd_boundary_should_withhold();
    if (avd_boundary_provenance) {
        char prov[256];
        sober_avd_boundary_provenance(prov, sizeof(prov));
        fprintf(stderr, "[app-shell] marker withheld: %s\n", prov);
        shell_cb = 0;
    }
    if (seeded_state) {
        fprintf(stderr, "[app-shell] marker withheld: diagnostic guest-state "
                        "seed active (no app-shell frame proven)\n");
        shell_cb = 0;
    } else if (!avd_boundary_provenance) {
        /* Worker-3 #494: emit only on a genuinely clean state.  The gate
         * function itself is fail-closed and emits nothing unless the notifier-
         * driven callback credential and placeId>0 are both genuine. */
        (void)sober_provenance_gate_emit_provenance();
        fprintf(stderr, "[app-shell] clean state: seeded_state=0 "
                        "(all opt-ins unset)\n");
    }
    /* Persisted phase-3 flag: stays valid after the render context shuts down,
     * unlike sober_render_get_active(). */
    /* Phase-3 evidence plus a swap whose return address was inside guest .text
     * (as tagged by render_guest's eglSwapBuffers hook). A host-originated
     * synthetic swap is explicitly not accept. */
    /* Lead's render evidence contract (analysis/MODULE_INTEGRATION.md §2):
     * phase-3 ancestry + a guest-tagged swap + a counted guest GLES draw + a
     * stable ANativeWindow binding. A synthetic-only swap fails closed. */
    /* F7/frame_evidence is the sole render verdict owner: phase3 ancestry,
     * stable guest-RA swap recurrence, bracketed guest draw, G8 window proof. */
    int render_ok = guest_render_ok();

    /* Gate-owned render provenance (wave5 F4).  Emitted ONLY on a fully clean,
     * genuine pass; seeded diagnostics never produce this line, so the strict
     * test's positive assertion cannot be satisfied by a withheld run. */
    if (!seeded_state && shell_cb && render_ok) {
        fprintf(stderr,
                "[app-shell] render provenance: guest-tagged frame at 0x649d422 "
                "(swaps=%llu, draws=%llu), phase3_done=1 -> OK\n",
                (unsigned long long)sober_frame_dispatch_swaps_guest_tagged(),
                (unsigned long long)sober_frame_draw_guest_draws());
    }

    if (!shell_cb || !render_ok) {
        fprintf(stderr,
                "[app-shell] marker withheld: callback=%d render=%d "
                "(env calls=%llu callbacks=%llu last='%s')\n",
                shell_cb, render_ok,
                (unsigned long long)sober_jni_env_call_count(j),
                (unsigned long long)sober_jni_callback_count(j),
                sober_jni_last_callback(j));
        if (getenv("SOBER_APP_SHELL_DIAGNOSTIC")) {
            fprintf(stderr, "[app-shell] DIAGNOSTIC ONLY (no marker): "
                            "JNI bridge exercised, app shell frame NOT proven\n");
        }
        fprintf(stderr, "[app-shell] shutting down JNI bridge\n");
        (void)sober_notifier_probe_uninstall();
        sober_jni_shutdown(j);
        fprintf(stderr, "[app-shell] JNI bridge shutdown complete\n");
        return -4;
    }

    if (alt_path) {
        fprintf(stderr,
                "[app-shell] HONESTY NOTICE: SOBER_ALT_CALLBACK_PATH exercised a "
                "genuine guest->Java callback via a harness-triggered thunk; "
                "NO login/app-shell frame is proven by this run.\n");
    }
    /* Checkpoint ladder: no single marker. Every rung below has an
     * AVD-captured reference (analysis/CHECKPOINT-LADDER.md). Rung 3 is
     * intentionally UNDEFINED until an authenticated AVD capture exists. */
    {
        int debt = avd_boundary_provenance || seeded_state || sober_avd_boundary_enabled();
        int rung1 = g_ck_rung1_returned &&
                    (*(volatile unsigned char *)(base + 0x7cdd149u) == 1);
        int rung2 = g_ck_rung2_app_start && g_ck_rung2_v2_init && g_ck_rung2_lua_dm;
        /* W22 LABEL ONLY: when the read-only boundary credential holds, print
         * the two-tier PASS-BOUNDARY-REPLAY label with the debt count.  No gate
         * logic changes: `rung1` above is left exactly as it was, and the
         * marker decision elsewhere is untouched.  Never print CLEAN while a
         * seeder slot or replayed argument is in use (DEBT != 0). */
        {
            const int boundary = rung1 &&
                (sober_w21_boundary_ok() ||
                 (g_w32_latch_before == 0 && g_w32_latch_after == 1 &&
                  g_w29_arg0_sha_ok));
            /* W29 INPUT FIRST: a SHA-mismatched / modified boundary input can
             * never earn a pass label.  Label-only change; no gate logic. */
            extern int g_w29_arg0_sha_ok;
            const int modified_input = !g_w29_arg0_sha_ok ||
                getenv(SOBER_AVD_BOUNDARY_UNVERIFIED) != NULL;
            if (modified_input) {
                fprintf(stderr,
                        "[checkpoint] rung1 MODIFIED-INPUT (no credit) "
                        "DEBT=%d\n", debt);
            } else if (boundary)
                fprintf(stderr,
                        "[checkpoint] rung1 PASS-BOUNDARY-REPLAY (DEBT=%d)\n",
                        debt);
            else
                fprintf(stderr,
                        "[checkpoint] rung1 client_settings_latch=%s "
                        "(AVD ref: w12-gate-avd.log) DEBT=%d\n",
                        rung1 ? "PASS" : "FAIL", debt);
        }
        fprintf(stderr, "[checkpoint] rung2 app_start_to_lua_dm=%s "
                        "(AVD ref: w12-j2n-300.log) DEBT=%d\n",
                rung2 ? "PASS" : "FAIL", debt);
        fprintf(stderr, "[checkpoint] rung3 UNDEFINED "
                        "(no authenticated AVD capture; placeId/surface/"
                        "StartGame/gameLoadedCallback not provable here)\n");
    }

    const char *v = getenv("SOBER_WINDOW_HOLD_MS");
    long ms = v ? atol(v) : 4000;
    if (ms > 0) {
        struct timespec ts = { .tv_sec = ms / 1000,
                               .tv_nsec = (ms % 1000) * 1000000L };
        nanosleep(&ts, NULL);
    }

    (void)sober_notifier_probe_uninstall();
    sober_jni_shutdown(j);
    return 0;
}
