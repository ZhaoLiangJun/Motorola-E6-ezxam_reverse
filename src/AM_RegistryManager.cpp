/* AM_RegistryManager.cpp - 逆向重建自 libezxam.so.1.0.0 (Motorola E6)
   QObject 派生；虚表 _ZTV18AM_RegistryManager @ 0x41b22b10
   构造(0x41af5ab8)：QObject(parent,name) → new m_pAppMap(0x24) → new m_pAutoRunList(0x28)
                     → new AM_AppFactory(0x2C) → QCopChannel(0x30,"AM/RegistryiMgr/Channel")
                     → initRegistry()
   成员布局：m_pAppMap@0x24, m_pAutoRunList@0x28, m_pAppFactory@0x2C, m_pChannel@0x30
   关键方法反编译逻辑已还原（getRootFolder/getAppObjById/getAllAppList/setAppLockValue/
   initRegistry/clearAppMap/changeFileAttr/copyTextFile/getConfigFileName/initRegistryFiles）。
   依赖外部：ZConfig/QCopChannel/QFile/QDir/AM_AppFactory/AM_Folder/AM_AppObject/AM_AppLnk。
*/
#include "AM_RegistryManager.h"

#include <qobject.h>
#include <qapplication.h>   /* v72: qApp —— 原库构造函数 connect(qApp, systemMessage, ...) */
#include <qstring.h>
#include <qlist.h>
#include <qmap.h>
#include <qglist.h>
#include <qvaluestack.h>
#include <qvaluelist.h>
#include <qcopchannel_qws.h>
#include <qfile.h>
#include <qfileinfo.h>
#include <qdir.h>
#include <qdatastream.h>
#include <string.h>      /* v74: strcmp —— 热插拔消息频道过滤 */
#include <ZConfig.h>
#include <AM_AppFactory.h>
#include <AM_Global.h>   /* AM_Global::getPreloadDir/getAppWriteAmDir/getInstCardDir 平台方法 */
#include <DmtExternal.h> /* DmtTreeFactory::Initialize/Uninitialize 平台入口 (libezx-dmt.so) */
#include <AM_MimeType.h> /* AM_MimeType::instance/getMimeIdByExt/getMimeTypeDataEx (libezx-mime.so) */
#include <SETUP_Utility.h> /* v48: getFlexBit(0xae) —— getSystemFolderName 的 Games/User2 二选一 */
#include <qdatetime.h>   /* QDateTime/QDate/QTime: createUuid 用当前时间做随机种子 */
#include <time.h>        /* tm/mktime: createUuid 时间转种子 */
#include <stdlib.h>      /* srand/rand: createUuid 随机数 */
#include <qtextstream.h> /* QTextStream: copyTextFile 逐行复制 */
#include <unistd.h>      /* chmod/chown: changeFileAttr (备用，工具链缺失时用下方 extern) */
#include <stdio.h>       /* fopen/fclose: getAllAppList 的 DRM2 license 探测 */
#include <sys/time.h>    /* gettimeofday: v37 日志毫秒时间戳（定位卡死点） */

/* changeFileAttr 用的 libc 权限/属主接口（ARM 工具链 unistd.h 未完整导出，显式声明） */
extern "C" {
int  chmod(const char* path, unsigned int mode);
int  chown(const char* path, unsigned int owner, unsigned int group);
void sync(void);   /* addAppToRegistry: 注册表写入后落盘 */
}

/* ---- 运行调试日志（写 /ram/tmp/amreg.log，定位注册表链路用） ---- */
#include <stdio.h>
#include <stdarg.h>
#include "ringlog.h"
#include "amlog.h"    /* v38: AMLOG/AMVLOG 日志开关 */
/* v38：alog 统一走 amlog.cpp 的实现（同一份毫秒时间戳 + 同一个文件），
   避免本文件再存一份 static 计时基准、出现两套时间轴。
   AMLOG = 低频必写；AMVLOG = 热路径，仅 /ram/tmp/am_debug 存在时写。 */
/* v41：本文件里 39 处 alog() 全部改走 AMVLOG（默认不写）。
   原因：本文件的函数几乎全是"每刷一次界面就调几十次"的热路径
   （getAppObject/getSize/getAppList/getExecFullPath/removeApp 前后的一致性修补等）。
   实测（换回原版库 A/B）：原库空闲时 utime/stime 恒为 0，本库空闲却持续涨
   stime ≈ 25%/核 —— 内核态占绝对多数说明是"系统调用风暴"，
   而每次 amLogWrite 就是 fopen+vfprintf+fclose 三次以上系统调用。
   需要看这些日志时：touch /ram/tmp/am_debug 再重启 am 即可全量输出。 */
#define alog(...) AMVLOG(__VA_ARGS__)

/* ============ 构造函数 ============ */
AM_RegistryManager::AM_RegistryManager(QObject* parent, const char* name)
    : QObject(parent, name)
{
    /* v37：先装信号处理器 + 写进程分隔行，保证 BOOT 是本进程的第一行日志 */
    ring_install_handler();
    /* 反编译 0x41af5ab8：
       0x24 = new QMap<QString,AM_AppObject*>(refcount wrapper)
       0x28 = new QGList(auto-run uuid list)
       0x2C = new AM_AppFactory
       0x30 = new QCopChannel("AM/RegistryiMgr/Channel") + connect(...)
       最后 initRegistry() */
    m_pAppMap      = new QMap<QString, AM_AppObject*>();
    m_pAutoRunList = new QValueList<QString>();
    m_pAppFactory  = new AM_AppFactory();
    /* QCop 通道：仅当未注册时创建（反编译 isRegistered 分支） */
    const char* channel = "AM/RegistryiMgr/Channel";
    if (!QCopChannel::isRegistered(QCString(channel))) {
        m_pChannel = new QCopChannel(QCString(channel), this);
        if (m_pChannel) {
            QObject::connect(m_pChannel,
                             SIGNAL(received(const QCString&, const QByteArray&)),
                             this,
                             SLOT(slotChannelMessage(const QCString&, const QByteArray&)));
        }
    } else {
        m_pChannel = 0;
    }
    /* v72 / H1：原库构造函数 0x41af5c64-0x41af5c84 的第二条 connect 取证完成：
       GOT[0x41b23af8] 重定位符号为 qApp（R_ARM_GLOB_DAT，槽里存 &qApp），
       `ldr r0,[r2]` 解引用后得到真正的 QApplication 对象；
       GOT[0x41b236d8] = "2systemMessage( const QCString &, const QByteArray & )"
       GOT[0x41b236dc] = "1slotHotPlugForceUpdate( const QCString &, const QByteArray & )"
       即原库恒成立：connect(qApp, systemMessage(...), this, slotHotPlugForceUpdate(...))。
       旧版只连了 QCopChannel::received → slotChannelMessage，导致 SD 插拔事件
       永远进不到 slotHotPlugForceUpdate（H1 热插拔先天不通）。 */
    bool hotplugConnected = false;
    if (qApp) {
        hotplugConnected = QObject::connect(qApp,
                         SIGNAL(systemMessage(const QCString&, const QByteArray&)),
                         this,
                         SLOT(slotHotPlugForceUpdate(const QCString&, const QByteArray&)));
    }
    /* v74：热插拔链是【运行时 QCop 信号】触发，静态调用图（_callermap.json）看不到，
       审计盲区。这里把 connect 返回值落到日志，部署后可直接确认 H1 是否真正生效
       （Qt2 在签名不匹配时 connect 返回 false 且只 qWarning，静默失效）。 */
    AMLOG("[RMCtor] hotplug connect=%d qApp=%p", (int)hotplugConnected, (void*)qApp);
    /* v37：ezxaid 的 AM_QtPackageManager::uninstallModifyDatabase 会 new 一个
       AM_RegistryManager（在 am 进程内！）→ 这里会跑一遍完整 loadMenuTree。
       打上 this 指针与耗时，才能分辨"这是 am 自己那个"还是"卸载时临时那个"。 */
    AMLOG("[RMCtor] this=%p begin", (void*)this);
    initRegistry();
    AMLOG("[RMCtor] this=%p done", (void*)this);
}

/* ============ 析构函数 ============ */
AM_RegistryManager::~AM_RegistryManager()
{
    alog("[RMDtor] this=%p map=%d", (void*)this,
         m_pAppMap ? (int)m_pAppMap->count() : -1);
    clearAppMap();
    delete m_pAppMap;
    delete m_pAutoRunList;
    delete m_pAppFactory;
    delete m_pChannel;
}

/* ============ clearAppMap ============ */
/* 原库 0x41AF6058（1212 字节）：遍历 m_pAppMap，逐个 delete 对象，再清空。
 *
 * v41 决定性修正：旧实现只有一句 `m_pAppMap->clear()` —— QMap::clear() 只释放
 * 映射节点，**不 delete 指向的 AM_AppObject**，于是每销毁一个 manager 就泄漏
 * 整套菜单对象（本机 81 个，含 FUUID/FName/FDir 等 QString 堆块，约 300~400B/个
 * → 每次泄漏 ~30KB）。
 *
 * 而 manager 不止一个：
 *   - am 启动时 1 个；
 *   - **每次 C 导出调用**（AM_RegistryManager_getAppParentFolderUid /
 *     removeAppFromMainMenu / changeFolder）都会 `new AM_RegistryManager(0,0)`
 *     再 `delete`（原库就是这么写的，见 AM_RegistryManager_cwrap.cpp）；
 *   - 装/卸载一个应用会连续触发多次 → 一次卸载泄漏上百 KB。
 * 反复装卸 → MemFree 见底（实测本机 MemFree 仅 ~800KB / 46MB）→ malloc 越来越慢、
 * 桌面重绘缺内存 → 表现为"越用越卡，最后点不动"。
 *
 * 去重：同一个对象理论上只挂一个键，但 loadMenuTree 里根 Folder 可能被多处引用，
 * 为安全起见只删首次出现的指针（O(n²)，n≈81，析构期一次，可忽略）。 */
void AM_RegistryManager::clearAppMap()
{
    if (!m_pAppMap)
        return;
    /* Qt2 的 QList<type> 内部存的就是 `const type*`，所以这里写 QList<AM_AppObject>
       （写 QList<AM_AppObject*> 会变成指针的指针，contains/append 都不匹配）。 */
    QList<AM_AppObject> seen;
    for (QMap<QString, AM_AppObject*>::Iterator it = m_pAppMap->begin();
         it != m_pAppMap->end(); ++it) {
        AM_AppObject* o = it.data();
        if (!o)
            continue;
        if (seen.contains(o))
            continue;                      /* 别名键：只删一次 */
        seen.append(o);
        delete o;
    }
    m_pAppMap->clear();
    AMLOG("[clearAppMap] deleted=%d", (int)seen.count());
}

/* ============ initRegistry ============ */
/* 原库 0x41af6514（Ghidra 实证）：clearAppMap(this); loadMenuTree(this); return 1。
   v73：补上 clearAppMap —— 旧版只调 loadMenuTree，在"已填充的注册表"上二次
   init（am 侧直接调用本导出函数时）会重复建根 Folder/对象并泄漏旧对象。
   构造路径不受影响：m_pAppMap 刚建出来是空的，clearAppMap 为空操作；
   RefreshDesktop 路径 clearAppMap()+initRegistry() 也只是多一次空 clear。 */
void AM_RegistryManager::initRegistry()
{
    ring_install_handler(); ring_put(24, 0, 0);
    clearAppMap();
    loadMenuTree();
}

/* ============ loadMenuTree ============ */
/* 反编译 0x41afa684：
   initRegistryFiles() → 创建根 Folder（createAppObject(Folder)）
   → 用固定 UUID 作键加入 m_pAppMap → loadRegistry()
   → 读 CardRegistry(ZConfig) 的组条目，对每个组创建子 Folder，
   add 到根 Folder 的 m_appList。最后 handleCardPlugIn(4)。
   字面池解码确认：根 Folder 用固定 UUID "2a58c0d6-...9d41a4e9"
   作注册键（rodata 0x41b18d80），与 getRootFolder() 的查找键一致；
   子 Folder 用 CardRegistry 组名作键。 */
void AM_RegistryManager::loadMenuTree()
{
    ring_put(23, 0, 0);
    alog("[loadMenuTree] begin");
    initRegistryFiles();

    /* 1. 备用根文件夹（原库：createAppObject(Folder)，key=2a58c0d6-...，
          名字=AM_ROOT_FOLDER（rodata 0x41b18f20），插入 m_pAppMap） */
    AM_AppObject* root = m_pAppFactory->createAppObject(AM_AppObject::Folder);
    alog("[loadMenuTree] root=%p", root);
    if (root) {
        root->setUUID("2a58c0d6-05c6-45aa-8938-63059d41a4e9");
        root->setName(QString("AM_ROOT_FOLDER"));
        (*m_pAppMap)[QString("2a58c0d6-05c6-45aa-8938-63059d41a4e9")] = root;
    }

    /* 2. 加载注册表对象（SysRegistry/UserRegistry/CardRegistry/InstalledDB，
          文件夹对象来自 SysRegistry：组名=UUID，Name=中文文件夹名） */
    loadRegistry();

    /* 3. 真实菜单根（原库 0x41afa684：getAppObject("cb839c8f-72c3-42ac-bf17-
          b01080cb5705")——SysRegistry 里 UUID 为 cb839c8f 的 "All" 文件夹） */
    AM_Folder* rootFolder = static_cast<AM_Folder*>(getAppObject(
        QString("cb839c8f-72c3-42ac-bf17-b01080cb5705")));
    alog("[loadMenuTree] rootFolder=%p", rootFolder);

    /* 4. UserMenuTree（getConfigFileName(2)）：每组 [uuid] 只有 Items 键，
          值为 ';' 分隔的子 UUID。folder=getAppObject(组名)，逐个把
          getAppObject(子uuid) add 进 folder，并设可见标志（原库 obj+0x40=1）；
          folder != rootFolder 时记录父指针（原库 obj+4=folder）。 */
    QString treeFile = getConfigFileName(UserMenuTree);
    ZConfig cfg(treeFile, false);
    QStringList groups;
    cfg.getGroupsKeyList(groups);
    alog("[loadMenuTree] tree=%s groups=%d",
         (const char*)treeFile.utf8(), (int)groups.count());
    int linked = 0;
    for (QStringList::ConstIterator gi = groups.begin(); gi != groups.end(); ++gi) {
        QString g = *gi;
        QString items = cfg.readEntry(g, QString("Items"), QString::null);
        if (items.isEmpty())
            continue;
        QStringList list = QStringList::split(QChar(';'), items);
        AM_Folder* folder = static_cast<AM_Folder*>(getAppObject(g));
        if (!folder)
            continue;
        for (QStringList::ConstIterator ii = list.begin(); ii != list.end(); ++ii) {
            QString uuid = (*ii).stripWhiteSpace();
            if (uuid.isEmpty())
                continue;
            AM_AppObject* obj = getAppObject(uuid);
            if (!obj)
                continue;
            folder->add(obj, -1);
            obj->setVisibleFlag(true);
            if (folder != rootFolder)
                obj->setParentFolder(folder);
            ++linked;
        }
    }
    alog("[loadMenuTree] linked=%d appMap size=%d", linked,
         (int)m_pAppMap->count());

    handleCardPlugIn(static_cast<RegFileType>(4));
}

/* ============ loadRegistry ============ */
/* 反编译 0x41af8084：
   getConfigFileName(5)=InstalledDB → ZConfig
   getConfigFileName(6)=AppLockConfig → ZConfig
   循环 4 次 createObject(type, installedDB, appLockConfig)
   再读 DMT 自启动状态。 */
void AM_RegistryManager::loadRegistry()
{
    ring_put(24, 1, 0);
    /* v16 对齐原库 0x41af8084：
       getConfigFileName(5)=UserConfig → ZConfig（参数1）
       getConfigFileName(6)=AppLockConfig → ZConfig（参数2）
       类型数组 {1,3,4,7}（rodata 0x41b191b0，经 GOT 解析）：
         1=SysRegistry 3=UserRegistry 4=CardRegistry 7=InstalledDB */
    QString userCfgPath = getConfigFileName(UserConfig);
    QString appLockPath = getConfigFileName(AppLockConfig);
    alog("[loadRegistry] userCfg=%s appLock=%s",
         (const char*)userCfgPath.utf8(), (const char*)appLockPath.utf8());
    ZConfig userCfg(userCfgPath, false);
    ZConfig lockCfg(appLockPath, false);
    static const int types[4] = { 1, 3, 4, 7 };
    for (int i = 0; i < 4; ++i) {
        alog("[loadRegistry] createObject type=%d", types[i]);
        createObject(static_cast<RegFileType>(types[i]), userCfg, lockCfg);
    }
    alog("[loadRegistry] appMap size=%d", (int)m_pAppMap->count());
}

/* ============ initRegistryFiles ============ */
/* 反编译 0x41af7ef8：
     getConfigFileName(0); getConfigFileName(2);
     getConfigFileName(8); getConfigFileName(9);
     createConfigFile(5); createConfigFile(6); */
void AM_RegistryManager::initRegistryFiles()
{
    ring_put(25, 0, 0);
    /* v16 对齐原库 0x41af7ef8：
       copyTextFile(getConfigFileName(0) SysMenuTree, getConfigFileName(2) UserMenuTree) 重试≤3
       copyTextFile(getConfigFileName(8) SysMime,     getConfigFileName(9) UserMime)     重试≤3
       createConfigFile(5 UserConfig) createConfigFile(6 AppLockConfig) */
    QString sysTree = getConfigFileName(static_cast<RegFileType>(0));
    QString userTree = getConfigFileName(static_cast<RegFileType>(2));
    for (int i = 0; i < 3; ++i) {
        if (copyTextFile(sysTree, userTree))
            break;
    }
    QString sysMime = getConfigFileName(static_cast<RegFileType>(8));
    QString userMime = getConfigFileName(static_cast<RegFileType>(9));
    for (int i = 0; i < 3; ++i) {
        if (copyTextFile(sysMime, userMime))
            break;
    }
    createConfigFile(static_cast<RegFileType>(5));
    createConfigFile(static_cast<RegFileType>(6));
}

/* UUID 规范化（去花括号/空白）——定义在 getAppObjById 前，此处前置声明供
   getAllAppList 的 DRM 判定使用。 */
static QString amNormUuid(const QString& s);

/* ============ getConfigFileName ============ */
/* 反编译 0x41af7210：switch(type) 用 AM_Global::getPreloadDir()/
   getAppWriteAmDir()/getInstCardDir() 拼接路径（fromLatin1 常量）。
   RegFileType 值域 0..10，配置文件名已从 .rodata 0x41b18de8-0x41b18ea8 确认：
     SysMenuTree/SysRegistry/UserMenuTree/UserRegistry/CardRegistry/
     UserConfig/AppLockConfig/InstalledDB/SysMime/UserMime/PlugCardDB
   系统类(Sys 前缀)走 preload，用户类(User 前缀/AppLockConfig/InstalledDB)走 appWrite，
   卡类(CardRegistry/PlugCard 前缀)走 card。 */
