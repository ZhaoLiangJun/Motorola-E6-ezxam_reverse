# -*- coding: utf-8 -*-
"""sizecmp.py - 用"符号体量"审计签名/实现是否对得上

原理：同一个导出符号，原库与本库的实现规模应当大致相当。
      如果出现 12B vs 352B 这种数量级差异，几乎一定是**原型不兼容**
      （例：`AM_RegistryManager_getAppParentFolderUid` 原库是
       `(const char*, char*)` 自己 new 一个管理器，我们却写成
       `(self, QString&, QString&)` 三层转发 → 12B，调用即踩内存）。

用法:
  python sizecmp.py                 # 列出所有可疑差异
  python sizecmp.py --all           # 连正常的一起列
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from edis import ArmElf

HERE = os.path.dirname(os.path.abspath(__file__))
ORIG = os.path.join(HERE, 'orig_local.so')
OURS = os.path.join(HERE, 'libezxam_reconstructed.so.1.0.0')


def syms(path):
    E = ArmElf(path)
    d = {}
    for v, n, s in E.syms:
        d.setdefault(n, (v, s))
    return d


def main():
    show_all = '--all' in sys.argv
    A = syms(ORIG)
    B = syms(OURS)
    rows = []
    for n, (va, sa) in A.items():
        if n not in B:
            rows.append((n, sa, None, '本库缺失'))
            continue
        vb, sb = B[n]
        if sa == 0 and sb == 0:
            continue
        if sa == 0 or sb == 0:
            rows.append((n, sa, sb, '一方无大小信息'))
            continue
        ratio = float(sb) / sa
        if ratio < 0.45 or ratio > 2.4:
            rows.append((n, sa, sb, '体量比 %.2f' % ratio))
    miss = [r for r in rows if r[3] == '本库缺失']
    odd = [r for r in rows if r[3] != '本库缺失']
    odd.sort(key=lambda r: (float(r[2]) / r[1]) if r[1] and r[2] is not None else 9)

    print('原库导出 %d 个，本库缺失 %d 个，体量异常 %d 个\n' % (len(A), len(miss), len(odd)))
    print('=== 体量异常（最可能是原型/实现不符，按可疑度排序）===')
    for n, sa, sb, why in odd:
        print('  orig=%-6s ours=%-6s %-14s %s' % (sa, sb, why, n))
    if show_all:
        print('\n=== 本库缺失 ===')
        for n, sa, sb, why in miss:
            print('  orig=%-6s %s' % (sa, n))


if __name__ == '__main__':
    main()
