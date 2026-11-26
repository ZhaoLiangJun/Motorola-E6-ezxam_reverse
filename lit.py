# -*- coding: utf-8 -*-
"""lit.py - 列出某个函数里所有 GOT 字面量（按地址顺序），用于还原常量
用法: python lit.py <函数名子串|vaddr> [长度]
"""
import sys, os, json, struct, bisect
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pltmap
from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM

PATH = '_origlib/libezxam.so.1'
f, segs, byname, stubmap = pltmap.build(PATH)
sv = json.load(open('orig_symvals.json'))

addr2name = {}
for k, v in sv.items():
    if isinstance(v, int) and v:
        addr2name.setdefault(v, k)
funcs = sorted(addr2name)


def rd(va, n):
    for (v, o, s) in segs:
        if v <= va < v + s:
            return f[va - v + o: va - v + o + n]
    return b''


def cstr(va, lim=120):
    return rd(va, lim).split(b'\x00')[0].decode('latin1')


arg = sys.argv[1]
if arg.startswith('0x'):
    base = int(arg, 16)
    i = bisect.bisect_right(funcs, base)
    size = int(sys.argv[2], 0) if len(sys.argv) > 2 else (funcs[i] - base if i < len(funcs) else 0x2000)
else:
    hits = [(a, n) for a, n in addr2name.items() if arg in n]
    if not hits:
        raise SystemExit('no symbol matches ' + arg)
    base, name = sorted(hits)[0]
    i = bisect.bisect_right(funcs, base)
    size = (funcs[i] - base) if i < len(funcs) else 0x2000

print('func %s @%#x size=%d' % (addr2name.get(base, '?'), base, size))

code = rd(base, size)
md = Cs(CS_ARCH_ARM, CS_MODE_ARM)
md.skipdata = True

# GOT 基址按 prologue 的 add sl,pc,#off 计算（本库统一用 sl/sb/fp 之一）
got = None
ins = list(md.disasm(code, base))
# 找 ldr X,[pc,#imm] 后紧跟 add X,pc,X 的模式
for k in range(len(ins) - 1):
    a = ins[k]
    if a.mnemonic == 'ldr' and '[pc' in a.op_str and '#' in a.op_str:
        try:
            imm = int(a.op_str.split('#')[1].rstrip(']'), 16)
        except Exception:
            continue
        lit = a.address + 8 + imm
        v = struct.unpack('<I', rd(lit, 4))[0]
        nxt = ins[k + 1]
        if nxt.mnemonic == 'add' and nxt.op_str.startswith(a.op_str.split(',')[0]) and 'pc' in nxt.op_str:
            got = (nxt.address + 8 + v) & 0xFFFFFFFF
            print('  GOT base = %#x  (via %#x)' % (got, a.address))
            break

if got is None:
    got = 0x41b229f0
    print('  GOT base = %#x (default)' % got)

for a in ins:
    if a.mnemonic == 'ldr' and '[pc' in a.op_str and ',' in a.op_str:
        try:
            imm = int(a.op_str.split('#')[1].rstrip(']'), 16)
        except Exception:
            continue
        v = struct.unpack('<I', rd(a.address + 8 + imm, 4))[0]
        if v < 0x1000:      # 小值 = GOT 偏移
            p = struct.unpack('<I', rd((got + v) & 0xFFFFFFFF, 4))[0]
            s = cstr(p) if p else ''
            if s and all(32 <= ord(c) < 127 for c in s):
                print('  %#x  got[%#x] -> %r' % (a.address, v, s))
        elif 0x40000000 < v < 0x42000000:
            s = cstr(v)
            if s and all(32 <= ord(c) < 127 for c in s):
                print('  %#x  direct -> %r' % (a.address, s))
