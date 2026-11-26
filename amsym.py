# -*- coding: utf-8 -*-
"""amsym.py - am_dev 带符号/字符串注释的反汇编视图

用法:
  python amsym.py 3a7d8            # 反汇编包含 0x3a7d8 的整个函数
  python amsym.py 3a7d8 --raw      # 不截断(默认最长 400 条)
  python amsym.py 3a7d8 3ad00      # 反汇编区间
  python amsym.py --find 46b50     # 只列出函数边界

注释规则:
  bl 0x2xxxx  -> .plt stub -> R_ARM_JUMP_SLOT 符号名
  ldr rX,[pc,#N] / add rX,pc,#N -> 字面池值; 若指向 ASCII 则直接显示字符串,
      若是 GOT 槽则再跳一层取字符串
"""
import sys, os, re, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import plt_am

HERE = os.path.dirname(os.path.abspath(__file__))
DIS = os.path.join(HERE, 'am_full.dis')
BIN = os.path.join(HERE, 'am_dev')

_pat = re.compile(r'^\s+([0-9a-f]+):\s+[0-9a-f ]+\t(.*)$')


def load_dis():
    out = []
    for line in open(DIS, 'r', errors='replace'):
        m = _pat.match(line.rstrip('\n'))
        if m:
            out.append((int(m.group(1), 16), m.group(2).strip()))
    return out


class Bin:
    def __init__(self, path):
        self.d = open(path, 'rb').read()
        d = self.d
        e_shoff = struct.unpack_from('<I', d, 0x20)[0]
        ents = struct.unpack_from('<H', d, 0x2E)[0]
        n = struct.unpack_from('<H', d, 0x30)[0]
        self.secs = []
        for i in range(n):
            o = e_shoff + i * ents
            nameoff, typ, flags, addr, off, size = struct.unpack_from('<IIIIII', d, o)
            self.secs.append((nameoff, typ, addr, off, size))

    def off(self, addr):
        for _n, _t, a, o, s in self.secs:
            if s and a <= addr < a + s:
                return o + (addr - a)
        return None

    def word(self, addr):
        f = self.off(addr)
        if f is None or f + 4 > len(self.d):
            return None
        return struct.unpack_from('<I', self.d, f)[0]

    def cstr(self, addr, maxlen=120):
        f = self.off(addr)
        if f is None:
            return None
        e = self.d.find(b'\x00', f)
        if e < 0 or not (2 <= e - f <= maxlen):
            return None
        s = self.d[f:e]
        if all(32 <= c < 127 for c in s):
            return s.decode('ascii')
        return None


B = Bin(BIN)


def resolve(v):
    """v: 字面池里的 32bit 值。返回可读说明或 None"""
    if v is None:
        return None
    s = B.cstr(v)
    if s:
        return '"%s"' % s
    # GOT / 指针槽: 再跳一层
    w = B.word(v)
    if w is not None and w != v:
        s2 = B.cstr(w)
        if s2:
            return '-> %#x "%s"' % (w, s2)
        if w and w < 0x100000:
            return '-> %#x (off %#x)' % (w, w)
    return None


def main():
    args = [a for a in sys.argv[1:]]
    raw = '--raw' in args
    args = [a for a in args if not a.startswith('--')]
    if not args:
        print(__doc__)
        return
    ins = load_dis()
    idx_of = dict((a, i) for i, (a, _t) in enumerate(ins))

    # 函数起点: push {..lr..} / stmdb sp!,{..lr..}
    starts = [a for a, t in ins
              if (t.startswith('push') or t.startswith('stmdb')) and 'lr' in t]
    starts.sort()

    def fn_start(a):
        lo, hi, best = 0, len(starts) - 1, None
        while lo <= hi:
            mid = (lo + hi) // 2
            if starts[mid] <= a:
                best = starts[mid]
                lo = mid + 1
            else:
                hi = mid - 1
        return best

    rl = plt_am.relocs()
    st = plt_am.stubs()
    pltmap = {}
    for stub, slot in st.items():
        nm = rl.get(slot)
        if nm:
            pltmap[stub] = nm

    def annot(a, t):
        notes = []
        m = re.match(r'(bl|b|bx|blx)\s+(?:0x)?([0-9a-f]+)$', t)
        if m:
            tgt = int(m.group(2), 16)
            if tgt in pltmap:
                notes.append('=> ' + pltmap[tgt])
            else:
                fs = fn_start(tgt)
                if fs is not None and fs != tgt:
                    notes.append('-> sub_%x+%#x' % (fs, tgt - fs))
        # ldr rX, [pc, #N]
        m = re.match(r'ldr\s+(\w+), \[pc, #(-?\d+)\]', t)
        if m:
            pool = a + 8 + int(m.group(2))
            v = B.word(pool)
            r = resolve(v)
            if r:
                notes.append('%s = %s' % (m.group(1), r))
            elif v is not None and v > 0x10000:
                notes.append('%s = %#x' % (m.group(1), v))
        # add rX, pc, #N  (ADR)
        m = re.match(r'add\s+(\w+), pc, #(-?\d+)$', t)
        if m:
            tgt = a + 8 + int(m.group(1) and m.group(2))
            r = resolve(tgt)
            notes.append('adr %#x%s' % (tgt, (' ' + r) if r else ''))
        return notes

    if len(args) == 1:
        a = int(args[0], 16)
        fs = fn_start(a)
        if fs is None:
            print('no function boundary found for %#x' % a)
            fs = a
        i0 = idx_of.get(fs, 0)
        i1 = idx_of.get(a, i0)
        for s in starts:
            if s > a:
                i1 = idx_of.get(s)
                break
        if i1 is None:
            i1 = len(ins)
        if not raw and i1 - i0 > 400:
            i1 = i0 + 400
        print('=== sub_%x  (%#x..%#x, %d ins) ===' % (fs, fs, ins[i1 - 1][0], i1 - i0))
        rng = range(i0, i1)
    else:
        a0, a1 = int(args[0], 16), int(args[1], 16)
        fs = fn_start(a0) or a0
        print('=== sub_%x  range %#x..%#x ===' % (fs, a0, a1))
        rng = range(idx_of[a0], min(len(ins), idx_of.get(a1, len(ins))))

    for i in rng:
        a, t = ins[i]
        nt = annot(a, t)
        print('  %5x: %-42s%s' % (a, t, ('   ; ' + ' | '.join(nt)) if nt else ''))


if __name__ == '__main__':
    main()
