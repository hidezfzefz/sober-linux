/*
 * provenance_gate.c - DEFAULT-ON positive provenance credential for the
 *                     strict decision-013 `app shell rendered` gate.
 *
 * See provenance_gate.h for the RVA contract, evidence chain, the no-marker /
 * no-callback rule and the exact lead wiring.  NEW FILE ONLY: this file and its
 * header are the only files this agent touches; native_jni.c,
 * guest_app_shell.c, CMakeLists, docs, tests and the engine are untouched.
 *
 * The discriminator (all fail-closed, all read-only w.r.t. the guest):
 *   * find the live return address of `call *0x470(%rax)` (base+0x2584960);
 *   * read the frame directly above it and require the notifier return
 *     base+0x27eafb3 (GENUINE), classify base+0x2eb4bb1 as APPBRIDGE
 *     (disqualified, never genuine/harness), and any other/host return as a
 *     HARNESS_THUNK taint;
 *   * require the class/method handles passed to sr_dispatch_call to be the
 *     guest's cached globals at base+0x72a2f48 / base+0x72a2fc0;
 *   * require the frozen BuildID to match and the name to be qualifying.
 */

#define _GNU_SOURCE
#include "provenance_gate.h"

#include <elf.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef NT_GNU_BUILD_ID
#define NT_GNU_BUILD_ID 3
#endif

/* 20-byte NT_GNU_BUILD_ID of the frozen engine (204ad049...). */
static const unsigned char k_build_id[20] = {
    0x20, 0x4a, 0xd0, 0x49, 0x59, 0x9c, 0x96, 0xaf,
    0xb7, 0x5b, 0x8a, 0x21, 0xd5, 0x05, 0x10, 0x63,
    0xe8, 0x21, 0xd6, 0xc9
};

/* Only this name can carry the positive credential.  A second candidate
 * (onAppShellReloadNeeded) is statically dead natively and its notifier RVA
 * differs, so it is deliberately excluded: a fabricated harness log of that
 * name must never become a credential. */
#define SOBER_PG_QUAL_COUNT 1u
static const char *const k_qual_names[SOBER_PG_QUAL_COUNT] = {
    SOBER_PROV_GATE_QUALIFYING_NAME
};

/* Stack scan window.  The genuine chain is a handful of frames deep; 8 KiB is
 * ample and the scan stops at the nearest thunk return site. */
#define SOBER_PG_SCAN_WORDS 1024u
/* Guest .text span (frozen first R E PT_LOAD).  Used only for a read-only
 * classification of the frame above the notifier return site. */
#define SOBER_PG_TEXT_SIZE 0x6d33b90u

/* ---- state (all atomics: bridge and gate may live on different threads) - */
static _Atomic int      g_enabled;        /* kill-switch state (default ON)     */
static _Atomic int      g_installed;       /* base resolved + observer ready     */
static _Atomic int      g_build_verified;  /* frozen BuildID matched             */
static _Atomic uintptr_t g_base;           /* resolved load bias                 */
static _Atomic uint64_t g_dispatch_count;  /* all dispatches while active        */
static _Atomic uint64_t g_harness_count;   /* host-return thunk observations      */
static _Atomic uint64_t g_appbridge_count; /* onAppBridgeNotification observations*/
static _Atomic int      g_harness;         /* sticky global taint                */
static _Atomic int      g_appbridge;       /* sticky appbridge-seen flag          */
static _Atomic uintptr_t g_thunk_ra;       /* last base+0x2584960 observed        */
static _Atomic uintptr_t g_caller_ra;      /* last caller return address observed */
static _Atomic uintptr_t g_node_caller_ra; /* who entered node dispatch 0x27eaf04 */
static _Atomic uintptr_t g_cached_class;   /* value read at base+0x72a2f48       */
static _Atomic uintptr_t g_cached_method;  /* value read at base+0x72a2fc0       */
static _Atomic int64_t  g_place_id;        /* read at base+0x72a31c0              */
static _Atomic int      g_prov_emitted;    /* one-shot gate provenance line       */
static _Atomic int      g_last_verdict;    /* SOBER_PROV_GATE_VERDICT_*           */

