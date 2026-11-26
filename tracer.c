/* tracer.c - 迷你 ptrace 崩溃抓取器（替代不可用的 gdb bt）
 * 用法: tracer <pid> [seconds]
 * 行为: attach -> continue -> 等 SIGSEGV/SIGBUS/SIGILL/SIGABRT
 *       -> 读寄存器(r0-r15/cpsr) + 栈内容 -> 写 /ram/tmp/trace.txt -> detach
 * 交叉编译: arm-linux-gcc -O1 -static -o tracer tracer.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>

#define OUT "/ram/tmp/trace.txt"

static unsigned long
peek(pid_t pid, unsigned long addr, int *ok)
{
    long v = ptrace(PTRACE_PEEKDATA, pid, (void *) addr, 0);
    if (v == -1 && errno != 0) {
        *ok = 0;
        return 0;
    }
    *ok = 1;
    return (unsigned long) v;
}

int
main(int argc, char **argv)
{
    pid_t pid;
    int secs = 60, st, ok, i;
    long regs[20];
    unsigned long pc, lr, sp;
    char buf[4096];
    int n = 0, fd;
    time_t t0;

    if (argc < 2) {
        fprintf(stderr, "usage: tracer <pid> [secs]\n");
        return 2;
    }
    pid = (pid_t) atoi(argv[1]);
    if (argc > 2)
        secs = atoi(argv[2]);

    if (ptrace(PTRACE_ATTACH, pid, 0, 0) < 0) {
        fprintf(stderr, "attach failed: %s\n", strerror(errno));
        return 3;
    }
    if (waitpid(pid, &st, 0) < 0) {
        fprintf(stderr, "waitpid failed\n");
        return 3;
    }

    /* 若 attach 时停在某个信号上，先吞掉继续跑 */
    if (WIFSTOPPED(st)) {
        int sig = WSTOPSIG(st);
        if (sig != SIGSTOP && sig != SIGTRAP)
            ptrace(PTRACE_CONT, pid, 0, (void *) sig);
        else
            ptrace(PTRACE_CONT, pid, 0, 0);
    }

    t0 = time(0);
    for (;;) {
        if (waitpid(pid, &st, 0) < 0)
            break;
        if (WIFEXITED(st) || WIFSIGNALED(st)) {
            n = sprintf(buf, "PROCESS GONE (exited=%d code=%d signaled=%d sig=%d)\n",
                        WIFEXITED(st), WIFEXITED(st) ? WEXITSTATUS(st) : -1,
                        WIFSIGNALED(st), WIFSIGNALED(st) ? WTERMSIG(st) : -1);
            break;
        }
        if (WIFSTOPPED(st)) {
            int sig = WSTOPSIG(st);
            if (sig == SIGSEGV || sig == SIGBUS || sig == SIGILL || sig == SIGFPE || sig == SIGABRT) {
                memset(regs, 0, sizeof(regs));
                if (ptrace(PTRACE_GETREGS, pid, 0, regs) == 0) {
                    pc = (unsigned long) regs[15];
                    lr = (unsigned long) regs[14];
                    sp = (unsigned long) regs[13];
                    n = sprintf(buf, "SIGNAL=%d PC=%08lx LR=%08lx SP=%08lx\n",
                                sig, pc, lr, sp);
                    n += sprintf(buf + n,
                                 "r0=%08lx r1=%08lx r2=%08lx r3=%08lx r4=%08lx r5=%08lx r6=%08lx\n"
                                 "r7=%08lx r8=%08lx r9=%08lx r10=%08lx fp=%08lx ip=%08lx\n",
                                 (unsigned long) regs[0], (unsigned long) regs[1],
                                 (unsigned long) regs[2], (unsigned long) regs[3],
                                 (unsigned long) regs[4], (unsigned long) regs[5],
                                 (unsigned long) regs[6], (unsigned long) regs[7],
                                 (unsigned long) regs[8], (unsigned long) regs[9],
                                 (unsigned long) regs[10], (unsigned long) regs[11],
                                 (unsigned long) regs[12]);
                    n += sprintf(buf + n, "STACK:");
                    for (i = 0; i < 80; i++) {
                        unsigned long v = peek(pid, sp + i * 4, &ok);
                        if (!ok)
                            break;
                        if (i % 4 == 0)
                            n += sprintf(buf + n, "\n%08lx:", (unsigned long) (sp + i * 4));
                        n += sprintf(buf + n, " %08lx", v);
                        if (n > (int) sizeof(buf) - 64)
                            break;
                    }
                    n += sprintf(buf + n, "\n");
                } else {
                    n = sprintf(buf, "GETREGS failed: %s\n", strerror(errno));
                }
                fd = open(OUT, O_WRONLY | O_CREAT | O_TRUNC, 0666);
                if (fd >= 0) {
                    write(fd, buf, n);
                    close(fd);
                }
                /* 保留现场：不 detach，直接让进程被信号杀死 */
                ptrace(PTRACE_DETACH, pid, 0, (void *) sig);
                return 0;
            }
            ptrace(PTRACE_CONT, pid, 0, (void *) sig);
        }
        if (time(0) - t0 > secs)
            break;
    }

    fd = open(OUT, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd >= 0) {
        if (n == 0)
            n = sprintf(buf, "TIMEOUT no crash in %ds\n", secs);
        write(fd, buf, n);
        close(fd);
    }
    ptrace(PTRACE_DETACH, pid, 0, 0);
    return 0;
}
