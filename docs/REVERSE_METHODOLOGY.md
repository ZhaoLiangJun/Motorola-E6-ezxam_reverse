# 逆向并 1:1 重建一个 ARM/Qt2 共享库的方法论

> 本工程把 Motorola E6 的 `libezxam.so.1.0.0`（282KB，410 个导出符号，无源码无调试信息）
> 重建为可替换的 `libezxam.so.1.0.0`，主桌面功能正常、空闲 CPU 与原厂一致。
> 下面是可复用的做法，按"照做就能开工"的粒度写。

## 1. 先摸清约束（决定后面所有技术选择）

| 约束 | 实测值 | 影响 |
|---|---|---|
| CPU / 内核 | PXA27x，Linux **2.4.20** | 无 `perf`、无 `strace`、`/proc` 字段少；`ptrace` 对 cloned 进程会失败 |
| 内存 | 46MB，`MemFree` 常态 **仅 1.3MB** | 任何按帧分配/泄漏都会致命；缓存要限上限 |
| 库定位 | **prelink 到 0x41ad8000** | 运行地址 == ELF vaddr，不用减基址；A/B 对比可直接用地址对地址 |
| 根文件系统 | `/` 是 **cramfs 只读** | 不能直接覆写库文件（见 §6 血案） |
| 工具链 | arm-linux-gcc **3.3.6** + Qt/Embedded **2.3.8**（cygwin 版） | 只能用它编，才能与设备 ABI 一致（`-fno-exceptions -fno-rtti -fPIC`） |
| 调试器 | 设备**没有** gdb / strace；`$T/gdb` 缺 `libncurses.so.5` 起不来 | 必须自建观测手段 |

## 2. 工具链与环境（Windows + cygwin）

```bash
# 编译（注意：cygwin 的 make 处理不了 D:/ 路径里的冒号，用 mingw 原生 make）
export MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL='*'
export PATH="/d/cygwin/home/ezx/arm-linux-gcc/bin:$PATH"
/d/MSYS64/mingw64/bin/make
```

关键编译参数（`Makefile`）：
```
-pipe -DQWS -fno-exceptions -fno-rtti -O2 -D_REENTRANT -fPIC
-Wl,-soname,libezxam.so.1 -Wl,-T,fixed_base.lds      # 用链接脚本钉死基址
```

**为什么必须钉死基址**：原库是 prelink 过的，库内所有绝对地址引用（GOT、vtable、
字符串字面量指针）都按 0x41ad8000 编排。基址一变，凡是"把函数指针存进结构体"
的地方（Qt 元对象、信号槽表）全错位。

## 3. 七步法（每步都有可交付物）

### 第 1 步：恢复"有什么" —— 符号表与函数边界

原库有 `.dynsym` 但函数边界要靠扫描。用 `capstone` 自建反汇编（cygwin 的
`objdump/readelf` 在 MSYS 下 rc=127，不能依赖）：

```python
# adis.py / edis.py：读 ELF 段 → 线性扫 ARM 指令 → 找 push{...,lr} 开头 + 对齐
# 产物：每个符号的起始地址与体量（sizecmp.py 直接用）
```

### 第 2 步：恢复"调用了谁" —— PLT / GOT

ARM 位置无关代码的固定套路：

```
add  sl, pc, sl          ; sl = GOT 基址
ldr  r3, [pc, #0xNNN]    ; r3 = GOT 槽偏移
ldr  r0, [sl, r3]        ; r0 = 真实函数地址
```

**最大的坑**：`add sl, pc, sl` 里的 `pc` 读作 **当前指令地址 + 8**。
按 `+0` 算会得到 0x41B229E8，真值是 0x41B229F0，差 8 字节会让**整张 GOT 错位一个槽**，
表现为"每个符号都解析成了隔壁那个"（All 变成 User1、Office 变成 Multimedia）。
这个错误会一路传染到所有交叉引用分析，必须第一时间用已知符号校准。

### 第 3 步：恢复"数据结构" —— 偏移 + 类型

- 对象布局靠 `operator new(size)` 的常量推断：`new 0x84` → 类大小 0x84。
- 字段语义靠"谁读谁写"：`[this+0x1c]` 既被 `QString::operator=` 写、又被当作参数传给
  `ZConfig::readEntry` 的 key，那它就是某个 QString 成员。