static _Atomic uint64_t g_qual_obs[SOBER_PG_QUAL_COUNT];       /* observations  */
static _Atomic uint64_t g_qual_genuine[SOBER_PG_QUAL_COUNT];   /* genuine count */
static _Atomic int      g_qual_flag[SOBER_PG_QUAL_COUNT];      /* sticky genuine*/

/* ---- enable / build verification / discovery --------------------------- */

int sober_provenance_gate_enabled(void)
{
    const char *v = getenv(SOBER_PROVENANCE_GATE_ENV);
    if (!v) return 1;                     /* default ON, no opt-in needed */
    if (v[0] == '0' && v[1] == '\0') return 0;
    if (strcasecmp(v, "off") == 0 || strcasecmp(v, "no") == 0 ||
        strcasecmp(v, "false") == 0) return 0;
    return 1;
}

/* Scan a PT_NOTE segment for NT_GNU_BUILD_ID == k_build_id. */
static int notes_match_build_id(const unsigned char *notes, size_t size)
{
    size_t off = 0;
    while (off + sizeof(Elf64_Nhdr) <= size) {
        Elf64_Nhdr nh;
        memcpy(&nh, notes + off, sizeof(nh));
        off += sizeof(Elf64_Nhdr);

        size_t namesz = (size_t)nh.n_namesz;
        size_t descsz = (size_t)nh.n_descsz;
        size_t name_pad = (namesz + 3u) & ~(size_t)3u;
        size_t desc_pad = (descsz + 3u) & ~(size_t)3u;
        if (namesz > 4096u || descsz > 4096u ||
            name_pad > size - off || desc_pad > size - off - name_pad)
            return 0;

        const unsigned char *name = notes + off;
        const unsigned char *desc = name + name_pad;
        if (nh.n_type == NT_GNU_BUILD_ID && namesz >= 4u &&
            memcmp(name, "GNU", 4) == 0 && descsz == sizeof(k_build_id) &&
            memcmp(desc, k_build_id, sizeof(k_build_id)) == 0)
            return 1;

        off += name_pad + desc_pad;
    }
    return 0;
}

int sober_provenance_gate_verify_build(void *guest_base)
{
    if (!guest_base) return 0;
    const unsigned char *b = (const unsigned char *)guest_base;

    if (b[EI_MAG0] != ELFMAG0 || b[EI_MAG1] != ELFMAG1 ||
        b[EI_MAG2] != ELFMAG2 || b[EI_MAG3] != ELFMAG3 ||
        b[EI_CLASS] != ELFCLASS64 || b[EI_DATA] != ELFDATA2LSB)
        return 0;

    Elf64_Ehdr eh;
    memcpy(&eh, b, sizeof(eh));
    if (eh.e_phentsize < sizeof(Elf64_Phdr) || eh.e_phnum == 0u ||
        eh.e_phnum > 4096u)
        return 0;

    for (unsigned i = 0; i < eh.e_phnum; ++i) {
        Elf64_Phdr ph;
        memcpy(&ph, b + eh.e_phoff + (size_t)i * eh.e_phentsize, sizeof(ph));
        if (ph.p_type == PT_NOTE && ph.p_memsz >= sizeof(Elf64_Nhdr) &&
            notes_match_build_id(b + ph.p_vaddr, (size_t)ph.p_memsz))
            return 1;
    }
    return 0;
}

/* Find the guest load bias: explicit read-only env hint, else the largest
 * r-x mapping in /proc/self/maps (the guest is ~20 MB, dwarfing host DSOs).
 * A BuildID match is preferred; the result is returned even when verification
 * fails so diagnostics can show it, but g_build_verified gates GENUINE. */
static uintptr_t discover_base(void)
{
    const char *env = getenv(SOBER_PROVENANCE_GATE_BASE_ENV);
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
        if (perms[0] != 'r' || perms[2] != 'x') continue;
        if ((hi - lo) <= (100ULL << 20)) continue;
        uintptr_t cand = (uintptr_t)lo;
        if (sober_provenance_gate_verify_build((void *)cand)) {
            best = cand;                  /* definitive: frozen engine found */
            break;
        }
        if ((hi - lo) > bestsz) { bestsz = hi - lo; best = cand; }
    }
    fclose(f);
    return best;
}

