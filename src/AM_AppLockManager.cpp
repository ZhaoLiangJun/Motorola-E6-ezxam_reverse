/* AM_AppLockManager.cpp - 逆向重建自 libezxam.so.1.0.0 (Motorola E6)
   QObject 单例；instance() 首次调用 new(0x6C) 并缓存于静态指针。
   依赖外部：AM_RegistryManager::getConfigFileName / ZConfig（AppLockConfig 文件）。
   成员：m_nameCache @ 0x4C (QGCache)，静态 pAppLockMgr。
*/
#include "AM_AppLockManager.h"

#include <qobject.h>
#include <qstring.h>
#include <qstringlist.h>
#include <qcache.h>
#include <qdatastream.h>
#include <qcopchannel_qws.h>
#include <quuid.h>
#include <string.h>
#include <ZConfig.h>
#include <AM_RegistryManager.h>

AM_AppLockManager* AM_AppLockManager::pAppLockMgr = 0;

/* ============ 构造函数（private） ============ */
/* 反编译 0x41b0bfa0 起逐条：
     QObject::QObject(parent, name)
     QGCache 构造 ×2（this+0x2C、this+0x4C，各 100 项 / 17 字节 / StringKey）
     QCString("AM/RegistryiMgr/Channel")  → QCopChannel::isRegistered
     !isRegistered 时：m_pChannel = new QCopChannel(ch, this, 0)
                       connect(m_pChannel, SIGNAL(received(const QCString&, const QByteArray&)),
                               this, SLOT(slotChannelMessage(const QCString&, const QByteArray&)))
   v63：此前只建了缓存、**没有建通道也没有 connect**，导致别的进程改了应用锁
        本进程收不到通知（与 v62 修的 PHONE_MsgClient 缺 connect 同一类缺陷）。 */
AM_AppLockManager::AM_AppLockManager(QObject* parent, const char* name)
    : QObject(parent, name)
{
    m_nameCache = new QCache<QString>(100, 17, TRUE);

    m_pChannel = 0;
    QCString channel("AM/RegistryiMgr/Channel");
    if (!QCopChannel::isRegistered(channel)) {
        m_pChannel = new QCopChannel(channel, this, 0);
        if (m_pChannel) {
            QObject::connect(m_pChannel,
                             SIGNAL(received(const QCString&, const QByteArray&)),
                             this,
                             SLOT(slotChannelMessage(const QCString&, const QByteArray&)));
        }
    }
}

/* ============ 析构函数 ============ */
AM_AppLockManager::~AM_AppLockManager()
{
    delete m_nameCache;
}

/* ============ instance ============ */
/* 反编译：
     if (pAppLockMgr == 0) { pAppLockMgr = new AM_AppLockManager(0,0); }
     return pAppLockMgr;
   （operator_new(0x6C) 即 sizeof=0x6C）
   原库导出强符号 _ZN17AM_AppLockManager11pAppLockMgrE @ BSS 0x41b74b9c */
AM_AppLockManager* AM_AppLockManager::instance()
{
    if (pAppLockMgr == 0) {
        pAppLockMgr = new AM_AppLockManager(0, 0);
    }
    return pAppLockMgr;
}

/* ============ getAppLockValue ============ */
/* 反编译 0x41b0c294：
     AM_RegistryManager::getConfigFileName(6)  → AppLockConfig 路径
     ZConfig cfg(path, false);
     QString key = <rodata 组名> + uuid.toString();
     QString val = cfg.readEntry(groupName, key);
     if (val.isEmpty()) return 0;
     else return val.toInt(); */
int AM_AppLockManager::getAppLockValue(const QUuid& uuid)
{
    /* v63：原库 0x41b0c2bc 直接调**静态** AM_RegistryManager::getConfigFileName(6)，
       不构造实例。此前写成栈上 AM_RegistryManager 临时对象——它的构造函数会跑
       完整 initRegistry()/loadMenuTree()（60+ 个临时 QList + 可能建 QCop 通道），
       而这个函数是按图标查询的，等于每次查询重载一遍注册表。
       属 v60 把 getConfigFileName 改成 static 后漏改的调用点。 */
    QString configFile = AM_RegistryManager::getConfigFileName(
        AM_RegistryManager::RegFileType(6));
    ZConfig cfg(configFile, false);

    QString key = uuid.toString();
    /* v63：组名必须是 "AppLockValue"（原库 GOT[0xe70]，读/写/重载三处一致）。
       此前写成空组 ""，自读自写能对上，但与原厂数据**不兼容**：
       原库写下的锁值我们读不到，我们写下的原库也读不到。 */
    QString val = cfg.readEntry("AppLockValue", key, QString::null);
    if (val.isEmpty())
        return 0;                /* 未锁定 */
    return val.toInt();
}

