/* crashlog.cpp - am 崩溃现场抓取器（v36 新增）
 *
 * 背景：卸载软件时 am 直接死掉，只留下 amreg.log 的最后一条记录，
 * 无法判断是撞在 am / libezxaid / 我们库 的哪一行。设备上虽有 gdb，
 * 但 uninstall 路径必须点屏幕触发，远程没法复现。
 *
 * 做法：
 *   1. ring_install_handler() 里调 crash_install()，安装 SIGSEGV/SIGBUS/
 *      SIGFPE/SIGILL/SIGABRT 处理器（SA_SIGINFO 取 ucontext）。
 *   2. 安装时把 /proc/self/maps 落到 /ram/tmp/am_maps_<pid>.txt，
 *      崩溃后用地址减去对应模块基址即可反查符号。
 *   3. 崩溃时用 async-signal-safe 的 write() 把 信号/故障地址/全部寄存器/
 *      sp 起 384 个字 追加到 /ram/tmp/amreg.log，然后恢复默认处理器重发信号。
 *
 * 注意：ARM Linux 2.4 的 ucontext 布局是
 *   uc_flags(4) uc_link(4) uc_stack(12) uc_mcontext ...  → mcontext 在 +20
 * sigcontext 起始三个 word 是 trap_no/error_code/oldmask，然后 r0..pc。
 */
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>

#include "ringlog.h"   /* v43: RingEnt/g_ring/RING_SIZE，崩溃时回放调用记录 */

struct arm_sigcontext {
    unsigned long trap_no;
    unsigned long error_code;
    unsigned long oldmask;
    unsigned long arm_r0, arm_r1, arm_r2, arm_r3, arm_r4, arm_r5, arm_r6;
    unsigned long arm_r7, arm_r8, arm_r9, arm_r10, arm_fp, arm_ip;
    unsigned long arm_sp, arm_lr, arm_pc, arm_cpsr;
    unsigned long fault_address;
};

struct arm_ucontext {
    unsigned long uc_flags;
    struct arm_ucontext* uc_link;
    struct { void* ss_sp; int ss_flags; unsigned long ss_size; } uc_stack;
    struct arm_sigcontext uc_mcontext;
};

#define CRASH_LOG "/ram/tmp/amreg.log"

static volatile int s_fd = -1;

static void cw(const char* s)
{
    if (s_fd >= 0)
        write((int)s_fd, s, strlen(s));
}

static void cwhex(unsigned long v)
{
    char b[11];
    int i;
    b[0] = '0';
    b[1] = 'x';
    for (i = 0; i < 8; ++i) {
        int d = (int)((v >> (28 - i * 4)) & 0xF);
        b[2 + i] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
    }
    b[10] = 0;
    cw(b);
}

static void cwreg(const char* name, unsigned long v)
{
    cw(name);
    cw("=");
    cwhex(v);
    cw(" ");
}