void sober_provenance_gate_reset(void)
{
    atomic_store_explicit(&g_enabled,
                          sober_provenance_gate_enabled(), memory_order_release);
    atomic_store_explicit(&g_installed, 0, memory_order_release);
    atomic_store_explicit(&g_build_verified, 0, memory_order_release);
    atomic_store_explicit(&g_base, 0, memory_order_release);
    atomic_store_explicit(&g_dispatch_count, 0, memory_order_relaxed);
    atomic_store_explicit(&g_harness_count, 0, memory_order_relaxed);
    atomic_store_explicit(&g_appbridge_count, 0, memory_order_relaxed);
    atomic_store_explicit(&g_harness, 0, memory_order_release);
    atomic_store_explicit(&g_appbridge, 0, memory_order_release);
    atomic_store_explicit(&g_thunk_ra, 0, memory_order_relaxed);
    atomic_store_explicit(&g_caller_ra, 0, memory_order_relaxed);
    atomic_store_explicit(&g_node_caller_ra, 0, memory_order_relaxed);
    atomic_store_explicit(&g_cached_class, 0, memory_order_relaxed);
    atomic_store_explicit(&g_cached_method, 0, memory_order_relaxed);
    atomic_store_explicit(&g_place_id, 0, memory_order_relaxed);
    atomic_store_explicit(&g_prov_emitted, 0, memory_order_relaxed);
    atomic_store_explicit(&g_last_verdict, SOBER_PROV_GATE_VERDICT_NONE,
                          memory_order_relaxed);
    for (unsigned i = 0; i < SOBER_PG_QUAL_COUNT; ++i) {
        atomic_store_explicit(&g_qual_obs[i], 0, memory_order_relaxed);
        atomic_store_explicit(&g_qual_genuine[i], 0, memory_order_relaxed);
        atomic_store_explicit(&g_qual_flag[i], 0, memory_order_release);
    }
}

int sober_provenance_gate_install(void *guest_base)
{
    atomic_store_explicit(&g_enabled,
                          sober_provenance_gate_enabled(), memory_order_release);
    if (!atomic_load_explicit(&g_enabled, memory_order_acquire))
        return SOBER_PROV_GATE_DISABLED;   /* fail closed: no credential possible */

    if (atomic_load_explicit(&g_installed, memory_order_acquire)) return SOBER_PROV_GATE_OK;

    uintptr_t base = guest_base ? (uintptr_t)guest_base : discover_base();
    if (!base) return SOBER_PROV_GATE_NO_BASE;

    atomic_store_explicit(&g_build_verified,
                          sober_provenance_gate_verify_build((void *)base),
                          memory_order_release);
    atomic_store_explicit(&g_base, base, memory_order_release);
    atomic_store_explicit(&g_installed, 1, memory_order_release);
    return SOBER_PROV_GATE_OK;
}

/* ---- stack scan --------------------------------------------------------- */

enum {
    CALLER_NONE = 0,       /* no recognised guest caller return site found    */
    CALLER_NOTIFIER,       /* base+0x27eafb3 (gameLoadedCallback notifier)    */
    CALLER_APPBRIDGE       /* base+0x2eb4bb1 (onAppBridgeNotification reader) */
};

typedef struct {
    int      thunk_seen;
    uintptr_t thunk_off;
    int      caller;        /* CALLER_* */
    uintptr_t caller_off;
    uintptr_t caller_ra;    /* absolute address of the recognised caller RA   */
    /* 1 only when the full 3-frame guest walk succeeds: the notifier's
     * `call [rax+0x30]` return (base+0x27eaf32) is live above the notifier RA
     * AND node dispatch 0x27eaf04 was itself entered from guest .text.  A
     * host-entered 0x27eaf04 (publish_default re-drive) leaves a harness
     * return address as the node dispatch's caller, so this stays 0 and
     * cannot be certified GENUINE even if the thunk/notifier RA pair is
     * present. */
    int      guest_above_notifier;
    uintptr_t node_caller_ra; /* node dispatch caller RA (guest or host)      */
    uintptr_t frame_cand;    /* saved-RBP candidate when found via frame-chain */
} ProvScan;

