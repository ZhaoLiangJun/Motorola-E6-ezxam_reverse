# -*- coding: utf-8 -*-
"""amxref.py - 反汇编 am_dev 并解析 PLT 调用目标符号；找调用某符号的函数
用法:
  python amxref.py map               # 打印 PLT 映射
  python amxref.py callers <sym>     # 找哪些地址调用了 sym
  python amxref.py dis <addr_hex> <len> [t]
"""
import sys, struct
from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM, CS_MODE_THUMB

PATH = r'D:\WorkBuddy\2026-11-08-18-54-48\ezxam_reverse\am_dev'

def parse():
    f = open(PATH, 'rb').read()
    e_phoff = struct.unpack('<I', f[28:32])[0]
    e_phnum = struct.unpack('<H', f[44:46])[0]
    segs = []
    for i in range(e_phnum):
        off = e_phoff + i*32
        p_type, p_offset, p_vaddr, _, p_filesz = struct.unpack('<8I', f[off:off+32])[:5]
        if p_type == 1:
            segs.append((p_vaddr, p_offset, p_filesz))
    return f, segs

def read_at(f, segs, addr, size):
    for vaddr, offset, filesz in segs:
        if vaddr <= addr < vaddr + filesz:
            return f[addr-vaddr+offset: addr-vaddr+offset+size]
    return b''

def plt_map(f, segs):
    """返回 plt stub 地址 -> 符号名。通过 .rel.plt + .dynsym 解析。"""
    # 找 section headers
    e_shoff = struct.unpack('<I', f[32:36])[0]
    e_shentsize = struct.unpack('<H', f[46:48])[0]
    e_shnum = struct.unpack('<H', f[48:50])[0]
    e_shstrndx = struct.unpack('<H', f[50:52])[0]
    secs = []
    for i in range(e_shnum):
        o = e_shoff + i*e_shentsize
        name, typ, flags, addr, offset, size, link, info, align, entsize = struct.unpack('<10I', f[o:o+40])
        secs.append(dict(name=name, type=typ, addr=addr, offset=offset, size=size, link=link, entsize=entsize))
    shstr = secs[e_shstrndx]
    def sname(n):
        s = f[shstr['offset']+n:]
        return s[:s.index(0)].decode()
    byname = {sname(s['name']): s for s in secs}
    dynsym = byname.get('.dynsym'); dynstr = byname.get('.dynstr')
    def sym_name(idx):
        o = dynsym['offset'] + idx*16
        st_name = struct.unpack('<I', f[o:o+4])[0]
        s = f[dynstr['offset']+st_name:]
        return s[:s.index(0)].decode()
    m = {}
    for rn in ('.rel.plt', '.rel.dyn'):
        rel = byname.get(rn)
        if not rel: continue
        for i in range(rel['size']//8):
            o = rel['offset'] + i*8
            r_offset, r_info = struct.unpack('<II', f[o:o+8])
            symidx = r_info >> 8
            rtype = r_info & 0xff
            if rtype == 21 and symidx:  # R_ARM_JUMP_SLOT
                m[r_offset] = sym_name(symidx)
            elif rtype == 22 and symidx:  # R_ARM_GLOB_DAT
                m[r_offset] = sym_name(symidx)
    # PLT stub: 每个 stub 对应 .got.plt 一个槽。stub 地址需从 .plt 内容推。
    # ARM PLT: push {r4/ip}; add; ldr pc,[pc,#-x]... 复杂。直接扫 .plt 内每条指令
    # 引用的 got 地址。
    return m, byname

def analyze_plt(f, segs, gotmap, secs):
    """按位解码 PLT stub（add ip,ip,#imm,ror / ldr pc,[ip,#off]!）"""
    plt = secs.get('.plt')
    if not plt: return {}
    stubmap = {}
    n = (plt['size']-0x14)//16
    for i in range(n):
        stub = plt['addr'] + 0x14 + i*16
        w1, w2, w3 = struct.unpack('<3I', read_at(f, segs, stub, 12))
        if (w2>>25)&1 != 1 or (w2>>20)&0x1f != 8: continue
        if (w2>>12)&0xf != 12 or (w2>>16)&0xf != 12: continue
        rot = ((w2>>8)&0xf)*2; imm = w2&0xff
        val = ((imm >> rot) | (imm << (32-rot))) & 0xffffffff if rot else imm
        if (w3>>25)&7 != 2 or (w3>>22)&1 or (w3>>21)&1 != 1 or (w3>>20)&1 != 1: continue
        if (w3>>16)&0xf != 12: continue
        U = (w3>>23)&1; off = w3 & 0xfff
        got = stub + 8 + (val + off if U else val - off)
        if got in gotmap: stubmap[stub] = gotmap[got]
    return stubmap

def main():
    f, segs = parse()
    gotmap, secs = plt_map(f, segs)
    stubmap = analyze_plt(f, segs, gotmap, secs)
    cmd = sys.argv[1] if len(sys.argv) > 1 else 'map'
    if cmd == 'map':
        for a in sorted(stubmap):
            print('%08x %s' % (a, stubmap[a]))
        return
    if cmd == 'callers':
        target = sys.argv[2]
        # 全 .text 反汇编，记录 bl 目标
        text = secs.get('.text')
        md = Cs(CS_ARCH_ARM, CS_MODE_ARM); md.skipdata = True
        code = f[text['offset']:text['offset']+text['size']]
        # 先建函数边界（用 symbols? 无 symtab，用 bl 调用点近似）
        calls = []
        for i in md.disasm(code, text['addr']):
            if i.mnemonic == 'bl':
                try:
                    tgt = int(i.op_str.lstrip('#'), 16)
                except Exception:
                    continue
                name = stubmap.get(tgt)
                if name and target in name:
                    calls.append((i.address, name))
        for a, n in calls:
            print('%08x -> %s' % (a, n))
        return
    if cmd == 'dis':
        addr = int(sys.argv[2], 16); size = int(sys.argv[3], 0)
        thumb = len(sys.argv) > 4 and sys.argv[4] == 't'
        md = Cs(CS_ARCH_ARM, CS_MODE_THUMB if thumb else CS_MODE_ARM); md.skipdata = True
        code = read_at(f, segs, addr, size)
        for i in md.disasm(code, addr):
            ann = ''
            if i.mnemonic == 'bl':
                try:
                    tgt = int(i.op_str.lstrip('#'), 16)
                    if tgt in stubmap: ann = '  ; <%s>' % stubmap[tgt]
                except Exception: pass
            print('%08x  %-8s %-40s%s' % (i.address, i.mnemonic, i.op_str, ann))

if __name__ == '__main__':
    main()