/* ★v60：static 成员（类外定义不重复写 static） */
QString AM_RegistryManager::getConfigFileName(RegFileType type)
{
    /* 忠实调用平台 API（反编译第22-24行）：getPreloadDir/getAppWriteAmDir/getInstCardDir，
       而非硬编码路径。配置文件名已从 .rodata 确认。 */
    const QString preload  = AM_Global::getPreloadDir();
    const QString appWrite = AM_Global::getAppWriteAmDir();
    const QString card     = AM_Global::getInstCardDir();
    static bool s_dirsLogged = false;
    if (!s_dirsLogged) {
        s_dirsLogged = true;
        alog("[dirs] preload=%s appWrite=%s card=%s",
             (const char*)preload.utf8(), (const char*)appWrite.utf8(),
             (const char*)card.utf8());
    }
    QString path;
    /* 实测（v10 日志）：getPreloadDir/getAppWriteAmDir/getInstCardDir 返回值
       均不带尾部 '/'（preload=/usr/data_resource/preloadapps
       appWrite=/ezx_user/download/appwrite//am card=/mmc/mmca1/.system/java），
       拼文件名前必须补 '/'，否则全部路径错误（amInstalledDB 等）→ 组数 0。 */
    /* v16 枚举映射（决定性）：逐 case 反汇编原库 0x41af7210，
       case0=SysMenuTree case1=SysRegistry case2=UserMenuTree case3=UserRegistry
       case4=CardRegistry case5=UserConfig case6=AppLockConfig case7=InstalledDB
       case8=SysMime case9=UserMime case10=PlugCardDB case11=PlugCardRegistry。
       preload 类（case0/1/8）不存在时回退 "/usr/SYSqtapp/am/"（rodata 0x41b18df8）。 */
    switch (type) {
    case 0:  /* SysMenuTree：preload，回退 */
        path = preload + "/SysMenuTree";
        if (!QFileInfo(path).exists())
            path = "/usr/SYSqtapp/am/SysMenuTree";
        return path;
    case 1:  /* SysRegistry：preload，回退 */
        path = preload + "/SysRegistry";
        if (!QFileInfo(path).exists())
            path = "/usr/SYSqtapp/am/SysRegistry";
        return path;
    case 2:  return appWrite + "/UserMenuTree";    /* UserMenuTree */
    case 3:  return appWrite + "/UserRegistry";    /* UserRegistry */
    case 4:  return card     + "/CardRegistry";    /* CardRegistry */
    case 5:  return appWrite + "/UserConfig";      /* UserConfig */
    case 6:  return appWrite + "/AppLockConfig";   /* AppLockConfig */
    case 7:  return appWrite + "/InstalledDB";     /* InstalledDB */
    case 8:  /* SysMime：preload，回退 */
        path = preload + "/SysMime";
        if (!QFileInfo(path).exists())
            path = "/usr/SYSqtapp/am/SysMime";
        return path;
    case 9:  return appWrite + "/UserMime";        /* UserMime */
    case 10: return card     + "/PlugCardDB";      /* PlugCardDB */
    case 11: return card     + "/PlugCardRegistry";/* PlugCardRegistry */
    default: return QString::null;
    }
}

/* ============ createConfigFile ============ */
/* 反编译 0x41afef80：QFileInfo 检查文件存在；不存在则 QFile open(WriteOnly)
   创建空文件，flush 后 changeFileAttr(0777)。 */
void AM_RegistryManager::createConfigFile(RegFileType type)
{
    QString path = getConfigFileName(type);
    if (path.isEmpty())
        return;
    QFileInfo fi(path);
    if (fi.exists())
        return;
    QFile f(path);
    if (f.open(IO_WriteOnly)) {
        f.flush();
        f.close();
        changeFileAttr(path);
    }
}

/* ============ 注册表文件本地解析（避开 getGroupEntriesKeyList 的 ABI 不确定性） ============ */
/* ZConfig 文件格式（实测 SysRegistry/InstalledDB）：
     [groupUUID]
     Key = value
   组名行独占一行，键值行 'Key = value'（等号两侧可有空格）。 */
/* 一次性解析整个注册表文件（v15 性能版）：
   原实现每组重开文件+QTextStream 逐行 QString 分配，72 组 ≈ 5 万次分配，
   在设备上启动耗时超过 windowsserver 的 am 启动超时（约 6 秒）导致重启循环。
   现改为 QFile::readBlock 一次读入 + C 指针扫描，避免中间 QString 分配。 */
#include <qtextcodec.h> /* QTextCodec: 注册表 GBK/UTF-8 → Unicode（v18） */
static QTextCodec* s_codecUtf8 = 0;
static QTextCodec* s_codecGbk = 0;
/* v18：严格 UTF-8 合法性检测（整串多字节序列全部合法且含非 ASCII 才判 UTF-8）。
   GBK 中文首字节 0x81-0xFE 与 UTF-8 0xC2-0xEF 重叠有歧义，故必须整串验证：
   一个非法尾字节即判非 UTF-8。 */
static bool amLooksLikeUtf8(const char* s)
{
    bool multi = false;
    const unsigned char* p = (const unsigned char*)s;
    while (*p) {
        unsigned char c = *p;
        if (c < 0x80) {                       /* ASCII */
            ++p;
            continue;
        }
        int n = 0;                            /* 后续字节数 */
        if ((c & 0xE0) == 0xC0)      n = 1;   /* 110xxxxx */
        else if ((c & 0xF0) == 0xE0) n = 2;   /* 1110xxxx */
        else if ((c & 0xF8) == 0xF0) n = 3;   /* 11110xxx */
        else return false;                    /* 10xxxxxx / 1111111x 非法 */
        multi = true;
        for (int i = 0; i < n; ++i) {
            ++p;
            if ((*p & 0xC0) != 0x80)          /* 尾字节必须 10xxxxxx */
                return false;
        }
        ++p;
    }
    return multi;
}
static void amParseAll(const QString& file,
                       QMap<QString, QMap<QString, QString> >& out)
{
    QFile f(file);
    if (!f.open(IO_ReadOnly))
        return;
    long sz = (long) f.size();
    if (sz == 0) {
        f.close();
        return;
    }
    char* buf = new char[sz + 1];
    long got = (long) f.readBlock(buf, (unsigned long) sz);
    if (got < 0)
        got = 0;
    buf[got] = '\0';
    f.close();

    char* p = buf;
    char* ln = buf;
    char group[64];
    group[0] = '\0';
    bool inGroup = false;
    for (;;) {
        char c = *p;
        if (c == '\n' || c == '\0') {
            *p = '\0';
            char* s = ln;
            while (*s == ' ' || *s == '\r' || *s == '\t')
                ++s;
            char* e = p - 1;
            while (e >= s && (*e == ' ' || *e == '\r' || *e == '\t'))
                *e-- = '\0';
            if (*s) {
                if (*s == '[') {
                    char* ge = strchr(s, ']');
                    if (ge) {
                        *ge = '\0';
                        char* g = s + 1;
                        while (*g == ' ')
                            ++g;
                        char* ge2 = strlen(g) + g - 1;
                        while (ge2 >= g && *ge2 == ' ')
                            *ge2-- = '\0';
                        strncpy(group, g, sizeof(group) - 1);
                        group[sizeof(group) - 1] = '\0';
                        inGroup = true;
                    }
                } else if (inGroup) {
                    char* eq = strchr(s, '=');
                    if (eq) {
                        *eq = '\0';
                        char* k = s;
                        char* v = eq + 1;
                        char* ke = eq - 1;
                        while (ke >= k && (*ke == ' ' || *ke == '\t'))
                            *ke-- = '\0';
                        while (*v == ' ' || *v == '\t')
                            ++v;
                        if (*k) {
                            /* v18（决定性）：注册表编码不统一——SysRegistry 纯英文、
                               appWrite 下 GBK、卡上 CardRegistry 是 UTF-8
                               （实测 Name = e8 bd af ... = UTF-8 "软件管理"，
                               用 GB18030 解码 UTF-8 字节 → 乱码）。
                               自动检测：值是【合法 UTF-8 多字节序列】→ UTF-8；
                               否则 GB18030。键/组名 ASCII 不受影响。 */
                            if (!s_codecUtf8)
                                s_codecUtf8 = QTextCodec::codecForName("UTF-8");
                            if (!s_codecGbk) {
                                s_codecGbk = QTextCodec::codecForName("GB18030");
                                if (!s_codecGbk)
                                    s_codecGbk = QTextCodec::codecForLocale();
                            }
                            QTextCodec* c = s_codecGbk;
                            if (s_codecUtf8 && amLooksLikeUtf8(v)) {
                                c = s_codecUtf8;
                            }
                            out[QString::fromLatin1(group)]
                               [QString::fromLatin1(k)] = c->toUnicode(v);
                        }
                    }
                }
            }
            if (c == '\0')
                break;
            ln = p + 1;
        }
        ++p;
    }
    delete[] buf;
}

/* ============ createObject ============ */
/* ABI 补齐（v51）：原库导出的是 **3 参数**版本
   `AM_RegistryManager::createObject(RegFileType, ZConfig&, ZConfig&)` @0x41af8330。
   我们以前只导出带 bool onlyNew 的 4 参数版本，于是 .dynsym 里少了这个符号 ——
   任何按原库原型调用它的固件库都会在 dlopen 期解析失败。
   这里把对外接口转回内部实现（onlyNew=false = 原库语义：全量重建）。 */
void AM_RegistryManager::createObject(RegFileType type, ZConfig& cfg, ZConfig& reg)
{
    createObject(type, cfg, reg, false);
}

/* 原库 0x41af8330：自己打开 getConfigFileName(type) 对应文件，枚举每个组，
   读组内 "Type" 键 → toInt → AM_AppFactory::createAppObject(type)，
   再把该组全部键值对作为 props 传给虚函数 obj->createObject(QMap)，
   最后按 RegFileType 设设备来源并加入 m_pAppMap。
   注意：props 是组内【全部】键值（原库经 getGroupEntriesKeyList 迭代构造），
   不是 readListEntry(group,"keys")——实测文件里没有 "keys" 元键，
   旧实现读到空列表导致一个对象都建不出来（图标全消失）。 */
void AM_RegistryManager::createObject(RegFileType type, ZConfig& cfg, ZConfig& reg,
                                      bool onlyNew)
{
    ring_put(22, type, 0);
    QString path = getConfigFileName(type);
    QMap<QString, QMap<QString, QString> > all;
    amParseAll(path, all);
    alog("[createObject] type=%d path=%s groups=%d onlyNew=%d",
         (int)type, (const char*)path.utf8(), (int)all.count(), (int)onlyNew);
    int created = 0;
    for (QMap<QString, QMap<QString, QString> >::Iterator git = all.begin();
         git != all.end(); ++git) {
        QString group = git.key();
        QMap<QString, QString>& props = git.data();

        /* v36 决定性修复：SysRegistry 里的 [AutoRun] 元组（无 Type 键）说明
           "哪些应用开机自启（守护进程）"：
             [AutoRun]
             Apps = 220072c6-…(Messages);95ffa462-…(Browser);0a92f2c5-…(Contacts)
           原库 createObject @0x41af8330 实测（0x41af9770..0x41af98xx）：
             组名 == QString("AutoRun") →
               取 "Apps" 值 → QStringList::split(QChar(';'), v) →
               逐项 append 到 m_pAutoRunList(0x28) → 跳过建对象。
           旧实现完全没读 → getAutoRunApps(0x41afffd4) 永远返回空表 →
           am 的 slotLaunchAutoRun() 什么都不启动 → Messaging/Contacts 这两个
           Daemon=1 的守护进程根本没被拉起，点图标只是发 QCOP 唤醒一个不存在的
           进程 → "短信启动不了"。
           注意：这两个应用不在 UserMenuTree 的 All 组里（All 只有 38 项，
           Messages/Contacts 不在其中），所以只可能由 AutoRun 拉起。 */
        if (group == QString("AutoRun")) {
            QMap<QString, QString>::ConstIterator ai = props.find("Apps");
            if (ai != props.end() && m_pAutoRunList) {
                QStringList uu = QStringList::split(QChar(';'), ai.data());
                int n = 0;
                for (QStringList::ConstIterator ui = uu.begin(); ui != uu.end(); ++ui) {
                    QString u = (*ui).stripWhiteSpace();
                    if (u.isEmpty())
                        continue;
                    if (m_pAutoRunList->find(u) == m_pAutoRunList->end()) {
                        m_pAutoRunList->append(u);
                        ++n;
                    }
                }
                alog("[createObject] AutoRun apps=%d -> %s",
                     n, (const char*)ai.data().utf8());
            }
            continue;                       /* 元组本身不是应用对象 */
        }

        /* v43：onlyNew 模式下跳过已存在的组。
           为什么需要：loadMenuTree 末尾会调 handleCardPlugIn(4)（原库同），
           而 loadMenuTree 在它之前已经把卡应用链进 Folder::m_appList 了。
           若这里再全量重建，3 个卡对象会**被新建一份覆盖进 m_pAppMap**，
           于是 Folder 里挂的是旧对象、m_pAppMap 里是新对象 —— 指针分裂，
           外加每次启动泄漏 3 个对象。原库 handleCardPlugIn 用
           getGroupsKeyList 取卡注册表与菜单表的组名做差集、再逐个
           getAppObject() 判存在性，已存在的一律跳过，所以那一步是空操作。
           实测（v43 详细日志）二次进入确实是 `created=3 map=79`（map 没涨），
           正是重复创建的证据。 */
        if (onlyNew && m_pAppMap->contains(group))
            continue;

        /* 读组内 "Type" 决定对象类型（原库 readEntry+toInt） */
        int t = -1;
        QMap<QString, QString>::ConstIterator ti = props.find("Type");
        if (ti != props.end())
            t = ti.data().toInt();
        AM_AppObject* obj = m_pAppFactory->createAppObject((AM_AppObject::Type)t);
        if (!obj)
            continue;
        obj->createObject(props);
        /* v29 决定性修正：Folder 的注册表组里没有 "AppID" 键，AM_Folder::createObject
           只读 Type/BigIcon/Icon/AniIcon/LockEnabled/FixedPosition/Name，
           因此文件夹对象的 FUUID(0x1C) 一直是空串。实测后果（v29 日志）：
             [Folder::getAppList] folder=0xeeba8 uuid='' name='All' 原有=34
             [getAppObject] MISS '' (共81项) × N
           am 用 folder->UUID() 回查对象，拿到空串 → 调整顺序进不去、按 folder 定位失败。
           原库 UserMenuTree 的组名就是 folder 的 uuid（removeAppFromMainMenu 里
           组名取 parent+0x1C 可直接印证），故这里用注册表组名回填 UUID。 */
        if (obj->UUID().isEmpty())
            obj->setUUID(group);
        /* 0x0C 安装位置类型（v17）：getExecFullPath（0x41af1518）按此分支拼接：
           1=native(/usr/SYSqtapp/<Dir>/<Exec>) 2=preload/qtapps 3=preload/javaapps
           4/5=getInstalledQtDir（卡上应用）。getInstalledAppList（0x41b064b4）
           过滤 0x0C∈{4,5} = 可卸载应用（卡上/Java）。 */
        /* v36 决定性修正：删除 obj->setDeviceSlot()！
           0x0C 由 AM_AppLnk::createObject 从注册表 "Attribute" 读出（原库实证），
           这里再按 RegFileType 覆盖成 1/4/5 会把 Attribute 彻底冲掉。
           实测后果（v35 日志，全部 slot=1）：
             [getExecFullPath] dir=gotone  exec=gotone  slot=1 -> /usr/SYSqtapp/gotone/gotone
             [getExecFullPath] dir=service exec=service slot=1 -> /usr/SYSqtapp/service/service
           而真实位置是 /usr/data_resource/preloadapps/qtapps/{gotone,service}/
           （注册表 Attribute = 2 → getPreloadDir()+"/qtapps"）。
           → 中国移动(China Mobile In)、客户服务(Customer Services) 拿到的可执行
             路径与图标路径都不存在 → 点了没反应 / 主桌面第 4 个快捷书签空缺。
           SysRegistry 72 组里 71 组带 Attribute（唯一缺的是 [AutoRun] 元组），
           CardRegistry 3/3、InstalledDB 8/8 均带，故按 Attribute 完全够用。 */
        /* 反编译 0x41af8330 switch(type)：FTemp044 设备来源
           case4(CardRegistry)=2，case1(SysRegistry)/3(UserRegistry)/7(InstalledDB)=1，
           其余=0xffffffff。handleCardPlugOut 判 ==2 卸载卡应用。 */
        int deviceSource = 0xffffffff;
        switch (type) {
        case CardRegistry:  deviceSource = 2; break;
        case SysRegistry:   deviceSource = 1; break;
        case UserRegistry:  deviceSource = 1; break;
        case InstalledDB:   deviceSource = 1; break;
        default:            break;
        }
        obj->setDeviceSource(deviceSource);
        /* v43 决定性补齐：卡注册表里的应用必须过一遍 checkAndUpdateCardApp。
           反编译 0x41b08a70（handleCardPlugIn）对每个卡应用依次做
             createAppObject → setName → checkAndUpdateCardApp → FTemp044=2
           其中 checkAndUpdateCardApp(0x41b0b940) 的语义是
             Type!=2 → return；Device(0x0C)∈{3,5} → return；否则 Device=4。
           Device(0x0C) 在本库由 AM_AppLnk::createObject 从注册表 "Attribute" 读出，
           而卡注册表里的 Attribute 并不可靠：实测 /mmc/mmca1/.system/java/CardRegistry
             软件管理 Attribute=4、Telnet Attribute=4、**Engineering Attribute=1**
           Engineering 不纠正就会停在 Device=1，getExecFullPath 走
           /usr/SYSqtapp/ap_tcmd/efem（不存在）→ 卡上这个应用点了没反应。
           原库 handleCardPlugIn 只有一个出口，本库把它收敛到 createObject 里，
           对 CardRegistry 的每个对象等价执行，行为一致。 */
        if (type == CardRegistry)
            checkAndUpdateCardApp(*obj);
        /* v31 决定性修正：映射表恒以【注册表组名】为键。
           实测反例：SysRegistry 里
             [fc4eee73-2b5b-4ebd-b6dc-32dd62fbd490] AppID = {af8770bf-...}
           组名与 AppID 不同；而 UserMenuTree 的 Items、am 的 getAppObject()
           一律按组名引用。旧实现以 AppID 作键 → 该应用查不到 → 图标点了没反应、
           在菜单里"丢失"。原库 UserMenuTree 的组名 = 对象 uuid（removeAppFromMainMenu
           取 parent+0x1C 作组名即可印证），故键必须用组名。 */
        QString key = group;
        if (key.isEmpty())
            key = obj->UUID();
        (*m_pAppMap)[key] = obj;
        ++created;
    }
    alog("[createObject] type=%d created=%d map=%d", (int)type, created,
         (int)m_pAppMap->count());
}