static void crash_handler(int sig, siginfo_t* si, void* uc)
{
    struct arm_ucontext* u = (struct arm_ucontext*)uc;
    struct arm_sigcontext* m = u ? &u->uc_mcontext : 0;
    unsigned long sp = 0;
    unsigned long i;

    /* v46：先落 /proc/self/maps。改成"崩溃时才 dump"而不是"进程启动时 dump"，
       这样可以在**所有**加载本库的进程里无条件装处理器（安装时零开销），
       崩溃时再取映射表 —— 装 SoftManage / ezxaid 里崩的现场才能反查到模块。 */
    {
        char path[64];
        char buf[4096];
        int in  = open("/proc/self/maps", O_RDONLY);
        if (in >= 0) {
            memset(path, 0, sizeof(path));
            strcpy(path, "/ram/tmp/am_maps_");
            {
                char num[16];
                int v = (int)getpid(), k = 0, j;
                char tmp[16];
                if (v == 0) tmp[k++] = '0';
                while (v > 0) { tmp[k++] = (char)('0' + v % 10); v /= 10; }
                for (j = 0; j < k; ++j) num[j] = tmp[k - 1 - j];
                num[k] = 0;
                strcat(path, num);
            }
            strcat(path, ".txt");
            int out = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (out >= 0) {
                int n;
                while ((n = (int)read(in, buf, sizeof(buf))) > 0)
                    write(out, buf, (unsigned)n);
                close(out);
            }
            close(in);
        }
    }

    cw("\n==== CRASH pid=");
    cwhex((unsigned long)getpid());
    /* 进程名（cmdline 第一段），多进程环境下必须能分辨是谁崩了 */
    cw(" comm=");
    {
        int cf = open("/proc/self/cmdline", O_RDONLY);
        if (cf >= 0) {
            char cb[128];
            int n = (int)read(cf, cb, sizeof(cb) - 1);
            close(cf);
            if (n > 0) {
                int k;
                for (k = 0; k < n; ++k) {
                    if (cb[k] == 0)
                        cb[k] = '|';
                    else if (cb[k] == '\n' || cb[k] == '\r')
                        cb[k] = ' ';
                }
                cb[n] = 0;
                cw(cb);
            }
        }
    }
    cw(" sig=");
    cwhex((unsigned long)sig);
    if (si) {
        cw(" addr=");
        cwhex((unsigned long)si->si_addr);
        cw(" code=");
        cwhex((unsigned long)si->si_code);
    }
    cw(" ====\n");
    if (m) {
        cwreg("r0", m->arm_r0);   cwreg("r1", m->arm_r1);
        cwreg("r2", m->arm_r2);   cwreg("r3", m->arm_r3);
        cwreg("r4", m->arm_r4);   cwreg("r5", m->arm_r5);
        cwreg("r6", m->arm_r6);   cwreg("r7", m->arm_r7);
        cw("\n");
        cwreg("r8", m->arm_r8);   cwreg("r9", m->arm_r9);
        cwreg("r10", m->arm_r10); cwreg("fp", m->arm_fp);
        cwreg("ip", m->arm_ip);   cwreg("sp", m->arm_sp);
        cwreg("lr", m->arm_lr);   cwreg("pc", m->arm_pc);
        cw("\n");
        cwreg("cpsr", m->arm_cpsr);
        cw("\n");
        sp = m->arm_sp;
    }
    /* 栈扫描：从 sp 起 384 个字原样落盘，事后用 maps 基址反查返回地址 */
    if (sp && (sp & 3) == 0) {
        cw("---- stack ----\n");
        for (i = 0; i < 384; ++i) {
            unsigned long v;
            /* 越界读会再次触发 SIGSEGV；先做粗略范围检查 */
            if (sp + i * 4 < 0x1000 || sp + i * 4 > 0xbffff000)
                break;
            v = ((volatile unsigned long*)sp)[i];
            cwhex(sp + i * 4);
            cw(": ");
            cwhex(v);
            cw("\n");
        }
        cw("---- end ----\n");
    }
    /* v43：把环形调用记录一起落盘。堆被写坏之后，光看寄存器/栈只能知道
       "崩在 malloc"，看不出是哪条库内路径把元数据踩坏的。ring 里每个函数
       入口都记了 (tag,a,b)，这里回放崩前最后 64 条，基本能还原调用链尾部。
       tag 含义见 src/ringlog.h 顶部的标签表。 */
    {
        unsigned pos = g_ring_pos;
        unsigned n = (pos >= 64) ? 64u : pos;
        unsigned start = pos - n;
        unsigned k;
        cw("---- ring(last ");
        cwhex((unsigned long)n);
        cw(") ----\n");
        for (k = start; k < pos; ++k) {
            const RingEnt* e = &g_ring[k % RING_SIZE];
            cwhex((unsigned long)k);
            cw(": tag="); cwhex((unsigned long)e->tag);
            cw(" a=");    cwhex((unsigned long)e->a);
            cw(" b=");    cwhex((unsigned long)e->b);
            cw("\n");
        }
    }
    cw("CRASH_END\n");

    /* 恢复默认行为并重发，让系统照常产生退出码/核心 */
    signal(sig, SIG_DFL);
    raise(sig);
}

/* ★v54：被 SIGTERM/SIGHUP/SIGQUIT 要求退出时留一行再按默认动作死。
   目的：区分三种"静默消失"——
     · SIGKILL   → 什么都不会留下（内核直接抹掉）
     · 本处理器  → 留 [SIG] 行（说明是被"礼貌地"要求退出）
     · exit()    → 留 [EXIT] 行 */
