/*
 * publish_default.c - DEFAULT-ON genuine MessageBus GameLoaded publication.
 * See publish_default.h for the full evidence chain, the frozen RVAs and the
 * exact wiring signature for the lead.
 *
 * HARD RULES OBSERVED HERE
 * ------------------------
 *  - Default ON; SOBER_PUBLISH_DEFAULT=0|off|no|false -> every entry point is
 *    an inert no-op returning SOBER_PD_DISABLED.
 *  - The only guest state change is the guest's OWN subscription-node dispatch
 *    (base+0x27eaf04) running its genuine chain 0x27eaf2f -> 0x27eaf4c ->
 *    0x27eaf6e -> 0x25848e0.  This module performs NO host byte writes to
 *    guest memory, NO code patches, NO fabricated descriptor/node/vtable and
 *    NO direct call to the notifier (0x27eaf6e) or the thunk (0x25848e0).
 *  - The placeId payload is supplied as a host stack jlong whose ADDRESS is
 *    passed in RSI; the guest reads it.  No pointer is written into the guest.
 *  - No callbacks are registered and no decision-013 marker is emitted;
 *    sober_publish_default_should_withhold() is provided so the lead can keep
 *    the marker force-withheld while this state-seeding module is active.
 *  - Guest access is read-only except for entering the genuine dispatch.
 */
#define _GNU_SOURCE
#include "publish_default.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

typedef void (*sober_pd_dispatch_fn)(void *node, const int64_t *place_id);
typedef long (*sober_pd_start_game_fn)(void *env, void *jclass, void *params);
/* Genuine guest registration: register(event_id=1, &cached_slot_global). */
typedef void (*sober_pd_register_fn)(int event_id, void *slot_global);

static _Atomic uint64_t g_hits[SOBER_PD_STAGE_COUNT];
static _Atomic uint64_t g_publish_calls;
static _Atomic uint64_t g_chain_ok;
static _Atomic int64_t  g_last_place_id;
static _Atomic int64_t  g_observed_place_id;
static _Atomic uintptr_t g_guest_base;
static _Atomic uintptr_t g_node;
static _Atomic uintptr_t g_descriptor;
static _Atomic int       g_last_rc;
static _Atomic int       g_verified;
static _Atomic uint64_t  g_register_calls;

/* ---- gate ---------------------------------------------------------------- */
int sober_publish_default_enabled(void)
{
    const char *e = getenv(SOBER_PUBLISH_DEFAULT_ENV);
    if (!e)
        return 1;
    if (strcmp(e, "0") == 0 || strcasecmp(e, "off") == 0 ||
        strcasecmp(e, "no") == 0 || strcasecmp(e, "false") == 0)
        return 0;
    return 1;
}

const char *sober_publish_default_env_vars(void)
{
    return SOBER_PUBLISH_DEFAULT_ENV "," SOBER_PUBLISH_NODE_REDRIVE_ENV;
}

/* Kill switch for the DIAGNOSTIC 0x253cb56 re-drive.  Default ENABLED so
 * existing behaviour is preserved; an exact falsy value makes pd_ensure_node
 * an inert no-op.  The re-drive still seeds state and stays force-withheld. */
static int pd_node_redrive_enabled(void)
{
    const char *e = getenv(SOBER_PUBLISH_NODE_REDRIVE_ENV);
    if (!e)
        return 1;
    return !(strcmp(e, "0") == 0 || strcasecmp(e, "off") == 0 ||
             strcasecmp(e, "no") == 0 || strcasecmp(e, "false") == 0);
}

const char *sober_publish_default_node_redrive_env(void)
{
    return SOBER_PUBLISH_NODE_REDRIVE_ENV;
}

int sober_publish_default_should_withhold(void)
{
    /* Decision 013: the re-drive is a diagnostic seed and stays force-withheld,
     * but a module that was merely compiled/enabled and never actually drove
     * 0x253cb56 / 0x27eaf04 manufactured no state, so it must not make the
     * clean path structurally unreachable.  Once it has re-driven (or a hit
     * was recorded) it is withheld for the rest of the run. */
    if (!sober_publish_default_enabled()) return 0;
    if (atomic_load_explicit(&g_register_calls, memory_order_relaxed) != 0u)
        return 1;
    if (atomic_load_explicit(&g_publish_calls, memory_order_relaxed) != 0u)
        return 1;
    for (unsigned i = 0u; i < (unsigned)SOBER_PD_STAGE_COUNT; ++i)
        if (atomic_load_explicit(&g_hits[i], memory_order_relaxed) != 0u)
            return 1;
    return 0;
}

