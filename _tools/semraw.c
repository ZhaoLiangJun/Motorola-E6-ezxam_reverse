/* semraw.c - 直接对 0x00090022 做 semget/semctl/semop，打印 uid/gid/mode/val/errno */
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/sem.h>

int main(int argc, char** argv)
{
    key_t k = (key_t)0x00090022;
    if (argc > 1) k = (key_t)strtoul(argv[1], 0, 0);
    printf("uid=%d euid=%d key=0x%08x\n", (int)getuid(), (int)geteuid(), (unsigned)k);

    errno = 0;
    int id = semget(k, 1, 0);
    printf("semget(lookup) = %d errno=%d(%s)\n", id, errno, strerror(errno));
    if (id < 0) return 1;

    struct semid_ds ds;
    memset(&ds, 0, sizeof(ds));
    errno = 0;
    int r = semctl(id, 0, IPC_STAT, &ds);
    printf("IPC_STAT r=%d errno=%d uid=%d gid=%d cuid=%d cgid=%d mode=0%o nsems=%d\n",
           r, errno, (int)ds.sem_perm.uid, (int)ds.sem_perm.gid,
           (int)ds.sem_perm.cuid, (int)ds.sem_perm.cgid,
           ds.sem_perm.mode, (int)ds.sem_nsems);
    printf("  val[0]=%d GETPID=%d GETNCNT=%d GETZCNT=%d\n",
           semctl(id, 0, GETVAL),
           semctl(id, 0, GETPID),
           semctl(id, 0, GETNCNT),
           semctl(id, 0, GETZCNT));

    struct sembuf sb;
    sb.sem_num = 0; sb.sem_op = -1; sb.sem_flg = IPC_NOWAIT | SEM_UNDO;
    errno = 0;
    r = semop(id, &sb, 1);
    printf("lock  r=%d errno=%d(%s) val=%d\n", r, errno, strerror(errno), semctl(id,0,GETVAL));

    sb.sem_op = 1; errno = 0;
    r = semop(id, &sb, 1);
    printf("unlock r=%d errno=%d(%s) val=%d\n", r, errno, strerror(errno), semctl(id,0,GETVAL));
    return 0;
}
