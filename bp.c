/* bp.c - 极简 ARM 断点器（ptrace），观测 am 内部函数是否被调用
 *   bp <pid> <secs> <addr> [addr2 ...]
 * 每命中打印 HIT <addr> pc lr r0..r3，随后恢复原指令、单步、重装断点、继续。
 * 到达 secs 秒 或 收到 SIGINT/SIGTERM 时：恢复所有原指令并 detach。
 * 注意：绝不能用 kill -9 结束本进程（会留下 BKPT 地雷）。时间到自动退出。
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
#include <sys/user.h>

#define BKPT 0xe1200070UL
#define MAXBP 8

static pid_t g_pid;
static unsigned long g_addr[MAXBP];
static unsigned long g_orig[MAXBP];
static int g_n;
static volatile int g_stop;

static void restore(void)
{
    int i, st = 0;
    /* 被跟踪进程此刻可能仍在运行；必须先用 SIGSTOP 停住，否则 POKETEXT 会被内核拒绝 */
    kill(g_pid, SIGSTOP);
    waitpid(g_pid, &st, 0);
    for (i = 0; i < g_n; i++) {
        unsigned long back;
        ptrace(PTRACE_POKETEXT, g_pid, (void *)g_addr[i], (void *)g_orig[i]);
        back = (unsigned long)ptrace(PTRACE_PEEKTEXT, g_pid, (void *)g_addr[i], 0);
        printf("RESTORE %08lx want=%08lx got=%08lx %s\n", g_addr[i], g_orig[i], back,
               back == g_orig[i] ? "OK" : "FAIL");
    }
    ptrace(PTRACE_DETACH, g_pid, 0, 0);
    fflush(stdout);
}

static void on_sig(int s)
{
    (void)s;
    g_stop = 1;
}

static void setsig(int sig)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_sig;
    sa.sa_flags = 0;            /* 关键：不要 SA_RESTART，否则 waitpid 不返回 */
    sigaction(sig, &sa, NULL);
}

int main(int argc, char **argv)
{
    int i, hits = 0, secs;
    struct user_regs regs;
    int st;

    if (argc < 4) {
        fprintf(stderr, "usage: bp <pid> <secs> <addr> [addr2 ...]\n");
        return 1;
    }
    g_pid = (pid_t)atoi(argv[1]);
    secs = atoi(argv[2]);
    for (i = 3; i < argc && g_n < MAXBP; i++)
        g_addr[g_n++] = strtoul(argv[i], 0, 0);

    setsig(SIGINT); setsig(SIGTERM); setsig(SIGALRM);

    if (ptrace(PTRACE_ATTACH, g_pid, 0, 0) < 0) { perror("attach"); return 1; }
    if (waitpid(g_pid, &st, 0) < 0) { perror("waitpid"); return 1; }

    for (i = 0; i < g_n; i++) {
        g_orig[i] = (unsigned long)ptrace(PTRACE_PEEKTEXT, g_pid, (void *)g_addr[i], 0);
        ptrace(PTRACE_POKETEXT, g_pid, (void *)g_addr[i], (void *)BKPT);
        printf("ARM %08lx orig=%08lx\n", g_addr[i], g_orig[i]);
    }
    printf("ARMED pid=%d secs=%d\n", (int)g_pid, secs);
    fflush(stdout);

    alarm(secs > 0 ? secs : 90);
    while (!g_stop) {
        unsigned long pc;
        if (ptrace(PTRACE_CONT, g_pid, 0, 0) < 0) break;
        if (waitpid(g_pid, &st, 0) < 0) {
            if (errno == EINTR) break;
            break;
        }
        if (WIFEXITED(st) || WIFSIGNALED(st)) {
            printf("TARGET GONE status=%x\n", st);
            fflush(stdout);
            return 0;
        }
        if (!WIFSTOPPED(st)) continue;
        if (ptrace(PTRACE_GETREGS, g_pid, 0, &regs) < 0) break;
        pc = regs.uregs[15];

        for (i = 0; i < g_n; i++) {
            if (pc == g_addr[i] || pc == g_addr[i] + 4) {
                unsigned long a = g_addr[i];
                printf("HIT %08lx sig=%d pc=%08lx lr=%08lx r0=%08lx r1=%08lx r2=%08lx r3=%08lx\n",
                       a, WSTOPSIG(st), pc, regs.uregs[14], regs.uregs[0],
                       regs.uregs[1], regs.uregs[2], regs.uregs[3]);
                fflush(stdout);
                hits++;
                ptrace(PTRACE_POKETEXT, g_pid, (void *)a, (void *)g_orig[i]);
                regs.uregs[15] = a;
                ptrace(PTRACE_SETREGS, g_pid, 0, &regs);
                if (ptrace(PTRACE_SINGLESTEP, g_pid, 0, 0) == 0)
                    waitpid(g_pid, &st, 0);
                ptrace(PTRACE_POKETEXT, g_pid, (void *)a, (void *)BKPT);
                break;
            }
        }
        if (i == g_n) {
            int s = WSTOPSIG(st);
            if (s != SIGTRAP && s != SIGILL)
                ptrace(PTRACE_CONT, g_pid, 0, (void *)s);
        }
    }
    restore();
    printf("DETACHED hits=%d\n", hits);
    fflush(stdout);
    return 0;
}
