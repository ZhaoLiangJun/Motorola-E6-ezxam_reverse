# -*- coding: utf-8 -*-
"""gnext.py - 提取 AM_AppObject::getName 里的 UUID 分组与 tr() 键对

用法: python gnext.py [start] [size]
"""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from edis import ArmElf
from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM, CS_MODE_LITTLE_ENDIAN
from capstone.arm import ARM_OP_REG, ARM_OP_IMM, ARM_OP_MEM

FUNC = 0x41AEAC88
SIZE = 14020
SL   = 0x41B229F0

class Elf2:
    def __init__(self, path):
        self.a = ArmElf(path)
        self.d = self.a.d
        self.segs = []
        e = self.d
        e_phoff = struct.unpack_from('<I', e, 0x1C)[0]
        e_phentsize = struct.unpack_from('<H', e, 0x2A)[0]
        e_phnum = struct.unpack_from('<H', e, 0x2C)[0]
        for i in range(e_phnum):
            o = e_phoff + i*e_phentsize
            p_type, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz = struct.unpack_from('<IIIIII', e, o)
            if p_type == 1:
                self.segs.append((p_vaddr, p_offset, p_filesz))
    def off(self, va):
        for v, o, sz in self.segs:
            if v <= va < v + sz:
                return o + (va - v)
        return None
    def u32(self, va):
        o = self.off(va)
        return None if o is None else struct.unpack_from('<I', self.d, o)[0]
    def cstr(self, va, n=200):
        o = self.off(va)
        if o is None: return None
        q = self.d.find(b'\x00', o)
        return self.d[o:min(q, o+n)].decode('latin-1')

def sweep(E, start, size):
    md = Cs(CS_ARCH_ARM, CS_MODE_ARM | CS_MODE_LITTLE_ENDIAN)
    md.detail = True
    o = E.off(start)
    blob = E.d[o:o+size]
    res = []
    off = 0
    while off < len(blob):
        got = False
        for ins in md.disasm(blob[off:off+4], start+off):
            res.append(ins); got = True
        if not got:
            res.append(None)
        off += 4
    return res

def analyze(E, start, size):
    insns = sweep(E, start, size)
    regs = {}     # 常量
    spof = {'sp': 0}   # 寄存器相对 sp 的偏移
    bufs = {}     # sp偏移 -> {byte:val}
    guids = []    # (addr, 16B)
    keys  = []    # (addr, off1, off2)
    for ins in insns:
        if ins is None:
            continue
        m = ins.mnemonic; n = len(ins.operands)
        try:
            if m == 'mov' and n == 2 and ins.operands[0].type == ARM_OP_REG and ins.operands[1].type == ARM_OP_IMM:
                regs[ins.reg_name(ins.operands[0].reg)] = ins.operands[1].imm & 0xffffffff
            elif m in ('add', 'sub') and n == 3 and ins.operands[0].type == ARM_OP_REG:
                d = ins.reg_name(ins.operands[0].reg)
                r1 = ins.reg_name(ins.operands[1].reg) if ins.operands[1].type == ARM_OP_REG else None
                if r1 == 'sp' and ins.operands[2].type == ARM_OP_IMM:
                    spof[d] = ins.operands[2].imm
                    regs.pop(d, None)
                elif r1 and r1 in regs and ins.operands[2].type == ARM_OP_IMM:
                    v = regs[r1] + ins.operands[2].imm if m == 'add' else regs[r1] - ins.operands[2].imm
                    regs[d] = v & 0xffffffff
                else:
                    regs.pop(d, None); spof.pop(d, None)
            elif m == 'ldr' and n == 2 and ins.operands[0].type == ARM_OP_REG and ins.operands[1].type == ARM_OP_MEM:
                mem = ins.operands[1].mem
                base = ins.reg_name(mem.base)
                d = ins.reg_name(ins.operands[0].reg)
                if base == 'pc':
                    v = E.u32(ins.address + 8 + mem.disp)
                    if v is not None:
                        regs[d] = v
                        spof.pop(d, None)
                elif base == 'sl' and mem.index:
                    off = regs.get(ins.reg_name(mem.index))
                    keys.append((ins.address, off))
                    regs.pop(d, None)
                else:
                    regs.pop(d, None); spof.pop(d, None)
            elif m in ('strh', 'strb', 'str') and n == 2 and ins.operands[1].type == ARM_OP_MEM:
                mem = ins.operands[1].mem
                base = ins.reg_name(mem.base)
                if base in spof and ins.operands[0].type == ARM_OP_REG:
                    val = regs.get(ins.reg_name(ins.operands[0].reg))
                    if val is not None:
                        a = spof[base] + mem.disp
                        cnt = {'strb':1,'strh':2,'str':4}[m]
                        for k in range(cnt):
                            bufs[a+k] = (val >> (8*k)) & 0xff
                        for st in sorted(set(x & ~0xf for x in bufs)):
                            if all((st+k) in bufs for k in range(16)):
                                g = bytes(bufs[st+k] for k in range(16))
                                guids.append((ins.address, g))
                                for k in range(16):
                                    bufs.pop(st+k, None)
        except Exception:
            pass
    return insns, guids, keys

def fmt(g):
    d1, d2, d3 = struct.unpack('<IHH', g[:8])
    d4 = g[8:16]
    return '{%08x-%04x-%04x-%02x%02x-%s}' % (d1, d2, d3, d4[0], d4[1], ''.join('%02x'%x for x in d4[2:]))

def main():
    E = Elf2('orig_local.so')
    start = int(sys.argv[1], 0) if len(sys.argv) > 1 else FUNC
    size  = int(sys.argv[2], 0) if len(sys.argv) > 2 else SIZE
    insns, guids, keys = analyze(E, start, size)
    ev = [(a, 'GUID', g) for a, g in guids] + [(a, 'KEY', k) for a, k in keys]
    ev.sort()
    for a, t, v in ev:
        if t == 'GUID':
            print('%#010x  GUID %s' % (a, fmt(v)))
        else:
            off = v
            if off is None:
                print('%#010x  KEY  ?' % a); continue
            p1 = E.u32(SL+off)
            s = E.cstr(p1) if p1 else None
            print('%#010x  KEY  %#06x %r' % (a, off, s))

if __name__ == '__main__':
    main()
