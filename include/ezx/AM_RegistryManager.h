/*
libezxam.so.1.0.0  逆向重建（AM_RegistryManager — 应用注册表核心，48 方法）
继承 QObject（Q_OBJECT 信号槽）；虚表 _ZTV18AM_RegistryManager @ 0x41b22b10
构造（0x41af5ab8）成员布局（operator_new 证据）：
  0x24  m_pAppMap       (QMap<QString,AM_AppObject*>*)
  0x28  m_pAutoRunList  (QGList 派生：QValueList<QString> autorun UUID)
  0x2C  m_pAppFactory   (AM_AppFactory*)
  0x30  m_pChannel      (QCopChannel*，注册 "AM/RegistryiMgr/Channel")
枚举 RegFileType：注册表配置文件类型（SysRegistry/UserRegistry/CardRegistry…）
枚举 SystemFolder：系统文件夹（0..7，含 MainMenu/All 等）
信号：appLockValueChanged() / folderChanged(const AM_Floor*) / appInstalled / appUnInstalled 等
注：完整 48 方法逻辑（addAppToRegistry 2474 行、loadRegistry、changeFolder 等）依赖
    ZConfig/DmtData/QCopChannel 等外部类，此处保留方法签名与委托骨架。
*/
#ifndef AM_REGISTRYMANAGER_H
#define AM_REGISTRYMANAGER_H

#include <qobject.h>
#include <qstring.h>
#include <qlist.h>
#include <qmap.h>
#include <qvaluelist.h>
#include <quuid.h>
#include <qpixmap.h>   /* OpenWithApp::icon (0x14-0x17) */
#include <qcopchannel_qws.h>
#include <ZConfig.h>
#include <AM_Global.h>
#include <AM_AppObject.h>
#include <AM_AppLnk.h>
#include <AM_Folder.h>

class AM_AppFactory;

class AM_RegistryManager : public QObject
{
    Q_OBJECT

public :
    /* 注册表配置文件类型
       v16（决定性修正）：映射从原库 getConfigFileName(0x41af7210) 的 switch
       逐 case 反汇编核对：case0=SysMenuTree case1=SysRegistry case2=UserMenuTree
       case3=UserRegistry case4=CardRegistry case5=UserConfig case6=AppLockConfig
       case7=InstalledDB case8=SysMime case9=UserMime case10=PlugCardDB
       case11=PlugCardRegistry。旧映射（SysRegistry=0...）是错的，导致
       loadRegistry 循环类型错、loadMenuTree 打开错文件。 */
    enum RegFileType {
        SysMenuTree      = 0,
        SysRegistry      = 1,
        UserMenuTree     = 2,
        UserRegistry     = 3,
        CardRegistry     = 4,
        UserConfig       = 5,
        AppLockConfig    = 6,
        InstalledDB      = 7,
        SysMime          = 8,
        UserMime         = 9,
        PlugCardDB       = 10,
        PlugCardRegistry = 11
    };
    /* 系统文件夹
       v48：数值→UUID 映射已用反汇编 0x41b087b4 的 jump table 逐项实证
       （表头 0x41b087cc 起 8 项，默认 0x41b08800 = 与 case0 同）。
       此前本枚举把 FolderAll 当 1、FolderUser 当 2，整体错位：
       任何按真实 SDK 枚举传参的调用方都会拿到别的文件夹 UUID。 */
    enum SystemFolder {
        FolderMainMenu   = 0,   /* 2a58c0d6-05c6-45aa-8938-63059d41a4e9 根（UserMenuTree 总组） */
        FolderOffice     = 1,   /* e8c83dac-db4e-4fb5-98a6-61de1998b277 Office */
        FolderMultimedia = 2,   /* b18dbbd2-503a-4442-9308-5575ee38c210 Multimedia */
        FolderGames      = 3,   /* 67dc1191-2928-4938-abce-1a73cb5ce490 Games/Application */
        FolderReserved4  = 4,   /* c34463b7-800f-483b-8e01-5ea39730d7d5 无显示名 */
        FolderReserved5  = 5,   /* fe948c2a-b7a6-4dac-a1d1-915b3ba57168 无显示名 */
        FolderExtra      = 6,   /* 51f2cb88-3040-4f00-b66c-c4bb974e4d18 Extra */
        FolderAll        = 7    /* cb839c8f-72c3-42ac-bf17-b01080cb5705 All */
    };
    /* 打开方式应用记录 */
    /* OpenWithApp：反编译 getOpenWithApps operator_new(0x2C)=44 字节。
       布局：QUuid(16) + QString name(4) + QPixmap icon(20) + bool(1)+pad = 44。
       反编译 174-184 行确认：
         piVar5[0..3]=QUuid(应用UUID,仅用于返回)
         piVar5+4    = QString(getName())
         piVar5+5    = QPixmap(getSmallIcon())
         piVar5+10   = bool(1)
       注：piVar5+5 是 word偏移=字节 0x14，QPixmap=20字节(QPaintDevice12+data4+Temp4)。 */
    struct OpenWithApp {
        QUuid   uuid;      /* 0x00-0x0F：应用 UUID（返回给调用方） */
        QString name;      /* 0x10-0x13：应用名称（getName()） */
        QPixmap icon;      /* 0x14-0x27：应用图标（getSmallIcon()，20字节） */
        bool    matched;   /* 0x28：匹配标志（反编译写1） */
        OpenWithApp() : uuid(), matched(false) {}
    };
    /* 安装应用记录 */
    struct AM_AppRecord {
        int     type;
        QString serialId;
    };

