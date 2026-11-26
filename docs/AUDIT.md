# 一致性审计与待优化清单（v43）

> 审计口径：`python sizecmp.py`（符号体量）+ 导出符号集合 diff + 实机行为验证。
> 结论先说：**"体量不一致"绝大多数是 Qt2 的 QString COW 内联噪音，不等于行为不一致**；
> 真正要补的是少数"桩函数"和几处符号层差异。

## 1. 量化总览（2026-11-17 复测，v43）

```
原库定义符号：411
本库定义符号：515
本库缺失：    2      ← 都是 Qt2 内部模板构造函数，无调用方
本库多出：    106    ← 调试设施 + 新增模板实例 + 2 个匿名命名空间符号
体量比 < 0.2：40   （其中原库 > 500B 的重点项 10 个）
```

复测命令（自包含，不依赖 binutils —— 那套 `arm-linux-*` 在本机缺 DLL 跑不起来，
`readelf.exe` 直接 exit 127）：

```python
# 读 .dynsym/.dynstr，st_value!=0 即"已定义"，按 st_info>>4 分绑定、&0xf 分类型
# 注意 Elf32_Sym 是 16 字节：'<IIIBBH'，不是 18 字节
```

## 2. 缺失的 2 个符号（符号层差异，非功能缺失）

| 符号 | 原库体量 | 说明 |
|---|---|---|
| `QMapPrivate<QString,QString>::QMapPrivate(const QMapPrivate*)` | 296B | Qt2 内部类模板实例 |
| `QValueListPrivate<QString>::QValueListPrivate(const QValueListPrivate&)` | 284B | 同上 |

原因：原库编译单元里这些模板被**显式实例化并导出**，我们的编译单元没有触发同一实例。
影响：am 运行正常，说明当前没有调用方需要它们（我库是多出 104 个符号，
说明是"谁用了谁导出"的随机差异）。

**处理建议**：低优先级。要严格对齐可加显式实例化：
```cpp
template class QMapPrivate<QString, QString>;
template class QValueListPrivate<QString>;
```
风险：可能与 libqte-mt 里的同名弱符号产生重复定义告警，需实测。

## 3. 多出的 104 个符号（应隐藏，但不影响功能）

三类：

1. **本工程自建的调试设施**（应当隐藏或保留，取决于是否还要用）
   `crash_install` / `ring_install_handler` / `ring_dump_file` / `amLogWrite` /
   `amLogVerbose` / `amAppIconCacheClear` / `Idle_Setup::openMgr` …
2. **我们新增的 Qt 模板实例**（因为我们多用了 `QMap<QString,QPixmap>` 做图标缓存）
   `QMapPrivate<QString,QPixmap>::copy/clear` 等
3. **内联函数被导出**（`-O2` 下尾部合并/内联策略差异）

严格 1:1 的做法是加 version script 只导出原库那 410 个：
```
{ global: <410 个符号>; local: *; };
```
**不建议现在做**：一旦漏掉 am 通过 `dlopen/dlsym` 动态取的符号，会直接崩，
而收益只是"符号表更干净"。

## 4. 体量异常的重点 10 项

| 函数 | 原库 | 本库 | 判定 |
|---|---|---|---|
| `AM_AppObject::getName` | 14020 | 668 | ✅ 等价重写（13 组 if 链 → 表驱动），实机验证 6 个文件夹名 |
| `AM_AppObject::getBigIcon()` | 1624 | 36 | ✅ 等价（原库 60 次 bl 全是库内 QString/QPixmap COW 辅助，非 PLT） |
| `AM_EmbeddedLink::getInvisiblNetLinkFromDmt` | 1996 | 32 | ✅ **死代码**，无需实现（证明见 §4.1） |
| `AM_RegistryManager::getAppAutoStartStatusFromDmt` | 1164 | 32 | ✅ **死代码**，无需实现（证明见 §4.1） |
| `AM_RegistryManager::handleCardPlugIn` | 6844 | 340 → 348 | ✅ 收敛实现 + v43 补齐 `checkAndUpdateCardApp`（见 §4.2），实机日志确认 3 个卡应用均建成 |
| `AM_RegistryManager::addAppToRegistry` | 16748 | 856 | ⚠️ 简化实现，安装主流程字段可能不全（未验证） |
| `AM_RegistryManager::modifyApp` | 2168 | 128 | ⚠️ 简化（未验证） |
| `AM_Folder::makeNewRecord` | 6052 | 384 | ⚠️ 简化（未验证；原库大头是 QMap 内联噪音） |
| `AM_RegistryManager::createFolder` | 5956 | 868 | ⚠️ 简化（未验证） |
| `AM_RegistryManager::addAppToMainMenu` | 3308 | 492 | ⚠️ 简化（未验证） |

