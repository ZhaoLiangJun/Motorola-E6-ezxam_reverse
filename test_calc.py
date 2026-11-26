# -*- coding: utf-8 -*-
# 决定性试验：telnet 直接跑 calculator，验证 Qt 客户端环境
import sys, os, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dev import connect, run

T = '/mmc/mmca1/.system/arm-linux-gcc/bin'
c = connect()
env = ('. /etc/init.d/ezxenvr.sh; export QWS_DISPLAY=:0 QTDIR=/usr/lib/ezx HOME=/ '
       'PATH=' + T + ':$PATH LD_LIBRARY_PATH=/usr/lib/ezx/lib; ')
# 后台启动 calculator
c._send_line(env + '/usr/SYSqtapp/calculator/calculator >/ram/tmp/calc.out 2>&1 &')
time.sleep(4)
out = run(c, 'ps ax 2>&1', wait=2)
print([l for l in out.splitlines() if 'calc' in l])
print(run(c, 'cat /ram/tmp/calc.out', wait=2))
# 清理
run(c, 'kill -9 $(pidof calculator) 2>/dev/null', wait=1)
