# -*- coding: utf-8 -*-
"""trials.py —— 连跑 N 次"杀 am → 观察启动"，统计额外重启次数

用法: python trials.py <标签> [轮数] [每轮观察秒数]
判读：kill 后 windowsserver 拉起新 am；若启动期崩溃，会出现一组与首组
      完全不重叠的 pid → 记 1 次重启。
"""
import sys, os, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dev import connect, run

tag = sys.argv[1] if len(sys.argv) > 1 else '?'
N = int(sys.argv[2]) if len(sys.argv) > 2 else 4
WATCH = int(sys.argv[3]) if len(sys.argv) > 3 else 20

state = {'c': None}

def getc():
    for _ in range(6):
        try:
            c = connect()
            state['c'] = c
            return c
        except Exception:
            time.sleep(6)
    return None

def rd(cmd, w=1.0):
    try:
        return run(state['c'], cmd, w).replace('\r', '')
    except Exception:
        if getc() is None:
            raise SystemExit('device lost')
        return ''

def pset():
    return set(x for x in rd('pidof am', 0.8).replace('#', '').split() if x.isdigit())

if getc() is None:
    raise SystemExit('no conn')

res = []
for t in range(N):
    rd('kill -9 $(pidof am)', 1.0)
    time.sleep(2)
    first = None
    restarts = 0
    t0 = time.time()
    while time.time() - t0 < WATCH:
        p = pset()
        if p:
            if first is None:
                first = set(p)
            elif not (p & first):
                restarts += 1
                first = set(p)
                print('   trial%d t=%4.1fs RESTART -> %s'
                      % (t + 1, time.time() - t0, ' '.join(sorted(p))), flush=True)
        time.sleep(1.3)
    res.append(restarts)
    print(' [%s] trial%d restarts=%d' % (tag, t + 1, restarts), flush=True)
    time.sleep(2)

print('=== [%s] %d 轮: %s  合计 %d 次重启 ===' % (tag, N, res, sum(res)), flush=True)
