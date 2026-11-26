/* mutex_test.c —— 完整复现 AM_Mutex::setBusy() 的调用序列并打印结果
   目的：判定 AM_QtPackageManager::install 里那个 AM_Global::systemBusyMessageBox()
         到底会不会被触发。

   libezxappbase 反汇编（AM_Mutex::setBusy @0x418726e0）：
     key   = ftok(AM_Global::getInstalledQtDir(1).latin1(), 0)        // proj_id=0
     semid = semget(key, 1, IPC_CREAT|0666)
     if (semid == -1 && errno == EEXIST) semid = semget(key, 1, 0)
     if (semid == -1) return false
     if (semop(semid, {-1, IPC_NOWAIT|SEM_UNDO}) == -1) { perror(); return false; }
     return true
   注意：新建的信号量内核初值是 0，若没有任何地方 SETALL(1)，
         setop(-1) 必然一直失败 —— 那就是“永远系统忙”。
*/
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/sem.h>
#include <sys/types.h>
#include <sys/ipc.h>

union semun {
    int val;
    struct semid_ds* buf;
    unsigned short* array;
};

static int try_key(const char* path, int proj)
{
    key_t key = ftok(path, proj);
    if (key == -1) { printf("  ftok(%s,%d) failed: %s\n", path, proj, strerror(errno)); return -1; }

    int semid = semget(key, 1, IPC_CREAT | 0666);
    int created = 1;
    if (semid == -1 && errno == EEXIST) { created = 0; semid = semget(key, 1, 0); }
    if (semid == -1) { printf("  semget failed: %s\n", strerror(errno)); return -1; }

    union semun u; u.val = 0;
    int val = semctl(semid, 0, GETVAL, u);
    printf("  path=%s proj=%d key=0x%08x semid=%d %s val=%d\n",
           path, proj, (unsigned)key, semid, created ? "(NEW)" : "(exist)", val);

    struct sembuf op;
    op.sem_num = 0;
    op.sem_op = -1;
    op.sem_flg = IPC_NOWAIT | SEM_UNDO;
    if (semop(semid, &op, 1) == -1) {
        printf("  >>> setBusy FAILED (errno=%d %s) —— 这里就会弹“系统忙”\n",
               errno, strerror(errno));
        return 0;
    }
    printf("  >>> setBusy OK\n");
    u.val = 0;
    printf("  after acquire val=%d\n", semctl(semid, 0, GETVAL, u));
    op.sem_op = 1;
    op.sem_flg = SEM_UNDO;      /* 与 setNotBusy 对等，恢复原值 */
    semop(semid, &op, 1);
    u.val = 0;
    printf("  after release val=%d\n", semctl(semid, 0, GETVAL, u));
    return 1;
}

int main(void)
{
    printf("AM_Mutex::setBusy() 复现\n");
    printf("[1] Flash 安装目录（install 里 device 硬编码为 1）\n");
    try_key("/ezxlocal/.system/QTDownLoad/", 0);
    printf("[2] 卡安装目录（备用）\n");
    try_key("/mmc/mmca1/.system/QTDownLoad/", 0);
    return 0;
}
