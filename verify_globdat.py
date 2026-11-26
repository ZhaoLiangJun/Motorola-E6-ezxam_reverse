#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""verify_globdat.py - 验证新库所有 GLOB_DAT/REL 符号可解析"""
import struct, subprocess, os, sys

R_ARM_GLOB_DAT = 21
R_ARM_REL = 3
R_ARM_RELATIVE = 3

def parse_elf(path):
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

def main():
    lib = sys.argv[1] if len(sys.argv) > 1 else 'libezxam_relinked.so.1.0.0'
    data, rel, dynsym, dynstr = parse_elf(lib)
    dynsym_es = dynsym['entsize'] or 16
    dynsym_n = dynsym['size'] // dynsym_es
    dynstr_data = data[dynstr['offset']: dynstr['offset'] + dynstr['size']]

    def get_str(idx):
        e = dynstr_data.find(b'\x00', idx)
        return dynstr_data[idx:e].decode('ascii', 'replace')

    globdat_syms = []
    rel_syms = []
    for i in range(rel['size'] // 8):
        ro = rel['offset'] + i * 8
        r_offset, r_info = struct.unpack_from('<II', data, ro)
        sym = r_info >> 8
        typ = r_info & 0xFF
        if typ == R_ARM_GLOB_DAT:
            if sym < dynsym_n:
                so = dynsym['offset'] + sym * dynsym_es
                st_name, st_value = struct.unpack_from('<II', data, so)
                name = get_str(st_name) if st_name else '(unnamed)'
                globdat_syms.append((r_offset, sym, name, st_value))
        elif typ in (R_ARM_REL, R_ARM_RELATIVE):
            rel_syms.append(r_offset)

    print(f"GLOB_DAT 条目: {len(globdat_syms)}")
    # 外部UND的GLOB_DAT（st_value==0）需运行时解析
    und = [s for s in globdat_syms if s[3] == 0]
    def_ = [s for s in globdat_syms if s[3] != 0]
    print(f"  外部UND(运行时解析): {len(und)}")
    print(f"  内部定义(已固化):    {len(def_)}")
    print(f"REL/RELATIVE 条目:    {len(rel_syms)}")

    print("\n外部UND GLOB_DAT 符号样例(前15):")
    for r_offset, sym, name, val in und[:15]:
        print(f"  0x{r_offset:08x} sym[{sym:4d}] {name[:60]}")

    # 检查外部UND符号名是否出现在依赖库导出中
    # 依赖库
    deps = ['qte_local.so', 'appbase_local.so', 'orig_local.so']
    # 用 readelf 提取各依赖库的导出符号
    export_sets = {}
    TOOL = '/d/cygwin/home/ezx/arm-linux-gcc/bin/arm-linux-readelf.exe'
    for dep in deps:
        if not os.path.exists(dep):
            continue
        try:
            out = subprocess.check_output([TOOL, '-s', dep], stderr=subprocess.DEVNULL).decode('utf-8', 'replace')
            # 只取定义符号（DEFAULT 且非UND）
            names = set()
            for line in out.splitlines():
                if 'DEFAULT' in line and 'UND' not in line:
                    # 提取最后一列名字
                    parts = line.split()
                    if parts:
                        nm = parts[-1]
                        if nm.startswith('_ZN') or nm.startswith('_Z'):
                            names.add(nm)
            export_sets[dep] = names
        except Exception as e:
            export_sets[dep] = set()

    print("\n外部UND GLOB_DAT 符号解析检查:")
    unresolved = []
    resolved_in = {}
    for r_offset, sym, name, val in und:
        found = None
        for dep, names in export_sets.items():
            if name in names:
                found = dep
                break
        if found:
            resolved_in[name] = found
        else:
            unresolved.append(name)
    print(f"  可解析: {len(resolved_in)}")
    print(f"  未解析: {len(unresolved)}")
    if unresolved:
        from collections import Counter
        c = Counter(unresolved)
        print("  未解析符号分布:")
        for nm, cnt in c.most_common(20):
            print(f"    {cnt:3d}x {nm[:70]}")

if __name__ == '__main__':
    main()