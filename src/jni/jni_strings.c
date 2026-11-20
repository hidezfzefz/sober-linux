/*
 * jni_strings.c - canonical jstring / java.lang.String payload builder
 * (Team B, worker B4).  NEW FILE - no existing src/ file is modified.
 *
 * Evidence (frozen engine BuildID 204ad049599c96a..., SHA 3e498d6494...):
 *   0x2315532  jstring -> native std::string.  Calls GetStringUTFChars via
 *              `call *0x548(%rax)` where 0x548 = (4 reserved + 165) * 8,
 *              copies bytes with 0x1dc1208, then ReleaseStringUTFChars at
 *              `*0x550(%rax)` (fn[166]).
 *   0x2e5d420  generic weak-ref helper: `rax = *(jobj); disp = *(rax-0x18)`
 *              -> a NEGATIVE-OFFSET header is required on the backing word.
 *   0x6ed8d00  genuine guest std::string header vtable (formatter 0x20ab4b6
 *              stores it at `data-0x20`; `data+0x08` self pointer; size/cap
 *              at +0x10/+0x18; capacity 0x1f4 at 0x20ab4f5).
 *   GOT 0x72293f8 holds &(__stack_chk_guard) = 0xABCDEF0123456789; it is a
 *   register-resident canary, never an object field (sentinel trace doc).
 *
 * No guest byte is patched, no host function pointer is published, no
 * callback is emitted, and the decision-013 marker is untouched.
 */
#define _GNU_SOURCE
#include "jni_strings.h"

#include "native_jni.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- SrObject-compatible handle mirror ---------------------------------
 * Byte-identical to SrObject in src/jni/native_jni.c through offset 0x90, so
 * both the bridge's sr_check() and our own accessors can read it.  Private
 * state lives in a module-local registry, never past +0x88 (bridge handles
 * are only 0x90 bytes; writing beyond that would corrupt the heap). */
typedef struct {
    void    *backing;      /* +0x00 */
    uint32_t magic;        /* +0x08  SR_OBJ_MAGIC "SJNI" */
    uint32_t reserved;     /* +0x0c */
    int32_t  kind;         /* +0x10  SR_K_STRING = 3 */
    uint32_t pad0;         /* +0x14 */
    char    *name;         /* +0x18 */
    char    *sig;          /* +0x20 */
    char    *class_name;   /* +0x28 */
    int32_t  refs;         /* +0x30 */
    uint32_t pad1;         /* +0x34 */
    void    *payload;      /* +0x38  GetStringUTFChars reads this */
    size_t   payload_size; /* +0x40 */
    uint64_t pad2[4];      /* +0x48 */
    void    *guest_c;      /* +0x68  GetStringChars reads this */
    void    *guest_d;      /* +0x70 */
    void    *guest_deep_c; /* +0x78 */
    void    *guest_table;  /* +0x80 */
    void    *guest_sub;    /* +0x88 */
} JstrHandle;

#define JSTR_MAGIC      0x534A4E49u   /* "SJNI" */
#define JSTR_K_STRING   3
#define JSTR_A_PREFIX   0x20u         /* negative header before backing */
#define JSTR_A_BYTES    0x1200u       /* matches sr_new_object's A block */
#define JSTR_D_PREFIX   0x20u         /* negative header before data object */
#define JSTR_D_BODY     0x20u         /* vtable/self/size/cap header */
#define JSTR_CLASS_NAME "java/lang/String"

/* ---- module state ------------------------------------------------------- */
typedef struct {
    void *h;        /* handle */
    void *hbase;    /* handle allocation base (== h) */
    void *bbase;    /* backing allocation base */
    void *dbase;    /* data-object allocation base */
} JstrRec;

#define JSTR_MAX_RECS 256
static JstrRec   g_recs[JSTR_MAX_RECS];
static unsigned  g_nrecs;
static void     *g_guest_base;
static unsigned long g_created;

int sober_jstr_enabled(void)
{
    const char *e = getenv("SOBER_CANONICAL_JSTRINGS");
    return (e && strcmp(e, "1") == 0);
}

