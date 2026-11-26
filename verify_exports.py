# -*- coding: utf-8 -*-
"""verify_exports.py —— 重建库 / 原厂库 导出面一致性验收（退出码非 0 = 不通过）

检查三件事：
  1. 原库导出而我们没导出（缺符号 -> 别的库 dlopen 会失败）       必须 0
  2. 我们导出而原库没有（符号抢占隐患）                          必须 0（有 exports.map 后）
  3. 同名符号体量比（ours/orig）落在 [0.45, 2.4] 之外的重点项    仅提示

用法：python verify_exports.py [ours.so] [orig.so]
"""
import struct
import sys
import os

HERE = os.path.dirname(os.path.abspath(__file__))


def dynsym(path):
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
    out = {}
    for i in range(ds[5] // ds[9]):
        o = ds[4] + i * ds[9]
        name, val, size, info, other, shndx = struct.unpack_from('<IIIBBH', d, o)
        if not name:
            continue
        e = strb.find(b'\0', name)
        s = strb[name:e].decode('latin1')
        if s:
            out[s] = (val, size, shndx)
    return out


def main():
    ours_p = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        HERE, 'libezxam_reconstructed.so.1.0.0')
    orig_p = sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, 'orig_local.so')
    o = dynsym(orig_p)
    u = dynsym(ours_p)
    odef = {k: v for k, v in o.items() if v[2] != 0}
    udef = {k: v for k, v in u.items() if v[2] != 0}

    miss = sorted(set(odef) - set(udef))
    extra = sorted(set(udef) - set(odef))
    print('原库导出 %d / 本库导出 %d' % (len(odef), len(udef)))
    print('缺符号 %d%s' % (len(miss), '' if not miss else '  <<< FAIL'))
    for k in miss:
        print('   MISS %s (orig %dB)' % (k, odef[k][1]))
    print('多出符号 %d%s' % (len(extra), '' if not extra else '  <<< FAIL'))
    for k in extra:
        print('   EXTRA %s' % k)

    thin = []
    for k, v in sorted(odef.items()):
        ov, mv = v[1], udef.get(k, (0, 0, 1))[1]
        if ov >= 200 and (mv == 0 or mv < ov * 0.45):
            thin.append((mv * 100.0 / ov if ov else 0, k, ov, mv))
    print('体量明显偏小的实现 %d 项（仅提示，不影响加载）' % len(thin))
    for pct, k, ov, mv in thin:
        print('   %5.1f%%  orig=%6dB ours=%5dB  %s' % (pct, ov, mv, k))

    ok = not miss and not extra
    print('\n结论: %s' % ('PASS 导出面与原库一致' if ok else 'FAIL'))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
