/*
libezxam.so.1.0.0  逆向重建
createAppObject 证据（operator_new 的 sizeof）：
  Type=Folder(1)       -> new 0x58  -> AM_Folder
  Type=Application(2)  -> new 0x84  -> AM_AppLnk
  Type=EmbeddedLink(3) -> new 0x58  -> AM_EmbeddedLink
  Type=Shortcut(4)     -> new 0x5C  -> AM_Shortcut
  其余 -> 返回 NULL
*/
#ifndef AM_APPFACTORY
#define AM_APPFACTORY

#include <AM_AppObject.h>

class AM_AppFactory
{
public :
  AM_AppFactory();
  ~AM_AppFactory();

  AM_AppObject* createAppObject(AM_AppObject::Type type);

private :
  /* 无成员（sizeof 极小，反编译未见成员访问）*/
};

#endif
