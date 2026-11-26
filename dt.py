# -*- coding: utf-8 -*-
"""dt.py - 设备命令执行（自动补齐工具链 PATH，避免 $PATH 被本地 shell 改写）
  python dt.py "objdump -d xxx | head -20"
  from dt import run; run(c, "readelf -sW ...")
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dev

TC = '/mmc/mmca1/.system/arm-linux-gcc/bin'
BASE = '/bin:/usr/bin:/sbin:/usr/sbin:/usr/local/bin'
TLIB = '/mmc/mmca1/.system/arm-linux-gcc/lib'
PREFIX = ('export PATH=%s:%s; export LD_LIBRARY_PATH=%s:/usr/lib:/lib; '
          % (TC, BASE, TLIB))

_c = None


def conn():
    global _c
    if _c is None:
        _c = dev.connect()
    return _c


def run(cmd, wait=3):
    return dev.run(conn(), PREFIX + cmd, wait).replace('\r', '')


def sh(local_script, wait=20):
    """把本地 .sh 推上设备并执行，适合复杂引号/循环"""
    import pushh
    name = os.path.basename(local_script)
    dst = '/ram/tmp/' + name
    sys.argv = ['pushh.py', local_script, dst, '755']
    pushh.main()
    return run('sh ' + dst, wait)


if __name__ == '__main__':
    print(run(' '.join(sys.argv[1:]), 20))
