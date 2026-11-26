/* t_runas.c - 以 ezx(2000:233) 身份 exec 目标程序，模拟 am 的 launchAppWithUidGid
 * 用法: t_runas <program> [args...]
 */
#include <stdio.h>
#include <unistd.h>
#include <grp.h>
#include <errno.h>
#include <string.h>

int main(int argc, char** argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: t_runas <program> [args...]\n");
        return 2;
    }
    if (setgroups(0, 0) != 0)
        fprintf(stderr, "setgroups: %s\n", strerror(errno));
    if (setgid(233) != 0)
        fprintf(stderr, "setgid: %s\n", strerror(errno));
    if (setuid(2000) != 0)
        fprintf(stderr, "setuid: %s\n", strerror(errno));
    execv(argv[1], argv + 1);
    fprintf(stderr, "execv %s: %s\n", argv[1], strerror(errno));
    return 1;
}