/* Walk from the notifier's `call 0x25848e0` return slot up through the guest
 * frames and verify the COMPLETE id=1 dispatch chain (task #522):
 *
 *   notifier_slot = stack address holding base+0x27eafb3
 *   rbp_th        = notifier_slot - 8        (`push rbp` in thunk 0x25848e0)
 *   rbp_not       = [rbp_th]                 (saved notifier frame pointer)
 *   [rbp_not+8]   == base+0x27eaf32           (`call [rax+0x30]` @0x27eaf2f;
 *                                              invoke 0x27eaf4c tail-jmps to
 *                                              the notifier, so this word is
 *                                              still live while it runs)
 *   rbp_node      = [rbp_not]                 (saved node-dispatch rbp)
 *   node_caller_ra= [rbp_node+8]              (who entered 0x27eaf04)
 *
 * Returns 1 only when the invoke return matches AND node_caller_ra is guest
 * .text.  Only the MessageBus dispatcher leaves a guest return address there;
 * a host-entered 0x27eaf04 (publish_default re-drive) leaves a host return
 * address and is rejected.  This fails closed independently of seeded_state.
 * *out_caller_ra is written (0 on an incomplete walk) for diagnostics. */
static int pg_walk_notifier_to_dispatch(uintptr_t base, uintptr_t notifier_slot,
                                        uintptr_t frame_cand,
                                        uintptr_t *out_caller_ra)
{
    *out_caller_ra = 0u;

    /* The frame-chain path already found the thunk frame pointer (the saved
     * RBP whose +8 word was the notifier RA).  The fixed-offset path derives
     * it from the notifier RA slot: `push rbp` wrote at that slot - 8. */
    const uintptr_t rbp_th = frame_cand ? frame_cand : (notifier_slot - 8u);
    if (!rbp_th || (rbp_th & 7u) != 0u) return 0;
    if (notifier_slot <= rbp_th || (notifier_slot - rbp_th) > (1u << 20))
        return 0;

    const uintptr_t rbp_not = *(const volatile uintptr_t *)rbp_th;
    if (!rbp_not || (rbp_not & 7u) != 0u) return 0;
    if (rbp_not <= rbp_th || (rbp_not - rbp_th) > (1u << 20)) return 0;

    /* Leg 0x27eaf2f -> 0x27eaf4c -> 0x27eaf6e must be live. */
    const uintptr_t invoke_ret =
        *(const volatile uintptr_t *)(rbp_not + sizeof(uintptr_t));
    if (invoke_ret != base + (uintptr_t)SOBER_PROV_GATE_RVA_DISPATCH_RET)
        return 0;

    const uintptr_t rbp_node = *(const volatile uintptr_t *)rbp_not;
    if (!rbp_node || (rbp_node & 7u) != 0u) return 0;
    if (rbp_node <= rbp_not || (rbp_node - rbp_not) > (1u << 20)) return 0;

    const uintptr_t caller =
        *(const volatile uintptr_t *)(rbp_node + sizeof(uintptr_t));
    *out_caller_ra = caller;
    return caller >= base && (caller - base) < (uintptr_t)SOBER_PG_TEXT_SIZE;
}

/* Locate the guest call chain on the live stack.  We do NOT use
 * __builtin_return_address(N) across our own (possibly frame-pointer-less)
 * bridge frames.  Instead:
 *   1. find the thunk's `call *0x470(%rax)` return site (base+0x2584960) as a
 *      word on the stack -- pushed by the guest on every thunk-driven call;
 *   2. classify the frame directly above it by matching the caller return
 *      address: notifier 0x27eafb3 (GENUINE) or appbridge 0x2eb4bb1
 *      (DISQUALIFIED).  Two independent checks are used:
 *        (a) fixed-offset: the thunk prologue pushes rbp/rbx and reserves
 *            0xd8 bytes, so the notifier/appbridge `call 0x25848e0` return
 *            sits ~0xf0 (30 words) above 0x2584960; a small window absorbs
 *            compiler/prologue variation;
 *        (b) frame-chain: the thunk keeps a frame pointer, so a saved-RBP word
 *            above 0x2584960 points to a frame whose +8 word is the caller RA.
 *      Only these specific return-site values count.  In the ALT path the word
 *      above the thunk frame is a HOST return address, so neither check fires
 *      and the dispatch is classified HARNESS_THUNK rather than GENUINE. */
