# ARM Linux 闭源共享库逆向重建 —— 经验与参考手册

> 来源项目：Motorola E6（PXA27x / ARMv5TE / Linux 2.4.20）`libezxam.so.1.0.0`
> 用源码重新编译出一个**行为等价、导出面等价、可原位替换**的库。
> 本文只记录**可复用的方法与判据**，具体版本流水见 `STATUS.md`。

---

## 0. 一句话原则

**不猜。每个结论都必须落到「地址 + 指令 + 解析出的数据」三件套上。**

这个项目里凡是靠"看起来应该对"下的结论，后面都返工了；凡是靠反汇编取证下的结论，
一次到位。下面所有技巧都是这一条的展开。

---

## 1. 「行为不一致」的三大根因模板

用户报「和原版不一样」时，先往这三个筐里放，命中率极高：

| 模板 | 表现 | 判据 | 本项目实例 |
|---|---|---|---|
| **A. 调用点不同** | 功能能跑，但取到的值/查到的表不对 | 同名函数、同参数，但**上下文/寄存器约定**不同 | `QObject::tr` vs `AM_Carrier_Info::tr` |
| **B. 数据表缺项** | 大部分正常，个别条目回落默认值 | 对照表少一行/多一行 | `g_folderNames[]` 漏 `User2` |
| **C. 状态不刷新** | 初值对，操作后不跟着变 | 构造函数里的 `connect` 数量 | 5 条 `connect` 全是注释 |
| **D. 尺寸/布局不符** | 偶发崩溃、越晚越难查 | `new` 的分配大小、成员偏移 | `PHONE_MsgClient` 少 4 字节 |
| **E. 原型不兼容** | 崩溃或返回垃圾 | 符号体量差一个数量级 | `getAppParentFolderUid` static vs 非 static |

模板 C 和 D 最容易被漏掉：C 不崩溃只是"不更新"，D 可能跑几小时才崩。

---

## 2. 五个反复奏效的取证动作

拿到可疑函数地址后，按顺序做这五件事，基本能把函数"读穿"。

### 2.1 反汇编 + 解析每个 `bl` 目标（PLT 解析）

```bash
python edis.py orig_local.so 0x41b0e938        # 反汇编
python pltdbg.py orig_local.so bl 0x41ae8c64   # bl 目标 → 符号名
python pltdbg.py orig_local.so plt              # 全部 PLT 桩
```

原理：`.rel.plt` 第 i 项 ↔ `.plt` 第 i 个桩，`addr = plt_base + 20 + i*12`
（ARM PLT 头 20 字节，每桩 12 字节）。

**坑**：不要用 `(off-0x10000)//4` 之类推索引，会解出荒谬地址；
也不要用 capstone 线性反汇编去扫调用点——遇到非法指令会静默截断，
漏掉的正是你要找的那些。**扫调用点要用指令编码暴力匹配**：

```python
if (w >> 24) & 0xff == 0xeb:      # BL
    ...
```

### 2.2 解析每个 `[sl, off]`（GOT 槽）→ 字符串

ARM PIC 代码取外部数据都走 `sl`（GOT 基址）。要还原"这个参数到底是什么字符串"，
必须做**二级间接**：先读 `[sl+off]` 得到指针，再读指针指向的内容。

```bash
python pltdbg.py orig_local.so got 0x41b229f0 0xeb4 0xeb8 0xebc
# sl+0xeb4 = 41b238a4 -> 41b193b8  "1slotUpdateInfoString()"
# sl+0xeb8 = 41b238a8 -> 41b193d0  "2signalServiceName(QString)"
```

`sl` 的求法：函数开头 `ldr rX,[pc,#imm]` 取字面量，紧跟 `add sl, pc, rX`，
`sl = 该 add 指令地址 + 8 + 字面量`。

