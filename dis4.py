# -*- coding: utf-8 -*-
"""dis4.py - 带「寄存器常量追踪」的 ARM 反汇编（比 dis2.py 多一步数据流）

为什么需要：
  本库大量代码是这种两级取址：
      ldr r3,[pc,#0x594]      ; 字面池里放的是 GOT 偏移 0x0d54
      ldr r1,[sl,r3]          ; 真正的 QString 构造参数
  dis2.py 只解 [sl,#imm] 的直接形式，遇到上面这种"偏移先进寄存器"的写法
  就只剩一句 `ldr r1,[sl,r3]`，参数是什么看不出来——而这恰好是
  writeEntry(组名, 键名, 值) 三个字符串参数最关键的地方。

做法：
  线性扫一遍，维护 rX 的"已知值"（字面量常量 / GOT 槽解析出的字符串），
  遇到 ldr rX,[sl,rY] 用 rY 的常量解；遇到 bl 就把 r0..r3 的已知值打到注释里。

用法:
  python dis4.py 0x41afd844 2168
  python dis4.py 0x41afd844 2168 --from 0x41afdd90
"""
import sys, os, json, struct, bisect

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pltmap
from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM

PATH = '_origlib/libezxam.so.1'
f, segs, byname, stubmap = pltmap.build(PATH)
sv = json.load(open('orig_symvals.json'))

addr2name = {}
for k, v in sv.items():
    if isinstance(v, int) and v:
        addr2name.setdefault(v, k)
funcs = sorted(addr2name)
SL = 0x41b229f0


def rd(va, n):
    for (v, o, s) in segs:
        if v <= va < v + s:
            return f[va - v + o: va - v + o + n]
    return b''


def u32(va):
    b = rd(va, 4)
    return struct.unpack('<I', b)[0] if len(b) == 4 else None


def cstr(va, lim=80):
    b = rd(va, lim)
    if not b:
        return None
    s = b.split(b'\x00')[0]
    if not s:
        return None
    try:
        t = s.decode('latin1')
    except Exception:
        return None
    if any(not (32 <= ord(c) < 127) for c in t):
        return None
    return t


def symat(a):
    if a in addr2name:
        return pltmap.demangle(addr2name[a])
    i = bisect.bisect_right(funcs, a)
    if i:
        base = funcs[i - 1]
        if a - base < 0x800:
            return '%s+%#x' % (pltmap.demangle(addr2name[base]), a - base)
    return None


def gotstr(off):
    p = u32(SL + off)
    if not p:
        return None
    return cstr(p)


def split_ops(ops):
    """把 'r1, [sl, r3]' 拆成 (dst, mem_parts) 或 (dst, imm)"""
    if '[' not in ops:
        return [x.strip() for x in ops.split(',')]
    i = ops.index('[')
    dst = ops[:i].strip().rstrip(',')
    body = ops[i + 1:ops.rindex(']')]
    mem = [x.strip() for x in body.split(',')]
    return [dst] + mem


def main():
    a = int(sys.argv[1], 16)
    n = int(sys.argv[2]) if len(sys.argv) > 2 else 256
    start = None
    if '--from' in sys.argv:
        start = int(sys.argv[sys.argv.index('--from') + 1], 16)
    md = Cs(CS_ARCH_ARM, CS_MODE_ARM)
    md.detail = True
    code = rd(a, n)
    regs = {}
    for ins in md.disasm(code, a):
        if start is not None and ins.address < start:
            continue
        note = []
        m, ops = ins.mnemonic, ins.op_str
        p = split_ops(ops)
        if m == 'ldr' and len(p) >= 2 and p[1] == 'pc':
            try:
                imm = int(p[2].lstrip('#'), 0)
            except Exception:
                imm = 0
            v = u32((ins.address + 8 + imm) & ~3)
            if v is not None:
                regs[p[0]] = ('lit', v)
                note.append('pool=%#x' % v)
        elif m == 'ldr' and len(p) >= 2 and p[1] in ('sl', 'sb', 'fp', 'ip'):
            off = None
            if len(p) == 3:
                if p[2].startswith('#'):
                    off = int(p[2].lstrip('#'), 0)
                else:
                    st = regs.get(p[2])
                    if st and st[0] == 'lit':
                        off = st[1]
            if off is not None:
                s = gotstr(off)
                if s:
                    regs[p[0]] = ('gotstr', '%r@%#x' % (s, off))
                    note.append('%r (%#x)' % (s, off))
                else:
                    regs[p[0]] = ('got', off)
                    note.append('got[%#x]' % off)
            else:
                regs[p[0]] = ('unk', None)
        elif m == 'mov' and len(p) == 2 and p[1].startswith('#'):
            try:
                regs[p[0]] = ('lit', int(p[1].lstrip('#'), 0))
            except Exception:
                regs.pop(p[0], None)
        elif m in ('add', 'sub') and len(p) == 3:
            a1 = regs.get(p[1])
            a2 = regs.get(p[2]) if not p[2].startswith('#') else ('lit', int(p[2].lstrip('#'), 0))
            if a1 and a2 and a1[0] == 'lit' and a2[0] == 'lit':
                regs[p[0]] = ('lit', a1[1] + a2[1] if m == 'add' else a1[1] - a2[1])
            else:
                regs.pop(p[0], None)
        elif m == 'bl':
            args = []
            for r in ('r0', 'r1', 'r2', 'r3'):
                v = regs.get(r)
                if v and v[0] == 'gotstr':
                    args.append(v[1])
            tgt = ins.operands[0].imm if ins.operands else 0
            note.append('-> %s' % (symat(tgt) or ('sub_%08x' % tgt)))
            if args:
                note.append('args: ' + ' , '.join(args))
        elif m == 'blx' or m.startswith('b'):
            pass
        print('%08x  %-8s %-40s ; %s' % (ins.address, m, ops, ' | '.join(note)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