判定依据：`getBigIcon` 已被反汇编确认是 COW 噪音；`getName` 已被实机确认；
两个 DMT 函数已被三条独立证据判定为死代码（§4.1）；`handleCardPlugIn` 已在插卡
实机上确认生效（§4.2）。标 ⚠️ 的没有做过"调用序列比对"，属于**未验证**而非"确定有错"。

### 4.1 两个 DMT 桩是死代码（2026-11-17 证明）

`getAppAutoStartStatusFromDmt`(@`0x41b0ae70`) 和 `getInvisiblNetLinkFromDmt`(@`0x41af4c28`)
在 v42 及之前都是返回 `false` 的桩，一直被列为"待补"。本轮用三条独立方法确认
**没有任何东西会调用它们**，因此补全它们没有功能收益：

1. **capstone 线性扫描**库内 5340 条 `bl/b`，目标地址命中数 = 0。
2. **暴力解码**：遍历每个 4 字节字，按 ARM `BL` 编码（`cond=0b1110`、`bits[27:24]=0b1011`）
   现算目标地址，全库 4133 条 BL，命中数 = 0。
   （对照组：`vtable for QList<AM_Folder>`、`AM_Folder::getAppList` 等都能正常命中，
   说明扫描器本身没问题。）
3. **主程序导入表**：`am` 只导入 57 个符号（`_amneeds.txt`），其中
   **一个 DMT 相关符号都没有**，也没有这两个函数。

两者都是**导出给其他模块的公共 API**，本机唯一的消费者 `am` 并不使用。

顺带把 1164 字节的 `getAppAutoStartStatusFromDmt` 整条 ABI 链反出来了，将来真要补
可以直接照抄（`JemSmartPtr` = `{vtable@0, refcount@4}`；`DmtTree` 取子项走
`vtable+0x1c`、取属性走 `vtable+0x08`；`DmtData::GetBoolean(bool&)` 是真实 PLT 符号）：

```
if (!DmtTreeFactory::Initialize()) return false;
DMString principal; principal = <默认 principal 串>;
JemSmartPtr<DmtTree> tree = DmtTreeFactory::GetSubtree(principal, path.latin1(), outData);
if (!tree) { Uninitialize(); return false; }
JemSmartPtr<DmtData> node = tree->vfunc_1c(*DmtData(path));      // vtable+0x1c
if (!node) { bool v=false; node->GetBoolean(v); ... }
```

### 4.2 卡应用 Device 纠正（v43 修复的真实缺口）

原库 `handleCardPlugIn`（6844 字节）对卡注册表里每个应用依次做
`createAppObject → setName → checkAndUpdateCardApp → FTemp044=2`。
本库前两项和最后一项都等价实现了，**唯独 `checkAndUpdateCardApp` 有实现却从未被调用**
（v42 及之前全工程只有定义和声明，没有调用点）。

它的语义（`0x41b0b940`，逐条反汇编）：

```asm
ldr r3,[r1,#8]   ; Type != 2  → return
ldr r3,[r1,#0xc] ; Device == 3 → return
                 ; Device == 5 → return
strne r3,[r1,#0xc]  ; 否则 Device = 4
```

为什么必须补：本库的 `Device`(`0x0C`) 在 `AM_AppLnk::createObject` 里从注册表
`Attribute` 读出，而**卡注册表里的 `Attribute` 不可靠**。实测
`/mmc/mmca1/.system/java/CardRegistry` 三个应用：

| Name | Attribute | 纠正后 |
|---|---|---|
| 软件管理 | 4 | 4 |
| Engineering | **1** | **4** ← 不补就错 |
| Telnet | 4 | 4 |

`Engineering` 停在 `Device=1` 时 `getExecFullPath` 会拼 `/usr/SYSqtapp/ap_tcmd/efem`
（不存在），卡上这个应用点了没反应。

v43 在 `createObject()` 的 `CardRegistry` 分支里调用 `checkAndUpdateCardApp(*obj)`。
原库只有一个出口（`handleCardPlugIn` 内），本库把它收敛到 `createObject`，
对每个卡对象等价执行一次。

