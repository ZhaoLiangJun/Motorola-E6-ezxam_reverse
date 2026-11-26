# -*- coding: utf-8 -*-
"""pltmap.py - 为 prelink ARM ELF 构建 PLT stub -> 符号名映射
用法: python pltmap.py <elf> [addr_hex ...]   # 打印指定地址所属符号或全映射
"""
import sys, struct, json

def parse(path):
    f = open(path, 'rb').read()
    e_phoff = struct.unpack('<I', f[28:32])[0]
    e_phnum = struct.unpack('<H', f[44:46])[0]
    segs = []
    for i in range(e_phnum):
        off = e_phoff + i*32
        p_type, p_offset, p_vaddr, _, p_filesz = struct.unpack('<8I', f[off:off+32])[:5]
        if p_type == 1:
            segs.append((p_vaddr, p_offset, p_filesz))
    e_shoff = struct.unpack('<I', f[32:36])[0]
    e_shentsize = struct.unpack('<H', f[46:48])[0]
    e_shnum = struct.unpack('<H', f[48:50])[0]
    secs = []
    for i in range(e_shnum):
        o = e_shoff + i*e_shentsize
        vals = struct.unpack('<10I', f[o:o+40])
        secs.append(dict(name=vals[0], type=vals[1], addr=vals[3], offset=vals[4],
                         size=vals[5], link=vals[6], entsize=vals[9]))
    return f, segs, secs

def read_at(f, segs, addr, size):
    for vaddr, offset, filesz in segs:
        if vaddr <= addr < vaddr + filesz:
            return f[addr-vaddr+offset: addr-vaddr+offset+size]
    return b''

def build(path):
    f, segs, secs = parse(path)
    shstrndx = struct.unpack('<H', f[50:52])[0]
    shstr = secs[shstrndx]
    def sname(n):
        s = f[shstr['offset']+n:]
        return s[:s.index(0)].decode()
    byname = {sname(s['name']): s for s in secs}
    if '.dynsym' not in byname:
        # 无 section header，从 PT_DYNAMIC 解析 dynsym/dynstr/rel
        for vaddr, offset, filesz in segs:
            pass
        dyn = None
        e_phoff = struct.unpack('<I', f[28:32])[0]
        e_phnum = struct.unpack('<H', f[44:46])[0]
        for i in range(e_phnum):
            off = e_phoff + i*32
            p_type = struct.unpack('<I', f[off:off+4])[0]
            if p_type == 2:
                dyn = (struct.unpack('<I', f[off+8:off+12])[0], struct.unpack('<I', f[off+4:off+8])[0])
        d_vaddr, d_off = dyn
        d = {}
        for j in range(0, 4000, 8):
            tag, val = struct.unpack('<iI', f[d_off+j:d_off+j+8])
            if tag == 0: break
            if tag not in d: d[tag] = val
        def sec_from(vaddr, size):
            for sv, so, sf in segs:
                if sv <= vaddr < sv+sf:
                    return dict(addr=vaddr, offset=vaddr-sv+so, size=size)
            return None
        byname['.dynsym'] = sec_from(d[6], 0x100000)
        byname['.dynstr'] = sec_from(d[5], 0x100000)
        byname['.rel.plt'] = sec_from(d[23], d[2]) if 23 in d else None
        byname['.rel.dyn'] = sec_from(d[17], d[18]) if 17 in d else None
        byname['.plt'] = sec_from(d[20], 0x100000) if 20 in d else None  # DT_PLTGOT? no; DT_JMPREL=23
    dynsym, dynstr = byname['.dynsym'], byname['.dynstr']
    def sym_name(idx):
        o = dynsym['offset'] + idx*16
        st_name = struct.unpack('<I', f[o:o+4])[0]
        s = f[dynstr['offset']+st_name:]
        return s[:s.index(0)].decode()
    gotmap = {}
    for rn in ('.rel.plt', '.rel.dyn'):
        rel = byname.get(rn)
        if not rel: continue
        for i in range(rel['size']//8):
            o = rel['offset'] + i*8
            r_offset, r_info = struct.unpack('<II', f[o:o+8])
            symidx, rtype = r_info >> 8, r_info & 0xff
            if symidx and rtype in (21, 22):
                gotmap[r_offset] = sym_name(symidx)
    # 统一用暴力扫描（兼容 12/16 字节两种 PLT stub 布局）
    words = struct.unpack('<%dI' % (len(f)//4), f[:len(f)//4*4])
    stubmap = {}

    def dec_add_imm(w, rn_want):
        # ADD Rd, Rn, #imm ror rot (I=1, op=8)
        if (w>>25)&1 != 1 or (w>>20)&0x1f != 8: return None
        if (w>>12)&0xf != rn_want: return None
        rot = ((w>>8)&0xf)*2; imm = w&0xff
        return ((imm >> rot) | (imm << (32-rot))) & 0xffffffff if rot else imm

    for idx in range(len(words)-2):
        stub = idx*4
        # 锚点 words[idx] = add ip, pc, #hi
        v_hi = dec_add_imm(words[idx], 12) or 0
        if (words[idx]>>16)&0xf != 15: continue  # 必须 add ip,pc 形式
        w2, w3 = words[idx+1], words[idx+2]
        v_mid = dec_add_imm(w2, 12)
        if v_mid is None: continue
        if (w3>>25)&7 != 2 or (w3>>22)&1 or (w3>>21)&1 != 1 or (w3>>20)&1 != 1: continue
        if (w3>>16)&0xf != 12: continue
        U = (w3>>23)&1; off = w3 & 0xfff
        val = v_hi + v_mid
        for sv, so, sf in segs:
            if so <= stub < so+sf:
                vstub = sv + (stub - so)
                got = vstub + 8 + (val + off if U else val - off)
                if got in gotmap: stubmap[vstub] = gotmap[got]
                break
    return f, segs, byname, stubmap

def demangle(s):
    try:
        import subprocess
        r = subprocess.run(['c++filt', s], capture_output=True, text=True)
        if r.returncode == 0:
            return r.stdout.strip()
    except Exception:
        pass
    return s

if __name__ == '__main__':
    path = sys.argv[1]
    f, segs, byname, stubmap = build(path)
    json.dump({hex(k): v for k, v in stubmap.items()}, open(path + '.plt.json', 'w'))
    if len(sys.argv) > 2:
        for a in sys.argv[2:]:
            stub = int(a, 16)
            name = stubmap.get(stub)
            # 向下对齐: stub 们是 16 字节对齐的
            while name is None and stub > 0:
                stub -= 4
                name = stubmap.get(stub)
            print('%s -> %s' % (a, demangle(name) if name else '???'))
    else:
        for a in sorted(stubmap):
            print('%08x %s' % (a, stubmap[a]))
