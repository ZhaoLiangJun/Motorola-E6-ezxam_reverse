# 启动期 SIGSEGV 分析（2026-11-17）

> **状态：未定责，间歇性，自愈。**
> 现象是 `am` 启动时偶发 SIGSEGV（崩在 glibc `malloc` 内部），
> `windowsserver` 立刻拉起新实例，用户感知只是"开机慢了约 7 秒"。
> 实测频率约 **1/7 次启动**，崩一次后就稳定。
> 已加装现场抓取（v43 build `d5ecf9c1`），下次崩溃会自带调用记录。
>
> ⚠️ **本文件初版曾错误地断言"只在 `/ram/tmp/am_debug` 存在时出现"。**
> 那个结论来自两次"开日志连崩 2 次 / 关日志 1 次没崩"的对照，
> 后来关日志也崩了一次，证明是**间歇性**而非日志相关。教训：低频随机故障
> 不能用 n=1 的对照下结论。

## 1. 现象

`am` 启动后约 7 秒，进程 SIGSEGV 死掉，`windowsserver` 立刻拉起。
崩溃次数不固定：有时连崩 2 次（pid 663→667→671），有时 1 次（1374→1378），
有时 0 次。

日志（`/ram/tmp/amreg.log`）里的样子：

```
===== BOOT v43 pid=1374 Jan  1 14:43:39 =====
... 约 220 行功能日志 ...
==== CRASH sig=0x0000000b addr=0x001a40ac code=0x00000001 ====
r0=0x00000040 r1=0x000ccd98 r2=0x000befd0 r3=0x0019c0f0 r4=0x000d72d1 r5=0x001a40a8 r6=0x411863d4 r7=0x41186378
r8=0x00000000 r9=0x411863bc r10=0x41184694 fp=0x411863c0 ip=0x000ccdd8 sp=0xbeffefb8 lr=0x000d72d0 pc=0x410d0798
cpsr=0x80000010
---- stack ----  (1536 字节原始栈)
CRASH_END
===== BOOT v43 pid=1378 Jan  1 14:43:46 =====   ← 这次活了
```

`code=0x00000001` 是 `SEGV_MAPERR`（地址未映射），不是权限错。

两次崩溃的**不变量**（很强，说明是同一处）：

| 值 | 第一次 | 第二次 |
|---|---|---|
| `pc` | `0x410d0798` | `0x410d0798` |
| `r0` | `0x40` | `0x40` |
| `ip - r1` | `0x40` | `0x40` |
| `r4` | `lr + 1` | `lr + 1` |
| 故障地址 | `r5 + 4` | `r5 + 4` |

## 2. 现场解码

先把 `pc` 落到具体库。`/proc/<pid>/maps` 里有（预链接固定基址）：

```
00008000-00087000 r-xp  /usr/SYSqtapp/am/am
000ae000-001f4000 rwxp  (anonymous)          ← am 的堆
41058000-4117a000 r-xp  /lib/libc-2.3.2.so
413b0000-41749000 r-xp  /usr/lib/ezx/lib/libqte-mt.so.2.3.8
41ad8000-...      r-xp  /usr/lib/ezx/lib/libezxam.so.1.0.0
```

- `pc=0x410d0798` → 落在 **libc** 里。
- 用设备上的 `libc_dev.so`（就是这份预链接 libc，基址一致）查符号表：
  在它之前最近的导出符号是 `mallopt @0x410cffe4`，之后是 `posix_memalign @0x410d1524`。
  → `0x410d0798` 落在 **malloc 内部函数区**（`_int_malloc` / `_int_free` /
  `sysmalloc` / `malloc_consolidate` 这一族，都是 static，符号表里没有）。
  这与 `r0=0x40`（64 字节请求）、`r1/ip` 相差正好 0x40 一起，看起来是
  一次分配/合并过程中的元数据访问。
- `addr=0x001a9f74` = `r5+4`（`r5=0x001a9f70`），是**堆内地址**但当时未被映射。
  说明访问的指针**超出了当时 brk 的上界**。
- `lr=0x000cdfa0`、`r4=0x000cdfa1` 都在 `0x000ae000-0x001f4000` 这个 **rwx 匿名区**
  里 —— 那是数据不是代码。**返回地址本身是垃圾**，说明寄存器现场已被污染，
  不能靠 `lr` 回溯调用者。

### 实际崩溃指令（拿本地 `libc_dev.so` 反汇编确认）

`libc_dev.so` 就是设备上那份预链接 libc（基址同为 `0x41058000`），可以直接对上：

```
0x410d0790  streq  r3, [r1, #4]
0x410d0794  beq    #0x410d07dc
0x410d0798  ldr    r3, [r5, #4]      <<< CRASH PC
0x410d079c  bic    r2, r4, #1
0x410d07a0  tst    r3, #1
0x410d07a4  ldreq  r3, [ip, #0xc]
0x410d07a8  ldreq  r2, [ip, #8]
0x410d07ac  strne  r2, [ip, #4]
```