static void pg_scan_stack(uintptr_t base, ProvScan *out)
{
    const uintptr_t thunk_ra = base + (uintptr_t)SOBER_PROV_GATE_RVA_THUNK_CALL_RET;
    const uintptr_t notifier_ra = base + (uintptr_t)SOBER_PROV_GATE_RVA_NOTIFIER_RET;
    const uintptr_t appbridge_ra = base + (uintptr_t)SOBER_PROV_GATE_RVA_APPBRIDGE_RET;

    volatile uintptr_t anchor = 0;
    /* Integer-address arithmetic (not `&anchor[i]`) keeps this bounded stack
     * read free of -Warray-bounds at -O2 -Werror. */
    const uintptr_t sp0 = (uintptr_t)&anchor;

    memset(out, 0, sizeof(*out));

    for (uintptr_t i = 0; i < (uintptr_t)SOBER_PG_SCAN_WORDS; ++i) {
        const uintptr_t slot_addr = sp0 + i * sizeof(uintptr_t);
        if (*(const volatile uintptr_t *)slot_addr != thunk_ra) continue;
        out->thunk_seen = 1;
        out->thunk_off = i;

        /* (a) fixed-offset window, ~16..48 words above the thunk return. */
        const uintptr_t hi = (i < (uintptr_t)SOBER_PG_SCAN_WORDS - 48u)
                                 ? i + 48u : (uintptr_t)SOBER_PG_SCAN_WORDS;
        for (uintptr_t j = i + 16u; j < hi; ++j) {
            const uintptr_t w =
                *(const volatile uintptr_t *)(sp0 + j * sizeof(uintptr_t));
            if (w == notifier_ra) {
                out->caller = CALLER_NOTIFIER;
                out->caller_ra = notifier_ra;
                out->caller_off = j;
                break;
            }
            if (w == appbridge_ra) {
                out->caller = CALLER_APPBRIDGE;
                out->caller_ra = appbridge_ra;
                out->caller_off = j;
                break;
            }
        }

        /* (b) frame-chain fallback (guest thunk keeps rbp). */
        if (out->caller == CALLER_NONE) {
            for (uintptr_t j = i + 1u;
                 j < (uintptr_t)SOBER_PG_SCAN_WORDS && j < i + 512u; ++j) {
                const uintptr_t cand =
                    *(const volatile uintptr_t *)(sp0 + j * sizeof(uintptr_t));
                if (cand <= slot_addr || (cand & 7u) != 0u) continue;
                if (cand - slot_addr > (1u << 20)) continue;
                const uintptr_t w = *(const volatile uintptr_t *)(cand + 8u);
                if (w == notifier_ra) {
                    out->caller = CALLER_NOTIFIER;
                    out->caller_ra = notifier_ra;
                    out->caller_off = j;
                    out->frame_cand = cand;
                    break;
                }
                if (w == appbridge_ra) {
                    out->caller = CALLER_APPBRIDGE;
                    out->caller_ra = appbridge_ra;
                    out->caller_off = j;
                    break;
                }
            }
        }
        break; /* nearest thunk return site is the most relevant one */
    }

    /* Task #522: verify the FULL guest dispatch chain, not just the RA pair.
     * The old check read the word at [notifier_RA + 8], which in the genuine
     * frame layout is r14 (the va_list where 0x227b9e2 stored a HOST JNIEnv
     * pointer) -- it could never be guest .text, so a genuine chain was never
     * certified.  Walk the real frame chain instead; this also rejects a
     * host-entered 0x27eaf04 whose node-dispatch caller is harness code. */
    if (out->caller == CALLER_NOTIFIER) {
        const uintptr_t notifier_slot =
            sp0 + (uintptr_t)out->caller_off * sizeof(uintptr_t);
        out->guest_above_notifier = pg_walk_notifier_to_dispatch(
            base, notifier_slot, out->frame_cand, &out->node_caller_ra);
    }
}

static int qual_index(const char *name)
{
    if (!name) return -1;
    for (unsigned i = 0; i < SOBER_PG_QUAL_COUNT; ++i)
        if (strcmp(name, k_qual_names[i]) == 0) return (int)i;
    return -1;
}

static void *read_ptr(uintptr_t addr)
{
    void *p;
    memcpy(&p, (const void *)addr, sizeof(p));
    return p;
}

static uint64_t read_u64(uintptr_t addr)
{
    uint64_t v;
    memcpy(&v, (const void *)addr, sizeof(v));
    return v;
}