/* ============ copyTextFile ============ */
/* 反编译 0x41aff114（导出函数，size=860）：返回 int（成功1/失败0），
   initRegistryFiles 用 while(!copyTextFile(...)) 控制重试（最多3次）。
   逻辑：
     src/dst 任一为空 → return 0；
     src 不存在 → return 0；
     dst 已存在 → return 1（无需复制）；
     否则 QFile 打开源/目标，QTextStream 逐行 readLine → operator<< + '\n' 复制，
     close/flush/close 后 changeFileAttr(dst)，成功 return 1。 */
bool AM_RegistryManager::copyTextFile(const QString& src, const QString& dst)
{
    ring_put(50, (unsigned)&src, 0);
    if (src.isEmpty() || dst.isEmpty())
        return false;
    QFileInfo fiSrc(src);
    if (!fiSrc.exists())
        return false;
    QFileInfo fiDst(dst);
    if (fiDst.exists())
        return true;   /* dst 已存在 → 无需复制 */

    QFile in(fiSrc.filePath());
    QFile out(fiDst.filePath());
    if (!in.open(IO_ReadOnly)) {
        /* 反编译 LAB_41aff3dc：源打开失败时检查 dst 是否已有内容（之前已复制成功） */
        return (fiDst.size() != 0);
    }
    if (!out.open(IO_WriteOnly)) {
        in.close();
        return false;
    }
    QTextStream tin(&in);
    QTextStream tout(&out);
    while (!tin.eof()) {
        QString line = tin.readLine();
        tout << line;
        tout << '\n';
    }
    out.close();
    out.flush();
    in.close();
    changeFileAttr(fiDst.filePath());
    return true;
}

/* ============ changeFileAttr ============ */
/* 反编译 0x41afef34（导出函数）：返回 bool。
     chmod(path.latin1(), 0x1ff/*0777*)
     chown(path.latin1(), 2000, 0xe9)
     return (chown > -1) && (chmod == 0); */
bool AM_RegistryManager::changeFileAttr(const QString& path)
{
    int rchmod = chmod(path.latin1(), 0x1ff);   /* 0777 */
    int rchown = chown(path.latin1(), 2000, 0xe9);
    /* v37：卸载链上会被调用，打日志以便把卡死点夹在两次日志之间 */
    alog("[changeFileAttr] %s chmod=%d chown=%d",
         (const char*)path.utf8(), rchmod, rchown);
    return (rchown > -1) && (rchmod == 0);
}

/* ============ createUuid ============ */
/* 反编译 0x41afedb8（导出函数，size=380）：
   createFolder / addAppToRegistry 调用时把栈上 16 字节 QUuid 缓冲区地址当 this 传入，
   本函数生成 UUID v4 写入该缓冲区：
     首次调用：QDateTime::currentDateTime → tm(sec/min/hour/mday/mon-1/year-1900/isdst=-1)
               → mktime → srand(seed)，再 rand() 丢弃一个；
     之后：清零 this[0..3]（4×32bit），循环 rand() 填充（每 32bit 用步长 randBits）；
           this[8] 设 variant 位：~((byte)((((byte)this[8]&0x3f)<<0x19)>>0x18)>>1)；
           this[6]（ushort）设 version：& 0xfff | 0x4000。
   注：this 是调用方的栈缓冲区，非真实 AM_RegistryManager 对象。 */
void AM_RegistryManager::createUuid()
{
    /* 静态初始化标志 + 随机步长（反编译 iVar6+DAT 槽位） */
    static int  s_randBits = 0;
    static bool s_seeded   = false;
    if (!s_seeded) {
        /* 计算 rand() 能产生的位数：0x7fffffff 反复 >>1 直到 0 */
        int bits = 0x7fffffff, n = 0;
        do { bits >>= 1; n++; } while (bits != 0);
        s_randBits = n;
        /* 用当前时间做种子 */
        QDateTime dt = QDateTime::currentDateTime();
        struct tm t;
        t.tm_sec  = dt.time().second();
        t.tm_min  = dt.time().minute();
        t.tm_hour = dt.time().hour();
        t.tm_mday = dt.date().day();
        t.tm_mon  = dt.date().month() - 1;
        t.tm_year = dt.date().year() - 1900;
        t.tm_isdst = -1;
        time_t seed = mktime(&t);
        srand((unsigned int)(seed < 0 ? 0xffffffffu : (unsigned int)seed));
        rand();   /* 丢弃首个 */
        s_seeded = true;
    }

    /* this 当作 16 字节缓冲区：清零后用 rand 填充 4 个 32bit 字 */
    unsigned char* buf = reinterpret_cast<unsigned char*>(this);
    for (int i = 0; i < 16; i++) buf[i] = 0;
    /* 反编译：iVar4 从 3 递减到 0，写 this + iVar4*4（即字 [3],[2],[1],[0]） */
    for (int w = 3; w >= 0; w--) {
        unsigned int val = 0;
        for (int shift = 0; shift < 32; shift += s_randBits)
            val |= (unsigned int)rand() << (shift & 0xff);
        *reinterpret_cast<unsigned int*>(buf + w * 4) = val;
    }
    /* variant 位 */
    buf[8] = (unsigned char)~((unsigned char)(((buf[8] & 0x3f) << 0x19) >> 0x18) >> 1);
    /* version = 4 */
    unsigned short* p16 = reinterpret_cast<unsigned short*>(buf + 6);
    *p16 = (unsigned short)((*p16 & 0xfff) | 0x4000);
}

/* ============ getAllAppList ============ */
/* 反编译 0x41af6f6c（导出函数，size=676）：遍历 m_pAppMap，
   跳过 getType()==Folder(1) 和 getType()==EmbeddedLink(3) 的对象
   （只返回 Application/Shortcut），其余 append 到 list。
   （原库另对两个特定 DRM UUID 做 fopen 授权文件探测，属运行期平台路径，此处略。） */
/* ============ getAllAppList ============ */
/* 反编译 0x41af6f6c：遍历 m_pAppMap（通过 QGList）。
   过滤逻辑：
     1. 仅跳过 type == Folder(1)（原库只检查 *ptr+8 != 1，不过滤 EmbeddedLink）
     2. 若 FTemp040@0x40 != 0（可见）：直接 append
     3. 若 FTemp040@0x40 == 0（隐藏）：
        - DRM1 UUID 匹配（FTemp020..FTemp02C）→ append
        - DRM2 UUID 匹配 AND fopen(license_path) 成功 → append
        - 否则 → 跳过
   DRM1/DRM2 UUID 和 license_path 由 GOT 运行期解析，此处实现类型过滤+可见性；
   DRM UUID 详细比较依赖 GOT 值，无法静态重建，注释标记位置。 */
void AM_RegistryManager::getAllAppList(QList<AM_AppObject>& list) const
{
    /* v25（1:1 补齐，0x41af6f6c 字面池已全部解出）：
       原库等价 C：
         list.clear();                       // QGList 虚表 slot1（反编译行 30）
         for (obj in m_pAppMap) {
             if (obj == 0 || obj->Type == 1) continue;
             if (obj->FTemp040 == 0) {       // 未置可见标志的对象走 DRM 判定
                 if (uuid != DRM1) {
                     if (uuid != DRM2) continue;
                     if ((f = fopen(LICENSE,"r")) == 0) continue;
                     fclose(f);
                 }
             }
             list.append(obj);
         }
       字面池实测（.got 槽解码）：
         DRM1 uuid  = 9667de53-e43e-47bd-96ff-89c06dc3cdbd
         DRM2 uuid  = b92adf27-d073-451d-a990-4ac5e0c87596
         LICENSE    = /usr/data_resource/preloadapps/qtapps/mflashplayer/MFlashPlayer  ("r")
       即：FTemp040==0 的对象只有「命中 DRM1」或「命中 DRM2 且预置 license 存在」才收录。 */
    static const char* kDrm1 = "9667de53-e43e-47bd-96ff-89c06dc3cdbd";
    static const char* kDrm2 = "b92adf27-d073-451d-a990-4ac5e0c87596";
    static const char* kLicense = "/usr/data_resource/preloadapps/qtapps/mflashplayer/MFlashPlayer";

    list.clear();
    if (!m_pAppMap)
        return;
    for (QMap<QString, AM_AppObject*>::ConstIterator it = m_pAppMap->begin();
         it != m_pAppMap->end(); ++it) {
        AM_AppObject* obj = it.data();
        if (!obj)
            continue;
        /* 原库仅跳过 Folder(type=1)，不过滤 EmbeddedLink */
        if (obj->getType() == AM_AppObject::Folder)
            continue;
        if (!obj->isVisibleFlag()) {
            /* 原库比较的是 this+0x20 的 QUuid（= 注册表 AppID），不是 FUUID。
               AppID 未填（全零）时回退用 FUUID，避免漏判。 */
            QString u = amNormUuid(obj->appId().toString());
            if (u.isEmpty() || u == "00000000-0000-0000-0000-000000000000")
                u = amNormUuid(obj->UUID());
            if (u != kDrm1) {
                if (u != kDrm2)
                    continue;
                FILE* f = fopen(kLicense, "r");
                if (!f)
                    continue;
                fclose(f);
            }
        }
        list.append(obj);
    }
    AMVLOG("[getAllAppList] map=%d -> list=%d", (int)m_pAppMap->count(), (int)list.count());
}

/* ============ getAppObjById ============ */
/* 反编译 0x41af6ca0：遍历 m_pAppMap，仅匹配 Type==Application(2)，
   比较对象 QUuid@0x20-0x2C 与参数 uuid，返回对象。 */
/* UUID 规范化比较（v12）：QUuid::toString() 可能带花括号，统一去括号后比较 */
static QString amNormUuid(const QString& s)
{
    QString t = s.stripWhiteSpace();
    int len = t.length();
    if (len > 2 && t[0] == '{' && t[len - 1] == '}')
        t = t.mid(1, len - 2);
    return t;
}

AM_AppObject* AM_RegistryManager::getAppObjById(const QUuid& uuid) const
{
    {   /* 记录 uuid 字符串内容 */
        /* v43：原写法 `const char* sc = uuid.toString().latin1();` 是**悬垂指针**——
           toString() 返回的临时 QString 在语句结束即析构，latin1() 的缓冲区随之释放，
           后面循环读的是已释放堆内存（实测会污染 malloc → 启动期 SIGSEGV）。
           必须让 QString 活到读完。 */
        QString us = uuid.toString();
        const char* sc = us.latin1();
        unsigned a = 0, b = 0;
        if (sc) {
            for (int i = 0; i < 8 && sc[i]; ++i)
                a |= ((unsigned)(unsigned char)sc[i]) << (i*8);
            for (int i = 8; i < 16 && sc[i]; ++i)
                b |= ((unsigned)(unsigned char)sc[i]) << ((i-8)*8);
        }
        ring_put(20, a, b);
    }
    if (uuid.isNull() || !m_pAppMap)
        return 0;
    QString want = amNormUuid(uuid.toString());
    /* v36：同一 AppID 被多个组复用时（c1e0df66 = Camera/Camcord/Camcord快捷方式），
       优先返回"主对象"——组名与 AppID 相同、且 Type==Application 的那个。
       旧实现取 QMap 顺序首个 Type=2（命中 5f05b101=Camcord）→ am 的
       getAppObject(AppID) / getAppLnkById 拿到 Camcord，照相机入口错乱。 */
    AM_AppObject* fallback = 0;
    for (QMap<QString, AM_AppObject*>::ConstIterator it = m_pAppMap->begin();
         it != m_pAppMap->end(); ++it) {
        AM_AppObject* obj = it.data();
        if (!obj)
            continue;
        if (obj->getType() != AM_AppObject::Application)
            continue;
        /* QUuid@0x20 与 FUUID 字符串等价（反编译以字节序列比较） */
        if (amNormUuid(obj->UUID()) != want)
            continue;
        if (amNormUuid(it.key()) == want) {
            AMVLOG("[getAppObjById] HIT(primary) uuid=%s -> %s", (const char*)want.utf8(),
                 (const char*)obj->getName().utf8());
            return obj;
        }
        if (!fallback)
            fallback = obj;
    }
    if (fallback) {
        AMVLOG("[getAppObjById] HIT uuid=%s -> %s", (const char*)want.utf8(),
             (const char*)fallback->getName().utf8());
        return fallback;
    }
    AMVLOG("[getAppObjById] MISS uuid=%s", (const char*)want.utf8());
    return 0;
}

/* ============ getAppLnkById ============ */
/* 反编译 0x41aea578（AppDataInterface 委托）：
   getAppObjById(uuid) 且 Type==Application(2) */
AM_AppLnk* AM_RegistryManager::getAppLnkById(const QUuid& uuid) const
{
    AM_AppObject* obj = getAppObjById(uuid);
    if (obj && obj->getType() == AM_AppObject::Application)
        return static_cast<AM_AppLnk*>(obj);
    return 0;
}

/* ============ getAppLnkBySerialId ============ */
/* 反编译 0x41aea5c0（AppDataInterface 委托）：
   getAppObject(serialId) 且 Type==Application(2) */
AM_AppLnk* AM_RegistryManager::getAppLnkBySerialId(const QString& serialId) const
{
    AM_AppObject* obj = getAppObject(serialId);
    if (obj && obj->getType() == AM_AppObject::Application)
        return static_cast<AM_AppLnk*>(obj);
    return 0;
}

/* ============ getJavaObj ============ */
/* 反编译 0x41af6da8：遍历 m_pAppMap，仅匹配 Type==Application(2)，
   比较 AM_AppLnk::m_javaId@0x6C 与 javaId，返回对象。 */
AM_AppObject* AM_RegistryManager::getJavaObj(const int& javaId) const
{
    if (!m_pAppMap)
        return 0;
    for (QMap<QString, AM_AppObject*>::ConstIterator it = m_pAppMap->begin();
         it != m_pAppMap->end(); ++it) {
        AM_AppObject* obj = it.data();
        if (!obj)
            continue;
        if (obj->getType() != AM_AppObject::Application)
            continue;
        AM_AppLnk* lnk = static_cast<AM_AppLnk*>(obj);
        if (lnk->getJavaId() == javaId)
            return obj;
    }
    return 0;
}

/* ============ getRootFolder ============ */
/* 反编译 0x41af6e48：getAppObject(<根文件夹键名>)。
   字面池解码确认：根文件夹的注册键是 UUID
   "2a58c0d6-05c6-45aa-8938-63059d41a4e9"（rodata 0x41b18d80），
   而非硬编码字符串——EZX 用固定 UUID 标识根文件夹。 */
AM_Folder* AM_RegistryManager::getRootFolder() const
{
    return static_cast<AM_Folder*>(getAppObject(
        QString("2a58c0d6-05c6-45aa-8938-63059d41a4e9")));
}

/* ============ getAppObject ============ */
/* v39 决定性修正：删掉原来的 `static int s_n; if (s_n < 400) { ... }` 守卫。
 *
 * 那个守卫本来只是为了"日志最多 400 条"，却把**未命中的 return 0** 也一起包进了
 * if 里，导致 `return it.data()` 落在 if 之外：
 *
 *     static int s_n = 0;
 *     if (s_n < 400) {
 *         ++s_n;
 *         if (it == m_pAppMap->end()) { ... return 0; }   // 未命中
 *         ...日志...
 *     }
 *     return it.data();        // ← 第 401 次以后未命中就掉到这里，返回 end() 的垃圾
 *
 * 实测崩溃（v38 日志，第二次卸载）：
 *     pc=0x41AF82C4  = loadMenuTree+0x5FC
 *     41AF829C bl getAppObject        → r0 = 0x20003D00（野值）
 *     41AF82C4 strb r3,[r4,#0x40]     → 写 obj->visible，fault addr = 0x20003D40
 * 这正好解释"第一次卸载正常、第二次卡死"：s_n 是进程级静态，正常用一会就过 400，
 * 此后任何一次"查不到"都会返回垃圾指针。
 *
 * 日志量现在由 AMVLOG（默认关闭）控制，不再需要这个计数器。 */
AM_AppObject* AM_RegistryManager::getAppObject(const QString& name) const
{
    {   /* 记录查找键内容（前16字节），定位 am 死等/轮询的 key */
        const char* sc = name.latin1();
        unsigned a = 0, b = 0;
        if (sc) {
            for (int i = 0; i < 8 && sc[i]; ++i)
                a |= ((unsigned)(unsigned char)sc[i]) << (i*8);
            for (int i = 8; i < 16 && sc[i]; ++i)
                b |= ((unsigned)(unsigned char)sc[i]) << ((i-8)*8);
        }
        ring_put(21, a, b);
    }
    if (!m_pAppMap)
        return 0;

    QMap<QString, AM_AppObject*>::ConstIterator it = m_pAppMap->find(name);

    /* ---- 命中 ---- */
    if (it != m_pAppMap->end()) {
        if (amLogVerbose()) {
            amLogWrite("[getAppObject] HIT '%s' -> %p type=%d name='%s'",
                       (const char*)name.utf8(), (void*)it.data(),
                       (int)it.data()->getType(),
                       (const char*)it.data()->getName().utf8());
            /* 卸载分派完全由 obj+0x0C 决定，Java 卸载还会读 obj+0x6C 当 JavaId
               （ezxaid 0x42326254 `ldr r1,[r1,#0x6c]`）。字段不对就会把对象
               交给错误的管理器 → 卸载卡死/崩溃。 */
            if (it.data()->getType() == AM_AppObject::Application) {
                AM_AppLnk* l = static_cast<AM_AppLnk*>(it.data());
                int sl = l->getDeviceSlot();
                if (sl == 3 || sl == 4 || sl == 5 || sl == 8)
                    amLogWrite("[getAppObject]   slot=0x%X dev=0x%X javaId=%d uid=%d gid=%d exec='%s'",
                               sl, l->getDeviceSource(), l->getJavaId(),
                               l->getUid(), l->getGid(),
                               (const char*)l->getExecPath().utf8());
            }
        }
        return it.data();
    }

    /* ---- 未命中：兜底①按对象 FUUID（AppID）反查 ----
       有些调用方用 AppID 而非组名。同一 AppID 可能被多个注册表组复用，实测
       SysRegistry 里 c1e0df66-11ef-4b33-bddb-0cbd13827312 出现 3 次：
         [c1e0df66-…] Type=2 Attribute=1 Camera   Args=photo
         [5f05b101-…] Type=2 Attribute=1 Camcord   Args=video
         [5e558577-…] Type=4 Attribute=6 Camcord   Action=RecordingVideo ← 快捷方式
       取 QMap 顺序第一个会拿到快捷方式 → 录像机/照相机启动错乱。
       优先级：Type=Application 且 组名==AppID（"主对象"） > 其它 Application > 任意 */
    AM_AppObject* best = 0;
    QString bestKey;
    int bestRank = 99;
    for (QMap<QString, AM_AppObject*>::ConstIterator ci = m_pAppMap->begin();
         ci != m_pAppMap->end(); ++ci) {
        if (!ci.data())
            continue;
        if (amNormUuid(ci.data()->UUID()) != amNormUuid(name))
            continue;
        int rank;
        if (ci.data()->getType() == AM_AppObject::Application)
            rank = (amNormUuid(ci.key()) == amNormUuid(name)) ? 0 : 1;
        else
            rank = 2;
        if (rank < bestRank) {
            bestRank = rank;
            best = ci.data();
            bestKey = ci.key();
            if (rank == 0)
                break;
        }
    }
    if (best) {
        AMVLOG("[getAppObject] 兜底命中(AppID) '%s' -> 组名 %s rank=%d",
               (const char*)name.utf8(), (const char*)bestKey.utf8(), bestRank);
        return best;
    }

    /* ---- 兜底②按显示名反查（只用于日志提示，不返回） ---- */
    QString cand;
    for (QMap<QString, AM_AppObject*>::ConstIterator ci = m_pAppMap->begin();
         ci != m_pAppMap->end(); ++ci) {
        if (ci.data() && ci.data()->getName() == name) {
            cand = ci.key();
            break;
        }
    }
    AMVLOG("[getAppObject] MISS '%s' (共%d项) 按名候选=%s",
           (const char*)name.utf8(), (int)m_pAppMap->count(),
           cand.isEmpty() ? "(无)" : (const char*)cand.utf8());
    return 0;
}