- **不要用 `int[4]` 占位 16 字节的东西**：本工程 `AM_AppObject+0x20` 一度写成 4 个 int，
  后来发现是 `QUuid`（`ulong data1; ushort data2; ushort data3; uchar data4[8]`，恰好 16 字节），
  导致 am 取到全零 UUID、应用启动失败。判据：am 侧 `add r7,r1,#32` 之后按 `ldr` 取 4 字节比较，
  就是 QUuid.data1。

### 第 4 步：恢复"逻辑" —— 巨型 if/switch 链用解释器跑

`getName` 有 14020 字节，全是 `strcmp(uuid, "xxxx") ? tr(key, cmt) : 下一个`。
硬读指令流要几百行人工。**自建迷你 ARM 解释器**（`gnemu.py`，约 200 行）反而更快：

```python
python gnemu.py <Type> <uuid[,uuid...]> [device]
# 在原库机器码上真跑：实现 mov/ldr/str/cmp/b/bl/bx/push/pop/adr/add/sub...
# 在 bl（调 tr）处停下，打印 r1/r2 指向的字符串 → 直接得到 (key, comment)
```

产物就是 `docs/I18N.md` 里那张 13 组 UUID → `TXT_RID_*` 表。
**这是本工程性价比最高的工具**：把"读汇编"变成"跑汇编"。

同理 `gnext.py` / `gnext2.py`：线性扫描 + 常量传播 + sp 偏移跟踪，
从 `strh/strb/str/strb` 序列里还原 16 字节 GUID 和"GUID → 两条 GOT 键"的对应。

### 第 5 步：写实现（一个函数一个 .cpp 段，注释里带地址和字节数）

```cpp
/* ============ getAppObject ============ */
/* 反编译 0x41af6e48（原库 1024 字节）
   1) 先按名字精确命中 m_pAppMap
   2) 未命中则按 AppID 兜底
   3) 仍未命中打 MISS（热路径 → AMVLOG） */
```

注释里写"原库地址 + 字节数"，是后面做一致性审计的锚点。

### 第 6 步：行为对齐（不是体量对齐）

函数体量只能当**线索**，不能当验收标准：原库把大量 QString COW 内联噪音算进了函数体，
我们的实现短 10 倍也可能完全等价（`getName` 14020B → 668B，表驱动）。

真正的验收是**在设备上跑通功能**，并做 A/B：

```python
# A/B：把原厂库换回去，采同样的指标
python deploy_lib.py orig_local.so
# 采 /proc/<pid>/stat 的 utime/stime
```

本工程决定性的一步就是这个 A/B：原厂库空闲 60 秒 `u12 s511` 一动不动，
我们的库 40 秒涨 1100 tick stime —— **证明空转是我们的问题**（系统调用风暴），
而不是"这机器本来就慢"。没有这一步，就会一直在错误的方向上猜。

### 第 7 步：回归（每次都跑同一组指标）

| 指标 | 采集方式 | 期望 |
|---|---|---|
| 启动日志 | `cat /ram/tmp/amreg.log` | `[RMCtor] ... done`，无 CRASH |
| 空闲 CPU | `/proc/<pid>/stat` 字段 14/15（utime/stime） | 60 秒内 **不增长** |
| 内存 | `/proc/meminfo` MemFree | 不阶梯下降 |
| 本地化 | `grep getFolderName` | `全部/办公/多媒体/...` |
| 功能 | 人工点 | 开应用、卸载两次、排序、插卡 |

## 4. 板上观测手段（没有 gdb 怎么办）

1. **内存环形日志**（`ringlog.h`）：`ring_put(tag, a, b)` 只写全局数组，零 I/O；
   `SIGUSR1` 转储到 `/tmp/ring_dump.txt`。适合还原"am 到底按什么顺序调我们"。
   - 注意：`kill -USR1 <pidof am>` 可能发给了**僵尸进程**，先看 `stat` 的 state 是不是 Z。
2. **分级日志** `AMLOG / AMVLOG`：热路径必须走 `AMVLOG`（默认不写，`touch /ram/tmp/am_debug` 开）。
   本工程 39 处 `alog()` 忘了接开关，直接导致"屏幕点不动"。