/* 信号处理器里只能用手写的格式化（见下面 sig_puts/sig_putnum 的定义与说明） */
static int sig_puts(char* b, int n, int cap, const char* s);
static int sig_putnum(char* b, int n, int cap, int v);

static void term_handler(int sig)
{
    char b[192];
    int n = 0;
    n = sig_puts(b, n, (int)sizeof(b), "[SIG] pid=");
    n = sig_putnum(b, n, (int)sizeof(b), (int)getpid());
    n = sig_puts(b, n, (int)sizeof(b), " sig=");
    n = sig_putnum(b, n, (int)sizeof(b), sig);
    n = sig_puts(b, n, (int)sizeof(b), " ");
    {
        int fd = open("/proc/self/cmdline", O_RDONLY);
        if (fd >= 0) {
            char cb[64];
            int r = (int)read(fd, cb, sizeof(cb) - 1);
            close(fd);
            for (int k = 0; k < r && cb[k] && n < (int)sizeof(b) - 4; ++k)
                b[n++] = cb[k];
        }
    }
    b[n++] = '\n';
    if (s_fd >= 0)
        write(s_fd, b, n);
    signal(sig, SIG_DFL);
    raise(sig);
}

/* ★v57 血案修正：v55/v56 在这两个信号处理器里用了 snprintf ——
   那是**非异步信号安全**的（内部会取 stdio/区域设置锁、可能 malloc）。
   am 在 malloc 临界区里被 SIGCHLD 打断时，处理器再进 snprintf 就自锁，
   表现为"界面卡死"，然后被平台看门狗每 ~30 秒 kill 一次、无限重启
   （日志里连续 5 个 BOOT）。现在改成纯手写数字格式化，处理器里只用
   write() 和 syscall()，不碰任何 libc 缓冲区/分配器。 */
static int sig_puts(char* b, int n, int cap, const char* s)
{
    while (*s && n < cap - 1)
        b[n++] = *s++;
    return n;
}

static int sig_putnum(char* b, int n, int cap, int v)
{
    char t[12];
    int k = 0, neg = 0;
    unsigned u;
    if (v < 0) { neg = 1; u = (unsigned)(-v); } else u = (unsigned)v;
    if (!u) t[k++] = '0';
    while (u) { t[k++] = (char)('0' + (u % 10)); u /= 10; }
    if (neg && n < cap - 1) b[n++] = '-';
    while (k && n < cap - 1) b[n++] = t[--k];
    return n;
}

/* ★v58：删除 [CHLD] 探针。
   原设计想用 waitid(P_ALL, WNOWAIT) 窥探子进程死因而不抢占回收，走
   syscall(280,...) 绕开 2004 年 uClibc 头文件缺 __USE_XOPEN2K8 的问题。
   真机实测（/ram/tmp/t_chld）：
       sys280 ret=-1 errno=38 (ENOSYS)   —— 三种 options 组合全部如此
   本机内核是 Linux 2.4.20_mvlcee31，sys_waitid 根本不存在，探针永远不可能
   产生一条记录；而它却要在**每个** SIGCHLD 里做一次无用系统调用。
   结论：整段删除。子进程死因改用已有的 [MAP]/[EXIT]/CRASH 三条线判断。 */

