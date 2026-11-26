/* pktrace.c - 通过 ptrace 读写目标进程内存（2.4 内核 /proc/pid/mem 不可用时的替代）
 *   pktrace <pid> dump <addr> <len>
 *   pktrace <pid> find <start> <len> <val>     # 找 4 字节小端 == val
 *   pktrace <pid> poke <addr> <val>
 * attach 后立刻 detach，目标仅暂停毫秒级；带 alarm 防卡死。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/types.h>

static pid_t g_pid = 0;

static void on_alarm(int s)
{
    (void)s;
    if (g_pid > 0) ptrace(PTRACE_DETACH, g_pid, 0, 0);
    _exit(3);
}

static int attach(pid_t pid)
{
    int st;
    if (ptrace(PTRACE_ATTACH, pid, 0, 0) < 0) { perror("attach"); return -1; }
    if (waitpid(pid, &st, 0) < 0) { perror("waitpid"); ptrace(PTRACE_DETACH, pid, 0, 0); return -1; }
    return 0;
}

static int peekw(pid_t pid, unsigned long a, unsigned long *out)
{
    long w;
    errno = 0;
    w = ptrace(PTRACE_PEEKDATA, pid, (void *)a, 0);
    if (w == -1 && errno) return -1;
    *out = (unsigned long)w & 0xffffffffUL;
    return 0;
}

static int pokew(pid_t pid, unsigned long a, unsigned long v)
{
    if (ptrace(PTRACE_POKEDATA, pid, (void *)a, (void *)v) < 0) return -1;
    return 0;
}

int main(int argc, char **argv)
{
    unsigned long addr, len, val, w;
    int i, j;

    if (argc < 4) {
        fprintf(stderr, "usage: pktrace <pid> dump|find|poke <addr> [len|val]\n");
        return 1;
    }
    g_pid = (pid_t)atoi(argv[1]);
    addr = strtoul(argv[3], 0, 0);
    signal(SIGALRM, on_alarm);
    alarm(20);

    if (attach(g_pid) < 0) return 1;

    if (!strcmp(argv[2], "dump")) {
        len = (argc > 4) ? strtoul(argv[4], 0, 0) : 16;
        for (addr &= ~3UL; len > 0; addr += 16, len = (len > 16) ? len - 16 : 0) {
            unsigned char b[16];
            int got = 0;
            printf("%08lx ", addr);
            for (i = 0; i < 4; i++) {
                if (peekw(g_pid, addr + i * 4, &w) < 0) { printf(" <err>"); break; }
                for (j = 0; j < 4; j++) b[got++] = (w >> (j * 8)) & 0xff;
            }
            for (i = 0; i < 16; i++) {
                if (i < got) printf("%02x", b[i]); else printf("  ");
                printf(" ");
                if (i == 7) printf(" ");
            }
            printf(" |");
            for (i = 0; i < got; i++) putchar((b[i] >= 32 && b[i] < 127) ? b[i] : '.');
            printf("|\n");
            if (got < 16) break;
        }
    } else if (!strcmp(argv[2], "find")) {
        unsigned long start, end;
        len = (argc > 4) ? strtoul(argv[4], 0, 0) : 0x1000;
        val = (argc > 5) ? strtoul(argv[5], 0, 0) : 0;
        start = addr & ~3UL;
        end = start + len;
        for (addr = start; addr < end; addr += 4) {
            if (peekw(g_pid, addr, &w) < 0) continue;
            if (w == val) printf("%08lx\n", addr);
        }
    } else if (!strcmp(argv[2], "poke")) {
        val = strtoul(argv[4], 0, 0);
        if (pokew(g_pid, addr, val) < 0) { perror("poke"); }
        else printf("poke %08lx = %08lx ok\n", addr, val);
    } else {
        fprintf(stderr, "unknown mode %s\n", argv[2]);
    }

    alarm(0);
    ptrace(PTRACE_DETACH, g_pid, 0, 0);
    return 0;
}
