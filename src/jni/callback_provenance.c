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
/* Task #522: return site of `call [rax+0x30]` at node dispatch 0x27eaf2f.
 * Descriptor invoke 0x27eaf4c tail-jmps to notifier 0x27eaf6e, so this word is
 * still live above the notifier RA and proves the dispatch leg ran. */
#define SOBER_PROV_DISPATCH_RET_RVA     0x27eaf32u
/* Guest first R E PT_LOAD span (RVA == file offset). */
#define SOBER_PROV_TEXT_SIZE            0x6d33b90u

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
static _Atomic uintptr_t g_node_caller;  /* node dispatch 0x27eaf04 caller RA */
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

/* Walk from the notifier's `call 0x25848e0` return slot up to the caller of
 * node dispatch 0x27eaf04 (task #522).  The fixed-offset scan finds
 * 0x27eafb3; the real frame chain is:
 *   rbp_th         = notifier_slot - 8            (`push rbp` in thunk)
 *   rbp_not        = [rbp_th]                     (saved notifier rbp)
 *   [rbp_not+8]    == base+0x27eaf32              (call [rax+0x30] @0x27eaf2f)
 *   rbp_node       = [rbp_not]                    (saved node-dispatch rbp)
 *   node_caller_ra = [rbp_node+8]                 (guest dispatcher or host)
 * Returns 1 only when the invoke return matches and the node was entered from
 * guest .text; a host-entered 0x27eaf04 leaves a harness return address and
 * is never reported GENUINE.  Fails closed (0) on any malformed frame. */
