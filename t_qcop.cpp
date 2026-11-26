/* t_qcop.cpp - 通过 QCopChannel 自主触发 am 启动应用（复现二次启动失效）
 * 用法: t_qcop <uuid> [次数] [消息名(默认launchApp)]
 * 序列化格式与 appbase launchApp(QUuid,bool) 一致: BE data1,data2,data3,data4[8]
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

extern "C" {
void qapp_ctor(void* s, int* argc, char** argv) __asm__("_ZN12QApplicationC1ERiPPc");
void qgarray_def_ctor(void*) __asm__("_ZN7QGArrayC1Ev");
void qgarray_store(void*, const char*, unsigned) __asm__("_ZN7QGArray5storeEPKcj");
void qcstring_ctor(void*, const char*) __asm__("_ZN8QCStringC1EPKc");
int  qcop_send(const void*, const void*, const void*)
    __asm__("_ZN11QCopChannel4sendERK8QCStringS2_RK6QArrayIcE");
}

int main(int argc, char** argv)
{
    if (argc < 2) { printf("usage: t_qcop <uuid> [n] [msg]\n"); return 1; }
    const char* uuid = argv[1];
    int n = (argc > 2) ? atoi(argv[2]) : 1;
    const char* msgname = (argc > 3) ? argv[3] : "launchApp";

    static char qapp[4096];
    memset(qapp, 0, sizeof(qapp));
    static int ac = 1;
    static char* av[2] = { (char*)"t_qcop", 0 };
    printf("QApplication ctor...\n"); fflush(stdout);
    qapp_ctor(qapp, &ac, av);
    printf("ok\n"); fflush(stdout);

    unsigned d1; unsigned short d2, d3;
    unsigned int b[8];
    if (sscanf(uuid, "%08x-%04hx-%04hx-%02x%02x%02x%02x%02x%02x%02x%02x",
               &d1, &d2, &d3, &b[0], &b[1], &b[2], &b[3],
               &b[4], &b[5], &b[6], &b[7]) != 11) {
        printf("bad uuid\n"); return 1;
    }
    unsigned char pay[16];
    pay[0] = d1 >> 24; pay[1] = d1 >> 16; pay[2] = d1 >> 8; pay[3] = d1;
    pay[4] = d2 >> 8;  pay[5] = d2;
    pay[6] = d3 >> 8;  pay[7] = d3;
    for (int i = 0; i < 8; i++) pay[8 + i] = (unsigned char)b[i];

    for (int i = 0; i < n; i++) {
        static char ch[64], msg[64], arr[64];
        memset(ch, 0, 64); memset(msg, 0, 64); memset(arr, 0, 64);
        qcstring_ctor(ch, "/AM/Launcher");
        qcstring_ctor(msg, msgname);
        qgarray_def_ctor(arr);
        qgarray_store(arr, (const char*)pay, 16);
        int ok = qcop_send(ch, msg, arr);
        printf("send #%d -> %d\n", i + 1, ok); fflush(stdout);
        sleep(10);
    }
    printf("DONE\n");
    return 0;
}