/* ---- raw read-only guest accessors --------------------------------------- */
static void *pd_ptr(const unsigned char *base, uint32_t rva)
{
    return *(void *volatile *)(base + rva);
}

static uint64_t pd_u64(const unsigned char *base, uint32_t rva)
{
    return *(volatile uint64_t *)(base + rva);
}

/* ---- read-only chain verification / discovery ---------------------------- */
void *sober_publish_default_descriptor(void *guest_base, void *node)
{
    unsigned char *b = (unsigned char *)guest_base;
    void *desc;

    if (!b || !node)
        return NULL;
    desc = *(void *volatile *)((unsigned char *)node + SOBER_PD_NODE_OFF_DESC_SELF);
    if (!desc)
        return NULL;
    /* Genuine id=1 GameLoaded descriptor? */
    if (*(void *volatile *)((unsigned char *)desc + SOBER_PD_DESC_OFF_VPTR) !=
        (void *)(b + SOBER_PD_RVA_DESC_VPTR))
        return NULL;
    if ((uint64_t)*(volatile uint64_t *)((unsigned char *)desc + SOBER_PD_DESC_OFF_ID) !=
        (uint64_t)SOBER_PD_DESC_ID_GAMELOADED)
        return NULL;
    if (*(void *volatile *)((unsigned char *)desc + SOBER_PD_DESC_OFF_SLOT) !=
        (void *)(b + SOBER_PD_RVA_SLOT_GLOBAL))
        return NULL;
    return desc;
}

void *sober_publish_default_find_node(void *guest_base)
{
    unsigned char *b = (unsigned char *)guest_base;
    unsigned char *buses[2];
    size_t i, nbus = 0;

    if (!b)
        return NULL;

    /* Default singleton first; mode-3 singleton when its guards are armed. */
    buses[nbus++] = b + SOBER_PD_RVA_BUS_DEFAULT;
    if (*(volatile uint8_t *)(b + SOBER_PD_RVA_BUS_GUARD_A) &&
        *(volatile uint8_t *)(b + SOBER_PD_RVA_BUS_GUARD_B))
        buses[nbus++] = b + SOBER_PD_RVA_BUS_MODE3;

    for (i = 0; i < nbus; ++i) {
        unsigned char *cur =
            *(void *volatile *)(buses[i] + SOBER_PD_OFF_BUS_LIST);
        unsigned n;

        for (n = 0; cur && n < SOBER_PD_MAX_NODE_WALK; ++n) {
            void *dispatch =
                *(void *volatile *)(cur + SOBER_PD_NODE_OFF_DISPATCH);
            if (dispatch == (void *)(b + SOBER_PD_RVA_NODE_DISPATCH) &&
                sober_publish_default_descriptor(b, cur) != NULL)
                return cur;
            cur = *(void *volatile *)(cur + SOBER_PD_NODE_OFF_NEXT);
        }
    }
    return NULL;
}

int64_t sober_publish_default_seeded_place_id(void *guest_base)
{
    if (!guest_base)
        return 0;
    return (int64_t)pd_u64((unsigned char *)guest_base,
                           SOBER_PD_RVA_PLACE_ID_GLOBAL);
}

/* ---- latch-ready prerequisite report (task #500) ------------------------
 * Read-only: no guest call, no write, no callback, no publish.  The id=1
 * registration at 0x253cb56 consumes its one-shot guard 0x72a31ba at function
 * entry, BEFORE it registers any descriptor.  If the guard is spent while the
 * node is still absent, the guest's only registration attempt ran too early
 * and no host-side re-drive can recover it.  This report makes that exact
 * condition visible instead of collapsing everything into NO_NODE. */
