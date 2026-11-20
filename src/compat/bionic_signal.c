/*
 * bionic_signal.c - ABI-correct Bionic signal shims.
 *
 * The guest is an Android LP64 ELF: its `struct sigaction` is 32 bytes
 * (int sa_flags; union handler; unsigned long sa_mask; sa_restorer) and its
 * `sigset_t` is one unsigned long.  Host glibc's struct sigaction is 152
 * bytes and sigset_t is 128 bytes.  Forwarding sigaction() directly let glibc
 * write 152 bytes into the guest's 32-byte stack slot, corrupting adjacent
 * locals (including F_inner's object slot and stack canary) and producing the
 * long-standing bogus +0x68 / stack-smashing failures.  These wrappers copy
 * exactly the Bionic-sized fields and translate the mask in both directions.
 *
 * Native Linux code only: no Android runtime and no translation layer.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <stddef.h>
#include <string.h>

/* Host targets resolved by bionic_libc.c's constructor. */
extern void *sober_libc_fn_sigaction;
extern void *sober_libc_fn_sigemptyset;
extern void *sober_libc_fn_sigfillset;
extern void *sober_libc_fn_pthread_sigmask;

/* Bionic LP64 layout (NDK bits/signal_types.h). */
typedef struct {
    int      sa_flags;
    int      __pad;
    union {
        void (*handler)(int);
        void (*action)(int, void *, void *);
    } u;
    unsigned long sa_mask;
    void (*sa_restorer)(void);
} sober_bionic_sigaction_t;

_Static_assert(sizeof(sober_bionic_sigaction_t) == 32,
               "Bionic struct sigaction must be 32 bytes");

#ifndef SA_RESTORER
#define SA_RESTORER 0x04000000
#endif

static void bionic_mask_to_host(unsigned long bm, sigset_t *host)
{
    memset(host, 0, sizeof(*host));
    unsigned char *p = (unsigned char *)host;
    for (int sig = 1; sig <= 64; ++sig) {
        if (bm & (1UL << (sig - 1)))
            p[(sig - 1) / 8] |= (unsigned char)(1u << ((sig - 1) % 8));
    }
}

static unsigned long host_mask_to_bionic(const sigset_t *host)
{
    const unsigned char *p = (const unsigned char *)host;
    unsigned long bm = 0;
    for (int sig = 1; sig <= 64; ++sig) {
        if (p[(sig - 1) / 8] & (1u << ((sig - 1) % 8)))
            bm |= (1UL << (sig - 1));
    }
    return bm;
}

__attribute__((visibility("default")))
int sober_bionic_sigaction(int signum, const sober_bionic_sigaction_t *act,
                           sober_bionic_sigaction_t *oldact) __asm__("sigaction");

__attribute__((visibility("default")))
int sober_bionic_sigaction(int signum, const sober_bionic_sigaction_t *act,
                           sober_bionic_sigaction_t *oldact)
{
    if (!sober_libc_fn_sigaction) {
        errno = ENOSYS;
        return -1;
    }
    typedef int (*host_sigaction_fn)(int, const struct sigaction *,
                                     struct sigaction *);
    host_sigaction_fn host = (host_sigaction_fn)sober_libc_fn_sigaction;

    struct sigaction hact, hold;
    struct sigaction *pact = NULL;
    struct sigaction *pold = oldact ? &hold : NULL;

    if (act) {
        memset(&hact, 0, sizeof(hact));
        hact.sa_flags = act->sa_flags;
        hact.sa_handler = act->u.handler;
        hact.sa_restorer = act->sa_restorer;
        if (!hact.sa_restorer) hact.sa_flags &= ~SA_RESTORER;
        bionic_mask_to_host(act->sa_mask, &hact.sa_mask);
        pact = &hact;
    }

    int rc = host(signum, pact, pold);
    if (rc != 0) return rc;

    if (oldact) {
        memset(oldact, 0, sizeof(*oldact));
        oldact->sa_flags = hold.sa_flags;
        oldact->u.handler = hold.sa_handler;
        oldact->sa_mask = host_mask_to_bionic(&hold.sa_mask);
        oldact->sa_restorer = hold.sa_restorer;
    }
    return 0;
}

__attribute__((visibility("default")))
int sober_bionic_sigemptyset(unsigned long *set) __asm__("sigemptyset");

__attribute__((visibility("default")))
int sober_bionic_sigemptyset(unsigned long *set)
{
    (void)sober_libc_fn_sigemptyset;   /* keep the resolved host symbol live */
    if (!set) {
        errno = EINVAL;
        return -1;
    }
    *set = 0UL;                        /* Bionic sigset_t is 8 bytes */
    return 0;
}

__attribute__((visibility("default")))
int sober_bionic_sigfillset(unsigned long *set) __asm__("sigfillset");

__attribute__((visibility("default")))
int sober_bionic_sigfillset(unsigned long *set)
{
    (void)sober_libc_fn_sigfillset;
    if (!set) {
        errno = EINVAL;
        return -1;
    }
    *set = ~0UL;
    return 0;
}

__attribute__((visibility("default")))
int sober_bionic_pthread_sigmask(int how, const unsigned long *set,
                                 unsigned long *oldset)
    __asm__("pthread_sigmask");

__attribute__((visibility("default")))
int sober_bionic_pthread_sigmask(int how, const unsigned long *set,
                                 unsigned long *oldset)
{
    if (!sober_libc_fn_pthread_sigmask) {
        errno = ENOSYS;
        return -1;
    }
    typedef int (*host_pthread_sigmask_fn)(int, const sigset_t *, sigset_t *);
    host_pthread_sigmask_fn host =
        (host_pthread_sigmask_fn)sober_libc_fn_pthread_sigmask;

    sigset_t hset, hold;
    const sigset_t *pset = NULL;
    sigset_t *pold = oldset ? &hold : NULL;

    if (set) {
        bionic_mask_to_host(*set, &hset);
        pset = &hset;
    }

    int rc = host(how, pset, pold);
    if (rc != 0) return rc;

    if (oldset) *oldset = host_mask_to_bionic(&hold);
    return 0;
}