static int prov_walk_to_dispatch(uintptr_t base, uintptr_t notifier_slot,
                                 uintptr_t frame_cand,
                                 uintptr_t *out_node_caller)
{
    *out_node_caller = 0u;
    const uintptr_t rbp_th = frame_cand ? frame_cand : (notifier_slot - 8u);
    if (!rbp_th || (rbp_th & 7u) != 0u) return 0;
    if (notifier_slot <= rbp_th || (notifier_slot - rbp_th) > (1u << 20))
        return 0;

    const uintptr_t rbp_not = *(const volatile uintptr_t *)rbp_th;
    if (!rbp_not || (rbp_not & 7u) != 0u) return 0;
    if (rbp_not <= rbp_th || (rbp_not - rbp_th) > (1u << 20)) return 0;

    const uintptr_t invoke_ret =
        *(const volatile uintptr_t *)(rbp_not + sizeof(uintptr_t));
    if (invoke_ret != base + (uintptr_t)SOBER_PROV_DISPATCH_RET_RVA) return 0;

    const uintptr_t rbp_node = *(const volatile uintptr_t *)rbp_not;
    if (!rbp_node || (rbp_node & 7u) != 0u) return 0;
    if (rbp_node <= rbp_not || (rbp_node - rbp_not) > (1u << 20)) return 0;

    const uintptr_t caller =
        *(const volatile uintptr_t *)(rbp_node + sizeof(uintptr_t));
    *out_node_caller = caller;
    return caller >= base && (caller - base) < (uintptr_t)SOBER_PROV_TEXT_SIZE;
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
                            int *out_notifier, uintptr_t *out_notifier_off,
                            int *out_guest_driven,
                            uintptr_t *out_node_caller)
{
    const uintptr_t thunk_ra = base + (uintptr_t)SOBER_PROV_THUNK_CALL_RET_RVA;
    const uintptr_t notifier_ra = base + (uintptr_t)SOBER_PROV_NOTIFIER_RET_RVA;

    volatile uintptr_t anchor = 0;
    /* Integer-address arithmetic (rather than array subscripting `&anchor`)
     * keeps this bounded stack read free of -Warray-bounds at -O2 -Werror. */
    const uintptr_t sp0 = (uintptr_t)&anchor;
    int thunk = 0, notifier = 0;
    uintptr_t thunk_off = 0, notifier_off = 0, frame_cand = 0;

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
                frame_cand = cand;   /* saved thunk rbp, for the full-chain walk */
                break;
            }
        }
        break; /* nearest thunk return site is the most relevant one */
    }

    *out_thunk = thunk;
    *out_thunk_off = thunk_off;
    *out_notifier = notifier;
    *out_notifier_off = notifier_off;

    /* Task #522: require the full guest dispatch chain, not just the RA pair. */
    *out_guest_driven = 0u;
    *out_node_caller = 0u;
    if (notifier) {
        const uintptr_t notifier_slot =
            sp0 + notifier_off * (uintptr_t)sizeof(uintptr_t);
        *out_guest_driven = prov_walk_to_dispatch(
            base, notifier_slot, frame_cand, out_node_caller);
    }
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

    int thunk = 0, notifier = 0, guest_driven = 0;
    uintptr_t thunk_off = 0, notifier_off = 0, node_caller = 0;
    prov_scan_stack(base, &thunk, &thunk_off, &notifier, &notifier_off,
                    &guest_driven, &node_caller);

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
    if (node_caller)
        atomic_store_explicit(&g_node_caller, node_caller,
                              memory_order_relaxed);

    /* Genuine iff the thunk return site is nearer the dispatch than the
     * notifier return site (call order), the cached handles were used, AND
     * the full chain 0x27eaf04->0x27eaf2f->0x27eaf4c->0x27eaf6e is guest-
     * driven (task #522).  A bare thunk return site (host return address
     * above it) is the ALT path; a host-entered 0x27eaf04 is not genuine. */
    const int genuine = class_ok && method_ok && thunk && notifier &&
                        (thunk_off < notifier_off) && guest_driven;
    const int harness = thunk && !notifier;
    if (genuine) atomic_store_explicit(&g_genuine, 1, memory_order_release);
    if (harness) atomic_store_explicit(&g_harness_thunk, 1, memory_order_release);

    fprintf(stderr,
            "[callback-provenance] %s: class=%p (cached=%p %s) "
            "method=%p (cached=%p %s) thunk_ret=0x%llx(%s,+%llu) "
            "notifier_ret=0x%llx(%s,+%llu) node_ret=0x%llx(%s) -> %s\n",
            SOBER_PROV_METHOD_NAME,
            clazz, cached_class, class_ok ? "match" : "MISMATCH",
            method, cached_method, method_ok ? "match" : "MISMATCH",
            (unsigned long long)(base + (uintptr_t)SOBER_PROV_THUNK_CALL_RET_RVA),
            thunk ? "seen" : "absent", (unsigned long long)thunk_off,
            (unsigned long long)(base + (uintptr_t)SOBER_PROV_NOTIFIER_RET_RVA),
            notifier ? "seen" : "absent", (unsigned long long)notifier_off,
            (unsigned long long)node_caller,
            guest_driven ? "guest" : "host/none",
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

/* Node dispatch 0x27eaf04 caller RA (guest dispatcher vs host re-drive). */
uintptr_t sober_callback_provenance_node_caller_ret(void)
{
    return atomic_load_explicit(&g_node_caller, memory_order_relaxed);
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
             "node_ret=0x%llx class=%p method=%p",
             atomic_load_explicit(&g_active, memory_order_acquire),
             atomic_load_explicit(&g_installed, memory_order_acquire),
             (unsigned long long)atomic_load_explicit(&g_dispatches,
                                                      memory_order_relaxed),
             atomic_load_explicit(&g_genuine, memory_order_acquire),
             atomic_load_explicit(&g_harness_thunk, memory_order_acquire),
             (unsigned long long)atomic_load_explicit(&g_guest_base,
                                                      memory_order_acquire),
             (unsigned long long)atomic_load_explicit(&g_node_caller,
                                                      memory_order_relaxed),
             (void *)atomic_load_explicit(&g_observed_class,
                                          memory_order_relaxed),
             (void *)atomic_load_explicit(&g_observed_method,
                                          memory_order_relaxed));
    return buf;
}
