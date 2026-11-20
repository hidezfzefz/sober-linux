/*
 * callback_provenance.c -- opt-in, observation-only provenance recorder for
 * the guest's `gameLoadedCallback(J)V` dispatch.
 *
 * Worker B11 (logical Team B, wave 2).  NEW FILE ONLY -- src/jni/native_jni.c,
 * src/linux/guest_app_shell.c, CMakeLists and the engine are NOT touched here.
 *
 * Target engine (BuildID 204ad049599c96afb75b8a21d5051063e821d6c9):
 *   thunk 0x25848e0 : push rbp/mov rbp,rsp; `call *0x470(%rax)` at 0x258495a,
 *                     whose return site is 0x2584960 (JNIEnv+0x470 == slot 142
 *                     == CallStaticVoidMethodV).
 *   notifier 0x27eaf6e : loads cached class base+0x72a2f48 and method
 *                     base+0x72a2fc0, `call 0x25848e0` at 0x27eafae, return
 *                     site 0x27eafb3.
 *   ALT path (SOBER_ALT_CALLBACK_PATH) calls base+0x25848e0 directly from host
 *   code: it also produces 0x2584960 but the frame above it is host code, not
 *   0x27eafb3.  That pair is the decision-013 discriminator.
 *
 * The module records the return-address pair and the class/method pointer
 * identity, and classifies the dispatch.  It never calls into the guest, never
 * registers a handler, never emits a callback and never changes a marker/gate.
 */

#include "callback_provenance.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- RVA contract (frozen engine 204ad049) ------------------------------ */
#define SOBER_PROV_METHOD_NAME          "gameLoadedCallback"
#define SOBER_PROV_THUNK_ENTRY_RVA      0x25848e0u  /* thunk entry (reference) */
#define SOBER_PROV_THUNK_CALL_RET_RVA   0x2584960u  /* call *0x470 return site */
#define SOBER_PROV_NOTIFIER_RET_RVA     0x27eafb3u  /* notifier -> thunk return */
#define SOBER_PROV_CACHED_CLASS_RVA     0x72a2f48u  /* cached jclass global */
#define SOBER_PROV_CACHED_METHOD_RVA    0x72a2fc0u  /* cached methodID global */

/* Number of pointer-sized stack words scanned upward from the observer frame
 * looking for the two return sites.  The genuine chain is only a few frames
 * deep, so 8 KiB is ample; the scan stops as soon as both are found. */
#define SOBER_PROV_SCAN_WORDS           1024u

/* --- state (all atomics; bridge and gate may live on different threads) -- */
static _Atomic int      g_active;        /* SOBER_CALLBACK_PROVENANCE=1 seen */
static _Atomic int      g_installed;     /* base resolved + observer ready */
static _Atomic uintptr_t g_guest_base;
static _Atomic uint64_t g_dispatches;    /* gameLoadedCallback observations */
static _Atomic int      g_genuine;       /* sticky: notifier chain observed */
static _Atomic int      g_harness_thunk; /* sticky: bare thunk observed */
static _Atomic uintptr_t g_thunk_ret;    /* absolute return-site addresses */
static _Atomic uintptr_t g_notifier_ret;
static _Atomic uintptr_t g_observed_class;
static _Atomic uintptr_t g_observed_method;

/* Find the guest image base: explicit env override, else the largest r-x
 * mapping (the same heuristic used by src/trace/native_trace.c). */
static uintptr_t prov_discover_guest_base(void)
{
    const char *env = getenv("SOBER_CALLBACK_PROVENANCE_GUEST_BASE");
    if (env && *env) return (uintptr_t)strtoull(env, NULL, 0);

    FILE *f = fopen("/proc/self/maps", "r");
    if (!f) return 0;
    char line[1024];
    uintptr_t best = 0;
    unsigned long long bestsz = 0;
    while (fgets(line, sizeof(line), f)) {
        unsigned long long lo = 0, hi = 0;
        char perms[8] = {0};
        if (sscanf(line, "%llx-%llx %7s", &lo, &hi, perms) != 3) continue;
        if (perms[0] == 'r' && perms[2] == 'x' &&
            (hi - lo) > bestsz && (hi - lo) > (100ULL << 20)) {
            bestsz = hi - lo;
            best = (uintptr_t)lo;
        }
    }
    fclose(f);
    return best;
}

/* Locate the guest call chain on the live stack.
 *
 * We do NOT use __builtin_return_address(N) across our own (possibly
 * frame-pointer-less) bridge frames.  Instead:
 *   1. find the thunk's `call *0x470(%rax)` return site (base+0x2584960) as a
 *      word on the stack -- it is pushed by the guest on BOTH the genuine and
 *      the ALT path;
 *   2. confirm the notifier return site (base+0x27eafb3) immediately above the
 *      thunk frame.  Two independent checks are used:
 *        (a) fixed-offset: the thunk prologue is `push rbp; push rbx;
 *            sub $0xd8,rsp`, so `call *0x470` pushes 0x2584960 at rbp_th-0xf0
 *            and the notifier's `call 0x25848e0` return sits at rbp_th+8,
 *            i.e. exactly 0xf0 (30 words) above 0x2584960 -- no frame pointer
 *            in our own C frames is needed;
 *        (b) frame-chain: the thunk keeps a frame pointer, so a saved-RBP word
 *            above 0x2584960 points to a frame whose +8 word is 0x27eafb3.
 *      Requiring one of these specific shapes (rather than "the value appears
 *      anywhere on the stack") avoids false positives from stale stack data.
 *      In the ALT path the word at +0xf0 is the host caller's return address,
 *      not 0x27eafb3, so neither check fires. */
