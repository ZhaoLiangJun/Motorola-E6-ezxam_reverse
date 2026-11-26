/* mutex_dl.c —— 直接调用设备上 libezxappbase 的 AM_Mutex::setBusy()
   目标：证明“系统忙”是 setBusy() 返回 false 引起，并查出到底哪个 syscall 失败。

   反汇编依据（libezxappbase）：
     AM_Mutex::setBusy()  @0x418726e0
       key   = AM_Mutex::makeKey()          @0x418725b0  → ftok(getInstalledQtDir(1), 0)
       semid = AM_Mutex::getSem(key)        @0x41872484  → semget(key,1,0x7ff) + SETALL(1)
       semop(semid, -1, IPC_NOWAIT|SEM_UNDO) == 0 ? true : false
   因此失败只可能来自三处：key==-1、getSem<0、semop 报错。errno 能直接区分。
*/
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <dlfcn.h>
#include <sys/sem.h>
#include <sys/types.h>
#include <sys/ipc.h>

union semun {
    int val;
    struct semid_ds* buf;
    unsigned short* array;
};

/* 观测：直接用同一路径算 key，读该信号量当前值 */
static void peek(const char* path, int proj)
{
    key_t key = ftok(path, proj);
    if (key == -1) { printf("  [peek] ftok(%s,%d) failed\n", path, proj); return; }
    int semid = semget(key, 1, 0);
    if (semid == -1) { printf("  [peek] key=0x%08x sem 不存在\n", (unsigned)key); return; }
    union semun u; u.val = 0;
    int v = semctl(semid, 0, GETVAL, u);
    printf("  [peek] key=0x%08x semid=%d val=%d %s\n",
           (unsigned)key, semid, v, v == 0 ? "<== 0 就是“忙”" : "");
}

typedef int (*fn_setbusy)(void);
typedef int (*fn_setnotbusy)(void);
typedef void (*fn_getdir)(void* ret, int dev);

/* Qt2 QStringData：+0xc = len（本项目已实测），UCS2 数据在 +0x10 */
static void dump_qstring(const char* tag, unsigned long ptr)
{
    if (!ptr) { printf("  %s = (null QString)\n", tag); return; }
    unsigned* d = (unsigned*)ptr;
    unsigned len = d[3];
    printf("  %s len=%u text=\"", tag, len);
    if (len > 0 && len < 256) {
        unsigned short* u = (unsigned short*)((char*)d + 0x10);
        unsigned i;
        for (i = 0; i < len; i++) putchar(u[i] < 128 ? (char)u[i] : '?');
    } else {
        printf("<len 异常>");
    }
    printf("\"\n");
}

int main(void)
{
    void* h = dlopen("libezxappbase.so.1", RTLD_LAZY);
    if (!h) { printf("dlopen appbase: %s\n", dlerror()); return 1; }

    fn_getdir getdir = (fn_getdir)dlsym(h, "_ZN9AM_Global17getInstalledQtDirENS_6DeviceE");
    if (getdir) {
        int dev;
        for (dev = 1; dev <= 2; dev++) {
            unsigned long qs[2];
            qs[0] = 0; qs[1] = 0;
            getdir(qs, dev);
            char tag[64];
            sprintf(tag, "getInstalledQtDir(dev=%d)", dev);
            dump_qstring(tag, qs[0]);
        }
    }

    fn_setbusy sb = (fn_setbusy)dlsym(h, "_ZN8AM_Mutex7setBusyEv");
    fn_setnotbusy sn = (fn_setnotbusy)dlsym(h, "_ZN8AM_Mutex10setNotBusyEv");
    if (!sb) { printf("dlsym setBusy failed\n"); return 1; }

    printf("--- 观测安装目录信号量 ---\n");
    peek("/ezxlocal/.system/QTDownLoad/", 0);

    errno = 0;
    int r1 = sb();
    int e1 = errno;
    printf("setBusy#1 = %d  errno=%d (%s)\n", r1, e1, strerror(e1));
    peek("/ezxlocal/.system/QTDownLoad/", 0);

    errno = 0;
    int r2 = sb();
    int e2 = errno;
    printf("setBusy#2 = %d  errno=%d (%s)\n", r2, e2, strerror(e2));

    if (sn) {
        if (r1) sn();
        if (r2) sn();
    }
    errno = 0;
    int r3 = sb();
    printf("setBusy#3(释放后) = %d  errno=%d (%s)\n", r3, errno, strerror(errno));
    if (r3 && sn) sn();
    return 0;
}
