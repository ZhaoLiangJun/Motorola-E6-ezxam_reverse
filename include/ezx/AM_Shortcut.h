/*
libezxam.so.1.0.0  逆向重建（成员布局依据 Ghidra 反编译 + 虚表分析）
虚表证据：AM_Shortcut : public AM_AppObject
  slot[0..3] 指向基类 AppObject::getAppLockEnabled/setAppLockEnabled/getAppLockValue/setAppLockValue
  slot[4]    = AM_Shortcut::dump (与 EmbeddedLink 共享实现 0x41af4c24)
  注意：AM_Shortcut::getAppLockValue/setAppLockValue（访问 this+0x58）是【非虚 name-hiding】函数，
        不在虚表内；虚表中的锁函数仍走基类实现。
*/
#ifndef AM_SHORTCUT
#define AM_SHORTCUT

#include <qmap.h>
#include <AM_AppObject.h>
#include <AM_LauncherInterface.h>

class AM_Shortcut : public AM_AppObject
{
public :
  AM_Shortcut();                          /* C1/C2/C4 */
  ~AM_Shortcut();

  void   invoke(AM_LauncherInterface const&);
  void   createObject(QMap<QString, QString> const&);
  /* 原库虚表证据：AM_Shortcut 未覆盖 dump()，虚表槽 10 继承基类
     AM_AppObject::dump()。重建库曾错误声明 dump() 覆盖导致槽位偏移。 */
  /* 非虚 name-hiding：隐藏基类同名虚函数 */
  int    getAppLockValue() const;
  void   setAppLockValue(int);

private :
  int     m_pad50;                         /* 0x50 派生前导 */
  QString m_cmdLine;                       /* 0x54 createObject 存 Exec，invoke 执行 */
  int     m_lockValue;                     /* 0x58 */
};

#endif