**实机验证**：`[getAppObject] slot=0x4 dev=0x2 exec='efem'`（修复前 `slot=0x1`）。

### 4.3 卡对象重复创建（v43 修复）

`loadMenuTree` 末尾调 `handleCardPlugIn(4)`，而此时卡应用**已经**被 loadMenuTree
链进 `Folder::m_appList`。旧实现又全量 `createObject(CardRegistry)`，
于是 3 个卡对象被新建一份覆盖进 `m_pAppMap`：

```
[loadMenuTree] linked=77 appMap size=79
[createObject] type=4 ... created=3 map=79     ← map 没涨，全是覆盖
```

后果：每次启动泄漏 3 个对象；且 Folder 挂旧对象、`m_pAppMap` 挂新对象 —— 指针分裂，
`handleCardPlugOut` 摘掉对象后菜单里图标会残留。

原库的做法是 `getGroupsKeyList` 取卡注册表与菜单表组名列表做差集、
再用 `getAppObject()` 逐个判存在性，已存在的跳过。v43 给 `createObject` 加
`onlyNew` 参数（默认 false，只有 `handleCardPlugIn` 传 true），等价实现：

```
[createObject] type=4 ... onlyNew=1 created=0 map=79   ← 空操作
```

### 4.4 安装链路：`slotAppInstalled` 三处硬错误（v44 修复）

**结论：v43 及以前「装完应用菜单里不出现」是确定的 bug，不是没测出来的偶发问题。**

先厘清链路（这一步以前没做过，导致一直在 `addAppToRegistry` 上找错方向）：

| 环节 | 归属库 | 证据 |
|---|---|---|
| 解包 / 写 InstalledDB·CardRegistry | **ezxaid.so**（`AM_JavaPackageManager` / `AM_QtPackageManager`） | `ezxelf.py ezxaid.so PackageManager` 有 67 个命中；libezxam 里 **0 个** |
| 通知 am | ezxaid → `QCopChannel::send` | ezxaid 导入 `_ZN11QCopChannel4sendE…`；字面量 `AM/RegistryiMgr/Channel` + `InstallApp(int, QString, QString, int)`（0x08cbb0 / 0x08cbc8） |
| 收消息 | libezxam `slotChannelMessage` → `emit appInstalled` | 消息名逐字节一致（原库 0x040f98 / 0x040fc0） |
| **真正建对象入菜单** | libezxam **`slotAppInstalled`** | 反向调用图：无直接 BL，靠 moc 信号派发 |

顺带证实：`am` 根本不导入 `addAppToRegistry` / `createFolder` / `modifyApp` /
`makeNewRecord` / `handleCardPlugIn`（`/ram/tmp/amund.txt` 里 117 条 AM_ 导入符号，
逐条核过）。所以那几个"体量只有原库零头"的简化实现**不在这条链路上**，
拿它们当"安装坏了"的嫌疑是错的方向。

`slotAppInstalled`（原库 0x41b07194）逐指令反汇编后，v43 实现有三处错：

**① 选错注册表文件（致命）**

```
0x41b071ec  cmp  r8, #1          ; r8 = device
0x41b07208  moveq r1, #7         ; device==1(Flash2) → getConfigFileName(7)=InstalledDB
0x41b07210  cmp  r8, #2
0x41b07248  mov  r1, #4          ; device==2(Card1)  → getConfigFileName(4)=CardRegistry
```

v43 写的是 `UserConfig(5)` / `UserMenuTree(2)`。这两个文件里不会出现
"组名 == 应用 UUID" 的记录（实测 `UserConfig` 全文只有 `[AutoRun] Apps=…`），
于是函数第一关就 `return`。

`getConfigFileName` 的 12 个 case 这次全量落实到了 GOT 字面量，映射无误：
`0xd10 SysMenuTree / 0xd18 SysRegistry / 0xd1c UserMenuTree / 0xd20 UserRegistry /
0xd24 CardRegistry / 0xd28 UserConfig / 0xd2c AppLockConfig / 0xd30 InstalledDB /
0xd34 SysMime / 0xd38 UserMime / 0xd3c PlugCardDB / 0xd40 PlugCardRegistry`。

**② props 取法错（致命）**

