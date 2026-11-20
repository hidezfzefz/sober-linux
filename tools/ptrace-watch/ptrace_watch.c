/*
 * ptrace_watch.c - dynamic watchpoint on the object actually consumed by the
 * guest app-shell handle path (worker-1, task #399).
 *
 * OWNER: worker-1. Owned paths: analysis/trace/, tools/ptrace-watch/.
 *
 * The child runs the harness WITHOUT PTRACE_TRACEME so the 3618 ctors and
 * packed-RELA init execute natively. The harness's SOBER_GDB_PAUSE_MS hook
 * pauses just before the app-shell RVAs; we then PTRACE_ATTACH as the parent
 * (Yama-legal), set a software breakpoint at guest RVA 0x23331ae, read RBX
 * (the real consumed object) on hit, arm a hardware WRITE watchpoint on
 * [rbx+0x68] on ALL threads, and report the writer RIP + backtrace.
 */
#define _GNU_SOURCE 1
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/user.h>
#include <stddef.h>

#define BP_RVA    0x23331aeULL   /* consumer: rbx = *(rdi) */
#define WATCH_OFF 0x68ULL
#define DR7_ARM   0x90001UL      /* L0 | R/W0=write | LEN0=8 */

static pid_t g_pid;
static unsigned long long g_base;
static unsigned long long g_watch;
static int g_fd = -1;
static pid_t g_tids[128];
static int g_ntids;

static void out(const char *s) { if (g_fd>=0) { ssize_t n=write(g_fd,s,strlen(s)); (void)n; } }
static void outf(const char *fmt, ...)
{
    char b[512]; va_list ap; va_start(ap, fmt);
    int n = vsnprintf(b, sizeof(b), fmt, ap); va_end(ap);
    if (n > 0) out(b);
}

static unsigned long long guest_base(pid_t pid)
{
    char path[128], line[2048];
    snprintf(path, sizeof(path), "/proc/%d/maps", pid);
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    unsigned long long b = 0;
    while (fgets(line, sizeof(line), f)) {
        unsigned long long lo, hi;
        if (sscanf(line, "%llx-%llx", &lo, &hi) == 2 && strstr(line, "sober-guest")) {
            b = lo; break;
        }
    }
    fclose(f);
    return b;
}

static long peek(pid_t pid, unsigned long long a)
{
    errno = 0;
    long v = ptrace(PTRACE_PEEKDATA, pid, (void *)a, 0);
    if (errno) outf("[pw] peek(%#llx): %s\n", a, strerror(errno));
    return v;
}
static void poke(pid_t pid, unsigned long long a, long v)
{
    if (ptrace(PTRACE_POKEDATA, pid, (void *)a, (void *)v) < 0)
        outf("[pw] poke(%#llx): %s\n", a, strerror(errno));
}
static void poke_user(pid_t pid, unsigned long off, unsigned long long val)
{
    if (ptrace(PTRACE_POKEUSER, pid, (void *)off, (void *)val) < 0)
        outf("[pw] poke_user(%#lx): %s\n", off, strerror(errno));
}
static unsigned long long peek_user(pid_t pid, unsigned long off)
{
    errno = 0;
    long v = ptrace(PTRACE_PEEKUSER, pid, (void *)off, 0);
    if (errno) return 0;
    return (unsigned long long)v;
}

static void attach_all_threads(pid_t pid)
{
    char dpath[128];
    snprintf(dpath, sizeof(dpath), "/proc/%d/task", pid);
    DIR *d = opendir(dpath);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        pid_t tid = (pid_t)atoi(e->d_name);
        if (tid == pid) continue;
        if (ptrace(PTRACE_ATTACH, tid, 0, 0) == 0) {
            int st; waitpid(tid, &st, 0);
            if (g_ntids < 128) g_tids[g_ntids++] = tid;
            outf("[pw] attached tid=%d\n", tid);
        }
    }
    closedir(d);
}

static void arm_watch_on_thread(pid_t tid)
{
    ptrace(PTRACE_ATTACH, tid, 0, 0);
    int st; waitpid(tid, &st, 0);
    poke_user(tid, offsetof(struct user, u_debugreg[0]), g_watch);
    poke_user(tid, offsetof(struct user, u_debugreg[6]), 0);
    poke_user(tid, offsetof(struct user, u_debugreg[7]), DR7_ARM);
    outf("[pw] watch armed on tid=%d addr=%#llx\n", tid, g_watch);
}

