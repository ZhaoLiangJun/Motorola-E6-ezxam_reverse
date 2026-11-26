/* amlaunch.c - am 启动器：重定向输出 + 注入 LD_PRELOAD + 记录退出码，再 exec 真正的 am
 * 交叉编译：arm-linux-gcc -O1 -o am amlaunch.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>

#define REAL_AM   "/ram/tmp/amdir/am.real"
#define PRELOAD   "/ram/tmp/libcrashdump.so"
#define ARGV0     "/usr/SYSqtapp/am/am"
#define ERRLOG    "/ram/tmp/am.err"
#define EXITLOG   "/ram/tmp/am.exit"

int
main(int argc, char **argv)
{
    char **nargv;
    int i, fd;
    pid_t pid;
    int st = 0;

    nargv = (char **) malloc(sizeof(char *) * (argc + 2));
    if (!nargv)
        return 127;
    nargv[0] = (char *) ARGV0;
    for (i = 1; i < argc; i++)
        nargv[i] = argv[i];
    nargv[argc] = NULL;

    pid = fork();
    if (pid == 0) {
        fd = open(ERRLOG, O_WRONLY | O_CREAT | O_APPEND, 0666);
        if (fd >= 0) {
            dup2(fd, 1);
            dup2(fd, 2);
            if (fd > 2)
                close(fd);
        }
        setenv("LD_PRELOAD", PRELOAD, 1);
        execv(REAL_AM, nargv);
        unsetenv("LD_PRELOAD");
        execv(REAL_AM, nargv);
        _exit(127);
    }

    if (pid < 0)
        return 127;

    waitpid(pid, &st, 0);

    fd = open(EXITLOG, O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (fd >= 0) {
        char b[128];
        int n;
        if (WIFSIGNALED(st))
            n = sprintf(b, "am pid=%d KILLED sig=%d\n", (int) pid, WTERMSIG(st));
        else
            n = sprintf(b, "am pid=%d exit code=%d\n", (int) pid, WEXITSTATUS(st));
        write(fd, b, n);
        close(fd);
    }
    return 0;
}
