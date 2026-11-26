/* t_dlopen.c - 验证 libezxam 副本能否被独立进程正常加载（RTLD_NOW）
 * 用途：应用进程启动即静默死亡时，判断是否我们的库在加载/重定位阶段就失败。
 * 用法：t_dlopen /mmc/mmca1/v23
 */
#include <stdio.h>
#include <dlfcn.h>

int main(int argc, char** argv)
{
    const char* p = (argc > 1) ? argv[1] : "/usr/lib/ezx/lib/libezxam.so.1";
    printf("dlopen(%s) ...\n", p);
    fflush(stdout);
    void* h = dlopen(p, RTLD_NOW);
    if (!h) {
        printf("FAIL: %s\n", dlerror());
        return 1;
    }
    printf("OK handle=%p\n", h);
    const char* syms[] = {
        "_ZN18AM_RegistryManager12initRegistryEv",
        "_ZNK9AM_AppLnk15getExecFullPathEv",
        "_ZNK12AM_AppObject7getNameEv",
        0
    };
    int i;
    for (i = 0; syms[i]; ++i) {
        void* s = dlsym(h, syms[i]);
        printf("  dlsym %s -> %p%s\n", syms[i], s, s ? "" : " (MISSING)");
    }
    dlclose(h);
    printf("done\n");
    return 0;
}
