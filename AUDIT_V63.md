# 全量函数级对比审计（v63）

> 目的：回答「重建库和原库是否行为一致、还有没有隐藏 BUG」。
> 方法：对两个库的**同名导出函数逐个**比对四类指纹，再按实际调用方决定风险等级。
> 工具：`audit_all.py`（新增）、`calldiff.py`、`chk_v63.py`

---

## 1. 覆盖率

| 项 | 数值 |
|---|---|
| 原库导出函数 | 373 |
| 本库导出函数 | 373 |
| 逐个比对 | **373 / 373（100%）** |
| 导出符号面 | 410 / 410，缺 0 多 0 |
| 被标记需人工判定 | 109（按指纹差异） |
| 其中「近乎空壳」(<5% 体量) | 8（逐一分析后：5 个是转发/别名，3 个是真差异） |

比对指纹四项：

1. **调用序列** —— `BL` 目标解析成符号名后的有序列表
2. **`this` 写入** —— 函数体内 `str rX,[r0,#imm]` 的偏移集合（暴露成员布局、判 static）
3. **分配尺寸** —— `mov r0,#N` 紧跟 `operator new` 的 N（暴露空壳类 sizeof）
4. **体量比** —— ours / orig

> 别名陷阱：GCC 会把 `C1/C2/C4`（`D0/D1/D2/D4`）中的一个做成真身、其余做成 4 字节
> `b <真身>` 甚至 `b <真身PLT桩>`。原库与我们的**别名方向可能相反**
> （原库 C4 是真身 788B、我们是 C2 真身 548B、C4 只是 4B 跳转）。
> `audit_all.py` 会先跟随别名再比，否则会凭空多出 25 个「空壳」误报。

---

## 2. 本轮查出并修复的真 BUG（v63）

### BUG-1　应用锁配置组名错误 → 与原厂数据不兼容（最严重）

| | 原库 | 我们(v62) |
|---|---|---|
| 读 | `readEntry("AppLockValue", uuid)` | `readEntry("", uuid)` |
| 写 | `writeEntry("AppLockValue", uuid, v)` | `writeEntry("", uuid, v)` |
| 重载 | `readNumEntry("AppLockValue", ...)` | `readNumEntry("", ...)` |

组名取自原库 `GOT[0xcf8] = "AppLockValue"`（三处一致）。
自己读自己写能对上，所以自测发现不了；但**原厂写下的锁值我们读不到、
我们写下的原厂也读不到** —— 典型的数据格式不兼容，升级/回滚时用户会丢设置。

已修 3 处：`AM_AppLockManager.cpp:getAppLockValue`、
`AM_RegistryManager.cpp:setAppLockValue`、`AM_RegistryManager.cpp:slotReloadAppLockValue`。

### BUG-2　`AM_AppLockManager` 构造缺 QCopChannel + connect

原库 `0x41b0bfa0` 构造时：

```
operator new → QCopChannel("AM/RegistryiMgr/Channel", this, 0)
             → QObject::connect(m_pChannel, SIGNAL(received(const QCString&, const QByteArray&)),
                                this, SLOT(slotChannelMessage(const QCString&, const QByteArray&)))
```

我们只建了缓存对象，**通道和 connect 都没有** → `slotChannelMessage` 永远不会被调用
→ 别的进程改了应用锁，本进程不刷新。与 v62 修的 `PHONE_MsgClient` 缺 connect 同一类缺陷。

### BUG-3　`AM_RegistryManager::setAppLockValue` 缺空 UUID 早退 + 缺跨进程通知

原库四步（`0x41af67b8`，1256B）：

```
1) if (uuid.isNull()) return;                                  ← 0x41af67d8
2) ZConfig(getConfigFileName(6)).writeEntry("AppLockValue", uuid, value)
3) cfg.flush()
4) QCopChannel::send("AM/RegistryiMgr/Channel",
                     "AppLockValueChanged()")                  ← 0x41af6a54
```

我们缺 1) 和 4)。第 4 步是跨进程通知：am / 我的收藏 / 设置各自持有一个
`AM_RegistryManager`，靠这条消息才知道要重读锁值。

> **反向教训（差点引入新 BUG）**：我一开始"顺手"给 `slotReloadAppLockValue`
> 也补了一句相同广播。用 `calldiff slotReloadAppLockValue` 核对原库 24 个调用后
> 发现原库**不**在那里广播（它用的是 `getGroupEntriesKeyList` 遍历组内键）。
> 补了会形成「收到通知 → 重载 → 再通知」的跨进程回环。已撤销。
> **结论：补任何调用之前，先用 calldiff 确认原库有没有那一条。**

