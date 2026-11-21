/*
 * flags_latch_default.c - DEFAULT-ON flags-loaded latch module (task #489).
 * See flags_latch_default.h for the evidence chain and hard rules.
 *
 * Every state change here is produced by genuine shipped guest code.  This
 * module itself performs NO byte write to 0x7cdd149, no code patch, no fake
 * vtable and no callback fabrication.  All attempted routes are opt-in
 * modifiers of an otherwise inert default run and force the marker withhold.
 */
#define _GNU_SOURCE
#include "flags_latch_default.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "native_jni.h"
#include "jni_arrays.h"
#include "jni_strings.h"

static void  *g_guest_base;          /* set by install(), for latch()        */
static int    g_installed;
static int    g_active;              /* any route attempted -> withhold      */
static unsigned g_route_count;

static int env_off(const char *name)
{
    const char *v = getenv(name);
    return v && (v[0] == '0' || !strcasecmp(v, "off") ||
                 !strcasecmp(v, "no") || !strcasecmp(v, "false"));
}

int sober_flags_latch_default_enabled(void)
{
    return !env_off(SOBER_FLAGS_LATCH_DEFAULT_ENV);
}

const char *sober_flags_latch_default_env_vars(void)
{
    return SOBER_FLAGS_LATCH_DEFAULT_ENV "," SOBER_FLAGS_LATCH_ROUTE_ENV;
}

int sober_flags_latch_default_should_withhold(void)
{
    return g_active;
}

unsigned sober_flags_latch_default_latch(void)
{
    if (!g_guest_base) return 0;
    return *(volatile unsigned char *)(
        (unsigned char *)g_guest_base + SOBER_FLAGS_LATCH_RVA_BYTE);
}

/* Build the three strings nativeInitClientSettings expects.  Use the tagged
 * JNI string path when the guest-facing jstr module is enabled, otherwise the
 * ordinary bridge jstring (both are real JNI objects, not byte writes). */
static void *make_utf8(void *j, void *base, const char *s)
{
    if (sober_jstr_enabled()) return sober_jstr_create(base, s);
    return sober_jni_new_string_utf8(j, s);
}

/* Route 1: nativeInitClientSettings 0x2359277 -> 0x2eb7155 -> 0x2eb7520.
 * Safe (no fatal tail) but its parse-ctx gate 0x7697d08 is 1 in our default
 * state, so the writer is skipped and the latch stays 0. */
static int route_client_settings(void *j, void *base, void *env, void *cls)
{
    void *a = make_utf8(j, base, "{}");
    void *b = make_utf8(j, base, "LIVE");
    void *c = make_utf8(j, base, "2.740.931");
    long rc = ((long (*)(void *, void *, void *, void *, void *))
        (unsigned char *)base + SOBER_FLAGS_LATCH_RVA_INIT_CS)(env, cls, a, b, c);
    fprintf(stderr, "[flags-latch] nativeInitClientSettings 0x2359277 -> rc=%ld "
                    "parse_ctx=%u latch=%u\n", rc,
            (unsigned)*(volatile unsigned char *)
                ((unsigned char *)base + SOBER_FLAGS_LATCH_RVA_PARSE_CTX),
            sober_flags_latch_default_latch());
    return 0;
}

/* Route 2 (A15 VIABLE ROUTE): nativeRunCrashpadHandler 0x2eca720 with a real
 * jstring array containing the genuine "--channel","LIVE" argv the guest's
 * own String[] path turns into the channel value at 0x72a3710/0x72a3718.
 * The guest then reaches latch writer 0x2ec9fae with an argv it built itself
 * (no hand-mutated pointer).  Its post-latch log path used to abort in our
 * compat stdio shim; lead's #491 resolves that, after which this route sets
 * the latch cleanly. */
static int route_crashpad_jni(void *j, void *base, void *env, void *cls)
{
    unsigned char *b = (unsigned char *)base;
    static char channel[] = "LIVE";
    /* A15: 0x2ec9eed is gated on channel string @0x72a3718 != NULL
     * (0x2ec9f2e). Seed it through the guest's OWN std::string setter first so
     * the gate is satisfied regardless of what the JNI String[] walk does. */
    ((void (*)(void *, const char *))((unsigned char *)base + 0x2e57c20u))
        (b + 0x72a3710u, channel);
    const char *args[] = { "--channel", "LIVE" };
    void *arr = sober_jni_new_string_array(j, args, 2);
    if (arr && env && cls)
        ((void (*)(void *, void *, void *))
            ((unsigned char *)base + SOBER_FLAGS_LATCH_RVA_CRASHPAD_JNI))
            (env, cls, arr);
    fprintf(stderr, "[flags-latch] nativeRunCrashpadHandler 0x2eca720 -> "
                    "channel_tag=%u latch=%u\n",
            (unsigned)*(volatile unsigned char *)(b + 0x72a3710u),
            sober_flags_latch_default_latch());
    return 0;
}

