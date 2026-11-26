# -*- coding: utf-8 -*-
"""watch_install.py —— 实时抓取 /ram/tmp/amreg.log 的增量

用法:
  python watch_install.py [总时长秒=900]

设备只有 46MB 内存，日志缓冲会被后续写入覆盖，所以必须边跑边拉。
每次内容变化就把「新增部分」追加到 _live.log，同时把出现过的
/ram/tmp/am_maps_<pid>.txt 拉回来（崩溃现场反查模块基址用）。
"""
import sys, os, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dev import connect, run

HERE = os.path.dirname(os.path.abspath(__file__))
LIVE = os.path.join(HERE, '_live.log')
LOG = '/ram/tmp/amreg.log'
DUR = int(sys.argv[1]) if len(sys.argv) > 1 else 900

c = connect()
last = ''
seen_maps = set()
t0 = time.time()
open(LIVE, 'w').close()

print('[watch] start, %ds, -> %s' % (DUR, LIVE))
while time.time() - t0 < DUR:
    try:
        out = run(c, 'cat ' + LOG, 0.9).replace('\r', '')
    except Exception as e:
        print('[watch] reconnect: %s' % e)
        try:
            c = connect()
        except Exception:
            time.sleep(3)
        continue

    # 去掉回显的首行（命令本身）和结尾提示符
    lines = [l for l in out.splitlines()]
    if lines and lines[0].strip().endswith(LOG):
        lines = lines[1:]
    while lines and lines[-1].strip() == '#':
        lines.pop()
    cur = '\n'.join(lines)

    if cur != last:
        # 计算增量：取最长公共前缀
        n = 0
        m = min(len(cur), len(last))
        while n < m and cur[n] == last[n]:
            n += 1
        delta = cur[n:]
        if delta:
            with open(LIVE, 'a') as f:
                f.write(delta)
                if not delta.endswith('\n'):
                    f.write('\n')
            for l in delta.splitlines():
                if l.strip():
                    print(l)
        last = cur

    # 只有出现 CRASH 才去拉 maps（省设备 I/O）
    if 'CRASH' in locals().get('delta', '') or 'CRASH' in cur:
        try:
            ls = run(c, 'ls /ram/tmp/ | egrep "am_maps_"', 0.8).replace('\r', '')
            for name in ls.split():
                if name.startswith('am_maps_') and name not in seen_maps:
                    seen_maps.add(name)
                    body = run(c, 'cat /ram/tmp/' + name, 2.0).replace('\r', '')
                    p = os.path.join(HERE, '_' + name)
                    with open(p, 'w') as f:
                        f.write(body)
                    print('[watch] saved %s (%d B)' % (name, len(body)))
        except Exception:
            pass

    time.sleep(1.2)

print('[watch] done after %.0fs' % (time.time() - t0))
