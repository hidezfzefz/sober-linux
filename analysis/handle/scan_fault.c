/* Capture guest/host memory at the exact SIGSEGV stop (worker-1, #387). */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/user.h>

#define GARBAGE 0xffffff3085894cffULL

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
        execl("/home/user/sober-linux/build/guest-init-host", "guest-init-host",
              "/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so", (char *)NULL);
        _exit(127);
    }
    int st; waitpid(pid, &st, 0);
    ptrace(PTRACE_SETOPTIONS, pid, 0, (void *)(PTRACE_O_TRACECLONE|PTRACE_O_TRACEFORK|PTRACE_O_TRACEVFORK));
    ptrace(PTRACE_CONT, pid, 0, 0);
    int got = 0;
    for (int i = 0; i < 100000; ++i) {
        if (waitpid(-1, &st, 0) < 0) break;
        if (WIFEXITED(st) || WIFSIGNALED(st)) break;
        unsigned event = (unsigned)st >> 16;
        int sig = WSTOPSIG(st);
        if (event == PTRACE_EVENT_CLONE || event == PTRACE_EVENT_FORK || event == PTRACE_EVENT_VFORK) {
            unsigned long long nt = 0;
            ptrace(PTRACE_GETEVENTMSG, pid, 0, &nt);
            if (nt) ptrace(PTRACE_CONT, (pid_t)nt, 0, 0);
            continue;
        }
        if (sig == SIGSEGV) { got = 1; break; }
        ptrace(PTRACE_CONT, pid, 0, (void *)(long)(sig == SIGTRAP || sig == SIGSTOP ? 0 : sig));
    }
    if (!got) { fprintf(stderr, "[fault] no SIGSEGV stop\n"); return 1; }
    struct user_regs_struct r;
    ptrace(PTRACE_GETREGS, pid, 0, &r);
    fprintf(stderr, "[fault] rip=%#llx rdi=%#llx rsi=%#llx rbx=%#llx rbp=%#llx\n",
            (unsigned long long)r.rip, (unsigned long long)r.rdi,
            (unsigned long long)r.rsi, (unsigned long long)r.rbx,
            (unsigned long long)r.rbp);

    char mp[128], line[2048];
    snprintf(mp, sizeof(mp), "/proc/%d/maps", pid);
    char memp[128]; snprintf(memp, sizeof(memp), "/proc/%d/mem", pid);
    int fd = open(memp, O_RDONLY);
    unsigned long long gbase = 0;
    FILE *f = fopen(mp, "r");
    while (f && fgets(line, sizeof(line), f)) {
        unsigned long long lo, hi;
        if (sscanf(line, "%llx-%llx", &lo, &hi) == 2 && strstr(line, "sober-guest")) {
            gbase = lo; break;
        }
    }
    if (f) fclose(f);
    fprintf(stderr, "[fault] guest base=%#llx\n", gbase);

    unsigned long long needle = (unsigned long long)r.rdi;
    f = fopen(mp, "r");
    int hits = 0;
    while (f && fgets(line, sizeof(line), f)) {
        unsigned long long lo, hi; char perms[8]={0};
        if (sscanf(line, "%llx-%llx %7s", &lo, &hi, perms) != 3) continue;
        if (perms[1] != 'w') continue;
        size_t len = (size_t)(hi - lo);
        unsigned char *buf = malloc(len);
        if (!buf) continue;
        ssize_t gotb = pread(fd, buf, len, (off_t)lo);
        if (gotb > 0) {
            for (ssize_t i = 0; i + 8 <= gotb; i += 8) {
                unsigned long long v; memcpy(&v, buf + i, 8);
                if (v == needle) {
                    unsigned long long a = lo + i;
                    unsigned long long m0 = 0, m68 = 0;
                    pread(fd, &m0, 8, (off_t)(a - 0x68));
                    pread(fd, &m68, 8, (off_t)a);
                    fprintf(stderr, "[fault] HIT %#llx (guest RVA %#llx) obj=%#llx obj[0]=%#llx\n",
                            a, gbase ? a - gbase : 0, a - 0x68, m0);
                    hits++;
                    if (hits > 12) break;
                }
            }
        }
        free(buf);
        if (hits > 12) break;
    }
    if (f) fclose(f);
    fprintf(stderr, "[fault] hits=%d\n", hits);
    close(fd);
    ptrace(PTRACE_DETACH, pid, 0, 0);
    return 0;
}
