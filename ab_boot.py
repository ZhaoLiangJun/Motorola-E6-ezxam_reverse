# -*- coding: utf-8 -*-
"""ab_boot.py —— A/B 对照：换指定库后重启 am，统计启动阶段重启次数

用法: python ab_boot.py <libfile> [观察秒数]
判定：kill am 后 windowsserver 会拉起。若 am 在启动阶段崩溃，pid 会连续变化。
      统计 90 秒内出现过的 pid 集合大小 + 变化次数。
"""
import sys, os, time, threading, functools, http.server, hashlib
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dev import connect, run

T = '/mmc/mmca1/.system/arm-linux-gcc/bin'
DST = '/usr/lib/ezx/lib/libezxam.so.1.0.0'
RAMDIR = '/ram/amlib'
RAMPATH = RAMDIR + '/libezxam.so.1.0.0'
ROOT = os.path.dirname(os.path.abspath(__file__))

src = sys.argv[1]
watch = int(sys.argv[2]) if len(sys.argv) > 2 else 90
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
    return run(c, cmd, w).replace('\r', '')

def pids():
    out = rd('pidof am', 0.9)
    return tuple(sorted(x for x in out.replace('#', '').split() if x.isdigit()))

# 1) 上传
rd('mkdir -p ' + RAMDIR, 2)
rd('rm -f /ram/tmp/ab.so', 1)
print(rd(T + '/wget -O /ram/tmp/ab.so http://192.168.16.1:8000/' + src, 90), flush=True)
import re
ls = rd('ls -l /ram/tmp/ab.so', 2.2)
print('dl:', ls.strip(), flush=True)
raw = rd(T + '/md5sum /ram/tmp/ab.so', 3.0)
m = re.search(r'\b([0-9a-f]{32})\b', raw)
got = m.group(1) if m else '?'
print('dl md5=%s expect=%s %s' % (got, md5, 'OK' if got == md5 else 'MISMATCH'), flush=True)
if not m:
    print('raw md5 output: %r' % raw, flush=True)
if got != md5:
    print('ABORT: upload mismatch', flush=True); sys.exit(2)

# 2) 落到 ramfs + bind
print(rd('[ -s /ram/tmp/ab.so ] && cat /ram/tmp/ab.so > ' + RAMPATH + ' && echo WROTE=ok', 6), flush=True)
print(rd('ls -l ' + RAMPATH, 1.2), flush=True)
print(rd('mount | grep -c libezxam', 1.2), flush=True)
rd('mount -o bind ' + RAMPATH + ' ' + DST, 3)
mm = re.search(r'\b([0-9a-f]{32})\b', rd(T + '/md5sum ' + DST, 3.0))
print('DST md5=%s %s' % (mm.group(1) if mm else '?',
                         'OK' if mm and mm.group(1) == md5 else 'CHECK'), flush=True)

# 3) 清空日志标记（我们的库才有；原库忽略）
rd('touch /ram/tmp/am_debug', 1)
rd(': > /ram/tmp/ab_boot.log', 1)
rd('cp /ram/tmp/amreg.log /ram/tmp/amreg.keep 2>/dev/null; : > /ram/tmp/amreg.log', 1)

# 4) kill 并观察
print('kill am ...', flush=True)
rd('kill -9 $(pidof am)', 1.5)

seen = []
t0 = time.time()
while time.time() - t0 < watch:
    p = pids()
    if p and (not seen or seen[-1] != p):
        seen.append(p)
        print('  t=%5.1fs pidof=%s' % (time.time() - t0, ' '.join(p)), flush=True)
    time.sleep(1.8)

print('\n=== 结论 ===', flush=True)
print('观察到 %d 个不同 pid 组合: %s' % (len(seen), seen), flush=True)
print('重启次数 = %d' % (len(seen) - 1 if seen else 0), flush=True)
crash = rd('grep -c "==== CRASH" /ram/tmp/amreg.log', 1.5)
print('amreg.log CRASH 计数: %s' % crash.strip(), flush=True)
