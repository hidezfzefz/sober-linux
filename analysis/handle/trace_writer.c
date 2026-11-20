/*
 * trace_writer.c - ptrace hardware-watchpoint tracer for the phase-4 handle bug.
 * OWNER: worker-1 (task #387), analysis/handle/ only.
 *
 * Flow:
 *  1. spawn build/guest-init-host with app-shell env under PTRACE_TRACEME;
 *  2. break at host guest_app_shell_run (PIE offset 0xa4e0) -> guest base in RDI;
 *  3. break at guest RVA 0x23331ae (rbx = *(rdi) = handle object);
 *  4. arm an x86-64 hardware WRITE watchpoint on [rbx+0x68];
 *  5. report the writer RIP/RVA and an RBP-chain backtrace.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <stddef.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/user.h>

#define HOST_BP_OFFSET 0xa4e0ULL   /* guest_app_shell_run */
#define GUEST_BP_RVA   0x23331aeULL

static const char *HOST_PATH = "/home/user/sober-linux/build/guest-init-host";

struct maps_range { unsigned long long lo, hi; };

static int find_mapping(pid_t pid, const char *needle, struct maps_range *out)
{
    char path[128], line[2048];
    snprintf(path, sizeof(path), "/proc/%d/maps", pid);
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    int found = 0;
    while (fgets(line, sizeof(line), f)) {
        unsigned long long lo, hi;
        if (sscanf(line, "%llx-%llx", &lo, &hi) != 2) continue;
        if (needle && strstr(line, needle)) {
            if (!found || lo < out->lo) { out->lo = lo; out->hi = hi; }
            found = 1;
        }
    }
    fclose(f);
    return found;
}

static unsigned long long find_guest_base(pid_t pid)
{
    struct maps_range r;
    if (!find_mapping(pid, "sober-guest", &r)) return 0;
    fprintf(stderr, "[trace] guest mapping %#llx-%#llx size=%#llx\n",
            r.lo, r.hi, r.hi - r.lo);
    return r.lo;
}

static long peek(pid_t pid, unsigned long long addr)
{
    errno = 0;
    long v = ptrace(PTRACE_PEEKDATA, pid, (void *)addr, 0);
    if (errno) fprintf(stderr, "[trace] peek(%#llx): %s\n", addr, strerror(errno));
    return v;
}
static void poke(pid_t pid, unsigned long long addr, long val)
{
    if (ptrace(PTRACE_POKEDATA, pid, (void *)addr, (void *)val) < 0)
        fprintf(stderr, "[trace] poke(%#llx): %s\n", addr, strerror(errno));
}