> ⚠ 这里的 `+8` 是 ARM 的 pc 偏移（当前指令 + 8），**不要用"下一条指令的地址"**
> （那是 +4，也就是当前指令 +4，不是 +8 的等价写法）。差 4 字节的后果不是报错，
> 而是**整张 GOT 表错位一格**：`signalServiceName` 被读成槽、`slotUpdateInfoString`
> 被读成信号，于是"原库的 connect 参数居然是反的/不合法的"这种荒谬结论就会出来。
> 症状：解出来的 member 以 `2` 开头（`2` 是 SIGNAL 的编码，member 只应是 `1`）。
> 见到这个症状，先复查 sl 的 +8，别怀疑原库写错。

**这一步是整个项目性价比最高的动作。** `connect` 的信号槽字符串、`tr` 的
sourceText/comment、文件名、QCop 消息名，全靠它一句话定死。

> 但本库里更常见的写法是**两级取址**：
> ```
> ldr r3,[pc,#0x594]     ; 字面池里放的是 GOT 偏移 0x0d54，不是指针
> ldr r1,[sl,r3]         ; 真正的字符串在这里
> ```
> 只看 `[sl,#imm]` 的直接形式会漏掉绝大多数参数。用 `dis4.py`：它跟踪 rX 的已知
> 常量，再把 `[sl,rX]` 解开，并把 `bl` 前 r0..r3 的已知字符串直接打出来——
> `writeEntry(组名, 键名, 值)` 这类三参数调用就是靠它一次看全的。

### 2.3 解析字面池 → 常量

`ldr r0,[pc,#imm]` 后面跟的那串 `.word` 就是常量池。批量解出来能看到
`mov r0,#40`（分配大小）、`cmp r0,#1`（判断值）、跳转表等。

```bash
python lit.py <函数名子串>        # 按地址顺序列出该函数的 GOT 字面量
python gotslot.py 0x1138 0xd80    # 查具体槽内容
```

### 2.4 数调用序列，与原库对比（calldiff）

只比"函数体大小"不够，要**比调用序列**：同一个函数里，原库调了哪些符号、
按什么顺序调。少了 `connect`、少了 `emit`、多了 `fopen`，一眼就能看出来。

```bash
python calldiff.py slotChannelMessage
python tr_audit.py          # 专门比「哪个函数调用哪个 tr」
```

### 2.5 比符号体量（sizecmp）→ 抓原型不兼容

```
     2.7%  orig= 1164B ours=  32B  _ZNK18AM_RegistryManager28getAppAutoStartStatusFromDmtERK7QString
```

出现 32B vs 1164B 这种数量级差，几乎一定是**原型不兼容**（少参数、
static 与非 static 混用、返回值类型错），而不是"实现得精简"。

> **注意反向误读**：体量小不等于有 bug。`getName` 原库 14020B / 我们 752B，
> 那是原库自己有额外缓存层，我们是等价实现。**体量差要结合调用序列判断**，
> 不能只凭比值报警。

---

## 3. C++ 层的五个致命细节

### 3.1 名字修饰**不编码 `static`**

这是本项目上一轮闪退的根因，也是最隐蔽的一个。

```cpp
static QString getSystemFolderName(...);   // r0 = 第1参
       QString getSystemFolderName(...);   // r0 = this,  r1 = 第1参
```

两者**导出符号名完全相同**，`nm` 看不出来，链接不报错，运行起来寄存器错位一格
→ 拿 `this` 当参数 → 解引用垃圾指针 → 闪退。

**判据**：反汇编看函数体第一条指令有没有 `str rX,[r0,#...]` 写成员。
写成员 = 有 `this` = 非 static。实战里"同一份头文件里有的函数带成员访问、
有的不带"，就是混用了。

### 3.2 `tr()` 的上下文是**类名**，写错静默失效

`Q_OBJECT` 宏给每个类生成一份**静态** `tr()`，context = 类名；
`QObject::tr` 的 context 是 `"QObject"`。

```cpp
tr("TXT_RID_...", "Antenna Off")        // context = AM_Carrier_Info  ✔
QObject::tr("TXT_RID_...", "Antenna Off") // context = QObject        ✘ 查不到
```

