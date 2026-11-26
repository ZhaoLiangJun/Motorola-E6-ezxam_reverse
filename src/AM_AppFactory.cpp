/* AM_AppFactory.cpp - 逆向重建自 libezxam.so.1.0.0 (Motorola E6)
   createAppObject 依据 operator_new 的 sizeof 分配对应派生类：
     Type=1 Folder       -> new 0x58  AM_Folder
     Type=2 Application  -> new 0x84  AM_AppLnk
     Type=3 EmbeddedLink -> new 0x58  AM_EmbeddedLink
     Type=4 Shortcut     -> new 0x5C  AM_Shortcut
     其余 -> 返回 NULL
*/
#include <AM_AppFactory.h>
#include <AM_Folder.h>
#include <AM_AppLnk.h>
#include <AM_EmbeddedLink.h>
#include <AM_Shortcut.h>

/* ============ 构造函数 ============ */
AM_AppFactory::AM_AppFactory()
{
}

/* ============ 析构函数 ============ */
AM_AppFactory::~AM_AppFactory()
{
}

/* ============ createAppObject ============ */
AM_AppObject* AM_AppFactory::createAppObject(AM_AppObject::Type type)
{
    AM_AppObject* obj = 0;
    switch (type) {
    case AM_AppObject::Folder:         /* 1 -> 0x58 */
        obj = new AM_Folder();
        break;
    case AM_AppObject::Application:    /* 2 -> 0x84 */
        obj = new AM_AppLnk();
        break;
    case AM_AppObject::EmbeddedLink:   /* 3 -> 0x58 */
        obj = new AM_EmbeddedLink();
        break;
    case AM_AppObject::Shortcut:       /* 4 -> 0x5C */
        obj = new AM_Shortcut();
        break;
    default:
        return 0;
    }
    return obj;
}