int main(void)
{
    pid_t pid = fork();
    if (pid == 0) {
        setenv("LD_LIBRARY_PATH", "/home/user/sober-linux/build/ndk-stubs", 1);
        setenv("EGL_PLATFORM", "x11", 1);
        setenv("LIBGL_ALWAYS_SOFTWARE", "1", 1);
        setenv("SOBER_RENDER", "1", 1);
        setenv("SOBER_APP_SHELL", "1", 1);
        setenv("SOBER_WINDOW_HOLD_MS", "1000", 1);
        ptrace(PTRACE_TRACEME, 0, 0, 0);
        execl(HOST_PATH, "guest-init-host",
              "/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so", (char *)NULL);
        perror("execl"); _exit(127);
    }
    int st;
    if (waitpid(pid, &st, 0) < 0) { perror("waitpid"); return 1; }
    if (!WIFSTOPPED(st)) { fprintf(stderr, "[trace] no exec stop\n"); return 1; }
    ptrace(PTRACE_SETOPTIONS, pid, 0,
           (void *)(PTRACE_O_TRACECLONE | PTRACE_O_TRACEFORK | PTRACE_O_TRACEVFORK));

    struct maps_range hm;
    if (!find_mapping(pid, "guest-init-host", &hm)) {
        fprintf(stderr, "[trace] host mapping not found\n"); return 2;
    }
    unsigned long long hostbp = hm.lo + HOST_BP_OFFSET;
    long hostorig = peek(pid, hostbp);
    unsigned char hostbyte = (unsigned char)(hostorig & 0xff);
    fprintf(stderr, "[trace] host base=%#llx bp=%#llx byte=%02x\n", hm.lo, hostbp, hostbyte);
    poke(pid, hostbp, (hostorig & ~0xffL) | 0xcc);

    unsigned long long base = 0, gbp = 0, watch = 0;
    long gorig = 0;
    int stage = 0; /* 0 -> host bp, 1 -> guest bp, 2 -> watch */
    for (int i = 0; i < 1000000; ++i) {
        ptrace(PTRACE_CONT, pid, 0, 0);
        if (waitpid(-1, &st, 0) < 0) break;
        if (WIFEXITED(st) || WIFSIGNALED(st)) {
            fprintf(stderr, "[trace] child ended exit=%d sig=%d\n",
                    WIFEXITED(st)?WEXITSTATUS(st):-1, WIFSIGNALED(st)?WTERMSIG(st):0);
            break;
        }
        unsigned event = (unsigned)st >> 16;
        if (event == PTRACE_EVENT_CLONE || event == PTRACE_EVENT_FORK ||
            event == PTRACE_EVENT_VFORK) {
            unsigned long long newtid = 0;
            ptrace(PTRACE_GETEVENTMSG, pid, 0, &newtid);
            fprintf(stderr, "[trace] clone event newtid=%llu\n", newtid);
            if (newtid) ptrace(PTRACE_CONT, (pid_t)newtid, 0, 0);
            continue;
        }
        int sig = WSTOPSIG(st);
        struct user_regs_struct r;
        ptrace(PTRACE_GETREGS, pid, 0, &r);
        fprintf(stderr, "[trace] stop sig=%d rip=%#llx stage=%d\n",
                sig, (unsigned long long)r.rip, stage);

        if (stage == 0 && sig == SIGTRAP && r.rip == hostbp + 1) {
            poke(pid, hostbp, hostorig);
            r.rip = hostbp; ptrace(PTRACE_SETREGS, pid, 0, &r);
            base = (unsigned long long)r.rdi;
            fprintf(stderr, "[trace] app-shell entry: guest_base=%#llx\n", base);
            if (!base) { base = find_guest_base(pid); }
            if (!base) { fprintf(stderr, "[trace] no guest base\n"); break; }
            gbp = base + GUEST_BP_RVA;
            gorig = peek(pid, gbp);
            fprintf(stderr, "[trace] guest bp@%#llx byte=%02x\n", gbp,
                    (unsigned char)(gorig & 0xff));
            poke(pid, gbp, (gorig & ~0xffL) | 0xcc);
            stage = 1;
            continue;
        }
        if (stage == 1 && sig == SIGTRAP && r.rip == gbp + 1) {
            poke(pid, gbp, gorig);
            r.rip = gbp; ptrace(PTRACE_SETREGS, pid, 0, &r);
            unsigned long long obj = r.rbx;
            fprintf(stderr, "[trace] consumer hit: slot=%#llx obj=%#llx\n",
                    (unsigned long long)r.rdi, obj);
            if (obj < 0x10000) { fprintf(stderr, "[trace] bad obj\n"); break; }
            fprintf(stderr, "[trace] obj[0]=%#lx obj[0x68]=%#lx (BEFORE)\n",
                    peek(pid, obj), peek(pid, obj + 0x68));
            watch = obj + 0x68;
            ptrace(PTRACE_POKEUSER, pid, (void *)offsetof(struct user, u_debugreg[0]),
                   (void *)watch);
            ptrace(PTRACE_POKEUSER, pid, (void *)offsetof(struct user, u_debugreg[7]),
                   (void *)0xd0001UL);
            fprintf(stderr, "[trace] WATCH armed on %#llx\n", watch);
            stage = 2;
            continue;
        }
        if (stage == 2 && sig == SIGTRAP) {
            long dr6 = ptrace(PTRACE_PEEKUSER, pid,
                              (void *)offsetof(struct user, u_debugreg[6]), 0);
            fprintf(stderr, "[trace] WATCH HIT rip=%#llx dr6=%#lx ",
                    (unsigned long long)r.rip, dr6);
            if (r.rip >= base && r.rip < base + (200ULL<<20))
                fprintf(stderr, "guest RVA=%#llx", r.rip - base);
            else fprintf(stderr, "host/non-guest");
            fprintf(stderr, "\n[trace] obj[0x68] AFTER=%#lx\n", peek(pid, watch));
            unsigned long long fp = r.rbp;
            for (int k = 0; k < 14 && fp > 0x10000; ++k) {
                long ret = peek(pid, fp + 8);
                unsigned long long nx = (unsigned long long)peek(pid, fp);
                if (!ret) break;
                fprintf(stderr, "[trace]  frame[%d] rbp=%#llx ret=%#lx", k, fp, ret);
                if ((unsigned long long)ret >= base &&
                    (unsigned long long)ret < base + (200ULL<<20))
                    fprintf(stderr, " guest RVA=%#llx", (unsigned long long)ret - base);
                fprintf(stderr, "\n");
                if (nx <= fp) break;
                fp = nx;
            }
            ptrace(PTRACE_POKEUSER, pid, (void *)offsetof(struct user, u_debugreg[7]), 0);
            break;
        }
        /* Deliver non-SIGTRAP signals (the harness SIGSEGV handler must run);
         * suppress unmatched SIGTRAPs so our int3 never reaches its handler. */
        ptrace(PTRACE_CONT, pid, 0, (void *)(long)(sig == SIGTRAP ? 0 : sig));
    }
    ptrace(PTRACE_DETACH, pid, 0, 0);
    return 0;
}
