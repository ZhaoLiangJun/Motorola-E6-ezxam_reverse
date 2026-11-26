# E6 设备 ↔ PC 文件传输方式总表

设备：Motorola E6，telnet `192.168.16.2:23`（root 免密）
PC：`192.168.16.1`（与设备同一 USB-LAN 网段；`WLAN 192.168.0.7` 不可用于设备）
设备工具链目录（下文记作 `$T`）：`/mmc/mmca1/.system/arm-linux-gcc/bin`

---

## 1. PC → 设备：HTTP + wget ★首选

设备自带 `wget`，PC 起一个只监听指定网卡的静态服务器即可。

```bash
# PC 侧（后台）
python -m http.server 8809 --bind 192.168.16.1 \
       --directory "D:/WorkBuddy/2026-11-08-18-54-48/ezxam_reverse"

# 设备侧
T=/mmc/mmca1/.system/arm-linux-gcc/bin
$T/wget -q http://192.168.16.1:8809/libezxam_v24.so.1.0.0 -O /ram/tmp/v24.so
$T/md5sum /ram/tmp/v24.so
```

* 实测：280366 字节 **≈1 秒**（旧 heredoc 方式要 10 分钟）。
* 必须 `--bind 192.168.16.1`：绑 0.0.0.0 会被安全策略拦；绑错网卡设备连不上。
* Windows 防火墙可能拦截 8809：首次运行若设备 `wget` 返回非 0，放行一次即可。
* 反向（设备当服务端）不可行：设备只有 wget 客户端，没有 httpd applet。

## 2. 设备 → PC：`nc_listen.py` + `t_up` ★首选

设备没有 nc/curl/php（php 缺 `libsnmp-0.4.2.so` 起不来），但**我们自己交叉编译一个 30 行的上传器**即可，
一次推送长期复用（本仓库 `t_up.c` / `t_up`，已编好）。

```bash
# PC 侧（后台监听，收到即落盘并打印 md5）
python nc_listen.py 9911 out.bin 40

# 设备侧
/ram/tmp/t_up 192.168.16.1 9911 /mmc/mmca1/v24.so
```

* 实测：280366 字节，PC 侧 md5 = 设备侧 md5，字节完全一致（可传二进制、core、注册表文件）。
* 适合"把设备上的东西拿下来分析"——比 `cat` 文本回显可靠得多，也不会被 CR/LF 改写。

## 3. 无网络/应急：八进制 heredoc + gzip（`dev_push.py`）

```bash
MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL='*' python dev_push.py 本地文件 /ram/tmp/目标
```

* 原理：gzip → `\ooo` 八进制文本 → 设备 `cat >> f.oct << 'EOF'` 分块追加 → `printf %b` 还原 → `gzip -d`。
* **只适合小文件（≤ 约 64KB）**：1400 字节/块、每块约 2 秒，280KB 要 10 分钟；
  且最后 `while read … printf %b` 的转换步骤会因 `run()` 等待时间不足而**中途截断**（实测 280KB 只落地 32768 字节）。
* 服务端无网时唯一的通用路径；`t_dlopen`（9KB）这类小工具用它很稳。

## 4. 编码变体（设备端解码能力已验证）

| 编码 | 设备端解码命令 | 结论 |
|------|----------------|------|
| base64 | `openssl base64 -d -in a.b64 -out a.bin` | ✓ 可用（单文件、小体积稳定） |
| uuencode | `busybox uuencode /path/u.txt u.txt > u.uu` / `busybox uudecode -o out u.uu` | ✓ 设备自测通过 |
| xxd/hex | `busybox hexdump -C`（无 xxd） | 可看不可逆，不适合传输 |

注意：编码只解决"内容能不能贴过去"，**贴过去的通道仍然是 heredoc**，
所以大文件依然慢且易被截断；这类需求请直接走 HTTP。

## 5. Samba（设备已在跑，但 Windows 侧多半用不了）

设备 `smbd` 由 inetd 拉起，**139 端口可连**（445 关闭）：

```
[system]  path = /   read only = no   guest ok = yes   guest account = root
[home]    path = /diska
# 全局: interfaces = usbl0, security = share, null passwords = yes
```

* 意味着 `\\192.168.16.2\system` 就是设备根目录，游客可读写。
* 但 EZX 用的是 **SMB1 + share 级认证 + guest**，Windows 10/11 默认禁用 SMB1 与来宾访问，
  `net use` 多数会失败；若本机开启 `SMB1` 与 `InsecureGuestLogons` 策略则可用（图形化拖拽最省事）。
