# -*- coding: utf-8 -*-
"""scan_sym.py - 在设备上逐个库解析 .dynsym，找出某符号的 DEF/UND 归属
用法: python scan_sym.py AM_Mutex7setBusy
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dev

DIRS = ['/usr/lib/ezx/lib', '/usr/lib', '/lib']


def main():
    want = sys.argv[1] if len(sys.argv) > 1 else 'AM_Mutex'
    c = dev.connect()
    files = []
    for d in DIRS:
        out = dev.run(c, 'find %s -maxdepth 1 -name "*.so*"' % d, 4)
        for line in out.replace('\r', '').splitlines():
            line = line.strip()
            if line.startswith(d + '/') and line not in files:
                files.append(line)
    print('扫描 %d 个库文件' % len(files))
    for p in files:
        out = dev.run(c, '/ram/tmp/dynsym %s %s' % (p, want), 2)
        for line in out.splitlines():
            line = line.strip()
            if not line or 'no match' in line or line.startswith('#') or 'Segmentation' in line:
                continue
            if line.endswith('DEF') or ' DEF ' in line:
                print('%-52s %s' % (p, line))


if __name__ == '__main__':
    main()
