/* worker-1 / task #446: non-perturbing diagnostic interposer.
 * Intercepts __stack_chk_fail to print the exact guest return address / frame
 * chain BEFORE libc tears the stack down.
 * Compile: gcc -shared -fPIC -O0 -o stack_chk_interpose.so stack_chk_interpose.c -ldl
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

typedef void (*fail_fn)(void);
static void wp(const char *s) { write(2, s, strlen(s)); }

static void show(uintptr_t pc, const char *tag)
{
    Dl_info di;
    char buf[256];
    if (pc && dladdr((void *)pc, &di) && di.dli_fname) {
        int n = snprintf(buf, sizeof buf,
            "[abort-diag] %s pc=%p %s+0x%lx\n", tag, (void *)pc,
            di.dli_fname, (unsigned long)(pc - (uintptr_t)di.dli_fbase));
        if (n > 0) write(2, buf, (size_t)n);
    } else {
        int n = snprintf(buf, sizeof buf, "[abort-diag] %s pc=%p (unmapped)\n", tag, (void *)pc);
        if (n > 0) write(2, buf, (size_t)n);
    }
}

void __stack_chk_fail(void)
{
    static fail_fn real;
    wp("[abort-diag] __stack_chk_fail INTERCEPTED\n");

    /* Return address into the guest function whose canary check failed. */
    uintptr_t ra = (uintptr_t)__builtin_return_address(0);
    show(ra, "CALLER-RA");

    register uintptr_t r14v asm("r14");
    register uintptr_t r12v asm("r12");
    register uintptr_t rbxv asm("rbx");
    uintptr_t finner_rbp = (uintptr_t)__builtin_frame_address(1);
    char rb[320];
    int rn = snprintf(rb, sizeof rb,
        "[abort-diag] at-fail r14=%p r12=%p rbx=%p rbp0=%p finner_rbp=%p saved_canary=%#lx live_guard=%#lx\n",
        (void *)r14v, (void *)r12v, (void *)rbxv,
        __builtin_frame_address(0), (void *)finner_rbp,
        finner_rbp ? (unsigned long)*(uintptr_t *)(finner_rbp - 0x18) : 0ul,
        r14v ? (unsigned long)*(uintptr_t *)r14v : 0ul);
    if (rn > 0) write(2, rb, (size_t)rn);

    /* Resolve guest base from the caller PC and dump the canary source. */
    Dl_info di;
    if (dladdr((void *)ra, &di) && di.dli_fname) {
        uintptr_t base = (uintptr_t)di.dli_fbase;
        uintptr_t slot = base + 0x72293f8u;
        uintptr_t sing = *(uintptr_t *)slot;
        char buf[320];
        int n = snprintf(buf, sizeof buf,
            "[abort-diag] guestbase=%p singleton@0x72293f8=%p (rva=%#lx)\n",
            (void *)base, (void *)sing,
            sing >= base ? (unsigned long)(sing - base) : 0ul);
        if (n > 0) write(2, buf, (size_t)n);
        if (sing >= 0x1000 && sing < 0x0000800000000000ULL) {
            n = snprintf(buf, sizeof buf,
                "[abort-diag]   *(singleton)=%#lx  *(singleton+0x68)=%#lx\n",
                (unsigned long)*(uintptr_t *)sing,
                (unsigned long)*(uintptr_t *)(sing + 0x68));
            if (n > 0) write(2, buf, (size_t)n);
        }
    }

    /* Frame-pointer chain starting from the current frame. */
    uintptr_t fp = (uintptr_t)__builtin_frame_address(0);
    for (int i = 0; i < 20 && fp; ++i) {
        uintptr_t nxt = *(uintptr_t *)fp;
        uintptr_t fr = *(uintptr_t *)(fp + 8);
        if (fr) show(fr, "FP-CHAIN");
        if (nxt <= fp) break;
        fp = nxt;
    }
    wp("[abort-diag] === end ===\n");

    if (!real) real = (fail_fn)dlsym(RTLD_NEXT, "__stack_chk_fail");
    if (real) real();
    _exit(134);
}