int sober_publish_default_prereqs(void *guest_base, SoberPublishPrereqs *out,
                                  char *buf, size_t cap)
{
    unsigned char *b = (unsigned char *)guest_base;
    SoberPublishPrereqs local;
    SoberPublishPrereqs *p = out ? out : &local;
    uint32_t flags = 0u;
    int rc = SOBER_PD_OK;

    memset(p, 0, sizeof(*p));
    if (buf && cap)
        buf[0] = '\0';
    if (!b) {
        p->rc = SOBER_PD_ERR;
        return SOBER_PD_ERR;
    }

    /* 1. APS2-relocated virtual / slot / notifier chain + dispatch canary. */
    if (pd_ptr(b, SOBER_PD_RVA_CANARY_GLOBAL) &&
        pd_ptr(b, SOBER_PD_RVA_INVOKE_SLOT) ==
            (void *)(b + SOBER_PD_RVA_DESC_INVOKE) &&
        pd_ptr(b, SOBER_PD_RVA_SLOT_GLOBAL) ==
            (void *)(b + SOBER_PD_RVA_NOTIFIER_TABLE) &&
        pd_ptr(b, SOBER_PD_RVA_NOTIFIER_TABLE) ==
            (void *)(b + SOBER_PD_RVA_NOTIFIER))
        flags |= SOBER_PD_PREREQ_CHAIN;
    else if (rc == SOBER_PD_OK)
        rc = SOBER_PD_BAD_CHAIN;

    /* 2. Cached JNI class/method the notifier 0x27eaf6e dereferences. */
    if (pd_ptr(b, SOBER_PD_RVA_CLASS_GLOBAL) &&
        pd_ptr(b, SOBER_PD_RVA_METHOD_GLOBAL))
        flags |= SOBER_PD_PREREQ_JNI;
    else if (rc == SOBER_PD_OK)
        rc = SOBER_PD_NO_STATE;

    /* 3. MessageBus singleton: guards 0x72cc270/0x72cc718 armed (the guest's
     * 0x24a8e44(4) constructor path) and the default bus object present. */
    p->bus_guard_a = *(volatile uint8_t *)(b + SOBER_PD_RVA_BUS_GUARD_A);
    p->bus_guard_b = *(volatile uint8_t *)(b + SOBER_PD_RVA_BUS_GUARD_B);
    p->bus = pd_ptr(b, SOBER_PD_RVA_BUS_DEFAULT);
    if (p->bus_guard_a && p->bus_guard_b && p->bus)
        flags |= SOBER_PD_PREREQ_BUS;
    else if (rc == SOBER_PD_OK)
        rc = SOBER_PD_NO_NODE;

    /* 4. Genuine registered id=1 GameLoaded node (descriptor vptr/id/slot). */
    p->node = sober_publish_default_find_node(b);
    if (p->node)
        flags |= SOBER_PD_PREREQ_NODE;

    /* 5. Guest-seeded placeId from StartGameWithParam (0x2ebd86a -> 0x72a31c0). */
    p->place_id = (int64_t)pd_u64(b, SOBER_PD_RVA_PLACE_ID_GLOBAL);
    if (p->place_id > 0)
        flags |= SOBER_PD_PREREQ_PLACE_ID;
    else if (rc == SOBER_PD_OK)
        rc = SOBER_PD_NO_STATE;

    /* 6. App singleton the closure 0x2ec1340 reads via 0x30ef840. */
    p->app = pd_ptr(b, SOBER_PD_RVA_APP_SINGLETON);
    if (p->app)
        flags |= SOBER_PD_PREREQ_APP;
    else if (rc == SOBER_PD_OK)
        rc = SOBER_PD_NO_STATE;

    /* Always report the one-shot guard, even when everything else is ready. */
    p->node_guard = *(volatile uint8_t *)(b + SOBER_PD_RVA_NODE_GUARD);
    p->flags = flags;
    p->rc = rc;

    if (buf && cap)
        snprintf(buf, cap,
                 "chain=%u jni=%u bus=%u node=%u placeId=%u app=%u "
                 "guard@0x72a31ba=%u bus_guards=%u/%u bus=%p app=%p node=%p "
                 "placeId=%lld -> flags=0x%x rc=%d",
                 !!(flags & SOBER_PD_PREREQ_CHAIN),
                 !!(flags & SOBER_PD_PREREQ_JNI),
                 !!(flags & SOBER_PD_PREREQ_BUS),
                 !!(flags & SOBER_PD_PREREQ_NODE),
                 !!(flags & SOBER_PD_PREREQ_PLACE_ID),
                 !!(flags & SOBER_PD_PREREQ_APP),
                 (unsigned)p->node_guard, (unsigned)p->bus_guard_a,
                 (unsigned)p->bus_guard_b, p->bus, p->app, p->node,
                 (long long)p->place_id, flags, rc);
    return rc;
}

