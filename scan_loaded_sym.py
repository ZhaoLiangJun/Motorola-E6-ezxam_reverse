# -*- coding: utf-8 -*-
"""scan_loaded_sym.py - 只在 am 已加载的库中查找某符号的 DEF 归属
用法: python scan_loaded_sym.py AM_Mutex7setBusy
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dev


def main():
    want = sys.argv[1] if len(sys.argv) > 1 else 'AM_Mutex'
    c = dev.connect()
    out = dev.run(c, 'cat /proc/%s/maps' % (sys.argv[2] if len(sys.argv) > 2 else '2890'), 4)
    libs = []
    for line in out.replace('\r', '').splitlines():
        p = line.split()
        if not p or '/' not in p[-1]:
            continue
        path = p[-1]
        if path.endswith('.so') or '.so.' in path:
            if path not in libs:
                libs.append(path)
    print('am 已加载 %d 个库' % len(libs))
    for p in libs:
        o = dev.run(c, '/ram/tmp/dynsym %s %s' % (p, want), 2)
        for line in o.replace('\r', '').splitlines():
            line = line.strip()
            if not line or 'no match' in line or 'Segmentation' in line or line.startswith('#'):
                continue
            if ' DEF ' in line:
                print('DEF  %-46s %s' % (p.split('/')[-1], line))
            elif ' UND ' in line:
                print('und  %-46s %s' % (p.split('/')[-1], line))


if __name__ == '__main__':
    main()
