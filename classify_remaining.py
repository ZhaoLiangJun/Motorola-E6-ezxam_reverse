#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
classify_remaining.py - 复核当前"原库引用/重建库未引用"差异的实际性质
纯 Python。分类：EZX内部类 / Qt平台类 / libc / 已知安全取舍
"""
import json, os, re

BASE = os.path.dirname(os.path.abspath(__file__))
sym = json.load(open(os.path.join(BASE, "symbols.json"), encoding="utf-8"))
undef_orig = set(n for n, i in sym.items() if not i.get("defined"))

undef_recon = set()
for line in open(os.path.join(BASE, "recon_undef.txt"), encoding="utf-8"):
    p = line.split()
    if len(p) >= 2 and p[0] == "U":
        undef_recon.add(p[1])

only_orig = sorted(undef_orig - undef_recon)
print(f"当前差异外部符号: {len(only_orig)} 个\n")

def demangle_hint(m):
    # 极简：提取类名
    m2 = re.match(r"_ZN?K?(\d+)([A-Za-z_]\w*)", m)
    if m2:
        ln = int(m2.group(1))
        return m2.group(2)[:ln]
    return m

# 分类
ezx_internal = []
qt_platform = []
libc = []
for s in only_orig:
    if s.startswith("_Z") and re.search(r"AM_|Idle_|SETUP_|UTIL_|Dmt|DRM|RES_|PHONE_|QPTF|SysSrv|SysService", s):
        ezx_internal.append(s)
    elif s.startswith("_Z") and re.search(r"\d+Q[A-Z]", s):
        qt_platform.append(s)
    else:
        libc.append(s)

print("=== EZX 内部类 (需逐条核实是否本库应实现) ===")
for s in ezx_internal:
    print(f"  [{demangle_hint(s)}] {s}")

print(f"\n=== Qt 平台类 (libqte-mt 运行期解析, 等价安全) ===  共 {len(qt_platform)} 个")
bycls = {}
for s in qt_platform:
    bycls.setdefault(demangle_hint(s), []).append(s)
for c in sorted(bycls):
    print(f"  {c}: {len(bycls[c])}")

print(f"\n=== libc/其它 (C 函数) ===  共 {len(libc)} 个")
for s in libc:
    print(f"  {s}")
