/* 测试 appbase 的 launchAppWithUidGid 直接启动（无析构调用）。 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* 假析构 stub：qte 不导出 QString/QApplication 析构，测试进程无需真正析构 */
void fake_qstring_dtor(void*) __asm__("_ZN7QStringD1Ev");
void fake_qstring_dtor(void*) {}
void fake_qapp_dtor(void*) __asm__("_ZN12QApplicationD1Ev");
void fake_qapp_dtor(void*) {}

extern "C" {
void qstr_ctor(void* s, const char* latin1) __asm__("_ZN7QStringC1EPKc");
void qapp_ctor(void* s, int* argc, char** argv) __asm__("_ZN12QApplicationC1ERiPPc");
void launch_app_uidgid(void const* path, unsigned uid, unsigned gid)
    __asm__("_ZN20AM_LauncherInterface19launchAppWithUidGidERK7QStringjj");
}

int main(int argc, char** argv)
{
    const char* path = (argc > 1) ? argv[1] : "/usr/SYSqtapp/calculator/calculator";
    printf("QApplication ctor...\n"); fflush(stdout);
    static char qapp[512];
    memset(qapp, 0, sizeof(qapp));
    static int ac = 1;
    static char* av[2] = { (char*)"t_launch", 0 };
    qapp_ctor(qapp, &ac, av);
    printf("launchAppWithUidGid(%s, 2000, 2000)...\n", path); fflush(stdout);
    static char p[256];
    memset(p, 0, sizeof(p));
    qstr_ctor(p, path);
    launch_app_uidgid(p, 2000, 2000);
    printf("returned, sleeping 3s...\n"); fflush(stdout);
    sleep(3);
    printf("DONE\n");
    return 0;
}