/* ---- THE HOOK ----------------------------------------------------------- */

void sober_provenance_gate_on_dispatch(const char *name, void *clazz,
                                       void *method)
{
    if (!atomic_load_explicit(&g_enabled, memory_order_acquire)) return;
    if (!atomic_load_explicit(&g_installed, memory_order_acquire)) {
        if (sober_provenance_gate_install(NULL) != SOBER_PROV_GATE_OK) return;
    }

    const uintptr_t base = atomic_load_explicit(&g_base, memory_order_acquire);
    if (!base) return;

    const int qi = qual_index(name);
    atomic_fetch_add_explicit(&g_dispatch_count, 1, memory_order_relaxed);

    /* Pointer identity: exactly the handles the guest cached in JNI_OnLoad. */
    void *cached_class = read_ptr(base + (uintptr_t)SOBER_PROV_GATE_RVA_CACHED_CLASS);
    void *cached_method = read_ptr(base + (uintptr_t)SOBER_PROV_GATE_RVA_CACHED_METHOD);
    const int class_ok = (cached_class != NULL && clazz == cached_class);
    const int method_ok = (cached_method != NULL && method == cached_method);

    ProvScan sc;
    pg_scan_stack(base, &sc);

    atomic_store_explicit(&g_cached_class, (uintptr_t)cached_class,
                          memory_order_relaxed);
    atomic_store_explicit(&g_cached_method, (uintptr_t)cached_method,
                          memory_order_relaxed);
    if (sc.thunk_seen)
        atomic_store_explicit(&g_thunk_ra,
                              base + (uintptr_t)SOBER_PROV_GATE_RVA_THUNK_CALL_RET,
                              memory_order_relaxed);
    if (sc.caller != CALLER_NONE)
        atomic_store_explicit(&g_caller_ra, sc.caller_ra, memory_order_relaxed);
    if (sc.node_caller_ra)
        atomic_store_explicit(&g_node_caller_ra, sc.node_caller_ra,
                              memory_order_relaxed);

    /* placeId is the real jlong the guest cached from StartGameParams.placeId()
     * at 0x2ebd86a.  A dispatch without a positive value is NOT the game-loaded
     * callback the strict gate requires, even if the RA pair matches. */
    const int64_t place_id = (int64_t)read_u64(
        base + (uintptr_t)SOBER_PROV_GATE_RVA_PLACE_ID);
    atomic_store_explicit(&g_place_id, place_id, memory_order_relaxed);

    const int build_ok = atomic_load_explicit(&g_build_verified,
                                              memory_order_acquire);
    /* READINESS PREDICATE (fail-closed, task #522): every leg must hold at the
     * same dispatch -- qualifying name + frozen build + cached class/method
     * identity + placeId>0 + thunk RA 0x2584960 + notifier RA 0x27eafb3 above
     * it + descriptor-invoke RA 0x27eaf32 + node 0x27eaf04 entered from guest
     * .text.  guest_above_notifier is the last two checks; if either fails the
     * dispatch is UNCLASSIFIED, never GENUINE. */
    const int genuine = (qi >= 0) && build_ok && class_ok && method_ok &&
                        place_id > 0 &&
                        sc.thunk_seen && sc.caller == CALLER_NOTIFIER &&
                        sc.guest_above_notifier;
    const int harness = sc.thunk_seen && sc.caller == CALLER_NONE;
    const int appbridge = sc.caller == CALLER_APPBRIDGE;

    if (qi >= 0) {
        atomic_fetch_add_explicit(&g_qual_obs[(unsigned)qi], 1,
                                  memory_order_relaxed);
        if (genuine) {
            atomic_fetch_add_explicit(&g_qual_genuine[(unsigned)qi], 1,
                                      memory_order_relaxed);
            atomic_store_explicit(&g_qual_flag[(unsigned)qi], 1,
                                  memory_order_release);
        }
    }
    if (harness) {
        atomic_fetch_add_explicit(&g_harness_count, 1, memory_order_relaxed);
        atomic_store_explicit(&g_harness, 1, memory_order_release);
    }
    if (appbridge) {
        atomic_fetch_add_explicit(&g_appbridge_count, 1, memory_order_relaxed);
        atomic_store_explicit(&g_appbridge, 1, memory_order_release);
    }

    const int verdict = genuine ? SOBER_PROV_GATE_VERDICT_GENUINE
                       : harness ? SOBER_PROV_GATE_VERDICT_HARNESS_THUNK
                       : appbridge ? SOBER_PROV_GATE_VERDICT_APPBRIDGE
                       : SOBER_PROV_GATE_VERDICT_UNCLASSIFIED;
    atomic_store_explicit(&g_last_verdict, verdict, memory_order_relaxed);
}

