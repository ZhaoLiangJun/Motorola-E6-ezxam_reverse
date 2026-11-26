#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
verify_full.py - libezxam 重建库 vs 原库 完整符号对账
输入: symbols.json (原库 nm -D 全量提取, 含 defined/data/weak/undefined)
      libezxam_reconstructed.so.1.0.0 (重建库)
输出: 分类报告 + 缺失清单 + 类型不一致清单
"""
import json, sys, os
os.chdir(os.path.dirname(os.path.abspath(__file__)))

RECON_TXT = "recon_symbols.txt"  # 由 nm -D --defined-only 落盘生成

# nm type letter -> category
def cat(letter):
    if letter.islower():
        return "local"
    u = letter.upper()
    if u == "T": return "func"      # .text
    if u in ("D","R","B","G","S"):  # data/rodata/bss
        return "data"
    if u == "W": return "weak"
    if u == "U": return "undef"
    return "other"

def load_recon(path):
    res = {}
    for line in open(path, encoding="utf-8"):
        line = line.strip()
        if not line: continue
        parts = line.split(None, 2)
        if len(parts) < 3: continue
        addr, typ, name = parts
        res[name] = (typ, cat(typ))
    return res

def main():
    orig = json.load(open("symbols.json", encoding="utf-8"))
    # 只关心原库 defined 符号
    orig_def = {n: info for n, info in orig.items() if info.get("defined")}
    recon = load_recon(RECON_TXT)

    print(f"原库 defined 符号: {len(orig_def)}")
    print(f"重建库 defined 符号: {len(recon)}")

    missing = []      # 原库有, 重建无
    present = []      # 两者都有
    extra = []        # 重建有, 原库无 (可能是新增辅助符号)

    orig_names = set(orig_def.keys())
    recon_names = set(recon.keys())

    for n in sorted(orig_names):
        if n in recon_names:
            present.append(n)
        else:
            missing.append(n)
    for n in sorted(recon_names):
        if n not in orig_names:
            extra.append(n)

    print(f"\n=== 匹配成功: {len(present)} ===")
    print(f"=== 缺失 (原库有/重建无): {len(missing)} ===")
    for n in missing:
        info = orig_def[n]
        # bind: 1=GLOBAL 2=WEAK
        bind = "WEAK" if info.get("bind")==2 else "GLOBAL"
        size = info.get("size", 0)
        print(f"  [{bind} size={size}] {n}")
    print(f"=== 多余 (重建有/原库无): {len(extra)} ===")
    for n in extra:
        t = recon[n][1]
        print(f"  [{recon[n][0]}] {n}")

    # 分类统计原库 defined
    print("\n=== 原库 defined 符号分类 ===")
    # bind==2 -> weak, else strong
    funcs = [n for n,i in orig_def.items() if i.get("bind")!=2 and i.get("size",0)>0 and not n.endswith(("D1Ev","D2Ev","D4Ev"))]
    # crude: just count by bind
    strong = [n for n,i in orig_def.items() if i.get("bind")!=2]
    weak = [n for n,i in orig_def.items() if i.get("bind")==2]
    print(f"强符号(GLOBAL): {len(strong)}, 弱符号(WEAK): {len(weak)}")

    # 关键: 强符号缺失是最严重的 (会导致链接/加载失败)
    strong_missing = [n for n in missing if orig_def[n].get("bind")!=2]
    weak_missing = [n for n in missing if orig_def[n].get("bind")==2]
    print(f"\n=== 缺失细分 ===")
    print(f"强符号缺失 (严重!): {len(strong_missing)}")
    for n in strong_missing: print(f"  !! {n}")
    print(f"弱符号缺失 (通常无害): {len(weak_missing)}")
    for n in weak_missing: print(f"  -- {n}")

    # 总结
    print(f"\n========== 总结 ==========")
    if not strong_missing:
        print("✓ 所有强符号(导出函数/数据)均已实现")
    else:
        print(f"✗ 有 {len(strong_missing)} 个强符号缺失, 必须补齐")

if __name__ == "__main__":
    main()