/* ============ getAppName ============ */
/* 反编译 0x41b0c4b8：
   name = <rodata 默认空串>;
   QString* cached = (QString*)QGCache::find_string(m_nameCache, uuid.toString());
   if (cached) name = *cached;
   else {
     for each type in {1, 7, 4}:  ← rodata 配置类型序号
       cfgPath = AM_RegistryManager::getConfigFileName(type);
       if (lookUpAppName(cfgPath, uuid, name))
       {
         cached = new QString(name);
         QGCache::insert_string(m_nameCache, uuid.toString(), cached, 1);
         break;
       }
   } */
void AM_AppLockManager::getAppName(const QUuid& uuid, QString& name)
{
    name = QString::null;

    QString key = uuid.toString();
    QString* cached = (QString*)m_nameCache->find(key);
    if (cached) {
        name = *cached;
        return;
    }

    /* 遍历 3 个配置类型（1/7/4，与反编译一致），依次查找 */
    static const int cfgTypes[] = { 1, 7, 4 };
    /* v63：同 getAppLockValue —— 静态调用即可，不要为取一个路径字符串构造
       整个 AM_RegistryManager（那会跑一遍 loadMenuTree）。 */
    for (int i = 0; i < 3; ++i) {
        QString cfgPath = AM_RegistryManager::getConfigFileName(
            (AM_RegistryManager::RegFileType)cfgTypes[i]);
        if (lookUpAppName(cfgPath, uuid, name)) {
            /* 命中则缓存 */
            QString* entry = new QString(name);
            m_nameCache->insert(key, entry, 1);
            return;
        }
    }
    /* 未命中，不缓存 */
}

/* ============ lookUpAppName ============ */
/* 反编译 0x41b0c6d8：
   ZConfig cfg(config, false);
   遍历组，匹配 UUID 字段与 param_2.toString()，提取 Name 写 name，return TRUE。
   未匹配则 return FALSE。 */
bool AM_AppLockManager::lookUpAppName(const QString& config,
                                      const QUuid& uuid,
                                      QString& name)
{
    if (config.isEmpty())
        return false;
    ZConfig cfg(config, false);

    QStringList groups;
    cfg.getGroupsKeyList(groups);
    for (QStringList::ConstIterator gi = groups.begin(); gi != groups.end(); ++gi) {
        QString groupUuid = cfg.readEntry(*gi, "UUID", QString::null);
        if (groupUuid.compare(uuid.toString()) == 0) {
            name = cfg.readEntry(*gi, "Name", QString::null);
            return true;
        }
    }
    return false;
}

/* ============ slotChannelMessage ============ */
/* 反编译 0x41b0d810：QCop 广播分发。
   msg = channel.constData();  (param_1+4 → QCString 字符串指针)
   if strcmp(msg, <rodata Msg1>) == 0 → slotReloadAppLockValue();
   else if strcmp(msg, <rodata Msg2>) == 0 →
       QDataStream ds(data); QString name; ds >> name;
       slotReloadAppName(name);
   v71 修正：原库 0x41b0d81c/0x41b0d854 经 GOT[0xe7c]/GOT[0xe80] 取到的真实
   消息串是 'AppLockValueChanged()' / 'AppNameChanged(QString)'（dis2.py 已实证），
   与发送方 AM_RegistryManager::setAppLockValue / modifyApp 的 QCopChannel::send
   消息名完全一致。旧值 'AppLockValue'/'AppNameChanged' 是占位错值 → 永不匹配 →
   跨进程/本进程加锁刷新信号全断。 */
void AM_AppLockManager::slotChannelMessage(const QCString& channel,
                                           const QByteArray& data)
{
    const char* msg = channel;
    if (msg == 0)
        return;

    if (strcmp(msg, "AppLockValueChanged()") == 0) {
        slotReloadAppLockValue();
    }
    else if (strcmp(msg, "AppNameChanged(QString)") == 0) {
        QDataStream ds(data, IO_ReadOnly);
        QString name;
        ds >> name;
        slotReloadAppName(name);
    }
}

/* ============ slotReloadAppName ============ */
void AM_AppLockManager::slotReloadAppName(const QString& name)
{
    emit appNameChanged(name);
}

/* ============ slotReloadAppLockValue ============ */
void AM_AppLockManager::slotReloadAppLockValue()
{
    emit appLockValueChanged();
}

#include "AM_AppLockManager.moc"
