# -*- coding: utf-8 -*-
# 查 am 的 maps（0x4112bb78 属于哪个库）+ wchan + 栈
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dev import connect, run

T = '/mmc/mmca1/.system/arm-linux-gcc/bin'
c = connect()
out = run(c, 'cat /proc/244/maps', wait=3)
print('--- around 0x4112bb78:')
for l in out.splitlines():
    a, b = l.split('-')[0], l.split('-')[1].split(' ')[0]
    if int(a, 16) <= 0x4112bb78 < int(b, 16):
        print(l)
print('--- 0x4113da24:')
for l in out.splitlines():
    a, b = l.split('-')[0], l.split('-')[1].split(' ')[0]
    if int(a, 16) <= 0x4113da24 < int(b, 16):
        print(l)
print('--- wchan:')
print(run(c, 'cat /proc/244/wchan 2>&1; echo; cat /proc/246/wchan 2>&1', wait=2))
print('--- status:')
print(run(c, 'cat /proc/244/status | ' + T + '/head -12', wait=2))