`ldr r3,[r5,#4]` 之后紧跟 `ldreq ip+0xc` / `ldreq ip+8` / `streq` 回写，
这是 glibc `_int_malloc` 里**双向链表解链**（unlink）的标准形状：
`r5` 是 bin 里的 `victim`，`[r5,#4]` 读的是 `victim->size`。

**结论：某个 bin 的空闲链表里出现了伪造的 chunk 指针 → 堆元数据被写坏。**

故障地址每次都"刚过当时的 brk"（`0x001a40ac` / `0x001a9f74`，而事后 heap 已长到
`0x001f4000`），说明那个伪造指针指到了堆顶之外。

## 3. 频率实测（v43，`kill am` 后观察 20 秒）

| 次 | 结果 |
|---|---|
| 1 | 干净 |
| 2 | 干净 |
| 3 | 干净 |
| 4 | 干净 |
| 5 | **崩 1 次**（1374 → 1378） |
| 6 | 干净 |
| 7 | 干净 |

约 **1/7**。之前有一次连崩 2 次（663→667→671），所以措辞是"偶发、次数不定"。

## 3b. 原厂库对照（**未完成**）

只有 1 轮有效数据：原厂库 `kill am` 后新实例 `989/992` 稳定 75 秒，无重启。
但既然我们库的崩溃率是 1/7，**1 轮干净不能证明原厂库不会崩**，
需要 5~10 轮才能定责。

之所以没继续：**连续多次 `kill -9 am` 会让整机掉网**（观察到的两次都掉了
10~30 分钟，`ping` 100% 丢包）。这台机器 46MB 内存 + jffs2，反复重启 am 会
把系统拖进一段不可用期。为了查一个"每次只多花 7 秒"的问题去反复折腾设备，
风险收益不划算，所以主动停在这里。要做时建议：每轮之间歇 30 秒以上，
且不要在用户正在用手机时做。

## 3c. 崩溃点固定，但"崩不崩"是随机的

崩塌点每次都一样（同一 `pc`、同一 `r0`、同一指令），说明**逻辑位置固定**；
是否真的触发取决于当时的堆布局（伪造指针恰好落在未映射页才炸）。
所以：

- 连崩 2 次的那两轮（`663→667→671`、`837→841→845`）功能日志都是 596 行，
  崩前最后几行完全一致 —— 位置确定；
- 但也有若干轮完全干净 —— 同一位置、同样的代码，只是堆布局没踩上。

### 崩溃前最后几行功能日志

```
[getAppObject] HIT '2a58c0d6-05c6-45aa-8938-63059d41a4e9' -> 0xb92c8 type=1 name='AM_ROOT_FOLDER'
[Folder::getAppList] folder=0xb92c8 uuid='2a58c0d6-…' name='AM_ROOT_FOLDER' 原有=7
[Folder::getAppList] folder=0xea910 uuid='cb839c8f-…' name='All' 原有=34
[getAppObject] HIT '2a58c0d6-…' -> 0xb92c8 type=1 name='AM_ROOT_FOLDER'
[Folder::getAppList] folder=0xb92c8 … 原有=7
← 这里死
```

也就是"根文件夹应用列表刚取完、准备继续建菜单"的位置。
崩在这个点之后，真正的写坏堆的操作可能**更早就发生了**（延迟暴露），
所以不能只盯着这几行。

## 4. 已经排掉的几个假设

| 假设 | 结论 |
|---|---|
| `QList<AM_AppObject>` 析构会 `delete` 元素 → 二次释放 | **不成立**。Qt2 `QCollection(){ del_item = FALSE; }`，`QList::deleteItem` 是 `if (del_item) delete (type*)d;`，默认不删。见 `qt-2.3.8/include/qt/qcollection.h:63`。 |
| `amLogWrite` 用固定长度缓冲区 → 栈溢出 | **不成立**。它直接 `vfprintf(fp, fmt, ap)`，无中间缓冲。 |
| `ring_put` 环形缓冲越界 | **不成立**。`&g_ring[g_ring_pos % RING_SIZE]`，取模保护的。 |
| `[i18n]` 那条日志格式串与实参不匹配 | **不成立**。9 个 `%s` 对 9 个实参，逐个数过。 |
| 是 v43 应用名中文表引入的 | **不太可能**。同样的崩溃在应用名表之前（v42 标签、同一产物）就出现过。 |
| 是 `QString::sprintf` 拼缓存键 | **不太可能**。`ck.sprintf("%s|%s|%d", ...)` 内容只有路径，且崩溃在 malloc 元数据上。 |

## 5. 已修掉的两处真实隐患（未必是元凶）

### 5.1 `getAppObjById` 的 use-after-free

```cpp
const char* sc = uuid.toString().latin1();   /* 悬垂：临时 QString 语句结束即析构 */
```