/* ============ getLockEnabledAppList ============ */
void AM_RegistryManager::getLockEnabledAppList(QList<AM_AppObject>& list) const
{
    list.clear();
    if (!m_pAppMap)
        return;
    for (QMap<QString, AM_AppObject*>::ConstIterator it = m_pAppMap->begin();
         it != m_pAppMap->end(); ++it) {
        AM_AppObject* obj = it.data();
        if (obj && obj->getAppLockEnabled())
            list.append(obj);
    }
}

/* ============ getAppLockValue ============ */
/* 反编译 0x41af6698：遍历 m_pAppMap 找到 UUID 对应对象，返回其 lockValue。 */
int AM_RegistryManager::getAppLockValue(const QUuid& uuid) const
{
    AM_AppObject* obj = getAppObjById(uuid);
    if (obj)
        return obj->getAppLockValue();
    return -1;   /* 反编译：return 0xffffffff */
}

/* ============ setAppLockValue ============ */
/* 反编译 0x41af67b8（size=1256），完整语义四步：
     1) if (uuid.isNull()) return;                       ← 0x41af67d8 isNull 早退
     2) cfg = getConfigFileName(6)=AppLockConfig；ZConfig writeEntry(组,取值,值)
        组名是 "AppLockValue"（GOT[0xcf8]）              ← 我们此前写成 ""
     3) cfg.flush()
     4) QCopChannel::send("AM/RegistryiMgr/Channel",     ← 0x41af6a54
                          "AppLockValueChanged()")       我们此前完全没有
   第 4 步是跨进程通知：别的进程（am / 我的收藏 / 设置）各自持有
   AM_RegistryManager 实例，靠这条消息才知道要重读锁值。
   v63 补齐 1) 4) 与组名，否则"改了应用锁但其它界面不生效"。 */
void AM_RegistryManager::setAppLockValue(const QUuid& uuid, int value)
{
    if (uuid.isNull())
        return;                       /* 原库 0x41af67e4：空 UUID 直接返回 */

    AM_AppObject* obj = getAppObjById(uuid);
    if (obj)
        obj->setAppLockValue(value);

    QString cfg = getConfigFileName(static_cast<RegFileType>(6));
    ZConfig zc(cfg, false);
    zc.writeEntry("AppLockValue", uuid.toString(), value);
    zc.flush();

    QCopChannel::send(QCString("AM/RegistryiMgr/Channel"),
                      QCString("AppLockValueChanged()"));
}

/* ============ getAutoRunApps ============ */
/* 反编译 0x41afffd4：遍历 m_pAutoRunList（QValueList<QString> UUID），
   用 getAppObject(uuid) 找到对象后 append 到 list。 */
void AM_RegistryManager::getAutoRunApps(QList<AM_AppLnk>& list) const
{
    ring_put(33, (unsigned)&list, 0);
    list.clear();
    if (!m_pAutoRunList)
        return;
    for (QValueList<QString>::ConstIterator it = m_pAutoRunList->begin();
         it != m_pAutoRunList->end(); ++it) {
        AM_AppObject* obj = getAppObject(*it);
        if (obj && obj->getType() == AM_AppObject::Application)
            list.append(static_cast<AM_AppLnk*>(obj));
    }
}

/* ============ getOpenWithApps ============ */
/* 反编译 0x41b00094（size=1928）：按 path 扩展名取 MIME UUID，遍历应用按 UUID 匹配，
   命中的 Application 加入 OpenWithApp 列表。
   流程：
     1. AM_MimeType::instance()->getMimeIdByExt(path.lower()) -> mimeId
     2. mimeId 为空 -> return（operator== 与 null 比较）
     3. getMimeTypeDataEx(mimeId) -> mimeData；为 NULL -> return
     4. 从 mimeData+0x08 取 MIME UUID（toQUuid() 展开为 QUuid）
     5. getAllAppList() 遍历，比较 AppLnk 的 QUuid@0x20-0x2C 与 MIME UUID
     6. 命中：new OpenWithApp(0x2C)，填 UUID=AppLnk UUID、name=getName()、
        icon=getSmallIcon()、matched=1，append。
   AM_MimeType 是外部平台类（AM_MimeType.h 已声明接口，运行期由 libezx-mime 解析）。 */
void AM_RegistryManager::getOpenWithApps(const QString& path,
                                         QList<OpenWithApp>& list) const
{
    /* ★v49 诊断：这是 libezxmystuff_album / mystuff（我的收藏·文件管理器）
       唯一真正调用的重函数（符号实证：album 只从 libezxam 取 3 个符号
       —— RMCtor / RMDtor / getOpenWithApps）。它被调用时一定伴随
       "在别的进程里 new 一个 AM_RegistryManager"（655ms 全量菜单装载），
       所以这里恒写一行日志，便于定位"我的收藏崩溃"到底卡在哪。 */
    list.clear();
    if (path.isEmpty())
        return;

    AM_MimeType* mime = AM_MimeType::instance();
    if (!mime) {
        AMLOG("[getOpenWithApps] path=%s -> mime instance NULL", (const char*)path.latin1());
        return;
    }
    QString mimeId = mime->getMimeIdByExt(path.lower());
    if (mimeId.isNull() || mimeId.isEmpty()) {
        AMLOG("[getOpenWithApps] path=%s -> mimeId empty", (const char*)path.latin1());
        return;
    }

    AM_MimeData* mimeData = mime->getMimeTypeDataEx(mimeId);
    if (!mimeData) {
        AMLOG("[getOpenWithApps] path=%s mimeId=%s -> mimeData NULL",
               (const char*)path.latin1(), (const char*)mimeId.latin1());
        return;
    }
    QUuid mimeUuid = mimeData->toQUuid();

    QList<AM_AppObject> appList;
    getAllAppList(appList);
    for (AM_AppObject* obj = appList.first(); obj; obj = appList.next()) {
        if (!obj)
            continue;
        /* 比较 AppLnk 的 QUuid@0x20-0x2C 与 MIME UUID */
        QUuid appUuid(obj->UUID());
        if (!(appUuid == mimeUuid))
            continue;
        if (obj->getType() != AM_AppObject::Application)
            continue;
        OpenWithApp* app = new OpenWithApp;
        app->uuid    = appUuid;
        app->name    = obj->getName();
        obj->getSmallIcon(app->icon);
        app->matched = true;
        list.append(app);
        AMLOG("[getOpenWithApps] hit name='%s'", (const char*)app->name.latin1());
    }
    AMLOG("[getOpenWithApps] path=%s mimeId=%s apps=%d matched=%d",
           (const char*)path.latin1(), (const char*)mimeId.latin1(),
           (int)appList.count(), (int)list.count());
}

/* ============ getInstalledAppList ============ */
/* 反编译 0x41b064b4：遍历 m_pAppMap，仅匹配 Type==Application(2) 且
   Device(0x0C)==4(Preload)或5(Card)；
   若 !param2 还需 m_visible@0x80 != 0。 */
void AM_RegistryManager::getInstalledAppList(QList<AM_AppLnk>& list, bool b) const
{
    ring_put(31, (unsigned)&list, b);
    list.clear();
    if (!m_pAppMap)
        return;
    for (QMap<QString, AM_AppObject*>::ConstIterator it = m_pAppMap->begin();
         it != m_pAppMap->end(); ++it) {
        AM_AppObject* obj = it.data();
        if (!obj)
            continue;
        if (obj->getType() != AM_AppObject::Application)
            continue;
        AM_AppLnk* lnk = static_cast<AM_AppLnk*>(obj);
        int dev = lnk->getDeviceId();
        if (dev != 4 && dev != 5)
            continue;
        if (!b && !lnk->isVisible())
            continue;
        list.append(lnk);
    }
}

/* ============ getAppNameByFullPath ============ */
/* 反编译 0x41b0aab8：遍历 Application 型对象，将 exec 路径的
   前缀目录替换后与 param_1 比较；匹配则返回对象 getName()。 */
void AM_RegistryManager::getAppNameByFullPath(const QString& path, QString& name) const
{
    name = QString::null;
    if (!m_pAppMap || path.isEmpty())
        return;
    for (QMap<QString, AM_AppObject*>::ConstIterator it = m_pAppMap->begin();
         it != m_pAppMap->end(); ++it) {
        AM_AppObject* obj = it.data();
        if (!obj || obj->getType() != AM_AppObject::Application)
            continue;
        AM_AppLnk* lnk = static_cast<AM_AppLnk*>(obj);
        if (lnk->getExecFullPath() == path) {
            name = obj->getName();
            return;
        }
    }
}

/* ============ getAppAutoStartStatusFromDmt ============ */
/* 反编译 0x41b0ae70：
   bool getAppAutoStartStatusFromDmt(const QString& path) {
       if (!DmtTreeFactory::Initialize()) return false;
       ... GetSubtree(path) → 节点虚调用(vtable+0x1c) → DmtData::GetBoolean ...
       DmtTreeFactory::Uninitialize();
       return <查询到的布尔值>;
   }
   关键：返回类型必须是 bool（loadRegistry 用其返回值 cVar1 判断分支）。
   子树查询/取值依赖平台对象的虚表派发(vtable+0x1c)，这些平台类
   (DmtData/JemSmartPtr/DmtPrincipal)无公开头文件，无法在不臆造 ABI 的前提下
   逐条重建虚调用。故保留 ABI 正确的最小实现：调用真实平台入口
   Initialize()/Uninitialize()（符号在原库同为 undefined，运行期解析），
   查询路径返回 false（等价于原库 Initialize 失败时的返回值）。
   调用方拿到正确的 bool 返回类型，不会读到未定义垃圾值。 */
bool AM_RegistryManager::getAppAutoStartStatusFromDmt(const QString& path) const
{
    if (!DmtTreeFactory::Initialize())
        return false;   /* Initialize 失败 → 原库 return 0 */
    /* 子树查询/取布尔值需平台虚表派发，此处无法安全重建；返回 false。 */
    DmtTreeFactory::Uninitialize();
    return false;
}

/* ============ getSystemFolderName ============ */
/* 反编译 0x41b083cc（1000B，v48 逐句复原）。
   形参是 **UUID**（不是"路径"），逐条与系统文件夹 UUID 比对后写本地化名。
   字符串常量已用 gotslot.py 从字面池实测（括号内为 GOT 槽）：
     0xd74 cb839c8f-…  ALL         0xe3c/0xe40 TXT_RID_CBUTTON_ALL/All
     0xde8 e8c83dac-…  OFFICE      0xdec/0xdf0 …OFFICE/Office
     0xdf4 b18dbbd2-…  MULTIMEDIA  0xdf8/0xdfc …MULTIMEDIA/Multimedia
     0xda0 67dc1191-…  GAMES       0xe00/0xe04 …GAMES/Games    ← flexbit(0xae)=0
                                   0xe08/0xe0c …APPLICATION/Application ← flexbit=1
     0xe10 51f2cb88-…  EXTRA       0xe14/0xe18 …EXTRA/Extra
     0xe1c 48973844-…  USER1       0xe20/0xe24 …USER1/User1
     0xe28 3bb7f1b5-…  USER2       0xe2c/0xe30 …USER2/User2   ← flexbit(0xae)=0
                                   0xe34/0xe38 …CMCC/CMCC     ← flexbit=1
   默认（都不匹配）走 0x41b086d0：name = QString::null。

   ★ 原库的一处「怪癖」，已按字节实证、照抄不改：
     "All" 分支（0x41b086e4）赋值后 **不返回**，而是 `b 0x41b083fc`
     回落到 Office 那条比对链继续走（0x41b08740 = eaffff2d，目标 0x41b083fc，
     与 0x41b08720 的 bne 同目标；两条都指回链头，没有独立收尾）。
     于是 uuid==All 时 name 先被赋 tr("All")，再被链尾的 QString::null 覆盖。
     实测原库就是如此（"全部"这个页签的名字来自 _SysRegistry 的 Name 字段，
     不走本函数），所以这里必须保持一致，不能"修好"。 */
void AM_RegistryManager::getSystemFolderName(const QString& path, QString& name)
{
    if (path == QString("cb839c8f-72c3-42ac-bf17-b01080cb5705")) {
        name = QObject::tr("TXT_RID_CBUTTON_ALL", "All");
        /* 原库在此回落继续比对，最终被链尾 null 覆盖 —— 保持原样 */
    }
    if (path == QString("e8c83dac-db4e-4fb5-98a6-61de1998b277")) {
        name = QObject::tr("TXT_RID_CBUTTON_OFFICE", "Office");
        return;
    }
    if (path == QString("b18dbbd2-503a-4442-9308-5575ee38c210")) {
        name = QObject::tr("TXT_RID_CBUTTON_MULTIMEDIA", "Multimedia");
        return;
    }
    if (path == QString("67dc1191-2928-4938-abce-1a73cb5ce490")) {
        /* 0x41b08500-0x41b08594：SETUP_Utility 临时对象取 flexbit 0xae
           v69：改堆分配——本地只是空壳声明，真身在 libezxappbase（见 v68 事故）。 */
        SETUP_Utility* util = new SETUP_Utility();
        bool flex = false;
        util->getFlexBit((SETUP_FLEX_ID_T)0xae, flex);
        delete util;
        name = flex ? QObject::tr("TXT_RID_CBUTTON_APPLICATION", "Application")
                    : QObject::tr("TXT_RID_CBUTTON_GAMES", "Games");
        return;
    }
    if (path == QString("51f2cb88-3040-4f00-b66c-c4bb974e4d18")) {
        name = QObject::tr("TXT_RID_CBUTTON_EXTRA", "Extra");
        return;
    }
    if (path == QString("48973844-3955-4323-b5a0-f69435a1dba0")) {
        name = QObject::tr("TXT_RID_CBUTTON_USER1", "User1");
        return;
    }
    if (path == QString("3bb7f1b5-12bb-4b79-985b-96436e094256")) {
        SETUP_Utility* util = new SETUP_Utility();
        bool flex = false;
        util->getFlexBit((SETUP_FLEX_ID_T)0xae, flex);
        delete util;
        name = flex ? QObject::tr("TXT_RID_CBUTTON_CMCC", "CMCC")
                    : QObject::tr("TXT_RID_CBUTTON_USER2", "User2");
        return;
    }
    name = QString::null;   /* 0x41b086d0: operator=(GOT[0x1138]=QString::null) */
}

/* ============ getSystemFolderUid ============ */
/* 反编译 0x41b087b4（168B，v48 逐句复原）。
   `cmp r0,#7 / addls pc,pc,r0,lsl#2` 的 8 项跳表在 0x41b087cc 起：
     case0 0x41b087cc -> 0x41b08800 (0xd00) 2a58c0d6-05c6-45aa-8938-63059d41a4e9
     case1 0x41b087d0 -> 0x41b087ec (0xde8) e8c83dac-db4e-4fb5-98a6-61de1998b277
     case2 0x41b087d4 -> 0x41b08808 (0xdf4) b18dbbd2-503a-4442-9308-5575ee38c210
     case3 0x41b087d8 -> 0x41b08810 (0xda0) 67dc1191-2928-4938-abce-1a73cb5ce490
     case4 0x41b087dc -> 0x41b08818 (0xe44) c34463b7-800f-483b-8e01-5ea39730d7d5
     case5 0x41b087e0 -> 0x41b08820 (0xe48) fe948c2a-b7a6-4dac-a1d1-915b3ba57168
     case6 0x41b087e4 -> 0x41b08828 (0xe10) 51f2cb88-3040-4f00-b66c-c4bb974e4d18
     case7 0x41b087e8 -> 0x41b08830 (0xd74) cb839c8f-72c3-42ac-bf17-b01080cb5705
     default (>7)   0x41b08800 (0xd00) = 与 case0 同一个字面量
   每个分支都是 `uid = "字面量"; return uid;`（尾跳 QString::operator=(char const*)）。
   v47 及之前整体错位（All 当 1、User 当 2），已修。 */
void AM_RegistryManager::getSystemFolderUid(SystemFolder folder, QString& uid)
{
    switch (folder) {
    case FolderMainMenu:
        uid = "2a58c0d6-05c6-45aa-8938-63059d41a4e9"; return;
    case FolderOffice:
        uid = "e8c83dac-db4e-4fb5-98a6-61de1998b277"; return;
    case FolderMultimedia:
        uid = "b18dbbd2-503a-4442-9308-5575ee38c210"; return;
    case FolderGames:
        uid = "67dc1191-2928-4938-abce-1a73cb5ce490"; return;
    case FolderReserved4:
        uid = "c34463b7-800f-483b-8e01-5ea39730d7d5"; return;
    case FolderReserved5:
        uid = "fe948c2a-b7a6-4dac-a1d1-915b3ba57168"; return;
    case FolderExtra:
        uid = "51f2cb88-3040-4f00-b66c-c4bb974e4d18"; return;
    case FolderAll:
        uid = "cb839c8f-72c3-42ac-bf17-b01080cb5705"; return;
    default:    /* >7 落回 case0 的字面量，不是 QString::null */
        uid = "2a58c0d6-05c6-45aa-8938-63059d41a4e9"; return;
    }
}

