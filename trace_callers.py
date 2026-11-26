#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
trace_callers.py - 精确归因：每个差异符号被哪些【本库导出函数】调用
输出：仅保留 caller 是本库 defined 函数的差异（= 真实函数体偏离候选）
"""
import json, re, os, glob
BASE=os.path.dirname(os.path.abspath(__file__))
PERFUNC=os.path.join(BASE,"ghidra","decompiled","perfunc")

sym=json.load(open(os.path.join(BASE,"symbols.json"),encoding="utf-8"))
undef_orig=set(n for n,i in sym.items() if not i.get("defined"))
undef_recon=set()
for line in open(os.path.join(BASE,"recon_undef.txt"),encoding="utf-8"):
    p=line.split()
    if len(p)>=2 and p[0]=="U": undef_recon.add(p[1])
only_orig=sorted(undef_orig-undef_recon)

# 本库 defined 函数地址集合 (ghidra) -> 名字
addr_map=json.load(open(os.path.join(BASE,"func_addr_map.json"),encoding="utf-8"))
defined_funcs={}
for a,info in addr_map.items():
    if info.get("defined"):
        defined_funcs[a]=info.get("demangled") or info.get("mangled")

# 读所有 perfunc，建立 demangled调用名 -> [caller_addr]
# perfunc 文件内容里调用形如 Class::method( 或 C函数名(
caller_index={}
for fp in glob.glob(os.path.join(PERFUNC,"*.c")):
    caller_addr=os.path.basename(fp).split("_")[0]  # 0x...
    try:
        txt=open(fp,encoding="utf-8",errors="replace").read()
    except: continue
    # 收集本文件出现的 Class::method 标记
    for m in set(re.findall(r'([A-Za-z_][\w:]*::[~\w]+)',txt)):
        caller_index.setdefault(m,set()).add(caller_addr)

def mangled_parts(m):
    """_ZN<len><Cls>...<len><method>... -> (Cls, method)"""
    out=re.match(r"_Z(N)?K?(\d+)([A-Za-z_]\w*)",m)
    if not out: return None,None
    n=int(out.group(2)); cls=out.group(3)[:n]
    rest=m[out.end():]
    mm=re.match(r"(\d+)([A-Za-z_]\w*)",rest)
    if mm:
        ln=int(mm.group(1)); meth=mm.group(2)[:ln]
        return cls,meth
    return cls,""

results=[]
for s in only_orig:
    cls_,meth_=mangled_parts(s)
    # 在原库 perfunc 中搜：哪些 caller 文本里出现 cls_::meth_ 或 meth_
    callers=set()
    if cls_ and meth_:
        key=f"{cls_}::{meth_}"
        callers=caller_index.get(key,set())
    elif not s.startswith("_Z"):
        callers=caller_index.get(s,set())
    # 只保留 caller 属于本库 defined 函数的
    inlib=[]
    for ca in callers:
        if ca in defined_funcs:
            inlib.append(defined_funcs[ca])
    results.append((s,cls_,meth_,sorted(set(inlib))))

# 输出：有本库 caller 的（真实偏离候选）
real=[r for r in results if r[3]]
print(f"=== 差异符号总 {len(results)}，其中被本库导出函数调用 {len(real)} ===\n")
for s,cls_,meth_,inlib in real:
    print(f"[{cls_}::{meth_}]")
    for f in inlib[:4]:
        print(f"    <- {f}")

# 保存
with open(os.path.join(BASE,"analysis","caller_map.txt"),"w",encoding="utf-8") as f:
    f.write(f"差异符号总 {len(results)}，被本库导出函数调用 {len(real)}\n\n")
    for s,cls_,meth_,inlib in real:
        f.write(f"[{s}]\n  callers: {', '.join(inlib)}\n")
print("\n已写 analysis/caller_map.txt")
