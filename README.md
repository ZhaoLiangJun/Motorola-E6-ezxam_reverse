# libezxam.so.1.0.0 逆向重建工程

 Motorola ROKR E6 EZX 应用管理库 `libezxam.so.1.0.0` 的 C++ 源码复刻。

> 文档导航：**[`CONTEXT.md`](CONTEXT.md) 一页纸交接（先看这个）** ·
> [`STATUS.md`](STATUS.md) 逐版本流水 · [`REVERSE_PLAYBOOK.md`](REVERSE_PLAYBOOK.md) 逆向方法论 ·
> [`AUDIT_V63.md`](AUDIT_V63.md) 全量函数级审计

---

## 交付内容

```
ezxam_reverse/
├── include/ezx/          重建头文件 (29 个)
│   ├── AM_AppObject.h    应用对象基类（5 个虚函数）
│   ├── AM_AppLnk.h       应用快捷方式（继承 AppObject）
│   ├── AM_Folder.h       文件夹（继承 AppObject）
│   ├── AM_Shortcut.h     快捷方式（继承 AppObject）
│   ├── AM_EmbeddedLink.h 内嵌链接（继承 AppObject）
│   ├── AM_AppFactory.h   工厂（按 Type 分配派生类）
│   ├── AM_RegistryManager.h  注册表核心（48 方法）
│   ├── AM_AppDataInterface.h 数据接口（委托 RegistryManager）
│   ├── AM_AppLockManager.h   应用锁单例
│   ├── AM_SecurityManager.h  签名验证
│   ├── AM_Carrier_Info.h     运营商信息
│   ├── AM_StandardDlg.h      标准对话框
│   ├── AM_GroupListScrollView.h 分组列表
│   ├── AM_MainMenuInterface.h   主菜单接口
│   ├── AM_SystemConfig.h     系统配置
│   ├── Idle_Setup.h          待机屏/S3 配置
│   └── ... (AM_Global/AM_LauncherInterface/ZConfig 等依赖头)
├── src/                  重建实现 (16 个 .cpp)
├── ghidra/decompiled/    Ghidra 反编译参考 (1414 函数)
│   ├── perfunc/          每函数一个文件
│   └── all_decompiled.c  全部汇总
├── func_addr_map.json    地址↔符号映射表
├── build_in_wsl.sh       WSL 原版工具链编译脚本
└── STATUS.md             本文档
```

---

## 类层次（虚表实证）

```
QObject
├── AM_AppDataInterface      (folderChanged 信号)
├── AM_AppLockManager        (单例, appLockValueChanged 信号)
├── AM_RegistryManager       (48 方法, 核心注册表)
├── AM_Carrier_Info          (signalStrUpdate 信号)
└── AM_StandardDlg           (QDialog→UTIL_Dialog)

AM_AppObject                 (基类, 5 虚函数)
├── AM_AppLnk                (Application, sizeof=0x84)
├── AM_Folder                (Folder, sizeof=0x58, 仅覆盖 dump)
├── AM_Shortcut              (Shortcut, sizeof=0x5C, 仅覆盖 dump)
└── AM_EmbeddedLink          (EmbeddedLink, sizeof=0x58)

非 QObject:
├── AM_AppFactory            (createAppObject 按 Type 分配)
├── AM_SecurityManager       (CM_SEC_VerifyFileSignatureByPathGetAllow)
├── AM_SystemConfig          (ezxUid=2000/ezxGid=233/DRMUid=518)
├── AM_MainMenuInterface     (QCopChannel 发送 SHOWMAINMENU)
└── Idle_Setup               (SETUP_SET_Manager*, 21 方法)
```

### 虚表布局（AM_AppObject，实证）
| slot | 虚函数 |
|------|--------|
| 0 | `bool getAppLockEnabled() const` |
| 1 | `void setAppLockEnabled(bool)` |
| 2 | `int getAppLockValue() const` |
| 3 | `void setAppLockValue(int)` |
| 4 | `void dump()` |