/* ---- THE QUERIES -------------------------------------------------------- */

int sober_provenance_gate_is_genuine(const char *name)
{
    const int qi = qual_index(name);
    if (qi < 0) return 0;
    /* A harness thunk anywhere in this run taints it: fail closed. */
    if (atomic_load_explicit(&g_harness, memory_order_acquire)) return 0;
    return atomic_load_explicit(&g_qual_flag[(unsigned)qi],
                                memory_order_acquire);
}

int sober_provenance_gate_is_harness_thunk(void)
{
    return atomic_load_explicit(&g_harness, memory_order_acquire);
}

int sober_provenance_gate_is_appbridge_notification(void)
{
    return atomic_load_explicit(&g_appbridge, memory_order_acquire);
}

/* ---- evidence accessors ------------------------------------------------- */

uint64_t sober_provenance_gate_dispatch_count(void)
{
    return atomic_load_explicit(&g_dispatch_count, memory_order_relaxed);
}

uint64_t sober_provenance_gate_genuine_count(const char *name)
{
    const int qi = qual_index(name);
    if (qi < 0) return 0;
    return atomic_load_explicit(&g_qual_genuine[(unsigned)qi],
                                memory_order_relaxed);
}

uint64_t sober_provenance_gate_harness_count(void)
{
    return atomic_load_explicit(&g_harness_count, memory_order_relaxed);
}

uint64_t sober_provenance_gate_appbridge_count(void)
{
    return atomic_load_explicit(&g_appbridge_count, memory_order_relaxed);
}

uintptr_t sober_provenance_gate_guest_base(void)
{
    return atomic_load_explicit(&g_base, memory_order_acquire);
}

uintptr_t sober_provenance_gate_thunk_return_addr(void)
{
    return atomic_load_explicit(&g_thunk_ra, memory_order_relaxed);
}

uintptr_t sober_provenance_gate_caller_return_addr(void)
{
    return atomic_load_explicit(&g_caller_ra, memory_order_relaxed);
}

uintptr_t sober_provenance_gate_node_caller_return_addr(void)
{
    return atomic_load_explicit(&g_node_caller_ra, memory_order_relaxed);
}

uintptr_t sober_provenance_gate_cached_class(void)
{
    return atomic_load_explicit(&g_cached_class, memory_order_relaxed);
}

uintptr_t sober_provenance_gate_cached_method(void)
{
    return atomic_load_explicit(&g_cached_method, memory_order_relaxed);
}

int64_t sober_provenance_gate_place_id(void)
{
    return atomic_load_explicit(&g_place_id, memory_order_relaxed);
}

/* Gate-owned positive provenance line.  Emitted at most once and ONLY when
 * every leg of the credential is satisfied; never on the ALT/host-thunk path
 * and never as a decision-013 marker.  The lead calls this from the strict
 * gate after confirming seeded_state==0. */
int sober_provenance_gate_emit_provenance(void)
{
    int expected = 0;
    int64_t pid = atomic_load_explicit(&g_place_id, memory_order_relaxed);

    if (!sober_provenance_gate_enabled()) return 0;
    if (!sober_provenance_gate_is_genuine(SOBER_PROV_GATE_QUALIFYING_NAME))
        return 0;
    if (pid <= 0) return 0;
    if (!atomic_compare_exchange_strong_explicit(&g_prov_emitted, &expected, 1,
            memory_order_relaxed, memory_order_relaxed))
        return 0;

    fprintf(stderr,
            "[app-shell] gate provenance: notifier-driven gameLoadedCallback "
            "at 0x27eaf6e placeId=%lld (RSI>0), class/method cached non-NULL, "
            "return 0x27eafb3 over thunk 0x2584960, dispatch 0x27eaf04 "
            "guest-entered via 0x27eaf2f/0x27eaf4c -> GENUINE\n",
            (long long)pid);
    return 1;
}