/* ============ getAppParentFolderUid ============ */
/* 反编译 0x41b0885c（204B，v46 逐句重写）：
     uid = QString::null;                       ← 0x41b08884 operator=(GOT[0x1138]=QString::null)
     if (path.isEmpty()) return;                ← [[path]+0xc] == 0
     obj = getAppObject(path);  if (!obj) return;
     folder = *(AM_Folder**)(obj+0x04);         ← 父 Folder 指针（obj+4）
     if (!folder) return;
     uid = *(QString*)(folder+0x1c);            ← 父 Folder 的 FUUID
   旧实现是"遍历 m_pAppMap 所有 Folder，挨个 getAppList 比对指针" ——
   不仅慢（每次调用建 60+ 个临时 QList），而且对"父指针已回填"的对象会
   返回错误的文件夹（找到的是包含它的任意 Folder，不是 obj+4 记录的那个）。
   原库只读 obj+4，一条指令的事。 */
void AM_RegistryManager::getAppParentFolderUid(const QString& path, QString& uid)
{
    uid = QString::null;
    if (path.isEmpty()) {
        AMLOG("[getAppParentFolderUid] %s -> (empty)", (const char*)path.utf8());
        return;
    }
    AM_AppObject* obj = getAppObject(path);
    if (!obj) {
        AMLOG("[getAppParentFolderUid] %s -> (no object)", (const char*)path.utf8());
        return;
    }
    AM_Folder* folder = static_cast<AM_Folder*>(obj->parentFolder());   /* obj+0x04 */
    if (!folder) {
        AMLOG("[getAppParentFolderUid] %s -> (parent=null)", (const char*)path.utf8());
        return;
    }
    uid = folder->UUID();
    AMLOG("[getAppParentFolderUid] %s -> %s", (const char*)path.utf8(),
          (const char*)uid.utf8());
}

/* ============ createFolder ============ */
/* 反编译 0x41afb384：getAppObject(path) 找父文件夹，
   创建 AM_Folder，写 ZConfig(getConfigFileName(3))，加入 m_pAppMap，
   folderChanged 通知。语义等价实现。 */
bool AM_RegistryManager::createFolder(const QString& name, const QString& path,
                                      const QString& icon, const QString& aniIcon)
{
    if (name.isEmpty())
        return false;
    /* 已存在则不重复创建 */
    if (getAppObject(name))
        return false;
    AM_AppObject* folder = m_pAppFactory->createAppObject(AM_AppObject::Folder);
    if (!folder)
        return false;
    folder->setName(name);
    (*m_pAppMap)[name] = folder;

    /* 写配置（等价：createFolder 写 ZConfig type=3），flush 成败决定返回值 */
    ZConfig cfg(getConfigFileName(static_cast<RegFileType>(3)), false);
    cfg.writeEntry(name, "type", 1);
    cfg.writeEntry(name, "path", path);
    cfg.writeEntry(name, "icon", icon);
    cfg.writeEntry(name, "aniicon", aniIcon);
    bool ok = cfg.flush();

    /* 通知（原库第 758 行：flush 成功才 folderChanged） */
    if (ok)
        folderChanged(static_cast<AM_Folder*>(folder));
    return ok;
}

/* ============ deleteFolder ============ */
/* 反编译 0x41afcac8：getAppObject(name) 找 folder，移除并写配置。 */
void AM_RegistryManager::deleteFolder(const QString& name)
{
    AM_AppObject* obj = getAppObject(name);
    if (!obj || obj->getType() != AM_AppObject::Folder)
        return;
    m_pAppMap->remove(name);
    ZConfig cfg(getConfigFileName(static_cast<RegFileType>(3)), false);
    cfg.removeEntry(name, "");
    cfg.flush();
    folderChanged(static_cast<AM_Folder*>(obj));
    delete obj;
}

/* ============ changeOrder ============ */
/* 反编译 0x41afd464（1:1 重写，v25）：
     if (folderPath.empty() || order.count()==0) return 0;
     folder = getAppObject(folderPath);          // 必须是 AM_Folder
     if (!folder) return 0;
     folder->clear();                            // AM_Folder::clear()
     for (s in order) {                          // 按新顺序重建内存列表
         list.append(s);
         folder->add(getAppObject(s), -1);       // add 内部已做 null/去重保护
     }
     ZConfig cfg(getConfigFileName(2));          // ← 2 = UserMenuTree（旧版错写成 3）
     cfg.writeEntry(folderPath, "Items", list, ';');  // ← 键 = "Items"（旧版错写成 "order"）
     if (cfg.flush()) folderChanged(folder);     // ← 旧版漏发，UI 不刷新
     返回 flush 成败（原库返回 char，am 侧会用该返回值判断成功与否）。
   字面池实测：DAT_41afd840 = "Items"，getConfigFileName 参数 = 2。 */
bool AM_RegistryManager::changeOrder(const QString& folderPath,
                                     const QList<QString>& order)
{
    ring_put(36, (unsigned)&folderPath, (int)order.count());
    alog("[changeOrder] in folder='%s' n=%d", (const char*)folderPath.utf8(),
         (int)order.count());
    if (folderPath.isEmpty() || order.isEmpty()) {
        alog("[changeOrder] !! 参数为空 -> 0");
        return false;
    }
    AM_AppObject* obj = getAppObject(folderPath);
    alog("[changeOrder] getAppObject('%s') = %p type=%d",
         (const char*)folderPath.utf8(), (void*)obj,
         obj ? (int)obj->getType() : -1);
    /* 容错回退（v28）：am 传来的 folderPath 取自它自己的成员（this+0x114），
       不一定是本库 map 的键。"照原库"在这里直接返回 0 会让调整顺序永远无效，
       因此按语义补三级回退，并把每次回退打进日志：
         1) uuid 直接命中         —— 原库路径
         2) 按 Folder 显示名匹配   —— am 传的是 Name 的情况
         3) 传空串时退回根 Folder  —— am 传空的情况 */
    if (!obj || obj->getType() != AM_AppObject::Folder) {
        if (m_pAppMap) {
            for (QMap<QString, AM_AppObject*>::ConstIterator it = m_pAppMap->begin();
                 it != m_pAppMap->end(); ++it) {
                AM_AppObject* cand = it.data();
                if (cand && cand->getType() == AM_AppObject::Folder &&
                    cand->getName() == folderPath) {
                    obj = cand;
                    alog("[changeOrder] 回退①按名命中 folder='%s' uuid=%s",
                         (const char*)folderPath.utf8(), (const char*)it.key().utf8());
                    break;
                }
            }
        }
        if (!obj && folderPath.isEmpty()) {
            obj = getRootFolder();
            alog("[changeOrder] 回退②空串 -> 根 Folder=%p", (void*)obj);
        }
        if (!obj || obj->getType() != AM_AppObject::Folder) {
            alog("[changeOrder] !! 目标不是 Folder 对象 -> 0");
            return false;
        }
    }
    AM_Folder* folder = static_cast<AM_Folder*>(obj);
    folder->clear();
    QStringList list;
    QListIterator<QString> it(order);
    for (QString* s = it.toFirst(); s; s = ++it) {
        list.append(*s);
        folder->add(getAppObject(*s), -1);
    }
    /* 组名恒用对象的 UUID（与原库一致：原库写入的键就是 folder 的 uuid） */
    const QString groupUid = obj->UUID().isEmpty() ? folderPath : obj->UUID();
    ZConfig cfg(getConfigFileName(UserMenuTree), false);
    cfg.writeEntry(groupUid, "Items", list, QChar(';'));
    alog("[changeOrder] write group=%s key=Items n=%d",
         (const char*)groupUid.utf8(), (int)list.count());
    if (!cfg.flush()) {
        alog("[changeOrder] flush FAIL folder=%s n=%d",
             (const char*)folderPath.utf8(), (int)list.count());
        return false;
    }
    alog("[changeOrder] OK folder='%s' group=%s n=%d", (const char*)folderPath.utf8(),
         (const char*)groupUid.utf8(), (int)list.count());
    folderChanged(folder);
    return true;
}

/* ============ modifyApp ============ */
/* 1:1 复原原库 0x41afd844（size=2168）。v65 重写。
 *
 * 以前的实现只有 128B：trim 都不做、只改内存、不写配置、不 flush、
 * 不发 AppNameChanged —— 等于"改了名字重开就没"。审计把它列为
 * 「若将来设置界面调用会改了不生效」的第一项。
 *
 * 逐句对照（地址均取自 dis4.py 追踪结果 + 字面池解码）：
 *   0x41afd870/880/88c  三个参数 stripWhiteSpace（newName / arg3 / arg4；name 不 strip）
 *   0x41afd890..d8d8    name、newName、arg3、arg4 任一为空 → return
 *   0x41afd988          obj = getAppObject(name)；为空 → return
 *   0x41afd998          dev = obj->FTemp00C(0x0c)；dev ∈ {6,1,2,0} → return
 *   0x41afd9d0          getConfigFileName(5)  + ZConfig(path, false)
 *   0x41afda30          guid = obj->FUUID（0x1c）——后续所有 writeEntry 的**组名**
 *   0x41afda48          renamed = (newName != obj->getName())
 *   改名分支 0x41afdda0..0x41afe010：
 *     0x41afdda4        getDefaultName()
 *     0x41afddb0        isDefault = (newName == defaultName)
 *     0x41afde08        strb #0 → 0x50（isDefault 时）
 *     0x41afde20/41afe04  writeEntry(guid, "modification", isDefault ? "NO" : "YES")
 *     0x41afde64        ok = writeEntry(guid, "Name", newName)
 *     0x41afdecc        setName(newName)；0x41afdee0 strb #1 → 0x41
 *     0x41afdefc..dfd4  Type==2(Application) 才发 QCop：
 *                       payload 里流的**只有 AppID.toString()**（一个 QString）
 *                       QCopChannel::send("AM/RegistryiMgr/Channel",
 *                                         "AppNameChanged(QString)", ds)
 *     失败(ok==0) 直接析构 ZConfig 返回，**不 flush**
 * 图标分支 0x41afdaac..0x41afdc6c（改名成功也会落到这里）：
 *     0x41afdab4        bigIconChanged = (arg3 != getBigIcon())
 *     0x41afdad4        若 big 没变则 aniChanged = (arg4 != FAniIcon(0x18))
 *     0x41afdde8        writeEntry(guid, "BigIcon", arg3)
 *     0x41afde28        ok = writeEntry(guid, "AniIcon", arg4)
 *     0x41afdf00        ok 才 setBigIcon/setAniIcon，并 strb #1 → 0x42
 *   0x41afdc6c          if (flush()) folderChanged(parentFolder)
 *
 * 字面池（[sl+off]）实证：0xd58=BigIcon 0xd5c=AniIcon 0xd54=Name
 *   0xd84=modification 0xd88=NO 0xd90=YES
 *   0xce4=AM/RegistryiMgr/Channel 0xd8c=AppNameChanged(QString) */
void AM_RegistryManager::modifyApp(const QString& name, const QString& newName,
                                   const QString& newIcon, const QString& newAniIcon)
{
    ring_put(73, (unsigned)&name, 0);   /* 73 未被占用（原用 25 与 loadRegistry 撞号） */

    QString nn = newName.stripWhiteSpace();
    QString ic = newIcon.stripWhiteSpace();
    QString an = newAniIcon.stripWhiteSpace();
    AMLOG("[modifyApp] name=%s new=%s big=%s ani=%s",
          (const char*)name.utf8(), (const char*)nn.utf8(),
          (const char*)ic.utf8(), (const char*)an.utf8());

    if (name.isEmpty() || nn.isEmpty() || ic.isEmpty() || an.isEmpty())
        return;

    AM_AppObject* obj = getAppObject(name);
    if (!obj) {
        AMLOG("[modifyApp] object '%s' not found", (const char*)name.latin1());
        return;
    }
    /* 原库 0x41afd998：设备槽 6/1/2/0 一律不处理（这些是系统内建项） */
    int dev = obj->getDeviceSlot();
    if (dev == 6 || dev == 1 || dev == 2 || dev == 0) {
        AMLOG("[modifyApp] skip dev=%d", dev);
        return;
    }

    QString cfgPath = getConfigFileName(static_cast<RegFileType>(5));  /* InstalledDB */
    ZConfig cfg(cfgPath, false);
    QString guid = obj->UUID();          /* 0x1c，作组名 */

    /* ---- 改名分支 ---- */
    if (nn != obj->getName()) {
        bool isDefault = (nn == obj->getDefaultName());
        obj->setModificationFlag(!isDefault);
        /* 值一律走 QString 重载：ZConfig 还有 writeEntry(g,k,bool) 重载，
           直接传 "NO" 这种 const char* 会被它优先匹配成 bool（写成 1/0）。 */
        cfg.writeEntry(guid, QString::fromLatin1("modification"),
                       QString::fromLatin1(isDefault ? "NO" : "YES"));

        if (!cfg.writeEntry(guid, QString::fromLatin1("Name"), nn)) {
            AMLOG("[modifyApp] write Name failed");
            return;                      /* 原库此处不 flush */
        }
        obj->setName(nn);
        obj->setChangedName(true);       /* 0x41 */

        if (obj->getType() == AM_AppObject::Application) {
            /* payload 只有一个 QString：AppID.toString()（原库 0x41afdf5c/df68） */
            QByteArray data;
            QDataStream ds(data, IO_WriteOnly);
            QString appIdStr = obj->appId().toString();
            ds << appIdStr;
            QCopChannel::send(QCString("AM/RegistryiMgr/Channel"),
                              QCString("AppNameChanged(QString)"), data);
        }
    }

    /* ---- 图标分支（改名成功也走） ---- */
    /* 原库 0x41afdab4 调的是 getBigIcon()（QString 路径解析版），
       本项目该函数仍声明为 QPixmap（见 AM_AppObject.h 的未修项说明），
       故这里直接比注册表里的图标路径字段。 */
    bool iconChanged = (ic != obj->bigIconPath()) || (an != obj->aniIconPath());
    if (iconChanged) {
        cfg.writeEntry(guid, QString::fromLatin1("BigIcon"), ic);
        if (cfg.writeEntry(guid, QString::fromLatin1("AniIcon"), an)) {
            obj->setBigIcon(ic);
            obj->setAniIcon(an);
            obj->setChangedIcon(true);   /* 0x42 */
        }
    }

    if (cfg.flush())
        emit folderChanged(static_cast<const AM_Folder*>(obj->parentFolder()));
}

/* ============ changeFolder ============ */
/* 1:1 复原 0x41afe0bc（size=3324，v47 重写）。与原库逐句对照：
     入口 r1=name r2=oldFolder r3=newFolder [sp]=index
     0x41afe0c4  三参数 isEmpty → return 0
     0x41afe124  oldFolder == newFolder → return 0
     0x41afe138  name == newFolder      → return 0
     0x41afe14c  getAppObject(name) / (oldFolder) / (newFolder) 任一为空 → return 0
     0x41afe1b8  ZConfig(getConfigFileName(2)=UserMenuTree, false)
     0x41afe24c  fromList = readListEntry(oldFolder, "Items", ';')
     0x41afe3a4  toList   = readListEntry(newFolder, "Items", ';')
     0x41afe280..0x41afe5c8  fromList 里找到 name 就删掉（只删第一个）
     0x41afe5cc  index < 0 || index >= toList.count() → 走「字符串追加」分支
     0x41afe9f8  ：items = readEntry(newFolder,"Items",QString::null)
                   items = items.isEmpty() ? name : items + ";" + name
                   writeEntry(newFolder, "Items", items)              ← 字符串版
     否则        ：toList.insert(toList.at(index), name)
                   writeEntry(newFolder, "Items", toList, ';')        ← 列表版
     0x41afe754  writeEntry(oldFolder, "Items", fromList, ';')
     0x41afe78c  ok = flush()
     0x41afe930  ok 才动内存：from->remove(app) / to->add(app,index)
                 app->setParentFolder(to)（obj+0x04）
                 folderChanged(from) / folderChanged(to)
     0x41afe928  return ok
   ⚠️ 原库这里【不】发 notifyAddAppToFolder/notifyRemoveAppFromFolder
      （那是 addAppToMainMenu / removeAppFromMainMenu 的事），
      发的是 folderChanged 信号。旧实现在这里多发了两条 QCop 通知，已删。 */
bool AM_RegistryManager::changeFolder(const QString& name, const QString& oldFolder,
                                      const QString& newFolder, int index)
{
    ring_put(26, (unsigned)&name, 0);
    AMLOG("[changeFolder] in name=%s old=%s new=%s idx=%d",
          (const char*)name.utf8(), (const char*)oldFolder.utf8(),
          (const char*)newFolder.utf8(), index);

    if (name.isEmpty() || oldFolder.isEmpty() || newFolder.isEmpty())
        return false;
    if (oldFolder == newFolder)
        return false;
    if (name == newFolder)
        return false;

    AM_AppObject* app  = getAppObject(name);
    AM_AppObject* from = getAppObject(oldFolder);
    AM_AppObject* to   = getAppObject(newFolder);
    if (!app || !from || !to) {
        AMLOG("[changeFolder] object missing app=%p from=%p to=%p", (void*)app, (void*)from, (void*)to);
        return false;
    }

    ZConfig cfg(getConfigFileName(UserMenuTree), false);
    QStringList fromList = cfg.readListEntry(oldFolder, QString("Items"), QChar(';'));
    QStringList toList   = cfg.readListEntry(newFolder, QString("Items"), QChar(';'));

    /* fromList：删掉 name（只删第一个，与原库 0x41afe280.. 的 find→remove 一致） */
    QStringList::Iterator fi = fromList.find(name);
    if (fi != fromList.end())
        fromList.remove(fi);

    /* toList：插到 index；越界（含 index<0）走「字符串追加」分支 */
    if (index < 0 || index >= (int)toList.count()) {
        QString items = cfg.readEntry(newFolder, QString("Items"), QString::null);
        if (items.isEmpty())
            items = name;
        else
            items += QString(";") + name;
        cfg.writeEntry(newFolder, QString("Items"), items);
    } else {
        toList.insert(toList.at((uint)index), name);
        cfg.writeEntry(newFolder, QString("Items"), toList, QChar(';'));
    }
    cfg.writeEntry(oldFolder, QString("Items"), fromList, QChar(';'));

    bool ok = cfg.flush();
    AMLOG("[changeFolder] %s: %s -> %s idx=%d flush=%d",
          (const char*)name.utf8(), (const char*)oldFolder.utf8(),
          (const char*)newFolder.utf8(), index, (int)ok);

    if (ok) {
        static_cast<AM_Folder*>(from)->remove(app);
        static_cast<AM_Folder*>(to)->add(app, index);
        app->setParentFolder(to);              /* 原库 0x41afe95c: str r3,[r2,#4] */
        folderChanged(static_cast<const AM_Folder*>(from));
        folderChanged(static_cast<const AM_Folder*>(to));
    }
    return ok;
}

