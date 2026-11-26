# -*- coding: utf-8 -*-
# 对比 t_qcop 与 calculator 的 ELF ABI 标志
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dev import connect, run

T = '/mmc/mmca1/.system/arm-linux-gcc/bin'
c = connect()
print('=== t_qcop ===')
print(run(c, T + '/readelf -A /mmc/mmca1/t_qcop', wait=3))
print('=== calculator ===')
print(run(c, T + '/readelf -A /usr/SYSqtapp/calculator/calculator', wait=3))
