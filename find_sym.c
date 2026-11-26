/* find_sym.c - 在若干 .so 中查找某个符号由谁提供
 *   find_sym <symbol> <so1> [so2 ...]
 */
#include <stdio.h>
#include <dlfcn.h>

int main(int argc, char **argv)
{
    int i;
    if (argc < 3) { fprintf(stderr, "usage: find_sym <sym> <so...>\n"); return 1; }
    for (i = 2; i < argc; i++) {
        void *h = dlopen(argv[i], RTLD_NOW);
        if (!h) { printf("  [dlopen-fail] %s : %s\n", argv[i], dlerror()); continue; }
        void *p = dlsym(h, argv[1]);
        printf("%s %s %s\n", p ? "FOUND" : "     ", argv[i], p ? "" : "");
        dlclose(h);
    }
    return 0;
}