void crash_install()
{
    static int done = 0;
    struct sigaction sa;

    if (done)
        return;
    done = 1;

    s_fd = open(CRASH_LOG, O_WRONLY | O_APPEND | O_CREAT, 0644);

    /* v46：maps 改到崩溃时 dump（见 crash_handler），这里不再占每次启动的 IO */

    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = crash_handler;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, 0);
    sigaction(SIGBUS,  &sa, 0);
    sigaction(SIGFPE,  &sa, 0);
    sigaction(SIGILL,  &sa, 0);
    sigaction(SIGABRT, &sa, 0);

    /* ★v54：可捕获的"被要求退出"信号。
       排查"安装秒退"时出现的现象是：进程既没有 CRASH 段、也没有 [EXIT]，
       那就是 SIGKILL（抓不住）或其它"默认动作=静默终止"的信号
       （默认终止**不走 atexit**，所以同样没有 [EXIT]）。
       下面这些能抓，记一行再按默认动作走，行为不变、只多一行日志。 */
    struct sigaction st;
    memset(&st, 0, sizeof(st));
    st.sa_handler = term_handler;
    st.sa_flags = 0;
    sigemptyset(&st.sa_mask);
    sigaction(SIGTERM, &st, 0);
    sigaction(SIGHUP,  &st, 0);
    sigaction(SIGQUIT, &st, 0);

    /* ★v57：下面这批"诊断专用"处理器一律**默认不装**，必须先
       touch /ram/tmp/am_probe 才启用。
       血案：v55/v56 无条件装 SIGCHLD 处理器，而处理器里调了 snprintf
       （非异步信号安全，可能在 malloc/stdio 锁上自锁）→ am 卡死 →
       平台看门狗每 ~30 秒 kill 一次 → 日志连续 5 个 BOOT、用户看到"界面卡死"。
       现在：默认只保留 SIGTERM/HUP/QUIT（极低频、且只写一行就按默认动作死），
       诊断信号一律走开关，避免再次把日常使用拖下水。 */
    if (access("/ram/tmp/am_probe", F_OK) != 0)
        return;
#ifdef SIGUSR1
    /* 注意：am 里的 SIGUSR1 稍后会被 ring_install_handler 换成"dump 环形缓冲"，
       这里装只是保证**没走 RMCtor 的进程**（如 mystuff/安装器）也能留痕。 */
    sigaction(SIGUSR1, &st, 0);
#endif
#ifdef SIGUSR2
    sigaction(SIGUSR2, &st, 0);
#endif
#ifdef SIGPIPE
    sigaction(SIGPIPE, &st, 0);
#endif
#ifdef SIGALRM
    sigaction(SIGALRM, &st, 0);
#endif
#ifdef SIGVTALRM
    sigaction(SIGVTALRM, &st, 0);
#endif
#ifdef SIGPROF
    sigaction(SIGPROF, &st, 0);
#endif
#ifdef SIGXCPU
    sigaction(SIGXCPU, &st, 0);
#endif
#ifdef SIGXFSZ
    sigaction(SIGXFSZ, &st, 0);
#endif
    /* ★v58：原来这里装 SIGCHLD 处理器窥探子进程死因，因内核 2.4.20 无
       waitid（实测 errno=38 ENOSYS）已整段删除，见上方 chld_handler 处注释。 */
}

/* ============================================================
   v46：库加载即装崩溃处理器（__attribute__((constructor))）
   为什么：安装/卸载崩溃的现场不在 am 进程里 ——
     · am 是常驻的，它的 BOOT 行没断就说明 am 没崩；
     · SoftManage（安装器）、ezxaid（解包器）都是**被 fork/exec 出来的**，
       它们只在真的 new 了 AM_RegistryManager 时才会顺带装处理器，
       崩在更早的位置就什么也留不下。
   本文件加一个 constructor 后，任何加载 libezxam 的进程一启动就有处理器，
   崩了必然在 /ram/tmp/amreg.log 留一条 `==== CRASH pid=.. comm=.. sig=..`。
   开销：每次加载本库 open 一次日志（一个 fd，常驻），崩溃时才写。
   关闭办法（万一某进程不允许改信号处理器）：touch /ram/tmp/am_nocrash
   ============================================================ */
/* ---- v49：进程"正常退出"也留痕 ----
   为什么需要：真机反馈"我的收藏直接崩溃了"，但日志里既没有 CRASH 段也没有
   `am_maps_*.txt` —— 说明进程**不是被信号打死的**，而是 exit()/qFatal() 走的
   （例如 Qt 在没有 QApplication 时 QCopChannel::send 会 qFatal→exit(1)）。
   加了 atexit 之后，三种死法可以一眼区分：
     · 有 CRASH 段               → 信号致死，段里有 pc/lr/栈
     · 只有 [EXIT] 行            → 自己 exit / qFatal / return from main
     · 两者都没有                → 被 SIGKILL / 平台 kill 掉
   注：atexit 只在正常退出路径跑；am 关机会因此多一行，量极小。 */