    AM_RegistryManager(QObject* parent = 0, const char* name = 0);
    ~AM_RegistryManager();

    /* ---- 查询 ---- */
    void getAllAppList(QList<AM_AppObject>& list) const;
    AM_AppObject* getAppObjById(const QUuid& uuid) const;
    AM_AppObject* getJavaObj(const int& javaId) const;
    AM_AppLnk*    getAppLnkById(const QUuid& uuid) const;
    AM_AppLnk*    getAppLnkBySerialId(const QString& serialId) const;
    AM_Folder*    getRootFolder() const;
    AM_AppObject* getAppObject(const QString& name) const;
    void getLockEnabledAppList(QList<AM_AppObject>& list) const;
    int  getAppLockValue(const QUuid& uuid) const;
    void getAutoRunApps(QList<AM_AppLnk>& list) const;
    void getOpenWithApps(const QString& path, QList<OpenWithApp>& list) const;
    void getInstalledAppList(QList<AM_AppLnk>& list, bool b) const;
    void getAppNameByFullPath(const QString& path, QString& name) const;
    bool getAppAutoStartStatusFromDmt(const QString& path) const;   /* 原库返回 bool（loadRegistry 用其返回值） */
    static void getSystemFolderName(const QString& path, QString& name);   /* ★v60 static，见下 */
    void getSystemFolderUid(SystemFolder folder, QString& uid);
    void getAppParentFolderUid(const QString& path, QString& uid);

    /* ---- 注册表维护 ---- */
    void initRegistry();
    void clearAppMap();
    void loadMenuTree();
    void loadRegistry();
    /* ★ABI：原库只导出 3 参数版本
       `createObject(RegFileType, ZConfig&, ZConfig&)` @0x41af8330 / 9044B。
       我们内部还需要一个 onlyNew 开关（对应原库 handleCardPlugIn 的差集做法），
       所以拆成两个重载：3 参数 = 对外 ABI（全量重建，语义与原库一致），
       4 参数 = 内部实现（由 exports.map 收敛为 local，不进 .dynsym）。
       两个都不给默认值，避免 3 实参调用出现二义性。 */
    void createObject(RegFileType type, ZConfig& cfg, ZConfig& reg);
    /* onlyNew=true 时跳过 m_pAppMap 里已存在的组（对应原库 handleCardPlugIn
       先用 getGroupsKeyList 取两表组名做差集、再 getAppObject 判存在性的做法）。 */
    void createObject(RegFileType type, ZConfig& cfg, ZConfig& reg,
                      bool onlyNew);
    /* ★v60 ABI 修正：这三个原库都是 **static** 成员。
       C++ 名字修饰不区分 static / 非 static，符号名完全相同，但寄存器约定不同：
         非 static:  r0=this, r1=第1参, r2=第2参
         static   :  r0=第1参, r1=第2参
       实测 libezxaid.so 的调用点（mov r0,栈上QString; mov r1,r6; bl）从不传 this，
       原库序言也证明 r0 就是 QString 指针或返回值缓冲（见 getConfigFileName@0x41af7210
       的 `mov r8,r0` + `cmp r4,#0xb` 跳表、changeFileAttr@0x41afef34 的
       `mov r5,r0` + chmod/chown）。声明成非 static 会让参数整体错位一格，
       把残留值当指针解引用 → SIGSEGV（我的收藏装 mpkg 秒退即此因）。 */
    static QString getConfigFileName(RegFileType type);
    void initRegistryFiles();
    void createConfigFile(RegFileType type);
    bool copyTextFile(const QString& src, const QString& dst);   /* 原库返回 int：成功1/失败0（initRegistryFiles 用它控制重试循环） */
    void createUuid();
    static bool changeFileAttr(const QString& path);   /* 原库返回 bool：chmod==0 && chown>-1；★v60 static */
    void checkAndUpdateCardApp(AM_AppObject& app);
    void handleCardPlugIn(RegFileType type);
    void handleCardPlugOut();

