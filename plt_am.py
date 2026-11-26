# -*- coding: utf-8 -*-
"""plt_am.py - 把 am_dev 的 .plt stub 地址映射到重定位符号名
原理: stub 形如
    add ip, pc, #0
    add ip, ip, #imm
    ldr pc, [ip, #off]!
首次执行时 ip = (addr+8) + imm, 槽地址 = ip + off; 该槽在 R_ARM_JUMP_SLOT
重定位表里对应一个符号。
"""
import re, sys, os, struct, subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
DIS = os.path.join(HERE, 'am_full.dis')
BIN = os.path.join(HERE, 'am_dev')
RB = 'D:/cygwin/home/ezx/arm-linux-gcc/bin/arm-linux-readelf.exe'


def relocs():
    env = dict(os.environ, PATH='D:/cygwin/bin;' + os.environ.get('PATH', ''))
    out = subprocess.run([RB, '-rW', BIN], capture_output=True, text=True, env=env).stdout
    m = {}
    sec = None
    for line in out.splitlines():
        s = line.strip()
        if s.startswith('Relocation section'):
            sec = s
            continue
        p = s.split()
        if len(p) >= 5 and p[2] == 'R_ARM_JUMP_SLOT':
            try:
                off = int(p[0], 16)
            except ValueError:
                continue
            m[off] = p[4]
    return m


def stubs():
    """扫汇编里 3 条一组的 PLT stub, 得到 (stub_addr, slot_addr)"""
    res = {}
    lines = []
    pat = re.compile(r'^\s+([0-9a-f]+):\s+[0-9a-f ]+\t(.*)$')
    for line in open(DIS, 'r', errors='replace'):
        mm = pat.match(line.rstrip('\n'))
        if mm:
            lines.append((int(mm.group(1), 16), mm.group(2).strip()))
    i = 0
    while i + 2 < len(lines):
        a, t0 = lines[i]
        _, t1 = lines[i + 1]
        _, t2 = lines[i + 2]
        m0 = re.match(r'add\s+ip, pc, #0', t0)
        m1 = re.match(r'add\s+ip, ip, #(\d+)', t1)
        m2 = re.match(r'ldr\s+pc, \[ip, #(\d+)\]!', t2)
        if m0 and m1 and m2:
            imm = int(m1.group(1))
            off = int(m2.group(1))
            ip = (a + 8) + imm
            res[a] = ip + off
            i += 3
        else:
            i += 1
    return res


def main():
    rl = relocs()
    st = stubs()
    q = sys.argv[1:]
    if not q:
        print('relocs=%d stubs=%d' % (len(rl), len(st)))
        return
    for a in q:
        a = int(a, 16)
        slot = st.get(a)
        print('%#x -> slot %s -> %s' % (a, hex(slot) if slot else None, rl.get(slot, '?')))


if __name__ == '__main__':
    main()
