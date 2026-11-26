# -*- coding: utf-8 -*-
# 设备端诊断 g++ include 搜索（路径修正版）
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dev import connect, run

BASE = '/mmc/mmca1/.system/arm-linux-gcc'
BIN = BASE + '/bin'
c = connect()
cmd = ("echo 'int main(){return 0;}' > /ram/tmp/t1.cpp && PATH=" + BIN + ":$PATH "
       "LD_LIBRARY_PATH=" + BASE + "/lib " + BIN + "/g++ -B" + BIN + "/ "
       "-v -o /ram/tmp/t1 /ram/tmp/t1.cpp 2>&1")
out = run(c, cmd, wait=40)
print(out[-2200:])