原库 0x41b07380 调 `ZConfig::getGroupEntriesKeyList(group)` 取组内全部键值。
v43 写 `cfg.readListEntry(group, "keys", ';')` —— 注册表里没有 `keys` 这个元键
（同一个坑在 `createObject` 里早就记过一次），拿到空表 →
`Type` 缺失 → `toInt()=0` → `createAppObject(InvalidType)` 返回 NULL → `return`。
**即便文件选对了，这一步仍然是空操作。** v44 改用本文件已有的 `amParseAll`
（一次读盘解析整表 + UTF-8/GBK 自动判别），与 `createObject` 走同一条已验证路径。

**③ "全部应用"文件夹取错**

```
0x41b07ac0  ldr r3,[pc,#0x350]   ; GOT 0xd74 → "cb839c8f-72c3-42ac-bf17-b01080cb5705"
```

v43 用 `getSystemFolderUid(FolderAll)`，取回的是根 `2a58c0d6-…`，不是"全部应用"。
（同一个错误在 `slotAppUnInstalled` 里 v25 修过一次，这里是漏改的孪生点。）

另外补上原库 `0x41b07ab4 str r4,[r2,#0x04]`（写父 Folder 指针），v43 漏了。

**自测（v44，设备实跑）**

设备上没有命令行 qcop 工具，且空闲内存只有 ~1.2MB，起一个 Qt 客户端去发消息
风险太高。改用临时钩子：在 `initRegistry` 末尾按 `/ram/tmp/am_insttest` 开关
直接调 `slotAppInstalled` 三次，覆盖三个分支，跑完即删。

```
[slotAppInstalled] dev=1 key=b4e87ac6-… folder=67dc1191-… idx=-1 type=2 inMap=1 inFolder=1 inAll=1
[slotAppInstalled] 组 deadbeef-… 不在 /ezx_user/download/appwrite//am/InstalledDB（注册表未写入？）
[slotAppInstalled] dev=2 key=3f754806-… folder=cb839c8f-… idx=-1 type=2 inMap=1 inFolder=1 inAll=1
```

三行分别说明：Flash2 从 InstalledDB 取到 Java MIDlet 并建出 `AM_AppLnk`(type=2)、
入 map、入父文件夹、入"全部"；不存在的组正确走"未写入"分支；Card1 从
CardRegistry 取到卡应用并入库。

> 注意：这个自测**不适合**在 `initRegistry` 里对"已存在的应用"重放——
> 会向 Folder 追加重复项并在初始化期间发 `folderChanged`，实测两轮都触发
> libc 堆内的 SIGSEGV（`addr=0x74007810`，见 `docs/BOOT_CRASH.md` §7）。
> 真实安装走的是"新组 + 初始化完成后"，不会走到那个状态。

### 4.5 证书校验：我们的实现把**正规签名的应用也拦了**（v45 加旁路）

#### 现场

设备日志（v44 运行时，用户点"软件管理"之后）：

```
[verifySecurityByLaunch] path=/mmc/mmca1/.system//QTDownLoad/SoftManage2/SoftManage
  dirPath=/mmc/mmca1/.system//QTDownLoad/SoftManage2
  absPath=/mmc/mmca1/.system/QTDownLoad/SoftManage2 entries=0
[verifySecurityByLaunch] -> 1 (no entries)
```

`entries=0` 之后函数按 `return 1` 收场 → am 判定"证书无效"，直接拒绝启动。
同一套逻辑也挡安装（ezxaid 调 `verifySecurityByInstall`）。

#### 为什么 entries=0

`ls /mmc/mmca1/.system/QTDownLoad/SoftManage2` 只有**文件**，没有子目录：

```
SoftManage  SoftManage.desktop  ezx_SoftManage_lang.cfg  icon-big.png  icon-sm.png
```

而我们的实现是 `QDir(dirPath, "*", QDir::Dirs, ...)` —— 只列**子目录**。
应用一般是"一个扁平目录放可执行文件 + .desktop"，天生没有子目录 → 永远 0 项。

#### 真正的签名文件是 .desktop，而且键名是前缀

`SoftManage.desktop`（**Motorola 正规签名**）里明确带着：

```
[Desktop Entry]
EzXApp-Certificate-1-1 = MIIEATCCAumgAwIBAgICBqYwDQYJKoZIhvcNAQEFBQAwgZYx…
EzXApp-Exec-RSA-SHA1   = HvAtWY7eLSVJiWucRNPynufiGq4ydwRRiRARFk6uE1SJ…
Vendor = asmotoe2   Exec = SoftManage   Size = 40185   Type = Application
```

于是可以确定三处偏差：

