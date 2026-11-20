/* Runtime experiment helper (worker-1, task #387): patch a guest byte range
 * in a running traced harness, then let it run to conclusion. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <stddef.h>

int main(int argc, char **argv)
{
    if (argc < 5) { fprintf(stderr, "usage: %s rva hexbytes delay_ms\n", argv[0]); return 2; }
    unsigned long long rva = strtoull(argv[1], 0, 0);
    unsigned char patch[64]; int n = 0;
    char *s = argv[2];
    while (s[0] && s[1] && n < (int)sizeof(patch)) {
        char tmp[3] = {s[0], s[1], 0};
        patch[n++] = (unsigned char)strtoul(tmp, 0, 16);
        s += 2;
    }
    long delay_ms = strtol(argv[3], 0, 0);
    const char *path = argv[4];

    pid_t pid = fork();
    if (pid == 0) {
        setenv("LD_LIBRARY_PATH", "/home/user/sober-linux/build/ndk-stubs", 1);
        setenv("EGL_PLATFORM", "x11", 1);
        setenv("LIBGL_ALWAYS_SOFTWARE", "1", 1);
        setenv("SOBER_RENDER", "1", 1);
        setenv("SOBER_APP_SHELL", "1", 1);
        setenv("SOBER_WINDOW_HOLD_MS", "1000", 1);
        ptrace(PTRACE_TRACEME, 0, 0, 0);
        execl("/home/user/sober-linux/build/guest-init-host", "guest-init-host", path,
              (char *)NULL);
        perror("execl"); _exit(127);
    }
    int st;
    waitpid(pid, &st, 0);
    ptrace(PTRACE_CONT, pid, 0, 0);
    usleep(delay_ms * 1000);
    kill(pid, SIGSTOP);
    waitpid(pid, &st, 0);

    /* find guest memfd base */
    char pth[128], line[2048];
    snprintf(pth, sizeof(pth), "/proc/%d/maps", pid);
    FILE *f = fopen(pth, "r");
    unsigned long long base = 0;
    while (f && fgets(line, sizeof(line), f)) {
        unsigned long long lo, hi;
        if (sscanf(line, "%llx-%llx", &lo, &hi) == 2 && strstr(line, "sober-guest")) {
            base = lo; break;
        }
    }
    if (f) fclose(f);
    fprintf(stderr, "[patch] base=%#llx patch RVA=%#llx bytes=%d\n", base, rva, n);
    if (!base) { kill(pid, SIGKILL); return 3; }

    unsigned long long addr = base + rva;
    for (int i = 0; i < n; ++i) {
        errno = 0;
        long w = ptrace(PTRACE_PEEKDATA, pid, (void *)(addr + i - (i % 8)), 0);
        (void)w;
    }
    /* write in aligned 8-byte words preserving neighbours */
    for (int off = 0; off < n; ) {
        unsigned long long wa = (addr + off) & ~7ULL;
        int skip = (int)((addr + off) - wa);
        long word = ptrace(PTRACE_PEEKDATA, pid, (void *)wa, 0);
        for (int k = skip; k < 8 && off < n; ++k, ++off)
            word = (word & ~(0xffL << (8 * k))) | ((long)patch[off] << (8 * k));
        ptrace(PTRACE_POKEDATA, pid, (void *)wa, (void *)word);
    }
    ptrace(PTRACE_CONT, pid, 0, (void *)SIGCONT);
    waitpid(pid, &st, 0);
    fprintf(stderr, "[patch] child exit=%d sig=%d\n",
            WIFEXITED(st)?WEXITSTATUS(st):-1, WIFSIGNALED(st)?WTERMSIG(st):0);
    return 0;
}
