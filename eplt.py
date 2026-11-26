# -*- coding: utf-8 -*-
"""eplt.py - 通用 ELF 的 PLT stub -> 导入符号名映射"""
import struct, sys, os

def _rotimm(w):
    imm8 = w & 0xFF
    rot = ((w >> 8) & 0xF) * 2
    if rot:
        v = (imm8 >> rot) | ((imm8 << (32 - rot)) & 0xFFFFFFFF)
    else:
        v = imm8
    return v & 0xFFFFFFFF

def load(path):
    d = open(path, 'rb').read()
    e_shoff = struct.unpack_from('<I', d, 0x20)[0]
    ents = struct.unpack_from('<H', d, 0x2E)[0]
    n = struct.unpack_from('<H', d, 0x30)[0]
    secs = [struct.unpack_from('<IIIIIIIIII', d, e_shoff + i*ents) for i in range(n)]
    shstr = secs[struct.unpack_from('<H', d, 0x32)[0]]
    nb = d[shstr[4]:shstr[4]+shstr[5]]
    def nm(x):
        e = nb.find(b'\0', x); return nb[x:e].decode('latin1')
    byname = {}
    for i, s in enumerate(secs):
        byname.setdefault(nm(s[0]), s)
    # dynsym
    ds = byname['.dynsym']; st = secs[ds[6]]
    strb = d[st[4]:st[4]+st[5]]
    dynsym = []
    for i in range(ds[5]//ds[9]):
        o = ds[4] + i*ds[9]
        name, val, size, info, other, shndx = struct.unpack_from('<IIIBBH', d, o)
        e = strb.find(b'\0', name)
        dynsym.append((strb[name:e].decode('latin1'), val))
    # .rel.plt
    rp = byname['.rel.plt']
    slots = {}
    for i in range(rp[5]//rp[9]):
        o = rp[4] + i*rp[9]
        r_off, r_info = struct.unpack_from('<II', d, o)
        sym = r_info >> 8
        typ = r_info & 0xFF
        if typ == 22 and sym < len(dynsym):
            slots[r_off] = dynsym[sym][0]
    # scan .plt
    plt = byname['.plt']
    m = {}
    for a in range(plt[3], plt[3] + plt[5] - 11, 4):
        o = plt[4] + (a - plt[3])
        w0, w1, w2 = struct.unpack_from('<III', d, o)
        if (w0 & 0xFFFFF000) != 0xE28FC000:
            continue
        if (w1 & 0xFFFFF000) != 0xE28CC000:
            continue
        if (w2 & 0xFFFFF000) != 0xE5BCF000:
            continue
        stub = a
        ip = (stub + 8) + _rotimm(w0)
        ip = (ip + _rotimm(w1)) & 0xFFFFFFFF
        slot = (ip + (w2 & 0xFFF)) & 0xFFFFFFFF
        if slot in slots:
            m[stub] = slots[slot]
    return d, byname, m

_CACHE = {}

if __name__ == '__main__':
    path = sys.argv[1]
    d, byname, m = load(path)
    print('path', path, 'stubs', len(m))
    text = byname['.text']
    for rng in sys.argv[2:]:
        a, b = rng.split('-')
        a = int(a, 16); b = int(b, 16)
        for addr in range(a, b, 4):
            o = text[4] + (addr - text[3])
            try:
                w = struct.unpack_from('<I', d, o)[0]
            except Exception:
                break
            if (w & 0xFF000000) == 0xEB000000:
                imm = w & 0xFFFFFF
                if imm & 0x800000: imm -= 0x1000000
                tgt = addr + 8 + imm*4
                if tgt in m:
                    print(hex(addr), '->', hex(tgt), m[tgt])
