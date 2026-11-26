/* t_reg.cpp - 注册表级 A/B 探针（同一二进制分别加载原厂库与本库运行）
 *
 * 目的：给 v63 修的「应用锁组名兼容性」（BUG-1）做一个**真机可执行**的验收，
 *       同时给 AUDIT_V63 列出的剩余语义差异（createFolder/deleteFolder/modifyApp）
 *       提供同一套 A/B 取证手段。
 *
 * 为什么不用普通 C++ 调用：
 *   设备上没有 Qt 头文件，且**故意不链接我们自己的头文件布局**——
 *   探针只按修饰名调用，任何类布局差异都不会让探针自己崩掉，
 *   这样"用原厂库跑"和"用本库跑"是字面意义上的同一个二进制。
 *
 * 用法：
 *   t_reg w <uuid> <value>        ctor -> setAppLockValue -> dtor
 *   t_reg r <uuid>                ctor -> getAppLockValue
 *   t_reg mkfolder <name> <path>  ctor -> createFolder(name, path, "", "")
 *   t_reg rmfolder <name>         ctor -> deleteFolder(name)
 * 说明：
 *   * 类对象分配在静态大缓冲里（AM_RegistryManager 只用到指针成员，
 *     真身 sizeof 由原厂 ctor 决定，不会越界）。
 *   * 运行期加载哪个 libezxam.so.1 由 LD_LIBRARY_PATH 决定，见 ab_lock.sh。
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

extern "C" {
/* --- Qt 基础 ---
   注意：设备上的 libqte-mt.so.2 并不导出 QUuid::QUuid(QString const&) 与
   QString::~QString（实测链接期 undefined reference），所以探针不走
   "字符串构造 QUuid"的老路，而是直接按 QUuid 的 16 字节布局填字节
   （Qt2: uint data1; ushort data2; ushort data3; uchar data4[8]），
   QUuid::toString 出来的字符串与字符串构造法完全一致。
   QString 只用于 createFolder/deleteFolder，构造后不析构（探针进程即退出）。 */
void qapp_ctor(void*, int*, char**)              __asm__("_ZN12QApplicationC1ERiPPc");
void qstring_ctor(void*, const char*)            __asm__("_ZN7QStringC1EPKc");

/* --- AM_RegistryManager --- */
void reg_ctor(void*, void*, const char*)         __asm__("_ZN18AM_RegistryManagerC1EP7QObjectPKc");
void reg_dtor(void*)                             __asm__("_ZN18AM_RegistryManagerD1Ev");
void reg_set_lock(void*, const void*, int)       __asm__("_ZN18AM_RegistryManager15setAppLockValueERK5QUuidi");
int  reg_get_lock(const void*, const void*)      __asm__("_ZNK18AM_RegistryManager15getAppLockValueERK5QUuid");
bool reg_create_folder(void*, const void*, const void*, const void*, const void*)
    __asm__("_ZN18AM_RegistryManager12createFolderERK7QStringS2_S2_S2_");
void reg_delete_folder(void*, const void*)       __asm__("_ZN18AM_RegistryManager12deleteFolderERK7QString");
void reg_modify_app(void*, const void*, const void*, const void*, const void*)
    __asm__("_ZN18AM_RegistryManager9modifyAppERK7QStringS2_S2_S2_");
}

/* 静态缓冲：布局无关 */
static char g_app[65536];          /* QApplication  */
static char g_reg[4096];          /* AM_RegistryManager 实活对象 */
static char g_qs[8];              /* QString（Qt2 只有一个指针） */
static char g_qu[64];             /* QUuid */

static void mkstr(char* buf, const char* s)
{
    qstring_ctor(buf, s);
}

/* 按 QUuid 的内存布局直接填 16 字节（见文件头说明） */
static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

