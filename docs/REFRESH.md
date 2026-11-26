# 安装 / 卸载 是怎么刷新主菜单的（完整链路）

> 这条链路是本工程最折腾的地方：信号名差一个字符、少发一个 `folderChanged`，
> 界面就"卸载完图标还在"或"排序不生效"。下面是实机跑通后的完整链路。

## 0. 全景图

```
 ┌──────────────┐  QCop "AM/RegistryiMgr/Channel"   ┌────────────────────────┐
 │ 安装器 /     │ ────────────────────────────────> │ AM_RegistryManager     │
 │ ezxaid       │  InstallApp / UninstallApp /      │  slotChannelMessage()  │
 │ (AM_QtPkgMgr)│  AddAppToFolder                   │        │               │
 └──────────────┘                                   │        ├─ slotAppInstalled()
                                                    │        └─ slotAppUnInstalled()
                                                    │        │               │
                                                    │  修改 m_pAppMap + 写 ZConfig
                                                    │  修改 AM_Folder::m_appList
                                                    │        │               │
                                                    │   emit folderChanged / appInstalled
                                                    └────────┼───────────────┘
                                                             │ Qt 信号
                                              ┌──────────────▼───────────────┐
                                              │ AM_AppDataInterface          │
                                              │  (am 侧唯一入口，构造时把    │
                                              │   folderChanged 原样中继)    │
                                              └──────────────┬───────────────┘
                                                             │
                                              ┌──────────────▼───────────────┐
                                              │ am 的 AM_HomeScreen /        │
                                              │ AM_GroupListView             │
                                              │  → 重新取 getAppList() 重绘  │
                                              └──────────────────────────────┘
```

## 1. QCop 通道与消息

通道名（原库字面池，`AM_RegistryManager` 构造时注册）：

```
AM/RegistryiMgr/Channel        ← 注意是 RegistryiMgr（原厂拼错了，必须照抄）
```

| 消息名（QCop 的 "member(QType,QType)"） | 载荷（QDataStream 顺序） | 落到哪个函数 |
|---|---|---|
| `InstallApp(int, QString, QString, int)` | device, path, name, index | `slotAppInstalled()` |
| `UninstallApp(QString)` | path, name | `slotAppUnInstalled()` + `emit appUnInstalled()` |
| `AddAppToFolder(QString, QString, int)` | app, folder, index | `doAddAppToFolder()` + `emit addAppToFolder()` |

热插拔走另一条：`slotHotPlugForceUpdate(QCString, QByteArray)`（解析 int 后转 `handleCardPlugIn`）。

分发实现（`src/AM_RegistryManager.cpp::slotChannelMessage`）：

```cpp
if (channel == "UninstallApp(QString)") {
    QDataStream ds(data, IO_ReadOnly);
    QString path, name;
    ds >> path >> name;
    slotAppUnInstalled(path, name);   // 动作体（真的删对象）
    emit appUnInstalled(path, name);  // 通知体
    return;
}
```

> 原库里 `appUnInstalled` 只是**信号发射体**，动作体在 `slotAppUnInstalled`，
> 两者靠构造函数里的自连串起来。我们额外直接调一次，避免依赖连接顺序。

## 2. 安装：`slotAppInstalled`（原库 0x41b07194，8 步）

```c
1) path 为空 → 直接返回
2) 按 device 选注册表文件：
     Flash2(1) → getConfigFileName(UserConfig)   写 /ezx_user/download/appwrite/am/UserConfig
     Card1(2)  → getConfigFileName(UserMenuTree) 写 .../am/UserMenuTree
     其它      → 返回（原库只处理这两种）
3) 在该 cfg 里按组名 == path 找到组，读组内 key 列表 → QMap<String,String> props
4) props["Type"].toInt() → AM_AppFactory::createAppObject(type)
5) 新对象 createObject(props)   // 虚表 +0xc，填 FName/FDir/FBigIcon/AppID...
6) 对象 UUID = 组名；FTemp044 = device
7) 插入 m_pAppMap[group]
8) 加入父文件夹：parent->add(app, index)；置可见位(0x40)=true；emit folderChanged(parent)
   再加入"全部"文件夹：getSystemFolderUid(FolderAll) → add(app,-1) → emit folderChanged(all)
```

最后 `addAppToMainMenu()` 成功才 `sync()` 落盘，然后 `emit appInstalled(...)`。

## 3. 卸载：`slotAppUnInstalled`（原库 0x41b07e2c，5 步）

