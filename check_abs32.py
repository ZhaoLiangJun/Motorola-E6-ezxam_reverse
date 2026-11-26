#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""check_abs32.py - 检查重建库 ABS32 重定位指向符号的绑定/可见性/所属库"""
import struct, sys

R_ARM_ABS32 = 2
R_ARM_GLOB_DAT = 21
R_ARM_RELATIVE = 3

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
    # find sections
    rel = dynsym = dynstr = strtab = symtab = None
    for sh in shdrs:
        if sh['name'] == '.rel.dyn': rel = sh
        if sh['name'] == '.dynsym': dynsym = sh
        if sh['name'] == '.dynstr': dynstr = sh
        if sh['name'] == '.symtab': symtab = sh
        if sh['name'] == '.strtab': strtab = sh
    return data, shdrs, rel, dynsym, dynstr, symtab, strtab

def get_str(stroff, strtab_data, idx):
    e = strtab_data.find(b'\x00', idx)
    return strtab_data[idx:e].decode('ascii', 'replace')

data, shdrs, rel, dynsym, dynstr, symtab, strtab = parse(sys.argv[1] if len(sys.argv)>1 else 'libezxam_reconstructed.so.1.0.0')

dynstr_data = data[dynstr['offset']: dynstr['offset']+dynstr['size']]
dynsym_es = dynsym['entsize'] or 16
dynsym_n = dynsym['size'] // dynsym_es
symtab_es = symtab['entsize'] or 16
symtab_n = symtab['size'] // symtab_es
strtab_data = data[strtab['offset']: strtab['offset']+strtab['size']] if strtab else b''

print(f"dynsym={dynsym_n} symtab={symtab_n} rel_dyn_entries={rel['size']//8}")

from collections import Counter
c = Counter()
examples = {}
for i in range(rel['size']//8):
    ro = rel['offset'] + i*8
    r_offset, r_info = struct.unpack_from('<II', data, ro)
    sym = r_info >> 8
    typ = r_info & 0xFF
    if typ != R_ARM_ABS32:
        continue
    # 在 dynsym 中
    if sym < dynsym_n:
        so = dynsym['offset'] + sym*dynsym_es
        st_name, st_value, st_size, st_info, st_other, st_shndx = struct.unpack_from('<IIIBBH', data, so)
        bind = st_info >> 4
        vis = st_other & 0x3
        name = get_str(st_name, dynstr_data, st_name) if st_name else '(unnamed)'
        key = (bind, vis, st_shndx)
        c[key] += 1
        if key not in examples:
            examples[key] = name
    else:
        c[('SYMTAB_ONLY', sym-symtab_n, -1)] += 1
        if ('SYMTAB_ONLY', sym-symtab_n, -1) not in examples:
            # 读 symtab
            if sym < symtab_n:
                so = symtab['offset'] + sym*symtab_es
                st_name, st_value, st_size, st_info, st_other, st_shndx = struct.unpack_from('<IIIBBH', data, so)
                nm = get_str(st_name, strtab_data, st_name) if (st_name and strtab) else f'idx{sym}'
                examples[('SYMTAB_ONLY', sym-symtab_n, -1)] = nm
            else:
                examples[('SYMTAB_ONLY', sym-symtab_n, -1)] = f'OOB{sym}'

print("\nABS32 指向符号的 (bind,vis,shndx) 分布:")
for k, v in sorted(c.items(), key=lambda x: -x[1]):
    nm = examples[k]
    print(f"  bind={k[0]} vis={k[1]} shndx={k[2]} count={v:4d}  eg={nm}")