/*
libezxam.so.1.0.0  逆向重建（成员布局依据 Ghidra 反编译）
AM_AppLockManager : public QObject（单例，Q_OBJECT 信号槽）
  instance(): static 单例；首次调用 new 0x6C(this, 0, 0) 并缓存于 pAppLockMgr 静态指针
  成员：nameCache @ 0x4C (QGCache，QUuid->AppName 缓存)
  信号：appLockValueChanged() / appNameChanged(QString)
虚表证据：_ZTV17AM_AppLockManager @ 0x41b22b70
*/
#ifndef AM_APPLOCKMANAGER
#define AM_APPLOCKMANAGER

#include <qobject.h>
#include <qstring.h>
#include <qstringlist.h>
#include <qcache.h>
#include <quuid.h>

class QCopChannel;

class AM_AppLockManager : public QObject
{
    Q_OBJECT
public :
  static AM_AppLockManager* instance();

  int    getAppLockValue(const QUuid& uuid);
  void   getAppName(const QUuid& uuid, QString& name);
  bool   lookUpAppName(const QString& config, const QUuid& uuid, QString& name);

public slots:
  /* v32：元对象 1:1（原库 _ZN17AM_AppLockManager16staticMetaObjectEv
     @0x41b1745c：槽表 3 项 = slotReloadAppLockValue / slotReloadAppName /
     slotChannelMessage；信号表 2 项 = appLockValueChanged / appNameChanged）。 */
  void   slotReloadAppLockValue();
  void   slotReloadAppName(const QString& name);
  void   slotChannelMessage(const QCString& channel, const QByteArray& data);

signals:
  void appLockValueChanged();
  void appNameChanged(const QString&);

private :
  AM_AppLockManager(QObject* parent, const char* name);
  ~AM_AppLockManager();

  /* v63：原库构造时会建一个 QCopChannel("AM/RegistryiMgr/Channel") 并 connect
     到 slotChannelMessage（0x41b0c0a4 起：operator new → QCopChannel ctor →
     QObject::connect）。缺了它，别的进程改应用锁后本进程收不到通知。 */
  QCopChannel* m_pChannel;       /* 0x28  通道指针（原库 [this+0x28]） */
  QCache<QString>* m_nameCache;  /* 0x4C  QUuid->AppName 缓存 (QCache<value>, key=QString) */
  static AM_AppLockManager* pAppLockMgr;   /* 全局静态单例（原库导出符号名） */
};

#endif  /* AM_APPLOCKMANAGER */
