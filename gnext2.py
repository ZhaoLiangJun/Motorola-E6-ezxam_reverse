# -*- coding: utf-8 -*-
"""gnext2.py - 提取 getName 的 UUID -> tr(key1,key2) 表

结构（编译器生成的 if-链）:
    GUIDi build (16B 栈缓冲)
    cmp data1 ; beq VERIFYi
    mov r3,#0 ; and r0,r3,#0xff ; cmp r0,#0 ; bne KEYi    ← 命中跳到 KEYi
    GUID(i+1) build ...
  VERIFYi: (比 data2/data4) → r3=1/0 ; b ROLLi
  ROLLi: and r0,r3,#0xff ; cmp r0,#0 ; bne KEYi
  KEYi:  ldr r3,[pc,#..]=off1 ; ldr ip,[pc,#..]=off2 ; b EPILOGUE
  EPILOGUE: ldr r1,[sl,r3] ; ldr r2,[sl,ip] ; bl QObject::tr
"""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gnext import Elf2, sweep, fmt, SL
from capstone.arm import ARM_OP_REG, ARM_OP_IMM, ARM_OP_MEM

FUNC = 0x41AEAC88
SIZE = 14020

def build_events(insns, E):
    regs = {}; spof = {'sp': 0}; bufs = {}
    guids = []; keys = []
    def resolve_pc_load(ins):
        mem = ins.operands[1].mem
        if ins.reg_name(mem.base) != 'pc':
            return None
        return E.u32(ins.address + 8 + mem.disp)
    for i, ins in enumerate(insns):
        if ins is None: continue
        m = ins.mnemonic; n = len(ins.operands)
        try:
            if m == 'mov' and n == 2 and ins.operands[0].type == ARM_OP_REG and ins.operands[1].type == ARM_OP_IMM:
                regs[ins.reg_name(ins.operands[0].reg)] = ins.operands[1].imm & 0xffffffff
            elif m in ('add','sub') and n == 3 and ins.operands[0].type == ARM_OP_REG:
                d = ins.reg_name(ins.operands[0].reg)
                r1 = ins.reg_name(ins.operands[1].reg) if ins.operands[1].type == ARM_OP_REG else None
                if r1 == 'sp' and ins.operands[2].type == ARM_OP_IMM:
                    spof[d] = ins.operands[2].imm; regs.pop(d, None)
                elif r1 and r1 in regs and ins.operands[2].type == ARM_OP_IMM:
                    v = regs[r1] + ins.operands[2].imm if m == 'add' else regs[r1] - ins.operands[2].imm
                    regs[d] = v & 0xffffffff
            elif m == 'ldr' and n == 2 and ins.operands[0].type == ARM_OP_REG and ins.operands[1].type == ARM_OP_MEM:
                d = ins.reg_name(ins.operands[0].reg)
                v = resolve_pc_load(ins)
                if v is not None:
                    regs[d] = v
                else:
                    regs.pop(d, None); spof.pop(d, None)
            elif m in ('strh','strb','str') and n == 2 and ins.operands[1].type == ARM_OP_MEM:
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
                                guids.append((ins.address, bytes(bufs[st+k] for k in range(16))))
                                for k in range(16): bufs.pop(st+k, None)
        except Exception:
            pass
    # KEY 检测：连续两条 ldr rX,[pc] 且字面量都是小的 sl 偏移
    for i in range(len(insns)-2):
        a, b = insns[i], insns[i+1]
        if a is None or b is None: continue
        if a.mnemonic != 'ldr' or b.mnemonic != 'ldr': continue
        if len(a.operands) != 2 or len(b.operands) != 2: continue
        if a.operands[1].type != ARM_OP_MEM or b.operands[1].type != ARM_OP_MEM: continue
        if a.operands[0].type != ARM_OP_REG or b.operands[0].type != ARM_OP_REG: continue
        v1 = resolve_pc_load(a); v2 = resolve_pc_load(b)
        if v1 is None or v2 is None: continue
        if not (0 < v1 < 0x2000 and 0 < v2 < 0x2000): continue
        nxt = insns[i+2]
        ok = nxt is not None and (nxt.mnemonic == 'b' or
             (nxt.mnemonic == 'ldr' and nxt.operands[1].type == ARM_OP_MEM and nxt.reg_name(nxt.operands[1].mem.base) == 'sl'))
        if ok:
            keys.append((a.address, v1, v2))
    return guids, keys

def main():
    E = Elf2('orig_local.so')
    start = int(sys.argv[1], 0) if len(sys.argv) > 1 else FUNC
    size  = int(sys.argv[2], 0) if len(sys.argv) > 2 else SIZE
    insns = sweep(E, start, size)
    guids, keys = build_events(insns, E)
    keyat = {a: (o1, o2) for a, o1, o2 in keys}
    # 分支目标 → 命中
    ev = [(a, 'G', g) for a, g in guids]
    ev.sort()
    bound = []
    for idx, (a, _, g) in enumerate(ev):
        lo = a
        hi = ev[idx+1][0] if idx+1 < len(ev) else start+size
        hit = None
        for ins in insns:
            if ins is None or ins.address < lo or ins.address >= hi: continue
            if ins.mnemonic in ('bne','beq','b') and ins.operands and ins.operands[0].type == ARM_OP_IMM:
                t = ins.operands[0].imm
                if t in keyat:
                    hit = (t, keyat[t])
                    break
        bound.append((a, g, hit))
    for a, g, hit in bound:
        if hit:
            o1, o2 = hit[1]
            s1 = E.cstr(E.u32(SL+o1) or 0) if E.u32(SL+o1) else None
            s2 = E.cstr(E.u32(SL+o2) or 0) if E.u32(SL+o2) else None
            print('%#010x %s -> tr(%r, %r)' % (a, fmt(g), s1, s2))
        else:
            print('%#010x %s -> (无独立 KEY)' % (a, fmt(g)))

if __name__ == '__main__':
    main()
