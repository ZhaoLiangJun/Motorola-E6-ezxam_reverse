# -*- coding: utf-8 -*-
"""calldiff.py - 提取原库函数体内的直接调用序列（BL -> 符号名）
用法: python calldiff.py <funcname_substr> ...
"""
import sys, os, json, struct, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pltmap

PATH = '_origlib/libezxam.so.1'
f, segs, byname, stubmap = pltmap.build(PATH)

symvals = json.load(open('orig_symvals.json'))
addr2name = {}
for k, v in symvals.items():
    if isinstance(v, int) and v:
        addr2name.setdefault(v, k)

# 函数边界：按地址排序
funcs = sorted(set(v for k, v in symvals.items() if isinstance(v, int) and v))
def next_func(a):
    import bisect
    i = bisect.bisect_right(funcs, a)
    return funcs[i] if i < len(funcs) else a + 0x8000

def read(va, sz):
    for (v, o, s) in segs:
        if v <= va < v + s:
            return f[va - v + o: va - v + o + sz]
    return b''

def calls_in(va, sz):
    """返回 [(call_addr, target_addr, name)]，跳过 PLT 桩之外的仅记录名字"""
    code = read(va, sz)
    out = []
    for p in range(0, len(code) - 3, 4):
        w = struct.unpack('<I', code[p:p+4])[0]
        if (w >> 24) & 0xF == 0xB:           # BL
            imm = w & 0xFFFFFF
            if imm & 0x800000: imm -= 0x1000000
            tgt = va + p + 8 + imm * 4
            nm = stubmap.get(tgt) or addr2name.get(tgt)
            if nm:
                nm = pltmap.demangle(nm)
            else:
                nm = 'sub_%08x' % tgt
            out.append((va + p, tgt, nm))
    return out

for arg in sys.argv[1:]:
    matches = [k for k in symvals if arg in k and isinstance(symvals[k], int) and symvals[k]]
    if not matches:
        print('!! no symbol matching %r' % arg); continue
    for k in matches:
        va = symvals[k]
        end = next_func(va)
        sz = min(end - va, 0x8000)
        cs = calls_in(va, sz)
        print('=' * 78)
        print('%s   @0x%08x  span=%d  calls=%d' % (pltmap.demangle(k), va, end - va, len(cs)))
        print('=' * 78)
        for (a, t, n) in cs:
            print('  0x%08x  %s' % (a, n))