static int fill_quuid(void* out, const char* s)
{
    unsigned char b[16];
    int n = 0;
    for (const char* p = s; *p && n < 16; ++p) {
        if (*p == '-' || *p == '{' || *p == '}') continue;
        b[n++] = (unsigned char)hexval(*p);
    }
    if (n != 16) return 0;
    unsigned char* o = (unsigned char*)out;
    unsigned int d1 = ((unsigned)b[0] << 24) | ((unsigned)b[1] << 16) |
                      ((unsigned)b[2] << 8) | b[3];
    unsigned short d2 = (unsigned short)((b[4] << 8) | b[5]);
    unsigned short d3 = (unsigned short)((b[6] << 8) | b[7]);
    memcpy(o + 0, &d1, 4);
    memcpy(o + 4, &d2, 2);
    memcpy(o + 6, &d3, 2);
    memcpy(o + 8, b + 8, 8);
    return 1;
}

int main(int argc, char** argv)
{
    if (argc < 3) {
        printf("usage: t_reg w|r <uuid> [value]\n"
               "       t_reg mkfolder <name> <path>\n"
               "       t_reg rmfolder <name>\n");
        return 2;
    }
    const char* mode = argv[1];
    const char* arg1 = argv[2];

    /* 构造 QApplication（am 里始终有一个；缺了它 QCop/ZConfig 可能异常）
       ★实测：在 telnet shell 里直接 new QApplication 会 SIGSEGV
       （Qt/E 的 QApplication 要走 QWS 连接，非 -qws 客户端路径在这个
       精简环境下没有初始化）。用 T_NOAPP=1 跳过，先看注册表本身是否需要它。 */
    if (!getenv("T_NOAPP")) {
        memset(g_app, 0, sizeof(g_app));
        static int ac = 1;
        static char* av[2] = { (char*)"t_reg", 0 };
        printf("[t_reg] QApplication ctor...\n"); fflush(stdout);
        qapp_ctor(g_app, &ac, av);
        printf("[t_reg] QApplication ok\n"); fflush(stdout);
    } else {
        printf("[t_reg] QApplication skipped (T_NOAPP)\n"); fflush(stdout);
    }

    memset(g_reg, 0, sizeof(g_reg));
    printf("[t_reg] AM_RegistryManager ctor...\n"); fflush(stdout);
    reg_ctor(g_reg, 0, 0);
    printf("[t_reg] AM_RegistryManager ok\n"); fflush(stdout);

    int rc = 0;
    if (strcmp(mode, "w") == 0 || strcmp(mode, "r") == 0) {
        memset(g_qu, 0, sizeof(g_qu));
        if (!fill_quuid(g_qu, arg1)) {
            printf("[t_reg] bad uuid: %s\n", arg1);
            return 2;
        }

        if (mode[0] == 'w') {
            int val = (argc > 3) ? atoi(argv[3]) : 1;
            printf("[t_reg] setAppLockValue(%s, %d)\n", arg1, val); fflush(stdout);
            reg_set_lock(g_reg, g_qu, val);
            printf("[t_reg] SET OK\n");
        } else {
            int v = reg_get_lock(g_reg, g_qu);
            printf("[t_reg] GET %s -> %d\n", arg1, v);
        }
    } else if (strcmp(mode, "mkfolder") == 0) {
        static char qs[4][8];
        memset(qs, 0, sizeof(qs));
        mkstr(qs[0], argv[2]);
        mkstr(qs[1], (argc > 3) ? argv[3] : "");
        mkstr(qs[2], "");
        mkstr(qs[3], "");
        bool ok = reg_create_folder(g_reg, qs[0], qs[1], qs[2], qs[3]);
        printf("[t_reg] createFolder(%s,%s) -> %d\n", argv[2],
               (argc > 3) ? argv[3] : "", (int)ok);
    } else if (strcmp(mode, "rmfolder") == 0) {
        memset(g_qs, 0, sizeof(g_qs));
        mkstr(g_qs, arg1);
        printf("[t_reg] deleteFolder(%s)\n", arg1); fflush(stdout);
        reg_delete_folder(g_reg, g_qs);
        printf("[t_reg] DELETE OK\n");
    } else {
        printf("unknown mode %s\n", mode);
        rc = 2;
    }

    reg_dtor(g_reg);
    printf("[t_reg] DONE rc=%d\n", rc);
    return rc;
}
