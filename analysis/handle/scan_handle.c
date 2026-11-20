/* Runtime memory scanner (worker-1, task #387): stop the harness mid-app-shell
 * and locate the bogus handle by scanning guest RW mappings for the observed
 * garbage pointer and the 0xc0dedbad magic. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/uio.h>

#define GARBAGE 0xffffff3085894cffULL
#define MAGIC   0xc0dedbadU

int main(int argc, char **argv)
{
    long delay_ms = argc > 1 ? strtol(argv[1], 0, 0) : 2000;
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
    ptrace(PTRACE_CONT, pid, 0, 0);
    usleep(delay_ms * 1000);
    kill(pid, SIGSTOP);
    waitpid(pid, &st, 0);

    char mp[128], line[2048];
    snprintf(mp, sizeof(mp), "/proc/%d/maps", pid);
    FILE *f = fopen(mp, "r");
    int fd = -1;
    char memp[128]; snprintf(memp, sizeof(memp), "/proc/%d/mem", pid);
    /* open /proc/pid/mem before ptrace detaches */
    fd = open(memp, O_RDONLY);
    unsigned long long guest_lo = 0, guest_hi = 0;
    if (f) {
        while (fgets(line, sizeof(line), f)) {
            unsigned long long lo, hi;
            if (sscanf(line, "%llx-%llx", &lo, &hi) == 2 && strstr(line, "sober-guest")) {
                if (!guest_lo || lo < guest_lo) guest_lo = lo;
                if (hi > guest_hi) guest_hi = hi;
            }
        }
        fclose(f);
    }
    fprintf(stderr, "[scan] pid=%d guest=%#llx-%#llx memfd=%d\n",
            pid, guest_lo, guest_hi, fd);
    if (fd < 0) { kill(pid, SIGKILL); return 2; }

    unsigned long long needle = GARBAGE;
    int found = 0;
    /* re-read maps, scan writable guest ranges */
    f = fopen(mp, "r");
    while (f && fgets(line, sizeof(line), f)) {
        unsigned long long lo, hi; char perms[8] = {0};
        if (sscanf(line, "%llx-%llx %7s", &lo, &hi, perms) != 3) continue;
        if (perms[1] != 'w') continue;
        /* scan all writable mappings, guest and host */
        size_t len = (size_t)(hi - lo);
        unsigned char *buf = malloc(len);
        if (!buf) continue;
        ssize_t got = pread(fd, buf, len, (off_t)lo);
        if (got < 0) { free(buf); continue; }
        for (ssize_t i = 0; i + 8 <= got; i += 8) {
            unsigned long long v;
            memcpy(&v, buf + i, 8);
            if (v == needle) {
                unsigned long long addr = lo + i;
                fprintf(stderr, "[scan] GARBAGE at %#llx (guest RVA %#llx)\n",
                        addr, addr - guest_lo);
                unsigned long long obj = addr - 0x68;
                unsigned long long m0 = 0;
                pread(fd, &m0, 8, (off_t)obj);
                fprintf(stderr, "[scan]   candidate obj=%#llx obj[0]=%#llx (magic expect %#x)\n",
                        obj, m0, MAGIC);
                found++;
            }
        }
        free(buf);
    }
    if (f) fclose(f);
    fprintf(stderr, "[scan] total garbage hits=%d\n", found);
    close(fd);
    ptrace(PTRACE_CONT, pid, 0, (void *)SIGCONT);
    waitpid(pid, &st, 0);
    fprintf(stderr, "[scan] child exit=%d sig=%d\n",
            WIFEXITED(st)?WEXITSTATUS(st):-1, WIFSIGNALED(st)?WTERMSIG(st):0);
    return 0;
}