int sober_provenance_gate_last_verdict(void)
{
    return atomic_load_explicit(&g_last_verdict, memory_order_relaxed);
}

const char *sober_provenance_gate_verdict_name(int verdict)
{
    switch (verdict) {
    case SOBER_PROV_GATE_VERDICT_GENUINE:       return "genuine";
    case SOBER_PROV_GATE_VERDICT_HARNESS_THUNK: return "harness_thunk";
    case SOBER_PROV_GATE_VERDICT_APPBRIDGE:     return "appbridge_disqualified";
    case SOBER_PROV_GATE_VERDICT_UNCLASSIFIED:  return "unclassified";
    default:                                   return "none";
    }
}

void sober_provenance_gate_snapshot(SoberProvenanceGateSnapshot *out)
{
    if (!out) return;
    out->guest_base = atomic_load_explicit(&g_base, memory_order_acquire);
    out->enabled = atomic_load_explicit(&g_enabled, memory_order_acquire);
    out->installed = atomic_load_explicit(&g_installed, memory_order_acquire);
    out->build_verified = atomic_load_explicit(&g_build_verified,
                                               memory_order_acquire);
    out->dispatch_count = atomic_load_explicit(&g_dispatch_count,
                                               memory_order_relaxed);
    out->genuine_count = sober_provenance_gate_genuine_count(
                             SOBER_PROV_GATE_QUALIFYING_NAME);
    out->harness_count = atomic_load_explicit(&g_harness_count,
                                              memory_order_relaxed);
    out->appbridge_count = atomic_load_explicit(&g_appbridge_count,
                                                memory_order_relaxed);
    out->thunk_return_addr = atomic_load_explicit(&g_thunk_ra,
                                                  memory_order_relaxed);
    out->caller_return_addr = atomic_load_explicit(&g_caller_ra,
                                                  memory_order_relaxed);
    out->node_caller_addr = atomic_load_explicit(&g_node_caller_ra,
                                                 memory_order_relaxed);
    out->cached_class = atomic_load_explicit(&g_cached_class,
                                            memory_order_relaxed);
    out->cached_method = atomic_load_explicit(&g_cached_method,
                                             memory_order_relaxed);
    out->place_id = atomic_load_explicit(&g_place_id, memory_order_relaxed);
    out->last_verdict = atomic_load_explicit(&g_last_verdict,
                                             memory_order_relaxed);
}

const char *sober_provenance_gate_describe(char *buf, size_t cap)
{
    if (!buf || cap == 0u) return "";
    snprintf(buf, cap,
             "provenance-gate: enabled=%d installed=%d build=%d base=0x%llx "
             "dispatches=%llu genuine=%llu harness=%llu appbridge=%llu "
             "thunk_ret=0x%llx caller_ret=0x%llx node_ret=0x%llx verdict=%s "
             "class=%p method=%p",
             atomic_load_explicit(&g_enabled, memory_order_acquire),
             atomic_load_explicit(&g_installed, memory_order_acquire),
             atomic_load_explicit(&g_build_verified, memory_order_acquire),
             (unsigned long long)atomic_load_explicit(&g_base,
                                                      memory_order_acquire),
             (unsigned long long)atomic_load_explicit(&g_dispatch_count,
                                                      memory_order_relaxed),
             (unsigned long long)sober_provenance_gate_genuine_count(
                 SOBER_PROV_GATE_QUALIFYING_NAME),
             (unsigned long long)atomic_load_explicit(&g_harness_count,
                                                      memory_order_relaxed),
             (unsigned long long)atomic_load_explicit(&g_appbridge_count,
                                                      memory_order_relaxed),
             (unsigned long long)atomic_load_explicit(&g_thunk_ra,
                                                      memory_order_relaxed),
             (unsigned long long)atomic_load_explicit(&g_caller_ra,
                                                      memory_order_relaxed),
             (unsigned long long)atomic_load_explicit(&g_node_caller_ra,
                                                      memory_order_relaxed),
             sober_provenance_gate_verdict_name(
                 atomic_load_explicit(&g_last_verdict, memory_order_relaxed)),
             (void *)atomic_load_explicit(&g_cached_class, memory_order_relaxed),
             (void *)atomic_load_explicit(&g_cached_method, memory_order_relaxed));
    return buf;
}
