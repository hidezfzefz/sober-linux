/*
 * global_string_seed.c - faithful guest-shaped seed of the global std::string
 * handle at guest_base+0x72a0520 (Phase 8, task #461 / follow-up).  Pure C.
 *
 * ---------------------------------------------------------------------------
 * DISASSEMBLY EVIDENCE (frozen engine, BuildID 204ad049599c96a..., SHA
 * 3e498d6494...; engine bytes are NEVER modified)
 * ---------------------------------------------------------------------------
 * Reader / fault site:
 *   2e5d420: movb  $0,(%rdi)          ; out flag = false
 *   2e5d423: mov   %rsi,0x8(%rdi)
 *   2e5d427: mov   (%rsi),%rax        ; rax = *(handle+0)   <-- NULL in the fault
 *   2e5d42a: mov   -0x18(%rax),%rax   ; D   = *(rax-0x18)   <-- si_addr = -0x18
 *   2e5d42e: cmpl  $0,0x20(%rsi,%rax) ; flags = handle[D+0x20]
 *   2e5d436: mov   0x88(%rsi,%rax),%rax ; sub = handle[D+0x88]
 *   2e5d45d: movb  $1,(%rdi)          ; NULL sub -> "disposed" (safe) branch
 *
 * Guest's own static initialiser for the SAME global (0x2e613ed, reached from
 * ELF ctor[0] 0x2e62390 -> 0x2e618b0 -> 0x2e61280):
 *   2e6138b: lea   0x6db3ff0,%r15     ; type descriptor; raw word[0] = 8 (D)
 *   2e61392: lea   0x18(%r15),%rbx    ; 0x6db4008 = descriptor + 0x18
 *   2e613ed: lea   0x72a0520,%r12     ; THE GLOBAL
 *   2e613fd: mov   %rbx,(%r12)        ; handle+0 = descriptor+0x18
 *   2e61401: mov   %r15,0x8(%r12)     ; handle+8 = descriptor
 *   2e61416: movq  $0,0x90(%r12)      ; handle+0x90 = 0
 *   2e61422: movl  $-1,0x98(%r12)     ; handle+0x98 = -1
 *   2e61499: orl   $0x2000,0x8(%r12,%rcx)   ; handle[D+8] flags |= 0x2000
 *
 * Genuine string header vtable 0x6ed8d00 (jstring helper 0x2315532, formatter
 * 0x20ab4b6):
 *   20ab4e6: lea   0x6ed8d00,%rax
 *   20ab4ed: mov   %rax,-0x20(%r15)   ; data+0x00 = header vtable (vptr)
 *   20ab4f1: mov   %r15,-0x18(%r15)   ; data+0x08 = self/data pointer
 *   20ab4de: movq  $0,-0x10(%r15)     ; data+0x10 = size 0
 *   20ab4f5: movq  $0x1f4,-0x8(%r15)  ; data+0x18 = capacity 500
 *
 * ---------------------------------------------------------------------------
 * WHAT WE PUBLISH (no host function pointers, no guest byte patches)
 * ---------------------------------------------------------------------------
 *   global handle (inline at base+0x72a0520):
 *     +0x00  base+0x6db4008     genuine descriptor+0x18 -> reader's rax
 *     +0x08  base+0x6db3ff0     genuine descriptor (word[0]=8 -> D)
 *     +0x10  0x2000             genuine flag bit from the guest initialiser
 *     +0x28  0                  handle[D+0x20] flags (0 -> reader checks sub)
 *     +0x30  guest-malloc'd     handle[D+0x28] data object (string header)
 *     +0x90  NULL               handle[D+0x88] -> reader's "disposed" safe branch
 *     +0x98  -1                 per guest initialiser
 *   data object (guest malloc, 0x40 bytes):
 *     +0x00  base+0x6ed8d00     genuine string header vtable
 *     +0x08  data+0x20          self / inline-buffer pointer
 *     +0x10  0                  size
 *     +0x18  500                capacity (0x1f4)
 *     +0x20  zeroed             inline SSO buffer
 *
 * ---------------------------------------------------------------------------
 * ORDERING (follow-up finding — IMPORTANT FOR WIRING)
 * ---------------------------------------------------------------------------
 * The faulting access is made by a guest WORKER thread spawned by the faithful
 * JavaVM attach path.  It calls nativeRegisterJavaFlagProvider (0x2ed3aa4,
 * rdi = 0x72a0520) -> reader 0x2e5d420.  That thread can issue its first read
 * BEFORE a late seed call returns.  Instrumented A/B (seed_done flag in the
 * fault handler) caught seed_done=0 / global+0=NULL at the fault when the call
 * sits after the JNI seeds, proving the race.
 *
 * Therefore the seed must be in place BEFORE sober_jni_seed_guest_onload_globals
 * / sober_javavm_install.  sober_seed_global_string_early() is provided for
 * exactly that; it has the same effect and is safe to call with just guest_base
 * (before the JNI bridge seeds run).  Calling the regular entry point early
 * works identically.  The module is idempotent: repeated calls are no-ops once
 * the global has a non-NULL backing word (unless SOBER_GSEED_FORCE=1).
 *
 * No callback is emitted; decision-013 marker is untouched; default OFF.
 */
#define _GNU_SOURCE
#include "global_string_seed.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned long g_calls;
static unsigned long g_writes;
static void         *g_last_data;   /* diagnostic only, never dereferenced */

int sober_global_string_seed_enabled(void)
{
    const char *e = getenv("SOBER_SEED_GLOBAL_STRING");
    return (e && strcmp(e, "1") == 0);
}

