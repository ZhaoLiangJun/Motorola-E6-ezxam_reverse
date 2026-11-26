# -*- coding: utf-8 -*-
"""plt_resolve.py —— 把某个函数的 bl 目标解析成导入符号名（自己解析 .rel.plt，不依赖 am_plt.json）"""
import struct, sys
from capstone import *
from capstone.arm import ARM_OP_IMM

BASE = 0x41ad8000

def secs_and_syms(path):
    d = open(path, 'rb').read()
    e_shoff = struct.unpack_from('<I', d, 0x20)[0]
    ents = struct.unpack_from('<H', d, 0x2E)[0]
    n = struct.unpack_from('<H', d, 0x30)[0]
    secs = [struct.unpack_from('<IIIIIIIIII', d, e_shoff + i * ents) for i in range(n)]
    shstr = secs[struct.unpack_from('<H', d, 0x32)[0]]
    nb = d[shstr[4]:shstr[4] + shstr[5]]
    def nm(x):
        e = nb.find(b'\0', x); return nb[x:e].decode('latin1')
    by = {}
    for s in secs:
        by.setdefault(nm(s[0]), s)
    ds, st = by['.dynsym'], by['.dynstr']
    strb = d[st[4]:st[4] + st[5]]
    syms = []
    for i in range(ds[5] // ds[9]):
        o = ds[4] + i * ds[9]
        name, val, size, info, other, shndx = struct.unpack_from('<IIIBBH', d, o)
        e = strb.find(b'\0', name)
        syms.append((strb[name:e].decode('latin1') if name else '', val, size, shndx))
    return d, by, syms

def pltmap(path):
    d, by, syms = secs_and_syms(path)
    rel = by.get('.rel.plt') or by.get('.rel.dyn')
    plt = by['.plt']
    m = {}
    for i in range(rel[5] // 8):
        off, info = struct.unpack_from('<II', d, rel[4] + i * 8)
        idx = info >> 8
        if idx < len(syms):
            stub = plt[3] + 20 + i * 12
            m[stub] = syms[idx][0]
    return m

if __name__ == '__main__':
    tgt = int(sys.argv[1], 16)
    size = int(sys.argv[2])
    path = sys.argv[3] if len(sys.argv) > 3 else 'orig_local.so'
    m = pltmap(path)
    d = open(path, 'rb').read()
    md = Cs(CS_ARCH_ARM, CS_MODE_ARM); md.detail = True
    for ins in md.disasm(d[tgt - BASE:tgt - BASE + size], tgt):
        if ins.mnemonic in ('bl', 'blx') and ins.operands[0].type == ARM_OP_IMM:
            t = ins.operands[0].imm
            nm = m.get(t)
            print('%08x  %s' % (ins.address, nm if nm else ('sub_%08x' % t)))