static void arm_watch_all_threads(pid_t pid)
{
    char dpath[128];
    snprintf(dpath, sizeof(dpath), "/proc/%d/task", pid);
    DIR *d = opendir(dpath);
    if (!d) { arm_watch_on_thread(pid); return; }
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        pid_t tid = (pid_t)atoi(e->d_name);
        if (tid == pid) continue; /* main thread already attached */
        arm_watch_on_thread(tid);
    }
    closedir(d);
    /* main thread */
    poke_user(pid, offsetof(struct user, u_debugreg[0]), g_watch);
    poke_user(pid, offsetof(struct user, u_debugreg[6]), 0);
    poke_user(pid, offsetof(struct user, u_debugreg[7]), DR7_ARM);
    outf("[pw] watch armed on main tid=%d\n", pid);
}

static void backtrace(pid_t pid, struct user_regs_struct *r)
{
    unsigned long long fp = r->rbp;
    for (int i = 0; i < 16 && fp > 0x10000; ++i) {
        long ret = peek(pid, fp + 8);
        unsigned long long nx = (unsigned long long)peek(pid, fp);
        if (!ret) break;
        outf("[pw]  frame[%02d] rbp=%#llx ret=%#lx", i, fp, ret);
        if ((unsigned long long)ret >= g_base &&
            (unsigned long long)ret < g_base + (200ULL<<20))
            outf(" guest_rva=%#lx", (unsigned long)ret - g_base);
        out("\n");
        if (nx <= fp) break;
        fp = nx;
    }
}

