/* crashdump.c - LD_PRELOAD 崩溃抓取（v2）
 *   1) 安装 SIGSEGV/SIGBUS/SIGILL/SIGFPE 处理器，写入 PC/LR/SP/寄存器/栈
 *   2) sigaltstack + SA_ONSTACK：防止栈溢出时处理器自身再次崩溃
 *   3) 后台线程每 100ms 重装一次：防止 am 自己覆盖信号处理器
 *   4) 构造函数写 marker，用于确认 preload 生效
 * 交叉编译：arm-linux-gcc -shared -fPIC -O1 -o libcrashdump.so crashdump.c -lpthread
 */
#define _GNU_SOURCE
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <ucontext.h>
#include <pthread.h>

#define OUT      "/ram/tmp/crash.txt"
#define MARKER   "/ram/tmp/preload_ok"

static char g_altstack[SIGSTKSZ * 4];
static struct sigaction g_sa;

static void
write_file(const char *path, const char *buf, int n)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd >= 0) {
        write(fd, buf, n);
        close(fd);
    }
}

static void
dump(int sig, siginfo_t *si, void *ctx)
{
    ucontext_t *uc = (ucontext_t *) ctx;
    char buf[4096];
    int n = 0, i;
    unsigned long *sp;
    unsigned long pc = 0, lr = 0, spv = 0;

    if (uc) {
        pc  = (unsigned long) uc->uc_mcontext.gregs[R15];
        lr  = (unsigned long) uc->uc_mcontext.gregs[R14];
        spv = (unsigned long) uc->uc_mcontext.gregs[R13];
    }

    n += sprintf(buf + n, "SIG=%d ADDR=%p PC=%08lx LR=%08lx SP=%08lx\n",
                 sig, si ? si->si_addr : (void *) 0, pc, lr, spv);
    if (uc) {
        n += sprintf(buf + n,
                     "r0=%08lx r1=%08lx r2=%08lx r3=%08lx r4=%08lx r5=%08lx\n"
                     "r6=%08lx r7=%08lx r8=%08lx r9=%08lx r10=%08lx fp=%08lx ip=%08lx\n",
                     (unsigned long) uc->uc_mcontext.gregs[R0],
                     (unsigned long) uc->uc_mcontext.gregs[R1],
                     (unsigned long) uc->uc_mcontext.gregs[R2],
                     (unsigned long) uc->uc_mcontext.gregs[R3],
                     (unsigned long) uc->uc_mcontext.gregs[R4],
                     (unsigned long) uc->uc_mcontext.gregs[R5],
                     (unsigned long) uc->uc_mcontext.gregs[R6],
                     (unsigned long) uc->uc_mcontext.gregs[R7],
                     (unsigned long) uc->uc_mcontext.gregs[R8],
                     (unsigned long) uc->uc_mcontext.gregs[R9],
                     (unsigned long) uc->uc_mcontext.gregs[R10],
                     (unsigned long) uc->uc_mcontext.gregs[R11],
                     (unsigned long) uc->uc_mcontext.gregs[R12]);
        n += sprintf(buf + n, "STACK:");
        sp = (unsigned long *) spv;
        for (i = 0; i < 96 && sp; i++) {
            if (i % 4 == 0)
                n += sprintf(buf + n, "\n%08lx:", (unsigned long) (sp + i));
            n += sprintf(buf + n, " %08lx", sp[i]);
            if (n > (int) sizeof(buf) - 64)
                break;
        }
        n += sprintf(buf + n, "\n");
    }

    write_file(OUT, buf, n);
    _exit(139);
}

static void
install_handlers(void)
{
    sigaction(SIGSEGV, &g_sa, 0);
    sigaction(SIGBUS, &g_sa, 0);
    sigaction(SIGILL, &g_sa, 0);
    sigaction(SIGFPE, &g_sa, 0);
}

static void *
reinstall_loop(void *arg)
{
    (void) arg;
    for (;;) {
        install_handlers();
        usleep(100000);
    }
    return 0;
}

__attribute__((constructor))
static void
init_crashdump(void)
{
    stack_t ss;
    pthread_t th;

    write_file(MARKER, "ok\n", 3);

    memset(&g_sa, 0, sizeof(g_sa));
    g_sa.sa_sigaction = dump;
    g_sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&g_sa.sa_mask);

    memset(&ss, 0, sizeof(ss));
    ss.ss_sp = g_altstack;
    ss.ss_size = sizeof(g_altstack);
    sigaltstack(&ss, 0);

    install_handlers();
    pthread_create(&th, 0, reinstall_loop, 0);
}