int sober_publish_default_verify(void *guest_base, char *buf, size_t cap)
{
    unsigned char *b = (unsigned char *)guest_base;
    void *node, *desc;
    int rc = SOBER_PD_OK;

    if (buf && cap)
        buf[0] = '\0';
    if (!b) {
        rc = SOBER_PD_ERR;
        goto done;
    }

    /* Stack canary used by the dispatch prologue must be live. */
    if (!pd_ptr(b, SOBER_PD_RVA_CANARY_GLOBAL)) {
        rc = SOBER_PD_BAD_CHAIN;
        goto done;
    }
    /* APS2-resolved virtual / slot / notifier chain. */
    if (pd_ptr(b, SOBER_PD_RVA_INVOKE_SLOT) !=
            (void *)(b + SOBER_PD_RVA_DESC_INVOKE) ||
        pd_ptr(b, SOBER_PD_RVA_SLOT_GLOBAL) !=
            (void *)(b + SOBER_PD_RVA_NOTIFIER_TABLE) ||
        pd_ptr(b, SOBER_PD_RVA_NOTIFIER_TABLE) !=
            (void *)(b + SOBER_PD_RVA_NOTIFIER)) {
        rc = SOBER_PD_BAD_CHAIN;
        goto done;
    }
    /* Cached JNI globals the notifier dereferences. */
    if (!pd_ptr(b, SOBER_PD_RVA_CLASS_GLOBAL) ||
        !pd_ptr(b, SOBER_PD_RVA_METHOD_GLOBAL)) {
        rc = SOBER_PD_NO_STATE;
        goto done;
    }
    /* Genuine registered id=1 subscription node. */
    node = sober_publish_default_find_node(b);
    if (!node) {
        rc = SOBER_PD_NO_NODE;
        goto done;
    }
    desc = sober_publish_default_descriptor(b, node);
    if (!desc) {
        rc = SOBER_PD_NO_NODE;
        goto done;
    }

    atomic_store(&g_node, (uintptr_t)node);
    atomic_store(&g_descriptor, (uintptr_t)desc);
    atomic_store(&g_guest_base, (uintptr_t)b);
    atomic_store(&g_verified, 1);

done:
    if (rc != SOBER_PD_OK)
        atomic_store(&g_verified, 0);
    if (buf && cap) {
        int64_t pid = b ? (int64_t)pd_u64(b, SOBER_PD_RVA_PLACE_ID_GLOBAL) : 0;
        snprintf(buf, cap,
                 "canary=%p invoke=%p slot=%p table=%p notifier=%p "
                 "class=%p method=%p node=%p desc=%p placeId=%lld -> %s",
                 b ? pd_ptr(b, SOBER_PD_RVA_CANARY_GLOBAL) : NULL,
                 b ? pd_ptr(b, SOBER_PD_RVA_INVOKE_SLOT) : NULL,
                 b ? pd_ptr(b, SOBER_PD_RVA_SLOT_GLOBAL) : NULL,
                 b ? pd_ptr(b, SOBER_PD_RVA_NOTIFIER_TABLE) : NULL,
                 b ? (void *)(b + SOBER_PD_RVA_NOTIFIER) : NULL,
                 b ? pd_ptr(b, SOBER_PD_RVA_CLASS_GLOBAL) : NULL,
                 b ? pd_ptr(b, SOBER_PD_RVA_METHOD_GLOBAL) : NULL,
                 (void *)(uintptr_t)atomic_load(&g_node),
                 (void *)(uintptr_t)atomic_load(&g_descriptor),
                 (long long)pid,
                 rc == SOBER_PD_OK ? "OK" :
                 rc == SOBER_PD_NO_NODE ? "NO-NODE" :
                 rc == SOBER_PD_BAD_CHAIN ? "BAD-CHAIN" :
                 rc == SOBER_PD_NO_STATE ? "NO-STATE" : "ERR");
    }
    return rc;
}

