/*
libezxam.so.1.0.0  逆向重建（成员布局依据 Ghidra 反编译 + 虚表分析）
虚表证据：AM_EmbeddedLink : public AM_AppObject
  slot[4] = AM_EmbeddedLink::dump (0x41af4c24，空实现；AM_Shortcut::dump 共享此地址)
  AM_Shortcut vtable slot[4] 亦指向此函数。
*/
#ifndef AM_EMBEDDEDLINK
#define AM_EMBEDDEDLINK

#include <qmap.h>
#include <AM_AppObject.h>
#include <AM_LauncherInterface.h>

class AM_EmbeddedLink : public AM_AppObject
{
public :
  AM_EmbeddedLink();                      /* C1/C2/C4 */
  ~AM_EmbeddedLink();

  void invoke(AM_LauncherInterface const&);
  void createObject(QMap<QString, QString> const&);
  bool getInvisiblNetLinkFromDmt(QString*, QString*);  /* 返回 bool：createObject 消费其值做分支 */
  void dump();                            /* 虚表 slot[4] 覆盖 */

private :
  int m_pad50;                            /* 0x50 派生前导 */
  QString m_cmdLine;                      /* 0x54 invoke 用 system() 执行的命令行 */
};

#endif // AM_EMBEDDEDLINK
