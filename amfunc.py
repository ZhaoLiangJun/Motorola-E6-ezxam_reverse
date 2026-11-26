# -*- coding: utf-8 -*-
# 定位 am 中 0x33224(doLaunch调用) 所在函数并完整反汇编
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import amxref, struct
from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM, CS_MODE_THUMB

if __name__ == '__main__':
    f, segs = amxref.parse()
m, secs = amxref.plt_map(f, segs)
plt = secs['.plt']
n = (plt['size']-0x14)//16
stubmap = {}
for i in range(n):
    stub = plt['addr'] + 0x14 + i*16
    w1, w2, w3 = struct.unpack('<3I', amxref.read_at(f, segs, stub, 12))
    if (w2>>25)&1 != 1 or (w2>>20)&0x1f != 8: continue
    if (w2>>12)&0xf != 12 or (w2>>16)&0xf != 12: continue
    rot = ((w2>>8)&0xf)*2; imm = w2&0xff
    val = ((imm >> rot) | (imm << (32-rot))) & 0xffffffff if rot else imm
    if (w3 & 0x0f700000) != 0x05300000 or (w3>>16)&0xf != 12: continue
    U = (w3>>23)&1; off = w3 & 0xfff
    got = stub + 8 + (val + off if U else val - off)
    if got in m: stubmap[stub] = m[got]
print('stubmap:', len(stubmap))

def find_start(addr, back=0x3000):
    md = Cs(CS_ARCH_ARM, CS_MODE_ARM); md.skipdata = True
    code = amxref.read_at(f, segs, addr-back, back)
    last = None
    for i in md.disasm(code, addr-back):
        if i.address >= addr: break
        if i.mnemonic == 'push' and 'lr' in i.op_str:
            last = i.address
    return last

start = find_start(0x33224)
print('function start:', hex(start) if start else 'NOT FOUND')

if start:
    md = Cs(CS_ARCH_ARM, CS_MODE_ARM); md.skipdata = True
    code = amxref.read_at(f, segs, start, 0x33224+0x180-start)
    for i in md.disasm(code, start):
        ann = ''
        if i.mnemonic == 'bl':
            try:
                tgt = int(i.op_str.lstrip('#'), 16)
                if tgt in stubmap: ann = '  ; <%s>' % stubmap[tgt]
            except Exception: pass
        print('%08x  %-8s %-36s%s' % (i.address, i.mnemonic, i.op_str, ann))