/* ============ doAddAppToFolder ============ */
/* 1:1 复原 0x41b08348（**只有 132 字节**，v47 重写）。原库全文就这些指令：
     0x41b0834c  app 空 或 folder 空 → return
     0x41b0837c  appObj = getAppObject(app)；空 → return
     0x41b08394  folderObj = getAppObject(folder)；空 → return
     0x41b083ac  ((AM_Folder*)folderObj)->add(appObj, index)
     0x41b083bc  appObj->FTemp040 = 1        （"可见/已安装"标志，strb）
     0x41b083c0  appObj->FTemp004 = folderObj（父指针，str）
     0x41b083c8  folderChanged(folderObj)     （尾调用 b 0x41ae8688）
   ⚠️ 原库此函数里 **一次 QCopChannel::send 都没有**（全函数 132 字节，只 bl 了
      getAppObject×2 + AM_Folder::add + folderChanged）。
      旧实现末尾调了 notifyAddAppToFolder → 它在 "AM/RegistryiMgr/Channel" 上发
      "AddAppToFolder(QString, QString, int)" → am 自己订阅着这个通道 →
      slotChannelMessage 又派发回本函数 → 再发一次 …… **无限自环**。
      表现就是 QWS 消息队列越积越长、InstallApp 被反复重放、每次启动重跑
      slotAppInstalled、最终 _int_malloc 堆损坏。这是安装崩溃的头号嫌疑，已删。
   注：0x41b083cc 起的 getSystemFolderName / getSystemFolderUid 是另外两个函数，
       dis2.py 会把它们连在一起输出，别误算进本函数。 */
void AM_RegistryManager::doAddAppToFolder(const QString& app, const QString& folder, int index)
{
    ring_put(37, (unsigned)&app, index);
    AMLOG("[addAppToFolder] in app=%s folder=%s idx=%d",
          (const char*)app.utf8(), (const char*)folder.utf8(), index);
    if (app.isEmpty() || folder.isEmpty())
        return;
    AM_AppObject* appObj = getAppObject(app);
    if (!appObj)
        return;
    AM_AppObject* folderObj = getAppObject(folder);
    if (!folderObj || folderObj->getType() != AM_AppObject::Folder)
        return;
    static_cast<AM_Folder*>(folderObj)->add(appObj, index);
    appObj->setVisibleFlag(true);                    /* obj+0x40 = 1 */
    appObj->setParentFolder(folderObj);              /* obj+0x04 = folder */
    folderChanged(static_cast<const AM_Folder*>(folderObj));
}

/* ============ addAppToMainMenu ============ */
/* 1:1 复原 0x41b0081c（size=3308，v47 重写）。字面池实测（GOT 偏移 → 内容）：
     0xd7c "Items"                                  0xd78 ";"
     0xd74 "cb839c8f-72c3-42ac-bf17-b01080cb5705"  ← "全部应用"组（All）
     0xd80 ""（空串，readEntry 默认值）             0x1138 QString::null
     0xda0 "67dc1191-2928-4938-abce-1a73cb5ce490"  ← 兜底组（游戏/未归类）
     0xda4 "Folder"                                 0xdda8 "Pos"
   控制流（每个 return 点/F 与原库一致）：
     1) app、folder 各自 stripWhiteSpace；app 空 → 返回 0
     2) obj = getAppObject(app)；空 → 0
        obj->parentFolder() != 0 → 0    （已经挂在某个组里，不再动主菜单）
     3) folder 空 → 现场 new ZConfig(UserConfig=5)，按 app 反查它上次的位置：
          folder = cfg->readEntry(app, "Folder", QString::null)
          index  = cfg->readEntry(app, "Pos",    QString::null).toInt(0, 10)
        并置 flagUser（收尾时 clearGroup(app)+flush，把这条临时记录抹掉）
     4) folderObj = getAppObject(folder)；为空 → index = -1、folder = "67dc1191-…"
     5) cfg = ZConfig(UserMenuTree=2)；exists = QFile::exists(该文件名)
        items = cfg.readEntry(folder, "Items", QString::null)
        if (exists && items.find(app, 0, true) == -1) {     已存在 → 直接返回 0
            index == -1 : items = items.isEmpty() ? app : items + ";" + app
                          cfg.writeEntry(folder, "Items", items)          ← 字符串版
            否则         : list = split(';', items, false); insert/append
                          cfg.writeEntry(folder, "Items", list, ';')      ← 列表版
            all = cfg.readEntry("cb839c8f-…", "Items", QString(""))
            if (all.find(app, 0, true) == -1) { all += ";" + app; writeEntry(All,"Items",all) }
            r = cfg.flush()
            if (r) { if (flagUser) { cfgUser->clearGroup(app); cfgUser->flush(); }
                     notifyAddAppToFolder(app, folder, index) }   ← 原库虚表 slot15
            delete cfgUser; return r
        }
        delete cfgUser; return 0
   注：原库在 QValueList 上做的是 `it = list.at(index); it == list.end() ? append : insert`，
       即 index >= count 时落到 append。此处用 count 比较表达同一语义（对越界 index 更安全）。 */
bool AM_RegistryManager::addAppToMainMenu(const QString& app_, const QString& folder_, int index)
{
    QString app    = app_.stripWhiteSpace();
    QString folder = folder_.stripWhiteSpace();

    /* v46：安装器（SoftManage）直接调本函数 —— 必写一条，装崩时靠它定位停在哪 */
    AMLOG("[addAppToMainMenu] in app=%s folder=%s idx=%d",
          (const char*)app.utf8(), (const char*)folder.utf8(), index);

    if (app.isEmpty())
        return false;

    AM_AppObject* obj = getAppObject(app);
    if (!obj)
        return false;
    /* 已经挂在某个组里（obj+0x04 非空）→ 原库 0x41b008f8 直接返回 0 */
    if (obj->parentFolder() != 0) {
        AMLOG("[addAppToMainMenu] app already in folder, skip");
        return false;
    }

    /* 3) folder 为空：从 UserConfig 反查上次的位置 */
    ZConfig* cfgUser = 0;
    bool     flagUser = false;          /* 原库 [sp+0x30] */
    if (folder.isEmpty()) {
        cfgUser  = new ZConfig(getConfigFileName(UserConfig), false);
        flagUser = true;
        folder = cfgUser->readEntry(app, QString("Folder"), QString::null);
        index  = cfgUser->readEntry(app, QString("Pos"), QString::null).toInt(0, 10);
        AMLOG("[addAppToMainMenu] folder empty -> UserConfig says folder=%s idx=%d",
              (const char*)folder.utf8(), index);
    }

    /* 4) 目标组不存在 → 兜底到 "67dc1191-…" 且追加到末尾 */
    if (!getAppObject(folder)) {
        AMLOG("[addAppToMainMenu] folder object missing -> fallback 67dc1191 idx=-1");
        folder = QString("67dc1191-2928-4938-abce-1a73cb5ce490");
        index  = -1;
    }

    /* 5) 主菜单配置 */
    QString cfgName = getConfigFileName(UserMenuTree);
    bool    exists  = QFile::exists(cfgName);
    ZConfig cfg(cfgName, false);

    QString items = cfg.readEntry(folder, QString("Items"), QString::null);

    if (!exists) {
        AMLOG("[addAppToMainMenu] %s missing -> return 0", (const char*)cfgName.latin1());
        delete cfgUser;
        return false;
    }
    if (items.find(app, 0, true) != -1) {
        AMLOG("[addAppToMainMenu] already listed in folder=%s -> return 0",
              (const char*)folder.utf8());
        delete cfgUser;
        return false;
    }

    if (index == -1) {
        /* 追加到末尾：纯字符串拼接 + 字符串版 writeEntry */
        if (items.isEmpty())
            items = app;
        else
            items += QString(";") + app;
        cfg.writeEntry(folder, QString("Items"), items);
    } else {
        /* 插到 index 位置：split → insert/append → 列表版 writeEntry（分隔符 ';'） */
        QStringList list = QStringList::split(QString(";"), items, false);
        if (index >= (int)list.count())
            list.append(app);
        else
            list.insert(list.at((uint)index), app);
        cfg.writeEntry(folder, QString("Items"), list, QChar(';'));
    }

    /* 6) "全部应用"组同步维护（默认值在本组是空串，不是 QString::null） */
    QString all = cfg.readEntry(QString("cb839c8f-72c3-42ac-bf17-b01080cb5705"),
                                QString("Items"), QString(""));
    if (all.find(app, 0, true) == -1) {
        if (all.isEmpty())
            all = app;
        else
            all += QString(";") + app;
        cfg.writeEntry(QString("cb839c8f-72c3-42ac-bf17-b01080cb5705"),
                       QString("Items"), all);
    }

    bool r = cfg.flush();
    AMLOG("[addAppToMainMenu] flush=%d app=%s folder=%s idx=%d",
          (int)r, (const char*)app.utf8(), (const char*)folder.utf8(), index);

    if (r) {
        if (flagUser && cfgUser) {
            cfgUser->clearGroup(app);
            cfgUser->flush();
        }
        notifyAddAppToFolder(app, folder, index);
    }
    delete cfgUser;
    return r;
}

/* ============ addAppToRegistry ============ */
/* 反编译 0x41b01508（2474 行）：安装应用到注册表。
   流程：创建 AppLnk → 设置属性(serialId/type/device) → 加入 m_pAppMap
   → 写 ZConfig → QCop 广播 appInstalled → out 返回安装信息。
   依赖 QDataStream/DMT 等外部类，此处语义等价实现核心流程。 */
void AM_RegistryManager::addAppToRegistry(const AM_AppRecord& record, QString& out,
                                          const QString& folder,
                                          AM_Global::Device device, int index)
{
    /* 创建 Application 对象 */
    AM_AppObject* obj = m_pAppFactory->createAppObject(AM_AppObject::Application);
    if (!obj) {
        out = QString::null;
        return;
    }
    /* 设置设备与序列号 */
    AM_AppLnk* lnk = static_cast<AM_AppLnk*>(obj);
    lnk->setDeviceId(static_cast<int>(device));
    /* 以 serialId 为注册键 */
    QString key = record.serialId.isEmpty() ? obj->UUID() : record.serialId;
    (*m_pAppMap)[key] = obj;
    out = key;

    /* 写配置（等价：写 AppLockConfig / 对应注册表） */
    ZConfig cfg(getConfigFileName(static_cast<RegFileType>(6)), false);
    cfg.writeEntry(key, "type", record.type);
    cfg.writeEntry(key, "serialid", record.serialId);
    cfg.flush();

    /* 反编译 2245 行：addAppToMainMenu 返回真才 sync() 落盘 */
    if (addAppToMainMenu(key, folder, index))
        sync();

    /* 广播安装事件（v32：appInstalled 已是信号，用 emit） */
    emit appInstalled(device, key, obj->getName(), index);
}

/* ============ removeAppFromMainMenu ============ */
/* 1:1 复原 0x41b05674（size=3648，v47 重写）。实测字面池：
     0xd7c "Items"    0xd00 "2a58c0d6-05c6-45aa-8938-63059d41a4e9"（根组）
     0xda4 "Folder"   0xda8 "Pos"      0x1138 QString::null
   控制流（与原库逐句对应，注意顺序与 return 点）：
     0x41b05678  app 空 → 0
     0x41b056b8  obj = getAppObject(app) 空 → 0
     0x41b056c8  flag != 0 → 先做「设备注册表 clearGroup」前置块（0x41b0630c）：
                   dev = obj->[0x44]；dev==2 → getConfigFileName(4=CardRegistry)
                                      dev==1 → getConfigFileName(7=InstalledDB)
                   名字仍为 QString::null → return 0
                   ZConfig(名)；clearGroup(app) 失败 → return 0
                   flush() 失败 → return 0
                 成功后【继续】主流程（不是提前返回）
     0x41b056dc  cfg = ZConfig(getConfigFileName(2=UserMenuTree), false)
     0x41b05730  parent = obj->FTemp004（0x04）；为空 → return 0
     0x41b05740  parentUid = parent->UUID()（parent+0x1c）
     0x41b057b0  items = readListEntry(parentUid, "Items", ';') → 删 app
     0x41b05938  writeEntry(parentUid, "Items", items, ';')
     0x41b059a0  根组 rootUid = "2a58c0d6-…"
     0x41b059f4  ritems = readListEntry(rootUid, "Items", ';') → 删 app
     0x41b05b7c  writeEntry(rootUid, "Items", ritems, ';')
     0x41b05be8  flush() 失败 → return 0
     0x41b05d88  flag == 0 → 走 UserConfig 段（0x41b05f88）：
                   ZConfig(getConfigFileName(5=UserConfig), false)
                   writeEntry(app, "Folder", parentUid)
                   writeEntry(app, "Pos", parent->getAppPosition(obj))   ← int 版
                   flush()
                 然后回到公共尾
     0x41b05e7c  notifyRemoveAppFromFolder(app, parentUid)  ← 虚表 slot16（偏移 0x40）
     0x41b05f80  return 1
   ⚠️ 原库在 parent 为空时【直接 return 0】。本实现保留 v28 加的「扫描全部 Folder
      反查容器」容错（否则 parent==0 的应用永远卸不干净），这是唯一有意偏差。
   ⚠️ 原库此函数【不】动内存对象（AM_Folder::remove / m_pAppMap 删除都由
      slotAppUnInstalled 负责），旧实现曾在此 delete 对象，已删。 */
int AM_RegistryManager::removeAppFromMainMenu(const QString& app, bool flag)
{
    ring_put(38, (unsigned)&app, flag);
    AMLOG("[removeAppFromMainMenu] in app=%s flag=%d", (const char*)app.utf8(), (int)flag);
    if (app.isEmpty())
        return 0;
    AM_AppObject* obj = getAppObject(app);
    if (!obj)
        return 0;

    /* ---- 前置块：flag != 0 时清掉该设备注册表里本应用的组 ---- */
    if (flag) {
        int dev = obj->getDeviceSource();                 /* FTemp044 @0x44 */
        QString devCfg;                                   /* QString::null */
        if (dev == 2)
            devCfg = getConfigFileName(CardRegistry);      /* 4 */
        else if (dev == 1)
            devCfg = getConfigFileName(InstalledDB);       /* 7 */
        if (devCfg == QString::null) {
            AMLOG("[removeAppFromMainMenu] dev=%d -> no config name, return 0", dev);
            return 0;
        }
        ZConfig zc(devCfg, false);
        if (!zc.clearGroup(app)) {
            AMLOG("[removeAppFromMainMenu] dev=%d clearGroup FAIL", dev);
            return 0;
        }
        if (!zc.flush()) {
            AMLOG("[removeAppFromMainMenu] dev=%d clearGroup flush FAIL", dev);
            return 0;
        }
        AMLOG("[removeAppFromMainMenu] dev=%d clearGroup %s in %s ok",
              dev, (const char*)app.utf8(), (const char*)devCfg.utf8());
    }

    /* ---- 主流程：UserMenuTree ---- */
    ZConfig cfg(getConfigFileName(UserMenuTree), false);
    AM_Folder* parent = static_cast<AM_Folder*>(obj->parentFolder());  /* FTemp004 @0x04 */
    if (!parent) {
        /* 容错回退（v28，原库此处直接 return 0）：loadMenuTree 与原库一样，
           只在 folder != rootFolder 时回填父指针，所以"直接挂根组下"的对象
           parent==NULL。原库此时什么都不做；这里扫一遍找出真正的容器。 */
        if (m_pAppMap) {
            for (QMap<QString, AM_AppObject*>::ConstIterator it = m_pAppMap->begin();
                 it != m_pAppMap->end(); ++it) {
                AM_AppObject* cand = it.data();
                if (!cand || cand->getType() != AM_AppObject::Folder)
                    continue;
                AM_Folder* f = static_cast<AM_Folder*>(cand);
                if (f->getAppPosition(obj) >= 0) {
                    parent = f;
                    AMLOG("[removeAppFromMainMenu] 回退: 由容器反查父 Folder=%s",
                          (const char*)it.key().utf8());
                    break;
                }
            }
        }
    }
    if (!parent) {
        AMLOG("[removeAppFromMainMenu] no parent for %s -> 0", (const char*)app.utf8());
        return 0;
    }
    const QString parentUid = parent->UUID();             /* parent+0x1c */

    /* 父组：删 app */
    QStringList items = cfg.readListEntry(parentUid, QString("Items"), QChar(';'));
    QStringList::Iterator fi = items.find(app);
    if (fi != items.end())
        items.remove(fi);
    cfg.writeEntry(parentUid, QString("Items"), items, QChar(';'));

    /* 根组（恒收录全部应用）：删 app */
    const QString rootUid = QString("2a58c0d6-05c6-45aa-8938-63059d41a4e9");
    QStringList ritems = cfg.readListEntry(rootUid, QString("Items"), QChar(';'));
    QStringList::Iterator ri = ritems.find(app);
    if (ri != ritems.end())
        ritems.remove(ri);
    cfg.writeEntry(rootUid, QString("Items"), ritems, QChar(';'));

    if (!cfg.flush()) {
        AMLOG("[removeAppFromMainMenu] menuTree flush FAIL app=%s", (const char*)app.utf8());
        return 0;
    }

    /* flag == 0：记住原属 folder 与位置（flag != 0 时原库跳过） */
    if (!flag) {
        int pos = parent->getAppPosition(obj);
        ZConfig uc(getConfigFileName(UserConfig), false);  /* 5 */
        uc.writeEntry(app, QString("Folder"), parentUid);
        uc.writeEntry(app, QString("Pos"), pos);           /* int 版 */
        uc.flush();
    }

    /* 通知（两种 flag 都发；原库是虚表 slot16，这里直接调用等价） */
    notifyRemoveAppFromFolder(app, parentUid);
    AMLOG("[removeAppFromMainMenu] OK app=%s parent=%s flag=%d",
          (const char*)app.utf8(), (const char*)parentUid.utf8(), (int)flag);
    return 1;
}

/* ============ notifyAddAppToFolder ============ */
/* 反编译 0x41b0b2fc：QCopChannel::send(channel, message, QDataStream)。
   字面池解码确认：
     channel = "AM/RegistryiMgr/Channel"
     message = "AddAppToFolder(QString, QString, int)"
   参数序：app, folder, index（与 slotChannelMessage 解包序一致）。 */
void AM_RegistryManager::notifyAddAppToFolder(const QString& app, const QString& folder, int index)
{
    ring_put(39, (unsigned)&app, index);
    AMLOG("[notifyAddAppToFolder] app=%s folder=%s idx=%d chan=%p",
          (const char*)app.utf8(), (const char*)folder.utf8(), index, (void*)m_pChannel);
    /* 原库 0x41b0b3c0 直接 QCopChannel::send(...) —— 不看 m_pChannel。
       静态 send 不依赖本进程有没有建 channel 对象，旧实现加了 if (m_pChannel)
       守卫，恰好在"通道已被别人注册（构造时 m_pChannel=0）"的进程里把通知吞掉。 */
    {
        QCString msg("AddAppToFolder(QString, QString, int)");
        QByteArray data;
        QDataStream ds(data, IO_WriteOnly);
        ds << app << folder << index;
        QCopChannel::send(QCString("AM/RegistryiMgr/Channel"), msg, data);
    }
}