写错不报错、不崩溃，只是查不到译文 → 平台回落到 comment 位上的英文。
现象就是"中文变英文"，非常像"翻译没做"，其实翻译一直在，是**键错了**。

**定位工具**：`tr_audit.py`（比两个库里每个函数调用哪个 `tr`）、
`qm_lookup.py`（按 hash 反查 .qm 验证译文确实存在）。

### 3.3 Qt 2.3 `.qm` 格式与 hash 顺序

文件结构：16 字节 magic → 若干 section（`tag:u8 + len:u32BE + data`）。
section tag：`0x42`=Contexts、`0x69`=Messages、`0x2f`=Hashes。

Message 内的 tag：

| tag | 含义 | 编码 |
|---|---|---|
| 1 | End | — |
| 2 | SourceText16 | len + UTF-16BE |
| 3 | Translation | len + UTF-16BE |
| 4 | Context16 | len + UTF-16BE |
| **5** | **Hash** | **裸 4 字节，无长度前缀** |
| 6 | SourceText | len + latin1 |
| 7 | Context | len + latin1 |
| 8 | Comment | len + latin1 |

> **坑**：`Tag_Hash` 是**裸 4 字节**。按"所有字段都有 u32 长度前缀"去解，
> 158 条里 150 条会错位，还以为是数据损坏。

hash 算法（`elfHash`，取自 `libqte-mt.so.2`）：

```python
def elf_hash(s):
    if not s: return 1
    h = 0
    for ch in s.encode('latin1'):
        h = ((h << 4) + ch) & 0xffffffff
        g = h & 0xf0000000
        if g: h ^= g >> 24
        h &= ~g & 0xffffffff
    return h or 1
```

> **坑（最贵的一个）**：hash 输入是 **`comment + sourceText`，英文在前、ID 在后**。
> 写成 `sourceText + comment` 会全部查不到，而且不会报错——只会"译文不存在"。
> 实测：`"Antenna Off" + "TXT_RID_CONTENT_SLINE_ANTENNA_OFF"` → `0ac84ea6` → **电话功能关**。

### 3.4 交叉跳转（cross-jumping）不是 bug

GCC 会把多个 case 里**完全相同的指令序列**合并成一块，各分支只负责把不同的
指针/常量装进寄存器再跳进去。

现象：源码 6 个 `tr()` 调用，目标文件里只有 2 条 `tr` 重定位。

**不要急着当成编译错误。** 判定方法：看跳转表（`addls pc,pc,rX,lsl#2`）
的每个目标块，是不是最后都 `b` 到同一个地址；是，就是正常优化。

### 3.5 手写外部类空壳，`sizeof` 必须与真身一致

只为 `new` 它 / 只为调用它的构造函数而写的前置声明式空壳，
`sizeof` 由**我们自己**的声明决定，而真实构造函数由**别的库**提供：

```cpp
class PHONE_MsgClient : public QObject {          // 空壳 sizeof = 36
public:  PHONE_MsgClient(QObject*, const char*);
};
new PHONE_MsgClient(this, 0);                      // 真身要 40 字节 → 越界 4 字节
```

**防御**：补齐填充到真实尺寸，并加**编译期断言**，让下次退步直接编不过：

```cpp
class PHONE_MsgClient : public QObject {
public:  PHONE_MsgClient(QObject*, const char*);
private: char m_alignToRealSize[4];                // 36 + 4 = 40
};
typedef char PHONE_MsgClient_size_must_be_40[
    sizeof(PHONE_MsgClient) == 40 ? 1 : -1 ];
```

真实尺寸从原库取：`mov r0,#N` 紧跟 `bl operator new`。
成品再反汇编自查一遍：`mov r0,#40` 才算过关。

---

## 4. 链接层：prelink、固定基址、导出白名单

### 4.1 原库是 prelink 过的

第一个 `LOAD` vaddr 是固定值（本项目 `0x41b28000`），`vtable` 槽里的
`R_ARM_ABS32` 目标已经是**绝对地址**。重建时要用固定基址链接脚本复现布局，
否则 vtable 里的地址全错。

