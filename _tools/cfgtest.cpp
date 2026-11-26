/* cfgtest.cpp - 打印 getConfigFileName 对 12 种 RegFileType 返回的真实路径
   （安装链路 ezxaid 会用它的返回值去读写注册表；返回错路径 = 写坏数据库） */
#include <qstring.h>
#include <qapplication.h>
#include <stdio.h>
#include <AM_RegistryManager.h>

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    AM_RegistryManager mgr(0, "cfgtest");
    for (int i = 0; i < 12; ++i) {
        QString s = mgr.getConfigFileName((AM_RegistryManager::RegFileType)i);
        printf("  type %2d -> '%s'\n", i, s.latin1());
        fflush(stdout);
    }
    printf("[cfgtest] done\n");
    return 0;
}
