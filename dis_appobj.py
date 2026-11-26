# -*- coding: utf-8 -*-
# 反汇编 0x3cb4c / 0x3cbc4 附近（getAppObjById 调用后的校验链）
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import amxref
from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM

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

def dis(addr, size, title):
    print('====', title, '====')
    md = Cs(CS_ARCH_ARM, CS_MODE_ARM); md.skipdata = True
    code = amxref.read_at(f, segs, addr, size)
    for i in md.disasm(code, addr):
        ann = ''
        if i.mnemonic == 'bl':
            try:
                tgt = int(i.op_str.lstrip('#'), 16)
                if tgt in stubmap:
                    ann = '  ; <%s>' % stubmap[tgt].split('(')[0]
            except Exception: pass
        print('%08x  %-8s %-34s%s' % (i.address, i.mnemonic, i.op_str, ann))

dis(0x3ca80, 0x140, 'before 0x3cb4c')
dis(0x3cb4c, 0x140, 'after 0x3cb4c')