/* ============ notifyRemoveAppFromFolder ============ */
/* 反编译 0x41b0b408：QCopChannel::send(channel, message, QDataStream)。
   字面池解码确认：
     channel = "AM/RegistryiMgr/Channel"
     message = "UninstallApp(QString)"   ← 复用该路由标签，参数序 app+folder */
void AM_RegistryManager::notifyRemoveAppFromFolder(const QString& app, const QString& folder)
{
    ring_put(39, (unsigned)&app, 99);
    AMLOG("[notifyRemoveAppFromFolder] app=%s folder=%s chan=%p",
          (const char*)app.utf8(), (const char*)folder.utf8(), (void*)m_pChannel);
    /* 原库 0x41b0b4bc 同样无条件 send（消息名复用 UninstallApp(QString)，
       参数仍是 app + folder 两个 QString） */
    {
        QCString msg("UninstallApp(QString)");
        QByteArray data;
        QDataStream ds(data, IO_WriteOnly);
        ds << app << folder;
        QCopChannel::send(QCString("AM/RegistryiMgr/Channel"), msg, data);
    }
}

/* ============ addAppToAutoRun (QString) ============ */
/* 1:1 复原 0x41b06588（size=828）。实测：
     uuid 空 → return 0
     ZConfig(getConfigFileName(5)=UserConfig, false)
     apps = readEntry(group="AutoRun", key="Apps", QString::null)   ← 键名 'Apps' 大写 A
     apps 空           → apps = uuid
     apps 已含 uuid    → return 0（连 flush 都不做）
     否则              → apps += QString(";") + uuid
     writeEntry("AutoRun","Apps",apps) 失败 → return 0
     return flush()
   ⚠️ 原库此函数【不碰】m_pAutoRunList（0x28），全程未解引用 this；
      旧实现自行 append 内存列表 + 用 readListEntry("apps") 键名小写，两处都错。 */
bool AM_RegistryManager::addAppToAutoRun(const QString& uuid)
{
    AMLOG("[addAppToAutoRun] in uuid=%s", (const char*)uuid.utf8());
    if (uuid.isEmpty())
        return false;

    ZConfig cfg(getConfigFileName(UserConfig), false);
    QString apps = cfg.readEntry(QString("AutoRun"), QString("Apps"), QString::null);

    if (!apps.isEmpty()) {
        if (apps.find(uuid, 0, true) != -1) {
            AMLOG("[addAppToAutoRun] already present -> 0");
            return false;
        }
        apps += QString(";") + uuid;
    } else {
        apps = uuid;
    }

    if (!cfg.writeEntry(QString("AutoRun"), QString("Apps"), apps))
        return false;
    return cfg.flush();
}

/* ============ addAppToAutoRun (QUuid) ============ */
/* 反编译 0x41b068c4：getAppObjById(uuid) → 取对象 FUUID@0x1C → addAppToAutoRun(QString) */
bool AM_RegistryManager::addAppToAutoRun(const QUuid& uuid)
{
    if (uuid.isNull())
        return false;
    AM_AppObject* obj = getAppObjById(uuid);
    if (obj)
        return addAppToAutoRun(obj->UUID());
    return false;
}

/* ============ removeAppFromAutoRun (QUuid) ============ */
/* 反编译 0x41b06970：getAppObjById(uuid) → 取对象 FUUID@0x1C → removeAppFromAutoRun(QString) */
bool AM_RegistryManager::removeAppFromAutoRun(const QUuid& uuid)
{
    if (uuid.isNull())
        return false;
    AM_AppObject* obj = getAppObjById(uuid);
    if (obj)
        return removeAppFromAutoRun(obj->UUID());
    return false;
}

/* ============ removeAppFromAutoRun (QString) ============ */
/* 1:1 复原 0x41b06a1c（size=1912）。实测与原库逐句对应：
     uuid 空 → return 0
     ZConfig(getConfigFileName(5)=UserConfig, false)
     s = readEntry("AutoRun", "Apps", QString::null)
     s 空            → return 1（0x41b06bbc: mov r0,#1 —— 原库对"本来就是空"返真）
     s 里找不到 uuid → return 1（0x41b0714c → 0x41b06bb0 同一出口）
     否则：list = split(QChar(';'), s, false)
           list 里删掉 uuid（一个）
           writeEntry("AutoRun","Apps",list,';')
           return flush()
   ⚠️ 同样不碰 m_pAutoRunList；键名 'Apps' 大写。 */
bool AM_RegistryManager::removeAppFromAutoRun(const QString& uuid)
{
    if (uuid.isEmpty())
        return false;

    ZConfig cfg(getConfigFileName(UserConfig), false);
    QString s = cfg.readEntry(QString("AutoRun"), QString("Apps"), QString::null);

    if (s.isEmpty()) {
        AMLOG("[removeAppFromAutoRun] AutoRun/Apps empty -> 1");
        return true;
    }
    if (s.find(uuid, 0, true) == -1) {
        AMLOG("[removeAppFromAutoRun] uuid not in list -> 1");
        return true;
    }

    QStringList list = QStringList::split(QChar(';'), s, false);
    QStringList::Iterator li = list.find(uuid);
    if (li != list.end())
        list.remove(li);
    cfg.writeEntry(QString("AutoRun"), QString("Apps"), list, QChar(';'));
    return cfg.flush();
}

/* ============ appLockValueChanged ============ */
/* v32：本函数在原库是【信号】（元对象第 2 张表 0x1004 项），由 moc 生成
   activate_signal 发射体；旧版自造的 appLockValueChangedSignal 已删除。
   触发点：slotChannelMessage 收到 "AppLockValueChanged()"。 */

/* 反编译 0x41aff8d8（293 行，多为 Qt COW/迭代器噪音）业务核心：
   从磁盘刷新内存锁值：
   1) getConfigFileName(6) → AppLockConfig 路径，构造 ZConfig cfg(path,false)
   2) 取 group 名（漂移字面池 DAT_41afffc0，无法静态解出——用 setAppLockValue 写入的 "" 对称）
   3) 遍历 m_pAppMap 中每个 AM_AppObject*：
      - 跳过 type==1 Folder，仅处理 type 2(Application)/4(Shortcut)
      - 读对象 UUID 四段（偏移 0x8..0xC）→ QUuid::toString()
      - cfg.readEntry("", uuidStr) → toInt() 得磁盘锁值
      - 若磁盘值 ≠ 对象当前值，调 obj->setAppLockValue(磁盘值)（虚表 +0x1c） */
void AM_RegistryManager::slotReloadAppLockValue()
{
    QString cfgPath = getConfigFileName(static_cast<RegFileType>(6));  /* AppLockConfig */
    ZConfig cfg(cfgPath, false);

    if (!m_pAppMap)
        return;

    for (QMap<QString, AM_AppObject*>::Iterator it = m_pAppMap->begin();
         it != m_pAppMap->end(); ++it)
    {
        AM_AppObject* obj = it.data();
        if (!obj)
            continue;

        AM_AppObject::Type t = obj->getType();
        if (t == AM_AppObject::Folder)          /* 跳过 Folder(type=1) */
            continue;
        if (t != AM_AppObject::Application &&   /* 仅 Application(2)/Shortcut(4) */
            t != AM_AppObject::Shortcut)
            continue;

        const QString uuidStr = obj->UUID();
        /* v63：组名 "AppLockValue"（原库 0x41aff94c GOT[0xcf8]），此前误写空组。 */
        int diskVal = cfg.readNumEntry("AppLockValue", uuidStr, obj->getAppLockValue());
        if (diskVal != obj->getAppLockValue())
            obj->setAppLockValue(diskVal);      /* 虚表 +0x1c = setAppLockValue */
    }

    /* 注意（v63 取证）：原库本函数**不**广播 AppLockValueChanged()。
       calldiff slotReloadAppLockValue 的 24 个调用里没有 QCopChannel::send；
       广播是 setAppLockValue 的职责。这里若补发会造成
       "收到通知 → 重载 → 再通知" 的跨进程回环，故不补。 */
}

/* ============ appInstalled / appUnInstalled ============ */
/* v32（决定性修正）：这两个在原库里是【信号】（元对象第 2 张表 0x1008/0x100c）。
   反编译里看到的 "PTR_xxx(this)" 跳转就是 moc 生成的 activate_signal 内联体，
   旧版误判成"空函数体/虚槽派发"并写成 no-op，
   结果 am 的 connect(SIGNAL(appUnInstalled(...)), SLOT(slotAppUnInstalled(...)))
   既找不到信号（头文件没声明），本库自己也不发通知 —— 卸载后菜单项不消失。
   现在由 moc 生成发射体，调用点改为 emit（见 slotChannelMessage）。 */

/* 反编译 0x41b07194：新应用安装后加入注册表与内存。
   ============ v44 修正（安装链路三处硬错误，全部逐指令反汇编定案） ============
   [错误1] 选错注册表文件。
     原库 0x41b071ec-0x41b0724c 的实际指令流：
       0x41b071ec  cmp  r8, #1            ; r8 = device
       0x41b07208  moveq r1, #7           ; device==1(Flash2) → getConfigFileName(7)
       0x41b0720c  beq  0x41b0724c        ;   → 落到同一个调用点
       0x41b07210  cmp  r8, #2
       0x41b07214  beq  0x41b0723c
       0x41b07248  mov  r1, #4            ; device==2(Card1)  → getConfigFileName(4)
       0x41b07218..0x41b07238             ; 其余 device → 释放临时 QString 后返回
     即：Flash2 → 7 = **InstalledDB**，Card1 → 4 = **CardRegistry**。
     旧实现写成 UserConfig(5) / UserMenuTree(2)，两个文件里根本不会有
     "组名 == path" 的应用记录 → 函数必定在"找不到组"处 return，
     安装完的应用既不入 m_pAppMap 也不进文件夹（界面无图标）。
     getConfigFileName 的 12 个 case 已全量核对到 GOT 字面量，枚举映射无误：
       0xd10 SysMenuTree  0xd18 SysRegistry  0xd1c UserMenuTree 0xd20 UserRegistry
       0xd24 CardRegistry 0xd28 UserConfig   0xd2c AppLockConfig 0xd30 InstalledDB
       0xd34 SysMime      0xd38 UserMime     0xd3c PlugCardDB    0xd40 PlugCardRegistry
   [错误2] props 取法错。
     原库 0x41b07380 直接调 ZConfig::getGroupEntriesKeyList(group)（calldiff 实测），
     迭代该组的**全部键值**构造 QMap。旧实现写 readListEntry(group,"keys")，
     但注册表里没有 "keys" 这个元键（本文件 createObject 处早已记过同样的坑），
     读到空表 → props 空 → "Type" 缺失 → toInt()=0 → createAppObject(InvalidType)
     返回 NULL → 直接 return。即便文件选对了，安装照样是空操作。
     这里改用本文件既有的 amParseAll（一次读盘解析整表，编码自动判别），
     与 createObject 走同一条已验证的解析路径。
   [错误3] "全部应用"文件夹取错。
     原库 0x41b07ac0 `ldr r3,[pc,#0x350]` → GOT 0xd74 → 字面量
     "cb839c8f-72c3-42ac-bf17-b01080cb5705"，直接 getAppObject(该字面量)。
     旧实现用 getSystemFolderUid(FolderAll)，取回的是根 2a58c0d6-…（不是"全部"），
     于是新装应用被塞进主菜单根组而不是"全部应用"列表
     （同一处错误在 slotAppUnInstalled 里也修过）。
   [补齐] 原库 0x41b07ab4 `str r4,[r2,#4]` 显式写 obj+0x04 = 父 Folder，
     旧实现漏了（loadMenuTree 里同样要写这个指针）。
   其余顺序一致：path 空→return；UUID(0x1c)=组名；FTemp044(0x44)=device；
   入 m_pAppMap(键=组名) → 父 Folder::add(app,index) → 0x40=1 → folderChanged
   → "全部"Folder::add(app,-1) → folderChanged。 */
void AM_RegistryManager::slotAppInstalled(AM_Global::Device device, const QString& path,
                                          const QString& name, int index)
{
    ring_put(30, (unsigned)device, (unsigned)index);
    if (path.isEmpty())
        return;

    /* 1) 按 Device 选 cfg：Flash2(1)→InstalledDB(7)，Card1(2)→CardRegistry(4) */
    RegFileType cfgType;
    if (device == AM_Global::Flash2)           /* 1 */
        cfgType = InstalledDB;                 /* 7 */
    else if (device == AM_Global::Card1)        /* 2 */
        cfgType = CardRegistry;                 /* 4 */
    else
        return;                                /* 原库非 1/2 直接返回 */

    QString cfgPath = getConfigFileName(cfgType);

    /* 2) 整表解析后按组名精确取（原库 getGroupsKeyList + operator== path） */
    QMap<QString, QMap<QString, QString> > all;
    amParseAll(cfgPath, all);
    if (!all.contains(path)) {
        AMLOG("[slotAppInstalled] 组 %s 不在 %s（注册表未写入？）",
              (const char*)path.utf8(), (const char*)cfgPath.utf8());
        return;
    }
    QMap<QString, QString> props = all[path];

    /* 3) props 的 "Type" toInt → 对象类型 */
    int typeVal = props.contains("Type") ? props["Type"].toInt() : 0;
    AM_AppObject* app = m_pAppFactory
        ? m_pAppFactory->createAppObject(static_cast<AM_AppObject::Type>(typeVal))
        : 0;
    if (!app)
        return;

    /* 4) 调 createObject(props) 填充成员（反编译第269行经虚表 +0xc；
       头文件声明为 void，故不判返回值——原库失败分支仅销毁对象，此处保持填充后继续） */
    app->createObject(props);

    /* 5) UUID = 组名；设备来源 = device（第364-365行） */
    app->setUUID(path);
    app->setDeviceSource(static_cast<int>(device));

    /* 6) 插入 m_pAppMap（第408-413行） */
    if (m_pAppMap)
        (*m_pAppMap)[path] = app;

    /* 7) 加入父 Folder（param_4=name），设可见标志与父指针，发 folderChanged */
    int parentAdded = 0, allAdded = 0;
    AM_AppObject* parentObj = getAppObject(name);
    if (parentObj && parentObj->getType() == AM_AppObject::Folder) {
        AM_Folder* parent = static_cast<AM_Folder*>(parentObj);
        parent->add(app, index);
        app->setVisibleFlag(true);          /* 0x41b07ab0 strb r3,[r2,#0x40] */
        app->setParentFolder(parent);       /* 0x41b07ab4 str  r4,[r2,#0x04] */
        folderChanged(parent);
        parentAdded = 1;

        /* 8) 再加入"全部应用"Folder（字面量，非 getSystemFolderUid） */
        AM_AppObject* allObj = getAppObject(
            "cb839c8f-72c3-42ac-bf17-b01080cb5705");
        if (allObj && allObj->getType() == AM_AppObject::Folder) {
            AM_Folder* allFolder = static_cast<AM_Folder*>(allObj);
            allFolder->add(app, -1);
            folderChanged(allFolder);
            allAdded = 1;
        }
    }
    amAppIconCacheClear();      /* v41：新装应用的图标要重新取 */
    AMLOG("[slotAppInstalled] dev=%d key=%s folder=%s idx=%d type=%d "
          "inMap=%d inFolder=%d inAll=%d",
          (int)device, (const char*)path.utf8(), (const char*)name.utf8(),
          index, typeVal,
          (m_pAppMap && m_pAppMap->contains(path)) ? 1 : 0,
          parentAdded, allAdded);
}

void AM_RegistryManager::slotAppUnInstalled(const QString& path, const QString& name)
{
    /* 反编译 0x41b07e2c（1:1 重写，v25）。字面池实测：
         DAT_41b08338 = "cb839c8f-72c3-42ac-bf17-b01080cb5705"（"全部应用"容器 Folder）
       原库流程：
         1) (path 非空 || name 非空) 且 getAppObject(path)、getAppObject(name) 均非空
         2) AM_Folder::remove(name 组, app)
         3) folder2 = getAppObject("cb839c8f-...")；非空则同样 remove
         4) m_pAppMap.find(path) → 命中则 delete 该 AM_AppObject（虚析构 slot1）并 erase
         5) folderChanged(name 组)；folder2 非空则再 folderChanged(folder2)
       旧实现两处错：把第3步写成 getSystemFolderUid(FolderAll)（取到的是根 2a58c0d6，
       不是 cb839c8f，导致"全部"列表残留）；第4步只 erase 不 delete（对象泄漏 +
       am 侧仍持有悬空指针语义不一致）。 */
    alog("[slotAppUnInstalled] in path=%s name=%s",
         (const char*)path.utf8(), (const char*)name.utf8());
    if (path.isEmpty() && name.isEmpty())
        return;

    AM_AppObject* app = getAppObject(path);
    AM_AppObject* folderObj = getAppObject(name);
    if (!app || !folderObj)
        return;

    AM_Folder* folder = 0;
    if (folderObj->getType() == AM_AppObject::Folder)
        folder = static_cast<AM_Folder*>(folderObj);

    /* 2) 从所在 Folder 移除 */
    if (folder)
        folder->remove(app);

    /* 3) 从"全部应用"容器 Folder 移除（字面池常量，非 getSystemFolderUid） */
    AM_AppObject* allObj = getAppObject("cb839c8f-72c3-42ac-bf17-b01080cb5705");
    AM_Folder* allFolder = (allObj && allObj->getType() == AM_AppObject::Folder)
                           ? static_cast<AM_Folder*>(allObj) : 0;
    if (allFolder)
        allFolder->remove(app);

    /* 4) 从 m_pAppMap 删除节点并销毁对象（原库：delete(节点数据) + removeAndRebalance） */
    if (m_pAppMap) {
        /* 用对象反查真实键再删（am 传进来的 path 可能是 AppID 而不是组名） */
        QString realKey = path;
        for (QMap<QString, AM_AppObject*>::ConstIterator it = m_pAppMap->begin();
             it != m_pAppMap->end(); ++it) {
            if (it.data() == app) {
                realKey = it.key();
                break;
            }
        }
        alog("[slotAppUnInstalled] erase key=%s (入参 path=%s)",
             (const char*)realKey.utf8(), (const char*)path.utf8());
        m_pAppMap->remove(realKey);
        delete app;
    }

    /* 5) 通知 UI 刷新 */
    if (folder)
        folderChanged(folder);
    if (allFolder)
        folderChanged(allFolder);
    amAppIconCacheClear();      /* v41：图标可能已被删除/替换，缓存作废 */
    AMLOG("[slotAppUnInstalled] path=%s folder=%s done", (const char*)path.utf8(),
         (const char*)name.utf8());
}

