# -*- coding: utf-8 -*-
"""pltdbg.py - 把库里的 bl 目标 / GOT 槽解析成符号名和字符串

用法:
  python pltdbg.py lib.so plt                 列出 PLT 桩地址 -> 符号名
  python pltdbg.py lib.so got  0x41b229f0 0xeb4  读 [sl+off] 指向的字符串
  python pltdbg.py lib.so bl   0x41ae8c64       解析一个 bl 目标
"""
import struct
import sys

SEG = []


def load(p):
    d = open(p, 'rb').read()
    phoff = struct.unpack_from('<I', d, 0x1c)[0]
    phes = struct.unpack_from('<H', d, 0x2a)[0]
    phn = struct.unpack_from('<H', d, 0x2c)[0]
    segs = []
    for i in range(phn):
        t, off, va, pa, fsz, msz, fl, al = struct.unpack_from('<IIIIIIII', d, phoff + i * phes)
        if t == 1:
            segs.append((va, off, fsz))
    shoff = struct.unpack_from('<I', d, 0x20)[0]
    shent = struct.unpack_from('<H', d, 0x2E)[0]
    shn = struct.unpack_from('<H', d, 0x30)[0]
    shs = [struct.unpack_from('<IIIIIIIIII', d, shoff + i * shent) for i in range(shn)]
    shstr = shs[struct.unpack_from('<H', d, 0x32)[0]]
    nb = d[shstr[4]:shstr[4] + shstr[5]]
    by = {}
    for s in shs:
        nm = nb[s[0]:nb.find(b'\0', s[0])].decode('latin1')
        by.setdefault(nm, s)
    return d, segs, by


def rd(d, segs, va, n=4):
    for v, o, s in segs:
        if v <= va < v + s:
            return d[o + va - v:o + va - v + n]
    return None


def dstr(d, segs, va):
    b = rd(d, segs, va, 256)
    if b is None:
        return '<bad %x>' % va
    return b[:b.find(b'\0')].decode('latin1')


def dynsym(by, d):
    ds, st = by['.dynsym'], by['.dynstr']
    strb = d[st[4]:st[4] + st[5]]
    out = []
    for i in range(ds[5] // ds[9]):
        o = ds[4] + i * ds[9]
        name, val, size, info, other, shndx = struct.unpack_from('<IIIBBH', d, o)
        e = strb.find(b'\0', name)
        out.append((strb[name:e].decode('latin1'), val))
    return out


def pltmap(by, d):
    """PLT 桩地址 -> 符号名"""
    syms = dynsym(by, d)
    plt = by.get('.plt')
    rel = by.get('.rel.plt') or by.get('.rel.dyn')
    if not plt or not rel:
        return {}
    base = plt[3]
    out = {}
    n = rel[5] // rel[9]
    for i in range(n):
        o = rel[4] + i * rel[9]
        off, info = struct.unpack_from('<II', d, o)
        typ = info & 0xff
        idx = info >> 8
        if typ != 22:  # R_ARM_JUMP_SLOT
            continue
        name = syms[idx][0]
        out[base + 20 + i * 12] = name
    return out


def main():
    p = sys.argv[1]
    d, segs, by = load(p)
    cmd = sys.argv[2]
    if cmd == 'plt':
        m = pltmap(by, d)
        for a in sorted(m):
            print('%08x  %s' % (a, m[a]))
    elif cmd == 'bl':
        m = pltmap(by, d)
        a = int(sys.argv[3], 0)
        print('%08x  ->  %s' % (a, m.get(a, '<不在 PLT 内>')))
    elif cmd == 'got':
        sl = int(sys.argv[3], 0)
        for spec in sys.argv[4:]:
            mo = int(spec, 0)
            pv = rd(d, segs, sl + mo, 4)
            if pv is None:
                print('%-8s 槽不在段内' % spec)
                continue
            ptr = struct.unpack('<I', pv)[0]
            print('sl+%-8s = %08x -> %08x  "%s"' % (spec, sl + mo, ptr, dstr(d, segs, ptr)))


if __name__ == '__main__':
    main()
