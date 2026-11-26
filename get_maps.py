# -*- coding: utf-8 -*-
# 启动 sampler 附着 am，供用户点击时抓 PC 轨迹；并导出 am 的映射
import sys, os, re, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dev import connect, run

T = '/mmc/mmca1/.system/arm-linux-gcc/bin'
c = connect()
print(run(c, 'PATH=' + T + ':$PATH cat /proc/244/maps', wait=3))
