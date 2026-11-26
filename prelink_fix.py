#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
prelink_fix.py - 完整复现 prelink：重建库 .data 段 ABS32 -> 固化绝对地址 + GLOB_DAT + PRELINKED
作者：赵良军

prelink 的完整语义（对照 orig_local.so 的实际情况）：
  1. .data 段槽位【已固化绝对地址】：
     - 内部符号：st_value（已是绝对地址，如 0x41aea674）
     - 外部符号：依赖库(qte/appbase)中该符号的绝对地址（如 0x415bf644 = QObject::event）
  2. 重定位仍为 GLOB_DAT（保留 sym 索引），但加了 GNU_PRELINKED 标记后
     ld.so 不再运行时解析（槽位已是地址）。
  3. .got 段保持链接器原样（JUMP_SLOT/RELATIVE/GLOB_DAT），RELCOUNT 更新。

这样得到与 orig_local.so 完全一致的语义：槽位有地址，GLOB_DAT+PRELINKED。
"""
import struct, sys, os

R_ARM_ABS32 = 2
R_ARM_GLOB_DAT = 21
R_ARM_RELATIVE = 23

# 依赖库（用于查外部符号绝对地址）
DEP_LIBS = ['qte_local.so', 'appbase_local.so', 'libezxappbase.so.1.0.0']
DEP_NAMES = {'qte_local.so': 'libqte-mt.so.2',
             'appbase_local.so': 'libezxappbase.so.1',
             'libezxappbase.so.1.0.0': 'libezxappbase.so.1'}


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
    return data, shdrs


def get_symtable(data, shdrs, secname='.dynsym'):
    dyn = dynstr = None
    for sh in shdrs:
        if sh['name'] == secname: dyn = sh
        if sh['name'] == '.dynstr': dynstr = sh
    if not dyn or not dynstr: return {}, {}
    esz = dyn['entsize'] or 16
    ds = data[dynstr['offset']: dynstr['offset'] + dynstr['size']]
    syms = {}
    for i in range(dyn['size'] // esz):
        o = dyn['offset'] + i * esz
        st_name, st_val, st_size, st_info, st_other, st_shndx = struct.unpack_from('<IIIBBH', data, o)
        e = ds.find(b'\x00', st_name)
        nm = ds[st_name:e].decode('ascii', 'replace') if st_name else ''
        syms[i] = (nm, st_val, st_shndx)
    return syms, ds


def build_dep_symmap():
    """从依赖库构建 {符号名: 绝对地址} 映射。"""
    symmap = {}
    for lib in DEP_LIBS:
        if not os.path.exists(lib):
            continue
        try:
            data, shdrs = parse(lib)
            syms, _ = get_symtable(data, shdrs)
            for idx, (nm, val, shndx) in syms.items():
                if nm and shndx != 0 and val != 0:
                    # 依赖库可能是 prelink 库（val 已是绝对）或普通库（val 相对）
                    # 对 prelink 库 val 是绝对地址；普通库需加基址。这里用 val 直接
                    # （qte/appbase 本地副本都是 prelink 处理过，val 为绝对）
                    if nm not in symmap or symmap[nm] == 0:
                        symmap[nm] = val
        except Exception as e:
            print(f"[!] 解析依赖库 {lib} 失败: {e}")
    return symmap


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else 'libezxam_reconstructed.so.1.0.0'
    dst = sys.argv[2] if len(sys.argv) > 2 else 'libezxam_relinked.so.1.0.0'
    dry = '--dry' in sys.argv

    data, shdrs = parse(src)
    rel = dynsym = dynstr = None
    for sh in shdrs:
        if sh['name'] == '.rel.dyn': rel = sh
        if sh['name'] == '.dynsym': dynsym = sh
        if sh['name'] == '.dynstr': dynstr = sh
    assert rel and dynsym and dynstr, '关键节缺失'

    n = rel['size'] // 8
    base = rel['offset']

    # 构建符号表
    syms, ds = get_symtable(data, shdrs)
    dep_map = build_dep_symmap()

    # 可写 LOAD 段映射 (vaddr -> fileoff)
    e_phoff = struct.unpack_from('<I', data, 28)[0]
    e_phentsize = struct.unpack_from('<H', data, 42)[0]
    e_phnum = struct.unpack_from('<H', data, 44)[0]
    write_ph = []
    for i in range(e_phnum):
        o = e_phoff + i * e_phentsize
        p_type, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_flags, p_align = \
            struct.unpack_from('<IIIIIIII', data, o)
        if p_type == 1 and (p_flags & 0x2):
            write_ph.append((p_vaddr, p_offset, p_filesz))
    def vaddr_to_off(v):
        for (pv, po, pf) in write_ph:
            if pv <= v < pv + pf:
                return po + (v - pv)
        return None

    # 读出所有 .rel.dyn 条目
    entries = []
    for i in range(n):
        r_offset, r_info = struct.unpack_from('<II', data, base + i * 8)
        entries.append([r_offset, r_info])

    to_globdat = 0
    fixed_internal = 0
    fixed_external = 0
    fixed_unknown = 0
    log = []

    for e in entries:
        typ = e[1] & 0xFF
        if typ != R_ARM_ABS32:
            continue
        sym_idx = e[1] >> 8
        nm, st_val, st_shndx = syms.get(sym_idx, ('', 0, 0))
        # 1. 固化绝对地址到槽位
        foff = vaddr_to_off(e[0])
        if foff is not None:
            addr = 0
            if st_val != 0 and st_shndx != 0:
                addr = st_val  # 内部符号，st_value 已是绝对地址
                fixed_internal += 1
            else:
                # 外部符号：查依赖库
                addr = dep_map.get(nm, 0)
                if addr != 0:
                    fixed_external += 1
                else:
                    fixed_unknown += 1
            if not dry and addr != 0:
                struct.pack_into('<I', data, foff, addr)
            if dry:
                log.append((e[0], nm, addr, st_shndx))
        # 2. 重定位类型 ABS32 -> GLOB_DAT
        e[1] = (e[1] & 0xFFFFFF00) | R_ARM_GLOB_DAT
        to_globdat += 1

    # 3. 第二遍：所有 GLOB_DAT（含原链接器生成的、位于 .got 的）也预先填入符号绝对地址。
    #    理由：所有依赖库（qte/appbase）都是 prelink 库并加载在链接地址上，l_addr=0，
    #    故 GLOB_DAT 的结果恒等于符号的绝对 st_value —— 预填与运行时解析结果一致（幂等）。
    #    这样无论 ld.so 是否跳过重定位，槽位都不会是 0。
    def resolve_addr(sym_idx):
        nm, st_val, st_shndx = syms.get(sym_idx, ('', 0, 0))
        if st_val != 0 and st_shndx != 0:
            return st_val
        return dep_map.get(nm, 0)

    globdat_filled = 0
    globdat_missing = 0
    for e in entries:
        if (e[1] & 0xFF) != R_ARM_GLOB_DAT:
            continue
        foff = vaddr_to_off(e[0])
        if foff is None:
            continue
        addr = resolve_addr(e[1] >> 8)
        if addr == 0:
            globdat_missing += 1
            continue
        if not dry:
            struct.pack_into('<I', data, foff, addr)
        globdat_filled += 1

    # 4. 重排：RELATIVE 前，其余后
    relatives = [e for e in entries if (e[1] & 0xFF) == R_ARM_RELATIVE]
    others = [e for e in entries if (e[1] & 0xFF) != R_ARM_RELATIVE]
    new_entries = relatives + others
    new_relcount = len(relatives)

    if not dry:
        for i, e in enumerate(new_entries):
            struct.pack_into('<II', data, base + i * 8, e[0], e[1])
        update_relcount(data, new_relcount)
        # 不再添加 GNU_PRELINKED：所有槽位已按 l_addr=0 幂等预填，
        # 让 ld.so 正常走一遍重定位（结果相同），避免依赖 prelink 跳过逻辑的未定行为。

    from collections import Counter
    cnt = Counter((e[1] & 0xFF) for e in new_entries)
    print(f"[i] .rel.dyn 条目={n}")
    print(f"[i] ABS32 -> GLOB_DAT : {to_globdat}")
    print(f"[i] 固化内部符号槽位 : {fixed_internal}")
    print(f"[i] 固化外部符号槽位 : {fixed_external}")
    print(f"[i] 未找到外部符号地址: {fixed_unknown}")
    print(f"[i] GLOB_DAT 预填: {globdat_filled}  无法解析: {globdat_missing}")
    print(f"[i] 重排后: RELATIVE={len(relatives)}  其他={len(others)}  DT_RELCOUNT={new_relcount}")
    type_names = {2: 'ABS32', 21: 'GLOB_DAT', 22: 'JUMP_SLOT', 23: 'RELATIVE'}
    for t in sorted(cnt):
        print(f"     {type_names.get(t, str(t)):10s} : {cnt[t]}")

    if dry:
        print("\n[--dry] 前 20 条固化明细:")
        for a, nm, addr, shndx in log[:20]:
            print(f"   0x{a:08x} {nm:50s} addr=0x{addr:08x} shndx={shndx}")
        return

    with open(dst, 'wb') as f:
        f.write(bytes(data))
    print(f"\n[+] 已写出 {dst} ({os.path.getsize(dst)} bytes)")


def add_prelinked(data):
    """给动态段添加 GNU_PRELINKED (0x6ffffdf5) 标记。"""
    e_phoff = struct.unpack_from('<I', data, 28)[0]
    e_phentsize = struct.unpack_from('<H', data, 42)[0]
    e_phnum = struct.unpack_from('<H', data, 44)[0]
    dyn_off = dyn_size = None
    for i in range(e_phnum):
        o = e_phoff + i * e_phentsize
        p_type, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_flags, p_align = \
            struct.unpack_from('<IIIIIIII', data, o)
        if p_type == 2:
            dyn_off, dyn_size = p_offset, p_filesz
            break
    if dyn_off is None:
        print("[!] 未找到 PT_DYNAMIC")
        return
    for i in range(dyn_size // 8):
        o = dyn_off + i * 8
        d_tag, d_val = struct.unpack_from('<II', data, o)
        if d_tag == 0x6ffffdf5:
            return
    null_idx = None
    for i in range(dyn_size // 8):
        o = dyn_off + i * 8
        d_tag, d_val = struct.unpack_from('<II', data, o)
        if d_tag == 0:
            null_idx = i
            break
    if null_idx is None:
        print("[!] 动态段无 NULL 槽位")
        return
    o = dyn_off + null_idx * 8
    struct.pack_into('<II', data, o, 0x6ffffdf5, 0x14)
    print(f"[+] GNU_PRELINKED (0x6ffffdf5) 添加到动态段 [{null_idx}]")


def update_relcount(data, new_relcount):
    e_phoff = struct.unpack_from('<I', data, 28)[0]
    e_phentsize = struct.unpack_from('<H', data, 42)[0]
    e_phnum = struct.unpack_from('<H', data, 44)[0]
    dyn_off = dyn_size = None
    for i in range(e_phnum):
        o = e_phoff + i * e_phentsize
        p_type, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_flags, p_align = \
            struct.unpack_from('<IIIIIIII', data, o)
        if p_type == 2:
            dyn_off, dyn_size = p_offset, p_filesz
            break
    if dyn_off is None:
        print("[!] 未找到 PT_DYNAMIC")
        return
    for i in range(dyn_size // 8):
        o = dyn_off + i * 8
        d_tag, d_val = struct.unpack_from('<II', data, o)
        if d_tag == 0:
            break
        if d_tag == 0x6ffffffa:
            struct.pack_into('<I', data, o + 4, new_relcount)
            print(f"[+] DT_RELCOUNT 更新为 {new_relcount}")
            return
    print("[!] 无 DT_RELCOUNT，跳过")


if __name__ == '__main__':
    main()