/* ---- ensure the guest's own id=1 subscription node exists ----------------
 * On the real app path, nativeAppBridgeV2InitWithParams (0x24abf96) registers
 * the GameLoaded descriptor at 0x24ad6a5 via `register(1, base+0x7234520)`.
 * That call is reached only after the app-state prerequisites (FFlag control
 * block / singleton) exist; when they are absent, the guest never registers
 * the node and find_node() returns NULL, so publication cannot start.
 *
 * We re-drive that SAME genuine guest registration function here.  The guest
 * builds the id=1 descriptor (vptr 0x6dbbf98, slot &0x7234520) and the 0x60
 * node with its own allocator (0x1dbab02) and inserts it into the MessageBus
 * +0x288 list.  The host supplies only (event_id=1, &slot_global); no guest
 * byte is written by us, no descriptor/node/vtable is fabricated, and we
 * never call the notifier (0x27eaf6e) or thunk (0x25848e0) directly.  The
 * guest's one-shot guard at base+0x72a31ba makes repeated calls inert. */
static void *pd_ensure_node(unsigned char *b)
{
    void *node = sober_publish_default_find_node(b);
    uint8_t guard;

    if (node) return node;      /* already registered by the guest */

    if (!sober_publish_default_enabled()) return NULL;
    if (!pd_node_redrive_enabled()) {
        fprintf(stderr,
                "[publish-default] 0x253cb56 re-drive disabled by %s=0 "
                "(diagnostic seed stays WITHHELD)\n",
                SOBER_PUBLISH_NODE_REDRIVE_ENV);
        return NULL;
    }

    /* FAULT FIX (#500): 0x253cb56 spends its one-shot guard at 0x253cb91,
     * BEFORE registering the id=1 descriptor at 0x253d06e.  If the guard is
     * already 1 while no node exists, the guest's single attempt ran before
     * its prerequisites were ready; a re-drive would just hit 0x253d1b7 and
     * do nothing.  Detect that here instead of silently retrying forever. */
    guard = *(volatile uint8_t *)(b + SOBER_PD_RVA_NODE_GUARD);
    if (guard) {
        fprintf(stderr,
                "[publish-default] id=1 node absent but one-shot guard@0x72a31ba=1: "
                "guest registration 0x253cb56(1,&0x7234520) already spent before "
                "prerequisites were ready; re-drive INERT (WITHHELD, no fabricated node)\n");
        return NULL;
    }

    {
        sober_pd_register_fn reg =
            (sober_pd_register_fn)(uintptr_t)(b + SOBER_PD_RVA_NODE_REGISTER);
        atomic_fetch_add(&g_register_calls, 1);
        reg(1, b + SOBER_PD_RVA_SLOT_GLOBAL);
    }
    node = sober_publish_default_find_node(b);
    /* One-line diagnostic: the guest registers several MessageBus descriptors,
     * but the id=1 GameLoaded node only exists once nativeAppBridgeV2InitWithParams
     * reaches 0x24ad6a5 (which needs the app-state prerequisites).  Until then
     * find_node() returns NULL and publication is WITHHELD, not fabricated. */
    fprintf(stderr,
            "[publish-default] guest node registration 0x253cb56(1,&0x7234520) "
            "-> node=%p (NULL = id=1 GameLoaded node not registered yet)\n", node);
    return node;
}

