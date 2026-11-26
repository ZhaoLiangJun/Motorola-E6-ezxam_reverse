# -*- coding: utf-8 -*-
"""oplt.py - orig_local.so 的 PLT stub -> 导入符号名映射（R_ARM_JUMP_SLOT 驱动）

用法:
  from oplt import stubmap, sym_at
  m = stubmap()            # {stub_addr: symbol_name}
"""
import struct
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def _sections(d):
    e_shoff = struct.unpack_from('<I', d, 0x20)[0]
    ents = struct.unpack_from('<H', d, 0x2E)[0]
    n = struct.unpack_from('<H', d, 0x30)[0]
    out = []
    for i in range(n):
        o = e_shoff + i * ents
        nm, typ, fl, addr, off, size, link, info, align, esz = struct.unpack_from(
            '<IIIIIIIIII', d, o)
        out.append(dict(name=nm, type=typ, flags=fl, addr=addr, off=off,
                        size=size, link=link, entsize=esz))
    return out


def _strtab(d, sec):
    f = sec['off']
    e = d.find(b'\x00', f)
    return d[f:e].decode('latin1')


def load(path=None):
    path = path or os.path.join(HERE, 'orig_local.so')
    d = open(path, 'rb').read()
    secs = _sections(d)
    # shstrtab 名
    shstr_off = secs[struct.unpack_from('<H', d, 0x32)[0]]['off']
    for s in secs:
        e = d.find(b'\x00', shstr_off + s['name'])
        s['n'] = d[shstr_off + s['name']:e].decode('latin1')
    return d, secs


def _rotimm(w):
    """解码 ARM 数据处理指令的 12 位循环立即数"""
    imm8 = w & 0xFF
    rot = ((w >> 8) & 0xF) * 2
    v = (imm8 >> rot) | ((imm8 << (32 - rot)) & 0xFFFFFFFF) if rot else imm8
    return v & 0xFFFFFFFF


def stubmap(path=None):
    d, secs = load(path)
    dynsym = [s for s in secs if s['type'] == 11][0]
    strtab = secs[dynsym['link']]
    syms = []
    for i in range(dynsym['size'] // 16):
        o = dynsym['off'] + i * 16
        nm, val, sz, info, oth, shndx = struct.unpack_from('<IIIBBH', d, o)
        f = strtab['off'] + nm
        e = d.find(b'\x00', f)
        syms.append(d[f:e].decode('latin1'))
    # 收集 JUMP_SLOT: slot -> symbol
    slots = {}
    for rs in secs:
        if rs["type"] != 9:      # SHT_REL (9)
            continue
        for i in range(rs['size'] // 8):
            o = rs['off'] + i * 8
            r_off, r_info = struct.unpack_from('<II', d, o)
            typ = r_info & 0xFF
            idx = r_info >> 8
            if typ == 22:          # R_ARM_JUMP_SLOT
                slots[r_off] = syms[idx]
    m = {}
    for s in secs:
        if s['type'] != 1 or not s['size']:      # PROGBITS only
            continue
        a = s['addr']
        for k in range(0, s['size'] - 12, 4):
            p = s['off'] + k
            try:
                w0, w1, w2 = struct.unpack_from('<III', d, p)
            except struct.error:
                break
            # add ip,pc,#imm ; add ip,ip,#imm ; ldr pc,[ip,#imm]!
            if (w0 & 0xFFFFF000) != 0xE28FC000:
                continue
            if (w1 & 0xFFFFF000) != 0xE28CC000:
                continue
            if (w2 & 0xFFFFF000) != 0xE5BCF000:
                continue
            stub = a + k
            ip = (stub + 8) + _rotimm(w0)
            ip = (ip + _rotimm(w1)) & 0xFFFFFFFF
            # 注意: LDR 的 offset 是 12 位无符号立即数，不做循环移位
            slot = (ip + (w2 & 0xFFF)) & 0xFFFFFFFF
            if slot in slots:
                m[stub] = slots[slot]
    return m


_CACHE = None


def get():
    global _CACHE
    if _CACHE is None:
        _CACHE = stubmap()
    return _CACHE


if __name__ == '__main__':
    m = get()
    print('stubs =', len(m))
    for a in sys.argv[1:]:
        k = int(a, 16)
        print(hex(k), '->', m.get(k))
