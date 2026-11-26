# -*- coding: utf-8 -*-
"""instlog.py —— 安装/运行链路的日志速查（v46 起）

用法:
  python instlog.py            # 摘要：BOOT / CRASH / 校验 / 安装链路
  python instlog.py <pid>      # 额外打印某个 pid 的 maps（崩溃后反查模块基址用）
  python instlog.py clean      # 清空日志（换版本前先清，免得旧记录混在一起）

为什么要有它：设备上 grep/tail/head 全都缺 DLL（exit 127），只有 egrep/sed 能跑，
所以过滤在设备侧用 egrep，切片在 PC 侧做（避免依赖 tail）。
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dev import connect, run

T = '/mmc/mmca1/.system/arm-linux-gcc/bin'
LOG = '/ram/tmp/amreg.log'

arg = sys.argv[1] if len(sys.argv) > 1 else ''

c = connect()


def rd(x, w=2.5):
    return run(c, x, w).replace('\r', '')


def lines(pat, n=25):
    out = rd('%s/egrep "%s" %s' % (T, pat, LOG), 3.0)
    ls = [l for l in out.splitlines() if l.strip() and l.strip() != '#']
    return ls[-n:]


if arg == 'clean':
    print(rd('rm -f %s; ls -l %s 2>&1' % (LOG, LOG)))
    print(rd('rm -f /ram/tmp/am_maps_*.txt; echo maps-cleared'))
elif arg:
    print('=== maps for pid %s ===' % arg)
    print(rd('cat /ram/tmp/am_maps_%s.txt 2>&1' % arg, 4))
else:
    print('=== 库版本 / 进程 ===')
    print(rd('%s/md5sum /usr/lib/ezx/lib/libezxam.so.1.0.0' % T))
    print('=== BOOT 行（每行=一次进程启动）===')
    for l in lines('BOOT v4'):
        print('   ', l)
    print('=== CRASH（任何进程崩了都在这里，含 pid/comm）===')
    for l in lines('CRASH|==== *CRASH', 12):
        print('   ', l)
    print('=== 证书校验 ===')
    for l in lines('allow\\]|verifySecurity|parseSignedFile', 20):
        print('   ', l)
    print('=== 安装链路 ===')
    for l in lines('RMCtor|addAppToMainMenu|addAppToFolder|changeFolder|getAppParentFolderUid|slotAppInstalled|qcop|addAppToAutoRun|notifyAdd', 30):
        print('   ', l)
    print('=== /ram/tmp 里的 maps（崩溃过的 pid）===')
    print(rd('ls -l /ram/tmp/am_maps_*.txt 2>&1', 2))
