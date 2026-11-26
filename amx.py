# -*- coding: utf-8 -*-
"""amx.py - 在 am_dev 里查某个导入符号的全部 bl 调用点（用 plt_am 精确映射）

用法:
  python amx.py getDefaultShortcut      # 关键字模糊匹配符号名
  python amx.py _ZN10Idle_Setup18getDefaultShortcutEi --ctx 12
"""
import re, sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import plt_am

HERE = os.path.dirname(os.path.abspath(__file__))
DIS = os.path.join(HERE, 'am_full.dis')
_pat = re.compile(r'^\s+([0-9a-f]+):\s+[0-9a-f ]+\t(.*)$')


def load_dis():
    out = []
    for line in open(DIS, 'r', errors='replace'):
        m = _pat.match(line.rstrip('\n'))
        if m:
            t = m.group(2).replace('\t', ' ')
            out.append((int(m.group(1), 16), re.sub(r'\s+', ' ', t).strip()))
    return out


def build():
    rl = plt_am.relocs()
    st = plt_am.stubs()
    pltmap = {}
    for stub, slot in st.items():
        nm = rl.get(slot)
        if nm:
            pltmap[stub] = nm
    return pltmap


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return
    key = sys.argv[1]
    ctx = 8
    if '--ctx' in sys.argv:
        ctx = int(sys.argv[sys.argv.index('--ctx') + 1])
    ins = load_dis()
    pltmap = build()
    hits = {}
    for stub, nm in pltmap.items():
        if key in nm:
            hits[stub] = nm
    if not hits:
        print('no symbol matching', key)
        # 打印相近的
        for stub, nm in sorted(pltmap.items()):
            if key.split('::')[-1][:8] in nm:
                print('  candidate', hex(stub), nm)
        return
    for stub, nm in sorted(hits.items()):
        print('=== %s  (plt stub %#x) ===' % (nm, stub))
        idx = {a: i for i, (a, _t) in enumerate(ins)}
        found = 0
        for i, (a, t) in enumerate(ins):
            m = re.match(r'(bl|b)\s+0x([0-9a-f]+)$', t)
            if not m:
                continue
            if int(m.group(2), 16) != stub:
                continue
            found += 1
            print('  call @ %#x   (in sub_%#x)' % (a, fn(ins, a)))
            for j in range(max(0, i - ctx), min(len(ins), i + 3)):
                mark = '>>' if j == i else '  '
                print('   %s %5x: %s' % (mark, ins[j][0], ins[j][1]))
            print()
        if not found:
            print('  (no direct bl; 可能被取地址或经寄存器间接调用)')


def fn(ins, a):
    for i in range(len(ins) - 1, -1, -1):
        if ins[i][0] > a:
            continue
        t = ins[i][1]
        if (t.startswith('push') or t.startswith('stmdb')) and 'lr' in t:
            return ins[i][0]
        if ins[i][0] < a - 0x2000:
            break
    return a


if __name__ == '__main__':
    main()