int sober_seed_global_string_early(void *guest_base)
{
    return sober_seed_global_string(NULL, guest_base);
}

int sober_seed_global_string(struct SoberJni *j, void *guest_base)
{
    (void)j;
    g_calls++;
    if (!guest_base) return SOBER_GSEED_ERR;
    if (!sober_global_string_seed_enabled()) return SOBER_GSEED_DISABLED;

    unsigned char *base = (unsigned char *)guest_base;
    unsigned char *slot = base + SOBER_GSEED_RVA_GLOBAL;

    /* Do not clobber a value the guest installed itself. */
    void *existing = *(void **)slot;
    if (existing && !getenv("SOBER_GSEED_FORCE")) {
        fprintf(stderr,
                "[gseed] global@0x%x already non-NULL (%p); leaving untouched "
                "(set SOBER_GSEED_FORCE=1 to override)\n",
                SOBER_GSEED_RVA_GLOBAL, existing);
        return SOBER_GSEED_OK;
    }

    /* The guest allocator (base+0x1dbab02) is the ONLY allocator used here.
     * Its memory is visible to the guest's own free/destructor paths. */
    void *(*guest_malloc)(size_t) =
        (void *(*)(size_t))(base + SOBER_GSEED_RVA_MALLOC);
    if (!guest_malloc) return SOBER_GSEED_ERR;

    /* ---- 1. guest-shaped std::string data object -------------------------
     * vptr at +0 is the genuine 0x6ed8d00 header vtable; layout mirrors the
     * local string the guest's formatter builds at 0x20ab4d7..0x20ab4f5. */
    unsigned char *data = (unsigned char *)guest_malloc(SOBER_GSEED_STRING_BYTES);
    if (!data) {
        fprintf(stderr, "[gseed] guest malloc(%u) for string failed\n",
                SOBER_GSEED_STRING_BYTES);
        return SOBER_GSEED_ERR;
    }
    memset(data, 0, SOBER_GSEED_STRING_BYTES);
    uintptr_t vt = (uintptr_t)(base + SOBER_GSEED_RVA_STRING_VT);
    void     *self = data + 0x20;      /* inline SSO buffer starts at +0x20 */
    uint64_t  z = 0, cap = SOBER_GSEED_STRING_CAP;
    memcpy(data + 0x00, &vt,   sizeof(vt));    /* vptr (genuine guest RVA) */
    memcpy(data + 0x08, &self, sizeof(self));  /* data pointer (self)       */
    memcpy(data + 0x10, &z,    sizeof(z));     /* size = 0                  */
    memcpy(data + 0x18, &cap,  sizeof(cap));   /* capacity = 500            */

    /* ---- 2. handle inline at the global ---------------------------------
     * +0 = descriptor+0x18 (reader's rax; *(rax-0x18) = D = 8 in the engine),
     * +8 = descriptor.  Both are genuine guest RVAs, never host pointers. */
    void *hp = (void *)(base + SOBER_GSEED_RVA_HANDLE_PTR);
    void *dp = (void *)(base + SOBER_GSEED_RVA_DESCRIPTOR);
    uint32_t flags = 0x2000u;
    void    *nil   = NULL;
    int64_t  minus1 = -1;
    memcpy(slot + 0x00, &hp,     sizeof(hp));
    memcpy(slot + 0x08, &dp,     sizeof(dp));
    memcpy(slot + 0x10, &flags,  sizeof(flags));  /* guest ORs 0x2000 at 0x61499 */
    /* slot+0x28 = handle[D+0x20]: leave 0 so the reader falls through to the
     * +0x88 sub-object check; that word is NULL, selecting the safe branch. */
    memcpy(slot + 0x30, &data,   sizeof(data));   /* handle[D+0x28] data object */
    memcpy(slot + 0x90, &nil,    sizeof(nil));    /* handle[D+0x88] -> safe branch */
    memcpy(slot + 0x98, &minus1, sizeof(minus1)); /* guest initialiser: -1       */

    g_writes++;
    g_last_data = data;
    fprintf(stderr,
            "[gseed] SEEDED global@0x%x (SOBER_SEED_GLOBAL_STRING=1): "
            "handle=%p +0=%p(desc+0x18) +8=%p(desc) +0x30=%p(vptr=%#lx) "
            "+0x90=NULL; guest malloc@0x%x used, no host fn ptr, "
            "no code patch, no callback\n",
            SOBER_GSEED_RVA_GLOBAL, (void *)slot, hp, dp, (void *)data,
            (unsigned long)vt, SOBER_GSEED_RVA_MALLOC);
    return SOBER_GSEED_OK;
}

const char *sober_global_string_seed_describe(char *buf, size_t cap)
{
    if (!buf || !cap) return "";
    snprintf(buf, cap,
             "enabled=%d calls=%lu writes=%lu data=%p "
             "global=0x%x desc=0x%x desc+18=0x%x strvt=0x%x malloc=0x%x "
             "env=SOBER_SEED_GLOBAL_STRING=1",
             sober_global_string_seed_enabled(), g_calls, g_writes,
             g_last_data, SOBER_GSEED_RVA_GLOBAL, SOBER_GSEED_RVA_DESCRIPTOR,
             SOBER_GSEED_RVA_HANDLE_PTR, SOBER_GSEED_RVA_STRING_VT,
             SOBER_GSEED_RVA_MALLOC);
    return buf;
}

void sober_global_string_seed_reset(void)
{
    g_calls = g_writes = 0;
    g_last_data = NULL;
}

unsigned long sober_global_string_seed_writes(void) { return g_writes; }
int sober_global_string_seed_done(void) { return g_writes > 0; }