/* Route 4 (A15 NARROW ROUTE): fill the channel std::string with the guest's
 * own 0x2e57c20 setter, then call latch writer 0x2ec9eed directly with a
 * consistent native argv.  Useful if #491 needs the narrower surface. */
static int route_narrow(void *base)
{
    static char val[] = "LIVE";
    static char key[] = "--channel";
    static char prog[] = "crashpad";
    static char *argv[] = { prog, key, val, NULL };
    unsigned char *b = (unsigned char *)base;
    /* std::string channel at 0x72a3710/0x72a3718 via guest assignment. */
    ((void (*)(void *, const char *))((unsigned char *)base + 0x2e57c20u))
        (b + 0x72a3710u, val);
    ((void (*)(int, char **))((unsigned char *)base + 0x2ec9eedu))(3, argv);
    fprintf(stderr, "[flags-latch] narrow 0x2e57c20+0x2ec9eed -> latch=%u\n",
            sober_flags_latch_default_latch());
    return 0;
}

/* Route 3: CrashpadHandlerMain 0x2ec9c30(int, char**).  Same writer, native
 * argv route.  Diagnostic only (fatal tail possible). */
static int route_crashpad_main(void *base)
{
    char val[] = "LIVE";
    char key[] = "--channel";
    char prog[] = "crashpad";
    char *argv[] = { prog, key, val, NULL };
    ((void (*)(int, char **))
        ((unsigned char *)base + SOBER_FLAGS_LATCH_RVA_CRASHPAD_MAIN))(3, argv);
    fprintf(stderr, "[flags-latch] CrashpadHandlerMain 0x2ec9c30 -> latch=%u\n",
            sober_flags_latch_default_latch());
    return 0;
}

int sober_flags_latch_default_install(void *guest_base, void *jni_env,
                                      void *jclass, void *jni)
{
    if (!sober_flags_latch_default_enabled()) {
        fprintf(stderr, "[flags-latch] disabled (SOBER_FLAGS_LATCH_DEFAULT=0)\n");
        return SOBER_FLAGS_LATCH_DISABLED;
    }
    if (!guest_base) return SOBER_FLAGS_LATCH_ERR;

    g_guest_base = guest_base;
    if (!g_installed) g_installed = 1;

    const char *route = getenv(SOBER_FLAGS_LATCH_ROUTE_ENV);
    /* Lead ruling: the module is default-ON but NON-SEEDING by default.
     * route=none is inert: no guest call, should_withhold() stays 0.  The
     * A15 viable crashpad routes are explicitly opt-in and abort in the
     * compat stdio path until lead's #491 lands. */
    if (!route || !*route) route = "none";

    if (!strcmp(route, "none")) {
        fprintf(stderr, "[flags-latch] route=none (inert; no guest call, "
                        "latch=%u, withhold=0)\n",
                sober_flags_latch_default_latch());
        return sober_flags_latch_default_latch() ? SOBER_FLAGS_LATCH_OK
                                                 : SOBER_FLAGS_LATCH_ZERO;
    }

    g_active = 1;                    /* any real route seeds guest state */
    ++g_route_count;

    if (!strcmp(route, "client-settings"))
        route_client_settings(jni, guest_base, jni_env, jclass);
    else if (!strcmp(route, "crashpad-jni"))
        route_crashpad_jni(jni, guest_base, jni_env, jclass);
    else if (!strcmp(route, "crashpad-main"))
        route_crashpad_main(guest_base);
    else if (!strcmp(route, "narrow"))
        route_narrow(guest_base);
    else {
        fprintf(stderr, "[flags-latch] unknown route '%s'; no guest call\n", route);
        g_active = 0;               /* unknown route is inert too */
    }

    fprintf(stderr, "[flags-latch] installed route=%s latch@0x7cdd149=%u "
                    "installs=%u withhold=%d\n",
            route, sober_flags_latch_default_latch(), g_route_count,
            sober_flags_latch_default_should_withhold());

    return sober_flags_latch_default_latch() ? SOBER_FLAGS_LATCH_OK
                                             : SOBER_FLAGS_LATCH_ZERO;
}