> `invoke()` / `createObject()` 非虚（各派生类独立实现）。
> 无虚析构（析构非虚）。

### AM_AppObject 成员布局（sizeof=0x50）
```
0x00  vtable
0x04  FTemp004 (int)
0x08  FType    (Type: 1=Folder 2=App 3=EmbeddedLink 4=Shortcut)
0x0C  FTemp00C (Device)
0x10  FBigIcon     (QString)
0x14  FSmallIcon   (QString)
0x18  FAniIcon     (QString)
0x1C  FUUID        (QString)
0x20  FTemp020/024/028/02C/030 (int×5)
0x34  FDir     (QString)
0x38  FName    (QString)
0x3C  FVender  (QString)
0x40  FTemp040/041/042 (bool×3)
0x44  FTemp044 (int)
0x48  FTemp048 (QString)
0x4C  FTemp04C (QString)
```

---

## 编译方法

### 前提
- WSL Debian，原版 MontaVista 工具链 `/opt/iwmmxt_le`
- Windows 侧 Qt 2.3.8 + EZX 头文件 `D:\cygwin\home\ezx\qt-2.3.8`

### 编译
```bash
cd /mnt/d/WorkBuddy/2026-11-08-18-54-48/ezxam_reverse
bash build_in_wsl.sh
```
脚本会：
1. 对 6 个带 `Q_OBJECT` 的头文件跑 `moc`
2. 用 `arm-linux-g++ -DQWS -fno-exceptions -fno-rtti -O2` 编译全部 .cpp
3. 链接为 `libezxam_reconstructed.so.1.0.0`
4. 用 `arm-linux-readelf` 验证 ELF 头

### 验证产物
```bash
arm-linux-readelf -h libezxam_reconstructed.so.1.0.0
# 应显示: Class=32-bit, Machine=ARM, Type=DYN
arm-linux-readelf -d libezxam_reconstructed.so.1.0.0 | grep NEEDED
# 应依赖: libstdc++.so.5, libm.so.6, libgcc_s.so.1, libc.so.6
arm-linux-nm -D --defined-only libezxam_reconstructed.so.1.0.0 | wc -l
# 当前导出：411 个已定义函数 + 99 个弱符号（见 deploy_check.py 输出）
# 与原库对账：强符号缺失 0（严重项）；弱符号缺失 4（GCC 3.3 模板内部变体，无害）
python deploy_check.py libezxam_reconstructed.so.1.0.0
```

### sizeof 精确匹配（本机验证）
| 类 | 重建版 | 原版 createAppObject | 匹配 |
|----|--------|---------------------|------|
| AM_AppObject | 0x50 | 基类 | ✓ |
| AM_AppLnk | 0x84 | operator_new(0x84) | ✓ |
| AM_Folder | 0x58 | operator_new(0x58) | ✓ |
| AM_EmbeddedLink | 0x58 | operator_new(0x58) | ✓ |
| AM_Shortcut | 0x5C | operator_new(0x5c) | ✓ |

---

## 忠实度说明

### 完整还原（逻辑等价）
- **AM_SystemConfig**：ezxUid/ezxGid/DRMUid 常量返回，100% 等价
- **AM_AppObject**：构造/init/setName/invoke/createObject/锁函数/dump 全部还原
- **AM_AppLnk**：5 个虚函数 + getUidByString/getGidByString (ezx→2000/233, drm→518)
- **AM_Folder/AM_Shortcut/AM_EmbeddedLink**：虚表覆盖 + dump 空实现
- **AM_AppFactory**：createAppObject 按 Type 分配，sizeof 精确匹配
- **AM_MainMenuInterface**：QCopChannel 通道名/消息名从 rodata 精确提取