### BUG-4　每次查应用锁值都重载一遍注册表（性能）

```cpp
AM_RegistryManager tmpRegMgr;                       // ← 构造函数里跑 initRegistry()/loadMenuTree()
tmpRegMgr.getConfigFileName(RegFileType(6));        // ← 只为取一个路径字符串
```

`getConfigFileName` 在 v60 已经改成 **static**（与原库一致），此处是漏改的调用点。
`AM_RegistryManager` 的构造函数会完整加载菜单树（60+ 个临时 QList、可能建 QCop 通道），
而 `getAppLockValue` 是**按图标查询**的。已改为静态调用，两处（`getAppLockValue` / `getAppName`）。

---

## 3. 剩余差异：按「是否真的会被调用」定级

设备的两个真实调用方实际用到的接口（`nm -D -u`）：

**am 主程序（12 个）**
```
getAppObject / initRegistry / initRegistryFiles / getAllAppList / getAppObjById
getRootFolder / getConfigFileName / slotAddAppToFolder / slotAppUnInstalled
staticMetaObject / className / ctor,dtor
```

**ezxaid.so 安装器（5 个）**
```
changeFileAttr / getConfigFileName / getSystemFolderName / removeAppFromMainMenu / ctor
```

对照审计标记出的 16 项「语义性缺失调用」，**没有任何一项出现在上面两份清单里**。
即：目前都不在实际执行路径上。逐项列在这里，供后续按需补齐：

| 函数 | 体量 ours/orig | 差异 | 风险 |
|---|---|---|---|
| `modifyApp` | 128B / 2168B | 只改内存，**不写配置、不 flush、不发 AppNameChanged** | 无调用方；若将来设置界面调用会"改了不生效" |
| `addAppToRegistry` | 852B / 16748B | 缺 `createUuid`、`QDataStream`+`QCopChannel::send` | 无调用方 |
| `createFolder` | 864B / 5956B | 缺 `createUuid`、`writeEntry` | 无调用方 |
| `handleCardPlugOut` | — | 缺 `writeEntry`×2、`flush` | 由热插拔触发，需真插卡才能测 |
| `handleCardPlugIn` | 268B / 6844B | 走 `createObject` 复用，缺 `folderChanged`×2、`addAppToMainMenu` | 需真插卡才能测 |
| `deleteFolder` | — | 缺 `readListEntry`、`writeEntry(列表版)` | 无调用方 |
| `Idle_Setup::doAutoSelection` / `setS3Info` / `setEnableS3Feature` / `setS3KillStatus` / `setDisableEnableStatus` | — | 原库走 `SETUP_SET_Manager::setSettingInfo`，我们直接 `ZConfig::writeEntry` | 机制不同，待屏保程序验证 |

**为什么没有就地全改**：这几项要忠实复原都得先解出原库的组名/键名/类型枚举分支
（`modifyApp` 就对 Type 0/1/2/6 直接返回），猜错的代价正好是 BUG-1 那种
「自测通过、与原厂不兼容」。没有调用方的功能上做这种改动，收益为负。

---

## 4. 已验证为「等价，不是缺功能」的项（避免后续误判）

| 现象 | 真相 |
|---|---|
| `createObject` 28B vs 9044B | 3 参版是转发包装，实活在本地 4 参重载里（导出符号仍做完整工作） |
| `slotAddAppToFolder` 4B | `b doAddAppToFolder` 尾跳转 |
| 25 个 `C4`/`D4` 只有 4B | 别名 thunk，原库方向相反（原库 C4 是真身） |
| `getName` 752B vs 14020B | 原库自带额外缓存层，我们是等价实现 |
| `.o` 里 `tr` 重定位只有 2 个（源码 6 处） | GCC 交叉跳转把相同序列合并，行为正确 |

---

## 5. 结论

- **导出面**：410/410，缺 0 多 0。
- **全量比对**：373/373 函数逐个过指纹，无未解释的调用缺失落在实际执行路径上。
- **本轮修掉 4 个真 BUG**（组名不兼容、缺通道+connect、缺通知、按图标重载注册表）。
- **剩余 7 类差异**全部位于当前无调用方的接口上，已记录并说明为何暂不全改。
- 设备验证：`BOOT v63 pid=6275`，无 CRASH/SIGSEGV，idle 8s `du=0 ds=0`。
