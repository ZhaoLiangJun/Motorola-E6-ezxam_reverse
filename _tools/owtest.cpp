/* owtest.cpp - 复现 libezxmystuff_album 对 libezxam 的全部用法：
   new AM_RegistryManager(QObject*,char*) -> getOpenWithApps(path,list) -> ~AM_RegistryManager
   （符号实证：album 只从 libezxam 取这 3 个符号） */
#include <qstring.h>
#include <qapplication.h>
#include <stdio.h>
#include <AM_RegistryManager.h>

int main(int argc, char** argv)
{
    const char* p = (argc > 1) ? argv[1] : "/mmc/mmca1/eclock-0.2.mpkg";
    /* mystuff 是真 QWS 客户端，必须同样有 QApplication，否则 QCopChannel::send 会 qFatal */
    QApplication app(argc, argv);
    printf("[owtest] before ctor\n"); fflush(stdout);

    AM_RegistryManager mgr(0, "owtest");
    printf("[owtest] ctor ok, path=%s\n", p); fflush(stdout);

    QList<AM_RegistryManager::OpenWithApp> list;
    mgr.getOpenWithApps(QString(p), list);
    int n = (int)list.count();
    printf("[owtest] getOpenWithApps -> %d\n", n); fflush(stdout);

    for (int i = 0; i < n; ++i) {
        const AM_RegistryManager::OpenWithApp* a = list.at(i);
        printf("  [%d] name='%s'\n", i, a ? a->name.latin1() : "(null)");
    }
    printf("[owtest] dtor...\n"); fflush(stdout);
    return 0;
}
