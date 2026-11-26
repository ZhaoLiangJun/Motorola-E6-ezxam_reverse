/*
libezxam.so.1.0.0  逆向重建（成员布局依据 Ghidra 反编译 + 虚表分析）
虚表证据：AM_AppLnk : public AM_AppObject，覆盖全部 5 个虚函数
  slot[0..3] = AM_AppLnk::getAppLockEnabled/setAppLockEnabled/getAppLockValue/setAppLockValue
  slot[4]    = AM_AppLnk::dump
成员：getAppLockEnabled @ 0x5A(bool), getAppLockValue @ 0x5C(int)
*/
#ifndef AM_APPLNK
#define AM_APPLNK

#include <qmap.h>
#include <AM_AppObject.h>
#include <AM_LauncherInterface.h>

class AM_AppLnk : public AM_AppObject
{
public :
  AM_AppLnk();                              /* C1/C2/C4 */
  AM_AppLnk(AM_AppObject::Type type);
  ~AM_AppLnk();

  void   invoke(AM_LauncherInterface const&);
  QString getExecFullPath() const;
  QString execFullPathImpl() const;   /* v18: 实现体（getExecFullPath 包日志） */
  void   createObject(QMap<QString, QString> const&);
  int    getUidByString(QString const&);
  int    getGidByString(QString const&);
  void   dump();                            /* 虚表 slot[4] 覆盖 */
  bool   getAppLockEnabled() const;         /* 虚表 slot[0] 覆盖 */
  void   setAppLockEnabled(bool);           /* 虚表 slot[1] 覆盖 */
  int    getAppLockValue() const;           /* 虚表 slot[2] 覆盖 */
  void   setAppLockValue(int);              /* 虚表 slot[3] 覆盖 */

  /* RegistryManager 查询用访问器（对应反编译偏移） */
  int    getJavaId() const  { return m_javaId; }        /* 0x6C */
  int    getUid() const     { return m_uid; }           /* 0x60 */
  int    getGid() const     { return m_gid; }           /* 0x64 */
  const QString& getExecPath() const { return m_execPath; } /* 0x54 */  bool   isVisible() const  { return m_visible; }        /* 0x80 = 注册表 "Eraseable" */
  bool   isDaemon() const   { return m_field58; }        /* 0x58 = 注册表 "Daemon" */
  /* FTemp00C(0xc) 是卡槽字段：checkAndUpdateCardApp 读 ==3/5(保留)否则置4；
     FTemp044(0x44,基类) 才是设备来源（1=内置/2=卡，handleCardPlugIn/Out 用）。
     注意：getDeviceId 返回的是 0xc 卡槽字段，非设备来源。 */
  int    getDeviceId() const { return FTemp00C; }       /* 0x0C 卡槽字段 */
  void   setDeviceId(int d)  { FTemp00C = d; }          /* 0x0C 卡槽字段 */

private :
  /* 继承自 AM_AppObject 的成员布局：基类占 0x00..0x50 */
  int    m_pad50;                          /* 0x50 (派生前导，原版保留) */
  QString m_execPath;                       /* 0x54 注册表 "Exec"（getExecFullPath 用） */
  bool   m_field58;                         /* 0x58 注册表 "Daemon" */
  bool   m_massStorage;                     /* 0x59 注册表 "MassStorageStatus" */
  bool   m_appLockEnabled;                  /* 0x5A 注册表 "LockEnabled" (getAppLockEnabled) */
  int    m_appLockValue;                    /* 0x5C */
  int    m_uid;                             /* 0x60 (getUidByString) */
  int    m_gid;                             /* 0x64 (getGidByString) */
  QString m_javaCmdLine;                    /* 0x68 (Java 命令行) */
  int    m_javaId;                          /* 0x6C (JavaId, getJavaObj 匹配) */
  QString m_javaWorkDir;                    /* 0x70 */
  QString m_javaClassPath;                  /* 0x74 */
  int    m_javaMemSize;                     /* 0x78 */
  QString m_javaUrl;                        /* 0x7C */
  bool   m_visible;                         /* 0x80 (getInstalledAppList 用) */
};

#endif // AM_APPLNK
