# -*- coding: utf-8 -*-
"""dis2.py - 带 GOT 字面量解析的 ARM 反汇编（比 adis.py 多解析 ldr rX,[sl,#imm]）

用法: python dis2.py <addr_hex> [len] [--no-got]
  addr 可用函数名子串（在 orig_symvals.json 里查）

输出每行:
  <addr> <指令>   ; <注释>
注释规则:
  bl 目标 -> 符号名
  ldr rX,[sl,#imm] / [sb,#imm] / [fp,#imm]  -> GOT 槽内容（字符串 or 符号名 or 地址）
  ldr rX,[pc,#imm]                          -> 字面量池内容
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
    if not s or len(s) != len(b.split(b'\x00')[0]):
        return None
    try:
        t = s.decode('latin1')
    except Exception:
        return None
    if not t or any(not (32 <= ord(c) < 127) for c in t):
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


def describe(v):
    """把一个 32 位值描述成 字符串 / 符号 / 地址"""
    if v is None:
        return '?'
    s = cstr(v)
    if s:
        return repr(s)
    n = symat(v)
    if n:
        return '<%s>' % n
    return '%#x' % v


arg = sys.argv[1]
if arg.startswith('0x'):
    base = int(arg, 16)
    i = bisect.bisect_right(funcs, base)
    size = int(sys.argv[2], 0) if len(sys.argv) > 2 and not sys.argv[2].startswith('-') \
        else (funcs[i] - base if i < len(funcs) else 0x2000)
else:
    hits = [(a, n) for a, n in addr2name.items() if arg in n]
    if not hits:
        raise SystemExit('no symbol matches ' + arg)
    base, name = sorted(hits)[0]
    i = bisect.bisect_right(funcs, base)
    size = (funcs[i] - base) if i < len(funcs) else 0x2000

print('func %s @%#x size=%d' % (pltmap.demangle(addr2name.get(base, '?')), base, size))

code = rd(base, size)
md = Cs(CS_ARCH_ARM, CS_MODE_ARM)
md.skipdata = True
ins = list(md.disasm(code, base))

# GOT 基址
got = None
for k in range(len(ins) - 1):
    a = ins[k]
    if a.mnemonic == 'ldr' and '[pc' in a.op_str:
        try:
            imm = int(a.op_str.split('#')[1].rstrip(']'), 16)
        except Exception:
            continue
        v = u32(a.address + 8 + imm)
        nxt = ins[k + 1]
        if nxt.mnemonic == 'add' and nxt.op_str.startswith(a.op_str.split(',')[0]) and 'pc' in nxt.op_str:
            got = (nxt.address + 8 + v) & 0xFFFFFFFF
            break
if got is None:
    got = 0x41b229f0

# 记录每个寄存器当前含义（GOT offset），用于把 ldr rX,[rY] 解析成 GOT 内容
reg_got = {}

for a in ins:
    ann = []
    if a.mnemonic == 'bl':
        try:
            tgt = int(a.op_str.lstrip('#'), 16)
            nm = stubmap.get(tgt) or addr2name.get(tgt)
            ann.append('call %s' % (pltmap.demangle(nm) if nm else '%#x' % tgt))
        except Exception:
            pass
    elif a.mnemonic in ('b', 'beq', 'bne', 'bgt', 'blt', 'bge', 'ble', 'bhi', 'bls', 'bcs', 'bcc', 'bmi', 'bpl', 'bvs', 'bvc'):
        try:
            tgt = int(a.op_str.lstrip('#'), 16)
            ann.append('-> %#x' % tgt)
        except Exception:
            pass
    elif a.mnemonic == 'ldr':
        # ldr rX, [pc, #imm]
        if '[pc' in a.op_str:
            try:
                imm = int(a.op_str.split('#')[1].rstrip(']'), 16)
            except Exception:
                imm = None
            if imm is not None:
                v = u32(a.address + 8 + imm)
                if v is not None:
                    if v < 0x1000:
                        p = u32((got + v) & 0xFFFFFFFF)
                        ann.append('GOT[%#x] = %s' % (v, describe(p)))
                        reg_got[a.op_str.split(',')[0].strip()] = v
                    else:
                        ann.append('lit %s' % describe(v))
        else:
            # ldr rX, [rY, #imm]
            try:
                body = a.op_str.split('[', 1)[1].rstrip(']')
                parts = [p.strip() for p in body.split(',')]
                base_reg = parts[0]
                off = int(parts[1].lstrip('#'), 16) if len(parts) > 1 else 0
                if base_reg in reg_got:
                    p = u32((got + reg_got[base_reg] + off) & 0xFFFFFFFF)
                    ann.append('GOT[%#x] = %s' % (reg_got[base_reg] + off, describe(p)))
            except Exception:
                pass
    if a.mnemonic == 'add' and 'pc' in a.op_str:
        try:
            lhs = a.op_str.split(',')[0].strip()
            rhs = a.op_str.split(',')[1].strip()
            if rhs == 'pc':
                v = int(a.op_str.split('#')[1], 16)
                ann.append('=> %#x (GOT base)' % ((a.address + 8 + v) & 0xFFFFFFFF))
        except Exception:
            pass
    if a.mnemonic == 'mov' and a.op_str.startswith('r') and ', #' in a.op_str:
        pass

    print('%#x  %-8s %-38s %s' % (a.address, a.mnemonic, a.op_str,
                                  ('; ' + ' | '.join(ann)) if ann else ''))
