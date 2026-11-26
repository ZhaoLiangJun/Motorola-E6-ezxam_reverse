# -*- coding: utf-8 -*-
"""pk.py - 对 am 进程调用设备端 peek 的便捷封装
  python pk.py dump 0x8fd88 32
  python pk.py find 0x8000 0x100000 0x38b4c
  python pk.py poke 0x8fd88 0
连接可复用: from pk import peek, pid
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dev

_c = None


def conn():
    global _c
    if _c is None:
        _c = dev.connect()
    return _c


def pid(refresh=False):
    out = dev.run(conn(), 'pidof am', 1.5)
    for tok in out.split():
        if tok.isdigit():
            return int(tok)
    return None


def peek(*args, wait=4):
    p = pid()
    if p is None:
        return 'NO AM'
    cmd = '/ram/tmp/pktrace %d %s' % (p, ' '.join(str(a) for a in args))
    return dev.run(conn(), cmd, wait)


def sh(cmd, wait=2):
    return dev.run(conn(), cmd, wait)


if __name__ == '__main__':
    if len(sys.argv) < 2:
        print(__doc__); sys.exit(0)
    print(peek(*sys.argv[1:]))


# ---------- 高层读取 ----------
import re


def rd(addr, n):
    """ptrace 读内存，返回 bytes（失败处截断）"""
    out = peek('dump', hex(addr), n, wait=3)
    data = bytearray()
    for line in out.splitlines():
        line = line.strip().lstrip('#').strip()
        if '|' not in line:
            continue
        head = line.split('|')[0].split()
        if not head or not re.fullmatch(r'[0-9a-f]{8}', head[0]):
            continue
        base = int(head[0], 16)
        toks = [t for t in head[1:] if re.fullmatch(r'[0-9a-f]{2}', t)]
        if base != addr + len(data):
            continue
        data += bytes(int(t, 16) for t in toks)
    return bytes(data[:n])


def qstring(addr):
    """读 QString：addr 处是 d 指针；返回 (d, 字符串)"""
    b = rd(addr, 4)
    if len(b) < 4:
        return None, '<unreadable>'
    d = int.from_bytes(b[:4], 'little')
    info = {'d': d}
    if d == 0:
        return d, '<null>'
    h = rd(d, 20)
    if len(h) < 20:
        return d, '<bad d %#x>' % d
    cnt = int.from_bytes(h[0:4], 'little')
    uni = int.from_bytes(h[4:8], 'little')
    asc = int.from_bytes(h[8:12], 'little')
    ln = int.from_bytes(h[12:16], 'little')
    info.update(count=cnt, unicode=uni, ascii=asc, len=ln)
    s = ''
    if uni and 0 < ln < 300:
        raw = rd(uni, ln * 2)
        s = raw.decode('utf-16-le', 'replace')
    elif asc and 0 < ln < 300:
        s = rd(asc, ln).decode('latin1', 'replace')
    return d, s


_LOCAL = None


def _local_bin():
    global _LOCAL
    if _LOCAL is None:
        _LOCAL = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'am_dev'), 'rb').read()
    return _LOCAL


def rostr(v, maxlen=80):
    """从本地 am_dev 读只读段字符串（vaddr -> 文件偏移：text/rodata -0x8000，data -0x10000）"""
    if not v or v < 0x8000:
        return ''
    off = v - 0x8000 if v < 0x8e000 else v - 0x10000
    d = _local_bin()
    if off < 0 or off >= len(d):
        return ''
    e = d.find(b'\x00', off, off + maxlen)
    if e < 0:
        return ''
    try:
        return d[off:e].decode('latin1')
    except Exception:
        return ''
