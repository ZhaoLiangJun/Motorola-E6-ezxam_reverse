# -*- coding: utf-8 -*-
# 直接分块拉取 /lib/libc-2.3.2.so 的 hex（不经 /ram）
import sys, os, re, binascii
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dev import connect, run

T = '/mmc/mmca1/.system/arm-linux-gcc/bin'
c = connect()
out = run(c, T + '/stat -c %s /lib/libc-2.3.2.so', wait=3)
size = int(re.search(r'(\d+)', out).group(1))
print('libc size:', size)

parts = []
chunk = 40000
pos = 0
ok = True
while pos < size:
    ln = min(chunk, size - pos)
    expect = 2 * ln
    done = False
    for attempt in range(4):
        o = run(c, T + '/od -v -j %d -N %d -An -tx1 -w60 /lib/libc-2.3.2.so' % (pos, ln), wait=10)
        o = o.replace('# ', '')
        s = ''.join(o.split())
        if len(s) >= expect:
            parts.append(s[:expect])
            done = True
            break
        print('retry @%d got %d/%d' % (pos, len(s), expect))
    if not done:
        print('FAILED @', pos)
        ok = False
        break
    pos += ln
if ok:
    h = ''.join(parts)
    raw = binascii.unhexlify(h)
    open('libc_dev.so', 'wb').write(raw)
    print('libc OK:', len(raw))
