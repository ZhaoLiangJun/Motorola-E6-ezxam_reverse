# -*- coding: utf-8 -*-
# 清理 tracer 残留、恢复 246、重新附着 tracer 到主 am
import sys, os, re, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dev import connect, run

T = '/mmc/mmca1/.system/arm-linux-gcc/bin'
c = connect()
out = run(c, 'ps ax 2>&1', wait=2)
print('--- current am/tracer:')
for l in out.splitlines():
    if '/usr/SYSqtapp/am/am' in l or 'tracer' in l:
        print(l)
# 恢复被停住的进程
print(run(c, 'kill -CONT 246 2>/dev/null', wait=1))
# 清 trace.txt
run(c, ': > /ram/tmp/trace.txt', wait=1)
pids = []
for l in out.splitlines():
    if '/usr/SYSqtapp/am/am' in l:
        m = re.match(r'\s*(\d+)', l)
        if m: pids.append(int(m.group(1)))
pids.sort()
pid = pids[0] if pids else None
print('attach target:', pid)
if pid:
    cmd = 'PATH=' + T + ':$PATH ' + T + '/tracer ' + str(pid) + ' 3600 >/ram/tmp/tracer.log 2>&1 &'
    c._send_line(cmd)
    time.sleep(2)
    print(run(c, 'cat /ram/tmp/tracer.log', wait=2))
    out2 = run(c, 'ps ax 2>&1', wait=2)
    print([l for l in out2.splitlines() if 'tracer' in l])
