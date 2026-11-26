#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""列出 ABS32 指向的 dynsym 中 LOCAL 绑定符号"""
import struct, sys

R_ARM_ABS32 = 2

def parse(path):
    data = bytearray(open(path, 'rb').read())
    e_shoff = struct.unpack_from('<I', data, 32)[0]
    e_shentsize = struct.unpack_from('<H', data, 46)[0]
    e_shnum = struct.unpack_from('<H', data, 48)[0]
    e_shstrndx = struct.unpack_from('<H', data, 50)[0]
    shdrs = []
    for i in range(e_shnum):
        off = e_shoff + i * e_shentsize
        sh = struct.unpack_from('<10I', data, off)
        shdrs.append({'name': sh[0], 'type': sh[1], 'flags': sh[2], 'addr': sh[3],
                      'offset': sh[4], 'size': sh[5], 'link': sh[6], 'info': sh[7],
                      'entsize': sh[9]})
    shstr = data[shdrs[e_shstrndx]['offset']: shdrs[e_shstrndx]['offset'] + shdrs[e_shstrndx]['size']]
    def cname(i):
        e = shstr.find(b'\x00', i); return shstr[i:e].decode('ascii', 'replace')
    for sh in shdrs:
        sh['name'] = cname(sh['name'])
    rel = dynsym = dynstr = None
    for sh in shdrs:
        if sh['name'] == '.rel.dyn': rel = sh
        if sh['name'] == '.dynsym': dynsym = sh
        if sh['name'] == '.dynstr': dynstr = sh
    return data, rel, dynsym, dynstr

def get_str(stroff, strtab_data, idx):
    e = strtab_data.find(b'\x00', idx)
    return strtab_data[idx:e].decode('ascii', 'replace')

data, rel, dynsym, dynstr = parse(sys.argv[1] if len(sys.argv)>1 else 'libezxam_reconstructed.so.1.0.0')
dynstr_data = data[dynstr['offset']: dynstr['offset']+dynstr['size']]
dynsym_es = dynsym['entsize'] or 16
dynsym_n = dynsym['size'] // dynsym_es

local_names = {}
for i in range(rel['size']//8):
    ro = rel['offset'] + i*8
    r_offset, r_info = struct.unpack_from('<II', data, ro)
    sym = r_info >> 8
    typ = r_info & 0xFF
    if typ != R_ARM_ABS32:
        continue
    if sym >= dynsym_n:
        continue
    so = dynsym['offset'] + sym*dynsym_es
    st_name, st_value, st_size, st_info, st_other, st_shndx = struct.unpack_from('<IIIBBH', data, so)
    bind = st_info >> 4
    name = get_str(st_name, dynstr_data, st_name) if st_name else '(unnamed)'
    if bind == 1 and st_shndx != 0:  # LOCAL 且已定义
        local_names[name] = (sym, st_value, st_shndx)

print(f"ABS32 指向的 LOCAL 已定义符号: {len(local_names)} 个")
for nm, (sym, val, shndx) in sorted(local_names.items(), key=lambda x: x[1][0]):
    print(f"  dynsym[{sym:4d}] value=0x{val:08x} shndx={shndx}  {nm[:70]}")