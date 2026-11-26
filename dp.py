#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""dp.py - 解码 Ghidra 反编译里 `DAT_xxx + funcbase` 形式的字面池引用 -> GOT 槽 -> 字符串"""
import sys
from litpool import LitPool

GH = 0x50058
lp = LitPool()


def w_g(g):
    return lp.word_at_ghidra(g)


def to_g(v):
    """可能是 ELF vaddr，也可能是 ghidra vaddr，统一到 ghidra"""
    if v >= 0x41b28000:
        return v - GH
    return v


def repr_word(g):
    v = w_g(g)
    if v is None:
        return f"{g:#x}: <oob>"
    cands = []
    for gg in (v, to_g(v)):
        s = lp.str_at(gg)
        if s:
            cands.append((gg, s))
    if cands:
        return f"{g:#x}: {v:#010x} -> {cands[0][1]!r}"
    return f"{g:#x}: {v:#010x}"


def got(base_gh, pool_word_gh):
    """return got base (ghidra space)"""
    v = w_g(pool_word_gh)
    if v is None:
        return None
    return v + base_gh


FUNCS = {
    "getAllAppList@0x41af6f6c": (0x41af6f88, 0x41af71f8, dict(
        r0=(0x41af7200, "DRM1 uuid[0:4]"),
        r1=(0x41af7204, "DRM2 uuid[0:4]"),
        r2=(0x41af7208, "license path"),
        r3=(0x41af720c, "fopen mode"),
        r4=(0x41af71fc, "QGList vfn"),
        r5=(0x41af7210, "?"),
        r6=(0x41af7214, "?"),
    )),
    "changeOrder@0x41afd464": (0x41afd48c, 0x41afd834, dict(
        r0=(0x41afd838, "shared null / items key?"),
        r1=(0x41afd83c, "QList<QString> copy fn"),
        r2=(0x41afd840, "Items key"),
    )),
    "removeAppFromMainMenu@0x41b05674": (0x41b056a8, 0x41b06494, dict(
        r0=(0x41b06498, "shared null"),
        r1=(0x41b0649c, "key A (Items?)"),
        r2=(0x41b064a0, "key B"),
        r3=(0x41b064a4, "key C"),
        r4=(0x41b064a8, "key D"),
        r5=(0x41b064ac, "QList copy fn"),
        r6=(0x41b064b0, "default config name"),
    )),
    "slotAppUnInstalled@0x41b07e2c": (0x41b07e54, 0x41b08334, dict(
        r0=(0x41b08338, "root folder uuid?"),
        r1=(0x41b0833c, "shared null"),
        r2=(0x41b08340, "find fn"),
        r3=(0x41b08344, "?fn"),
    )),
}

for name, (base, poolword, dats) in FUNCS.items():
    g = got(base, poolword)
    print(f"== {name}  GOTbase(gh)={g and hex(g)}")
    if g is None:
        continue
    for k, (dat, desc) in dats.items():
        off = w_g(dat)
        if off is None:
            print(f"   {k} {dat:#x} <oob>")
            continue
        slot = g + off
        print(f"   {k} off={off:#08x} slot={slot:#x} {desc}: {repr_word(slot)}")
    print()
