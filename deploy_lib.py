# -*- coding: utf-8 -*-
"""deploy_lib.py —— 安全部署 libezxam 到设备（v41 起的标准做法）

为什么不能直接覆写 /usr/lib/ezx/lib/libezxam.so.1.0.0：
  `/` 是 **cramfs（只读）**。历史上能 `cat x.so > 该路径` 写成功，是因为
  平台自己在那个路径上叠了一个可写的单文件挂载；重启后这个挂载就没了，
  此时再写就是往 cramfs 写 → 而且 `> 路径` 的 shell 重定向会先 O_TRUNC，
  于是**目标被截成 0 字节**，am 起不来，平台反复 respawn 失败 → 整机重启。

现在的做法（已实测可用，且不动固件）：
  1) 把库放到 ramfs：/ram/amlib/libezxam.so.1.0.0
  2) `mount -o bind` 把它盖到 /usr/lib/ezx/lib/libezxam.so.1.0.0
  3) kill -9 am → windowsserver 拉起，新进程即映射新库
  重启后 ramfs 与 bind 挂载都消失 → 自动回到原厂库（天然"安全网"）。

用法：
  python deploy_lib.py                    # 部署 libezxam_reconstructed.so.1.0.0
  python deploy_lib.py orig_local.so      # 回滚到原版
"""
import sys, os, time, threading, functools, http.server, hashlib
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dev import connect, run

T = '/mmc/mmca1/.system/arm-linux-gcc/bin'
DST = '/usr/lib/ezx/lib/libezxam.so.1.0.0'
RAMDIR = '/ram/amlib'
RAMPATH = RAMDIR + '/libezxam.so.1.0.0'
ROOT = os.path.dirname(os.path.abspath(__file__))

src = sys.argv[1] if len(sys.argv) > 1 else 'libezxam_reconstructed.so.1.0.0'
full = os.path.join(ROOT, src)
md5 = hashlib.md5(open(full, 'rb').read()).hexdigest()
size = os.path.getsize(full)
print('SRC %s md5=%s size=%d' % (src, md5, size), flush=True)

httpd = http.server.ThreadingHTTPServer(
    ('192.168.16.1', 8000),
    functools.partial(http.server.SimpleHTTPRequestHandler, directory=ROOT))
threading.Thread(target=httpd.serve_forever, daemon=True).start()

c = connect()
print('connected', flush=True)


def rd(cmd, w=1.5):
    return run(c, cmd, w)


def alive_am():
    ps = [x for x in rd('pidof am', 1.2).replace('#', '').split() if x.isdigit()]
    ok = []
    for p in ps:
        st = rd('cat /proc/%s/stat' % p, 0.5).split()
        if len(st) > 2 and st[2] != 'Z':
            ok.append(p)
    return ps, ok


# 1) 确保 ramfs 目录
rd('mkdir -p ' + RAMDIR, 2)

# 2) 下载到 /ram/tmp（不动任何已挂载的东西）
rd('rm -f /ram/tmp/new.so', 1)
rd(T + '/wget -O /ram/tmp/new.so http://192.168.16.1:8000/' + src, 45)
chk = rd('ls -l /ram/tmp/new.so', 1.2)
print('DL', chk, flush=True)
if (' %d ' % size) not in chk:
    print('!! 下载大小不符，中止', flush=True)
    raise SystemExit(1)
dm = rd(T + '/md5sum /ram/tmp/new.so', 2)
if md5 not in dm:
    print('!! 下载 md5 不符，中止', flush=True)
    raise SystemExit(1)

# 3) 拷贝到 ramfs（先确认源存在，再 cat；ramfs 无空间限制）
rd('[ -s /ram/tmp/new.so ] && cat /ram/tmp/new.so > %s ; echo CP=$?' % RAMPATH, 8)
print('RAM md5', rd(T + '/md5sum ' + RAMPATH, 2), flush=True)

# 4) bind 挂载（已挂过则忽略失败）
m = rd('mount -o bind %s %s ; echo M=$?' % (RAMPATH, DST), 4)
print('MOUNT', m.strip(), flush=True)
inst = rd(T + '/md5sum ' + DST, 2)
print('DST md5', inst, flush=True)
if md5 not in inst:
    print('!! 目标 md5 不符，未重启 am', flush=True)
    raise SystemExit(2)

# 5) 重启 am 并观察
rd('rm -f /ram/tmp/new.so', 1)
print(rd('kill -9 $(pidof am) 2>&1; echo K', 2), flush=True)
time.sleep(20)
ps, ok = alive_am()
print('pids=%s alive=%s' % (ps, ok), flush=True)
if not ok:
    print('!! am 未起来，检查日志', flush=True)
    raise SystemExit(3)

pid = ok[0]
a = rd('cat /proc/%s/stat' % pid, 0.6).split()
time.sleep(8)
b = rd('cat /proc/%s/stat' % pid, 0.6).split()
if len(a) > 14 and len(b) > 14:
    print('idle 8s: du=%d ds=%d' % (int(b[13]) - int(a[13]), int(b[14]) - int(a[14])), flush=True)
print('mem:', rd('cat /proc/meminfo | grep -E "MemFree|MemTotal"', 2).replace('\n', ' | '), flush=True)
print('DONE', flush=True)