```bash
-Wl,-T,fixed_base.lds
```

### 4.2 导出面必须用 version script 收敛

重新编译会导出源码里所有外部可见符号（Qt2 模板实例 `QString::~QString`、
`QMap<...>::operator=`、自己的调试函数……）。这些多余"提供者"会在别的进程里
**抢占同名 UND**，是最隐蔽的崩溃源。

白名单 = 原厂 `.dynsym` 里 `st_shndx != 0` 的全部条目，用**匿名版本节点**
（只改可见性，不引入 `@V1` 后缀，与原库无版本节一致）：

```
{ global: <410 个符号>; local: *; };
```

```bash
python gen_exports.py            # 从原厂库生成
python verify_exports.py ours.so orig.so
# 原库导出 410 / 本库导出 410   缺 0   多 0   → PASS
```

> **坑**：`Makefile` 里有 `--version-script`，但另写的 `link_win.sh` 里**没有**。
> 走脚本这条路时白名单静默失效，多出 109 个符号。**多条构建路径要保证参数一致**，
> 并用 `verify_exports.py` 作为门禁。

### 4.3 ARM9 没有硬件除法

`% 1024` 这种在 `RING_SIZE` 不是编译期常量幂时，会编译成 `__umodsi3` 软调用。
放在每帧每图标都走的热路径上（54 个调用点），就是肉眼可见的卡。

**判据**：`objdump -d | grep -cE "__umodsi3|__udivsi3|__divsi3"`，与原库同数量级才算过。

---

## 5. 设备侧：安全部署与现场取证

### 5.1 绝不直接覆写固件路径

`/` 是 **cramfs（只读）**。历史上"能 `cat x.so > 该路径` 写成功"是因为平台
在那边叠了一个可写的单文件挂载；重启后挂载没了，再写就是往 cramfs 写，
而且 `>` 会先 `O_TRUNC` → **目标被截成 0 字节** → am 起不来 → 平台反复 respawn 失败 → 整机重启。

**安全做法（天然安全网）**：

```bash
cp lib.so /ram/amlib/libezxam.so.1.0.0            # ramfs
mount -o bind /ram/amlib/libezxam.so.1.0.0 /usr/lib/ezx/lib/libezxam.so.1.0.0
kill -9 $(pidof am)                               # 平台重新拉起
# 重启后 ramfs 与 bind 都消失 → 自动回到原厂库
```

`deploy_lib.py` 固化了这套流程，并在每一步验证 md5，不符就中止、不重启 am。

### 5.2 传输走 HTTP，不要走终端回显

28 万字节走 telnet 回显要十分钟且容易截断；设备上有 `wget`，
PC 起一个 `http.server` 让设备来拉，一秒完成。`pushh.py` 固化了这条。

> **坑**：Git Bash 会把 `/mmc/mmca1/...` 改写成 Windows 路径。
> 所有带设备绝对路径的命令都要加 `MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL='*'`。

### 5.3 截图不可靠时，改反汇编取证

目标设备的帧缓冲不是朴素 RGB565，按 RGB565 解出来是重复噪点。
**不要在解码上死磕**——同一尺寸的历史截图也一样乱，说明是格式问题不是本次改动。

判据要换：改读 `/proc/<pid>/stat` 的 utime/stime、反汇编确认调用序列、
日志确认分支走到。

---

## 6. 标准作业流程（SOP）

```
① 复现并量化
   同一操作跑两次，记录 /proc/<pid>/stat 的 utime+stime；截图/日志留证。

② 定位到函数
   符号表（nm -D）→ 反汇编（edis.py）→ 5 个取证动作（2.1~2.5）

③ 与原库逐句对齐
   每个 bl 目标解析成符号名；每个 [sl+off] 解析成字符串；
   每个 case 的常量从字面池取。写进源码注释，标注原库地址。

④ 编译期防御
   尺寸加静态断言；上下文/签名加注释写明依据（原库地址）。

⑤ 产物自查（不看源码，只看二进制）
   反汇编成品：分配大小、connect 数量、tr 重定位、软除法计数
   verify_exports.py：缺 0 多 0

⑥ 部署并验收
   deploy_lib.py（每步验 md5）→ 确认 BOOT 版本号 → 无 CRASH →
   idle 8s du/ds 零增长 → 用户实测行为
```