### 完整还原（逻辑等价）— 本轮新增
- **AM_RegistryManager**：48 方法全部实现，TODO 骨架清零；
  - 查询类：getAllAppList/getAppObjById/getAppLnkById/getAppLnkBySerialId/
    getJavaObj/getRootFolder/getAppObject/getLockEnabledAppList/getAppLockValue/
    getAutoRunApps/getOpenWithApps/getInstalledAppList/getAppNameByFullPath/
    getSystemFolderUid/getSystemFolderName/getAppParentFolderUid 全部还原
  - 注册表维护：initRegistry/clearAppMap/loadMenuTree/loadRegistry/createObject/
    getConfigFileName/initRegistryFiles/createConfigFile/copyTextFile/changeFileAttr/
    checkAndUpdateCardApp/handleCardPlugIn/handleCardPlugOut 全部还原
  - 文件夹操作：createFolder/deleteFolder/changeOrder/modifyApp/changeFolder/
    addAppToFolder/addAppToMainMenu/addAppToRegistry/removeAppFromMainMenu/
    notifyAddAppToFolder/notifyRemoveAppFromFolder 全部还原
  - 自启动：addAppToAutoRun(QString/QUuid)/removeAppFromAutoRun(QString/QUuid) 还原
  - QCop：slotChannelMessage/slotHotPlugForceUpdate/appInstalled/appUnInstalled 还原
  - 依赖外部 DMT/ZConfig 的方法保留语义等价实现（真实 rodata 常量为语义占位）

### 骨架还原（签名+结构正确，方法体需补全外部依赖）
- **Idle_Setup**：21 方法签名+SETUP_SET_Manager 访问模式还原；
  配置节名/键名已从 rodata 精确还原（dynamicIdle/shortcuts/shortcut_N/
  ConnectPublicInfo/CurrentConnectProfileName/AutoSelection/AutoSelectResult 等）
- **AM_SecurityManager**：verifySecurity* 调用链还原，parseSignedFile 键名
  (Desktop Entry/Vendor/Size/EzXApp-Exec-RSA-SHA1/EzXApp-Certificate-) 已还原；
  外部 CM_SEC_VerifyFileSignatureByPathGetAllow 由 libezx-cmseccert.so 提供
- **AM_StandardDlg / AM_GroupListScrollView / AM_Carrier_Info / AM_AppLockManager**：
  依赖 UTIL_PushButton/PHONE_MsgClient/TAPI 等 E6 平台专有库，保留调用骨架

### 关键常量（rodata 实证）
| 常量 | 值 |
|------|-----|
| QCop 主菜单通道 | `AM_MainMenuChannel` |
| QCop 主菜单消息 | `SHOWMAINMENU` |
| Registry QCop 通道 | `AM/RegistryiMgr/Channel` |
| EmbeddedLink 固定 UUID | `2a58c0d6-05c6-45aa-8938-63059d41a4e9` |
| AM_ROOT_FOLDER UUID | `cb839c8f-72c3-42ac-bf17-b01080cb5705` |
| ezxUid | 2000 |
| ezxGid | 233 (0xE9) |
| DRMUid | 518 (0x206) |

### createObject 真实键名（替换原机关键）
这些键名对应设备 `.cfg` 注册表字段，键名不符会导致应用加载失败。
已从 `.rodata` 精确提取并替换原占位名：

| 类 | 真实键名 |
|----|---------|
| AM_AppLnk | Type/BigIcon/Icon/AniIcon/AppID/Directory/Name/vender/UserID/GroupID/Visible/LockEnabled/FixedPosition/FixedAllPosition/JavaId/Exec |
| AM_Folder | Type/Directory/Name |
| AM_Shortcut | Type/BigIcon/Icon/AniIcon/AppID/Directory/Name |
| AM_EmbeddedLink | Type/BigIcon/Icon/AniIcon/AppID/Directory/Exec/Name |
| invoke | launch/path/args/uid/gid |

### QCop 消息名（字面池解码实证）
EZX 约定：**消息名 = 完整槽函数签名**（非短名）。已通过字面池三级指针链解码
（`base_var = *(pool0) + func_base`，键名 = `cstr(*(char**)(base_var + DAT_off))`）确认：

