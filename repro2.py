# -*- coding: utf-8 -*-
# 复现主流程：挂载v21 -> kill am(用我们的库重生) -> tracer附着 -> t_qcop触发3次启动
import sys, os, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dev import connect, run

CALC = '6aff7618-c940-48d9-ad3b-3106d9a884c2'
c = connect()

print('=== ps ax ==='); print(run(c, 'ps ax 2>&1', wait=2))
print('=== 挂载 v21 ===')
print(run(c, 'cp /mmc/mmca1/v21.so /ram/tmp/v21.so && mount --bind /ram/tmp/v21.so /usr/lib/ezx/lib/libezxam.so.1.0.0', wait=3))
print(run(c, 'ls -la /ram/tmp/v21.so; md5sum /ram/tmp/v21.so', wait=2))
print('=== 清日志, kill am 重生 ===')
print(run(c, ': > /ram/tmp/amreg.log; : > /ram/tmp/trace.txt', wait=1))
print(run(c, 'kill -9 `pidof am`', wait=2))
time.sleep(8)
out = run(c, 'ps ax 2>&1', wait=2)
print(out)
# 找 am pid（匹配 /usr/SYSqtapp/am/am）
import re
am_pid = None
for line in out.splitlines():
    if '/usr/SYSqtapp/am/am' in line:
        m = re.match(r'\s*(\d+)\s', line)
        if m:
            am_pid = m.group(1)
            break
print('am pid =', am_pid)
if not am_pid:
    print('!! am 未重生'); sys.exit(1)

print('=== tracer 附着 am (500s 后台) ===')
print(run(c, 'cat /ram/tmp/trace.txt 2>/dev/null', wait=1))
c._send_line('/mmc/mmca1/tracer %s 500 >/ram/tmp/tracer.log 2>&1 &' % am_pid)
time.sleep(1)

print('=== t_qcop 触发 3 次 ===')
print(run(c, '/mmc/mmca1/t_qcop %s 3' % CALC, wait=70))
