# -*- coding: utf-8 -*-
# 用 stat 取 libc.hex 大小并拉取解析阻塞符号
import sys, os, re, binascii, gzip
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dev import connect, run

T = '/mmc/mmca1/.system/arm-linux-gcc/bin'
c = connect()
out = run(c, T + '/stat -c %s /ram/tmp/libc.hex', wait=3)
print('stat out:', repr(out))
size = int(re.search(r'(\d+)', out).group(1))
print('size:', size)

parts = []
chunk = 60000
pos = 0
while pos < size:
    ln = min(chunk, size - pos)
    expect = 2 * ln
    for attempt in range(4):
        o = run(c, T + '/od -v -j %d -N %d -An -tx1 -w60 /ram/tmp/libc.hex' % (pos, ln), wait=12)
        o = o.replace('# ', '')
        s = ''.join(o.split())
        if len(s) >= expect:
            parts.append(s[:expect])
            break
        print('retry chunk @%d: got %d expect %d' % (pos, len(s), expect))
    else:
        print('FAILED chunk @%d' % pos)
        break
    pos += ln
h = ''.join(parts)
print('collected hex:', len(h), 'expect:', size*2)
text = binascii.unhexlify(h[:len(h)//2*2]).decode('ascii')
raw = binascii.unhexlify(text[:len(text)//2*2])
open('libc_dev.gz', 'wb').write(raw)
libc = gzip.decompress(raw) if False else None
import gzip
libc = gzip.decompress(raw)
open('libc_dev.so', 'wb').write(libc)
print('libc pulled:', len(libc))
