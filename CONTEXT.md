# CONTEXT.md — 一页纸交接（v77，2026-11-18）

> **先看这一页**。`STATUS.md` 是逐版本流水，`REVERSE_PLAYBOOK.md` 是可复用方法论。
> 需要刨细节再去翻那两份；日常开工只读本文件。

---

## 1. 这是什么

Motorola ROKR E6（PXA27x / ARMv5TE / Linux 2.4.20 / MontaVista）的
`libezxam.so.1.0.0`（EZX 应用管理库，Qt/Embedded 2.3.8）**源码级重建**。
目标不是"能跑"，而是**能原位替换原厂库**：导出面一致、行为一致、ABI 一致。

## 2. 当前状态

| 项 | 值 |
|---|---|
| 版本 | `v77`（交付版；调试日志编译期消除，`make EZXAM_DEBUG=1` 恢复全量日志） |
| 产物 | `libezxam_reconstructed.so.1.0.0`，352550 字节，md5 `afa076c07b8d5394d5b6a631818c0654` |
| 可复现 | 纯净化后 `make rebuild` md5 **逐字节相同**（2026-11-18 已验） |
| 门禁 | `verify_exports.py` PASS（410/410，缺 0 多 0）｜`chk_v63.py` PASS |
| 真机 | `BOOT v77`，FAULT=0；**卡插拔实测通过**（v76 起）；日志仅剩 BOOT/崩溃现场/互斥修复三类 |
| 备份 | `backup/v76_hotplug_ok_20261118/`（实测产物）+ `v77_sources_20261118.zip`（源码+工具快照） |
| 设备 | 192.168.16.2:23（telnet，root 无密码）；PC 侧 192.168.16.1 |

## 3. 部署与回滚（绝不用 `cat >` 写固件路径）

```bash
python deploy_lib.py                       # 部署本库：/ram/amlib → mount -o bind → kill -9 am
python deploy_lib.py orig_local.so         # 回滚原厂
python sh2.py "cmd1" "cmd2"                # 设备执行（sh2.py v2 会做 IAC 协商 + 哨兵串）
python pushh.py <本地> <设备路径>          # 传文件走 HTTP（记得 MSYS_NO_PATHCONV=1）
```
> 重启后 ramfs 与 bind 都没了 → 自动回原厂库，这是天然安全网。

## 4. 目录地图

| 路径 | 是什么 | 要不要看 |
|---|---|---|
| `src/` `include/ezx/` | 重建的实现与头文件（23 个 TU） | **主战场** |
| `Makefile` `exports.map` `fixed_base.lds` `stubs/` | 构建：固定基址 prelink + 导出白名单 + 平台桩库 | 改构建才看 |
| `orig_local.so` `_origlib/libezxam.so.1` | 原厂库（`_origlib` 是工具默认输入，别删） | 反汇编输入 |
| `v23` `appbase_local.so` `ezxaid.so` `libc_dev.so` `qte_local.so` | 从设备拉的原始二进制（比对用） | 按需 |
| `ghidra/decompiled/perfunc/` | Ghidra 每函数一个 .c（1414 个） | 快速读逻辑从这里起 |
| `docs/` `AUDIT_V63.md` | 专题文档（I18N / REFRESH / 崩溃 …） | 按需 |
| `backup/vNN_ok_YYYYMMDD/` | 版本备份（README + .so + 源码 zip） | 回滚 |
| `_backup/ezxam_reverse_full_20261118_v65.zip` | 全量快照（12.4 MB） | 灾难恢复 |
| `_trash_20261118/` | 本轮清理出的一次性产物（可整目录删） | 可忽略 |

## 5. 关键事实（改代码前必须知道的）

**类布局**（`AM_AppObject` sizeof=0x50）

```
0x00 vtable   0x04 父文件夹*  0x08 Type  0x0C Device  0x10/14/18 Big/Small/AniIcon(QString)
0x1C FUUID(QString)  0x20 FAppID(QUuid,16B)  0x30 FixedPosition|FixedAllPosition
0x34 Dir  0x38 Name  0x3C Vender  0x40/41/42 bool  0x44 int  0x48/4C QString
0x50 起是派生区（AppLnk 的 m_pad50 …）；派生 sizeof：Folder 0x58 / Shortcut 0x5C / EmbeddedLink 0x58 / AppLnk 0x84
```

**配置文件映射**（`getConfigFileName`）

```
0 SysRegistry(preload) 1 UserRegistry(appWrite) 2 CardRegistry(card) 3 SysMenuTree(preload)
4 UserMenuTree 5 InstalledDB 6 AppLockConfig 7 UserConfig 8 SysMime 9 UserMime 10 PlugCardDB
```

**QCop**：通道 `AM/RegistryiMgr/Channel`、`AM_MainMenuChannel`
消息名＝**完整槽签名**，如 `AppLockValueChanged()`、`InstallApp(int,QString,QString,int)`、
`UninstallApp(QString)`、`AddAppToFolder(QString,QString,int)`、`AppNameChanged(QString)`