**⑤ 是关键**：源码写对了但编出来不对，这种事故只有反汇编成品才能发现。

---

## 7. 工具速查表

| 目的 | 工具 | 典型用法 |
|---|---|---|
| 通用反汇编（带 PLT/字面池注释） | `edis.py` | `python edis.py lib.so 0x41b0e938` |
| 带 GOT 解析的反汇编 | `dis2.py` | `python dis2.py <addr\|函数名> [len]` |
| **带寄存器追踪**的反汇编（两级取址必用） | `dis4.py` | `python dis4.py 0x41afd844 2168` |
| PLT 桩 → 符号 | `pltdbg.py` / `eplt.py` / `pltmap.py` | `python pltdbg.py lib.so plt` |
| `bl` 目标 → 符号 | `pltdbg.py` | `... bl 0x41ae8c64` |
| GOT 槽 → 字符串 | `pltdbg.py` / `gotslot.py` | `... got 0x41b229f0 0xeb4` |
| 字面池 / 常量 | `lit.py` / `litpool.py` | `python lit.py <函数名>` |
| 调用序列对比 | `calldiff.py` | `python calldiff.py <函数名>` |
| 符号体量审计 | `sizecmp.py` | `python sizecmp.py` |
| `tr` 上下文审计 | `tr_audit.py` | `python tr_audit.py` |
| `.qm` 解析 / 反查 | `qm_dump.py` / `qm_lookup.py` | `python qm_lookup.py` |
| 导出白名单生成/校验 | `gen_exports.py` / `verify_exports.py` | `python verify_exports.py a.so b.so` |
| 崩溃地址反查 | `where.py` | `python where.py 0x41af27fc` |
| 部署 | `deploy_lib.py` | `python deploy_lib.py [so]` |
| 推文件 | `pushh.py` | `python pushh.py <本地> <设备路径> [mode]` |
| 设备执行 | `sh.py` | `python sh.py "cmd1" "cmd2"` |
| 构建 | `build_win.sh` + `link_win.sh` | `bash build_win.sh && bash link_win.sh` |

---

## 8. 踩坑清单（错 → 对）

| # | 错误做法 | 现象 | 正确做法 |
|---|---|---|---|
| 1 | `(off-0x10000)//4` 推 PLT 索引 | 解出 `108c52274` 这种荒谬地址 | `plt_base + 20 + i*12` |
| 2 | capstone 线性反汇编扫调用点 | 漏掉全部调用点 | 按 `(w>>24)&0xff==0xeb` 扫 BL 编码 |
| 3 | `Tag_Hash` 按带长度前缀解 | 158 条错 150 条 | 裸 4 字节 |
| 4 | hash 用 `sourceText+comment` | 全部"无译文" | `comment+sourceText` |
| 5 | 以为 `.o` 里 tr 重定位少是编译错 | 白查一轮 | 交叉跳转，属正常优化 |
| 6 | 用 `QObject::tr` 代替类内 `tr` | 静默退回英文 | 用类自己的 `tr` |
| 7 | 空壳类不补尺寸 | 4 字节堆越界，偶发崩溃 | 补齐 + 编译期断言 |
| 8 | 直接 `cat >` 写 `/usr/lib/ezx/lib` | 目标被截 0 字节，整机重启 | ramfs + `mount -o bind` |
| 9 | 只改 `Makefile` 不改 `link_win.sh` | 导出面多 109 个符号 | 两条路径参数一致 + 门禁校验 |
| 10 | 在帧缓冲解码上死磕 | 浪费时间，图仍乱 | 改反汇编/进程统计取证 |
| 11 | 以为"体量小 = 有 bug" | 误报 | 体量差要结合调用序列判断 |
| 12 | `sl` 用"下一条指令地址"当 pc | GOT 整表错位一格，signal/slot 对调，误判原库写错 | `sl = add 指令地址 + 8 + 字面量` |
| 13 | 设备 shell 用裸 socket 连 telnet | 只回 `\xff\xfd\x18…` 后再无输出，看着像设备死机 | 必须回应 IAC（统一 `WONT/DONT`），并用唯一哨兵串判命令结束，别等提示符 |
| 14 | telnet shell 里 `new QApplication` | SIGSEGV 在 `QObject::queryMetaObject()+12`（Qt/E 应用只能由平台 launcher 起） | 别在外部 shell 起 Qt/E 应用；要驱动平台代码，改成"让平台进程自己调"（QCop 消息等） |
| 15 | 只靠 mangled 名判断函数语义 | 返回类型**不进** mangled 名，`getBigIcon()` 被当成 QPixmap 用了很久 | 返回类型要用**函数体证据**定：sret 只写 4 字节 + 全是 QString/QFileInfo 调用 ⇒ 返回 QString |
| 16 | `ZConfig::writeEntry(g,k,"NO")` | 命中 `writeEntry(g,k,bool)` 重载，静默写成 `1` | 值一律显式 `QString::fromLatin1(...)`，或 `QLatin1String`；`const char*`→`bool` 是标准转换，赢过用户定义转换 |

