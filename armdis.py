# -*- coding: utf-8 -*-
"""dis.py - capstone ARM 反汇编助手（appbase/qte/libezxam prelink ELF）
用法: python dis.py <elf> <addr_hex> <len>
"""
import sys, struct

def load_segments(path):
    f = open(path, 'rb').read()
    assert f[:4] == b'\x7fELF'
    e_phoff = struct.unpack('<I', f[28:32])[0]
    e_phentsize = struct.unpack('<H', f[42:44])[0]
    e_phnum = struct.unpack('<H', f[44:46])[0]
    segs = []
    for i in range(e_phnum):
        off = e_phoff + i*e_phentsize
        p_type, p_offset, p_vaddr, p_paddr, p_filesz = struct.unpack('<8I', f[off:off+32])[:5]
        if p_type == 1:  # PT_LOAD
            segs.append((p_vaddr, p_offset, p_filesz))
    return f, segs

def read_at(f, segs, addr, size):
    for vaddr, offset, filesz in segs:
        if vaddr <= addr < vaddr + filesz:
            o = addr - vaddr + offset
            return f[o:o+size]
    return b''

def dis(path, addr, size, thumb=False):
    from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM, CS_MODE_THUMB
    f, segs = load_segments(path)
    code = read_at(f, segs, addr, size)
    md = Cs(CS_ARCH_ARM, CS_MODE_THUMB if thumb else CS_MODE_ARM)
    md.skipdata = True
    out = []
    for i in md.disasm(code, addr):
        out.append('%08x  %-8s %s' % (i.address, i.mnemonic, i.op_str))
    return '\n'.join(out)

if __name__ == '__main__':
    path = sys.argv[1]
    addr = int(sys.argv[2], 16)
    size = int(sys.argv[3], 0)
    thumb = len(sys.argv) > 4 and sys.argv[4] == 't'
    print(dis(path, addr, size, thumb))