static void prov_scan_stack(uintptr_t base,
                            int *out_thunk, uintptr_t *out_thunk_off,
                            int *out_notifier, uintptr_t *out_notifier_off)
{
    const uintptr_t thunk_ra = base + (uintptr_t)SOBER_PROV_THUNK_CALL_RET_RVA;
    const uintptr_t notifier_ra = base + (uintptr_t)SOBER_PROV_NOTIFIER_RET_RVA;

    volatile uintptr_t anchor = 0;
    /* Integer-address arithmetic (rather than array subscripting `&anchor`)
     * keeps this bounded stack read free of -Warray-bounds at -O2 -Werror. */
    const uintptr_t sp0 = (uintptr_t)&anchor;
    int thunk = 0, notifier = 0;
    uintptr_t thunk_off = 0, notifier_off = 0;

    for (uintptr_t i = 0; i < (uintptr_t)SOBER_PROV_SCAN_WORDS; ++i) {
        const uintptr_t slot_addr = sp0 + i * sizeof(uintptr_t);
        if (*(const volatile uintptr_t *)slot_addr != thunk_ra) continue;
        thunk = 1;
        thunk_off = i;

        /* (a) fixed-offset check: notifier return 0xf0 above thunk return.
         *     Tolerate a small window for compiler/prologue variation. */
        const uintptr_t hi = (i < (uintptr_t)SOBER_PROV_SCAN_WORDS - 40u)
                                 ? i + 40u : (uintptr_t)SOBER_PROV_SCAN_WORDS;
        for (uintptr_t j = i + 20u; j < hi; ++j) {
            if (*(const volatile uintptr_t *)(sp0 + j * sizeof(uintptr_t))
                == notifier_ra) {
                notifier = 1;
                notifier_off = j;
                break;
            }
        }

        /* (b) frame-chain fallback (guest thunk keeps rbp). */
        if (!notifier) {
            for (uintptr_t j = i + 1u;
                 j < (uintptr_t)SOBER_PROV_SCAN_WORDS && j < i + 512u; ++j) {
                const uintptr_t cand =
                    *(const volatile uintptr_t *)(sp0 + j * sizeof(uintptr_t));
                if (cand <= slot_addr || (cand & 7u) != 0) continue;
                if (cand - slot_addr > (1u << 20)) continue;
                if (*(const volatile uintptr_t *)(cand + 8u) != notifier_ra)
                    continue;
                notifier = 1;
                notifier_off = j;
                break;
            }
        }
        break; /* nearest thunk return site is the most relevant one */
    }

    *out_thunk = thunk;
    *out_thunk_off = thunk_off;
    *out_notifier = notifier;
    *out_notifier_off = notifier_off;
}

int sober_callback_provenance_install(void *guest_base)
{
    const char *env = getenv("SOBER_CALLBACK_PROVENANCE");
    const int on = (env && strcmp(env, "1") == 0);
    atomic_store_explicit(&g_active, on, memory_order_release);
    if (!on) return -1;                 /* default OFF: never observed */

    if (atomic_load_explicit(&g_installed, memory_order_acquire)) return 0;

    uintptr_t base = guest_base ? (uintptr_t)guest_base
                                : prov_discover_guest_base();
    if (!base) return -1;
    atomic_store_explicit(&g_guest_base, base, memory_order_release);
    atomic_store_explicit(&g_installed, 1, memory_order_release);

    fprintf(stderr,
            "[callback-provenance] enabled guest_base=0x%llx "
            "thunk_ret=base+0x%x notifier_ret=base+0x%x "
            "cached class=base+0x%x method=base+0x%x\n",
            (unsigned long long)base,
            SOBER_PROV_THUNK_CALL_RET_RVA, SOBER_PROV_NOTIFIER_RET_RVA,
            SOBER_PROV_CACHED_CLASS_RVA, SOBER_PROV_CACHED_METHOD_RVA);
    return 0;
}

