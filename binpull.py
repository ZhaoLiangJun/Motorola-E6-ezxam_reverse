# -*- coding: utf-8 -*-
"""从设备拉二进制文件（od 十六进制，分块）
用法: python binpull.py <远端路径> <本地路径>
"""
import sys, os, re, binascii
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dev import connect, run

T = '/mmc/mmca1/.system/arm-linux-gcc/bin'

def pull(c, path, total, chunk=16384):
    parts = []
    pos = 0
    while pos < total:
        ln = min(chunk, total - pos)
        o = run(c, '%s/od -v -j %d -N %d -An -tx1 -w64 %s' % (T, pos, ln, path),
                wait=max(4.0, ln / 1500.0))
        o = o.replace('\r', '')
        # 去掉提示符残留与所有空白
        h = ''.join(o.split())
        h = re.sub(r'[^0-9a-fA-F]', '', h)
        parts.append(h)
        pos += ln
        sys.stderr.write('\r%6d/%6d' % (pos, total)); sys.stderr.flush()
    h = ''.join(parts)
    sys.stderr.write('\n')
    return binascii.unhexlify(h[:len(h)//2*2])

def main():
    rpath, lpath = sys.argv[1], sys.argv[2]
    c = connect()
    o = run(c, 'ls -l %s' % rpath, 1.8).replace('\r', '')
    m = re.match(r'\S+\s+\d+\s+\S+\s+\S+\s+(\d+)\s', o.strip())
    if not m:
        print('无法取大小, 原始输出:\n%r' % o); return
    size = int(m.group(1))
    print('remote size =', size)
    data = pull(c, rpath, size)
    open(lpath, 'wb').write(data)
    print('wrote %d bytes -> %s' % (len(data), lpath))

main()
