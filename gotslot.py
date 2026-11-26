# -*- coding: utf-8 -*-
"""gotslot.py - 打印 GOT 槽内容（含二级间接），用于解析 dis2 未跟进的 [sl+rX] 二次取值

用法: python gotslot.py 0x1138 0xd80 0xd84 ...
      偏移是相对 GOT 基址(默认 0x41b229f0)的字节偏移
"""
import sys, os, struct, bisect, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pltmap

PATH = '_origlib/libezxam.so.1'
f, segs, byname, stubmap = pltmap.build(PATH)

sv = json.load(open('orig_symvals.json'))
addr2name = {}
for k, v in sv.items():
    if isinstance(v, int) and v:
        addr2name.setdefault(v, k)
funcs = sorted(addr2name)

GOT = 0x41b229f0


def rd(va, n):
    for (v, o, s) in segs:
        if v <= va < v + s:
            return f[va - v + o: va - v + o + n]
    return b''


def u32(va):
    b = rd(va, 4)
    return struct.unpack('<I', b)[0] if len(b) == 4 else None


def cstr(va, lim=120):
    b = rd(va, lim)
    if not b:
        return None
    s = b.split(b'\x00')[0]
    if not s:
        return None
    if any(not (32 <= c < 127) for c in s):
        return None
    return s.decode('latin1')


def symat(a):
    if a in addr2name:
        return pltmap.demangle(addr2name[a])
    i = bisect.bisect_right(funcs, a)
    if i:
        base = funcs[i - 1]
        if a - base < 0x600:
            return '%s+%#x' % (pltmap.demangle(addr2name[base]), a - base)
    return None


def describe(v):
    if v is None:
        return '?'
    s = cstr(v)
    if s:
        return 'str %r' % s
    n = symat(v)
    if n:
        return 'sym <%s>' % n
    return '%#010x' % v


for arg in sys.argv[1:]:
    off = int(arg, 0)
    slot = GOT + off
    v = u32(slot)
    print('GOT[%#x] @%#x = %s' % (off, slot, describe(v)))
    # 二级：若指向库内可读区再取一次
    if v and cstr(v) is None:
        v2 = u32(v)
        if v2:
            print('         -> [%#x] = %s' % (v, describe(v2)))
