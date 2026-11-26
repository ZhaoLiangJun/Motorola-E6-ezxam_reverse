/* sems2.c —— 深挖 AM_Mutex
   1) 打印所有 SysV 信号量的 key/semid/nsems/val/otime/ctime（otime = 最后一次 semop 时间）
   2) 对候选目录做 ftok(path, proj) 反算 key，定位 0x61080237 到底对应哪个路径
   3) 可选：-r <semid> 把该信号量 +1（释放一次被泄漏的占用），用于验证“系统忙”是否是它引起

   AM_Mutex 的实现（libezxappbase AM_Mutex::setBusy @0x418726e0）：
     semid = ftok(AM_Global::getInstalledQtDir(device), proj) → semget(...,1,0)
     semop(semid, {-1, IPC_NOWAIT|SEM_UNDO})   // 0x1800
     失败 → 调用方弹 AM_Global::systemBusyMessageBox()
*/
#include <stdio.h>
#include <string.h>
#include <sys/sem.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>

union semun {
    int val;
    struct semid_ds* buf;
    unsigned short* array;
};

static unsigned dev_inode_key(const char* path, int proj)
{
    struct stat st;
    if (stat(path, &st) != 0) return 0xffffffffu;
    unsigned k = ((unsigned)(proj & 0xff) << 24)
               | ((unsigned)(st.st_dev & 0xff) << 16)
               | ((unsigned)(st.st_ino & 0xffff));
    return k;
}

int main(int argc, char** argv)
{
    /* ---- 可选：删除指定 semid（清理测试残留） ---- */
    if (argc >= 3 && strcmp(argv[1], "-d") == 0) {
        int id = atoi(argv[2]);
        union semun u; u.val = 0;
        if (semctl(id, 0, IPC_RMID, u) != 0) { perror("semctl RMID"); return 1; }
        printf("removed semid=%d\n", id);
        return 0;
    }

    /* ---- 可选：释放指定 semid ---- */
    if (argc >= 3 && strcmp(argv[1], "-r") == 0) {
        int id = atoi(argv[2]);
        struct sembuf op;
        op.sem_num = 0;
        op.sem_op = 1;
        op.sem_flg = 0;
        if (semop(id, &op, 1) != 0) { perror("semop +1"); return 1; }
        union semun u; u.val = 0;
        printf("released semid=%d, now val=%d\n", id, semctl(id, 0, GETVAL, u));
        return 0;
    }

    /* ---- 列出信号量 ---- */
    FILE* f = fopen("/proc/sysvipc/sem", "r");
    char line[512];
    if (!f) { perror("open"); return 1; }
    if (!fgets(line, sizeof(line), f)) return 1;
    printf("%-12s %7s %5s %6s %16s %16s\n", "key", "semid", "nsems", "val0", "otime", "ctime");
    while (fgets(line, sizeof(line), f)) {
        int key = 0, id = 0, perms = 0, nsems = 0;
        unsigned uid = 0, gid = 0, cu = 0, cg = 0;
        unsigned long ot = 0, ct = 0;
        int n = sscanf(line, "%d %d %o %d %u %u %u %u %lu %lu",
                       &key, &id, &perms, &nsems, &uid, &gid, &cu, &cg, &ot, &ct);
        if (n < 4) continue;
        union semun u; u.val = 0;
        int v = semctl(id, 0, GETVAL, u);
        printf("0x%08x %7d %5d %6d %16lu %16lu", key, id, nsems, v, ot, ct);
        if (nsems > 1) {
            int i;
            printf("  vals=");
            for (i = 0; i < nsems && i < 8; i++) printf("%d,", semctl(id, i, GETVAL, u));
        }
        printf("\n");
    }
    fclose(f);

    /* ---- 候选路径 ftok 反算 ---- */
    if (argc >= 2 && strcmp(argv[1], "-q") == 0) return 0;
    const char* paths[] = {
        "/ezxlocal/.system/QTDownLoad/",
        "/ezxlocal/.system/QTDownLoad",
        "/ezxlocal/.system/",
        "/ezxlocal/.system",
        "/ezxlocal/",
        "/ezxlocal",
        "/ezx_user/download/appwrite/",
        "/mmc/mmca1/.system/QTDownLoad/",
        "/mmc/mmca1/.system/QTDownLoad",
        "/mmc/mmca1/.system/",
        "/mmc/mmca1/",
        "/usr/SYSqtapp/am/",
        "/usr/SYSqtapp/am",
        "/usr/data_resource/preloadapps",
        "/ezxlocal/download/appwrite/",
        "/ram/",
        "/tmp/",
        "/",
        0
    };
    printf("\n%-38s %16s %10s %10s\n", "path", "st_dev", "st_ino", "devInode");
    int i;
    for (i = 0; paths[i]; i++) {
        struct stat st;
        if (stat(paths[i], &st) != 0) { printf("%-38s (miss)\n", paths[i]); continue; }
        unsigned k61 = dev_inode_key(paths[i], 0x61);
        printf("%-38s %16lx %10lu   ftok('a')=0x%08x %s\n",
               paths[i], (unsigned long)st.st_dev, (unsigned long)st.st_ino, k61,
               (k61 == 0x61080237u) ? "<<< MATCH" : "");
    }
    (void)argv;
    return 0;
}