3. **PC 采样**（`sampler.c`）：`ptrace(PTRACE_ATTACH)` → `GETREGS` → PC 直方图 → 对符号表定位热点。
4. **帧缓冲取证**：`dd if=/dev/fb0 of=/ram/tmp/fb.raw bs=4096 count=63` 抓屏，
   本地用自写 PNG 编码器（`fb2png.py`，环境无 PIL）转图。注意 stride 不一定是 `宽×2`。

## 5. 部署方法（必须遵守）

```
库文件放 ramfs → mount -o bind 盖到目标路径 → kill -9 am
```

```bash
mkdir -p /ram/amlib
[ -s /ram/tmp/new.so ] && cat /ram/tmp/new.so > /ram/amlib/libezxam.so.1.0.0
mount -o bind /ram/amlib/libezxam.so.1.0.0 /usr/lib/ezx/lib/libezxam.so.1.0.0
$T/md5sum /usr/lib/ezx/lib/libezxam.so.1.0.0   # 校验不过就别 kill am
```

好处：固件零改动、重启自动回原厂（天然安全网）。工程里 `deploy_lib.py` 是封装。

## 6. 血案清单（每一条都真实发生过，代价是设备重启/半天时间）

| 坑 | 表现 | 正确做法 |
|---|---|---|
| `cat x > 只读fs上的文件` | shell 先 `O_TRUNC` → 目标变 0 字节 → am 起不来 → **整机重启**，telnetd 卡死十几分钟 | 先 `[ -s 源 ]`，写 ramfs 副本再 bind mount |
| `dd` 写同一目标 | 同样先截断再失败，md5 变 `d41d8cd9...`（空文件） | 同上；`dd` 不是安全网 |
| `add sl,pc,sl` 的 pc 偏移 | GOT 整表错位一槽，符号全部张冠李戴 | pc = 当前地址 + 8，用已知符号校准 |
| 设备 `grep` | 对二进制/`/proc` 给出错误结果（曾误报"所有 zh-hk 文件都含某串"） | 二进制搜串拉回 PC 用 Python 做 |
| 设备 `find` | 返回空但文件确实存在 | 用 `ls` + shell glob |
| `pidof am` 返回僵尸 | 采 CPU 全程 `Z`，日志/信号都无反应 | 先读 `/proc/<pid>/stat` 第 3 字段确认 `R/S` |
| cygwin `make` + `D:/` 路径 | `target pattern contains no '%'` | 用 `/d/MSYS64/mingw64/bin/make`，并删掉旧的 `.d`（里面带冒号路径） |
| Qt2 的 `QList<T>` | `QList<AM_AppObject*>` 编译不过 | Qt2 的 `QList<type>` 内部就存 `const type*`，写 `QList<AM_AppObject>` |
| `QMap::clear()` | 只释放节点，不 delete 值 → 每次泄漏整套对象 | 遍历 `delete` 后再 clear，并用 `seen` 去重 |

## 7. 可直接复用到别的库的资产

| 文件 | 用途 | 移植成本 |
|---|---|---|
| `edis.py` / `adis.py` | ARM ELF 读段 + 符号 + 线性反汇编（不依赖 cygwin binutils） | 改路径即可 |
| `gnemu.py` | 迷你 ARM 解释器，跑原库代码取常量 | 改入口地址/停止条件 |
| `gnext.py` | 常量传播恢复 GUID / 字面量对应 | 中 |
| `sizecmp.py` | 符号体量比对，快速找"原型不符" | 改两个路径 |
| `deploy_lib.py` | 安全部署（ramfs + bind + md5 双校） | 改路径 |
| `nc_listen.py` + `t_up` | 设备 → PC 传文件（比 base64 over telnet 稳） | 零 |
| `amlog.h` + `ringlog.h` | 分级日志 + 零 I/O 环形日志 | 零 |

## 8. 一句话总结

**先用 A/B 把"是不是我们的锅"钉死，再用解释器把"数据长什么样"跑出来，
最后用设备指标而不是函数体量验收。**
逆向的绝大部分时间不是花在读懂汇编，而是花在"确认自己没有读错"。
