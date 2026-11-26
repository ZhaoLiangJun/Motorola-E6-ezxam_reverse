#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
litpool.py - libezxam.so.1.0.0 ARM 字面池解码器（已验证可工作）
作者：赵良军

用法：
  from litpool import LitPool
  lp = LitPool()
  lp.str_at(ghidra_addr)   # 读 Ghidra 空间地址处的 C 字符串
  lp.pool_strings(iVar9_ghidra, dat_list)  # 解码 [DAT_addr, ...] -> 字符串列表

核心：Ghidra 地址 -> ELF 文件偏移 = ghidra + GH - SEG0_VADDR
      其中 GH = 0x50058（由 _init 交叉验证：ELF DT_INIT 0x41b38694 - Ghidra _init 0x41ae863c）
"""
import struct
import os

ORIG_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                         "..", "..", "..", "cygwin", "home", "ezx", "qt-2.3.8",
                         "lib", "libezxam.so.1.0.0")
ORIG_ABS = r"D:\cygwin\home\ezx\qt-2.3.8\lib\libezxam.so.1.0.0"

# Ghidra -> ELF 偏移（_init 交叉验证）
GH = 0x50058
# ELF Program Header: seg0 (R-X, 含 .text/.rodata) fileoff=0 vaddr=0x41b28000
#                     seg1 (RW, .data.rel.ro/GOT) fileoff=0x43000 vaddr=0x41b73000
SEGS = [(0x41b28000, 0x0, 0x42a31), (0x41b73000, 0x43000, 0x1b94)]


class LitPool:
    def __init__(self, path=ORIG_ABS):
        with open(path, "rb") as f:
            self.data = f.read()

    def _e2f(self, vaddr):
        """ELF vaddr -> file offset"""
        for va, fo, sz in SEGS:
            if va <= vaddr < va + sz:
                return fo + (vaddr - va)
        return None

    def _gfile(self, ghidra):
        """Ghidra addr -> file offset"""
        return (ghidra + GH) - 0x41b28000

    def word_at_ghidra(self, g):
        """读 Ghidra 地址处的 32 位字"""
        f = self._gfile(g)
        if f is None or f < 0 or f + 4 > len(self.data):
            return None
        return struct.unpack_from("<I", self.data, f)[0]

    def str_at(self, g):
        """读 Ghidra 地址处的 C 字符串（ASCII 可打印）"""
        f = self._gfile(g)
        if f is None or f < 0 or f >= len(self.data):
            return None
        e = self.data.find(b"\x00", f)
        if e < 0 or e - f > 120:
            return None
        s = self.data[f:e]
        if not s or not all(32 <= c < 127 for c in s):
            return None
        return s.decode()

    def str_at_elf(self, vaddr):
        """读 ELF vaddr 处的 C 字符串"""
        f = self._e2f(vaddr)
        if f is None:
            return None
        e = self.data.find(b"\x00", f)
        if e < 0 or e - f > 120:
            return None
        s = self.data[f:e]
        if not s or not all(32 <= c < 127 for c in s):
            return None
        return s.decode()

    def pool_strings_elf(self, iVar9_elf, dat_ghidra_addrs):
        """解码 [iVar9_elf + *DAT] 双重解引用 -> 字符串。
        DAT_addr（Ghidra）处存的值是相对 iVar9(ELF) 的偏移；
        iVar9_elf+offset 处存 char*（ELF vaddr），解引用得字符串。
        适用于 DAT 值已确认是小偏移（<0x50000）的字面池。"""
        results = []
        for dat in dat_ghidra_addrs:
            off = self.word_at_ghidra(dat)
            if off is None or off >= 0x50000:
                results.append((dat, None, None))
                continue
            ptr_slot = iVar9_elf + off          # ELF addr holding char*
            char_star = self._read_elf_word(ptr_slot)
            s = self.str_at_elf(char_star) if char_star else None
            results.append((dat, char_star, s))
        return results

    def _read_elf_word(self, vaddr):
        f = self._e2f(vaddr)
        if f is None or f + 4 > len(self.data):
            return None
        return struct.unpack_from("<I", self.data, f)[0]


if __name__ == "__main__":
    lp = LitPool()
    # 验证：getName 第一个 UUID 分支键名（iVar9 在 ELF 空间 = 0x41b739f0）
    iVar9_elf = 0x41b739f0
    dats = list(range(0x41aebcec, 0x41aebd14, 4))
    print("getName 第一个 UUID 分支字面池键名：")
    for dat, ptr, s in lp.pool_strings_elf(iVar9_elf, dats):
        print(f"  DAT_{dat:#x} -> {s!r}")
