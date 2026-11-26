# -*- coding: utf-8 -*-
"""semwatch.py —— 盯住设备上所有 SysV 信号量的值变化

用途：AM_QtPackageManager::install() 的第一道门是 AM_Mutex::setBusy()，
      它失败就弹“系统忙”。它本质是 semop(sem, -1, IPC_NOWAIT|SEM_UNDO)，
      只有信号量值为 0 才会失败。所以安装那一刻哪个信号量变成 0 就是元凶。

用法:
  python semwatch.py [时长秒=600] [间隔秒=1.0]

输出同时写到 _semwatch.log，只在内容变化时追加。
"""
import sys, os, time, datetime
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dev import connect, run

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '_semwatch.log')
DUR = int(sys.argv[1]) if len(sys.argv) > 1 else 600
GAP = float(sys.argv[2]) if len(sys.argv) > 2 else 1.0

c = connect()
last = None
t0 = time.time()
open(OUT, 'w').close()

print('[semwatch] %ds / %.1fs, -> %s' % (DUR, GAP, OUT))
while time.time() - t0 < DUR:
    try:
        raw = run(c, '/ram/sems2 -q', 1.0).replace('\r', '')
    except Exception as e:
        print('[semwatch] reconnect: %s' % e)
        try:
            c = connect()
        except Exception:
            time.sleep(3)
        continue

    rows = []
    for l in raw.splitlines():
        s = l.strip()
        if not s.startswith('0x'):
            continue
        parts = s.split()
        # key semid nsems val otime ctime
        if len(parts) < 4:
            continue
        try:
            rows.append((parts[0], int(parts[1]), int(parts[3])))
        except ValueError:
            pass
    rows.sort()
    sig = ';'.join('%s=%d' % (k, v) for k, _, v in rows)
    if sig != last:
        stamp = datetime.datetime.now().strftime('%H:%M:%S')
        line = '[%s] %s' % (stamp, sig)
        with open(OUT, 'a') as f:
            f.write(line + '\n')
        print(line)
        last = sig
    time.sleep(GAP)

print('[semwatch] done')