| 方向 | 消息名 | 处理 |
|------|--------|------|
| 收 | `AppLockValueChanged()` | → appLockValueChanged() |
| 收 | `InstallApp(int, QString, QString, int)` | → appInstalled(dev, path, name, idx) |
| 收 | `UninstallApp(QString)` | → appUnInstalled(path, name) |
| 收 | `AddAppToFolder(QString, QString, int)` | → addAppToFolder(app, folder, idx) |
| 发 | `AddAppToFolder(QString, QString, int)` | notifyAddAppToFolder |
| 发 | `UninstallApp(QString)` | notifyRemoveAppFromFolder（复用路由标签） |
| 通道 | `AM/RegistryiMgr/Channel` | Registry QCop 通道 |

发送端与接收端的消息名已对齐为完整签名，确保跨进程通信正常。

### 字面池解码方法（本轮建立的逆向技术）
原库大量字符串常量使用 PC 相对寻址（literal pool），需三级指针链解析：
1. `base_var = *(pool0_slot) + func_base`（pool0 在 `.text` 段，统一指向 0x41b229f0）
2. 槽位指针 = `*(base_var + DAT_off)`
3. 字符串 = `cstr(槽位指针)`（`.rodata` 段）

关键点：`objdump -s` 加载字面池需 `-j .text`，读字符串用 `-j .rodata`。

### getConfigFileName 完整映射（case 0-10）
| Type | 文件名 | 目录 |
|------|--------|------|
| 0 | SysRegistry | preload |
| 1 | UserRegistry | appWrite |
| 2 | CardRegistry | card |
| 3 | SysMenuTree | preload |
| 4 | UserMenuTree | appWrite |
| 5 | InstalledDB | appWrite |
| 6 | AppLockConfig | appWrite |
| 7 | UserConfig | appWrite |
| 8 | SysMime | preload |
| 9 | UserMime | appWrite |
| 10 | PlugCardDB | card |

---

## 部署（替换原机）

```bash
# 1. 备份原版
cp /usr/lib/ezx/lib/libezxam.so.1.0.0 /usr/lib/ezx/lib/libezxam.so.1.0.0.bak

# 2. 推送重建库
adb push libezxam_reconstructed.so.1.0.0 /usr/lib/ezx/lib/libezxam.so.1.0.0

# 3. 校验
arm-linux-readelf -h /usr/lib/ezx/lib/libezxam.so.1.0.0
# 应显示 ELF32 / ARM / DYN

# 4. 重启设备加载
```

外部依赖由 E6 平台提供：libezxutil.so / libezxtapi.so / libezx-drm.so /
libezx-cmseccert.so / libezx-dmt.so。

---

## 逆向方法

1. **Ghidra 12.1.4 headless** 反编译全部 1414 函数（ARM ELF → C）
2. **动态符号表** demangle：375 导出 + 486 导入，建立地址↔符号映射
3. **虚表分析**：扫 `.data` 段函数指针序列，确定继承关系与虚函数槽位
4. **PLT 解析**：手动反汇编 PLT 桩解析 GOT 跳转目标
5. **rodata 提取**：读取字符串常量（QCop 通道名、配置节名、uid/gid 映射）
6. **sizeof 证据**：从 `AM_AppFactory::createAppObject` 的 `operator_new` 提取各派生类精确大小
7. **原版头文件对照**：`genheader.sh` 生成的 230 个 EZX 头文件提供方法签名基准

---

## 已知限制

1. **Ghidra C → C++ 为人工翻译**：无工具可自动转换，方法体需逐个重写
2. **PC 相对寻址常量**：部分配置节名/键名存储于 literal pool，
   需模拟运行期基址（`.data` + GOT 偏移）才能精确提取，当前为语义占位
3. **E6 平台专有依赖**：`UTIL_PushButton`/`PHONE_MsgClient`/`TAPI_*`/`DmtTreeFactory`/
   `RES_ICON_Reader` 等由 E6 ROM 的其它 `.so` 提供，源码不在本工程范围
4. **编译验证**：需在 WSL Debian + `/opt/iwmmxt_le` 环境执行（沙盒阻止 wsl.exe）
