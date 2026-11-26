# -*- coding: utf-8 -*-
"""missym.py - 三方符号比对：原库导出 / 我们导出 / 消费者(am 等)需要
用法: python missym.py [consumer...]
"""
import subprocess, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
RE = 'D:/cygwin/home/ezx/arm-linux-gcc/bin/arm-linux-readelf.exe'
ENV = dict(os.environ, PATH='D:/cygwin/bin;' + os.environ.get('PATH', ''))
ORIG = os.path.join(HERE, 'orig_local.so')
MINE = os.path.join(HERE, 'libezxam_reconstructed.so.1.0.0')


def syms(path):
    out = subprocess.run([RE, '-sW', path], capture_output=True, text=True, env=ENV).stdout
    res = []
    for line in out.splitlines():
        p = line.split()
        if len(p) >= 8 and p[0].endswith(':'):
            try:
                int(p[1], 16)
            except ValueError:
                continue
            res.append((p[7], p[3], p[4], p[6]))
    return res


def exported(path):
    return {n for n, t, b, nd in syms(path) if t in ('FUNC', 'OBJECT') and nd != 'UND'}


def main():
    consumers = sys.argv[1:] or ['am_dev']
    o, m = exported(ORIG), exported(MINE)
    print('orig export=%d  mine export=%d' % (len(o), len(m)))
    print('\n== 原库有、我们没有（%d） ==' % len(o - m))
    for n in sorted(o - m):
        print('  -', n)
    print('\n== 我们有、原库没有（%d） ==' % len(m - o))
    for n in sorted(m - o):
        print('  +', n)
    for c in consumers:
        need = {n for n, t, b, nd in syms(os.path.join(HERE, c)) if nd == 'UND' and t == 'FUNC'}
        need &= o          # 只关心本库该提供的
        miss = sorted(need - m)
        print('\n== %s 需要本库 %d 个, 缺 %d ==' % (c, len(need), len(miss)))
        for n in miss:
            print('  MISS', n)


if __name__ == '__main__':
    main()