/* ---- the genuine publication --------------------------------------------- */
int sober_publish_default_publish(void *guest_base, int64_t place_id)
{
    unsigned char *b = (unsigned char *)guest_base;
    void *node;
    sober_pd_dispatch_fn dispatch;
    int rc;

    if (!sober_publish_default_enabled()) {
        atomic_store(&g_last_rc, SOBER_PD_DISABLED);
        return SOBER_PD_DISABLED;
    }
    if (!b) {
        atomic_store(&g_last_rc, SOBER_PD_ERR);
        return SOBER_PD_ERR;
    }
    if (place_id <= 0) {
        atomic_store(&g_last_rc, SOBER_PD_NO_STATE);
        return SOBER_PD_NO_STATE;  /* 0 skips the guest log at 0x27eafb3 */
    }

    /* Make sure the guest itself registered its id=1 subscription node before
     * verifying the chain.  Genuine guest call; no fabricated state. */
    (void)pd_ensure_node(b);

    rc = sober_publish_default_verify(b, NULL, 0);
    if (rc != SOBER_PD_OK) {
        atomic_store(&g_last_rc, rc);
        return rc;
    }
    node = (void *)(uintptr_t)atomic_load(&g_node);
    if (!node) {
        atomic_store(&g_last_rc, SOBER_PD_NO_NODE);
        return SOBER_PD_NO_NODE;
    }

    /* Enter the guest's OWN subscription-node dispatch.  It reads its embedded
     * id=1 descriptor from node+0x50 and issues `call [vptr+0x30]` at 0x27eaf2f
     * with RDI=descriptor and RSI=&place_id, so the guest itself reaches
     * 0x27eaf4c -> 0x27eaf6e -> 0x25848e0 (return address 0x27eafb3). */
    dispatch = (sober_pd_dispatch_fn)(uintptr_t)(b + SOBER_PD_RVA_NODE_DISPATCH);
    atomic_fetch_add(&g_chain_ok, 1);
    atomic_fetch_add(&g_publish_calls, 1);
    atomic_fetch_add(&g_hits[SOBER_PD_STAGE_DISPATCH], 1);
    atomic_store(&g_last_place_id, place_id);
    atomic_store(&g_guest_base, (uintptr_t)b);
    dispatch(node, &place_id);
    atomic_store(&g_last_rc, SOBER_PD_OK);
    return SOBER_PD_OK;
}

int sober_publish_default_publish_seeded(void *guest_base)
{
    int64_t pid;

    if (!sober_publish_default_enabled())
        return SOBER_PD_DISABLED;
    if (!guest_base)
        return SOBER_PD_ERR;
    pid = sober_publish_default_seeded_place_id(guest_base);
    if (pid <= 0) {
        atomic_store(&g_last_rc, SOBER_PD_NO_STATE);
        return SOBER_PD_NO_STATE;
    }
    return sober_publish_default_publish(guest_base, pid);
}

int sober_publish_default_drive(void *guest_base, void *jni_env, void *jclass,
                                void *start_game_params,
                                int64_t place_id_override)
{
    unsigned char *b = (unsigned char *)guest_base;
    int64_t pid;
    int rc;

    if (!sober_publish_default_enabled())
        return SOBER_PD_DISABLED;
    if (!b)
        return SOBER_PD_ERR;

    /* Optional genuine upstream: nativeAppBridgeV2StartGameWithParam.  This is
     * real guest code; it reads placeId()J through the bridge and caches it at
     * 0x72a31c0, feeding the DataModel/SessionReporter game-load state. */
    if (jni_env && jclass && start_game_params) {
        sober_pd_start_game_fn start_game =
            (sober_pd_start_game_fn)(uintptr_t)(b + SOBER_PD_RVA_START_GAME);
        long sg_rc = start_game(jni_env, jclass, start_game_params);
        fprintf(stderr,
                "[publish-default] guest call 0x2ebd51a "
                "nativeAppBridgeV2StartGameWithParam -> rc=%ld\n", sg_rc);
    }

    pid = place_id_override ? place_id_override :
          sober_publish_default_seeded_place_id(b);
    rc = sober_publish_default_publish(b, pid);
    fprintf(stderr,
            "[publish-default] publish placeId=%lld node=%p desc=%p -> %s\n",
            (long long)pid,
            (void *)(uintptr_t)atomic_load(&g_node),
            (void *)(uintptr_t)atomic_load(&g_descriptor),
            rc == SOBER_PD_OK ? "OK" :
            rc == SOBER_PD_NO_NODE ? "NO-NODE" :
            rc == SOBER_PD_BAD_CHAIN ? "BAD-CHAIN" :
            rc == SOBER_PD_NO_STATE ? "NO-STATE" :
            rc == SOBER_PD_DISABLED ? "DISABLED" : "ERR");
    return rc;
}

