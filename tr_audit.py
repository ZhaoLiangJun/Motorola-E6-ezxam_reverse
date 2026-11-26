# -*- coding: utf-8 -*-
"""tr_audit.py —— 比对原库与重建库里「哪个函数调用了哪个 tr」

为什么需要：C++ 的 tr 是 Q_OBJECT 宏生成的**静态成员**，每个类一份，
上下文 = 类名。QObject::tr 的上下文是 "QObject"。同一个 (source, comment)
在不同上下文下命中不同的 .qm 条目，写错了不会编译报错、也不会崩，只是静默退回英文。

输出：按「离它最近的已导出函数符号」聚合，打印 原库/重建库 各自用到的 tr 符号。
"""
import struct, sys
from capstone import *

SYMS_OF_INTEREST = ('2trEPKc', 'trEPKcS1_', 'trEPKc')


def load(path):
    d = open(path, 'rb').read()
    e_shoff = struct.unpack_from('<I', d, 0x20)[0]
    ents = struct.unpack_from('<H', d, 0x2E)[0]
    n = struct.unpack_from('<H', d, 0x30)[0]
    secs = [struct.unpack_from('<IIIIIIIIII', d, e_shoff + i * ents) for i in range(n)]
    shstr = secs[struct.unpack_from('<H', d, 0x32)[0]]
    nb = d[shstr[4]:shstr[4] + shstr[5]]

    def nm(x):
        e = nb.find(b'\0', x)
        return nb[x:e].decode('latin1')
    by = {}
    for s in secs:
        by.setdefault(nm(s[0]), s)
    ds, st = by['.dynsym'], by['.dynstr']
    strb = d[st[4]:st[4] + st[5]]
    syms = []
    for i in range(ds[5] // ds[9]):
        o = ds[4] + i * ds[9]
        name, val, size, info, other, shndx = struct.unpack_from('<IIIBBH', d, o)
        e = strb.find(b'\0', name)
        syms.append({'name': strb[name:e].decode('latin1') if name else '',
                     'val': val, 'size': size, 'shndx': shndx})
    rel = by.get('.rel.plt')
    plt = by['.plt']
    stub2sym = {}
    if rel:
        for i in range(rel[5] // 8):
            off, info = struct.unpack_from('<II', d, rel[4] + i * 8)
            idx = info >> 8
            if idx < len(syms):
                stub2sym[plt[3] + 20 + i * 12] = syms[idx]['name']
    return d, by, syms, stub2sym


def demangle_tr(name):
    """从 _ZN...2trEPKc 里取出类名"""
    if not name.endswith('2trEPKc') and '2trEPKc' not in name:
        return None
    body = name[3:] if name.startswith('_ZN') else name
    body = body.split('2trEPKc')[0]
    # 逐段解析 <len><name>
    parts = []
    i = 0
    while i < len(body):
        j = i
        while j < len(body) and body[j].isdigit():
            j += 1
        if j == i:
            break
        ln = int(body[i:j])
        parts.append(body[j:j + ln])
        i = j + ln
    return '::'.join(parts) if parts else '?'


def audit(path, label):
    d, by, syms, stub2sym = load(path)
    text = by['.text']
    base, off, size = text[3], text[4], text[5]
    # 已导出函数：addr -> name（按地址排序，用于归属判定）
    import bisect
    fn = sorted((s['val'], s['size'], s['name']) for s in syms
                if s['shndx'] != 0 and s['val'] and s['size'])
    faddrs = [a for a, _, _ in fn]

    def owner(addr):
        i = bisect.bisect_right(faddrs, addr) - 1
        if i < 0:
            return '(未知)'
        a, sz, nmz = fn[i]
        return nmz if addr < a + sz else '(间隙)'

    trstubs = {a: n for a, n in stub2sym.items() if demangle_tr(n) is not None}
    res = {}
    for i in range(0, size, 4):
        w = struct.unpack_from('<I', d, off + i)[0]
        if (w >> 24) & 0xff != 0xeb:
            continue
        o = w & 0xffffff
        if o & 0x800000:
            o -= 0x1000000
        tgt = (base + i + 8 + o * 4) & 0xffffffff
        if tgt not in trstubs:
            continue
        who = owner(base + i)
        res.setdefault(who, set()).add(demangle_tr(trstubs[tgt]))
    print('===== %s：调用 tr 的函数 =====' % label)
    for k in sorted(res):
        print('  %-56s -> %s' % (k, ', '.join(sorted(res[k]))))
    return res


def main():
    a = audit(sys.argv[1], sys.argv[1])
    b = audit(sys.argv[2], sys.argv[2])
    print()
    print('===== 差异（同函数内 tr 上下文不一致）=====')
    same = True
    for k in sorted(set(a) | set(b)):
        if a.get(k) != b.get(k):
            same = False
            print('  %s' % k)
            print('     原库  : %s' % (', '.join(sorted(a[k])) if k in a else '(无)'))
            print('     重建库: %s' % (', '.join(sorted(b[k])) if k in b else '(无)'))
    if same:
        print('  (无差异)')


if __name__ == '__main__':
    main()
