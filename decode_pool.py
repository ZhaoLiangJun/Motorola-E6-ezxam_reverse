#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
decode_pool.py v2 - 字面池解码（全程 Ghidra 空间，仅在 cstr 时转 ELF）
ARM pics: iVar10 = *(DAT_pool) + PC_addend   ← 全在 Ghidra 地址空间
          key_str_addr = iVar10 + *(DAT_off)  ← 仍是 Ghidra 地址
校验：键名解码出可读 ASCII（如 Type/Exec）即偏移正确。
"""
import struct

ORIG = r"D:\cygwin\home\ezx\qt-2.3.8\lib\libezxam.so.1.0.0"
data = open(ORIG, "rb").read()

# Ghidra -> ELF vaddr。用 _init 交叉：DT_INIT ELF=0x41b38694，Ghidra _init=0x41ae863c
GH2ELF = 0x50058
def E(g):  # ghidra addr -> ELF vaddr
    return g + GH2ELF

# PT_LOAD: seg0 off=0x0 vaddr=0x41b28000 filesz=0x42a31
SEG0_VA, SEG0_OFF, SEG0_SZ = 0x41b28000, 0x0, 0x42a31
def g2f(g):
    v = E(g)
    fo = v - SEG0_VA + SEG0_OFF
    if 0 <= fo < len(data):
        return fo
    return None

def rd32g(g):
    f = g2f(g)
    if f is None: return None
    return struct.unpack_from("<I", data, f)[0]

def cstrg(g):
    f = g2f(g)
    if f is None: return None
    end = data.find(b"\x00", f)
    if end < 0 or end - f > 64: return None
    s = data[f:end]
    # 可读 ASCII 校验
    if not all(32 <= c < 127 for c in s):
        return None
    return s.decode("ascii")

def decode_func(name, pool_addr, pc_addend, dat_list):
    print(f"=== {name} ===")
    pool_val = rd32g(pool_addr)
    if pool_val is None:
        print("  pool 读取失败"); return
    base = (pool_val + pc_addend) & 0xFFFFFFFF   # Ghidra 空间
    print(f"  pool@{pool_addr:#x}={pool_val:#x} + PC{pc_addend:#x} = base={base:#x}")
    for label, dat in dat_list:
        off = rd32g(dat)
        if off is None:
            print(f"  {label:24} dat@{dat:#x} 读取失败"); continue
        s = cstrg(base + off)
        mark = "OK " if s else "?? "
        print(f"  {mark}{label:24} dat@{dat:#x} off={off:#x} -> {s!r}")
    print()

# createObject: iVar10 = DAT_41af4bf0 + 0x41af4620
decode_func("createObject", 0x41af4bf0, 0x41af4620, [
    ("0x0c (ToInt Type)",   0x41af4bf4),
    ("0x10 BigIcon",        0x41af4bfc),
    ("0x18 AniIcon",        0x41af4c04),
    ("0x14 Icon",           0x41af4c08),
    ("0x20 AppID(UUID)",    0x41af4c0c),
    ("0x30 (ToInt bool1)",  0x41af4c10),
    ("0x31 (ToInt bool2)",  0x41af4c14),
    ("0x34 Directory",      0x41af4c18),
    ("0x54 cmdLine(fail)",  0x41af4c1c),
    ("0x38 Name(fail)",     0x41af4c20),
])

# getInvisiblNetLinkFromDmt: iVar6 = DAT_41af53dc + 0x41af4c40
decode_func("getInvisiblNetLinkFromDmt", 0x41af53dc, 0x41af4c40, [
    ("DMString path",    0x41af53e0),
    ("principal (uuid)", 0x41af53e4),
    ("node 'url'",       0x41af53e8),
    ("node 'name'",      0x41af53ec),
    ("default (url?)",   0x41af53f0),
])
