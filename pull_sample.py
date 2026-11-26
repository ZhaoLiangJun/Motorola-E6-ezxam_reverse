# -*- coding: utf-8 -*-
# 拉取采样轨迹+日志并分类 PC
import sys, os, re
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dev import connect, run

c = connect()
print(run(c, 'kill -9 $(pidof sampler) 2>/dev/null', wait=1))
out = run(c, 'ls -la /ram/tmp/sample.txt /ram/tmp/amreg.log', wait=2)
print(out)
# 采样文件可能很大，用 od 分块拉
size = 0
for line in out.splitlines():
    if 'sample.txt' in line:
        m = re.match(r'\S+\s+\d+\s+\S+\s+\S+\s+(\d+)', line.strip())
        if m:
            size = int(m.group(1))
            break
print('sample size:', size)

def pull(path, total):
    T = '/mmc/mmca1/.system/arm-linux-gcc/bin'
    parts = []
    chunk = 100000
    pos = 0
    while pos < total:
        ln = min(chunk, total - pos)
        o = run(c, T + '/od -v -j %d -N %d -An -tx1 -w60 %s' % (pos, ln, path), wait=10)
        o = o.replace('# ', '')
        parts.append(''.join(o.split()))
        pos += ln
    import binascii
    h = ''.join(parts)
    return binascii.unhexlify(h[:len(h)//2*2]).decode('utf-8', 'replace')

if size:
    data = pull('/ram/tmp/sample.txt', size)
    open('sample.txt', 'w').write(data)
    print('pulled', len(data), 'bytes')