| # | 我们的实现 | 应该是 |
|---|---|---|
| 1 | `QDir(dirPath,"*",QDir::Dirs,...)` 取子目录 | 取同一目录里的**文件**（`QDir::Files`），目标就是 `<dir>/<X>.desktop` |
| 2 | `entries.first()` 随便取第一个 | 取与该应用对应的 `.desktop`（同名或唯一那个） |
| 3 | `parseSignedFile` 用 `readEntry(group,"EzXApp-Certificate-")` **精确键名**读 | 原库注释写的是"遍历组条目、`QString::compare` 匹配" → 应按**前缀**匹配，因为真实键是 `EzXApp-Certificate-1-1`（带序号后缀） |

第 3 点还会连带影响 `m_status`（证书内存链）始终为 NULL —— 我们只 malloc 注释、没有真填，
传给 `CM_SEC_VerifyFileSignatureByPathGetAllow` 的证书指针是空的。

**所以 v44 及以前"正版应用也起不来"不是签名过期，是我们的校验实现错了。**
这一点比"临时关掉校验"重要得多：它意味着校验恢复前必须先修好这三处。

#### v45 的做法：加旁路，不删代码

三个入口（`verifySecurityByLaunch` / `verifySecurityByInstall` / `verifySecurity`）
开头统一检查开关文件：

```cpp
#define AM_NOSEC_FLAG "/ram/tmp/am_nosec"
static bool amNoSecBypass() { return QFileInfo(QString(AM_NOSEC_FLAG)).exists(); }
```

- **文件存在 → 直接 return 0（放行）**，并在 `/ram/tmp/amreg.log` 留一行
  `[nosec] verifySecurityByLaunch BYPASS path=… -> 0`；
- 校验代码**一行没删**，全部保留在旁路分支之后；开关不存在时行为与 v44 完全一致；
- 每次调用都 `stat` 一次（ramfs 上约 1µs），所以 `touch`/`rm` **立即生效**，不用重启 am；
- `/ram` 是 ramfs，**重启自动消失** → 忘了关也不会一直裸奔。

工具：`python nosec.py on|off|status`（见仓库根目录）。


## 5. 已实机验证行为一致的功能

| 功能 | 验证方式 |
|---|---|
| 主菜单装载（81 项 / 8 组） | 启动日志 `linked=81 appMap size=81` |
| 文件夹中文名 | `全部/办公/多媒体/其它/游戏/自定1` |
| 应用启动（含带参数的相机/摄像机） | 逐个点开 |
| 通话记录 / 浏览器 / 短信 / 中国移动 / 客户服务 | 逐个点开 |
| 卸载（连续两次） | 人工操作 + `[clearAppMap] deleted=81` |
| 调整顺序/更改分组 | 人工操作 |
| 控制台 C 导出（3 个） | 卸载流程会走到 |
| 空闲 CPU / 内存 | `/proc` 采样，与原厂一致 |
| **卡注册表装载** | 日志 `[createObject] type=4 path=/mmc/mmca1/.system/java/CardRegistry groups=3` → `created=3`；`[getAppObject] HIT 5e558577-… type=4 name='Camcord'`；`[getAppObject] slot=0x4 dev=0x2 exec='SoftManage'/'efem'/'telnet.lin'`；菜单树 `linked=77 / appMap 79`；二次装载 `onlyNew=1 created=0` |
| **应用名中文化（v43 增强）** | 日志 38 条 `[appName] <uuid> -> '<中文>'` 全部命中，无 CRASH |
| **空闲 CPU / 内存（v43）** | 30 秒窗口 `du=0 ds=0` state=S，MemFree 1252~1572KB |

## 6. 尚未验证的功能面（优先补这些）

按"用户能感知"排序：

> **前置**：任何"启动应用 / 安装应用"的动作都要先过证书校验，
> 而我们的校验实现有 3 处偏差，连 Motorola 正规签名的应用都被拦（§4.5）。
> 所以下面 1、3 两项**必须先 `python nosec.py on` 才有意义**；
> 想在"不旁路"的状态下验证，得先把 §4.5 那三处修好。

1. **装一个新应用**（走 ezxaid → QCop → `slotAppInstalled`，见 §4.4）——
   三处硬错误已在 v44 修掉并自测通过，但**还没在真实安装动作下跑过**：
   需要人工装一个 mpkg / jar，确认菜单出现图标、属性页信息齐全、重启后仍在。
