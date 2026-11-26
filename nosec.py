# -*- coding: utf-8 -*-
"""nosec.py —— 证书校验临时开关（v45）

用法:
  python nosec.py on       # 打开旁路（touch /ram/tmp/am_nosec），立即生效
  python nosec.py off      # 关闭旁路（rm），立即生效
  python nosec.py status   # 看当前状态 + 最近几行 [nosec] 日志

背景：AM_SecurityManager 里 verifySecurityByLaunch / verifySecurityByInstall /
      verifySecurity 三个入口在开头检查该文件，存在就直接返回 0（放行）。
      校验代码本身一行没删，文件不存在时行为与 v44 完全一致。
      /ram 是 ramfs，重启自动清掉 —— 忘了关也不会一直裸奔。
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dev import connect, run

T = '/mmc/mmca1/.system/arm-linux-gcc/bin'
FLAG = '/ram/tmp/am_nosec'

cmd = (sys.argv[1] if len(sys.argv) > 1 else 'status').lower()

c = connect()


def rd(x, w=1.8):
    return run(c, x, w).replace('\r', '')


if cmd == 'on':
    print(rd('touch %s; ls -l %s' % (FLAG, FLAG)))
elif cmd == 'off':
    print(rd('rm -f %s; ls -l %s 2>&1' % (FLAG, FLAG)))
elif cmd in ('status', 'st'):
    print('=== 开关 ===')
    print(rd('ls -l %s 2>&1' % FLAG))
    print('=== 库版本 / md5 ===')
    print(rd(T + '/md5sum /usr/lib/ezx/lib/libezxam.so.1.0.0', 2.0))
    print(rd(T + '/egrep "BOOT v4" /ram/tmp/amreg.log', 2.0))
    print('=== 最近 8 条 nosec 日志 ===')
    out = rd(T + '/egrep "nosec" /ram/tmp/amreg.log', 2.5)
    for line in [l for l in out.splitlines() if l.strip() and l.strip() != '#'][-8:]:
        print('   ', line)
    print('=== 最近 8 条校验日志（未旁路时会出现在这里）===')
    out = rd(T + '/egrep "verifySecurity|parseSignedFile" /ram/tmp/amreg.log', 2.5)
    for line in [l for l in out.splitlines() if l.strip() and l.strip() != '#'][-8:]:
        print('   ', line)
else:
    print('用法: python nosec.py on|off|status')