    /* ---- 文件夹/应用操作 ---- */
    bool createFolder(const QString& name, const QString& path,
                      const QString& icon, const QString& aniIcon);
    void deleteFolder(const QString& name);
    bool changeOrder(const QString& folderPath, const QList<QString>& order);
    /* v65：参数语义按原库 0x41afd844 修正——
       (name, newName, newBigIcon, newAniIcon)。第三个参数原实现误当"路径"，
       实际写的是注册表 "BigIcon"；第四个是 "AniIcon"。 */
    void modifyApp(const QString& name, const QString& newName,
                   const QString& newBigIcon, const QString& newAniIcon);
    bool changeFolder(const QString& name, const QString& oldFolder,
                      const QString& newFolder, int index);
    /* v32：addAppToFolder 在原库是【信号】（见下方 signals 段），此处不再重复声明。 */
    bool addAppToMainMenu(const QString& app, const QString& folder, int index);
    void addAppToRegistry(const AM_AppRecord& record, QString& out,
                          const QString& folder, AM_Global::Device device, int index);
    int  removeAppFromMainMenu(const QString& app, bool flag);
    virtual void notifyAddAppToFolder(const QString& app, const QString& folder, int index);
    virtual void notifyRemoveAppFromFolder(const QString& app, const QString& folder);

    /* ---- 自启动 ---- */
    bool addAppToAutoRun(const QString& uuid);
    bool addAppToAutoRun(const QUuid& uuid);
    bool removeAppFromAutoRun(const QUuid& uuid);
    bool removeAppFromAutoRun(const QString& uuid);

    /* ---- 应用锁 ---- */
    void setAppLockValue(const QUuid& uuid, int value);

public slots:
    /* v32：元对象槽表 1:1 复原。
       证据：_ZN18AM_RegistryManager16staticMetaObjectEv @0x41b16974 反汇编 ——
         new_metadata(6) 建第一张表（6 项，均带 qt_invoke 代码指针=槽），
         第二张表 5 项在 new_metaobject 的第 5/6 参数位（=信号）。
         两张表的签名字符串（GOT 0xfe8..0xffc / 0x1000..0x1010）实测为：
           槽: slotReloadAppLockValue() / slotAppInstalled(...) /
               slotAppUnInstalled(...) / slotAddAppToFolder(...) /
               slotHotPlugForceUpdate(...) / slotChannelMessage(...)
         顺序必须与原库一致（槽索引参与 qt_invoke 派发）。 */
    void slotReloadAppLockValue();
    void slotAppInstalled(AM_Global::Device device, const QString& path,
                          const QString& name, int index);
    void slotAppUnInstalled(const QString& path, const QString& name);
    void slotAddAppToFolder(const QString& app, const QString& folder, int index);
    void slotHotPlugForceUpdate(const QCString& channel, const QByteArray& data);
    void slotChannelMessage(const QCString& channel, const QByteArray& data);

signals:
    /* v32：信号表 1:1 复原（顺序 = 信号索引），字符串实测为
         folderChanged(const AM_Folder*)
         appLockValueChanged()
         appInstalled(AM_Global::Device,const QString&,const QString&,int)
         appUnInstalled(const QString&,const QString&)
         addAppToFolder(const QString&,const QString&,int)
       关键：am 里存在 2appUnInstalled( const QString &, const QString & )、
       2appInstalled(...)、2addAppToFolder(...)、folderChanged( const AM_Folder * )
       这些 SIGNAL() 字符串，说明 am 会 connect 到本类这 5 个信号。
       旧版头文件把它们全写成普通成员函数（外加两个自造的
       appLockValueChangedSignal / folderChangedSignal），connect() 必然
       报 "No such signal" 静默失败 —— 这正是"卸载软件不能用"（卸载后
       am 的 slotAppUnInstalled 收不到 appUnInstalled）与"排序/增删后
       界面不刷新"（收不到 folderChanged）的直接原因。 */
    void folderChanged(const AM_Folder* folder);
    void appLockValueChanged();
    void appInstalled(AM_Global::Device device, const QString& path,
                      const QString& name, int index);
    void appUnInstalled(const QString& path, const QString& name);
    void addAppToFolder(const QString& app, const QString& folder, int index);

private:
    /* 信号 addAppToFolder 同名的实际动作体（原库 slotAddAppToFolder 的实现，
       改名以便与同名信号共存；对外可见的入口仍是 slotAddAppToFolder）。 */
    void doAddAppToFolder(const QString& app, const QString& folder, int index);

    QMap<QString, AM_AppObject*>* m_pAppMap;        /* 0x24 */
    QValueList<QString>*          m_pAutoRunList;   /* 0x28 QValueList<QString> autorun uuid */
    AM_AppFactory*                m_pAppFactory;    /* 0x2C */
    QCopChannel*                  m_pChannel;       /* 0x30 */
};

#endif // AM_REGISTRYMANAGER_H