```c
1) (path 非空 || name 非空) 且 getAppObject(path)、getAppObject(name) 都非空
2) folder(name 所在组) → folder->remove(app)          // 从分组里摘掉
3) allFolder = getAppObject("cb839c8f-72c3-42ac-bf17-b01080cb5705")
   allFolder->remove(app)                             // 从"全部"里摘掉
   ※ 这里必须用字面常量，不能用 getSystemFolderUid(FolderAll)
     —— 后者返回的是根 2a58c0d6-...，写了就"全部"列表残留
4) m_pAppMap 里按对象反查真实键（am 传进来的 path 可能是 AppID 不是组名）→ remove + delete
5) emit folderChanged(folder)；emit folderChanged(allFolder)
```

**删对象时要注意**：`m_pAppMap` 里同一个对象可能挂多个键（别名），
且 `AM_Folder::m_appList` 里也有指针 —— `AM_Folder::~AM_Folder()` 不 delete 子对象
（原库如此），所以删除责任只在 `m_pAppMap` 这一处，去重后 delete 一次。

## 4. 刷新主菜单：靠的是 `folderChanged`

`AM_Folder` 的 `add/remove` 只改自己内部的 `QList<AM_AppObject>`，**不发信号**；
所以每次增删后必须由调用方 `emit folderChanged(folder)`，否则界面不刷新。

中继链（`src/AM_AppDataInterface.cpp` 构造函数，原库 0x41aea438 实证）：

```cpp
QObject::connect(pRegMgr, SIGNAL(folderChanged(const AM_Folder*)),
                 this,     SIGNAL(folderChanged(const AM_Folder*)));
```

`AM_AppDataInterface` 是 am 拿注册表通知的**唯一入口**。没有这个 connect，
am 在 interface 上 connect 的 `folderChanged` 永远收不到 —— 这就是早期
"排序改了但界面不变""卸载后图标还留着"的直接原因。

## 5. 三个必须照抄的坑

**(1) 信号名要带 `2` 前缀。**
原库的元对象里信号名是 `2folderChanged( const AM_Folder*)`、`2appUnInstalled( const QString &, const QString & )`。
am 在二进制里就是按这个字符串 connect 的。头文件里少声明、或名字差一个字符，
`connect()` 静默失败，界面永不刷新。

**(2) `addAppToFolder` 既是信号又是动作体。**
原库元对象里它是信号，同时存在同名的槽实现（`slotAddAppToFolder`）。
我们保留信号，动作体改名 `doAddAppToFolder`，由 `slotChannelMessage` 调它再 emit。

**(3) 装/卸载后清图标缓存。**
`getIcon()` 现在有进程内 `QMap<QString,QPixmap>` 缓存，装/卸载必须
`amAppIconCacheClear()`，否则换了图标仍显示旧的（v41 加的钩子）。

## 6. 怎么观测（实机验证手法）

```bash
touch /ram/tmp/am_debug && kill -9 $(pidof am)   # 打开热路径日志
# 卸载一个应用，然后看：
cat /ram/tmp/amreg.log | grep -E "cwrap|removeAppFromMainMenu|slotAppUnInstalled|clearAppMap"
# 期望看到（一次卸载）：
#   [cwrap] removeAppFromMainMenu('xxxx',0)
#   [removeAppFromMainMenu] in app=... flag=0
#   [removeAppFromMainMenu] OK app=... parent=... flag=0
#   [slotAppUnInstalled] path=... folder=... done
#   [clearAppMap] deleted=81
rm -f /ram/tmp/am_debug
```

判断"泄漏有没有修好"：每次卸载后 `clearAppMap` 都要打印 `deleted=81`，
且 `/proc/meminfo` 的 `MemFree` 不该阶梯式下降。

| 症状 | 直接原因 | 检查点 |
|---|---|---|
| 卸载后图标还在 | 没 emit `folderChanged`，或删的是 `m_pAppMap` 而没动 `allFolder` | 步骤 3、5 |
| 第二次卸载卡死 | `clearAppMap` 不 delete 对象 → 每次泄漏 ~30KB → MemFree 见底 | `[clearAppMap] deleted=?` |
| 排序不生效 | `changeFolder` 没写回 `Items` 或没 emit | `[cwrap] changeFolder` 后的 flush |
| 新装应用不出现 | 组名 != path，或 device 不是 Flash2/Card1 被第 2 步挡掉 | `[createObject] type=? created=?` |