extern "C" void am_exit_marker(void);
extern "C" void am_exit_marker(void)
{
    char b[128];
    int  n = 0;
    const char* tag = "[EXIT] pid=";
    while (*tag && n < (int)sizeof(b) - 1) b[n++] = *tag++;
    n += snprintf(b + n, sizeof(b) - n, "%d comm=", (int)getpid());
    /* 从 /proc/self/cmdline 取 comm，避免依赖 /proc/self/stat 的格式 */
    {
        int fd = open("/proc/self/cmdline", O_RDONLY);
        if (fd >= 0) {
            char cb[64];
            int  r = (int)read(fd, cb, sizeof(cb) - 1);
            close(fd);
            if (r > 0) {
                int k = 0;
                while (k < r && cb[k] && n < (int)sizeof(b) - 1)
                    b[n++] = cb[k++];
            }
        }
    }
    b[n++] = '\n';
    b[n]   = 0;
    if (s_fd >= 0)
        write(s_fd, b, n);
}

/* ★v54：进程加载本库时先自报家门（pid / 父 pid / 命令行）。
   为什么：安装链路是 am → 我的收藏 → 安装器 的多进程接力，而"我的收藏"
   和安装器都可能在 new AM_RegistryManager 之前就消亡，于是日志里既没有
   BOOT（BOOT 由 RMCtor 写）也没有 EXIT（被信号打死）。有了这一行，
   任何加载本库的进程都会留下 pid+ppid，能直接看出"是谁的孩子、活了几毫秒"。 */
static int am_ppid()
{
    int fd = open("/proc/self/stat", O_RDONLY);
    if (fd < 0)
        return -1;
    char b[512];
    int r = (int)read(fd, b, sizeof(b) - 1);
    close(fd);
    if (r <= 0)
        return -1;
    b[r] = 0;
    char* p = strrchr(b, ')');          /* comm 里可能有空格/括号，从最后一个 ')' 起算 */
    if (!p)
        return -1;
    p++;
    while (*p == ' ')
        ++p;
    ++p;                                 /* 跳过 state */
    return atoi(p);
}

__attribute__((constructor)) static void am_auto_crash_install()
{
    if (access("/ram/tmp/am_nocrash", F_OK) == 0)
        return;
    crash_install();
    atexit(am_exit_marker);

    if (s_fd >= 0) {
        char b[192];
        int n = snprintf(b, sizeof(b), "[MAP] pid=%d ppid=%d ", (int)getpid(), am_ppid());
        int fd = open("/proc/self/cmdline", O_RDONLY);
        if (fd >= 0) {
            char cb[64];
            int r = (int)read(fd, cb, sizeof(cb) - 1);
            close(fd);
            for (int k = 0; k < r && cb[k] && n < (int)sizeof(b) - 4; ++k)
                b[n++] = cb[k];
        }
        b[n++] = '\n';
        write(s_fd, b, n);
    }
}

/* ============================================================
   v52：抓 Qt 的 qFatal / qWarning 文本
   ------------------------------------------------------------
   为什么需要：v49/v51 的 atexit 标记已经证明"我的收藏"和"安装器 SoftManage"
   都是**自己 exit()** 走的（有 [EXIT]、无 CRASH 段），但 exit 之前它到底遇到
   了什么没有任何记录 —— 日志里只剩我们自己的函数调用，看不到对方放弃的原因。

   Qt 的 qFatal/qWarning 全部经过全局消息处理器，而**本库被这些进程加载**
   （SoftManage 需要 addAppToMainMenu/addAppToFolder 等 11 个符号），
   所以在构造函数里挂一个处理器，就能把对方的原生错误文本连 pid/comm 一起
   落到 amreg.log。不改对方行为：处理后仍转发给原来的处理器。

   限制：每进程最多记 40 条（QtWarning 在热路径上可能出现很多），
        只记 QtFatal / QtWarning，Debug 一律不记。
   关闭：touch /ram/tmp/am_noqtmsg
   ============================================================ */
#include <qglobal.h>

/* Qt 2.3.8 的命名与 Qt3 不同：
     enum QtMsgType { QtDebugMsg, QtWarningMsg, QtFatalMsg };
     typedef void (*msg_handler)(QtMsgType, const char *);
     Q_EXPORT msg_handler qInstallMsgHandler( msg_handler );   */
