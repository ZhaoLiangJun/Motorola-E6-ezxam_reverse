/*
libezxam.so.1.0.0  逆向重建（成员布局依据 Ghidra 反编译）
AM_AppDataInterface : public QObject（Q_OBJECT 信号槽）
  成员：m_pRegMgr @ 0x24 (AM_RegistryManager*)
  所有公共方法都委托给 m_pRegMgr
  构造签名：AM_AppDataInterface(AM_RegistryManager*, QObject*, const char* name = 0)
虚表证据：_ZTV19AM_AppDataInterface @ 0x41b22d08
信号：folderChanged(const AM_Folder*)
反编译证据：
  getAllAppList(list)  → m_pRegMgr->getAllAppList(list)
  getRootFolder()      → m_pRegMgr->getRootFolder()
  getRegistryManager() → return m_pRegMgr
*/
#ifndef AM_APPDATAINTERFACE_H
#define AM_APPDATAINTERFACE_H

#include <qobject.h>
#include <qstring.h>
#include <qlist.h>
#include <quuid.h>
#include <AM_AppObject.h>
#include <AM_AppLnk.h>
#include <AM_Folder.h>

class AM_RegistryManager;

class AM_AppDataInterface : public QObject
{
    Q_OBJECT
public :
  AM_AppDataInterface(AM_RegistryManager* pRegMgr, QObject* parent, const char* name = 0);
  ~AM_AppDataInterface();

  void          getAllAppList(QList<AM_AppObject>& list);
  AM_AppObject* getAppObjById(const QUuid& uuid) const;
  AM_AppLnk*    getAppLnkById(const QUuid& uuid) const;
  AM_AppObject* getJavaObj(const int& javaId) const;
  AM_Folder*    getRootFolder();
  AM_AppObject* getAppObject(const QString& name);
  AM_AppLnk*    getAppLnkBySerialId(const QString& serialId);
  void   createFolder(const QString& name, const QString& path,
                      const QString& icon, const QString& aniIcon);
  void   deleteFolder(const QString& name);
  /* 原库返回 char：am 侧调用后 `and r0,r0,#0xff` 取返回值（0x47538/0x475a8 实证），
     故这里必须返回 bool，void 会让 am 拿到垃圾返回值。 */
  bool   changeOrder(const QString& folderPath, const QList<QString>& order);
  void   modifyApp(const QString& name, const QString& newName,
                   const QString& newPath, const QString& newIcon);
  bool   changeFolder(const QString& name, const QString& oldFolder,
                      const QString& newFolder, int index);
  void   getAutoRunApps(QList<AM_AppLnk>& list) const;
  AM_RegistryManager* getRegistryManager();

signals:
  void folderChanged(const AM_Folder* folder);

private :
  AM_RegistryManager* m_pRegMgr;  /* 0x24 */
};

#endif  /* AM_APPDATAINTERFACE_H */
