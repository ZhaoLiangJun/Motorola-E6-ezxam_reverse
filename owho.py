# -*- coding: utf-8 -*-
"""owho.py - 在原库里查「哪个函数引用了某个字符串常量」

用法:
  python owho.py AppID
  python owho.py Args --all
原理: 字符串 vaddr -> 在 .got 里找保存它的槽 -> 在 .text 里找 `ldr rX,[sl,#off]`
      形态（sl = GOTBASE, off = slot-GOTBASE），也支持字面池直接指向 GOT 槽。
"""
import sys, os, re, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from elfrd import Elf
import oplt

HERE = os.path.dirname(os.path.abspath(__file__))
E = Elf(os.path.join(HERE, 'orig_local.so'))
DATA = E.data


def str_vaddrs(s):
    """返回该字符串在文件中的所有出现位置对应的 vaddr"""
    pat = s.encode('latin1') + b'\x00'
    out = []
    i = 0
    while True:
        i = DATA.find(pat, i)
        if i < 0:
            break
        for _n, _t, a, o, sz in E.secs:
            if sz and o <= i < o + sz:
                out.append(a + (i - o))
                break
        i += 1
    return out


def got_slots_for(vaddr):
    """在 .got 区间里找值等于 vaddr 的字"""
    got = [s for s in E.secs if s[2] == Elf.GOTBASE]
    if not got:
        # 用已知基址 + 大小做范围
        lo, hi = Elf.GOTBASE, Elf.GOTBASE + 0x1164
    else:
        lo, hi = Elf.GOTBASE, Elf.GOTBASE + got[0][4]
    out = []
    for a in range(lo, hi, 4):
        if E.word(a) == vaddr:
            out.append(a)
    return out


def xref_slot(slot):
    """找引用 GOT[slot] 的代码地址: 字面池里含 (slot-GOTBASE) 且附近有 ldr rX,[sl,#off]"""
    off = slot - Elf.GOTBASE
    hits = []
    for _n, t, a, o, sz in E.secs:
        if t != 1 or not sz:
            continue
        for k in range(0, sz - 4, 4):
            if struct.unpack_from('<I', DATA, o + k)[0] == off:
                hits.append(a + k)      # 这是字面池字地址
    return hits


def fn_start(a):
    ins = []
    for line in open(os.path.join(HERE, '_orig_full.txt'), 'r', errors='replace'):
        m = re.match(r'^([0-9a-f]+):\t[0-9a-f ]+\t(.*)$', line.rstrip('\n'))
        if m:
            ins.append((int(m.group(1), 16),
                        re.sub(r'\s+', ' ', m.group(2).replace('\t', ' ')).strip()))
    import bisect
    starts = [x for x, t in ins if (t.startswith('push') or t.startswith('stmdb')) and 'lr' in t]
    i = bisect.bisect_right(starts, a)
    return starts[i - 1] if i else a


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return
    s = sys.argv[1]
    vs = str_vaddrs(s)
    print('"%s" vaddr: %s' % (s, [hex(v) for v in vs]))
    for v in vs:
        slots = got_slots_for(v)
        print('  vaddr %#x -> GOT slots %s' % (v, [hex(x) for x in slots]))
        for sl in slots:
            pools = xref_slot(sl)
            print('    slot %#x 字面池引用 %s  -> 函数 %s'
                  % (sl, [hex(p) for p in pools],
                     sorted(set(hex(fn_start(p)) for p in pools))))


if __name__ == '__main__':
    main()
