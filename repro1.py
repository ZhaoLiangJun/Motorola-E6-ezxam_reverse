# -*- coding: utf-8 -*-
# 自主复现：重启 -> 挂载v21 -> tracer -> t_qcop 触发3次计算器启动 -> 收集证据
import sys, os, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dev import connect, run

c = connect()

print('=== 1) 找计算器 UUID ===')
out = run(c, 'grep -B2 -A6 -i calculator /usr/data_resource/preloadapps/SysRegistry', wait=4)
print(out[:1500])

print('=== 2) 重启设备 ===')
print(run(c, 'reboot', wait=2))
time.sleep(8)
c = None
time.sleep(45)

print('=== 3) 重连 ===')
c = connect()
print(run(c, 'ps', wait=2))
