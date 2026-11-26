/* 决定性对照实验 v4：直接写 AM_AppLnk 成员字段（布局已逆向确认），
   调原库 getExecFullPath 验证真实拼接格式。无需 QMap/createObject。
   成员：0x34=FDir(QString) 0x54=m_execPath(QString) 0x0C=slot 0x44=dev */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" {
void qstr_ctor(void* s, const char* latin1) __asm__("_ZN7QStringC1EPKc");
void qstr_dtor(void* s) __asm__("_ZN7QStringD1Ev");
const char* qstr_utf8(void const* s) __asm__("_ZNK7QString6latin1Ev");
void applnk_ctor(void* self) __asm__("_ZN9AM_AppLnkC2Ev");
void applnk_getExecFullPath(void* ret, void const* self)
    __asm__("_ZNK9AM_AppLnk15getExecFullPathEv");
void objobj_getPath(void* ret, void const* self)
    __asm__("_ZNK12AM_AppObject7getPathEv");
void objobj_getSize(void const* self)
    __asm__("_ZN12AM_AppObject7getSizeEv");
}

/* 字段偏移（AM_AppLnk 布局，逆向已证实） */
#define OFF_DIR    0x34   /* FDir (QString, 4字节共享指针) */
#define OFF_EXECP  0x54   /* m_execPath (QString) */
#define OFF_SLOT   0x0C
#define OFF_DEV    0x44

static void setStr(char* obj, int off, const char* latin1)
{
    char* s = obj + off;
    memset(s, 0, 8);
    qstr_ctor(s, latin1);
}

static void run_case(int slot, int dev,
                     const char* dir, const char* exec)
{
    printf("step1 malloc\n"); fflush(stdout);
    char* obj = (char*)malloc(1024);
    memset(obj, 0, 1024);
    printf("step2 ctor\n"); fflush(stdout);
    applnk_ctor(obj);
    printf("step3 setStr\n"); fflush(stdout);
    setStr(obj, OFF_DIR, dir);
    setStr(obj, OFF_EXECP, exec);
    printf("step4 call\n"); fflush(stdout);
    *(int*)(obj + OFF_SLOT) = slot;
    *(int*)(obj + OFF_DEV) = dev;
    char ret[16];
    memset(ret, 0, sizeof(ret));
    applnk_getExecFullPath(ret, obj);
    printf("step5 done\n"); fflush(stdout);
    const char* u = qstr_utf8(ret);
    printf("  execFull: slot=%d dev=%d dir=%s exec=%s -> [%s]\n", slot, dev, dir, exec, u ? u : "(null)");
    qstr_dtor(ret);
    char ret2[16];
    memset(ret2, 0, sizeof(ret2));
    objobj_getPath(ret2, obj);
    const char* u2 = qstr_utf8(ret2);
    printf("  getPath : [%s]\n", u2 ? u2 : "(null)");
    objobj_getSize(obj);   /* getSize 返回 int，sret 特殊处理——这里跳过 */
    printf("  getSize : (skipped)\n");
    qstr_dtor(ret2);
    qstr_dtor(obj + OFF_DIR);
    qstr_dtor(obj + OFF_EXECP);
    free(obj);
}

int main()
{
    printf("=== 原库 getExecFullPath 对照 ===\n");
    run_case(1, 1, "addressbook", "addrbk");
    run_case(4, 2, "SoftManage2", "SoftManage");
    run_case(2, 1, "tools", "toolapp");
    run_case(3, 1, "javas", "javaapp");
    printf("DONE\n");
    return 0;
}