/* v58：不再保存"旧处理器"。见下方 am_qt_msg_handler 上方的血案说明。 */
static int s_msgQuota = 40;

static void am_comm(char* out, int cap)
{
    int n = 0;
    out[0] = 0;
    int fd = open("/proc/self/cmdline", O_RDONLY);
    if (fd < 0)
        return;
    char cb[64];
    int r = (int)read(fd, cb, sizeof(cb) - 1);
    close(fd);
    for (int k = 0; k < r && cb[k] && n < cap - 1; ++k)
        out[n++] = cb[k];
    out[n] = 0;
}

/* ★v58 血案修正二：这条转发链会把进程打成一个永不退出的缺页死循环。
   ------------------------------------------------------------
   真机现场（pid 3191/4049，均为 /usr/SYSqtapp/messaging/msgctrloadslaveapp）：
     · 两个进程 state=R，majflt 各 1500+，stime 分别 4325s / 953s，长期占满 CPU
       （loadavg 5.1），表现就是用户说的"界面卡死、点不动"
     · 内核反复打印同一条现场：pc=0x6c735b28（未映射）/ pc=0x00000000
     · 用 PTRACE 从活进程取到的真实寄存器：
         r0=0x0 (QtDebugMsg)  r1=栈上的 qDebug 文本  lr=libqte 里 blx 之后
         r12=0x41b10718 = sig_putnum+0x78（**函数中段**，不是入口）
       而该处紧邻 `ldmia sp!, {r4-r8,pc}`——从中段进入、没有正常栈帧，
       这个 pop 就会从垃圾栈槽里弹出 pc，于是 pc 变成野地址。
   机制：本处理器把"旧处理器"存进 s_prevMsgHandler 再无条件调用。那个值是
   qInstallMsgHandler 的返回值，在本环境里可能不是合法函数指针；一旦调用，
   就从中段进入别的函数、pop 出野 pc、缺页；而缺页后信号又被平台自己的
   处理器接管，进程不退出、指令重试，形成死循环。
   修法：
     1. 默认**不装**这个处理器（与其它诊断一致，走 /ram/tmp/am_probe 开关），
        日常使用零风险，平台行为完全不被改动；
     2. 不再转发给旧处理器；QtFatalMsg 直接 abort()，与 Qt 默认"致命即终止"
        语义等价，且不再有机会调到野指针。
   ============================================================ */

static void am_qt_msg_handler(QtMsgType type, const char* msg)
{
    if ((type == QtFatalMsg || type == QtWarningMsg) && s_msgQuota > 0 && msg) {
        --s_msgQuota;
        int fd = open("/ram/tmp/amreg.log", O_WRONLY | O_APPEND);
        if (fd >= 0) {
            char comm[64];
            am_comm(comm, (int)sizeof(comm));
            char line[512];
            int n = snprintf(line, sizeof(line), "[qtmsg] pid=%d %s %s: ",
                             (int)getpid(), comm,
                             (type == QtFatalMsg) ? "FATAL" : "WARN");
            if (n > 0) {
                write(fd, line, (unsigned)n);
                write(fd, msg, strlen(msg));
                write(fd, "\n", 1);
            }
            close(fd);
        }
    }
    /* 只对致命消息复现 Qt 默认语义（终止进程）；警告/调试不再转发，
       避免调用 s_prevMsgHandler 这个可能已失效的指针。 */
    if (type == QtFatalMsg)
        abort();
}

__attribute__((constructor)) static void am_auto_qtmsg_install()
{
    if (access("/ram/tmp/am_noqtmsg", F_OK) == 0)
        return;
    /* v59：改为**独立窄开关** /ram/tmp/am_qtmsg，不再挂在 am_probe 上。
       理由：查"装完就退"必须拿到 Qt 的 qWarning 文本，但一开 am_probe 就会
       连带装上 SIGUSR1/2、SIGPIPE、SIGALRM… 一整包诊断处理器，风险面太大。
       现在只开这一个处理器：它只做 open/write，且不再转发失效指针。 */
    if (access("/ram/tmp/am_qtmsg", F_OK) != 0)
        return;
    qInstallMsgHandler(am_qt_msg_handler);
}