/* ---- hit observation (fed by external, lead-wired probes) ---------------- */
void sober_publish_default_record_hit(unsigned stage)
{
    if (stage < SOBER_PD_STAGE_COUNT)
        atomic_fetch_add(&g_hits[stage], 1);
}

void sober_publish_default_record_hit_place(unsigned stage, int64_t place_id)
{
    if (stage < SOBER_PD_STAGE_COUNT) {
        atomic_fetch_add(&g_hits[stage], 1);
        atomic_store(&g_observed_place_id, place_id);
    }
}

/* ---- counters / diagnostics ---------------------------------------------- */
uint64_t sober_publish_default_hits(unsigned stage)
{
    return stage < SOBER_PD_STAGE_COUNT ? atomic_load(&g_hits[stage]) : 0;
}

uint64_t sober_publish_default_publish_calls(void)
{
    return atomic_load(&g_publish_calls);
}

int64_t sober_publish_default_last_place_id(void)
{
    return atomic_load(&g_last_place_id);
}

int sober_publish_default_last_rc(void)
{
    return atomic_load(&g_last_rc);
}

void sober_publish_default_snapshot(SoberPublishSnapshot *out)
{
    unsigned i;

    if (!out)
        return;
    for (i = 0; i < SOBER_PD_STAGE_COUNT; ++i)
        out->hits[i] = atomic_load(&g_hits[i]);
    out->publish_calls     = atomic_load(&g_publish_calls);
    out->chain_ok          = atomic_load(&g_chain_ok);
    out->last_place_id     = atomic_load(&g_last_place_id);
    out->observed_place_id = atomic_load(&g_observed_place_id);
    out->guest_base        = atomic_load(&g_guest_base);
    out->node              = atomic_load(&g_node);
    out->descriptor        = atomic_load(&g_descriptor);
    out->last_rc           = atomic_load(&g_last_rc);
    out->verified          = atomic_load(&g_verified);
}

const char *sober_publish_default_describe(char *buf, size_t cap)
{
    if (!buf || cap == 0)
        return "";
    snprintf(buf, cap,
             "enabled=%d gate=%s publish=%llu chain_ok=%llu register=%llu "
             "hits[dispatch=%llu call=%llu invoke=%llu notifier=%llu "
             "thunk_ret=%llu callback=%llu] placeId=%lld last_rc=%d "
             "node=%p desc=%p redrive=%d",
             sober_publish_default_enabled(), SOBER_PUBLISH_DEFAULT_ENV,
             (unsigned long long)atomic_load(&g_publish_calls),
             (unsigned long long)atomic_load(&g_chain_ok),
             (unsigned long long)atomic_load(&g_register_calls),
             (unsigned long long)atomic_load(&g_hits[SOBER_PD_STAGE_DISPATCH]),
             (unsigned long long)atomic_load(&g_hits[SOBER_PD_STAGE_PUBLISH_CALL]),
             (unsigned long long)atomic_load(&g_hits[SOBER_PD_STAGE_INVOKE]),
             (unsigned long long)atomic_load(&g_hits[SOBER_PD_STAGE_NOTIFIER]),
             (unsigned long long)atomic_load(&g_hits[SOBER_PD_STAGE_THUNK_RET]),
             (unsigned long long)atomic_load(&g_hits[SOBER_PD_STAGE_CALLBACK]),
             (long long)atomic_load(&g_last_place_id),
             atomic_load(&g_last_rc),
             (void *)(uintptr_t)atomic_load(&g_node),
             (void *)(uintptr_t)atomic_load(&g_descriptor),
             pd_node_redrive_enabled());
    return buf;
}

void sober_publish_default_reset(void)
{
    unsigned i;

    for (i = 0; i < SOBER_PD_STAGE_COUNT; ++i)
        atomic_store(&g_hits[i], 0);
    atomic_store(&g_publish_calls, 0);
    atomic_store(&g_chain_ok, 0);
    atomic_store(&g_last_place_id, 0);
    atomic_store(&g_observed_place_id, 0);
    atomic_store(&g_guest_base, 0);
    atomic_store(&g_node, 0);
    atomic_store(&g_descriptor, 0);
    atomic_store(&g_last_rc, SOBER_PD_DISABLED);
    atomic_store(&g_verified, 0);
    atomic_store(&g_register_calls, 0);
}
