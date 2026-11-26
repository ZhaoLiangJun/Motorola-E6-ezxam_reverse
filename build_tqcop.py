# -*- coding: utf-8 -*-
# 在设备上编译 t_qcop 并部署
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dev import connect, run

T = '/mmc/mmca1/.system/arm-linux-gcc/bin'
c = connect()
cmd = ("PATH=" + T + ":$PATH LD_LIBRARY_PATH=/mmc/mmca1/.system/arm-linux-gcc/lib "
       + T + "/g++ -B" + T + "/ -O1 -o /mmc/mmca1/t_qcop /mmc/mmca1/t_qcop.cpp "
       "-I" + T + "/include -I" + T + "/include/c++/3.3.1 -I" + T + "/include/c++/3.3.1/arm-linux "
       "-L/usr/lib/ezx/lib -lqte-mt -lEZXappbase "
       "-Wl,--unresolved-symbols=ignore-all -Wl,-rpath,/usr/lib/ezx/lib 2>&1")
print(run(c, cmd, wait=60))
print(run(c, 'ls -la /mmc/mmca1/t_qcop', wait=2))