/* ============ slotAddAppToFolder ============ */
/* 反编译 0x41b08348：QCop "AddAppToFolder" 的落地动作体。 */
void AM_RegistryManager::slotAddAppToFolder(const QString& app, const QString& folder, int index)
{
    doAddAppToFolder(app, folder, index);
}

/* ============ checkAndUpdateCardApp ============ */
/* 反编译 0x41b0b940：
   if Type != Application(2) return;
   if Device != 3 && Device != 5: Device = 4; */
void AM_RegistryManager::checkAndUpdateCardApp(AM_AppObject& app)
{
    if (app.getType() != AM_AppObject::Application)
        return;
    AM_AppLnk* lnk = static_cast<AM_AppLnk*>(&app);
    int dev = lnk->getDeviceId();
    if (dev != 3 && dev != 5)
        lnk->setDeviceId(4);
}

/* ============ handleCardPlugIn / handleCardPlugOut ============ */
/* 反编译 0x41b08a70：SD 卡插入，加载 CardRegistry 中的应用，
   对每个卡应用：Type(0x8)==2 时设 FTemp044(0x44)=2（设备来源=卡），
   并按 cfg 锁值更新 setAppLockValue。 */
void AM_RegistryManager::handleCardPlugIn(RegFileType type)
{
    ring_put(27, type, 0);
    if (!m_pAppMap)
        return;
    /* v20（决定性修正）：原库 0x41b08a70 只处理【卡注册表自身的组】
       （getConfigFileName(4)=CardRegistry、(6)=AppLockConfig、(5)=UserConfig、
       (11)=PlugCardRegistry，对比组列表后 createObject），【绝不】遍历
       appMap 改全部对象的 deviceSource。
       旧实现把 66 个内置应用全部污染成卡应用（slot=4 dev=2）→
       getExecFullPath 返回 QTDownLoad 路径（不存在）→ am 校验失败 →
       "无效应用程序文件"。卡应用对象的 slot/dev 已在 createObject 里
       按注册表来源正确设置，这里只需重建卡注册表对象。 */
    ZConfig cardCfg(getConfigFileName(CardRegistry), false);
    ZConfig lockCfg(getConfigFileName(AppLockConfig), false);
    /* v43：onlyNew=true —— 只补卡注册表里"当前 m_pAppMap 还没有"的组。
       卡还在、应用都已装载时这一步变成空操作（与原库一致）；
       热插卡（对象已被 handleCardPlugOut 摘掉）时才会真正创建。 */
    createObject(CardRegistry, cardCfg, lockCfg, /*onlyNew=*/true);

    /* v74 / H6：原库 0x41b08a70 后半（LAB_41b08f54）从 PlugCardRegistry(11)
       读回 Folder/Pos，把卡应用重新挂进菜单并发 folderChanged。
       本库 createObject 只重建对象、不挂 Folder，而 loadMenuTree 只在启动时跑
       一次（热插卡不会重跑）；若不补这段，卡应用虽回到 m_pAppMap，却不在任何
       Folder::m_appList（界面不显示），且 H2 写出去的 Folder/Pos 永远无人读取。 */
    if (type != 4)
        return;   /* 原库 0x41b08a70 仅 case 4 做事 */

    ZConfig plugCfg(getConfigFileName(PlugCardRegistry), false);
    ZConfig cardGroupsCfg(getConfigFileName(CardRegistry), false);
    QStringList cardGroups;
    cardGroupsCfg.getGroupsKeyList(cardGroups);
    const QString allUid("cb839c8f-72c3-42ac-bf17-b01080cb5705");
    AM_Folder* allFolder = static_cast<AM_Folder*>(getAppObject(allUid));

    for (QStringList::ConstIterator gi = cardGroups.begin(); gi != cardGroups.end(); ++gi) {
        const QString appKey = (*gi).stripWhiteSpace();
        if (appKey.isEmpty())
            continue;
        AM_AppObject* obj = getAppObject(appKey);
        if (!obj || obj->getType() != AM_AppObject::Application)
            continue;
        /* 原库条件 obj+0x40==0 || *(obj+4)==0 才恢复。已挂菜单的一律跳过：
           启动路径（loadMenuTree 末尾调本函数）因此恒为空操作，不动既有菜单。 */
        if (obj->parentFolder() != 0)
            continue;

        QString folder = plugCfg.readEntry(appKey, QString("Folder"), QString::null);
        QString posStr = plugCfg.readEntry(appKey, QString("Pos"), QString::null);
        int pos = posStr.isEmpty() ? -1 : posStr.toInt(0, 10);
        if (folder.isEmpty()) {
            /* 回退：removeAppFromMainMenu(flag=false) 也把 Folder/Pos 写进了
               UserConfig(5)，addAppToMainMenu 的 "folder 为空" 分支同样读它。 */
            ZConfig uc(getConfigFileName(UserConfig), false);
            folder = uc.readEntry(appKey, QString("Folder"), QString::null);
            QString p2 = uc.readEntry(appKey, QString("Pos"), QString::null);
            if (!p2.isEmpty())
                pos = p2.toInt(0, 10);
        }

        /* 1:1 对齐原库：先写 UserMenuTree + 通知（内部有去重，不会自环） */
        addAppToMainMenu(appKey, folder, pos);

        AM_Folder* target = static_cast<AM_Folder*>(getAppObject(folder));
        if (!target)
            target = allFolder;
        if (!target)
            continue;

        target->add(obj, pos);              /* findRef 去重，pos<=0 时 append */
        obj->setVisibleFlag(true);
        obj->setParentFolder(target);
        if (allFolder && allFolder != target)
            allFolder->add(obj, -1);
        folderChanged(target);
        if (allFolder && allFolder != target)
            folderChanged(allFolder);
        AMLOG("[handleCardPlugIn] restored app=%s folder=%s pos=%d",
              (const char*)appKey.utf8(), (const char*)folder.utf8(), pos);
    }
}

/* 反编译 0x41b0a52c：SD 卡拔出，卸载 m_pAppMap 中卡应用（FTemp044==2）
   的应用对象并删除。原库判断：Type(0x8)==2 且 FTemp044(0x44)==2。 */
void AM_RegistryManager::handleCardPlugOut()
{
    ring_put(27, 99, 0);
    AMLOG("[handleCardPlugOut] begin");
    if (!m_pAppMap)
        return;

    /* v72 / H2：按原库 0x41b0a52c(1420B) 1:1 复原拔出链。
       字面池实证（GOT 基址 0x41b229f0）：
         getConfigFileName(11) = PlugCardRegistry
         GOT[0x41b23794] = "Folder"   GOT[0x41b23798] = "Pos"
       原库每个卡应用（Type==2 且 FTemp044==2）依次做：
         parent = *(obj+0x04)
         pos    = AM_Folder::getAppPosition(parent, obj)
         removeAppFromMainMenu(obj->getName(), false)   ← 摘 UserMenuTree + UserConfig + 通知
         cfg.writeEntry(appName, "Folder", parent->getName())
         cfg.writeEntry(appName, "Pos", pos)
         循环结束若命中过则 cfg.flush()
       旧实现只做 m_pAppMap->remove + delete，既不给父 Folder::m_appList 摘指针
       （拔卡后桌面仍画已 delete 的对象 → UAF/崩溃），也不写回 PlugCardRegistry
       （重插卡后分组/位置丢失）。这里补齐原库调用序列；保留 remove+delete
       是为了让本库 createObject(onlyNew=true) 在重插时能真正重建卡对象。 */
    ZConfig cfg(getConfigFileName(PlugCardRegistry), false);
    bool changed = false;

    QValueList<QString> toRemove;
    for (QMap<QString, AM_AppObject*>::Iterator it = m_pAppMap->begin();
         it != m_pAppMap->end(); ++it) {
        AM_AppObject* obj = it.data();
        if (!obj)
            continue;
        /* 原库：*(obj+8)==2(Type=Application) 且 *(obj+0x44)==2(设备来源=卡) */
        if (obj->getType() != AM_AppObject::Application)
            continue;
        if (obj->getDeviceSource() != 2)   /* FTemp044：1=内置，2=卡 */
            continue;

        AM_Folder* parent = static_cast<AM_Folder*>(obj->parentFolder());  /* obj+0x04 */
        if (!parent)
            continue;

        /* 顺序关键：pos 必须在 removeAppFromMainMenu 之前取（原库同）。 */
        const int pos = parent->getAppPosition(obj);
        /* v74 / H7：原库 0x41b0a52c 取 pAVar10+0x1c（对象 FUUID）作组名、
           this_00+0x1c（父 Folder FUUID）作 Folder 值；m_pAppMap / UserMenuTree
           一律按 UUID/组名索引。getName() 对 Application 是本地化名字（还会读
           SETUP 配置），旧实现用它会导致 removeAppFromMainMenu 查不到对象、
           写出去的组名在插卡时也读不回来，整条拔卡→插卡恢复链断裂。 */
        const QString appName = it.key();
        const QString folderName = parent->UUID();

        removeAppFromMainMenu(appName, false);

        cfg.writeEntry(appName, QString("Folder"), folderName);
        cfg.writeEntry(appName, QString("Pos"), pos);
        changed = true;
        toRemove.append(it.key());
    }

    if (changed)
        cfg.flush();

    /* 内存对象清理：原库由 removeAppFromMainMenu 的下游链路完成；本库该
       函数刻意不动内存对象，所以这里显式 remove + delete，避免悬空指针。 */
    /* v76 / H8：delete 前必须从【所有】Folder::m_appList 摘链。
       本库 removeAppFromMainMenu 只写 UserMenuTree/UserConfig 配置、刻意不动内存
       （v28 设计），而其尾部 notifyRemoveAppFromFolder 是异步 QCop（消息名复用
       "UninstallApp(QString)"）—— am 侧收到后立即刷新界面、遍历 Folder::m_appList。
       v75 真机拔卡实测：直接 delete 导致 Folder 残留悬空指针 → UAF → am 重启；
       且 removeAppFromMainMenu 只删 parent 组与 2a58c0d6 组的 Items，不删
       cb839c8f（All）组 → 重启 loadMenuTree 后卡应用图标仍在（用户实测吻合）。
       原库由 removeAppFromMainMenu 下游链路完成内存摘链，这里显式补齐。 */
    for (QValueList<QString>::ConstIterator ri = toRemove.begin(); ri != toRemove.end(); ++ri) {
        AM_AppObject* obj = m_pAppMap->find(*ri).data();
        if (obj) {
            for (QMap<QString, AM_AppObject*>::ConstIterator fit = m_pAppMap->begin();
                 fit != m_pAppMap->end(); ++fit) {
                AM_AppObject* fo = fit.data();
                if (fo && fo != obj && fo->getType() == AM_AppObject::Folder)
                    static_cast<AM_Folder*>(fo)->remove(obj);
            }
            obj->setParentFolder(0);
            m_pAppMap->remove(*ri);
        }
        delete obj;
    }
    AMLOG("[handleCardPlugOut] done removed=%d changed=%d",
          (int)toRemove.count(), (int)changed);
}

/* ============ folderChanged ============ */
/* v32：恢复成【信号】（元对象第 2 张表 0x1000 项，顺序=信号索引 0）。
   反编译 0x41b16cdc 的 AM_RegistryManager::folderChanged 就是一段内联
   activate_signal（遍历 receivers 逐个派发），证实它是信号而非普通函数。
   am 侧存在 connect(SIGNAL(folderChanged(const AM_Folder*)), ...)，
   旧版发的是自造的 folderChangedSignal，am 永远收不到 → 排序后界面不刷新。 */

/* ============ slotChannelMessage ============ */
/* 反编译 0x41aff470：解析 QCop 消息（4 分支）。真实消息名已从字面池解码确认
   （EZX 约定：消息名 = 槽函数签名）：
     strcmp#1 → "AppLockValueChanged()"                  → emit appLockValueChanged()
     strcmp#2 → "InstallApp(int, QString, QString, int)"  → emit appInstalled(dev,path,name,idx)
     strcmp#3 → "UninstallApp(QString)"                   → emit appUnInstalled(path,name)
     strcmp#4 → "AddAppToFolder(QString, QString, int)"    → emit addAppToFolder(app,folder,idx)
   ⚠️ v47 修正：原库这四个分支【只发本地信号】，一个 QCopChannel::send 都没有。
      QCop 通道 "AM/RegistryiMgr/Channel" 是本进程自己订阅着的，如果在收到消息后
      再 send 一条同名消息，就会自己喂自己 → 死循环。
      旧实现在 AddAppToFolder 分支里多调了 doAddAppToFolder（而它当时末尾会
      notifyAddAppToFolder → 再 send 一条 "AddAppToFolder(...)"）→ **无限自环**，
      每个循环都 new 一份 QByteArray+QString，46MB 内存瞬间打满，表现就是
      QWS 队列越积越长、InstallApp 被反复重放、_int_malloc 堆损坏、SoftManage 崩。
      现在 doAddAppToFolder 已不再 send，本分支保留一次直接调用 + emit 信号
      （原库只 emit；这里多发一次是幂等的——AM_Folder::add 有 findRef 去重，
      setParentFolder/setVisibleFlag 重复写同值无副作用），
      目的是不依赖 am 侧是否 connect 了 addAppToFolder 信号。
   注意：QDataStream << 先发 app 再发 folder（与 appInstalled 的 path/name 一致顺序）。 */
void AM_RegistryManager::slotChannelMessage(const QCString& channel,
                                            const QByteArray& data)
{
    AMLOG("[qcop] msg=%s len=%d", channel.data() ? channel.data() : "(null)",
          (int)data.size());
    if (channel == "AppLockValueChanged()") {
        emit appLockValueChanged();
        return;
    }
    if (channel == "InstallApp(int, QString, QString, int)") {
        /* 反编译 71-75：ds >> device(int) >> path >> name >> index(int)
           → appInstalled(device, path, name, index) */
        QDataStream ds(data, IO_ReadOnly);
        int device = 0, index = -1;
        QString path, name;
        ds >> device >> path >> name >> index;
        emit appInstalled(static_cast<AM_Global::Device>(device), path, name, index);
        return;
    }
    if (channel == "UninstallApp(QString)") {
        /* 反编译 120-122：ds >> path >> name → appUnInstalled(path, name)
           v32：这里必须真的把应用从文件夹/映射表里摘掉，否则"卸载后图标还在"。
           原库的 appUnInstalled 只是发射体，动作体在 slotAppUnInstalled，
           两者由 QObject::connect 串起来（本类构造函数里做自连），
           这里再直接调用一次以保证不依赖连接顺序。 */
        QDataStream ds(data, IO_ReadOnly);
        QString path, name;
        ds >> path >> name;
        slotAppUnInstalled(path, name);
        emit appUnInstalled(path, name);
        return;
    }
    if (channel == "AddAppToFolder(QString, QString, int)") {
        /* 反编译 165-169：ds >> app >> folder >> index
           → if (!folder.isEmpty()) addAppToFolder(app, folder, index)
           注：app 先入流、folder 后入流，addAppToFolder(app, folder, index) */
        QDataStream ds(data, IO_ReadOnly);
        QString app, folder;
        int index = -1;
        ds >> app >> folder >> index;
        if (!folder.isEmpty()) {
            doAddAppToFolder(app, folder, index);
            emit addAppToFolder(app, folder, index);
        }
        return;
    }
    /* ============ v61 新增：主桌面刷新入库 ============
       原库这条通道只有 4 条消息（AppLockValueChanged / InstallApp /
       UninstallApp / AddAppToFolder，已用 GOT 槽逐个解出核实），没有通用刷新入口。
       走安装器装/卸时自然有 InstallApp/UninstallApp 触发刷新；但别的程序
       直接改了 _InstalledDB / _UserMenuTree（比如批量导入、恢复备份）时，
       am 的内存表不会自己重读，桌面就停在旧状态。

       用法（任意进程）：
           QCopChannel::send("AM/RegistryiMgr/Channel", "RefreshDesktop()");
       无参数，不需要回执。

       实现要点：
         1) 必须先 clearAppMap() 再 initRegistry()。loadMenuTree 会往 m_pAppMap
            里插根 Folder 和一批 AM_AppObject，不清就重入会重复建对象并泄漏。
         2) 刷新完发 folderChanged(根文件夹)。am 连的就是这个信号，
            且传的是真实对象（不是 0），不会让对端解引用空指针。 */
    if (channel == "RefreshDesktop()") {
        AMLOG("[qcop] RefreshDesktop: reload registry");
        clearAppMap();
        initRegistry();
        AM_Folder* root = getRootFolder();
        if (root) {
            emit folderChanged(root);
            AMLOG("[qcop] RefreshDesktop: done, onRoot ok");
        } else {
            AMLOG("[qcop] RefreshDesktop: done, but root folder is null!");
        }
        return;
    }
}

/* ============ slotHotPlugForceUpdate ============ */
/* 反编译 0x41b08928：解析 int 类型（4=Card, 1=Flash1, 8=...），
   对应调用 handleCardPlugIn。 */
void AM_RegistryManager::slotHotPlugForceUpdate(const QCString& channel,
                                                const QByteArray& data)
{
    /* 反编译 0x41b08928：
       1. 先 strcmp 比对 channel 是否为热插拔通知频道（GOT 运行期解析，非本库职责）
       2. QDataStream 反序列化一个 int type
       3. type==4/1/8 → handleCardPlugIn(4)（注意：原库传常量 4，非 type）
       4. type==6/2/3 → handleCardPlugOut()
       channel 名指针由 GOT 提供运行期解析，此处不臆造比较值，仅还原处理分支。 */
    /* v74 / H5：原库 0x41b08928 在解析前先 strcmp(channel, "MmcsdShmUpdate(int)")，
       只有相等才继续。字面池实证：GOT[0x41b2383c]="MmcsdShmUpdate(int)"、
       GOT[0x41b23840]="%s,%s,%d\n"、GOT[0x41b23844]="utility/AM_RegistryManager.cpp"。
       qApp 的 systemMessage 会被【所有】系统 QCop 消息触发；若不过滤，任意消息
       反序列化出的首 int 恰为 6/2/3 就会误触发 handleCardPlugOut()，把全部卡应用
       卸掉并回写配置 —— 属灾难性误动作。原库对 __s1/__s2 有 null 判断，这里同。 */
    const char* ch = channel.data();
    if (!ch || strcmp(ch, "MmcsdShmUpdate(int)") != 0)
        return;
    QDataStream ds(data, IO_ReadOnly);
    int type = -1;
    ds >> type;
    /* v75：热插拔链靠运行时 QCop 触发，静态审计看不到。留一条必写日志，
       插卡/拔卡实测时可直接从 amreg.log 确认整条链是否走通。 */
    AMLOG("[HotPlug] msg=%s type=%d", ch, type);
    if (type == 4 || type == 1 || type == 8)
        handleCardPlugIn(static_cast<RegFileType>(4));   /* 原库传常量 4 */
    else if (type == 6 || type == 2 || type == 3)
        handleCardPlugOut();
}

#include "AM_RegistryManager.moc"