---

## 9. 验收清单（可直接照抄）

- [ ] 编译全绿（本项目 23 个翻译单元，失败 0）
- [ ] 导出面：原库 N / 本库 N，缺 0 多 0
- [ ] 关键构造函数反汇编：`new` 分配大小与真身一致
- [ ] 关键构造函数反汇编：`connect` 数量与真身一致
- [ ] 关键 `connect` 的**信号/槽字符串**逐条与原库一致（数量对不代表对）
- [ ] 软除法/软模调用数与原库同数量级
- [ ] 埋点调用数为 0（`AM_RING_ENABLE=0` 生效）
- [ ] 设备 `BOOT <版本> pid=<pid>` 出现在日志
- [ ] 无 `CRASH` / `SIGSEGV` / `FATAL`
- [ ] idle 8s：`du=0 ds=0`（无内存/栈增长）
- [ ] 用户实测：逐条对照报障项

---

## 10. 最值得记住的三条

1. **上下文比实现更容易错。** 同一个函数、同样的参数，`this`/context/寄存器
   错一格，行为就完全不同，而且**不报错**。模板 A 永远是第一嫌疑。
2. **"能跑"不等于"对"。** 少了 5 条 `connect`，程序照样跑，只是永远显示旧值；
   少了 4 字节，程序照样跑，只是几小时后崩。验收要查**行为是否跟随状态变化**。
3. **产物自查不可省。** 源码看着对、编出来不对，这种事只有反汇编成品能发现。
   第 6 节 SOP 的第 ⑤ 步是整条流水线的保险丝。

---

## 11. 运行时触发路径的复查（静态审计的盲区）

> 来源：本项目收尾阶段（H5～H8）。此前"32 项触达差异全部判定完毕"的结论，
> 完全没覆盖热插拔链——因为它们在静态引用图里显示 NOT TOUCHED。

### 11.1 触达 ≠ 运行时可达

调用方映射（`audit_all.py` 生成的 `_callermap.json`）基于**导出符号的静态引用**。
凡是**信号槽、QCop 消息、事件驱动**进入的路径（`slotHotPlugForceUpdate`、
`handleCardPlugIn/Out` 这类），静态图一律 NOT TOUCHED——审计工具看不见，
人工差异判定也就跳过了它们。**判据**：写完差异报告后，再拿原库的
Q_OBJECT 信号表 / QCop 消息名清单逐个问一遍"这条链的每个函数我查过吗"。

### 11.2 H5～H8 案例与方法

