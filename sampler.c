/* PC 采样器：循环 attach/detach 目标进程，每轮读 r0-r15，
   输出 PC 轨迹到 stdout（重定向到文件）。用于定位 am 点击启动的执行路径。 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/ptrace.h>
#include <sys/wait.h>

/* ARM pt_regs 布局（内核 asm/ptrace.h）——工具链内核头不可用，自行定义 */
struct arm_pt_regs {
    long uregs[18];
};
#define A_pc   uregs[15]
#define A_lr   uregs[14]
#define A_sp   uregs[13]
#define A_r0   uregs[0]
#define A_r1   uregs[1]
#define A_r2   uregs[2]


int main(int argc, char** argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: sampler <pid> <rounds>\n");
        return 1;
    }
    pid_t pid = atoi(argv[1]);
    int rounds = atoi(argv[2]);

    int i;
    for (i = 0; i < rounds; ++i) {
        if (ptrace(PTRACE_ATTACH, pid, 0, 0) != 0) {
            printf("%d ATTACH_FAIL\n", i);
            fflush(stdout);
            usleep(200000);
            continue;
        }
        int st;
        waitpid(pid, &st, 0);
        struct arm_pt_regs regs;
        memset(&regs, 0, sizeof(regs));
        if (ptrace(PTRACE_GETREGS, pid, 0, &regs) == 0) {
            printf("%d PC=%08lx LR=%08lx SP=%08lx R0=%08lx R1=%08lx R2=%08lx\n",
                   i, (unsigned long)regs.A_pc, (unsigned long)regs.A_lr,
                   (unsigned long)regs.A_sp, (unsigned long)regs.A_r0,
                   (unsigned long)regs.A_r1, (unsigned long)regs.A_r2);
        } else {
            printf("%d GETREGS_FAIL\n", i);
        }
        fflush(stdout);
        ptrace(PTRACE_DETACH, pid, 0, 0);
        usleep(300000);   /* 300ms 采样间隔 */
    }
    return 0;
}