void *sober_jstr_guest_base(void) { return g_guest_base; }
unsigned long sober_jstr_created(void) { return g_created; }
void sober_jstr_reset(void) { g_created = 0; }

static JstrRec *find_rec(const void *h)
{
    for (unsigned i = 0; i < g_nrecs; ++i)
        if (g_recs[i].h == h) return &g_recs[i];
    return NULL;
}

static void add_rec(void *h, void *hbase, void *bbase, void *dbase)
{
    if (g_nrecs >= JSTR_MAX_RECS) return;
    g_recs[g_nrecs].h = h;
    g_recs[g_nrecs].hbase = hbase;
    g_recs[g_nrecs].bbase = bbase;
    g_recs[g_nrecs].dbase = dbase;
    g_nrecs++;
}

/* Reject the sentinel and obviously bogus/foreign pointers before reading. */
static int safe_ptr(const void *p)
{
    uintptr_t v = (uintptr_t)p;
    if (!p || v < 0x10000u) return 0;
    if (v == (uintptr_t)SOBER_JSTR_SENTINEL) return 0;
    if (v > 0x00007fffffffffffULL && v < 0xffff000000000000ULL) return 0;
    return 1;
}

static JstrHandle *valid_handle(void *h)
{
    if (!safe_ptr(h)) return NULL;
    JstrHandle *x = (JstrHandle *)h;
    if (x->magic != JSTR_MAGIC || x->kind != JSTR_K_STRING) return NULL;
    return x;
}

static uintptr_t guest_vptr(void *base)
{
    if (!base) return 0;
    return (uintptr_t)((unsigned char *)base + SOBER_JSTR_RVA_STRING_VTABLE);
}

/* ---- payload builders --------------------------------------------------- */
/* Guest-shaped std::string data object with a negative-offset header:
 *   data-0x20 = genuine guest vtable 0x6ed8d00 (or 0 without guest_base)
 *   data-0x18 = displacement 0 (0x2e5d420 takes its safe branch)
 *   data+0x00 = vtable (mirror); +0x08 = self/inline-buffer; +0x10 = size;
 *   +0x18 = capacity 0x1f4; +0x20 = inline NUL-terminated bytes. */
static void *build_data_object(void *guest_base, const char *text,
                               size_t len, void **base_out)
{
    size_t bufbytes = len + 1 > JSTR_D_BODY ? len + 1 : JSTR_D_BODY;
    size_t total = JSTR_D_PREFIX + JSTR_D_BODY + bufbytes;
    unsigned char *blk = (unsigned char *)calloc(1, total);
    if (!blk) { if (base_out) *base_out = NULL; return NULL; }
    unsigned char *d = blk + JSTR_D_PREFIX;
    uintptr_t vt = guest_vptr(guest_base);
    uintptr_t self = (uintptr_t)(d + JSTR_D_BODY);
    uint64_t sz = (uint64_t)len, cap = SOBER_JSTR_STRING_CAPACITY;
    uint64_t zero = 0;

    memcpy(d - 0x20, &vt, sizeof(vt));      /* negative-offset vtable */
    memcpy(d - 0x18, &zero, sizeof(zero));  /* negative-offset displacement */
    memcpy(d + 0x00, &vt, sizeof(vt));
    memcpy(d + 0x08, &self, sizeof(self));
    memcpy(d + 0x10, &sz, sizeof(sz));
    memcpy(d + 0x18, &cap, sizeof(cap));
    memcpy(d + JSTR_D_BODY, text, len);
    d[JSTR_D_BODY + len] = 0;
    if (base_out) *base_out = blk;
    return d;
}

/* Modified-UTF-8 payload (NUL terminated, malloc'd so the bridge can free it). */
static void *build_utf8(const char *text, size_t len)
{
    char *p = (char *)malloc(len + 1);
    if (!p) return NULL;
    memcpy(p, text, len);
    p[len] = 0;
    return p;
}