**常量**：ezxUid 2000 / ezxGid 233 / DRMUid 518；根文件夹 UUID `cb839c8f-72c3-42ac-bf17-b01080cb5705`

## 6. 已经修掉的真 BUG（别再改回去）

| 版本 | 问题 | 教训 |
|---|---|---|
| v50 | 固件 `AM_Mutex` 自己每台机只允许成功一次 → 装第二个软件必报「系统忙」 | — |
| v51 | 导出面多出 109 个符号，会在别的进程里抢占同名 UND | 多条构建路径参数必须一致 |
| v58 | 界面卡死 = 两个 `msgctrloadslaveapp` 缺页死循环 | — |
| v60 | `getConfigFileName` 等三个函数原库是 **static**，我们写成普通成员 → r0 错位一格闪退 | mangled 名不编码 static，要看有没有 `str rX,[r0,#…]` |
| v62 | `AM_Carrier_Info` 少 5 条 `connect`（状态栏不跟随）；`PHONE_MsgClient` 少 4 字节 | 空壳类 sizeof 必须与真身一致 + 编译期断言 |
| v63 | 应用锁组名写成 `""`（原库 `"AppLockValue"`）；`AM_AppLockManager` 缺 QCop 通道+connect；`setAppLockValue` 缺空 UUID 早退与跨进程广播 | 补调用前先用 `calldiff` 确认原库有没有那一条 |
| v64 | 应用名查表键用组名，实际是 **AppID**（18 个组不一样）→ 一批英文 | — |
| v65 | `modifyApp` 只改内存不落盘（128B→2072B）；第 3/4 参数语义错 | `writeEntry(g,k,"NO")` 会命中 bool 重载 |
| v71-v77 | **H1～H8 热插拔与应用锁链**：connect 缺失、频道过滤缺、插卡不恢复菜单、拔卡键用错（getName≠UUID）、拔卡 UAF（异步通知+delete 时序）、initRegistry 泄漏、应用锁消息名错——全部修复并真机实测（详见 STATUS.md v72-v77 节、PLAYBOOK §11） | 静态审计触达 ≠ 运行时可达；事件驱动路径必须单独复查原库反编译 |

## 7. 还剩什么（不阻塞替换，遇报障再查）

| # | 事项 | 依据 |
|---|---|---|
| 1 | Java 改分类按钮灰：链路日志健康，使能逻辑在闭源 ezxaid/am 侧；若复现需拿具体界面与应用对象 | 用户实测（v77 环境） |
| 2 | 应用锁跨进程刷新：无法在本环境构造第二进程场景；消息名已在平台库实证（H3） | 无阻塞 |

> 判别顺序：① 有没有真实调用方（`nm -D -u` 看 am / ezxaid）→ ② 有调用方才逐条对调用序列
> → ③ 没有调用方的**先记档不要动**。BUG-1 那种"自测通过、与原厂不兼容"就是这么来的。

## 8. 工具速查（只列真正常用的）

| 目的 | 命令 |
|---|---|
| 两级取址反汇编（带参数追踪，**首选**） | `python dis4.py 0x41afd844 2168` |
| 直接 GOT 形式反汇编 | `python dis2.py <addr\|名> [len]` |
| 调用序列对比 | `python calldiff.py <函数名子串>` |
| 字面池常量 | `python lit.py 0x41afd844 2168` |
| `bl`/GOT → 符号/字符串 | `python pltdbg.py lib.so bl 0x...` / `... got <sl> <off>` |
| 全量函数指纹比对 | `python audit_all.py` |
| 导出面校验 / 21 项门禁 | `python verify_exports.py ours.so orig.so` / `python chk_v63.py` |
| 编译探针（设备侧 A/B） | `bash mk_t.sh t_reg` |
| 设备执行 / 传文件 | `python sh2.py "..."` / `python pushh.py <本地> <设备路径>` |

## 9. 坑位速查（全量 16 条见 PLAYBOOK §8）

1. `sl = add 指令地址 + 8 + 字面量`，写成 +4 会让整张 GOT 错位一格（signal/slot 对调）。
2. `tr()` 用**类自己的**，用 `QObject::tr` 会静默回落英文。
3. `.qm` 的 hash 输入是 **`comment + sourceText`**（英文在前），且 Hash 字段是**裸 4 字节**。
4. 空壳类必须补到真身尺寸 + 静态断言（`PHONE_MsgClient` 40）。
5. 绝不 `cat > /usr/lib/ezx/lib/...`（先 O_TRUNC 再写，cramfs 上直接 0 字节整机重启）。
6. telnet 必须回应 IAC，否则设备只回 `\xff\xfd\x18…` 就没下文（看着像死机）。
7. telnet shell 里 `new QApplication` 必崩（`QObject::queryMetaObject()+12`）——Qt/E 应用只能由平台 launcher 起。
8. 返回类型**不进 mangled 名**，`getBigIcon()` 就是靠函数体才判出是 QString。

## 10. 开工三步

```bash
cd D:/WorkBuddy/2026-11-08-18-54-48/ezxam_reverse
make && make verify                     # 24s，产物 352919B，md5 应等于 §2
python sh2.py "pidof am"                # 设备在线？
```
