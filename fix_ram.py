# -*- coding: utf-8 -*-
# 检查 /ram 空间并清理，重新生成 libc.hex
import sys, os, re, binascii, gzip
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dev import connect, run

T = '/mmc/mmca1/.system/arm-linux-gcc/bin'
c = connect()
print(run(c, 'df /ram /tmp; ls /ram/tmp/', wait=3))
print(run(c, 'rm -f /ram/tmp/am.hex /ram/tmp/kmsg.txt /ram/tmp/t1 /ram/tmp/t1.cpp', wait=2))
print(run(c, T + '/gzip -9 -c /lib/libc-2.3.2.so > /ram/tmp/libc.gz; ls -la /ram/tmp/libc.gz', wait=6))