/* UTF-16 (host-endian uint16_t) payload for GetStringChars. */
static uint16_t *build_utf16(const char *text, size_t len)
{
    uint16_t *w = (uint16_t *)calloc(len + 1, sizeof(uint16_t));
    if (!w) return NULL;
    for (size_t i = 0; i < len; ++i) {
        unsigned char b = (unsigned char)text[i];
        w[i] = b < 0x80u ? (uint16_t)b : (uint16_t)'?';
    }
    return w;   /* w[len] == 0 from calloc */
}

/* Canonical A backing with a negative-offset header, so a guest C++ deref of
 * `*(jobj)` then `*(backing-0x18)` lands on a guest RVA / zero, never on
 * malloc metadata, host code, or the stack-guard sentinel. */
static void *build_backing(void *guest_base, void **base_out)
{
    unsigned char *blk = (unsigned char *)calloc(1, JSTR_A_PREFIX + JSTR_A_BYTES);
    if (!blk) { if (base_out) *base_out = NULL; return NULL; }
    unsigned char *a = blk + JSTR_A_PREFIX;
    uintptr_t vt = guest_vptr(guest_base);
    uint64_t zero = 0;
    memcpy(a - 0x20, &vt, sizeof(vt));      /* A-0x20: guest string vtable */
    memcpy(a - 0x18, &zero, sizeof(zero));  /* A-0x18: displacement 0 */
    *(uint32_t *)(a + 0x00) = 0xc0dedbadu;  /* validator magic (0x245605d) */
    *(uint32_t *)(a + 0x10) = 0u;           /* index */
    memset(a + 0x20, 0, 8);                 /* no stale pointer */
    memset(a + 0x68, 0, 8);                 /* teardown safe branch */
    if (base_out) *base_out = blk;
    return a;
}

/* Heap duplicate so sober_jni_shutdown() can free() the field (bridge
 * handles).  Static literals here would crash its free(name/sig/class_name). */
