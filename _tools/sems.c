/* sems.c —— 列出设备上所有 SysV 信号量及其当前值
   用途：确认 AM_Mutex 的“忙”信号量是否卡在 0（卡 0 = 永久“系统忙”）。
   dev=0 的 AM_Mutex 抢占失败是 AM_QtPackageManager::install 里
   systemBusyMessageBox() 的唯一触发条件。 */
#include <stdio.h>
#include <sys/sem.h>
#include <sys/types.h>

union semun {
    int val;
    struct semid_ds* buf;
    unsigned short* array;
};

int main(void)
{
    FILE* f = fopen("/proc/sysvipc/sem", "r");
    char line[512];
    if (!f) { perror("open /proc/sysvipc/sem"); return 1; }
    if (!fgets(line, sizeof(line), f)) { printf("empty\n"); return 1; }
    printf("key      semid  nsems  val...\n");
    while (fgets(line, sizeof(line), f)) {
        int key = 0, id = 0, perms = 0, nsems = 0;
        unsigned uid = 0, gid = 0, cu = 0, cg = 0;
        unsigned long ot = 0, ct = 0;
        int n = sscanf(line, "%d %d %o %d %u %u %u %u %lu %lu",
                       &key, &id, &perms, &nsems, &uid, &gid, &cu, &cg, &ot, &ct);
        if (n < 4) continue;
        printf("0x%08x %6d %5d  ", key, id, nsems);
        {
            int i;
            for (i = 0; i < nsems && i < 8; i++) {
                union semun u;
                u.val = 0;
                printf("%d ", semctl(id, i, GETVAL, u));
            }
        }
        printf("(uid=%u)\n", uid);
    }
    fclose(f);
    return 0;
}
