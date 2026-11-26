# -*- coding: utf-8 -*-
"""gnemu.py - 迷你 ARM 解释器：在 getName 的 if 链上求解 uuid -> tr(key1,key2)

用法: python gnemu.py <type> <uuid> [device]
"""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gnext import Elf2, sweep, SL
from capstone.arm import ARM_OP_REG, ARM_OP_IMM, ARM_OP_MEM

NAMES = {'sp': 13, 'lr': 14, 'pc': 15, 'ip': 12, 'fp': 11, 'sl': 10, 'sb': 9}
def rn(nm):
    if nm in NAMES:
        return NAMES[nm]
    return int(nm[1:])

CONDS = ['eq','ne','cs','hs','cc','lo','mi','pl','vs','vc','hi','ls','ge','lt','gt','le']
BASES = ('b','mov','add','sub','ldr','str','orr','and','eor','mul','ldrb','strb','strh','ldrh','rsb','mvn','ldm','stm','cmp')

class Emu:
    def __init__(self, E, insns):
        self.E = E
        self.map = {i.address: i for i in insns if i is not None}
        self.mem = {}
        self.z = self.n = self.c = self.v = 0
    def u32(self, a):
        if all((a+k) in self.mem for k in range(4)):
            return sum(self.mem[a+k] << (8*k) for k in range(4))
        if a in self.mem:
            return self.mem[a]
        v = self.E.u32(a)
        return v if v is not None else 0
    def u8(self, a):
        if a in self.mem: return self.mem[a] & 0xff
        o = self.E.off(a)
        return self.E.d[o] if o is not None else 0
    def cond_ok(self, cc):
        z, n, c, v = self.z, self.n, self.c, self.v
        return {'eq': z, 'ne': not z, 'hi': (c and not z), 'ls': (not c or z),
                'cc': not c, 'cs': c, 'hs': c, 'lo': not c,
                'ge': n == v, 'lt': n != v, 'gt': (not z and n == v),
                'le': (z or n != v), 'mi': n == 1, 'pl': n == 0}.get(cc, True)
    def run(self, entry, thisptr, maxsteps=60000):
        r = [0]*16
        r[13] = 0x400000
        r[14] = 0xdeadbeef
        r[1] = thisptr
        r[0] = 0x300000
        pc = entry
        trace = os.environ.get('TRACE')
        self.hist = []
        at = os.environ.get('AT')
        for _ in range(maxsteps):
            r[15] = pc + 8   # ARM: pc 读作 当前地址+8
            if at and pc == int(at, 0):
                print('REGS', ' '.join('r%d=%#x' % (i, r[i]) for i in range(16)), '| z=%d n=%d c=%d' % (self.z, self.n, self.c))
            if trace and pc >= int(trace, 0) and pc < int(trace, 0) + int(os.environ.get('TLEN','0x200'),0):
                ins0 = self.map.get(pc)
                print('%#x %s %s' % (pc, ins0.mnemonic if ins0 else '??', ins0.op_str if ins0 else ''))
            ins = self.map.get(pc)
            if ins is None:
                return ('LOST', hex(pc))
            m, ops = ins.mnemonic, ins.operands
            cc = 'al'; base = m
            if m in CONDS:
                cc = m; base = 'b'
            else:
                for c in CONDS:
                    if m.endswith(c) and m[:-len(c)] in BASES:
                        cc = c; base = m[:-len(c)]; break
            nxt = pc + 4
            if cc != 'al' and not self.cond_ok(cc):
                pc = nxt; continue
            try:
                if base == 'mov':
                    d = rn(ins.reg_name(ops[0].reg))
                    r[d] = (ops[1].imm & 0xffffffff) if ops[1].type == ARM_OP_IMM else r[rn(ins.reg_name(ops[1].reg))]
                elif base in ('add','sub','and','orr','eor','rsb','mul'):
                    d = rn(ins.reg_name(ops[0].reg))
                    a = r[rn(ins.reg_name(ops[1].reg))]
                    if ops[2].type == ARM_OP_IMM: b = ops[2].imm & 0xffffffff
                    elif ops[2].type == ARM_OP_REG: b = r[rn(ins.reg_name(ops[2].reg))]
                    else: b = 0
                    if base == 'add': r[d] = (a + b) & 0xffffffff
                    elif base == 'sub': r[d] = (a - b) & 0xffffffff
                    elif base == 'rsb': r[d] = (b - a) & 0xffffffff
                    elif base == 'and': r[d] = a & b
                    elif base == 'orr': r[d] = a | b
                    elif base == 'eor': r[d] = a ^ b
                    else: r[d] = (a * b) & 0xffffffff
                elif base == 'cmp':
                    a = r[rn(ins.reg_name(ops[0].reg))]
                    b = (ops[1].imm & 0xffffffff) if ops[1].type == ARM_OP_IMM else r[rn(ins.reg_name(ops[1].reg))]
                    x = (a - b) & 0xffffffff
                    self.z = (x == 0); self.n = (x >> 31) & 1
                    self.c = 1 if a >= b else 0; self.v = 0
                elif base in ('ldr','ldrb','ldrh'):
                    mem = ops[1].mem
                    b = ins.reg_name(mem.base)
                    idx = ins.reg_name(mem.index) if mem.index else None
                    if b == 'pc':
                        addr = pc + 8 + mem.disp
                    elif idx:
                        addr = r[rn(b)] + r[rn(idx)]
                    else:
                        addr = r[rn(b)] + mem.disp
                    d = rn(ins.reg_name(ops[0].reg))
                    r[d] = self.u8(addr) if base == 'ldrb' else self.u32(addr)
                elif base in ('str','strb','strh'):
                    mem = ops[1].mem
                    b = ins.reg_name(mem.base)
                    addr = r[rn(b)] + mem.disp
                    v = r[rn(ins.reg_name(ops[0].reg))]
                    cnt = {'str': 4, 'strh': 2, 'strb': 1}[base]
                    for k in range(cnt):
                        self.mem[addr+k] = (v >> (8*k)) & 0xff
                elif base == 'b':
                    pc = ops[0].imm; continue
                elif base == 'bl':
                    if os.environ.get('HIST'):
                        print('HIST', ' '.join(hex(x) for x in self.hist))
                    return ('BL', hex(pc), hex(ops[0].imm), r[1], r[2])
                elif base == 'push':
                    rl = [rn(ins.reg_name(o.reg)) for o in ops if o.type == ARM_OP_REG]
                    r[13] = (r[13] - 4*len(rl)) & 0xffffffff
                    for k, rg in enumerate(rl):
                        a = r[13] + 4*k
                        v = r[rg]
                        for j in range(4):
                            self.mem[a+j] = (v >> (8*j)) & 0xff
                elif base == 'pop':
                    rl = [rn(ins.reg_name(o.reg)) for o in ops if o.type == ARM_OP_REG]
                    for k, rg in enumerate(rl):
                        a = r[13] + 4*k
                        r[rg] = sum(self.mem.get(a+j, 0) << (8*j) for j in range(4))
                    r[13] = (r[13] + 4*len(rl)) & 0xffffffff
                    if 15 in rl:
                        pc = r[15]; continue
                elif base in ('bx','ldm','stm','mvn','ldrsb','ldmib'):
                    return ('STOP', base, hex(pc))
                else:
                    return ('UNK', base, hex(pc), ins.op_str)
            except Exception as ex:
                return ('ERR', base, hex(pc), ins.op_str, str(ex))
            pc = nxt
        return ('MAX', hex(pc))

def guidbytes(uuid):
    d = uuid.strip('{}').replace('-', '')
    b = bytes.fromhex(d)
    return struct.pack('<IHH', int.from_bytes(b[0:4], 'big'), int.from_bytes(b[4:6], 'big'), int.from_bytes(b[6:8], 'big')) + b[8:16]

def main():
    E = Elf2('orig_local.so')
    insns = sweep(E, 0x41AEAC88, 14020)
    em = Emu(E, insns)
    type_ = int(sys.argv[1], 0) if len(sys.argv) > 1 else 1
    uuids = sys.argv[2].split(',') if len(sys.argv) > 2 else []
    dev = int(sys.argv[3], 0) if len(sys.argv) > 3 else 0
    for uuid in uuids:
        em.mem = {}
        this = 0x200000
        em.mem[this+8] = type_
        em.mem[this+0xc] = dev
        g = guidbytes(uuid)
        for k in range(16):
            em.mem[this+0x20+k] = g[k]
        res = em.run(0x41AEAC88, this)
        if res[0] == 'BL':
            print('%-40s tr(%r, %r)' % (uuid, E.cstr(res[3]), E.cstr(res[4])))
        else:
            print('%-40s %s' % (uuid, res))

if __name__ == '__main__':
    main()