static char *dup_str(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = (char *)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

/* Fill the SrObject-compatible fields of a freshly allocated handle. */
static int fill_handle(JstrHandle *x, void *guest_base,
                       const char *text, int canonical_backing)
{
    size_t len = strlen(text);
    void *dbase = NULL, *bbase = NULL;
    char *name = NULL, *sig = NULL, *cls = NULL;

    x->magic = JSTR_MAGIC;
    x->kind = JSTR_K_STRING;
    x->refs = 1;
    /* Heap-allocated, matching the bridge's SrObject contract.  They carry
     * only the literal class name / empty signature - no host metadata and
     * never the stack-guard sentinel. */
    name = dup_str(JSTR_CLASS_NAME);
    sig  = dup_str("");
    cls  = dup_str(JSTR_CLASS_NAME);
    if (!name || !sig || !cls) {
        free(name); free(sig); free(cls);
        return -1;
    }
    x->name = name;
    x->sig = sig;
    x->class_name = cls;

    void *data = build_data_object(guest_base, text, len, &dbase);
    void *utf8 = build_utf8(text, len);
    uint16_t *utf16 = build_utf16(text, len);
    if (!data || !utf8 || !utf16) {
        free(dbase); free(utf8); free(utf16);
        free(name); free(sig); free(cls);
        return -1;
    }
    x->payload = utf8;
    x->payload_size = len + 1;
    x->guest_c = utf16;      /* sr_GetStringChars returns +0x68 */
    x->guest_sub = data;     /* keep the shaped data object reachable */

    if (canonical_backing) {
        void *a = build_backing(guest_base, &bbase);
        if (!a) {
            free(dbase); free(utf8); free(utf16);
            free(name); free(sig); free(cls);
            return -1;
        }
        x->backing = a;
        add_rec(x, x, bbase, dbase);
    }
    g_created++;
    return 0;
}

/* ---- public builders ---------------------------------------------------- */
void *sober_jstr_create(void *guest_base, const char *text)
{
    if (!sober_jstr_enabled()) return NULL;
    if (!text) text = "";
    JstrHandle *x = (JstrHandle *)calloc(1, sizeof(*x));
    if (!x) return NULL;
    if (fill_handle(x, guest_base, text, 1) != 0) { free(x); return NULL; }
    return x;
}

void *sober_jstr_create_bridge(struct SoberJni *j, void *guest_base,
                               const char *text)
{
    if (!sober_jstr_enabled() || !j) return NULL;
    if (!text) text = "";
    void *h = sober_jni_new_string_utf8(j, text);
    if (!h) return NULL;
    /* sr_new_object allocated the original payload and B-node; fill_handle
     * replaces payload/guest_c with malloc'd buffers that the bridge frees at
     * shutdown.  The original B-node stays reachable via guest_table/sub and
     * is a harmless short-lived leak; canonical_backing=0 keeps guest_b at
     * the original calloc base so shutdown's free() stays valid. */
    JstrHandle *x = (JstrHandle *)h;
    if (fill_handle(x, guest_base, text, 0) != 0) return NULL;
    return h;
}

void sober_jstr_destroy(void *h)
{
    JstrHandle *x = valid_handle(h);
    if (!x) return;
    JstrRec *r = find_rec(h);
    if (r) {
        free(r->hbase);
        free(r->bbase);
        free(r->dbase);
        r->h = r->hbase = r->bbase = r->dbase = NULL;
        return;
    }
    /* Bridge handles are freed by sober_jni_shutdown(); our own buffers are
     * reachable through the registered record only for standalone handles. */
}

/* ---- payload accessors (JNI-shaped) ------------------------------------ */
const char *sober_jstr_get_utf8(void *env, void *str, uint8_t *is_copy)
{
    (void)env;
    if (is_copy) *is_copy = 0;
    JstrHandle *x = valid_handle(str);
    if (!x || !safe_ptr(x->payload)) return "";
    return (const char *)x->payload;
}

const uint16_t *sober_jstr_get_chars(void *env, void *str, uint8_t *is_copy)
{
    (void)env;
    if (is_copy) *is_copy = 0;
    static const uint16_t empty[1] = { 0 };
    JstrHandle *x = valid_handle(str);
    if (!x || !safe_ptr(x->guest_c)) return empty;
    return (const uint16_t *)x->guest_c;
}

void sober_jstr_release_utf8(void *env, void *str, const char *chars)
{ (void)env; (void)str; (void)chars; }

void sober_jstr_release_chars(void *env, void *str, const uint16_t *chars)
{ (void)env; (void)str; (void)chars; }

const char *sober_jstr_bytes(void *str)
{
    JstrHandle *x = valid_handle(str);
    if (!x || !safe_ptr(x->payload)) return "";
    return (const char *)x->payload;
}

size_t sober_jstr_length(void *str)
{
    JstrHandle *x = valid_handle(str);
    if (!x || !safe_ptr(x->payload)) return 0;
    return strlen((const char *)x->payload);
}

void *sober_jstr_to_string(void *str)
{
    JstrHandle *x = valid_handle(str);
    if (!x) return NULL;
    x->refs++;              /* java.lang.String.toString() returns this */
    return x;
}

/* ---- opt-in JNIEnv installation ---------------------------------------- */
int sober_jstr_install(void **env_fn_array, void *guest_base)
{
    if (!env_fn_array) return SOBER_JSTR_ERR;
    if (!sober_jstr_enabled()) return SOBER_JSTR_DISABLED;
    g_guest_base = guest_base;
    /* Index = JNIEnv slot; byte offset = (4 reserved + slot) * 8.
     * 165 -> 0x548 GetStringUTFChars; 166 -> 0x550 ReleaseStringUTFChars;
     * 161 -> 0x528 GetStringChars; 162 -> 0x530 ReleaseStringChars. */
    env_fn_array[161] = (void *)(uintptr_t)&sober_jstr_get_chars;
    env_fn_array[162] = (void *)(uintptr_t)&sober_jstr_release_chars;
    env_fn_array[165] = (void *)(uintptr_t)&sober_jstr_get_utf8;
    env_fn_array[166] = (void *)(uintptr_t)&sober_jstr_release_utf8;
    fprintf(stderr,
            "[jstr] opt-in canonical strings (SOBER_CANONICAL_JSTRINGS=1): "
            "guest_base=%p vtable=0x%x; installed GetStringUTFChars@+0x548, "
            "Release@+0x550, GetStringChars@+0x528, Release@+0x530; "
            "no guest byte patched, no host fn ptr in any object\n",
            guest_base, SOBER_JSTR_RVA_STRING_VTABLE);
    return SOBER_JSTR_OK;
}

/* ---- audit / diagnostics ------------------------------------------------ */
static uintptr_t fn_bits(const void *fnptr)
{
    uintptr_t u = 0;
    memcpy(&u, &fnptr, sizeof(u));
    return u;
}

static int scan_words(const char *tag, const unsigned char *p, size_t bytes,
                      char *buf, size_t cap, size_t *used)
{
    int bad = 0;
    uintptr_t own[] = {
        fn_bits((const void *)(uintptr_t)&sober_jstr_release_utf8),
        fn_bits((const void *)(uintptr_t)&sober_jstr_release_chars),
    };
    for (size_t off = 0; off + 8 <= bytes; off += 8) {
        uint64_t v;
        memcpy(&v, p + off, sizeof(v));
        if (v == (uint64_t)SOBER_JSTR_SENTINEL) {
            bad++;
            if (buf && cap && *used < cap) {
                int n = snprintf(buf + *used, cap - *used,
                                 "%s+%#zx=SENTINEL ", tag, off);
                *used += (n > 0 && (size_t)n < cap - *used) ? (size_t)n
                                                            : cap - *used;
            }
        }
        for (size_t k = 0; k < sizeof(own) / sizeof(own[0]); ++k) {
            if (v == (uint64_t)own[k]) {
                bad++;
                if (buf && cap && *used < cap) {
                    int n = snprintf(buf + *used, cap - *used,
                                     "%s+%#zx=HOST_FN ", tag, off);
                    *used += (n > 0 && (size_t)n < cap - *used) ? (size_t)n
                                                                : cap - *used;
                }
            }
        }
    }
    return bad;
}

int sober_jstr_audit(void *str, char *buf, size_t cap)
{
    if (buf && cap) buf[0] = 0;
    JstrHandle *x = valid_handle(str);
    if (!x) {
        if (buf && cap) snprintf(buf, cap, "invalid handle %p", str);
        return 1;
    }
    size_t used = 0;
    int bad = scan_words("handle", (const unsigned char *)x, sizeof(*x),
                         buf, cap, &used);
    JstrRec *r = find_rec(str);
    if (safe_ptr(x->backing)) {
        if (r) {
            /* Standalone: backing has a 0x20-byte negative header. */
            bad += scan_words("A", (const unsigned char *)x->backing - JSTR_A_PREFIX,
                              JSTR_A_PREFIX + 0x40, buf, cap, &used);
        } else {
            /* Bridge handle: backing is sr_new_object's bare calloc block;
             * reading backing-0x20 would be out of bounds. */
            bad += scan_words("A", (const unsigned char *)x->backing,
                              0x40, buf, cap, &used);
        }
    }
    /* Data object: standalone has its base in the registry; for a bridge
     * handle guest_sub holds `data` = base + JSTR_D_PREFIX, so subtract the
     * prefix to recover the allocation base.  0x60 bytes covers the negative
     * header, body header and at least 0x20 of inline buffer. */
    void *dbase = r ? r->dbase
                    : (safe_ptr(x->guest_sub)
                           ? (void *)((unsigned char *)x->guest_sub - JSTR_D_PREFIX)
                           : NULL);
    if (safe_ptr(dbase))
        bad += scan_words("data", (const unsigned char *)dbase,
                          JSTR_D_PREFIX + JSTR_D_BODY + 0x20, buf, cap, &used);
    if (buf && cap && used < cap)
        snprintf(buf + used, cap - used, "sentinel/host_fn words=%d", bad);
    return bad;
}

const char *sober_jstr_describe(char *buf, size_t cap)
{
    if (!buf || !cap) return "";
    snprintf(buf, cap,
             "enabled=%d created=%lu base=%p vtable=0x%x cap=0x%x "
             "env=SOBER_CANONICAL_JSTRINGS=1",
             sober_jstr_enabled(), g_created, g_guest_base,
             SOBER_JSTR_RVA_STRING_VTABLE, SOBER_JSTR_STRING_CAPACITY);
    return buf;
}
