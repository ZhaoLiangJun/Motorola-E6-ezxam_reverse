# -*- coding: utf-8 -*-
"""am_dev 调用图分析：找 am 调用 libezxam / launcher 相关 API 的调用点及其调用者。
用法：python amxref2.py <sym-substr> [...]   （无参数则打印预置关注列表）
"""
import re, sys, subprocess, os, json

HERE = os.path.dirname(os.path.abspath(__file__))
DIS = os.path.join(HERE, 'am_full.dis')
RB = 'D:/cygwin/home/ezx/arm-linux-gcc/bin'

def load_dis():
    insns = []   # (addr, text)
    pat = re.compile(r'^\s+([0-9a-f]+):\s+[0-9a-f ]+\t(.*)$')
    for line in open(DIS, 'r', errors='replace'):
        m = pat.match(line.rstrip('\n'))
        if m:
            insns.append((int(m.group(1), 16), m.group(2).strip()))
    return insns

def load_syms():
    out = subprocess.run([RB + '/arm-linux-readelf.exe', '-sW',
                          os.path.join(HERE, 'am_dev')],
                         capture_output=True, text=True,
                         env=dict(os.environ, PATH='D:/cygwin/bin;' + os.environ.get('PATH', ''))).stdout
    syms = []
    for line in out.splitlines():
        p = line.split()
        if len(p) >= 8 and p[3] in ('FUNC', 'OBJECT') and p[1]:
            try:
                val = int(p[1], 16)
            except ValueError:
                continue
            syms.append((p[7], val, p[6]))
    return syms

def main():
    pats = sys.argv[1:] or ['LauncherInterface', 'appStart', 'ActionData', 'AM_AppObject',
                            'getAppObjById', 'getExecFullPath', 'getAppLockEnabled', 'verifySecurity']
    insns = load_dis()
    byaddr = {a: t for a, t in insns}
    syms = load_syms()
    plt2sym = {}
    for name, val, bind in syms:
        if bind in ('UND', 'GLOBAL', 'WEAK') and 0x21600 <= val < 0x26000:
            plt2sym.setdefault(val, name)
    # 找 bl <plt addr> 的调用点
    xref = {}
    for a, t in insns:
        m = re.match(r'bl\s+0x([0-9a-f]+)\s*;?\s*(?:<(.*)>)?', t)
        if not m:
            continue
        tgt = int(m.group(1), 16)
        name = m.group(2) or plt2sym.get(tgt)
        if name:
            xref.setdefault((tgt, name), []).append(a)
    print('=== call sites for patterns:', pats)
    for (tgt, name), sites in sorted(xref.items()):
        if any(p in name for p in pats):
            print('%-70s plt=%#x  sites=%s' % (name, tgt, ','.join(hex(s) for s in sites[:12])))
    # bl 目标集合 → 函数起点（近似）
    starts = set()
    for a, t in insns:
        m = re.match(r'bl\s+0x([0-9a-f]+)', t)
        if m:
            starts.add(int(m.group(1), 16))
        elif re.match(r'(push|stmdb|str\s+lr)', t):
            starts.add(a)
    json.dump(sorted(starts), open('/tmp/am_funcstarts.json', 'w'))
    print('=== funcstarts:', len(starts), 'insns:', len(insns))

if __name__ == '__main__':
    main()