2. **创建/删除文件夹**（`createFolder` / `deleteFolder`）—— 新建分组后重启是否还在。
3. **卡上应用的实际启动**（`Engineering`）—— v43 补齐了 Device 纠正，需要在设备上
   确认 `[getExecFullPath] dir=ap_tcmd exec=efem` 的 slot 已经是 4 并能启动。

已从本清单移除的：

- ~~插 SD 卡~~ → 已实测（§5 最后两行）。
- ~~自启动开关~~ / ~~DMT 不可见链接~~ → 死代码，无法也不需要验证（§4.1）。

## 7. 建议的下一步（按性价比）

| 优先级 | 事项 | 做法 | 风险 |
|---|---|---|---|
| P0 | v45 实机验收 | 部署 `backup/v45_20261117/` 的库；`python nosec.py on` 后再装/跑应用 | 低 |
| P0 | **修 `AM_SecurityManager` 三处偏差**（§4.5） | `Dirs`→取文件、`entries.first()`→找 `.desktop`、`parseSignedFile` 改前缀匹配 + 真填 `m_status` | 中（要跑通 CM_SEC） |
| P0 | 真机装一次应用 | 装 mpkg / jar 各一次，看菜单图标 + `[slotAppInstalled] … inMap=1 inFolder=1 inAll=1`（§4.4 已修三处错） | 无 |
| P1 | 启动期崩溃定责 | `python ab_boot.py orig_local.so 90` 跑原厂库 A/B（见 `docs/BOOT_CRASH.md` §6） | 低 |
| P1 | 卸载一个应用实测 | `[slotAppUnInstalled]` + 菜单项消失 + "全部"不残留 | 无 |
| P2 | 清掉 `[i18n]` 6 组对照日志 | 那是当初定 context 的实验代码，不该留在热路径 | 低 |
| ~~P2~~ | ~~显式实例化缺失符号~~ | **v51 已完成**：`QMapPrivate<QString,QString>::C4`、`QValueListPrivate<QString>::C4` 用 asm 别名转发；另外发现并补上了第 3 个：`AM_RegistryManager::createObject(RegFileType, ZConfig&, ZConfig&)`（原库唯一导出形态，我们之前只有 4 参数版） | 低 |
| ~~P2~~ | ~~version script 收敛导出符号~~ | **v51 已完成**：`gen_exports.py` 从原库 `.dynsym` 机器生成 `exports.map`，实测 410/410、缺 0 多 0；隐藏的 109 个符号无任何固件库依赖（见 STATUS.md v51 §4） | 已消除 |
| P3 | `addAppToRegistry` / `createFolder` / `modifyApp` 补全 | **优先级降到底**：`am` 根本不导入它们（§4.4），只有当别的进程用到才需要 | — |
| P3 | ~~补两个 DMT 桩~~ | 已证明是死代码，不做（§4.1） | — |

## 8. 怎么继续做函数级核对（可复现的操作）

```bash
# 1) 谁的体量差得离谱
python sizecmp.py | head -40

# 2) 看某个函数原库到底调了哪些外部函数
python - <<'EOF'
import json
from capstone import *
from capstone.arm import ARM_OP_IMM
d=open('orig_local.so','rb').read(); BASE=0x41ad8000
plt={int(k,16):v for k,v in json.load(open('am_plt.json')).items()}
md=Cs(CS_ARCH_ARM, CS_MODE_ARM); md.detail=True
v=0x41b01508   # 目标函数 vaddr（orig_symvals.json 里查）
for ins in md.disasm(d[v-BASE:v-BASE+16748], v):
    if ins.mnemonic=='bl' and ins.operands[0].type==ARM_OP_IMM:
        n=plt.get(ins.operands[0].imm-BASE)
        if n: print('%08x %s'%(ins.address,n))
EOF

# 3) 行为比对：换原厂库跑同一操作，对比日志/界面
python deploy_lib.py orig_local.so
#    ... 操作 ...
python deploy_lib.py          # 换回本库
```

第 2 步的产物（"原库调了哪些外部函数"）与我们的实现逐条对照，
**差集里出现的关键调用就是漏掉的步骤** —— 这是补桩最快的方法。

> 注：`am_plt.json` 只覆盖了一部分 PLT 槽。要完整映射需按 `.rel.plt`
> 的 `R_ARM_JUMP_SLOT` 重定位表重建（`eplt.py` 可以，但本轮没跑完整）。
> 上面对 `getBigIcon` 的手工核对表明：**多数 bl 目标是库内局部函数（COW 辅助），
> 不是外部调用**，所以"体量小"本身不构成缺陷证据。
