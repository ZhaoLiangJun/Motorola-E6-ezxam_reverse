/* t_refresh.cpp - 向 am 的注册表通道发 RefreshDesktop()，验证 v61 新增的刷新接口
 *
 * 用法: t_refresh [次数]
 * 发送: QCopChannel::send("AM/RegistryiMgr/Channel", "RefreshDesktop()")
 *       第三参数（payload）为空 QByteArray —— 该消息无参数，与原库
 *       slotChannelMessage 里按 channel 名精确匹配的写法一致。
 *
 * 为什么不用普通 C++ 调用：设备上没有 Qt 头文件可用（只有运行时 so），
 * 所以和 t_qcop.cpp 一样直接按修饰名调用 Qt 的构造函数/send。
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

extern "C" {
void qapp_ctor(void* s, int* argc, char** argv) __asm__("_ZN12QApplicationC1ERiPPc");
void qgarray_def_ctor(void*) __asm__("_ZN7QGArrayC1Ev");
void qcstring_ctor(void*, const char*) __asm__("_ZN8QCStringC1EPKc");
int  qcop_send(const void*, const void*, const void*)
    __asm__("_ZN11QCopChannel4sendERK8QCStringS2_RK6QArrayIcE");
}

int main(int argc, char** argv)
{
    int n = (argc > 1) ? atoi(argv[1]) : 1;
    if (n < 1) n = 1;
    if (n > 20) n = 20;

    static char qapp[4096];
    memset(qapp, 0, sizeof(qapp));
    static int ac = 1;
    static char* av[2] = { (char*)"t_refresh", 0 };
    printf("QApplication ctor...\n"); fflush(stdout);
    qapp_ctor(qapp, &ac, av);
    printf("ok\n"); fflush(stdout);

    for (int i = 0; i < n; i++) {
        static char ch[64], msg[64], arr[64];
        memset(ch, 0, 64);
        memset(msg, 0, 64);
        memset(arr, 0, 64);
        qcstring_ctor(ch, "AM/RegistryiMgr/Channel");
        qcstring_ctor(msg, "RefreshDesktop()");
        qgarray_def_ctor(arr);          /* 空 payload */
        int ok = qcop_send(ch, msg, arr);
        printf("send #%d -> %d\n", i + 1, ok); fflush(stdout);
        if (i + 1 < n) sleep(3);
    }
    printf("DONE\n");
    return 0;
}