随后 16 次 `sc[i]` 读的是**已释放的堆内存**。v43 改为先落具名 `QString us`。
这是确定的错误，但崩在 malloc 内部而非读该缓冲区的现场，**是否能解释本崩溃未证实**。

### 5.2 `handleCardPlugIn` 重复创建卡对象

`loadMenuTree` 末尾调 `handleCardPlugIn(4)`，而 `loadMenuTree` 之前已经把卡应用
链进了 `Folder::m_appList`；旧实现又全量 `createObject(CardRegistry)`，
于是 3 个卡对象**被新建一份覆盖进 `m_pAppMap`**：

- 每次启动泄漏 3 个对象；
- Folder 里挂旧对象、`m_pAppMap` 里是新对象 —— 指针分裂，
  会让 `handleCardPlugOut` 摘对象后菜单里图标还在。

日志证据：`[createObject] type=4 … created=3 map=79`（map 没涨 → 全是覆盖）。
v43 加了 `onlyNew` 参数，现在是 `created=0 map=79`（空操作），与原库
（先取两表组名做差集 + `getAppObject` 判存在性）的**可观测行为**一致。

### 5.3 给崩溃处理器加了调用记录回放

原来是崩了只知道"崩在 malloc"。现在 `crash_handler` 会把 `g_ring`（每个库函数
入口都记一条 `tag/a/b`）最后 64 条一起落盘：

```
---- ring(last 64) ----
1234: tag=0x16 a=0xb89a0 b=0x0
...
CRASH_END
```

tag 含义见 `src/ringlog.h` 顶部标签表。下次自然崩溃就能直接看出崩前跑了哪些函数。

## 6. 下一步

1. **等一次自然崩溃**，看 `---- ring(last N) ----` 段。这是成本最低、信息量最大的
   一步（不用再反复杀 am）。
2. 若 ring 尾部指向某个具体函数，就在该函数里查越界写（重点：往
   `QMap`/`QString` 里写入超过长度的内容、或把栈上对象地址存进堆结构）。
3. 想定责 A/B 的话：`python trials.py orig 6 20`（已写好）。**但要接受掉网风险**，
   且每轮之间歇 30 秒以上。
4. 顺手可做：`[i18n]` 那段 6 组对照日志是当初定 context 的实验代码，
   收进 `#ifdef` 或删掉，别长期留在热路径。

## 7. 使用上的建议

- 这个崩溃**不影响使用**（自愈，只多花约 7 秒开机），不必因此不用 v43。
- `/ram/tmp/am_debug` 是排查开关，日常不要留（热路径日志会显著加重卡顿）。
  `rm /ram/tmp/am_debug` 下次启动生效（`amLogVerbose()` 是一次性缓存）。
- 别在正常使用中反复 `kill -9 am`：这台机器会掉网一段时间。

## 8. 2026-11-17 新增现场（安装自测引起，可复现 2/2）

装应用链路修好后（`docs/AUDIT.md` §4.4），为了在没有 qcop 命令行工具的情况下
验证 `slotAppInstalled`，临时在 `initRegistry` 末尾加了个开关钩子，对
**已经在 map 里**的 Java 应用重放一次安装。结果是：

```
2947| [Folder::getAppList] folder=0xea930 uuid='cb839c8f-…' name='All' 原有=36
==== CRASH sig=0x0000000b addr=0x74007810 code=0x00000001 ====
r0=0x000d93d0 r1=0x000d93d8 r2=0x74007808 r3=0x000d9458 r4=0x41186378 r5=0x000d9430
r6=0x00000028 r7=0x00000029 r8=0xbefff5c0 r9=0xbefff370 r10=0x41184694
fp=0x00005d88 ip=0x41b4b1c8 sp=0xbefff240 lr=0x00000088 pc=0x410d060c
```

- `pc=0x410d060c` 与本篇 §2 那个自然崩溃点（`0x410d0798`）**同处 libc 的
  `_int_malloc` 附近**，`addr=0x74007810` 是典型的"用字符串内容当地址"
  （0x74='t'），与 §2 的"堆元数据被写坏后解链读到垃圾"是同一类现象。
- **可复现性**：带钩子的那次构建启动 3 次，前两次崩、第三次活；把钩子删掉后
  重新部署，启动 1 次干净。所以这次**是钩子引入的**，不是原发故障。
- 机理推断：对已存在的应用重放安装会把新对象覆盖进 `m_pAppMap`，
  而旧对象仍留在 Folder 的 `m_appList` 里 → 列表同一 uuid 出现两项；
  同时 `folderChanged` 是在 `initRegistry`（UI 还没建完）期间发出的。
  真实安装是"新组 + 初始化之后"，不会进入这个状态。
- **教训**：这类自测只能在"新组"上做，且不要挑初始化期发信号。

日志在 `_amreg_v44t.log`（含 ring 前 64 条与三次启动的对照）。

