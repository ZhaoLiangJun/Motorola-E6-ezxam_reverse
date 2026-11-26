/* sectest.cpp - 在真机上验证 AM_SecurityManager::verifySecurityByInstall 是否
   把 mpkg 的 Name/Vendor/Comment/Size/Version 解析进对象成员（v49 修复验证） */
#include <qstring.h>
#include <stdio.h>
#include <AM_SecurityManager.h>

int main(int argc, char** argv)
{
    const char* dir = (argc > 1) ? argv[1] : "/ram/tmp/ek/eclock";
    printf("[sectest] dir=%s\n", dir);

    AM_SecurityManager sm;
    QString sig;
    int r = sm.verifySecurityByInstall(QString(dir), sig);

    const unsigned char* p = (const unsigned char*)&sm;
    const QString* name = (const QString*)(p + 0x10);
    const QString* vend = (const QString*)(p + 0x14);
    const QString* comm = (const QString*)(p + 0x18);
    const int*     size = (const int*)(p + 0x1c);
    const QString* ver  = (const QString*)(p + 0x20);

    printf("[sectest] ret=%d sig='%s'\n", r, sig.latin1());
    printf("[sectest] Name='%s'\n", name->latin1());
    printf("[sectest] Vendor='%s'\n", vend->latin1());
    printf("[sectest] Comment='%s'\n", comm->latin1());
    printf("[sectest] Size=%d\n", *size);
    printf("[sectest] Version='%s'\n", ver->latin1());
    fflush(stdout);
    return 0;
}