* 未在本机验证成功，仅记录配置事实。

## 6. 其他已确认的传输相关事实

* 设备**没有**：`nc`、`curl`、`base64`、`scp`、`rsync`、`tr`、`sort`、`killall`、`cat -n`、
  可用 `grep`（toolchain 里只有 `egrep`）、`wc/head/tail/awk/sed` 需用 `$T/` 前缀。
* 设备**有**：`wget`、`openssl`、`perl`（无 `IO::Socket::INET` 模块）、`busybox 1.10.4`
  （`uuencode/uudecode/hexdump/dd/tar/gzip/dmesg/telnet` 可用）、`$T/gcc`、`$T/gdb`（缺 `libncurses.so.5` 起不来）、
  `$T/gdbserver` 未验证。
* 读文本仍可用 telnet `cat`：`run(c, 'cat /path')`，简单但会被 telnet 的 CR/LF 与 `#` 提示符干扰，二进制勿用。

## 7. Git Bash 必踩的坑

Python 收到 `"/ram/tmp/x"` 这种以 `/` 开头的参数时，Git Bash 会把它改写成 Windows 路径
（实测变成 `D:/Program Files/…/ram/tmp/x`），于是设备端写入一个不存在的目录、静默失败。

```bash
export MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL='*'   # 调 python 传设备绝对路径前必须加
```

## 8. 常用组合（本工程部署流程）

```bash
# 发库（1 秒）
python -m http.server 8809 --bind 192.168.16.1 --directory <工程目录> &
$T/wget -q http://192.168.16.1:8809/libezxam_v24.so.1.0.0 -O /ram/tmp/v24.so
cp /ram/tmp/v24.so /mmc/mmca1/v24.so           # 落 SD 卡（持久）
mount --bind /ram/tmp/v24.so /usr/lib/ezx/lib/libezxam.so.1.0.0
$T/md5sum /usr/lib/ezx/lib/libezxam.so.1.0.0   # 必须与本地一致
kill -9 <am_pid>                               # windowsserver 会自动 respawn am

# 收结果（日志/注册表/崩溃现场）
python nc_listen.py 9911 amreg.log 30 &
/ram/tmp/t_up 192.168.16.1 9911 /ram/tmp/amreg.log
```

## 9. ⚠️ 部署库文件：只能用 bind mount（2026-11-17 血案）

**`/` 是 cramfs（只读）**。`/usr/lib/ezx/lib/libezxam.so.1.0.0` 之所以有时能写，
是因为平台自己在那个路径上叠过一层可写的**单文件挂载**；**重启后这层就没了**。

此时执行 `cat new.so > /usr/lib/ezx/lib/libezxam.so.1.0.0`：
1. shell 先以 `O_TRUNC` 打开重定向目标 → **目标立刻被截成 0 字节**；
2. 然后 `cat` 往 cramfs 写 → `write error: Invalid argument`；
3. 结果 = 库文件 0 字节 → am 起不来 → windowsserver 反复 respawn 失败
   → **整机重启**（telnetd 也会被卡死，十几分钟内连不上）。
4. 更糟：如果 `cat` 的**源**文件恰好不存在（下载失败），目标照样被截断成 0，
   连"内容不对"都看不出来，只看到 md5 变成 `d41d8cd98f00b204e9800998ecf8427e`（空文件）。

`dd` 也一样（它同样先打开输出再写，且 ftruncate 在这类挂载上还失败）。

**正确做法（v41 起统一用 `deploy_lib.py`）**：

```bash
mkdir -p /ram/amlib
$T/wget -O /ram/tmp/new.so http://192.168.16.1:8000/libezxam_reconstructed.so.1.0.0
[ -s /ram/tmp/new.so ] && cat /ram/tmp/new.so > /ram/amlib/libezxam.so.1.0.0   # ramfs，随便写
mount -o bind /ram/amlib/libezxam.so.1.0.0 /usr/lib/ezx/lib/libezxam.so.1.0.0
$T/md5sum /usr/lib/ezx/lib/libezxam.so.1.0.0    # 必须与本地一致，不一致就别 kill am
kill -9 $(pidof am)
```

要点：
* 写的是 **ramfs** 上的副本，固件 cramfs 一个字节都不动 → 重启自动回到原厂库（天然安全网）。
* 每条命令前都 `[ -s 源 ]`，绝不让重定向截断任何"真文件"。
* 校验 md5 不过这关，**不要 kill am**（否则界面就没了，且不一定能靠 telnet 救回来）。
* 重启后需要重新 `mount -o bind` 一次（ramfs 内容不持久）。