| # | 现象 | 根因 | 修复要点 |
|---|---|---|---|
| H5 | 拔卡槽收到任意系统消息都可能误触拔卡 | 槽实现 `(void)channel` 丢了原库的 `strcmp(channel,"MmcsdShmUpdate(int)")` 过滤 | qApp 的 systemMessage 会被**所有**系统消息触发；字面池解码出频道常量（GOT 二级间接），并与平台互证：设备上 `grep -al MmcsdShmUpdate /usr/SYSqtapp/*/*` 找到 5 个系统程序引用、事件源 misc1 里同时有 channel `EZX/Application/mmcsdhotplug` |
| H6 | 插卡后卡应用不在菜单 | 原库 handleCardPlugIn(6844B) 从 PlugCardRegistry 读回 Folder/Pos → addAppToMainMenu → Folder::add → folderChanged；重建版只 createObject 不挂链 | 挂链前判断 `parentFolder()!=0` 则跳过——启动路径因此恒为空操作，无副作用（真机日志 restored=0 实证） |
| H7 | 拔卡后整条恢复链静默断裂 | 拔卡链用 `getName()` 传键；AM 对象一律按 **UUID/注册表组名**索引，getName 是本地化名字（还会读 SETUP 配置） | 涉及 m_pAppMap/UserMenuTree 的键一律 `it.key()` / `parent->UUID()` |
| H8 | 拔卡时 am 整族重启（真机实测崩溃） | `removeAppFromMainMenu` 只写配置不动内存，但尾部发**异步 QCop**（消息名复用 "UninstallApp(QString)"）；am 收到即刷新界面遍历 Folder::m_appList——此时对象已 delete，Folder 里全是悬空指针 → UAF | delete 前遍历所有 Folder 调 `AM_Folder::remove` 摘链 + 清父指针；**异步通知 + 删除的时序差**是此类 UAF 的通用模式 |

### 11.3 让实测可诊断的三个埋点设计

1. **connect 返回值落日志**：Qt2 connect 失败只 qWarning 静默失效。构造函数里
   `AMLOG("hotplug connect=%d", connect(...))` 一行，部署后读日志即证信号链真实生效
   （H1 的 connect=1 就是这么实证的，比任何静态推理都硬）。
2. **事件入口埋点**：`[HotPlug] msg=%s type=%d`——物理插拔后从日志直接读出
   平台真实消息名与 type 取值，过滤逻辑是否放行一目了然。
3. **关键动作首尾埋点**：`begin/done removed=N`——崩溃时能精确定位"死在动作里
   还是动作之后"（H8 恰是 done 打完才崩 → 崩点在事件循环，不在函数内）。

### 11.4 平台侧消息名互证法

原库 strcmp 的常量就是规范本身，但**双向验证**更稳：在设备上
`grep -al "<消息名>" /usr/SYSqtapp/*/*`，发送方程序里必然出现同一字符串；
事件源进程（如 windowsserver/misc1）的字面池里通常还能挖出 QCop channel 名
与配套槽签名（`slotLaunchMMCSD(void)`、`mmcsdeject`）。三方一致（原库比较目标 /
平台发送方 / 重建过滤）才算定案。

### 11.5 其他收尾期教训

- **EZXAID/Java 管理热点**：卸载或改库时 ezxaid 会 new **两个**临时
  `AM_RegistryManager`，各自全量 loadMenuTree + clearAppMap（84 个对象）——
  每次约 650ms，是"Java 改分类卡"类问题的固定嫌疑点，排查先看这里。
- **跨进程刷新链**：应用锁/改名等跨进程刷新全部走 QCop，消息名 = 完整槽签名；
  修消息名前先在设备库 `grep -al` 验证平台侧真实存在的写法（H3 的
  `AppLockValueChanged()` 就是在 libezxmystuff-*/libezxvoicenote 里命中的）。
- **release 化收尾**：调试日志用编译开关（`EZXAM_DEBUG`）消除而不是逐条删——
  排障时 `make EZXAM_DEBUG=1` 一键恢复，交付件零开销；但 BOOT 分隔行、
  崩溃现场抓取、功能性修复日志这三类是"安全设施"，永远保留。