void sober_callback_provenance_on_dispatch(const char *name, void *clazz,
                                           void *method)
{
    if (!atomic_load_explicit(&g_active, memory_order_acquire)) return;
    if (!atomic_load_explicit(&g_installed, memory_order_acquire)) {
        if (sober_callback_provenance_install(NULL) != 0) return;
    }
    if (!name || strcmp(name, SOBER_PROV_METHOD_NAME) != 0) return;

    const uintptr_t base = atomic_load_explicit(&g_guest_base,
                                                memory_order_acquire);
    if (!base) return;

    atomic_fetch_add_explicit(&g_dispatches, 1, memory_order_relaxed);

    /* Pointer identity: exactly the handles the guest cached in JNI_OnLoad. */
    void *cached_class = *(void **)((unsigned char *)base +
                                    SOBER_PROV_CACHED_CLASS_RVA);
    void *cached_method = *(void **)((unsigned char *)base +
                                     SOBER_PROV_CACHED_METHOD_RVA);
    const int class_ok = (cached_class != NULL && clazz == cached_class);
    const int method_ok = (cached_method != NULL && method == cached_method);

    int thunk = 0, notifier = 0;
    uintptr_t thunk_off = 0, notifier_off = 0;
    prov_scan_stack(base, &thunk, &thunk_off, &notifier, &notifier_off);

    atomic_store_explicit(&g_observed_class, (uintptr_t)clazz,
                          memory_order_relaxed);
    atomic_store_explicit(&g_observed_method, (uintptr_t)method,
                          memory_order_relaxed);
    if (thunk)
        atomic_store_explicit(&g_thunk_ret,
                              base + (uintptr_t)SOBER_PROV_THUNK_CALL_RET_RVA,
                              memory_order_relaxed);
    if (notifier)
        atomic_store_explicit(&g_notifier_ret,
                              base + (uintptr_t)SOBER_PROV_NOTIFIER_RET_RVA,
                              memory_order_relaxed);

    /* Genuine iff the thunk return site is nearer the dispatch than the
     * notifier return site (call order) and the cached handles were used.
     * A bare thunk return site (host return address above it) is the ALT path. */
    const int genuine = class_ok && method_ok && thunk && notifier &&
                        (thunk_off < notifier_off);
    const int harness = thunk && !notifier;
    if (genuine) atomic_store_explicit(&g_genuine, 1, memory_order_release);
    if (harness) atomic_store_explicit(&g_harness_thunk, 1, memory_order_release);

    fprintf(stderr,
            "[callback-provenance] %s: class=%p (cached=%p %s) "
            "method=%p (cached=%p %s) thunk_ret=0x%llx(%s,+%llu) "
            "notifier_ret=0x%llx(%s,+%llu) -> %s\n",
            SOBER_PROV_METHOD_NAME,
            clazz, cached_class, class_ok ? "match" : "MISMATCH",
            method, cached_method, method_ok ? "match" : "MISMATCH",
            (unsigned long long)(base + (uintptr_t)SOBER_PROV_THUNK_CALL_RET_RVA),
            thunk ? "seen" : "absent", (unsigned long long)thunk_off,
            (unsigned long long)(base + (uintptr_t)SOBER_PROV_NOTIFIER_RET_RVA),
            notifier ? "seen" : "absent", (unsigned long long)notifier_off,
            genuine ? "GENUINE" : (harness ? "HARNESS-THUNK" : "UNCLASSIFIED"));
}

int sober_callback_provenance_is_active(void)
{
    return atomic_load_explicit(&g_active, memory_order_acquire);
}

int sober_callback_provenance_is_genuine(void)
{
    return atomic_load_explicit(&g_genuine, memory_order_acquire);
}

int sober_callback_provenance_is_harness_thunk(void)
{
    return atomic_load_explicit(&g_harness_thunk, memory_order_acquire);
}

uint64_t sober_callback_provenance_dispatch_count(void)
{
    return atomic_load_explicit(&g_dispatches, memory_order_relaxed);
}

uintptr_t sober_callback_provenance_guest_base(void)
{
    return atomic_load_explicit(&g_guest_base, memory_order_acquire);
}

uintptr_t sober_callback_provenance_thunk_ret(void)
{
    return atomic_load_explicit(&g_thunk_ret, memory_order_relaxed);
}

uintptr_t sober_callback_provenance_notifier_ret(void)
{
    return atomic_load_explicit(&g_notifier_ret, memory_order_relaxed);
}

uintptr_t sober_callback_provenance_observed_class(void)
{
    return atomic_load_explicit(&g_observed_class, memory_order_relaxed);
}

uintptr_t sober_callback_provenance_observed_method(void)
{
    return atomic_load_explicit(&g_observed_method, memory_order_relaxed);
}

const char *sober_callback_provenance_describe(char *buf, size_t cap)
{
    if (!buf || cap == 0) return "";
    snprintf(buf, cap,
             "callback-provenance: active=%d installed=%d dispatches=%llu "
             "genuine=%d harness_thunk=%d base=0x%llx "
             "class=%p method=%p",
             atomic_load_explicit(&g_active, memory_order_acquire),
             atomic_load_explicit(&g_installed, memory_order_acquire),
             (unsigned long long)atomic_load_explicit(&g_dispatches,
                                                      memory_order_relaxed),
             atomic_load_explicit(&g_genuine, memory_order_acquire),
             atomic_load_explicit(&g_harness_thunk, memory_order_acquire),
             (unsigned long long)atomic_load_explicit(&g_guest_base,
                                                      memory_order_acquire),
             (void *)atomic_load_explicit(&g_observed_class,
                                          memory_order_relaxed),
             (void *)atomic_load_explicit(&g_observed_method,
                                          memory_order_relaxed));
    return buf;
}