int main(void)
{
    const char *log = getenv("SOBER_PW_LOG");
    if (!log) log = "/tmp/sober-ptrace-watch.log";
    g_fd = open(log, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    out("=== PTRACE WATCH START ===\n");

    pid_t pid = fork();
    if (pid == 0) {
        setenv("LD_LIBRARY_PATH", "/home/user/sober-linux/build/ndk-stubs", 1);
        setenv("EGL_PLATFORM", "x11", 1);
        setenv("LIBGL_ALWAYS_SOFTWARE", "1", 1);
        setenv("SOBER_RENDER", "1", 1);
        setenv("SOBER_APP_SHELL", "1", 1);
        setenv("SOBER_GDB_PAUSE_MS", "12000", 1);
        setenv("SOBER_TRACE_NO_SIGTRAP_HANDLER", "1", 1);
        setenv("SOBER_WINDOW_HOLD_MS", "100", 1);
        execl("/home/user/sober-linux/build/guest-init-host", "guest-init-host",
              "/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so", (char *)NULL);
        perror("execl"); _exit(127);
    }
    g_pid = pid;
    /* let init + ctors + render run natively during the pause */
    usleep(7500000);
    if (ptrace(PTRACE_ATTACH, pid, 0, 0) < 0) {
        outf("[pw] ATTACH failed: %s\n", strerror(errno));
        kill(pid, SIGKILL); return 1;
    }
    int st;
    waitpid(pid, &st, 0);
    ptrace(PTRACE_SETOPTIONS, pid, 0,
           (void *)(PTRACE_O_TRACECLONE | PTRACE_O_TRACEFORK | PTRACE_O_TRACEVFORK));
    g_tids[g_ntids++] = pid;  /* main thread */
    attach_all_threads(pid);
    g_base = guest_base(pid);
    outf("[pw] attached pid=%d guest_base=%#llx\n", pid, g_base);
    if (!g_base) { kill(pid, SIGKILL); return 2; }

    /* Neutralise the harness crash_handler so it cannot steal our SIGTRAP:
     * overwrite its first byte with 0xc3 (ret).  Guest code is unaffected. */
    {
        unsigned long long hh = 0, hb = 0;
        char mm[128], ln[2048];
        FILE *f = fopen("/proc/self/maps", "r");
        (void)f;
        /* harness base from the main mapping of its own executable */
        snprintf(mm, sizeof(mm), "/proc/%d/maps", pid);
        f = fopen(mm, "r");
        while (f && fgets(ln, sizeof(ln), f)) {
            unsigned long long lo, hi;
            if (sscanf(ln, "%llx-%llx", &lo, &hi)==2 && strstr(ln, "guest-init-host")) {
                hb = lo; break;
            }
        }
        if (f) fclose(f);
        hh = hb + 0x95a0ULL;
        outf("[pw] harness base=%#llx handler@%#llx\n", hb, hh);
        long hw = peek(pid, hh);
        poke(pid, hh, (hw & ~0xffL) | 0xc3);
    }
    unsigned long long bp = g_base + BP_RVA;
    long orig = peek(pid, bp);
    outf("[pw] int3 bp@%#llx orig byte=%02x\n", bp, (unsigned char)orig);
    poke(pid, bp, (orig & ~0xffL) | 0xcc);

    int stage = 0; /* 0 -> wait for bp; 1 -> watch armed */
    /* resume all threads once after attach/bp setup */
    for (int t = 0; t < g_ntids; ++t)
        ptrace(PTRACE_CONT, g_tids[t], 0, 0);
    for (int i = 0; i < 2000000; ++i) {
        pid_t stopped = waitpid(-1, &st, 0);
        if (stopped < 0) break;
        pid = stopped;
        if (WIFEXITED(st) || WIFSIGNALED(st)) {
            outf("[pw] child ended exit=%d sig=%d\n",
                 WIFEXITED(st)?WEXITSTATUS(st):-1, WIFSIGNALED(st)?WTERMSIG(st):0);
            break;
        }
        unsigned event = (unsigned)st >> 16;
        if (event == PTRACE_EVENT_CLONE || event == PTRACE_EVENT_FORK ||
            event == PTRACE_EVENT_VFORK) {
            unsigned long long nt = 0;
            ptrace(PTRACE_GETEVENTMSG, pid, 0, &nt);
            outf("[pw] clone event newtid=%llu\n", nt);
            if (nt && g_ntids < 128) {
                g_tids[g_ntids++] = (pid_t)nt;
                ptrace(PTRACE_CONT, (pid_t)nt, 0, 0);
            }
            ptrace(PTRACE_CONT, pid, 0, 0);
            continue;
        }
        int sig = WSTOPSIG(st);
        struct user_regs_struct r;
        { struct user_regs_struct rr; ptrace(PTRACE_GETREGS, pid, 0, &rr);
          outf("[pw] stop tid=%d sig=%d rip=%#llx stage=%d\n",
               pid, sig, (unsigned long long)rr.rip, stage); }
        ptrace(PTRACE_GETREGS, pid, 0, &r);
        siginfo_t si;
        memset(&si, 0, sizeof(si));
        ptrace(PTRACE_GETSIGINFO, pid, 0, &si);

        if (stage == 0 && sig == SIGTRAP && r.rip == bp + 1) {
            poke(pid, bp, orig);
            r.rip = bp;
            ptrace(PTRACE_SETREGS, pid, 0, &r);
            unsigned long long obj = r.rbx;
            outf("[pw] CONSUMER HIT rdi_slot=%#llx obj(rbx)=%#llx magic=%#lx field=%#lx\n",
                 (unsigned long long)r.rdi, obj, peek(pid, obj),
                 peek(pid, obj + WATCH_OFF));
            if (obj < 0x10000) { out("[pw] bad object\n"); break; }
            g_watch = obj + WATCH_OFF;
            arm_watch_all_threads(pid);
            stage = 1;
            continue;
        }
        if (stage == 1 && sig == SIGTRAP &&
            si.si_code == TRAP_HWBKPT &&
            (peek_user(pid, offsetof(struct user, u_debugreg[6])) & 1)) {
            outf("[pw] WATCH WRITE rip=%#llx ", (unsigned long long)r.rip);
            if (r.rip >= g_base && r.rip < g_base + (200ULL<<20))
                outf("guest_rva=%#llx", r.rip - g_base);
            out("\n");
            outf("[pw] dr6=%#llx obj=%#llx field_now=%#lx\n",
                 peek_user(pid, offsetof(struct user, u_debugreg[6])),
                 g_watch - WATCH_OFF, peek(pid, g_watch));
            /* bytes ending at RIP (decoded writer instruction) */
            for (int k = -3; k <= 1; ++k)
                outf("[pw] bytes[%+d]=%#lx\n", k, peek(pid, r.rip + 8*k));
            backtrace(pid, &r);
            outf("[pw] regs rax=%#llx rbx=%#llx rcx=%#llx rdx=%#llx rdi=%#llx rsi=%#llx\n",
                 (unsigned long long)r.rax,(unsigned long long)r.rbx,
                 (unsigned long long)r.rcx,(unsigned long long)r.rdx,
                 (unsigned long long)r.rdi,(unsigned long long)r.rsi);
            break; /* decisive hit */
        }
        /* suppress loader's stray int3 SIGTRAPs; deliver real signals */
        ptrace(PTRACE_CONT, pid, 0, (void *)(long)((sig == SIGTRAP || sig == SIGSTOP) ? 0 : sig));
    }
    ptrace(PTRACE_DETACH, pid, 0, 0);
    kill(pid, SIGKILL);
    return 0;
}
