#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""扫描依赖库（appbase/qte/am）中是否硬编码了指向 libezxam 地址范围的绝对指针。
   prelink 会把跨库引用固化为绝对地址且不留重定位，这类引用对重建库是致命的。"""
import struct, sys, os, glob


def sections(path):
    d = open(path, 'rb').read()
    e_shoff = struct.unpack_from('<I', d, 32)[0]
    es = struct.unpack_from('<H', d, 46)[0]
    n = struct.unpack_from('<H', d, 48)[0]
    snd = struct.unpack_from('<H', d, 50)[0]
    sh = []
    for i in range(n):
        o = e_shoff + i * es
        s = struct.unpack_from('<10I', d, o)
        sh.append(dict(name=s[0], type=s[1], flags=s[2], addr=s[3], off=s[4],
                       size=s[5], link=s[6], info=s[7], entsize=s[9]))
    ss = d[sh[snd]['off']:sh[snd]['off'] + sh[snd]['size']]

    def cn(i):
        e = ss.find(b'\0', i)
        return ss[i:e].decode('ascii', 'replace')
    for s in sh:
        s['name'] = cn(s['name'])
    return d, sh


def reloc_offsets(d, sh):
    """所有重定位目标地址集合（这些位置的值是链接器/加载器写的，不算硬编码）"""
    out = set()
    for tab in ('.rel.dyn', '.rel.plt'):
        for s in sh:
            if s['name'] == tab:
                for i in range(s['size'] // 8):
                    o = s['off'] + i * 8
                    r_off = struct.unpack_from('<I', d, o)[0]
                    out.add(r_off)
    return out


def main():
    lo = int(sys.argv[1], 0) if len(sys.argv) > 1 else 0x41ad8000
    hi = int(sys.argv[2], 0) if len(sys.argv) > 2 else 0x41b24000
    files = sys.argv[3:] or ['appbase_local.so', 'qte_local.so']
    print("扫描范围: 0x%08x - 0x%08x" % (lo, hi))
    for f in files:
        if not os.path.exists(f):
            print("\n[跳过] %s 不存在" % f)
            continue
        d, sh = sections(f)
        relocs = reloc_offsets(d, sh)
        hits = []
        # 只扫可写数据段（.data/.got/.bss 之外还可以扫 .data.rel.ro）
        for s in sh:
            if not (s['flags'] & 0x1):  # SHF_WRITE
                continue
            if s['type'] != 1:  # 仅 PROGBITS（.bss 文件内无内容）
                continue
            base = s['off']
            for off in range(0, s['size'] - 3, 4):
                v = struct.unpack_from('<I', d, base + off)[0]
                if lo <= v < hi:
                    va = s['addr'] + off
                    hits.append((s['name'], va, v, va in relocs))
        print("\n=== %s  命中 %d 处 ===" % (f, len(hits)))
        rel_count = sum(1 for h in hits if h[3])
        print("    其中已被重定位表覆盖（安全）: %d   硬编码（危险）: %d"
              % (rel_count, len(hits) - rel_count))
        for sec, va, v, isrel in hits[:40]:
            print("   %-10s @0x%08x -> 0x%08x  %s"
                  % (sec, va, v, 'reloc' if isrel else '!! HARDCODED'))


if __name__ == '__main__':
    main()
