# -*- coding: utf-8 -*-
# 直接拉 libc.gz（非管道）解析
import sys, os, re, binascii, gzip
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dev import connect, run

T = '/mmc/mmca1/.system/arm-linux-gcc/bin'
c = connect()
print(run(c, T + '/gzip -9 -c /lib/libc-2.3.2.so > /ram/tmp/libc.gz; sleep 1', wait=8))
out = run(c, T + '/stat -c %s /ram/tmp/libc.gz', wait=3)
size = int(re.search(r'(\d+)', out).group(1))
print('libc.gz size:', size)

parts = []
chunk = 60000
pos = 0
ok = True
while pos < size:
    ln = min(chunk, size - pos)
    expect = 2 * ln
    for attempt in range(4):
        o = run(c, T + '/od -v -j %d -N %d -An -tx1 -w60 /ram/tmp/libc.gz' % (pos, ln), wait=12)
        o = o.replace('# ', '')
        s = ''.join(o.split())
        if len(s) >= expect:
            parts.append(s[:expect])
            break
        print('retry chunk @%d: got %d expect %d' % (pos, len(s), expect))
    else:
        ok = False
        break
    pos += ln
if not ok:
    print('PULL FAILED')
    sys.exit(1)
h = ''.join(parts)
raw = binascii.unhexlify(h[:len(h)//2*2])
libc = gzip.decompress(raw)
open('libc_dev.so', 'wb').write(libc)
print('libc OK:', len(libc))
