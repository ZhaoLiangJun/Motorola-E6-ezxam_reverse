#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
attribute_diff.py - 把"原库引用但重建库未引用"的外部符号归因到具体函数
扫描 ghidra/decompiled/perfunc/*.c, 找出调用了 diff 符号的函数
"""
import json, os, re, glob

BASE = os.path.dirname(os.path.abspath(__file__))
PERFUNC = os.path.join(BASE, "ghidra", "decompiled", "perfunc")

# 1. 读入 diff (原库引用/重建库无引用)
# 重新计算
sym = json.load(open(os.path.join(BASE, "symbols.json"), encoding="utf-8"))
undef_orig = set(n for n, i in sym.items() if not i.get("defined"))

undef_recon = set()
for line in open(os.path.join(BASE, "recon_undef.txt"), encoding="utf-8"):
    p = line.split()
    if len(p) >= 2 and p[0] == "U":
        undef_recon.add(p[1])

only_orig = sorted(undef_orig - undef_recon)  # 85 个
print(f"归因目标: {len(only_orig)} 个差异外部符号")

# 2. 扫描 perfunc, 建立符号->调用它的函数 映射
# perfunc 文件名: 0x<addr>_<name>.c, 内容是反编译
# 在反编译 C 中, 调用形式可能是 demangled 名或 DAT_/PTR_ 引用
# 我们对每个 diff 符号, demangle 出可读名, 然后在 perfunc 里搜

def simple_demangle(m):
    # 只提取类::方法 可读片段用于文本搜索
    # _ZN<len><Cls>... -> Cls
    m2 = re.match(r"_Z(N?)K?(\d+)(\w+)", m)
    return m

# 建立 perfunc 内容索引 (一次性读入)
print("加载 perfunc 索引...")
func_files = {}
for fp in glob.glob(os.path.join(PERFUNC, "*.c")):
    fn = os.path.basename(fp)
    try:
        func_files[fn] = open(fp, encoding="utf-8", errors="replace").read()
    except Exception:
        pass
print(f"加载 {len(func_files)} 个函数文件")

# 对每个 diff 符号, 用其 demangled 可读名搜索
def readable_keys(mangled):
    """从 mangled 提取可能的文本搜索关键字"""
    keys = set()
    # 类名
    m = re.match(r"_Z(N?)K?(\d+)([A-Za-z_]\w*)", mangled)
    if m:
        ln = int(m.group(2))
        cls = m.group(3)[:ln]
        keys.add(cls)
    # C 函数名 (无 _Z)
    if not mangled.startswith("_Z"):
        keys.add(mangled)
    # 特殊: operator new/delete
    return keys

# 结果: diff符号 -> 调用它的函数列表
attribution = {}
for sym_mangled in only_orig:
    keys = readable_keys(sym_mangled)
    callers = []
    if keys:
        for fn, content in func_files.items():
            for k in keys:
                if k in content:
                    callers.append(fn)
                    break
    attribution[sym_mangled] = callers

# 3. 输出报告
os.makedirs(os.path.join(BASE, "analysis"), exist_ok=True)
out = []
out.append("# 85 个外部引用差异归因报告\n")
out.append(f"归因目标: {len(only_orig)} 个原库引用但重建库未引用的符号\n")
out.append("\n## 按类/模块分组\n")
# 按类分组
by_class = {}
for s in only_orig:
    m = re.match(r"_Z(N?)K?(\d+)([A-Za-z_]\w*)", s)
    if m:
        cls = m.group(3)[:int(m.group(2))]
    elif s.startswith("_Z"):
        cls = "(operator/other)"
    else:
        cls = "(C函数)"
    by_class.setdefault(cls, []).append(s)

for cls in sorted(by_class):
    syms = by_class[cls]
    out.append(f"\n### {cls} ({len(syms)} 个)\n")
    for s in syms:
        callers = attribution.get(s, [])
        # 简化调用者名 (取文件名)
        caller_names = sorted(set(c.split("_", 1)[-1].replace(".c", "") for c in callers))[:5]
        out.append(f"- `{s}`")
        out.append(f"  - 调用者: {', '.join(caller_names) if caller_names else '(未在perfunc找到直接文本引用)'}")

report = "\n".join(out)
with open(os.path.join(BASE, "analysis", "diff_attribution.md"), "w", encoding="utf-8") as f:
    f.write(report)

# 4. 控制台摘要: 按类统计
print("\n=== 按类/模块分组统计 ===")
for cls in sorted(by_class, key=lambda c: -len(by_class[c])):
    print(f"  {cls}: {len(by_class[cls])}")

# 5. 关键: 哪些是本库应该实现但没实现的 vs 平台外部的
print("\n=== 判断: 本库方法 vs 平台外部 ===")
ezx_internal = [s for s in only_orig if re.match(r"_Z.*(AM_|Idle_|SETUP_|UTIL_|Dmt|DRM|RES_|PHONE_|QPTF)", s)]
qt_platform = [s for s in only_orig if s.startswith("_Z") and re.match(r"_Z.*Q[A-Z]", s)]
libc = [s for s in only_orig if not s.startswith("_Z")]
print(f"  EZX内部类(AM_/Idle_/SETUP_/UTIL_等): {len(ezx_internal)}")
print(f"  Qt平台类(Q*): {len(qt_platform)}")
print(f"  libc/其它(C函数): {len(libc)}")

print(f"\n报告已写入: analysis/diff_attribution.md")
