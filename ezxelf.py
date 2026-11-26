# -*- coding: utf-8 -*-
"""ezxelf.py - 纯 Python ELF 动态符号表读取（设备/PC 的 nm 都不可用时用）
用法: python ezxelf.py <elf> [符号过滤子串]
"""
import sys, struct


def load(path):
    d = open(path, 'rb').read()
    if d[:4] != b'\x7fELF':
        raise SystemExit('not ELF: ' + path)
    shoff = struct.unpack('<I', d[0x20:0x24])[0]
    shentsize, shnum, shstrndx = struct.unpack('<HHH', d[0x2E:0x34])
    secs = []
    for i in range(shnum):
        o = shoff + i * shentsize
        secs.append(struct.unpack('<IIIIIIIIII', d[o:o + 40]))
    return d, secs


def dynsyms(path):
    d, secs = load(path)
    out = []
    for s in secs:
        typ = s[1]
        if typ != 11:          # SHT_DYNSYM
            continue
        _, _, _, off, size, link = s[0], s[1], s[2], s[4], s[5], s[6]
        strsec = secs[link]
        stroff, strsz = strsec[4], strsec[5]
        b = d[stroff:stroff + strsz]
        for i in range(size // 16):
            p = off + i * 16
            nm, val, sz, info, other, shndx = struct.unpack('<IIIBBH', d[p:p + 16])
            e = b.find(b'\x00', nm)
            name = b[nm:e].decode('ascii', 'replace')
            out.append((name, val, sz, shndx, info))
    return out


def main():
    path = sys.argv[1]
    pat = sys.argv[2] if len(sys.argv) > 2 else ''
    syms = dynsyms(path)
    sel = [s for s in syms if pat in s[0]]
    print('%s: total dynsym=%d, matched=%d' % (path, len(syms), len(sel)))
    for name, val, sz, shndx, info in sel:
        kind = 'UND' if shndx == 0 else 'DEF'
        print('  %-3s %08x %5d  %s' % (kind, val, sz, name))


main()
