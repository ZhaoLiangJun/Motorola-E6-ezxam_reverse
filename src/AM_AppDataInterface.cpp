/* AM_AppDataInterface.cpp - 逆向重建自 libezxam.so.1.0.0 (Motorola E6)
   QObject 包装器，所有方法委托给 m_pRegMgr (AM_RegistryManager*) @ 0x24。
   反编译证据（委托链）：
     getAllAppList(list)  → m_pRegMgr->getAllAppList(list)
     getRootFolder()      → m_pRegMgr->getRootFolder()
     getRegistryManager() → return m_pRegMgr
     getAppObject(name)   → m_pRegMgr->getAppObject(name)
*/
#include "AM_AppDataInterface.h"

#include <qobject.h>
#include <AM_RegistryManager.h>
#include "ringlog.h"

/* ---- 入口探针（写 /ram/tmp/amreg.log）：定位"UI 操作有没有进到本库" ---- */
#include <stdio.h>
#include <stdarg.h>
/* v38：日志统一走 amlog.cpp；热路径用 AMVLOG（默认静默，
   touch /ram/tmp/am_debug 后才输出），避免日志 I/O 拖慢界面。 */
#include "amlog.h"
#define ilog(...)  AMVLOG(__VA_ARGS__)

/* ============ 构造函数 ============ */
/* v32：反编译 0x41aea438（真实构造体，C1/C2 thunk -> PTR_41b22b24）实证：
     m_pRegMgr = operator new(8) 得到的小代理（[0]=regMgr, [4]=0）-> this+0x24
     QObject::connect(regMgr, "2folderChanged( const AM_Folder*)",
                      this,   "2folderChanged( const AM_Folder*)");
   —— 即【把 AM_RegistryManager 的 folderChanged 信号中继成本类同名信号】。
   本类是 am 侧拿到注册表变化通知的入口，没有这个 connect，am 在
   AM_AppDataInterface 上 connect 的 folderChanged 永远收不到（旧版是空实现）。 */
AM_AppDataInterface::AM_AppDataInterface(
    AM_RegistryManager* pRegMgr, QObject* parent, const char* name)
    : QObject(parent, name),
      m_pRegMgr(pRegMgr)
{
    if (pRegMgr) {
        QObject::connect(pRegMgr, SIGNAL(folderChanged(const AM_Folder*)),
                         this, SIGNAL(folderChanged(const AM_Folder*)));
    }
}

/* ============ 析构函数 ============ */
AM_AppDataInterface::~AM_AppDataInterface()
{
}

/* ============ getAllAppList ============ */
void AM_AppDataInterface::getAllAppList(QList<AM_AppObject>& list)
{
    ilog("[iface] getAllAppList in list=%d mgr=%p", (int)list.count(), (void*)m_pRegMgr);
    if (m_pRegMgr)
        m_pRegMgr->getAllAppList(list);
}

/* ============ getAppObjById ============ */
AM_AppObject* AM_AppDataInterface::getAppObjById(const QUuid& uuid) const
{
    return m_pRegMgr ? m_pRegMgr->getAppObjById(uuid) : 0;
}

/* ============ getAppLnkById ============ */
AM_AppLnk* AM_AppDataInterface::getAppLnkById(const QUuid& uuid) const
{
    ring_put(60, (unsigned)m_pRegMgr, 0);
    return m_pRegMgr ? m_pRegMgr->getAppLnkById(uuid) : 0;
}

/* ============ getJavaObj ============ */
AM_AppObject* AM_AppDataInterface::getJavaObj(const int& javaId) const
{
    ring_put(62, (unsigned)javaId, 0);
    return m_pRegMgr ? m_pRegMgr->getJavaObj(javaId) : 0;
}

/* ============ getRootFolder ============ */
AM_Folder* AM_AppDataInterface::getRootFolder()
{
    return m_pRegMgr ? m_pRegMgr->getRootFolder() : 0;
}

/* ============ getAppObject ============ */
AM_AppObject* AM_AppDataInterface::getAppObject(const QString& name)
{
    return m_pRegMgr ? m_pRegMgr->getAppObject(name) : 0;
}

/* ============ getAppLnkBySerialId ============ */
AM_AppLnk* AM_AppDataInterface::getAppLnkBySerialId(const QString& serialId)
{
    ring_put(61, (unsigned)m_pRegMgr, 0);
    return m_pRegMgr ? m_pRegMgr->getAppLnkBySerialId(serialId) : 0;
}

/* ============ createFolder ============ */
void AM_AppDataInterface::createFolder(
    const QString& name, const QString& path,
    const QString& icon, const QString& aniIcon)
{
    if (m_pRegMgr)
        m_pRegMgr->createFolder(name, path, icon, aniIcon);
}

/* ============ deleteFolder ============ */
void AM_AppDataInterface::deleteFolder(const QString& name)
{
    ilog("[iface] deleteFolder %s", (const char*)name.utf8());
    if (m_pRegMgr)
        m_pRegMgr->deleteFolder(name);
}

/* ============ changeOrder ============ */
bool AM_AppDataInterface::changeOrder(
    const QString& folderPath, const QList<QString>& order)
{
    ring_put(64, (unsigned)order.count(), 0);
    ilog("[iface] changeOrder folder=%s n=%d mgr=%p",
         (const char*)folderPath.utf8(), (int)order.count(), (void*)m_pRegMgr);
    if (!m_pRegMgr) {
        ilog("[iface] changeOrder !! m_pRegMgr == NULL (委托断链)");
        return false;
    }
    bool ok = m_pRegMgr->changeOrder(folderPath, order);
    ilog("[iface] changeOrder <- ok=%d", (int)ok);
    return ok;
}

/* ============ modifyApp ============ */
void AM_AppDataInterface::modifyApp(
    const QString& name, const QString& newName,
    const QString& newPath, const QString& newIcon)
{
    ilog("[iface] modifyApp %s -> %s", (const char*)name.utf8(),
         (const char*)newName.utf8());
    if (m_pRegMgr)
        m_pRegMgr->modifyApp(name, newName, newPath, newIcon);
}

/* ============ changeFolder ============ */
bool AM_AppDataInterface::changeFolder(
    const QString& name, const QString& oldFolder,
    const QString& newFolder, int index)
{
    ilog("[iface] changeFolder name=%s old=%s new=%s idx=%d mgr=%p",
         (const char*)name.utf8(), (const char*)oldFolder.utf8(),
         (const char*)newFolder.utf8(), index, (void*)m_pRegMgr);
    if (!m_pRegMgr)
        return false;
    return m_pRegMgr->changeFolder(name, oldFolder, newFolder, index);
}

/* ============ getAutoRunApps ============ */
void AM_AppDataInterface::getAutoRunApps(QList<AM_AppLnk>& list) const
{
    ring_put(63, (unsigned)m_pRegMgr, 0);
    if (m_pRegMgr)
        m_pRegMgr->getAutoRunApps(list);
}

/* ============ getRegistryManager ============ */
AM_RegistryManager* AM_AppDataInterface::getRegistryManager()
{
    return m_pRegMgr;   /* 反编译：return *(this + 0x24) */
}

#include "AM_AppDataInterface.moc"
