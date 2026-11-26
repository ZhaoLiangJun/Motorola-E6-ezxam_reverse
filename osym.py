# -*- coding: utf-8 -*-
"""osym.py - orig_local.so 带字面池/字符串注释的反汇编视图（amsym.py 的原库版）

用法:
  python osym.py 41b15cb4              # 反汇编包含该地址的函数（最长 400 条）
  python osym.py 41b15cb4 --raw
  python osym.py 41b15a5c 41b15cb4     # 区间
  python osym.py --str 41b15cb4        # 只列出该函数引用的所有字符串/GOT 槽
"""
import sys, os, re, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from elfrd import Elf

HERE = os.path.dirname(os.path.abspath(__file__))
DIS = os.path.join(HERE, '_orig_full.txt')
BIN = os.path.join(HERE, 'orig_local.so')

_pat = re.compile(r'^([0-9a-f]+):\t[0-9a-f ]+\t(.*)$')

E = Elf(BIN)


def load_dis():
    out = []
    for line in open(DIS, 'r', errors='replace'):
        m = _pat.match(line.rstrip('\n'))
        if m:
            t = m.group(2).replace('\t', ' ')
            t = re.sub(r'\s+', ' ', t).strip()
            out.append((int(m.group(1), 16), t))
    return out


def cstr(addr, maxlen=160):
    f = E.off(addr)
    if f is None:
        return None
    e = E.data.find(b'\x00', f)
    if e < 0 or not (2 <= e - f <= maxlen):
        return None
    s = E.data[f:e]
    try:
        t = s.decode('utf-8')
    except UnicodeDecodeError:
        try:
            t = s.decode('gb18030')
        except Exception:
            return None
    if not all(32 <= ord(c) < 0xFF for c in t):
        return None
    return t


def resolve(v):
    """字面池里的 32bit 值 -> 可读说明"""
    if v is None:
        return None
    # 直接指向字符串
    s = cstr(v)
    if s:
        return '"%s"' % s
    # GOT 相对偏移
    if 0 < v < 0x40000:
        g = E.word(Elf.GOTBASE + v)
        if g:
            s2 = cstr(g)
            if s2:
                return 'GOT[%#x] -> "%s"' % (v, s2)
            return 'GOT[%#x] -> %#x' % (v, g)
    # 绝对地址槽
    w = E.word(v)
    if w and w != v:
        s2 = cstr(w)
        if s2:
            return '*(%#x) -> "%s"' % (v, s2)
    return None


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    only_str = '--str' in sys.argv
    raw = '--raw' in sys.argv
    if not args:
        print(__doc__)
        return
    ins = load_dis()
    idx_of = {}
    for i, (a, _t) in enumerate(ins):
        idx_of[a] = i

    starts = [a for a, t in ins
              if (t.startswith('push') or t.startswith('stmdb')) and 'lr' in t]
    starts.sort()
    import bisect

    def fn_start(a):
        i = bisect.bisect_right(starts, a)
        return starts[i - 1] if i else None

    def annot(a, t):
        notes = []
        m = re.match(r'(bl|b|bx|blx)\s+(?:0x)?([0-9a-f]+)$', t)
        if m:
            tgt = int(m.group(2), 16)
            fs = fn_start(tgt)
            if fs is not None and fs != tgt:
                notes.append('-> %x+%#x' % (fs, tgt - fs))
        m = re.match(r'ldr\s+(\w+), \[pc, #(-?\d+)\]', t)
        if m:
            pool = a + 8 + int(m.group(2))
            v = E.word(pool)
            r = resolve(v)
            if r:
                notes.append('%s = %s' % (m.group(1), r))
            elif v is not None:
                notes.append('%s = %#x' % (m.group(1), v))
        m = re.match(r'add\s+(\w+), pc, #(-?\d+)$', t)
        if m:
            tgt = a + 8 + int(m.group(2))
            r = resolve(tgt)
            notes.append('adr %#x%s' % (tgt, (' = ' + r) if r else ''))
        return notes

    if len(args) == 1:
        a = int(args[0], 16)
        fs = fn_start(a) or a
        i0 = idx_of.get(fs, 0)
        i1 = len(ins)
        j = bisect.bisect_right(starts, fs)
        if j < len(starts):
            i1 = idx_of.get(starts[j], len(ins))
        if not raw and i1 - i0 > 400:
            i1 = i0 + 400
        print('=== sub_%x  (%x..%x, %d ins) ===' % (fs, fs, ins[i1 - 1][0], i1 - i0))
        rng = range(i0, i1)
    else:
        a0, a1 = int(args[0], 16), int(args[1], 16)
        print('=== range %x..%x ===' % (a0, a1))
        rng = range(idx_of.get(a0, 0), min(len(ins), idx_of.get(a1, len(ins))))

    seen = set()
    for i in rng:
        a, t = ins[i]
        nt = annot(a, t)
        if only_str:
            for n in nt:
                if '"' in n or 'GOT[' in n:
                    if n not in seen:
                        seen.add(n)
                        print('  %5x: %s' % (a, n))
            continue
        print('  %5x: %-42s%s' % (a, t, ('   ; ' + ' | '.join(nt)) if nt else ''))


if __name__ == '__main__':
    main()
