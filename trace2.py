#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
trace2.py - 直接法：对每个差异符号，grep perfunc 找调用它的函数文件，
再验证该 caller 是否为本库 defined（导出）函数。
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

# 本库 defined 函数的 ghidra 地址集合
addr_map=json.load(open(os.path.join(BASE,"func_addr_map.json"),encoding="utf-8"))
defined_addrs={}
for a,info in addr_map.items():
    if info.get("defined"):
        defined_addrs[a.upper()]=info.get("demangled") or info.get("mangled")

# 对每个差异符号提取搜索关键字（method 名或 C 函数名）
def keyword(m):
    if not m.startswith("_Z"):
        return m
    mm=re.match(r"_Z(N)?K?(\d+)([A-Za-z_]\w*)(\d+)([A-Za-z_]\w*)",m)
    if mm:
        return mm.group(5)[:int(mm.group(4))]  # method 名
    mm=re.match(r"_Z(N)?K?(\d+)([A-Za-z_]\w*)(\d+)",m)
    if mm:
        return mm.group(3)[:int(mm.group(2))]  # 只有类名
    return None

results=[]
for s in only_orig:
    kw=keyword(s)
    if not kw:
        continue
    # grep 所有 perfunc 找调用者文件
    callers=[]
    for fp in glob.glob(os.path.join(PERFUNC,"*.c")):
        base=os.path.basename(fp)
        addr="0x"+base.split("_")[0][2:].upper()  # 0x41aeac88
        addr_key=addr.upper()
        try:
            txt=open(fp,encoding="utf-8",errors="replace").read()
        except: continue
        if kw in txt:
            # 该文件调用了 kw；若 caller 是本库 defined 函数则记录
            if addr_key in defined_addrs:
                callers.append(defined_addrs[addr_key])
    # 去掉自身（若差异符号本身就是这个函数）
    results.append((s,kw,sorted(set(callers))))

real=[r for r in results if r[2]]
print(f"差异符号 {len(results)} 个，其中被本库导出函数调用：{len(real)} 个\n")
for s,kw,callers in real:
    print(f"{kw}  ({s[:50]}...)")
    for c in callers[:5]:
        print(f"    <- {c}")
    print()

with open(os.path.join(BASE,"analysis","caller_map.txt"),"w",encoding="utf-8") as f:
    f.write(f"差异符号 {len(results)}，被本库导出函数调用 {len(real)}\n\n")
    for s,kw,callers in real:
        f.write(f"{kw}: {', '.join(callers)}\n")
print("已写 analysis/caller_map.txt")
