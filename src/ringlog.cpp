/* ringlog.cpp - 环形日志实现：SIGUSR1 处理器 + 转储 */
#include "ringlog.h"
#include <time.h>   /* v37: time/localtime —— BOOT 分隔行的时间戳 */

RingEnt g_ring[RING_SIZE];
volatile unsigned g_ring_pos = 0;
volatile unsigned g_name_skip = 0;

void ring_dump_file()
{
    FILE* f = fopen("/tmp/ring_dump.txt", "w");
    if (!f) return;
    unsigned start = (g_ring_pos >= RING_SIZE) ? g_ring_pos - RING_SIZE : 0;
    for (unsigned i = start; i < g_ring_pos; ++i) {
        const RingEnt& e = g_ring[i % RING_SIZE];
        fprintf(f, "%u %u %x %x\n", i, e.tag, e.a, e.b);
    }
    fclose(f);
}

static void ring_usr1_handler(int)
{
    ring_dump_file();
    signal(SIGUSR1, ring_usr1_handler);
}

/* crashlog.cpp 提供：SIGSEGV/SIGBUS/FPE/ILL/ABR 现场抓取（寄存器+栈+maps） */
extern void crash_install();

/* v37 构建标签：改版本时同步改这里，日志里一眼能看出跑的是哪版
   v77：release 化 —— AMLOG/AMVLOG 已由 EZXAM_DEBUG=0 编译期消除，
   日志只剩 BOOT 分隔行、崩溃现场与安装互斥修复三类非调试条目。 */
#define EZXAM_BUILD_TAG "v80"

/* v37：进程边界标记。
   之前连续多次 am 重启的日志全混在一个 /ram/tmp/amreg.log 里，无法分辨
   "哪几行属于同一次运行"，导致卸载卡死只能靠猜。
   这里在库首次初始化时写一条带 pid/时间/版本的分隔行，之后每段日志都能归属。 */
static void ring_banner()
{
    FILE* fp = fopen("/ram/tmp/amreg.log", "a");
    if (!fp)
        return;
    static const char* mon[] = { "Jan","Feb","Mar","Apr","May","Jun",
                                 "Jul","Aug","Sep","Oct","Nov","Dec" };
    time_t t = time(0);
    struct tm* tm = localtime(&t);
    fprintf(fp, "\n===== BOOT %s pid=%d %s %2d %02d:%02d:%02d =====\n",
            EZXAM_BUILD_TAG, (int)getpid(),
            (tm && tm->tm_mon < 12) ? mon[tm->tm_mon] : "???",
            tm ? tm->tm_mday : 0,
            tm ? tm->tm_hour : 0, tm ? tm->tm_min : 0, tm ? tm->tm_sec : 0);
    fclose(fp);
}

void ring_install_handler()
{
    static int done = 0;
    if (done) return;
    done = 1;
    ring_banner();
    signal(SIGUSR1, ring_usr1_handler);
    crash_install();
}

/* ============================================================
   v50：修固件 AM_Mutex 的「系统忙」死锁（装第二个软件必现）
   ------------------------------------------------------------
   现象：安装/卸载时报「系统忙」，重启手机后只能成功装**一个**，之后又忙。
   证据链（全部真机实证）：
     1. 全固件只有两个地方弹「系统忙」：libezxaid 的
        AM_QtPackageManager::install(0x42319770) 与 AM_JavaPackageManager::install。
        弹它 ⇔ AM_Mutex::setBusy() 返回 0。
     2. setBusy(0x418726e0) 三步：makeKey→getSem→semop(-1,NOWAIT|SEM_UNDO)；
        任一步失败即 return 0。
     3. getSem(0x41872484)：semget(key,1,IPC_CREAT|IPC_EXCL|0777)
          成功 → semctl SETALL({1}) → 返回 semid
          失败 → **先 printf 诊断信息**，再判断 errno==EEXIST 才 semget(key,1,0) 重试
        ★ 那个 printf 会把 errno 冲掉（实测留下 ESPIPE=29/ENOENT），于是
          `cmp r3,#17; bne → 直接返回 -1`，重试分支形同虚设。
     4. 实测（/ram/sems2 + /ram/mutdl + /ram/tmp/semraw）：
          信号量不存在 → setBusy = 1（成功，新建 semid）
          信号量已存在 → setBusy = 0（失败，且值一直是 1，说明根本没走到 semop）
        删掉信号量后立刻恢复成功。
   ⇒ 固件的行为是：**每个信号量只允许成功 setBusy 一次**，此后永远「系统忙」。
     重启能恢复，是因为 System V 信号量在内核里，重启即清空。

   做法：本库在**每个进程加载时**（constructor，早于用户点安装）检查安装目录
   的互斥信号量；若它存在且值为 1（无人持有 ⇒ 上一次安装已结束），就 IPC_RMID 删掉，
   让下一次 install 走「新建」分支。
   安全性：
     · 只动 1 个信号量、只动 nsems==1 且 val==1 的；
     · val==0 说明有安装正在进行 → 完全不碰；
     · 关闭办法：touch /ram/tmp/am_nomutexfix（ramfs，重启自动消失）
   ============================================================ */
#include <sys/ipc.h>
#include <sys/sem.h>
#include <stdlib.h>
#include <unistd.h>   /* v59: getpid —— [fix] 行要带进程号 */

static const char* amFixQtDir()
{
    const char* e = getenv("EZX_INSTALLED_QT_PATH");
    if (e && *e)
        return e;
    return "/ezxlocal/.system/QTDownLoad/";
}

__attribute__((constructor)) static void am_fix_install_mutex()
{
    if (access("/ram/tmp/am_nomutexfix", F_OK) == 0)
        return;

    key_t key = ftok(amFixQtDir(), 0);
    if (key == (key_t)-1)
        return;

    int id = semget(key, 1, 0);          /* 只查，不建 */
    if (id < 0)
        return;                          /* 不存在 → 首次安装会正常新建，无需处理 */

    struct semid_ds ds;
    memset(&ds, 0, sizeof(ds));
    if (semctl(id, 0, IPC_STAT, &ds) < 0)
        return;
    if (ds.sem_nsems != 1)
        return;                          /* 不是安装互斥用的那一个，别碰 */

    int val = semctl(id, 0, GETVAL);
    if (val != 1)
        return;                          /* 0 = 有安装正在进行（或值异常）→ 不碰 */

    if (semctl(id, 0, IPC_RMID) == 0) {
        FILE* fp = fopen("/ram/tmp/amreg.log", "a");
        if (fp) {
            /* v59：补 pid 与判定依据。原来没有 pid，日志里分不清是哪个进程
               的构造器干的（安装链路上 mystuff/fileexp 会连着起来）。 */
            fprintf(fp, "[fix] pid=%d ipcrm stale install-mutex semid=%d key=0x%08x "
                        "nsems=%d val=%d "
                        "(固件 EEXIST 回退分支失效；删掉后下次 install 会新建)\n",
                    (int)getpid(), id, (unsigned)key,
                    (int)ds.sem_nsems, val);
            fclose(fp);
        }
    }
}
